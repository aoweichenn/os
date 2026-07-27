# ADR 0037：以软件 PTE 位、稀疏引用表和两阶段提交实现 fork/COW

## 状态

已接受并在 v1.10 实现。

## 背景

v1.7 已经建立 Process/Thread、磁盘 ELF、spawn/exec/wait 和父子进程树；
v1.8 把地址意图从 PTE 中拆出为 VMA，并让匿名页按需驻留；v1.9 又把文件
来源、FileBacking 和 clean page cache 放入同一个 fault 分发边界。此前创建
新进程只能重新装入一个程序，不能表达 Unix 的“先复制当前执行环境，再由父子
分别决定继续执行或 exec”。

直接复制整个地址空间虽然容易理解，但有三个问题：

1. fork 后立即 exec 会复制随后立刻丢弃的代码、数据、heap 和 stack；
2. 复制成本与父进程已经驻留的页数成正比，而不是与真正修改的页数成正比；
3. 失败发生在中途时，父子页表、物理页和进程资源很难形成可审计事务。

x86-64 没有“COW”硬件位。处理器只理解 PTE 的 Present、Read/Write、
User/Supervisor、Execute Disable 等权限，并在写只读页时产生 `#PF`。因此
COW 必须是操作系统赋予一个软件可用 PTE 位的语义，再由页故障处理器完成
私有化。

## 决策

### fork 的 Process/Thread 语义

`ForkProcess` 创建一个新 Process，并只为调用者创建一个子 Thread：

- 父 Thread 返回子 PID；
- 子 Thread 从同一条系统调用返回路径继续，`RAX` 为 0；
- 子 Thread 继承调用者通用寄存器、用户栈、信号 mask 预留字段和 512 字节
  FXSAVE 现场；
- 不复制同一 Process 的其他 Thread。当前 v1.10 还没有开放用户多线程，
  该规则提前冻结 v1.12 之后的语义。

子 Process 获得独立的 ProcessTree 节点、AddressSpace、FileTable 和
FsContext。FileTable 保持相同 fd 编号和 fd flags，但表项强引用同一个
FileDescription，所以文件 offset 与 file status flags 继续共享。
FsContext 值复制 cwd/root/mount 视图；父子后续 `chdir` 相互独立。

### COW 页的硬件编码

页表软件位 9 表示 `copy_on_write`。合法 COW leaf 必须满足：

```text
Present = 1
User = 1
Read/Write = 0
Software bit 9 = 1
```

`Read/Write=1` 与 `copy_on_write=1` 的组合被页表 API 明确拒绝。只读代码、
只读文件页和原本不可写的 VMA 保持普通只读，不标 COW；否则一个非法写会被
错误地解释成合法私有化。

修改父 PTE 或替换发生故障的 leaf 后使用 `INVLPG` 失效当前虚拟地址。子地址
空间尚未运行，不需要额外 shootdown。项目仍为单 BSP，不宣称已经实现 SMP
跨核 TLB shootdown。

### 稀疏物理页引用

普通独占用户页不登记引用表，隐含引用数为 1。页第一次参与 fork 时，
`UserPageReferenceManager::RetainForFork` 建立一项并把引用数设为 2；后续
fork 再递增。这样元数据只与“曾被共享且尚未结束共享”的页数相关，而不是与
全部受管物理内存或全部驻留页相关。

表容量为 32768 项，条目使用固定宽度字段并由 SpinLock 保护。统计同时记录：

- active entry/reference；
- peak entry/reference；
- first share、retain、release；
- 引用恢复为独占时删除元数据的次数。

启动与所有 Process 回收后，active entry 和 active reference 必须都为零。

### COW break 的两个分支

用户 present+write `#PF`、Kernel `CopyToUser`、用户可写范围校验和系统调用
结果写回都进入同一个 `BreakCopyOnWritePage` 路径：

1. 查 VMA，确认软件权限允许写；
2. 查 PTE，确认这是 present、user、COW、not-writable leaf；
3. 读取共享引用数；
4. 引用数为 1：删除稀疏元数据，原 frame 不复制，PTE 恢复 writable；
5. 引用数大于 1：申请新 frame，复制 4096 字节，原子替换 leaf，释放旧引用；
6. 更新 AddressSpace 与 ABI 统计。

第 4 条很重要：如果另一个 Process 已经退出，最后持有者再次写入时没有理由
复制自己唯一拥有的页。第 5 条必须先准备新 frame 和内容，再改变 PTE；申请
失败时旧映射仍然完整。

### 文件页的分类

fork 按当前 PTE 与 VMA 政策分类：

| 页面 | fork 后处理 |
| --- | --- |
| 匿名可写驻留页 | 父子共享 frame，双方 PTE 改 COW |
| writable `MAP_PRIVATE` 驻留页 | 父子共享 frame，双方 PTE 改 COW |
| ELF 可写 private 页 | 父子共享 frame，双方 PTE 改 COW |
| 只读完整 clean cache 页 | 继续只读共享，不加入 COW 引用表 |
| 只读私有尾页 | 继续只读共享，生命周期引用被复制 |
| 尚未驻留 VMA | 只复制 VMA/后备，未来各自 fault |

FileBacking 的 clone 增加 VFS 打开实例或内存镜像引用。clean cache 映射引用
独立增加；它与匿名/private COW 引用不是一张表，避免把“可从文件重读的 clean
缓存所有权”和“需要在最后一次引用时释放的私有 frame”混成一个状态机。

### 两阶段 fork 事务

fork 分成准备与提交两段：

```text
准备：
  预留 Process/Thread/内核栈/页表根
  clone VMA、FileBacking、cache 引用、FileTable、FsContext
  child 先安装共享 leaf

提交：
  对父 writable private leaf 逐页设置 COW
  登记共享引用
  发布 child 到 ProcessTree 和 run queue
```

父页权限修改被推迟到子候选地址空间已经完整建立以后。提交中途仍可能失败，
因此每个已经修改的父 leaf 都可由子映射反查并恢复。回滚先销毁子地址空间，
释放其共享引用；若某页只剩父引用，则删除稀疏元数据并恢复父 PTE writable。
随后按逆序销毁 FileTable、FsContext、Thread 和 ProcessTree 预留。

失败不能留下：

- 可被调度但资源不完整的子 Thread；
- ProcessTree 中不可回收的 Alive/Zombie；
- 父页无故保持只读 COW；
- 多余 frame、页表分支、VMA、FileBacking 或对象强引用。

### ABI 与观测

系统调用号 44 为 `ForkProcess`。`VirtualMemoryStatistics` 从 160 字节扩展
为 200 字节，追加 COW 驻留页、fault、真实复制、独占恢复和 fork clone
计数。ABI 只使用显式固定宽度类型。

高频 fault 不逐页刷串口；来宾只累计统计，阶段结束打印一次资源摘要。用户
探针只打印四个语义完成标记，失败使用具名失败标记。宿主为每条 QEMU 串口
行附加相对启动时间，来宾继续打印 PIT 单调时间。

## 被拒绝的方案

### fork 时 eager copy 全部驻留页

实现短，但无法教学 COW、会让 fork+exec 浪费内存，也把 64 MiB 兼容档压力
不必要地放大。

### 把物理页引用计数塞入全局 frame allocator

这会改变所有内核页、页表页和 cache 页的所有权模型，并要求按最高 PFN
永久扩展元数据。v1.10 只需要追踪共享 private 用户页，稀疏表边界更清晰。

### 只处理用户 `#PF`，让 CopyToUser 绕过

Kernel 通过 direct-map 写物理页不会触发目标用户 PTE 的 `#PF`，会直接改坏
父子共享内容。用户写和内核代写必须共用同一 COW break。

### 先修改父 PTE，再尝试建立子地址空间

失败窗口会让父进程永久降权，并迫使回滚猜测哪些页已经修改。候选子优先和
显式提交日志能缩小并审计该窗口。

### 让所有只读页都标 COW

会把真正的只读保护错误变成可写私有页，破坏 W^X 和文件只读语义。只有 VMA
本来允许写的 private leaf 才能成为 COW。

## 后果

正面后果：

- fork 初始成本主要是页表、VMA 和引用复制，不复制页内容；
- 用户写和 Kernel 代写语义一致；
- fork+exec 只复制真正被写过的页；
- 文件 offset、cwd 和后备生命周期具有明确继承边界；
- 引用模型能以纯宿主单元/随机测试验证，真实权限变化由 QEMU 验证。

代价与限制：

- 当前稀疏表有 32768 个同时共享 private 页的硬上限；
- 仍是四级 4 KiB 用户 leaf，不支持 huge-page COW；
- 单 BSP 不需要跨核 TLB shootdown；
- v1.10 没有用户 Thread，尚未触发多 Thread fork 的停世协调；
- 不支持共享匿名映射、writable `MAP_SHARED`、swap、overcommit 或
  `vfork`。

## 验证

- `UserPageReferenceManager` 单元测试覆盖初始化、首次共享、retain/release、
  独占恢复、容量与非法输入；
- 100000 步固定种子随机模型逐步比较每个 frame 引用与全局统计，最后排空；
- 页表集成测试证明父子共享、COW bit 编解码、单页替换和两边独立恢复；
- FileTable、VFS FsContext 与 FileBacking 单元测试覆盖继承语义；
- Ring 3 `/bin/fork_probe` 覆盖匿名/private/readonly、CopyToUser、cwd、
  共享 fd offset 和 32 次 fork/exec/wait；
- 64 MiB、256 MiB 与 64 GiB QEMU 使用同一语义工作负载，结束时 COW 活动
  引用和全部 Process 资源归零。

## 关联

- [v1.10 发布记录](../releases/v1.10.md)
- [v1.10 学习章](../learning/18-v1.10-fork-copy-on-write.md)
- [ADR 0035](0035-anonymous-vma-demand-paging-user-heap.md)
- [ADR 0036](0036-file-backed-vma-lazy-elf-clean-page-cache.md)
- [路线图](../roadmap.md)

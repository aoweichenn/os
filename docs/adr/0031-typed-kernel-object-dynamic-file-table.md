# ADR 0031：以类型化 KernelObject 和共享 FileDescription 建立动态描述符语义

- 状态：已接受
- 日期：2026-07-26

## 背景

v1.0 为每个进程建立了八槽统一描述符表。它第一次让控制台、管道、普通文件
和目录都能通过整数 fd 进入相同的系统调用，但槽本身仍直接保存资源种类，
文件偏移另放在 `ProcessRuntimeProcess` 的平行数组中。这种布局适合证明接口，
却不能继续承担 v2 的所有权语义：

- fd 数值同时被当作数组索引和资源身份，关闭后复用时没有对象代次；
- 复制 fd 无处表达“两个 fd 指向同一个打开实例”；
- 文件偏移与描述符槽绑定，无法让 duplicate 共享偏移、独立 open 隔离偏移；
- 管道端点由某一个槽直接关闭，无法等到最后一个引用消失；
- `close-on-exec` 属于某个 fd，而读写模式属于打开实例，旧结构无法分开；
- 八个槽是数据结构形状，不是可配置的运行时限额；
- 文件、管道和控制台的关闭逻辑散落在进程退出与系统调用路径中。

Unix 从早期实现开始就区分“进程看到的描述符编号”和“系统内核维护的打开
文件状态”。现代接口把前者称为 file descriptor，把后者称为 open file
description。`dup` 复制的是到后者的引用，因此共享偏移；再次 `open` 才会
建立另一个独立偏移。`FD_CLOEXEC` 只属于 fd，`O_APPEND` 等 file status
flags 则属于共享打开实例。这个区分不是命名细节，而是后续 `fork`、`exec`、
管道、VFS 与 TTY 都依赖的所有权边界。

v1.1 已提供可释放 KernelHeap 和不可复活强引用原语，v1.2 已让 Process
成为共享资源容器，v1.3 已稳定用户入口。v1.4 因而只迁移对象与 fd，不同时
引入 VFS、fork 或磁盘 exec。

## 决策

### 用 KernelObjectManager 统一强引用生命周期

动态对象由 `KernelObjectManager` 从 KernelHeap 申请。管理器私有存储头包含：

| 字段 | 语义 |
| --- | --- |
| type / variant | 稳定对象类别与模块内具体种类 |
| generation | 全局单调代次，零值永不表示活动对象 |
| strong reference count | 活动强引用数量，归零后不可复活 |
| state | Inactive、Active、Finalizing |
| finalizer / context | 最后引用释放时的模块回调 |
| operation lock | 对共享 payload 的短临界区保护 |
| active links | 管理器活动对象双向链 |

跨模块执行使用不可复制、可移动的 `KernelObjectReference`。它在析构或
`Reset` 时自动释放强引用。长期所有权容器使用私有 `KernelObjectHandle`；
只有管理器和 `FileTable` 能构造、取得或释放它。业务模块拿不到 payload
裸指针，也不能自行修改引用计数。

地址不足以识别对象。堆释放一个对象后可能把同一地址交给新对象，因此 handle
同时保存地址和 generation。只有地址仍处于 Active 状态且 generation 相同，
才能取得临时 reference。这把“陈旧 fd 访问后来对象”的失败从时间巧合变成
显式校验。

最后一个引用释放时按以下次序执行：

```text
manager lock
  → strong reference 1 -> 0
  → state Active -> Finalizing
  → 从活动链摘除并扣减当前所有权统计
unlock
  → 模块 finalizer 关闭底层资源
  → KernelHeap::TryRelease
lock
  → 记录销毁与 finalizer 结果
unlock
```

finalizer 不在对象管理器锁内运行，避免文件系统或管道锁与对象锁形成反向
依赖。进入 finalizer 前引用已经归零，不再存在合法并发 payload 操作。

### FileDescription 成为 fd 背后的共享对象

v1.4 的第一个 KernelObject 类型是 `FileDescription`。其 payload 保存：

- ConsoleInput、ConsoleOutput、ConsoleError、RegularFile、Directory、
  PipeReader、PipeWriter 七种具体类别；
- readable/writable file status flags；
- 普通文件或目录的 `FileSystemHandle`，包括当前偏移；
- 管道、控制台输入或设备输出的明确依赖；
- 对共享 payload 操作使用的对象级锁。

`duplicate` 只增加同一对象的强引用，所以普通文件偏移自然共享。独立 `open`
建立新对象并复制一个新的 `FileSystemHandle`，所以偏移互不影响。最后一个
普通文件或目录引用调用 `FileSystem::Close`；最后一个管道读端或写端引用才
关闭对应端点并触发等待者唤醒。

控制台输出通过注入的 `FileDescriptionDeviceWriteOperation` 接入，不让通用
对象模型直接依赖串口硬件。这样同一实现可在宿主测试中使用丢弃写设备，在
目标内核中使用 COM1。Console、Pipe 和 legacy File 最终都通过 `TryRead`、
`TryWrite`、`ReadCanProgress`、`WriteCanProgress` 与 finalizer 契约工作。

### FileTable 只拥有对象 handle 和 fd flags

每个 Process 内嵌一个 `FileTable`。表不保存业务 payload，只保存：

```text
FileTableEntry {
    KernelObjectHandle object;
    uint64_t descriptor_flags;
}
```

当前唯一 fd flag 是 close-on-exec。它和 `FileDescription` 中的 file status
flags 明确分离：复制描述符时可以为新 fd 单独设置 close-on-exec，不改变
源 fd，也不改变共享对象读写状态。

表以 64 项为一个分块，按 base fd 递增链接。未使用的范围不申请分块。当前
规格为：

| 配置 | soft/hard 初值 | 可建立的表形状 |
| --- | ---: | --- |
| bootstrap 64 MiB | 64 | 1 个分块 |
| functional 256 MiB | 256 | 4 个分块 |
| capacity 64 GiB | 4096 | 64 个分块 |

这些数字是运行时限额，不是编译期进程数组。所有配置使用同一个动态分块实现。
soft limit 可以在不超过 hard limit 的范围内下降或恢复；已经打开但位于新
soft limit 以上的 fd 保持有效，只有新安装被拒绝。分配算法从调用方给出的
minimum fd 开始，返回第一个空位，因此关闭后的最低编号会确定性复用。

### 分块申请使用两阶段提交

KernelHeap 申请不能在 FileTable 锁内进行。缺少分块时先在锁内确认目标，
释放锁后申请和清零一个候选分块，再重新取得锁：

```text
查找空 fd
  → 发现分块不存在
  → unlock
  → heap allocate candidate
  → lock + 重新验证
      ├─ 表已销毁：释放 candidate，返回 NotInitialized
      ├─ 其他路径已安装同分块：释放 candidate，记录 rollback
      └─ 条件仍成立：按 base fd 有序提交
```

安装对象时，只有分块存在、目标槽为空、soft limit 仍允许后，才把
`KernelObjectReference` 原子地转成 table handle。任何更早失败都保持传入
reference 活动，调用方或 RAII 析构可以完整回滚底层资源。

### lookup、duplicate、close 的所有权次序

- `Lookup` 在持表锁时从 handle 取得一个临时强引用，然后释放表锁；
- 业务操作持有临时引用并取得对象 operation lock；
- `Duplicate` 先 lookup，再把临时引用安装到最低可用新 fd；
- `Close` 先在表锁内移除 handle 和 fd flags，再在表锁外释放引用；
- `Destroy` 先把整条分块链从活动表摘除，再逐项释放对象与分块；
- `CloseOnExec` 只选择设置了 fd flag 的项目，未设置项目保持不变。

锁顺序固定为 `FileTable → KernelObjectManager`。最后引用的模块 finalizer
发生在两把锁之外。当前单 BSP 内核不可抢占，但 IRQ 可以进入；明确顺序仍是
后续多线程共享 FileTable 与 fork 的必要基础。

### 用户 ABI 增加描述符控制调用

系统调用编号 23..28 增加：

| 调用 | 结果 |
| --- | --- |
| DuplicateDescriptor | 从 minimum 起复制 fd，可为新 fd设置 close-on-exec |
| GetDescriptorFlags | 读取单个 fd flags |
| SetDescriptorFlags | 更新单个 fd flags |
| SetDescriptorSoftLimit | 调整当前进程 soft limit |
| GetDescriptorSoftLimit | 返回当前 soft limit |
| GetDescriptorHardLimit | 返回不可由用户越过的 hard limit |

fd 或内存容量耗尽返回 `-24`，对象层失败返回 `-25`。未知 flag、零 soft limit
和超过 hard limit 的请求返回既有无效参数错误。所有返回继续经过 v1.3 的
统一 `SYSCALL/INT 0x80` 分发和安全用户返回边界。

### 扩大固定 KernelHeap 后备以容纳当前动态对象

v1.1 的 64 KiB 堆足以验证分配器，但无法同时承载 functional/capacity
描述符分块、对象头和后续 VFS 适配。v1.4 把同一 RW/NX 高半区后备扩大到
512 KiB；allocator 算法、地址边界、权限和回收语义不变。这个调整仍不是
按需增长堆，按需页后备留给后续内存阶段。

## 验证

### 宿主测试

- FileTable 单元测试覆盖依赖、限额、精确安装、所有权转移、临时 lookup、
  duplicate、独立 fd flags、最低编号复用、代次变化、close-on-exec 和销毁；
- FileDescription 集成测试在真实 legacy 文件系统与 Pipe 上证明 duplicate
  共享偏移、独立 open 不共享、管道端点等待最后引用及全部 finalizer 守恒；
- capacity 集成测试实际安装 4096 个 fd、建立 64 个分块，验证耗尽失败
  不改变强引用，随后逐项关闭并释放全部分块；
- 固定种子 `0x46445441424C4531` 执行 100000 步 open、duplicate、close、
  soft-limit 变化，与独立逐 fd 模型比较，并周期性验证表、对象链和堆。

### QEMU 系统测试

PID4 在真实 Ring 3 路径：

1. 写入八字节 `/fdv14.bin`；
2. 打开读 fd；256 MiB/64 GiB 档从 minimum 64 起 duplicate，hard limit
   为 64 的兼容档从 minimum 8 起 duplicate；
3. 通过源 fd 和副本顺序读取三字节，证明偏移从 0 推进到 6；
4. 独立 open 再读取三字节，证明新偏移仍从 0 开始；
5. 检查源 fd 与副本的 close-on-exec flags 相互独立；
6. 把 soft limit 降至选定 minimum，要求同一下界的 duplicate 精确返回
   容量错误；
7. 恢复 hard limit，关闭 fd 4 后要求下一次 open 复用 fd 4；
8. 关闭全部动态 fd 并输出 `FILE_DESCRIPTION_MODEL_OK`。

四个进程退出后，内核要求：

- 活动对象、活动 FileDescription、活动强引用均为零；
- object creations 等于 destructions；
- finalizations 等于 destructions，failed finalizations 为零；
- FileTable chunk allocations 等于 releases；
- Process/Thread、页、KVA、堆和对象 ResourceSnapshot 零差异；
- runtime、smoke 和 process-resource 三个验证值都为 1。

## 后果

### 正面

- fd 重新成为局部整数名字，不再冒充内核对象身份；
- duplicate、共享偏移、独立 open、端点最后关闭和 close-on-exec 具有一致
  所有权解释；
- functional 256 与 capacity 4096 只改变限额，不改变代码路径；
- VFS 可以在 v1.5 把 FileDescription 的 legacy handle 替换为 Vnode 引用，
  不再修改用户 fd 语义；
- fork/exec 可以复用 FileTable 的强引用与 close-on-exec 边界。

### 代价与边界

- 当前分块使用有序单链，4096 项扫描满足阶段规格，但不是百万 fd 优化结构；
- FileDescription 目前只含 readable/writable，append/nonblocking 等状态位
  要随 Unix I/O 阶段扩展；
- CloseOnExec 已有对象语义与测试，但磁盘 exec 到 v1.7 才调用它；
- KernelObject 目前只有 FileDescription 一种类型，Vnode、CachePage 和
  BlockRequest 会按各自阶段接入；
- 512 KiB KernelHeap 仍是固定映射，不是按需增长的 slab/页分配系统；
- 当前单 BSP 模型不声明 SMP 安全，跨 CPU 原子引用和锁扩展仍需独立设计。

## 未采用方案

### 直接把固定数组扩大到 4096 项

会让每个 Process 无条件占用完整表空间，并继续把容量写进对象形状；也无法
解决共享偏移、最后引用和 fd/file-status flags 分离。

### duplicate 时复制 FileSystemHandle

这会产生独立偏移，语义等同再次 open，不是 duplicate。管道端点也会被错误
地重复关闭。

### 让 FileTable 保存 FileDescription 裸指针

关闭与并发 lookup 之间无法证明对象仍存活，堆地址复用还会形成 use-after-free。
私有 handle、generation 和临时强引用共同消除这个隐式时间假设。

### 在持 FileTable 锁时申请堆

实现更短，但扩大锁临界区并固定错误锁序；后续堆扩展或回收可能取得更多锁。
两阶段候选分块把申请失败和竞争回滚变成可测试状态。

### 在 v1.4 同时引入 VFS

会把 fd 所有权错误和路径/Vnode 缓存错误混在同一验收面。先冻结
KernelObject、FileDescription 与 FileTable，v1.5 才能用同一 fd 语义比较
memfs、legacy adapter 与 mount traversal。

## 关联

- [ADR 0015：统一描述符、交互式 Shell 与 idle](0015-unified-descriptors-interactive-shell-and-idle.md)
- [ADR 0019：v2 可执行基线](0019-v2-executable-program-baseline.md)
- [ADR 0020：可回收 KernelHeap](0020-reclaimable-kernel-heap.md)
- [ADR 0027：资源生命周期基础](0027-v1.1-resource-lifecycle-foundation.md)
- [v1.4 发布记录](../releases/v1.4.md)

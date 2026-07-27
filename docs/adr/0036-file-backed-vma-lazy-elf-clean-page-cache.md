# ADR 0036：文件后备 VMA、按需 ELF 与有界 clean page cache

## 状态

已接受并在 v1.9 实现。

## 背景

v1.8 已经把“地址区间允许怎样访问”放入 VMA，把“这一页现在是否驻留”放入
PTE。匿名页可以在首次访问时零填充分配，但 ELF 的所有 `PT_LOAD` 仍在
`spawn/exec` 时读取、分配和映射。这个过渡模型有三个问题：

1. 启动成本与 ELF 虚拟大小线性相关，即使进程只执行很少代码；
2. 两个进程装入同一个只读程序时各自保存一份相同物理页；
3. 普通文件不能参与统一的 VMA、页故障和解除映射生命周期。

直接加入完整 Linux page cache、dirty/writeback、可写共享映射和异步块 I/O
会同时引入文件页身份、并发装入、淘汰、脏页状态、回写错误与持久化排序，无法
在一个学习阶段中分辨错误来源。v1.9 因此只建立可丢弃的 clean 页模型。

## 决策一：文件身份由稳定对象代数构成

缓存键不是 fd，也不是 `OpenFile*` 地址。fd 是进程局部编号，关闭后可以复用；
对象地址也可能在池中复用。VFS 为普通文件暴露：

```text
FileIdentity = {
  superblock_identifier,
  superblock_generation,
  node_identifier,
  node_generation
}
```

页身份再追加 `page_index`。任一 generation 改变都会形成新身份，旧缓存不能
错误命中新对象。

`FileBackingManager` 使用固定容量槽保存两种来源：

- VFS 打开实例：增加底层打开文件引用，使 fd 关闭后映射仍然有效；
- 内存 ELF 镜像：只用于专用内嵌异常夹具，不让正常磁盘程序重新进入 Kernel。

每个后备具有 owner identifier 与 generation。VMA 保存描述符和 generation，
解除映射、exec 或退出时释放引用；最后引用才关闭 VFS 实例或归还槽位。

## 决策二：VMA 保存文件区间，不保存驻留页

v1.9 的 VMA kind 为：

- `ExecutableImage`
- `FilePrivate`
- `FileShared`
- `Anonymous`
- `ProgramBreak`
- `UserStack`

文件 VMA 追加后备描述符、后备 generation、文件起始偏移和有效数据长度。
split 后右半区同时平移文件偏移并缩短有效长度；merge 只有在身份、权限、
kind、文件偏移连续且数据边界兼容时才允许。这样 `munmap` 中段拆分不会让
虚拟页重新指向错误的文件字节。

VMA 建立只表示承诺。没有为完整 ELF 或文件映射提前创建叶 PTE，也不会提前
消耗数据 frame。实际 resident 页只由 fault 路径产生。

## 决策三：clean cache 固定容量并使用外部存储

`FilePageCache` 初始化时由调用方提供固定 `FilePageCacheEntry` 数组，不在
fault 热路径使用动态堆。每个条目保存：

```text
active
FilePageIdentity
PhysicalFrame
mapping_reference_count
access_generation
```

容量按 managed frame 数量缩放：

- 最小 256 页；
- 通常取 managed frames 的 1/16；
- 最大 4096 页。

这不是性能调优结论，而是可验证的资源上界。缓存满时只淘汰引用数为零的
clean LRU。没有候选时返回明确容量错误，不扫描后无界等待，也不偷走仍由 PTE
引用的 frame。

装入、查找和发布由同一自旋锁保护。当前单 BSP 不会产生两个 CPU 并行 fault，
但状态机已经保证同一 `(file,page)` 只有一个权威条目；后续 SMP 可以把磁盘
读取移出全局锁并增加 `Loading` 状态，而不改变公开身份。

## 决策四：共享与私有策略按页面内容决定

完整 4 KiB、只读且完全落在文件数据范围内的页面可以进入 clean cache。

- `MAP_SHARED + READ`：PTE 引用 cache frame；
- 只读 ELF 文本/rodata：PTE 引用 cache frame；
- `MAP_PRIVATE + WRITE`：分配私有 frame，读文件后允许进程修改；
- 文件尾部部分页：分配私有 frame，先整页清零，再读取有效字节；
- ELF `p_memsz > p_filesz` 的 BSS 尾部：同样零填充未覆盖字节。

文件尾部页不进入共享 cache，避免同一个文件页身份同时代表不同 VMA 的
“有效长度之后必须为零”视图。这个选择牺牲少量共享，换取清晰的 ELF 和
`mmap` 零填充契约。

v1.9 只接受可写 `MAP_PRIVATE` 和只读 `MAP_SHARED`。writable shared 与
`msync` 明确返回不支持；不能把可写共享悄悄降级为 private。

## 决策五：write/truncate 先撤销映射，再失效缓存

当前没有反向映射索引，因此文件修改沿固定进程槽扫描仍存活的地址空间。对每个
匹配身份：

1. 找到只读共享或可重装的文件 PTE；
2. 解除 PTE 并减少 cache mapping reference；
3. 保留 VMA，让下一次访问重新 fault；
4. 更新文件长度后失效零引用 cache entry。

已经销毁地址空间的 Zombie 仍可能在进程结果槽中等待父进程回收，但
`root_physical_address == 0`；扫描必须跳过它，不能把合法 Zombie 当作内存
损坏。可写 private 页属于进程，不因文件后续写入而改变。

失效日志只在累计值真实变化且达到 1、2、4、8……时输出。一次 4096 字节用户
写会由 ABI 包装拆成多个 256 字节系统调用，但没有新 cache 条目被失效时不会
重复打印相同计数。

## 决策六：ELF 校验和物理装入分离

ELF reader 仍执行完整两遍结构校验，拒绝：

- 非 x86-64 `ET_EXEC`；
- 越界或溢出的 header/segment；
- W+X 段；
- 重叠、未对齐或超出程序窗口的映射；
- 入口不落在可执行段。

通过校验后只建立候选 VMA、初始参数栈和后备引用。用户返回前解析入口所在
可执行页；其余文本、rodata、data 和 BSS 由真实 `#PF` 延迟装入。工具侧映射
上限由 `0x40000000..0x80000000` 程序窗口和 4 KiB 页计算为 262143 页，
不再保留历史 512 页限制。

## 失败语义与回滚

- 后备池耗尽：映射或 exec 在提交前失败；
- cache 满且全部被引用：fault 返回容量失败并终止当前用户进程；
- 读取短于要求：只允许文件尾/BSS 规则覆盖的部分，其余视为损坏；
- PTE 建立失败：释放刚取得的 cache 引用或私有 frame；
- unmap/exec/exit：逐页区分 cache frame 与私有 frame，按所有权释放；
- write/truncate 撤销失败：文件操作返回明确失败，不继续发布半失效状态。

## 被否决方案

### 每个 fault 都直接读取新 frame

实现简单，但两个进程无法共享只读 ELF，也无法学习缓存身份、引用和淘汰。

### 以 fd 作为缓存身份

fd 可关闭、复制和复用，不能跨进程稳定标识文件。

### 在 v1.9 同时实现 dirty/writeback

会把同步 ATA、页缓存、回写错误和文件系统事务混为一体。dirty 页留到异步块层
阶段。

### write 后只失效 cache，不撤销 PTE

已有 PTE 仍指向旧 frame，下一次访问不会 fault，形成永久陈旧映射。

### 文件尾页也直接共享

不同映射的有效长度和 ELF BSS 语义可能不同，会使一个 cache frame 表达多个
逻辑视图。

## 验收

- 页缓存单元测试覆盖唯一身份、引用、LRU、硬容量、busy invalidation；
- 100000 步固定种子模型逐步核对 entry、引用和统计；
- VMA 单元/随机测试覆盖文件 split、offset rebase、merge 与 generation；
- 文件后备单元测试覆盖 VFS 引用、内存来源、owner 与 slot reuse；
- 共享页生命周期集成测试证明两个地址空间引用同一 frame 并独立解除；
- Ring 3 验证 fd-close lifetime、3000 字节尾零、双 shared cache hit、
  write invalidation、private write/no writeback 和完整 unmap；
- 64 MiB、256 MiB、64 GiB 使用同一 workload；
- 64 GiB 仍完成完整容量对象创建/销毁和最终资源守恒。

## 后果

v1.9 之后，VMA、PTE、FileBacking 和 CachePage 各自只表达一类事实：

```text
VMA         地址意图与权限
PTE         当前硬件驻留
FileBacking 可重复读取的稳定来源
CachePage   可丢弃 clean 内容及共享引用
```

下一阶段 v1.10 可以在这个分层上增加匿名/私有页引用计数和 COW，而无需重写
文件身份或 ELF fault 路径。

## 关联

- [v1.9 发布记录](../releases/v1.9.md)
- [v1.9 学习章](../learning/17-v1.9-file-backed-vma-lazy-elf-page-cache.md)
- [ADR 0035](0035-anonymous-vma-demand-paging-user-heap.md)
- [系统架构](../architecture.md)

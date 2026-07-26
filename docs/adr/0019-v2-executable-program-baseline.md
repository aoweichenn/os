# ADR 0019：冻结可执行的 v2 演进基线

## 状态

已接受，取代 ADR 0018 中的版本顺序、执行模型和容量验收定义。

## 背景

ADR 0018 已经完成三项关键纠偏：把 Process 与 Thread 分开、把 VFS 与
rootfs v2 分开、把 VMA 放在 fork/COW 之前。再次按“实现时是否会被迫提前
引入下一阶段机制”逐项审查后，仍然发现以下问题：

1. v1.1 要求删除静态 PCB 与四进程路径，但新的 Process/Thread 直到 v1.2
   才建立，版本边界之间没有可运行的过渡状态；
2. Process/Thread、`SYSCALL/SYSRET`、CPU 本地状态和入口安全被放在同一
   版本，无法分别证明调度对象模型与架构入口；
3. “单核”只描述了 CPU 数量，没有定义内核是否可抢占、IRQ 能否阻塞、
   哪些位置允许调度以及锁的适用范围；
4. v1.7 同时承担匿名 VMA、文件页故障、页缓存、按需 ELF 和用户运行时，
   任何错误都可能跨越三个所有权层；
5. fork、exec、exit、信号和 futex 在多线程 Process 中的语义没有冻结，
   后续实现会在最危险的失败路径上临时作决定；
6. 时间、信号、TTY 与作业控制，以及异步块层、页缓存写回与 journal，
   分别被压进单个版本，无法形成小而完整的测试闭环；
7. 三种内存配置只有职责说明，没有功能容量矩阵和运行频率；
8. 当前 Kernel 禁用 x87/SSE 保存；若 Thread 模型不同时解决扩展现场，
   用户程序以后会再次改变上下文切换 ABI；
9. 网站位于独立仓库，只说“来自同一提交”无法建立可追溯发布关系。

本 ADR 不改变 v2.0 的方向，而是把方向改写为能够逐版本实现、独立验收且不
需要跨版本悬空重构的工程基线。

## 决策

### 目标系统保持不变

v2.0 是由自研 ROM 与 Stage 1 启动、在 QEMU TCG 的单个 x86-64 BSP 上
运行的多进程、多线程类 Unix 教学操作系统。正常启动从自研 rootfs 执行
`/sbin/init`，再由 PID1 启动外部 Shell 和普通 ELF64 工具。系统具备动态
资源管理、VFS、按需虚拟内存、fork/COW、动态 fd、用户线程、信号、TTY 和
可恢复持久化。

“能力对齐现代 Linux”只表示采用可扩展身份、对象所有权、失败原子性和模块
分层，不表示复制 Linux 的源码规模、全部 POSIX 接口、SMP 或设备广度。

### 冻结单 BSP 内核执行模型

v2.0 内核采用“中断可进入、内核不可抢占”的执行模型：

- 普通 Thread 在 Ring 0 执行时不会被调度器强制切走；
- 硬件 IRQ 可以打断可中断的内核代码，但 IRQ handler 永远不得阻塞、等待
  WaitQueue、访问可能按需分页的用户内存或取得 sleep mutex；
- 调度只发生在显式阻塞、显式让出、Thread 退出，以及即将返回用户态且观察
  到重调度请求的位置；
- IRQ 唤醒 Thread 时只改变状态并设置重调度请求，不在任意内核栈中切换；
- NMI 不进入通用锁、调度、日志和用户复制路径，只记录最小故障事实或 panic；
- BSP 也使用单元素 `CpuLocal`，保存当前 Thread、入口内核栈、IRQ/抢占
  深度、重调度标记和体系结构临时状态，为入口正确性服务，不宣称 SMP 完成。

锁被明确分为三类：

| 原语 | 适用位置 | 是否可睡眠 | 关键约束 |
| --- | --- | --- | --- |
| `SpinLock` | 只被 Thread 上下文访问的短提交区 | 否 | 持有期间禁止调度 |
| `IrqSaveSpinLock` | Thread 与本 CPU IRQ 共享的短状态 | 否 | 保存并恢复原 IF，IRQ 中不得递归取得 |
| `Mutex` | 只在 Thread 上下文使用的长临界区 | 是 | 基于 WaitQueue，睡眠前不得持有 spinlock |

所有阻塞设施统一建立在 `WaitQueue` 上。一次等待只能由一个
`WakeReason` 获胜，例如条件满足、超时、信号、对象关闭或映射取消；输掉
竞争的唤醒源只能观察已完成状态，不能再次把 Thread 入队或重复消费资源。

### Process、Thread 与用户现场

`Process` 是共享资源容器，`Thread` 是唯一调度实体：

```text
Process
  ├─ ProcessId / parent / children / process group
  ├─ AddressSpace
  ├─ FileTable / FsContext
  ├─ SignalDisposition
  └─ Thread list
       └─ Thread
            ├─ ThreadId / scheduler state / WakeReason
            ├─ general + x87/SSE2 context
            ├─ kernel stack / user stack
            ├─ TLS base
            └─ signal mask
```

v1.2 先用现有 `INT 0x80` 建立这一对象关系。v1.3 再建立 `CpuLocal`、
`SYSCALL/SYSRET` 与统一 `UserContext`。两个入口必须进入同一分发器并产生
相同可观察结果；`SYSRET` 只用于安全的 canonical 返回状态，其他状态通过
验证后的 `IRETQ` 返回。

v1.2 同时使用 `FXSAVE/FXRSTOR` 保存每 Thread 的 x87/SSE/SSE2 状态。
AVX、XSAVE 和用户可见 AVX 在 v2.0 保持禁用。QEMU CPU 型号和必需 CPUID
特性形成固定测试基线，启动时缺少所需特性必须明确拒绝。

### 多线程生命周期语义

以下行为在实现系统调用之前冻结：

| 操作 | v2.0 语义 |
| --- | --- |
| `fork` | 子 Process 只复制调用它的 Thread；其他父 Thread 不出现在子进程 |
| `exec` | 先在隔离的候选地址空间中完成读取与验证；成功后终止或汇合兄弟 Thread 并原子替换映像，失败时原 Process 与全部 Thread 保持可运行 |
| `ThreadExit` | 只退出当前 Thread；最后一个 Thread 退出时才触发 Process 退出 |
| `ProcessExit` | 终止整个 Thread 组，发布共享资源，并向父 Process 留下可 wait 的 Process Zombie |
| `wait` | 等待 Process 子对象，不暴露已回收的 Thread 内部对象 |
| 信号处置 | handler/ignore/default 属于 Process；阻塞 mask 与待交付选择属于 Thread |
| 阻塞系统调用 | 条件、超时、信号、关闭只允许一个 WakeReason 获胜；返回部分进度还是中断错误由调用契约固定 |

参数和环境合计至少支持 128 KiB。exec 必须把字符串分批暂存在可回收页中，
不能在任一内核栈上建立同等大小的连续缓冲区。

### 虚拟内存与页缓存边界

匿名虚拟内存和文件支持分两个版本建立：

- v1.8 只建立 VMA、匿名按需页、`brk`、匿名 `mmap`、`munmap`、guarded
  用户栈与用户堆；
- v1.9 再建立文件页故障、按需 ELF 与有界 clean page cache。

v2.0 支持匿名映射、`MAP_PRIVATE` 文件映射与只读 `MAP_SHARED`。可写
`MAP_SHARED`、`msync`、swap、内存 overcommit 和 OOM killer 延后到 v2.x。

clean page cache 使用有界 LRU。回收优先丢弃可重新读取的 clean 页；达到
硬限制时分配明确失败。truncate 与文件写入必须使相关缓存页失效并撤销对应
映射，不能让进程继续观察旧页。dirty/writeback 状态直到 v1.16 才引入。

`CopyToUser` 写入 COW 页面时必须走与用户写页故障相同的私有化逻辑，不能
绕过只读 PTE 直接修改父子共享页。

### VFS 与存储边界

v1.5 的 legacy-fs 适配器只提供读取、基础创建和现有 v1.0 Shell 回归所需
的最小子集。完整 rename、unlink、truncate 与目录修改语义在 rootfs v2
的 v1.6 完成。v2.0 核心不要求正/负 dentry cache；先把 Path、Mount、
Vnode 与失败语义做正确，再把缓存留给 v2.x。

异步存储分两步完成：

- v1.16 建立 IRQ14 完成的 block request、请求队列、等待与
  clean/dirty/writeback 页缓存；
- v1.17 建立 metadata-only、ordered mode journal，包含事务 credits、
  descriptor/commit、设备 flush、幂等 replay 和断电注入。

ordered mode 要求相关文件数据先到稳定介质，元数据提交记录才能持久化。
journal 不承诺完整数据日志，崩溃后文件内容可为旧数据或已完整写入的新数据，
但命名空间和分配元数据必须一致。

### 优化后的版本依赖

第二周期扩展为十八个开发版本和一个集成发布：

| 波次 | 版本 | 闭环 |
| --- | --- | --- |
| A：资源与执行 | v1.1 | buddy、内核堆/对象缓存、KVA、动态内核栈、页表回收；保留四进程通路 |
|  | v1.2 | Process/Thread、调度、WaitQueue/WakeReason、锁模型、FXSAVE |
|  | v1.3 | CpuLocal、`SYSCALL/SYSRET`、统一 UserContext 与安全返回 |
|  | v1.4 | 类型化 KernelObject、FileTable 与 FileDescription |
| B：命名与程序 | v1.5 | VFS、memfs 与 legacy-fs 基础适配 |
|  | v1.6 | rootfs v2、完整命名空间修改、mkfs/fsck |
|  | v1.7 | PID1、进程树、磁盘 exec/wait |
| C：VM 与组合 | v1.8 | 匿名 VMA、`brk`/`mmap`、guarded stack、用户堆 |
|  | v1.9 | 文件页故障、clean page cache、按需 ELF |
|  | v1.10 | fork/COW、引用与 `CopyToUser` 私有化 |
|  | v1.11 | Unix I/O、pipe/dup/继承、外部 Shell 与代表性工具 |
| D：并发与交互 | v1.12 | 用户 Thread、TLS、private futex 与运行时同步 |
|  | v1.13 | 单调时钟、deadline 与 timed wait |
|  | v1.14 | signal、进程组与系统调用中断语义 |
|  | v1.15 | TTY、session、前台进程组与作业控制 |
| E：持久与冻结 | v1.16 | ATA IRQ14、block request、dirty/writeback cache |
|  | v1.17 | metadata journal、ordered mode 与崩溃恢复 |
|  | v1.18 | ABI v2 冻结、加固、devfs/procfs 与发布溯源 |
| 发布 | v2.0 | 只集成已冻结机制，不增加主要能力 |

v1.1 不删除当前四个 PCB 和用户演示路径。它先提供替代旧资源所需的通用
基础；静态 Ring 0 栈已经在不改变四进程调度语义的前提下按
[ADR 0025](0025-kva-backed-dynamic-kernel-stacks.md) 原位迁移为动态栈。
v1.2 在新 Process/Thread 路径通过对等测试后，把栈所有权从 PCB 槽迁移到
Thread，并删除旧 PCB 执行路径。任何版本结束时主分支都必须能启动和通过回归。

### 数值配置矩阵

下表是功能验收下限，不是固定数组长度：

| 配置 | RAM | Process | Thread | 每 Process Thread | fd hard | Pipe |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| bootstrap | 64 MiB | 不规定；只运行启动兼容场景 | 不规定 | 不规定 | 不规定 | 不规定 |
| functional | 256 MiB | 64 | 128 | 32 | 256 | 128 |
| capacity | 64 GiB | 256 | 512 | 64 | 4096 | 1024 |

两种完整配置使用同一动态数据结构、64 位身份和失败语义。256 MiB 可以采用
更低的运行时 hard limit，但不能换回固定表实现。capacity 仍要求 1 GiB
稀疏磁盘、256 MiB rootfs、64 MiB 单文件、32 个独立 ELF 和 16 级流水线；
v1.11 只先交付约 12 个能代表 I/O 组合面的核心工具，剩余工具在 v1.18
收口但不得引入新内核机制。

### 测试运行频率

| 时机 | 必须运行 |
| --- | --- |
| 每次提交 | 受影响单元/集成/固定种子随机测试、64 MiB boot、256 MiB smoke |
| 每个小版本 | 全部宿主测试、产物审计、完整 256 MiB functional 套件 |
| nightly 与候选发布 | 完整 64 GiB capacity、长时间 soak、崩溃点矩阵 |
| v2.0 发布 | 三种配置、所有故障镜像、教材与网站构建、发布溯源检查 |

64 GiB 测试不能成为普通开发提交的唯一反馈路径，但也不能只在文档中存在。
随机和崩溃测试失败必须输出种子、迭代/断电点和最小可复现信息。

### 发布溯源

主项目、网站和托管版本拥有不同身份。每次公开发布必须记录：

- 主仓库 commit SHA；
- `web/` 独立仓库 commit SHA；
- 网站托管保存版本号；
- 教材 PDF 的哈希和真实代码统计；
- 使用的 QEMU 版本、冻结 CPU 型号与 CPUID 特性摘要。

只有这些证据指向同一次内容同步，网站才可以把版本标为已发布。

## 明确延后

以下能力不进入 v2.0：

- SMP、AP 启动、跨核调度、跨核 TLB shootdown；
- writable `MAP_SHARED`、`msync`、swap、overcommit、OOM killer；
- 正/负 dentry cache、通用权限模型、完整 POSIX；
- 数据 journal、在线文件系统扩容、快照；
- 网络、图形、音频、USB、AHCI、NVMe、通用 PCI；
- AVX/XSAVE、动态链接器、共享库和自举编译器。

## 后果

正面影响：

- 每个版本都能以可运行主分支结束，v1.1 不再制造对象模型真空；
- 调度语义、入口语义和用户 VM 分开验证，故障定位范围更小；
- Thread、signal、futex 和 exec 在编码前已有一致的生命周期定义；
- clean cache、writeback 与 journal 依次建立，断电模型可以被独立证明；
- 三种内存配置与执行频率不再依赖口头约定；
- 独立网站仍可建立精确发布溯源。

代价：

- 第二周期从十三个开发版本扩展为十八个，版本管理与文档同步次数增加；
- v1.1 会短期保留旧四进程路径，v1.2 才删除，存在一段受控迁移成本；
- writable shared mapping、dentry cache 和更高级内存策略需要留到 v2.x；
- 为上下文完整性必须提前实现 FXSAVE，即使早期用户程序很少使用浮点。

这些代价都换取了更小的单次风险、可解释的测试边界和不依赖运气的失败恢复，
符合本项目“学习机制而不是堆叠功能”的目标。

## 参考资料

- [AMD64 Architecture Programmer's Manual, Volume 2](https://docs.amd.com/v/u/en-US/24593_3.44_APM_Vol2)
- [Linux Virtual File System](https://docs.kernel.org/filesystems/vfs.html)
- [Linux file-system locking](https://docs.kernel.org/filesystems/locking.html)
- [Linux process address spaces](https://docs.kernel.org/mm/process_addrs.html)
- [futex(2)](https://man7.org/linux/man-pages/man2/futex.2.html)
- [signal(7)](https://man7.org/linux/man-pages/man7/signal.7.html)
- [fork(2)](https://man7.org/linux/man-pages/man2/fork.2.html)
- [execve(2)](https://man7.org/linux/man-pages/man2/execve.2.html)
- [Linux journalling API](https://docs.kernel.org/filesystems/journalling.html)

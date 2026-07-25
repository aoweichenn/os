# 开发路线

## 文档定位

本文是项目版本顺序、范围和验收标准的唯一当前来源。架构选择由 ADR 解释，
模块细节由 `docs/modules/` 维护，已发布证据保存在 `docs/releases/`。若本文
与旧路线冲突，以 [ADR 0019](adr/0019-v2-executable-program-baseline.md)
和本文为准。

每个版本必须形成一条独立、可复现的学习闭环：

```text
背景与历史约束
  → 状态机、所有权与失败语义
  → 最小可运行实现
  → 单元 / 集成 / 固定种子随机 / 产物 / QEMU 证据
  → 资源守恒与失败回滚
  → 文档、教材和网站同步
```

版本号不是进度百分比。全部退出条件通过、证据可复现且主分支保持可启动后，
版本才允许完成。不得把“下一阶段会修复”作为当前阶段的验收结果。

## 第一周期：从复位到交互式用户态

| 版本 | 能力闭环 | 状态 |
| --- | --- | --- |
| v0.0 | CMake/Ninja、Python 自动化、测试和文档工程基线 | 完成 |
| v0.1 | 自研 ROM 从 x86 复位向量启动并输出串口日志 | 完成 |
| v0.2 | 自研固件通过 ATA PIO 加载自研 Stage 1 | 完成 |
| v0.3 | 自行完成实模式、保护模式和 IA-32e Long Mode 切换 | 完成 |
| v0.4 | 严格验证并装载 ELF64 Kernel，交接 BootInfo | 完成 |
| v0.5 | Kernel 接管 GDT、IDT、TSS、异常和 panic | 完成 |
| v0.6 | 建立页帧分配、内核页表、权限和早期堆 | 完成 |
| v0.7 | 驱动 PIC、LAPIC virtual-wire、PIT、PS/2 和 ATA | 完成 |
| v0.8 | 进入 Ring 3，装载用户 ELF 并提供系统调用 | 完成 |
| v0.9 | 独立地址空间、抢占调度和进程生命周期 | 完成 |
| v0.10 | 阻塞/唤醒、同步和有界管道 IPC | 完成 |
| v0.11 | 自研 inode 文件系统、缓存、写回和持久化 | 完成 |
| v1.0 | 统一描述符、控制台输入、idle 和交互式 Shell | 完成 |

第一周期的实现细节见模块文档、发布记录和教材。v1.0 的 73 项自动化测试是
第二周期必须持续保持的回归基线。

## 第二周期最终目标

### v2.0 的定义

v2.0 是由自研 ROM 和 Stage 1 启动、运行在一个 x86-64 BSP 上的多进程、
多线程类 Unix 教学操作系统。正常 Kernel 只创建初始 Process/Thread，从
自研 rootfs 执行 `/sbin/init`；PID1 再启动外部 Shell 和普通 ELF64 工具。
系统具备动态资源管理、VFS、按需虚拟内存、fork/COW、动态描述符、用户线程、
信号、TTY 和可恢复持久化。

“能力对齐现代 Linux”表示采用长期可扩展的身份、所有权、边界和失败语义，
不表示复制 Linux 的源码规模、全部 POSIX 接口、SMP 或硬件覆盖面。

```text
Process ──共享──> AddressSpace / FileTable / FsContext / SignalDisposition
   │
   └──拥有多个──> Thread ──拥有──> TID / CPU Context / Kernel Stack /
                                  User Stack / TLS / Signal Mask

FileTable[fd] ──引用──> FileDescription ──引用──> Vnode
AddressSpace ──拥有──> VMA ──描述意图──> anonymous / file / stack
PTE ──描述驻留事实──> PhysicalPage / permissions / COW
Vnode ──定位──> CachePage ──提交──> BlockRequest ──落盘──> ATA
```

### 内核执行契约

v2.0 是中断可进入、内核不可抢占的单 BSP 内核：

- IRQ 可以打断可中断的 Ring 0 代码，但 IRQ handler 永不阻塞、睡眠或访问
  可能触发页故障的用户内存；
- 调度只发生在显式阻塞、让出、Thread 退出和返回用户态前；
- IRQ 唤醒只提交状态并设置重调度请求，不在任意内核调用链中换栈；
- `SpinLock` 保护 Thread 上下文短提交区，`IrqSaveSpinLock` 保护与本 CPU
  IRQ 共享的状态，`Mutex` 只用于可睡眠 Thread 上下文；
- 所有等待统一使用 `WaitQueue`，条件、超时、信号、关闭和取消只能有一个
  `WakeReason` 获胜；
- 单元素 `CpuLocal` 保存当前 Thread、入口栈、IRQ/抢占深度和重调度标记。

单 BSP 并不豁免锁顺序、引用计数和中断重入正确性，也不宣称已经具备 SMP
安全性。

### 三种正式配置与容量下限

数字是功能验收下限和运行时限制，不是固定数组形状。

| 配置 | RAM | Process | Thread | 每 Process Thread | fd hard | Pipe | 职责 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| bootstrap | 64 MiB | 不规定 | 不规定 | 不规定 | 不规定 | 不规定 | 启动链、异常、基础内存和历史故障镜像 |
| functional | 256 MiB | 64 | 128 | 32 | 256 | 128 | PID1、Shell、VM、线程、信号、TTY、持久化 |
| capacity | 64 GiB | 256 | 512 | 64 | 4096 | 1024 | 全 RAM、高地址、容量、长时间压力 |

64 GiB 是主容量规格而不是实现上限。实际受管上界取 E820、CPUID 物理地址
宽度、64 TiB direct-map 与运行时限制的交集。两种完整配置必须使用相同动态
结构、64 位身份和失败语义。

capacity 还必须支持：

- 1024 条按需获得缓冲页的 64 KiB 管道；
- 4096 字节路径和 255 字节单组件；
- `argv` 与 `envp` 合计至少 128 KiB；
- 1 GiB 稀疏磁盘、256 MiB rootfs、64 MiB 单文件；
- 32 个独立 ELF64 工具和 16 级流水线。

到达 soft/hard limit 必须返回明确错误，并逆序回滚本次获得的页、引用、fd
和磁盘资源。禁止覆盖旧槽、静默截断或部分提交。

### 测试执行频率

| 时机 | 必须完成的证据 |
| --- | --- |
| 每次提交 | 受影响单元/集成/固定种子随机测试、64 MiB boot、256 MiB smoke |
| 每个小版本 | 全部宿主测试、产物审计、完整 256 MiB functional |
| nightly / 候选发布 | 完整 64 GiB capacity、soak、故障与崩溃点矩阵 |
| v2.0 发布 | 三种配置、全部故障镜像、教材/网站构建、发布溯源 |

随机失败必须报告种子和迭代位置；崩溃恢复失败必须报告断电点和镜像哈希。

## 依赖波次

| 波次 | 版本 | 稳定边界 |
| --- | --- | --- |
| A：资源与执行 | v1.1–v1.4 | 可回收资源、Thread 调度、架构入口、动态对象/fd |
| B：命名与程序 | v1.5–v1.7 | VFS、rootfs v2、PID1、磁盘 exec/wait |
| C：VM 与组合 | v1.8–v1.11 | 匿名/文件 VM、COW、Unix I/O 与外部 Shell |
| D：并发与交互 | v1.12–v1.15 | 用户线程、时间、信号、TTY 与作业控制 |
| E：持久与冻结 | v1.16–v1.18 | 异步存储、journal、ABI 冻结和发布证据 |
| 发布 | v2.0 | 只集成已冻结机制，不增加主要能力 |

```text
v1.1 resource foundation
  → v1.2 Process/Thread + WaitQueue + FXSAVE
  → v1.3 CpuLocal + SYSCALL/SYSRET
  → v1.4 KernelObject + dynamic fd
  → v1.5 VFS + memfs + legacy adapter
  → v1.6 rootfs v2
  → v1.7 PID1 + disk exec/wait
  → v1.8 anonymous VMA
  → v1.9 file faults + clean page cache
  → v1.10 fork/COW
  → v1.11 Unix I/O + external shell
  → v1.12 threads/TLS/private futex
  → v1.13 clocks/deadlines
  → v1.14 signals/process groups
  → v1.15 TTY/job control
  → v1.16 async block + writeback
  → v1.17 metadata journal
  → v1.18 ABI freeze/hardening/provenance
  → v2.0 integration release
```

关键顺序不可颠倒：

- v1.1 保留当前四进程路径，v1.2 的新对象模型对等通过后才删除；
- 先分离 Process/Thread，再建立依赖 Thread 入口栈的原生 syscall；
- 先在 memfs 与旧格式上验证 VFS，再改变磁盘格式；
- 先建立匿名 VMA 和统一页故障，再加入文件页和 COW；
- 先稳定 clean page cache，再加入 dirty/writeback，最后加入 journal；
- 信号先于 TTY，TTY 才能把 Ctrl-C 定向交付给前台进程组。

## 波次 A：资源与执行

### v1.1 可回收资源基础

**目标**

为动态对象提供真实的申请、释放、失败回滚和虚拟地址空间。当前四个 PCB、
静态 Ring 0 栈和用户演示路径保持工作，直到 v1.2 完成迁移。

**当前已完成增量**

- 根据 E820 与 CPUID 管理全部普通 RAM；
- 动态放置 2-bit 页帧元数据并避开平台、Kernel、栈和自身；
- 建立从 `0xFFFF888000000000` 开始的 64 TiB direct-map；
- 64 GiB 下实际分配、读写并释放 4 GiB 以上页帧。
- 64 KiB kernel heap 支持 best-fit、任意二次幂对齐、释放、前后合并、
  非法释放检测、完整一致性检查和生命周期统计；
- 固定种子堆模型执行 100000 次随机申请/释放，目标启动自检结束后活动对象
  和当前占用恢复为零。

**剩余产出**

- 可分裂、合并和检测非法释放的 buddy frame allocator；
- 建立在通用 heap 之上的固定尺寸 type cache；
- 内核虚拟地址分配器 KVA，为非连续映射和 guard page 分配区间；
- 动态内核栈与 guard page；
- 中间页表回收、通用引用计数、作用域回滚和资源快照。

**退出条件**

- 固定种子模型至少执行 100000 次分裂、合并、申请、释放和耗尽操作；
- 重复释放、保留页释放、错误对齐、地址溢出均明确失败且状态不变；
- 256 MiB 与 64 GiB 反复创建/销毁资源后页、KVA、堆和对象统计回到基线；
- 64 MiB boot 与 v1.0 的 73 项回归继续通过；
- Kernel ELF 不新增构造器、隐藏分配或未解析运行时符号；
- 当前四进程完整 QEMU 路径没有被提前删除。

### v1.2 Process/Thread、等待与完整现场

**目标**

让 Process 成为共享资源容器、Thread 成为唯一调度实体，并冻结单 BSP 内核
执行、等待和锁语义；本版本继续使用 `INT 0x80`。

**产出**

- 独立 `uint64_t` ProcessId/ThreadId，不与指针、数组槽或即时复用绑定；
- Process 持有 AddressSpace 和 Thread 列表，后续共享对象通过明确接口附加；
- Thread 持有通用寄存器、内核栈、调度状态、用户栈和信号 mask 位置；
- 动态 run queue、Thread 创建/退出/reap 和 Process Zombie；
- `WaitQueue`、单赢家 `WakeReason`、SpinLock/IrqSaveSpinLock/Mutex；
- `FXSAVE/FXRSTOR` 保存每 Thread 的 x87/SSE/SSE2；AVX 保持禁用；
- 将当前四个 PCB/静态栈迁移到新模型，然后删除旧实现。

**退出条件**

- functional 配置可同时存在 64 Process/128 Thread，单 Process 32 Thread；
- 容量模型可建立 256 Process/512 Thread，单 Process 64 Thread；
- Ready/Running/Blocked/Exited 与 Process Zombie 集合互斥、计数守恒；
- IRQ handler 不阻塞，持有 spinlock 的路径不调度，所有等待都有 WakeReason；
- 浮点/SSE2 模式在抢占、阻塞、退出后逐 Thread 隔离；
- 100000 步随机调度/唤醒/退出模型和现有四程序 QEMU 行为通过。

### v1.3 CpuLocal 与 x86-64 原生系统调用

**目标**

在稳定 Thread 模型上建立安全的 `SYSCALL/SYSRET` 入口，同时保留
`INT 0x80` 兼容入口。

**产出**

- 单元素 `CpuLocal`：current Thread、入口 RSP、IRQ/抢占深度、need-resched；
- 启动时冻结并检查 QEMU CPU 型号、long mode、NX、SSE2、SYSCALL 等特性；
- 配置 STAR/LSTAR/FMASK/EFER，使用 `SWAPGS` 和可信内核栈进入；
- 两种入口规范化为同一 `UserContext` 和同一 syscall dispatcher；
- canonical RIP/RSP、RFLAGS、段状态和用户地址验证；
- 安全状态走 `SYSRETQ`，其他合法状态走 `IRETQ`；NMI 使用最小独立路径。

**退出条件**

- 双入口对同一调用返回值、错误码和副作用一致；
- 用户控制的 RSP 不被用作入口内核栈；
- 非 canonical RIP/RSP、危险 RFLAGS 和伪造段状态不能返回 Ring 3；
- 中断嵌套、系统调用被 IRQ 打断和返回前 reschedule 具有 QEMU 证据；
- 缺失必需 CPUID 特性时启动明确失败，不以未定义行为继续。

### v1.4 类型化对象与动态描述符

**目标**

用动态 KernelObject、FileTable 与 FileDescription 取代“fd 就是固定资源槽”。

**产出**

- 类型化对象引用和统一 acquire/release，禁止跨模块持有裸内部指针；
- 分块增长 FileTable，独立 soft/hard limit 和最低可用 fd 分配；
- FileDescription 保存偏移与 file status flags；
- fd flags 与 file status flags 分离，支持 close-on-exec；
- Console、Pipe 和 legacy File 经统一 I/O 契约接入；
- 安装 fd 的两阶段提交与失败回滚。

**退出条件**

- functional hard limit 256，capacity hard limit 4096；
- duplicate 共享 FileDescription 偏移，独立 open 不共享；
- fd 关闭后可复用，但陈旧引用不能访问新对象；
- 100000 步 open/duplicate/close/limit 随机模型最终计数归零；
- 内存或 fd 耗尽不产生半安装、重复关闭或引用泄漏。

## 波次 B：命名与程序

### v1.5 VFS、memfs 与旧格式基础适配

**目标**

先稳定独立于磁盘布局的对象、路径和挂载语义，再升级磁盘格式。

**产出**

- Superblock、Mount、Vnode、Path、FileDescription 契约；
- 每 Process 的 root/cwd `FsContext`；
- 绝对/相对路径、`.`、`..`、根夹取和挂载点遍历；
- 完整 memfs 测试后端；
- legacy-fs 只读、基础创建和 v1.0 回归所需的最小适配；
- 明确锁顺序；v2.0 核心不加入正/负 dentry cache。

**退出条件**

- 4096 字节路径、255 字节组件、循环与越界具有明确结果；
- 路径解析参考模型与 100000 步随机目录树一致；
- 同一 VFS 测试在 memfs 与 legacy-fs 基础子集上通过；
- Shell 不再直接访问具体 inode 或 ATA；
- 旧磁盘可读取，未知非零磁盘不会被自动格式化。

### v1.6 rootfs v2 与完整命名空间

**目标**

在 VFS 不变的前提下建立可容纳程序与大文件的新磁盘格式。

**产出**

- 版本化小端 superblock、inode、位图、目录和直接/间接块；
- create/mkdir/unlink/rename/truncate/stat 的完整 VFS 后端；
- 稀疏大文件、短写、磁盘满和原子失败语义；
- 独立 Python mkfs、image inspector 和只读 fsck；
- legacy-fs 保留为迁移和回归后端，不再扩展功能。

**退出条件**

- capacity 处理 1 GiB 稀疏镜像、256 MiB rootfs 和 64 MiB 单文件；
- rename 覆盖、非空目录、跨挂载、truncate 扩缩和 ENOSPC 路径通过；
- 100000 步命名空间模型与 memfs/rootfs v2 一致；
- 冷启动写入、flush、重启与独立 fsck 得出相同可达性；
- 损坏元数据只读拒绝，禁止静默重格式化。

### v1.7 PID1、进程树与磁盘 exec/wait

**目标**

移除正常启动对内嵌用户程序的依赖，由 PID1 形成真实用户进程树。

**产出**

- 父子关系、reparent、Process Zombie、wait/waitpid；
- 从 VFS 读取并两遍验证 ELF64 的 spawn/exec；
- `/sbin/init` 启动 `/bin/sh` 并持续回收孤儿；
- `argc/argv/envp` 由可回收页分批暂存并构造用户栈；
- exec 候选地址空间先完整建立，失败不改变调用 Process。

**退出条件**

- 正常 Kernel 不再内嵌 Shell、producer、consumer 和 worker；
- 参数环境合计 128 KiB 可逐字节验证，内核栈占用保持有界；
- ELF 截断、权限、参数过大、内存不足均保持旧映像可运行；
- 4096 次 spawn/exec/wait 后 Process、Thread、页、fd 和 vnode 回到基线；
- PID1 对孤儿 reparent/reap，无不可达 Zombie。

## 波次 C：虚拟内存与用户组合

### v1.8 匿名 VMA 与用户运行时内存

**目标**

让 VMA 表示地址空间意图，让匿名物理页只在首次访问时出现。

**产出**

- 非重叠 VMA 容器与 split/merge/unmap；
- 匿名 `mmap`、`munmap`、`brk` 和统一匿名页故障；
- guarded 用户栈与受控增长；
- 自研用户 heap，提供有界 allocation/free；
- TLB 失效和页表空分支回收。

**退出条件**

- 未触及匿名范围不分配物理页，首次读为零，首次写可持久到映射生命周期；
- 100000 步 VMA map/unmap/split/merge 参考模型通过；
- guard、权限、越界、重叠和地址溢出均明确失败；
- munmap/exec/exit 后 VMA、页表和物理页回到基线；
- 用户 heap 随机申请/释放、耗尽和失败原子性通过。

### v1.9 文件页故障与有界 clean page cache

**目标**

在匿名 VM 稳定后加入文件来源、按需 ELF 与可回收 clean cache。

**产出**

- file-backed VMA、`MAP_PRIVATE` 和只读 `MAP_SHARED`；
- 按需 ELF 文本/只读数据页及最后一页零填充；
- 以 `(Vnode, page index)` 为身份的有界 clean page cache；
- LRU 回收与共享驻留页引用；
- 文件 write/truncate 的缓存失效和映射撤销规则。

**退出条件**

- 大 ELF 启动不预先分配全部 PT_LOAD 页面；
- 并发 fault 同一文件页只安装一个权威 cache page；
- cache 达到硬限制时先回收 clean LRU，不能回收则明确失败；
- truncate/write 后旧映射不可继续观察陈旧页；
- `MAP_PRIVATE` 修改不回写文件，只读 `MAP_SHARED` 观察一致 clean 页；
- writable `MAP_SHARED` 与 `msync` 明确返回不支持。

### v1.10 fork 与写时复制

**目标**

在统一 VMA/page-fault 模型上实现延迟复制和多线程 Process 的明确 fork 语义。

**产出**

- fork 只在子 Process 创建调用 Thread；
- AddressSpace、物理页、页表和 FileTable 的引用复制/回滚；
- 私有可写页标记 COW，首次写入执行单页私有化；
- `CopyToUser` 使用同一 COW break 路径；
- fork/exec/exit 的 TLB、引用和异常清理。

**退出条件**

- 父子初始共享，任一方写入后数据正确隔离；
- 只读文件页、私有文件页、匿名 COW 和非法写故障不混淆；
- Kernel 向用户 COW 页复制不会修改另一 Process；
- fork 中途内存不足不留下子对象或降低父页权限；
- 深度 32 的 fork/exec/wait 树和 100000 步引用模型完整回收。

### v1.11 Unix I/O、外部 Shell 与核心工具

**目标**

通过 fd 继承、pipe、dup 与重定向组合磁盘上的独立程序。

**产出**

- 动态 64 KiB pipe、按需缓冲页、EOF 与 broken-pipe；
- duplicate/duplicate-to、close-on-exec、fd 继承和重定向；
- 外部 `/bin/sh`，只保留必须修改自身状态的内建命令；
- 约 12 个代表性工具，覆盖文本、目录、文件、进程和流水线；
- 解析、执行和失败清理相互分离。

**退出条件**

- functional 同时建立 128 条 pipe，capacity 建立 1024 条；
- 16 级流水线在短读/短写、早退、关闭和失败注入下不死锁；
- fd 偏移共享、EOF、broken-pipe、继承和 close-on-exec 语义正确；
- Shell 任一中间 fork/pipe/dup/exec 失败后关闭全部临时资源；
- 代表性工具全部从 rootfs ELF 执行，不内嵌进 Kernel。

## 波次 D：并发与交互

### v1.12 用户 Thread、TLS 与 private futex

**目标**

开放同一 Process 内的多个用户执行流，并提供可组合同步基础。

**产出**

- ThreadCreate/ThreadExit/Join 与用户栈生命周期；
- FS-base TLS 和每 Thread runtime state；
- private futex key 为 `(AddressSpaceId, aligned user VA)`；
- compare-and-block、wake、超时/信号预留与 unmap cancellation；
- 用户 mutex、condition variable 和 once。

**退出条件**

- functional 单 Process 32 Thread，capacity 单 Process 64 Thread；
- TLS 经抢占、阻塞、syscall、signal 预留路径后保持隔离；
- compare-and-block 与 wake 的随机交错不丢失唤醒；
- munmap/exec/ProcessExit 取消相关 futex waiter，不保留悬空用户地址；
- ThreadExit 与 ProcessExit 的资源和 wait 可见性严格区分。

### v1.13 单调时间、deadline 与 timed wait

**目标**

把 tick 计数提升为统一单调时钟和可组合超时机制。

**产出**

- 固定宽度 monotonic time ABI 与溢出安全换算；
- deadline queue、sleep 与 timed WaitQueue；
- PIT tick、超时到期和条件唤醒的单赢家竞争；
- 用户 sleep 和 runtime timed wait；
- wall clock 明确不进入 v2.0。

**退出条件**

- 时间在长 tick、除数舍入和 64 位边界模型中不倒退；
- sleep 不忙等，空闲时仍经 `sti; hlt; cli`；
- 条件与 deadline 同时发生只完成一次等待；
- 100000 步虚拟时间模型无丢失、重复唤醒或队列残留；
- QEMU 串口里程碑使用来宾单调时间且热路径不刷日志。

### v1.14 signal、进程组与中断语义

**目标**

建立异步用户通知和多线程 Process 的统一交付规则。

**产出**

- Process signal disposition、Thread mask 与 pending 选择；
- 默认/忽略/用户 handler、signal frame 和 sigreturn；
- process group 与按 Process/Group 定向发送；
- 阻塞 syscall 的部分进度、interrupt error 和可重启策略表；
- exec 重置信号处置、fork 复制处置和 Thread mask。

**退出条件**

- signal frame、canonical 地址、RFLAGS、栈边界和 sigreturn 严格验证；
- signal 与条件/超时竞争只产生一个 WakeReason；
- 多 Thread Process 只选择一个符合 mask 的 Thread 交付同一普通信号；
- 畸形 frame 只终止目标 Process，不 panic Kernel；
- fork/exec/exit 和阻塞 I/O 的信号语义与契约一致。

### v1.15 TTY、session 与作业控制

**目标**

把控制台升级为具备前台所有权和作业控制的终端。

**产出**

- TTY input/output queue、基本 canonical mode 和控制字符；
- session、session leader、controlling TTY、foreground process group；
- Ctrl-C/Ctrl-Z 向前台组发送 signal；
- Shell 前后台 job、wait/continue 与终端所有权切换；
- Console 先经 VFS device vnode 暴露；通用 devfs 在 v1.18 收口。

**退出条件**

- 后台组不能读取控制终端，前台切换保持输入归属；
- Ctrl-C 只终止前台流水线，Shell 保持可交互；
- stop/continue/exit 后无遗失 Thread、fd 或 Process Zombie；
- 真实 QMP 键盘输入经过 i8042/IRQ/TTY/signal 全链路；
- 输入、tick 和调度热路径不逐事件刷串口。

## 波次 E：持久化与冻结

### v1.16 IRQ 块层与 writeback page cache

**目标**

将 ATA 从同步轮询调用升级为可等待请求，并建立 dirty/writeback 页状态。

**产出**

- BlockDevice/BlockRequest、队列、完成状态和错误传播；
- ATA PIO 命令提交与 IRQ14 完成，超时仍有明确恢复；
- Thread 在 I/O 等待时阻塞，其他 Ready Thread 可运行；
- cache page 的 clean/dirty/writeback/error 状态机；
- 有界后台/显式 writeback、flush 与 backpressure。

**退出条件**

- IRQ handler 只确认设备、推进有界状态并唤醒，不睡眠；
- 请求完成、超时、设备错误只允许一个结果获胜；
- dirty 页在压力下受限，写回失败不被伪装为 clean；
- I/O 阻塞期间调度证据显示其他 Thread 前进；
- 随机请求完成顺序和 cache 状态模型无重复完成或资源泄漏。

### v1.17 ordered metadata journal 与崩溃恢复

**目标**

让命名空间与分配元数据在任意已覆盖断电点后恢复为旧状态或完整新状态。

**产出**

- metadata-only journal、transaction credits 和空间预留；
- descriptor/metadata/commit 记录、校验和与序列号；
- ordered mode：相关数据稳定后才提交元数据；
- FLUSH CACHE 边界、checkpoint 和幂等 replay；
- 宿主断电注入器与独立 fsck oracle。

**退出条件**

- transaction credits 不足时在修改前失败；
- nightly 至少覆盖 1000 个确定性断电点；
- 每个恢复结果只能是旧事务或完整新事务，元数据始终可达且无重复分配；
- replay 可重复执行，损坏/截断记录不会越界或伪造提交；
- fsck、重放统计和磁盘镜像哈希形成可复现证据。

### v1.18 ABI v2 冻结、系统加固与发布溯源

**目标**

停止增加基础机制，冻结用户边界，补齐系统观察面、工具宽度和发布证据。

**产出**

- 固定 syscall 号、错误、结构布局、时间、signal 和 ELF 契约；
- `procfs` 只读进程/内存/资源统计与最小 `devfs`；
- 用户工具补齐到至少 32 个，但不新增 Kernel 机制；
- user-copy、整数溢出、引用、锁顺序、日志和资源限制全审计；
- 发布清单记录主仓 SHA、web SHA、Sites 版本、PDF 哈希、代码量、QEMU/CPU。

**退出条件**

- ABI 结构大小、偏移、枚举和调用号由编译期与产物测试锁定；
- 畸形 syscall、ELF、路径、signal frame、文件系统不能 panic Kernel；
- functional/capacity soak 无单调资源增长、锁死或日志洪泛；
- 32 个工具和 16 级流水线通过；
- 文档、教材、网站与发布清单相互链接且内容一致。

## v2.0 集成发布

v2.0 不实现新机制，只完成版本冻结、回归、教材和公开发布：

- 64 MiB bootstrap、256 MiB functional、64 GiB capacity 全部通过；
- 所有单元、集成、随机、产物、QEMU、soak 和崩溃恢复套件通过；
- `/sbin/init → /bin/sh → /bin/*` 正常路径不依赖内嵌用户载荷；
- 256 Process/512 Thread 完成 VM、I/O、signal、TTY 和资源回收；
- Kernel 和用户 ELF 不链接宿主运行时，不出现构造器或未解析符号；
- 主仓、网站仓、托管版本、教材 PDF 和源代码统计具有同一发布清单。

任何新增核心机制都进入 v2.x，不得为了“发布更完整”破坏冻结期。

## 跨阶段不可妥协门禁

### 正确性

- 所有大小、地址、计数、身份和时间使用固定宽度类型；
- 无魔法数字/字符串；常量按项目、模块、功能全大写命名；
- C++ 变量的语义单词以单个下划线分隔，私有/受保护成员允许尾部下划线；
- 命名空间每层只用一个简短小写单词，复杂归属使用多层命名空间；
- C++ 类成员访问显式使用 `this->`；
- 普通 C++ 函数和自研 C/汇编函数符号使用大驼峰；
- 头文件与源文件分离，公开/私有头文件隔离，依赖只向下；
- 用户指针只经统一 user-copy，且 user-copy 不持有 VFS/cache/journal/device 锁；
- 失败路径要么不改变状态，要么完整逆序回滚。

### 测试

- 纯逻辑必须有单元、模块集成和固定种子随机测试；
- 每个机制至少有一条 QEMU 成功路径和一条真实失败路径；
- 目标 ELF 必须审计段、权限、符号、ABI 和运行时依赖；
- 完整回归必须通过 Clang AST 标识符门禁与命名空间单词门禁；
- 随机测试验证性质和守恒，不以随机打印样例替代 oracle；
- 64 GiB 测试必须实际触及 4 GiB 以上地址。

### 可观测性

- 日志包含来宾单调时间、模块、事件和必要标识；
- 热路径只更新有界计数，不逐 tick、逐字节、逐页或逐锁输出；
- 状态提交后再输出成功日志，失败只报告最接近根因的一次；
- panic 路径不依赖动态分配、VFS、页缓存或可睡眠锁。

### 文档与交付

- 修改架构、ABI、测试或版本边界时，同步 ADR、路线、模块文档和教材；
- 网站只展示主项目源码与文档，不展示 `web/` 自身代码；
- 教材代码统计只计算 `source/` 下 `.cpp`、`.hpp`、`.asm` 目标代码；
- 小版本退出时创建发布记录，包含命令、测试数、关键日志和已知限制；
- commit 和远程推送只能在完整验证通过后完成。

## v2.0 之后

以下内容明确留给 v2.x/v3.0：

- SMP、AP 启动、跨核调度与 TLB shootdown；
- writable `MAP_SHARED`、`msync`、swap、overcommit、OOM killer；
- 正/负 dentry cache、数据 journal、快照与在线扩容；
- 网络、图形、音频、USB、AHCI、NVMe、通用 PCI；
- AVX/XSAVE、动态链接、共享库、自举编译器和完整 POSIX。

这些不是遗漏，而是保护 v2.0 主线可证明、可学习、可按期收敛的范围边界。

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
版本才允许完成。网站生产内容也必须已经同步到同一版本；不得把“下一阶段会
修复”或“网站以后再更新”作为当前阶段的验收结果。具体状态机见
[发布闭环](releasing.md)。

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
第二周期必须持续保持的历史回归基线；当前动态内存、可回收堆、buddy 与类型
缓存、KVA、动态内核栈、页表空分支回收、Process/Thread、WaitQueue 和
扩展现场以及 v1.3 原生入口收口完成后，完整集合为 103 项。
v1.4 删除旧固定描述符表并新增四层对象/fd 证据，实体学习图门禁随后把完整
集合推进到 107 项；v1.5 再加入 VFS 单元、双后端契约和十万步命名空间模型，
形成 110 项历史集合。v1.6 再加入 rootfs 格式、集成、真实容量证据，并扩展
随机、工具与 QEMU 持久化；v1.7 又加入进程树、参数布局、4096 轮生命周期
集成、固定种子随机模型、rootfs ELF 安装和 PID1 整机证据。v1.8 再加入
VMA/UserHeap 单元与十万步模型、128 轮页表生命周期、三个 Ring 3 VM probe
和具名 64 MiB bootstrap；v1.9 再加入文件页缓存；v1.10 新增 COW 引用
单元、页表集成和十万步引用随机模型；v1.11 再加入动态 Pipe、Shell 执行
计划、dup2、QEMU 重定向与 16 级管线证据；v1.12 加入用户 Thread/TLS/futex，
v1.13 再加入单调时钟、deadline queue 与 timed wait；v1.14 又加入普通信号、
进程组、用户 handler 和安全 sigreturn；v1.15 再加入 TTY、session、
字符设备控制台和前后台作业控制；v1.16 又加入 IRQ14 块请求、I/O 等待、
writable shared 文件页和 dirty/writeback/error cache，当前构建图为 161 项。
数量仍由构建图自动生成，不作为未来版本的固定常量。

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
| functional | 256 MiB | 64 | 128 | 32 | 512 | 128 | PID1、Shell、VM、线程、信号、TTY、持久化 |
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

**状态：完成**

**目标**

为动态对象提供真实的申请、释放、失败回滚和虚拟地址空间。当前四个 PCB 和
用户演示路径保持工作，直到 v1.2 完成 Process/Thread 迁移；其 Ring 0 栈已
先行迁移到动态资源层。

**当前已完成增量**

- 根据 E820 与 CPUID 管理全部普通 RAM；
- 动态放置 2-bit 页帧元数据并避开平台、Kernel、栈和自身；
- 建立从 `0xFFFF888000000000` 开始的 64 TiB direct-map；
- 64 GiB 下实际分配、读写并释放 4 GiB 以上页帧。
- 64 KiB kernel heap 支持 best-fit、任意二次幂对齐、释放、前后合并、
  非法释放检测、完整一致性检查和生命周期统计；
- 固定种子堆模型执行 100000 次随机申请/释放，目标启动自检结束后活动对象
  和当前占用恢复为零。
- 双位图 buddy 在同一页帧状态机中提供 order 0 兼容分配和连续块接口，
  支持范围约束、递归分裂/合并、错阶/错位/重复释放拒绝与完整一致性检查；
- buddy 固定种子模型执行 100000 步申请、释放、耗尽和重复释放；64 MiB 与
  64 GiB QEMU 均实际读写并回收 order 3 连续块。
- 固定尺寸 type cache 用单个 heap 后备块保存活动位图、对齐槽与槽内 LIFO
  空闲链，提供常数时间申请/释放、精确指针与重复释放拒绝、整体销毁和独立
  生命周期统计；
- type cache 固定种子模型执行 100000 步申请/释放；64 MiB QEMU 实际耗尽
  32 个缓存行对齐对象、验证模式、复用槽并在销毁后恢复 heap 基线。
- KVA 用 256 个有序所有权描述符管理 32 TiB 高半区窗口，区分保留区、活动
  分配、描述符耗尽和连续虚拟地址耗尽；释放后空闲缝隙隐式合并；
- KVA 固定种子逐页模型执行 100000 步 best-fit 申请/释放；64 MiB 与 64 GiB
  QEMU 均以双 guard + 四个 RW/NX 数据页完成真实映射、写回、撤销和资源恢复。
- 四个进程 Ring 0 栈已从静态 BSS 迁移为六页 KVA 区间：上下双 guard 保持
  not-present，中间四页使用独立 buddy order-0 后备和 supervisor RW/NX
  映射；终止栈只在汇编回到永久启动栈后的安全点清零并回收。
- 动态栈固定种子模型执行 100000 步创建/销毁；独立进程 CR3 集成测试验证
  共享高半映射，正常四进程与用户 `#UD/#PF` QEMU 路径验证 TSS.RSP0、延迟
  销毁和 frame/KVA 统计恢复。
- 页表根显式区分 `Exclusive`、`KernelShared` 与 `Process` 所有权；撤销
  最后一张叶映射时按 PT、PD、PDPT 逆序回收独占空分支，共享内核根则保留
  仍可能被进程 PML4 引用的 PDPT。
- 映射事务记录新表帧和父项原值；任一级申请失败都会恢复父项权限并逆序
  释放新表。固定种子页表模型执行 100000 步，单元与集成测试同时覆盖精确
  空表判断、借用分支拒绝、所有权损坏和进程根递归销毁。
- 64 MiB 与 64 GiB QEMU 自检各自真实回收两张 PT 和两张 PD，只保留一张
  共享 PDPT，并用有界启动摘要记录结果。
- `ReferenceCounter` 用显式 `uint64_t` 定义不可复活的强引用生命周期、
  最后引用转换、上溢和失败输出保持；并发同步明确留给对象所有者。
- `ScopeRollback` 使用调用方固定动作存储，按取得顺序注册、按严格逆序执行，
  单个动作失败不短路其余清理；动态内核栈创建已删除私有回滚实现并迁移到
  九项真实补偿事务。
- `ResourceSnapshot` 以 26 个具名字段同时记录 frame、buddy、heap、KVA、
  kernel stack 与后续对象扩展槽的当前所有权，不把累计事件误判为泄漏；
  比较返回稳定差异位和变化字段数。
- 启动自检真实创建并经通用回滚销毁一个动态栈，引用计数、作用域回滚和
  快照三项边界均由目标代码验证；四进程生命周期结束后再次要求完整快照
  零差异。
- 固定种子 `0x5245534F55524345` 对引用与回滚模型执行 100000 轮；快照
  单元和真实 buddy/页表/KVA/栈集成测试补齐跨层证据。
- `os_qemu_functional_smoke` 以具名 256 MiB 配置执行完整 Shell、IPC、文件
  系统和资源回收协议；64 MiB bootstrap 与 64 GiB capacity 路径继续使用
  同一实现。

**退出条件**

- 固定种子模型至少执行 100000 次分裂、合并、申请、释放和耗尽操作；
- 重复释放、保留页释放、错误对齐、地址溢出均明确失败且状态不变；
- 256 MiB 与 64 GiB 反复创建/销毁资源后页、KVA、堆和对象统计回到基线；
- 64 MiB boot 与 v1.1 当时的 97 项完整回归继续通过；
- Kernel ELF 不新增构造器、隐藏分配或未解析运行时符号；
- 当前四进程完整 QEMU 路径没有被提前删除。

以上退出条件均已闭环；发布证据见
[v1.1 发布记录](releases/v1.1.md) 与
[ADR 0027](adr/0027-v1.1-resource-lifecycle-foundation.md)。下一实施阶段
是 v1.2，不能把 v1.2 的 Process/Thread 对象提前塞回 v1.1。

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
- 将当前四个 PCB 与动态栈的所有权从 Process 迁移到 Thread，然后删除旧
  PCB 执行模型。

**退出条件**

- functional 配置可同时存在 64 Process/128 Thread，单 Process 32 Thread；
- 容量模型可建立 256 Process/512 Thread，单 Process 64 Thread；
- Ready/Running/Blocked/Exited 与 Process Zombie 集合互斥、计数守恒；
- IRQ handler 不阻塞，持有 spinlock 的路径不调度，所有等待都有 WakeReason；
- 浮点/SSE2 模式在抢占、阻塞、退出后逐 Thread 隔离；
- 100000 步随机调度/唤醒/退出模型和现有四程序 QEMU 行为通过。

以上退出条件均已闭环。64 MiB、256 MiB、64 GiB 三档分别实际建立
4/4/1、64/128/32、256/512/64 的 Process/Thread 容量对象；容量事务使用
真实用户页表根、动态双 guard 内核栈和 16 字节对齐 FXSAVE 区，回收后
ResourceSnapshot 零差异。四个 Ring 3 程序的独立 x87/SSE2 模式经过抢占、
阻塞、唤醒和退出验证；`qemu64,-sse2` 失败配置在任何用户执行前被拒绝。
发布证据见 [v1.2 发布记录](releases/v1.2.md) 与
[ADR 0029](adr/0029-process-thread-waitqueue-fxsave.md)。其后的 v1.3 与
v1.4 也已经按独立验收阶段完成。

### v1.3 CpuLocal 与 x86-64 原生系统调用

**状态：完成**

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

以上退出条件均已闭环。默认用户包装已切换到 `SYSCALL`，`INT 0x80` 保留为
兼容入口；两者进入同一 176 字节 `UserContext` 和同一 C++ 分发器。256 MiB
QEMU functional 路径实际记录 2 次兼容入口、数百次原生入口、非零 IRQ
打断系统调用、非零返回前调度、非零 SYSRET、非零 IRET，以及恰好一次由 DF
触发的原生 IRET 回退；拒绝返回数必须为零，可信栈校验数不得小于入口总数。
固定种子随机测试对 100000 个现场分别验证规范地址、段、RFLAGS 与返回选择，
`qemu64,-syscall` 失败配置则在任何用户代码之前输出缺失能力位图并停止。
发布证据见 [v1.3 发布记录](releases/v1.3.md) 与
[ADR 0030](adr/0030-cpu-local-native-system-call.md)。下一实施阶段为 v1.4。

### v1.4 类型化对象与动态描述符

**状态：完成**

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

以上退出条件均已闭环。`KernelObjectManager` 现以 type、variant、全局单调
generation 和不可复活强引用管理动态对象；`KernelObjectReference` 提供
RAII 临时所有权，私有 handle 只由 `FileTable` 长期持有。首个类型
`FileDescription` 统一承载控制台、管道、普通文件和目录，duplicate 共享
同一 `FileSystemHandle` 与 offset，独立 open 建立独立 offset；管道和文件
只在最后引用释放时关闭。

每 Process 的 FileTable 以 64 项分块按需增长，soft/hard limit 与表形状
分离。64 MiB、256 MiB、64 GiB 分别选择 64、256、4096 hard limit；4096
容量测试实际建立 64 个分块并填满全部 fd。分块申请使用锁外准备、锁内复验
和竞争回滚；安装失败保持传入引用活动。fd flags 独立保存并支持
close-on-exec，用户 ABI 可 duplicate、读取/设置 flags 和调整/查询 soft/hard
limit。

v1.11 为了允许 functional 档 128 条 Pipe 的 256 个端点与标准/文件描述符
同时存在，已把该档 hard limit 从 v1.4 的 256 提升到 512；bootstrap 64 与
capacity 4096 不变。v1.4 的上述数值保留为当时验收历史。

固定种子 `0x46445441424C4531` 已完成 100000 步 open、duplicate、close 与
limit 参考模型；文件系统/管道集成测试、4096 fd 容量测试和 PID4 Ring 3
共享偏移证明共同通过。四 Process 退出后活动对象、FileDescription 和强引用
均为零，创建等于销毁、finalizer 无失败、分块申请等于释放，ResourceSnapshot
保持零差异。发布证据见 [v1.4 发布记录](releases/v1.4.md) 与
[ADR 0031](adr/0031-typed-kernel-object-dynamic-file-table.md)。该对象边界
现已由 v1.5 的 `Vfs + OpenFile` 后端替换完成。

## 波次 B：命名与程序

### v1.5 VFS、memfs 与旧格式基础适配

**状态：完成**

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

以上退出条件均已闭环。`Vnode`、`Path`、`Superblock`、`Mount` 和
`FsContext` 已建立独立于后端的对象边界；每个 Process 保存自己的 root/cwd，
`FileDescription` 保存 `Vfs + OpenFile`，Shell 不再直接访问旧 inode。
绝对/相对路径、重复分隔符、`.`、`..`、root clamp、尾部分隔符和挂载进入/
退出均由同一逐组件算法处理，公共上限为 4096 字节路径和 255 字节名称。

根文件系统继续通过 legacy 适配器读取旧磁盘，`/tmp` 挂载完整 memfs。
同一基础契约已分别在两个后端通过；固定种子
`0x5646532026001500` 的独立目录树模型执行 100000 步并逐步一致。functional
QEMU 由真实 Shell 在 `/tmp` 完成相对路径、cwd、文件和目录操作，再回到
legacy 根目录完成持久写入；跨实例持久化、损坏拒绝、用户异常隔离和非法
ELF 拒绝继续通过。memfs 长期资源由 VFS 精确登记，不会被误判为 Process
泄漏，未登记的 frame、KVA、heap、对象或 fd 泄漏仍会使资源快照失败。

发布证据见 [v1.5 发布记录](releases/v1.5.md) 与
[ADR 0032](adr/0032-vfs-mount-namespace-and-memfs.md)。该版本作为
rootfs v2 的差分基线继续保留。

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

以上退出条件已经闭环。生产根文件系统现由 `RootFileSystem` 严格挂载，
legacy 后端只保留格式兼容与回归用途。逻辑 1 GiB 稀疏启动盘中的固定
256 MiB rootfs v2 使用小端、版本化且带 CRC32 的 superblock、inode、
320 字节目录项和间接指针块；八个直接块与单/双/三级间接树共同覆盖
64 MiB 文件规格。inode/data bitmap、可达性、generation、目录父链和
数据/元数据块所有权均能由内核与独立 Python fsck 重新计算。

VFS 与 memfs/rootfs 后端现共同支持 unlink、rmdir、同目录及跨目录 rename、
替换、非空目录/祖先环/挂载点保护、truncate 扩缩与 stat。普通文件允许
稀疏空洞，读取空洞返回零；空间不足时已经写入的完整前缀形成明确短写，
若一个字节也不能提交则返回容量耗尽，文件大小与 bitmap 仍保持一致。
打开引用与目录链接生命周期分开，删除或替换打开对象返回 Busy，避免本阶段
引入尚未实现的 orphan inode 回收协议。

所有 rootfs 修改都执行 `Dirty → 数据及元数据 flush → Clean`。设备失败会
永久关闭当前实例，Dirty 或任何校验不一致的介质在下次挂载时只会被拒绝，
内核没有格式化入口。宿主提供 `mkfs-rootfs`、`inspect-rootfs`、
`fsck-rootfs` 与 `corrupt-rootfs`；构建系统预先格式化启动盘。

验收包括格式单元测试、真实 256 MiB 近满镜像短写/ENOSPC、64 MiB 文件三级
间接树与 truncate、设备写失败 Dirty 拒绝、memfs/rootfs 同种子 100000 步
模型、QEMU 同盘两次启动、独立 fsck 以及损坏后的第三次拒绝。详细证据见
[v1.6 发布记录](releases/v1.6.md) 与
[ADR 0033](adr/0033-rootfs-v2-namespace-mutations.md)。

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

以上退出条件已经闭环。普通用户 ELF 由构建工具离线安装到 rootfs，正常
Kernel 只读取 `/sbin/init` 并创建 PID1；Shell、worker、producer 和 consumer
不再进入正常 Kernel 映像。`UserElfReader` 先分块验证全部程序头，再按页
读取段；截断、W+X、范围和短读由 ELF/reader 测试覆盖，真实 QEMU 又证明
截断与 E2BIG exec 失败后旧映像继续运行。

`ProcessTree` 独立保存 PID/父关系/Alive/Zombie/退出结果，非 PID1 父进程
退出时把 Alive 和 Zombie 子项重新托管给 PID1。wait 使用统一 WaitQueue，
结果地址先验证再回收。参数与环境合计精确支持 128 KiB，以 256 字节缓冲
搬运到 256 KiB 用户栈。4096 轮宿主集成模型负责放大槽位复用和状态机，
64/256 MiB 真实 QEMU 负责证明页表、Thread、KernelStack、fd、FsContext、
Vnode 和对象资源回到基线；两层证据共同满足生命周期退出条件。

详细证据见 [v1.7 发布记录](releases/v1.7.md) 与
[ADR 0034](adr/0034-pid1-process-tree-disk-exec-wait.md)。

## 波次 C：虚拟内存与用户组合

### v1.8 匿名 VMA 与用户运行时内存

**状态：完成**

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

以上退出条件已经闭环。每个地址空间现拥有按地址递增的页对齐 VMA 图，
全局 8192 描述符池以 owner identifier 隔离不同 Process，单 Process
hard limit 为 4096。Insert 拒绝重叠并合并同属性邻居；Remove 先预检 kind，
中段拆分在修改前取得额外描述符，元数据耗尽保持旧映射不变。

匿名窗口固定为 `[0x60000000, 0x80000000)`。系统调用 39..42 提供非覆盖
fixed/first-fit 匿名 map、匿名 unmap、program break 与 112 字节 VM 统计。
map 与 break growth 只登记 VMA；用户 not-present `#PF` 经 CR2/error code、
VMA 权限和 kind 检查后才分配并清零一页。完整 8 MiB 栈先预留，只有紧邻
committed bottom 且与用户 RSP 邻近的 fault 才增长；底部下一页永久没有
VMA。

撤销只释放实际驻留 frame，并继续使用页表所有权协议回收空 PT/PD/私有
PDPT。exec 与 exit 销毁整张 VMA 图；最终描述符 active 为零且
acquire/release 增量守恒。Ring 3 `UserHeap` 在 program break 上用 64 字节
header、16 字节对齐、first-fit、split 和前后 coalesce 管理最多 8 MiB。

VMA 与 heap 各自通过 100000 步固定种子参考模型；128 轮组合测试证明预留
不耗 frame、首次触页才映射、中段撤销回收页表并恢复基线。真实 memory probe
覆盖 32 MiB 稀疏触页、零填充、写保持、split/remap/unmap、2 MiB break、
栈增长和 5000 步 heap；guard 与只读写 probe 必须分别以用户 vector 14
结束。64 MiB、256 MiB 和 64 GiB 三档均运行相同完整工作负载。

详细证据见 [v1.8 发布记录](releases/v1.8.md) 与
[ADR 0035](adr/0035-anonymous-vma-demand-paging-user-heap.md)。

### v1.9 文件页故障与有界 clean page cache

**目标**

在匿名 VM 稳定后加入文件来源、按需 ELF 与可回收 clean cache。

**状态：已完成。**

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

**已冻结证据**

- VMA、FileBacking、FilePageCache 与 PTE 分别保存地址政策、稳定来源、
  clean 内容和当前驻留，fd 关闭不破坏映射；
- ELF reader 仍完整两遍校验，但 `PT_LOAD` 只建立文件 VMA，入口及后续页面
  由真实用户 fault 装入；
- cache 以完整文件 generation 身份加 page index 唯一标识，容量按内存缩放
  为 256..4096 页，零引用 clean LRU 可丢弃；
- write/truncate 撤销旧只读 PTE 并失效 cache，private 写不回写；
- 单元、共享页集成、VMA/cache 十万步随机模型和 64 MiB/256 MiB/64 GiB
  QEMU 全部闭环；
- 启动 staging 扩为 8 MiB，rootfs 移到 LBA 32768，并由跨语言布局契约阻止
  再次不同步。

详细证据见 [v1.9 发布记录](releases/v1.9.md) 与
[ADR 0036](adr/0036-file-backed-vma-lazy-elf-clean-page-cache.md)。

### v1.10 fork 与写时复制

**目标**

在统一 VMA/page-fault 模型上实现延迟复制和多线程 Process 的明确 fork 语义。

**状态：已完成。**

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
- 连续 32 次 fork/exec/wait 和 100000 步引用模型完整回收；64 MiB 兼容档
  不承担超过八个 Process 的并发压力。

**已冻结证据**

- 系统调用 44 只复制调用 Thread；父返回子 PID，子从同一现场以 0 返回；
- PTE 软件位 9 表示 COW，Writable+COW 被拒绝，真正只读页不混入 COW；
- 普通 private 页隐含独占，32768 项稀疏表只追踪已经共享的 frame；
- 用户 present+write `#PF` 与 Kernel `CopyToUser` 共用 COW break；
- 候选 child 先完整准备，父 PTE 后提交；中途失败恢复父权限和所有引用；
- FileTable 保留精确 fd/flags 并共享 FileDescription offset，FsContext 与
  FileBacking 具有独立继承引用；
- 单元、页表集成、100000 步引用模型和 64 MiB/256 MiB/64 GiB QEMU
  全部闭环；连续 32 次 fork/exec/wait 后活动引用与 Zombie 均为零。

详细证据见 [v1.10 发布记录](releases/v1.10.md) 与
[ADR 0037](adr/0037-fork-copy-on-write.md)。

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

**完成证据**

v1.11 已完成。functional 的 128 条 Pipe 与 capacity 的 1024 条 Pipe 均真实
耗尽并回收；动态 64 KiB 流使用 4 KiB 按需页，固定种子模型执行 100000 步。
系统调用 45/46 提供 Pipe pair 与精确 dup2，外部 Shell 只保留 cd/exit，
十九个工具全部从 rootfs 多调用 ELF 执行。256 MiB QEMU 真实建立 15 条 Pipe
和 16 个并发 stage，并验证 `<`、`>`、EOF、fd 关闭和最终零 Zombie/零活跃
Pipe。详细证据见 [v1.11 发布记录](releases/v1.11.md)、
[学习章](learning/19-v1.11-unix-io-external-shell.md) 与
[ADR 0038](adr/0038-dynamic-pipe-dup2-external-shell.md)。

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

**完成状态**

v1.12 已完成。系统调用 47--53、64 KiB guarded user stack、FS-base TLS、
`(AddressSpaceId, aligned VA)` private futex、Mutex/ConditionVariable/Once、
多线程 exec/exit/unmap cancellation 与 Join 回收均已落地。64 MiB 单线程
降级、256 MiB 32 Thread 和 64 GiB 64 Thread 三档整机均完成，最后一个
capacity 创建被明确拒绝，TLS/futex/Join 与全部 KernelStack/Process 资源
守恒。详细证据见 [v1.12 发布记录](releases/v1.12.md)、
[学习章](learning/20-v1.12-user-threads-tls-private-futex.md) 与
[ADR 0039](adr/0039-user-threads-fs-tls-private-futex.md)。

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

**完成状态**

v1.13 已完成。PIT 输入频率与实际除数进入精确有理数累计器，整数余数跨 tick
保留，64 位边界饱和而不回绕；512 槽 deadline queue 按
`(deadline, sequence)` 稳定排序并由 ThreadScheduler 统一拥有。
GetMonotonicTime/SleepUntil/WaitPrivateFutexUntil 固定为系统调用 54--56，
用户 ConditionVariable 提供重新加锁后的强类型 timed wait。64 MiB idle
sleep、256 MiB 与 64 GiB notifier-before-deadline 均由真实 PIT IRQ 证明，
100000 步随机模型和整机汇总证明无早醒、重复 Ready 或 deadline 残留。
详细证据见 [v1.13 发布记录](releases/v1.13.md)、
[学习章](learning/21-v1.13-monotonic-clock-deadline-timed-wait.md) 与
[ADR 0040](adr/0040-monotonic-clock-deadline-timed-wait.md)。

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

**完成状态**

v1.14 已完成。系统调用 57--63、Process disposition/group、Thread
mask/pending、普通信号合并和唯一 Thread 选择均已落地；Signal 复用 scheduler
单赢家 WakeReason 并取消 deadline。Kernel 在用户栈构造 240 字节固定 frame，
Intel NASM restorer 进入严格 sigreturn；cookie、canonical 地址、RFLAGS、段、
栈边界和页权限全部验证，畸形 frame 只隔离目标 Process。fork/exec/exit 和
阻塞描述符重启由宿主四层测试及 64 MiB、256 MiB、64 GiB QEMU 闭环。详细证据
见 [v1.14 发布记录](releases/v1.14.md)、
[学习章](learning/22-v1.14-process-signals-sigreturn.md) 与
[ADR 0041](adr/0041-process-signals-user-frame-and-sigreturn.md)。

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

**完成状态**

v1.15 已完成。TTY 现在具有 canonical 编辑、EOF、退格、受锁输入/输出账本，
并保存 controlling SID 与 foreground PGID；`/dev/console` 已通过字符
vnode 挂载。ProcessTree 与 scheduler 支持 Stopped/Continued/Exited，
Shell 已实现 `jobs`、`fg`、`bg`、尾部 `&` 和整条管线同 PGID。真实 QMP
Ctrl-Z/Ctrl-C 路径、后台读取拒绝、100000 步组迁移与最终资源归零均通过。

见 [v1.15 发布记录](releases/v1.15.md)、
[学习章](learning/23-v1.15-tty-session-job-control.md) 与
[ADR 0042](adr/0042-tty-session-and-job-control.md)。

## 波次 E：持久化与冻结

### v1.16 IRQ 块层与 writeback page cache

**状态：完成**

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

**完成状态**

v1.16 已完成。`BlockRequestQueue` 现在以 64 位单调标识、FIFO 排队、单个
in-flight ATA 命令和显式 Completed/Reap 生命周期管理请求；IRQ14 只读取
设备状态或搬运一个扇区、确认 PIC、提交唯一结果并定向唤醒等待 Thread。
PIT deadline 与设备 IRQ 竞争时，由请求状态机保证成功、设备错误、超时和
取消中只有一个结果获胜；超时路径执行 ATA software reset 后才允许发出下一
请求。早期 ROM、Stage 1 和启动自检仍使用有界轮询，避免把尚未建立的调度
依赖倒灌进启动链。

文件页缓存已经具备 Empty/Clean/Dirty/Writeback/Error 状态、脏页硬上限、
失败保留与显式重试。完整页、可写打开的 `MAP_SHARED` 映射以初始只读 PTE
捕获第一次写故障，先标脏再开放该映射；`sync` 重新写保护共享映射，按
VFS identity 回写 dirty/error 页，随后经 rootfs cache 和异步 ATA FLUSH
建立稳定边界。`MAP_PRIVATE` 仍只进入 COW，不会污染文件。

单元、集成与两个十万步固定种子随机模型覆盖 FIFO、单赢家、容量拒绝、
回压、写回失败与重试；三档 QEMU 使用同一实现，真实日志证明 IRQ14 完成
期间另一个 Ready Thread 已前进。完整证据见
[v1.16 发布记录](releases/v1.16.md)、
[学习章](learning/24-v1.16-irq14-block-request-writeback.md) 与
[ADR 0043](adr/0043-irq14-block-request-and-writeback-cache.md)。

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

**完成状态**

v1.17 已完成。rootfs 盘面格式升级为 `OSRFV003`，在 256 MiB 分区中固定
保留 256 个 512 B journal 块；四个 descriptor 块提供最多 124 个 metadata
credit。生产路径已经接入 transaction overlay、相关数据先写、prepared
记录、两次提交 FLUSH、home checkpoint 与清理，并在 mount 读取 superblock
前完成恢复。1000 个确定性写入/FLUSH 断电点证明恢复结果只能是完整旧状态或
完整新状态；100000 步随机模型冻结 credit、abort、commit 和 durable-state
守恒。详细契约见 [ADR 0044](adr/0044-ordered-metadata-journal.md)、
[学习章](learning/25-v1.17-ordered-metadata-journal.md) 与
[v1.17 发布记录](releases/v1.17.md)。

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
- commit 和远程推送只能在完整验证通过后完成；
- 每个小版本都必须同步独立网站的首页、路线、文档、代码目录、走读和教材，
  通过网站同步门禁与生产构建后再保存、部署 Sites 版本；
- 公网必须能访问当前发布记录、新增核心代码和同哈希教材；主仓 SHA、web SHA、
  Sites 版本与 PDF SHA-256 必须形成同一发布证据。

## v2.0 之后

以下内容明确留给 v2.x/v3.0：

- SMP、AP 启动、跨核调度与 TLB shootdown；
- `msync`、swap、overcommit、OOM killer；
- 正/负 dentry cache、数据 journal、快照与在线扩容；
- 网络、图形、音频、USB、AHCI、NVMe、通用 PCI；
- AVX/XSAVE、动态链接、共享库、自举编译器和完整 POSIX。

这些不是遗漏，而是保护 v2.0 主线可证明、可学习、可按期收敛的范围边界。

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

### 当前正式配置与容量下限

数字是功能验收下限和运行时限制，不是固定数组形状。

| 配置 | RAM | Process | Thread | 每 Process Thread | fd hard | Pipe | 职责 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| phone-primary | 4 GiB | 64 | 128 | 32 | 512 | 128 | 启动链、PID1、完整功能、PCI-hole 高内存、持久化和故障矩阵 |

4 GiB 是当前验收规格而不是实现上限。实际受管上界取 E820、CPUID 物理地址
宽度、64 TiB direct-map 与运行时限制的交集。

独立宿主容量模型仍必须支持：

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
| 每次提交 | 受影响单元/集成/固定种子随机测试、4 GiB primary smoke |
| 每个小版本 | 全部宿主测试、产物审计、4 GiB 正常/持久化/故障矩阵 |
| nightly / 候选发布 | 4 GiB soak、完整故障与崩溃点矩阵 |
| 公开发布 | 4 GiB 全量、教材/网站构建、发布溯源 |

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

**完成状态**

v1.18 核心实现与独立发布闭环已经完成。ABI v2.0.0 统一冻结 syscall
1--69、错误 -1---57、ELF64/x86-64 身份和关键结构偏移；`/dev` 已由调用者
固定存储的通用最小 devfs 提供，`/proc` 已挂载 version、uptime、meminfo、
processes、resources、mounts 六个动态只读快照。rootfs 安装 32 个独立
工具 inode，来宾 tool probe 检查 ELF，functional Shell 实际执行新增工具。
CTest 共用 QEMU 资源锁，使宿主测试可并行而 TCG/来宾内存串行。详细契约见
[ADR 0045](adr/0045-abi-v2-devfs-procfs-release-freeze.md)、
[学习章](learning/26-v1.18-abi-v2-devfs-procfs-release-freeze.md) 与
[v1.18 发布记录](releases/v1.18.md)。

## v2.0 集成发布

v2.0 不实现新机制，只完成版本冻结、回归、教材和公开发布：

- 64 MiB bootstrap、256 MiB functional、64 GiB capacity 全部通过；
- 所有单元、集成、随机、产物、QEMU、soak 和崩溃恢复套件通过；
- `/sbin/init → /bin/sh → /bin/*` 正常路径不依赖内嵌用户载荷；
- 256 Process/512 Thread 完成 VM、I/O、signal、TTY 和资源回收；
- Kernel 和用户 ELF 不链接宿主运行时，不出现构造器或未解析符号；
- 主仓、网站仓、托管版本、教材 PDF 和源代码统计具有同一发布清单。

**完成状态**

v2.0 已完成上述集成闭环。项目版本提升到 2.0.0，但 ABI 仍是 v1.18 已冻结
并经契约测试锁定的 ABI v2.0.0；生产源码没有借发布之名引入新系统调用、
新设备或新持久格式。173 项 CTest、24 项真实 QEMU、64 MiB/256 MiB/64 GiB
三档机器、跨启动持久化、目标 ELF 审计和发布门禁在同一源码快照上通过。
教材、手机副本、主仓、独立网站、Sites 保存版本和公网路由继续由发布清单
绑定。详细证据见 [v2.0 发布记录](releases/v2.0.md) 与
[学习章](learning/27-v2.0-integration-release.md)。

任何新增核心机制都进入 v2.x，不得为了“发布更完整”破坏冻结期。

## 第三周期：v2.1 至 v2.6

第三周期把当前系统收敛为可在手机 QEMU 中使用的离线本地类 Unix 环境。参考机
固定为 4 GiB 预分配 RAM、128 GiB rootfs 与 28 GiB 独立交换数据；网络在整个
周期中都是非目标。工程故障副本保持稀疏，手机运行副本必须完整物化。

| 版本 | 唯一主目标 | 状态 |
| --- | --- | --- |
| v2.1 | 参考机、VGA 前台、内存日志、键盘和手机显示 | 本地实现基线已推送 |
| v2.2 | 终端、Shell 与本地命令环境 | 本地候选完成，待 caw/手机发布闭环 |
| v2.3 | 使用完整 128 GiB 的 rootfs v4 与可靠持久化 | 本地候选完成，待 caw/手机发布闭环 |
| v2.4 | 本地身份、文件权限与资源限制 | 本地候选完成，待 caw/手机发布闭环 |
| v2.5 | 4 GiB 实体内存、28 GiB 交换盘、回收与 OOM | caw 候选完成，待手机/发布闭环 |
| v2.6 | 全系统集成、长稳验证与规范冻结 | caw 主工程候选完成，待手机/发布 |

每个版本必须独立完成实现、失败路径、全部相关测试、模块文档、ADR、发布记录、
教材、手机 PDF、网站和生产部署。v2.6 不是遗留功能收容阶段；任何未通过自己
版本门禁的机制都不允许拖到 v2.6 拼装。

### v2.1 参考机与可见交互

**范围**

- 手机 QEMU 参考 RAM 固定为 4096 MiB 并使用 `-mem-prealloc`；32 GiB 只保留为
  非手机可选压力档，不属于本周期正式机器身份。
- 启动盘固定为 137438953472 字节稀疏文件，即 LBA `0..0x0FFFFFFF`；本版
  rootfs v2 仍为固定 256 MiB，不宣称已可分配完整 128 GiB。
- ROM 自行设置 VGA 文本模式、16 色 DAC 和 8×16 Basic Latin 字形；Firmware、
  Stage 1 的 16/32/64 位代码和 Kernel 共享光标、属性与滚屏语义。
- 共享输出头升级为版本 3。启动诊断同时写内存日志和 VGA；用户终端激活后，
  Kernel 诊断只写日志，TTY stdout/stderr 写 VGA，panic 始终双写。
- `qemu-display` 自动把内存日志导出到宿主文件；系统测试对成功和失败路径都
  同时检查日志协议与非黑 VGA 截图。
- PS/2 输入、手机 noVNC 命令栏、横竖屏等比适应、原始像素模式和仅回环 VNC
  形成同一交互闭环；串口保持 `none`。

**退出条件**

- 单元测试区分启动诊断、终端文本、紧急输出、清屏切换、滚屏、光标和日志
  溢出；输出模式错误或共享版本不匹配必须拒绝接管。
- 128 GiB 空镜像和启动镜像保持稀疏，逻辑末扇区精确等于 LBA28 最大值；
  普通构建、复制和故障镜像不能把稀疏洞实体化。
- 64 MiB bootstrap、256 MiB functional 和 4 GiB primary 使用同一代码；
  primary 必须实际读写 PCI hole 重映射后位于 4 GiB 物理地址以上的页帧。
- 正常 Shell 画面没有 Kernel 统计刷屏；panic 和早期失败在屏幕可见，完整
  诊断在宿主日志可复现。
- `caw` 隔离全量验证和手机 Termux/noVNC 交互证据通过，再完成发布闭环。

### v2.2 终端、Shell 与本地命令环境

加入有界行编辑、历史和补全；完成 `;`、`&&`、`||`、追加/错误重定向、环境
变量和通配；补齐 grep、find、sort、tail、df、du、hexdump、clear、date、env
等离线工具。磁盘格式和权限模型不在本版改变。

**已完成范围**

- 最多 8 条命令组成执行序列，`;` 无条件继续，`&&`/`||` 按上一条实际退出码
  短路；整行先预解析，后段语法错误不能让前段先产生副作用。
- `>`/`>>` 分别截断/追加 stdout，`2>`/`2>>` 对 stderr 使用相同语义；append
  是共享 FileDescription 状态，每次写前在受保护事务中重新取得文件尾。
- `/bin/err` 提供确定性的 stderr 组合验证；`env`、grep、find、sort、tail、df、
  du、hexdump、clear、date 等把 rootfs 工具路径增至 43 个。
- argument offset/length 依据 512 字节命令上限收紧为 16 位，执行计划与候选
  计划同时驻栈仍保持有界；可执行路径缓冲只保留 `/bin/`、命令与 NUL。
- ShellEditor/Canonical 两态只允许控制会话前台组切换；前台作业继续使用规范
  行规程，Shell 自身获得逐字节输入、左右插入、16 条历史和唯一/共同前缀补全。
- 环境表固定 32 项、每项 128 字节；支持赋值、export/unset、`$NAME`、`${NAME}`、
  `$?` 和 exec 继承。glob 只解释未引用、未转义的 `*`/`?`，每 stage 展开后仍
  不得超过 8 个参数。
- VGA 终端解释有界 CSI 光标、清行与清屏；date 经 ABI v2.1.0 的 GetRealtime
  读取稳定 CMOS RTC 快照并输出 UTC。网络、完整 termios 和时区数据库不进入本版。

### v2.3 128 GiB rootfs v4

一次性建立 64 位几何、可扩展 inode/位图/日志区、大文件、链接、时间戳、
打开后删除和崩溃恢复，使 rootfs 能使用参考盘在启动前缀之后的全部容量。
mkfs、fsck、inspect、损坏注入、高 LBA 和断电矩阵必须同时完成。

**本地候选已实现范围**

- 生产盘面升级为 `OSRFV004`，rootfs 相对块数 268402688，数据区 268300303
  块，最后一个可分配块映射绝对 LBA `0x0FFFFFFF`；格式 3 明确拒绝。
- inode 增至 65536，journal 增至 4096 块/八个 descriptor/248 credits；
  数据 bitmap 65504 块但不进入 Kernel 常驻 BSS。
- 八个直接块与单至五级间接树把单文件稀疏上限提高到 137369755136 字节；
  Kernel 与宿主测试均从末 LBA 读回真实数据。
- 硬链接、绝对/相对/链式符号链接、40 跳环路上限、四时间戳、打开后删除和
  mount orphan 回收进入生产 VFS/rootfs；普通 read 使用 noatime。
- ABI 兼容升级为 v2.2.0，`FileInformation` 扩至 96 字节；`stat` 输出四个
  纳秒时间戳，`df` 使用完整 rootfs 容量。
- `rootfs_v4.py` 公开 mkfs/install/inspect/fsck/corrupt/read 接口；历史
  `rootfs_v2.py` 只保留导入兼容。128 GiB 新建、安装、复制与高 LBA 用例保持
  稀疏。
- journal 1000 点矩阵覆盖 1..248 个 target，共 374620 项断言；rootfs 集成
  另覆盖 open-unlink、最后 close、模拟断电 mount 回收、链接与时间戳。

**退出条件**

- 全部宿主单元、集成、随机、tooling 与失败路径通过；Kernel payload 的
  文件/BSS/装载末端仍在冻结窗口内。
- 64 MiB、256 MiB、32 GiB 三档 QEMU 使用同一 rootfs v4 启动；功能与容量
  路径观察 `ROOTFS_V4_MOUNTED`，持久化二次启动和损坏拒绝通过。
- 128 GiB 镜像及故障副本的逻辑末 LBA、物理占用和稀疏 extent 通过工具门禁。
- caw 隔离全量与手机 Termux/QEMU 持久化证据完成前，只标记为本地候选，
  不宣称正式发布闭环。

### v2.4 本地身份、安全与资源边界

实现 UID/GID、补充组、mode、umask、访问检查、凭据跨 fork/exec、资源限制
和 proc/dev 权限；提供 chmod、chown、ln、readlink、umask 等工具。本版不
加入网络身份、远程登录或密码服务。

**本地候选已实现范围**

- 未由项目硬件/容量约束决定的用户可见规格采用 Linux 语义：32 位 UID/GID/
  mode、root=0、默认 umask 0022、Linux 八进制 mode 与 RLIMIT 0..15 编号。
- real/effective/saved UID/GID、32 项 Kernel 补充组、fork/spawn/exec 继承和
  setuid/setgid exec 已贯通；失败 exec 不改变凭据。
- VFS 逐组件检查目录 search；open、exec、chdir、readdir、truncate 和父目录
  mutation 各自检查所需权限，并实现 sticky 与 setgid 目录继承。
- rootfs v4 用 required feature `UNIX_METADATA` 激活 inode 偏移 200/204/208 的
  uid/gid/mode；旧 v4 镜像明确拒绝，不升级 magic 或静默猜测预留字节。
- FSIZE、DATA、STACK、NPROC、NOFILE、AS 已进入执行点，CORE 固定为 0；其余
  Linux 编号保持可查询，但不存在对应子系统时不伪报已约束。
- `/proc` 为 root:root 0555/0444，`/dev` 为 root:root 0755、字符设备 root:tty
  0660；ABI 升为 v2.3.0、84 个 syscall、112 字节 FileInformation。
- chmod、chown、ln、readlink、真实 id/stat 和 Shell builtin umask 已加入；独立
  工具路径增至 47。Ring 3 探针真实验证非 root、rlimit 短写、fork 继承与 set-ID
  exec。

**退出条件**

- 凭据纯逻辑、VFS 权限、rootfs 持久化、proc/dev 和 ABI 均有独立宿主测试；
- 64/256 MiB 与 4 GiB QEMU 使用同一安全探针，functional 还实际运行权限工具；
- 全部分层和失败路径通过；caw、手机与正式发布身份继续留给 v2.6 闭环。

### v2.5 4 GiB 实体内存与 28 GiB 交换盘

**范围**

- 单 Normal 域按 min/low/high 水位管理；手机参考机用 `-mem-prealloc` 实际提交
  4 GiB RAM，64/256 MiB 档仍使用自己的完整可管理内存。
- 页分配先收缩未引用 clean file page；脏页回写与匿名 swap 进入同一有界回收
  计划，单次最多扫描 65536 个虚拟页。
- secondary IDE master 提供 7340032 个 4 KiB 槽；28 GiB 数据和磁盘哈希元数据
  逐页校验，短 I/O 与损坏保留映射，fork/unmap/exec/exit 维护槽所有权。
- overcommit 采用 Linux 0/1/2 编号，默认 heuristic，严格模式按 swap + 50%
  RAM；匿名 mmap、brk、fork 与销毁路径提交/撤销守恒。
- OOM 按 resident+swap 和 adjustment 确定性选取牺牲者，保护 PID 1；非当前
  Ready/Blocked 进程可以被完整终止并释放地址空间后重试一次 fault。
- `/proc/meminfo` 和最终聚合统计暴露驻留预算、水位、swap、commit 与 OOM；
  终端不逐页打印日志。

**本地候选已实现范围**

- 水位、回收计划、overcommit 和 OOM 选择器已有独立纯逻辑实现；10 万步随机
  事务逐步对照驻留/commit 与 OOM oracle。
- file page cache 裁剪返回真实释放页数；用户 demand fault 和 COW 分配已接入
  驻留预算与直接回收。
- swap manager 已实现数据后元数据提交、读校验后 tombstone、clone、损坏保留和
  统计；SwapStorage 通过独立 ATA 盘运行真实 4 KiB I/O 自检。
- 匿名、program-break 和用户栈页可换出/换入；fork 复制已换出独占页；unmap、
  exec 和 exit 回收未换入槽。
- mmap、brk、fork、失败回滚和地址空间销毁已接 overcommit；调度器支持 OOM
  终止非当前进程，当前牺牲者按 SIGKILL 退出。

**退出条件**

- 单元、集成、随机、tooling、失败路径与目标 ELF 审计通过，正常整机最终
  committed=0、active swap=0、COW/VMA/页帧资源守恒；
- 64 MiB、256 MiB 与 4 GiB QEMU 通过，同一内核验证非黑 VGA、内存日志、
  28 GiB swap 几何、4 GiB 以上重映射地址和宿主 RSS；
- 128 GiB rootfs 与 28.44 GiB 交换盘运行副本通过无宿主空洞门禁；
- 交换短读/短写/校验损坏和 OOM 无候选均有明确失败证据；长时间压力不死锁；
- caw、手机和公开发布闭环仍由 v2.6 完成前，只能标记本地候选。

SMP、NUMA、THP、zswap、休眠恢复和 memory cgroup 不进入本版。

### v2.6 集成冻结与正式发布

不增加主要机制。完成手机 4 GiB/128 GiB/28 GiB swap 整机、`caw` 全分层回归、长稳、
高 LBA、断电恢复、权限、内存压力和 UI 验证，冻结项目版本、ABI 次版本、
rootfs v4、文档、教材和公开发布身份。

**冻结范围**

- 项目版本提升为 2.6.0；ABI 保持 2.3.0/84/-59，rootfs 保持格式 4；
- 发布身份门禁同时读取 CMake、ABI、盘面、Kernel/PID1/Shell/探针、QEMU、README
  和发布记录，拒绝部分升级；
- 发布清单绑定精确主仓 SHA、源码规模、ROM/Kernel 完整哈希和两块大盘结构化
  身份，不对 156 GiB 全零空闲区做无意义顺序哈希；
- 已物化 4 GiB soak 默认连续三轮，每轮运行完整 PID1/Shell/权限/文件系统/内存
  工作负载，仍要求非黑 VGA、最终资源守恒和零 swap 校验失败；
- caw 工程候选完成后，再进行手机 noVNC、持久化、温度/存储余量、教材、网站、
  Sites 与公网闭环。

**退出条件**

- 最终源码快照在 caw 全新隔离目录零警告构建，全量 CTest 与失败矩阵 0 失败；
- 唯一 128 GiB rootfs 和 28.44 GiB swap 运行副本无宿主空洞，三轮 4 GiB soak
  与三启动持久化通过；
- 发布清单的主仓 SHA 等于已推送 `main`，主仓项目范围工作树干净；
- 教材 PDF 与手机副本 SHA-256 相同，独立网站从该主仓 SHA 同步并通过测试/构建；
- Sites 保存版本部署成功，公网首页、架构、路线、v2.6 文档、代码、教材、
  sitemap、robots 和 Worker 日志全部通过。

## 第四周期：v2.7 起的核心机制深化

用户决定暂停 v2.6 公开发布并继续演进 v2。第四周期不增加网络、USB、音频、
图形或一组并行存储驱动；先把文件系统、缓存、内存回收和进程线程推进到可扩展
的单核 Linux 式核心。v2.6 的候选证据作为回归基线保留，不回写其冻结身份。

### v2.7 通用块设备层与自研 NVMe

**范围**

- 把块设备协议从文件系统模块上移到设备模块，rootfs、journal、swap 和页缓存
  只依赖 `BlockDevice`；
- 通用请求保存设备几何和逻辑块数，支持多块、有限多 outstanding、乱序完成、
  确定超时与唯一终态；
- ATA PIO 保持启动和回退；Kernel 自行实现最小 PCI/NVMe 1.4 单控制器、单
  namespace、单 I/O queue；
- QEMU 只模拟 `nvme` 设备，不使用 virtio、passthrough、外部固件或宿主驱动；
- 本版不改 rootfs v4 格式、不增加系统调用，也不启动公开发布闭环。

**增量顺序**

1. 通用 `BlockDevice`、几何与多深度请求模型（已完成）；
2. 最小 PCI configuration space 与 BAR/MMIO（已完成）；
3. NVMe disable/enable、admin queue、Identify（已完成）；
4. I/O queue、Read/Write/Flush 与有界轮询（已完成）；
5. PRP list、四槽完成、MSI-X、超时/EIO reset 与故障矩阵（已完成）；
6. rootfs/swap 迁移、ATA 回退、三档 QEMU 与重启持久化（已完成）。

**退出条件**

- 单元、集成和十万步随机请求模型覆盖所有几何、深度、乱序和失败边界；
- 自研 NVMe 驱动在 QEMU 真实完成 Identify、数据读写、Flush 和重启持久化；
- rootfs、swap、页缓存和 journal 不包含控制器分支；
- ATA 启动/回退与 NVMe 正常路径使用同一上层镜像语义，故障不会错误报告稳定；
- 全量 caw 回归零警告、零失败，主工程仍只标记工程候选。

### v2.8 统一文件页缓存与写回回收

**范围**

- 以稳定文件身份和 64 位 page index 建立动态稀疏缓存地址空间；
- buffered read/write、ELF/file fault 与 `MAP_SHARED` 最终共享同一权威页面；
- Dirty/Writeback/Error 页面支持有界后台写回、按文件同步和错误推进；
- clean file page、dirty writeback、匿名 swap 与 OOM 接入同一内存压力顺序；
- rootfs v4、NVMe/ATA 选择和启动链不因缓存迁移改变；第五增量只把 ABI 从 2.3.0
  追加升级到 2.4.0，旧 1..84 编号与错误区间不变。

**增量顺序**

1. 64 路动态 radix、`FileCacheAddressSpace`、状态/引用契约（已完成）；
2. buffered read、ELF/file fault 与只读 shared 迁移（已完成）；
3. buffered write、writable shared、truncate 与失效一致性（已完成）；
4. 后台 writeback、Dirty 软硬水位和回压（已完成）；
5. `fsync`/`fdatasync`/`msync` 与 writeback error sequence（已完成）；
6. file reclaim、swap、direct reclaim 和 OOM 集成矩阵（已完成）。

**第一增量完成状态**

`SparsePageIndex` 每层解释 6 bit，最多 11 层覆盖完整 `uint64_t`；Present、Dirty、
Writeback、Error bitmap 在父节点聚合。插入先取得完整缺失路径，失败释放未发布
节点；删除从叶到 root 裁剪空分支。`FileCacheAddressSpace` 动态拥有页元数据并冻结
映射引用和状态转换，但尚未替换生产 `FilePageCache`。

宿主单元覆盖全部层边界和 2 KiB 堆失败回滚；生命周期测试动态持有 8192 页；
固定种子随机模型执行十万步；目标 Kernel 又在真实 Heap 上覆盖最高索引与失败路径。

**第二增量完成状态**

生产 `FilePageCache` 已删除固定 BSS entry 数组，改用文件身份注册表和动态地址空间。
rootfs/legacy 的 VFS 普通读取、ELF header 读取和后续 file fault 使用同一缓存页；
backend fill 显式走 `ReadUncachedAt`，procfs/memfs/devfs 不缓存。早期增量曾观察
2/8/32 MiB 自适应 metadata arena；当前唯一 4 GiB 验收规格使用 32 MiB 和 8192 页。

buffered write 仍在提交前撤销映射并失效 clean 页；writable shared 仍使用该动态
cache 的 dirty/writeback 状态，因此一致性保持但尚未达到 write-through page cache。

**第三增量完成状态**

VFS cache capability 已扩展为 read/write/size/truncate 四个 hook；公共写按页取得
唯一 frame，在修改前通过 Dirty hard limit，并用独立 retained writeback file 保证
原 fd 关闭或 unlink 后仍能提交。缓存逻辑长度覆盖后端 `stat`，写回按 EOF 裁短最后
一页并显式调用 `WriteUncachedAt`，不会递归回写缓存。

普通写不再 revoke/invalidate。既有只读和 writable shared PTE 直接观察同一 frame；
private COW 保持隔离。truncate 仅撤销 EOF 后驻留页，丢弃范围外 Clean/Dirty/Error，
清零保留尾页；扩大只建立逻辑零区间。`sync` 继续先写保护 shared alias，再写回并
释放 clean writeback 引用，最后进入 rootfs journal 与设备 Flush。

**第四增量完成状态**

cache miss 先发布唯一 Loading entry 和 frame，再释放全局 cache lock 执行后端 fill；
成功后原子转为 Clean，失败丢弃 Loading 并归还 frame。重入或未来并发观察者只会看到
Busy，不会重复读取或发布第二个 frame。

Dirty hard limit 从 50% 收紧为约 20%，后台阈值由其一半得到约 10%，worker 目标为
约 5%。软水位只合并一次 pending 请求；单 BSP 不伪造 Kernel Thread，而是在返回
用户态的非 IRQ 安全点执行最多 64 页的有界批次。每批前重新写保护全部 writable
shared alias；硬水位在下一次普通写前先平衡。后台失败保留 Error、暂停自动重试，
显式 sync 才重新尝试。

**第五增量完成状态**

`FileWritebackErrorTracker` 从页缓存专用 KernelHeap 动态维护“文件身份、当前错误序列、
打开实例引用”；写回失败只对当时已有的独立 FileDescription 可见。独立 open 分别
采样游标，duplicate/fork 因共享 FileDescription 而共享游标；错误报告后推进，历史错误
不污染后来打开的实例。

`FilePageCache::WritebackFile` 只选择指定文件和 page-index 范围。ABI 2.4.0 在旧编号
84 后追加 `fsync`、`fdatasync`、`msync` 三项；同步调用按“写保护 shared PTE、范围
writeback、错误游标推进、VFS/设备 Flush”执行。`MS_ASYNC` 只强制挂起后台请求，
`MS_SYNC` 等待范围稳定；private 映射不进入写回。当前 rootfs 对 fdatasync 安全地执行
完整 metadata Flush，语义正确但尚无相对 fsync 的 I/O 优化。

**第六增量完成状态**

`ExecuteMemoryReclaim` 固定 clean file、dirty writeback/reclaim、anonymous swap 三阶段，
每个阶段独立返回失败，无实际进展时停止。生产驻留分配统一返回类型化结果；写回设备
失败和 swap 失败不会错误进入 OOM，成功回收后重新同步真实页帧账本并只重试一次。

ProcessRuntime 提供全局 shared PTE 写保护、跨进程轮转 swap 和 OOM 回调。PID1 被排除，
单线程进程的活动用户返回栈页受保护，多用户栈进程本轮跳过；只有所有阶段仍不能满足
请求时才选择 OOM victim。专用压力机仍使用 4 GiB `-mem-prealloc` 和真实 28 GiB swap，
在 PID1 建立后把逻辑驻留预算降到 9216 页，完整运行同一用户环境与探针。

**退出条件**

- 4 GiB 配置可缓存超过 4096 页，容量不再由 Kernel BSS 固定数组决定；
- lookup 不扫描全部驻留页面，同一文件页只有一个权威身份；
- buffered read/write 与 mmap 对修改、truncate 和失效观察一致；
- 写回失败保持 Error，按文件同步能向正确的打开实例报告新错误；
- 低水位依次尝试 clean 回收、dirty 写回和匿名 swap，耗尽后才选择 OOM；
- 4 GiB QEMU、NVMe/ATA fallback、重启持久化和故障矩阵全部保持通过。

### v2.9 内核后台执行与工作集回收

**范围**

- 让调度器原生表达不属于用户 Process 的 Kernel Thread；
- 建立 WorkQueue，并把 writeback 从 user-return safe point 迁移到可睡眠 worker；
- 用 x86 PTE Accessed 位形成 file/anonymous 冷热队列；
- 低水位唤醒后台回收，高水位停止，direct reclaim 只保留紧急路径；
- 不增加网络、SMP、图形或无关硬件驱动。

**增量顺序**

1. Kernel Thread 生命周期、协作上下文切换和安全回收（已完成）；
2. WorkQueue、延迟任务、合并、取消与 drain（已完成）；
3. writeback worker 迁移和按时间老化（已完成）；
4. PTE Accessed 采样与 active/inactive 队列（已完成）；
5. 后台水位回收线程（已完成）；
6. direct/background reclaim、swap 与 OOM 压力矩阵（已完成）。

**第一增量完成状态**

`ThreadEntry` 已显式区分 User 与 Kernel。Kernel Thread 不进入 Process 线程链，不拥有
用户 CR3/栈/TLS/信号状态，并使用独立高位 TID，因而 PID1/TID1 不变。动态内核栈顶部
预构造协作上下文；汇编在 IF 关闭时切换 RSP，FXSAVE 仍由 C++ 运行时显式管理。

PID1 创建前会运行两个真实 Kernel Thread，依次完成两次 yield、一次 WaitQueue
block/wake 和两次 exit；恢复调度栈后再 reap 并销毁目标栈。第一增量明确不让 Kernel
Thread 与 User Thread 同批运行，也不迁移 writeback，为 WorkQueue 保留一个可验证的
最小基础。

**第二增量完成状态**

`WorkQueue` 使用调用方长期持有的 entry/heap 存储。注册得到 generation handle；即时任务
进入 FIFO，延迟任务进入 deadline/sequence 最小堆。重复提交合并，Queued/Delayed 可
取消，Running 拒绝异步撤销；Completed/Cancelled 经 reset 后才能重排或 release。

`BeginDrain` 封闭新注册和 Idle 提交，已有任务失败不会停止 drain。任务 operation 只由
worker 在队列锁外执行。PID1 前的真实 worker 执行三个成功任务和一个失败任务，继续
完成延迟项，最后释放全部六个句柄。

**第三增量完成状态**

生产 dispatcher 现在识别当前 ThreadKind：User→Kernel 先收束用户页表、CpuLocal、
SYSCALL/IRQ 入口并回到 dispatcher，Kernel→User 保存 Kernel RSP 后同样回到 dispatcher，
同类型切换仍走原有快速路径。Ring 0 timer 不直接抢占 Worker。

常驻文件回写 Worker 以一个稳定 WorkHandle 工作。后台请求即时入队；仅有低水位 Dirty
页时按 Linux 默认 5 秒窗口延迟，新的即时请求会提升 Delayed 项。Worker 用 scheduler
deadline park/unpark，每批最多写 64 页后 yield。最后一个 User Thread 退出时取消残余
任务并停止/reap Worker。硬 Dirty limit、显式同步和退出前 flush 仍是同步前进保证。

**第四增量完成状态**

`PageTableManager::TestAndClearAccessed` 读取 4 KiB 用户叶项的硬件 A 位，只清该位并在
目标 CR3 当前活动时执行 `invlpg`。周期 WorkItem 每秒先登记全部 file-cache frame，再
扫描拥有 CR3 的用户 VMA；同一物理帧的 alias 对 Accessed 做 OR、对回收资格做 AND。

`PageAgingManager` 以物理帧和 File/Anonymous 分类为身份，维护 active-file、
inactive-file、active-anonymous、inactive-anonymous 四条队列。新页先进入 Active；一次
未访问降到 Inactive，连续第二次未访问且资格成立才记录 candidate；重新访问则提升。
跨轮物理帧复用允许重分类，同轮两类并存仍判为损坏。Zombie 无 CR3 时跳过，PID1、
UserStack、COW alias、映射中的 file page、Dirty/Writeback/Error 只参与温度观察，不成为
候选。本增量不释放、写回或交换候选。

元数据使用真实 frame+KVA 常驻分配：4 GiB 功能档为 4096 entry/8192 hash，32 GiB
容量档为 32768/65536。两档均在 Process 资源基线前建立，不占 Kernel BSS，也不伪造
resident 计数。

**第五增量完成状态**

`BackgroundReclaimController` 用 Sleeping/Running/BackingOff 表达 low 唤醒、high 停止和
一秒退避；每个决策最多 64 页。用户分配在 low 到 min 之间只合并后台 WorkItem，低于
min 才进入 direct reclaim。Worker 依次处理显式 cold clean file candidate、dirty/error
writeback 和 cold anonymous candidate，每批后 yield；IRQ 和 WorkQueue 锁内不执行扫描
或 I/O。

PageAging candidate 已从派生条件改成显式提交状态，刚降级的页必须再冷一轮。文件缓存
access generation 改变会撤销同类旧候选，selection 再核对当前 generation；成功 eviction/
swap 在 frame 释放前 completion 并忘记 aging 身份。停止时三个持久 handle、controller、
deadline、WaitQueue 与当前四队列一起归零。

4 GiB ATA/NVMe pressure 已分别以 39.00/40.06 秒通过稳定门禁。ATA 首次有效轨迹观察
wake/sleep 9/9、24 批、clean 588 页、writeback 329 页、anonymous 36 页和实际回收
624 页；后台 failure 为零。该第五增量当时尚未冻结 direct/background 公平配额，后续由
第六增量的统一 planner 与 OOM 组合矩阵完成。

最终 CAW Debug `verify` 为 215/215、0 失败：65 unit、72 integration、47 randomized、
31 system，含 25 条 failure-path，CTest 374.98 秒；全量 ATA/NVMe pressure 为
38.40/41.19 秒，primary 为 36.49/36.84 秒，持久化为 72.49/74.89 秒。

**第六增量完成状态**

direct 与 background 现在共同调用 `PlanMemoryReclaim`。file/anonymous 配额按 Linux
同范围的 0..200 swappiness 计算；两类均可用时保留最小公平份额，某类不足则把未用
预算转赠另一类。file 预算内仍固定 clean-first、writeback-second。匿名 frame 的最后
引用在 unmap/exec/exit/OOM 或 swap completion 后都会删除 aging 身份。

新增 12288 页、swappiness 0 的 OOM profile。`oom_probe` 从 `/proc/meminfo` 读取实时
resident headroom，保证匿名子进程比制造压力的父进程大 64 页，确定性验证非当前 victim
SIGKILL、wait/reap、PID1 存活和全部资源归零。4 GiB ATA/NVMe OOM 分别 40.19/40.93 秒，
reclaim-pressure 分别 39.05/39.52 秒；两种 OOM 路径均为一次 invocation/kill、零
anonymous swap，并到达三项状态验证和 READY。

最终 CAW Debug `verify` 为 218/218、0 失败：65 unit、73 integration、47 randomized、
33 system，含 25 条 failure-path，CTest 476.96 秒；ATA/NVMe persistence 为
77.80/79.08 秒。V2.9 实现完成，但仍保持工程候选且不启动公开发布闭环。

**退出条件**

- Kernel Thread 不改变 Process、PID1、用户 TID 和 ABI；
- create/yield/block/wake/exit/reap 的正常与失败路径有纯模型和 QEMU 证据；
- 完成后 scheduler entry、CpuLocal、动态栈、KVA 与 frame 恢复基线；
- 4 GiB ATA/NVMe、持久化和既有故障矩阵保持通过；
- 常规 writeback 已迁移；IRQ 不执行 VFS I/O，硬压力 direct fallback 必须继续守恒。
- aging candidate 由第五、六增量消费；第四增量的历史边界仍不得回写。

### v2.10 异步块 I/O 与可等待页缓存

**范围**

- 把已有 BlockRequest 多深度模型提升为设备无关的异步提交/完成契约；
- 让 ATA/NVMe、rootfs、swap 和页缓存通过 WaitQueue 睡眠等待，不轮询、不返回瞬态 Busy；
- 合并同页 Loading，加入有界顺序预读，并让 writeback/reclaim 与并发 I/O 保持守恒；
- 不增加网络、SMP、图形和无关驱动，不改变 rootfs v4 与 ABI 2.4.0。

**增量顺序**

1. BlockRequest 有序 completion FIFO 与 owner 交付（已完成）；
2. 异步 BlockDevice adapter 与 ATA/NVMe 统一接口（已完成）；
3. BlockIo WaitQueue 与生产 rootfs/swap 迁移：3a 协调器/Worker/Kernel 等待、3b 栈式
   User Kernel 续体、RuntimeMutex 锁拆分和生产迁移均已完成；
4. FilePageCache Loading waiter 与同页 miss 合并（已完成）；
5. 顺序预读、命中/浪费反馈与压力收缩（5a 纯策略、5b 生产执行、5c 取消/反馈均已完成）；
6. 并发 writeback/reclaim 和 ATA/NVMe 错误、持久化矩阵（已完成）。

**第一增量边界**

完成路径按实际解析顺序进入独立有界 FIFO；`TakeCompletion` 返回 request id、操作、LBA、
块数、owner 与结果并立即回收槽。IRQ/timeout/cancel 仍由单赢家状态机解析，按 id 直接
Reap 必须从 FIFO 中间安全摘除。ATA 先消费该公共出口；同步 BlockDevice 和 rootfs 迁移
留给后续增量。

第一增量最终 CAW Debug `verify` 为 218/218、0 失败：65 unit、73 integration、
47 randomized、33 system，含 25 条 failure-path，CTest 453.70 秒；4 GiB ATA/NVMe
primary、reclaim、OOM、persistence 与既有错误恢复协议保持通过。

**第二增量边界**

同步 `BlockDevice` 旁新增静态类型擦除 `AsynchronousBlockDevice`；ATA/NVMe namespace
统一 geometry、submit、best-effort cancel、timeout 和 completion。NVMe 公共 request id
与硬件 CID 分离，Read DMA 回拷留在非 IRQ take 阶段，timeout reset 保留所有已解析完成。
生产 rootfs/swap、WaitQueue 和页缓存 Loading waiter 仍留给后续增量。

第二增量最终 CAW Debug `verify` 为 221/221、0 失败：66 unit、74 integration、
48 randomized、33 system，含 25 条 failure-path，CTest 472.08 秒；ATA/NVMe primary、
reclaim、OOM、persistence 与 EIO/timeout reset 全部保持通过。

**第三增量基础边界**

`BlockIoCoordinator` 以 owner、request id 和 generation ticket 管理登记、等待、完成、遗弃
与槽位复用；completion-before-wait 不丢唤醒。常驻 Kernel Thread 在非 IRQ 上消费
`AsynchronousBlockDevice` completion、执行必要的数据收尾并精确唤醒 owner；IRQ14、IRQ15
和 timer 只解析状态与通知。secondary ATA Flush probe 已真实走完 submit、IRQ15、Worker、
WaitQueue 和结果回收。

生产 rootfs/journal/cache 和 swap 本增量仍使用同步回退。原因不是设备接口缺失，而是这些
深层调用持有 spin lock，用户系统调用阻塞还会展开 C++ 栈；直接打开异步包装会形成锁内
睡眠或失效 buffer。下一增量先引入浅层 I/O worker 委托并拆分锁临界区，再迁移生产
read/write/flush。该边界由
[ADR 0065](adr/0065-v2-10-block-io-kernel-wait-and-migration-boundary.md) 冻结。

第三增量 3a 最终 CAW Debug `verify` 为 224/224、0 失败：67 unit、75 integration、
49 randomized、33 system，含 25 条 failure-path，CTest 460.56 秒。4 GiB primary、
ATA/NVMe reclaim 为 37.07/38.01/41.06 秒，ATA/NVMe persistence 为 74.10/76.16 秒。

**第三增量生产迁移边界**

3b 为 User Thread 保留可恢复的 Kernel stack 续体，并成对保存 FX、系统调用入口、GS 与
CR3 模式；`RuntimeMutex` 在运行期竞争时睡眠，在 early boot/IRQ 不可睡眠边界退化为短
spin lock。rootfs、VFS、BlockCache、文件后备和 swap 完成锁迁移，root/swap
`BlockIoDevice` 打开异步等待。Thread 以 `Initializing -> Ready` 发布，退出以
Scheduler `Zombie -> ProcessTree event` 发布，关闭半初始化运行和 wait 半提交窗口。

普通 4 GiB ATA primary 必须观察非零 root async operation，且 BlockIo registration、wait、
completion 严格相等；匿名换出压力 profile 还必须观察非零 swap async operation。early
boot 和受限 Kernel worker 保留同步回退。设计由
[ADR 0066](adr/0066-v2-10-stackful-user-kernel-continuation-and-runtime-mutex.md) 冻结。

3b final fresh CAW `verify` 为 224/224、0 失败：67 unit、75 integration、49 randomized、
33 system，含 25 条 failure-path，CTest 868.49 秒。4 GiB ATA primary 69.79 秒，ATA/NVMe
reclaim 73.46/75.36 秒，ATA/NVMe persistence 138.87/138.73 秒。v2.10 仍为未发布工程候选。

**第四增量边界**

`FilePageLoadCoordinator` 用固定槽、per-thread waiter、per-slot WaitQueue 和 generation token
保存同一次 Loading 的全部观察者。冲突线程在 cache lock 内登记后才解锁；owner 在同一
锁内冻结 waiter 数，为每个 waiter 预留真实 page reference，再完成广播。完成先于等待
提交时直接取结果，等待先于完成时精确唤醒；失败 entry 已撤销后，同一错误仍保存到最后
一个 waiter 领取。

early boot、受限 Kernel worker 和调度停止期继续保留 Busy，不伪装成可睡眠上下文；
Loading 的 truncate/invalidate/reclaim/writeback 边界不放宽。强制重叠 host integration
验证 owner 先释放后 waiter 引用仍阻止淘汰，固定种子十万轮模型覆盖 0..7 waiter 的成功、
失败和随机领取。设计由
[ADR 0067](adr/0067-v2-10-file-page-loading-waiter-and-reference-handoff.md) 冻结。

第四增量 final fresh CAW `verify` 为 228/228、0 失败：68 unit、77 integration、
50 randomized、33 system，含 25 条 failure-path；CTest 907.05 秒。4 GiB ATA primary
75.96 秒，ATA/NVMe reclaim 78.95/80.12 秒，ATA/NVMe persistence 143.21/140.14 秒。

**第五增量 5a 边界**

`FileReadaheadPolicy` 已把每个未来打开文件流的 start/size/async tail、触发页和 generation
冻结为无分配纯状态。默认上限 32 页；单页顺序流按 4、8、16、32 增长，随机访问清除
窗口。Balanced/BelowHigh/BelowLow/BelowMinimum 分别采用全量、1/2、1/4、关闭上限；浪费
反馈减半，有用反馈翻倍恢复。EOF 裁剪、非法触发和所有计数失败保持原状态。

5a 不接 FileDescription、不提交 WorkQueue、不创建 Loading，也不增加 QEMU marker。5b
负责打开实例所有权、异步 Worker 和 FilePageCache 执行；5c 再接预读页身份、实际命中/
浪费归因、close/truncate/invalidate/reclaim 取消。设计由
[ADR 0068](adr/0068-v2-10-per-open-file-readahead-policy.md) 冻结。

5a final fresh CAW `verify` 为 231/231、0 失败：69 unit、78 integration、51 randomized、
33 system，含 25 条 failure-path；CTest 888.82 秒。4 GiB ATA primary 71.51 秒，ATA/NVMe
reclaim 79.25/73.06 秒，ATA/NVMe persistence 142.10/141.47 秒。

**第五增量 5b 边界**

共享 FileDescription 现在拥有策略，VFS 缓存读取返回真实页级 hit/miss/prefetched-hit。
decision 经 64 槽 generation FIFO 转交常驻 Kernel worker；请求持有 retained OpenFile，
退出先排空再停止 worker。FilePageCache 的 Demand/Prefetch intent 共用唯一 Loading，新预取
Clean 页用 one-shot 标记归因 useful，未消费失效/裁剪/回收归因 waste。

预读 worker 只开放 Loading owner 能力，不等待已有同页 Loading；缓存满或 BelowMinimum
停止预测，不驱逐 demand 页。队列满只拒绝预测，不改变 demand read。queue、cache 和
FileDescription 的 unit/integration/十万步 randomized 加上 4 GiB QEMU 真实工具 ELF 顺序读
共同证明 schedule/enqueue/completion 守恒、loaded/useful/hit 非零、失败/active/最终驻留为
零。设计由
[ADR 0069](adr/0069-v2-10-production-readahead-execution.md) 冻结。

5b final fresh CAW 构建与 CTest 覆盖 234 项：70 unit、79 integration、52 randomized、
33 system，含 25 条 failure-path。严格完整轮次 233/234，唯一异常为 ATA reclaim 的一次
QMP VGA 非追加快照；同一 fresh 构建按原验收器重试 75.17 秒通过。4 GiB ATA primary
72.42 秒，NVMe root primary 70.08 秒，ATA/NVMe OOM 77.43/76.30 秒，ATA/NVMe
persistence 144.32/142.86 秒。

**第五增量 5c 边界**

固定 4096 槽 FeedbackLedger 为每个共享 FileDescription 分配 generation token；请求和缓存页
携带 token/policy generation。Demand、定向取消、truncate、invalidate、reclaim/trim 的
useful/waste 进入 producer 账本，存活策略在 read/close 领取。queued 请求稳定摘除并释放
OpenFile/task retain，running 每页检查取消，最后共享描述 close 进入 Retiring 后等 task
归零再复用槽。random reset 只取消旧 generation，BelowMinimum/close 取消全部。

5c 的 unit、两组固定种子十万步 randomized、cache/FileDescription integration 与 4 GiB
QEMU 联合验证 terminal、task、feedback 和 stale 守恒。设计由
[ADR 0070](adr/0070-v2-10-readahead-cancellation-and-feedback-ledger.md) 冻结。final fresh
CAW `verify` 为 236/236、0 失败：71 unit、79 integration、53 randomized、33 system，
含 25 条 failure-path；CTest 1038.99 秒，端到端 1198 秒。4 GiB ATA primary 86.47 秒，
ATA/NVMe reclaim 85.05/82.94 秒，ATA/NVMe OOM 88.71/85.99 秒，NVMe root primary
83.06 秒，ATA/NVMe persistence 177.10/169.12 秒。

**第六增量完成状态**

`FilePageWritebackCoordinator` 按 Thread capacity 固定提供 writeback 槽、per-thread waiter 与
per-slot WaitQueue。Dirty/Error 页进入 Writeback 时分配独立 writeback generation；同页 writer
和同步调用在 cache lock 内登记，再锁外等待。成功 writer 只有在旧 I/O 完成后才能重新
脏化，fsync/fdatasync/同步 msync 则继续扫描范围；失败向所有同期 waiter 广播同一结果。

Clean reclaim 不等待当前 Writeback，可以回收其他无引用候选；脏页 direct/background
reclaim 经公共 writeback 等待后再回收。Loading 和已取消预读仍等待已提交 BlockIo 的唯一
终态后收束。已签发 ATA/NVMe 命令保持 best-effort cancel，不把无法保证的硬件 abort 写成
系统能力。

unit、WaitQueue integration、具名固定种子十万步 randomized 和强制 `std::thread` 交错分别
验证完成先行、失败广播、旧 token、同页重新脏化、其他 Clean 页并发回收和 Error 重试。
公共异步块模型的 success/EIO/timeout/cancel 与 4 GiB ATA/NVMe primary、reclaim、OOM、
设备恢复和 persistence 共同形成分层故障矩阵。设计由
[ADR 0071](adr/0071-v2-10-file-page-writeback-wait-and-failure-matrix.md) 冻结。

final fresh CAW `verify` 为 240/240、0 失败：72 unit、81 integration、54 randomized、
33 system，含 25 条 failure-path；CTest 933.09 秒，端到端 1085 秒。4 GiB ATA primary
73.90 秒，ATA/NVMe reclaim 81.34/80.23 秒，ATA/NVMe OOM 78.05/76.97 秒，NVMe root
primary 72.26 秒，ATA/NVMe persistence 162.50/144.34 秒。

V2.10 六个实现增量至此完成，但按用户要求继续保持工程候选，不发布版本标签。

**退出条件**

- completion 不丢失、不重复、不因乱序硬件完成改按 identifier 排序；
- request、buffer、deadline 与 owner 的所有权在 submit 到 completion 之间唯一；
- IRQ 路径不分配、不阻塞、不进入 VFS；
- 同页 miss 最终只执行一次来源 I/O，所有 waiter 观察同一成功或失败；
- ATA/NVMe primary、reclaim、OOM、persistence 与 EIO/timeout 全部保持通过。

### v2.11 VFS 元数据缓存与可扩展路径解析

**范围**

- 以 Linux dcache/inode cache 的核心语义减少重复组件 lookup 和 stat 后端访问；
- Positive、Negative、Stale、引用、generation 与 LRU 必须覆盖 mount 和命名空间修改；
- 缓存只作为 VFS 加速层，不能改变 rootfs v4、打开文件、orphan、DAC 或符号链接语义；
- 不增加网络、SMP、图形和无关硬件驱动，不改变 ABI 2.4.0 与 4 GiB/128 GiB 规格。

**增量顺序**

1. dentry/inode identity、Positive/Negative/Stale、引用、generation 与 LRU 纯模型（已完成）；
2. inode metadata cache 与 stat/open/exec 共享（已完成）；
3. Positive dentry 生产 lookup 与同组件 miss 合并（已完成）；
4. Negative dentry、NotFound/EIO 分离和创建失效（已完成）；
5. create/link/rename/unlink/symlink/mkdir/rmdir、mount crossing 与 cwd/root 一致性（已完成）；
6. hash/LRU shrinker、内存压力、ATA/NVMe 错误和持久化矩阵（已完成）。

**第一增量完成状态**

`VfsNamespaceCache` 使用调用方提供的固定 dentry/inode 槽。dentry key 是 mount、parent
superblock/node generation 与完整 1..255 字节名称；inode identity 不含 mount，因此不同
mount 的同一 vnode 可以共享 inode。Positive dentry 持 inode generation token，Negative
不持 inode；Cached 失效后从 lookup 消失，但有引用的旧项保留为 Stale，允许同 key 新项与
旧 token 并存。

inode 分开保存 dentry/external 引用。inode 失效同时撤销所有指向它和以它为 parent 的
Cached 正负 dentry；Stale 资源等全部引用归零后释放。dentry LRU 只回收零外部引用项，inode
LRU 还要求零 dentry 引用。第一增量只验证线性纯模型，不接 `Vfs::Resolve`、不减少后端 I/O、
不新增运行期日志。设计由
[ADR 0072](adr/0072-v2-11-vfs-namespace-cache-identity-and-lifecycle.md) 冻结。

**第二增量完成状态**

inode slot 已加入 backend 原始 metadata、Empty/Loading/Ready 和独立 generation ticket。
失效会使旧 owner 的迟到 completion 永久无效；Loading 竞争者、容量或 generation 耗尽
只旁路 backend，不把缓存状态变成新的系统调用失败。

生产 VFS 已让 stat、权限检查、普通/目录/exec 打开、sticky/创建检查和打开文件 stat
共享同一 identity。chmod/chown/write/truncate/create/link/rename/unlink/rmdir/symlink 成功后
按 target/parent 失效；页缓存逻辑 size 在返回时覆盖原始缓存。内核固定配置 4096 dentry
和 2048 inode 槽，不新增热路径分配或来宾日志。设计由
[ADR 0073](adr/0073-v2-11-inode-metadata-load-and-invalidation.md) 冻结。

**第三至第五增量完成状态**

生产 path walk 已命中 Positive/Negative dentry，只有 miss 访问 backend。NotFound 才发布
Negative，EIO 等错误保持可重试。resolution transaction 让同 key 并发 miss 只有一个 owner；
create/link/rename/remove/symlink 的 key 与 metadata 在 backend commit 后同事务失效。
mount crossing、cwd/root、符号链接和打开文件 generation 继续由原 VFS 语义裁决。设计由
[ADR 0074](adr/0074-v2-11-production-dentry-lookup-and-namespace-mutation.md) 冻结。

**第六增量完成状态**

4096 dentry/2048 inode 固定槽分别使用 8192/4096 bucket 的调用方 hash backing。Cached
项入 index，Stale 只保留旧 token；LRU 与 background pressure shrinker 回收可重建逻辑
条目，但不把固定 BSS 计作物理页回收。生产与两个十万轮 oracle 全部启用 hash。设计由
[ADR 0075](adr/0075-v2-11-namespace-hash-lru-and-pressure-shrinker.md) 冻结。

V2.11 六个工程增量至此闭合；按既有用户要求继续保持未发布候选，不创建公开 tag。

**退出条件**

- 热路径重复组件解析不再访问后端，Negative 不能掩盖 EIO；
- rename/unlink/create 后不存在陈旧命中，旧打开对象仍按 generation 保持生命周期；
- mount crossing、root/cwd、符号链接、DAC 和 orphan 语义不因缓存改变；
- dentry/inode 元数据可由内存压力回收，引用项和进行中的 lookup 不能被释放；
- ATA/NVMe primary、错误恢复、reclaim/OOM 与三启动 persistence 保持通过。

### v2.12 可扩展、页后备 VFS 命名空间

**范围**

- 64 shard dentry/inode waiter 替代全局读侧事务，128 个解析上下文允许无关 path walk 并行；
- 单写 namespace mutation 以偶/奇 sequence 让并发 resolver 检测提交并有界重试；
- 4096/2048 固定容量保持不变，但 slot、index、bucket 和 scratch 使用真实内核页；
- preferred 8192/4096 bucket 在首次 pressure 后重建到 compact 4096/2048 并归还物理页；
- 不改变 ABI、rootfs v4、4 GiB/128 GiB 规格，不引入网络、SMP 或图形。

**六个增量（全部完成）**

1. lookup waiter 分片；
2. 独立 resolution context 与无关路径并行；
3. rename/unlink/create/mount sequence retry；
4. 溢出安全的 page-backed layout 与真实 frame/KVA 所有权；
5. 在线 hash rebuild、compact tier 和 preferred 页释放；
6. concurrency、mutation、layout、pressure 与资源账本完整矩阵。

设计由 [ADR 0076](adr/0076-v2-12-scalable-page-backed-vfs-namespace.md) 冻结。V2.12 只形成
工程候选，不创建公开 tag。

**退出条件**

- 同 key miss 合并、不同 shard/metadata miss 并行，mutation 跨窗结果必须重试；
- backing 每一页都来自真实分配，释放数与 frame/buddy/KVA 账本一致；
- 9216 页压力规格不得因长期 namespace 页而漂移；pressure 必须触发 compact rebuild/release；
- host oracle、4 GiB primary/reclaim 和完整 fresh CAW verify 全绿。

### v2.13 目录句柄与 `*at` 路径事务

**范围**

- 用稳定目录 vnode 句柄表达 cwd 之外的相对路径基准，目录 rename 后句柄仍有效；
- source/destination 可使用不同 `dirfd`，权限、root clamp、mount 和符号链接仍由 VFS 统一；
- namespace writer 从解析前覆盖到 backend commit/cache invalidation，并复验 expected sequence；
- ABI v2.5.0 在 87 后追加九项基础 `*at` 调用，不改变 rootfs v4 与磁盘格式；
- 保持 4 GiB/128 GiB、单 BSP、VGA 终端与无网络/无无关驱动边界。

**六个增量（全部完成）**

1. `DirectoryHandle` retain/release 与 `ResolveAt`；
2. 单目录 VFS `*At` 操作；
3. writer 全路径串行、expected sequence 与同名 create 复验；
4. ABI v2.5.0 的 88..93、请求布局和失败边界；
5. 双 parent 94..96、目录 rename 后句柄和 Ring 3 rootfs 路径；
6. 生命周期统计、并发、ABI、VGA、资源与完整 fresh CAW 回归。

设计由 [ADR 0077](adr/0077-v2-13-directory-handles-and-at-path-transactions.md) 冻结。V2.13
继续保持工程候选，不创建公开 tag。

**退出条件**

- close/duplicate/fork 与并发 `*at` 之间不存在悬空 FileDescription/VFS handle；
- 双 parent mutation 不提交基于过期 namespace sequence 的解析结果；
- ABI 1..87、错误区间、rootfs v4 和旧用户工具保持兼容；
- hosted 强制交错、Ring 3 rootfs、4 GiB primary 与完整 fresh CAW verify 全绿。

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
- 4 GiB `-mem-prealloc` 主规格必须实际触及 4 GiB 以上的 PCI-hole 重映射地址。

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

## v3 以后

网络、SMP/AP、跨核调度、真机 ACPI/PCI/AHCI/NVMe、USB、图形桌面、高分辨率
framebuffer、音频、AVX/XSAVE、动态链接、共享库、容器和自举编译器不进入
v2.1 至 v2.6。它们需要独立的硬件、ABI 和故障模型，留给 v3 重新排序。

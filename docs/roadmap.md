# 开发路线

## 文档定位

本文是项目版本排序、范围和验收标准的唯一当前来源。架构选择由 ADR 解释，
模块细节由 `docs/modules/` 维护，已经发布的版本证据保存在 `docs/releases/`。
若本路线与历史文档冲突，以
[ADR 0018](adr/0018-v2-program-rebaseline.md) 和本文为准。

每个版本都必须形成一条能够独立解释的学习闭环：

```text
问题与历史背景
  → 契约、状态机和所有权设计
  → 最小实现
  → 单元/集成/随机/产物/QEMU 证据
  → 失败路径和资源守恒
  → 文档、教材和网站同步
```

版本号不是进度百分比。一个版本只有在全部退出条件通过、证据可复现且没有把
关键状态留给下一版本收尾时，才允许标记完成。

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

第一周期的详细实现与验收不在本文重复展开，见各模块文档、发布记录和教材。
v1.0 的 73 项自动化测试是第二周期必须持续保持的回归基线。

## 第二周期：从静态演示系统到可组合的类 Unix 系统

### v2.0 最终定义

v2.0 是由自研 ROM 和 Stage 1 启动、运行在单个 x86-64 BSP 上的多进程、
多线程类 Unix 教学操作系统。它必须能够从自研根文件系统执行 PID1、外部
Shell 和普通 ELF64 工具，并具备动态资源管理、按需虚拟内存、fork/COW、
动态描述符、线程同步、信号、TTY 和可恢复持久化。

“能力对齐现代 Linux”在本项目中具有严格边界：学习可扩展的身份、所有权、
资源限制和分层，不复制 Linux 的源码规模、全部 POSIX 接口或硬件覆盖面。

目标对象关系如下：

```text
Process ──共享──> AddressSpace / FileTable / FsContext / SignalState
   │
   └──拥有多个──> Thread ──拥有──> TID / CPU Context / Kernel Stack /
                                  User Stack / TLS / Signal Mask

FileTable[fd] ──引用──> FileDescription ──引用──> Vnode
AddressSpace ──拥有──> VMA ──描述意图──> anonymous / file / stack
PTE ──只描述当前驻留事实──> Physical Page / COW / permissions
```

### 三种正式测试配置

| 配置 | 内存 | 职责 | 不承担的职责 |
| --- | ---: | --- | --- |
| bootstrap | 64 MiB | 复位、启动链、异常、基础内存和历史失败镜像 | 完整用户环境与规模压力 |
| functional | 256 MiB | PID1、Shell、文件、VM、线程、信号和持久化全功能 | 64 GiB 高地址与最大规模 |
| capacity | 64 GiB | 全 RAM 管理、4 GiB 以上访问、容量与长时间压力 | 替代低内存兼容验证 |

64 GiB 是主验收规格而不是代码上限。实现上界由 E820、CPUID 物理地址宽度、
direct-map 和明确资源限制共同决定。

### v2.0 容量验收下限

这些数字是 64 GiB 配置的验收下限，不是 ABI 位宽或固定数组长度。256 MiB
配置可以使用更低 soft limit，但必须沿用相同动态数据结构和失败语义。

| 维度 | v2.0 下限 |
| --- | --- |
| 调度实体 | 同时存在至少 512 个 Thread，其中至少 256 个 Process |
| 单进程线程 | 一个 Process 内至少 64 个 Thread |
| 身份 | PID/TID 为独立 `uint64_t` 身份，不与对象地址或槽位绑定 |
| 描述符 | 分块动态表；默认 soft limit 256，hard limit 至少 4096 |
| 管道 | 至少 1024 条 64 KiB 管道，缓冲页按需分配 |
| 路径 | `PATH_MAX` 至少 4096 字节，单组件至少 255 字节 |
| 参数环境 | `argv` 与 `envp` 合计至少 128 KiB |
| 磁盘与文件 | 稀疏镜像至少 1 GiB，rootfs 至少 256 MiB，单文件至少 64 MiB |
| 用户工具 | 至少 32 个独立 ELF64 工具从磁盘执行 |
| Shell 组合 | 至少 16 级流水线，并支持重定向、环境和前后台任务 |

达到 soft/hard limit 必须返回明确错误，保持已有对象不变并完整回滚本次获得的
页、引用、描述符和磁盘资源。禁止以覆盖旧槽、静默截断或部分提交“通过”测试。

### 版本波次与依赖

| 波次 | 版本 | 解决的问题 |
| --- | --- | --- |
| A：资源与执行实体 | v1.1–v1.3 | 可回收内存、Process/Thread、对象与动态 fd |
| B：命名空间与程序来源 | v1.4–v1.6 | VFS、rootfs v2、PID1 和磁盘 exec |
| C：虚拟内存与组合 | v1.7–v1.9 | VMA/按需分页、COW、Unix I/O 和外部 Shell |
| D：并发与交互控制 | v1.10–v1.11 | 用户线程、futex、信号、TTY 和作业控制 |
| E：持久化与收口 | v1.12–v1.13 | 异步块层、journal、ABI 冻结和系统加固 |
| 发布 | v2.0 | 集成、容量、文档、发布证据；不增加主要机制 |

```text
v1.1 memory lifetime
  → v1.2 Process/Thread + syscall entry
  → v1.3 kernel objects + dynamic fd
  → v1.4 VFS on legacy-fs adapter
  → v1.5 rootfs v2
  → v1.6 PID1 + disk exec
  → v1.7 VMA + demand paging + runtime
  → v1.8 fork/COW
  → v1.9 Unix I/O + external shell
  → v1.10 threads/TLS/futex
  → v1.11 time/signals/TTY/job control
  → v1.12 async block/journal/recovery
  → v1.13 ABI freeze/hardening
  → v2.0 integration release
```

关键顺序不可颠倒：

- 先有 VMA 和统一页故障解析，再做 COW；否则无法区分匿名页、文件私有映射、
  只读页和真正的 COW 故障。
- 先在旧文件系统适配器上验证 VFS 语义，再改变磁盘格式；否则路径错误与磁盘
  布局错误无法被测试隔离。
- 先把 Process 与 Thread 分开，再开放用户线程；否则 PID、调度现场、地址
  空间和退出语义会混成一个对象。
- `SYSCALL/SYSRET` 骨架随 Thread 现场在 v1.2 建立，避免全部新 ABI 最后
  一次性迁移；`INT 0x80` 到 v1.13 才退出兼容角色。
- journal 在用户文件负载和页缓存语义稳定后加入，避免同时调试命名空间、
  缓存、异步完成和崩溃恢复。

## 波次 A：资源与执行实体

### v1.1 内存分配与资源生命周期

**目标**

消除低 64 MiB、单调堆和固定内核栈，让后续对象能够按真实生命周期申请、
失败回滚并释放资源。

**当前已完成增量**

- 通过 E820、CPUID 和 direct-map 管理全部可用 RAM；
- 64 GiB 主配置已在 4 GiB 以上完成页帧分配、读写和释放；
- 页帧 2-bit 元数据动态选址并避开平台、Kernel、栈和自身；
- 建立 `0xFFFF888000000000` 起始的 64 TiB 物理直映；
- 页表遍历、用户页清零和 ELF 复制统一通过 direct-map。

**剩余产出**

- 可分裂、合并、回收并检测重复释放的物理页分配器；
- 支持对齐、释放和统计的内核堆或类型化对象缓存；
- 动态 Ring 0 栈、guard page 和中间页表回收；
- 通用引用计数、作用域回滚工具与资源快照；
- 删除正常路径对四份 PCB、静态栈和固定资源池的依赖。

**退出条件**

- 固定种子模型至少完成 100000 次分配、释放、分裂、合并和耗尽操作；
- 释放保留页、重复释放、错误对齐和范围溢出均明确失败；
- 256 MiB 与 64 GiB 下反复创建/销毁对象后页帧、堆字节和对象计数回到基线；
- 64 MiB bootstrap 继续通过，64 GiB 必须实际触及 4 GiB 以上页；
- Kernel ELF 不新增 `.init_array`、隐藏分配或未解析运行时符号；
- v1.0 的 73 项回归保持通过。

### v1.2 Process/Thread 与可扩展调度、双系统调用入口

**目标**

让调度器调度 Thread，让 Process 成为共享资源容器，并在状态模型稳定前建立
x86-64 原生系统调用入口。

**产出**

- 独立的 `ProcessId` 与 `ThreadId`，均使用 `uint64_t`；
- Process 持有地址空间、文件表、文件系统上下文和共享信号状态；
- Thread 持有 CPU 现场、内核栈、调度状态、用户栈元数据和信号掩码；
- 动态运行队列、睡眠队列、等待原因和 Thread 创建/退出/回收；
- `SYSCALL/SYSRET` 入口骨架、每 Thread 内核入口栈和完整返回现场验证；
- `INT 0x80` 继续作为同一分发器的兼容入口，不复制系统调用实现。

**退出条件**

- 256 MiB 下至少 128 个内核 Thread 反复阻塞、唤醒、抢占和退出；
- 64 GiB 下至少 512 个 Thread 分布于至少 256 个 Process 容器；
- TID/PID 不因对象释放立即复用，不依赖数组下标；
- 两种入口对相同调用产生一致结果，非法 canonical RIP/RSP/RFLAGS 被拒绝；
- Thread 退出只回收私有现场，最后一个 Thread 退出才触发 Process 生命周期；
- 随机调度模型验证 Ready/Running/Blocked/Zombie 集合互斥和资源守恒。

### v1.3 类型化内核对象与动态描述符

**目标**

用可扩展对象和引用关系替代“fd 就是固定数组里的资源”，为 fork、dup、VFS
和线程共享文件表建立稳定所有权。

**产出**

- 类型化句柄或受控引用，禁止跨模块持有裸内部指针；
- 分块增长的 FileTable，独立 soft/hard limit 和最低可用 fd 分配；
- FileDescription 保存偏移、状态标志和底层对象引用；
- fd flag 与 file status flag 分离，预留 close-on-exec；
- Pipe、Console 和 legacy File 通过统一 I/O 对象契约接入；
- 引用获取、发布、关闭和失败回滚具有统一统计。

**退出条件**

- 默认 soft limit 256，提升限制后单进程可打开至少 4096 个测试对象；
- dup 型内部模型共享 FileDescription 偏移，独立 open 不共享偏移；
- fd 关闭后可复用，但陈旧引用不能访问新对象；
- 随机 open/duplicate/close/limit 模型至少 100000 步且计数归零；
- 描述符耗尽、内存不足和对象关闭竞态不产生部分安装或引用泄漏；
- 现有控制台、文件和管道的 QEMU 行为不退化。

## 波次 B：命名空间与程序来源

### v1.4 VFS 命名空间与旧文件系统适配

**目标**

先建立独立于磁盘布局的路径和文件对象语义，在旧文件系统适配器上验证 VFS，
不在同一版本同时更换命名空间和磁盘格式。

**产出**

- Superblock、Mount、Vnode、Dentry/Path 和 FileDescription 契约；
- 每 Process 的 cwd/root 文件系统上下文；
- 绝对/相对路径、`.`、`..`、根目录夹取和挂载点遍历；
- legacy-fs 适配器，使 v0.11 格式继续作为测试后端；
- create、mkdir、unlink、rename、truncate、stat 的 VFS 语义；
- 面向路径解析的正/负缓存和明确失效规则。

**退出条件**

- 4096 字节路径和 255 字节组件具有成功、超限与规范化测试；
- rename 覆盖、目录非空、`.`/`..`、根夹取和跨挂载错误语义明确；
- 路径解析参考模型与固定种子随机目录树结果一致；
- VFS 测试能够同时运行内存后端和 legacy-fs 适配器；
- Shell 不再直接调用具体 inode 或磁盘文件系统接口；
- QEMU 双启动继续读取旧格式数据，未知非零磁盘不得被静默格式化。

### v1.5 rootfs v2 与大文件磁盘格式

**目标**

在 VFS 契约不变的前提下替换磁盘布局，提供足以容纳程序、页缓存和用户数据的
大文件根文件系统。

**产出**

- 带新 magic/version、固定宽度字段和校验范围的 rootfs v2；
- 4 KiB 逻辑块、直接/间接索引、目录项和可扩展 inode；
- 至少 1 GiB 稀疏磁盘布局，启动区与至少 256 MiB rootfs 明确隔离；
- mkfs、image、fsck/审计工具和确定性镜像生成；
- 同步写入、flush 和一致性检查；本阶段不引入异步队列与 journal；
- 旧格式明确拒绝或由离线工具迁移，不允许原地猜测。

**退出条件**

- 创建、卸载、重挂载并逐字节验证至少 64 MiB 的普通文件；
- 跨直接/单级/多级索引边界、稀疏洞、截断和磁盘满均有测试；
- bitmap、inode、目录项和块所有权由独立审计器交叉验证；
- 损坏索引、重复块、孤儿 inode、CRC 和版本均安全拒绝；
- QEMU 冷启动、写入、flush、重启后数据一致；
- 文件系统格式文档能够从字节偏移重建镜像，不依赖 C++ 内存布局。

### v1.6 PID1、进程树、磁盘 exec 与 wait

**目标**

把正常镜像中的内嵌用户 ELF 和固定四进程脚手架替换为由 PID1 管理的磁盘
程序生命周期。

**产出**

- 父子关系、Zombie、退出状态、reparent 和唯一回收；
- Kernel 只创建初始 Process/Thread，执行 `/sbin/init`；
- Spawn、Exec、Wait/WaitPid 与 `argc/argv/envp` 用户初始栈；
- VFS 读取 ELF，保持两遍验证、W^X、范围、重叠和入口检查；
- exec 以新地址空间准备成功后原子替换，失败保留原程序；
- PID1 启动 `/bin/sh` 并持续回收孤儿。

**退出条件**

- `/sbin/init`、`/bin/sh` 和 `/bin/echo` 均从 rootfs v2 读取执行；
- 参数和环境跨系统调用、VFS、ELF 栈构造后逐字节一致，总量至少 128 KiB；
- 退出状态只能成功获取一次，父进程退出后子进程重归 PID1；
- 非法 ELF、路径、参数、内存和进程限制失败保持原进程与资源不变；
- 连续创建并等待至少 4096 个短进程后资源统计回到基线；
- 正常 Kernel ELF 不再嵌入 Shell、producer、consumer 或 worker 载荷。

## 波次 C：虚拟内存与用户组合

### v1.7 VMA、按需分页、页缓存与用户运行时

**目标**

把“地址区间意图”与“当前驻留页表项”分离，让 ELF、匿名内存、用户栈和文件
映射通过同一个页故障解析器获得页面。

**产出**

- AddressSpace 拥有非重叠 VMA，定义权限、来源、偏移和共享方式；
- 统一 `#PF` 分类：非法、权限、匿名缺页、文件缺页、栈增长和预留 COW；
- 按需 ELF、匿名 mmap/munmap、brk 与受控栈增长；
- 文件页缓存身份为 vnode + page index，并与物理页生命周期关联；
- 自研 freestanding 用户运行时：入口、系统调用、字符串、堆和错误码；
- unmap、exec 和 exit 对 VMA、PTE、缓存引用执行完整回收。

**退出条件**

- 大型 BSS/匿名区只在触碰后分配物理页，未触碰页不进入 resident 统计；
- VMA split/merge/unmap 随机模型至少 100000 步且区间始终不重叠；
- 文件映射跨 EOF、权限冲突、栈越 guard 和非法 canonical 地址均确定失败；
- 同一文件页的缓存命中、回收和重新装入内容一致；
- exec/exit/munmap 后页表页、物理页和 VMA 计数回到基线；
- 256 MiB functional 配置可运行全部用户场景，不依赖 64 GiB。

### v1.8 fork 与写时复制

**目标**

在稳定 VMA 和页故障模型上实现 Unix 进程复制，避免 fork 立即复制全部私有页。

**产出**

- Fork 复制 Process 资源引用并创建调用现场对应的子 Thread；
- 私有可写匿名页与文件私有页转换为只读 COW；
- 物理页引用计数、写故障私有化和单引用快速升级；
- 父子描述符共享 FileDescription，VMA 元数据独立；
- fork、exec、exit、unmap 和并发故障间的回滚与引用释放；
- 多线程 Process 的 fork 语义限定为只复制调用 Thread。

**退出条件**

- fork 后父子读取同页，写入后只复制被写页且内容相互隔离；
- 只读页不会被误分类为 COW，文件共享映射不执行私有复制；
- 深度至少 32 的进程树和 fork/exec/wait 组合能够全部回收；
- 内存不足发生在任一步骤都不暴露半构造子进程；
- 固定种子 COW 参考模型覆盖父写、子写、双写、退出和页复用；
- QEMU 日志以汇总计数证明共享页、COW fault、copy 和 fast-upgrade 路径。

### v1.9 Unix I/O、外部 Shell 与工具集

**目标**

让用户程序通过继承、dup、pipe 和 exec 组合，而不是依靠 Shell 内建命令或
内核专用测试管道。

**产出**

- pipe、dup/dup2、close-on-exec、继承和共享偏移的用户 ABI；
- 64 KiB 动态管道、阻塞读写、EOF、broken pipe 和原子小写入边界；
- Shell 词法/语法执行层，支持引号、变量、环境、重定向和流水线；
- 只有 `cd`、`export`、`exit` 等修改 Shell 自身状态的命令保留为内建；
- `/bin` 至少 32 个独立 ELF64 工具；
- 删除历史专用管道/文件调用的生产使用，兼容入口仅供回归。

**退出条件**

- 至少 16 级流水线在小缓冲和大输入下完成，无忙等和死锁；
- `<`、`>`、`>>`、标准错误重定向和 fd 关闭顺序符合明确语义；
- 64 GiB 下同时创建至少 1024 条管道，失败后已有管道保持可用；
- Shell 解析器具备单元、语法集成和固定种子随机 token 测试；
- 外部工具确实从磁盘读取，Kernel ELF 和 Shell ELF 不包含工具实现；
- 进程、fd、FileDescription、pipe page 和 vnode 引用在任务结束后回到基线。

## 波次 D：并发与交互控制

### v1.10 用户线程、TLS 与 futex

**目标**

开放共享地址空间的用户线程，并用 futex 把用户态原子快路径与内核阻塞/
唤醒连接起来。

**产出**

- ThreadCreate/Join/Exit、独立用户栈和线程返回 trampoline；
- FS base 或明确 TLS 机制，按 Thread 保存与恢复；
- 32 位对齐 futex word 的 Wait/Wake；
- wait 操作在“比较用户值”和“加入等待队列”之间保持原子性；
- 用户运行时 mutex、condition variable 和 once；
- exec、Process exit 与线程组回收规则。

**退出条件**

- 单 Process 至少创建并 join 64 个 Thread；
- 64 GiB 下 512 个 Thread 混合进程/线程负载稳定完成；
- futex 值不匹配立即返回，wake 数量、虚假唤醒和地址失效语义明确；
- 固定种子模型验证 compare-and-block 不丢失唤醒；
- TLS 在抢占、阻塞、系统调用和线程复用后保持隔离；
- 锁竞争压力不刷日志、不忙等，退出后所有 Thread 栈与等待节点被释放。

### v1.11 时间、信号、TTY 与作业控制

**目标**

把时钟、异步事件和终端所有权组织成可交互的进程组模型。

**产出**

- 单调时间、绝对 deadline 睡眠和有序 timer queue；
- signal disposition、mask、pending、用户 signal frame 与 sigreturn；
- Process group、session、controlling TTY 和 foreground group；
- 规范输入、退格、回显、EOF、Ctrl-C 和 Ctrl-Z；
- Shell 前后台任务、jobs、fg、bg 和 wait；
- 时间戳日志使用单调时钟，早期无时钟阶段明确标为 boot phase。

**退出条件**

- sleep 使用阻塞/唤醒，长时间运行无 tick 轮询列表和用户态忙等；
- signal frame 的 RIP/RSP/RFLAGS/地址和嵌套边界经过严格验证；
- Ctrl-C 只到达前台进程组，Shell 在默认场景下存活；
- 前后台任务切换、停止、继续和退出不会遗留 Zombie；
- timer/signal/TTY 状态机具备固定种子随机测试；
- QEMU 串口日志打印有界时间与关键状态汇总，禁止每 tick、每字符或每页刷屏。

## 波次 E：持久化与收口

### v1.12 异步块 I/O、journal 与崩溃恢复

**目标**

在稳定 VFS 和页缓存上引入设备完成、写序和事务恢复，让断电点具有可解释语义。

**产出**

- IRQ14 驱动的 ATA 请求队列、超时、错误传播和完成唤醒；
- 块请求合并边界与缓存页回写状态机；
- metadata journal：事务描述、提交记录、flush barrier 和重放；
- mount 时恢复、只读失败模式和独立 fsck；
- 宿主故障注入在具名写点截断、终止或损坏镜像；
- I/O、缓存和 journal 锁序文档。

**退出条件**

- 前台用户 Thread 在磁盘 I/O 期间阻塞，其他 Ready Thread 可运行；
- 至少 1000 个固定种子断电点重启后满足“旧状态或完整新状态”；
- journal 重放幂等，已提交事务不会丢失，未提交事务不会部分可见；
- ATA timeout、ERR/DF、短写模拟和磁盘满保持资源与命名空间一致；
- fsck 独立验证块所有权、链接、目录可达性和 journal 边界；
- 256 MiB 与 64 GiB 持久化压力均通过，日志只输出事务级汇总。

### v1.13 ABI v2 冻结、加固与可观测性

**目标**

停止增加基础机制，冻结 v2 用户契约，删除生产兼容依赖并补齐全系统防御证据。

**产出**

- 固定宽度 syscall 编号、参数、错误码和跨特权结构版本；
- `SYSCALL/SYSRET` 成为正式入口，`INT 0x80` 仅保留明确兼容测试或移除；
- 用户复制、canonical 地址、长度溢出、权限和 TOCTOU 审计；
- `/dev` 和最小只读 `/proc` 或等价诊断接口；
- Process/Thread、内存、VFS、块层、信号和 syscall 的有界统计；
- 模糊测试语料、长时间 soak、发布构建和调试构建差异审计。

**退出条件**

- ABI 文档可独立生成用户端声明，结构偏移和调用号由产物测试锁定；
- 所有用户指针先复制/校验，任何长度计算均有溢出测试；
- 非法 syscall、畸形 ELF、畸形文件系统和恶意参数不能 panic Kernel；
- 256 MiB 连续交互 soak 与 64 GiB 容量 soak 无资源单调增长；
- 正常启动和热路径日志满足速率限制，panic 保留足够现场；
- 所有历史兼容入口列出保留、迁移或删除结论，不留隐式双实现。

## v2.0 集成发布

v2.0 不再引入主要状态模型。它把 v1.13 已冻结的系统作为一个可安装、可运行、
可学习和可复现的整体发布。

### 端到端验收场景

1. 自研 ROM 从复位向量启动，自研 Stage 1 装载 Kernel。
2. Kernel 发现 64 GiB RAM，建立 direct-map、分配器和全部核心服务。
3. 挂载 rootfs v2，执行 `/sbin/init`，再启动 `/bin/sh`。
4. Shell 执行外部工具、16 级流水线、重定向和前后台任务。
5. 用户程序 fork、exec、创建 64 个 Thread，并用 futex 同步。
6. 按需 ELF、匿名内存、文件映射和 COW 产生可核对的汇总统计。
7. 文件系统完成事务写入，在注入断电后重启并恢复一致状态。
8. 256 MiB 完成功能套件，64 GiB 完成容量套件，64 MiB 保持启动兼容。

### 发布产物

- 可复现的 ROM、Stage 1、Kernel、rootfs、稀疏磁盘和 QEMU 启动命令；
- 完整 CTest 分类清单、种子、超时和失败日志；
- ABI、磁盘格式、内存布局、寄存器、锁序和资源所有权文档；
- 从硬件历史到实现取舍、代码走读和实验的 LaTeX 教材及手机导出；
- 核心 C++/头文件/汇编代码统计，不计测试、工具、书籍和网站代码；
- 发布说明、已知限制和 v2.x/v3.0 候选问题。

## 所有版本的统一质量门禁

每个版本必须依次通过以下门禁，不能用 QEMU “看起来能启动”替代较低层证据：

1. **设计门禁**：ADR、公开契约、状态机、不变量、锁序、所有权和失败原子性；
2. **逻辑门禁**：宿主单元测试和独立参考模型；
3. **组合门禁**：模块集成、固定种子随机测试和资源守恒；
4. **产物门禁**：ELF、汇编、ABI、镜像、磁盘格式或生成代码审计；
5. **整机门禁**：适用配置上的 QEMU 正常、边界和故障注入；
6. **交付门禁**：回归基线、指标、发布记录、教材、网站和手机导出同步。

测试必须固定超时，随机失败必须报告 seed、迭代位置、最小性质和可复现命令。
涉及创建、复制、打开、映射、等待或提交的操作必须同时验证成功路径、容量边界、
中途失败和清理后的资源基线。

## v2.0 非目标与后续入口

以下内容不进入 v2.0：

- AP 启动、SMP 调度、per-CPU 分配器和跨核 TLB shootdown；
- ACPI 全解析、IOAPIC/MSI、AHCI、NVMe、USB 和通用 PCI 框架；
- 网络协议栈、套接字、图形、音频和鼠标；
- 多用户权限、容器、虚拟化和完整 POSIX；
- 动态链接器、共享库、自举编译器和在本系统内构建本系统。

它们进入 v2.x/v3.0 候选池。当前代码可以保留固定宽度 ABI、对象边界和锁规则
所带来的扩展空间，但不得以未来 SMP 或 POSIX 为理由增加当前无法验收的抽象。

# 项目需求

## 目标

通过从 CPU 复位向量开始实现一个 x86-64 教学操作系统，系统学习启动、处理器模式、内存管理、中断、设备、用户态、进程、同步、文件系统和用户环境。

## 固定约束

- 目标指令集为 x86-64。
- 使用 QEMU TCG 模拟硬件，不要求宿主机采用 x86-64 架构。
- QEMU 只提供硬件模型，不替代固件、引导程序或内核。
- 64 MiB、256 MiB 与 64 GiB 分别作为启动兼容、完整功能和主容量配置，
  不要求低内存配置承担高并发压力。
- 64 GiB 是测试规格而不是实现上限；内核容量由 E820、处理器物理地址宽度
  和当前 direct-map 容量共同决定。
- 正式 QEMU CPU 型号与必需 CPUID 特性必须冻结并在启动时检查；v2.0 要求
  long mode、NX、SSE2 与 `SYSCALL/SYSRET`。
- 固件、磁盘加载、模式切换、ELF64 加载和运行时均由项目实现。
- 高级语言使用 freestanding C++20，汇编使用 NASM Intel 语法。
- 宿主自动化使用 Python 3.11+ 标准库，构建图由 CMake 与 Ninja 管理。
- 不使用 SeaBIOS、OVMF、GRUB、Limine、Multiboot 或 QEMU `-kernel`。

## 质量要求

- 每个里程碑必须定义可自动化或可重复执行的验收标准。
- 正常、边界和失败路径必须具有明确的验证方式。
- 纯逻辑模块必须同时具备单元测试、模块集成测试和固定种子随机测试。
- 随机测试失败时必须报告种子、迭代位置和失败性质，确保故障可复现。
- 架构、重要决策、测试方案和复杂故障必须形成文档。
- 构建应当可复现，串口日志和有界 QEMU 生命周期应当支持自动回归。
- 每个源码模块必须隔离公开头文件和私有实现，并由独立 CMake target
  强制单向依赖。
- Kernel 内部必须按功能所有权划分目录；`include/os/kernel/<module>` 与
  `src/<module>` 使用相同模块集合和相对头源路径，两个根目录不得重新堆放
  实现文件。生成与汇编例外必须具名登记并由完整回归检查。
- C++ 变量和参数的语义单词使用下划线分隔；普通函数使用大驼峰；命名空间
  每一层使用一个简短小写单词。完整回归必须通过 Clang AST 与命名空间词法
  门禁。

## v2.0 最终目标

v2.0 的目标不是成为完整 POSIX 或现代桌面系统，而是形成一个边界清晰、
能够解释核心机制的单处理器、多进程、多线程类 Unix 教学操作系统：

- 正常启动只由内核创建 PID1，PID1 从自研根文件系统启动 Shell 和其他程序；
- 用户程序以磁盘 ELF64 文件存在，通过 Spawn、Exec、Fork 和 Wait 形成
  父子进程树；
- Process 与 Thread 分离：Process 共享地址空间、文件表和文件系统上下文，
  调度器调度拥有独立 CPU 现场、栈、TLS 与信号掩码的 Thread；
- 内核采用“中断可进入、内核不可抢占”的单 BSP 执行模型；IRQ 不阻塞，
  调度只发生在显式阻塞/让出/退出和返回用户态前；
- WaitQueue 统一全部阻塞，条件、超时、信号、关闭和取消只允许一个
  WakeReason 获胜；spinlock、irq-save spinlock 和 sleep mutex 不混用；
- CpuLocal、`SYSCALL/SYSRET` 与 `INT 0x80` 共同进入统一 UserContext 和
  dispatcher；返回前验证 canonical 地址、RFLAGS 和特权状态；
- 每 Thread 使用 `FXSAVE/FXRSTOR` 隔离 x87/SSE2 现场；AVX/XSAVE 在 v2.0
  保持禁用；
- 物理内存、内核堆、进程栈、页表、描述符和 VFS 对象都具有可回收生命周期；
- VMA 描述地址空间意图，PTE 只表示当前驻留事实；用户地址空间支持按需
  ELF、匿名映射、`MAP_PRIVATE`、只读 `MAP_SHARED`、受控栈增长、写时
  复制和自研用户堆；
- VFS 统一根文件系统、设备和只读进程信息，根文件系统支持大文件、命名空间
  修改、同步、日志与崩溃重放；
- 描述符使用分块动态表，支持继承、dup、close-on-exec、动态管道和共享
  open-file description；默认 soft limit 为 256，hard limit 至少 4096；
- Shell 只保留必须修改自身状态的内建命令，其他命令从 `/bin` 执行，并支持
  流水线、重定向、环境、前后台任务和 Ctrl-C；
- 用户线程通过 TLS 和 futex 构造 mutex、condition variable 等同步原语，
  private futex 以 `(AddressSpaceId, aligned VA)` 为键，compare-and-block
  不允许丢失唤醒，unmap 必须取消相关等待；
- 信号、进程组、终端前台所有权和 sleep 使用阻塞/唤醒，不允许用户态或内核
  热路径忙等；
- 多线程语义固定：fork 只复制调用 Thread；exec 先构造候选映像，成功后才
  汇合兄弟 Thread；ThreadExit 与 ProcessExit 分离；信号处置属于 Process，
  signal mask 属于 Thread；
- clean page cache、dirty/writeback 和 ordered metadata journal 分阶段
  建立；事务必须预留 credits，并以 flush/commit/replay 证明恢复边界。

正式功能矩阵如下；数字是运行时验收下限，不是固定数组长度：

| 配置 | RAM | Process | Thread | 每 Process Thread | fd hard | Pipe |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| bootstrap | 64 MiB | 不规定 | 不规定 | 不规定 | 不规定 | 不规定 |
| functional | 256 MiB | 64 | 128 | 32 | 512 | 128 |
| capacity | 64 GiB | 256 | 512 | 64 | 4096 | 1024 |

capacity 另行验证 64 KiB pipe、1 GiB 稀疏磁盘、256 MiB rootfs、64 MiB
单文件、32 个独立用户 ELF 和 16 级流水线。`argv/envp` 合计 128 KiB 必须
用可回收页暂存，禁止放入大型内核栈缓冲。量化下限、运行频率和逐阶段验收见
[开发路线](roadmap.md)。

## v2.0 非目标

以下内容不进入 v2.0，以避免进程、内存、文件和用户环境主线被硬件广度稀释：

- SMP、多核调度和跨核 TLB shootdown；
- writable `MAP_SHARED`、`msync`、swap、overcommit 和 OOM killer；
- 正/负 dentry cache、数据 journal、快照和在线扩容；
- AVX/XSAVE；
- 网络、图形、音频、USB、AHCI、NVMe 和通用 PCI 设备框架；
- 多用户权限模型、完整 POSIX、动态链接器、共享库和自举编译器。

这些边界不是永久放弃，而是 v2.x/v3.0 的候选输入。v2.0 仍以 QEMU TCG 的
单个 x86-64 BSP 和传统 PC 设备为正式验收平台。

## v1.13 完成基线

第一周期已完成 `v1.0 用户环境`；第二周期的 v1.1 已完整闭合内存分配与资源
生命周期，v1.2 又完成 Process/Thread、WaitQueue、锁模型与完整扩展现场，
v1.3 已完成 CpuLocal、处理器能力冻结和原生系统调用安全边界；v1.4 又完成
类型化 KernelObject、共享 FileDescription 和动态 FileTable；v1.5 已完成
VFS、每 Process FsContext、memfs 与 legacy 文件系统适配；v1.6 已完成
rootfs v2、完整命名空间修改、稀疏大文件、独立 mkfs/fsck 和损坏拒绝。
v1.7 又完成 PID1、父子进程树、Zombie/reparent、磁盘 ELF spawn/exec/wait、
128 KiB `argv/envp` 和候选映像原子提交。v1.8 已完成非重叠 VMA、
匿名 `mmap/munmap`、按需零页、program break、8 MiB 受控用户栈、
自研用户 heap，以及 unmap/exec/exit 后的数据页、空页表分支与 VMA
描述符回收。v1.9 已进一步完成文件后备 VMA、按需 ELF、有界 clean page
cache、只读 shared、可写 private、文件修改失效和跨 fd-close 生命周期。
v1.10 又完成只复制调用 Thread 的 fork、匿名/private 页 COW、统一用户
`#PF`/Kernel `CopyToUser` 私有化、fd/FileDescription/FsContext/FileBacking
继承，以及失败时父状态完整恢复。v1.11 进一步完成 64 KiB 按需分配动态
管道、分档 PipeManager、`pipe/dup2` ABI、外部命令 Shell、16 级流水线、
输入输出重定向与 `/bin` 核心工具，并在成功、拒绝、退出和异常路径上闭合
描述符、端点和物理页生命周期。v1.12 又完成用户 Thread、FS-base TLS、
private futex、用户 Mutex/ConditionVariable/Once、32/64 Thread 三档整机
验收以及 `munmap`/`exec`/ProcessExit 取消。v1.13 已把 PIT 实际除数提升为
精确余数累计的 64 位单调纳秒，建立 512 槽 deadline queue，并完成非忙等
sleep、timed futex、timed condition、通知/超时单赢家与三档 QEMU 验收。
下一阶段为 v1.14 signal、进程组与系统调用中断语义。

## v1.9 文件虚拟内存冻结要求

- 文件映射身份必须包含 superblock/inode 的 identifier 与 generation，
  不得用 fd 或可复用对象地址作为 cache key。
- VMA 必须保存后备 generation、文件 offset 和有效数据长度；split/merge
  必须同步维护文件区间。
- ELF 结构在提交前完整校验，物理页允许延迟到用户访问；返回用户态前必须按
  executable VMA 解析入口页。
- clean page cache 必须有固定硬容量、共享映射引用和零引用 LRU；无候选时
  明确失败，不能无界等待或回收被 PTE 引用的页。
- 文件尾与 ELF BSS 未覆盖字节必须为零；不能读入相邻文件内容。
- 支持只读 `MAP_SHARED` 与可写 `MAP_PRIVATE`；writable shared 明确不支持。
- fd 关闭后映射继续有效；unmap、exec 和 exit 释放最后后备引用。
- write/truncate 必须撤销旧只读文件 PTE 并失效 cache；private 修改不得
  回写文件。
- 64 MiB、256 MiB、64 GiB 必须运行相同文件映射探针并最终资源守恒。

## v1.10 fork/COW 冻结要求

- fork 子 Process 只包含调用 Thread；父返回子 PID，子返回 0，失败保持父
  Process 可观察状态不变。
- AddressSpace、VMA、FileBacking、FileTable、FsContext、用户现场、FXSAVE
  和 KernelStack 都必须有显式 clone/所有权/回滚路径。
- FileTable clone 必须保留精确 fd 与 fd flags；父子共享 FileDescription
  offset，但关闭 fd 和后续表结构修改相互独立。
- 只有原本可写的 private 驻留页可以标 COW；真正只读页必须继续产生保护
  fault，Writable+COW PTE 属于非法状态。
- 用户 present+write `#PF` 与 Kernel `CopyToUser` 必须复用同一 COW break；
  内核不得经 direct-map 绕过私有化修改共享 frame。
- 引用数为 1 时恢复原 frame 可写；引用数大于 1 时准备新 frame、复制完整页、
  替换 PTE 后再释放旧引用。
- fork 必须先完成候选 child，再提交父 PTE；任意中途失败必须销毁 child、
  恢复父权限并清除仅剩单引用的 COW 元数据。
- 100000 步引用模型、连续 32 次 fork/exec/wait 与 64 MiB、256 MiB、
  64 GiB QEMU 都必须结束于零活动 COW 引用、零 Zombie 和跨层资源守恒；
  64 MiB 兼容档不要求同时保留 32 个活跃 Process。

## v1.11 Unix I/O 冻结要求

- 历史 64 字节启动管道继续服务早期兼容探针；普通用户管道必须使用 64 KiB
  逻辑容量和 4 KiB 按需物理页，未触及区间不得提前占用页帧。
- PipeManager 在 bootstrap、functional、capacity 三档分别提供 8、128、
  1024 个槽；创建失败不得遗留半安装端点，最后一个读端和写端关闭后必须
  释放全部后备页并回收槽。
- FileTable 在 functional 档 hard limit 为 512；`dup2(oldfd, newfd)` 必须
  精确替换目标 fd、保持共享 FileDescription 语义，并对相同 fd、非法 fd、
  引用获取失败保持 destination 不变。替换一旦提交便不得回滚；旧 destination
  的 finalizer 在表锁外失败时返回显式 release failure，并把它提升为内核
  资源账本错误，不能把已经公开的新 fd 伪装成未提交。
- 系统调用 ABI 固定新增 `CreatePipe=45` 与 `DuplicateDescriptorTo=46`；
  用户包装不得把内核地址、宿主句柄或实现对象泄露到 Ring 3。
- Shell 只把 `cd` 和 `exit` 保留为内建命令；help、文件与目录操作、文本
  处理和状态工具必须作为 rootfs 中的外部 ELF 从 `/bin` 执行。
- 解析与执行必须分离。解析器支持单引号、双引号、反斜杠、`<`、`>` 和
  最多 16 级流水线；语法错误、参数超限、阶段超限和行长超限均不得产生
  Process、fd 或管道副作用。
- 执行器必须先准备管道和重定向，再逐个 spawn；任一中途失败都要关闭父端、
  回收已创建子进程，并等待所有已提交子进程，不能泄漏 Zombie。
- 关闭读端后的写入必须报告 broken pipe；关闭写端且缓冲耗尽后的读取必须
  返回 EOF；阻塞等待必须通过 WaitQueue，不得轮询刷日志。
- 单元测试必须覆盖跨页、回绕、EOF、broken pipe、`dup2` 替换和 16 级解析；
  固定种子随机测试至少覆盖 100000 次动态管道操作和 4096 条任意 Shell
  输入；bootstrap 与 functional QEMU 必须真实键入重定向和 16 级流水线。
- functional QEMU 结束时动态管道 active 为 0、peak 为 128、创建数等于
  释放数且至少出现一次容量拒绝；所有 fd、FileDescription、Process、
  Zombie 和物理页统计必须回到阶段基线。

## v1.12 用户 Thread 与 futex 冻结要求

- 系统调用 ABI 固定为 CreateThread=47、ExitThread=48、JoinThread=49、
  SetThreadLocalStorage=50、GetThreadId=51、WaitPrivateFutex=52、
  WakePrivateFutex=53；结构尺寸必须由 `static_assert` 冻结。
- Thread 必须拥有独立 TID、通用/FXSAVE 现场、KernelStack、用户栈、FS-base
  TLS、signal mask 预留和等待关系；AddressSpace、FileTable 与 FsContext
  仍属于 Process。
- Create 必须先验证和准备全部上下文，再把 Ready Thread 暴露给调度器；
  子 Thread 的 TLS TID 由子入口自行发布，不能依赖父 Thread 返回后的写入。
- functional 单 Process 支持 32 Thread，capacity 支持 64 Thread；到达上限
  后下一次创建必须明确失败并完整回滚 KernelStack、调度槽和用户映射。
- IA32_FS_BASE 必须在 Thread 切换时恢复，并跨 PIT 抢占、Blocked/Ready、
  SYSCALL/INT 兼容入口与返回保持；系统调用汇编不得装载 FS selector 覆盖 base。
- private futex key 必须是 `(AddressSpaceId, aligned uint32_t VA)`；不同地址
  空间的相同 VA 不得共享队列，COW/物理 frame 不得进入 key。
- wait 必须在 wake 使用的同一 irq-save 临界区内二次读取 word 并入队；
  值变化返回明确状态，不允许“比较后、入队前”丢失唤醒。
- `munmap`、成功 exec 提交、ProcessExit 和用户异常必须取消旧地址范围或旧
  AddressSpaceId 上的 waiter；活动 Thread 的 stack/TLS 不允许被撤销。
- ThreadExit 只发布 Thread 退出值；唯一 Join 回收 KernelStack/Thread 后，
  用户运行库释放用户栈/TLS。ProcessExit 才关闭共享资源并终止 sibling。
- 用户 Mutex、ConditionVariable 与 Once 必须以固定宽度编译器原子实现快
  路径，只有竞争路径进入 futex；失败不得静默退化成未持锁继续执行。
- 单元测试覆盖 key/容量/生命周期，随机测试至少执行 100000 步，QEMU 必须
  完成 64 MiB 单线程降级、256 MiB 32 Thread 和 64 GiB 64 Thread，并在
  结束时满足零 futex waiter、零 KernelStack、零 Zombie 和资源快照守恒。

## v1.13 单调时间与 deadline 冻结要求

- 单调时间 ABI 固定为无符号 64 位纳秒；它只表达经过时间，不允许混入 RTC、
  日期、时区、闰秒或宿主墙钟。
- PIT 换算必须使用输入频率和实际编程除数，并保留整数除法余数；禁止把每个
  tick 硬编码为 1 ms。所有乘加在回绕前检查，边界饱和为 `UINT64_MAX`。
- 内核所有 timeout 统一转换为绝对 deadline。相对时长只能在接口边界用
  饱和加法转换一次，重试和虚假唤醒继续使用原 deadline。
- 每个 Blocked Thread 最多有一个活动 deadline；队列必须按
  `(deadline, insertion sequence)` 稳定排序，并支持按 Thread 身份直接取消。
- DeadlineQueue 的 schedule、expire、cancel 与 WaitQueue 状态转换必须位于
  同一 scheduler irq-save 临界区。condition、timeout、terminate、unmap、
  exec 和 ProcessExit 最多让 Thread 进入一次 Ready。
- IRQ0 无论来自 Ring 3、Ring 0 还是 idle 都必须推进时钟并解析到期项。
  唤醒只设置 Ready/need-resched，不能在任意 IRQ 调用链直接切换 C++ 栈。
- sleep 必须阻塞；没有 Ready Thread 时由 `sti; hlt; cli` 等待 PIT，禁止
  轮询时间。deadline 已到达时必须立即返回，不登记短命队列项。
- timed futex 必须在与 wake 相同的锁内完成最终用户字比较、deadline 检查
  和 block；到期返回 `TIMED_OUT`，并释放已经为空的 futex key。
- ConditionVariable 的 timed wait 无论通知、超时还是失败都必须先重新取得
  Mutex 再返回；通知只表示谓词可能变化，调用者仍需循环检查。
- 系统调用 ABI 固定新增 GetMonotonicTime=54、SleepUntil=55、
  WaitPrivateFutexUntil=56，超时错误固定为 `-51`。
- 单元、集成和固定种子随机测试必须覆盖余数、饱和、不早醒、同 deadline
  稳定顺序、通知/超时单赢家与 100000 步队列模型。三档 QEMU 结束时
  active deadline 为零，且 schedules 等于 expirations、cancellations 与
  active 三者之和。
Stage 1 在自研长模式环境中通过 ATA PIO 读取
Kernel 描述符和 ELF 文件，自行执行 CRC32、扇区补零、ELF64、权限、对齐、
范围和段重叠检查。所有 `PT_LOAD` 先完整验证，再复制到恒等映射目标地址并
清零 BSS。Stage 1 通过 `fw_cfg` 端口发现 QEMU PC 提供的 `etc/e820`，
自行完成大端目录解析、20 字节条目到 24 字节 ABI 的转换和基址排序，再以
104 字节、全 64 位字段的 BootInfo v2 通过 System V AMD64
首参数寄存器交给 C++20 内核。内核不继续借用 Stage 1 的描述符状态：v0.5
先建立五槽 GDT、104 字节 TSS、256 槽 IDT 和 32 个架构异常汇编桩；
v0.8 加入 Ring 3 数据/代码段后扩为七槽，并把 TSS 选择子移到 `0x28`。

正常路径与 Kernel ATA 超时、ATA 设备错误、描述符损坏、负载损坏和 CRC 正确
但 ELF 语义损坏路径均有 QEMU TCG 回归。内核验证排序、不重叠且无溢出的
物理内存图，并读取 `CPUID.80000008H` 的物理地址宽度。页帧状态元数据按
最高可用 RAM 页动态计算，在启动身份映射内选址；高地址保留洞不会错误扩大
状态表。分配器以每帧 2 bit 管理全部受管 E820 type 1 RAM，保留平台、内核、
早期栈和自身元数据。正式地址空间从 `0xFFFF888000000000` 建立 64 TiB
高半区 direct-map，内部优先采用 2 MiB 页并只映射普通 RAM。页表页、用户页
清零和 ELF 复制在 CR3 切换后统一经 direct-map 访问，不再把物理地址直接当作
C++ 指针。

主系统测试以 64 GiB RAM 启动，要求完整管理 64 GiB 可用 RAM、报告至少
4 MiB 页帧元数据、实际使用 2 MiB direct-map 页，并在 4 GiB 以上页帧写入、
读回和回收两个 64 位模式。64 MiB 回归则证明高内存自检可有条件跳过而不
缩小通用实现。内核启用 `IA32_EFER.NXE` 与 `CR0.WP`，切换到自建四级页表，
并对代码、只读数据、可写数据、堆和 guard page 执行权限验证。真实 `INT3`、
`UD2`、not-present 页故障和写保护页故障覆盖恢复、错误码、CR2 与 panic。

内核现已为向量 32..47 安装独立 NASM 硬件 IRQ 桩，映射本地 APIC MMIO 页，
通过 SVR 与 LVT LINT0 建立 ExtINT virtual-wire，再把两片 8259A 重映射到
`0x20..0x2F`。8254
PIT 通道 0 以模式 2 提供约 1000 Hz IRQ0，内核用 64 位 tick 和实际除数换算
单调毫秒；PS/2 控制器启用 IRQ1 与扫描码集合 1 翻译，C++ 解码器区分 make、
break 和 `E0` 扩展序列。内核还用自写 ATA PIO 驱动重读 LBA 0 并校验
`OSSTAGE1`，证明设备访问不依赖加载器函数。

v0.7 首次用 QMP 键盘前端注入 `A` 键证明 IRQ1 链路；v1.0 已扩展为逐字
输入完整 Shell 命令。无论单键还是命令，扫描码从 i8042 端口、IRQ1、8259A、
IDT、汇编桩到 C++ 解码均由目标代码处理。宿主只负责产生外部输入和验证串口
协议。

v0.8 新增独立 ABI 与用户模块。用户程序由 freestanding C++20 和 NASM
Intel 汇编构建为链接基址 `0x40000000` 的 AMD64 `ET_EXEC`，以内嵌原始
文件形式交给内核解析。内核验证 ELF 全部结构、W^X、地址、对齐、重叠、页数
与入口后，分配带 U/S 的 RX 或 RW/NX 页面，并建立四页用户栈和未映射
guard。GDT、TSS.RSP0、IDT DPL3 gate 与五项 `IRETQ` 帧共同完成 Ring 3
进入；`INT 0x80` 提供 `WriteLog` 与 `ExitProcess`，所有用户地址先逐页
验证再复制。

v0.9 在该边界上加入固定容量进程表和单核抢占式 round-robin。每个进程拥有
独立 PML4、同址 ELF 页、用户栈及带保护页的 16 KiB Ring 0 栈；内核映射只
以 supervisor 子树共享。PIT 每四个 tick 形成时间片，入口保存完整通用寄存器
和用户 `SS:RSP:RFLAGS:CS:RIP`，调度器再切换 CR3 与 TSS.RSP0。

正常镜像同时创建一个系统调用验收进程和三个相同 worker。三个 worker 都在
`0x40000000` 程序窗口运行，并让同一 BSS 虚拟地址独立从零演进，证明地址
空间隔离。`GetProcessId`、`ExitProcess` 和用户异常都经过同一当前进程
生命周期；最后一个进程结束后，物理页 free/allocated 统计必须恢复到创建前。
v0.10 在此基础上引入 `Blocked` 状态、可审计的等待原因、阻塞与定向唤醒
统计。当前单核系统在 interrupt gate 关闭 IF 的系统调用窗口内完成条件检查
和调度转换；共享管道内部同时使用 acquire/release 自旋锁，明确未来 SMP
扩展时的内存顺序边界。自旋临界区不得调度、打印或执行用户复制。

内核提供一个启动期 64 字节有界字节流管道。ABI 把非阻塞 `TryRead/TryWrite`
与 `WaitReadable/WaitWritable` 分开，用户包装在 `WouldBlock` 后睡眠并循环
重试。管道支持环形回绕、部分读写、EOF、broken pipe、读写端关闭和进程异常
终止时的端点自动关闭；只有生产者拥有写权限，只有消费者拥有读权限。

正常镜像创建生产者、消费者和两个调度 worker。生产者写入 256 字节确定性
载荷，消费者用 31 字节缓冲逐段校验，要求真实出现满管道写阻塞和空管道读
阻塞；全部完成后字节统计相等、缓冲为空、端点均关闭、EOF 恰观察一次，创建
前后物理页统计一致。v0.10 的 64 项 CTest 覆盖调度/管道单元测试、组合测试、
固定种子随机模型、四线程同步压力、六个用户 ELF 审计、QEMU 真实阻塞/唤醒
与全部旧失败路径。v0.11 又实现固定布局、CRC32、bitmap/inode/目录、八项写回缓存、
Dirty/Clean 提交协议、ATA PIO 写入与 flush，以及每进程四槽普通文件描述符；
真实 QEMU 连续启动证明文件跨来宾实例持久化，损坏超级块必须拒绝挂载。
v1.0 在每个 PCB 中建立八槽统一描述符表：fd 0/1/2 是标准输入、输出和
错误，动态槽保存普通文件、目录、管道读端或管道写端。通用
TryRead/TryWrite、WaitReadable/WaitWritable 和 Close 统一资源命名与阻塞
包装；目录保留 OpenDirectory/ReadDirectory 的类型化迭代语义。历史专用
系统调用仍作为兼容入口存在，正常生产者和消费者已经迁移到通用路径。

PS/2 Set 1 解码器把真实 make code 转成单字节字符并提交到 256 字节控制台
FIFO。Ring 3 Shell 在 fd 0 为空时阻塞；如果此时没有 Ready 进程，内核回到
永久地址空间执行同一汇编块中的 `sti; hlt; cli`，由 IRQ1 提交字符并唤醒后再恢复用户帧。QEMU
测试只通过键盘前端逐字输入 help、文件、目录、同步、未知命令和退出，不把
命令预置到内核。

Shell 是独立 freestanding C++20 ELF，使用固定容量解析器实现 help、echo、
pwd、cd、ls、mkdir、write、cat、rm、rmdir、mv、truncate、stat、sync 和
exit。v1.0 收口时完整回归为 97 项；当前回归继续覆盖单元、集成、固定种子
随机、最终产物审计、真实交互、双启动持久化与历史失败路径。测试项数由
构建图自动产生，不在需求中冻结。v1.0 是第一周期 `13 / 13`
的完成基线。v1.1 已经完成动态物理
内存元数据、64 TiB direct-map、64 GiB 管理、4 GiB 以上页帧读写回收，
可释放、可合并并经过十万步模型验证的通用内核堆，以及支持连续块、错阶拒绝
和十万步模型的双位图 buddy。固定尺寸 type cache 也已经建立在通用堆之上：
一个后备块同时保存活动位图和对齐槽，空闲槽组成 LIFO 索引链；缓存必须拒绝
空指针、内部/外部指针、重复释放、活动对象销毁和计数溢出，耗尽时保持输出
不变，销毁后把后备块完整归还通用堆。单元、三缓存集成、十万步固定种子随机
和 64 MiB QEMU 真实写回共同验收该契约。KVA 当前以 1024 个有序区间描述符
管理 32 TiB 高半区窗口，必须区分保留、分配、元数据耗尽和连续地址耗尽；
单元、页帧/页表集成、十万步逐页模型以及 QEMU 双 guard 四页真实映射共同
验收申请与逆序回收。当前四个 Ring 0 栈也已迁移为六页 KVA 所有权区间：
中间四页使用独立物理后备和 supervisor RW/NX 映射，上下两页保持
not-present；进程终止后必须先回到永久启动栈安全点，再按叶映射、物理页、
KVA 的逆序清零回收。单元、独立 CR3 集成、十万步随机模型、正常四进程和
两个用户异常 QEMU 路径共同验收该契约。页表根现在显式区分独占、内核共享
与进程三种所有权；撤销最后一张叶映射会逆序回收独占的空 PT、PD 与 PDPT，
但不会释放仍可能由进程 PML4 借用的共享 PDPT。映射任一级失败必须恢复父项
原值并释放本事务新建的表帧；单元、集成、十万步随机模型和 QEMU 回收摘要
共同验收该契约。

v1.1 最后以三个互相组合的原语收口：`ScopeRollback` 由调用方提供固定动作
数组，失败时严格逆序执行且不会因单个清理失败而短路；动态内核栈创建已经
使用同一九项事务回收映射、清零物理页并释放 KVA。`ReferenceCounter` 使用
显式 `uint64_t`，只定义单 BSP 或外部锁保护下的强引用生命周期、最后引用与
上溢，不提前承诺 v1.4 的原子/弱引用策略。`ResourceSnapshot` 用 26 个字段
记录 frame、buddy、heap、KVA、kernel stack 和后续对象槽的当前所有权，
排除累计成功/释放计数。启动目标代码会真实创建并回滚一个动态栈，四进程全部
结束后再次比较完整快照；两次都必须得到零差异。

单元测试、跨 buddy/页表/KVA/栈集成测试、固定种子十万轮引用/回滚模型、
64 MiB bootstrap、具名 256 MiB functional smoke 与 64 GiB capacity 共同
构成阶段证据。正常运行会按内存档位先执行 Process/Thread 容量事务：
256 MiB 路径建立 64 个页表根和 128 个动态栈，64 GiB 路径建立 256 个
页表根和 512 个动态栈；随后四个用户 Thread 各建立一栈。统计必须精确反映
这些生命周期并最终回到零活动资源。旧四 PCB 调度器已经删除，四程序行为
迁移到新的 Process 资源容器和 Thread 调度实体。v2 路线按
[ADR 0019](adr/0019-v2-executable-program-baseline.md) 划分为 v1.1 至
v1.18，v2.0 只承担集成发布。

v1.3 将硬件入口要求提升为可测试需求：CPU 必须同时具备 long mode、NX、
FXSR、SSE、SSE2 和 `SYSCALL/SYSRET`，物理地址宽度必须在 36..52 位，
虚拟地址宽度冻结为 48 位。缺少能力时，内核必须输出缺失位图并在初始化 GDT
和用户态之前停止。每个 CPU 的本地状态必须保存当前 Thread、可信入口 RSP、
IRQ/抢占深度、`need_reschedule`、入口类型和有界累计证据；当前单 BSP 使用
一个 64 字节对齐实例，但接口不得把“只有一个 CPU”编码进用户现场。

默认用户系统调用入口必须使用 `SYSCALL`，兼容 `INT 0x80` 仍保留并与原生
入口进入同一分发器。`SYSCALL` 不自动换栈，因此入口必须先 `SWAPGS`，只从
内核写入的 `CpuLocal` 读取当前 Thread 的动态 Ring 0 栈，绝不能在用户 RSP
上压入内核数据。两条入口都必须形成同一个 176 字节 `UserContext`。返回
Ring 3 前必须同时验证现场属于当前 Thread、RIP/RSP 位于 48 位低半规范区、
代码与栈映射权限、CS/SS 和 RFLAGS。只有原生入口且标志位属于快速白名单时
允许 `SYSRETQ`；其余合法现场必须 `IRETQ`，非法现场不得触发带攻击者地址的
Ring 0 `#GP`，而要终止相应用户进程。

v1.4 要求 fd 与对象身份严格分离。KernelObject 必须具有类型、全局单调
generation、强引用和最后引用 finalizer；跨模块业务路径只能持有 RAII
reference，不能保存 payload 裸指针。FileDescription 必须拥有打开实例的
offset 和 file status flags；duplicate 必须共享它们，独立 open 必须隔离。
fd flags 只保存在 FileTableEntry，close-on-exec 不得反向修改共享对象或源
描述符。

FileTable 必须以 64 项分块按需增长，functional/capacity hard limit 分别为
256/4096。soft limit 只限制新安装，达到限制必须返回明确错误；关闭后的最低
编号必须可复用。分块堆申请使用两阶段提交，任何申请、复验或安装失败都必须
保持旧槽与传入引用不变。进程退出后活动对象、强引用和分块全部归零，创建/
销毁、finalizer 和分块申请/释放分别守恒。宿主 4096 fd 容量测试、十万步
固定种子模型和真实 Ring 3 共享偏移证明必须同时通过。

v1.5 要求路径语义与磁盘格式彻底分层。Vnode 必须由 Superblock、非零
identifier、generation 和类型共同识别；Path 必须同时保存 mount identity，
不能把跨文件系统相同 inode number 当作同一对象。每个 Process 必须持有
独立 root/cwd FsContext，FileDescription 必须保存 `Vfs + OpenFile`，Shell
和系统调用不得直接读取 legacy inode 或 ATA。

公共路径与组件上限分别为 4096 和 255 字节；绝对/相对路径、重复分隔符、
`.`、`..`、root clamp、尾部分隔符、挂载进入/退出和 getcwd 必须由统一算法
处理。达到长度、遍历或挂载容量时必须返回独立错误，不允许截断、回绕或误建
文件。目录项 ABI 必须完整初始化后再复制到用户态，不得泄露结构填充。

memfs 必须实现完整基础后端，并从 KernelHeap 动态拥有节点与文件数据；
增长、truncate、空洞清零、目录枚举、校验与 Destroy 必须有明确资源守恒。
legacy 适配器必须保留旧磁盘格式、基础创建、读取、同步与一致性检查；未知
非零损坏介质继续拒绝，禁止自动格式化。两个后端必须通过同一契约测试，路径
命名空间必须通过固定种子 100000 步独立参考模型。

挂载拓扑只允许在用户调度前建立，v1.5 不提供动态 unmount。锁顺序必须让
FileTable/KernelObject 锁在进入 FileDescription、VFS 和后端前释放。memfs
等持久挂载资源必须由 VFS 精确登记，并与 Process 最终资源快照分账；扣除
持久资源后任何 frame、KVA、heap、fd 或对象残留仍必须使整机验收失败。

## v1.8 虚拟内存冻结要求

- VMA 必须使用页对齐半开区间，按地址严格排序且互不重叠；相同 kind 与权限
  的相邻区域必须合并。
- 全局 VMA 描述符容量为 8192，单 Process hard limit 为 4096；池耗尽与
  单进程上限必须返回可区分错误。
- 中段 unmap 所需 split 描述符必须在修改前取得；失败时原 VMA 图逐字段不变。
- 匿名窗口固定为 `[0x60000000, 0x80000000)`；自动映射使用 first-fit，
  fixed 映射只接受页对齐空洞并不得覆盖现有区域。
- map 与 break growth 只建立 VMA，不提前分配数据 frame；首次合法读必须
  得到全零页，首次写后的内容在映射生命周期内保持。
- protection 只接受 `NONE`、`R`、`R|W`、`R|X`，未知位、缺少 read 的
  write/execute 和 W+X 必须拒绝。
- 用户 stack 必须预留 8 MiB、按需连续向低地址提交；只有紧邻 committed
  bottom 且与保存用户 RSP 邻近的页 fault 才可增长。栈底下一页永久没有
  VMA。
- page-fault dispatcher 必须先区分 U/S、RSVD、present、write 与 instruction
  位，再查询 VMA；权限 fault、guard、空洞和非法栈跳跃不得创建页面。
- unmap 与 break shrink 必须释放实际驻留 frame，并按页表根所有权回收空
  PT/PD/私有 PDPT；未触及 reservation 不产生虚构释放。
- Kernel 用户复制只可按需解析 Anonymous 与 ProgramBreak，不得在没有用户
  异常现场时伪造 stack growth。
- 用户 ABI 必须以固定宽度系统调用 39..42 提供 map、unmap、break 与
  112 字节统计；已有编号和错误值不得重排。
- `UserHeap` 必须是 freestanding C++20 头源分离组件，支持有界增长、
  16 字节对齐、first-fit、split、前后 coalesce、重复/外部指针拒绝和完整
  结构校验，不得调用 libc 或宿主 allocator。
- VMA 与 UserHeap 纯逻辑必须各有单元和 100000 步固定种子随机参考模型；
  页表/VMA 必须有重复生命周期集成测试；真实 QEMU 必须分别验证成功 fault、
  guard fault 与 protection fault。
- 64 MiB、256 MiB、64 GiB 三档必须运行同一 v1.8 PID1 工作负载；最终 VMA
  active 为零、free 等于 capacity、acquire/release 增量相同，并与既有
  frame、buddy、heap、KVA、stack、fd、object 和 VFS 守恒同时成立。

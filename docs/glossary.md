# 项目词汇表

| 术语 | 含义 |
| --- | --- |
| freestanding | 不假定操作系统、C/C++ 标准运行时或完整标准库存在的编译环境 |
| host | 执行构建和快速测试的宿主机环境 |
| target | 项目生成代码所面向的 x86-64 环境 |
| TCG | QEMU 的动态二进制翻译执行引擎，可在非 x86-64 宿主机上模拟 x86-64 |
| ROM | 映射到处理器复位地址附近的只读固件镜像 |
| reset vector | x86 CPU 复位后开始取指的地址，本项目关注 `0xFFFFFFF0` |
| Stage 1 | 固件从磁盘载入的第一阶段引导代码 |
| ATA PIO | 处理器通过 ATA I/O 端口和数据寄存器主动搬运扇区的传输方式 |
| IDE | 传统 PC 集成磁盘控制器接口；本项目 v0.2 使用其主通道 |
| LBA | Logical Block Address，用连续扇区编号定位块设备数据 |
| BSY | ATA 状态寄存器的设备忙位 |
| DRQ | ATA 状态寄存器的数据请求位，表示数据端口可传输 |
| DF | ATA 状态寄存器的设备故障位 |
| Long Mode | x86-64 的 64 位执行模式 |
| half-open range | 包含起始地址、不包含结束地址的区间 `[begin, end)` |
| property test | 通过大量生成输入验证通用性质的随机测试 |
| ADR | Architecture Decision Record，记录重要架构决策及其原因 |
| cross compilation | 在一种宿主架构上生成面向另一种目标架构或运行环境的代码 |
| ELF | Executable and Linkable Format，保存目标文件、可执行文件和调试信息的格式 |
| `PT_LOAD` | ELF 程序头类型，声明需要复制或清零到内存的可加载段 |
| BSS | ELF 中只占内存、不保存初始化文件字节的零初始化区域 |
| CRC32 | 32 位循环冗余校验；本项目用于检测描述符和 Kernel 文件的偶然损坏 |
| BootInfo | Stage 1 传给内核的版本化固定宽度启动信息结构 |
| `fw_cfg` | QEMU PC 提供的配置硬件接口；本项目通过端口读取 `etc/e820` |
| E820 | 描述物理地址区间、长度和类型的内存图格式 |
| physical frame | 按 4 KiB 页粒度管理的物理内存所有权单位 |
| PFN | Page Frame Number，物理地址除以页大小得到的页帧编号 |
| PML4 / PDPT / PD / PT | x86-64 四级页表的四层结构 |
| page-table root kind | 页表管理器显式声明的 `Exclusive`、`KernelShared` 或 `Process` 所有权边界，决定哪些分支允许修改和回收 |
| borrowed page-table branch | 由另一个根拥有、当前根只通过复制父项引用的页表子树；进程根中的共享内核高半分支属于此类 |
| empty-branch reclamation | 撤销最后一张叶映射后，按 PT、PD、PDPT 从子到父解除并归还独占空表的过程 |
| table-frame ownership | 页表物理地址必须仍是页帧分配器记录的精确 order-0 allocation；地址可读或 Present 不等于拥有 |
| direct-map | 用固定高半区基址加物理地址访问普通 RAM 的线性映射窗口 |
| large page | 跳过最低级 PT 的 2 MiB PDE 叶映射；本项目用于物理直映内部区间 |
| canonical address | x86-64 要求高位为有效地址位符号扩展的线性地址 |
| NX | 页表 no-execute 权限；需要 `IA32_EFER.NXE` 启用 |
| WP | CR0 的 write-protect 位，使 supervisor 写也遵守只读页权限 |
| guard page | 故意保持 not-present，用页故障捕获越界的保护页 |
| TLB | 处理器缓存地址翻译和页权限的 Translation Lookaside Buffer |
| `INVLPG` | 使当前处理器中一个线性地址的 TLB 翻译失效 |
| monotonic heap | 只向前分配、不回收单个对象的早期堆 |
| boundary tag | 在块头保存自身与相邻块尺寸，使分配器能定位物理前后块 |
| coalescing | 释放时把相邻空闲块合并，恢复更大的连续可分配区间 |
| best-fit | 从所有可容纳请求的空闲块中选择最小者的分配策略 |
| type cache | 为同一尺寸与对齐的对象预留等步长槽、独立记录容量与生命周期的缓存；当前实现使用一个堆后备块 |
| slab | 为一类固定尺寸对象提供后备存储的一组页或连续区域；完整 slab 分配器通常还管理空/部分/满列表与回收 |
| in-slot free list | 只在槽空闲时借用槽首保存下一空闲索引，不增加每个活动对象的旁路链节点 |
| buddy allocator | 以二次幂阶拆分与合并连续页块、能够回收物理页的分配器 |
| KVA allocator | Kernel Virtual Address allocator；当前用有序有主区间和隐式空闲缝隙分配 32 TiB 内核虚拟窗口，物理页和映射由调用者另行提交 |
| dynamic kernel stack | 当前由六页 KVA 区间表示的 Ring 0 栈对象：上下双 guard，中间四页使用独立物理后备和 supervisor RW/NX 映射 |
| stack reaper | 在汇编回到永久启动栈后，验证当前 RSP 不属于目标栈并回收终止栈映射、物理页与 KVA 的运行时步骤 |
| safe point | 已知当前执行栈、CR3 和生命周期状态满足销毁前置条件的位置；逻辑终止不自动等于到达安全点 |
| strong reference | 保证对象在引用存续期间不能进入销毁的所有权；最后一个强引用释放才触发销毁资格 |
| reference counter | 记录强所有者数量的状态机；当前原语拒绝零后复活和整数上溢，v1.5 对象管理器在同一把锁内提交查找与计数变化 |
| KernelObject | 由 type、variant、全局单调 generation、强引用和 finalizer 定义生命周期的动态内核对象 |
| FileDescription | fd 背后的共享打开实例，保存种类、file status flags 和文件偏移；duplicate 共享，独立 open 不共享 |
| FileTable | Process 持有的分块 fd 名字空间；表项保存对象 handle 与独立 fd flags，soft/hard limit 是运行时策略 |
| scope rollback | 用外部固定动作数组记录已完成步骤，并在未提交事务退出时严格逆序执行补偿动作的失败展开机制 |
| resource snapshot | 聚合多个管理器稳定当前量、验证内部守恒式并以差异位掩码比较生命周期前后的诊断值 |
| failure atomicity | 操作失败时，既有对象、输出参数和资源所有权保持调用前状态的性质 |
| identity mapping | 虚拟地址与物理地址相同的分页映射，初期用于降低交接复杂度 |
| address space | 一个页表根定义的虚拟地址到物理页及权限的映射集合 |
| PCB | v0.9–v1.1 把 Process、调度现场与固定槽合并的过渡控制块；v1.2 后由 Process/Thread 取代 |
| Process | 共享 AddressSpace、FileTable、FsContext 和 signal disposition 的资源容器，不直接作为调度实体 |
| Thread | 调度器选择的执行实体，拥有 TID、CPU/FXSAVE 现场、内核栈、用户栈、TLS 和 signal mask |
| PID | Process Identifier；从 1 开始单调分配、与对象槽位无关的 64 位 Process 身份 |
| PID1 / init | 进程树唯一根用户进程；当前从 `/sbin/init` 加载，负责创建验收进程、收养孤儿并回收全部子 Zombie |
| process tree | 与调度队列分离的父子身份状态机，决定哪个父 Process 有权 wait 哪个孩子 |
| child process | 由另一个 Alive Process 创建并记录其父身份的 Process；当前 spawn 创建全新而非复制父地址空间 |
| orphan process | 父 Process 已退出但自身尚未被回收的孩子；当前统一重设父进程到 PID1 |
| Zombie | 已停止执行并释放普通运行资源，但退出记录仍等待父进程 wait 的 Process 状态 |
| reap / collect | 父进程消费 Zombie 退出记录后，清除进程树项与调度器槽的最后生命周期步骤 |
| spawn | 从文件路径、argv 和 envp 构造全新孩子的接口；当前语义不复制父地址空间或 FileTable |
| exec | 保持 PID、父子关系与 Process 身份，用候选事务替换当前程序 AddressSpace 和用户现场 |
| wait | 取得指定或任意直接子进程退出记录；孩子仍 Alive 时阻塞，没有孩子时返回明确错误 |
| candidate image | exec 提交前独立构造的地址空间、ELF 段、用户栈和初始现场；失败时可完整丢弃 |
| `argc` / `argv` / `envp` | 用户程序入口的参数数量、参数字符串指针向量和环境字符串指针向量 |
| ELF reader | 只暴露精确 `(offset, length)` 读取的解析输入边界，使同一验证器可读取内存镜像或 VFS 文件 |
| TID | Thread Identifier；与 PID、对象地址和容器槽位相互独立的 64 位身份 |
| TLS | Thread-Local Storage；同一进程内按 Thread 保存独立变量实例的用户区，本项目以 FS-base 定位 |
| FS-base | x86-64 长模式下 FS 段的 64 位线性地址基准；调度切换时随 Thread 恢复 |
| `IA32_FS_BASE` | 保存 FS-base 的架构 MSR；本项目用 `WRMSR/RDMSR` 写入并验证当前 Thread TLS 基址 |
| AddressSpaceId | 与页表根生命周期绑定的 64 位地址空间身份；private futex 用它区分不同进程的同值虚拟地址 |
| private futex | 以 `(AddressSpaceId, user address)` 为 key、只在同一地址空间内等待和唤醒的 futex |
| compare-and-block | 在同一调度临界区读取用户字、比较预期值并登记 Blocked，消除检查与睡眠之间的丢失唤醒窗口 |
| join | 一个 Thread 唯一消费另一个 joinable Thread 的退出值并触发最终回收的协议 |
| condition variable | 用单调 sequence 表达条件可能变化、由等待者在 Mutex 保护下重新检查谓词的同步原语 |
| once | 保证初始化函数只成功发布一次，其余 Thread 等待完成状态的三态同步原语 |
| monotonic clock | 只随经过时间不减的时钟；v1.13 以 PIT 实际除数、整纳秒和余数累计，不表示日期 |
| clock source | 提供经过时间度量的硬件或软件来源，与负责触发中断的 clock event、表示日期的 wall clock 不同 |
| deadline | 64 位单调时间域中的绝对截止时刻；相对时长只在接口边界转换一次 |
| deadline queue | ThreadScheduler 拥有的 512 槽有序等待结构，按 `(deadline, sequence)` 解析到期 Thread |
| timed wait | 同时登记 WaitQueue 和 deadline、由普通通知或 Timeout 中唯一一方完成的阻塞等待 |
| saturation | 算术结果超出固定宽度时保持最大可表示值而不回绕；单调时钟和相对时长换算都采用该语义 |
| signal | 发送到 Process 或进程组、在某个合格 Thread 的用户返回边界同步兑现的异步软件事件 |
| disposition | Process 级的信号处理规则：默认动作、忽略或用户 handler；fork 继承，exec 重置 handler |
| signal mask | 每 Thread 独立的 64 位屏蔽字；屏蔽只推迟普通信号交付，不删除 pending 状态 |
| pending signal | 已发送但尚未提交默认动作、忽略或 handler 交付的信号；普通信号按编号合并为一个 bit |
| signal frame | Kernel 在用户栈上构造的固定 ABI 记录，保存原 UserContext、旧 mask、信号号和一次性 cookie |
| `sigreturn` | 用户 restorer 请求 Kernel 从精确 signal frame 恢复现场的系统调用；必须重新验证所有返回权限 |
| CpuLocal | 当前 BSP 的本地内核状态，保存 current Thread、入口栈、IRQ/抢占深度和重调度标记 |
| UserContext | 把 INT 0x80、SYSCALL、异常和信号返回规范化后的统一用户寄存器现场 |
| FXSAVE / FXRSTOR | 保存和恢复 x87、MMX、SSE/SSE2 扩展现场的 x86 指令 |
| FxSaveArea | 每 Thread 独占、512 字节且 16 字节对齐的 FXSAVE64 内存区域 |
| x87 | 源自 8087 的八槽浮点寄存器栈及其 control/status/tag 等架构状态 |
| XMM | SSE/SSE2 的 128 位寄存器；x86-64 FXSAVE 区保存 XMM0..XMM15 |
| MXCSR | 控制 SIMD 浮点舍入、异常屏蔽和状态标志的 32 位寄存器 |
| context switch | 保存当前执行现场并恢复另一个执行现场，同时切换相关地址空间与内核栈状态 |
| round-robin | 就绪实体按循环次序取得固定时间片的调度策略 |
| time quantum | 一个 Thread 在被抢占前可消费的调度 tick 预算；当前固定为 4 tick |
| preemption | 进程未主动退出时，由时钟中断和调度策略收回 CPU 并切换到其他进程 |
| dispatch | 调度器选择一个进程成为 Running 并恢复其现场的一次动作 |
| blocked | 进程因具名条件暂时不能推进、不参与 Ready 调度，但保留现场等待唤醒的状态 |
| wakeup | 条件变化后把匹配的 Blocked 进程移回 Ready；不保证资源仍归该进程 |
| lost wakeup | 条件检查与登记等待不原子，事件发生在两者之间而被永久错过的并发故障 |
| spin lock | 用原子 read-modify-write 忙等取得的短临界区互斥；持有期间不得睡眠 |
| irq-save spin lock | 取得锁前保存并关闭当前 CPU 中断、释放时恢复原 IF 的短临界区原语 |
| sleep mutex | 竞争失败时把 Thread 放入 WaitQueue，而不是持续占用 CPU 的互斥原语 |
| WaitQueue | 把 Blocked Thread 与可使条件改变的对象关联起来的统一等待队列 |
| WakeReason | 条件满足、超时、信号、关闭或取消中的单赢家等待完成原因 |
| acquire / release | 建立临界区跨执行流可见性和 happens-before 的原子内存顺序 |
| backpressure | 有界缓冲已满时阻止生产者继续提交，使资源占用保持在容量上限内 |
| pipe | 提供顺序字节流的 IPC 对象；空/满、端点关闭和等待者共同决定读写语义 |
| EOF | End Of File；管道中表示缓冲已空且所有写端已关闭，不等同于暂时无数据 |
| broken pipe | 所有读端已关闭后写入无法被消费的永久错误 |
| W^X | 同一内存段不同时具备可写和可执行权限的约束 |
| ABI | Application Binary Interface，规定调用、寄存器、栈和二进制布局的契约 |
| GDT | Global Descriptor Table，x86 分段和特权级切换使用的描述符表 |
| GDTR | 保存 GDT 线性基址与 inclusive limit 的架构寄存器，由 `LGDT`/`SGDT` 访问 |
| IDT | Interrupt Descriptor Table，把 0..255 向量映射到门描述符 |
| IDTR | 保存 IDT 线性基址与 inclusive limit 的架构寄存器，由 `LIDT`/`SIDT` 访问 |
| TSS | Task State Segment；长模式下主要保存 RSP0..RSP2、IST1..IST7 和 I/O bitmap 位置 |
| TR | Task Register，保存当前 TSS 选择子及隐藏描述符状态，由 `LTR`/`STR` 访问 |
| IST | Interrupt Stack Table，允许指定异常门无条件切到 TSS 中的专用栈 |
| interrupt gate | IDT 门类型；进入处理程序时硬件清 IF，返回时由 `IRETQ` 恢复 |
| IRQ | Interrupt Request，设备向中断控制器提出的异步服务请求编号 |
| PIC / 8259A | 传统 PC 可编程中断控制器；双片级联把 IRQ0..15 映射为 IDT 向量 |
| IMR / IRR / ISR | PIC 的屏蔽、待处理和在服务寄存器，分别回答“允许、已请求、正在处理” |
| EOI | End Of Interrupt，处理完成后通知中断控制器释放当前在服务优先级 |
| spurious IRQ | 请求在确认前撤销形成的虚假 IRQ7/IRQ15，必须按 ISR 选择 EOI |
| PIT / 8254 | 使用固定输入时钟和 16 位除数周期产生 IRQ0 的计时器 |
| LAPIC | 每 CPU 的 Local APIC，负责局部中断、优先级、定时器与 EOI |
| virtual wire mode | 把传统 PIC 输出经 LAPIC ExtINT 交付的 APIC 兼容路由 |
| i8042 | PC 键盘/鼠标控制器，通过 `0x60/0x64` 连接 PS/2 设备与 IRQ1/IRQ12 |
| scan code | 键盘报告物理键按下/释放的编码序列，不等同于字符或 Unicode |
| QMP | QEMU Machine Protocol；测试用它产生可重复虚拟键盘输入和管理模拟器 |
| console input FIFO | IRQ1 解码字符进入的 256 字节先进先出缓冲；把异步硬件生产与进程读取解耦 |
| file descriptor | 进程局部的非负整数句柄；描述符表把它解析为控制台、文件、目录或管道对象及访问方向 |
| standard input/output/error | 固定为 fd 0/1/2 的标准输入、标准输出和标准错误约定 |
| Shell | 从标准输入读取并解析命令、再通过系统调用组合内核服务的 Ring 3 用户程序 |
| control operator | Shell 中按前一条实际退出码组织命令的 `;`、`&&`、`||`；只在未引用、未转义位置生效 |
| append redirection | `>>` 或 `2>>`；每次 write 都从当时的文件尾开始，而不是仅在 open 时定位一次 |
| idle state | 没有 Ready 进程但仍存在可唤醒 Blocked 进程时，内核以相邻的 `sti; hlt; cli` 开放中断、等待并恢复临界区的状态 |
| Zombie | 子进程已经停止执行并释放运行资源，但退出状态尚未被父进程 wait 收取的进程状态 |
| reparent | 父进程先退出时，把仍存活或 Zombie 子进程的回收责任转交 PID1 |
| VFS | Virtual File System，用 vnode、挂载和统一操作隔离路径语义与具体磁盘格式 |
| vnode | VFS 中表示一个命名对象身份和操作集合的内存对象，不等同于磁盘 inode 字节布局 |
| superblock | 一个已实例化文件系统后端及其根 vnode、操作表和后端状态的 VFS 对象 |
| mount | 把一个 superblock 的根 vnode 接到现有目录 vnode 上形成的命名空间边 |
| mount namespace | 从根挂载出发、按挂载边组合多个后端后形成的统一路径视图；v1.5 每个 Process 的 `FsContext` 共享同一启动期拓扑 |
| `FsContext` | Process 持有的文件系统上下文，当前保存 root 与 cwd vnode，供绝对和相对路径解析使用 |
| memfs | 由 KernelHeap 支撑、断电即失的内存文件系统；v1.5 挂载于 `/tmp`，名称内联于节点，并精确统计节点与数据容量 |
| rootfs v2 | v1.6 生产根格式；固定 256 MiB 区域，含版本化小端 superblock、bitmap、inode、目录项和三级间接块 |
| rootfs v4 | v2.3 生产根格式；使用完整 128 GiB 参考盘，含 64 位几何、五级块树、链接、时间戳、orphan 与 248-credit journal |
| rootfs v5 | V2 小型 ext4 的项目自研格式；v2.16 已冻结 4 KiB block、block group、256 字节 descriptor/inode、sparse backup 与 CRC32C，但尚未挂载或替换生产 v4 |
| block group | 把文件系统块和 inode 元数据分成局部管理单元；v5 每个完整组为 32768 个 4 KiB 块，并拥有自己的两张 bitmap 与 inode table |
| sparse superblock backup | 只在组 0、1 及组号为 3/5/7 纯幂的组保存 superblock 与完整 GDT 副本，降低固定备份开销并保留恢复证据 |
| CRC32C | 使用 Castagnoli 多项式的 32 位循环冗余校验；v5 用于 superblock、descriptor、inode 和 bitmap，标准向量 `123456789` 为 `0xE3069283` |
| journal descriptor | 描述一次事务的 metadata home target、journal payload index、payload CRC 和标志的记录；v2 每槽一个 |
| commit record | 证明 descriptor、revoke、payload 和 ordered data 已按顺序稳定的哨兵；没有有效 commit 的 prepared 事务不会 replay |
| checkpoint | 把 committed journal payload 写到最终 home block 并释放日志槽；它晚于 commit，可以在正常运行或恢复期间幂等执行 |
| revoke | 较晚 committed transaction 声明某个旧 metadata target 不得再 replay，防止释放或改作他用的块被陈旧日志覆盖 |
| orphan file | 保存已脱离目录但仍需在崩溃后完成 truncate/unlink 的 inode number；v2.17 冻结 CRC32C block 和事务原子性，尚未执行 extent 清理 |
| ordered data | 不写入 metadata journal、但必须在引用它的新 metadata commit 前 Flush 到 home 的文件数据 |
| extent | 用 logical start、physical start 和 block count 表示一段连续文件映射；相邻连续同态范围应合并 |
| unwritten extent | 已占用物理块但尚未发布有效文件数据的 extent；文件系统读取按零处理，SEEK_HOLE 将其视为 hole |
| delayed allocation | page cache 已有脏数据但尚未选择物理块的状态；writeback 才向 allocator 请求连续 run |
| multi-block allocator | 一次从 block-group bitmap 选择连续多个块的分配器；优先局部组并减少 extent 碎片 |
| reservation token | bitmap 临时置位后的 slot+generation 身份；mapping 成功后 commit，失败则 abort 回滚 |
| `SEEK_DATA` / `SEEK_HOLE` | 从给定逻辑位置寻找下一段数据或空洞；v2.18 把 Delayed/Initialized 视为 data，Absent/Unwritten 视为 hole |
| variable dirent | 以 record length 跳到下一项的目录记录；名称只占实际长度并按对齐填充 |
| HTree | 以名称 hash 路由目录 leaf 的有界树；hash 碰撞后仍须比较完整名称 |
| xattr | 由 namespace、名称和二进制值组成的扩展属性；ACL/security metadata 可复用该容器 |
| ACL mask | POSIX ACL 对 named user、group owner 和 named group 权限施加的共同上限 |
| quota grace | 使用量超过 soft limit 后仍允许暂时写入的期限；到期后继续超限会被拒绝 |
| inode | 文件系统内部对象身份；保存类型、逻辑大小、generation、父关系和数据块索引，名字由目录项另行保存 |
| inode generation | inode number 回收复用时递增的身份代次；目录项与 vnode 必须同时匹配编号和代次 |
| direct block | inode 直接保存的数据块指针，小文件无需额外索引块 |
| indirect block | 保存其他数据块或下级指针块号的元数据块；single/double/triple 表示一、二、三级索引 |
| logical block | 由文件字节 offset 换算出的块序号，先映射到盘面相对块，再转换为设备 LBA |
| sparse file | 逻辑大小中包含未分配 hole 的文件；读 hole 返回零，实际分配大小可小于逻辑大小 |
| short write | 写请求只提交连续前缀并返回实际字节数；调用方不得假设请求长度全部落盘 |
| ENOSPC | 文件系统没有足够可分配空间；本项目内部对应 `CapacityExhausted`，零字节写入时返回明确失败 |
| fsck | 独立读取盘面、遍历可达对象并重建 bitmap 的一致性检查器；v1.6 工具只读，不自动修复 |
| Dirty/Clean transaction | 修改前持久化 Dirty、全部数据/元数据 flush 后再写 Clean 的检测协议；能拒绝未完成事务，但不是 journal |
| orphan inode | 目录 link 已为零但仍因打开引用存活的 inode；v1.6 尚未实现，因此相关 unlink/replace 返回 Busy |
| path normalization | 在不改变最终对象语义的前提下处理重复斜杠、`.`、`..`、根边界与尾斜杠目录约束的状态机 |
| open-file description | 保存打开状态、共享文件偏移和 vnode 引用的系统级对象；一个或多个 fd 可以引用它 |
| cwd | Current Working Directory，进程解析相对路径时使用的目录引用 |
| COW | Copy-on-Write，父子暂时共享只读物理页，在首次写故障时再创建私有副本 |
| VMA | Virtual Memory Area，描述用户虚拟区间、来源、权限和映射策略，不表示物理页已经存在 |
| demand paging | 先登记 VMA，在首次访问页故障时才分配或读取实际页面的策略 |
| reservation | 已由 VMA 占有但尚未安装 PTE 或消耗数据页的虚拟地址区间 |
| resident page | 已有有效 PTE 和物理 frame、处理器当前能够实际访问的虚拟页 |
| committed stack bottom | 用户栈已经连续驻留部分的最低页地址；合法增长只允许提交紧邻其下的一页 |
| program break | 传统连续数据区的字节级逻辑末端；v1.8 用页级 VMA 表达其覆盖区，并在首次访问时提交物理页 |
| zero-fill-on-demand | 匿名页第一次合法访问时分配完整清零 frame，再返回重试原指令的策略 |
| page cache | 以 vnode 与页索引为身份缓存文件内容的内存页；clean、dirty、writeback 是不同状态 |
| Loading waiter | 观察到同一文件页正在填充后，登记到唯一 load 并睡眠等待其成功或失败结果的线程；不得再次发起来源读取 |
| page-reference handoff | owner 在完成广播前为每个 Loading waiter 预留真实页面引用，waiter 醒来后直接接管该引用的所有权协议 |
| readahead window | 针对一个打开文件流预测的连续文件页区间；包含当前窗口总大小、异步尾部与触发下一窗口的页 |
| adaptive readahead maximum | 根据 useful/wasted 反馈在 1 页到配置上限间调整、再与内存压力上限取最小值的未来窗口上限 |
| readahead stream token | 用固定槽索引与 generation 标识一个共享打开流的弱身份；页缓存可据此归因，但不能访问 FileDescription 地址 |
| retiring readahead stream | 最后描述引用已关闭、策略对象不再可用，但仍等待活动预读任务释放 token retain 的账本状态 |
| writeback error sequence | 文件级单调错误序列；独立打开实例以自己的游标判断是否还有未报告的写回失败 |
| fsync / fdatasync | 等待指定打开文件的数据稳定；fsync 包含完整 metadata，fdatasync 至少包含重读所需 metadata |
| msync | 按文件映射虚拟地址范围请求异步或同步写回；private COW 修改不进入底层文件 |
| direct reclaim | 当前分配线程在返回失败前同步执行的 clean 回收、dirty 写回和匿名换出 |
| background reclaim | free pages 低于 low watermark 后由 Kernel Worker 分批执行、达到 high watermark 后休眠的异步回收 |
| watermark hysteresis | 用不同的启动 low 与停止 high 阈值避免后台回收在边界反复唤醒和休眠 |
| reclaim candidate | 已在 Inactive 状态连续冷却一轮且通过全部 alias 资格检查的显式 PageAging 条目 |
| reclaim backoff | 无候选、仅完成写回或失败后等待 deadline 再重试，防止 Worker 忙循环的状态 |
| reclaim progress | 一轮回收实际归还的页数；计划数或仅完成写盘但仍被引用的页不算进展 |
| `MAP_PRIVATE` | 写入时产生私有 COW 页面、不把修改回写到底层文件的文件映射 |
| `MAP_SHARED` | 多个映射观察同一文件页的策略；完整页可写映射由首次写保护故障标脏，显式 sync 回写 |
| futex | 以用户地址上的值作为快速路径、仅在竞争时进入内核 WaitQueue 的同步机制 |
| process group | 用于信号投递和终端作业控制的一组进程身份 |
| session | 包含一个或多个进程组并关联控制终端的作业控制边界 |
| line discipline | 位于字符设备和用户读取之间，处理 canonical 输入、退格、EOF 与控制字符的终端状态机 |
| ShellEditor mode | 只允许控制终端前台 Shell 启用的逐字节无回显模式；外部作业仍切回 Canonical |
| glob | 把未引用、未转义的 `*`/`?` 与目录项匹配并按字节序扩展为 argv 的 Shell 步骤 |
| CSI | Control Sequence Introducer；本项目 VGA 支持光标、清行和清屏的有界 ANSI 子集 |
| RTC / CMOS | PC 端口 `0x70/0x71` 暴露的电池墙钟；date 读取稳定快照，不能用于 deadline |
| controlling terminal | 由一个 session 取得并保存其前台进程组的终端 |
| foreground process group | 当前被控制终端允许读取输入并接收终端控制信号的进程组 |
| stopped process | 保留地址空间、资源与执行现场但暂不参与调度，等待 SIGCONT 的进程 |
| job | Shell 维护的一条命令或管线；全部成员共享一个 PGID |
| journal | 文件系统提交前记录可重放事务的持久区域；v2.0 只记录 ordered metadata |
| transaction credit | journal 在修改前为事务预留的元数据块额度，防止执行到一半才发现日志空间不足 |
| ordered mode | 先持久化相关文件数据、再允许元数据 commit 落盘的 journal 顺序约束 |
| BlockRequest | 表示一次可等待设备 I/O 的独立对象，具有提交、完成、错误和超时状态 |
| completion FIFO | 按 IRQ、timeout、cancel 首次解析发生顺序保存块请求终态，交付后立即回收请求槽的有界队列 |
| AsynchronousBlockDevice | 以静态函数表统一设备 geometry、submit、best-effort cancel、timeout 和 completion 的类型擦除接口 |
| User Kernel continuation | User Thread 在深层 Kernel 路径阻塞时，由独立 Kernel stack 连同 FX、syscall、GS 与 CR3 模式保存的可恢复执行点 |
| RuntimeMutex | 调度运行期以 Mutex/WaitQueue 睡眠、early boot 或不可睡眠边界退化为短 SpinLock 的固定布局互斥原语 |
| initializing thread | 已取得 Thread 身份但尚未完成跨模块元数据提交、因此不能进入 Ready queue 的发布前状态 |
| BlockIo ticket | 由协调器槽位与单调 generation 组成的等待凭据；同时核对 owner/request id，防止槽位复用后的旧等待取得新结果 |
| FilePageLoad token | 由文件页 load 槽位与 generation 组成的等待凭据；配合文件页身份、frame 和 load generation 拒绝旧 waiter 误取复用槽结果 |
| completion worker | 在非 IRQ Kernel Thread 上消费设备 completion、执行 DMA 数据收尾并精确唤醒 BlockIo owner 的常驻 bottom-half |
| completion-before-wait | 设备在调用者提交 WaitQueue 阻塞前已经完成的竞争；协调器必须让调用者直接取结果，不能丢失事件 |
| shallow I/O delegation | 把深层 VFS/cache/swap 请求复制到稳定 request 对象，由浅层 Kernel I/O Thread 提交和睡眠，避免保留任意 C++ 调用栈或持锁阻塞 |
| command identifier | NVMe CQE 使用的 16 位硬件命令身份；可回绕，不等于上层 64 位 request identifier |
| `SYSCALL` / `SYSRET` | x86-64 快速特权转换指令；需要 MSR、内核栈、RFLAGS 掩码和 canonical 返回地址共同保证安全 |
| CpuLocal | 每 CPU 的内核本地状态；当前单 BSP 实例保存 current Thread、可信入口栈、IRQ/抢占深度和重调度请求 |
| UserContext | 统一保存初始进入、IRQ、INT 0x80 与 SYSCALL 用户现场的 176 字节结构 |
| `SWAPGS` | 交换当前 GS base 与 IA32_KERNEL_GS_BASE，使特权入口无需先占用通用寄存器即可取得 CpuLocal |
| STAR / LSTAR | SYSCALL/SYSRET 的段选择子 MSR 与 64 位入口 RIP MSR |
| FMASK | SYSCALL 进入 Ring 0 时从活动 RFLAGS 清除指定标志的 MSR；不改写已复制到 R11 的用户原值 |
| canonical address | x86-64 要求高位为已实现最高地址位符号扩展的虚拟地址；当前项目冻结四级 48 位规则 |
| return whitelist | 在执行 SYSRET 前对 frame 所有权、RIP/RSP、映射、段和 RFLAGS 的联合许可集合 |
| need-resched | IRQ 只提交、由返回用户态边界消费的延迟调度请求，避免任意 Ring 0 调用链非局部换栈 |
| exception vector | CPU 为异常选择的 0..31 编号，例如 3=#BP、6=#UD、14=#PF |
| exception error code | 部分异常由 CPU 压栈的原因字段；无错误码异常由项目桩规范化为零 |
| `IRETQ` | 64 位中断返回指令，恢复 RIP、CS、RFLAGS 以及可选旧 RSP/SS |
| CR2 | 保存最近一次页故障线性地址的控制寄存器 |
| panic | 内核无法安全恢复时输出有限诊断、禁止继续执行并停机的终止协议 |
| control register | CR0、CR3、CR4 等控制处理器模式、分页和特性的寄存器 |
| EFER | Extended Feature Enable Register，包含长模式启用等控制位的 MSR |
| red zone | System V AMD64 ABI 中栈指针下方可供叶函数使用、但不适合内核中断环境的区域 |
| static library | 由多个可重定位目标文件组成的归档，本身不等于最终可执行镜像 |
| symbol audit | 检查目标文件架构和未解析符号，防止引入未知运行时依赖 |
| hidden segment cache | x86 段寄存器内部保存的基址、界限和属性；复位时 CS 隐藏基址决定高端取指地址 |
| near jump | 只修改当前代码段内指令偏移、不重载 CS 的跳转 |
| VGA text console | 项目当前用户前台；字符与属性写入 `0xB8000` 的 80×25 文本页 |
| guest system log | `0x20000..0x9FFFF` 的只追加内存记录，保存启动诊断和终端转录并可由宿主导出 |
| output mode | 共享输出头中的启动/终端模式；决定普通诊断是否同时渲染到 VGA |
| COM1 | 传统 PC 第一串口，默认 I/O 基址为 `0x3F8`；仅用于历史版本说明 |
| 16550A | 项目早期版本使用的 UART 寄存器模型 |
| RFLAGS | x86 架构标志寄存器；本项目重点使用 CF、ZF、IF、DF |
| LSR | 16550A 线路状态寄存器；bit 5 THRE 表示发送保持寄存器为空 |
| ATA STATUS | IDE 命令块状态寄存器；本项目重点处理 BSY、DRQ、ERR、DF |
| ERROR | ATA 错误寄存器；记录 ABRT、IDNF、UNC 等设备错误原因 |
| DLAB | UART 线路控制寄存器中的除数锁存访问位 |
| THRE | UART 线路状态中的发送保持寄存器为空标志 |
| fault injection | 主动制造设备或输入失败，以验证错误路径和恢复契约 |
| memory watermark | 以 free page 数表示的 min/low/high 阈值；决定保留、直接回收和停止回收的边界 |
| resident budget | 允许来宾驻留的物理页帧上限；4 GiB 手机档等于实际预分配 RAM |
| swap slot | secondary ATA 交换盘中保存一个 4 KiB 匿名页的固定编号位置；身份与校验和位于磁盘哈希桶 |
| overcommit | 在建立 VMA 时允许虚拟承诺超过即时空闲 RAM 的策略；模式编号 0/1/2 与 Linux 一致 |
| commit limit | overcommit accountant 允许同时承诺的总页数上限 |
| OOM score | 根据 resident+swap 占用和 adjustment 计算的牺牲者优先级 |
| swappiness | 0..200 的 file/anonymous 回收权重；0 禁止匿名 swap，200 优先匿名，候选不足时可转赠预算 |
| reclaim budget | 单个 direct 或 background 批次分配给 file/anonymous 类别的目标页数，不等于实际完成数 |
| Kernel Thread | 不属于用户 Process、使用内核 CR3 和动态 KernelStack 执行内核入口的调度实体 |
| dispatcher stack | User 或 Kernel 调度入口保存的内核调用栈；跨类型切换先回到 dispatcher，再由目标现场形状重新进入 |
| cooperative switch | 当前线程主动 yield/block/exit 才发生的上下文切换；不表示 timer 抢占 |
| WorkQueue | 用 generation handle 管理即时 FIFO、延迟任务、取消、完成和 drain 的内核任务队列 |
| work expediting | 即时请求把同一 handle 的 Delayed 项从 deadline heap 提升到 ready FIFO，避免继续等待旧截止时间 |
| writeback Worker | 与 User Thread 同批运行、在 WorkQueue 锁外分批写脏文件页的常驻 Kernel Thread；IRQ 只负责唤醒 |
| writeback generation | 每次文件页进入 Writeback 时分配的 64 位代次；与文件页身份和物理地址共同定位唯一页级 I/O |
| writeback waiter | 在同页 I/O 完成前等待唯一成功/失败结果的 writer 或同步调用者；不自行重复提交设备请求 |
| dentry | 目录中的“parent + name”到 inode 的命名空间关系；Positive 指向对象，Negative 表示已确认不存在 |
| inode cache identity | 由 superblock 与 node identifier/generation 组成、可被多个 mount dentry 共享的节点身份 |
| inode metadata | VFS 从 backend stat 取得的 size、allocated size、link count、时间、uid、gid 和 mode 原始快照 |
| metadata load ticket | 同时绑定 inode slot/generation 与 metadata generation 的一次加载所有权；失效后迟到结果不能提交 |
| metadata bypass | Loading 竞争或缓存容量/代次耗尽时直接读取 backend、只返回当前调用且不填充缓存的正确性路径 |
| dentry miss owner | 在 resolution transaction 内对一个冷 key 执行唯一 backend lookup 并发布正负结果的调用者 |
| namespace hash entry | 把 Cached dentry/inode 槽接入固定 bucket 链的索引节点；Stale 不在链中 |
| namespace shrinker | 在容量或内存压力下回收零引用 Cached 逻辑条目的有界入口；固定 backing 不计作物理页回收 |
| stale dentry | 已从新 lookup 中撤销、但因旧引用尚未释放而继续保留 identity 和 generation token 的目录项 |
| PTE Accessed | x86 页表叶项的 A 位；硬件在翻译被使用时置位，内核清除后可观察下一周期是否再次访问 |
| active/inactive | 经典双队列近似 LRU；Active 表示近期访问，Inactive 连续未访问后才可成为回收候选 |
| aging round | 周期 Worker 对 file cache 与全部用户 PTE 完成一次 alias 聚合和冷热状态转换的事务 |
| reclaim candidate | 连续两轮未访问且没有 pinned/dirty/mapped 等排除条件的 Inactive 身份；第四增量只统计，不执行回收 |
| drain | 封闭新的任务提交，并等待已有 Delayed/Queued/Running 全部到达终态的屏障 |
| release identity | 项目、ABI、盘面、机器规格、来宾标记和主仓 SHA 共同组成的发布身份 |
| structured disk identity | 对大盘固定关键范围、长度和宿主分配状态的哈希清单，避免全读空闲零区 |
| soak | 在同一冻结产物上有界重复完整整机工作负载，用于发现跨轮次和长尾错误 |
| inode I/O identity | 由 superblock 与 node 的 identifier/generation 组成、用于串行同一文件逻辑 size 与映射提交的稳定身份 |
| inode I/O guard | 持有某一 inode 活跃槽 RuntimeMutex 的 RAII 临界区；前台写、truncate、共享 mmap dirty 与同步共用 |
| block group | 把磁盘切成局部 inode/data bitmap、inode table 和数据区的分配单元，降低全盘扫描与碎片 |
| extent | 用逻辑起点、物理起点和连续块数描述一段文件映射；hole、unwritten 与 initialized 状态必须区分 |
| delayed allocation | buffered write 先保留逻辑空间，在 writeback 时依据连续脏范围选择物理 extent 的分配策略 |
| HTree | 以文件名 hash 定位目录叶块的有界索引；readdir 仍按稳定目录记录遍历叶块 |

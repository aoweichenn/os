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
| KVA allocator | Kernel Virtual Address allocator；只分配内核虚拟区间，物理页和映射由调用者另行提交 |
| identity mapping | 虚拟地址与物理地址相同的分页映射，初期用于降低交接复杂度 |
| address space | 一个页表根定义的虚拟地址到物理页及权限的映射集合 |
| PCB | v0.9–v1.1 把 Process、调度现场与固定槽合并的过渡控制块；v1.2 后由 Process/Thread 取代 |
| Process | 共享 AddressSpace、FileTable、FsContext 和 signal disposition 的资源容器，不直接作为调度实体 |
| Thread | 调度器选择的执行实体，拥有 TID、CPU/FXSAVE 现场、内核栈、用户栈、TLS 和 signal mask |
| PID | Process Identifier；本项目 v0.9 使用从 1 开始单调分配的 64 位标识符 |
| TID | Thread Identifier；与 PID、对象地址和容器槽位相互独立的 64 位身份 |
| CpuLocal | 当前 BSP 的本地内核状态，保存 current Thread、入口栈、IRQ/抢占深度和重调度标记 |
| UserContext | 把 INT 0x80、SYSCALL、异常和信号返回规范化后的统一用户寄存器现场 |
| FXSAVE / FXRSTOR | 保存和恢复 x87、MMX、SSE/SSE2 扩展现场的 x86 指令 |
| context switch | 保存当前执行现场并恢复另一个执行现场，同时切换相关地址空间与内核栈状态 |
| round-robin | 就绪实体按循环次序取得固定时间片的调度策略 |
| time quantum | 一个进程在被抢占前可消费的调度 tick 预算；v0.9 固定为 4 tick |
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
| idle state | 没有 Ready 进程但仍存在可唤醒 Blocked 进程时，内核以相邻的 `sti; hlt; cli` 开放中断、等待并恢复临界区的状态 |
| Zombie | 子进程已经停止执行并释放运行资源，但退出状态尚未被父进程 wait 收取的进程状态 |
| reparent | 父进程先退出时，把仍存活或 Zombie 子进程的回收责任转交 PID1 |
| VFS | Virtual File System，用 vnode、挂载和统一操作隔离路径语义与具体磁盘格式 |
| vnode | VFS 中表示一个命名对象身份和操作集合的内存对象，不等同于磁盘 inode 字节布局 |
| open-file description | 保存打开状态、共享文件偏移和 vnode 引用的系统级对象；一个或多个 fd 可以引用它 |
| cwd | Current Working Directory，进程解析相对路径时使用的目录引用 |
| COW | Copy-on-Write，父子暂时共享只读物理页，在首次写故障时再创建私有副本 |
| VMA | Virtual Memory Area，描述用户虚拟区间、来源、权限和映射策略，不表示物理页已经存在 |
| demand paging | 先登记 VMA，在首次访问页故障时才分配或读取实际页面的策略 |
| page cache | 以 vnode 与页索引为身份缓存文件内容的内存页；clean、dirty、writeback 是不同状态 |
| `MAP_PRIVATE` | 写入时产生私有 COW 页面、不把修改回写到底层文件的文件映射 |
| `MAP_SHARED` | 多个映射观察同一文件页的策略；v2.0 仅支持只读形式 |
| futex | 以用户地址上的值作为快速路径、仅在竞争时进入内核 WaitQueue 的同步机制 |
| process group | 用于信号投递和终端作业控制的一组进程身份 |
| session | 包含一个或多个进程组并关联控制终端的作业控制边界 |
| line discipline | 位于字符设备和用户读取之间，处理 canonical 输入、退格、EOF 与控制字符的终端状态机 |
| journal | 文件系统提交前记录可重放事务的持久区域；v2.0 只记录 ordered metadata |
| transaction credit | journal 在修改前为事务预留的元数据块额度，防止执行到一半才发现日志空间不足 |
| ordered mode | 先持久化相关文件数据、再允许元数据 commit 落盘的 journal 顺序约束 |
| BlockRequest | 表示一次可等待设备 I/O 的独立对象，具有提交、完成、错误和超时状态 |
| `SYSCALL` / `SYSRET` | x86-64 快速特权转换指令；需要 MSR、内核栈、RFLAGS 掩码和 canonical 返回地址共同保证安全 |
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
| COM1 | 传统 PC 第一串口，默认 I/O 基址为 `0x3F8` |
| 16550A | 项目最早使用的 UART 寄存器模型 |
| RFLAGS | x86 架构标志寄存器；本项目重点使用 CF、ZF、IF、DF |
| LSR | 16550A 线路状态寄存器；bit 5 THRE 表示发送保持寄存器为空 |
| ATA STATUS | IDE 命令块状态寄存器；本项目重点处理 BSY、DRQ、ERR、DF |
| ERROR | ATA 错误寄存器；记录 ABRT、IDNF、UNC 等设备错误原因 |
| DLAB | UART 线路控制寄存器中的除数锁存访问位 |
| THRE | UART 线路状态中的发送保持寄存器为空标志 |
| fault injection | 主动制造设备或输入失败，以验证错误路径和恢复契约 |

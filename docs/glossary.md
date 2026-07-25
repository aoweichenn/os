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
| PML4 / PDPT / PD / PT | x86-64 四级页表的四层结构 |
| canonical address | x86-64 要求高位为有效地址位符号扩展的线性地址 |
| NX | 页表 no-execute 权限；需要 `IA32_EFER.NXE` 启用 |
| WP | CR0 的 write-protect 位，使 supervisor 写也遵守只读页权限 |
| guard page | 故意保持 not-present，用页故障捕获越界的保护页 |
| TLB | 处理器缓存地址翻译和页权限的 Translation Lookaside Buffer |
| `INVLPG` | 使当前处理器中一个线性地址的 TLB 翻译失效 |
| monotonic heap | 只向前分配、不回收单个对象的早期堆 |
| identity mapping | 虚拟地址与物理地址相同的分页映射，初期用于降低交接复杂度 |
| address space | 一个页表根定义的虚拟地址到物理页及权限的映射集合 |
| PCB | Process Control Block，保存 PID、状态、地址空间、现场与资源归属的进程控制块 |
| PID | Process Identifier；本项目 v0.9 使用从 1 开始单调分配的 64 位标识符 |
| context switch | 保存当前执行现场并恢复另一个执行现场，同时切换相关地址空间与内核栈状态 |
| round-robin | 就绪实体按循环次序取得固定时间片的调度策略 |
| time quantum | 一个进程在被抢占前可消费的调度 tick 预算；v0.9 固定为 4 tick |
| preemption | 进程未主动退出时，由时钟中断和调度策略收回 CPU 并切换到其他进程 |
| dispatch | 调度器选择一个进程成为 Running 并恢复其现场的一次动作 |
| blocked | 进程因具名条件暂时不能推进、不参与 Ready 调度，但保留现场等待唤醒的状态 |
| wakeup | 条件变化后把匹配的 Blocked 进程移回 Ready；不保证资源仍归该进程 |
| lost wakeup | 条件检查与登记等待不原子，事件发生在两者之间而被永久错过的并发故障 |
| spin lock | 用原子 read-modify-write 忙等取得的短临界区互斥；持有期间不得睡眠 |
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

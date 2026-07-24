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
| identity mapping | 虚拟地址与物理地址相同的分页映射，初期用于降低交接复杂度 |
| W^X | 同一内存段不同时具备可写和可执行权限的约束 |
| ABI | Application Binary Interface，规定调用、寄存器、栈和二进制布局的契约 |
| GDT | Global Descriptor Table，x86 分段和特权级切换使用的描述符表 |
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

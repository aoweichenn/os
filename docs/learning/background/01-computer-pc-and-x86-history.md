# B1：计算机、PC 平台与 x86 历史

## 1. 本册目标

本册建立最底层心智模型：

```text
电源与时钟
  → reset
  → CPU 在固定地址取指
  → 指令读写寄存器、内存和设备
  → 软件逐层建立运行环境
```

读完后应理解，操作系统不是“一个功能很多的普通程序”，而是在不存在进程、
文件、终端和运行库的条件下，逐步建立这些抽象的第一段软件。

## 2. “计算机”至少包含哪些角色

为了学习操作系统，可以先把机器抽象为四类角色：

| 角色 | 主要职责 | 项目中的例子 |
| --- | --- | --- |
| CPU | 取指、译码、执行、产生地址、检查权限 | QEMU x86-64 CPU model |
| RAM | 保存当前代码和可变状态 | QEMU `-m` 提供的物理内存 |
| 持久设备 | 断电后保存字节 | 2 MiB raw IDE disk |
| I/O 与中断设备 | 与外界通信、产生异步事件 | UART、PIC、PIT、PS/2、ATA |

它们不是通过 C++ 函数相互调用，而是通过平台协议连接：

- CPU 对内存地址发起 load/store。
- CPU 对 I/O port 执行 `in/out`。
- CPU 对 MMIO 地址执行普通内存访问。
- 设备通过 IRQ 请求 CPU 暂停当前控制流。
- CPU 按架构规则查 IDT、换栈并保存现场。

软件类型和函数最终必须落到这些动作之一。

## 3. CPU 的最小执行循环

经典教学模型是：

```text
fetch → decode → execute → retire → next instruction
```

### 3.1 Fetch

CPU 根据当前指令地址取机器码。x86-64 中常把它称为 RIP，但复位早期还要结合
CS 的隐藏状态形成取指地址。

取指本身也是内存访问：

- 分页开启后要先做地址翻译。
- 页必须 present 且允许 execute。
- cache/TLB 可能加速访问。
- 发生权限或不存在错误时产生异常，而不是返回普通错误码。

### 3.2 Decode

x86 指令长度可变，前缀、opcode、ModR/M、SIB、displacement 和 immediate
共同决定语义。汇编器把人类写的：

```asm
mov rax, [rbx + 8]
```

编码成机器字节；CPU 不认识符号名 `rax` 或源文件行号。

### 3.3 Execute

执行可能：

- 计算整数。
- 读写寄存器。
- 产生内存地址。
- 更新 RFLAGS。
- 改变控制流。
- 读写端口。
- 修改 CR0/CR3/CR4、MSR 等特权状态。

### 3.4 Retire 与可见状态

现代 CPU 内部可能乱序、预测并并行执行很多微操作，但架构保证软件观察到一个
受规则约束的提交结果。操作系统主要针对 ISA 与内存模型编程，不依赖某个具体
流水线实现。

## 4. ISA、微架构和平台不要混淆

### 4.1 ISA

Instruction Set Architecture 定义软件可见契约：

- 指令与编码。
- 寄存器。
- 执行模式。
- 异常和中断。
- 页表格式。
- 特权检查。
- 内存模型的一部分。

本项目面向 x86-64 ISA。

### 4.2 微架构

微架构是某颗 CPU 怎样实现 ISA，例如：

- pipeline 深度。
- cache 大小。
- branch predictor。
- execution ports。
- reorder buffer。

教学系统目前不做特定型号性能优化，只通过 CPUID 查询少量架构能力，例如物理
地址宽度。

### 4.3 平台

ISA 不规定整台 PC 有 COM1、IDE 或 8259A。平台模型补充：

- 设备和端口地址。
- 中断连线。
- 固件映射。
- 总线拓扑。
- 内存洞。

本项目固定 QEMU `pc` 风格平台。换成 ARM64 或 x86 的现代纯 PCIe/UEFI 平台，
即使高级调度和文件系统算法还能复用，复位、端口和中断代码也要改变。

## 5. 电源、时钟与复位

### 5.1 为什么需要时钟

同步数字电路通常在时钟边沿推进状态。电源刚稳定时，各寄存器不能处于任意
随机组合，否则 CPU 不知道第一条指令在哪里。

平台通过 reset 信号让处理器进入架构定义状态。它不会把所有晶体管“清零”，
而是让软件可见寄存器和控制逻辑满足启动规范。

### 5.2 Reset 不等于启动完成

Reset 只提供最小起点：

- 一个规定的取指位置。
- 一个规定的初始执行模式。
- 某些控制寄存器默认值。
- 中断通常尚不可安全使用。

它不提供：

- 可用 C++ 栈。
- 已初始化 RAM allocator。
- 串口输出函数。
- 磁盘文件。
- 页表。
- 用户进程。

所有后续能力必须由软件因果链建立。

## 6. 为什么有复位向量

CPU 需要在没有文件系统、驱动和设备枚举的情况下找到第一条指令。最简单可靠的
硬件契约是固定地址。

x86 复位取指位于物理地址 `0xFFFFFFF0` 附近。传统设计让它落在 4 GiB 顶部
固件窗口，平台把 ROM 映射到这里。

当前项目的 128 KiB ROM：

```text
0xFFFE0000  ROM begin
...
0xFFFFFFF0  reset vector
0xFFFFFFFF  last byte below 4 GiB
```

复位向量只有最后 16 bytes 可方便使用，所以通常放短跳转，再转到 ROM 中更大
初始化区域。[v0.1 文档](../02-v0.1-reset-and-serial.md) 会逐字节解释当前
near jump。

## 7. 为什么现代 x86-64 仍背着 1978 年的形状

x86 的核心特点不是“从头设计得很整齐”，而是向后兼容演进。新处理器要继续
运行大量旧软件，于是旧机制常被弱化、扩展或绕过，却很少彻底删除。

## 8. 8086：16 位寄存器与分段

### 8.1 当时的问题

8086 通用寄存器和偏移量主要是 16 位，只能直接表示 64 KiB。设计者希望访问
约 1 MiB 地址空间，于是使用：

```text
physical = segment × 16 + offset
```

segment 来自 CS/DS/SS/ES，offset 来自 IP、SP/BP/SI/DI 等。

例如：

```text
1234:0010 → 0x12340 + 0x0010 = 0x12350
1235:0000 → 0x12350 + 0x0000 = 0x12350
```

不同 segment:offset 可以指向同一物理字节，这叫 aliasing。

### 8.2 对启动代码的影响

复位早期的：

- CS:IP 决定取指。
- SS:SP 决定栈。
- DS/ES 决定许多数据访问。

因此 16 位汇编不能只看 offset 数值。项目会显式初始化段寄存器和栈，而不
继承未知环境。

## 9. 20 位地址与 A20

8086 物理地址总线只有 20 位。`0xFFFF:0x0010` 的算术结果是 `0x100000`，
在 20 位硬件上回绕为 `0x00000`。

后续处理器有更多地址线，但为兼容依赖回绕的旧程序，PC 平台提供 A20 gate：

- A20 关闭：bit 20 被强制为 0，1 MiB 边界可能回绕。
- A20 开启：可正常访问 1 MiB 以上。

现代 64 位系统看似不需要这项古老机制，但从兼容启动状态进入高地址前仍要明确
开启并验证。[v0.3 文档](../04-v0.3-long-mode.md) 使用两个相差 1 MiB 的
地址测试 alias 是否消失。

## 10. 80286：保护模式与描述符

实模式的 segment 值直接参与地址计算，没有现代意义上的进程隔离。
80286 引入保护模式，把段寄存器解释为 selector：

```text
selector → GDT/LDT descriptor → base + limit + type + privilege
```

描述符允许硬件检查：

- 是否 present。
- 是代码还是数据。
- 可读、可写或可执行。
- privilege 是否允许。
- offset 是否越过 limit。

这建立了 Ring、GDT、门等形状。286 的任务和分段模型过于复杂，现代 64 位
系统通常不靠段 limit 做地址空间隔离，但特权级、代码段属性、TSS 和 IDT
仍继承该体系。

## 11. 80386：32 位、分页和更实用的保护

80386 引入：

- 32 位通用寄存器。
- 32 位 offset。
- 4 GiB 线性地址空间。
- 两级分页。
- CR0/CR2/CR3 等控制寄存器语义。

地址路径变成：

```text
logical address
  → segmentation
  → linear address
  → paging
  → physical address
```

许多系统采用 flat segmentation：

- 段 base=0。
- limit 覆盖整个线性空间。
- 地址隔离交给页表。

x86-64 延续这个方向。

## 12. 从 PAE 到 NX

### 12.1 为什么需要 PAE

32 位线性地址仍只有 4 GiB，但机器物理 RAM 可以更大。Physical Address
Extension 扩大页表项和物理帧号，并引入更深的分页结构。

进入 IA-32e long mode 前必须设置 `CR4.PAE`，这是当前模式切换顺序的一部分。

### 12.2 NX

早期页表主要区分 present、read/write 和 user/supervisor。NX
（No Execute）允许把数据页标为不可执行：

- 代码页可执行但不可写。
- 数据、栈和多数映射可写但不可执行。

它为 W^X 和阻止数据作为指令执行提供硬件基础。项目通过
`IA32_EFER.NXE` 和页表 NX bit 建立这一权限。

## 13. APIC 为什么会与 PIC 同时出现

最初 PC 使用 8259A PIC，适合少量 IRQ 和单处理器。多处理器与更多中断需要
Local APIC 和 I/O APIC：

- Local APIC 属于每个 CPU。
- I/O APIC 路由设备中断到目标 CPU。
- legacy PIC 仍需兼容。

当前项目先保留 8259A 的 IRQ 编号和 EOI 语义，再显式配置 LAPIC LINT0 为
ExtINT，让 PIC 输出进入 CPU。这种 virtual-wire 路径就是历史兼容叠层的结果，
不是两个模块重复实现同一件事。

## 14. AMD64：不是简单“寄存器变宽”

AMD64/Intel 64 增加：

- 64 位通用寄存器和 RIP。
- R8..R15。
- IA-32e 四级分页。
- canonical virtual address 规则。
- 更大的物理地址空间能力。
- 64 位代码子模式和 compatibility 子模式。
- RIP-relative addressing。

### 14.1 Long mode 与 64-bit mode

术语要区分：

- IA-32e/long mode 是大模式。
- 其中包含 64-bit mode 和 compatibility mode。
- 开启 LME/PG 后，还要加载 L=1 的代码段才执行 64 位指令。

因此 `EFER.LME=1` 不等于当前代码已经是 64 位。

### 14.2 Canonical address

当前常见四级分页使用虚拟地址低 48 位，bit 63..48 必须复制 bit 47：

```text
low canonical:   0x0000000000000000..0x00007FFFFFFFFFFF
high canonical:  0xFFFF800000000000..0xFFFFFFFFFFFFFFFF
```

中间数值不是合法地址，即使能装进 `uint64_t`。未来五级分页可扩大有效位数，
所以代码应从能力和布局推导，不能把“64 位整数”当成 64 位地址空间全部可用。

## 15. 为什么模式切换有严格顺序

进入项目的 64 位代码前，需要同时满足：

```text
A20 enabled
GDT contains valid protected/64-bit code descriptors
CR3 points to valid aligned tables
CR4.PAE = 1
IA32_EFER.LME = 1
CR0.PE = 1
CR0.PG = 1
far control transfer loads 64-bit CS
current code/data/stack remain mapped
```

这些条件不是“建议”：

- 页表地址错，开启 PG 后下一次取指就 fault。
- fault handler 尚未建立，会继续 fault。
- 继续失败可能形成 double fault、triple fault。
- CPU reset 后表面现象像“跳转突然重新开始”。

所以 Stage 1 每建立一个不可逆状态，都先输出标记或做写后读回，避免把十项风险
压成一次黑盒跳转。

## 16. PC 是一组历史设备协议

“PC”不是 x86 ISA 的同义词。当前 QEMU 平台还包括：

```text
CPU
 ├─ RAM
 ├─ Local APIC
 └─ I/O instructions / MMIO

legacy chipset
 ├─ 8259A PIC
 ├─ 8254 PIT
 ├─ i8042 PS/2 controller
 ├─ 16550-compatible UART
 └─ IDE ATA controller
```

每个设备有独立寄存器和状态机。CPU 不理解“打印字符串”或“读取文件”：

- UART 只接收/发送字节。
- ATA 只按扇区和命令传输。
- 键盘只产生扫描码。
- PIT 只按分频产生周期事件。

Kernel 把这些原始能力组合成 console、block device、filesystem 和 timer。

## 17. Firmware、bootloader 与 Kernel 的职责

商业 PC 常见：

```text
platform firmware
  → firmware service / boot manager
  → third-party bootloader
  → kernel
```

本项目为了完整学习，固定：

```text
self-owned ROM
  → self-owned Stage 1
  → self-owned ELF loader
  → self-owned Kernel
```

### 17.1 ROM

必须在极受限状态下：

- 建立段和栈。
- 初始化 UART。
- 通过 ATA 找到并验证 Stage 1。
- 把它复制到 RAM。
- 转移控制。

### 17.2 Stage 1

必须：

- 开 A20。
- 进入 protected/long mode。
- 获取内存图。
- 读取 Kernel container。
- 验证 ELF。
- 装载 PT_LOAD。
- 建 BootInfo。

### 17.3 Kernel

必须重新接管长期状态：

- 自己的 GDT/TSS/IDT。
- 内存所有权和页表。
- 设备、中断和用户环境。

Kernel 不能永久依赖 Stage 1 的临时页表或私有栈。

## 18. QEMU TCG 的边界

QEMU TCG 在宿主机上模拟目标指令与设备：

```text
guest x86 instruction
  → TCG translation
  → host instruction sequence
  → emulated device/RAM state
```

允许 QEMU 提供硬件模型，不等于允许它提供启动软件。

项目可以使用：

- `-bios` 映射自研 ROM。
- raw IDE disk。
- `-serial stdio` 连接 UART。
- QMP `sendkey` 操作虚拟键盘。
- GDB stub 观察 CPU。
- `fw_cfg` 作为来宾自行读取的配置设备。

项目不能使用：

- QEMU 默认 BIOS。
- `-kernel` 直接放置 Kernel。
- SeaBIOS/OVMF/GRUB/Limine 替代启动链。
- 宿主直接修改 Guest 内存假装设备输入。

判断标准是：来宾是否仍亲自执行设备协议、验证和状态切换。

## 19. 从按下电源到 Shell 的因果链

```text
power/reset
  → RIP/CS reset state
  → ROM reset vector
  → COM1
  → ATA reads Stage 1
  → A20/GDT/protected mode
  → PAE/page tables/LME/paging
  → 64-bit Stage 1
  → E820 + Kernel ELF loader
  → BootInfo + C++ Kernel
  → Kernel GDT/TSS/IDT
  → allocator/page tables/heap
  → PIC/LAPIC/PIT/PS2/ATA
  → user ELF + Ring 3
  → processes/IPC/filesystem
  → fd + console + Shell
```

“系统启动成功”其实是这条链上所有前置条件同时成立。任何可见后果都只能证明
它之前的一部分，不会自动证明所有失败路径正确。

## 20. 常见误解

### 20.1 “64 位 CPU 一复位就在 64 位模式”

错误。兼容启动状态仍需软件显式进入 protected mode、建立分页并启用 IA-32e。

### 20.2 “QEMU 已经替系统做了 BIOS”

只有使用默认固件时才是。本项目用 `-bios` 提供自己的 ROM，QEMU 只映射字节和
模拟设备。

### 20.3 “GDT 在 64 位已经没用”

传统 base/limit 大多弱化，但 CS 特权/模式、TSS、IDT selector 仍依赖描述符。

### 20.4 “虚拟机里没有真实硬件行为”

电气细节由模型替代，但端口、寄存器、IRQ、异常和时序契约仍由 Guest 代码真实
执行。系统测试验证的是目标架构语义，不是硅片信号完整性。

### 20.5 “所有高地址都是 RAM”

PC 物理地址空间包含保留区和 MMIO hole。只有内存图标为 usable 的完整页可由
分配器拥有。

## 21. 对照项目阅读

1. [固件入口](../../../source/firmware/src/reset_and_serial.asm)
2. [ROM 链接脚本](../../../source/firmware/linker/rom.ld)
3. [Stage 1 入口](../../../source/boot/stage1/src/entry.asm)
4. [Kernel loader](../../../source/boot/stage1/src/kernel_loader.asm)
5. [Kernel C++ 入口](../../../source/kernel/src/boot/entry.cpp)
6. [Kernel 装配主线](../../../source/kernel/src/core/kernel_main.cpp)
7. [架构总览](../../architecture.md)

阅读时标注每个文件开始执行时的：

- CPU mode。
- CS:RIP 或 RIP。
- CR0/CR3/CR4/EFER 关键状态。
- 栈位置。
- 可访问 RAM 范围。
- 已可用设备。

## 22. 练习

### 练习 A：分层分类

把以下概念分类为 ISA、微架构或平台：RAX、branch predictor、COM1
`0x3F8`、PML4、L1 cache、IRQ1、IRETQ、IDE primary bus。

### 练习 B：实模式 alias

计算并比较：

```text
1000:0020
1001:0010
0FFF:0030
```

说明为什么 segment:offset 不是唯一地址身份。

### 练习 C：A20

假设 A20 关闭，比较 `0x000000` 与 `0x100000`。解释为什么在 64 位页表建立前
仍要处理这个 1980 年代兼容机制。

### 练习 D：模式依赖

画出 A20、GDT、CR0.PE、CR4.PAE、CR3、EFER.LME、CR0.PG 和 far jump 的
依赖图。任选一项放错顺序，预测最早在哪次取指失败。

### 练习 E：平台替换

假设目标改成没有 8259/PIT/IDE 的现代平台，列出：

- 哪些上层模块可以保留。
- 哪些硬件适配必须替换。
- 哪些 BootInfo 字段可继续作为稳定交接。

## 23. 通过标准

应能清楚解释：

- 为什么 CPU 需要 reset vector。
- 8086 分段、A20、保护模式、分页和 AMD64 之间的历史依赖。
- Long mode 与 64-bit code mode 的区别。
- ISA、微架构、平台和操作系统的职责边界。
- PIC 与 LAPIC 为什么会同时存在。
- ROM、Stage 1、Kernel 各自拥有的临时和长期状态。
- QEMU 提供硬件模型与替代自研软件的分界。

下一册进入
[位、整数、地址、汇编与 ABI](02-data-address-assembly-and-abi.md)。

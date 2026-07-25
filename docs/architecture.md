# 项目架构

## 启动链

```text
CPU Reset Vector
       ↓
自研 ROM Firmware
       ↓
自研 Stage 1 / Stage 2
       ↓
ELF64 Kernel
       ↓
用户态与系统服务
```

QEMU 只模拟 CPU、内存、串口、IDE 等硬件。每个箭头都是显式交接契约，必须定义输入状态、内存布局、错误报告和下一阶段入口。

## 处理器模式

启动代码按照实模式、保护模式、长模式的顺序推进。GDT、控制寄存器、EFER 和页表均由项目代码建立，并通过反汇编、GDB 和串口标记验证。

页表位于物理地址 `0x10000..0x12FFF`，使用 32 个 2 MiB 大页对低端
64 MiB 做身份映射。Stage 1 先打开并验证 A20，再加载平坦 GDT；进入保护模式后依次
构造页表、设置 `CR4.PAE`、加载 `CR3`、设置 `IA32_EFER.LME`、开启
`CR0.PG`，最后通过 L 位代码段远跳进入 64 位子模式。

v0.4 的内核是独立 ELF64 `ET_EXEC` 文件，入口和首个链接地址为
`0x00100000`。编译层只产生 x86-64 freestanding 对象，链接层直接调用 LLD，
不允许宿主 GCC 或宿主运行库参与。Stage 1 只消费程序头中的 `PT_LOAD`
契约，不依赖节表；第一遍验证全部段及相互关系，第二遍才复制并清零 BSS。

同一块 `boot_disk.img` 保持两套独立格式：LBA 0 和 LBA 1..64 属于 Stage 1，
LBA 65 保存 Kernel 描述符，LBA 66 开始保存精确的 `kernel.elf`。Kernel
描述符用 64 位字段记录 LBA、文件长度和扇区数，用两个 CRC32 分别覆盖完整
描述符扇区和精确 ELF 文件。当前 ATA 驱动仍显式限制为 LBA28。

当前使用的 CPU 架构状态、16550A UART、IDE/ATA 主通道寄存器、访问宽度和标志位
统一记录在 [芯片与寄存器结构](hardware/chips.md)；机器可读版本位于
`docs/hardware/register_map.yaml`。新增设备必须先补充这份结构化规格，再进入驱动实现。

## v0.1 ROM 与复位路径

```text
128 KiB ROM 文件偏移             x86 物理地址
0x00000                         0xFFFE0000
0x1F000  16 位入口              0xFFFFF000
0x1FFF0  E9 0D F0              0xFFFFFFF0  ← CPU 复位取指
0x1FFFF                         0xFFFFFFFF
```

复位时 CS 隐藏基址为 `0xFFFF0000`，IP 为 `0xFFF0`。复位向量使用 16 位
near jump，只修改 IP 为 `0xF000`，因此目标物理地址为 `0xFFFFF000`。
入口建立低端 RAM 栈，使用 CS override 从高端 ROM 读取常量。

```text
CPU Reset
   ↓ 物理地址 0xFFFFFFF0
near jump
   ↓
16 位入口（CLI、CLD、段寄存器、栈）
   ↓
COM1 初始化
   ↓
有界 THRE 轮询与串口协议
   ↓
HLT
```

## v0.2 Stage 1 加载路径

```text
IDE 主盘 LBA 0
   ↓ ATA PIO 单扇区读取
0x0000:0x0500 描述符缓冲区
   ↓ 格式、范围和描述符校验
IDE 主盘 LBA 1..N
   ↓ 每扇区等待 BSY=0、DRQ=1、ERR=0、DF=0
0x0800:0x0000 Stage 1 负载
   ↓ 负载校验
far return 重载 CS:IP
   ↓
Stage 1 独立入口
```

描述符使用小端固定宽度字段，覆盖 magic、版本、头长度、加载段、入口偏移、
负载扇区数、标志、LBA 和负载校验。固件不会把磁盘字节映射为宿主结构体，
而是按固定偏移读取，并在任何 I/O 或验证失败后停机。

## v0.4 Kernel 磁盘容器

```text
LBA 0       Stage 1 描述符
LBA 1..64   Stage 1 最大负载区
LBA 65      Kernel v1 描述符
LBA 66..N   kernel.elf（精确长度 + 扇区补零）
```

宿主与目标机都先验证 Stage 1/Kernel 分离、Kernel 描述符、CRC32、扇区补零、
磁盘范围与内嵌 ELF64。格式详见
[Boot Image 模块](modules/boot-image.md) 与
[ADR 0006](adr/0006-kernel-image-container.md)。目标机随后按下列内存布局
完成交接：

| 物理区间 | 用途 |
| --- | --- |
| `0x00008000..0x0000FFFF` | Stage 1 最大负载窗口 |
| `0x00010000..0x00012FFF` | PML4、PDPT、PD |
| `0x00013000..0x000131FF` | Kernel 描述符 |
| `0x00014000..0x00014067` | BootInfo v2 |
| `0x00015000..0x00015FFF` | ELF 验证工作区 |
| `0x00016000..0x00016FFF` | 物理内存图元数据 |
| `0x00017000..0x00017FFF` | `fw_cfg` 名称暂存区 |
| `0x00018000..0x00018BFF` | 最多 128 项的 24 字节物理内存图 |
| `0x00020000..0x0009FFFF` | 最大 512 KiB ELF 暂存 |
| `0x00100000..0x03EFFFFF` | Kernel `PT_LOAD` 目标窗口 |
| `0x03FEF000..0x03FFEFFF` | 早期内核栈保留区 |

BootInfo magic 为 `OSBOOT64`，版本 2 的 13 个字段全部为 64 位。Stage 1 把其地址放入
`RDI`，在 16 字节对齐的栈上使用 `CALL` 进入 `osKernelEntry`，从而满足
System V AMD64 函数入口的栈约束。详细理由见
[ADR 0007](adr/0007-two-pass-elf-loader-and-boot-info.md)。

## v0.5 内核描述符与异常路径

Stage 1 的 GDT 只负责把加载器带进长模式，不属于内核长期 ABI。内核验证
BootInfo、BSS 和 CR3 后，按以下顺序接管处理器描述符状态：

```text
构造 104 字节 TSS
  ├─ RSP0 = BootInfo 初始内核栈顶
  ├─ IST1 = 双重故障独立栈顶
  ├─ IST2 = NMI 独立栈顶
  └─ IST3 = 机器检查独立栈顶
       ↓
构造五槽 GDT → LGDT → 远返回刷新 CS → 重载数据段 → LTR
       ↓
构造 256 槽 IDT（0..31 present）→ LIDT
       ↓
SGDT / SIDT / CS / SS / STR 回读验证
       ↓
INT3 → 统一异常帧 → C++20 分发器 → IRETQ
```

GDT 当前选择子为代码段 `0x08`、数据段 `0x10`、TSS `0x18`。TSS 描述符占用
GDT 的两个连续 8 字节槽，因为 64 位 TSS 基址无法放进传统单槽描述符。
IDT 每槽 16 字节，完整表为 4096 字节；v0.5 时外部中断向量保持
not-present，直到 v0.7 建立控制器确认协议后才开放 32..47。

异常入口分成硬件、汇编和 C++ 三层：

```text
CPU 压入 RIP / CS / RFLAGS / 可选错误码
  ↓
每向量 NASM 桩补齐“无错误码”占位并压入向量号
  ↓
公共桩清 DF、保存 RAX..R15、对齐 System V AMD64 栈
  ↓
osKernelDispatchException(ExceptionFrame*)
  ├─ vector 3 且 error=0：记录 BREAKPOINT_HANDLED 后返回
  └─ 其他：panic，记录现场后 CLI + HLT
```

统一 `ExceptionFrame` 精确为 160 字节。硬件错误码向量集合是
8、10、11、12、13、14、17、21、29、30；其他异常由桩补零。双重故障、
NMI 和机器检查门分别指定 IST1、IST2、IST3。panic 不分配内存、不依赖锁，
通过单次状态位防止递归刷日志；页故障额外读取 CR2。

真实失败镜像与生产内核共享全部描述符、分发和 panic 代码，只替换最薄入口：
非法指令镜像执行 `UD2`，页故障镜像读取恒等映射末端后的
`0x04000000`。该边界是 Stage 1 明确未映射的首地址，因此测试不依赖随机
指针或 QEMU 偶然状态。设计理由见
[ADR 0008](adr/0008-kernel-descriptor-tables-and-exception-abi.md)。

## v0.6 物理内存发现与内核页表

自研 ROM 没有传统 PC BIOS 的 `INT 15h, E820h` 服务，Stage 1 又不能假设
所有 QEMU `-m` 内存都连续可用。项目因此把 QEMU PC 的 `fw_cfg` 当作一个
真实硬件配置设备：选择端口 `0x510`，数据端口 `0x511`。Stage 1 验证
`QEMU` 签名、读取 selector `0x19` 的大端文件目录、找到 `etc/e820`，
再把每个 20 字节 QEMU 项转换为项目的 24 字节项并按物理基址排序。

```text
fw_cfg selector/data 端口
  ↓ 签名、目录数量和名称边界
etc/e820 原始项
  ↓ 20 B → 24 B、最多 128 项、基址排序
BootInfo v2
  ↓ 指针、数量、条目宽度
内核验证：非空、无溢出、单调、不重叠、存在受管可用 RAM
```

内核不继续使用 Stage 1 的三个大页表页作为长期地址空间。它先在旧的低
64 MiB 身份映射下初始化页帧分配器，保留低 1 MiB、实际 Kernel ELF 映像和
64 KiB 初始栈，再从剩余页帧建立四级 4 KiB 页表。切换关系如下：

```text
Stage 1 CR3 = 0x10000（2 MiB 大页，低 64 MiB）
       ↓ 分配并清零 PML4 / PDPT / PD / PT
低半区 0..64 MiB 恒等映射
  ├─ 0 页 not-present
  ├─ Kernel text: RX
  ├─ rodata: R + NX
  ├─ data/BSS/stack: RW + NX
  ├─ 初始栈底 4 KiB guard: not-present
  └─ 三个 IST 栈底 guard: not-present
高半区 0xFFFF800000000000
  ├─ 64 KiB heap: RW + NX
  └─ 0xFFFF800000100000: R + NX 写保护测试页
       ↓ 设置 EFER.NXE 与 CR0.WP
内核 CR3 = 新 PML4 物理地址
```

页帧状态用 2 bit 编码：不可用、空闲、已分配、已保留。相较一位位图，它多
占 4 KiB 状态存储，却能区分“永不可分配”“启动所有权”“动态所有权”，从而
让重复释放、释放保留页和跨已分配页保留都具有明确失败语义。当前只管理低
64 MiB，接口和统计全部使用 64 位，后续扩大管理范围无需改变公共 ABI。

高半区堆是单调早期分配器：支持二的幂对齐和显式失败，不支持释放。该选择用于
启动期对象，而不是声称已经完成通用内核堆。页表管理器同样暂不回收空的中间
页表；v0.7 设备初始化可以使用当前堆，通用释放和页表生命周期将在进程地址
空间出现前补充。设计决策见
[ADR 0009](adr/0009-fw-cfg-memory-map-and-kernel-page-tables.md)。

## v0.7 传统中断与设备闭环

QEMU `pc` 同时包含 8259A、I/O APIC 和本地 APIC。没有外部 BIOS 时，内核
不能假定复位后的 APIC 已替 8259A 建立虚拟线模式。设备启动先清除并回读
`IA32_APIC_BASE[11]`，让传统 INTR 路径成为显式契约，再初始化两片 PIC：

```text
LAPIC global enable = 0
       ↓
8259A master 0x20/0x21 ──IRQ2 级联── slave 0xA0/0xA1
       ↓ 重映射
IRQ0..7 → IDT 32..39，IRQ8..15 → IDT 40..47
       ↓
每向量 NASM 桩 → 硬件 IRQ 公共入口 → C++ InterruptRuntime
       ↓
设备最小处理 → PIC ISR/EOI → IRETQ
```

异常与 IRQ 使用相同的 160 字节寄存器帧形状，但公共入口和 C++ 分发器分开：
异常决定恢复或 panic，IRQ 决定设备处理与 EOI。这样不会把同步异常错误码和
异步控制器确认混为一套策略。

设备初始化保持“先配置、后开放”的单向状态：

```text
PIC 全屏蔽
  ├─ PIT channel 0：mode 2，divisor 0x04A9，约 1000 Hz
  ├─ i8042：关闭端口、清输出、更新配置、开放第一端口、F4/FA
  └─ ATA primary master：nIEN + LBA28 PIO 重读 LBA 0
       ↓ 全部成功
PIC mask = 0xFFFC（仅 IRQ0、IRQ1）
       ↓ 软件 INT 0x27 验证虚假 IRQ7
STI → HLT → IRQ0/IRQ1
```

IRQ0 只递增 64 位 tick，按 PIT 实际除数换算单调毫秒。IRQ1 从数据端口读取
一个字节，集合 1 解码器处理 make、break 与 `E0` 前缀，语义事件留到中断
返回后的事件循环记录。ATA 暂不开放 IRQ14，保持同步 PIO；它的目标是证明
内核独立拥有设备协议，不是提前宣称完成异步块层。

QEMU 系统测试启动同一生产镜像，等待 `READY` 后通过 QMP 向虚拟键盘前端
发送 `A` 键。QEMU 只产生硬件输入；i8042、PIC、IDT、汇编桩、解码和日志均
由来宾代码完成。设计理由见
[ADR 0010](adr/0010-legacy-interrupt-routing-and-device-bootstrap.md)，模块
契约见 [Interrupt 与 Devices 模块](modules/devices.md)。

## 模块边界

- `foundation` 提供地址、字节数和地址区间等不依赖运行时的基础类型。
- `firmware` 负责复位入口、最小串口初始化和 Stage 1 磁盘加载。
- 引导阶段负责模式切换、内存探测与 ELF64 内核加载。
- 内核负责异常、中断、内存、调度、设备和系统调用。
- 用户空间只通过受控 ABI 使用内核能力。

操作系统目标代码与宿主机测试代码使用同一份模块实现。目标构建通过 Clang
交叉编译为 x86-64 ELF，并关闭异常、RTTI、栈保护、红区和宿主 C++ 标准库头文件。
宿主构建仅用于快速执行测试，不会进入最终镜像。

## 依赖方向

```text
tests ───────────────→ foundation
firmware ────────────→ foundation
boot stages ─────────→ foundation
kernel ──────────────→ foundation
```

`foundation` 不得反向依赖固件、引导阶段、内核或宿主测试框架。

## 源码组织与可见性

源码按领域模块组织，不按文件扩展名集中堆放。当前模块采用以下结构：

```text
source/foundation/
├── CMakeLists.txt
├── include/os/foundation/
│   └── address_range.hpp
└── src/
    └── address_range.cpp

source/firmware/
├── CMakeLists.txt
├── linker/
│   └── rom.ld
└── src/
    └── reset_and_serial.asm

source/boot/stage1/
├── CMakeLists.txt
├── include/
│   └── kernel_loader.inc
└── src/
    ├── entry.asm
    ├── kernel_loader.asm
    └── memory_map.asm

source/kernel/
├── CMakeLists.txt
├── include/os/kernel/
│   ├── boot_info.hpp
│   ├── ata_pio.hpp
│   ├── device_model.hpp
│   ├── descriptor_layout.hpp
│   ├── descriptor_tables.hpp
│   ├── entry.hpp
│   ├── exception_frame.hpp
│   ├── exceptions.hpp
│   ├── interrupt_runtime.hpp
│   ├── kernel_heap.hpp
│   ├── kernel_main.hpp
│   ├── memory_manager.hpp
│   ├── legacy_pic.hpp
│   ├── page_table.hpp
│   ├── panic.hpp
│   ├── physical_frame_allocator.hpp
│   ├── physical_memory_map.hpp
│   ├── port_io.hpp
│   ├── processor.hpp
│   ├── programmable_interval_timer.hpp
│   ├── ps2_keyboard.hpp
│   └── serial_port.hpp
├── linker/
│   └── kernel.ld.in
└── src/
    ├── architecture.asm
    ├── ata_pio.cpp
    ├── boot_info.cpp
    ├── descriptor_layout.cpp
    ├── descriptor_tables.cpp
    ├── device_model.cpp
    ├── entry.cpp
    ├── exception_frame.cpp
    ├── exceptions.cpp
    ├── kernel_heap.cpp
    ├── kernel_main.cpp
    ├── interrupt_runtime.cpp
    ├── legacy_pic.cpp
    ├── memory_manager.cpp
    ├── page_table.cpp
    ├── page_table_layout.cpp
    ├── panic.cpp
    ├── physical_frame_allocator.cpp
    ├── physical_memory_map.cpp
    ├── port_io.cpp
    ├── processor.cpp
    ├── programmable_interval_timer.cpp
    ├── ps2_keyboard.cpp
    └── serial_port.cpp
```

`include/os/<模块>/` 只保存其他模块可以依赖的公开契约；`src/` 保存实现和
模块私有头文件。CMake 将公开目录标记为 `PUBLIC`、私有目录标记为 `PRIVATE`，
消费者不能通过传递依赖获得私有包含路径。

每个源码模块拥有独立 `CMakeLists.txt` 和 CMake target。根构建文件只负责
公共策略、模块组合、镜像产物和测试入口，不保存具体模块的源文件清单。

测试目录按测试层和被测领域镜像生产代码：

```text
tests/
├── unit/foundation/
├── unit/kernel/
├── integration/boot/
├── randomized/foundation/
├── randomized/kernel/
├── system/kernel/
├── tooling/
└── support/cpp/
```

后续模块不会预先创建空目录；开始对应最小增量时再按照相同边界落地。

## 宿主工具边界

Python 只运行在宿主机，负责工具链检查、CMake/CTest 调度、镜像生成、ELF
与 ROM 审计和 QEMU 生命周期管理。Python 不进入操作系统镜像，也不解析或
替代 CMake 构建图。所有外部程序均以参数列表直接启动，不经过 Shell 字符串
求值。

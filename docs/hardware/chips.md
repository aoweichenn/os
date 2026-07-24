# 芯片与寄存器结构

这份文档把项目当前真正使用的硬件接口整理成“芯片—寄存器—字段—状态机”四层。
它不是把端口号抄成表格，而是规定软件什么时候拥有寄存器、哪些位可以推进状态、
哪些位必须立即进入失败路径。机器可读的同一份描述见
[`register_map.yaml`](register_map.yaml)。

## 1. 当前硬件拓扑

```text
x86-64 CPU
├── 架构寄存器：RIP、RFLAGS、CR0、CR2、CR3、CR4、IA32_EFER
├── 描述符状态：CS、SS、GDTR、IDTR、TR、TSS
├── 端口 I/O 总线
│   ├── 16550A 兼容 UART / COM1：0x3F8..0x3FF
│   └── PATA primary master：0x1F0..0x1F7、控制端口 0x3F6
└── RAM
    ├── 固件栈：0x7000 向低地址增长
    ├── Stage 1 描述符：0x0500..0x06FF
    ├── Stage 1 负载：0x8000..0xFFFF
    ├── 页表：0x10000..0x12FFF
    ├── Kernel 描述符与 BootInfo：0x13000..0x1404F
    ├── Kernel ELF 暂存：0x20000..0x9FFFF
    ├── Kernel PT_LOAD：0x100000..0x3EFFFFF
    └── Kernel 初始栈：0x3FEF000..0x3FFEFFF
```

QEMU 只提供这些设备的行为模型。端口顺序、访问宽度、状态位和错误处理都由项目
自己的汇编固件实现。

## 2. CPU 架构状态与标志位

### 2.1 RFLAGS

| 位 | 名称 | 语义 | 当前启动链用途 |
| ---: | --- | --- | --- |
| 0 | CF | 无符号进位/借位 | 检查地址或 LBA 加法是否溢出时观察 | 
| 2 | PF | 低字节偶校验 | 算术指令副作用，当前不作为协议条件 |
| 4 | AF | 半字节进位 | BCD 兼容状态，当前不使用 |
| 6 | ZF | 结果为零 | `test`、`cmp`、循环与状态判断 |
| 7 | SF | 结果最高位 | 当前不单独作为启动条件 |
| 8 | TF | 单步陷阱 | 调试器使用，正常启动必须保持关闭 |
| 9 | IF | 可屏蔽中断开关 | 固件初始化期间由 `CLI` 关闭 |
| 10 | DF | 字符串方向 | 入口由 `CLD` 清零，保证 `lodsb`/`lodsw` 向高地址推进 |
| 11 | OF | 有符号溢出 | 当前不用于硬件协议判断 |

标志位不是普通变量：一条 `cmp` 或 `test` 会覆盖一组标志。因此汇编代码在
产生条件码后必须立即消费，不能在中间插入会改变 RFLAGS 的指令。`CF`、`ZF` 和
`DF` 是当前启动链最重要的三个可见条件。

### 2.2 模式转换控制位

| 寄存器 | 位 | 名称 | 作用 | 前置/后置条件 |
| --- | ---: | --- | --- | --- |
| CR0 | 0 | PE | 进入保护模式 | GDT 已准备，随后必须远跳转刷新 CS |
| CR0 | 16 | WP | 内核写保护页 | 建立分页策略后再启用 |
| CR0 | 31 | PG | 开启分页 | CR3、CR4.PAE、EFER.LME 和页表先有效 |
| CR3 | 全宽地址字段 | PML4 | 当前页表根物理地址 | 必须满足页对齐和物理范围 |
| CR4 | 5 | PAE | 启用物理地址扩展 | 设置后才能按长模式页表解释 |
| EFER | 8 | LME | 请求长模式 | 通过 `RDMSR/WRMSR` 访问 |
| EFER | 10 | LMA | 长模式已激活 | 只读结果，不能直接写 |
| EFER | 11 | NXE | 启用 NX 位 | 只有策略需要且 CPU 支持时启用 |

模式切换是状态机，不是把几个 bit 任意置一。v0.3 已把每次写入和读回值加入
串口证据与 QEMU 检查；v0.4 的内核还会读回 CR3，与 BootInfo 中的页表根比较。

### 2.3 长模式描述符寄存器

| 状态 | 可见内容 | 装载/读取 | v0.5 不变量 |
| --- | --- | --- | --- |
| GDTR | 64 位 base + 16 位 limit | `LGDT` / `SGDT` | base 指向五槽内核 GDT，limit=39 |
| IDTR | 64 位 base + 16 位 limit | `LIDT` / `SIDT` | base 指向 256 槽 IDT，limit=4095 |
| CS | 16 位选择子 + 隐藏描述符 | far control transfer | `0x08`，Ring 0 长模式代码段 |
| SS | 16 位选择子 + 隐藏描述符 | `MOV SS` | `0x10`，Ring 0 数据段 |
| TR | 16 位选择子 +隐藏 TSS 描述符 | `LTR` / `STR` | `0x18`，busy 64-bit TSS |
| CR2 | 页故障线性地址 | `MOV reg, CR2` | 只在向量 14 的 panic 中读取 |

描述符表 limit 都是“最后一个有效字节偏移”，不是字节数。五个 8 字节 GDT 槽
的 limit 是 \(5\times8-1=39\)；256 个 16 字节 IDT 槽的 limit 是
\(256\times16-1=4095\)。

### 2.4 当前 GDT 与 TSS 结构

```text
GDT[0]  8 B  null
GDT[1]  8 B  Ring 0 code, selector 0x08
GDT[2]  8 B  Ring 0 data, selector 0x10
GDT[3]  8 B  TSS descriptor low, selector 0x18
GDT[4]  8 B  TSS descriptor high
```

64 位 TSS 描述符由 limit[19:0]、base[63:0]、type=9、DPL、present 和粒度
字段组成。执行 `LTR` 后处理器把 type 从 available 9 改为 busy 11。
TSS 内存布局：

| 偏移 | 宽度 | 字段 | 当前用途 |
| ---: | ---: | --- | --- |
| `0x04` | 64 | RSP0 | 未来 Ring 3→Ring 0 的内核栈 |
| `0x0C` / `0x14` | 64 | RSP1 / RSP2 | 保留为零 |
| `0x24` | 64 | IST1 | 双重故障栈 |
| `0x2C` | 64 | IST2 | NMI 栈 |
| `0x34` | 64 | IST3 | 机器检查栈 |
| `0x3C..0x5C` | 64 | IST4..IST7 | 保留为零 |
| `0x66` | 16 | I/O bitmap offset | 104，位于 TSS limit 之后 |

### 2.5 IDT gate 位字段

一个长模式 IDT gate 为 16 字节：

```text
127                       96 95           64
+---------------------------+---------------+
| reserved=0                | offset[63:32] |
+---------------------------+---------------+
63            48 47 46 45 40 39 32 31 16 15 0
+---------------+--+--+-----+-----+-----+-----+
| offset[31:16] |P |DPL|type | IST | sel |offlo|
+---------------+--+--+-----+-----+-----+-----+
```

- type=`1110b` 表示 64 位 interrupt gate。
- P=1 表示门存在；DPL=0 禁止低特权软件随意 `INT n`。
- breakpoint 与 overflow 使用 DPL=3，保持架构允许用户调试陷阱的语义。
- IST 为 0 使用当前/特权栈；1..7 选择 TSS 的对应专用栈。
- selector 固定引用当前 Ring 0 代码段 `0x08`。

### 2.6 架构异常与错误码

| 向量 | 缩写 | 类别 | CPU 错误码 | 当前 IST / 策略 |
| ---: | --- | --- | --- | --- |
| 0 | #DE | fault | 无 | panic |
| 1 | #DB | fault/trap | 无 | panic |
| 2 | NMI | interrupt | 无 | IST2 / panic |
| 3 | #BP | trap | 无 | 受控自检可返回 |
| 4 | #OF | trap | 无 | panic |
| 5 | #BR | fault | 无 | panic |
| 6 | #UD | fault | 无 | 故障注入 / panic |
| 7 | #NM | fault | 无 | panic |
| 8 | #DF | abort | 固定 0 | IST1 / panic |
| 9 | 保留 | — | 无 | panic |
| 10 | #TS | fault | 有 | panic |
| 11 | #NP | fault | 有 | panic |
| 12 | #SS | fault | 有 | panic |
| 13 | #GP | fault | 有 | panic |
| 14 | #PF | fault | 有 | 记录 CR2 / panic |
| 15 | 保留 | — | 无 | panic |
| 16 | #MF | fault | 无 | panic |
| 17 | #AC | fault | 有 | panic |
| 18 | #MC | abort | 无 | IST3 / panic |
| 19 | #XM | fault | 无 | panic |
| 20 | #VE | fault | 无 | panic |
| 21 | #CP | fault | 有 | panic |
| 22..27 | 保留 | — | 无 | panic |
| 28 | #HV | fault | 无 | panic |
| 29 | #VC | fault | 有 | panic |
| 30 | #SX | fault | 有 | panic |
| 31 | 保留 | — | 无 | panic |

### 2.7 页故障错误码

| 位 | 名称 | 0 | 1 |
| ---: | --- | --- | --- |
| 0 | P | 页不存在 | 页级权限违反 |
| 1 | W/R | 读访问 | 写访问 |
| 2 | U/S | supervisor 访问 | user 访问 |
| 3 | RSVD | 正常页表遍历 | 页表保留位被置一 |
| 4 | I/D | 数据访问 | 取指访问 |
| 5 | PK | 非 protection-key 原因 | protection-key 违反 |
| 6 | SS | 非 shadow-stack 原因 | shadow-stack 访问 |
| 15 | SGX | 非 SGX 原因 | SGX 访问控制违反 |

v0.5 的页故障注入得到错误码 0：supervisor 读取一个 not-present 数据页。
CR2=`0x04000000`，正好是当前 64 MiB 身份映射的第一个未映射线性地址。

## 3. 16550A UART / COM1

### 3.1 寄存器窗口

COM1 基址为 `0x3F8`，每个寄存器宽 8 位。偏移 0 和 1 会被 `LCR.DLAB` 复用，
所以初始化顺序是：关闭中断、置 DLAB、写除数、清 DLAB、写 8N1、配置 FIFO 与
modem control。

| 偏移 | DLAB=0 读 | DLAB=0 写 | DLAB=1 |
| ---: | --- | --- | --- |
| 0 | RBR 接收缓冲 | THR 发送保持 | DLL 除数低字节 |
| 1 | IER 中断使能 | IER 中断使能 | DLM 除数高字节 |
| 2 | IIR 中断识别 | FCR FIFO 控制 | 不复用 |
| 3 | LCR 线路控制 | LCR 线路控制 | 不复用 |
| 4 | MCR modem control | MCR modem control | 不复用 |
| 5 | LSR 线路状态 | 保留 | 不复用 |

### 3.2 LCR 与 LSR 位

| 寄存器 | 位 | 名称 | 含义 |
| --- | ---: | --- | --- |
| LCR | 0..1 | WLS | 字长选择；`11` 表示 8 位 |
| LCR | 2 | STB | 停止位选择；当前为 0，使用 1 个停止位 |
| LCR | 3 | PEN | 奇偶校验使能；当前为 0 |
| LCR | 7 | DLAB | 访问除数锁存器；配置完必须清零 |
| LSR | 0 | DR | 接收数据可读 |
| LSR | 1 | OE | 接收溢出 |
| LSR | 2 | PE | 奇偶校验错误 |
| LSR | 3 | FE | 帧错误 |
| LSR | 4 | BI | Break 中断 |
| LSR | 5 | THRE | 发送保持寄存器为空，可写下一个字节 |
| LSR | 6 | TEMT | 发送保持和移位寄存器都为空 |
| LSR | 7 | FIFO_ERROR | FIFO 中存在错误 |

当前固件只把 `THRE` 作为发送所有权条件；它不把 `TEMT` 误当成“可以写入”的
唯一条件，也不启用中断。每次等待最多 `0xFFFF` 次，超时后输出明确失败标记。

## 4. PATA IDE 主通道与 ATA 状态

### 4.1 命令块寄存器

| 端口 | 宽度 | 名称 | 访问语义 |
| ---: | ---: | --- | --- |
| `0x1F0` | 16 | DATA | 每扇区读取 256 个 little-endian word |
| `0x1F1` | 8 | ERROR/FEATURES | 读错误、写特性 |
| `0x1F2` | 8 | SECTOR COUNT | 当前每次写 1 |
| `0x1F3` | 8 | LBA LOW | LBA[7:0] |
| `0x1F4` | 8 | LBA MID | LBA[15:8] |
| `0x1F5` | 8 | LBA HIGH | LBA[23:16] |
| `0x1F6` | 8 | DRIVE/HEAD | LBA[27:24]、设备选择和 LBA 模式 |
| `0x1F7` | 8 | STATUS/COMMAND | 读状态、写命令 |
| `0x3F6` | 8 | ALTERNATE STATUS/DEVICE CONTROL | 读状态不清中断、写控制 |

### 4.2 STATUS 位

| 位 | 名称 | 1 的含义 | 固件决策 |
| ---: | --- | --- | --- |
| 0 | ERR | 命令执行错误 | 立即输出 `IDE_ERROR` |
| 3 | DRQ | 数据请求，数据端口可访问 | 只有 `BSY=0` 且无错误时读 256 个字 |
| 4 | DSC | 设备寻道完成 | 当前只记录协议，不单独决定读取 |
| 5 | DF | 设备故障 | 与 ERR 同属设备错误边界 |
| 6 | DRDY | 设备就绪 | 当前由 QEMU 模型隐含满足 |
| 7 | BSY | 设备忙 | 保持轮询，超出预算输出 `IDE_TIMEOUT` |

状态判定顺序固定为：

```text
BSY=1              -> 继续有界等待
BSY=0 且 ERR/DF=1  -> IDE_ERROR
BSY=0 且 DRQ=0     -> 继续有界等待
BSY=0 且 DRQ=1     -> 读取 DATA 的 256 个 16 位字
```

### 4.3 ERROR 位

| 位 | 名称 | 典型含义 |
| ---: | --- | --- |
| 0 | AMNF | 地址标记未找到 |
| 1 | TK0NF | 轨道 0 未找到 |
| 2 | ABRT | 命令中止 |
| 3 | MCR | 媒体改变请求 |
| 4 | IDNF | 扇区 ID 未找到 |
| 5 | MC | 媒体改变 |
| 6 | UNC | 不可校正数据错误 |
| 7 | BBK | 坏块标记 |

固件加载 Stage 1 时统一输出 `IDE_ERROR`；Stage 1 加载 Kernel 时输出
`KERNEL_ATA_ERROR`。两条路径都把 ERROR 寄存器作为诊断来源，后续设备驱动
阶段会保留原始 status/error 字节，形成更细的错误类型。

## 5. 结构化描述与代码的对应关系

### 5.1 8253/8254 PIT（当前阶段）

固件使用 PIT 通道 0 建立单调时钟的硬件基础：命令端口为 `0x43`，通道 0 数据端口
为 `0x40`，模式为 2（率发生器），分频值为 `0x04A9`，目标频率约为 1000 Hz。
本阶段只初始化计数器并输出 `CLOCK_READY`。进入保护模式并建立 IDT 后，IRQ0 才会
累加软件滴答，再用整数换算为毫秒并处理溢出。

| 寄存器 | 地址 | 本项目用途 |
| --- | ---: | --- |
| PIT 命令 | `0x43` | 选择通道 0、低/高字节、模式 2 |
| 通道 0 | `0x40` | 写入 16 位分频值；后续读取当前计数 |

当前计数不能直接当作时间戳，因为它会周期性回卷；必须由 IRQ0 软件滴答补足。

### 5.2 System Control Port A 与 A20

Stage 1 通过 I/O 端口 `0x92` 的位 1 打开 Fast A20 Gate，同时强制位 0 为零，
避免触发快速复位。写入后并不直接相信控制位，而是暂存物理地址 `0x000000` 与
`0x100000` 的原值，写入不同模式并检查它们是否仍然独立，最后恢复原值。

| 位 | 名称 | 本项目处理 |
| ---: | --- | --- |
| 0 | Fast Reset | 始终写零，禁止复位 |
| 1 | A20 Gate | 写一后执行地址别名验证 |

- `docs/hardware/register_map.yaml`：芯片、寄存器、位和访问宽度的机器可读规格。
- `source/firmware/src/reset_and_serial.asm`：固件端口访问与 ATA 状态机实现。
- `source/boot/stage1/src/entry.asm`：模式切换、页表和 Stage 1 串口路径。
- `source/boot/stage1/src/kernel_loader.asm`：长模式 ATA、CRC32、ELF 和 BootInfo。
- `source/kernel/src/architecture.asm`：LGDT/LIDT/LTR、异常桩和统一寄存器保存。
- `source/kernel/src/descriptor_tables.cpp`：GDT、TSS、IDT 构造与硬件回读验证。
- `source/kernel/src/panic.cpp`：异常现场和 CR2 的有界串口诊断。
- `source/kernel/src/serial_port.cpp`：内核独立的 COM1 访问层。
- `tests/tooling/test_qemu_runner.py`：串口标记的顺序与禁止条件。
- `docs/testing.md`：状态边界对应的 QEMU 失败注入。

以后新增 PIC、PIT、PS/2、PCI 或 LAPIC 时，先在这份结构化规格中定义寄存器和
标志，再实现驱动访问层；策略代码不能直接散落端口号。

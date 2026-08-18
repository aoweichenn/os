# 芯片与寄存器结构

这份文档把项目当前真正使用的硬件接口整理成“芯片—寄存器—字段—状态机”四层。
它不是把端口号抄成表格，而是规定软件什么时候拥有寄存器、哪些位可以推进状态、
哪些位必须立即进入失败路径。机器可读的同一份描述见
[`register_map.yaml`](register_map.yaml)。

## 1. 当前硬件拓扑

分阶段、可缩放的 QEMU Guest 整机图见
[从 v0.0 到 v1.0：整机硬件组装与连线图册](../learning/hardware-assembly-and-wiring.md)；
现实载板铜线与器件级电路另见
[N100 载板电路详解](../learning/physical-carrier-circuit-guide.md)。

```text
x86-64 CPU
├── 架构寄存器：RIP、RFLAGS、CR0、CR2、CR3、CR4、FS/GS base
├── 系统调用 MSR：IA32_EFER、STAR、LSTAR、FMASK、GS_BASE、KERNEL_GS_BASE
├── 描述符状态：CS、SS、GDTR、IDTR、TR、TSS
├── MMIO
│   └── Local APIC：IA32_APIC_BASE 指定的 4 KiB 页，当前为 0xFEE00000
├── 端口 I/O 总线
│   ├── 16550A 兼容 UART / COM1：0x3F8..0x3FF
│   ├── 8259A 主从中断控制器：0x20/0x21、0xA0/0xA1
│   ├── 8254 PIT：0x40..0x43
│   ├── i8042 / PS/2 键盘：0x60、0x64
│   ├── PATA primary master：0x1F0..0x1F7、控制端口 0x3F6
│   └── QEMU fw_cfg：selector 0x510、data 0x511
└── RAM
    ├── 固件栈：0x7000 向低地址增长
    ├── Stage 1 描述符：0x0500..0x06FF
    ├── Stage 1 负载：0x8000..0xFFFF
    ├── 页表：0x10000..0x12FFF
    ├── Kernel 描述符与 BootInfo v2：0x13000..0x14067
    ├── 内存图元数据、fw_cfg 暂存与内存图：0x16000..0x18BFF
    ├── Kernel ELF 暂存：0x03600000..0x03DFFFFF
    ├── Kernel PT_LOAD：0x100000..0x35FFFFF
    └── Kernel 初始栈：0x3FEF000..0x3FFEFFF
```

QEMU 只提供这些设备的行为模型。端口顺序、访问宽度、状态位和错误处理都由项目
自己的固件、汇编入口和 C++20 驱动实现。

## 2. CPU 架构状态与标志位

### 2.1 RFLAGS

| 位 | 名称 | 语义 | 当前启动链用途 |
| --- | ---: | --- | --- | --- |
| 0 | CF | 无符号进位/借位 | 检查地址或 LBA 加法是否溢出时观察 | 
| 2 | PF | 低字节偶校验 | 算术指令副作用，当前不作为协议条件 |
| 4 | AF | 半字节进位 | BCD 兼容状态，当前不使用 |
| 6 | ZF | 结果为零 | `test`、`cmp`、循环与状态判断 |
| 7 | SF | 结果最高位 | 当前不单独作为启动条件 |
| 8 | TF | 单步陷阱 | 调试器使用，正常启动必须保持关闭 |
| 9 | IF | 可屏蔽中断开关 | 初始化期间关闭；用户初始帧设为 1 |
| 10 | DF | 字符串方向 | 入口由 `CLD` 清零，保证 `lodsb`/`lodsw` 向高地址推进 |
| 11 | OF | 有符号溢出 | 当前不用于硬件协议判断 |
| 14 | NT | 遗留嵌套任务 | 系统调用入口 FMASK 清除；用户返回拒绝置位 |
| 16 | RF | 抑制下一条调试 fault | 合法用户现场可保存，但 SYSRET 快速路径回退 IRET |
| 18 | AC | 对齐检查/SMAP 相关状态 | 当前用户返回拒绝，入口 FMASK 清除 |
| 21 | ID | 用户可修改 CPUID 标志 | 当前用户返回不开放 |

标志位不是普通变量：一条 `cmp` 或 `test` 会覆盖一组标志。因此汇编代码在
产生条件码后必须立即消费，不能在中间插入会改变 RFLAGS 的指令。`CF`、`ZF` 和
`DF` 是当前启动链最重要的三个可见条件。

### 2.2 模式转换控制位

| 寄存器 | 位 | 名称 | 作用 | 前置/后置条件 |
| --- | ---: | --- | --- | --- |
| CR0 | 0 | PE | 进入保护模式 | GDT 已准备，随后必须远跳转刷新 CS |
| CR0 | 1 | MP | WAIT/FWAIT 与 TS 协同 | v1.2 扩展现场初始化置一 |
| CR0 | 2 | EM | x87 软件仿真 | v1.2 清零，要求真实硬件执行 |
| CR0 | 3 | TS | 首次 FPU 使用触发 #NM | v1.2 eager 切换清零 |
| CR0 | 5 | NE | x87 错误使用原生异常 | v1.2 置一，不走旧式 IRQ13 |
| CR0 | 16 | WP | supervisor 写也遵守只读页 | v0.6 在切换内核 CR3 前启用并回读 |
| CR0 | 31 | PG | 开启分页 | CR3、CR4.PAE、EFER.LME 和页表先有效 |
| CR3 | 全宽地址字段 | PML4 | 当前页表根物理地址 | 必须满足页对齐和物理范围 |
| CR4 | 5 | PAE | 启用物理地址扩展 | 设置后才能按长模式页表解释 |
| CR4 | 9 | OSFXSR | OS 管理 FXSAVE/FXRSTOR | v1.2 置一并回读 |
| CR4 | 10 | OSXMMEXCPT | SIMD 异常使用 #XM | v1.2 置一并回读 |
| CR4 | 18 | OSXSAVE | OS 管理 XSAVE/XCR0 | v1.2 明确清零，AVX 禁用 |
| EFER | 0 | SCE | 开启 SYSCALL/SYSRET | v1.3 在配置 LSTAR 等 MSR 后置一并回读 |
| EFER | 8 | LME | 请求长模式 | 通过 `RDMSR/WRMSR` 访问 |
| EFER | 10 | LMA | 长模式已激活 | 只读结果，不能直接写 |
| EFER | 11 | NXE | 启用 NX 位 | v0.6 先用 CPUID 检查，再启用并回读 |
| IA32_APIC_BASE | 10 | x2APIC enable | 切换 MSR 型 x2APIC 接口 | v0.7 要求为 0，使用 xAPIC MMIO |
| IA32_APIC_BASE | 11 | APIC global enable | 本地 APIC 全局开关 | v0.7 保持为 1，并回读确认 |

模式切换是状态机，不是把几个 bit 任意置一。v0.3 已把每次写入和读回值加入
串口证据与 QEMU 检查；v0.4 的内核还会读回 CR3，与 BootInfo 中的页表根比较。

#### CPUID 地址宽度叶

内核执行 `CPUID` 时先读取 `EAX=0x80000000` 的最大扩展叶，再读取
`EAX=0x80000008`：

| 返回字段 | 位 | 含义 | 当前用途 |
| --- | ---: | --- | --- |
| EAX | 7..0 | 最大物理地址宽度 | 限制 E820 与页表可表示的物理地址 |
| EAX | 15..8 | 最大线性地址宽度 | 记录处理器能力；当前页表仍采用四级 48 位 |

`qemu64` TCG 当前报告物理 40 位、线性 48 位，因此处理器物理上限为 1 TiB。
内核不会因为 E820 含有 1 TiB 末端的保留区就管理 1 TiB RAM；分配器还要取
最高 type 1 RAM 页和 direct-map 容量的较小值。标准叶
`CPUID.(EAX=7,ECX=0):ECX[16]` 表示 LA57 能力，当前只记录该能力，不设置
`CR4.LA57`。

#### CPUID 与 v1.3 完整处理器规格

v1.3 在使用任何相关能力前统一读取标准与扩展特性叶：

| 叶 | EDX 位 | 名称 | 项目 feature mask |
| --- | ---: | --- | ---: |
| `0x00000001` | 24 | FXSR | bit 2 |
| `0x00000001` | 25 | SSE | bit 3 |
| `0x00000001` | 26 | SSE2 | bit 4 |
| `0x80000001` | 11 | SYSCALL/SYSRET | bit 5 |
| `0x80000001` | 20 | NX | bit 1 |
| `0x80000001` | 29 | long mode | bit 0 |

required mask 固定为 `0x3F`。任何位缺失都会输出
`PROCESSOR_FEATURES_UNSUPPORTED` 与 missing mask，并在扩展现场/GDT 前停止。
`CPUID.80000008H` 还必须报告 36..52 位物理地址和精确 48 位虚拟地址。

能力检查通过后，内核按上表配置 CR0/CR4，并用 `FNINIT`、
`LDMXCSR [0x1F80]`、`FXSAVE64` 建立初始模板。每个 Thread 的保存区固定
512 字节并按 16 字节对齐：

```text
offset 0x000  FCW/FSW/FTW/FOP                8 B
offset 0x008  FIP/FDP                        16 B
offset 0x018  MXCSR/MXCSR_MASK                8 B
offset 0x020  ST0..ST7/MMX（每槽 16 B）     128 B
offset 0x0A0  XMM0..XMM15（每槽 16 B）      256 B
offset 0x1A0  reserved/software               96 B
total                                         512 B
```

`FXSAVE64` 的 64 位格式与普通中断帧相互独立；`IRETQ` 不会恢复这块状态。
调度边界必须显式 `FXSAVE64 current` 和 `FXRSTOR64 next`。本阶段清
OSXSAVE，所以 YMM 上半部不可用，不会出现“能执行 AVX 但内核只保存 XMM”
的部分支持。

### 2.3 长模式描述符寄存器

| 状态 | 可见内容 | 装载/读取 | v0.8 不变量 |
| --- | --- | --- | --- |
| GDTR | 64 位 base + 16 位 limit | `LGDT` / `SGDT` | base 指向七槽内核 GDT，limit=55 |
| IDTR | 64 位 base + 16 位 limit | `LIDT` / `SIDT` | base 指向 256 槽 IDT，limit=4095 |
| CS | 16 位选择子 + 隐藏描述符 | far control transfer | `0x08`，Ring 0 长模式代码段 |
| SS | 16 位选择子 + 隐藏描述符 | `MOV SS` | `0x10`，Ring 0 数据段 |
| TR | 16 位选择子 +隐藏 TSS 描述符 | `LTR` / `STR` | `0x28`，busy 64-bit TSS |
| CR2 | 页故障线性地址 | `MOV reg, CR2` | 向量 14 的 panic 或用户终止现场 |

描述符表 limit 都是“最后一个有效字节偏移”，不是字节数。七个 8 字节 GDT 槽
的 limit 是 \(7\times8-1=55\)；256 个 16 字节 IDT 槽的 limit 是
\(256\times16-1=4095\)。

### 2.4 当前 GDT 与 TSS 结构

```text
GDT[0]  8 B  null
GDT[1]  8 B  Ring 0 code, selector 0x08
GDT[2]  8 B  Ring 0 data, selector 0x10
GDT[3]  8 B  Ring 3 data, selector 0x1B
GDT[4]  8 B  Ring 3 code, selector 0x23
GDT[5]  8 B  TSS descriptor low, selector 0x28
GDT[6]  8 B  TSS descriptor high
```

64 位 TSS 描述符由 limit[19:0]、base[63:0]、type=9、DPL、present 和粒度
字段组成。执行 `LTR` 后处理器把 type 从 available 9 改为 busy 11。
TSS 内存布局：

| 偏移 | 宽度 | 字段 | 当前用途 |
| ---: | ---: | --- | --- |
| `0x04` | 64 | RSP0 | 当前 Thread 动态 16 KiB Ring 0 栈顶；上下各有一页 guard |
| `0x0C` / `0x14` | 64 | RSP1 / RSP2 | 保留为零 |
| `0x24` | 64 | IST1 | 双重故障栈 |
| `0x2C` | 64 | IST2 | NMI 栈 |
| `0x34` | 64 | IST3 | 机器检查栈 |
| `0x3C..0x5C` | 64 | IST4..IST7 | 保留为零 |
| `0x66` | 16 | I/O bitmap offset | 104，位于 TSS limit 之后 |

### 2.5 v1.2 调度切换的 CPU 状态

round-robin 的一次切换跨越三个硬件状态集合：

| 状态 | 旧 Thread 保存位置 | 新 Thread 恢复动作 | 不变量 |
| --- | --- | --- | --- |
| 通用寄存器 | 动态 Ring 0 栈上的 15 个 64 位槽 | 汇编逆序 `POP` | 帧地址属于对应 Thread 活动栈 |
| RIP/CS/RFLAGS/RSP/SS | CPU 特权帧 | `IRETQ` | CS.RPL=3、SS.RPL=3、IF=1 |
| x87/MMX/XMM/MXCSR | Thread 的 512 B FXSAVE 区 | `FXRSTOR64` | 区域 16 字节对齐且属于目标 Thread |
| CR3 | Process 地址空间根 | `MOV CR3` | 4 KiB 对齐且不是永久内核根 |
| TSS.RSP0 | TSS 内存字段 | 普通 64 位写并读回 | 指向新 Thread 动态栈顶且 16 字节对齐 |

IRQ0 到来时 CPU 已自动使用“旧”RSP0 压帧。C++ 可以在该栈上切换 CR3，
因为所有进程页表都共享 supervisor 内核代码以及 KVA 高半内核栈页表子树。
四个数据页为 RW/NX，lower/upper guard 始终 not-present。
但在执行新 Thread `IRETQ` 前必须写入“新”RSP0；否则下一次系统调用会在旧栈
压帧并破坏被挂起现场。

写 CR3 会刷新当前处理器的大多数非 global TLB 项。本阶段没有设置 global
页或 PCID，所以每次抢占都支付完整地址空间切换成本。这是明确、可验证的初始
语义；以后优化必须同时定义 PCID 分配、复用和失效规则。

### 2.6 v1.3 原生系统调用 MSR 与 CpuLocal

| MSR | 地址 | 当前内容 |
| --- | ---: | --- |
| IA32_EFER | `0xC0000080` | 保留 LME/LMA/NXE 并设置 SCE |
| IA32_STAR | `0xC0000081` | `0x0010000800000000` |
| IA32_LSTAR | `0xC0000082` | `OsKernelNativeSystemCallEntry` 规范地址 |
| IA32_FMASK | `0xC0000084` | `0x0000000000044700`，清 TF/IF/DF/NT/AC |
| IA32_GS_BASE | `0xC0000101` | 用户 GS base，当前为 0 |
| IA32_KERNEL_GS_BASE | `0xC0000102` | 64 字节对齐 CpuLocal 地址 |

STAR[47:32]=`0x08` 供 SYSCALL 选择内核代码段，内核 SS 隐式为 `0x10`。
STAR[63:48]=`0x10` 是 SYSRET 基值，不是最终用户 CS；处理器由它得到
SS=`0x1B`、CS=`0x23`。

`SWAPGS` 交换两个 GS base MSR 的活动角色。原生入口先交换，再按以下稳定
ABI 访问 CpuLocal：

| 偏移 | 宽度 | 字段 | 入口用途 |
| ---: | ---: | --- | --- |
| 0 | 64 | self address | C++ 完整性验证 |
| 8 | 64 | current Thread slot | 调度所有权与诊断 |
| 16 | 64 | trusted kernel entry RSP | `SYSCALL` 立即换栈 |
| 24 | 64 | transient user RSP | 换栈前暂存，随后与 UserContext 交叉验证 |

TSS.RSP0 与偏移 16 必须在激活 Thread 时写入同一个动态栈顶；前者供 IDT
interrupt gate，后者供 SYSCALL。UserContext 总长 176 字节，SYSRET 只用于
已验证的 48 位低半规范 RIP/RSP 和安全 RFLAGS；其他合法现场使用 IRETQ。

### 2.7 IDT gate 位字段

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
- breakpoint、overflow 与 `0x80` 系统调用门使用 DPL=3；其余门 DPL=0。
- IST 为 0 使用当前/特权栈；1..7 选择 TSS 的对应专用栈。
- selector 固定引用当前 Ring 0 代码段 `0x08`。

### 2.8 架构异常与错误码

| 向量 | 缩写 | 类别 | CPU 错误码 | 当前 IST / 策略 |
| ---: | --- | --- | --- | --- |
| 0 | #DE | fault | 无 | panic |
| 1 | #DB | fault/trap | 无 | panic |
| 2 | NMI | interrupt | 无 | IST2 / 最小 fail-stop，不进入普通 panic |
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

### 2.9 页故障错误码

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

not-present 页故障注入得到错误码 0：supervisor 读取一个 not-present 数据页。
CR2=`0x04000000`，正好是当前 64 MiB 身份映射的第一个未映射线性地址。
v0.6 写保护注入得到错误码 3：P=1、W/R=1、U/S=0，表示 supervisor 写入
present 但只读的 `0xFFFF800000100000`，证明 CR0.WP 生效。
v0.8 用户程序读取未映射 `0x30000000` 得到错误码 4：P=0、W/R=0、
U/S=1，证明访问确实来自 CPL3，而不是内核替用户触发故障。

### 2.10 四级页表项

48 位 canonical 虚拟地址按以下位段拆分：

```text
63            48 47       39 38       30 29       21 20       12 11       0
+----------------+-----------+-----------+-----------+-----------+-----------+
| bit47 符号扩展  | PML4 index| PDPT index|  PD index |  PT index | page off  |
+----------------+-----------+-----------+-----------+-----------+-----------+
```

四级索引各 9 位，一个页表页正好容纳 512 个 64 位表项。当前 4 KiB 叶项关键位：

| 位 | 名称 | 当前语义 |
| ---: | --- | --- |
| 0 | P | 1 表示存在；guard 和零页保持 0 |
| 1 | RW | 1 可写，0 只读；CR0.WP 使 Ring 0 也受约束 |
| 2 | US | 内核映射为 0；用户映射在四级路径上均为 1 |
| 4 | PCD | 1 禁用页级缓存；LAPIC MMIO 映射必须置一 |
| 7 | PS | PD 叶为 1 表示 2 MiB 页；direct-map 内部区间使用 |
| 12..51 | physical address | 4 KiB 对齐页帧地址 |
| 63 | NX | EFER.NXE=1 时禁止取指 |

上级权限会限制下级：任一层 US=0 都禁止用户访问，任一层 RW=0 都限制写。
2 MiB PDE 的物理基址位为 21..51，低 21 位必须为零；查询地址时把虚拟地址
低 21 位加回基址。项目中间表保持 writable，使叶项决定内核页写权限；用户
映射请求出现时才向父项传播 US。修改活动叶项后执行 `INVLPG`，加载新 CR3
时刷新当前地址空间翻译。

### 2.11 异常、硬件 IRQ 与 IDT 向量

v0.8 的 IDT present 范围为：

| 向量 | 来源 | 汇编入口 | C++ 策略 |
| ---: | --- | --- | --- |
| `0x00..0x1F` | CPU 架构异常 | exception stubs | Ring 0 恢复/panic；Ring 3 终止用户 |
| `0x20..0x27` | 8259A master IRQ0..7 | hardware IRQ stubs | 设备处理、主片 EOI |
| `0x28..0x2F` | 8259A slave IRQ8..15 | hardware IRQ stubs | 设备处理、从片后主片 EOI |
| `0x80` | Ring 3 `INT 0x80` | system call entry | 校验帧、分发、返回或退出 |
| 其他 | 未分配 | not-present | 禁止误入 |

本地 APIC、I/O APIC 和 PIC 是三种不同状态。只设置 RFLAGS.IF 不能建立它们
之间的路由；当前单核阶段保持本地 APIC 启用，把 SVR 与 LVT LINT0 配成
virtual-wire，再使用传统 8259A。

### 2.12 Local APIC virtual-wire 寄存器

LAPIC 基址来自当前 xAPIC 实现使用的 `IA32_APIC_BASE[35:12]`，内核将对应页
做 supervisor RW/NX/PCD 身份映射。当前只使用两个 32 位 MMIO 寄存器：

| 基址内偏移 | 寄存器 | 字段 | v0.7 策略 |
| ---: | --- | --- | --- |
| `0x0F0` | SVR | vector[7:0]、software enable[8] | vector=`0xFF`，enable=1 |
| `0x350` | LVT LINT0 | delivery mode[10:8]、mask[16] | ExtINT=`111b`，mask=0 |

初始化先验证 CPUID 的 APIC 能力、全局启用位和 x2APIC 关闭状态，再写 SVR、
写 LINT0 并逐字段回读。LAPIC 此时只把 8259A 输出桥接到处理器；中断向量仍由
PIC 的 INTA 周期提供，处理完成也仍向 PIC 发送 EOI，不写 LAPIC EOI。

### 2.13 特权转换时的硬件栈帧

从 CPL3 通过 interrupt gate 进入 CPL0 时，CPU 先从当前 TSS 读取 RSP0，
再在新内核栈上保存：

```text
高地址  用户 SS
        用户 RSP
        RFLAGS
        用户 CS
低地址  用户 RIP
```

随后汇编入口补入错误码/向量并保存 15 个通用寄存器。公共
`ExceptionFrame` 占 160 字节，特权来源额外带 RSP/SS，总计 176 字节。
返回 Ring 3 的 `IRETQ` 消费全部五个硬件字段；exit 与用户异常不再消费该帧，
而是恢复进入调度前单独保存的永久内核 RSP。终止栈只有在该恢复完成、当前
RSP 已位于栈外的安全点才撤销映射并回收物理后备。

首次从 Ring 0 降到 Ring 3 也使用同一种五字段形状，但由软件主动压入
SS=`0x1B`、用户栈顶、RFLAGS=`0x202`、CS=`0x23` 和 ELF 入口。RFLAGS
的 IOPL 位保持 0，所以用户程序即使 IF=1 也不能绕过系统调用直接访问 I/O
端口。

## 3. 8259A 可编程中断控制器

两片 8259A 通过主片 IRQ2 级联。初始化序列不可交换：

| 次序 | 主片数据 | 从片数据 | 语义 |
| ---: | ---: | ---: | --- |
| ICW1 | command=`0x11` | command=`0x11` | 开始初始化，需要 ICW4 |
| ICW2 | `0x20` | `0x28` | 向量基址 |
| ICW3 | `0x04` | `0x02` | 主片 IRQ2 有从片；从片级联 ID=2 |
| ICW4 | `0x01` | `0x01` | 8086/88 模式 |

初始化后 IMR=`0xFFFF`。v0.7 首次设备闭环改为 `0xFFFC`，仅允许 IRQ0/IRQ1；
v1.16 运行期改为 `0xBFF8`，同时允许 master IRQ2 cascade 与 slave IRQ14。
OCW3=`0x0B` 让 command 端口读取 ISR；非特定 EOI 为 `0x20`。

IRQ7 若 ISR bit7=0，是主片虚假中断，不发送 EOI。IRQ15 若从片 ISR bit7=0，
不向从片 EOI，但要向主片确认级联 IRQ2。真实从片 IRQ 总是先确认从片，再确认
主片，否则主片可能持续认为级联仍在服务。

## 4. 8254 PIT 与单调 tick

PIT 输入频率为 1193182 Hz。端口 `0x43` 写 `0x34` 表示通道 0、低/高字节、
模式 2、二进制计数；端口 `0x40` 先写除数低字节、再写高字节。当前除数
`0x04A9` 产生约 1000 Hz IRQ0。

计数器只有 16 位且会重复装载，不能直接作为长期时间。内核 IRQ0 维护 64 位
tick，再用实际除数计算：

```text
milliseconds = ticks × divisor × 1000 / 1193182
```

乘法前检查上界。整数结果向下取整，所以第 16 个 tick 可显示 15 ms；这不是
丢中断，而是硬件除数与整数时间单位的正常量化。

## 5. i8042 与 PS/2 键盘

| 端口 | 读 | 写 |
| ---: | --- | --- |
| `0x60` | 控制器/设备输出数据 | 第一 PS/2 设备命令或配置 byte |
| `0x64` | status | 控制器命令 |

status bit0=1 才能读 `0x60`，bit1=0 才能写命令或数据。初始化使用
`AD/A7` 关闭两端口、`20` 读取配置、`60` 写配置、`AE` 开第一端口；配置
打开 bit0 IRQ1 与 bit6 translation，清 bit1 IRQ12 和 bit4 第一端口时钟禁用。
键盘命令 `F4` 开扫描，必须收到 `FA` ACK。

集合 1 的 `A` 按下为 `0x1E`，释放为 `0x9E`；`0xE0` 是扩展前缀，下一字节
才构成完整方向键事件。前缀本身不能被错误报告为按键。

## 6. VGA 文本控制台与历史 COM1

当前系统由 ROM 直接编程 VGA `0x3C0..0x3DF`，把 80×25 文本单元写入
`0xB8000`。每个单元低字节为字符，高字节为前景/背景属性；当前默认属性
`0x07` 表示浅灰前景和黑色背景。字符到达第 25 行后，目标代码把后 24 行
前移并清空末行。

`0x3C6` 是 DAC mask，`0x3C8` 选择写入索引，连续写 `0x3C9` 提交红、绿、蓝
三个 6 位分量。自研 ROM 不经过 VGA BIOS，因此必须自行设置属性控制器引用的
16 个 EGA 兼容 DAC 表项；只写 `0xB8000` 而保留复位后的全黑 DAC，会得到有
字符、有追踪但物理扫描画面全黑的结果。

VGA 字形存储在字符平面，不能依赖未执行的第三方 VGA BIOS。ROM 装载项目自带的
Basic Latin 字形，并通过 CRTC `0x0E/0x0F` 更新硬件光标。完整自动化记录位于
`0x20000..0x9FFFF`：启动阶段同时记录并显示，终端激活后普通内核诊断只记录，
TTY 输出记录后显示，panic 始终尽力显示。

以下 COM1 内容只解释历史 v0.1/v0.2，不再是当前输出路径。

### 6.1 历史 16550A 寄存器窗口

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

### 6.2 历史 LCR 与 LSR 位

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

历史固件只把 `THRE` 作为发送所有权条件；它不把 `TEMT` 误当成“可以写入”的
唯一条件，也不启用中断。当前源码已由 VGA 文本控制台取代该发送路径。

## 7. PATA IDE 主通道与 ATA 状态

### 7.1 命令块寄存器

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

### 7.2 STATUS 位

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

### 7.3 ERROR 位

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
`KERNEL_ATA_ERROR`。两条路径使用 nIEN 和有界轮询。v1.16 Kernel runtime
则开放 IRQ14，以 64 槽 FIFO 和单个 Issued 请求拥有通道：Read 在 DRQ IRQ
搬运数据，Write 在 DRQ IRQ 写入后等待完成 IRQ，Flush 只等待完成；PIT
deadline 获胜后以 SRST 重置设备，迟到 IRQ 不能覆盖 TimedOut。

运行期中断路由为 ATA IRQ14 → slave IR6 → master IR2 cascade → LAPIC LINT0
ExtINT → IDT vector `0x2E`。真实 slave IRQ 必须先向 slave 发送 EOI，再向
master 发送 EOI。

## 8. 结构化描述与代码的对应关系

### 8.1 System Control Port A 与 A20

Stage 1 通过 I/O 端口 `0x92` 的位 1 打开 Fast A20 Gate，同时强制位 0 为零，
避免触发快速复位。写入后并不直接相信控制位，而是暂存物理地址 `0x000000` 与
`0x100000` 的原值，写入不同模式并检查它们是否仍然独立，最后恢复原值。

| 位 | 名称 | 本项目处理 |
| ---: | --- | --- |
| 0 | Fast Reset | 始终写零，禁止复位 |
| 1 | A20 Gate | 写一后执行地址别名验证 |

### 8.2 QEMU fw_cfg 与 `etc/e820`

当前 PC 机器模型提供传统端口形式的 `fw_cfg`：

| 端口 | 宽度 | 方向 | 语义 |
| ---: | ---: | --- | --- |
| `0x510` | 16 | 写 | 选择 key；x86 端口写入采用本机小端 |
| `0x511` | 8 | 读 | 顺序读取当前 key 的数据流 |

关键 selector：

| key | 内容 | 编码 |
| ---: | --- | --- |
| `0x0000` | 签名 | 四字节 ASCII `QEMU` |
| `0x0019` | 文件目录 | 32 位大端数量 + 固定 64 字节目录项 |
| 目录项 selector | `etc/e820` 文件 | selector 从目录项的 16 位大端字段取得 |

目录项由 32 位大端 size、16 位大端 selector、16 位 reserved 和 56 字节
NUL 结尾名称组成。`etc/e820` 数据本身每项是 x86 小端的 64 位 base、
64 位 length、32 位 type，共 20 字节。目录与文件使用不同端序是最容易出现
“找到文件但长度荒谬”的边界。

项目不使用 DMA 扩展，只用 PIO 数据端口逐字节读取。Stage 1 把条目扩展为
24 字节并排序；内核不直接依赖 `fw_cfg`。硬件接口依据 QEMU
[fw_cfg 规范](https://qemu.readthedocs.io/en/master/specs/fw_cfg.html)。

- `docs/hardware/register_map.yaml`：芯片、寄存器、位和访问宽度的机器可读规格。
- `source/firmware/src/reset_and_vga.asm`：固件端口访问与 ATA 状态机实现。
- `source/boot/stage1/src/entry.asm`：模式切换、页表和 Stage 1 串口路径。
- `source/boot/stage1/src/kernel_loader.asm`：长模式 ATA、CRC32、ELF 和 BootInfo。
- `source/boot/stage1/src/memory_map.asm`：`fw_cfg`、E820 转换与排序。
- `source/kernel/src/arch/architecture.asm`：LGDT/LIDT/LTR、异常桩和统一寄存器保存。
- `source/kernel/src/arch/interrupt_runtime.cpp`：IRQ 分发、同步快照和设备启动组合。
- `source/kernel/src/device/legacy_pic.cpp`：PIC 初始化、屏蔽、ISR 与 EOI。
- `source/kernel/src/device/programmable_interval_timer.cpp`：PIT 模式和除数写入。
- `source/kernel/src/device/ps2_keyboard.cpp`：i8042 与键盘 ACK 握手。
- `source/kernel/src/device/ata_pio.cpp`：内核 LBA28 单扇区读取。
- `source/kernel/src/arch/descriptor_tables.cpp`：GDT、TSS、IDT 构造与硬件回读验证。
- `source/kernel/src/memory/memory_manager.cpp`：页帧、四级页表、权限、guard 与堆。
- `source/kernel/src/user/user_elf.cpp`：严格 ELF64 用户文件解析。
- `source/kernel/src/user/user_memory.cpp`：用户页装载、栈与指针逐页检查。
- `source/kernel/src/user/system_calls.cpp`：`INT 0x80` 帧验证与系统调用分发。
- `source/kernel/src/process/thread_scheduler.cpp`：与硬件无关的
  Process/Thread 状态、run queue、WaitQueue 和量子决策。
- `source/kernel/src/arch/extended_state.cpp`：CPUID、CR0/CR4 与每 Thread
  FXSAVE/FXRSTOR 现场协议。
- `source/kernel/src/process/process_runtime.cpp`：CR3、TSS.RSP0、保存帧与资源生命周期。
- `source/kernel/src/memory/kernel_stack_manager.cpp`：KVA 支持的每 Thread Ring 0 动态栈、
  双 guard、精确所有权验证和安全点回收。
- `source/user/src/system_call.asm`：Ring 3 系统调用指令入口。
- `source/kernel/src/arch/panic.cpp`：异常现场和 CR2 的有界紧急诊断。
- `source/kernel/src/device/vga_text_console.cpp`：内核 VGA 文本、滚屏与内存日志访问层。
- `tests/tooling/test_qemu_runner.py`：内存日志标记、VGA 文本页和像素截图门禁。
- `docs/testing.md`：状态边界对应的 QEMU 失败注入。

以后新增 PCI、LAPIC、I/O APIC 或 MSI 时，先在这份结构化规格中定义寄存器和
标志，再实现驱动访问层；策略代码不能直接散落端口号。

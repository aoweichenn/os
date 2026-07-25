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

## v2.0 目标架构（演进中）

本节描述第二周期的目标依赖方向，不代表当前 v1.1 已经具备这些模块。每个箭头
只能依赖下层公开契约，不允许 Shell、进程或 VFS 绕过边界直接操作 ATA、
页表或执行实体内部结构。

```text
/sbin/init ──fork/exec/wait──> /bin/sh ──pipe/dup/job control──> /bin/*
       └──────────── User ABI v2 / 自研用户运行时 / TLS / futex ─────────┘
                                      ↓
          SignalDisposition <── Process ──owns──> Thread(s) ──> Scheduler
                                  │                      │          │
                                  │                      │          └─ WaitQueue / Deadline
                                  │                      └─ UserContext / TLS / SignalMask
                                  ├──> FileTable ──> FileDescription ──> VFS
                                  │                                      ├─ rootfs v2
                                  │                                      ├─ devfs
                                  │                                      └─ procfs
                                  │
                                  └──> AddressSpace ──> VMA ──> #PF / Demand paging / COW
                                            ↓
                              clean → dirty → writeback cache
                                            ↓
                      ordered metadata journal / BlockRequest / ATA IRQ14
                                            ↓
                    KVA / Object allocator / Buddy / Direct map
                                            ↓
                                  E820 / CPUID hardware facts

          CpuLocal ──> current Thread / trusted entry stack / need-resched
```

目标架构把“身份”和“存储位置”分开：

- PID 与 TID 是相互独立的 64 位身份，不是对象数组下标；
- Process 是共享资源容器，Thread 才是调度实体；
- fd 是进程局部引用，不是文件系统句柄或管道数组下标；
- open-file description 持有偏移和状态，多个 fd 可以引用同一对象；
- vnode 表示文件系统对象，不暴露具体 inode 的磁盘布局；
- VMA 表示用户虚拟区间，不等同于已经分配的物理页；
- block request 表示设备事务，不等同于缓存条目或文件页；
- 资源限制是运行时策略，不是对象表的编译期长度。

### Process、Thread 与架构现场

Process 是共享资源容器，Thread 是唯一调度实体。Process 的终止状态与
Thread 的退出状态属于不同状态机：

```text
Process
  ├─ ProcessId / parent / children / process group
  ├─ AddressSpace / FileTable / FsContext / SignalDisposition
  └─ Thread list
       └─ Thread
            ├─ ThreadId / Ready | Running | Blocked | Exited
            ├─ general registers + FXSAVE x87/SSE2 state
            ├─ kernel stack / user stack / TLS base
            └─ SignalMask / WaitQueue membership / WakeReason
```

`ThreadExit` 只发布当前 Thread 的栈、现场和等待关系；最后一个 Thread
离开才触发 ProcessExit。ProcessExit 发布共享对象后为父 Process 留下
Process Zombie，`wait` 只观察这个 Process 层状态。

v1.3 的两个用户入口规范化为同一个 `UserContext`：

```text
INT 0x80 ─┐
          ├─ validate + normalize → dispatcher → return validation
SYSCALL ──┘                                      ├─ safe → SYSRETQ
                                                 └─ fallback → IRETQ
```

`SYSCALL` 入口先通过 `SWAPGS` 取得单元素 CpuLocal，再从可信字段加载当前
Thread 的内核栈；绝不在用户 RSP 上压入内核数据。CpuLocal 还保存 IRQ 深度、
禁止调度深度和 need-resched。它是 BSP 本地入口状态，不表示已经实现 SMP。
每次上下文切换都用 `FXSAVE/FXRSTOR` 隔离 x87/SSE2；AVX/XSAVE 在 v2.0
禁用。

### 单 BSP 内核执行与锁模型

目标内核允许 IRQ 打断可中断 Ring 0 代码，但不允许调度器在任意内核调用链
中抢占。合法调度点只有显式阻塞、让出、Thread 退出和返回用户态之前。IRQ
只提交设备状态、唤醒 WaitQueue 并设置 need-resched，永不直接切换到另一
Thread。

| 原语 | 使用边界 | 禁止事项 |
| --- | --- | --- |
| SpinLock | 仅 Thread 上下文访问的短提交区 | 睡眠、调度、用户复制和无界日志 |
| IrqSaveSpinLock | 与当前 BSP IRQ 共享的短状态 | IRQ 递归取得、睡眠和长循环 |
| Mutex | Thread 上下文中的可睡眠临界区 | IRQ/NMI 使用、持有 spinlock 后等待 |

所有阻塞对象统一使用 WaitQueue。条件满足、deadline、signal、对象关闭和
unmap cancellation 竞争同一个原子 WakeReason；只有第一个成功提交者负责
移除等待关系和让 Thread Ready。NMI 不进入这些通用路径，只能记录最小故障
事实或进入不依赖动态资源的 panic。

### 多线程系统调用语义

| 操作 | 目标语义 |
| --- | --- |
| fork | 子 Process 只包含调用 fork 的 Thread |
| exec | 先在候选 AddressSpace 完成验证；成功后汇合兄弟 Thread 并原子替换，失败保持原组不变 |
| ThreadExit | 只退出当前 Thread |
| ProcessExit | 终止整个 Thread 组并产生 Process Zombie |
| signal | disposition 属于 Process，mask 属于 Thread |
| private futex | key 为 `(AddressSpaceId, aligned user VA)`，unmap 必须取消 waiter |
| CopyToUser | 写 COW 页前必须执行与写页故障相同的私有化 |

阻塞 syscall 对“已有部分进度、尚无进度、被 signal、超时、对象关闭”的返回
规则必须逐调用冻结。exec 的 `argv/envp` 使用可回收页分批暂存，不能在动态
内核栈上申请 128 KiB 连续缓冲。

### VM、缓存与持久化层次

VMA 只描述地址意图，PTE 只描述当前驻留。匿名页故障先于文件页故障，文件页
先进入有界 clean cache；dirty/writeback 状态在异步块层稳定后才开放。

```text
VMA policy
  ├─ anonymous → zero-fill fault → private PhysicalPage
  └─ file      → (Vnode, page index) clean CachePage
                       ├─ MAP_PRIVATE write → COW private page
                       └─ read-only MAP_SHARED → shared clean page
```

v2.0 不支持 writable `MAP_SHARED` 或 `msync`。write/truncate 必须使受影响
clean cache 失效并撤销现有文件映射，不能继续暴露陈旧页面。clean 页可由
LRU 丢弃后重读；dirty/writeback 页必须等写入完成或报告明确错误。

journal 只记录元数据，并使用 ordered mode：相关文件数据先写到稳定介质，
元数据 commit 才允许持久化。每个事务在修改前预留 credits，经
descriptor、metadata、flush、commit、checkpoint 与 replay 前进。任意
已覆盖断电点恢复后只能看到旧事务或完整新事务。

### v2.0 正常启动控制流

```text
自研 ROM → Stage 1 → Kernel ELF
  → 验证 BootInfo、内存图和处理器状态
  → 初始化可回收页/对象分配器
  → 建立 VFS，挂载 rootfs v2、devfs、procfs
  → 从 /sbin/init 读取并严格验证 ELF
  → 创建唯一初始 Process/Thread 并 exec /sbin/init（PID1）
  → PID1 启动 /bin/sh，并持续 wait/reap 孤儿
  → Shell fork/exec 外部命令，使用描述符组合 I/O
  → 无 Ready 进程时进入 STI/HLT/CLI idle
```

生产正常路径不再内嵌 Shell、生产者、消费者和 worker 四个用户 ELF。故障测试
可以继续使用最小内嵌载荷验证“根文件系统尚未可信之前”的用户异常边界，但它们
不得成为正常启动依赖。

### 资源与并发边界

v2.0 仍是单处理器内核，但“单核”不等于可以忽略资源生命周期：

- Process、Thread、页、VMA、vnode、open-file description、pipe、
  signal frame 和 block request 都必须有唯一所有者或显式引用计数；
- 任意可阻塞路径在睡眠前不得持有禁止调度的自旋锁；
- 用户复制不得发生在 VFS、页缓存、日志或设备锁内；
- 锁顺序由模块文档统一固定，失败回滚按获得资源的逆序执行；
- 热路径只更新有界统计，日志在状态提交后汇总输出。

多核启动、per-CPU 调度队列和跨核 TLB shootdown 不进入当前目标。正/负
dentry cache、writable shared mapping、swap 和 OOM killer 同样延后，避免
在 v2.0 主线中引入尚无独立验收闭环的并发状态。

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
| `0x00100000..0x03DFFFFF` | Kernel `PT_LOAD` 目标窗口 |
| `0x03E00000..0x03EFFFFF` | 最大 1 MiB ELF 暂存 |
| `0x03FEF000..0x03FFEFFF` | 早期内核栈保留区 |

BootInfo magic 为 `OSBOOT64`，版本 2 的 13 个字段全部为 64 位。Stage 1 把其地址放入
`RDI`，在 16 字节对齐的栈上使用 `CALL` 进入 `OsKernelEntry`，从而满足
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

v0.5 当时的选择子为代码段 `0x08`、数据段 `0x10`、TSS `0x18`。TSS 描述符占用
GDT 的两个连续 8 字节槽，因为 64 位 TSS 基址无法放进传统单槽描述符。
IDT 每槽 16 字节，完整表为 4096 字节；v0.5 时外部中断向量保持
not-present，直到 v0.7 建立控制器确认协议后才开放 32..47。
v0.8 又加入 Ring 3 数据/代码段，把 TSS 移到 `0x28`，并开放 DPL3 的
`0x80` 系统调用门；后文用户边界记录当前布局。

异常入口分成硬件、汇编和 C++ 三层：

```text
CPU 压入 RIP / CS / RFLAGS / 可选错误码
  ↓
每向量 NASM 桩补齐“无错误码”占位并压入向量号
  ↓
公共桩清 DF、保存 RAX..R15、对齐 System V AMD64 栈
  ↓
OsKernelDispatchException(ExceptionFrame*)
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
  ├─ 三个 IST 栈底 guard: not-present
  └─ TSS.RSP0 特权转换栈底 guard: not-present
高半区 0xFFFF800000000000
  ├─ 64 KiB heap: RW + NX
  └─ 0xFFFF800000100000: R + NX 写保护测试页
       ↓ 设置 EFER.NXE 与 CR0.WP
内核 CR3 = 新 PML4 物理地址
```

页帧状态用 2 bit 编码：不可用、空闲、已分配、已保留。相较一位位图，它多
占 4 KiB 状态存储，却能区分“永不可分配”“启动所有权”“动态所有权”，从而
让重复释放、释放保留页和跨已分配页保留都具有明确失败语义。v0.6 当时只管理
低 64 MiB；该限制已在 v1.1 的动态物理内存增量中解除。

高半区堆是单调早期分配器：支持二的幂对齐和显式失败，不支持释放。该选择用于
启动期对象，而不是声称已经完成通用内核堆。页表管理器同样暂不回收空的中间
页表；v0.7 设备初始化可以使用当前堆，通用释放和页表生命周期将在进程地址
空间出现前补充。设计决策见
[ADR 0009](adr/0009-fw-cfg-memory-map-and-kernel-page-tables.md)。

## v0.7 传统中断与设备闭环

QEMU `pc` 同时包含 8259A、I/O APIC 和本地 APIC。没有外部 BIOS 时，内核
不能假定复位后的 APIC 已替 8259A 建立虚拟线模式。内存管理器先把
`IA32_APIC_BASE` 给出的 LAPIC MMIO 页映射为 supervisor RW/NX/PCD；设备
启动保持 LAPIC 全局启用和 x2APIC 关闭，设置 SVR 软件启用，再把 LVT LINT0
配置为未屏蔽的 ExtINT。全部字段回读成功后才初始化两片 PIC：

```text
IA32_APIC_BASE → LAPIC MMIO page（RW/NX/PCD）
       ↓ SVR[8]=1、vector=0xFF
LVT LINT0 delivery=ExtINT、mask=0
       ↓ virtual wire
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
STI → HLT → IRQ0/IRQ1 → CLI
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

## v0.8 用户执行边界与 v0.9 调度入口

用户 ELF 在宿主构建时只是作为原始文件嵌入 Kernel；决定它能否执行的是目标
内核的严格解析器。正常路径的控制与数据关系如下：

```text
embedded ET_EXEC ELF64
        ↓ ValidateUserElf：结构、范围、权限、入口
PageFrameAllocator → AllocateAndMapUserPage
        ↓ PML4/PDPT/PD/PT 全层 U/S=1
0x40000000 用户段 + 高端四页用户栈
        ↓ ProcessRuntime 构造首个 176 字节保存现场
OsKernelEnterScheduledProcess → 恢复通用寄存器与五项特权帧 → IRETQ
        ↓ CPL3
用户 C++ → INT 0x80
        ↓ CPU 从 TSS 取 RSP0，压用户 SS/RSP/FLAGS/CS/RIP
系统调用汇编入口 → ValidateUserSystemCallFrame → C++ 分发
        ├─ 普通返回：恢复帧 → IRETQ → CPL3
        └─ exit/异常：回收当前地址空间 → 切换下一个进程或恢复内核调用者
```

v0.8 首次打通该边界时使用单次进入函数 `OsKernelEnterUserMode`；v0.9 已将
它替换为可从每进程内核栈恢复完整现场的
`OsKernelEnterScheduledProcess`。因此当前实现不存在“全局保存一个用户调用
者栈”的隐式单进程前提。

关键地址布局：

| 虚拟范围 | 权限 | 所有者/用途 |
| --- | --- | --- |
| `0..0xFFFF` | 不允许作为用户范围 | 捕获低地址与空指针 |
| `0x40000000...` | 按 ELF 为 RX、R/NX 或 RW/NX | 首批用户程序 |
| `0x00007FFFFFFEA000` | not-present | 用户栈 guard |
| `0x00007FFFFFFEB000..0x00007FFFFFFEFFFF` | user RW/NX | 四页用户栈 |
| `0x00007FFFFFFF0000` | 栈顶，不映射为叶页 | 初始用户 RSP |

GDT 中 CPL3 数据/代码选择子分别为 `0x1B`、`0x23`，TSS 选择子为
`0x28`。TSS.RSP0 不再指向当前启动调用栈，而指向专用 16 KiB 转换栈，
避免系统调用压栈覆盖“等待用户程序返回”的内核帧。

用户系统调用帧比 Ring 0 异常帧多旧 RSP 与 SS，公共前 160 字节保持兼容，
完整 `UserPrivilegeFrame` 为 176 字节。异常分发先查看保存 CS 的 RPL：
CPL0 故障沿用 panic；CPL3 故障写入有界的 `UserExecutionResult` 并恢复内核。
设计权衡见
[ADR 0011](adr/0011-user-mode-elf-and-int80-boundary.md)，代码边界见
[User 与 ABI 模块](modules/user.md)。

## v0.9 独立地址空间与抢占调度

v0.8 的单次“进入用户态—返回内核”被扩展成可反复保存与恢复的进程现场。
调度器是纯状态模型，不接触 CR3、TSS 或串口；`process_runtime` 负责把状态
决策落实到硬件和资源生命周期：

```text
PIT IRQ0 → 统一 176 字节用户帧 → HandleTimerTick
                                    │
                         时间片未到 ├─→ 原帧 IRETQ
                                    │
                         时间片到期 └─→ PCB 保存旧帧
                                           ↓
                              Ready/Running 状态轮转
                                           ↓
                              CR3 + TSS.RSP0 切换
                                           ↓
                         返回新 PCB 的帧地址 → IRETQ
```

页表所有权不是“复制整个地址空间”。内核 PML4 作为模板，每个进程只复制
PML4[0] 指向的 PDPT，保留其中 supervisor 内核子树并清空 PDPT[1]。
用户程序固定在 `0x40000000..0x7fffffff`，因此每个进程第一次映射时都会
分配独立 PD/PT/叶页；高端用户栈位于 PML4[255]，也完全归进程所有。高半区
堆、LAPIC MMIO 和低端内核映射仍共享，但沿途至少一层 U/S=0，CPL3 不可达。

```text
kernel PML4 template
├─ [0]   kernel low PDPT ── supervisor mappings
├─ [256] high-half heap  ── supervisor mappings
└─ [...]

process PML4
├─ [0] cloned PDPT
│  ├─ [0] shared supervisor kernel subtree
│  └─ [1] owned user program subtree
└─ [255] owned user stack subtree
```

四个 PCB 使用固定数组，避免在调度热路径依赖尚不能释放的早期堆。每个 PCB
关联独立 16 KiB Ring 0 栈，底部一页不映射。用户态 IRQ 到来时 CPU 从当前
TSS.RSP0 选取该进程的栈；切换前若不更新 RSP0，下一个进程的系统调用会覆盖
旧进程保存的现场。

退出和异常采用同一资源顺序：记录终止原因，选择后继，切回内核 CR3，释放
用户叶页和独占页表，再激活后继。最后一个进程结束时，汇编恢复
`ExecuteProcesses` 启动前保存的内核调用链。正常 QEMU 路径比较进程创建前后
页帧统计，避免把“状态变成 Terminated”误当作资源已回收。详细决策见
[ADR 0012](adr/0012-preemptive-process-scheduling.md)。

## v0.10 同步、阻塞/唤醒与有界管道

v0.10 在既有四槽调度器中加入 `Blocked`，并为每个阻塞 PCB 保存具名
`ProcessWaitReason`。阻塞不是忙等：当前进程保存系统调用现场后离开运行集合，
调度器立刻派发另一个 Ready 进程；条件变化时，唤醒方把匹配原因的 PCB 移回
Ready。单核 interrupt gate 已清 IF，所以“检查条件—标记阻塞—选择后继”
构成当前实现的原子窗口；面向未来 SMP 的共享管道状态仍由自旋锁保护。

```text
用户 ReadPipe / WritePipe
        ↓
TryRead / TryWrite（只尝试，不隐式切换）
   ├─ 成功/EOF/错误 → 返回用户态
   └─ WouldBlock
          ↓
WaitPipeReadable / WaitPipeWritable
          ↓ 保存当前 176 B 帧
Running → Blocked(wait reason) → 派发另一个 Ready
          ↑
对侧读/写/关闭 → 条件变化 → WakeBlockedProcesses
```

“尝试”和“等待”使用不同系统调用编号。若在一次调用中先发现资源不足、再保存
现场，唤醒后重放同一入口容易重复副作用或错误修改 RIP；分离后等待调用只负责
调度，返回成功表示“现在应重新尝试”，用户包装始终以循环重新检查条件。

内核管道是 64 字节环形缓冲，保存读索引、写索引和当前字节数。读写允许部分
传输，每次用户复制最多 64 字节：

- 缓冲为空且写端仍开：读返回 `WouldBlock`；
- 缓冲为空且写端关闭：读返回 EOF（零字节）；
- 缓冲已满且读端仍开：写返回 `WouldBlock`；
- 读端关闭：写返回 `BrokenPipe`；
- 任一读操作释放空间后唤醒写等待者，任一写操作提交数据后唤醒读等待者；
- 关闭端点也必须唤醒对侧，因为 EOF 或 broken pipe 已让条件发生变化。

本阶段只有一个启动期管道对象、一个生产者和一个消费者，管道尚未进入通用
文件描述符表。
PID1 只持写端，PID2 只持读端；PID3/PID4 继续承担抢占和地址空间隔离验收。
生产者生成 256 字节确定性序列，消费者用 31 字节用户缓冲分批读取并逐字节
验证，保证环形回绕、部分读写、满/空阻塞和 EOF 均实际发生。异常终止路径会
自动关闭当前进程持有的端点并唤醒对侧，避免永久睡眠。详细取舍见
[ADR 0013](adr/0013-blocking-wakeup-and-bounded-pipe.md)。

## v0.11 固定布局文件系统与持久化边界

启动磁盘扩为 2 MiB，并把所有者边界写成半开 LBA 区间：

```text
[0, 2048)       启动描述符、Stage 1、Kernel 描述符与 Kernel ELF
[2048, 3072)    1024 个文件系统逻辑块
[3072, 4096)    保留
```

镜像打包器在宿主侧证明 Kernel 文件不会越过 LBA 2048；内核文件系统只允许
访问自己的 1024 个块。这样启动链增长不会静默覆盖 superblock，文件系统分配
也不能侵入启动负载。块层依赖保持单向：

```text
每进程 fd 表
  → FileSystem：路径、inode、目录、事务与一致性
    → BlockCache：八项 LRU、dirty 写回与统计
      → FileSystemBlockDevice：512 字节 read/write/flush 契约
        → AtaPioDevice：LBA28、WRITE SECTORS、FLUSH CACHE
```

磁盘格式不使用 packed C++ 结构体。superblock、inode 与目录项全部按固定
偏移显式小端编码，superblock 和每个已分配 inode 独立计算 CRC32。bitmap
描述局部所有权；挂载时再从根目录执行最多 32 inode 的有界广度优先遍历，
联合证明所有 inode 可达、所有数据块唯一且没有孤儿、环或重复引用。

修改操作采用 Dirty/Clean 最小提交协议。预检成功后先把 Dirty superblock
写入并 flush，再写回数据与元数据，最后写 Clean superblock 并再次 flush。
这是一种“故障可检测”协议，不是可重放日志：下次挂载看到 Dirty 必须拒绝。
只有 superblock 整扇区全零才允许首次格式化，任何非零未知内容或 CRC 错误
都保留证据并停止。

系统调用层为每个 PCB 保存四个普通文件句柄槽，fd 只在当前进程内有效。用户
路径和文件数据先经过有界用户页验证与内核缓冲，不把用户地址传入文件系统锁
内。管道暂时仍使用专用 ABI；把管道、文件与设备统一成 open-file-description
留给 v1.0。格式、事务与测试取舍见
[ADR 0014](adr/0014-transactional-educational-file-system.md)，详细实现见
[File System 模块](modules/file-system.md)。

## v1.0 统一描述符、控制台与用户 Shell

v1.0 把进程资源入口收束为八槽描述符表：

```text
PCB descriptor[0..7]
  ├─ 0 ConsoleInput  ← 256 B FIFO ← IRQ1 / Set 1
  ├─ 1 ConsoleOutput → COM1
  ├─ 2 ConsoleError  → COM1
  └─ 3..7
      ├─ RegularFile / Directory → FileSystemHandle[fd]
      └─ PipeReader / PipeWriter → bootstrap Pipe
```

用户通用 I/O 先 Try；WouldBlock 后用同一 fd 执行 Wait，唤醒后重新 Try。
内核先验证完整用户区间，再用最多 256 字节的内核缓冲访问对象。目录不伪装成
字节流，而由 OpenDirectory/ReadDirectory 返回固定 64 字节类型化目录项。
关闭统一经过描述符表，进程退出和异常不再分别遗漏文件或管道资源。

交互输入链为：

```text
QEMU sendkey
  → i8042 scan code
  → IRQ1 / IDT / NASM 公共入口
  → ScanCodeSet1Decoder
  → ConsoleInput::Submit
  → Wake DescriptorReadable
  → Shell 保存帧从 Blocked 变 Ready
  → CR3 + TSS.RSP0 + IRETQ
  → fd 0 返回字符
```

如果 Shell 阻塞时没有其他 Ready，调度器不会把它误判为全部完成。运行时激活
永久内核页表和默认特权栈，回到执行循环完成相邻的 `sti; hlt; cli`；任意可屏蔽中断都会
让 CPU 返回，只有条件变化产生 Ready 后才重新进入用户态。该路径使键盘等待
不忙等，也不需要创建一个伪造的用户 idle 进程。

Shell 自身是独立 Ring 3 ELF，解析和命令实现全部位于 `source/user`。它只
使用固定容量数组和公开 ABI，提供 help、echo、pwd、ls、mkdir、write、
cat、sync 与 exit。正常启动还保留一个生产者、消费者和 worker，从而让
统一描述符变更同时回归管道、持久文件和抢占地址空间。详细决策见
[ADR 0015](adr/0015-unified-descriptors-interactive-shell-and-idle.md)，代码
边界见[用户环境模块](modules/user-environment.md)。

## v1.1 动态物理内存与高半区直映

v1.1 的第一个增量把“启动可访问范围”和“正式可管理 RAM”拆开。Stage 1
仍只建立低 64 MiB 身份映射，因为模式切换、Kernel 装载和 BootInfo 交接不
需要为整机 RAM 预建页表；内核接管后再根据硬件事实建立正式地址空间：

```text
CPUID.80000008H:EAX[7:0] ──> 处理器物理地址上限
etc/e820 type 1 RAM ─────────> 最高可用完整页
四级 direct-map 窗口 ───────> 64 TiB 实现上限
                 │
                 └─ 取最小值并向下页对齐
                         ↓
       在低 64 MiB 可用 RAM 中选择页帧状态存储
                         ↓
   2-bit frame allocator：unavailable/free/allocated/reserved
                         ↓
      新 CR3：低端启动映射 + 高半区 RAM direct-map
                         ↓
   CR3 切换后，页表页、用户页和 ELF 复制统一经 direct-map
```

管理上限使用 E820 中“最高可用 RAM 页末端”，而不是所有条目的最高末端。
例如自研固件可能描述一个延伸到 1 TiB 的保留设备窗口；该窗口必须保持
不可分配，却不能迫使 64 MiB 客户机为 1 TiB PFN 分配状态表。状态表按物理
页号覆盖 RAM 洞，因此 64 GiB QEMU 配置在 3--4 GiB 洞存在时需要
`0x410000` 字节，而不是简单的 `0x400000` 字节。

正式高半区布局新增：

| 虚拟范围 | 映射 | 权限与用途 |
| --- | --- | --- |
| `0xFFFF888000000000 + P` | E820 type 1 物理地址 `P` | supervisor RW/NX，普通缓存 |
| `0xFFFF888000000000..+64TiB` | 保留窗口 | 未声明 RAM 的洞保持 not-present |
| `0xFFFF800000000000..+64KiB` | 离散页帧 | 可回收内核堆，RW/NX |

每个对齐且完整的 RAM 内部区间优先用 2 MiB PDE 大页映射；E820 边界与尾部用
4 KiB PTE。LAPIC 等 MMIO 不进入普通 RAM 直映，继续由显式 PCD 映射负责。
CR3 和页表项始终保存物理地址；C++ 只在需要解引用时通过统一转换得到高半区
虚拟地址。新 CR3 激活前，页表帧分配被限制在 Stage 1 身份映射内；激活后才
允许从全部受管 RAM 分配页表页。

正式系统测试固定使用 64 GiB QEMU RAM，要求 64 GiB 可用 RAM 全部受管、
direct-map 使用 32768 个 2 MiB 页，并在 `0x0000000100001000` 或更高地址
完成两组 64 位模式的写入、读回和页帧回收。64 MiB 配置保留为最小兼容回归，
用于证明高内存自检可以有条件跳过，而通用初始化不会退回固定容量。设计与
取舍见 [ADR 0017](adr/0017-linux-style-physical-memory-and-direct-map.md)。

## v1.1 可回收内核堆

物理直映解决“任意普通 RAM 如何被内核访问”，通用内核堆解决“页之上的
不同尺寸对象如何拥有生命周期”。两者不能合并：页帧分配器管理物理所有权，
`KernelHeap` 管理已经映射为连续虚拟区间的字节块。

```text
64 KiB RW/NX 虚拟区间
  └─ 物理块链：size + previous-size 边界标记
       ├─ allocated：块头 + 调用者负载
       └─ free ───────────────┐
                              ↓
             地址递增双向空闲链
                    │
                    ├─ best-fit + 对齐分裂
                    └─ release + 前后合并
```

分配只在候选布局全部通过后提交，失败不改变调用者输出和旧拓扑。释放先验证
精确负载首地址、活动签名、物理块成员与相邻边界，再从空闲链摘除相邻块并
形成唯一合并结果。完整校验器把物理块集合与空闲链集合交叉核对，而不是只看
局部指针。

启动自检完成两次不同对齐的真实写回后逆序释放，要求活动对象和当前占用归零，
累计分配等于累计释放；日志只报告容量、峰值和最大连续空闲负载。当前分配器
由串行启动路径调用，尚不承诺 IRQ/NMI/panic 或多 Thread 并发分配。详细
布局、状态和取舍见
[ADR 0020](adr/0020-reclaimable-kernel-heap.md)。

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
user ────────────────→ abi
kernel ──────────────→ abi
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
│   ├── console_input.hpp
│   ├── device_model.hpp
│   ├── descriptor_layout.hpp
│   ├── descriptor_tables.hpp
│   ├── entry.hpp
│   ├── exception_frame.hpp
│   ├── exceptions.hpp
│   ├── interrupt_runtime.hpp
│   ├── io_descriptor.hpp
│   ├── kernel_heap.hpp
│   ├── kernel_main.hpp
│   ├── memory_manager.hpp
│   ├── legacy_pic.hpp
│   ├── page_table.hpp
│   ├── pipe.hpp
│   ├── panic.hpp
│   ├── physical_frame_allocator.hpp
│   ├── physical_memory_map.hpp
│   ├── port_io.hpp
│   ├── processor.hpp
│   ├── process_runtime.hpp
│   ├── process_scheduler.hpp
│   ├── programmable_interval_timer.hpp
│   ├── ps2_keyboard.hpp
│   ├── serial_port.hpp
│   └── spin_lock.hpp
├── linker/
│   └── kernel.ld.in
└── src/
    ├── architecture.asm
    ├── ata_pio.cpp
    ├── console_input.cpp
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
    ├── io_descriptor.cpp
    ├── legacy_pic.cpp
    ├── memory_manager.cpp
    ├── page_table.cpp
    ├── page_table_layout.cpp
    ├── pipe.cpp
    ├── panic.cpp
    ├── physical_frame_allocator.cpp
    ├── physical_memory_map.cpp
    ├── port_io.cpp
    ├── processor.cpp
    ├── process_runtime.cpp
    ├── process_scheduler.cpp
    ├── programmable_interval_timer.cpp
    ├── ps2_keyboard.cpp
    ├── serial_port.cpp
    └── spin_lock.cpp

source/abi/
├── CMakeLists.txt
└── include/os/abi/
    └── system_call.hpp

source/user/
├── CMakeLists.txt
├── include/os/user/
│   ├── freestanding_memory.hpp
│   ├── shell.hpp
│   ├── shell_parser.hpp
│   └── system_call.hpp
├── linker/
│   └── user.ld.in
├── programs/
│   ├── smoke.cpp
│   ├── invalid_opcode.cpp
│   ├── page_fault.cpp
│   ├── scheduler_worker.cpp
│   ├── ipc_producer.cpp
│   ├── ipc_consumer.cpp
│   └── shell_entry.cpp
└── src/
    ├── freestanding_memory.cpp
    ├── shell.cpp
    ├── shell_parser.cpp
    ├── system_call.asm
    └── system_call.cpp
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

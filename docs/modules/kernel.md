# Kernel 模块

## 职责

`kernel` 是 Stage 1 最终交接的 freestanding C++20 ELF64 可执行文件。v0.7
已经在真实交接之上建立内核自己的处理器、内存、中断与设备基础：

- 由 Clang 以 `x86_64-unknown-none-elf` 目标编译。
- 由 LLD 的 `elf_x86_64` 模式直接链接，不经过 ARM64 宿主 GCC。
- 入口符号为 C ABI 的 `OsKernelEntry`，链接地址为 `0x00100000`。
- 不链接 libc、C++ 标准库、异常、RTTI、栈保护或宿主运行时。
- 入口按 System V AMD64 ABI 从 RDI 接收 BootInfo。
- 内核独立初始化 COM1，不依赖 Stage 1 函数或隐藏状态。
- 入口验证 BootInfo、BSS 清零结果和当前 CR3，再接管 GDT、TSS 和 IDT。
- 32 个架构异常由 NASM 桩规范化为固定 C++ `ExceptionFrame`。
- 可恢复 breakpoint 经 `IRETQ` 返回，其他异常输出固定现场并 panic。
- 验证 BootInfo v2 的物理内存图，初始化 2-bit 页帧分配器。
- 建立并激活内核自己的四级 4 KiB 页表，执行 W^X、NX、WP 和 guard page。
- 映射并自检 64 KiB 高半区可回收内核堆。
- 映射 LAPIC MMIO 并建立 LINT0 ExtINT virtual-wire，接管 8259A、8254、
  i8042 和 ATA PIO。
- 向量 32..47 使用独立硬件 IRQ 汇编入口，设备处理后严格执行 PIC EOI。
- 验收完成后进入 IF 开启的 `HLT` 事件循环，不返回 Stage 1。

## 文件布局

链接脚本使用独立的 `PT_LOAD` 权限设计：代码为 `R E`，只读数据为 `R`，
可写数据与 BSS 为 `RW`。空输出节不会产生多余加载段。各加载节以 4 KiB
边界对齐，入口必须位于可执行加载段。

## 审计不变量

宿主工具拒绝以下内核：

- ELF 头截断、magic、类别、端序、版本、类型或目标机器错误。
- 程序头表越界或程序头宽度不匹配。
- 没有 `PT_LOAD`、文件长度大于内存长度或文件区间越界。
- 段没有按 4 KiB 对齐，或文件偏移与虚拟地址的页内偏移不一致。
- 目标地址不是初期恒等装载、地址范围溢出或加载段相互重叠。
- 段缺少读取权限、包含未知权限位，或同时可写与可执行。
- 入口不是 `0x00100000`，或不在可执行加载段中。
- 存在未解析运行时符号。
- 缺少描述符装载入口、异常/硬件 IRQ 公共入口、对应 C++ 分发器、桩表、
  链接器权限边界、任意 `os_kernel_exception_vector_0..31` 或
  `os_kernel_hardware_interrupt_vector_32..47` 符号。

宿主 ELF 审计和 Stage 1 目标机加载器各自实现这些不变量，避免构建工具通过
就被误认为目标代码也正确处理了不可信磁盘数据。

## BootInfo ABI

BootInfo 位于物理地址 `0x14000`，共 104 字节；每个字段都是明确的 64 位
小端整数：

| 偏移 | 字段 | 当前值或语义 |
| ---: | --- | --- |
| `0x00` | magic | `OSBOOT64` |
| `0x08` | version | `2` |
| `0x10` | structure size | `104` |
| `0x18` | Kernel 文件物理地址 | `0x03E00000` |
| `0x20` | Kernel 精确文件大小 | 来自已校验描述符 |
| `0x28` | Kernel 入口 | `0x100000` |
| `0x30` | `PT_LOAD` 数量 | `1..64` |
| `0x38` | 页表根物理地址 | `0x10000` |
| `0x40` | 恒等映射大小 | 64 MiB |
| `0x48` | 内核栈顶 | `0x3FFF000` |
| `0x50` | 物理内存图地址 | `0x18000` |
| `0x58` | 物理内存图条目数 | `1..128` |
| `0x60` | 物理内存图条目宽度 | `24` |

结构体用 `static_assert` 固定为 104 字节；内核不会因为拿到了非空指针就信任
内容，而是逐字段验证版本、范围和当前启动契约。

## 描述符表契约

### GDT

| 索引 | 选择子 | 内容 | 关键属性 |
| ---: | ---: | --- | --- |
| 0 | `0x00` | 空描述符 | 必须全零 |
| 1 | `0x08` | Ring 0 代码段 | present、execute/read、L=1、DPL=0 |
| 2 | `0x10` | Ring 0 数据段 | present、read/write、DPL=0 |
| 3 | `0x1B` | Ring 3 数据段 | present、read/write、DPL=3 |
| 4 | `0x23` | Ring 3 代码段 | present、execute/read、L=1、DPL=3 |
| 5..6 | `0x28` | 64 位 TSS | available TSS，完整 64 位基址 |

`LGDT` 不会自动刷新 CS 的隐藏属性，因此装载后使用远返回重新载入代码段，
再写 DS、ES、FS、GS、SS。`LTR` 会把 available TSS 描述符硬件类型改为
busy；运行时验证选择子和 TSS 内容，不错误要求内存仍保持初始 type。

### TSS

TSS 精确为 104 字节，I/O bitmap offset 等于结构末端，使当前没有实际 I/O
权限位图。RSP0 指向独立 16 KiB 特权转换栈，不能复用等待用户程序返回的
启动栈；该栈底部有单独 4 KiB guard。IST1、IST2、IST3 指向三个独立
16 KiB BSS 栈，分别服务双重故障、NMI、机器检查；每个存储块下方另有一个
4 KiB guard page，其余 RSP/IST 保持零。

### IDT

IDT 分配 256 个 16 字节门。向量 0..31 是架构异常，32..47 是传统 PIC
硬件 IRQ，均为 present interrupt gate；`0x80` 是系统调用 interrupt gate；
其余保持 not-present。双重故障、NMI、机器检查分别选择 IST1、IST2、IST3。
breakpoint、overflow 和系统调用门的 DPL 为 3，允许 Ring 3 显式触发；
其余门 DPL 为 0。

加载后必须同时满足：

- `SGDT` 的 base/limit 指向当前 GDT。
- `SIDT` 的 base/limit 指向完整 4096 字节 IDT。
- CS=`0x08`，SS=`0x10`，`STR`=`0x28`。
- TSS.RSP0、三个 IST 和 I/O bitmap offset 与构造值一致。

## v0.6 内存子系统契约

### 物理内存图

每个 24 字节条目由 `uint64_t base_address`、`uint64_t length_bytes`、
`uint32_t type`、`uint32_t attributes` 组成，结构大小由 `static_assert`
固定。内核只把 type 1 视为可用 RAM；未知类型保持不可分配。验证必须满足：

- 指针非空，条目数在 `1..128`，管理上限非零。
- 每项长度非零，`base + length` 不发生 64 位溢出。
- 条目按 base 单调排列，半开区间互不重叠。
- 总描述字节和可用字节求和不溢出。
- 由 E820 最高可用完整页、`CPUID.80000008H` 物理地址宽度和 64 TiB
  direct-map 容量共同确定的管理范围内至少存在一个可用页。

验证函数采用候选汇总，只有全部条目成功后才覆盖调用者输出，失败不会交付部分
统计。可用区域按 4 KiB 边界向内收缩，未完整覆盖的页不会错误变成可分配帧。

### 页帧所有权

每个受管 4 KiB 帧使用 2 bit 状态，帧数由运行期物理地址上界决定：

| 状态 | 含义 | 允许操作 |
| --- | --- | --- |
| unavailable | 非可用 RAM 或不完整页 | 永不分配 |
| free | 已验证可用且无人持有 | 可分配或保留 |
| allocated | 动态所有者持有 | 只允许对应释放 |
| reserved | 平台或启动关键对象 | 不允许普通释放 |

状态数组大小为 `ceil(frame_count / 4)` 字节，再向上取整到完整页。内核先在
Stage 1 的低 64 MiB 身份映射中搜索连续可用区，跳过低 1 MiB、链接器符号
界定的 Kernel 映像和 64 KiB 初始栈；配置并初始化分配器后，再把状态数组
自身标为 reserved。64 GiB QEMU 机器含 3--4 GiB 物理洞，受管上界为 65 GiB，
因此实际状态存储是 `0x410000` 字节。`ReserveRange` 先预检完整范围，发现
任意 allocated 帧就整体失败，避免只保留前半段。`AllocateInRange` 允许
启动期页表限制在身份映射内，也允许高内存自检明确要求 4 GiB 以上页帧。

### 页表与权限

`PageTableManager` 从分配器取得页表帧并清零，按虚拟地址的
`47:39`、`38:30`、`29:21`、`20:12` 提取四级索引。中间项只保存
present、writable、user 和物理地址；叶项另外编码 NX。映射拒绝非 canonical
地址、未对齐地址、物理地址越过处理器/页表地址字段和重复映射。普通映射使用
4 KiB PTE；物理直映的对齐内部区间允许使用带 PS 位的 2 MiB PDE，查询接口
返回命中页大小和包含页内偏移的物理地址。

内核先在 Stage 1 的旧 CR3 下建立全部新映射，确认 CPU 支持 NX 后设置
`IA32_EFER.NXE`，再设置 `CR0.WP` 并加载新 CR3。当前布局：

| 虚拟范围 | 权限 | 说明 |
| --- | --- | --- |
| `0x0` | not-present | 捕获空指针 |
| Kernel `.text` | RX | 可执行且不可写 |
| Kernel `.rodata` | R/NX | 常量不可写、不可执行 |
| Kernel `.data/.bss` | RW/NX | 状态和栈不可执行 |
| 初始栈、三个 IST 栈及特权转换栈底页 | not-present | 溢出 guard |
| `0xFFFF800000000000..+64KiB` | RW/NX | 高半区早期堆 |
| `0xFFFF800000100000` | R/NX | Ring 0 写保护故障验收页 |
| `0xFFFF888000000000 + P` | RW/NX | E820 type 1 RAM 的 64 TiB direct-map |

新 CR3 激活前，页表帧只能从低 64 MiB 身份映射取得，并用物理地址直接访问；
激活后 `PageTableMemoryAccess` 切换到 direct-map，随后新页表页可以来自全部
受管 RAM。用户页清零与 ELF 内容复制使用同一物理到虚拟转换接口。页表激活后
查询上述映射并确认所有 guard 仍为 not-present。每次活动叶项修改执行
`INVLPG`；切换 CR3 刷新当前地址空间的普通 TLB 项。当前单核启动阶段不需要
TLB shootdown。

### 可回收内核堆

v0.6 最初用单调分配器证明高半区映射之上能够放置对象。v1.1 保留同一
`0xFFFF800000000000..+64KiB` RW/NX 区间，把实现升级为边界标记与地址
有序空闲链表。每块包含自身长度、前块长度、请求长度、状态签名和双向空闲
链接；分配使用 best-fit，对齐前缀和剩余后缀只有达到最小块尺寸才独立拆分。

`TryRelease` 只接受活动负载的精确首地址，预检前后块后执行双向合并。空指针、
区间外/内部指针、重复释放和损坏元数据拥有独立状态。`Validate` 同时核对
物理块无缝覆盖、边界标记、空闲链排序/反链/无环、空闲集合唯一性，以及活动、
累计、峰值和最大连续空闲负载统计。分配失败不修改输出指针和堆拓扑。

目标机仍做 16 字节和 4 KiB 对齐分配，写入并读回两个 64 位模式；随后逆序
释放、合并并确认活动数与当前占用均为零。只有完整生命周期通过才输出
`HEAP_SELF_TEST_PASSED`。实现取舍见
[ADR 0020](../adr/0020-reclaimable-kernel-heap.md)。

## 异常 ABI

处理器对向量 8、10、11、12、13、14、17、21、29、30 自动压入错误码；
其他向量不压。每个无错误码桩先压入 64 位零，随后所有桩压入向量号。公共入口
清 DF 并依次保存通用寄存器，形成 160 字节结构：

```text
低地址
R15 R14 R13 R12 R11 R10 R9 R8
RDI RSI RBP RDX RCX RBX RAX
vector error_code RIP CS RFLAGS
高地址
```

没有发生特权级切换时，硬件不会额外保存旧 RSP/SS；当前结构只声明所有
Ring 0 异常稳定拥有的字段。公共入口在调用 C++ 前向下对齐 RSP，并单独保存
原异常帧指针，返回后严格逆序恢复并执行 `IRETQ`。

## panic 契约

只有向量 3 且规范化错误码为零可以返回。其他异常：

1. 立即 `CLI`。
2. 用 BSS 状态位拒绝递归进入，避免重复日志。
3. 重新初始化 COM1，绕过可能不完整的上层日志状态。
4. 固定输出向量、错误码、RIP、CS、RFLAGS；页故障增加 CR2。
5. 进入 `CLI; HLT` 循环，绝不返回异常入口。

panic 不使用动态分配、格式化库、锁、异常、RTTI 或可失败的构造链。当前只输出
最小充分证据，不在未知栈健康状态下尝试回溯。

## v0.7 硬件 IRQ 与设备契约

`InterruptRuntime` 在 IF 关闭时完成全部设备配置。内存管理器已把
`IA32_APIC_BASE` 指定页映射为 RW/NX/PCD；运行时保持 LAPIC 全局启用，
启用 SVR，并把 LVT LINT0 配为未屏蔽的 ExtINT，回读后再重映射 8259A。
PIC 初始屏蔽所有 IRQ，只有 PIT、PS/2 和 ATA 自检全部成功后，才把掩码改为
`0xFFFC` 并开放 IRQ0/IRQ1。

硬件 IRQ 桩统一压入零错误码和向量号，保存集合与异常 ABI 相同。分发器把
向量 32..47 还原为 IRQ0..15：

- IRQ0：增加 64 位 tick，不执行串口 I/O。
- IRQ1：读取 i8042 数据端口，推进扫描码集合 1 解码器，保留一个待消费事件。
- IRQ7/IRQ15：读取 PIC ISR 判定是否虚假，并遵守各自 EOI 规则。
- 其余 IRQ：当前保持屏蔽；若错误进入仍会完成合法 PIC 确认，但没有设备动作。

PIT 单调毫秒按 `tick × divisor × 1000 / 1193182` 计算，并在乘法前检查
64 位溢出。统计快照和事件交接保存原 IF、执行 `CLI`、复制状态后按原值恢复，
不把“函数返回时总是 STI”误作同步策略。

PS/2 初始化关闭两个端口、清空有界输出、读取控制器配置，打开 IRQ1 与翻译、
关闭 IRQ12，再启用第一端口并向键盘发送扫描使能 `0xF4`；只有收到
`0xFA` ACK 才成功。ATA 自检向 device control 写 `nIEN`，以 LBA28 读取
512 字节的 LBA 0，并检查前八字节 `OSSTAGE1`。

详细端口、位和测试边界见 [设备模块](devices.md)。

## v0.8 用户 ELF、用户内存与系统调用契约

### 解析与装载

`ValidateUserElf()` 是不接触硬件的纯解析层。它先把候选结果写入局部
`UserElfLayout`，全部程序头成功后才交付输出，因此格式失败不会留下半份布局。
它拒绝未知程序头，不把 section header 或调试信息当成装载依据。加载器随后：

1. 检查 ELF 段与用户栈/guard 不相交。
2. 按段页数分配物理帧并建立 U/S 映射。
3. 清零整页，再复制 `p_filesz`，使 BSS 区自然为零。
4. 映射四页 RW/NX 用户栈。
5. 任一步失败都逆序解除映射并释放本轮页帧。

用户页接口拒绝非对齐、低地址、高半地址、W+X 权限和非用户映射释放。
`CopyFromUser()` 先验证完整半开区间，再逐页查询 present/U/S，最后从当前
CR3 中的用户虚拟地址复制。v0.9 仍是单核且 `INT 0x80` interrupt gate
在内核入口清 IF，没有另一个线程能并发解除当前地址空间；加入内核抢占或
多线程后必须引入页固定、地址空间锁或可恢复的用户复制原语。

### 进入、系统调用与返回

`OsKernelEnterScheduledProcess` 保存调度启动前的内核 RSP、RFLAGS 和
非易失寄存器，再从首个 PCB 的完整现场执行 `IRETQ`。`INT 0x80` 让 CPU 自动从
TSS.RSP0 取安全内核栈，系统调用公共入口复用统一寄存器帧。

分发前必须满足当前存在活跃进程、帧来自 CPL3、向量为 `0x80`、
CS=`0x23`、SS=`0x1B`、RIP 所在叶页为 user RX、RSP 位于四页用户栈，
而且栈叶页是 user RW/NX。普通系统调用按原帧 `IRETQ`；exit 和用户异常
终止当前 PCB 并交接到下一个 Ready 进程，只有最后一个进程结束时才用
`OsKernelReturnFromUserMode` 恢复调度启动前的内核调用链。

`WriteLog` 最多复制 160 字节到固定内核缓冲后再访问串口。用户日志带
`[OS][USER]` 只是协议来源标记，不获得内核可信度。ABI 详见
[User 与 ABI 模块](user.md)。

## v0.9 进程运行时契约

`ProcessScheduler` 是可在宿主执行的纯模型，只保存状态、PID、tick、派发和
抢占统计。`ProcessRuntime` 拥有固定四槽 PCB，把每个槽位关联到独立
`UserAddressSpace`、保存帧、终止结果和 Ring 0 栈。IRQ、系统调用和异常
分发器都返回“下一份要恢复的帧地址”，汇编不内置调度策略。

地址空间根以当前内核 PML4 为模板。创建时克隆低端 PDPT，清空用户程序所在
PDPT[1]，并让用户栈从空的 PML4[255] 开始建立。销毁时只递归遍历这两个
独占子树，绝不释放共享内核页表。`DestroyUserPageTable` 拒绝销毁当前 CR3
或内核根，迫使终止路径先切回安全根。

进程栈存储按“4 KiB guard + 16 KiB 可用栈”连续排列。内存管理器构建低端
身份映射时跳过每块 guard；PCB 只接受落在自己可用范围内的 176 字节帧。
每次进程切换同时更新 CR3 和 TSS.RSP0，两者任一失败都停止系统，不能带着
不一致的页表/栈所有权继续执行。

IRQ0 先由设备运行时更新 tick 并向 PIC EOI，随后调度器计算预算。中断热路径
不分配、不释放、不写串口；用户页和页表只在进程退出/异常路径切回内核 CR3 后
释放。全部结束后一次性输出调度汇总与每进程结果，并比较物理页帧统计。

## v0.10 同步与 IPC 运行时契约

`SpinLock` 使用编译器原子内建完成 exchange-acquire、store-release 和
relaxed 观察；忙等内层执行 x86 `PAUSE`，降低共享执行资源上的无效竞争。
`SpinLockGuard` 以 RAII 限定锁生命周期。该锁只保护不能睡眠的短临界区；
锁内禁止系统调用阻塞、串口输出和用户内存复制。

`ProcessScheduler` 新增 `Blocked` 与
`PipeReadable/PipeWritable` 等待原因。`BlockCurrentProcess` 只允许
Running 进程在存在后继 Ready 进程时阻塞；`WakeBlockedProcesses` 只把
原因匹配的槽位转回 Ready，并分别累计 block/wakeup。若不存在 Ready 但仍有
Blocked，运行时报告 `NoReadyProcess`，不把死锁误报为正常完成。

`Pipe` 使用固定 64 字节环形数组，不依赖早期堆。所有索引和统计使用
`uint64_t`；数据元素使用 `uint8_t`。读写在锁内提交索引、计数和字节统计，
用户地址验证与 `CopyFromUser/CopyToUser` 在锁外完成。系统调用先把数据复制
到最多 64 字节的内核临时缓冲，再操作管道，避免持锁访问不可信页。

ABI 编号 4--9 分别为 `TryReadPipe`、`TryWritePipe`、
`WaitPipeReadable`、`WaitPipeWritable`、`ClosePipeReader` 和
`ClosePipeWriter`。返回值区分 `WouldBlock`、`BrokenPipe`、端点权限、
重复关闭、非法参数、用户内存和传输过长。等待调用被唤醒后只返回“允许重试”，
不会假装已经完成原读写；高层用户包装必须循环执行 Try。

当前只有一个 bootstrap pipe。PID1 的生产者独占写端，PID2 的消费者独占
读端；这是显式阶段边界，不是通用文件描述符接口。进程正常/异常终止时，
`ProcessRuntime` 自动关闭仍归其所有的端点并唤醒对侧，随后才回收地址空间。
冷路径最终核对 256 字节写入/读取、空缓冲、端点关闭、阻塞/唤醒守恒与 EOF。

## 入口验收序列

成功启动必须依次输出：

```text
[OS][KERNEL] ENTERED
[OS][KERNEL] BOOT_INFO_VALID
[OS][KERNEL] BSS_ZEROED
[OS][KERNEL] CR3_VALID
[OS][KERNEL] GDT_READY
[OS][KERNEL] TSS_READY
[OS][KERNEL] IDT_READY
[OS][KERNEL] DESCRIPTOR_TABLES_VALID
[OS][KERNEL] BREAKPOINT_HANDLED
[OS][KERNEL] EXCEPTION_SELF_TEST_READY
[OS][KERNEL] MEMORY_MAP_VALID
[OS][KERNEL] MEMORY_MAP_ENTRIES=0x...
[OS][KERNEL] MEMORY_DESCRIBED_BYTES=0x...
[OS][KERNEL] MEMORY_USABLE_BYTES=0x...
[OS][KERNEL] MEMORY_MANAGED_BYTES=0x...
[OS][KERNEL] FRAME_ALLOCATOR_READY
[OS][KERNEL] FREE_FRAMES=0x...
[OS][KERNEL] ALLOCATED_FRAMES=0x...
[OS][KERNEL] RESERVED_FRAMES=0x...
[OS][KERNEL] PAGING_READY
[OS][KERNEL] PAGING_ROOT=0x...
[OS][KERNEL] MEMORY_PERMISSIONS_VALID
[OS][KERNEL] HEAP_READY
[OS][KERNEL] HEAP_CAPACITY_BYTES=0x...
[OS][KERNEL] HEAP_ACTIVE_ALLOCATIONS=0x0000000000000000
[OS][KERNEL] HEAP_PEAK_CONSUMED_BYTES=0x...
[OS][KERNEL] HEAP_LARGEST_FREE_ALLOCATION_BYTES=0x...
[OS][KERNEL] HEAP_SELF_TEST_PASSED
[OS][KERNEL] PROCESS_RUNTIME_READY
[OS][KERNEL] PIPE_READY
[OS][KERNEL] USER_ELF_VALID
[OS][KERNEL] USER_ENTRY=0x0000000040000000
[OS][KERNEL] USER_MAPPED_PAGES=0x...
[OS][KERNEL] USER_STACK_READY
[OS][KERNEL] PROCESS_ID=0x...
[OS][KERNEL] PROCESS_CR3=0x...
[OS][KERNEL] LEGACY_INTERRUPT_ROUTING_READY
[OS][KERNEL] PIC_READY
[OS][KERNEL] PIC_MASK=0x...FFFC
[OS][KERNEL] PIT_READY
[OS][KERNEL] PIT_DIVISOR=0x...04A9
[OS][KERNEL] PIT_FREQUENCY_HZ=0x...03E8
[OS][KERNEL] PS2_KEYBOARD_READY
[OS][KERNEL] ATA_PIO_READY
[OS][KERNEL] ATA_BOOT_DESCRIPTOR_VALID
[OS][KERNEL] PIC_SPURIOUS_SELF_TEST_PASSED
[OS][KERNEL] INTERRUPTS_ENABLED
[OS][KERNEL] TIMER_TICKS=0x...
[OS][KERNEL] MONOTONIC_MILLISECONDS=0x...
[OS][KERNEL] TIMER_SELF_TEST_PASSED
[OS][KERNEL] USER_RING3_ENTER
[OS][KERNEL] SCHEDULER_STARTED
[OS][USER][PIPE] PRODUCER_STARTED
[OS][USER][PIPE] CONSUMER_STARTED
[OS][USER] UNKNOWN_SYSCALL_REJECTED
[OS][USER] HELLO_FROM_RING3
[OS][USER][PIPE] PRODUCER_COMPLETED
[OS][USER][PIPE] PAYLOAD_VERIFIED
[OS][USER][PIPE] EOF_OBSERVED
[OS][KERNEL] SCHEDULER_BLOCKS=0x...
[OS][KERNEL] SCHEDULER_WAKEUPS=0x...
[OS][KERNEL] PIPE_CAPACITY_BYTES=0x0000000000000040
[OS][KERNEL] PIPE_WRITTEN_BYTES=0x0000000000000100
[OS][KERNEL] PIPE_READ_BYTES=0x0000000000000100
[OS][KERNEL] PIPE_EOF_OBSERVATIONS=0x0000000000000001
[OS][KERNEL] USER_EXIT_CODE=0x0000000000000000
[OS][KERNEL] USER_SYSCALL_COUNT=0x0000000000000006
[OS][KERNEL] USER_TERMINATED
[OS][KERNEL] PIPE_TRANSFER_VALID
[OS][KERNEL] PIPE_ENDPOINTS_CLOSED
[OS][KERNEL] PROCESS_RESOURCES_RECLAIMED
[OS][KERNEL] SCHEDULER_COMPLETE
[OS][KERNEL] USER_RETURNED_TO_KERNEL
[OS][KERNEL] FILE_SIZE=0x...
[OS][KERNEL] LOAD_SEGMENTS=0x...
[OS][KERNEL] READY
[OS][KERNEL] KEYBOARD_SCANCODE=0x...001E
[OS][KERNEL] KEYBOARD_EVENT=A_PRESSED
```

未初始化的全局 64 位探针位于 BSS。只有加载器按 `p_memsz - p_filesz` 清零，
`BSS_ZEROED` 才能出现。CR3 读回值必须等于 BootInfo 中的页表根；这两项把
“段复制完成”和“处理器仍使用约定页表”变成目标机可观测证据。

故障镜像与生产内核共享所有实现，只替换 `OsKernelEntry` 选择的注入模式。
`UD2` 必须得到向量 6、错误码 0；访问 `0x04000000` 必须得到向量 14、
错误码 0 和同值 CR2。写 `0xFFFF800000100000` 必须得到向量 14、错误码
`0x3` 和同值 CR2，逐位表示 present 页上的 supervisor write 权限违反。
三者必须输出一次 `PANIC`，且禁止出现文件统计和 `READY`。

## 已知边界

- 当前仅使用单核 PIC，并让本地 APIC LINT0 承担 virtual-wire；LAPIC
  timer/IPI、I/O APIC、MSI/MSI-X 与 SMP 路由尚未实现。
- 键盘只保存一个待处理语义事件，ATA 仍是禁用设备 IRQ 的同步单扇区 PIO；
  环形队列、IRQ14、DMA 与通用块请求尚未实现。
- 当前 64 TiB direct-map 只支持四级页表，尚未启用 LA57；页帧状态仍按最高
  RAM PFN 线性编码，极端稀疏物理地址空间、NUMA 和分段 `vmemmap` 以后扩展。
- 内核堆已支持释放与合并，但后备区仍固定为 64 KiB，尚无 type cache、
  KVA 按需增长和内存压力回收；页表取消映射也不回收空中间表。
- panic 只支持单核早期环境；SMP 停核和崩溃转储尚未实现。
- Ring 0 页故障仍全部 panic；Ring 3 页故障只终止当前用户执行。按需映射和
  写时复制要等进程地址空间拥有完整生命周期后再实现。
- 当前是单核、固定四进程、单线程模型；没有阻塞、唤醒、优先级、父子关系、
  zombie/wait、FPU/SSE 状态保存或 SMP 负载均衡。
- 用户地址空间和通用内核堆均可回收；动态 KernelObject、引用计数与类型化
  对象缓存仍等待 v1.1 后续增量和 v1.4 对象模型。

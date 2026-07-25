# Kernel 模块

## 职责

`kernel` 是 Stage 1 最终交接的 freestanding C++20 ELF64 可执行文件。v0.7
已经在真实交接之上建立内核自己的处理器、内存、中断与设备基础：

- 由 Clang 以 `x86_64-unknown-none-elf` 目标编译。
- 由 LLD 的 `elf_x86_64` 模式直接链接，不经过 ARM64 宿主 GCC。
- 入口符号为 C ABI 的 `osKernelEntry`，链接地址为 `0x00100000`。
- 不链接 libc、C++ 标准库、异常、RTTI、栈保护或宿主运行时。
- 入口按 System V AMD64 ABI 从 RDI 接收 BootInfo。
- 内核独立初始化 COM1，不依赖 Stage 1 函数或隐藏状态。
- 入口验证 BootInfo、BSS 清零结果和当前 CR3，再接管 GDT、TSS 和 IDT。
- 32 个架构异常由 NASM 桩规范化为固定 C++ `ExceptionFrame`。
- 可恢复 breakpoint 经 `IRETQ` 返回，其他异常输出固定现场并 panic。
- 验证 BootInfo v2 的物理内存图，初始化 2-bit 页帧分配器。
- 建立并激活内核自己的四级 4 KiB 页表，执行 W^X、NX、WP 和 guard page。
- 映射并自检 64 KiB 高半区单调早期堆。
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
| `0x18` | Kernel 文件物理地址 | `0x20000` |
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

每个 24 字节条目由 `uint64_t baseAddress`、`uint64_t lengthBytes`、
`uint32_t type`、`uint32_t attributes` 组成，结构大小由 `static_assert`
固定。内核只把 type 1 视为可用 RAM；未知类型保持不可分配。验证必须满足：

- 指针非空，条目数在 `1..128`，管理上限非零。
- 每项长度非零，`base + length` 不发生 64 位溢出。
- 条目按 base 单调排列，半开区间互不重叠。
- 总描述字节和可用字节求和不溢出。
- 低 64 MiB 管理范围内至少存在一个可用字节。

验证函数采用候选汇总，只有全部条目成功后才覆盖调用者输出，失败不会交付部分
统计。可用区域按 4 KiB 边界向内收缩，未完整覆盖的页不会错误变成可分配帧。

### 页帧所有权

当前管理低 64 MiB，共 16384 个 4 KiB 帧。每帧使用 2 bit 状态：

| 状态 | 含义 | 允许操作 |
| --- | --- | --- |
| unavailable | 非可用 RAM 或不完整页 | 永不分配 |
| free | 已验证可用且无人持有 | 可分配或保留 |
| allocated | 动态所有者持有 | 只允许对应释放 |
| reserved | 平台或启动关键对象 | 不允许普通释放 |

2-bit 状态数组为 4096 字节。选择精确 `uint8_t` 存储是为了避免状态元数据扩大，
对外帧数、容量和地址仍全部使用 `uint64_t`。初始化后依次保留低 1 MiB、
链接器符号界定的 Kernel 映像和 64 KiB 初始栈。`ReserveRange` 先预检完整范围，
发现任意 allocated 帧就整体失败，避免只保留前半段。

### 页表与权限

`PageTableManager` 从分配器取得页表帧并清零，按虚拟地址的
`47:39`、`38:30`、`29:21`、`20:12` 提取四级索引。中间项只保存
present、writable、user 和物理地址；叶项另外编码 NX。映射拒绝非 canonical
地址、未对齐地址、物理地址越过页表地址字段、大页和重复映射。

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

页表激活后查询上述映射并确认所有 guard 仍为 not-present。每次叶项修改执行
`INVLPG`；切换 CR3 刷新当前地址空间的普通 TLB 项。当前单核启动阶段不需要
TLB shootdown。

### 早期堆

`KernelHeap` 是显式初始化的单调分配器。它验证非零范围、地址溢出、非零且
为二的幂的对齐、padding 和剩余容量；失败时不修改输出指针。它不提供释放，
只服务设备子系统启动前的小量永久对象。目标机分别做 16 字节和 4 KiB 对齐
分配，实际写入两个 64 位模式并读回，成功后才输出
`HEAP_SELF_TEST_PASSED`。

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
恒等可访问的用户虚拟地址复制。v0.8 没有并发解除映射，因此验证与复制之间
没有本阶段可触发的 TOCTOU；调度和多线程出现后必须重新设计页生命周期。

### 进入、系统调用与返回

`osKernelEnterUserMode` 保存进入前内核 RSP、RFLAGS 和非易失寄存器，
加载用户数据选择子并执行五项 `IRETQ`。`INT 0x80` 让 CPU 自动从
TSS.RSP0 取安全内核栈，系统调用公共入口复用统一寄存器帧。

分发前必须满足当前存在活跃用户执行、帧来自 CPL3、向量为 `0x80`、
CS=`0x23`、SS=`0x1B`、RIP 在用户范围、RSP 位于四页用户栈，而且当前栈
叶页是 user RW/NX。普通系统调用按原帧 `IRETQ`；exit 和用户异常不能回到
用户 RIP，而是记录结果并用 `osKernelReturnFromUserMode` 恢复进入用户态前
的内核调用链。

`WriteLog` 最多复制 160 字节到固定内核缓冲后再访问串口。用户日志带
`[OS][USER]` 只是协议来源标记，不获得内核可信度。ABI 详见
[User 与 ABI 模块](user.md)。

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
[OS][KERNEL] HEAP_SELF_TEST_PASSED
[OS][KERNEL] USER_ELF_VALID
[OS][KERNEL] USER_ENTRY=0x0000000040000000
[OS][KERNEL] USER_MAPPED_PAGES=0x...
[OS][KERNEL] USER_STACK_READY
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
[OS][USER] INVALID_POINTER_REJECTED
[OS][USER] UNKNOWN_SYSCALL_REJECTED
[OS][USER] HELLO_FROM_RING3
[OS][KERNEL] USER_EXIT_CODE=0x0000000000000000
[OS][KERNEL] USER_SYSCALL_COUNT=0x0000000000000006
[OS][KERNEL] USER_TERMINATED
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

故障镜像与生产内核共享所有实现，只替换 `osKernelEntry` 选择的注入模式。
`UD2` 必须得到向量 6、错误码 0；访问 `0x04000000` 必须得到向量 14、
错误码 0 和同值 CR2。写 `0xFFFF800000100000` 必须得到向量 14、错误码
`0x3` 和同值 CR2，逐位表示 present 页上的 supervisor write 权限违反。
三者必须输出一次 `PANIC`，且禁止出现文件统计和 `READY`。

## 已知边界

- 当前仅使用单核 PIC，并让本地 APIC LINT0 承担 virtual-wire；LAPIC
  timer/IPI、I/O APIC、MSI/MSI-X 与 SMP 路由尚未实现。
- 键盘只保存一个待处理语义事件，ATA 仍是禁用设备 IRQ 的同步单扇区 PIO；
  环形队列、IRQ14、DMA 与通用块请求尚未实现。
- 页帧分配器当前只管理低 64 MiB；高端 RAM、NUMA 和稀疏物理内存以后扩展。
- 早期堆不释放，页表取消映射也不回收空中间表；通用生命周期尚未实现。
- panic 只支持单核早期环境；SMP 停核和崩溃转储尚未实现。
- Ring 0 页故障仍全部 panic；Ring 3 页故障只终止当前用户执行。按需映射和
  写时复制要等进程地址空间拥有完整生命周期后再实现。
- 当前只有一个同步用户执行，页表根仍为内核全局根；PID、独立地址空间、
  调度和用户页回收属于 v0.9。

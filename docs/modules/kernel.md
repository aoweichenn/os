# Kernel 模块

## 职责

`kernel` 是 Stage 1 最终交接的 freestanding C++20 ELF64 可执行文件。当前
v1.10 在真实交接之上建立处理器、内存、中断、设备、文件系统与进程基础：

- 由 Clang 以 `x86_64-unknown-none-elf` 目标编译。
- 由 LLD 的 `elf_x86_64` 模式直接链接，不经过 ARM64 宿主 GCC。
- 入口符号为 C ABI 的 `OsKernelEntry`，链接地址为 `0x00100000`。
- 不链接 libc、C++ 标准库、异常、RTTI、栈保护或宿主运行时。
- 入口按 System V AMD64 ABI 从 RDI 接收 BootInfo。
- 内核验证并接管固定布局的 VGA 光标、输出模式与内存日志，不调用 Stage 1 函数。
- 用户环境启动前清屏并提交终端模式；普通诊断随后只进日志，TTY 写前台，
  panic 始终尽力双写。
- 入口验证 BootInfo、BSS 清零结果和当前 CR3，再接管 GDT、TSS 和 IDT。
- 32 个架构异常由 NASM 桩规范化为固定 C++ `ExceptionFrame`。
- 可恢复 breakpoint 经 `IRETQ` 返回，其他异常输出固定现场并 panic。
- 验证 BootInfo v2 的物理内存图，初始化 2-bit 页帧分配器。
- 建立并激活内核自己的四级 4 KiB 页表，执行 W^X、NX、WP 和 guard page。
- 映射并自检 512 KiB 高半区可回收内核堆。
- 映射 LAPIC MMIO 并建立 LINT0 ExtINT virtual-wire，接管 8259A、8254、
  i8042 和 ATA PIO。
- 向量 32..47 使用独立硬件 IRQ 汇编入口，设备处理后严格执行 PIC EOI。
- 从生产 rootfs 按需读取 `/sbin/init` ELF，建立 PID1、父子进程树和
  spawn/exec/wait 生命周期。
- 为每个用户地址空间维护 VMA，以用户 `#PF` 提交匿名、program-break 和
  受控增长栈页，并在 unmap/exec/exit 回收数据页、空页表分支与描述符。
- 通过稳定文件后备按需解析 ELF/文件 VMA；buffered read/write 与
  `MAP_SHARED` 共享动态文件页，write 直接脏化，truncate 只撤销 EOF 后 PTE。
- 验收完成后进入 IF 开启的 `HLT` 事件循环，不返回 Stage 1。

## 文件布局

Kernel 源码按功能所有权分成十三组，公开头文件与实现保持相同相对路径：

```text
include/os/kernel/<module>/<name>.hpp
src/<module>/<name>.cpp
```

| 模块 | 主要内容 |
| --- | --- |
| `arch` | GDT/TSS/IDT、异常/IRQ 汇编边界、处理器状态与 panic |
| `boot` | BootInfo 与 C ABI 入口 |
| `core` | 主流程和 freestanding 内存运行时 |
| `device` | 端口、VGA 控制台、PIC、PIT、PS/2、ATA |
| `fs` | 磁盘格式、块缓存、文件系统 |
| `io` | 控制台、FileDescription 和动态 FileTable |
| `ipc` | 管道 |
| `memory` | 页帧、buddy、页表、heap、KVA、动态栈、VMA、资源快照 |
| `object` | 类型化 KernelObject、generation 与强引用生命周期 |
| `process` | Process/Thread 状态机、run queue、WaitQueue 与目标机运行时 |
| `sync` | SpinLock、IrqSaveSpinLock 与可睡眠 Mutex |
| `time` | PIT 单调纳秒、稳定 deadline queue 与定时等待所有权 |
| `user` | 用户 ELF、用户内存、系统调用和内嵌镜像 |

例如 `memory/page_table.hpp` 与 `memory/page_table.cpp` 是一组对称接口和
实现。Kernel 的 include/src 根目录禁止继续堆放实现文件；模板实现与头文件
同目录。目录只表达维护所有权，不机械增加 C++ 命名空间层，当前公开类型仍在
`os::kernel`。

`source/kernel/CMakeLists.txt` 按同一模块集合维护头文件和源文件分组，
`tests/tooling/test_kernel_layout.py` 自动检查目录集合、根目录清洁、头源
配对和三个明确的生成/汇编例外。短规则见 `source/kernel/README.md`，完整
取舍见 [ADR 0028](../adr/0028-kernel-functional-directory-layout.md)。

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
| `0x18` | Kernel 文件物理地址 | `0x03600000` |
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

完成全部启动保留后，同一个 `PhysicalFrameAllocator` 初始化双位图 buddy。
每一阶分别保存 free 块和 allocated 块首：前者驱动查找、分裂与合并，后者
记录本次交付的精确 order，因此大块内部页、错误 order 和重复释放不会被误当
成合法块。原 `Allocate`、`AllocateInRange`、`Release` 统一映射到 order 0，
页表、heap、用户页和进程地址空间没有旁路状态机。只读
`OwnsAllocation(PhysicalFrame)` 只有在给定地址仍是精确 order-0 活动块时
返回 true；大块内部首帧、已释放帧和保留帧都不能冒充单页所有权。

页状态与 buddy 精确存储先按页对齐，再合并为一个低端启动元数据区整体选址和
reserved。64 GiB 可用页规模的 buddy 双位图精确需要 8388612 字节；QEMU
含 3--4 GiB 洞时按实际最高 PFN 计算并略大于该值。buddy 启用后
`ReserveRange` 冻结，防止只修改 2-bit 状态。`ValidateBuddy` 逐阶检查尾部
置位、父子重叠、可合并伙伴、块内页状态和计数守恒。

目标自检申请 order 3 的 8 页连续块，经 direct-map 在首尾页分别写入并读回
64 位模式，释放后核对进入前后的页统计与活动块数，并执行完整校验。64 GiB
配置要求该块位于 4 GiB 以上；64 MiB 配置使用普通可用区。实现与取舍见
[ADR 0022](../adr/0022-bitmap-buddy-frame-allocator.md)。

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
| `0xFFFF800000000000..+512KiB` | RW/NX | 高半区可回收内核堆 |
| `0xFFFF800000100000` | R/NX | Ring 0 写保护故障验收页 |
| `0xFFFF888000000000 + P` | RW/NX | E820 type 1 RAM 的 64 TiB direct-map |

新 CR3 激活前，页表帧只能从低 64 MiB 身份映射取得，并用物理地址直接访问；
激活后 `PageTableMemoryAccess` 切换到 direct-map，随后新页表页可以来自全部
受管 RAM。用户页清零与 ELF 内容复制使用同一物理到虚拟转换接口。页表激活后
查询上述映射并确认所有 guard 仍为 not-present。每次活动叶项修改执行
`INVLPG`；切换 CR3 刷新当前地址空间的普通 TLB 项。当前单核启动阶段不需要
TLB shootdown。

页表项本身没有所有权字段，而进程根会复制内核高半 PML4 项，因此管理器不能
只根据“表为空”决定释放。构造时必须显式选择：

| 根类型 | 可修改范围 | 空分支回收边界 |
| --- | --- | --- |
| `Exclusive` | 全地址空间 | PT、PD、PDPT 均可回收 |
| `KernelShared` | 内核根拥有的映射 | 回收 PT、PD；保留可能被进程根借用的 PDPT |
| `Process` | 用户程序与用户栈分支 | 只回收进程拥有分支；借用分支拒绝修改 |

`UnmapPage` 先只读走完整路径，验证子表物理地址范围、4 KiB 对齐、精确
order-0 frame ownership、无祖先环，并要求待回收表除目标项外的全部原始
64 位表项为零。预检失败不修改 PTE、TLB 或分配器；提交后只对目标叶执行
一次 `INVLPG`，再按 PT、PD、PDPT 从子到父解除父项并释放表帧。
`PageTableUnmapResult` 分层返回本次实际回收数量。

`MapPage` 与 `MapLargePage` 也记录每次父项修改和新表帧。任一级申请失败时
恢复父项原值（包括因 user 映射提升的 U/S 位），并逆序归还本事务创建的表，
不会留下半建立空分支。`QueryPage` 使用同一结构校验并正确叠加 4 KiB 或
2 MiB 页内偏移。进程根最终递归销毁只遍历进程拥有的用户分支，不追入复制
来的共享内核子树。完整决策见
[ADR 0026](../adr/0026-owned-page-table-branch-reclamation.md)。

### 可回收内核堆

v0.6 最初用单调分配器证明高半区映射之上能够放置对象。v1.1 在原 64 KiB
RW/NX 区间把实现升级为边界标记与地址有序空闲链表；v1.4 为类型化对象和
动态 FileTable 把同一基址的固定后备扩为 512 KiB。每块包含自身长度、前块
长度、请求长度、状态签名和双向空闲链接；分配使用 best-fit，对齐前缀和
剩余后缀只有达到最小块尺寸才独立拆分。

`TryRelease` 只接受活动负载的精确首地址，预检前后块后执行双向合并。空指针、
区间外/内部指针、重复释放和损坏元数据拥有独立状态。`Validate` 同时核对
物理块无缝覆盖、边界标记、空闲链排序/反链/无环、空闲集合唯一性，以及活动、
累计、峰值和最大连续空闲负载统计。分配失败不修改输出指针和堆拓扑。

目标机仍做 16 字节和 4 KiB 对齐分配，写入并读回两个 64 位模式；随后逆序
释放、合并并确认活动数与当前占用均为零。只有完整生命周期通过才输出
`HEAP_SELF_TEST_PASSED`。实现取舍见
[ADR 0020](../adr/0020-reclaimable-kernel-heap.md)。

### 固定尺寸类型缓存

`KernelFixedObjectCache` 从通用堆取得一个后备块，在块首保存每槽一位的活动
位图，随后按 `max(对象对齐, 8)` 对齐对象区。每槽步长至少为 8 字节；空闲时
槽首保存下一个空闲索引，活动时全部槽内容归调用者。申请弹出链头并置位，
释放只接受活动槽精确首地址，清位后压回链头。

`KernelTypeCache<ObjectType>` 用 `sizeof`/`alignof` 提供类型化存储，但不
隐式调用构造与析构。耗尽保持输出指针不变；空指针、区间外/槽内指针、重复
释放、计数溢出和活动对象销毁都有明确状态。`Validate` 重新计算布局并核对
位图尾位、活动/空闲/累计统计与有界空闲链遍历。`Destroy` 只在活动数为零、
校验成功且堆接受释放后清空缓存状态。

目标自检以 32 个 64 字节对齐对象执行全容量写回、耗尽、交错释放、重复释放
拒绝和 LIFO 复用，最终要求活动 0、空闲 32、累计申请/释放均为 33，并在销毁
后验证通用堆恢复基线。该路径只在初始化完成后输出一次汇总，不在每次对象
操作中打印。实现与阶段边界见
[ADR 0023](../adr/0023-heap-backed-fixed-size-type-cache.md)。

### 内核虚拟地址分配器

`KernelVirtualAddressAllocator` 只记录软件对 4 KiB 虚拟页区间的所有权，不
分配物理页，也不调用 `MapPage`。当前窗口为
`0xFFFFC90000000000..0xFFFFE90000000000`，容量 32 TiB；首个页永久保留。
调用方提供 1024 项描述符数组，活动前缀按起始地址递增，空闲区间由相邻描述符
之间的缝隙隐式表示。

`TryAllocate` 按绝对虚拟页号满足二次幂页对齐，从全部可用缝隙中选择最小者；
输出只在描述符插入和统计提交后修改。`ReserveRange` 拒绝重叠，且保留区不能
释放。`TryRelease` 必须精确匹配起始地址、页数与活动类型；内部地址、错页数、
重复释放分别诊断。描述符满返回 `MetadataExhausted`，连续地址不足返回
`OutOfVirtualAddressSpace`。

完整校验重新遍历有序描述符，核对窗口 canonical 边界、无重叠、尾部清零、
活动/保留/累计/峰值守恒和最大空洞。`OwnsAllocation` 还提供精确只读查询，
让上层对象同时证明地址区间仍由活动 allocation 持有。目标自检建立双 guard
六页区间，只映射
中间四页并真实写回；清理必须按 unmap、buddy block、KVA 顺序完成。实现、
虚拟区间取舍见
[ADR 0024](../adr/0024-reclaimable-kernel-virtual-address-allocator.md)，
共享页表回收边界见
[ADR 0026](../adr/0026-owned-page-table-branch-reclamation.md)。

### 动态内核栈管理器

`KernelStackManager` 把六页 KVA allocation 解释为一页 lower guard、四页
16 KiB 可用栈和一页 upper guard。guard 从创建到销毁始终没有叶项；每个
数据页独立申请 order-0 物理帧，完整清零后映射为 supervisor RW/NX。物理页
不要求连续，虚拟连续性由页表提供。

创建先在局部候选对象中取得 KVA、物理页和映射，验证精确 KVA allocation、
四个精确 order-0 物理 allocation、双 guard、叶权限、物理身份及栈内/栈间
帧唯一性，最后才提交槽位与统计。九项 `foundation::ScopeRollback` 动作
分别对应一次 KVA 释放，以及每个数据页的一次帧归还和一次叶映射撤销；失败
时从最后登记的动作开始严格逆序执行，单项失败不阻止其余清理。销毁也先完整
预检，再逆序 unmap、清零释放帧、释放精确 KVA，避免错误所有权进入部分清理。

运行时把动态栈所有权交给 Thread，初始 176 字节特权帧放在栈顶最后一页。
调度读取 Thread 的活动栈对象设置 TSS.RSP0，不从 Process 或槽位公式推导
地址。终止处理仍运行在该栈上，所以先发布 `Exited`；
`OsKernelEnterScheduledProcess` 恢复永久启动栈后，安全点确认当前 RSP
不属于目标栈才允许销毁。

当前管理器提供 512 个槽；64 GiB 容量事务峰值为 512 个栈、2048 个映射页
和 1024 个 guard 页。事务回收后活动数归零，随后正常 v1.9 十一个 Process/
Thread 生命周期也必须在各档并发容量内达到相应峰值并最终归零。完整设计、
故障模型和测试证据见
[ADR 0025](../adr/0025-kva-backed-dynamic-kernel-stacks.md)。

### 通用资源生命周期基础

v1.1 用两个不依赖宿主运行时的 foundation 原语和一个 Kernel 聚合快照收口
跨层资源所有权：

- `ReferenceCounter` 用显式 `uint64_t` 保存强引用数。对象必须先
  `Start()`，`TryAcquire()` 拒绝从零复活并拒绝上溢，`TryRelease()` 只在
  本次释放最后一个引用时通过输出参数报告。失败不修改计数或输出；
- `ScopeRollback` 使用调用方提供的固定动作数组，不分配内存、不抛异常。
  未提交事务在析构时自动回滚；显式回滚从后向前执行全部动作，即使某一项
  返回失败也继续清理，并把整体结果报告给上层；
- `ResourceSnapshot` 读取 frame、buddy、heap、KVA 和动态栈的稳定当前量，
  另为 Process、Thread、FileDescription、Vnode、CachePage 和 BlockRequest
  预留字段，共 26 项。它先验证各管理器内部守恒式，再比较前后快照并返回
  `uint64_t` 差异位掩码和变化字段数。

快照只比较当前所有权，不比较成功申请、释放、分裂、合并等历史累计量。例如
一个栈事务结束后，`successful_creations` 合理增加，但活动栈、映射页、
KVA 页和物理页必须恢复；把累计量放进泄漏快照会把正常历史误判成当前泄漏。

内存初始化的目标自检在保留的末端槽创建真实双 guard 动态栈，由外层回滚事务销毁，
再要求 26 字段零差异。进程运行时又在创建 v1.8 十一个进程前拍摄快照，在汇编切回
永久内核栈、销毁全部用户地址空间和终止栈后再次比较。第二道检查失败会返回
`ResourceLeakDetected`，不会只凭“进程数归零”宣布资源已回收。完整决策见
[ADR 0027](../adr/0027-v1.1-resource-lifecycle-foundation.md)。

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
3. 直接使用固定物理地址的 VGA 文本显存与只追加验收区。
4. 固定输出向量、错误码、RIP、CS、RFLAGS；页故障增加 CR2。
5. 进入 `CLI; HLT` 循环，绝不返回异常入口。

panic 不使用动态分配、格式化库、锁、异常、RTTI 或可失败的构造链。当前只输出
最小充分证据，不在未知栈健康状态下尝试回溯。

## v0.7 硬件 IRQ 与设备契约

`InterruptRuntime` 在 IF 关闭时完成全部设备配置。内存管理器已把
`IA32_APIC_BASE` 指定页映射为 RW/NX/PCD；运行时保持 LAPIC 全局启用，
启用 SVR，并把 LVT LINT0 配为未屏蔽的 ExtINT，回读后再重映射 8259A。
PIC 初始屏蔽所有 IRQ，只有 PIT、PS/2 和 ATA 自检全部成功后，才把掩码改为
`0x3FF8` 并开放 IRQ0/IRQ1、master IRQ2 cascade 与 slave IRQ14/IRQ15。

硬件 IRQ 桩统一压入零错误码和向量号，保存集合与异常 ABI 相同。分发器把
向量 32..47 还原为 IRQ0..15：

- IRQ0：增加 64 位 tick，不执行VGA 控制台 I/O。
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
非易失寄存器，再从首个 Thread 的完整现场执行 `IRETQ`。`INT 0x80` 让 CPU 自动从
TSS.RSP0 取安全内核栈，系统调用公共入口复用统一寄存器帧。

分发前必须满足当前存在活跃进程、帧来自 CPL3、向量为 `0x80`、
CS=`0x23`、SS=`0x1B`、RIP 所在叶页为 user RX、RSP 位于四页用户栈，
而且栈叶页是 user RW/NX。普通系统调用按原帧 `IRETQ`；exit 和用户异常
退出当前 Thread 并交接到下一个 Ready Thread，只有最后一个 Thread 结束时才用
`OsKernelReturnFromUserMode` 恢复调度启动前的内核调用链。

`WriteLog` 最多复制 160 字节到固定内核缓冲后再访问 VGA 控制台。用户日志带
`[OS][USER]` 只是协议来源标记，不获得内核可信度。ABI 详见
[User 与 ABI 模块](user.md)。

## v0.9 进程运行时契约

`ProcessScheduler` 是可在宿主执行的纯模型，只保存状态、PID、tick、派发和
抢占统计。`ProcessRuntime` 拥有固定四槽 PCB，把每个槽位关联到独立
`UserAddressSpace`、保存帧、终止结果和动态 Ring 0 栈所有权。IRQ、系统调用和异常
分发器都返回“下一份要恢复的帧地址”，汇编不内置调度策略。

地址空间根以当前内核 PML4 为模板。创建时克隆低端 PDPT，清空用户程序所在
PDPT[1]，并让用户栈从空的 PML4[255] 开始建立。销毁时只递归遍历这两个
独占子树，绝不释放共享内核页表。`DestroyUserPageTable` 拒绝销毁当前 CR3
或内核根，迫使终止路径先切回安全根。

进程栈从 32 TiB KVA 窗口取得“4 KiB lower guard + 16 KiB 可用栈 +
4 KiB upper guard”，中间四页由独立物理帧后备。PCB 只接受落在自己映射
范围内的 176 字节帧。每次进程切换同时更新 CR3 和 TSS.RSP0，两者任一失败
都停止系统，不能带着不一致的页表/栈所有权继续执行。终止后必须先由汇编回到
永久启动栈，再清零并释放叶映射、物理页和 KVA。

IRQ0 先由设备运行时更新 tick 并向 PIC EOI，随后调度器计算预算。中断热路径
不分配、不释放、不写 VGA 控制台；用户页和页表只在进程退出/异常路径切回内核 CR3 后
释放。全部结束后一次性输出调度汇总与每进程结果，并比较物理页帧统计。

## v0.10 同步与 IPC 运行时契约

`SpinLock` 使用编译器原子内建完成 exchange-acquire、store-release 和
relaxed 观察；忙等内层执行 x86 `PAUSE`，降低共享执行资源上的无效竞争。
`SpinLockGuard` 以 RAII 限定锁生命周期。该锁只保护不能睡眠的短临界区；
锁内禁止系统调用阻塞、VGA 控制台输出和用户内存复制。

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

## v1.2 Process/Thread 与统一等待运行时

`ThreadScheduler` 是不接触 CR3、TSS、VGA 控制台和动态分配的纯状态模型。
`ProcessRuntime` 把其决定落实到页表、动态内核栈、系统调用和 x87/SSE2
硬件现场。二者的职责不能反向渗透：

- `ProcessEntry` 只保存 PID、Alive/Zombie、地址空间根和 Thread 所有权链；
- `ThreadEntry` 是唯一调度实体，保存 TID、Ready/Running/Blocked/Exited、
  三类侵入式队列链接、动态栈槽及执行统计；
- `ProcessRuntimeProcess` 关联用户地址空间、描述符、文件句柄和终止结果；
- User `ProcessRuntimeThread` 关联 176 字节保存帧；Kernel kind 关联入口、参数和保存的
  内核 RSP；两者都拥有 16 字节对齐的 512 字节 `FxSaveArea`。

创建按“用户地址空间 → Process → 动态栈 → Thread → 初始帧/FXSAVE 模板”
逐层发布。任一步失败都只撤销已经取得的资源，输出身份和调度状态不出现半提交
对象。终止反向执行：保存 FXSAVE、发布 Thread Exited、切永久 CR3、释放
地址空间、恢复永久启动栈、销毁动态栈、reap Thread、最后 reap Zombie
Process。

run queue 是 Thread 内的双向 FIFO；Process Thread 链和 WaitQueue 是单向
侵入式链。`UINT64_MAX` 只表示“无槽索引”，零只表示“无 PID/TID”，两种
哨兵不会混用。`Validate()` 扫描完整存储，证明每个活动 User Thread 恰属于一个
Process、每个 Kernel Thread 不属于任何 Process，Ready 链无环、最多一个 Running，
以及累计 create/discard/reap 与当前拥有量守恒。

当前四个阻塞对象分别拥有 pipe-readable、pipe-writable、
descriptor-readable、descriptor-writable WaitQueue。阻塞一次性写入
WaitCondition、队列归属和 Blocked；唤醒只有第一个
`WakeReason != None` 的提交者能移除 waiter 并加入 Ready。关闭队列用
ObjectClosed FIFO 唤醒全部等待者。Mutex 复用同一队列并采用直接所有权
handoff；IRQ 上下文和持有 spinlock 的路径不能调用 Mutex。

架构初始化在 GDT 之前要求 CPUID FXSR/SSE/SSE2，设置 CR0 与 CR4 的
FXSAVE 相关位并明确清 OSXSAVE。每次抢占、阻塞、退出保存当前 Thread，
每次激活恢复目标 Thread。C++ 用户代码保持 `-mno-sse -mno-sse2`；单独的
NASM 验收桩安装和校验 XMM0、XMM15、MXCSR、x87 control word、ST0。

运行时限制按受管 RAM 选择，不改变实现：64 MiB 为 4/4/1，256 MiB 为
64/128/32，64 GiB 为 256/512/64。启动容量事务会同时占有该档全部 Process、
Thread、页表根和动态栈，中间快照核对活动数量，退出/reap 后再要求 26 字段
资源快照零差异。完整接口与状态图见
[ADR 0029](../adr/0029-process-thread-waitqueue-fxsave.md)。

## v1.3 CpuLocal 与原生系统调用模块

架构目录把可在宿主验证的纯策略与只能在目标机执行的特权操作分开：

| 文件 | 职责 |
| --- | --- |
| `processor_features.*` | CPUID 叶解码、required/available/missing mask 与地址宽度验证 |
| `cpu_local.*` | current Thread、可信入口栈、深度、重调度请求和冷路径统计 |
| `user_context.*` | 176 字节统一现场、规范地址/段/RFLAGS 验证与返回选择 |
| `native_system_call_layout.*` | EFER/STAR/LSTAR/FMASK/GS 六寄存器纯布局与回读比较 |
| `native_system_call.*` | 目标机 RDMSR/WRMSR 初始化和配置快照 |
| `architecture.asm` | SWAPGS、可信换栈、统一压帧、SYSRET/IRET 与最小 NMI 桩 |

`processor_features`、`user_context` 和 `native_system_call_layout` 进入 host
模型库；它们不得执行 CPUID/MSR 或访问全局目标状态。`processor.cpp` 只提供
精确位宽的 CPUID/RDMSR/WRMSR 边界，`native_system_call.cpp` 负责把纯布局
落实到真实 CPU。

`ProcessRuntime::ActivateThread()` 是 TSS.RSP0 与
`CpuLocal.kernel_entry_stack_pointer` 的唯一同步点。系统调用层不自行寻找
栈，也不持有 Thread 内部指针，只验证传入 frame 属于当前 Thread。Ring 0
定时器 IRQ 只能 `RequestReschedule()`；`OsKernelPrepareUserReturn()` 才能
消费请求并换 frame。

系统调用分发与返回故意拆成三个 C ABI 边界：

```text
OsKernelDispatchSystemCall(frame)
  → 验证入口所有权并处理 ABI
OsKernelPrepareUserReturn(frame)
  → 消费 need-reschedule，循环剔除非法用户现场
OsKernelSelectUserReturn(frame)
  → 记录 SYSRET/IRET 选择并结束活动系统调用
```

这样汇编只负责架构状态恢复，C++ 只负责可测试的策略。兼容和原生入口共享上述
三段函数，不允许再增加第二套 syscall switch。完整安全理由见
[ADR 0030](../adr/0030-cpu-local-native-system-call.md)。

## v1.4 KernelObject、FileDescription 与 FileTable

对象层由 `object/kernel_object.*` 独立维护。公开 API 不暴露 payload 类型：

| 类型 | 所有权 |
| --- | --- |
| `KernelObjectReference` | 操作期间的 RAII 强引用，不可复制 |
| `KernelObjectHandle` | FileTable 私有长期引用，地址与 generation 联合校验 |
| `KernelObjectManager` | 堆存储、活动链、acquire/release、finalizer 与统计 |

`KernelObjectManager::Validate()` 会重新遍历活动双向链，求和当前强引用并与
统计比较。最后引用释放时，管理器先在锁内把对象转为 Finalizing、摘除活动
链，再在锁外调用模块 finalizer 和释放堆后备。业务 finalizer 不允许在对象
管理器锁内调用文件系统或管道。

`io/file_description.*` 是第一个对象类型。共享 payload 保存 kind、
file status flags 和具体依赖；RegularFile/Directory 的
`FileSystemHandle::offset_bytes` 位于 payload 内。因此 duplicate 只增加
引用就能共享偏移，再次 open 则因新建对象而获得独立偏移。Console 输出通过
设备回调注入，通用 host 模型不依赖 VGA 控制台。

`io/file_table.*` 每 64 个 fd 申请一个有序分块。表项只保存对象 handle 和
fd flags。安装成功会把传入 reference 的强引用所有权转给表；lookup 在持表
锁时取得临时 reference；close 在锁内摘除表项后于锁外 release。缺少分块时
执行“锁内发现 → 锁外申请 → 锁内复验 → 提交或释放候选”的两阶段事务。

运行时按 RAM 为每 Process 选择 64、256 或 4096 hard limit，soft limit
初始相同。系统调用可以降低/恢复 soft limit，不能越过 hard limit。下降只
阻止新安装，不关闭已有 fd。完整设计与失败语义见
[ADR 0031](../adr/0031-typed-kernel-object-dynamic-file-table.md)。

## v1.5 VFS、FsContext 与文件后端运行时

Kernel 的 `fs` 模块现分为三部分：

- `vfs.hpp/.cpp`：Vnode、Path、Superblock、Mount、FsContext、OpenFile 和
  统一路径状态机；
- `memfs.hpp/.cpp`：KernelHeap 支持的目录/文件节点与数据；
- `legacy_file_system.hpp/.cpp`：把原固定磁盘格式接入 VFS 操作表。

`ProcessRuntimeProcess` 保存每 Process FsContext；所有普通文件和目录
FileDescription 保存 `Vfs* + OpenFile`。FileTable 与 KernelObject 生命周期
不因后端改变：duplicate 继续共享 OpenFile offset，独立 open 继续隔离。

内核启动必须先准备用户 ELF 和 Process，再初始化设备、挂载旧磁盘、建立
`/tmp`、挂载 memfs、校验 VFS，随后为所有活动 Process 初始化 FsContext，
最后才执行调度。这个次序保留非法 ELF 在中断开放前拒绝的历史失败边界。

进程结束资源校验从 VFS 读取 memfs 的精确 heap/vnode 所有权，把它作为
长期 Mount 资源与 Process 临时资源分账；任何未登记对象、frame、KVA、
页表、fd 或 heap 分配仍会产生非零差异。

路径、挂载、后端和锁协议见
[ADR 0032](../adr/0032-vfs-mount-namespace-and-memfs.md)。

## v1.7 PID1、进程树与映像事务

`process/process_tree.*` 保存与调度槽位分离的父子关系。PID 1 必须先注册且
没有父进程；普通 Process 必须指向一个 Alive 父 Process。状态只允许：

```text
Unused --Register--> Alive --MarkExited--> Zombie --Wait/Collect--> Unused
```

ThreadScheduler 管理可运行实体和安全回收，ProcessTree 管理“谁有权观察并
收集谁”。两者不能合并：父进程 wait 成功时先从树中收集退出状态，再要求
调度器目标已经是无 Thread 的 Zombie 并完成 Process 槽回收；任一不一致都
是 Kernel 内部不变量破坏，不映射为普通用户错误。

普通父进程退出时，树会在同一有界扫描中把所有直接孩子重设父进程到 PID 1。
孩子无论 Alive 还是 Zombie 都必须保留；提前丢弃 Zombie 会丢失退出状态，
继续保留已经不存在的父索引又会损坏树。PID 1 只有在没有任何孩子且自身已
Zombie 时由 Kernel 最终收集。

`process/program_arguments.*` 是不接触页表的纯布局规划器；它只记录长度并
用 64 位检查加法计算 64 页用户栈中的字符串、两个指针向量、终止空指针、
argc 和最终 16 字节对齐 RSP。`process_runtime.cpp` 才负责从用户页复制输入、
向候选用户页写入字符串/指针，并把 `argc/argv/envp` 写入初始寄存器。

磁盘装载复用 `user/user_elf.*` 的 reader 接口。第一遍只读 ELF 头和程序头，
验证 AMD64、ET_EXEC、4 KiB 对齐、用户范围、W^X、段不重叠与入口；第二遍
创建独立 Process 页表并逐段读取、清零 BSS、建立 64 页用户栈。底层短读和
设备失败保持 `ReadFailed`，格式错误保持 `InvalidElf`，两者不会被合并成
模糊的“启动失败”。

spawn 的发布顺序为：

```text
复制并规划请求
  -> VFS 打开并验证 ELF
  -> 候选 AddressSpace + 参数栈
  -> 可撤销的 Process/Ready Thread + 动态内核栈
  -> FileTable/FsContext
  -> ProcessTree 父子边
  -> Runtime active 与创建结果发布
```

失败展开与发布顺序相反。exec 不创建 PID 或父子边，而是先构造旁路候选
AddressSpace，试激活候选 CR3 后切回旧 CR3，最后在调度器锁下提交新根和
用户 RSP；随后才关闭 close-on-exec 描述符、销毁旧地址空间并把当前
`UserContext` 改写为新 RIP/argc/argv/envp。成功 `exec` 不返回旧代码；
失败 `exec` 的 CR3、RIP、RSP、fd 与父子身份保持不变。

正常启动由内核临时根上下文读取 `/sbin/init`，将第一个 Process 注册为
PID 1，再启动调度。普通 Shell、文件探针与生命周期探针都来自 rootfs；
Kernel 只嵌入启动模式需要直接选择的最小 smoke/异常夹具。模块接口、锁顺序、
错误映射和统计见 [进程模块](process.md) 与
[ADR 0034](../adr/0034-pid1-process-tree-disk-exec-wait.md)。

## 入口验收序列

成功启动必须依次输出：

```text
[OS][KERNEL] ENTERED
[OS][KERNEL] BOOT_INFO_VALID
[OS][KERNEL] BSS_ZEROED
[OS][KERNEL] CR3_VALID
[OS][KERNEL] PROCESSOR_FEATURES_READY
[OS][KERNEL] PROCESSOR_REQUIRED_FEATURES=0x000000000000003F
[OS][KERNEL] PROCESSOR_AVAILABLE_FEATURES=0x000000000000003F
[OS][KERNEL] EXTENDED_STATE_READY
[OS][KERNEL] EXTENDED_STATE_CR0=0x...
[OS][KERNEL] EXTENDED_STATE_CR4=0x...
[OS][KERNEL] EXTENDED_STATE_AVX_DISABLED=0x0000000000000001
[OS][KERNEL] GDT_READY
[OS][KERNEL] TSS_READY
[OS][KERNEL] IDT_READY
[OS][KERNEL] DESCRIPTOR_TABLES_VALID
[OS][KERNEL] CPU_LOCAL_READY
[OS][KERNEL] NATIVE_SYSCALL_READY
[OS][KERNEL] NATIVE_SYSCALL_STAR=0x0010000800000000
[OS][KERNEL] NATIVE_SYSCALL_FMASK=0x0000000000044700
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
[OS][KERNEL] BUDDY_STORAGE_ADDRESS=0x...
[OS][KERNEL] BUDDY_STORAGE_BYTES=0x...
[OS][KERNEL] BUDDY_ALLOCATOR_READY
[OS][KERNEL] BUDDY_MAX_ORDER=0x...
[OS][KERNEL] BUDDY_FREE_BLOCKS=0x...
[OS][KERNEL] BUDDY_ACTIVE_BLOCKS=0x...
[OS][KERNEL] BUDDY_SUCCESSFUL_ALLOCATIONS=0x...
[OS][KERNEL] BUDDY_RELEASES=0x...
[OS][KERNEL] BUDDY_SPLITS=0x...
[OS][KERNEL] BUDDY_MERGES=0x...
[OS][KERNEL] BUDDY_LARGEST_FREE_ORDER=0x...
[OS][KERNEL] BUDDY_SELF_TEST_ADDRESS=0x...
[OS][KERNEL] BUDDY_SELF_TEST_ORDER=0x0000000000000003
[OS][KERNEL] BUDDY_SELF_TEST_PASSED
[OS][KERNEL] PAGING_READY
[OS][KERNEL] PAGING_ROOT=0x...
[OS][KERNEL] PAGE_TABLE_RECLAIM_READY
[OS][KERNEL] PAGE_TABLE_RECLAIMED_LEVEL1_TABLES=0x0000000000000002
[OS][KERNEL] PAGE_TABLE_RECLAIMED_LEVEL2_TABLES=0x0000000000000002
[OS][KERNEL] PAGE_TABLE_RECLAIMED_LEVEL3_TABLES=0x0000000000000000
[OS][KERNEL] PAGE_TABLE_RETAINED_SHARED_LEVEL3_TABLES=0x0000000000000001
[OS][KERNEL] PAGE_TABLE_RECLAIM_SELF_TEST_PASSED
[OS][KERNEL] MEMORY_PERMISSIONS_VALID
[OS][KERNEL] HEAP_READY
[OS][KERNEL] HEAP_CAPACITY_BYTES=0x...
[OS][KERNEL] HEAP_ACTIVE_ALLOCATIONS=0x0000000000000000
[OS][KERNEL] HEAP_PEAK_CONSUMED_BYTES=0x...
[OS][KERNEL] HEAP_LARGEST_FREE_ALLOCATION_BYTES=0x...
[OS][KERNEL] HEAP_SELF_TEST_PASSED
[OS][KERNEL] TYPE_CACHE_READY
[OS][KERNEL] TYPE_CACHE_ACTIVE_OBJECTS=0x0000000000000000
[OS][KERNEL] TYPE_CACHE_SELF_TEST_PASSED
[OS][KERNEL] KVA_ALLOCATOR_READY
[OS][KERNEL] KVA_WINDOW_BASE=0xFFFFC90000000000
[OS][KERNEL] KVA_WINDOW_SIZE_BYTES=0x0000200000000000
[OS][KERNEL] KVA_ACTIVE_DESCRIPTORS=0x0000000000000001
[OS][KERNEL] KVA_ALLOCATED_PAGES=0x0000000000000000
[OS][KERNEL] KVA_RESERVED_PAGES=0x0000000000000001
[OS][KERNEL] KVA_SELF_TEST_MAPPED_PAGES=0x0000000000000004
[OS][KERNEL] KVA_SELF_TEST_GUARD_PAGES=0x0000000000000002
[OS][KERNEL] KVA_SELF_TEST_PASSED
[OS][KERNEL] RESOURCE_LIFECYCLE_READY
[OS][KERNEL] RESOURCE_SNAPSHOT_TRACKED_FIELDS=0x000000000000001A
[OS][KERNEL] RESOURCE_SNAPSHOT_CHANGED_FIELDS=0x0000000000000000
[OS][KERNEL] REFERENCE_COUNTER_SELF_TEST_PASSED
[OS][KERNEL] SCOPE_ROLLBACK_SELF_TEST_PASSED
[OS][KERNEL] RESOURCE_SNAPSHOT_SELF_TEST_PASSED
[OS][KERNEL] PROCESS_RUNTIME_READY
[OS][KERNEL] PROCESS_CAPACITY=0x...
[OS][KERNEL] THREAD_CAPACITY=0x...
[OS][KERNEL] THREADS_PER_PROCESS=0x...
[OS][KERNEL] CAPACITY_SELF_TEST_PROCESSES=0x...
[OS][KERNEL] CAPACITY_SELF_TEST_THREADS=0x...
[OS][KERNEL] CAPACITY_SELF_TEST_THREADS_PER_PROCESS=0x...
[OS][KERNEL] PROCESS_THREAD_CAPACITY_SELF_TEST_PASSED
[OS][KERNEL] KERNEL_STACK_MANAGER_READY
[OS][KERNEL] KERNEL_STACK_SLOT_CAPACITY=0x0000000000000200
[OS][KERNEL] KERNEL_STACK_MAPPED_PAGES=0x0000000000000004
[OS][KERNEL] KERNEL_STACK_GUARD_PAGES=0x0000000000000002
[OS][KERNEL] KERNEL_STACK_SIZE_BYTES=0x0000000000004000
[OS][KERNEL] PIPE_READY
[OS][KERNEL] USER_ELF_VALID
[OS][KERNEL] USER_ENTRY=0x0000000040000000
[OS][KERNEL] USER_MAPPED_PAGES=0x...
[OS][KERNEL] USER_STACK_READY
[OS][KERNEL] PROCESS_ID=0x...
[OS][KERNEL] THREAD_ID=0x...
[OS][KERNEL] PROCESS_CR3=0x...
[OS][KERNEL] PROCESS_KERNEL_STACK_LOWER_GUARD=0x...
[OS][KERNEL] PROCESS_KERNEL_STACK_TOP=0x...
[OS][KERNEL] PROCESS_KERNEL_STACK_UPPER_GUARD=0x...
[OS][KERNEL] LEGACY_INTERRUPT_ROUTING_READY
[OS][KERNEL] PIC_READY
[OS][KERNEL] PIC_MASK=0x...3FF8
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
[OS][USER] DUAL_SYSCALL_ENTRY_EQUIVALENT
[OS][USER] SYSRET_RETURNED
[OS][USER] SYSCALL_IRET_FALLBACK_RETURNED
[OS][USER] UNKNOWN_SYSCALL_REJECTED
[OS][USER] HELLO_FROM_RING3
[OS][USER][PIPE] PRODUCER_COMPLETED
[OS][USER][PIPE] PAYLOAD_VERIFIED
[OS][USER][PIPE] EOF_OBSERVED
[OS][USER] EXTENDED_STATE_ISOLATED
[OS][KERNEL] SCHEDULER_BLOCKS=0x...
[OS][KERNEL] SCHEDULER_WAKEUPS=0x...
[OS][KERNEL] EXTENDED_STATE_SAVES=0x...
[OS][KERNEL] EXTENDED_STATE_RESTORES=0x...
[OS][KERNEL] SYSCALL_IRQ_INTERRUPTS=0x...
[OS][KERNEL] SYSCALL_RETURN_RESCHEDULES=0x...
[OS][KERNEL] SYSRET_RETURNS=0x...
[OS][KERNEL] IRET_RETURNS=0x...
[OS][KERNEL] REJECTED_USER_RETURNS=0x0000000000000000
[OS][KERNEL] PIPE_CAPACITY_BYTES=0x0000000000000040
[OS][KERNEL] PIPE_WRITTEN_BYTES=0x0000000000000100
[OS][KERNEL] PIPE_READ_BYTES=0x0000000000000100
[OS][KERNEL] PIPE_EOF_OBSERVATIONS=0x0000000000000001
[OS][KERNEL] KERNEL_STACK_ACTIVE_STACKS=0x0000000000000000
[OS][KERNEL] KERNEL_STACK_SUCCESSFUL_CREATIONS=0x...
[OS][KERNEL] KERNEL_STACK_DESTRUCTIONS=0x...
[OS][KERNEL] KERNEL_STACK_PEAK_ACTIVE_STACKS=0x...
[OS][KERNEL] KERNEL_STACK_PEAK_MAPPED_PAGES=0x...
[OS][KERNEL] USER_EXIT_CODE=0x0000000000000000
[OS][KERNEL] USER_SYSCALL_COUNT=0x0000000000000006
[OS][KERNEL] USER_TERMINATED
[OS][KERNEL] PIPE_TRANSFER_VALID
[OS][KERNEL] PIPE_ENDPOINTS_CLOSED
[OS][KERNEL] KERNEL_STACK_RESOURCES_RECLAIMED
[OS][KERNEL] PROCESS_RESOURCES_RECLAIMED
[OS][KERNEL] RESOURCE_SNAPSHOT_PROCESS_LIFECYCLE_PASSED
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

## v1.8 VMA 与用户页故障职责

Kernel 内的 VM 边界分为三层：

```text
memory/virtual_memory_area.*
  ordered interval ownership, split/merge/gap, descriptor pool

user/user_memory.*
  per-process address layout, VMA policy, demand page, stack/brk/unmap

user/system_calls.*
  fixed-width ABI validation and current-process dispatch
```

`VirtualMemoryMap` 不分配物理页，也不读取 CR2；它只维护非重叠页对齐区间。
`user_memory` 把 VMA kind 与页表机制连接起来；`system_calls` 不直接修改 map，
而是把 ABI 参数交给当前 Process 的类型化入口。

vector 14 仍先经过统一 NASM 异常现场。ProcessRuntime 只有在当前 Thread
属于用户 Process 且 `HandleUserPageFault` 返回 `Handled` 时恢复同一用户
指令；guard、权限、越界与非法栈增长转成该 Process 的异常退出。Ring 0 fault、
RSVD 页表损坏与无法解释的内部状态仍不能被 demand paging 掩盖。

全局描述符池容量为 8192，单地址空间 hard limit 为 4096。池在 ProcessRuntime
初始化前建立，初始 active 为零。正常工作负载前后快照要求：

```text
final active == 0
final free == capacity
acquire delta == release delta
pool Validate == Succeeded
```

QEMU 日志只对 demand fault 与 stack growth 做二次幂采样；最终聚合一次打印
容量、峰值、申请与释放，避免 VGA 控制台吞吐改变调度和 fault 时序。详细算法见
[ADR 0035](../adr/0035-anonymous-vma-demand-paging-user-heap.md)。

## v2.5 内存压力模块契约

`memory/memory_pressure.*` 只计算水位、回收计划、commit 上限和 OOM 分数，
不访问页表、VFS 或调度器。Linux 兼容编号和默认值在该模块冻结；宿主单元和随机
测试可以不启动 QEMU 就检查全部算术、溢出、下溢与确定性平分规则。

`memory/swap_manager.*` 拥有开放寻址探测和事务语义，不拥有页帧。调用者提供
磁盘元数据与整页 read/write operation。Store 在数据 flush 后发布映射；
LoadAndRelease 在读满、校验一致且 tombstone 提交后清槽；Clone 保留源槽并写出
独立目标槽。任何 I/O、元数据提交或校验失败不得降低 active slot count。

`memory/swap_storage.*` 拥有 `OSSWAP01` 盘面、启动代次、64 字节哈希桶编码和
secondary ATA 扇区换算。28 GiB 数据区与 448 MiB 元数据区都在独立交换盘，
不会扩大 Kernel BSS；写页 flush 后才允许发布活动元数据。

`user/user_memory.*` 负责把策略接到 VMA/PTE：

- 只交换 Anonymous、ProgramBreak、UserStack 的非 COW present 页；
- 单次扫描最多 65536 页并保存地址空间游标；
- 换出顺序为 Store、Unmap、ReleaseFrame，失败逆序恢复；
- 换入用新 frame/PTE 承载候选，SwapManager 成功后再减少 swapped count；
- fork 为已换出独占页 clone 槽；unmap/exec/exit 释放 non-present 槽；
- 正常销毁要求 mapped、swapped、committed 均归零。

`process/process_runtime.*` 收集 Alive 候选并执行 OOM 结果。PID 1 永不进入可杀
集合。非当前牺牲者的 fd、FsContext、futex、进程树、Ready/Blocked Thread 和
UserAddressSpace 依次清理；当前牺牲者在异常出口转换为 SIGKILL。原 fault 最多
重试一次。

最终资源门禁同时要求 `MemoryPressureController::Validate`、
`MemoryOvercommitAccountant::Validate`、`SwapManager::Validate` 成功，且全局
committed 和 active swap 均为零。

## v2.8 动态文件缓存索引模块契约

`memory/sparse_page_index.*` 只管理 64 位整数索引到非空元数据指针的映射，不访问
页帧、VFS 或块设备。64 路节点从 KernelHeap 动态取得；父节点的四个 bitmap 分别
概括 Present、Dirty、Writeback 和 Error 子树。公开操作在索引锁内完成，插入的
所有可能失败申请都发生在连接现有树之前。

`memory/file_cache_address_space.*` 以一个 `FileCacheIdentity` 拥有页面元数据和
稀疏索引。物理地址必须 4 KiB 对齐，但地址空间不释放物理 frame。Retain/Release
只维护映射引用；Remove 只接受零引用 Clean 页；底层 Discard 接受零引用
Loading/Clean/Dirty/Error 页并拒绝 Writeback。cache 层只在填页失败时丢弃 Loading，
truncate 明确把 Loading 视为 Busy。Transition 固定 Loading 到 Clean 以及
Clean/Dirty/Writeback/Error 之间的合法有向边，并同步 radix mark。

第四增量增加 Loading 瞬态。`FilePageCache::Acquire` 在锁内取得 frame、地址空间和
唯一 entry，随后释放 cache lock 执行 source read；完成时重新核对身份、frame、零引用
和 Loading 状态，再转 Clean。Loading 不参与 Dirty radix mark，不能映射、回收或截断。

第一增量保持 `memory/file_page_cache.*` 生产接口不变；`FileIdentity` 只是
`FileCacheIdentity` 的兼容别名。第二增量已让 `memory/file_page_cache.*` 自身成为
动态地址空间注册表：它拥有 frame、全局容量/LRU/dirty 统计和 writeback 选择，单个
地址空间拥有 page metadata/radix/state/ref。

`fs::Vfs` 的公共 Read/ReadAt/Write/WriteAt、stat 和 truncate 只在 superblock
capability 为 true 时调用对应 data-cache hook；填页使用 `ReadUncachedAt`，写回使用
`WriteUncachedAt`。`FilePageCache` 的 address-space record 同时保存逻辑 EOF，控制末页
写回长度和 truncate 零区间。生产 metadata 使用独立 buddy-backed KernelHeap，不占
通用 512 KiB Heap。miss 仅在 cache spinlock 内发布唯一 Loading entry；来源读取在锁外
执行。v2.10.4 后，合格 User Thread 的同页并发 miss 在锁内登记、解锁后睡眠，成功广播前
预留真实引用；early boot、受限 Kernel worker 和调度停止期仍返回 EntryBusy。任何路径都
不能阻塞在 spinlock 临界区。

`user/file_backing.*` 的 VfsWriteback 描述符按文件身份去重并保留后端 open reference。
Dirty/Error 清空后通过反向查询回调释放，避免该模块直接依赖具体页缓存实现。共享映射
描述符与 writeback 描述符都能执行末页短写；所有写回必须绕过 VFS 公共 cache hook。

`user/user_memory.*` 维护约 10%/20%/5% 的后台、硬和目标水位，以及 64 页批次统计。
`process/process_runtime.*` 在 write 前处理硬水位，并在 `OsKernelPrepareUserReturn` 的
非 IRQ 安全点运行 pending worker；每批前写保护全部 writable shared alias。失败使
自动 worker 暂停，显式 sync 仍可重试 Error 页。

`memory/file_writeback_error_tracker.*` 从页缓存 metadata Heap 动态维护文件级 sequence
和独立打开实例引用。它不持有 VFS OpenFile，也不决定何时写盘；FileDescription 在
创建/最终释放时注册和注销，页缓存 writer 失败时记录 InputOutput。Check 只比较采样
游标，游标本身位于共享 FileDescription，因此 duplicate/fork 不会重复消费错误。

第五增量的 `FilePageCache::WritebackFile` 按文件身份与闭 page-index 范围选页。
`process/process_runtime.*` 将 fsync/fdatasync 和 msync 的范围、PTE 写保护、错误推进与
VFS Flush 排序；MS_ASYNC 只强制后台 pending。当前没有 mlock 和不一致的第二份 shared
cache，因此 MS_INVALIDATE 在完成统一权威页写回后不需额外撤销映射。

`memory/memory_pressure.*` 的 `ExecuteMemoryReclaim` 只编排 clean、writeback、swap
三种回调并核对实际计数，不访问 VFS、页表或调度器。`user/user_memory.*` 把
FilePageCache/SwapManager 接成生产操作并维护分阶段统计；`process/process_runtime.*`
提供跨进程轮转和 OOM。设备错误作为 FileWritebackFailed/AnonymousSwapFailed 向上传播，
只有 Succeeded/NoProgress 后仍低于水位才进入 OOM。

第六增量让 direct/background 都使用 `PlanMemoryReclaim` 的 file/anonymous 预算。
swappiness 范围为 0..200；两类候选同时存在时至少各保留一页，候选不足的预算转赠给
另一类。UserMemory 在私有匿名 frame 最后释放前通知 ProcessRuntime 删除 aging 身份，
覆盖普通 unmap/exec/exit、swap completion 与 OOM kill。决策见
[ADR 0062](../adr/0062-v2-9-unified-reclaim-fairness-and-oom-matrix.md)。

## 已知边界

- 当前是单核 PIC + LAPIC LINT0 virtual-wire，NVMe 另使用一个 MSI-X 向量；LAPIC
  timer/IPI、I/O APIC、通用 MSI/MSI-X 路由与 SMP 路由尚未实现。
- 键盘只保存一个待处理语义事件，ATA 仍保留同步单扇区 PIO，并通过两组 64 槽
  BlockRequest FIFO 和 IRQ14/IRQ15 驱动 primary/secondary 单飞 PIO；ATA DMA、tagged queue 和 AHCI
  尚未实现。Kernel 已有单控制器、双 namespace、16 页 PRP、四 outstanding 和
  单向量 MSI-X 的 NVMe rootfs/swap，并能对 EIO/timeout reset；多 I/O queue、
  MSI-X 多向量与多控制器尚未实现。ATA 保留启动与回退。
- 当前 64 TiB direct-map 只支持四级页表，尚未启用 LA57；页帧状态和 buddy
  位图仍按最高 RAM PFN 线性编码，极端稀疏物理地址空间、NUMA、zone、
  per-CPU page list 和分段 `vmemmap` 以后扩展。
- 内核堆已支持释放与合并，type cache 已支持固定容量单后备块，KVA 已管理
  32 TiB 独立窗口；堆后备区仍固定为 512 KiB，缓存尚不能跨多 slab 增长，
  KVA 描述符存储固定为 1024 项且尚无并发索引或内存压力回收。页表已按根
  所有权回收空分支，但单 BSP 阶段尚无 PCID、远端 TLB shootdown、RCU
  页表读取者或并发拆表。
- 当前 Thread 内核栈已动态化并支持双 guard、安全点回收与高水位统计，
  但仍固定为 16 KiB；管理器由单 BSP 串行调用，尚无 per-CPU 缓存或远端
  TLB shootdown。
- panic 只支持单核早期环境；SMP 停核和崩溃转储尚未实现。
- Ring 0 页故障仍全部 panic；Ring 3 的合法匿名、program-break、连续栈、
  file-backed、COW 和 swap-in fault 已解析，guard、权限、越界和损坏 swap 只
  终止相关用户执行。shared anonymous、huge-page COW 与多核 shootdown 尚未实现。
- 当前仍是单 BSP、固定优先级轮转；内核已经具备 Process/Thread 两级生命周期、
  PID1、父子关系、Zombie/reap、spawn/exec/wait、WaitQueue、完整 x87/SSE2
  现场、fork、用户 Thread、进程组信号、事件式 wait、session 和单 TTY
  作业控制，但尚未开放完整 POSIX wait option、多个终端或 SMP 负载均衡。
- 用户地址空间、通用内核堆、固定尺寸缓存与 v1.4 KernelObject 均可回收；
  当前对象引用在管理器锁内串行提交，尚未提供 weak reference、循环回收、
  SMP 原子引用或 RCU 延迟销毁。
- FileTable 已动态分块并支持 4096 hard limit，但仍使用有序单链；百万 fd
  位图/基数树和多 Thread exec 留给后续阶段；v1.10 已完成 fork 精确 clone
  与共享 FileDescription offset。
- VFS 已具有 Vnode、Mount、每 Process root/cwd、memfs、legacy 回归后端与
  生产 rootfs v4；unlink/rmdir/rename/truncate/stat、链接、时间戳、orphan、
  五级稀疏块树和 ordered journal 已完成。mount 拓扑仍仅在启动期建立；
  V2.11.2 已接生产 inode metadata，dentry lookup 和动态 unmount 仍在后续。
- v2.8 六个核心增量已有 64 位动态文件页 radix、统一 buffered read/write/file fault/
  `MAP_SHARED` frame、逻辑 EOF、精确 truncate、锁外 fill、按打开实例错误序列和统一
  direct reclaim；v2.9 已建立协作式 Kernel Thread、混合 User/Kernel dispatcher，并把
  常规 writeback 迁入常驻 Worker。第四增量用 PTE Accessed 建立 file/anonymous
  active/inactive 队列，第五增量已让 low/high 水位 Worker 消费显式候选；
  第六增量已让 direct/background 共用 swappiness 配额；MGLRU、memcg 与 NUMA 尚未完成。
- v2.9 WorkQueue 已有 generation WorkHandle、即时 FIFO、延迟最小堆、即时提升、合并、
  取消、失败隔离和 drain；生产 Worker 通过真实 monotonic deadline 睡眠，IRQ 只负责到期
  唤醒，硬 Dirty limit 仍由同步 direct fallback 保证前进。
- v2.10.3a 已建立 64 槽 BlockIo coordinator、completion Worker、Kernel WaitQueue 和真实
  secondary ATA IRQ15 probe；3b 又以 User Kernel stack 续体保存 FX/syscall/GS/CR3 状态，
  用 `RuntimeMutex` 拆除 VFS/cache/swap 锁内睡眠边界，并打开生产 root/swap 异步等待。
  early boot 与受限 Kernel worker 仍同步回退。
- v2.10.4 新增固定容量 `process/file_page_load.*`。同页 Loading 冲突在 cache lock 内登记，
  经 per-load WaitQueue 睡眠；owner 成功广播前为 waiter 预留真实 page reference，失败则在
  entry/frame 撤销后广播同一错误。early/受限路径仍返回 Busy，生产预读尚未实现。
- v2.10.5a 新增 `memory/file_readahead.*` 纯策略；5b 已把实例放入共享 FileDescription，
  由 VFS 页级观测产生 decision，再经 `process/file_readahead_request.*` 固定 FIFO 和第四个
  持久 WorkHandle 异步填充 FilePageCache。任务持有 retained OpenFile；Demand/Prefetch
  共用唯一 Loading。5c 新增 `memory/file_readahead_feedback.*` 固定账本，页保存
  stream/policy generation；close/reset/pressure/truncate 可取消 queued/running 请求，
  producer 领取 useful/waste 并调整后续窗口。
- v2.10.6 新增 `process/file_page_writeback.*`，按 Thread capacity 固定提供 writeback 槽、
  per-thread waiter 和 per-slot WaitQueue。同页重新脏化或同步写回等待唯一 generation 结果，
  其他 Clean 页仍可回收；失败广播保留 Error/paused 与打开实例错误序列。设备已签发请求
  继续采用 best-effort cancel，SMP 和硬件 abort 不在本阶段。
- `memory/page_aging.*` 是不依赖 Process/VFS 的纯状态模块，调用方提供 entry/hash 存储；
  ProcessRuntime 负责 file-cache/PTE 观察、代际刷新和候选 completion。4 GiB 元数据通过
  96 个左右
  的真实 frame 与两个 KVA allocation 常驻，32 GiB 档按最大容量分配；两者都在资源
  基线前建立。
- `memory/background_reclaim.*` 是只依赖 watermarks 的纯滞回状态机；ProcessRuntime
  将其决策接到第三个持久 WorkHandle，并在 Worker 锁外调用 FilePageCache、writeback
  与 SwapManager。low 到 min 留给后台调度，min 以下才走同步 direct fallback。

## v1.10 COW 内核边界

用户 private 页由 `memory/user_page_reference.*` 保存稀疏共享引用，
`memory/page_table.*` 编码软件 COW 位并替换 leaf，`user/user_memory.*`
联合 VMA、PTE 和引用状态执行 clone/private break/回滚。页故障入口仍只负责
解析 x86 error code 和当前 Thread 归属，不保存 COW 业务状态。

用户写 `#PF` 与 Kernel `CopyToUser` 共用同一 private break。引用为 1 时
原地恢复 writable；引用大于 1 时复制 4096 字节并替换当前 leaf。当前仍是
单 BSP，只执行本地 `INVLPG`；多核 TLB shootdown 不在 v1.10 范围。

结束时 Kernel 打印用户页引用 peak/retain/release 摘要，并要求 active entry
与 active reference 都为零。设计与失败事务见
[ADR 0037](../adr/0037-fork-copy-on-write.md)。

## v1.11 动态 PipeManager 与描述符边界

`ipc/pipe.*` 保留历史固定缓冲路径，并为普通用户管道增加 64 KiB 逻辑环、
16 个 4 KiB lazy page 槽和显式页分配/释放回调。写入在持有 Pipe 锁时只提交
已经准备好的页；分配失败不推进 write position。最后读端与写端关闭后，
所有后备页均返回物理分配器。

`ipc/pipe_manager.*` 拥有动态 Pipe 槽，按 RAM 档选择 8/128/1024 容量；
`io/file_description.*` 的 Pipe finalizer 把最后端点关闭交回 manager，
而不是只修改一个裸 Pipe 指针。`io/file_table.*` 的 functional hard limit
提高到 512，并以 `DuplicateTo` 提供精确目标 fd 的强引用替换。

`process/process_runtime.*` 是跨层编排点：系统调用 45 创建 manager 槽和两个
FileDescription，再原子安装两个 fd；系统调用 46 执行精确复制。任何对象、
表项或用户结果复制失败都逆序撤销。阶段末要求 PipeManager active=0、
动态页为零且 create/release 守恒。

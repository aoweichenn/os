# B4：内存、分页、保护与异常

## 1. “内存管理”其实是四个问题

学习内存时最常见的错误是把所有机制压成“地址转一下”。至少要分开：

| 问题 | 回答什么 | 当前模块 |
| --- | --- | --- |
| 物理发现 | 哪些物理区间是 RAM/保留/MMIO | BootInfo、E820 |
| 物理所有权 | 某个 4 KiB frame 属于谁 | PhysicalFrameAllocator |
| 地址翻译 | VA 通过哪个页表到哪个 PA | PageTable/MemoryManager |
| 访问权限 | read/write/execute/user 是否允许 | PTE、CR0.WP、EFER.NXE |

此外还有 cache/TLB、生命周期和并发。一个 frame “可用”不代表已经映射；一个
VA “已映射”不代表当前 CPL 可写；一个 PA “在数值范围内”不代表是 RAM。

## 2. 内存不是无限快数组

概念层级：

```text
register
  → L1/L2/L3 cache
  → RAM
  → persistent block device
```

越靠近 CPU 通常越快、容量越小。操作系统可直接管理 RAM 和页表，却不能把
cache coherence 当成普通数组细节。

### 2.1 Cache line

CPU cache 以 line 为单位传输，不一定按单个 C++ 变量。两个独立变量落在同一
line 时可能产生 false sharing；当前单核项目暂不做性能优化，但原子和设备
映射语义仍需正确。

### 2.2 RAM 与磁盘

RAM：

- 按 byte-addressable load/store。
- 断电丢失。
- CPU 可经页表直接访问。

磁盘：

- 通过设备命令按 sector 传输。
- 持久。
- 不能把 LBA cast 成指针。

文件系统 cache 把两类设备连接，但不会消除它们的语义差异。

## 3. PC 物理地址空间不是连续 RAM

一个物理地址可能落在：

- usable RAM。
- 固件保留。
- ROM。
- PCI/MMIO hole。
- LAPIC MMIO。
- 不存在区域。

所以：

```text
0 <= PA < installed-memory-number
```

不是有效分配判据。

## 4. E820 的角色

传统 PC 通过 E820 风格表描述物理区间。当前项目从 QEMU `fw_cfg`
`etc/e820` 读取，然后转成 BootInfo 固定条目。

典型类型：

- type 1：usable RAM。
- 其他：reserved 或特殊用途。

### 4.1 为什么仍需验证平台给的数据

内存图来自硬件/固件边界，必须检查：

- entry size。
- count×size 溢出。
- base+length 溢出。
- zero length。
- 排序。
- overlap。
- 地址宽度。

平台是数据来源，不是自动可信的 C++ 对象。

### 4.2 可用区间还要页对齐

allocator 管理完整 4 KiB frame：

```text
usable begin 向上对齐
usable end   向下对齐
```

两端不足一页的碎片不能分配，否则 frame 会覆盖保留字节。

## 5. Page 与 frame

术语：

- page：虚拟地址空间中的固定大小块。
- frame：物理 RAM 中的固定大小块。

映射建立：

```text
virtual page → physical frame
```

两者都常为 4 KiB，所以容易混用；名称必须注明 virtual/physical。

## 6. 物理所有权状态

最小 allocator 至少区分：

```text
Unusable / Reserved
Free
Allocated
```

### 6.1 为什么 reserved 不等于 allocated

- Reserved：永远不应进入普通分配池，例如 ROM/MMIO/启动占用区。
- Allocated：由动态请求获得，生命周期结束后可释放。

把两者合并会让统计和释放检查失去意义。

### 6.2 Double allocation

同一 frame 同时分给两个独立对象，会让：

- 一个进程写坏另一个进程。
- 页表页被用户数据覆盖。
- free 后仍被旧映射访问。

allocator metadata 是安全边界，不只是性能数据。

### 6.3 Double free

重复释放会让同一 frame 多次出现在 free pool，最终再次双重分配。释放必须
验证当前状态确实为 Allocated，并明确哪些保留 frame 永不可释放。

## 7. Metadata 本身占内存

若每 frame 需要状态 bit：

```text
metadata size ∝ maximum managed PFN
```

不能为了管理 64 GiB，把 metadata 固定塞进一个只按 64 MiB 计算的数组。

当前 v1.4 主线使用每 frame 2 bit，按实际最高 usable PFN 计算 metadata，
再从启动可达 RAM 中动态放置。v1.0 精确基线则只管理低 64 MiB；两者在
[v0.6 文档](../07-v0.6-memory-management.md) 中分开讲解。

## 8. Allocation policy 与正确性

first-fit、next-fit、buddy 等是策略。最先要证明的不变量：

- 只返回对齐的完整 frame。
- 返回前状态从 Free 变 Allocated。
- 不返回 reserved/hole。
- exhaustion 有明确错误。
- 失败不部分占有资源。
- free 后统计恢复。

策略可以后优化，所有权错误不能。

## 9. Segmentation 到 paging

x86 地址历史路径：

```text
selector + offset
  → segment descriptor
  → linear address
  → page walk
  → physical address
```

64-bit mode 中普通 CS/DS/ES/SS base 大多视为 0，地址隔离主要由 paging
负责。FS/GS base 和特权属性仍有特殊用途。

## 10. 四级分页

经典四级 4 KiB page 的 48-bit canonical VA 分解：

```text
63........48 47.....39 38.....30 29.....21 20.....12 11.....0
sign extension  PML4    PDPT      PD        PT       offset
                 9       9         9         9        12
```

每级 512 entries，每项 8 bytes，一张表正好 4096 bytes。

### 10.1 Index 计算

```text
pml4 = (va >> 39) & 0x1FF
pdpt = (va >> 30) & 0x1FF
pd   = (va >> 21) & 0x1FF
pt   = (va >> 12) & 0x1FF
off  = va & 0xFFF
```

### 10.2 页表项保存什么

简化为：

```text
physical base of next table/frame
flags
```

页表项不是指针。物理 base 必须通过 identity/direct map 变成 Kernel 可
解引用 VA，才能读取下一张表内容。

## 11. Page-table walk

CPU：

1. 从 CR3 取得 PML4 physical base。
2. 用 PML4 index 读取 PML4E。
3. 检查 present 与保留位。
4. 取得 PDPT physical base。
5. 重复到叶项。
6. 把 frame base 与 page offset 拼接。
7. 累积权限并完成 load/store/fetch。

任何一级 not-present 都产生 #PF。不是只有叶项才重要。

## 12. 常见页表位

| 位/属性 | 基本语义 |
| --- | --- |
| P | entry/映射存在 |
| RW | 允许写 |
| US | 用户可访问 |
| PWT/PCD | cache policy 相关 |
| A | 访问过 |
| D | 叶页写过 |
| PS | 大页 |
| G | global mapping |
| NX | 禁止取指 |

具体保留位和物理地址 bit 受页级、能力与地址宽度约束，不能用一个通用 mask
随意接受。

## 13. 权限沿路径累积

用户访问要求每一级路径都允许 user。写访问要求路径允许写。叶项：

```text
US=1
```

但上层 `US=0` 时，最终仍是 supervisor。

### 13.1 为什么中间表设 user 不会暴露所有叶页

中间项 US=1 只是允许继续走到下一层；最终叶项仍可 US=0。共享表设计要分析
每条路径，而不是看某一位孤立判断。

## 14. Canonical address

四级分页下 bit 63..48 必须等于 bit 47。非 canonical 地址常产生 #GP，而不是
普通 not-present #PF。

地址验证顺序需要区分：

- 64 位算术是否溢出。
- 数值是否 canonical。
- 是否在项目允许 VA window。
- 页表是否 present/user/writable。

## 15. CR3 与地址空间

CR3 选择当前页表根。相同 VA：

```text
Process A CR3 → frame X
Process B CR3 → frame Y
```

于是两个程序能在相同链接地址运行却拥有独立数据。

### 15.1 Kernel mapping

进程页表通常共享 supervisor Kernel 子树，使 syscall/IRQ 切到 Ring 0 后仍能
执行 Kernel。但 user bit 必须阻止 CPL3 访问。

### 15.2 切 CR3 的前置

新页表必须映射：

- 当前 RIP。
- 当前 RSP/stack。
- GDT/IDT/TSS。
- 立即执行的数据和函数。
- 必要设备映射。

漏一项，写 CR3 后下一条指令可能 fault。

## 16. TLB

Translation Lookaside Buffer cache 最近的 VA→PA 与权限结果。若每次 load 都
四级 walk，成本很高。

### 16.1 修改 PTE 后

普通内存中 PTE 改了，不代表 CPU 已丢弃旧 TLB：

- `invlpg` 可失效某 VA。
- 重载 CR3 通常刷新相应非-global translations。
- PCID/global page 会改变精确语义。

当前阶段策略较简单，但未来修改运行中地址空间必须建立 TLB invalidation
协议。

### 16.2 SMP shootdown

多核下其他 CPU 也可能 cache 同一地址空间翻译。需要 IPI/shootdown。当前
单 BSP 项目不实现，不能把单核正确性自动外推。

## 17. Page size

### 17.1 4 KiB

优点：

- 权限精细。
- internal fragmentation 小。
- 适合用户页、Kernel segment、guard。

缺点：

- 大范围映射需更多页表项。
- TLB coverage 较小。

### 17.2 2 MiB

PD 叶项设置 PS：

- 覆盖 512 个 4 KiB page。
- 减少页表内存和 TLB 压力。
- 要求大页对齐，边界和 holes 处理更粗。

当前 64 GiB direct-map 在完整对齐 usable 区域优先 2 MiB，E820 边缘回退
4 KiB。

## 18. Direct map

Kernel 经常需要访问任意 owned physical frame 的内容，例如：

- 清零用户页。
- 写页表。
- 复制 ELF。
- 读取 allocator metadata。

identity mapping 让 VA=PA，但会占用低 VA，并诱导把 PA 当指针。

高半 direct-map 建立：

```text
VA = directMapBase + PA
```

它不是：

- frame ownership。
- user mapping。
- 所有物理地址无条件可访问。

当前只映 usable RAM 区间；MMIO 应按设备 cache 属性独立映射。

## 19. Virtual layout 是资源规划

地址空间布局要给：

- Kernel text/rodata/data。
- direct map。
- heap。
- user text/data。
- user stack。
- guard pages。

分配互不重叠的 VA window。数值空闲不等于 PA 已分配，VA layout 与 physical
allocator 仍是两层。

## 20. Guard page

在栈边界留 not-present page：

```text
guard (not present)
stack pages (RW)
```

越界不再静默写坏相邻对象，而产生 #PF。

Guard 只能捕获跨进未映射页的错误：

- 栈内错误 offset 未必触发。
- 一次巨大跳跃可能越过 guard 落入别的映射。
- handler 仍需识别是哪一类 guard。

## 21. Ring 与特权

x86 有 Ring 0..3，常用：

- CPL0：Kernel。
- CPL3：User。

数字越小权限越高。

### 21.1 CPL、DPL、RPL

- CPL：当前 CS selector 低两位。
- DPL：descriptor/gate 声明的 privilege。
- RPL：selector 携带的请求级。

硬件结合它们检查 control transfer 和 segment access。仅在 PCB 写
`isUser=true` 不会改变 CPU 权限。

## 22. GDT 在 64 位的作用

普通分段 base/limit 弱化，但 GDT 仍提供：

- Kernel 64-bit code descriptor。
- Kernel data。
- User data/code。
- TSS descriptor。

CS 中的 L/DPL/type 决定 64-bit code 与 CPL。IDT gate 还需合法 code
selector。

## 23. TSS

64-bit TSS 主要用于：

- RSP0：CPL3→CPL0 时的 Kernel stack。
- IST：特定异常的独立 emergency stack。
- I/O bitmap offset。

当前每次切用户进程都更新 TSS.RSP0 指向该进程 Ring 0 stack。否则下一次
syscall/IRQ 会把 frame 压到错误进程栈。

## 24. IDT

IDT 每个 gate 描述：

- handler offset。
- code selector。
- IST index。
- gate type。
- DPL。
- present。

### 24.1 Interrupt gate 与 trap gate

Interrupt gate 进入时清 IF，减少 handler 被普通可屏蔽 IRQ 嵌套。
Trap gate 通常保留 IF。

项目 `int 0x80` 使用 DPL3 gate，让用户可主动调用；普通硬件/异常 gate 不能
全部开放给 CPL3。

## 25. Exception、interrupt 与 syscall

| 来源 | 示例 | 同步性 |
| --- | --- | --- |
| exception | #UD、#PF、#GP | 与当前指令同步 |
| hardware interrupt | PIT、keyboard | 外部异步 |
| software interrupt | `int 0x80`、`int3` | 指令主动触发 |

它们都可通过 IDT，但来源、错误码和恢复策略不同。

## 26. Fault、trap 与 abort

- fault：保存 RIP 通常指故障指令，修复后可重试。
- trap：指令已完成，保存 RIP 通常指下一条。
- abort：严重状态，可靠恢复不保证。

不要写一个“统一 RIP++”。当前只允许已知 INT3 self-test 返回，普通 Kernel
异常 panic；用户异常则终止当前进程并继续 Kernel。

## 27. Exception frame

CPU 根据：

- 是否 privilege change。
- 该向量是否有 hardware error code。
- 是否使用 IST。

压入不同字段。汇编桩必须规范化成统一 C++ view。

当前
[architecture.asm](../../../source/kernel/src/arch/architecture.asm)
保存 15 个通用寄存器、vector/error 和 hardware frame，总计固定用户现场。

## 28. Page fault

#PF 提供两类关键事实：

- CR2：发生访问的 linear address。
- error code：访问性质。

常见 bit：

| bit | 1 的含义 |
| ---: | --- |
| 0 P | 保护冲突；0 为 not-present |
| 1 W/R | 写访问 |
| 2 U/S | CPL3 来源 |
| 3 RSVD | 页表保留位错误 |
| 4 I/D | 取指 |

例子：

- `0x3`：present + write + supervisor，适合 CR0.WP read-only 测试。
- `0x4`：not-present + read + user。

错误码是硬件分类，不自动说明哪一个高级对象有错；Kernel 仍需用 VA layout 和
PCB 判断 guard、用户缺页或非法 Kernel access。

## 29. CR0.WP

若 CR0.WP=0，Ring 0 对 supervisor read-only page 可能仍可写。W^X 要求
Kernel 自己也服从：

```text
PTE RW=0 + CR0.WP=1
```

当前故障镜像让 Ring 0 真写 read-only page，要求 #PF `0x3`，比读取 PTE
更强。

## 30. NX

NX 生效需要：

```text
CPUID capability
IA32_EFER.NXE=1
leaf NX=1
```

对 data/stack/direct map 设置 NX，text 保持 executable。执行权限与 read
权限不是同一个概念。

## 31. User pointer 验证

syscall 参数由不可信用户控制。不能只检查首地址：

1. `address+length` 无溢出。
2. 整个 range 在 user VA window。
3. 每个涉及 page present。
4. 每级 U/S 允许 user。
5. 写目标还需 writable。
6. CopyFrom/ToUser 使用固定 Kernel buffer。

### 31.1 TOCTOU

验证后到复制前，若另一个线程可改映射，就可能 time-of-check/time-of-use。
当前单核、无用户多线程且 kernel entry 关中断，边界较简单；未来需要
address-space lock、page pinning 或 fault-aware copy。

## 32. 资源回收顺序

释放地址空间时：

```text
停止使用该 CR3
→ 切永久 Kernel CR3
→ 解除/遍历独占映射
→ 释放 user frames
→ 释放 lower-level page-table frames
→ 最后释放 root
```

若先 free 当前 root，下一条 Kernel 取指或栈访问仍通过已回收页表。

资源守恒要比较：

- 创建前 frame stats。
- 创建/运行中预期增长。
- 全部结束后恢复。

## 33. Cache policy 与 MMIO

普通 RAM 通常使用 write-back。设备 MMIO 寄存器不能被 CPU 当普通 cache line
延迟/合并。

PTE PCD/PWT 与 MTRR/PAT 共同影响精确 cache type。当前项目只建立所需最小
LAPIC MMIO 映射；扩展设备映射时必须显式设计，不能把 direct-map RAM policy
套给所有 PA。

## 34. 当前项目没有实现什么

v1.0 基线没有：

- demand paging。
- VMA。
- swap。
- mmap。
- shared memory。
- copy-on-write。
- page cache 与 filesystem cache 统一。
- PCID。
- SMP TLB shootdown。

当前 v1.4 已有动态物理内存、高半 direct-map、buddy、KVA、动态双 guard
内核栈和按根所有权回收页表空分支，但这不等于上述 VMA、demand paging 或
COW 已经存在。可回收“已经明确拥有的映射对象”与“按缺页创建用户映射意图”
是两个阶段。

## 35. 常见误解

### 35.1 “分配一个 frame 就能得到指针”

allocator 返回 PA ownership；必须经已存在 mapping 得到 VA 才能解引用。

### 35.2 “页表负责知道 frame 是否空闲”

页表只描述映射事实，不是全局所有权数据库。

### 35.3 “PTE RW=0 就绝对只读”

还需 CR0.WP 约束 supervisor write。

### 35.4 “CR3 切换只换用户内存”

它切换整个 page-table root；新 root 必须保留 Kernel 必需映射。

### 35.5 “#PF 都可以分配一页继续”

只有拥有 VMA、backing object、权限与回收协议后，not-present 才可能合法按需
恢复。非法用户访问和 Kernel bug 不能盲目补页。

## 36. 对照项目阅读

1. [内存图](../../../source/kernel/src/memory/physical_memory_map.cpp)
2. [物理分配器](../../../source/kernel/src/memory/physical_frame_allocator.cpp)
3. [页表](../../../source/kernel/src/memory/page_table.cpp)
4. [内存管理器](../../../source/kernel/src/memory/memory_manager.cpp)
5. [KVA allocator](../../../source/kernel/src/memory/kernel_virtual_address_allocator.cpp)
6. [动态 Kernel stack](../../../source/kernel/src/memory/kernel_stack_manager.cpp)
7. [进程运行时](../../../source/kernel/src/process/process_runtime.cpp)
8. [异常帧](../../../source/kernel/src/arch/exception_frame.cpp)
9. [异常策略](../../../source/kernel/src/arch/exceptions.cpp)
10. [panic](../../../source/kernel/src/arch/panic.cpp)
11. [v0.6 学习阶段](../07-v0.6-memory-management.md)

## 37. 练习

### 练习 A：VA 分解

对一个 canonical VA 手工计算 PML4/PDPT/PD/PT index 与 offset，并验证高位
sign extension。

### 练习 B：有效权限

给四级路径：

```text
PML4 US=1 RW=1
PDPT US=1 RW=1
PD   US=0 RW=1
PT   US=1 RW=0 NX=1
```

分别判断 CPL0/CPL3 read、write、execute。

### 练习 C：E820 对齐

给 usable range `[0x1234,0x9234)`，计算其中完整 4 KiB frames。

### 练习 D：错误码

解释 `#PF error=0x7` 和 `0x15` 各 bit，区分 protection/not-present、
read/write、user/supervisor、instruction/data。

### 练习 E：回收

画出一个进程拥有 PML4、两层私有页表、3 个 code/data frame、4 个 stack
frame 时的释放顺序。指出在切回 Kernel CR3 前哪些对象不能释放。

## 38. 通过标准

应能：

- 分开解释物理发现、所有权、翻译和权限。
- 手工完成四级 VA 分解与有效权限判断。
- 说明 E820、allocator metadata 和 direct-map 的不同职责。
- 解释 GDT/TSS/IDT 在 64-bit mode 中仍为何必要。
- 画出异常硬件帧与汇编规范化层。
- 用 CR2/error code 分析 #PF。
- 说明 CR0.WP、NX、guard page 和 user pointer validation 的边界。
- 说明当前系统为何不能把所有 not-present fault 当作 demand paging。

下一册进入
[中断、时间与 PC 设备](05-interrupts-time-and-pc-devices.md)。

# ADR 0024：用有序所有权区间管理内核虚拟地址

## 状态

已接受，作为 v1.1 可回收资源基础的第五个完整实现增量。此前四个增量依次建立
动态物理内存与 direct-map、可回收内核堆、双位图 buddy 页帧分配器和固定
尺寸 type cache。本 ADR 落地时记录的“页表暖机后保留三级空表”是历史过渡
状态，已由 [ADR 0026](0026-owned-page-table-branch-reclamation.md) 的
按根所有权回收取代。

## 背景

页帧分配器回答“哪一组物理页由谁持有”，页表回答“一个虚拟页当前翻译到哪一
物理页并具有什么权限”。这两层都不能回答“哪一段内核虚拟地址已经许诺给某个
上层对象”。在只有固定 heap、静态内核栈和固定 MMIO 页时，地址由常量直接
划分；动态内核栈、保护页、非连续物理页映射和多 slab 后备则需要独立的虚拟
区间所有权。

如果没有这一层，两个调用方可能各自找到同一段尚未映射的地址。页表在第一次
映射前无法发现冲突，撤销映射后也无法证明地址是否已经可以交给其他对象。
“当前 not-present”只描述硬件翻译状态，不等于“当前没有软件所有者”。

完整 Linux `vmalloc` 子系统还处理多种 VMA 索引、延迟 TLB 刷新、页表回收、
模块映射、调试保护和并发。v1.1 当前仍是单 BSP 串行内核，直接复制这些策略
会跨越动态 Thread、锁和页表生命周期边界。本增量先冻结最小而完整的所有权
协议。

## 决策

### 独立的三层所有权

KVA 分配器只管理虚拟页号区间，不申请物理页，也不修改页表：

```text
KernelVirtualAddressAllocator
    │ 申请 [virtual_begin, page_count)
    ▼
PhysicalFrameAllocator / buddy
    │ 申请一个或多个物理页
    ▼
PageTableManager
    │ 逐页提交 virtual → physical + permissions
    ▼
调用者可以访问映射
```

销毁按相反顺序执行：

```text
撤销叶映射并 INVLPG
  → 归还物理页
  → 释放 KVA 所有权区间
```

这条顺序是接口契约，不由任一单层偷偷替调用方完成。未来动态栈的首尾保护页
属于 KVA 区间，但故意不建立叶映射；因此“拥有但未映射”必须是合法状态。

### 32 TiB 四级分页窗口

当前四级分页布局采用：

```text
begin              = 0xFFFFC90000000000
size               = 32 TiB
end exclusive      = 0xFFFFE90000000000
page capacity      = 8,589,934,592
permanent guard    = first 4 KiB page
```

该区间位于 64 TiB direct-map 之后，完整落在 48 位 canonical 高半区中，并
保留与常见 x86-64 `vmalloc` 布局相似的阅读方向。它是项目自己的 ABI 常量，
不是调用宿主或 Linux 的映射能力。若未来启用 LA57，必须以新的地址布局 ADR
迁移，不能让同一常量在两种 canonical 规则下产生不同含义。

首个 4 KiB 页作为永久保留区，令零偏移和窗口基址附近的错误更容易诊断。它
占用一个软件描述符，但不占物理页，也不创建页表叶项。

### 调用方提供描述符存储

按每个 4 KiB 页保存一位，32 TiB 窗口仅活动位图就需要 1 GiB；按页保存字节
或结构会更大。实际启动阶段同时存在的 KVA 对象远少于页数，所以实现保存有主
区间而不是逐页状态。

调用方提供 `KernelVirtualAddressRangeDescriptor` 数组及容量。每个描述符
包含：

- `begin_address`：页对齐起始虚拟地址；
- `page_count`：区间页数；
- `kind`：`Allocation` 或 `Reservation`。

分配器只使用数组前部，按起始地址严格递增排列；尾部全部保持
`Unused + 0 + 0`。当前内核在 BSS 中提供 256 项，即 6144 字节。描述符容量
限制同时存在的区间数量，不限制单个区间或窗口能覆盖的页数。接口本身接受
调用方存储，未来可把元数据迁移到 type cache 或按页后备而不改变分配语义。

### 空闲区间隐式表示

空闲区间不保存独立节点，而是由窗口边界与相邻有主描述符之间的缝隙推导：

```text
window begin
    │ reservation │ free gap │ allocation │ free gap │ allocation │
                                                                window end
```

释放一个描述符后，相邻空闲缝隙在模型上立即成为一个连续区间，无需另做合并
操作，也不存在空闲链断裂。代价是申请需要扫描至多 256 个描述符，插入和删除
需要移动数组尾部，复杂度为 \(O(N)\)。对当前有界串行阶段，这比引入树节点、
节点分配失败和并发旋转更容易审计。

### best-fit 与绝对页对齐

`TryAllocate(page_count, alignment_page_count, range)` 要求页数非零、对齐页数
为二次幂。对每个空闲缝隙：

1. 把缝隙起始地址转换为绝对虚拟页号；
2. 按请求页数向上对齐绝对页号；
3. 验证对齐前缀之后仍能容纳请求；
4. 从全部可容纳缝隙中选择原始页数最小者，同大小时保留低地址者。

使用绝对页号而不是窗口相对偏移，保证 8 页对齐表示虚拟地址本身按 32 KiB
对齐。所有页到字节乘法、向上对齐加法和地址末端计算均在提交前检查 64 位
溢出。

候选全部确定后才移动描述符并更新统计。失败时输出 `range` 保持调用者原值。
描述符容量耗尽返回 `MetadataExhausted`，没有足够连续虚拟页返回
`OutOfVirtualAddressSpace`；两种资源压力不会混成一个错误。

### 保留、精确释放与诊断

`ReserveRange` 用于窗口内不应分配的固定软件区间。它执行与普通区间相同的
页对齐、溢出、边界和重叠检查，但不能通过 `TryRelease` 归还。重叠诊断优先
于描述符容量诊断，因此即使数组已满，重复保留已有范围仍返回
`RangeOverlap`。

普通释放必须同时匹配：

- 分配描述符的精确起始地址；
- 分配时记录的精确页数；
- 当前 `Allocation` 状态。

区间内部地址返回 `AllocationNotFound`，错误页数返回
`AllocationSizeMismatch`，保留区返回 `ReservedRange`，重复释放返回
`AllocationNotFound`。验证失败不移动描述符、不修改计数，也不猜测调用者
本来想释放哪个对象。

### 完整一致性校验

`Validate` 不申请临时内存，并重新证明：

- 窗口页对齐、乘法不溢出，首尾均为 48 位 canonical 地址；
- 活动描述符数量不超过容量，尾部元素全部为 `Unused`；
- 每个活动描述符非空、在窗口内、类型有效，且数组严格有序不重叠；
- 重新累计的分配页、保留页、活动分配和保留区数量等于缓存统计；
- `successful_allocations - releases == active_allocations`；
- 当前值不超过峰值，分配页与保留页之和不超过窗口容量。

最大连续空闲页数由同一有序描述符集合重新计算，不维护第二份可漂移索引。

### 并发边界

当前对象只在 BSP 串行启动路径或由调用者保证串行的路径使用。它不隐藏关中断、
spinlock 或宿主互斥量，也不承诺 IRQ、NMI 和 panic 路径安全。v1.2 冻结
Thread 与 `IrqSaveSpinLock` 语义后，拥有某个 KVA 域的上层对象负责外层同步。

## 目标启动自检

真实内核在 CR3、direct-map、buddy 和内存保护均生效后运行两段事务。

第一段申请一个虚拟页和一个 order 0 物理页，建立并撤销 KVA 页表路径。
最初实现把留下的三级空表视为页表基础设施基线；ADR 0026 落地后，同一暖机
事务必须回收 PT 与 PD，只保留仍可能被进程 PML4 引用的共享 PDPT。暖机因此
从“隔离已知泄漏”变为“验证共享所有权边界”。

第二段执行：

1. 申请 6 页、8 页对齐的 KVA 区间；
2. 从 buddy 申请 order 2，即 4 个连续物理页；
3. 保持区间第 0 页和第 5 页 not-present；
4. 把中间 4 页映射为 supervisor RW/NX；
5. 查询每个叶项的物理地址、4 KiB 粒度和权限；
6. 经 KVA 首尾数据页写入并读回两个 64 位模式；
7. 逆序撤销 4 个映射，归还物理块，再归还 KVA 区间；
8. 比较暖机后的页帧、buddy 与 KVA 统计基线并运行两套完整校验；
9. 核对两段事务累计回收两张 PT、两张 PD、零张 PDPT，并保留一张共享 PDPT。

成功日志中的稳定结果为：

```text
KVA_WINDOW_BASE                 = 0xFFFFC90000000000
KVA_WINDOW_SIZE_BYTES           = 0x0000200000000000
KVA_DESCRIPTOR_CAPACITY         = 256
KVA_ACTIVE_DESCRIPTORS          = 1
KVA_ALLOCATED_PAGES             = 0
KVA_RESERVED_PAGES              = 1
KVA_SUCCESSFUL_ALLOCATIONS      = 2
KVA_RELEASES                    = 2
KVA_PEAK_ALLOCATED_PAGES        = 6
KVA_SELF_TEST_MAPPED_PAGES      = 4
KVA_SELF_TEST_GUARD_PAGES       = 2
```

热路径不逐次打印申请、扫描、插入、释放或映射。只有两段事务全部提交后才输出
一次快照和 `KVA_SELF_TEST_PASSED`。

## 测试证据

- 单元测试覆盖未初始化、空元数据、零容量、跨 canonical 空洞、重复初始化、
  保留重叠/越界/释放、best-fit、绝对对齐、失败输出保持、错误页数、内部地址、
  重复释放、空洞复用、描述符耗尽、地址耗尽和外部元数据损坏；
- 集成测试用真实 `PhysicalFrameAllocator` 与 `PageTableManager` 串联六步所有权
  生命周期，验证两个 guard 保持 not-present、四个数据页物理身份与 RW/NX
  权限、模式写回和最终统计；
- 固定种子 `0x4B564152414E444F` 执行 100000 步随机申请/释放，与 512 页独立
  逐页模型比较 best-fit 地址、对齐、活动集合、元数据耗尽、地址耗尽、累计统计
  和最大空洞，并每 257 步运行完整校验；
- 64 MiB QEMU 已完成真实页表、TLB 失效、双 guard、写回与回收；64 GiB 主规格
  使用同一代码路径并继续验证高端物理内存能力；
- 本 ADR 三项测试加入后完整 CTest 集合当时为 86 项；动态栈与页表回收
  增量完成后当前为 92 项。Kernel ELF 审计继续要求没有异常、
  RTTI、隐藏宿主分配或未解析运行时符号。

## 结果

### 正面结果

- not-present 状态与虚拟地址软件所有权不再混为一层；
- 32 TiB 窗口只消耗 6 KiB 固定描述符，而不是 GiB 级逐页位图；
- 动态栈可以在同一所有权区间中保留未映射 guard；
- 失败输出、错误类型、累计统计和完整校验形成可机器验收的契约；
- 后续元数据存储可以替换，调用方的区间接口无需改变。

### 受控限制

- 当前最多同时保存 256 个分配或保留描述符；
- 申请、插入和删除为 \(O(N)\)，尚无平衡树或分片索引；
- 没有锁、per-CPU KVA cache、延迟 TLB 批处理或并发读者；
- KVA 只管理地址，不自动分配页、映射、清零或回滚跨层事务；
- 本 ADR 落地时 `UnmapPage` 尚不回收空中间页表；该历史限制已由
  [ADR 0026](0026-owned-page-table-branch-reclamation.md) 消除，当前共享
  根只按所有权有意保留一张 PDPT；
- 本 ADR 落地时动态内核栈尚未迁移；该历史限制已由
  [ADR 0025](0025-kva-backed-dynamic-kernel-stacks.md) 消除。

## 被否决方案

### 每页一位的 32 TiB 位图

查询简单，但需要 1 GiB 常驻元数据；若还要区分保留与分配则至少翻倍。它让
极少量区间的学习内核为数十亿个潜在页付费。

### 只查询页表寻找 not-present 区间

页表没有“已许诺但故意未映射”的状态，无法表示 guard，也无法在映射提交前
防止两个调用方取得同一地址。

### 在 KVA 内自动申请物理页并映射

接口看似方便，却把虚拟区间、物理连续性、页权限、回滚和 TLB 生命周期绑成
一个不可复用策略。设备 MMIO、非连续页和 guard 都需要不同后备方式。

### 立即引入红黑树

查找复杂度更好，但树节点本身需要稳定分配、旋转不变量、删除修复和并发策略。
当前上限 256 项，数组模型更小且能完整遍历验证；性能索引应在压测证明需要后
独立加入。

### 用通用堆保存描述符

可以工作，但会让 KVA 启动依赖当前固定 64 KiB heap，并长期占用一个活动堆
对象。调用方存储保持依赖方向清晰，也为未来 type cache 后备留下迁移点。

## 后续

- 动态内核栈从 KVA 取得“guard + data pages”区间、从 buddy 取得后备页并
  跨三层回滚的增量已经由 [ADR 0025](0025-kva-backed-dynamic-kernel-stacks.md)
  完成；
- 中间页表的根所有权、精确空表判断与 `UnmapPage` 逆序回收已经由
  [ADR 0026](0026-owned-page-table-branch-reclamation.md) 完成；
- 引入通用作用域回滚与 `ResourceSnapshot`，把手写清理序列升级为统一事务；
- v1.2 的 Thread 使用动态内核栈，通过对等测试后删除固定 PCB 栈；
- 多 slab type cache 可以用 KVA 与非连续物理页扩展后备。

## 关联文档

- [ADR 0017：Linux 风格物理内存规模与高半区直映](0017-linux-style-physical-memory-and-direct-map.md)
- [ADR 0022：双位图 buddy 页帧分配器](0022-bitmap-buddy-frame-allocator.md)
- [ADR 0023：固定尺寸类型缓存](0023-heap-backed-fixed-size-type-cache.md)
- [ADR 0025：动态内核栈与安全点回收](0025-kva-backed-dynamic-kernel-stacks.md)
- [ADR 0026：按根所有权回收页表空分支](0026-owned-page-table-branch-reclamation.md)
- [架构说明](../architecture.md)
- [内核模块](../modules/kernel.md)
- [测试策略](../testing.md)
- [日志规范](../logging.md)

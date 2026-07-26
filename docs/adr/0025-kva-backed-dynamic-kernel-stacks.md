# ADR 0025：用 KVA、独立物理页与安全点回收动态内核栈

## 状态

已接受，作为 v1.1 可回收资源基础的第六个完整实现增量。它建立在
[ADR 0024](0024-reclaimable-kernel-virtual-address-allocator.md) 的 KVA
所有权层之上，并把 v0.9 的四块静态进程内核栈全部迁移为可申请、可验证、
可延迟销毁的动态对象。

## 背景

从 Ring 3 进入 Ring 0 时，处理器不能继续信任用户 RSP。当前 `INT 0x80`、
异常和外部中断通过 TSS.RSP0 取得当前执行实体的内核栈，并在栈上保存完整
用户现场。v0.9 为四个固定 PCB 在 Kernel BSS 中预留四块 16 KiB 存储，每块
下方保留一个 not-present guard。该方案足以证明特权级切换，却有三个结构性
问题：

- 栈容量和 PCB 静态数组绑定，不能服务 v1.2 的动态 Thread 生命周期；
- BSS 中的物理页永远不能归还，也无法证明退出后资源恢复；
- 单侧 guard 只能保护向低地址增长的一侧，错误的栈顶或向上越界缺少对称边界。

页帧分配器、KVA 分配器和页表各自只回答一类问题。动态内核栈必须把三层组合
成一个上层事务：

```text
KVA：这六个虚拟页由该栈独占
Buddy/order 0：中间四页各自拥有一个物理后备
Page table：中间四页按 supervisor RW/NX 建立翻译
TSS.RSP0：下一次用户态进入应落到哪个栈顶
Scheduler：哪个槽位和生命周期拥有该栈
```

任何一层失败都不能留下半个可见栈。退出时还存在更严格的时序约束：CPU 正在
使用的栈不能撤销映射，哪怕该 Thread 已经被标记为终止。

## 决策

### 固定布局、动态所有权

每个 `KernelStack` 取得六个连续 KVA 页，但只映射中间四页：

```text
低地址                                                        高地址
┌────────────┬────────┬────────┬────────┬────────┬────────────┐
│ lower guard│ data 0 │ data 1 │ data 2 │ data 3 │ upper guard│
│ not present│ RW / NX│ RW / NX│ RW / NX│ RW / NX│ not present│
└────────────┴────────┴────────┴────────┴────────┴────────────┘
             ↑ mapped begin                     ↑ stack top
             栈从 stack top 向低地址增长
```

稳定规格为：

| 项目 | 数值 |
| --- | ---: |
| KVA 所有权区间 | 6 页，24 KiB |
| 可用栈空间 | 4 页，16 KiB |
| lower guard | 1 页，not-present |
| upper guard | 1 页，not-present |
| 数据页权限 | supervisor、read/write、NX、普通缓存 |
| 当前管理槽容量 | 256 |

`KernelStackTopAddress` 与 upper guard 的起始地址相同。前者表达向下增长栈的
初始 RSP 边界，后者表达该地址以上第一张禁止访问的页；二者语义不同但数值
相同。

KVA 连续只保证虚拟布局连续。四个物理后备逐页用 order 0 申请，不要求物理
连续。内核栈不从 DMA 或大页受益，为它强制申请 order 2 会把无关的连续性
压力带进 Thread 创建，并降低碎片化内存中的成功率。

### 栈槽只保存所有权事实

调用方提供长期存活的 `KernelStack` 槽数组。活动槽保存：

- 六页 `KernelVirtualAddressRange`；
- 四个 `PhysicalFrame`；
- 已提交叶映射数量；
- 活动标志。

管理器的槽索引当前与 PCB 索引一致，但地址、物理页和进程 ID 都不由索引
推导。该接口为 v1.2 把所有者替换成 Thread 留下迁移点；“槽 3”不再等于
“第 3 块静态内存”。

### 创建是跨三层的逆序回滚事务

`TryCreate(slot)` 按以下顺序提交：

1. 验证管理器、槽位、buddy、KVA、页表和统计状态；
2. 从 KVA 申请精确六页区间；
3. 查询六个虚拟页，确认没有陈旧叶映射；
4. 对四个数据页逐次申请 order 0 物理页；
5. 经 direct-map 把新物理页完整清零；
6. 建立 supervisor RW/NX 叶映射；
7. 验证双 guard、四个叶项、物理身份、权限和帧唯一性；
8. 最后才把候选对象写入目标槽并更新活动、累计和峰值统计。

第 4 至第 7 步任意失败时，已建立的叶映射按逆序撤销，已申请帧按逆序清零并
归还，最后释放 KVA 区间。目标槽在事务提交前保持全零，失败不会产生一个调度
器可见的半活动栈。

页表层目前不回收变空的中间表。启动 KVA 自检在动态栈管理器初始化之前已经
暖机同一条首级 KVA 页表路径，因此当前栈创建失败回滚不会把已知的三级页表
基础设施混入栈资源差额。通用中间页表回收仍作为 v1.1 的独立后续增量。

### 完整校验同时检查地址所有权和硬件映射

`KernelStackManager::Validate` 不申请临时内存，并重新证明：

- 活动槽数不超过容量，非活动槽全部清零；
- `creations - destructions == active`；
- 活动映射页和 guard 页统计可由活动栈数唯一推导；
- 峰值不小于当前值；
- 每个六页区间仍是 KVA 的精确活动 `Allocation`；
- lower/upper guard 查询结果均为 `NotMapped`；
- 四个数据页映射到记录的物理帧，页粒度为 4 KiB，权限为 supervisor RW/NX；
- 同一栈内以及任意两栈之间没有重复物理帧；
- buddy 和 KVA 自身的完整校验同时通过。

KVA 新增只读的 `OwnsAllocation` 查询。它区分“这个地址位于窗口中”与“这个
精确区间仍由活动分配描述符持有”。物理分配器也提供
`OwnsAllocation(PhysicalFrame)`：只有精确 order-0 活动块为 true，较大
allocation 的内部页或已经释放的旧帧都为 false。两项查询防止外部错误释放
KVA 或物理帧后，栈管理器只看 PTE 仍存在便继续运行或进入部分销毁。

### 进程创建顺序保证 CR3 看见共享高半栈映射

当前进程页表根复制内核 PML4 的高半入口，并只克隆需要隔离的低半用户路径。
动态栈位于 `0xFFFFC90000000000` 开始的 KVA 高半窗口，因此所有进程根共享
同一组内核上级页表。

创建进程时严格执行：

```text
创建 scheduler 槽
  → 创建动态内核栈
  → 创建并装载用户地址空间
  → 在动态栈顶构造初始 UserPrivilegeFrame
  → 提交 PCB 与创建结果
```

第一个栈先建立共享高半叶项，随后创建的进程根复制该高半入口；以后增加的栈
叶项通过共享上级页表立即对已有进程可见。集成测试会创建四个栈，再建立独立
进程 CR3，逐页确认双 guard 和 supervisor 权限，避免只在内核 CR3 下自证。

### TSS.RSP0 始终来自活动栈对象

调度前不再按 PCB 索引计算常量地址。运行时读取目标 `KernelStack`，确认保存
帧完整位于四页映射区，再把 `KernelStackTopAddress` 写入 TSS.RSP0。初始
176 字节 `UserPrivilegeFrame` 放在最后一个数据页顶部，保持 x86-64 ABI 所需
对齐。

若槽不活动、帧越界、页表切换失败或 TSS 更新失败，调度不会进入 Ring 3。
进程创建结果同时记录 lower guard、stack top 和 upper guard，供整机日志与
QEMU 协议直接验收，而不是让工具从旧静态公式猜测地址。

### 终止与销毁之间设置安全点

用户进程终止时，异常或系统调用处理仍运行在该进程内核栈上。此时可以切回
永久内核 CR3，并销毁用户地址空间；不能撤销当前内核栈。运行时只把调度项
提交为 `Terminated`，再选择下一进程，或通过汇编恢复进入
`OsKernelEnterScheduledProcess` 之前保存的永久启动栈。

只有汇编返回 C++ 调度循环后，`ReapTerminatedKernelStacks` 才执行：

1. 读取当前 RSP；
2. 遍历已终止调度槽；
3. 再次确认当前 RSP 不在待销毁栈的映射区；
4. 验证完整栈对象；
5. 逆序撤销四个叶映射；
6. 清零并归还四个物理页；
7. 最后释放六页 KVA 所有权并清空槽。

如果全部进程连续在栈间切换而没有返回启动栈，终止栈可以暂时保留；最终完成
或“无 Ready、仍有 Blocked”的返回路径会形成安全点并集中回收。延迟回收是
时序正确性，不是泄漏。运行结束后物理帧、KVA 活动页、栈活动数必须全部恢复
运行前基线。

## 日志契约

日志只在管理器初始化完成、进程创建提交和安全点回收完成后输出，不记录逐页
申请或扫描。正常四进程路径的稳定摘要为：

```text
KERNEL_STACK_MANAGER_READY
KERNEL_STACK_SLOT_CAPACITY       = 256
KERNEL_STACK_MAPPED_PAGES        = 4
KERNEL_STACK_GUARD_PAGES         = 2
KERNEL_STACK_SIZE_BYTES          = 16384
PROCESS_KERNEL_STACK_LOWER_GUARD = 0x...
PROCESS_KERNEL_STACK_TOP         = 0x...
PROCESS_KERNEL_STACK_UPPER_GUARD = 0x...
KERNEL_STACK_ACTIVE_STACKS       = 0
KERNEL_STACK_SUCCESSFUL_CREATIONS= 4
KERNEL_STACK_DESTRUCTIONS        = 4
KERNEL_STACK_PEAK_ACTIVE_STACKS  = 4
KERNEL_STACK_PEAK_MAPPED_PAGES   = 16
KERNEL_STACK_RESOURCES_RECLAIMED
```

QEMU 对三个进程栈地址标记各要求恰好四次，对配置和汇总各要求恰好一次，并
检查所有地址非零、累计数和峰值不低于四进程规格。用户态 `#UD` 与 `#PF`
隔离镜像也必须完成单栈创建、异常终止和安全点回收。

## 测试证据

- 单元测试覆盖未初始化、空存储、零/溢出容量、无效依赖、槽越界、重复创建、
  inactive 读取、双 guard、四页清零、权限、帧唯一性、地址包含边界、销毁
  清零、重复销毁、KVA 耗尽、物理页耗尽、陈旧 PTE、物理/KVA 所有权被外部
  释放、原址修复、叶映射篡改与修复后的安全销毁；
- 集成测试组合真实 2-bit 页状态、buddy、KVA 和四级页表，创建四个动态栈，
  在栈顶放置真实 `UserPrivilegeFrame`，再从独立进程 CR3 验证共享高半映射，
  最后逆序销毁并比较物理页和 KVA 基线；
- 固定种子 `0x4B535441434B524E` 执行 100000 步创建/销毁，与 4096 页独立
  best-fit 所有权模型逐步比较地址、槽状态、活动/累计/峰值统计和物理页数，
  每 257 步运行完整校验，最终排空全部 64 个模型槽；
- 64 MiB 与 64 GiB QEMU 使用真实 TSS.RSP0、CR3、抢占、阻塞、异常和汇编
  返回路径，正常四进程与两个用户故障镜像均证明资源回收；
- 三项新测试加入后完整 CTest 集合为 89 项，Kernel ELF 仍通过 freestanding
  符号、段权限和无未解析运行时依赖审计。

## 结果

### 正面结果

- 当前四进程已不再拥有任何静态 BSS Ring 0 栈；
- 每个栈的虚拟地址、物理页和页表映射都有独立、可查询的所有权；
- 双侧 guard、清零、RW/NX 和跨 CR3 可见性成为机器验收契约；
- 退出不在当前栈上自毁，安全点回收把汇编控制流与资源生命周期显式连接；
- v1.2 可把槽所有者从 Process 迁移为 Thread，而无需重写页后备事务。

### 受控限制

- 当前管理器由单 BSP 串行运行时调用，没有锁、per-CPU 栈缓存或远端 TLB
  shootdown；
- 16 KiB 是当前教学负载的固定栈尺寸，尚无按需增长、高水位测量或溢出
  unwind；
- guard 只捕获落入相邻禁止页的访问，跨越 guard 的大跨度错误仍需地址
  sanitizer 类诊断或更宽隔离区；
- 管理槽和 KVA 描述符仍由固定 BSS 数组提供，v1.2 的动态 Thread 容量会把
  元数据迁移到类型化对象层；
- 空中间页表尚不回收，KVA 暖机建立的三级基础设施仍长期保留。

## 被否决方案

### 继续扩大静态栈数组

它能提高并发数字，却不能提供申请失败、退出回收、物理页守恒或 Thread
所有权；容量越大，永久 BSS 浪费越明显。

### 只保留下侧 guard

x86 栈通常向低地址增长，但错误的 RSP 初始化、正偏移写和栈对象越界也可能
触及高侧。增加一个 not-present 页只消耗 KVA，不消耗物理内存，诊断收益
高于地址成本。

### 为每栈强制申请一个 order 2 连续块

实现更短，但把四页虚拟连续错误等同于物理连续。Thread 栈没有 DMA 约束，
逐页后备能在物理碎片化时继续成功，并直接证明页表承担重排职责。

### 在终止处理器上立即销毁当前栈

撤销当前 RSP 所在页后，下一条 C++ 返回、局部变量访问或汇编恢复都会产生
Ring 0 页故障。即使先切 CR3，所有进程根共享同一高半映射，也不能绕过这个
时序问题。

### 只看 PTE 判断栈仍然有效

PTE 不能表达 KVA 软件所有权。KVA 已被错误释放但叶项仍在时，只查页表会让
另一个对象取得同一地址；因此完整校验必须同时证明精确 KVA allocation。

## 后续

- 为页表叶和中间表建立可回收所有权，消除 KVA 暖机留下的三级基础设施；
- 引入通用 `ResourceSnapshot` 与作用域回滚，统一 frame/KVA/page-table/
  heap 的失败差额；
- v1.2 建立 Process/Thread 分离后，让每个 Thread 独占动态内核栈并支持
  退出/reap；
- 用栈高水位和故障注入决定 16 KiB 是否需要按配置调整，而不是凭经验扩大。

## 关联文档

- [ADR 0012：抢占式进程调度](0012-preemptive-process-scheduling.md)
- [ADR 0022：双位图 buddy 页帧分配器](0022-bitmap-buddy-frame-allocator.md)
- [ADR 0024：KVA 所有权分配器](0024-reclaimable-kernel-virtual-address-allocator.md)
- [架构说明](../architecture.md)
- [内核模块](../modules/kernel.md)
- [测试策略](../testing.md)
- [调试手册](../debugging.md)
- [日志规范](../logging.md)

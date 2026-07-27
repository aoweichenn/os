# ADR 0035：匿名 VMA、按需分页与用户堆

## 状态

已接受并在 v1.8 实现。

## 背景

v1.7 的每个用户地址空间只有两类事实：已经装入的 ELF 页和已经装入的固定
256 KiB 用户栈页。页表同时被迫承担“这个地址在逻辑上允许访问”和“这个地址
当前已经有物理页”两种含义。因此，not-present `#PF` 只能被当作进程错误，
无法区分以下情况：

- 用户为将来使用预留了 32 MiB 匿名地址，但尚未触及任何页；
- `brk` 已扩大进程数据区，但分配器只写到了其中一页；
- 用户栈正常向低地址推进到了下一页；
- 用户访问了永久 guard、只读页、空洞或完全不属于它的地址。

历史 Unix 的 `brk` 为连续数据段提供增长边界，后来出现的 `mmap` 则把不连续
对象放入统一虚拟地址空间。现代系统不会在预留虚拟范围时立即为每页消耗
物理内存；硬件页表表示当前驻留和硬件权限，VMA 等软件元数据表示允许出现
什么页以及出现时应如何构造。v1.8 需要先建立这层区别，后续文件页故障和
fork/COW 才有可靠的策略来源。

本阶段仍遵守项目的启动边界：QEMU 只模拟 x86-64 CPU、RAM、串口、PIT、
PIC、PS/2、IDE 和 `fw_cfg` 等硬件。ROM、Stage 1、页表、异常入口、VMA、
系统调用、用户分配器和测试程序全部由项目实现。

## 决策一：VMA 是地址空间意图，PTE 是驻留事实

每个 `UserAddressSpace` 拥有一个 `VirtualMemoryMap`。VMA 使用页对齐的半开
区间 `[begin, end)`，并显式保存：

- `ExecutableImage`、`Anonymous`、`ProgramBreak` 或 `UserStack` 来源；
- readable、writable、executable 三项软件权限；
- 与相邻区域的有序关系。

任何两个活动 VMA 都不得重叠。只有来源与三项权限全部相同的相邻区域才自动
合并。移除完整区间、前缀或后缀不需要额外描述符；从一个区域中间移除时会
先取得新的描述符，再修改旧区域。若描述符或单进程区域上限已经耗尽，操作在
修改前失败，原映射保持不变。

实现使用一个容量 8192 的全局描述符池和每地址空间唯一 owner identifier。
单进程最多拥有 4096 个 VMA。当前用有序双向索引链表换取实现可审计性：
查找和 first-gap 是线性复杂度，但没有按潜在地址页建立巨型位图，也不会把
宿主容器带入 freestanding Kernel。后续若压力证据表明需要树结构，可以在
保持 `VirtualMemoryMap` 契约不变的前提下替换索引。

池持续记录 capacity、active/free、peak、acquire 和 release。地址空间销毁
必须归还自己拥有的全部描述符；进程总资源快照又在正常工作负载前后核对
池的活动数与累计守恒。

## 决策二：冻结 v1.8 用户虚拟地址布局

本阶段公开以下固定宽度 ABI：

| 区域 | 范围或限制 | 语义 |
| --- | --- | --- |
| ELF 与 program break | ELF 低地址至 `0x0000000060000000` | ELF 仍 eager；`brk` 从最高 `PT_LOAD` 页尾开始 |
| 匿名窗口 | `[0x0000000060000000, 0x0000000080000000)` | 512 MiB，first-fit 自动选址或不覆盖的 fixed 选址 |
| 用户栈 | `[0x00007FFFFF7F0000, 0x00007FFFFFFF0000)` | 最多 8 MiB，只提交实际需要的连续低端 |
| 栈 guard | `0x00007FFFFF7EF000` | 永久不创建 VMA、不创建 PTE |
| 用户 heap | 最多 8 MiB | 用户分配器在 program break 上自行实施的软上限 |

地址是学习 ABI，不冒充 Linux 的完整进程布局。固定匿名窗口便于审计碰撞、
first-gap 和耗尽；8 MiB 栈与 heap 足以覆盖当前程序，同时让错误递归和失控
分配在明确边界停止。

## 决策三：匿名映射只预留，不立即分配物理页

系统调用号 39..42 分别为：

| 编号 | 接口 | 成功结果 |
| ---: | --- | --- |
| 39 | `MapAnonymousMemory` | 映射起始地址 |
| 40 | `UnmapMemory` | `0` |
| 41 | `SetProgramBreak` | 当前或更新后的 break 地址 |
| 42 | `GetVirtualMemoryStatistics` | 把 112 字节固定结构写回用户空间 |

长度向上取整到 4 KiB。自动映射只接受地址 `0`，在匿名窗口中选择第一个满足
页对齐和长度的空洞。fixed 映射要求非零页对齐地址；它不是 Linux
`MAP_FIXED` 的破坏性替换语义，任何重叠都返回 `AddressInUse`。未知 flag、
零长度、溢出、越界和错误对齐均在修改元数据前拒绝。

权限只接受合法子集，并继续执行 W^X：

- `NONE`、`R`、`R|W` 或 `R|X` 可以登记；
- `W` 或 `X` 缺少 `R` 时拒绝；
- `W|X` 以及包含未知位的组合拒绝。

成功 `mmap` 和扩大 `brk` 只创建 VMA，不申请 frame，不创建新的页表分支。
首次合法访问触发用户态 not-present `#PF`，Kernel 才按以下顺序提交一页：

```text
CR2 + page-fault error code
  -> 必须来自 CPL3
  -> reserved-bit 与 present violation 必须拒绝
  -> 查找包含 fault page 的 VMA
  -> 按读/写/取指核对 VMA 权限
  -> 对 stack 额外核对连续增长与用户 RSP
  -> 从 buddy 取得一个 frame
  -> 建立所需页表分支和用户 PTE
  -> 通过高半区 direct-map 把整页清零
  -> 更新驻留、fault 与峰值统计
  -> 返回原用户指令重试
```

若清零或映射提交失败，刚建立的页与空页表分支立即回滚。匿名页的首次读必须
得到零，写入内容在 VMA 生命周期内由同一 PTE 保存。

## 决策四：栈增长必须同时满足 VMA、连续性和 RSP 证据

装载 ELF 时先为完整 8 MiB 栈登记一个 RW/NX VMA，但只映射参数布局实际覆盖
的顶部页面。一次合法栈 fault 必须同时满足：

1. fault page 属于 `UserStack` VMA；
2. fault page 恰好位于当前 committed bottom 的下一页；
3. fault 地址与异常现场中的用户 RSP 相邻，最大容忍差为 64 KiB；
4. fault page 不低于栈 VMA 底部；
5. 访问权限与 RW/NX 策略一致。

这拒绝“在栈保留区中随机跳到很低地址”以及对永久 guard 的访问。Kernel 的
`CopyFromUser`/`CopyToUser` 可以按需解析匿名和 program-break 页，但不替
用户伪造栈增长，因为 Kernel copy 没有一条可信的用户栈推进指令作为证据。

## 决策五：撤销同时回收数据页和空页表分支

`munmap` 只允许撤销 `Anonymous` 区域，不能借同一接口破坏 ELF、program
break 或栈。缩小 `brk` 只撤销 `ProgramBreak` 区域。两条路径均执行：

```text
预检完整范围与 VMA kind
  -> VMA remove / trim / split
  -> 查询每个可能驻留的 PTE
  -> 释放实际存在的用户 frame
  -> 清除 PTE 并使对应地址的 TLB 状态失效
  -> 自底向上释放变空的 PT/PD/私有 PDPT
  -> 更新 resident、unmap 和 reclaimed-table 计数
```

未触及页没有 PTE，因此不会产生虚构的释放。地址空间退出和 exec 旧映像回收
继续销毁整个进程页表，再销毁全部 VMA。最终 VMA active 为零、acquire 与
release 增量相同，页帧、页表、KVA、内核栈、文件描述和 VFS context 也必须
回到工作负载前基线。

## 决策六：用户 heap 是独立、可测试的 C++20 组件

Kernel 只提供 program break；`UserHeap` 在 Ring 3 中自行管理块。它不调用
libc，也不使用宿主 `malloc`。布局采用：

- 16 字节块对齐；
- 64 字节显式 `BlockHeader`；
- 物理相邻块链由 previous offset 与容量推导；
- 空闲块使用双向 offset 链；
- first-fit 选择、按需 split、释放时向前和向后 coalesce；
- header signature、状态、请求尺寸和全链 `Validate()`。

heap 以配置的页粒度和 growth quantum 调用 `SetProgramBreak`，最大容量由
用户程序显式给出，当前公开上限为 8 MiB。零字节、整数溢出、耗尽、错误
break 返回、外部指针、重复释放和元数据损坏都有不同状态。失败申请不能改变
已有活动 allocation。

分配器不尝试在每次 free 后缩小 break。这样先把分配、拆分、合并和失败原子性
单独闭合；归还尾部整页、线程安全 arena 和 size class 属于后续用户运行时
优化，不改变本阶段正确性。

## 决策七：日志只记录可解释的阶段边界

每次 demand fault 都打印串口会放大 I/O、改变 TCG 调度并冲掉真正异常。
Kernel 因此只在累计量达到二次幂时输出采样日志：

```text
[OS][KERNEL][VM] DEMAND_FAULT_COUNT=...
[OS][KERNEL][VM] STACK_GROWTH_COUNT=...
```

用户探针只在一个完整不变量通过后输出一次聚合标记。进程工作负载结束后再
输出 VMA 描述符容量、峰值、申请、释放和最终 active。来宾 PIT 毫秒与宿主
`[QEMU][T+...ms]` 时间戳继续保留；runner 同时限制总超时和静默超时。

## 验收证据

- VMA 单元测试覆盖初始化、排序、重叠、三向合并、中段拆分、kind guard、
  first-gap、元数据耗尽失败原子性和 destroy。
- 固定种子参考模型执行 100000 步 map/unmap/split/merge，并逐步核对区间、
  统计和池守恒。
- 用户 heap 单元测试覆盖增长、拆分、复用、双向合并、耗尽、错误指针和重复
  释放；随机模型执行 100000 步申请/释放并逐字节验证活动载荷。
- 页表/VMA 集成测试重复 128 个完整地址空间生命周期，证明预留不耗 frame、
  首次触页才建立映射、中段撤销回收页表分支、销毁回到 frame 与描述符基线。
- Ring 3 memory probe 预留 32 MiB 只触及远隔页，验证零页、持久写、
  split/remap/unmap、2 MiB `brk`、递归栈增长与 5000 步用户 heap。
- 独立 guard probe 与只读匿名页 probe 必须分别以用户 `#PF` 被 Kernel
  终止；Kernel 自身不得 panic。
- 64 MiB、256 MiB 和 64 GiB QEMU 配置均运行同一完整 PID1 工作负载。

## 被拒绝的方案

### 在 `mmap` 时立即分配全部物理页

实现简单，但继续混淆虚拟承诺与物理驻留，无法成为文件页故障和 COW 的基础，
也会让一个未触及的 512 MiB 映射无意义地耗尽 RAM。

### 只检查 PTE、不保存 VMA

not-present PTE 本身没有来源、权限或增长政策。把所有 not-present fault
都当匿名零页会把 guard、空洞和任意地址越界变成合法内存。

### 使用宿主 STL 容器或 libc allocator

它们依赖当前系统没有提供的运行时和异常语义，也会隐藏本阶段最需要学习的
所有权、容量与失败路径。

### 让 fixed 映射隐式覆盖现有区域

破坏性替换需要跨 kind 撤销、PTE 回收和失败回滚事务。v1.8 先冻结
“重叠明确失败”，避免把半完成替换伪装成 `MAP_FIXED`。

### 允许栈 VMA 内任意 not-present 页自动出现

这会让远离 RSP 的越界写绕过 guard 的教学目的。连续 committed bottom 与
RSP 窗口提供了可解释、可测试的最小策略。

## 后果与阶段边界

v1.8 之后，地址空间第一次拥有独立的“允许映射集合”和“当前驻留集合”，
匿名页、program break、受控栈和用户 heap 已闭合，进程退出/exec 也覆盖
VMA 元数据守恒。

本阶段仍不包含：

- file-backed VMA、按需 ELF、page cache、`MAP_PRIVATE` 或 `MAP_SHARED`；
- fork、COW、匿名页引用计数或 swap；
- `mprotect`、`mremap`、共享匿名映射、ASLR 或 overcommit；
- 用户线程并发分配、per-thread arena 或完整 libc `malloc` ABI。

下一阶段 v1.9 在同一 VMA/page-fault 边界上增加文件来源与有界 clean page
cache；v1.10 才增加 fork/COW。

## 关联

- [v1.8 发布记录](../releases/v1.8.md)
- [v1.8 学习章](../learning/16-v1.8-anonymous-vma-demand-paging.md)
- [系统架构](../architecture.md)
- [用户模块](../modules/user.md)
- [Kernel 模块](../modules/kernel.md)
- [测试策略](../testing.md)
- [日志协议](../logging.md)

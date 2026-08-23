# 系统日志与前台终端规范

`0x20000..0x9FFFF` 的只追加内存区是系统诊断的权威启动观测接口，也是 QEMU
集成测试的稳定证据。VGA 是用户前台，不再承载进入 Shell 后的普通 Kernel
统计。日志必须同时满足“足够解释状态”和“不会淹没真正的故障”。

## 输出路由

| 来源 | 内存日志/转录 | VGA 屏幕 | 宿主文件 |
| --- | --- | --- | --- |
| Firmware/Stage 1/Kernel 启动诊断 | 是 | 终端激活前可见 | QEMU 工具导出 |
| Kernel 运行期诊断 | 是 | 否 | QEMU 工具导出 |
| TTY stdout/stderr | 是 | 是 | QEMU 工具导出 |
| panic | 尽力写入 | 始终写入 | 会话仍存活时导出 |

共享头版本 3 的 `output_mode` 初值为 0。Kernel 准备运行用户环境时先清屏、
复位光标，再提交模式 1；此后的诊断写入不能移动前台光标。日志溢出会令普通
诊断失败并置位 overflow，但 panic 仍继续渲染，不能因为观测缓冲耗尽而隐藏
致命现场。

自动测试用 QMP `pmemsave` 读取该区域；`qemu-display` 默认持续导出到
`build/developer/qemu-display.log`，可用 `--guest-log-file` 改路径。目标系统
不调用 QMP，复位后内存记录丢失；v2.3 以前不能让早期日志或 panic 依赖 VFS。

## 日志分层

| 层级 | 目的 | 允许频率 | 示例 |
| --- | --- | --- | --- |
| `MILESTONE` | 记录不可逆阶段边界 | 每个阶段一次 | `[OS][STAGE1] GDT_READY` |
| `DEVICE` | 记录设备初始化结果 | 每个设备一次 | `[OS][FIRMWARE] VGA_READY` |
| `FAILURE` | 记录停止原因 | 每条故障路径一次 | `[OS][FIRMWARE] STAGE1_CHECKSUM_INVALID` |
| `TRACE` | 调试内部细节 | 默认关闭 | 轮询计数、寄存器快照 |

当前系统日志格式使用稳定的组件和事件名代替显式等级字段：固件事件使用
`[OS][FIRMWARE]`，Stage 1 事件使用 `[OS][STAGE1]`，内核事件使用
`[OS][KERNEL]`。事件名必须是大写下划线形式，并且纳入 QEMU 测试的顺序或
禁止标记集合。

## 时间戳方案

来宾时间采用启动后建立的单调相对时间，不打印宿主机墙钟，也不在尚未初始化时
读取 RTC。v0.7 由自研 PIT/IRQ0 维护 64 位滴答，并在时钟自检边界输出：

```text
[OS][KERNEL] TIMER_TICKS=0x0000000000000010
[OS][KERNEL] MONOTONIC_MILLISECONDS=0x000000000000000F
```

固件的 `CLOCK_READY` 只表示早期 PIT 配置动作完成，不是耗时数据。内核会
重新编程 PIT，并且只有真实接收至少 16 个 IRQ0 后才输出单调毫秒。因为复位到
内核接管之间没有连续软件溢出计数，不能把内核 tick 伪装成“从复位开始”的时间。

QEMU 验收工具另用宿主单调时钟记录每条 VGA 行首次出现在追加区的时刻，格式为
`[QEMU][T+000123ms]`。该字段由 QMP 捕获线程在观察到完整换行时生成，只描述
测试进程观察到日志的时间，不冒充来宾 PIT 时间。捕获器在最终必需里程碑到达后主动结束，
未完成路径最多等待十五秒；两种路径都必须显式终止并等待回收 QEMU，避免后台残留
模拟器进程。

## 防刷屏规则

- 轮询循环、每个扇区、每个字节和每次寄存器读写都不得默认打印。
- 成功路径只打印阶段完成事件；详细过程通过单元测试、随机测试或离线审计观察。
- 故障路径只打印一次稳定故障事件，然后停止或转入明确的故障处理状态。
- 若未来启用 `TRACE`，必须有编译期或启动期开关，并设置最大事件数；达到预算后只打印一次 `TRACE_LIMIT_REACHED`。
- 日志文本不得依赖本地化、时间戳或不稳定地址，保证测试和文档可复现。

动态物理内存只在初始化事务提交后输出一次容量摘要：

```text
[OS][KERNEL] MEMORY_MANAGED_PHYSICAL_LIMIT=0x...
[OS][KERNEL] PHYSICAL_ADDRESS_BITS=0x...
[OS][KERNEL] VIRTUAL_ADDRESS_BITS=0x...
[OS][KERNEL] FIVE_LEVEL_PAGING_SUPPORTED=0x...
[OS][KERNEL] FRAME_STATE_STORAGE_ADDRESS=0x...
[OS][KERNEL] FRAME_STATE_STORAGE_BYTES=0x...
[OS][KERNEL] DIRECT_MAP_BASE=0xFFFF888000000000
[OS][KERNEL] DIRECT_MAP_MAPPED_BYTES=0x...
[OS][KERNEL] DIRECT_MAP_2M_PAGES=0x...
[OS][KERNEL] DIRECT_MAP_4K_PAGES=0x...
[OS][KERNEL] HIGH_MEMORY_TEST_ADDRESS=0x...
[OS][KERNEL] HIGH_MEMORY_VALIDATION_COMPLETE
```

这些字段不是逐 E820 项或逐页映射 TRACE。`MEMORY_MANAGED_PHYSICAL_LIMIT`
可以大于 `MEMORY_MANAGED_BYTES`，因为物理地址洞占用页号却不是 RAM；
`DIRECT_MAP_MAPPED_BYTES` 只统计完整 type 1 RAM 页。64 MiB 兼容配置的
高内存地址为零，表示策略检查完成但容量不足以执行 4 GiB 以上读写；64 GiB
主规格必须报告非零且不低于 `0x0000000100001000` 的地址。

buddy 同样只在启动事务和目标自检都提交后输出一次快照：

```text
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
```

`BUDDY_ACTIVE_BLOCKS` 包括仍被内核页表、heap 后备和固定映射持有的 order 0
页，正常情况下不是零。`BUDDY_SELF_TEST_PASSED` 的含义是 order 3 自检相对
进入前的活动块与页统计恢复基线，不是“整个内核不持有物理页”。64 GiB 主规格
额外要求存储至少 8 MiB、最大阶至少 24、自检地址高于 4 GiB。

这些统计不进入热路径日志。申请、逐阶分裂、释放和逐阶合并只累计计数；
`ValidateBuddy` 结果由单一自检里程碑表示。这样既能区分“从未发生拆分”和
“生命周期闭合”，又不会让页表建立或进程退出刷屏。

页表空分支回收同样只在两段 KVA 事务和分配器校验全部完成后输出一次：

```text
[OS][KERNEL] PAGE_TABLE_RECLAIM_READY
[OS][KERNEL] PAGE_TABLE_RECLAIMED_LEVEL1_TABLES=0x0000000000000002
[OS][KERNEL] PAGE_TABLE_RECLAIMED_LEVEL2_TABLES=0x0000000000000002
[OS][KERNEL] PAGE_TABLE_RECLAIMED_LEVEL3_TABLES=0x0000000000000000
[OS][KERNEL] PAGE_TABLE_RETAINED_SHARED_LEVEL3_TABLES=0x0000000000000001
[OS][KERNEL] PAGE_TABLE_RECLAIM_SELF_TEST_PASSED
```

第一段单页暖机回收一张 PT 和一张 PD；第二段四叶事务在最后一个叶撤销时
再回收一张 PT 和一张 PD。三级计数为零不是遗漏，而是
`KernelShared` 根必须保留仍可能被进程 PML4 项引用的 PDPT。保留计数精确
为一，把安全共享边界与无法回收的泄漏区分开。

`MapPage`、`QueryPage`、`UnmapPage`、逐项空表扫描和失败回滚均不打印。
这些路径会被动态栈、进程退出和十万步随机模型高频调用；逐次日志既改变时序，
也会淹没真正的事务边界。失败由上层单个
`MEMORY_INITIALIZATION_FAILED=0x...` 状态承载，细分原因通过
`PageTableStatus`、单元故障注入和 GDB 检查获得。

固定尺寸类型缓存也只在启动自检完整提交后输出一次快照：

```text
[OS][KERNEL] TYPE_CACHE_READY
[OS][KERNEL] TYPE_CACHE_OBJECT_SIZE_BYTES=0x0000000000000040
[OS][KERNEL] TYPE_CACHE_OBJECT_ALIGNMENT_BYTES=0x0000000000000040
[OS][KERNEL] TYPE_CACHE_SLOT_STRIDE_BYTES=0x0000000000000040
[OS][KERNEL] TYPE_CACHE_CAPACITY=0x0000000000000020
[OS][KERNEL] TYPE_CACHE_BACKING_STORAGE_BYTES=0x0000000000000840
[OS][KERNEL] TYPE_CACHE_ACTIVE_OBJECTS=0x0000000000000000
[OS][KERNEL] TYPE_CACHE_FREE_OBJECTS=0x0000000000000020
[OS][KERNEL] TYPE_CACHE_SUCCESSFUL_ALLOCATIONS=0x0000000000000021
[OS][KERNEL] TYPE_CACHE_RELEASES=0x0000000000000021
[OS][KERNEL] TYPE_CACHE_PEAK_ACTIVE_OBJECTS=0x0000000000000020
[OS][KERNEL] TYPE_CACHE_SELF_TEST_PASSED
```

这里的配置描述启动代表缓存，不是所有未来对象类型的全局固定规格。
`BACKING_STORAGE_BYTES=0x840` 由 4 字节活动位图、对齐填充和 32 个 64 字节
槽共同得到。申请、释放、位图修改和空闲链操作不打印逐对象日志；成功/释放
累计值 33 表示“先耗尽 32 槽，再复用 1 槽”，活动数精确为零与空闲数 32
证明缓存自身闭合。只有随后把唯一后备块归还通用堆并再次校验堆，才输出
`TYPE_CACHE_SELF_TEST_PASSED`。

KVA 同样采用一次提交后的有界快照，不记录每次区间扫描和页映射：

```text
[OS][KERNEL] KVA_ALLOCATOR_READY
[OS][KERNEL] KVA_WINDOW_BASE=0xFFFFC90000000000
[OS][KERNEL] KVA_WINDOW_SIZE_BYTES=0x0000200000000000
[OS][KERNEL] KVA_DESCRIPTOR_CAPACITY=0x0000000000000400
[OS][KERNEL] KVA_ACTIVE_DESCRIPTORS=0x0000000000000001
[OS][KERNEL] KVA_FREE_PAGES=0x00000001FFFFFFFF
[OS][KERNEL] KVA_ALLOCATED_PAGES=0x0000000000000000
[OS][KERNEL] KVA_RESERVED_PAGES=0x0000000000000001
[OS][KERNEL] KVA_SUCCESSFUL_ALLOCATIONS=0x0000000000000003
[OS][KERNEL] KVA_RELEASES=0x0000000000000003
[OS][KERNEL] KVA_PEAK_ALLOCATED_PAGES=0x0000000000000006
[OS][KERNEL] KVA_LARGEST_FREE_RANGE_PAGES=0x00000001FFFFFFFF
[OS][KERNEL] KVA_SELF_TEST_VIRTUAL_ADDRESS=0xFFFFC90000008000
[OS][KERNEL] KVA_SELF_TEST_PHYSICAL_ADDRESS=0x...
[OS][KERNEL] KVA_SELF_TEST_MAPPED_PAGES=0x0000000000000004
[OS][KERNEL] KVA_SELF_TEST_GUARD_PAGES=0x0000000000000002
[OS][KERNEL] KVA_SELF_TEST_PASSED
```

窗口包含 8589934592 个 4 KiB 页，首个页是永久软件保留区，因此最终 free 和
最大连续空闲均为 8589934591。前三次成功申请分别是页表暖机、六页 KVA
主事务和资源生命周期自检中的六页真实动态栈，三次均已释放；暖机现在用于
验证共享 PDPT 边界，而不是建立永久泄漏的 PT/PD。
峰值六页包含两个故意不映射的 guard。物理地址由本次 QEMU 内存图
与 buddy 状态决定，协议只要求非零、页对齐并存在字段，不硬编码具体页帧。
`KVA_SELF_TEST_PASSED` 同时表示四个数据页的 RW/NX 映射、真实写回、逆序
unmap/物理页/KVA 回收和统计校验通过。

资源生命周期协议只在一次完整事务结束后打印六行，不为引用的每次增减或
每个补偿动作打印热路径日志：

```text
[OS][KERNEL] RESOURCE_LIFECYCLE_READY
[OS][KERNEL] RESOURCE_SNAPSHOT_TRACKED_FIELDS=0x000000000000001A
[OS][KERNEL] RESOURCE_SNAPSHOT_CHANGED_FIELDS=0x0000000000000000
[OS][KERNEL] REFERENCE_COUNTER_SELF_TEST_PASSED
[OS][KERNEL] SCOPE_ROLLBACK_SELF_TEST_PASSED
[OS][KERNEL] RESOURCE_SNAPSHOT_SELF_TEST_PASSED
```

`TRACKED_FIELDS=0x1A` 表示当前协议比较 26 个稳定状态字段；它们覆盖 frame、
buddy、heap、KVA、动态内核栈和已经为后续对象预留的六个计数位置。累计
申请/释放量不进入差异判断，否则一个已经完整回收的事务仍会被误报为泄漏。
自检会创建并销毁一个真实动态内核栈，再验证引用计数状态机、作用域回滚动作
和跨层快照。只有守恒式有效、变化掩码为零且三个子检查全部通过，才一次性
输出这些标记。

全部进程执行结束还有一条独立的长期边界：

```text
[OS][KERNEL] PROCESS_RESOURCES_RECLAIMED
[OS][KERNEL] RESOURCE_SNAPSHOT_PROCESS_LIFECYCLE_PASSED
```

第二行不是第一行的别名：它比较进程创建前和所有安全点回收完成后的完整
26 字段快照。任意字段变化都会返回资源泄漏状态，禁止输出通过标记。

动态内核栈日志分为“配置提交、每进程稳定身份、运行结束汇总”三段：

```text
[OS][KERNEL] KERNEL_STACK_MANAGER_READY
[OS][KERNEL] KERNEL_STACK_SLOT_CAPACITY=0x0000000000000200
[OS][KERNEL] KERNEL_STACK_MAPPED_PAGES=0x0000000000000004
[OS][KERNEL] KERNEL_STACK_GUARD_PAGES=0x0000000000000002
[OS][KERNEL] KERNEL_STACK_SIZE_BYTES=0x0000000000004000

[OS][KERNEL] PROCESS_KERNEL_STACK_LOWER_GUARD=0x...
[OS][KERNEL] PROCESS_KERNEL_STACK_TOP=0x...
[OS][KERNEL] PROCESS_KERNEL_STACK_UPPER_GUARD=0x...

[OS][KERNEL] KERNEL_STACK_ACTIVE_STACKS=0x0000000000000000
[OS][KERNEL] KERNEL_STACK_SUCCESSFUL_CREATIONS=0x...
[OS][KERNEL] KERNEL_STACK_DESTRUCTIONS=0x...
[OS][KERNEL] KERNEL_STACK_PEAK_ACTIVE_STACKS=0x...
[OS][KERNEL] KERNEL_STACK_PEAK_MAPPED_PAGES=0x...
[OS][KERNEL] KERNEL_STACK_RESOURCES_RECLAIMED
```

配置只在管理器初始化和完整校验通过后打印一次。每进程三行只在 KVA、四个
物理页、页表映射、用户地址空间和初始保存帧全部提交后打印；`TOP` 与
`UPPER_GUARD` 数值相同，但分别表达初始 RSP 边界和上保护页起点。申请帧、
清零、逐页 map/unmap、扫描槽位和安全点遍历均不打印，避免调度与退出路径
刷屏。

运行结束汇总在汇编回到永久启动栈、全部 Exited Thread 的栈安全回收之后
生成。累计值包含资源事务、当前内存档位的 Process/Thread 容量事务和正常
用户 Thread，因此由 64 MiB、256 MiB、64 GiB 配置分别决定；协议要求创建
等于销毁、峰值至少等于该档 Thread 容量、最终活动数为零，而不硬编码单一
累计值。用户 `#UD` 与 `#PF` 隔离镜像使用同一字段，但只执行各自路径所需
的栈生命周期。只有 frame、KVA 和管理器三组
运行前后不变量同时成立，才输出 `KERNEL_STACK_RESOURCES_RECLAIMED`。QEMU
协议对正常路径的配置/汇总次数和每进程地址次数做精确计数，并对三个地址和
峰值做十六进制下界检查。

## v0.11 验收（历史基线）

以下日志记录 v0.11 完成时的正常启动边界，保留用于历史回归。v1.0 的当前
Shell 与控制台协议见文末。

```text
[OS][FIRMWARE] RESET
[OS][FIRMWARE] VGA_READY
[OS][FIRMWARE] STAGE1_HEADER_VALID
[OS][FIRMWARE] STAGE1_LOADED
[OS][STAGE1] ENTERED
[OS][STAGE1] GDT_READY
[OS][STAGE1] PROTECTED_MODE
[OS][STAGE1] PAGE_TABLES_READY
[OS][STAGE1] PAE_READY
[OS][STAGE1] LME_READY
[OS][STAGE1] PAGING_ENABLED
[OS][STAGE1] LONG_MODE
[OS][STAGE1] MEMORY_MAP_READY
[OS][STAGE1] KERNEL_HEADER_VALID
[OS][STAGE1] KERNEL_PAYLOAD_VALID
[OS][STAGE1] KERNEL_ELF_VALID
[OS][STAGE1] KERNEL_SEGMENTS_LOADED
[OS][STAGE1] BOOT_INFO_READY
[OS][STAGE1] KERNEL_TRANSFER
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
[OS][KERNEL] HEAP_CAPACITY_BYTES=0x0000000000080000
[OS][KERNEL] HEAP_ACTIVE_ALLOCATIONS=0x0000000000000000
[OS][KERNEL] HEAP_PEAK_CONSUMED_BYTES=0x...
[OS][KERNEL] HEAP_LARGEST_FREE_ALLOCATION_BYTES=0x...
[OS][KERNEL] HEAP_SELF_TEST_PASSED
[OS][KERNEL] PROCESS_RUNTIME_READY
[OS][KERNEL] USER_ELF_VALID
[OS][KERNEL] USER_ENTRY=0x0000000040000000
[OS][KERNEL] USER_MAPPED_PAGES=0x...
[OS][KERNEL] USER_STACK_READY
[OS][KERNEL] PROCESS_ID=0x...
[OS][KERNEL] PROCESS_CR3=0x...
[OS][KERNEL] PROCESS_KERNEL_STACK_TOP=0x...
[OS][KERNEL] LEGACY_INTERRUPT_ROUTING_READY
[OS][KERNEL] PIC_READY
[OS][KERNEL] PIC_MASK=0x000000000000FFFC
[OS][KERNEL] PIT_READY
[OS][KERNEL] PIT_DIVISOR=0x00000000000004A9
[OS][KERNEL] PIT_FREQUENCY_HZ=0x00000000000003E8
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
[OS][USER] INVALID_POINTER_REJECTED
[OS][USER] UNKNOWN_SYSCALL_REJECTED
[OS][USER] HELLO_FROM_RING3
[OS][USER][PID2] WORKER_STEP_1
[OS][USER] ADDRESS_SPACE_ISOLATED
[OS][KERNEL] SCHEDULER_CREATED_PROCESSES=0x0000000000000004
[OS][KERNEL] SCHEDULER_TERMINATED_PROCESSES=0x0000000000000004
[OS][KERNEL] SCHEDULER_TIMER_TICKS=0x...
[OS][KERNEL] SCHEDULER_PREEMPTIONS=0x...
[OS][KERNEL] SCHEDULER_DISPATCHES=0x...
[OS][KERNEL] USER_EXIT_CODE=0x0000000000000000
[OS][KERNEL] USER_SYSCALL_COUNT=0x0000000000000006
[OS][KERNEL] PROCESS_RUN_TICKS=0x...
[OS][KERNEL] PROCESS_DISPATCH_COUNT=0x...
[OS][KERNEL] USER_TERMINATED
[OS][KERNEL] PROCESS_RESOURCES_RECLAIMED
[OS][KERNEL] SCHEDULER_COMPLETE
[OS][KERNEL] USER_RETURNED_TO_KERNEL
[OS][KERNEL] FILE_SIZE=0x...
[OS][KERNEL] LOAD_SEGMENTS=0x0000000000000003
[OS][KERNEL] READY
[OS][KERNEL] KEYBOARD_SCANCODE=0x000000000000001E
[OS][KERNEL] KEYBOARD_EVENT=A_PRESSED
```

文件长度和加载段数使用固定 16 个十六进制数字的宽度，便于人工对照 ELF，也避免
十进制转换代码进入最早内核。数值日志只在结构验证完成后输出。

测试必须验证顺序、失败标记唯一性以及失败路径不会继续输出后续成功标记。
Kernel 读取阶段分别使用 `KERNEL_ATA_TIMEOUT`、`KERNEL_ATA_ERROR`、
`KERNEL_HEADER_INVALID`、`KERNEL_CHECKSUM_INVALID` 和
`KERNEL_ELF_INVALID`；它们不能合并，否则硬件事务、容器完整性和 ELF 语义
三种问题无法区分。

异常日志遵循“一个异常、一份有界现场、一次终止标记”：

```text
[OS][KERNEL] EXCEPTION
[OS][KERNEL] EXCEPTION_VECTOR=0x000000000000000E
[OS][KERNEL] EXCEPTION_ERROR_CODE=0x0000000000000000
[OS][KERNEL] EXCEPTION_RIP=0x...
[OS][KERNEL] EXCEPTION_CS=0x0000000000000008
[OS][KERNEL] EXCEPTION_RFLAGS=0x...
[OS][KERNEL] PAGE_FAULT_ADDRESS=0x0000000004000000
[OS][KERNEL] PANIC
```

`PAGE_FAULT_ADDRESS` 只在向量 14 出现。panic 设置不可重入状态后才写现场；
递归异常直接停机，不重复输出。RIP 和 RFLAGS 会随链接与处理器保存语义变化，
系统测试只要求字段存在，不把不稳定地址硬编码为协议。向量、错误码、CR2 和
`PANIC` 是稳定验收字段。

内存日志采用“一个阶段边界 + 少量汇总值”，不逐页打印：

- `MEMORY_MAP_VALID` 后只打印条目数、描述字节、可用字节和本阶段管理字节。
- `FRAME_ALLOCATOR_READY` 后打印 free、allocated、reserved 三类页总数；
  `BUDDY_ALLOCATOR_READY` 后只追加一次块级快照和自检结果，不逐次记录操作。
- `PAGING_READY` 只附带一个根物理地址，页级权限由
  `MEMORY_PERMISSIONS_VALID` 汇总。
- `HEAP_READY` 后只打印容量、自检结束活动数、峰值和最大连续空闲负载；
  `HEAP_SELF_TEST_PASSED` 表示真实写回、逆序释放、双向合并和一致性检查均
  已完成。禁止逐次记录分配、分裂或释放。
- `TYPE_CACHE_READY` 与 `KVA_ALLOCATOR_READY` 分别打印一次配置、最终状态和
  峰值；前者不逐对象打印，后者不逐区间、逐页表项或逐 TLB 失效打印。只有
  跨层回收全部提交后才输出对应 `SELF_TEST_PASSED`。
- `KERNEL_STACK_MANAGER_READY` 只打印布局；进程提交后各打印一组三地址，
  安全点回收完成后只打印一次累计/峰值与资源回收标记，不在上下文切换或
  定时器 IRQ 中记录栈事件。

写保护故障镜像使用 `FAULT_INJECTION=WRITE_PROTECTION`，随后必须报告向量
14、错误码 `0x3` 和 CR2=`0xFFFF800000100000`。正常日志禁止出现任何
`FAULT_INJECTION`、`EXCEPTION` 或 `PANIC`。

设备日志遵循“初始化一次、热路径计数、消费时记录”的规则：

- IRQ0 不写 VGA，只在启动自检结束时汇总 tick 与毫秒。
- IRQ1 不在汇编入口格式化日志；C++ 事件循环消费首个完整事件后记录。
- ATA PIO 不逐字、逐扇区输出，只记录驱动可用与启动描述符校验结果。
- PIC 只记录最终 mask 和一次虚假 IRQ 自检，不逐次记录 EOI。

这样 1000 Hz 时钟不会淹没键盘、异常与失败标记，也避免逐字符显存写和光标
更新延长中断服务时间。

用户日志分可信级别：

- `[OS][KERNEL] USER_*` 是内核根据已验证状态产生的生命周期证据。
- `[OS][USER] ...` 是经长度和地址检查后转发的用户文本，不能作为安全决策
  依据；测试只把三个内置验收程序的固定文本当作该镜像的行为证据。
- 系统调用不逐次打印。正常程序结束后只汇总一次
  `USER_SYSCALL_COUNT`，避免未来高频调用冲垮 VGA 控制台。
- 用户异常只输出向量、错误码、RIP 和可选 CR2，再输出一次终止与返回标记；
  不复用 `[OS][KERNEL] EXCEPTION`/`PANIC`，从协议上区分隔离事件与内核崩溃。

QEMU 捕获器继续给包括 Ring 3 文本在内的每一行加
`[QEMU][T+......ms]`。这解决“QEMU 里面也要看到时间”的观察需求，但该
前缀仍是宿主接收时间；来宾自身的可信单调时间只来自 PIT 汇总字段。

调度日志遵循“热路径零日志、冷路径批量汇总”：

- IRQ0 中只更新 tick、预算和 Thread 帧指针，不打印每次切换或 FXSAVE。
- `PROCESS_ID/THREAD_ID/CR3/KERNEL_STACK_TOP` 只在创建成功后各打印一次。
- worker 的九条进度文本用于证明三个实例都运行，不代表内核可信状态。
- 全部进程结束后才打印五个调度总量和四份进程结果；系统测试检查精确次数，
  防止重复日志掩盖遗漏。
- `PROCESS_RESOURCES_RECLAIMED` 只能在页帧统计与创建前完全一致后输出，
  `SCHEDULER_COMPLETE` 只能紧随四份合法终止结果之后。

宿主 `[QEMU][T+......ms]` 会显示进程文本交错发生的实际到达时间，但具体
PID 交错顺序受 TCG 和 IRQ 边界影响，不作为稳定协议；每个 PID 自己的
STEP_1→STEP_2→STEP_3 顺序和总出现次数才是稳定契约。

IPC 同样遵循“热路径零日志、冷路径可核对”的规则：

- 自旋等待、每次 Try、每个 64 字节内核块和每次 block/wakeup 均不打印。
- 生产者与消费者只在开始、完成、载荷验证和 EOF 四类语义边界打印用户文本；
  两个进程谁先开始不稳定，各自内部顺序和精确次数稳定。
- 全部进程结束后才输出 `SCHEDULER_BLOCKS/WAKEUPS`、管道容量、累计字节、
  读写阻塞数和 EOF 次数。
- 只有目标内核验证写入=读取=256、缓冲为空、端点均关闭、EOF=1 后，才能
  输出 `PIPE_TRANSFER_VALID` 与 `PIPE_ENDPOINTS_CLOSED`。
- 每份 Process 结果额外打印自己的 pipe read/write 字节；生产者只能写 256，
  消费者只能读 256，两个 worker 两项都必须为零。

这里故意不把每次唤醒的 PID 顺序写进日志协议。唤醒意味着条件可能推进，
真正取得资源仍取决于 Ready 队列和 PIT 边界；把一种合法交错固化为测试会让
日志反过来限制调度器。

文件系统日志也只记录冷路径状态转换和汇总：

- 每次成功启动只输出一次 `ROOTFS_V2_MOUNTED` 和
  `ROOTFS_JOURNAL_READY`。生产 Kernel 没有自动 format 分支；缺失、损坏
  或不可验证的 committed journal 必须失败，不能用重复格式化掩盖镜像构建
  或持久化错误。
- 挂载后输出一次 `ROOTFS_V2_REGION_BYTES=0x10000000` 与
  `ROOTFS_V2_MAX_FILE_BYTES=0x04000000`，冻结 256 MiB 区域和 64 MiB
  单文件规格。
- 第二次启动读到上一实例留下的正确载荷后输出
  `FILE_SYSTEM_PERSISTENCE_RESTORED`。该标记必须早于本轮 Ring 3 执行。
- 格式、CRC、journal 记录或全盘语义一致性失败只输出一次
  `FILE_SYSTEM_CORRUPT` 和状态码，然后停止；失败路径禁止进入用户态。
- 正常启动在首次挂载和用户进程结束后各输出一次
  `FILE_SYSTEM_CONSISTENT`。最终同步、内核独立读回通过后分别输出
  `FILE_SYSTEM_SYNCED` 与 `FILE_SYSTEM_PAYLOAD_VALID`。
- superblock 代次、已分配 inode/data block、journal commit/replay/
  discard/checksum failure 只在挂载和最终收口汇总；缓存不按命中、未命中、
  逐块读写或逐次 flush 打印。`ROOTFS_JOURNAL_CREDIT_CAPACITY` 每次启动只
  输出一次，断电矩阵不会把每个 checkpoint 写放进来宾日志。
- 用户态仅在完整文件写入/同步和完整文件读回/EOF 后输出
  `FILE_WRITTEN`、`FILE_VERIFIED`，系统调用本身仍不逐次打印。

持久化 QEMU 用例中，每行仍带 `[QEMU][T+......ms]`，因此能直接比较第一次
严格挂载写入、第二次挂载恢复与第三次损坏拒绝的宿主到达时间；这个前缀不改变来宾
日志的稳定文本，也不冒充磁盘中的文件时间戳。

## v1.0 交互与控制台日志基线

v1.0 保留上述冷路径原则，但把“首个 A 键”扩展为完整 Shell 会话。稳定协议
只记录命令的语义边界，不记录每个扫描码、字符、系统调用重试或提示符：

```text
[OS][USER][SHELL] READY
[OS][USER][SHELL] COMMAND=HELP
[OS][USER][SHELL] COMMAND=ECHO
[OS][USER][SHELL] COMMAND=PWD
[OS][USER][SHELL] COMMAND=MKDIR
[OS][USER][SHELL] COMMAND=WRITE
[OS][USER][SHELL] COMMAND=CAT
[OS][USER][SHELL] COMMAND=LS
[OS][USER][SHELL] COMMAND=SYNC
[OS][USER][SHELL] UNKNOWN_COMMAND_REJECTED
[OS][USER][SHELL] COMMAND=EXIT
[OS][USER][SHELL] EXIT
```

用户输入和进程调度是异步的，因此 Shell 命令标记可以与生产者、消费者和
worker 文本交错；Shell 自身的十条命令顺序、每条精确一次才是稳定契约。
帮助正文、提示符、回显和 `cat` 内容便于人工使用，但测试不以整屏文本为唯一
成功条件，避免显示层空格变化破坏语义协议。

进程全部结束后，内核一次性输出控制台汇总：

```text
[OS][KERNEL] CONSOLE_SUBMITTED_BYTES=0x000000000000006D
[OS][KERNEL] CONSOLE_READ_BYTES=0x000000000000006D
[OS][KERNEL] CONSOLE_DROPPED_BYTES=0x0000000000000000
[OS][KERNEL] CONSOLE_BUFFERED_BYTES=0x0000000000000000
```

其中 `0x6D` 是当前自动化脚本的 109 个输入字节，不是通用容量常量。验收要求
submitted=read、dropped=0、buffered=0；若以后更改脚本，应同时更新期望值，
而不是在内核里伪造固定统计。每个 Process 只在终止汇总中记录控制台读写字节，
Shell 读取全部输入，后台三个程序均不读取控制台。

没有 Ready 进程时，内核不会周期性打印 `IDLE`。它在永久地址空间和默认
RSP0 上执行同一汇编块中的 `sti; hlt; cli`；IRQ0 可能只推进时间，IRQ1 提交有效字符后才按原因
唤醒等待 fd 0 的 Shell。禁止逐 tick、逐按键、逐 FIFO 操作打印，是为了不让
显存滚屏与硬件光标更新反向改变中断时序，也让日志保持为可解释的状态摘要。

当前正常镜像的进程编号固定为 PID1 Shell、PID2 生产者、PID3 消费者、PID4
worker。创建、终止、描述符关闭、管道字节、文件系统一致性与控制台统计都在
冷路径汇总；任何 `PANIC`、`EXCEPTION`、`USER_EXECUTION_FAILED`、
`DEVICE_INITIALIZATION_FAILED` 或控制台丢弃都使成功路径失败。

## v1.2 Process/Thread 与扩展现场日志

v1.2 在架构初始化、容量事务和最终调度汇总各增加一组冷路径日志。CPU 能力
与控制寄存器只在配置回读成功后输出一次：

```text
[OS][KERNEL] EXTENDED_STATE_READY
[OS][KERNEL] EXTENDED_STATE_CR0=0x...
[OS][KERNEL] EXTENDED_STATE_CR4=0x...
[OS][KERNEL] EXTENDED_STATE_AVX_DISABLED=0x0000000000000001
```

v1.2 发布时缺少 FXSR、SSE 或 SSE2 会输出
`EXTENDED_STATE_UNSUPPORTED`。v1.3 已把这些位并入完整处理器 profile；
当前任一必需能力缺失都会输出 `PROCESSOR_FEATURES_UNSUPPORTED` 与
`PROCESSOR_MISSING_FEATURES`，并禁止出现扩展现场 ready、GDT、用户态和
`READY`。CR0/CR4 数值由 QEMU CPU 型号决定，测试验证必需位而不把无关位
硬编码。

容量事务完成并回到资源基线后输出：

```text
[OS][KERNEL] PROCESS_CAPACITY=0x...
[OS][KERNEL] THREAD_CAPACITY=0x...
[OS][KERNEL] THREADS_PER_PROCESS=0x...
[OS][KERNEL] CAPACITY_SELF_TEST_PROCESSES=0x...
[OS][KERNEL] CAPACITY_SELF_TEST_THREADS=0x...
[OS][KERNEL] CAPACITY_SELF_TEST_THREADS_PER_PROCESS=0x...
[OS][KERNEL] PROCESS_THREAD_CAPACITY_SELF_TEST_PASSED
```

前三项是运行时选择的策略上限，后三项是本次真实创建并回收的数量；两组三项
必须精确相等。QEMU 工具按 RAM 计算期望值：64 MiB 为 4/4/1，256 MiB 为
64/128/32，64 GiB 为 256/512/64。不能只搜索字段前缀，也不能用 64 GiB
QEMU 参数代替来宾容量证据。

正常四程序创建成功时各输出一次 `THREAD_ID`。PID 与 TID 当前都从一开始，
但测试分别计数，不以数值相同推断两种身份相同。四个用户程序在真实抢占或
阻塞路径后各输出一次：

```text
[OS][USER] EXTENDED_STATE_ISOLATED
```

最终只汇总累计次数：

```text
[OS][KERNEL] EXTENDED_STATE_SAVES=0x...
[OS][KERNEL] EXTENDED_STATE_RESTORES=0x...
```

二者必须非零；每次 FXSAVE/FXRSTOR 不打印。逐切换日志会显著增加 VGA 控制台耗时，
使测试改变调度时序，也会把真正的状态失败淹没。最终 `SCHEDULER_COMPLETE`
还要求 Process/Thread owned count 为零、create/reap 守恒和 26 字段资源
快照零差异。

## v1.3 CpuLocal 与原生系统调用日志

处理器规格只在架构初始化冷路径输出一次：

```text
[OS][KERNEL] PROCESSOR_FEATURES_READY
[OS][KERNEL] PROCESSOR_REQUIRED_FEATURES=0x000000000000003F
[OS][KERNEL] PROCESSOR_AVAILABLE_FEATURES=0x000000000000003F
[OS][KERNEL] PROCESSOR_PROFILE_PHYSICAL_ADDRESS_BITS=0x0000000000000028
[OS][KERNEL] PROCESSOR_PROFILE_VIRTUAL_ADDRESS_BITS=0x0000000000000030
```

required mask 的六个位依次表示 long mode、NX、FXSR、SSE、SSE2 与原生
system call。失败时只输出状态和 missing mask：

```text
[OS][KERNEL] PROCESSOR_FEATURES_UNSUPPORTED=0x...
[OS][KERNEL] PROCESSOR_MISSING_FEATURES=0x...
```

`qemu64,-syscall` 的 missing mask 为 `0x20`。失败配置禁止输出 ready、
扩展现场、GDT、用户态和最终 READY。

CpuLocal 与 MSR 都在完整初始化和回读后输出一次：

```text
[OS][KERNEL] CPU_LOCAL_READY
[OS][KERNEL] CPU_LOCAL_ADDRESS=0x...
[OS][KERNEL] NATIVE_SYSCALL_READY
[OS][KERNEL] NATIVE_SYSCALL_STAR=0x0010000800000000
[OS][KERNEL] NATIVE_SYSCALL_LSTAR=0x...
[OS][KERNEL] NATIVE_SYSCALL_FMASK=0x0000000000044700
[OS][KERNEL] NATIVE_SYSCALL_EFER=0x0000000000000501
```

初始化失败分别使用 `CPU_LOCAL_INITIALIZATION_FAILED` 和
`NATIVE_SYSCALL_INITIALIZATION_FAILED`，不得复用
`PROCESSOR_FEATURES_UNSUPPORTED`。这三类日志区分能力缺失、软件本地状态
损坏和 MSR 布局/回读失败。

用户程序只在功能边界输出三个一次性标记：

```text
[OS][USER] DUAL_SYSCALL_ENTRY_EQUIVALENT
[OS][USER] SYSRET_RETURNED
[OS][USER] SYSCALL_IRET_FALLBACK_RETURNED
```

每次系统调用、IRQ、SYSRET 或 IRET 都不打印。进程完成后一次性汇总：

```text
[OS][KERNEL] CPU_LOCAL_CURRENT_THREAD=0xFFFFFFFFFFFFFFFF
[OS][KERNEL] CPU_LOCAL_IRQ_DEPTH=0x0000000000000000
[OS][KERNEL] CPU_LOCAL_MAX_IRQ_DEPTH=0x...
[OS][KERNEL] CPU_LOCAL_PREEMPT_DEPTH=0x0000000000000000
[OS][KERNEL] CPU_LOCAL_MAX_PREEMPT_DEPTH=0x...
[OS][KERNEL] CPU_LOCAL_NEED_RESCHEDULE=0x0000000000000000
[OS][KERNEL] LEGACY_SYSCALL_ENTRIES=0x...
[OS][KERNEL] NATIVE_SYSCALL_ENTRIES=0x...
[OS][KERNEL] SYSCALL_IRQ_INTERRUPTS=0x...
[OS][KERNEL] SYSCALL_RETURN_RESCHEDULES=0x...
[OS][KERNEL] SYSRET_RETURNS=0x...
[OS][KERNEL] IRET_RETURNS=0x...
[OS][KERNEL] SYSCALL_IRET_FALLBACKS=0x...
[OS][KERNEL] REJECTED_USER_RETURNS=0x0000000000000000
[OS][KERNEL] TRUSTED_SYSCALL_STACK_VALIDATIONS=0x...
```

累计次数受 PIT 和宿主调度时序影响，验收使用语义下界而非精确常数；结束深度、
need-resched 与拒绝数则必须精确为零。可信栈验证次数必须覆盖兼容与原生入口
总数。

只有安全失败才输出 `USER_RETURN_REJECTED` 及 ownership、status、mapping、
entry、vector、RIP、RSP、RFLAGS 八项诊断。正常路径把该标记列为禁止项。
这种低频展开日志能定位返回攻击面，又不会让高频系统调用持续滚屏或反向改变
调度时序。

## v1.4 对象与动态描述符日志

用户态只在完整证明通过后输出一次：

```text
[OS][USER][PID4] FILE_DESCRIPTION_MODEL_OK
```

open、lookup、duplicate、每次读写、引用 acquire/release 和普通 close 都
不打印。它们是高频路径，逐项 VGA 控制台输出会延长持锁或系统调用时间，改变 PIT
抢占与管道阻塞时序。失败由系统调用返回值和测试断言定位，不靠无界日志。

四个 Process 全部退出、FileTable 已销毁后，Kernel 在冷路径输出一次有界
摘要：

```text
[OS][KERNEL] OBJECT_ACTIVE_COUNT=0x0000000000000000
[OS][KERNEL] OBJECT_ACTIVE_REFERENCES=0x0000000000000000
[OS][KERNEL] OBJECT_CREATIONS=0x...
[OS][KERNEL] OBJECT_DESTRUCTIONS=0x...
[OS][KERNEL] OBJECT_PEAK_REFERENCES=0x...
[OS][KERNEL] FILE_DESCRIPTION_ACTIVE_COUNT=0x0000000000000000
[OS][KERNEL] FILE_DESCRIPTION_FINALIZATIONS=0x...
[OS][KERNEL] FILE_DESCRIPTION_FAILED_FINALIZATIONS=0x0000000000000000
[OS][KERNEL] FILE_TABLE_HARD_LIMIT=0x...
[OS][KERNEL] FILE_TABLE_PEAK_DESCRIPTORS=0x...
[OS][KERNEL] FILE_TABLE_CHUNK_ALLOCATIONS=0x...
[OS][KERNEL] FILE_TABLE_CHUNK_RELEASES=0x...
[OS][KERNEL] FILE_TABLE_INSTALLATIONS=0x...
[OS][KERNEL] FILE_TABLE_CLOSES=0x...
```

active 和 failed 字段必须精确为零；creation/destruction、finalization/
destruction、chunk allocation/release 必须由内核不变量比较相等。peak、
installation 和 close 是有意义的容量/工作量证据，但不要求在不同宿主时序下
伪造精确常数。

接着输出三层布尔 oracle：

```text
[OS][KERNEL] RUNTIME_STATE_VALIDATION=0x0000000000000001
[OS][KERNEL] SMOKE_STATE_VALIDATION=0x0000000000000001
[OS][KERNEL] PROCESS_RESOURCE_VALIDATION=0x0000000000000001
```

它们分别覆盖对象/调度/CpuLocal 内部状态、functional Shell/IPC/控制台行为，
以及 frame/KVA/heap/stack/FileDescription 的跨层资源快照。三项全部为一且
每 Process 结果符合预期后，才允许输出：

```text
[OS][KERNEL] FILE_DESCRIPTION_MODEL_VALID
[OS][KERNEL] PROCESS_RESOURCES_RECLAIMED
[OS][KERNEL] SCHEDULER_COMPLETE
```

QEMU 验收器按日志顺序检查这些标记，并根据内存配置要求 hard limit 精确为
64、256 或 4096。宿主到达时间仍由 runner 添加 `[QEMU][T+...ms]`，来宾
计时仍以 PIT `MONOTONIC_MILLISECONDS` 为准；对象统计不伪装成时间戳。

## v1.5 VFS、挂载与 memfs 日志

VFS 初始化在用户调度前只输出一次阶段事实：

```text
[OS][KERNEL] VFS_READY
[OS][KERNEL] MEMFS_MOUNTED=/tmp
[OS][KERNEL] VFS_MAX_PATH_BYTES=0x0000000000001000
[OS][KERNEL] VFS_MAX_NAME_BYTES=0x00000000000000FF
[OS][KERNEL] VFS_VALID
```

其中 `VFS_READY` 表示 legacy root、VFS 根 Mount 和后端操作表已经初始化；
`MEMFS_MOUNTED` 表示 `/tmp` 已经真实解析为挂载点并接入子 Superblock；
`VFS_VALID` 只有在 mount 父链、Superblock 与两个后端一致性检查全部通过后
输出。任一步失败只输出 `VFS_STATUS=<enum>` 后停机，不能继续进入用户态。

初始化完成和全部 Process 退出后各输出一次汇总：

```text
[OS][KERNEL] VFS_MOUNTS=0x...
[OS][KERNEL] VFS_PATH_RESOLUTIONS=0x...
[OS][KERNEL] VFS_FAILED_PATH_RESOLUTIONS=0x...
[OS][KERNEL] VFS_MOUNT_TRANSITIONS=0x...
[OS][KERNEL] MEMFS_ACTIVE_NODES=0x...
[OS][KERNEL] MEMFS_DATA_CAPACITY_BYTES=0x...
```

path resolutions 和 failed resolutions 是累计工作量与失败分支证据，不要求
不同调度时序下具有相同常数；mount count、最大规格和成功阶段 marker 则按
协议精确检查。结束阶段在 `Vfs::Sync` 和第二次 `Vfs::Validate` 之后才输出
最终 `VFS_VALID`。

Shell 为每条已识别命令保留一个一次性 marker。v1.5 新增：

```text
[OS][USER][SHELL] COMMAND=CD
```

functional 输入要求该标记精确出现两次，`PWD` 精确出现三次；mkdir、write、
cat、ls 各出现两次，分别覆盖 memfs 与 legacy-fs。提示符中的 cwd 是用户
可见文本，不作为解析器 oracle；真正验收同时检查命令结果和后续路径操作。

v1.6 新增命名空间命令 marker：

```text
[OS][USER][SHELL] COMMAND=RM
[OS][USER][SHELL] COMMAND=RMDIR
[OS][USER][SHELL] COMMAND=MV
[OS][USER][SHELL] COMMAND=TRUNCATE
[OS][USER][SHELL] COMMAND=STAT
```

每条命令只在解析成功并进入对应系统调用路径时打印一次。块分配、pointer
索引、bitmap bit、短写循环和 fsck 遍历仍禁止逐项打印；RootFileSystem
只在初始化与最终同步输出事务代次、已分配 inode、数据/元数据块和空闲块。

不得在以下热路径逐项打印：

- 每个路径组件；
- 每次 vnode lookup 或 parent；
- 每次 mount 数组扫描；
- 每个 memfs 节点分配；
- 每次 OpenFile offset 推进。

这些次数由聚合统计和宿主模型覆盖。逐组件 VGA 控制台日志会显著改变系统调用被 PIT
打断的时序，也可能让 4096 字节路径制造数千行输出。需要调试某个失败时，应
在宿主单元测试中缩小种子/步骤，或临时启用有界诊断后再移除，不能把无界
trace 留在默认镜像。

时间仍采用双坐标：

- 来宾 `TIMER_TICKS` 与 `MONOTONIC_MILLISECONDS` 表示 PIT 驱动的目标时间；
- 宿主 QEMU 捕获器为每条内存日志行加 `[QEMU][T+...ms]`，表示从当前 QEMU
  进程启动开始的墙钟相对时间。

两者不能混为一谈。TCG 宿主负载会改变墙钟耗时，来宾 tick 又会受中断开放
窗口影响；日志保留两个来源才能区分“来宾没有推进”和“模拟器推进较慢”。
每个 QEMU 用例同时具有内部里程碑截止与外层 CTest 截止，超时后由 Python
回收进程和临时 socket，不允许留下永久后台终端。

## v1.7 PID1、进程映像与父子生命周期日志

进程创建、成功 exec 和成功 wait 都是低频生命周期边界，Kernel 各输出一行
带 64 位身份的事件：

```text
[OS][KERNEL][PROC] SPAWN_PID=0x0000000000000001
[OS][KERNEL][PROC] SPAWN_PID=0x...
[OS][KERNEL][PROC] EXEC_PID=0x...
[OS][KERNEL][PROC] WAIT_REAPED_PID=0x...
```

`SPAWN_PID=1` 与随后 `/sbin/init` 的参数验证共同证明初始 Process；普通失败
尝试不打印伪成功 PID。`EXEC_PID` 只在候选 CR3、用户栈与调度器映像已经提交
后出现；截断 ELF 与 E2BIG 失败不输出该事件。`WAIT_REAPED_PID` 只在 Kernel
已形成退出结果并且树项/调度槽完成回收后出现，随后 dispatcher 才把固定结构
复制到已预先验证的用户缓冲；它不为每次 would-block 重试打印。

用户探针只输出一次阶段事实：

```text
[OS][USER][INIT] STARTED
[OS][USER][INIT] ARGUMENTS_VALID
[OS][USER][INIT] CHILDREN_STARTED
[OS][USER][PROC] ORPHAN_CHILD_SPAWNED
[OS][USER][PROC] ORPHAN_CHILD_RUNNING
[OS][USER][PROC] ARG_ENV_128K_VERIFIED
[OS][USER][PROC] EXEC_FAILURE_PRESERVED_IMAGE
[OS][USER][PROC] EXEC_E2BIG_PRESERVED_IMAGE
[OS][USER][PROC] EXEC_COMMITTED
[OS][USER][INIT] ORPHAN_REAPED
[OS][USER][INIT] ALL_CHILDREN_REAPED
[OS][USER][INIT] NO_ZOMBIES
```

这些行描述语义边界，不逐字打印 128 KiB 参数、不打印每个 ELF chunk、每张
用户页、每个 argv 指针或每次 wait 扫描。参数内容由目标程序逐字节校验，ELF
字节由 reader 单元/集成测试覆盖；把这些热路径写入 VGA 控制台会改变调度时序并淹没
真正失败位置。

全部 Process 安全点回收后，Kernel 一次性输出进程树摘要：

```text
[OS][KERNEL] PROCESS_TREE_REGISTERED=0x0000000000000008
[OS][KERNEL] PROCESS_TREE_EXITED=0x0000000000000008
[OS][KERNEL] PROCESS_TREE_COLLECTED=0x0000000000000008
[OS][KERNEL] PROCESS_TREE_REPARENTED=0x0000000000000001
[OS][KERNEL] PROCESS_TREE_ZOMBIES=0x0000000000000000
[OS][KERNEL] PROCESS_TREE_WAIT_SUCCESSES=0x0000000000000007
[OS][KERNEL] PROCESS_TREE_WAIT_BLOCKS=0x...
[OS][KERNEL] PROCESS_TREE_WAIT_NO_CHILD=0x0000000000000001
[OS][KERNEL] PROCESS_TREE_VALID
```

wait block 次数受合法调度交错影响，只要求至少一次；注册、退出、收集、收养、
成功 wait、no-child 与最终 Zombie 数则是当前正常工作负载的精确协议。
`PROCESS_TREE_VALID` 还要求 active/alive 均为零，并与 ThreadScheduler 的八次
Process/Thread 创建回收、动态栈活动数零和资源快照零差异同时成立。

时间仍使用两套来源。上述每条内存日志行会由宿主补上 `[QEMU][T+...ms]`；来宾
在设备初始化后仍输出 PIT 的 `TIMER_TICKS` 与
`MONOTONIC_MILLISECONDS`。进程日志不自行读取 RTC，也不为每次系统调用打印
时间。若卡在某个生命周期事件，runner 的阶段 deadline 会终止并回收 QEMU；
不能通过增加无界 TRACE 来掩盖死锁或丢失唤醒。

## v1.8 按需分页与 VMA 日志

页故障是潜在高频事件。默认镜像不为每页打印 CR2、frame、PTE 或页表层级，
否则一个 32 MiB 线性触页就能制造 8192 组 VGA 控制台输出，并改变 TCG 下的抢占与
wait 交错。

Kernel 只在当前 Process 的累计计数达到二次幂时采样：

```text
[OS][KERNEL][VM] DEMAND_FAULT_COUNT=0x...
[OS][KERNEL][VM] STACK_GROWTH_COUNT=0x...
```

每个 Process 从自己的一开始采样，所以整机可能出现多行相同数值。QEMU
runner 必须把它们解释为“至少出现一次且解析值非零”，不得冻结精确行数。
RSVD、present、guard 与权限失败仍走统一用户异常结果；只有无法解释的 Ring 0
状态进入 panic。

成功 memory probe 使用阶段语义标记：

```text
[OS][USER][VM] STARTED
[OS][USER][VM] DEMAND_ZERO_VERIFIED
[OS][USER][VM] ANONYMOUS_UNMAP_RECLAIMED
[OS][USER][VM] PROGRAM_BREAK_VERIFIED
[OS][USER][VM] STACK_GROWTH_VERIFIED
[OS][USER][VM] USER_HEAP_RANDOMIZED_VERIFIED
[OS][USER][VM] COMPLETED
[OS][USER][INIT] MEMORY_PROBE_REAPED
```

每条只在一整组断言通过后打印一次。5000 次 heap 操作、每个块 split/coalesce、
每次 `brk`、每个 VMA 查找和每个页表回收都不逐项打印。失败通过探针退出码、
宿主单元/随机测试的种子与迭代位置定位。

两个保护 probe 在执行预期非法访问前各输出一次：

```text
[OS][USER][VM] GUARD_FAULT_ARMED
[OS][USER][VM] PROTECTION_FAULT_ARMED
```

它们之后必须由 ProcessRuntime 记录用户 vector 14，而不是输出“完成”。
PID1 同时核对 termination reason 和 vector 后才输出：

```text
[OS][USER][INIT] VM_FAULT_POLICIES_VERIFIED
```

全部十一 Process 生命周期结束后，Kernel 冷路径输出一次 VMA 池摘要：

```text
[OS][KERNEL] VMA_DESCRIPTOR_CAPACITY=0x0000000000002000
[OS][KERNEL] VMA_ACTIVE_DESCRIPTORS=0x0000000000000000
[OS][KERNEL] VMA_PEAK_DESCRIPTORS=0x...
[OS][KERNEL] VMA_ACQUIRES=0x...
[OS][KERNEL] VMA_RELEASES=0x...
```

capacity 与 active 是精确协议；peak/acquire/release 必须非零，并由目标内
资源验证要求 acquire 等于 release。v1.8 的进程树精确摘要随新增的三个探针
变为 registered/exited/collected 各 11、wait successes 10、Zombie 0；
峰值并发仍为八。

时间策略不变：来宾打印 PIT 单调毫秒，宿主为每行增加
`[QEMU][T+...ms]`。64 MiB bootstrap、256 MiB functional 和 64 GiB capacity
均有内部阶段截止与外层 CTest 超时；采样日志不能代替超时，也不能因等待某个
非必然的精确 fault 次数让 QEMU 留在后台。

## v1.9 文件页故障与缓存日志

文件页 fault、cache 命中、失效和阻塞描述符读取同样采用二次幂采样：

```text
[OS][KERNEL][VM] FILE_FAULT_COUNT=0x...
[OS][KERNEL][VM] PAGE_CACHE_HIT_COUNT=0x...
[OS][KERNEL][VM] PAGE_CACHE_INVALIDATION_COUNT=0x...
[OS][KERNEL][IO] DESCRIPTOR_READ_BLOCK_COUNT=0x...
```

失效日志必须同时满足“本次调用前后累计值变化”和“新值是二次幂”。用户层
4096 字节写会被拆成多个 256 字节调用，不能因为累计值仍为 1 而重复打印相同
行。descriptor block 的第一条标记也是 QMP 输入注入门槛，证明 Shell 已实际
阻塞在输入描述符上。

文件映射完整语义只打印一次：

```text
[OS][USER][VM] FILE_MAPPING_CACHE_VERIFIED
```

探针失败使用 `[OS][USER][VM][FAIL] <stage>`，正常与持久化 QEMU 都禁止出现
该前缀。无法继续进程退出时的冷路径诊断使用：

```text
[OS][KERNEL][FATAL] EXIT_PROCESS_ID=0x...
[OS][KERNEL][FATAL] PROCESS_TREE_STATUS=0x...
```

正常运行禁止任何 `[OS][KERNEL][FATAL]`。这些诊断曾定位到“地址空间已销毁、
结果槽仍存活”的 Zombie 失效扫描错误；修复后不会进入成功日志。

## v1.10 fork/COW 日志

fork/COW 的 page fault、单页复制和引用增减属于热路径，不逐事件输出。用户
探针只在一个完整语义提交后打印：

```text
[OS][USER][FORK] COW_ISOLATION_VERIFIED
[OS][USER][FORK] FD_OFFSET_AND_CWD_VERIFIED
[OS][USER][FORK] FORK_EXEC_WAIT_32_VERIFIED
[OS][USER][FORK] COMPLETED
[OS][USER][INIT] FORK_PROBE_REAPED
```

child/parent 验证失败使用 `CHILD_STATE_FAILURE`、
`CHILD_STATISTICS_FAILURE`、`CHILD_DESCRIPTOR_FAILURE` 或
`PARENT_STATE_FAILURE`。Kernel fork 事务失败时打印一次
`FORK_FAILURE_STAGE` 与 `FORK_FAILURE_STATUS`；正常 QEMU 禁止这些标记。

全部 Process 回收后，Kernel 冷路径打印稀疏页引用摘要：

```text
[OS][KERNEL] USER_PAGE_REFERENCE_ACTIVE_ENTRIES=0x...
[OS][KERNEL] USER_PAGE_REFERENCE_ACTIVE_REFERENCES=0x...
[OS][KERNEL] USER_PAGE_REFERENCE_PEAK_ENTRIES=0x...
[OS][KERNEL] USER_PAGE_REFERENCE_FIRST_SHARES=0x...
[OS][KERNEL] USER_PAGE_REFERENCE_RETAINS=0x...
[OS][KERNEL] USER_PAGE_REFERENCE_RELEASES=0x...
[OS][KERNEL] USER_PAGE_REFERENCE_EXCLUSIVE_RESTORES=0x...
```

active 两项必须精确为零；peak、first share 和 release 必须非零。retain
可以为零，因为当前探针主要创建两方共享，第一次共享直接把隐含 1 变为 2；
以后形成三方以上共享时才必然增长。

时间策略不变：来宾使用 PIT 单调毫秒，宿主捕获器为每行附加
`[QEMU][T+...ms]`。日志只描述已提交状态，不在页表锁、引用锁或回滚中间
打印，避免 VGA 控制台吞吐改变 fault 与调度顺序。

## v1.11 动态管道与外部 Shell 日志

Pipe 分配、每页首次写、每次短读和每个 fd close 都是热路径，不允许逐事件
打印。所有 Process 回收后只输出一次冷路径摘要：

```text
[OS][KERNEL] DYNAMIC_PIPE_CAPACITY=0x...
[OS][KERNEL] DYNAMIC_PIPE_ACTIVE=0x0000000000000000
[OS][KERNEL] DYNAMIC_PIPE_PEAK_ACTIVE=0x...
[OS][KERNEL] DYNAMIC_PIPE_CREATIONS=0x...
[OS][KERNEL] DYNAMIC_PIPE_RELEASES=0x...
[OS][KERNEL] DYNAMIC_PIPE_CAPACITY_REJECTIONS=0x...
```

functional 的 peak 必须等于 128，creation 必须等于 release，rejection
至少为 1。这个摘要同时覆盖启动容量事务和用户流水线，不通过逐管道 marker
人为增加成功证据。

Shell 只在完整命令事务成功后输出：

```text
[OS][USER][SHELL] REDIRECTION_VERIFIED
[OS][USER][SHELL] PIPELINE_16_VERIFIED
```

functional 中前者精确两次、后者精确一次。解析失败、child 接线失败或任一
stage 非零退出都不能输出这些 marker。来宾与宿主时间策略继续沿用本章统一
规则，不在管道热路径读 RTC 或打印 tick。

## v1.12 用户 Thread、TLS 与 private futex 日志

Thread create/exit/join 和 futex wait/wake 都是并发热路径。Kernel 只在累计
计数为二次幂时输出：

```text
[OS][KERNEL][THREAD] CREATE_COUNT=0x...
[OS][KERNEL][THREAD] EXIT_COUNT=0x...
[OS][KERNEL][THREAD] JOIN_COUNT=0x...
[OS][KERNEL][FUTEX] WAIT_COUNT=0x...
[OS][KERNEL][FUTEX] WAKE_OPERATION_COUNT=0x...
```

因此 64 Thread 工作负载最多产生对数级进度行，不把显存滚屏和光标更新成本引入每
次 mutex 临界区。用户探针只在完整语义提交后输出：

```text
[OS][USER][THREAD] FUNCTIONAL_32_READY
[OS][USER][THREAD] CAPACITY_64_READY
[OS][USER][THREAD] TLS_ISOLATED
[OS][USER][THREAD] FUTEX_SYNCHRONIZATION_VERIFIED
[OS][USER][THREAD] JOIN_RECLAIMED
[OS][USER][THREAD] COMPLETED
```

bootstrap 只输出 `BOOTSTRAP_SINGLE_THREAD_VERIFIED` 与 `COMPLETED`，因为
配置上限 1 本身是兼容档契约。PID1 在成功 wait 后输出
`THREAD_PROBE_REAPED`；不能用 probe 自己的 marker 替代父进程回收证据。

系统调用返回现场若被拒绝，冷失败路径额外输出 RIP/RSP、上下文状态和指令/
栈页查询状态及 W/X/U/COW flags，然后停机。正常测试禁止出现这些诊断。
所有行继续由宿主捕获器添加 `[QEMU][T+......ms]` 到达时间；来宾内核的 PIT
单调时间仍按既有里程碑输出，不在 futex 热路径读取或打印时间。

## v1.13 单调时间与 deadline 日志

PIT IRQ0、deadline 队首检查和逐 waiter 解析均为热路径，不输出逐 tick 或
逐 deadline 日志。`/bin/time_probe` 只在一个完整语义边界提交后输出：

```text
[OS][USER][TIME] STARTED
[OS][USER][TIME] MONOTONIC_START_NS=0x...
[OS][USER][TIME] MONOTONIC_WAKE_NS=0x...
[OS][USER][TIME] SLEEP_VERIFIED
[OS][USER][TIME] FUTEX_TIMEOUT_VERIFIED
[OS][USER][TIME] CONDITION_WON_BEFORE_DEADLINE
[OS][USER][TIME] CONDITION_TIMEOUT_VERIFIED
[OS][USER][TIME] COMPLETED
[OS][USER][INIT] TIME_PROBE_REAPED
```

64 MiB 配置以 `CONDITION_SINGLE_THREAD_PROFILE` 替代 condition-won marker，
明确说明资源规格而不伪造并发证据。内核在全部 Process/Thread 回收后一次性
输出：

```text
[OS][KERNEL][TIME] ACTIVE_DEADLINES=0x...
[OS][KERNEL][TIME] PEAK_DEADLINES=0x...
[OS][KERNEL][TIME] DEADLINE_SCHEDULES=0x...
[OS][KERNEL][TIME] DEADLINE_EXPIRATIONS=0x...
[OS][KERNEL][TIME] DEADLINE_CANCELLATIONS=0x...
[OS][KERNEL][TIME] FUTEX_TIMEOUT_OPERATIONS=0x...
```

正常终态 active 为零且 schedule=expiration+cancellation。数值使用来宾
单调时钟和内核账本；宿主添加的 `[QEMU][T+...ms]` 只描述内存日志观察时间。
二者起点和调度环境不同，不比较绝对值。

## v1.14 进程信号与用户返回日志

信号发送、等待唤醒和用户返回都可能成为热路径。Kernel 对累计量只在从零变为
非零或达到二次幂时输出局部进度，整机回收完成后再打印唯一权威汇总：

```text
[OS][KERNEL][SIGNAL] QUEUED_SIGNALS=0x...
[OS][KERNEL][SIGNAL] COALESCED_SIGNALS=0x...
[OS][KERNEL][SIGNAL] IGNORED_SIGNALS=0x...
[OS][KERNEL][SIGNAL] HANDLER_DELIVERIES=0x...
[OS][KERNEL][SIGNAL] DEFAULT_TERMINATIONS=0x...
[OS][KERNEL][SIGNAL] GROUP_SENDS=0x...
[OS][KERNEL][SIGNAL] INTERRUPTED_WAITS=0x...
[OS][KERNEL][SIGNAL] RESTARTED_WAITS=0x...
[OS][KERNEL][SIGNAL] REJECTED_FRAMES=0x...
```

重复发送同一普通信号只增加 `COALESCED_SIGNALS`，不重复输出相同 queued
进度。处置安装、首次 handler 交付、默认终止与 frame 拒绝属于低频语义边界，
分别携带信号号、处置类型或失败阶段；不得输出逐寄存器成功路径转储。

用户探针在完整事务提交后输出：

```text
[OS][USER][SIGNAL] STARTED
[OS][USER][SIGNAL] RESTART_WAIT_VERIFIED
[OS][USER][SIGNAL] MASK_COALESCE_VERIFIED
[OS][USER][SIGNAL] PROCESS_GROUP_VERIFIED
[OS][USER][SIGNAL] FORK_INHERITANCE_VERIFIED
[OS][USER][SIGNAL] BAD_FRAME_ISOLATED
[OS][USER][SIGNAL] DEFAULT_TERMINATION_VERIFIED
[OS][USER][SIGNAL] COMPLETED
```

这些来宾行继续由宿主捕获器添加 `[QEMU][T+......ms]` 到达时间。时间戳用于
判断“仍在推进”还是“静默卡死”，不是 signal ABI 的一部分，也不能代替
Kernel 的 pending、delivery 和 rejected-frame 账本。

## v1.15 TTY 与作业控制日志

键盘 IRQ、普通字符、run queue 扫描和 wait 重试都属于热路径，不逐事件输出。
Shell 只在可作为协议屏障的低频边界打印：

```text
[OS][USER][SHELL] JOB_CONTROL_READY
[OS][USER][SHELL] FOREGROUND_JOB_WAITING
[OS][KERNEL][SIGNAL] DEFAULT_CONTINUE_DELIVERED
[OS][USER][SHELL] FOREGROUND_JOB_STOPPED
[OS][USER][SHELL] BACKGROUND_JOB_STARTED
[OS][USER][SHELL] COMMAND_COMPLETE
```

Kernel 在全部 Process 收集后输出一次权威账本：

```text
[OS][KERNEL][SCHED] STOPPED_PROCESSES=0x...
[OS][KERNEL][SCHED] PROCESS_STOPS=0x...
[OS][KERNEL][SCHED] PROCESS_CONTINUES=0x...
[OS][KERNEL][TTY] CONSUMED_BYTES=0x...
[OS][KERNEL][TTY] COMMITTED_LINES=0x...
[OS][KERNEL][TTY] INTERRUPTS=0x...
[OS][KERNEL][TTY] STOPS=0x...
[OS][KERNEL][TTY] OUTPUT_QUEUED_BYTES=0x...
[OS][KERNEL][TTY] OUTPUT_WRITTEN_BYTES=0x...
[OS][KERNEL][TTY] OUTPUT_PENDING_BYTES=0x...
[OS][KERNEL][TTY] FOREGROUND_CHANGES=0x...
[OS][KERNEL][TTY] BACKGROUND_READ_REJECTIONS=0x...
[OS][KERNEL][PROC] STOPPED_EVENTS=0x...
[OS][KERNEL][PROC] CONTINUED_EVENTS=0x...
[OS][KERNEL][JOB] ACTIVE_PROCESSES=0x...
[OS][KERNEL][JOB] ACTIVE_SESSIONS=0x...
[OS][KERNEL][JOB] ACTIVE_PROCESS_GROUPS=0x...
```

控制字符采用“动作计数 + 终态账本”，不打印每次扫描码。QEMU runner 等待
新的 `COMMAND_COMPLETE` 再发送下一条命令；等待控制键前还要求新的
`FOREGROUND_JOB_WAITING`。`fg` 后还必须观察
`DEFAULT_CONTINUE_DELIVERED` 才注入 Ctrl-C，不能依靠宿主执行快慢决定
SIGCONT/SIGINT 顺序。每步超过 20 秒立即失败并终止协议等待，不能继续耗尽
整机总预算。

## v1.16 IRQ14、块请求与写回日志

ATA status poll、每个 16-bit PIO word、cache hit 和每次 PIT 检查都属于热
路径，不打印。初始化和单次显式 sync 使用低频语义 marker：

```text
[OS][KERNEL] ATA_IRQ14_READY
[OS][KERNEL] ATA_REQUEST_CAPACITY=0x0000000000000040
[OS][KERNEL][BLOCK] FLUSH_SUBMIT_REQUEST=0x...
[OS][KERNEL][BLOCK] SUBMIT_TIME_NS=0x...
[OS][KERNEL][BLOCK] COMPLETION_RESULT=0x...
[OS][KERNEL][BLOCK] COMPLETION_TIME_NS=0x...
[OS][KERNEL][BLOCK] OTHER_THREAD_PROGRESS=0x...
[OS][USER][VM] FILE_SHARED_WRITEBACK_VERIFIED
```

请求 identifier 用于关联一次提交/完成，不逐轮询递增日志。完成结果、
DeviceError、TimedOut 只在唯一解析边界记录；迟到 IRQ 进入累计重复解析统计，
不重复打印同一完成。

来宾时间来自 PIT 单调纳秒，必须满足 completion time 不早于 submit time。
宿主捕获器继续添加 `[QEMU][T+......ms]`，它只描述内存日志观察时间，用于看出
32 GiB TCG 扫描或宿主竞争造成的阶段延迟，不能参与请求 deadline。

页缓存只在 sync 边界记录 writeback 状态/页数/VFS 结果；Dirty/Writeback
的逐页状态转移由统计与测试观察，不冲 VGA 控制台。写回失败必须保留明确 error
结果，不能用一条“sync complete”掩盖部分失败。

## v2.8 统一文件页缓存日志

Acquire、MarkDirty、每页 copy、truncate 扫描和 writeback 状态迁移都不逐次输出。
Kernel 只在最终文件系统边界汇总：

```text
[OS][KERNEL] FILE_CACHE_BUFFERED_READS=0x...
[OS][KERNEL] FILE_CACHE_BUFFERED_HITS=0x...
[OS][KERNEL] FILE_CACHE_BUFFERED_BYTES=0x...
[OS][KERNEL] FILE_CACHE_BUFFERED_WRITES=0x...
[OS][KERNEL] FILE_CACHE_TRUNCATES=0x...
[OS][KERNEL] FILE_CACHE_WRITEBACK_WORKER_RUNS=0x...
[OS][KERNEL] FILE_CACHE_WRITEBACK_WORKER_PAGES=0x...
[OS][KERNEL] FILE_CACHE_WRITEBACK_BACKPRESSURE=0x...
[OS][KERNEL] FILE_CACHE_WRITEBACK_WORKER_FAILURES=0x0000000000000000
[OS][KERNEL] FILE_SYNCHRONIZATIONS=0x...
[OS][KERNEL] FILE_DATA_SYNCHRONIZATIONS=0x...
[OS][KERNEL] MEMORY_SYNCHRONOUS_SYNCHRONIZATIONS=0x...
[OS][KERNEL] MEMORY_ASYNCHRONOUS_SYNCHRONIZATIONS=0x...
[OS][KERNEL] WRITEBACK_ERROR_ACTIVE_RECORDS=0x0000000000000000
[OS][KERNEL] MEMORY_PRESSURE_LIMIT_PAGES=0x...
[OS][KERNEL] MEMORY_RECLAIM_CLEAN_FILE_PAGES=0x...
[OS][KERNEL] MEMORY_RECLAIM_WRITTEN_FILE_PAGES=0x...
[OS][KERNEL] MEMORY_RECLAIM_RECLAIMED_WRITTEN_FILE_PAGES=0x...
[OS][KERNEL] MEMORY_RECLAIM_SWAPPED_ANONYMOUS_PAGES=0x...
[OS][KERNEL] MEMORY_RECLAIM_NO_PROGRESS=0x0000000000000000
[OS][KERNEL] FILE_CACHE_RECLAIMED
```

第三增量不再输出正常 write 的 `PAGE_CACHE_INVALIDATION_COUNT`；该 marker 只属于
v1.9 的历史失效协议。runner 要求 buffered write 与 truncate 非零，并以用户态
`FILE_MAPPING_CACHE_VERIFIED` 证明 shared/private/truncate 数据语义。故障仍由 sync
边界的 WRITEBACK_STATUS 和禁止出现的用户 `[FAIL]` marker 表达，不把逐页诊断刷到
VGA 终端。

第五增量仍不逐 fd、逐 msync 页打印。四类同步计数必须非零，活动错误记录最终为零；
真实错误由 syscall 返回值和 tracker 单元/随机模型验证，不在终端重复打印每个打开
实例的游标。

第六增量仍只输出最终聚合，不逐扫描页、逐 swap 槽或逐 victim 候选打印。压力门禁以
分阶段非零计数证明真实路径。reclaim profile 要求 NoProgress=0、resident limit
`0x2400`；OOM profile 要求 NoProgress/OOM 各非零、resident limit `0x3000`、swappiness
和 anonymous swap 为零。两者都只输出最终摘要，不打印候选或逐页决策。

```text
[OS][KERNEL] MEMORY_SWAPPINESS=0x...
[OS][KERNEL] MEMORY_RECLAIM_PLANNED_FILE_PAGES=0x...
[OS][KERNEL] MEMORY_RECLAIM_PLANNED_ANONYMOUS_PAGES=0x...
[OS][KERNEL] OOM_INVOCATIONS=0x...
[OS][KERNEL] OOM_KILLS=0x...
[OS][KERNEL] OOM_LAST_VICTIM=0x...
```

V2.10.4 的 Loading 合并同样禁止逐 miss、逐 waiter 打印，只在全部用户进程结束后的
文件系统统计区追加固定八行：

```text
[OS][KERNEL][FILE_PAGE_LOAD] ACTIVE=0x0000000000000000
[OS][KERNEL][FILE_PAGE_LOAD] BEGINS=0x...
[OS][KERNEL][FILE_PAGE_LOAD] WAITERS=0x...
[OS][KERNEL][FILE_PAGE_LOAD] WAIT_COMMITS=0x...
[OS][KERNEL][FILE_PAGE_LOAD] COMPLETIONS=0x...
[OS][KERNEL][FILE_PAGE_LOAD] BROADCAST_WAKES=0x...
[OS][KERNEL][FILE_PAGE_LOAD] FAILURE_BROADCASTS=0x0000000000000000
[OS][KERNEL][FILE_PAGE_LOAD] RESULT_TAKES=0x...
```

runner 只校验三组守恒和最终零状态，不要求单 BSP 正常负载人为制造 waiter。强制重叠、
失败广播和 completion-before-wait 由 host integration/randomized 测试观察，避免通过热路径
日志或人为延迟改变 QEMU 调度。

V2.10.5a 的策略逐步行为仍只由 host 测试观察。5b 生产接入后也禁止逐页或逐任务日志，只在
进程、后台请求和文件系统全部收束后输出一次聚合：

```text
[OS][KERNEL][READAHEAD] OBSERVATIONS=0x...
[OS][KERNEL][READAHEAD] SCHEDULES=0x...
[OS][KERNEL][READAHEAD] SCHEDULE_REJECTIONS=0x0000000000000000
[OS][KERNEL][READAHEAD] USEFUL_PAGES=0x...
[OS][KERNEL][READAHEAD] WASTED_PAGES=0x...
[OS][KERNEL][READAHEAD] FEEDBACK_APPLICATIONS=0x...
[OS][KERNEL][READAHEAD] CANCELLATIONS=0x...
[OS][KERNEL][READAHEAD] CANCELLATION_FAILURES=0x0000000000000000
[OS][KERNEL][READAHEAD] REQUEST_ACTIVE=0x0000000000000000
[OS][KERNEL][READAHEAD] REQUEST_ENQUEUES=0x...
[OS][KERNEL][READAHEAD] REQUEST_COMPLETIONS=0x...
[OS][KERNEL][READAHEAD] REQUEST_TERMINATIONS=0x...
[OS][KERNEL][READAHEAD] REQUEST_CAPACITY_REJECTIONS=0x0000000000000000
[OS][KERNEL][READAHEAD] REQUEST_QUEUED_CANCELLATIONS=0x...
[OS][KERNEL][READAHEAD] REQUEST_RUNNING_CANCELLATIONS=0x...
[OS][KERNEL][READAHEAD] FEEDBACK_ACTIVE_STREAMS=0x0000000000000000
[OS][KERNEL][READAHEAD] FEEDBACK_ACTIVE_TASKS=0x0000000000000000
[OS][KERNEL][READAHEAD] FEEDBACK_STALE_WASTE=0x...
[OS][KERNEL][READAHEAD] WORKER_FAILURES=0x0000000000000000
[OS][KERNEL][READAHEAD] LOADED_PAGES=0x...
[OS][KERNEL][READAHEAD] FAILED_PAGES=0x0000000000000000
[OS][KERNEL][READAHEAD] PRESSURE_STOPS=0x...
[OS][KERNEL][READAHEAD] PREFETCH_LOADS=0x...
[OS][KERNEL][READAHEAD] PREFETCH_RESIDENT=0x0000000000000000
[OS][KERNEL][READAHEAD] PREFETCH_HITS=0x...
[OS][KERNEL][READAHEAD] PREFETCH_WASTE=0x...
```

5c 后所有 profile 要求 schedule=enqueues 且 enqueue=terminations，其中 termination 是
completion+queued cancellation。Smoke/OOM 的真实文件负载另要求
load/useful/hit 非零。纯 `#UD/#PF` 等不运行文件负载的故障 profile 允许这些值为零，但
active/worker/cache failure/final resident 仍必须为零。`PREFETCH_RESIDENT` 在最终 trim 后
采样；`PREFETCH_WASTE` 与 `PRESSURE_STOPS` 可以为零，不能为了制造非零统计插入延迟。

正常路径还要求 `[OS][USER][TOOLS] SEQUENTIAL_READAHEAD_VERIFIED`。只有 worker 或工具探针
失败时才允许输出 `[OS][KERNEL][READAHEAD][FAIL] ...` 或
`[OS][USER][TOOLS][FAIL] ...`；runner 将两者视为禁止标记，stage/status 仅用于定位失败，
不会进入成功路径终端输出。

## v2.9 Kernel Thread 聚合日志

```text
[OS][KERNEL] KERNEL_THREAD_ACTIVE=0x0000000000000000
[OS][KERNEL] KERNEL_THREAD_PEAK_ACTIVE=0x0000000000000002
[OS][KERNEL] KERNEL_THREAD_CREATIONS=0x0000000000000004
[OS][KERNEL] KERNEL_THREAD_DISPATCHES=0x...
[OS][KERNEL] KERNEL_THREAD_YIELDS=0x...
[OS][KERNEL] KERNEL_THREAD_BLOCKS=0x...
[OS][KERNEL] KERNEL_THREAD_WAKES=0x...
[OS][KERNEL] KERNEL_THREAD_EXITS=0x0000000000000004
[OS][KERNEL] KERNEL_THREAD_REAPS=0x0000000000000004
[OS][KERNEL] USER_TO_KERNEL_SWITCHES=0x...
[OS][KERNEL] KERNEL_TO_USER_SWITCHES=0x...
[OS][KERNEL] WORK_QUEUE_REGISTERED=0x0000000000000000
[OS][KERNEL] WORK_QUEUE_QUEUED=0x0000000000000000
[OS][KERNEL] WORK_QUEUE_DELAYED=0x0000000000000000
[OS][KERNEL] WORK_QUEUE_RUNNING=0x0000000000000000
[OS][KERNEL] WORK_QUEUE_COMPLETIONS=0x...
[OS][KERNEL] WORK_QUEUE_FAILURES=0x...
[OS][KERNEL] WORK_QUEUE_CANCELLATIONS=0x...
[OS][KERNEL] WORK_QUEUE_COALESCED=0x...
[OS][KERNEL] WORK_QUEUE_DRAINS=0x0000000000000001
```

这些行只在全部用户进程结束后的统计区输出。Kernel Thread 的 create、switch、block、
wake 和 exit，以及 WorkQueue 的提交/执行热路径都只累加计数，不打印 TID、RSP、
handle 或逐次任务记录。

其中 create/exit/reap 的 4 包含两个 Kernel Thread 生命周期自检、一个 WorkQueue 自检
worker 和一个生产 writeback Worker。dispatch/yield/block/wake、双向切换和 WorkQueue
历史值随真实脏页与 deadline 路径变化，runner 只检查前缀；内核自身检查下界与最终归零。

第四增量追加同一最终统计区的 aging 聚合：

```text
[OS][KERNEL][AGING] TRACKED_PAGES=0x0000000000000000
[OS][KERNEL][AGING] CAPACITY=0x0000000000001000
[OS][KERNEL][AGING] HASH_CAPACITY=0x0000000000002000
[OS][KERNEL][AGING] ACTIVE_FILE_PAGES=0x0000000000000000
[OS][KERNEL][AGING] INACTIVE_FILE_PAGES=0x0000000000000000
[OS][KERNEL][AGING] ACTIVE_ANONYMOUS_PAGES=0x0000000000000000
[OS][KERNEL][AGING] INACTIVE_ANONYMOUS_PAGES=0x0000000000000000
[OS][KERNEL][AGING] PEAK_TRACKED_PAGES=0x...
[OS][KERNEL][AGING] OBSERVATION_ROUNDS=0x...
[OS][KERNEL][AGING] REFERENCED_OBSERVATIONS=0x...
[OS][KERNEL][AGING] UNREFERENCED_OBSERVATIONS=0x...
[OS][KERNEL][AGING] PROMOTIONS=0x...
[OS][KERNEL][AGING] DEMOTIONS=0x...
[OS][KERNEL][AGING] CANDIDATE_OBSERVATIONS=0x...
```

每轮不逐页、逐 alias 打印。只有失败才输出 `[AGING][FAIL]` stage/status；KindConflict
额外输出最后一个身份上下文。runner 把任意 aging failure 作为禁止 marker，正常结束要求
当前四队列为零且历史冷热计数达到下界。

第五增量在同一最终统计区追加后台回收聚合：

```text
[OS][KERNEL][RECLAIM] BACKGROUND_STATE=0x0000000000000000
[OS][KERNEL][RECLAIM] BACKGROUND_WAKES=0x...
[OS][KERNEL][RECLAIM] BACKGROUND_SLEEPS=0x...
[OS][KERNEL][RECLAIM] BACKGROUND_BATCHES=0x...
[OS][KERNEL][RECLAIM] BACKGROUND_CLEAN_FILE_PAGES=0x...
[OS][KERNEL][RECLAIM] BACKGROUND_WRITTEN_FILE_PAGES=0x...
[OS][KERNEL][RECLAIM] BACKGROUND_ANONYMOUS_PAGES=0x...
[OS][KERNEL][RECLAIM] BACKGROUND_RECLAIMED_PAGES=0x...
[OS][KERNEL][RECLAIM] BACKGROUND_NO_PROGRESS=0x...
[OS][KERNEL][RECLAIM] BACKGROUND_FAILURES=0x0000000000000000
```

这些行不进入逐页、逐批或交互终端热路径。`BACKGROUND_STATE=0` 表示最终 Sleeping；
written 只表示写回完成，仍需下一轮 aging/clean eviction 才计入 reclaimed。pressure 门禁
要求 wake/sleep、batch、clean、written、reclaimed 非零且 failure 为零；anonymous 份额
受批次阶段时序影响，第五增量只要求系统总 anonymous swap 非零。

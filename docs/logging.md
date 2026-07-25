# 启动日志规范

串口日志是本项目的启动观测接口，也是 QEMU 集成测试的稳定证据。日志必须同时满足“足够解释状态”和“不会淹没真正的故障”。

## 日志分层

| 层级 | 目的 | 允许频率 | 示例 |
| --- | --- | --- | --- |
| `MILESTONE` | 记录不可逆阶段边界 | 每个阶段一次 | `[OS][STAGE1] GDT_READY` |
| `DEVICE` | 记录设备初始化结果 | 每个设备一次 | `[OS][FIRMWARE] SERIAL_READY` |
| `FAILURE` | 记录停止原因 | 每条故障路径一次 | `[OS][FIRMWARE] STAGE1_CHECKSUM_INVALID` |
| `TRACE` | 调试内部细节 | 默认关闭 | 轮询计数、寄存器快照 |

当前串口格式使用稳定的组件和事件名代替显式等级字段：固件事件使用
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

QEMU 验收工具另用宿主单调时钟记录每条串口行抵达时刻，格式为
`[QEMU][T+000123ms]`。该字段由逐行读取线程在收到换行时生成，只描述测试进程
观察到日志的时间，不冒充来宾 PIT 时间。捕获器在最终必需里程碑到达后主动结束，
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

## v0.11 验收（历史基线）

以下日志记录 v0.11 完成时的正常启动边界，保留用于历史回归。v1.0 的当前
Shell 与控制台协议见文末。

```text
[OS][FIRMWARE] RESET
[OS][FIRMWARE] SERIAL_READY
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
[OS][KERNEL] HEAP_CAPACITY_BYTES=0x0000000000010000
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
- `FRAME_ALLOCATOR_READY` 后只打印 free、allocated、reserved 三类总数。
- `PAGING_READY` 只附带一个根物理地址，页级权限由
  `MEMORY_PERMISSIONS_VALID` 汇总。
- `HEAP_READY` 后只打印容量、自检结束活动数、峰值和最大连续空闲负载；
  `HEAP_SELF_TEST_PASSED` 表示真实写回、逆序释放、双向合并和一致性检查均
  已完成。禁止逐次记录分配、分裂或释放。

写保护故障镜像使用 `FAULT_INJECTION=WRITE_PROTECTION`，随后必须报告向量
14、错误码 `0x3` 和 CR2=`0xFFFF800000100000`。正常日志禁止出现任何
`FAULT_INJECTION`、`EXCEPTION` 或 `PANIC`。

设备日志遵循“初始化一次、热路径计数、消费时记录”的规则：

- IRQ0 不写串口，只在启动自检结束时汇总 tick 与毫秒。
- IRQ1 不在汇编入口格式化日志；C++ 事件循环消费首个完整事件后记录。
- ATA PIO 不逐字、逐扇区输出，只记录驱动可用与启动描述符校验结果。
- PIC 只记录最终 mask 和一次虚假 IRQ 自检，不逐次记录 EOI。

这样 1000 Hz 时钟不会淹没键盘、异常与失败标记，也避免串口轮询延长中断
服务时间。

用户日志分可信级别：

- `[OS][KERNEL] USER_*` 是内核根据已验证状态产生的生命周期证据。
- `[OS][USER] ...` 是经长度和地址检查后转发的用户文本，不能作为安全决策
  依据；测试只把三个内置验收程序的固定文本当作该镜像的行为证据。
- 系统调用不逐次打印。正常程序结束后只汇总一次
  `USER_SYSCALL_COUNT`，避免未来高频调用冲垮串口。
- 用户异常只输出向量、错误码、RIP 和可选 CR2，再输出一次终止与返回标记；
  不复用 `[OS][KERNEL] EXCEPTION`/`PANIC`，从协议上区分隔离事件与内核崩溃。

QEMU 捕获器继续给包括 Ring 3 文本在内的每一行加
`[QEMU][T+......ms]`。这解决“QEMU 里面也要看到时间”的观察需求，但该
前缀仍是宿主接收时间；来宾自身的可信单调时间只来自 PIT 汇总字段。

调度日志遵循“热路径零日志、冷路径批量汇总”：

- IRQ0 中只更新 tick、预算和 PCB 帧指针，不打印每次切换。
- `PROCESS_ID/CR3/KERNEL_STACK_TOP` 只在创建成功后各打印一次。
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
- 每份 PCB 结果额外打印自己的 pipe read/write 字节；生产者只能写 256，
  消费者只能读 256，两个 worker 两项都必须为零。

这里故意不把每次唤醒的 PID 顺序写进日志协议。唤醒意味着条件可能推进，
真正取得资源仍取决于 Ready 队列和 PIT 边界；把一种合法交错固化为测试会让
日志反过来限制调度器。

文件系统日志也只记录冷路径状态转换和汇总：

- 首次全零超级块只输出一次 `FILE_SYSTEM_FORMATTED`；后续启动输出
  `FILE_SYSTEM_MOUNTED`。二者互斥，不能用重复格式化掩盖持久化失败。
- 第二次启动读到上一实例留下的正确载荷后输出
  `FILE_SYSTEM_PERSISTENCE_RESTORED`。该标记必须早于本轮 Ring 3 执行。
- 格式、CRC、Dirty 状态或全盘语义一致性失败只输出一次
  `FILE_SYSTEM_CORRUPT` 和状态码，然后停止；失败路径禁止进入用户态。
- 正常启动在首次挂载和用户进程结束后各输出一次
  `FILE_SYSTEM_CONSISTENT`。最终同步、内核独立读回通过后分别输出
  `FILE_SYSTEM_SYNCED` 与 `FILE_SYSTEM_PAYLOAD_VALID`。
- superblock 代次、已分配 inode 和数据块只在最终阶段各汇总一次；缓存不按
  命中、未命中、逐块读写或逐次 flush 打印。
- 用户态仅在完整文件写入/同步和完整文件读回/EOF 后输出
  `FILE_WRITTEN`、`FILE_VERIFIED`，系统调用本身仍不逐次打印。

持久化 QEMU 用例中，每行仍带 `[QEMU][T+......ms]`，因此能直接比较第一次
格式化、第二次挂载恢复与第三次损坏拒绝的宿主到达时间；这个前缀不改变来宾
日志的稳定文本，也不冒充磁盘中的文件时间戳。

## v1.0 当前交互与控制台日志协议

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
而不是在内核里伪造固定统计。每个 PCB 只在终止汇总中记录控制台读写字节，
Shell 读取全部输入，后台三个程序均不读取控制台。

没有 Ready 进程时，内核不会周期性打印 `IDLE`。它在永久地址空间和默认
RSP0 上执行同一汇编块中的 `sti; hlt; cli`；IRQ0 可能只推进时间，IRQ1 提交有效字符后才按原因
唤醒等待 fd 0 的 Shell。禁止逐 tick、逐按键、逐 FIFO 操作打印，是为了不让
115200 波特串口反向改变中断时序，也让“日志丰富”保持为可解释的状态摘要。

当前正常镜像的进程编号固定为 PID1 Shell、PID2 生产者、PID3 消费者、PID4
worker。创建、终止、描述符关闭、管道字节、文件系统一致性与控制台统计都在
冷路径汇总；任何 `PANIC`、`EXCEPTION`、`USER_EXECUTION_FAILED`、
`DEVICE_INITIALIZATION_FAILED` 或控制台丢弃都使成功路径失败。

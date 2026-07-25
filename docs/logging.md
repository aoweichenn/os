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
未完成路径最多等待五秒；两种路径都必须显式终止并等待回收 QEMU，避免后台残留
模拟器进程。

## 防刷屏规则

- 轮询循环、每个扇区、每个字节和每次寄存器读写都不得默认打印。
- 成功路径只打印阶段完成事件；详细过程通过单元测试、随机测试或离线审计观察。
- 故障路径只打印一次稳定故障事件，然后停止或转入明确的故障处理状态。
- 若未来启用 `TRACE`，必须有编译期或启动期开关，并设置最大事件数；达到预算后只打印一次 `TRACE_LIMIT_REACHED`。
- 日志文本不得依赖本地化、时间戳或不稳定地址，保证测试和文档可复现。

## v0.7 验收

正常启动日志应按阶段边界递进：

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
[OS][KERNEL] HEAP_SELF_TEST_PASSED
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
- `HEAP_READY` 只打印容量，并用一次 `HEAP_SELF_TEST_PASSED` 表示真实写回。

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

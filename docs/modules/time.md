# Time 与 Deadline 模块

## 职责与非目标

v1.13 把 PIT 的离散硬件事件提升为内核统一使用的 64 位单调纳秒时钟，并把
“等待到某个时刻”纳入 ThreadScheduler。模块只回答经过时间和截止时刻，不
让日期或 RTC 进入调度和同步正确性判断。v2.2 另加一条完全独立的只读 RTC
墙钟路径；deadline 始终只消费单调时钟。

公开边界分为三层：

```text
8254 channel 0 / IRQ0
  → MonotonicClock：精确有理数累加、饱和、不倒退
  → DeadlineQueue：按 (deadline, sequence) 稳定排序
  → ThreadScheduler：Blocked + 唯一 WakeReason
  → sleep / private futex / ConditionVariable 用户接口
```

纯模型位于 `source/kernel/include/os/kernel/time/` 与
`source/kernel/src/time/`，既进入 freestanding Kernel，也以 host target
进入单元、集成和随机测试。中断运行时只负责在 IRQ0 到达时推进时钟并请求
解析到期项；它不拥有等待队列。

## 单调时钟换算

PIT 输入频率是 1193182 Hz，通道 0 的实际周期由编程后的整数除数决定。一次
tick 对应的精确纳秒增量是：

```text
tick_ns = divisor × 1,000,000,000 / input_frequency
```

整数除法会留下余数。若每次 IRQ 都丢弃余数，误差会随运行时间线性增长。
`MonotonicClock` 因此保存：

- 已提交的整纳秒；
- 上一次换算留下的分子余数；
- PIT 输入频率与实际除数；
- 饱和状态。

每次推进先把 tick 数切成满足 `chunk × divisor <= UINT64_MAX` 的有限分块。
对每块再分别求 `remaining_cycles × 1e9` 的商与余数，最后把旧余数加入
已经缩小的余数；这样周期乘积或“乘积加旧余数”的中间值越界时不会误判为
时间饱和。只有最终单调纳秒确实无法表示时才饱和到 `UINT64_MAX`，以后保持
不变，绝不回绕为较小时间。

## DeadlineQueue 所有权

当前 Thread capacity 为 512，因此 deadline queue 也有 512 个固定槽。每个
活动 Thread 最多拥有一个 deadline，Thread 索引可直接定位槽；活动槽另以
有序双向关系组成队列。排序键是：

```text
(absolute_deadline_nanoseconds, insertion_sequence)
```

sequence 使相同 deadline 的解析顺序稳定。队列只保存 Thread 索引、截止时刻
和序号，不保存用户地址或 futex 对象指针。这样 unmap、exec 和 ProcessExit
不会给 timer 层留下悬空用户地址。

`Schedule`、`ResolveExpired`、`Cancel` 和 `Validate` 都只允许在 scheduler
irq-save lock 下调用。队列统计分别记录 active、peak、schedule、expiration
与 cancellation；稳定态必须满足：

```text
active = schedules - expirations - cancellations
```

## 单赢家唤醒

带 deadline 的等待同时登记两个关系：

```text
Thread ──membership──> WaitQueue
Thread ──deadline────> DeadlineQueue
```

条件通知先取得 scheduler lock，若 Thread 仍为 Blocked，就以条件原因唤醒并
取消 deadline。IRQ0 同样在该锁下取最早到期项；若它仍为 Blocked，就以
`Timeout` 唤醒并从原 WaitQueue 摘除。先取得锁并完成状态转换的一方获胜，
后到者观察到 Thread 已不再等待，只能返回未完成，不能再次进入 Ready queue。

对于 timed futex，超时路径还在同一事务中把保存用户现场的返回寄存器改为
`TIMED_OUT`，并尝试释放已经没有 waiter 的 futex entry。普通 condition
通知则让 futex wait 返回成功；用户库重新取得 Mutex 后再次检查谓词。超时
和通知都不能绕过“返回前重新加锁”的 condition-variable 契约。

## 用户 ABI

固定 ABI 新增：

| 系统调用 | 编号 | 语义 |
| --- | ---: | --- |
| `GetMonotonicTime` | 54 | 返回当前 64 位单调纳秒 |
| `SleepUntil` | 55 | 非忙等阻塞到绝对 deadline |
| `WaitPrivateFutexUntil` | 56 | compare-and-block 并附带绝对 deadline |

用户运行时提供 `SleepFor` 的饱和相对换算，以及
`ConditionVariable::WaitUntil` 的强类型结果：
`ConditionSatisfied`、`TimedOut`、`Failed`。相对时长只在包装层换算一次；
内核、调度器和队列统一使用绝对 deadline，避免多层各自扣减造成误差。

## 运行时与日志边界

IRQ0 每次推进时钟并检查队首，但不逐 tick 打印。系统空闲于
`sti; hlt; cli` 时，PIT 仍会进入 Ring 0、解析到期 sleeper 并设置
need-resched，因此 `SleepUntil` 不需要忙等线程维持系统前进。

整机结束只输出一次汇总：

```text
[OS][KERNEL][TIME] ACTIVE_DEADLINES=0x...
[OS][KERNEL][TIME] PEAK_DEADLINES=0x...
[OS][KERNEL][TIME] DEADLINE_SCHEDULES=0x...
[OS][KERNEL][TIME] DEADLINE_EXPIRATIONS=0x...
[OS][KERNEL][TIME] DEADLINE_CANCELLATIONS=0x...
[OS][KERNEL][TIME] FUTEX_TIMEOUT_OPERATIONS=0x...
```

用户 `/bin/time_probe` 记录开始/唤醒单调纳秒，并分别提交 sleep、futex
超时、条件先赢和条件超时里程碑。64 MiB 档只有一个 Thread，条件先赢场景
明确记录 `CONDITION_SINGLE_THREAD_PROFILE`；256 MiB 与 64 GiB 必须运行
真实 notifier Thread。

## 验证与扩展边界

- 单元测试覆盖 PIT 除数余数累加、多 tick、饱和和无倒退；
- deadline queue 单元测试覆盖稳定顺序、重复登记、取消、到期和结构校验；
- 集成测试把 clock、scheduler、WaitQueue 与单赢家原因组合起来；
- 固定种子随机测试执行 100000 次 schedule/cancel/advance/expire；
- QEMU 同时证明空闲睡眠、真实 IRQ0、用户 ABI、资源守恒与有界宿主回收。

v2.2 的 GetRealtime 读取 QEMU PC CMOS：先等待 update-in-progress 清零，再
比较两份完整快照；按 status B 处理 BCD/binary 与 12/24 小时模式，最后用
Gregorian 闰年规则换算 Unix 秒。date 只输出 UTC `YYYY-MM-DDTHH:MM:SSZ`；当前
没有时区数据库、闰秒表、RTC 设置接口、tickless 或高精度硬件 timer。

以后替换 PIT 时，只能更换 monotonic clock source / event 输入，不能改变绝对
deadline 与 WakeReason；替换 RTC 也不能让可回拨墙钟进入 timeout 判断。

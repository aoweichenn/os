# ADR 0040：单调时钟、deadline queue 与 timed wait

- 状态：已接受
- 日期：2026-07-27
- 目标版本：v1.13

## 背景

v1.12 已经让条件满足、对象关闭和取消通过同一个 `WakeReason` 完成等待，但
`Timeout` 仍只是枚举预留。PIT IRQ0 当前只增加 tick 计数，读取统计时再计算：

```text
tick_count * divisor * 1000 / 1193182
```

这个公式没有保存除法余数，直接乘法还会在长运行边界溢出。它不能作为系统调用
ABI，也不能安全驱动绝对 deadline。

当全部用户 Thread 都阻塞时，Kernel 在永久栈上执行 `sti; hlt; cli`。此时
IRQ0 的中断帧来自 Ring 0，因此超时到期不能只放在“用户态定时器抢占”分支里。

## 决策

### 时间 ABI

新增 `os::abi::MonotonicTime`，唯一字段是 64 位无符号纳秒。它表示从本次启动
开始的单调时间，不表示 UTC、时区、日期或持久化 wall clock。

系统调用 54--56 分别为：

1. `GetMonotonicTime`；
2. `SleepUntil`；
3. `WaitPrivateFutexUntil`。

用户运行时另外提供溢出安全的 `SleepFor` 和
`ConditionVariable::WaitUntil`。相对时长加到当前时间时执行饱和加法。

### PIT 精确累加

`MonotonicClock` 保存：

- PIT 输入频率；
- 实际 divisor；
- 已交付 tick；
- 整纳秒；
- 小于输入频率的有理数余量；
- 饱和状态。

逻辑上每次推进使用：

```text
cycles = ticks * divisor
whole_seconds = cycles / input_frequency
remaining_cycles = cycles % input_frequency
fraction = remaining_cycles * 1_000_000_000 + old_remainder
```

实现不能直接把上述表达式当作 64 位求值顺序：先把 ticks 切成
`chunk * divisor` 可表示的有限分块，再分别约化
`remaining_cycles * 1_000_000_000`，最后加入旧余数。这样只消除中间值
越界，不改变有理数结果。只有最终纳秒确实超过 `UINT64_MAX` 时才永久饱和，
绝不回绕。PIT 的整数除数造成实际频率与请求频率不同；时间换算始终使用输入
频率和真实 divisor，不使用截断后的 `actual_frequency_hz`。

### deadline queue

Kernel 新增 `time` 功能目录。`DeadlineQueue` 使用固定 512 项存储，每个
Thread index 直接对应一个 entry，并通过双向链按：

```text
(absolute_deadline_nanoseconds, registration_sequence)
```

稳定排序。同一 Thread 最多拥有一个 deadline，所以条件唤醒可以 O(1) 取消，
到期处理可以从队首依次取得。

队列记录 scheduled、expired、cancelled、active 和 peak 计数。sequence
耗尽时明确拒绝新登记，不允许回绕后改变同 deadline 的 FIFO 次序。

### 单赢家提交

deadline 登记、WaitQueue 入队、条件唤醒、到期唤醒和取消都在现有
`scheduler_lock` 的 irq-save 临界区中执行：

```text
Running
  → register deadline
  → Blocked + WaitQueue ownership
  → ConditionSatisfied / Timeout / Signal / ObjectClosed / Cancelled
  → first winner removes both WaitQueue and deadline ownership
  → Ready
```

`WakeThread` 是唯一完成点：

- `Timeout` 获胜时把 deadline 记为 expired；
- 其他原因获胜时把 deadline 记为 cancelled；
- 第二个完成者看到 Thread 已不再 Blocked，得到 `WakeAlreadyResolved`。

因此条件与同一个 tick 到期同时发生时也只会进入一次 Ready。

### IRQ 与空闲路径

IRQ0 先推进单调时钟，再无条件处理到期 deadline。该步骤不切换上下文，只把
Thread 变为 Ready 并请求稍后调度：

- 中断来自用户态时，保存当前现场后执行既有量子调度；
- 中断来自 Ring 0 空闲路径时，返回 `sti; hlt; cli` 循环，由永久栈上的调度
  入口选择刚到期的 Thread。

IRQ 热路径不打印逐 tick 日志。仅在启动自检、二次幂计数和用户整机探针里记录
具名时间里程碑。

## 失败语义

- 非法 ABI 大小或用户指针：`INVALID_ARGUMENT` / `INVALID_USER_MEMORY`；
- deadline 已到：`SleepUntil` 立即成功，timed futex 返回 `TIMED_OUT`；
- futex word 已变化优先返回 `FUTEX_VALUE_CHANGED`；
- deadline 容量或 sequence 耗尽：显式资源错误，不退化为忙等；
- unmap、exec、ProcessExit 和异常取消 waiter 时同步取消 deadline；
- 时钟饱和后，所有不大于 `UINT64_MAX` 的 deadline 都可到期，时间不会倒退。

## 不选择的方案

### 用请求的 100 Hz 直接令每 tick 等于 10 ms

PIT divisor 只能取整数，实际频率并不精确等于请求频率。长期会积累可避免漂移。

### 每个 waiter 在用户态轮询时间

这会占用量子、破坏 HLT 空闲，并重新引入检查条件到睡眠之间的丢失唤醒窗口。

### 只在用户态 IRQ 分支处理 timeout

全部 Thread 阻塞后 IRQ 来自 Ring 0，系统会永久睡眠。

### 在 IRQ0 中直接切换 Thread

项目冻结的单 BSP 规则要求 IRQ 只提交状态并设置调度请求，不在任意 Kernel
调用链中切换上下文。

## 验证

- 单元测试覆盖 PIT 舍入、余量、批量长 tick、饱和和不倒退；
- deadline queue 单元测试覆盖稳定排序、取消、到期和边界失败；
- 100000 步固定种子虚拟时间模型对照参考集合；
- scheduler 测试覆盖 condition/timeout 单赢家和终止取消；
- ABI 集成测试冻结结构尺寸、系统调用号和错误码；
- QEMU `/bin/time_probe` 验证读取、非忙等 sleep、timed futex、
  ConditionVariable deadline、同 tick 竞争和资源归零；
- 64 MiB、256 MiB、64 GiB 使用同一镜像，串口打印来宾单调时间。

## 阶段边界

v1.13 不实现 wall clock、RTC、时区、闰秒、TSC 校准、tickless、HPET、
POSIX `clock_*` ABI、signal 或跨 CPU timer。上述能力继续留给后续阶段。

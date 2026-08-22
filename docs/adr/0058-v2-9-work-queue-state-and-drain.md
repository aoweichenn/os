# ADR 0058：V2.9 WorkQueue 状态、延迟堆与 drain

状态：已接受（第二增量）

日期：2026-08-21

## 问题

Kernel Thread 已能运行，但直接把文件写回函数塞进线程入口会缺少任务身份、重复提交、
延迟执行、取消和退出排空语义。队列若持有 spinlock 调用任务，又会把未来的 VFS/设备
I/O 带入不可睡眠临界区。

## 决策

`WorkQueue` 是不依赖 VFS、设备、页表和 ThreadScheduler 的纯状态模块。调用方提供
`WorkQueueEntry[]` 与 delayed heap 数组；注册得到 `(slot,generation)` 句柄。条目状态为
Free、Idle、Delayed、Queued、Running、Completed、Cancelled。

即时任务进入侵入式 FIFO。延迟任务进入按 `(deadline, enqueue_sequence)` 排序的二叉
最小堆；`AcquireNext(now)` 先把到期项按稳定顺序追加到 FIFO，再把一个条目提交为
Running。返回的 operation/context 在锁外执行，调用方随后用相同 generation handle
提交成功或失败结果。

同一 handle 在 Delayed/Queued/Running 时再次提交只增加 coalesced 统计。Queued 或
Delayed 可以取消，Running 返回 AlreadyRunning。Completed/Cancelled 必须先 Reset 才能
再次提交，Release 只接受非活动状态。复用槽位时 generation 加一，旧 handle 明确返回
StaleHandle。

`BeginDrain` 封闭注册和新的 Idle 提交；已有重复提交仍报告 AlreadyPending。只有
Delayed/Queued/Running 全部归零后 `EndDrain` 才重新开放。任务失败只增加失败统计，
worker 继续取得后继任务。

## 第一生产接入

ProcessRuntime 在 PID1 前注册六个任务：三个即时任务、一个延迟任务、一个随后取消的
任务和一个用于验证 drain 拒绝的 Idle 任务。单个真实 Kernel Thread worker 执行四个
任务，其中一个返回 Failed；队列仍完成 delayed work 和 drain，随后 reset/release 全部
句柄。writeback 保持旧 safe-point 路径。

## 不变量

- 每个非 Free entry 只有一个当前 generation；
- Queued 恰出现于 FIFO 一次，Delayed 恰出现于 heap 一次；
- heap 父项不晚于子项，相同 deadline 按 sequence 稳定；
- Running 不在 FIFO/heap，operation 永不在锁内调用；
- registration = registered + release；
- drain 完成时 delayed/queued/running 均为零；
- 失败或 stale 操作不得改变队列拓扑。

## 后果

V2.9.3 可以把现有 writeback 请求映射为持久 WorkItem。当前 worker 仍运行在没有 User
Thread 的独立批次，没有 park/unpark、真实单调 deadline 唤醒或 User/Kernel 混合调度，
因此尚不能宣称 safe-point writeback 已被替换。

## 后续状态

V2.9.3 已按 [ADR 0059](0059-v2-9-mixed-worker-writeback.md) 完成上述生产迁移，并为
WorkQueue 增加最早 deadline 查询和即时请求提升 Delayed 项；本 ADR 的原始第二增量边界
保留为历史记录。

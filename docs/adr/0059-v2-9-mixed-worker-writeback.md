# ADR 0059：V2.9 混合 dispatcher 与常驻 writeback Worker

状态：已接受（第三增量）

日期：2026-08-21

## 问题

第二增量只能在没有 User Thread 时运行 Kernel Thread。若调度决定把 User 直接切到
Kernel，用户 ExceptionFrame 会被误作 Kernel saved RSP；反向切换也会误用现场。
同时，在 `OsKernelPrepareUserReturn` 直接执行 VFS writeback 会把存储延迟继续计入用户
返回路径，延迟 WorkItem 也没有真实 scheduler deadline 可以唤醒。

## 决策

`ExecuteProcesses` 成为两类 Thread 的共同 dispatcher。User→Kernel 先保存用户 frame/FX，
切回内核页表，清理 CpuLocal/TSS/FS 和入口状态，再恢复 User dispatcher stack；
Kernel→User 保存 Kernel RSP/FX 并恢复 Kernel dispatcher stack。dispatcher 按
`ThreadKind` 调用各自汇编入口。同类型切换保持直接路径，Ring 0 不增加 timer 抢占。

ProcessRuntime 注册一个常驻 writeback WorkHandle 和一个 `KernelWork` WaitQueue。普通
user-return safe point 只检查请求并提交工作：后台请求即时入队，低于软水位但已有 Dirty
页时使用 `now+5s`。即时请求遇到 Delayed 状态会从最小堆提升到 ready。Worker 在锁外
保护 shared aliases 并调用既有 64 页 batch writeback，完成后协作 yield；无 ready 工作
时读取最早 deadline 并阻塞。

timer IRQ 只让 scheduler 到期 `KernelWork` deadline、把 Worker 置 Ready 并请求重调度，
不调用页表保护、VFS、journal 或设备 I/O。硬 Dirty limit 仍同步回写，显式同步调用和
进程退出 flush 也保持同步完成语义。

最后一个 User Thread 退出后，dispatcher 取消残余 Queued/Delayed 工作并唤醒 Worker。
Worker 退出后按 scheduler reap、KernelStack destroy、WorkHandle release 的顺序收束。

## 不变量

- User ExceptionFrame 与 Kernel saved RSP 永不交叉解释；
- 每次跨类型切换后 CR3、TSS.RSP0、CpuLocal、FS 和 FX 属于目标 Thread；
- 从硬件中断放弃用户返回栈前必须先配对 `LeaveInterrupt`；
- 从原生系统调用放弃返回尾部时必须通过 ClearCurrentThread 收束入口状态与 GS；
- WorkQueue operation 永不在队列锁或 timer IRQ 内执行；
- Worker 每个 batch 有界，完成后主动 yield；
- 停止后 Kernel Thread、WaitQueue waiter、deadline、WorkItem 和动态栈均为零。

## 后果

常规 writeback 已不再占用 user-return safe point，延迟老化也由真实单调 deadline 驱动。
单 BSP Kernel Thread 仍是协作式；耗时 operation 必须自行分批。5 秒窗口当前是冻结常量，
尚无 sysctl。PTE Accessed active/inactive 队列和后台 reclaim Worker 留给后续增量。

# ADR 0057：V2.9 Kernel Thread 生命周期与协作切换

状态：已接受（第一增量）

日期：2026-08-21

## 问题

V2.8 的文件写回只能在返回用户态前的安全点运行。`ThreadScheduler` 又假定每个
Thread 都属于一个用户 Process，因此无法表达没有用户 CR3、用户栈、TLS 和信号状态的
内核执行实体。直接伪造 Process 会污染 PID、进程树、OOM 候选和资源统计。

## 决策

`ThreadEntry` 增加 `ThreadKind::{None, User, Kernel}`。User Thread 继续进入 Process
线程链并使用低半 TID；Kernel Thread 的 `process_index` 固定为 invalid，使用从
`0x8000000000000000` 开始的独立高位 TID，不改变 PID1/TID1。

Kernel Thread 仍使用 `KernelStackManager` 的四页动态栈和双 guard。初始栈保存六个
SysV 被调用者保存寄存器、RFLAGS、bootstrap RIP 与一个对齐槽；协作切换另保存
FXSAVE 状态。首次进入保存调度调用链，普通切换保存前一 RSP，阻塞且无就绪项时保存
RSP 后回到调度循环，最后一个 Thread 退出时放弃其调用链并恢复调度栈。

第一增量只允许在尚无 User Thread 时批量运行 Kernel Thread。它支持创建、yield、
WaitQueue block/wake、退出和安全点 reap，但不把 Kernel Thread 混入正在运行的用户
调度，也不迁移 writeback。Ring 0 timer IRQ 只记时并返回，Kernel Thread 仍是协作式；
抢占和统一 Worker 调度留给后续增量。

## 不变量

- Kernel Thread 不占 Process 槽、地址空间、用户栈、TLS 或 SignalManager 条目；
- User/Kernel TID 区间不重叠，用户首线程编号保持 1；
- 切换期间 IF 关闭，下一上下文发布到 CpuLocal/TSS 后才更换 RSP；
- 退出栈只能在恢复调度栈后销毁；
- 创建失败必须同时撤销 scheduler entry 和动态内核栈；
- 完成后 active Kernel Thread、scheduler entry、内核栈、KVA 和物理页全部回到基线。

## 验证

宿主单元测试覆盖独立 TID、无 Process 所有权、非法栈拒绝、yield、block/wake、exit、
reap 和用户专属状态拒绝。十万步随机模型混合 User/Kernel Thread，并逐步执行
`Validate`。4 GiB QEMU 在 PID1 前实际运行两个 Kernel Thread，要求 2 次创建、5 次
dispatch、2 次 yield、1 次 block/wake、2 次 exit/reap，最终 active 为 0。

## 后果

V2.9.2 可以在该生命周期之上建立 WorkQueue。当前没有跨 User/Kernel 的即时切换、
Kernel Thread timer preemption、取消运行中任务或持久 worker；因此 V2.8 safe-point
writeback 暂时保留。

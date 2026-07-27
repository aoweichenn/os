# ADR 0041：进程信号、用户帧与受控 sigreturn

- 状态：已接受
- 日期：2026-07-27
- 目标版本：v1.14

## 背景

v1.13 已经把条件满足、超时、对象关闭与取消收敛到
`ThreadScheduler::WakeThread` 的单赢家状态转换，但 `WakeReason::Signal`
仍没有生产者。若信号自行修改 Ready 队列或只在系统调用分发器里检查，它会与
deadline 形成第二套完成协议，并且无法覆盖被 IRQ 抢占后直接返回用户态的 Thread。

信号还跨越三种所有权：

- disposition 属于 Process，必须被同一 Process 的全部 Thread 共享；
- mask、定向 pending 与活动 handler frame 属于 Thread；
- 发往 Process 的普通 pending 在选择到一个合格 Thread 前属于 Process。

用户 handler 不能在 Ring 0 上直接调用。Kernel 必须把被中断现场编码为用户栈
上的固定 ABI 帧，改写下一次用户返回现场；handler 完成后再用专用系统调用把
控制权交还 Kernel。用户可以修改该帧，因此 sigreturn 是新的特权返回入口，
必须采用和原生系统调用返回相同甚至更严格的验证。

## 决策

### 固定 ABI

新增系统调用 57--63：

1. `SetSignalAction`；
2. `SetSignalMask`；
3. `SendProcessSignal`；
4. `SendProcessGroupSignal`；
5. `SignalReturn`；
6. `GetProcessGroup`；
7. `SetProcessGroup`。

支持 1--63 号普通位图信号，当前具名信号包括 Kill、User1、User2、Terminate
和 Child。Kill 不可屏蔽、不可忽略、不可安装 handler。普通信号采用位集合
语义：同一信号在 pending 期间重复发送只保留一份，不排队保存次数。

`SignalAction` 固定为 40 字节；`SignalUserContext` 逐字段冻结为 176 字节；
`SignalFrame` 固定为 240 字节并包含 magic、version、size、不可预测 cookie、
信号号、原 mask、restorer 和完整用户现场。公共 ABI 只使用明确位宽类型。

### 三层信号状态

`SignalManager` 独立维护：

```text
ProcessSignalState
  ├─ process_id / process_group_id
  ├─ process_pending_set
  ├─ dispositions[63]
  └─ round_robin_thread_cursor

ThreadSignalState
  ├─ thread_id / process_index
  ├─ signal_mask / thread_pending_set
  └─ active frame address / cookie / signal / restorer / previous mask
```

发往 Process 的普通信号先检查是否已在 Process 或任一 Thread pending。若已
存在则只增加 coalesced 统计。否则从轮转游标开始选择一个未屏蔽且属于该
Process 的 Thread；没有合格 Thread 时保留在 Process pending。mask 改变、
Thread 注册或活动 handler 完成后重新分配 Process pending。

当前单 BSP Kernel 不可抢占，Thread 上下文中的信号状态变更不会和另一个
Kernel Thread 并行；会与 IRQ 共享的调度状态仍由现有 irq-save scheduler
锁提交。未来 SMP 化前，`SignalManager` 必须再获得独立锁和明确锁序。

### disposition 与生命周期

- Default：除 Child 默认忽略外，终止整个目标 Process；
- Ignore：入队前或投递时消费，不进入 handler；
- Handler：只在返回用户态边界构造一帧并进入用户函数。

`fork` 复制 Process dispositions、进程组与调用 Thread 的 mask，不复制 pending
或活动 handler frame。`exec` 保留 Ignore，重置 Handler 为 Default，清空
pending 与活动 frame，并只保留提交 exec 的 Thread。ThreadExit 回收其 pending
到 Process 后重新选择；ProcessExit 删除全部信号状态。最终资源门禁要求活动
Process/Thread 信号状态均为零。

### 与等待的单赢家关系

发送信号选择到 Blocked Thread 时不直接接入 Ready 队列，而是调用：

```text
WakeThread(wait_queue, thread_index, WakeReason::Signal)
```

该调用与 condition、timeout、close 和 cancel 共用 scheduler irq-save lock。
胜者移除 WaitQueue membership、取消 deadline、设置唯一 wake reason，并只
发布一次 Ready。失败者看到等待已解析，不得再次完成。

阻塞调用在 Kernel 保存系统调用号和 restartable 属性。信号获胜后先把保存现场
的 RAX 设为 `INTERRUPTED=-52`。若 action 含 restart flag 且策略表允许重启，
构造信号帧时把保存 RIP 回退两字节到 `SYSCALL`，并把保存 RAX 恢复为原系统
调用号。当前管道/描述符读写、wait process 与 join 可重启；sleep 和 futex
明确不可重启。已经提交的部分 I/O 不回退。

### 用户返回帧

Kernel 在目标 Thread 的用户栈上向下构造：

```text
高地址
  被中断时的 RSP
  SignalFrame（16 字节对齐）
  restorer 返回地址（handler 入口 RSP % 16 == 8）
低地址
```

主栈若需要跨入紧邻的未提交页，复用用户页故障的 writable/user 与栈增长间距
规则逐页扩展；用户 Thread 自带匿名栈只允许在其登记边界内使用已有 VMA。
handler 入口取得 `RDI=signal_number`、`RSI=frame_address`。普通 C++ `ret`
到 Intel NASM restorer；restorer 以当前 RSP 作为精确帧地址调用系统调用 61，
成功后不再返回。

### sigreturn 验证

`SignalReturn` 只接受当前 Thread 已登记的唯一活动帧，并验证：

- 用户提供地址等于 Kernel 记录地址、16 字节对齐且完整位于该 Thread 用户栈；
- magic、version、size、reserved、cookie、信号号、restorer 与原 mask 精确；
- mask 不含不可屏蔽位；
- RIP/RSP 为当前 48 位低半 canonical 用户地址；
- CS、SS 与冻结用户 selector 一致；
- RFLAGS 不含危险保留状态；
- RIP 所在页为用户 R-X，RSP 所在页为用户 RW-NX；
- 恢复栈仍属于当前 Thread 的登记栈范围。

任一失败记一次 rejected frame，并以“非法用户返回”终止目标 Process，等待结果
为 Exception/vector 13。Kernel 不 panic，也不继续使用攻击者现场。

## 失败语义

- 非法信号、flag、Kill action 或进程组：`INVALID_ARGUMENT`；
- 不存在的 PID/进程组：`PROCESS_NOT_FOUND=-53`；
- 用户 action/previous-mask 指针不可访问：`INVALID_USER_MEMORY`；
- 无活动帧或帧身份不匹配：`SIGNAL_STATE_INVALID=-54`，目标 sigreturn 路径按
  非法返回隔离；
- handler/restorer 不在用户程序窗口：拒绝安装；
- 信号帧无法获得合法栈页、不可执行 handler 或可写栈：终止目标 Process；
- 发送与进程退出在当前单 BSP 串行，不能留下指向已回收 Thread 的选择结果。

## 不选择的方案

### 在 Kernel 栈上直接调用用户 handler

这会在 CPL0 执行用户地址、混淆异常归属，并让 handler 的普通栈操作破坏 Kernel
栈；不可接受。

### 信号自行把 Thread 放入 Ready 队列

这会绕过 deadline cancellation 和唯一 WakeReason，产生双重 Ready 与过期
deadline 残留。

### 完全复制 Linux `rt_sigframe`

项目尚无完整 POSIX `siginfo_t`、浮点 XSAVE 扩展或动态 trampoline ABI。复制
表面布局会制造并不存在的兼容承诺。当前帧只冻结本项目已经能够验证和恢复的
x86-64 状态。

### 每次普通信号都排队

传统普通信号本来就是合并语义；完整实时信号队列需要容量、排序与逐项资源失败
契约，留到 v2.0 之后。

## 验证

- 单元测试覆盖线程选择、mask、合并、Ignore/Default/Handler、帧身份以及
  fork/exec 生命周期；
- scheduler 集成测试证明 Signal 与 condition/timeout 的单赢家和 deadline
  取消；
- 固定种子随机模型执行 100000 次 send/mask/deliver/complete，逐步验证一个
  普通信号不会同时归属多个 Thread；
- ABI 集成测试冻结系统调用 57--63 与 40/176/240 字节布局；
- QEMU `/bin/signal_probe` 验证阻塞描述符重启、mask 合并、独立进程组、
  fork 继承、畸形帧隔离和默认终止；
- 64 MiB、256 MiB 与 64 GiB 终态验证信号状态归零、ProcessTree 无 Zombie、
  deadline 无残留，Kernel 汇总计数与用户 marker 一致。

## 阶段边界

v1.14 不实现实时信号队列、`siginfo_t`、alternate signal stack、暂停/继续状态、
session、controlling TTY 或 Ctrl-C。v1.15 在当前进程组和信号交付边界之上
实现 TTY 与作业控制，不得把终端输入直接绑定到任意 PID。

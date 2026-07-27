# Process 与 Thread 模块

## 职责

`source/kernel/{include,src}/os/kernel/process` 负责：

- Process/Thread 容量、身份、状态和调度；
- ready queue、WaitQueue 和阻塞/唤醒；
- 父子进程树、Zombie、reparent 和 wait；
- 用户程序参数布局；
- 地址空间、VMA、KernelStack、扩展现场、FileTable 与 FsContext 的运行时编排；
- spawn、exec、exit、异常终止和最终资源统计。

模块不负责解析 rootfs 盘面、操作 ATA 端口或解释 Shell 命令。可执行文件内容
经 VFS/ELF 接口输入；页表与 frame 经 memory 接口管理；用户 ABI dispatcher
位于 `user/system_calls.cpp`。

## 文件边界

| 文件 | 责任 |
| --- | --- |
| `thread_scheduler.*` | Process/Thread 槽位、PID/TID、run queue、WaitQueue、状态转换 |
| `wait_queue.*` | 有界 FIFO 等待队列状态与统计 |
| `process_tree.*` | 父子关系、Zombie、reparent、wait 和进程树不变量 |
| `program_arguments.*` | `argc/argv/envp` 长度规划、栈地址计算和精确上限 |
| `process_runtime.*` | 跨内存、VFS、fd、栈、现场和调度器的资源事务 |

private futex 的 entry/WaitQueue 容器位于 `sync/private_futex.*`，但 key 的
AddressSpaceId、用户内存复查、阻塞调度和取消由 ProcessRuntime 组合。

公开头文件与实现源文件保持同名、同模块相对路径；纯逻辑的 ProcessTree、
ProgramArgumentPlan 和 ThreadScheduler 组成 hosted 测试库。

## 依赖方向

```text
ABI / arch frame / memory / VFS / I/O objects
                    |
                    v
             ProcessRuntime
              /     |      \
             v      v       v
       ProcessTree  Scheduler  ProgramArgumentPlan
                         |
                         v
                     WaitQueue
```

ProcessTree 不依赖 VFS、页表或 FileTable；ProgramArgumentPlan 不访问用户
内存；ThreadScheduler 不解释 ELF 或进程路径。跨模块所有权只由
ProcessRuntime 组合。

## 身份与容量

- `ProcessId` 与 `ThreadId` 是单调 64 位身份；
- `process_index` 与 `thread_index` 是内部可复用槽位；
- 64 MiB：8 Process、8 Thread、每 Process 1 Thread；
- 256 MiB：64 Process、128 Thread、每 Process 32 Thread；
- 64 GiB：256 Process、512 Thread、每 Process 64 Thread；
- 配置由受管可用内存选择，不由宿主架构选择。

用户 ABI 和日志只暴露 PID，不暴露槽位。

## ProcessTree 不变量

- 初始化前所有操作失败；
- PID1 只能注册一次，PID 必须为 1；
- 普通子进程只能挂在 Alive 父进程下；
- 活动 PID 唯一、非零且不能等于 wait-any 哨兵；
- Alive 没有退出原因；Zombie 必须有退出原因；
- 非 PID1 项必须具有可达的活动父项；
- 非 PID1 父进程退出时，所有子项改挂 PID1；
- PID1 有子项时不能退出；
- wait 只能收集调用父进程的 Zombie；
- registered = active + collected；
- exited = zombie + collected；
- 最终 active/alive/zombie 均为零。

## 参数布局不变量

- 参数/环境分别最多 256 项；
- 字符串及 NUL 合计最多 128 KiB；
- 每个长度使用 64 位并检查加法溢出；
- Finalize 前只能追加长度，Finalize 后计划只读；
- RSP 16 字节对齐；
- argv 和 envp 都有 null 终止项；
- 字符串区域连续且精确结束于固定栈顶；
- 失败不发布半完成 layout。

## spawn 资源事务

正向取得：

```text
path/argument snapshot
  -> candidate user address space
  -> scheduler Process
  -> KernelStack
  -> scheduler Thread
  -> initial UserContext/FXSAVE
  -> FsContext
  -> FileTable and standard descriptors
  -> ProcessTree entry
```

任一步失败按逆序释放。成功后地址空间所有权从局部候选转移到
`ProcessRuntimeProcess`。当前 spawn 不复制父 FileTable/cwd，不等于 fork。

## exec 提交协议

提交前旧映像保持权威。候选完成 ELF、页表、用户栈和 CR3 激活探针后，
`ThreadScheduler::CommitProcessImage` 在单线程历史路径只允许当前 Thread。
v1.12 多线程路径会在候选映像完整后先取消旧 futex waiter、终止并回收
sibling，再满足以下提交条件：

- Process Alive；
- 恰有一个 Thread；
- 该 Thread 是当前 Running Thread；
- 新 CR3 非零。

成功提交后：

1. RuntimeProcess 接管候选地址空间；
2. 调用 FileTable close-on-exec；
3. 销毁旧地址空间的驻留页、私有页表和全部 VMA 描述符；
4. 重建 UserContext；
5. 激活候选 CR3。

提交前失败返回用户错误；提交后的内部失败不可安全继续，进入停机。

## exit/wait 回收协议

退出线程保存现场与结果，关闭 FileTable，释放 FsContext，进程树进入 Zombie
并执行 reparent。调度器选择下一 Thread 后切回 Kernel 页表，再销毁退出
进程用户地址空间。

wait 路径先回收 Exited Thread 的 KernelStack，再收集 ProcessTree Zombie，
最后回收调度器 Process。PID1 自己由 Kernel 在无子项后调用 `CollectInit`。

v1.8 的地址空间生命周期还必须维护两类相互独立的事实：

- `VirtualMemoryMap` 保存 ELF、匿名区、program break 与用户栈的区间意图；
- 页表保存已经驻留的 frame 与硬件 R/W/X 权限。

spawn/exec 候选失败要同时释放两者；退出路径必须先在永久 Kernel CR3 上销毁
用户页表，再归还该地址空间拥有的全部 VMA 节点。阶段末比较描述符池的
active/free 与 acquire/release 增量，不能只检查 Process 槽位归零。

## 并发边界

当前为单 BSP、“中断可进入、Kernel 不可抢占”模型。调度器状态更新由
irq-save spinlock 保护；阻塞操作不能在持有普通 spinlock 时执行。child
exit 使用统一 WaitQueue，允许无关唤醒，用户包装器必须重试条件。

参数计划当前为全局工作区，依赖同一 BSP 的系统调用内核路径不并发重入。
v1.12 已允许同一 Process 多 Thread exec：候选构造失败保持 sibling，候选
成功后由调用 Thread 收敛为新映像唯一 Thread。引入 SMP 或 Kernel
preemption 前仍必须把全局工作区改为调用级或 Process 级所有权。

## 失败语义

| 情况 | 结果 |
| --- | --- |
| 请求结构尺寸错误 | `InvalidArgument` |
| 请求或结果用户页不可访问 | `InvalidUserMemory`，不得回收子进程 |
| 参数项/字节超限 | `ArgumentListTooLarge` |
| 路径不存在/无法读取 | 可执行读取错误 |
| ELF 格式/W^X/范围错误 | `InvalidExecutable` |
| Process 容量耗尽 | `ProcessLimitExceeded` |
| 子进程仍运行 | 当前 Thread 阻塞 |
| 不存在匹配子进程 | `NoChildProcess` |
| exec 候选失败 | 旧映像、PID、fd 和父子关系保持 |
| 内部状态守恒失败 | 停机或整机验收失败，不伪装用户错误 |

## 测试

- `os_kernel_thread_scheduler_unit_tests`
- `os_kernel_process_tree_unit_tests`
- `os_kernel_program_arguments_unit_tests`
- `os_kernel_thread_scheduling_integration_tests`
- `os_kernel_process_lifecycle_integration_tests`
- `os_kernel_user_virtual_memory_lifecycle_integration_tests`
- `os_kernel_thread_scheduler_randomized_tests`
- `os_kernel_process_models_randomized_tests`
- normal、用户 VM/guard/保护异常、非法 ELF 与资源回收 QEMU 系统测试

进程树背景与代码走读见
[v1.7 学习章](../learning/15-v1.7-pid1-process-tree-exec.md)；地址空间增量见
[v1.8 学习章](../learning/16-v1.8-anonymous-vma-demand-paging.md)。设计取舍见
[ADR 0034](../adr/0034-pid1-process-tree-disk-exec-wait.md) 与
[ADR 0035](../adr/0035-anonymous-vma-demand-paging-user-heap.md)。

## v1.10 fork 事务

`ForkCurrentProcess` 把 Process 共享资源与调用 Thread 现场组合成一个候选
child。子现场复制通用寄存器和 FXSAVE，并把返回寄存器改为 0；父现场只在
成功发布后获得子 PID。其他 Thread 不进入 child。

资源顺序为：

```text
Process/Thread slot
  → AddressSpace + VMA/FileBacking/cache refs
  → FileTable exact clone
  → FsContext clone
  → KernelStack + FXSAVE/UserContext
  → parent COW commit
  → ProcessTree/run queue publish
```

父 PTE 只在候选 child 已完整以后修改。提交失败先销毁 child，让共享 frame
引用回落，再恢复 parent writable PTE 和独占元数据；随后逆序释放其余对象。
成功 child 可立即 exec，exec 仍通过既有候选映像原子替换路径，COW 页由旧
地址空间销毁过程释放。

FileTable clone 保留精确 fd 与 fd flags，但表项强引用同一个
FileDescription，所以 offset 共享。FsContext 初值相同、后续修改独立。
详细控制流见
[v1.10 学习章](../learning/18-v1.10-fork-copy-on-write.md) 和
[ADR 0037](../adr/0037-fork-copy-on-write.md)。

## v1.11 流水线 Process 事务

Shell 的一个 N 级流水线最多创建 N 个 Process 和 N-1 根动态 Pipe。执行器
不会先发布全部孩子再补接 fd，而是为当前 stage 准备 stdin/stdout，使用
`DuplicateDescriptorTo` 精确安装到 0/1，再 exec rootfs ELF。父 Shell
立即关闭已经不再需要的端点，保证 EOF 只取决于真实 writer 生命周期。

任一步失败时，尚未发布的资源逆序关闭，已发布孩子仍被逐个 wait；成功路径
也等待所有 stage，而不是只等待最后一个。因而 ProcessTree 的 Zombie 数、
FileTable 强引用、PipeManager active slot 和动态 Pipe 物理页在命令返回
prompt 前共同回到基线。16 级整机用例验证 16 次 spawn、15 根 Pipe 和 16 次
wait 的组合边界。

## v1.12 用户 Thread 与 private futex 事务

`CreateCurrentProcessThread` 先验证用户 entry/stack/TLS VMA，再创建
KernelStack。ThreadScheduler 的 Create 与 `UserContext`/FXSAVE/runtime
metadata 初始化位于同一 irq-save 发布临界区；中断恢复后新 Thread 才能
被选中。

ThreadExit 只把当前 Thread 变为 Exited 并发布 64 位退出值。第一个 Join
调用者取得 join ownership；目标退出后先销毁 KernelStack、Reap 调度槽，
再由用户库释放栈/TLS。ProcessExit 则取消 AddressSpaceId 上全部 futex，
从 Ready/Blocked 队列撤销 sibling，关闭共享资源并进入 ProcessTree Zombie。

private futex 的最后一次 word 比较和 BlockCurrentThread 共用调度器锁。wake、
unmap cancellation、exec cancellation 与 ProcessExit cancellation 也在该锁
内解析 WaitQueue 单赢家，队列清空后归还 512-entry 管理器槽。

详细控制流见
[v1.12 学习章](../learning/20-v1.12-user-threads-tls-private-futex.md) 与
[ADR 0039](../adr/0039-user-threads-fs-tls-private-futex.md)。

## v1.13 deadline 调度事务

ThreadScheduler 现在直接拥有 DeadlineQueue。`BlockCurrentThreadUntil` 在
同一 irq-save 临界区把 Running Thread 同时登记到业务 WaitQueue 与绝对
deadline；若 deadline 已到达则不改变 Thread 状态。普通 wake 会取消
deadline，到期路径会从原 WaitQueue 摘除 Thread，两者通过同一个 Blocked
状态提交点决定唯一 WakeReason。

ProcessRuntime 的 timed futex 在锁内完成用户字二次读取、当前时间读取、
deadline 检查、futex entry 取得和 block。IRQ0 到期时把保存用户 frame 的
返回寄存器设置为 `TIMED_OUT`，再释放空 entry。Thread terminate、unmap、
exec 和 ProcessExit 沿既有取消路径同时解析 deadline，不保留指向旧
AddressSpace 的等待关系。

完整不变量与统计见 [Time 模块](time.md) 和
[v1.13 学习章](../learning/21-v1.13-monotonic-clock-deadline-timed-wait.md)。

## v1.14 信号状态与阻塞重启事务

`SignalManager` 与 ThreadScheduler 使用相同 Process/Thread 槽索引，但公开
PID/TID 仍由查找解析，不把可复用槽暴露为身份。Process 状态拥有 group、
pending 和 63 项 disposition；Thread 状态拥有 mask、pending 与活动 frame
五元组。普通信号在所有 pending 集合中最多存在一次，无法选择未屏蔽 Thread
时继续留在 Process。

Blocked Thread 被选中后必须通过现有 WaitQueue：

```text
Send signal
  → select exactly one Thread
  → WakeThread(reason=Signal)
  → cancel deadline + remove WaitQueue
  → save Interrupted or restart context
  → user-return boundary builds SignalFrame
```

可重启调用在阻塞前保存系统调用号与 restartable 属性。Signal 获胜时默认把
保存 RAX 写为 `INTERRUPTED`；action 含 restart flag 且策略允许时，信号帧中
恢复原 RAX 并把 RIP 回退到 `SYSCALL`。descriptor/pipe wait、wait process
与 join 可重启，sleep/futex 不可重启。已有部分 I/O 始终直接返回部分结果。

fork 复制 Process action/group 和调用 Thread mask，不复制 pending 或活动帧。
exec 保留 Ignore、重置 Handler、清空 pending 并删除兄弟信号状态。ThreadExit
先把未处理 pending 归还 Process，再重新选择；ProcessExit 删除全部 Thread
状态后才能删除 Process 状态。`ExecuteProcesses` 最终验证两类 active count
为零并调用 `SignalManager::Validate()`。

模块测试新增：

- `os_kernel_signal_manager_unit_tests`；
- `os_kernel_signal_wait_integration_tests`；
- `os_kernel_signal_manager_randomized_tests`；
- `/bin/signal_probe` 三档 QEMU 整机路径。

完整状态机见
[v1.14 学习章](../learning/22-v1.14-process-signals-sigreturn.md) 与
[ADR 0041](../adr/0041-process-signals-user-frame-and-sigreturn.md)。

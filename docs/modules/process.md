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
`ThreadScheduler::CommitProcessImage` 只允许：

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

参数计划当前为全局工作区，依赖系统调用内核路径不并发重入。引入 SMP、
Kernel preemption 或同一 Process 多 Thread exec 前必须改变其所有权。

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

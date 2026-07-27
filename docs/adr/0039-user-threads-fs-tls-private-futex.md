# ADR 0039：以 Thread、FS-base TLS 与 private futex 建立用户并发边界

状态：已接受并在 v1.12 实现。

## 背景

v1.2 已把 Process 与 Thread 从数据结构上分开，但用户态仍只能使用每个
Process 的初始 Thread。这个状态能支撑多进程调度，却不能回答同一地址空间内
并发执行时最关键的问题：

- 哪些资源属于 Process，哪些资源属于 Thread；
- 用户栈、KernelStack、寄存器和 TLS 何时发布、退出和回收；
- 用户态原子快路径怎样在竞争时进入内核睡眠；
- 地址被 `munmap`、映像被 `exec` 或 Process 退出时，等待者如何脱离旧地址；
- `ThreadExit` 与 `ProcessExit` 是否会错误地拥有相同可见性。

本阶段仍坚持 QEMU 只模拟硬件。线程库、TLS、futex、同步原语、调度状态与
回收策略全部由项目实现，不引入 pthread、libc、宿主线程或半主机服务。

## 决策

### 1. 冻结七个用户线程系统调用

v1.12 使用系统调用 47--53：

| 编号 | 名称 | 主要语义 |
| ---: | --- | --- |
| 47 | `CreateThread` | 在当前 Process 中建立 Ready Thread |
| 48 | `ExitThread` | 只结束当前 Thread；最后一个 Thread 同时结束 Process |
| 49 | `JoinThread` | 等待指定同 Process Thread 并取得 64 位退出值 |
| 50 | `SetThreadLocalStorage` | 更新当前 Thread 的 IA32_FS_BASE |
| 51 | `GetThreadId` | 返回稳定 TID，不暴露槽位 |
| 52 | `WaitPrivateFutex` | 比较 32 位 word 并原子阻塞 |
| 53 | `WakePrivateFutex` | 唤醒同 key 的有界数量 waiter |

`ThreadCreateRequest` 固定为 48 字节，包含入口、参数、栈区间、精确初始 RSP
和 TLS base；`ThreadJoinResult` 固定为 16 字节。所有 ABI 字段使用固定宽度
整数，并用 `static_assert` 冻结布局。

### 2. Thread 创建采用“准备后发布”

用户运行库为每个 Thread 建立：

```text
64 KiB anonymous stack
  + 低端一页永久 unmapped guard
  + 一页 ThreadRuntimeState/TLS
```

Kernel 验证入口属于可执行且不可写 VMA，栈属于可读写、不可执行匿名 VMA，
TLS 可读写且 16 字节对齐，栈和 TLS 不与同 Process 其他活动 Thread 重叠。

创建顺序为：

```text
用户映射 stack/TLS
  → KernelStack
  → scheduler Thread/TID
  → UserContext + FXSAVE
  → runtime metadata
  → Ready 可调度
```

调度器锁一直保持到上下文和运行时元数据完成，避免定时器在半初始化 Thread
上运行。子 Thread 进入后自行读取 TID 并写入自己的 TLS；父 Thread 不替子
Thread 发布该字段，从而消除“子先运行、父尚未从 Create 返回”的竞态。

### 3. TLS 使用 IA32_FS_BASE

每个 `ThreadEntry` 保存 `thread_local_storage_base`。`ActivateThread` 在切换
CR3、KernelStack 与 FXSAVE 现场时写入 `IA32_FS_BASE`，并回读确认。用户运行库
把 `ThreadRuntimeState::self` 放在 FS 偏移 0，因此：

```asm
mov rax, qword [fs:0]
```

即可取得当前 Thread 的运行时状态。

系统调用入口和返回路径不得重新装载 FS selector。长模式下写 FS selector
可能重置隐藏 base，从而破坏刚恢复的 TLS；Kernel 自身不使用 FS，因此用户
FS base 可以跨系统调用和中断保持，真正的 Thread 切换再显式更新 MSR。

GS 继续属于 `SWAPGS`/CpuLocal 可信入口协议，FS 和 GS 的职责不混用。

### 4. private futex key 不使用物理地址

futex key 固定为：

```text
(AddressSpaceId, aligned user virtual address)
```

每次成功创建或 fork 地址空间都会取得单调、非零的 64 位
`AddressSpaceId`。相同 VA 在两个 Process 中不能合并等待队列；同一地址空间
内的 Thread 则共享 key。不能用物理地址作 key，因为 COW、按需分页和重新
映射会改变物理页，而且尚未驻留的合法 VMA 甚至没有物理页。

Kernel 提供 512 个 `PrivateFutexEntry`。entry 仅在第一个 waiter 到达时申请，
最后一个 waiter 被唤醒或取消后立即 Reset 并归还。容量耗尽返回明确错误，
不会退化为忙等。

### 5. compare-and-block 与 wake 共用调度器临界区

`WaitPrivateFutex` 先做一次用户访问探针，再在 irq-save 调度器锁内：

1. 重新读取 32 位 futex word；
2. 若值已经变化，返回 `FUTEX_VALUE_CHANGED`；
3. Acquire 对应 entry；
4. 把当前 Thread 加入该 WaitQueue；
5. 发布 Blocked 状态并选择下一 Thread。

`WakePrivateFutex` 在同一锁内查找 entry、按 FIFO 唤醒至多指定数量并在队列
清空后释放 entry。因此 wake 要么发生在第二次比较之前，使 wait 看到新值；
要么发生在入队之后，能找到 waiter。两者之间不存在丢失唤醒窗口。

futex 不替用户态提供锁语义。mutex/condition/once 仍使用编译器原子
acquire/release 操作维护共享状态，只有竞争路径进入 futex。

### 6. 取消是地址生命周期的一部分

等待队列不能比 key 对应的用户地址活得更久：

- `munmap` 成功后取消被撤销范围内的 waiter；
- `exec` 提交前取消旧 AddressSpaceId 的全部 waiter；
- `ProcessExit` 和用户异常取消当前地址空间的全部 waiter；
- 被取消 Thread 的系统调用返回 `WAIT_CANCELLED`。

活动 Thread 的 stack/TLS 区间不能被任意 `munmap`。Join 完成并回收 Kernel
Thread 后，用户运行库才释放目标 Thread 的栈和 TLS 映射。

`exec` 先完整建立并探测候选映像，再取消旧映像等待、终止并回收 sibling，
最后提交调用 Thread 的新 CR3/RSP。候选失败时旧映像和所有 Thread 保持。

### 7. ThreadExit、Join 与 ProcessExit 分离

普通 `ThreadExit`：

- 保存 64 位 Thread exit value；
- 只把当前 Thread 变为 Exited；
- 不关闭 Process 的 FileTable/FsContext，不销毁地址空间；
- 唤醒 Join 等待者；
- KernelStack 和调度器槽由唯一成功 Join 回收。

Join 只允许同 Process、非自身 TID。第一个等待者取得 join ownership；其他
Thread 收到 `THREAD_ALREADY_JOINED`。目标尚未退出时调用者在统一
`ThreadJoin` WaitQueue 上阻塞，任意 ThreadExit 可唤醒后重新检查条件。

`ProcessExit`、用户异常或最后一个 Thread 的 `ThreadExit`：

- 取消全部 futex wait；
- 终止 Ready/Blocked sibling，并从其 WaitQueue 中移除；
- 关闭共享 I/O 与 FsContext；
- 发布 ProcessTree Zombie；
- 销毁地址空间；
- 由父 Process wait 路径回收全部 KernelStack/Thread 和 Process。

所以 Thread 退出值只对 Join 可见，Process 退出状态只对父进程 wait 可见。

### 8. 用户同步原语保持最小语义面

自研用户运行库提供：

- `Mutex`：0/1 原子状态，竞争时 futex wait，unlock wake one；
- `ConditionVariable`：32 位序列号，wait 先观察序列再解锁，notify 递增后
  wake one/all；
- `Once`：not-started/running/completed 三态，唯一执行者发布 completed，
  竞争者等待 running 改变。

本阶段不提供递归 mutex、读写锁、barrier、取消清理 handler 或超时参数。
超时和 signal wake reason 已在 WaitQueue 中预留，但由 v1.13/v1.14 分别实现。

## 失败语义

| 情况 | 结果 |
| --- | --- |
| 创建结构尺寸或入口/栈约束错误 | `INVALID_ARGUMENT`/`INVALID_USER_MEMORY` |
| 单 Process Thread 达到配置上限 | `THREAD_LIMIT_EXCEEDED` |
| Join 自身 | `DEADLOCK` |
| TID 不存在或不属于当前 Process | `THREAD_NOT_FOUND` |
| 已被其他 Thread Join | `THREAD_ALREADY_JOINED` |
| futex 地址未对齐或 wake count 为零 | `INVALID_ARGUMENT` |
| futex word 已变化 | `FUTEX_VALUE_CHANGED` |
| futex entry 容量耗尽 | `FUTEX_LIMIT_EXCEEDED` |
| 地址撤销、exec 或 ProcessExit | waiter 得到 `WAIT_CANCELLED` 或随 Process 终止 |
| Kernel 生命周期账本不一致 | 停机/整机验收失败，不伪装成用户错误 |

## 测试决策

v1.12 保留四层证据：

- 单元：AddressSpaceId/VA 隔离、entry 复用/容量、waiter 未空拒绝释放、
  Ready/Blocked/Running sibling 一次性退出、TLS 更新和 TID 查找；
- 集成：ABI 47--53 与结构大小、Thread/Process/WaitQueue 生命周期；
- 随机：100000 步 futex key acquire/release 参考模型，并复用 100000 步
  Scheduler/WaitQueue 随机状态机；
- QEMU：64 MiB 验证单线程降级；256 MiB 建立 32 Thread；64 GiB 建立
  64 Thread 并证明第 65 个被拒绝。

多线程探针让所有 worker 经过 barrier、condition variable、mutex、once、
futex sleep/wake、系统调用、定时器抢占与 Join。完成后必须出现：

```text
TLS_ISOLATED
FUTEX_SYNCHRONIZATION_VERIFIED
JOIN_RECLAIMED
PROCESS_RESOURCE_VALIDATION=1
```

日志只在 create/exit/join/futex 计数为二次幂时输出，避免串口 I/O 反过来
改变调度竞争。

## 被否决的方案

### 用宿主 pthread 或 QEMU 辅助线程

这绕过自研调度器、KernelStack、用户栈、系统调用和资源回收，不具备教学证据。

### 用忙等代替 futex

忙等在单 vCPU 上会浪费完整时间片，并不能证明 Blocked/Ready 与取消语义。

### 用 VA 单独作为全局 key

不同 Process 的同址映射会错误互相唤醒。

### 用物理页和 offset 作为 key

COW、unmap/remap 与尚未 fault 的 VMA 会让 key 不稳定或不存在。

### 在系统调用入口装载 FS selector

这会覆盖用户 FS base，导致 TLS 在第一次系统调用后静默指向错误地址。

### 让父 Thread 在 Create 返回后填写子 TLS 的 TID

子 Thread 可以在父 Thread 返回用户态前被调度，形成真实发布竞态。

## 后果

正面后果：

- 同一 Process 终于拥有真实并发执行流；
- Process 共享资源与 Thread 私有资源拥有明确回收点；
- TLS 在抢占、阻塞、系统调用和异常预留路径上成为架构现场的一部分；
- 用户同步快路径不进入 Kernel，竞争路径又不忙等；
- v1.13 可以直接在统一 WaitQueue 上加入 deadline。

代价与边界：

- 当前仍是单 BSP，不提供 SMP 原子与跨核唤醒；
- 不提供 detach；创建者必须 Join，ProcessExit 负责兜底清理；
- futex 只有 private、32 位、无 timeout 的 wait/wake；
- `fork` 只复制调用 Thread，`exec` 终止 sibling；
- signal 交付、取消点和 robust mutex 尚未实现。

## 关联

- [v1.12 发布记录](../releases/v1.12.md)
- [v1.12 学习章](../learning/20-v1.12-user-threads-tls-private-futex.md)
- [Thread/WaitQueue 基础 ADR](0029-process-thread-waitqueue-fxsave.md)
- [原生系统调用与 CpuLocal ADR](0030-cpu-local-native-system-call.md)
- [路线图](../roadmap.md)

# ADR 0065：V2.10 BlockIo 内核等待与安全迁移边界

状态：已接受（第三增量基础）

日期：2026-08-23

## 问题

`AsynchronousBlockDevice` 已统一 ATA/NVMe 的提交和完成，但设备完成仍不能直接等价为
“任意调用链可以睡眠”。IRQ handler 不能执行 NVMe Read 的 DMA 回拷，也不能进入调度器；
rootfs、journal、BlockCache 和 swap 的既有调用链又可能持有 irq-save spin lock。当前用户
Thread 的系统调用阻塞会保存用户现场并展开 C++ 调用栈，只有 Kernel Thread 的浅层入口
可以由 `BlockCurrentKernelThread` 保留独立 Ring 0 栈。若直接把同步 `ReadBlock` 改成睡眠，
会同时引入锁内睡眠、失效栈返回地址和仍被设备持有的失效 buffer。

## 决策

第三增量先建立设备无关的 BlockIo 协调层和真实 Kernel Thread 等待闭环，不越过上述安全
边界。

`BlockIoCoordinator` 使用调用方提供的 64 槽固定存储，以
`(slot, generation)` ticket 管理 `Registered -> Waiting -> Completed -> Free`；退出所有者可
进入 `Abandoned`，迟到 completion 只回收身份而不再唤醒。登记和完成同时匹配 owner Thread
与 64 位 request id。completion 若先于等待提交到达，`PrepareWait` 直接返回无需睡眠，消除
检查条件与登记 WaitQueue 之间的丢失唤醒。

IRQ14、IRQ15、MSI-X 或 PIT timeout 只解析设备状态并增加通知 generation。常驻
`RuntimeBlockIoCompletionWorker` 在非 IRQ Kernel Thread 上遍历已登记设备，调用
`TakeCompletion` 完成 NVMe Read 回拷，再向协调器交付结果并精确唤醒 owner。worker 在无
进展时睡在独立 `BlockIoCompletion` WaitQueue；睡前关闭中断并复核 generation，关闭
“扫描为空”和“进入阻塞”之间的通知窗口。

`AwaitRuntimeBlockIo` 只允许 Kernel Thread 调用。它在同一个关中断区提交请求、登记
ticket、通知 worker、提交等待并切换 Kernel 栈，醒来后取走唯一终态。请求一旦被设备接受，
协调器损坏或异常唤醒采用 fail-stop；在设备尚无“取消并等待硬件静止”协议时，返回并释放
caller buffer 会形成 use-after-free，不能伪装成普通 I/O 错误。

本增量用 secondary ATA 的 Flush Kernel Thread probe 走完 submit、IRQ15、非 IRQ completion、
WaitQueue wake 和结果回收。primary ATA root 与 secondary ATA swap 使用不同控制器状态机，
避免异步探针/旧 sync flush 与 rootfs 同步 I/O 交叉污染。

`BlockIoDevice` 暂时包住 rootfs/swap 的同步和异步设备，但生产实例明确关闭
`asynchronous_wait_enabled`。early boot、用户系统调用、rootfs/journal/cache 以及 swap
调用链继续同步；统计必须报告 root/swap async operation 为零，不能把 probe 当作生产迁移。

## 不变量

- IRQ 只解析和通知；不分配、不阻塞、不执行 VFS、DMA 大块回拷或 Kernel Thread 切换；
- ATA completion FIFO 的摘链与统计更新在短 irq-save 区内原子提交，避免 PIT/IRQ14/IRQ15
  在 Reap 中间状态执行 Validate；NVMe 仍只在关中断区摘链和释放槽，64 KiB 回拷在区外；
- 每个活动 owner 最多持有一个协调请求，ticket generation 阻止槽位复用后的旧票命中；
- completion-before-wait、wait-before-completion、abandon-before-completion 都恰有一个结果；
- worker 必须在非 IRQ 上消费完成，并只唤醒精确的 Kernel owner；
- 持有 spin lock、处于用户系统调用深层 C++ 栈或 buffer 生命周期不能延长时不得调用 Await；
- 停止后两个 BlockIo WaitQueue 无 waiter、协调器无活动请求，三个生产 Kernel Thread 的
  栈、KVA 和 frame 全部回收；
- 停止请求可能早于启动探针执行；completion Worker 必须同时观察 probe 已完成和活动请求
  为零才能退出，不能让随后提交的探针失去 completion 消费者；
- 当前 PIC mask 为 `0x3FF8`，同时开放 primary IRQ14 与 secondary IRQ15；虚假 IRQ15 仍只
  向 master 确认 cascade。

## 后果

系统已经具备可复用的 Kernel BlockIo 等待协议和 bottom-half worker，真实 IRQ15 probe
证明调度闭环可运行，单元、集成和十万步模型证明状态机边界。代价是生产 rootfs/swap
尚未异步化，`BlockIoDevice` 仍使用同步回退。

下一增量在实现 FilePageCache Loading waiter 前，必须先把深层设备访问改成“提交给浅层
I/O worker + 调用者等待稳定对象”的委托模型，并把现有 spin lock 临界区拆为只保护状态
提交的短区。完成这些前置条件后，才能逐条迁移 rootfs/swap read/write/flush；不得在当前
接口上简单打开异步开关。

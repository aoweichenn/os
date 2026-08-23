# ADR 0066：V2.10 栈式用户内核续体与生产异步 I/O

状态：已接受（第三增量 3b）

日期：2026-08-23

## 问题

V2.10.3a 已让浅层 Kernel Thread 经 `BlockIoCoordinator` 睡眠等待 ATA/NVMe completion，
但 rootfs、journal、VFS、打开文件描述和 swap 的生产调用来自用户系统调用的深层 C++
调用栈。旧阻塞模型只保存用户寄存器并展开内核调用栈，因此不能在任意文件系统层暂停后
继续返回；这些层又使用 irq-save spin lock，直接打开异步设备会形成锁内睡眠。

把所有文件系统操作改写成手工状态机或单一 I/O worker 会扩大本增量，并使 pathname、
journal 和 copy-to-user 的中间状态跨任务共享。需要一种不引入 hosted runtime、动态分配或
外部固件的项目自有机制，同时保持单 BSP、固定容量和现有 ABI/rootfs 格式。

## 决策

### 栈式用户内核续体

用户 Thread 在可睡眠的 Kernel 路径上阻塞时保留自己的 Kernel stack、FX state、系统调用
入口方法、双 GS 基址状态和 CR3 模式。dispatcher 只保存自己的栈顶；目标再次 Ready 后，
汇编入口切回目标 Kernel stack，从原 C++ 调用点继续。普通用户现场仍由既有
`ExceptionFrame` 返回，ABI 不增加续体句柄。

用户到用户的调度不再在前任 Thread 的 Kernel stack 上准备后任返回，而是一律退回
dispatcher，再从后任自有栈或用户现场进入。销毁地址空间、关闭描述符等退出路径若在 I/O
中阻塞，续体明确记录“使用 Kernel page table”，恢复时不得重新装载已销毁的用户 CR3。
原生 `SYSCALL` 阻塞同时挂起 `CpuLocal` 的入口深度并显式保存 IA32_GS_BASE/
IA32_KERNEL_GS_BASE，恢复后再继续，避免 `SWAPGS` 所有权漂移。

### 可睡眠运行时互斥

新增固定布局 `RuntimeMutex`。调度运行时未就绪、处于 IRQ 或持有 spin lock 时，它退化为
短时 `SpinLock`；合格 User/Kernel Thread 则使用项目自有 `Mutex + WaitQueue` 阻塞。
BlockCache、rootfs、VFS pathname resolution、文件后备管理、swap 和打开文件 payload
改用独立的运行时互斥身份。IRQ 路径仍只使用短 irq-save 临界区，不接触 RuntimeMutex。

`SpawnCurrentProcess` 与 `ExecCurrentProcess` 共享的参数规划和路径 scratch 由专用
RuntimeMutex 串行化。新 User Thread 先进入不可调度的 `Initializing` 状态；地址空间、
文件表、信号、作业控制、运行时元数据和 Kernel stack 全部提交后才原子发布为 `Ready`，
失败则在从未运行过的状态下逆序丢弃。

### 生产设备迁移

ATA/NVMe root 与 swap 的 `BlockIoDevice` 打开异步等待。early boot 和调度器不可用时仍走
同步轮询；深层后台 Kernel worker 暂时使用同步回退，用户 rootfs/swap 路径使用栈式续体
等待真实 completion。ATA PIO Write 在命令后必须先轮询 DRQ 并立即传输扇区，最终 IRQ
只解析完成；不能等待不存在的“首个写 IRQ”。

块统计不再固定为一次探针。整机验收要求 registration、wait、completion 三者非零且严格
相等，普通 primary 的 root async operation 非零；产生匿名换出的 reclaim profile 还要求
swap async operation 非零。

作为 root/swap 的 NVMe controller 是系统级持久资源，正常 `READY`/事件循环保持其活动，
不能为了测试资源回收而提前关闭根存储。独立 probe、EIO/timeout recovery 仍调用通用
shutdown，验证控制器、DMA、MMIO 与 PCI/MSI-X 资源可完整回收。

### 退出与并发扫描

进程退出先等待同进程其他 Kernel 续体排空，再终止 sibling，完成 writeback、描述符关闭、
VFS context 释放和地址空间销毁，最后使调度器 Process 进入 `Zombie`。只有此后才向
ProcessTree 发布退出事件并唤醒父进程，防止共享 ChildProcess WaitQueue 的无关唤醒让父进程
收集到“树已 Zombie、调度器仍 Alive”的半提交。

当前单 BSP 上，page aging、background reclaim 和 file writeback worker 在存在活动 User
Kernel 续体的 epoch 跳过扫描，避免它们在续体锁外窗口改动同一 VMA/PTE。Shell 在退出前
继续并终止仍活动的作业组，逐成员 wait/reap 后才退出，PID 1 不接收遗留交互作业。

## 不变量

- 每个活动 User Kernel 续体只属于一个 User Thread 和一段已分配 Kernel stack；
- 续体挂起/恢复必须成对保存 FX、系统调用入口、GS 和 CR3 模式；
- 持有 spin lock、IRQ nesting 非零或 buffer 生命周期不稳定时不得阻塞；
- `Initializing` Thread 不在 Ready queue，发布前不得被 timer、yield 或 wake 选中；
- 设备接受请求后，caller buffer 保留到唯一 completion；异常协议采用 fail-stop；
- 调度器 `Zombie` 必须先于 ProcessTree 退出事件可见，父进程回收不得半提交；
- 正常结束时续体、BlockIo ticket、两个 I/O WaitQueue、作业控制、Kernel stack 和用户资源
  全部归零；
- 生产 root/swap NVMe controller 必须保持到系统关机；独立 probe/error recovery 关闭时，
  DMA/MMIO/PCI/MSI-X 必须完整回收；
- IRQ 不分配、不阻塞、不进入 VFS，也不执行 NVMe 大块 DMA 回拷。

## 后果

生产用户 rootfs 和 swap 已能在真实 ATA/NVMe completion 上睡眠，不再把设备忙等待藏在
VFS 后面；既有同步接口仍为 early boot 和受限 Kernel worker 提供有界回退。代价是每个
可阻塞 User Thread 必须长期拥有独立 Kernel stack，且单 BSP 后台内存 worker 在续体活动
epoch 会让出一次扫描。

本决策不实现 FilePageCache 同页 `Loading` waiter、预读窗口或并发 writeback/reclaim
矩阵；它们仍按 V2.10 第四至第六增量推进。ABI 2.4.0、rootfs v4、4 GiB/128 GiB 参考规格
和项目自有启动链保持不变。

## 被拒绝的方案

- **在深层调用直接复用 Kernel Thread wait**：返回地址仍位于前任栈，用户切换会破坏栈
  所有权。
- **把所有 VFS 操作委托给单一 I/O worker**：中间状态和 copy-to-user 生命周期被迫跨任务
  保存，本增量改动面更大。
- **继续同步轮询**：无法完成 V2.10 的生产迁移目标，也掩盖真实 completion/owner 竞态。
- **在 IRQ 中完成 VFS 或 DMA 回拷**：违反中断边界并引入不可界定的栈和延迟。

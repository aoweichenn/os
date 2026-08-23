# Kernel 源码布局

Kernel 按功能所有权分为十三组。公开头文件与实现使用完全对称的相对路径：

```text
include/os/kernel/<module>/<name>.hpp
src/<module>/<name>.cpp
```

例如页表接口和实现固定为：

```text
include/os/kernel/memory/page_table.hpp
src/memory/page_table.cpp
```

## 模块职责

| 目录 | 职责 |
| --- | --- |
| `arch/` | x86-64 描述符表、异常/IRQ、CpuLocal、SYSCALL、处理器现场和 panic |
| `boot/` | BootInfo 校验与 C ABI 内核入口 |
| `core/` | Kernel 主流程和 freestanding 内存运行时 |
| `device/` | 端口 I/O、VGA 控制台、PIC、PIT、PS/2 与 ATA |
| `fs/` | 磁盘格式、块缓存和文件系统 |
| `io/` | 控制台输入、共享 FileDescription 与动态 FileTable |
| `ipc/` | 有界管道和端点生命周期 |
| `memory/` | 物理页、buddy、页表、heap、KVA、动态栈、VMA 与资源快照 |
| `object/` | 类型化 KernelObject、generation 与强引用生命周期 |
| `process/` | Process/Thread 状态机、run queue、WaitQueue 和目标机生命周期 |
| `sync/` | SpinLock、IrqSaveSpinLock 与可睡眠 Mutex |
| `time/` | 单调时钟、deadline queue 与溢出安全时间换算 |
| `user/` | 用户 ELF、用户内存、系统调用和内嵌程序镜像边界 |

目录表达“谁负责维护这个文件”，不额外制造冗长 C++ 命名空间。当前公开类型
仍位于简短的 `os::kernel`；将来只有在同名概念或独立子系统 API 确实需要时，
才引入 `os::kernel::<module>`。

## 结构约束

- Kernel 根目录不允许再堆放 `.hpp`、`.tpp`、`.cpp` 或 `.asm`。
- 每个公开 `.hpp` 必须在同名模块下具有对应 `.cpp`。
- 模板实现 `.tpp` 必须与同名 `.hpp` 同目录。
- 只有 `arch/architecture.asm`、`memory/page_table_layout.cpp` 和
  `user/user_images.asm.in` 是记录在案的非一一配对实现。
- `source/kernel/CMakeLists.txt` 按同一模块集合维护清单，再组合目标；新增文件
  不得直接插入无分组的总列表。
- `tests/tooling/test_kernel_layout.py` 自动验证当前树和上述配对关系。

模块之间可以通过公开头文件组合，但 `core/kernel_main.cpp` 只负责编排，不承载
可独立测试的算法；算法应下沉到所属模块，使 host 模型目标仍能独立构建。

v1.2 的执行模型文件固定归属如下：

```text
arch/extended_state.*          CPUID、CR0/CR4 与目标机 FXSAVE/FXRSTOR
arch/extended_state_layout.*   可在宿主测试的 512/16 布局和能力解码
process/thread_scheduler.*     Process/Thread 状态与侵入式队列
process/wait_queue.*           等待对象的身份、计数与关闭状态
process/process_tree.*         PID1、父子身份、Zombie、wait 与孤儿收养
process/program_arguments.*    argc/argv/envp 的纯 64 位用户栈布局
process/process_runtime.*      CR3、TSS.RSP0、动态栈、用户帧和现场组合
sync/spin_lock.*               acquire/release 与 irq-save 短临界区
sync/mutex.*                   基于 WaitQueue 的可睡眠直接交接互斥
```

`ThreadScheduler` 不允许依赖 `memory/`、`device/` 或 VGA 控制台；硬件切换只存在于
`process_runtime.cpp`。`extended_state_layout.cpp` 不执行特权指令，因此
宿主单元测试可以验证 CPUID 位和结构布局；`extended_state.cpp` 只进入
freestanding Kernel 目标。新增执行机制时应保持这条“纯策略—目标机落实”
边界。

v1.3 的架构入口文件固定归属如下：

```text
arch/processor_features.*          CPUID 能力 profile 的纯解码与验证
arch/cpu_local.*                   每 CPU 当前 Thread、可信入口栈与深度/统计
arch/user_context.*                统一用户现场、返回校验和 SYSRET/IRET 选择
arch/native_system_call_layout.*   六个 MSR 的纯布局与回读比较
arch/native_system_call.*          目标机 MSR 初始化
arch/architecture.asm              SWAPGS、两类入口、统一返回和 NMI 最小桩
user/system_calls.*                共同 C++ dispatcher 与用户返回准备
```

`processor_features`、`user_context` 与 `native_system_call_layout` 必须保持
可由宿主测试直接链接；RDMSR/WRMSR 和汇编入口只能出现在目标机层。CpuLocal
虽然当前是单元素，也必须继续位于 `arch/`，不能塞入 ProcessRuntime 的私有
全局状态。

v1.4 的对象与描述符文件固定归属如下：

```text
object/kernel_object.*         类型、代次、强引用、活动链与最后引用 finalizer
io/file_description.*         共享偏移、file status flags 和统一资源操作
io/file_table.*               分块 fd 表、fd flags、limit 与两阶段安装
process/process_runtime.*      每 Process FileTable 和目标机等待/唤醒组合
```

`object/` 不依赖某一种文件系统、管道或设备；具体 payload 和 finalizer 由
`io/` 提供。FileTable 只持有私有 handle，不能包含 FileDescription 裸指针。

v1.5/v1.6 的文件系统文件固定归属如下：

```text
fs/vfs.*                       路径、Mount、FsContext 与 namespace mutation
fs/memfs.*                     /tmp 内存后端和差分模型
fs/legacy_file_system.*        旧盘面 VFS 适配，仅保留兼容回归
fs/file_system.*               v0.11 旧格式实现
fs/root_file_system_format.*   rootfs v4、64 位几何与五级块树盘面编码
fs/root_journal.*              格式 3 ordered metadata journal 与 mount replay
fs/root_file_system.*          生产根、链接/orphan、时间戳、journal 与流式校验
fs/block_cache.*               固定容量写回缓存
```

`root_file_system_format` 只负责字节布局，不访问设备、VFS 或全局 Kernel
状态，因此宿主单元测试可直接链接；`root_journal` 只依赖固定块设备接口，
负责 256 块日志区、124-credit overlay、CRC32、FLUSH、checkpoint 与 replay；
`root_file_system` 组合块设备、缓存、journal 和 VFS 操作表。Python
mkfs/fsck 是独立工具实现，不进入 Kernel 目标。模块名
`rootfs_v2.py` 保留历史入口兼容，实际创建与校验的是格式 3。

v1.7 的磁盘程序与进程树继续归属已有 `process/` 和 `user/` 边界：

```text
process/process_tree.*         与调度槽分离的父子关系、退出状态与 wait
process/program_arguments.*    不访问页表的参数/环境布局规划
process/process_runtime.*      VFS ELF reader、spawn/exec/wait 与失败展开
user/user_elf.*                内存/文件共用的 reader 两遍 ELF 验证
user/user_memory.*             候选 AddressSpace、8 MiB 用户栈预留与段复制
user/system_calls.*            36/37/38 号 ABI 的用户复制和结果映射
```

`process_tree` 与 `program_arguments` 必须保持宿主可链接，不得反向依赖
VFS、页表或设备。磁盘读取和真实资源提交只进入 `process_runtime`；
ELF parser 只通过 `UserElfReader` 观察 `(offset, length)`，不认识 inode。
普通用户程序属于 rootfs，不得重新加入 `user_program_images`；后者只保留
启动模式必须直接选择的最小 smoke/异常夹具。

v1.8 在相同目录边界内增加用户虚拟内存策略：

```text
memory/virtual_memory_area.*   有序 VMA、描述符池、split/merge/gap 与守恒
user/user_memory.*             匿名 fault、brk、unmap、栈增长与统计
user/system_calls.*            39..42 号 VM ABI 校验和当前 Process 分发
```

VMA 容器不访问 CR2、页表或 frame allocator；它保持宿主可链接。实际缺页
解析只进入 `user_memory`，异常入口仍归属 `arch/`，当前 Process 与退出/
exec 生命周期仍归属 `process/`。这种依赖方向防止区间算法、x86 fault 机制
和 Process 调度重新堆进一个不可测试文件。

v1.9 继续复用同一模块边界：

- `memory/file_page_cache.*` 管理有界 clean 页、引用与 LRU；
- `user/file_backing.*` 管理 VFS/内存来源和稳定 generation；
- `memory/virtual_memory_area.*` 保存文件区间意图；
- `user/user_memory.*` 组合 fault、PTE、cache、失效与回收；
- `process/process_runtime.*` 只负责 fd/进程生命周期与系统调用编排。

文件系统代码不直接建立 PTE，页缓存也不解析 fd；跨模块依赖保持单向。

v1.10 的 fork/COW 继续保持相同所有权边界：

```text
memory/user_page_reference.*  共享 private frame 的稀疏引用状态机
memory/page_table.*           COW 软件位编码、leaf 查询与 ReplacePage
user/user_memory.*            AddressSpace clone、COW break、失败恢复
io/file_table.*               精确 fd/flags clone，共享 FileDescription 引用
fs/vfs.*                      FsContext clone
user/file_backing.*           文件后备 clone
process/process_runtime.*      Process/Thread 候选事务、发布与回收
```

引用表不认识 VMA、Process 或文件；页表不决定某页是否允许 COW；只有
`user_memory` 联合 VMA、PTE 与引用状态。`process_runtime` 负责跨资源事务，
不重新实现页复制。设计理由见
[ADR 0037](../../docs/adr/0037-fork-copy-on-write.md)。

v1.11 在这些所有权边界上补齐 Unix I/O 组合：

```text
ipc/pipe.*                   64 KiB 环形流、4 KiB 按需页与端点状态
ipc/pipe_manager.*           8/128/1024 分档 slot、创建和最终回收
io/file_description.*        把 Pipe 端点纳入共享打开文件语义
io/file_table.*              512 functional hard limit 与定点 DuplicateTo
user/system_calls.*          CreatePipe/DuplicateDescriptorTo ABI 分发
process/process_runtime.*    当前进程 pipe/dup2 编排和页分配回调
```

Pipe 不认识 fd，FileTable 不解析 Shell，ProcessRuntime 只组合对象和当前
Process。`DuplicateTo` 在表锁内提交新强引用，在锁外释放旧对象；提交前失败
保持目标不变，提交后的 finalizer 失败显式升级为资源账本错误。外部 Shell 和
十九个 `/bin` 路径属于 `source/user/`，Kernel 不内嵌命令实现。设计理由见
[ADR 0038](../../docs/adr/0038-dynamic-pipe-dup2-external-shell.md)。

v1.12/v1.13 在既有执行边界上补齐用户同步与统一时间：

```text
sync/private_futex.*          AddressSpaceId + 用户 VA 的等待 key 与容量
process/thread_scheduler.*    WaitQueue/deadline 单赢家和 Thread 状态提交
time/monotonic_clock.*        PIT 实际除数的整纳秒、余数与饱和累计
time/deadline_queue.*         512 槽稳定绝对 deadline 顺序与统计
arch/interrupt_runtime.*      IRQ0 推进时钟并触发到期解析
process/process_runtime.*     sleep/timed futex 与保存用户 frame 的结果
user/system_calls.*           47..56 Thread/futex/time ABI 分发
```

`time/` 纯模型不访问端口、IRQ、Process、用户地址或 VGA 控制台，可直接进入宿主测试。
硬件周期只由 `arch/interrupt_runtime` 推进；等待对象和唯一 WakeReason 仍由
ThreadScheduler 拥有。设计理由见
[ADR 0040](../../docs/adr/0040-monotonic-clock-deadline-timed-wait.md)。

v1.14 继续在同一执行边界上加入异步用户通知：

```text
process/signal_manager.*     Process action/group/pending 与 Thread mask/frame
process/thread_scheduler.*   Signal/condition/timeout 的唯一 WakeReason
process/process_runtime.*    用户栈帧、阻塞重启、生命周期与 sigreturn
user/user_memory.*           主栈信号帧的受控相邻页扩展
user/system_calls.*          57..63 信号与进程组 ABI 分发
```

SignalManager 不直接接入 Ready 队列；ProcessRuntime 只通过 ThreadScheduler
唤醒。用户 frame 的 cookie、现场和页权限也只在当前 Thread 返回边界验证。
设计理由见
[ADR 0041](../../docs/adr/0041-process-signals-user-frame-and-sigreturn.md)。

v2.8 的文件同步继续沿用同一对象边界：

```text
memory/file_page_cache.*                  文件/页范围 Dirty/Error 选择
memory/file_writeback_error_tracker.*     文件错误序列与独立打开实例引用
io/file_description.*                    duplicate/fork 共享的错误游标
user/user_memory.*                        msync VMA 到文件页范围换算
process/process_runtime.*                 fsync/fdatasync/msync 顺序编排
user/system_calls.*                       ABI 2.4.0 的 85..87 分发
memory/memory_pressure.*                  clean/writeback/swap 纯逻辑执行顺序
process/process_runtime.*                 跨进程轮转、活动栈保护与 OOM 回调
process/file_page_writeback.*             同页写回 generation、waiter 与结果广播
```

v2.9 第一增量在同一个调度器中增加不属于 Process 的 Kernel Thread：

```text
process/thread_scheduler.*   ThreadKind、独立高位 TID、run/wait/exited 状态
memory/kernel_stack_manager.* 16 KiB 动态栈与双 guard
arch/architecture.asm        首次进入、协作切换、挂起与返回 dispatcher
process/process_runtime.*    entry/context、FXSAVE、CpuLocal/TSS、退出后 reap
```

独立生命周期 API 仍只执行没有 User Thread 的批次；生产 `ExecuteProcesses` 已支持
User/Kernel 混合 dispatcher。设计理由见
[ADR 0057](../../docs/adr/0057-v2-9-kernel-thread-lifecycle.md)。

第二增量增加 `process/work_queue.*`：调用方提供 entry 与 delayed heap 存储，队列维护
generation handle、即时 FIFO、延迟最小堆、合并、取消、完成和 drain。任务回调只由
ProcessRuntime 的 Kernel Thread worker 在锁外执行。第三增量增加最早 deadline 查询和
Delayed→Queued 即时提升，并让常驻 Worker 执行常规 writeback；user-return 只提交工作，
timer IRQ 只唤醒。设计理由见
[ADR 0058](../../docs/adr/0058-v2-9-work-queue-state-and-drain.md)。

混合切换与 Worker 停止/回收边界见
[ADR 0059](../../docs/adr/0059-v2-9-mixed-worker-writeback.md)。

第四增量增加 `memory/page_aging.*` 与页表 A 位采样。PageAgingManager 以物理帧身份聚合
alias，维护 file/anonymous 的 active/inactive 四队列；ProcessRuntime 用第二个周期
WorkItem 填充观察，结束时只保留累计统计，不实际回收。元数据使用 frame+KVA 动态常驻
分配，避免把大数组塞入 Kernel BSS。设计见
[ADR 0060](../../docs/adr/0060-v2-9-pte-accessed-page-aging.md)。

第五增量增加 `memory/background_reclaim.*` 和第三个生产 WorkHandle。low watermark
唤醒、high watermark 停止，low 到 min 之间只排队，min 以下保留 direct fallback；
Worker 按 clean/writeback/anonymous 执行最多 64 页并在无即时进展时退避。PageAging
candidate 改为显式状态，file access generation 和 completion 防止复用 frame 误回收。
设计见 [ADR 0061](../../docs/adr/0061-v2-9-background-watermark-reclaim.md)。

第六增量让 direct/background 共用 `PlanMemoryReclaim` 的 0..200 swappiness 权重和
file/anonymous 配额转赠。UserMemory 在私有匿名 frame 最后释放前通知 ProcessRuntime
执行 aging forget；专用 OOM profile 用 swappiness 0 和 `/proc/meminfo` 动态工作集验证
非当前 SIGKILL victim。设计见
[ADR 0062](../../docs/adr/0062-v2-9-unified-reclaim-fairness-and-oom-matrix.md)。

v2.10 第一增量扩展 `device/block_request.*`：Queued FIFO 只负责签发，新的 completion
FIFO 按 IRQ/timeout/cancel 首次解析顺序保存终态。`TakeCompletion` 交付 owner/result
快照并回收槽，按 id 的恢复 Reap 可安全摘除中间项；ATA IRQ 与 timeout 已迁移到公共
出口。设计见
[ADR 0063](../../docs/adr/0063-v2-10-ordered-block-completion-channel.md)。

第二增量增加 `device/asynchronous_block_device.*`，用静态函数表把 ATA/NVMe namespace
统一为 geometry、submit、cancel、timeout 和 completion 接口。ATA 的系统调用提交与
IRQ14/timer 服务已走该接口；NVMe probe 的四路 Read/Write/Flush 也走公共 adapter，
公共 64 位 request id 不再等同于 16 位 command id。NVMe Read 的 DMA 回拷只在非 IRQ
TakeCompletion 执行。设计见
[ADR 0064](../../docs/adr/0064-v2-10-asynchronous-block-device-adapter.md)。

第三增量 3a 增加 `process/block_io.*` 与 `process/block_io_device.*`。64 槽协调器用
owner/request id/generation ticket 关闭 completion-before-wait 丢唤醒；常驻 completion
Kernel Thread 在非 IRQ 上消费设备完成并精确唤醒等待者。secondary ATA Flush probe 真实
经过 IRQ15、Worker、BlockIo WaitQueue 和结果回收。设计见
[ADR 0065](../../docs/adr/0065-v2-10-block-io-kernel-wait-and-migration-boundary.md)。

第三增量 3b 增加 User Kernel stack 续体和 `sync/runtime_mutex.*`。续体跨阻塞保存 FX、
系统调用、GS 与 CR3 模式；RuntimeMutex 让 rootfs/VFS/cache/swap 的竞争者经 WaitQueue
睡眠。root/swap 包装已打开异步等待，early boot 和受限 Kernel worker 保留同步回退；
Thread 以 `Initializing -> Ready` 发布，退出以 Scheduler Zombie 先于 ProcessTree event
收束。设计见
[ADR 0066](../../docs/adr/0066-v2-10-stackful-user-kernel-continuation-and-runtime-mutex.md)。

第四增量增加 `process/file_page_load.*` 与 `WaitCondition::FilePageLoading`。同页 cache miss
冲突在唯一 Loading entry 上登记，等待先于完成时睡在 per-slot WaitQueue，完成先于等待时
直接领取同一结果。成功 owner 在广播前为全部 waiter 预留真实 page reference，避免 owner
先释放后被 reclaim 抢先淘汰；失败在缓存资源撤销后广播同一终态。设计见
[ADR 0067](../../docs/adr/0067-v2-10-file-page-loading-waiter-and-reference-handoff.md)。

第五增量 5a 增加 `memory/file_readahead.*`。策略实例对应未来共享 FileDescription，按
DemandHit/DemandMiss/PrefetchedHit 推进 start/size/async-tail 窗口和 generation；默认
32 页上限，顺序窗口按 4/2 倍增长，随机访问重置。四级内存压力和 useful/wasted 反馈只
调整下一窗口。5b 已将其接入 FileDescription、VFS 缓存观测、64 槽 retained-OpenFile 请求
FIFO 和常驻 Kernel worker；FilePageCache 用 Demand/Prefetch intent 与 one-shot 标记归因
实际 useful/waste。5c 再用固定 FeedbackLedger、stream token 和 policy generation 把反馈
交回 producer，并对 close/reset/BelowMinimum/truncate 执行有界取消。预读 worker 可拥有新
Loading，但不等待同页 owner。设计见
[ADR 0068](../../docs/adr/0068-v2-10-per-open-file-readahead-policy.md) 与
[ADR 0069](../../docs/adr/0069-v2-10-production-readahead-execution.md)、
[ADR 0070](../../docs/adr/0070-v2-10-readahead-cancellation-and-feedback-ledger.md)。

第六增量增加 `process/file_page_writeback.*` 与 `WaitCondition::FilePageWriteback`。页缓存
在 Dirty/Error→Writeback 前登记文件页、物理地址和新 access generation；同页 writer 与
同步 writeback 在 cache lock 内登记，再锁外等待唯一成功或失败。writer 成功后重新脏化，
同步者继续范围扫描；失败保留 Error/paused。Clean reclaim 仍可越过 Writeback 回收其他
候选。设计见
[ADR 0071](../../docs/adr/0071-v2-10-file-page-writeback-wait-and-failure-matrix.md)。

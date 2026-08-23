# ADR 0071：V2.10 文件页写回等待与故障矩阵

状态：已接受（第六增量）

日期：2026-08-23

## 问题

`FilePageCache` 已在写设备前把 Dirty/Error 页改成 Writeback，并在锁外执行 I/O。异步
BlockIo 等待会让出 CPU，因此其他 User Thread 可以在同一写回尚未结束时进入 `fsync`、
`fdatasync`、同步 `msync` 或重新写该页。旧实现只从 Dirty/Error 集合选择页面：范围内只剩
Writeback 时会错误返回成功；`MarkDirty` 则返回 Busy。前者破坏同步完成语义，后者会把合法
的共享映射写故障或 buffered write 变成进程级失败。

回收必须同时保持前进：一个页等待设备时，其他 Clean 页仍应允许被 active/inactive 回收。
设备错误、超时和 best-effort cancel 最终都会从公共异步块层变成一次终态失败，页缓存不能
丢失唤醒、重复重试或把失败伪装成成功。

## 决策

新增固定容量 `FilePageWritebackCoordinator`。每次 Dirty/Error→Writeback 前，页缓存分配新的
64 位代次并写入独立 `writeback_generation` 字段，再以“文件页身份、物理地址、writeback
generation、owner Thread”登记一次 Writing。普通只读 Retain 可以继续刷新 LRU
`access_generation`，但不能改变进行中的 I/O 身份。每个运行时 Thread 有一个 waiter 身份，
每个活动 writeback 槽有独立 WaitQueue；token 使用 slot+generation，拒绝槽复用后的旧 token。

观察同页 Writeback 的 `MarkDirty` 和同步 writeback 在仍持有 cache lock 时登记 waiter，随后
解锁并在 scheduler lock 内一次提交 PrepareWait 与 WaitQueue 入队。completion-before-wait
直接交付结果，wait-before-completion 广播唤醒。成功 waiter 重新检查页状态：写者再把 Clean
页脏化，同步者继续扫描范围；失败 waiter 取得 owner 的同一个 `SourceWriteFailed` 或
`FrameAccessFailed`，不自行启动第二次 I/O。

owner 在锁外写设备，完成后先验证文件页身份、物理地址、generation 和 Writeback 状态，再
原子转换为 Clean 或 Error，最后发布协调器结果。协调器协议一旦在登记后损坏便 fail-stop，
因为返回会遗留无法安全回收的 waiter。一个 owner 同时只能有一个 Writing，但可以在旧
Completed 结果尚未全部领取时开始下一页；这样 64 页批次不会被已经唤醒但尚未调度的 waiter
阻塞。

Clean reclaim 不等待 Writeback，也不持有协调器 token。它继续选择其他 Clean 候选；direct
或 background reclaim 需要脏页时，通过公共 writeback 路径等待当前页，完成后再回收 Clean。
Loading 页仍不可写回或回收；已取消的预读若已经提交 BlockIo，等待唯一设备终态后转成 Clean
并立即按 generation 丢弃，不伪造硬件撤销。

故障矩阵采用分层组合而不是复制四套页缓存状态机：

- coordinator unit、WaitQueue integration 和固定种子十万步模型验证成功/失败、完成先行、
  广播、旧 token 与多页 owner 前进；
- `std::thread` cache integration 强制重叠 Writeback、同页 MarkDirty、范围同步与另一 Clean
  页 reclaim，分别执行成功和来源写失败，验证 Error→Dirty 显式重试；
- 公共异步块设备与 BlockIo 既有模型继续覆盖 Succeeded、DeviceError、TimedOut、Cancelled，
  ATA/NVMe 驱动各自把硬件终态映射到该公共结果；
- 4 GiB ATA/NVMe primary、reclaim、OOM 和三启动 persistence 验证生产 writeback 与资源收束，
  NVMe EIO/timeout 及 ATA timeout/error 故障门禁验证设备恢复边界。

## 不变量

- `begin = completion + writing`；静止时 begin 必须等于 completion；
- `waiter registration = result take + active waiter`；静止时 registration 必须等于 take；
- `wait commit = broadcast wake + currently waiting`；静止时 commit 必须等于 wake；
- 同一文件页和 writeback generation 只能有一个 owner 结果，所有同期 waiter 观察同一终态；
- 页数据在 Writeback 时不能重新变为 writable；MarkDirty 必须等到旧 I/O 完成后才能修改；
- 写回失败保留 Error 并暂停自动重试，只有显式同步或重新脏化才能推进；
- reclaim 可以越过 Writeback 回收其他 Clean 页，不能释放设备仍在读取的 frame；
- cache lock 内只登记或发布协调器状态，不阻塞；scheduler lock 不进入 VFS/cache/device；
- IRQ 只解析设备完成并通知 BlockIo，不能直接唤醒页级 waiter 或调用 VFS；
- 已签发 ATA/NVMe 请求保持 best-effort cancel；拒绝硬取消不得改变 buffer 所有权。

## 后果

`fsync`、`fdatasync`、同步 `msync` 和 direct/background writeback 不再把并发 Writeback 当作
完成，buffered write 与共享映射写故障也不会因正常竞争得到 Busy。代价是按最大 Thread
容量增加 writeback 槽、waiter 和 WaitQueue 静态存储；4 GiB/32 GiB 规格沿用已有 Thread
容量分档，不分配热路径内存。

V2.10 不承诺对已经签发到设备的命令执行硬件级 abort，也不把单 BSP 描述为并行 CPU。
本决策完成的是协作调度下多 Thread 的可观察并发、唯一终态和资源所有权；SMP、NVMe Abort
命令、ATA 无损硬中止、动态 writeback hash、memcg 与 NUMA 仍属于后续阶段。

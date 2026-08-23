# ADR 0067：V2.10 文件页 Loading waiter 与引用交接

状态：已接受（第四增量）

日期：2026-08-23

## 问题

V2.8 已让 `FilePageCache` 在锁内发布唯一 `Loading` 页，再释放 cache spinlock 执行来源
读取。这个边界阻止重复插入，但同页冲突只能返回 `EntryBusy`。V2.10.3b 把生产 rootfs
迁移到可睡眠异步 I/O 后，owner 在设备完成前会离开 CPU；其他 User Thread 此时必须等待
同一次读取，不能忙重试或启动第二个请求。

仅在 `Loading` 上增加一个普通 WaitQueue 仍不完整：完成可能先于 waiter 真正阻塞，失败
结果可能在 entry 被移除后丢失，成功 owner 还可能先释放最后一个页面引用，使 reclaim 在
waiter 醒来前淘汰页面。需要同时冻结通知、结果和物理页所有权。

## 决策

### 固定容量协调器

新增 `process/file_page_load.*`。`FilePageLoadCoordinator` 使用调用方提供的固定槽位和
per-thread waiter 存储，不分配内存；每个活动 owner 同时最多持有一个 load。load 以
`FilePageIdentity + physical address + load generation` 定位，以 `slot + generation` token
拒绝槽位复用后的旧等待。

状态分为：

```text
load:   Free -> Loading -> Completed -> Free
waiter: Free -> Registered -> Waiting -> Ready -> Free
                           `-> Ready
```

第二条 waiter 路径表示 completion-before-wait。协调器为每个 load 保存唯一终态；同期
waiter 无论何时提交等待，都只能领取 owner 发布的同一成功或失败结果。

### 关闭 lost wakeup

owner 在 cache lock 内插入 `Loading` 后立即登记 load。冲突线程仍持有同一 cache lock 时
登记 waiter，然后才释放 cache lock。它在 scheduler lock 内把 `Registered` 转成
`Waiting` 并提交到该 load 槽专属的 WaitQueue；owner 也必须在 cache lock 内完成协调器
并广播。因而 waiter 要么已在队列中被精确唤醒，要么观察 `Completed` 并直接取结果，不
存在“检查完成后、入队前”丢通知窗口。

每个 load 槽拥有独立 WaitQueue。完成只唤醒该槽中已经提交阻塞的线程；尚处于
`Registered` 的线程随后走直接完成路径。正常结束要求协调器 active、所有 per-slot
WaitQueue 和 waiter 全部归零。

### 成功结果的物理页引用交接

owner 把页面从 `Loading` 转为稳定状态并保留自己的引用后，仍在 cache lock 内读取精确
waiter 数。cache 为每个 waiter 预先执行一次真实 `Retain`，随后才发布成功并广播。waiter
醒来后验证身份、物理地址和非 Loading 状态，直接接管自己的预留引用，不再次 Retain，也
不递归发起 miss。

这条顺序保证即使 owner 先复制完数据并释放引用，truncate、invalidate、reclaim 和
eviction 仍会看到 waiter 的真实 mapping reference，不能抢先移除该页。waiter 返回给
调用者后由既有 `Release` 路径释放该引用；全局 cache 统计和地址空间引用统计保持同步。

失败时不建立页引用。owner 先撤销 entry、frame 和空地址空间，再向所有同期 waiter 发布
同一个 `FrameAccessFailed`、`SourceReadFailed` 或结构错误。失败广播不会留下可查询的
缓存页，但结果由协调器保存到最后一个 waiter 领取。

### 运行时边界与锁顺序

`FilePageCache` 不依赖调度器，而是接收静态回调表。生产回调只在具备可恢复 Kernel stack
的活动 User Thread 上可用；early boot、调度器停止期和受限 Kernel worker 保留原
`EntryBusy` 边界。

锁顺序固定为 cache lock 到 scheduler lock。该方向只执行固定状态提交和 WaitQueue
wake，不阻塞；scheduler-lock 路径不得反向进入 `FilePageCache`。来源读取继续完全位于
cache lock 外。waiter 登记后若等待提交、唤醒原因、结果领取或 owner 完成协议损坏，生产
运行时采用 fail-stop，不能返回并遗留无法回收的 token 或页面引用。

Loading 页本身仍不能映射、脏化、writeback、truncate、invalidate 或 reclaim；第四增量
只改变同页 `Acquire` 冲突，不放宽任何修改者边界。

## 不变量

- 同一 `FilePageIdentity` 同时最多存在一个权威 `Loading` entry 和一次来源读取；
- waiter 必须在观察到 Loading 的同一 cache 临界区登记，owner 必须在发布页面终态的同一
  临界区完成广播；
- `begin_count == completion_count`，`waiter_registration_count == result_take_count`，
  `wait_commit_count == broadcast_wake_count`；
- 成功广播前预留引用数必须等于登记 waiter 数；每个 waiter 只接管并最终释放一个引用；
- 失败广播的全部 waiter 必须观察同一终态，失败页不得残留 entry、frame 或地址空间；
- generation 不匹配的 owner/waiter token 永远不能读取新槽结果；
- IRQ 不登记、阻塞、完成或唤醒 FilePageLoad waiter。

## 结果

同页并发 miss 从瞬态 Busy 变成真正的睡眠合并，成功和失败都有一次来源读取、一次完成
发布和确定的引用所有权。代价是 ProcessRuntime 按 Thread 容量增加同规模 load 槽、waiter
和 WaitQueue；当前 4 GiB 主规格使用固定上限，不产生热路径分配。

单 BSP QEMU 的 completion Worker 可能总在下一个 User Thread 前完成，正常整机运行允许
waiter 计数为零；这不是伪造并发证据。强制重叠的 host integration 负责证明 cache、
coordinator 和引用交接，QEMU 负责证明 4 GiB 生产路径的 begin/completion 守恒、失败为零、
资源归零和可见屏幕输出。

本决策不实现顺序预读、取消中的 file-page load、并发 writeback/reclaim 矩阵、SMP 或
per-CPU page cache；它们仍按 V2.10 第五、六增量推进。ABI 2.4.0、rootfs v4 和磁盘格式
不变。

## 放弃的方案

- **继续返回 Busy 并由调用者重试**：会把异步 I/O 延迟变成用户可见瞬态失败或忙等。
- **waiter 醒来后递归 Acquire**：若 owner 已释放且 reclaim 抢先淘汰，会发生第二次来源
  读取，不能保证同期 miss 合并。
- **只保存成功/失败，不预留引用**：结果不丢，但成功物理页所有权仍有空窗。
- **把 ThreadScheduler 直接写进 FilePageCache**：破坏内存模块的 host 可测性和 early boot
  边界；静态回调足以表达所需提交点。
- **为每页动态分配 wait object**：给 miss 热路径增加递归内存压力和额外失败展开。

## 验证

- coordinator unit 覆盖等待先于完成、完成先于等待、失败广播、旧 token 和统计守恒；
- WaitQueue integration 覆盖两个 waiter 的精确阻塞、广播和队列归零；
- 固定种子十万轮 randomized model 覆盖 0..7 waiter、两种完成顺序、成功/失败和随机领取；
- `FilePageCache` 强制重叠 integration 暂停 owner source read，确认 waiter 已提交后才放行；
  成功路径验证 owner 先释放后 invalidate 仍被 waiter 引用拒绝，失败路径验证同错广播；
- 4 GiB `os_qemu_primary_smoke` 要求 active/failure 为零，begin/completion、waiter/result 和
  wait/wake 分别相等，并继续通过 VGA screendump 与 QMP 协议。

# ADR 0070：V2.10 预读取消与反馈账本

状态：已接受（第五增量 5c）

日期：2026-08-23

## 问题

V2.10.5b 能异步填充并统计 useful/waste，但缓存页只有布尔预读标记。系统知道“某页被预读”
却不知道由哪个打开流、哪一代策略产生，因此无法把 reclaim、truncate 或 close 丢弃的页
反馈给正确的 `FileReadaheadPolicy`。close 后若任务仍排队，retained OpenFile 还会让已经失去
消费者的预测 I/O 继续运行。

直接把 FileDescription 指针写进 page cache 会形成 memory→io 反向依赖，并在最后一个 fd
关闭后悬空。5c 需要一个不依赖对象地址、可拒绝槽位复用旧身份的归因和取消边界。

## 决策

### Stream token 与固定反馈账本

新增 `memory/file_readahead_feedback.*`。`FileReadaheadStreamToken` 由 slot index 和
generation 组成；FileDescription 创建 RegularFile 时注册一个 token，duplicate/fork 因共享
同一对象而共享 token，独立 open 获得不同 token。

反馈账本由 ProcessRuntime 提供 4096 个固定槽，不在热路径分配。槽保存文件身份、pending
useful/waste、活动任务数和 `Active/Retiring/Free` 状态。任务入队前 retain token，任何完成或
queued cancellation 都必须 release。最后一个描述引用关闭时先取消、领取反馈，再把 stream
置为 Retiring；只有活动任务归零后槽才释放。迟到页反馈只增加 stale 统计，不能命中复用槽。

### 页来源归因

FilePageCache 的预读页保存 `{stream token, policy generation}`，不保存 FileDescription 指针。
首次 Demand 消费标记时向账本记录 useful；定向取消、truncate、invalidate、reclaim 或 trim
删除未消费页时记录 waste。缓存继续验证：

```text
successful prefetch = resident tagged + useful + waste
feedback records = useful + waste
```

账本再验证 recorded = taken + stale + pending。FileDescription 在同一次 read 返回后领取自己
的 pending 反馈，再调用纯策略 `RecordFeedback`，所以 producer 即使由另一个 open 消费也能
得到正确归因。waste 大于 useful 时，下一个 decision 的 adaptive maximum 按 5a 规则减半。

### Generation 与生命周期取消

请求队列支持两类过滤：stream + 最大 policy generation，以及文件身份。取消 queued 请求
会从 FIFO 中间稳定摘除，由 Runtime 关闭 retained OpenFile 并 release task token；取消
running 请求只设置一次标记，worker 在每一页开始前检查。正在进行的单页 BlockIo 不伪造
硬取消，完成后 worker 丢弃该 generation 已产生的无引用预读页。

- random stream reset 取消旧 generation；
- BelowMinimum 取消该 stream 全部预测；
- 最后一个共享 FileDescription 关闭时取消全部 generation，并定向丢弃已填充页；
- truncate 在修改映射和缓存前取消该文件的 queued/running 请求，越过新 EOF 的页仍由
  FilePageCache truncate 负责丢弃和归因；
- duplicate 关闭但对象仍有引用时不会 Finalize，因此不会误取消共享流。

`enqueue = completion + queued cancellation + active`。running cancellation 最终仍走正常
completion，确保 request、OpenFile 和 token retain 各自只有一个释放点。

## 失败语义

- queue 容量拒绝仍只放弃预测，不影响 demand read；
- 取消输出存储不足时先拒绝且不修改 FIFO；
- token、generation、计数或账本守恒损坏属于结构错误，不能继续释放未知所有权；
- running 单页 I/O 已提交时 truncate 仍可能短暂观察 Busy，调用方不得假装硬件访问已撤销；
- close 后到达的反馈可以计为 stale，但不能访问已销毁策略或让复用 stream 收到旧反馈。

## 不变量

- `registration = released stream + active + retiring`；
- `task retain = task release + active task`；
- retiring stream 必须至少持有一个活动任务，任务归零立即释放；
- 页 tag 非空时必须完整且页面状态为 Clean；
- queued cancellation 转移 OpenFile 所有权给调用者，running cancellation 不复制所有权；
- 一个 generation 的取消不删除更新 generation 的页或请求；
- FileDescription applied useful/waste 等于账本 taken useful/waste；
- 调度停止前 stream、retiring、task、request 和 tagged resident 全部归零。

## 边界

5c 不实现设备级硬取消，不改变 rootfs v4、ABI 2.4.0、4 GiB RAM 或 128 GiB 存储规格。
writeback/reclaim 与 Loading/取消同时发生时的完整 EIO/timeout 顺序矩阵仍属于第六增量。

## 验证

- feedback unit 覆盖 Active/Retiring、领取、迟到反馈、槽复用和 generation；
- 固定种子十万步 randomized 模型覆盖 register/task/record/take/retire/stale；
- request unit 覆盖 FIFO 中间取消、running 标记、更新 generation 保留和 terminal 守恒；
- cache integration 覆盖来源 tag、useful、定向 generation 丢弃、invalidate waste 和失败回滚；
- FileDescription integration 覆盖 duplicate token 共享、独立 open 隔离、BelowMinimum 取消和
  waste 使 adaptive maximum 从 32 页收缩到 16 页；
- 4 GiB QEMU 要求 enqueue 与 completion+queued cancellation 相等，反馈、取消、useful/waste
  非零，stream/task/failure/final resident 为零。

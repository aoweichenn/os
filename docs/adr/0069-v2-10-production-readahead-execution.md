# ADR 0069：V2.10 生产顺序预读执行与页身份

状态：已接受（第五增量 5b）

日期：2026-08-23

## 问题

V2.10.5a 只冻结 `FileReadaheadPolicy`。若在前台 `read` 内直接执行 decision，用户线程仍要
等待预测页 I/O；若任务只保存裸 `OpenFile`，close/exec/exit 会提前释放 vnode；若缓存不
区分 demand 与 prefetch，则无法证明预读真正被消费，也无法把未消费淘汰计为浪费。

5b 必须把策略接入生产 VFS、共享 FileDescription、Kernel WorkQueue、异步 BlockIo 和
FilePageCache，同时保持现有 Loading 唯一性、4 GiB 实内存规格和无分配热路径。

## 决策

### 打开实例与观测

每个 RegularFile `FileDescriptionStorage` 内嵌一个默认上限 32 页的
`FileReadaheadPolicy`。duplicate/fork 只共享同一 KernelObject，因此共享 offset 与策略；
独立 open 创建独立策略。VFS 缓存读取 hook 返回本次实际覆盖页、文件页数、hit、miss 和
prefetched hit，FileDescription 在持有自己的可睡眠 operation lock 时推进策略。

小于一页的连续 read 可能反复覆盖同一页。FileDescription 会裁掉已经推进到
`next_expected_page_index` 之前的页，不把同页内连续字节误判为随机访问。

### 有界异步所有权

ProcessRuntime 提供 64 槽 `FileReadaheadRequestQueue`。每个请求保存 VFS、由
`RetainOpenFile` 获得的真实打开引用、页范围和 policy generation；FIFO 只传递所有权，
generation token 防止旧 completion 命中复用槽。队列满属于可接受的预测拒绝：立即关闭
刚保留的引用，但不得让已经成功的 demand read 失败。

请求由现有常驻 Kernel worker 的独立持久 `WorkHandle` 消费。每个 batch 只执行一个请求并
交还调度权；最后一个用户线程退出后，Runtime 先排空全部预读请求，再停止、drain 和释放
worker。入队后若唯一调度通知无法建立，系统 fail-stop，避免静默泄漏打开引用。

### 缓存身份与 Loading

`FilePageCache::Acquire` 增加 `Demand`/`Prefetch` intent。新预取页在来源读取成功、发布
Clean 且完成广播前设置 one-shot `prefetched` 标记；并发 demand waiter 因而也能在接管预留
引用时消费标记。首次 demand hit 原子清除标记并增加 useful hit；普通 hit 和对既有页的
prefetch 都不得重新标记。

预取 worker 可以成为新 Loading 的 owner，并可通过生产 BlockIo 睡眠；它不得作为同页
Loading waiter 阻塞。为此 owner availability 与 waiter availability 分离：活动 User
续体两者都可用，预读 Kernel worker 只开放 owner 路径。已有/Loading 页由执行器跳过，
容量已满或内存进入 BelowMinimum 时停止本次预测，不驱逐 demand 热页。

invalidate、truncate、reclaim/trim 丢弃未消费标记时增加 waste；失败 fill 不留下 entry、
frame 或标记。5b 把真实 useful/waste 统计建立起来，但基于 waste 的跨任务反馈和按
generation 取消仍由 5c 完成。

## 不变量

- 一个请求从 enqueue 到 complete 恰好拥有一个 retained `OpenFile`；完成后必定关闭；
- `enqueue = completion + active`，且 queued/running 之和等于 active；
- 同一文件页仍只有一个 Loading owner，预读不创建第二个来源读取；
- `successful prefetch loads = resident prefetched + useful hits + wasted prefetched`；
- prefetched 标记只能属于 Clean 页，并且只被首次 demand 获取消费；
- 预测拒绝不改变 demand read 的成功结果，结构损坏不得静默继续；
- 退出时请求 active、worker registered/running 和 prefetched resident 均归零。

## 结果与边界

生产顺序读现在能在独立 Kernel worker 上提前填充 page cache，并通过真实异步 root
BlockIo 完成；close/exec/exit 不会让任务引用悬空。最终只输出聚合统计，不逐页打印。

5b 不实现按 generation 取消已排队范围，不在 pressure 变化后追溯撤销任务，也不把
truncate/invalidate/trim 的 waste 反馈回已经销毁的 FileDescription。上述策略闭环、并发
writeback/reclaim 矩阵和错误取消属于 5c/第六增量。

## 放弃的方案

- **在用户 read 内同步预取**：预测页延迟仍由前台承担。
- **任务保存裸 vnode/OpenFile 副本**：close/exec/exit 后形成悬空所有权。
- **预取命中既有页时补标记**：会把 demand 已加载页伪装成预读成果。
- **Kernel worker 等待同页 Loading**：可能让唯一通用 worker 被前台页长期占住。
- **缓存满时为预读主动淘汰**：预测流会驱逐 demand 工作集并放大抖动。

## 验证

- queue unit 与固定种子十万步 randomized 模型覆盖 FIFO、满载拒绝、乱序 complete、槽位
  复用和 stale token；
- cache lifecycle integration 覆盖预取发布、one-shot demand hit、既有页不重标、失败回滚
  和 invalidate waste；
- FileDescription integration 通过真实 VFS hook 验证 duplicate 共享、独立 open 隔离；
- 4 GiB `-mem-prealloc` QEMU 先校验 47 个真实工具 ELF，再顺序读取未执行的 `/bin/smoke`
  冷文件，要求 schedule/enqueue/completion 守恒、loaded/useful/prefetch-hit 非零、失败与
  活动请求为零，最终 trim 后无预取驻留页。

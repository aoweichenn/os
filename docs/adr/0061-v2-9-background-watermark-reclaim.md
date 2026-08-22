# ADR 0061：V2.9 后台水位回收与候选消费

状态：已接受（第五增量）

日期：2026-08-22

## 问题

V2.8 的用户分配在 free pages 低于 low watermark 时立即执行 direct reclaim，分配线程要
同步承担 cache 扫描、文件写回和 swap I/O。V2.9.4 已产生冷热候选，但只保存聚合数量；
如果后台线程用 `Inactive && eligible` 反推候选，刚降级的页会少经历一轮冷却。文件缓存
还可能复用同一个 frame，旧物理身份会错误命中新页。

## 决策

新增纯状态模块 `BackgroundReclaimController`。状态为 Sleeping、Running、BackingOff：
free 低于 low 时唤醒，达到 high 时停止；无候选、仅完成写回或失败时等待一秒 deadline。
每个决策最多回收 64 页，target 为 `min(64, high-free)`。用户分配在 low 到 min 之间返回
Allow 并通过回调合并后台 WorkItem；低于 min 才执行原有 direct reclaim。

ProcessRuntime 在现有常驻 Kernel Worker 上注册第三个生产 handle。批次在 WorkQueue 锁外
依次执行 cold clean file eviction、dirty/error writeback、cold anonymous swap，共享同一
64 页 I/O 预算，完成后 yield。timer IRQ 只到期 deadline 和请求重调度。

`PageAgingEntry` 保存显式 `reclaim_candidate`。Active 首次冷观察只降级；Inactive 再冷一
轮才提交 candidate。FilePageCache access generation 进入 aging entry；generation 改变时
撤销旧候选并重新冷却，后台 selection 再与当前 cache entry 比较。文件与匿名执行器都有
selection/completion：成功释放前调用 `PageAgingManager::Forget`，从 hash 和侵入队列删除
精确物理身份。

写回只算 I/O progress，不算 reclaimed page；写回后的 Clean 页必须由后续 aging 再确认。
设备失败沿用 FilePageCache Error/paused 或 swap 失败语义，WorkItem 记录 Failed 并退避；
PageAging、cache、账本损坏设置运行时失败门禁。

## 不变量

- Sleeping 只有 `free < low` 才进入 Running，Running/BackingOff 在 `free >= high` 停止；
- batch requested 不超过 64，clean+anonymous 等于实际 reclaimed，written 与 reclaimed
  合计不超过本批 requested；
- 刚降级页不是 candidate，candidate 必须是显式提交状态；
- file candidate 的 aging generation 必须等于当前 cache access generation；
- completion 失败时未选页不得计入回收，成功 frame 不得保留 aging 身份；
- IRQ、WorkQueue 锁和分配快路径不执行扫描、VFS 或设备 I/O；
- 无实际回收时不得即时自旋；仅写回也必须等待下一轮老化；
- 停止后 controller 为 Sleeping，三个生产 handle、deadline、waiter 和四队列均为零；
- 第五增量不冻结 direct/background 的 anonymous 公平份额，也不改变 OOM 终决策。

## 后果

普通低水位压力由可睡眠 Worker 承担，分配线程只在 minimum 紧急区同步回收。显式候选和
generation/completion 使物理身份在开始实际释放后仍可验证。clean-first 顺序可能让某些
批次没有后台 anonymous 页；第六增量需要专用压力矩阵决定 direct/background 公平性、
swappiness 配额和 OOM 顺序，不能从单次协作调度轨迹推断策略。

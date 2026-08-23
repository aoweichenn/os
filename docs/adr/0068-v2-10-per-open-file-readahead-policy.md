# ADR 0068：V2.10 打开文件级顺序预读策略

状态：已接受（第五增量 5a）

日期：2026-08-23

## 问题

V2.10.4 已让同一文件页的并发 miss 共享唯一 Loading 和来源读取，但每个新页仍要等前台
访问先产生 miss。顺序扫描 ELF、工具或普通文件时，磁盘只在用户已经需要下一页后才开始
工作，无法把设备延迟与应用处理时间重叠。

直接在每次 read 后固定多读若干页会放大随机访问、污染 page cache，并在内存压力下与
reclaim 争抢 frame。把状态放到 vnode 或全局文件身份上，又会让两个独立打开流互相改变
窗口。第五增量必须先冻结一个打开文件级、无分配、可验证的纯策略，再让后续 worker 接入
真实 I/O。

## 上游依据

Linux 的 `file_ra_state` 保存最近窗口的 start、size 与 async tail。miss 触发同步预读，
带 readahead 标记的缓存页被访问时触发下一异步窗口。当前 `get_init_ra_size()` 先把请求页数
向上取二次幂，再按相对上限分档放大；`get_next_ra_size()` 对小窗口 4 倍、普通窗口 2 倍
增长，最终封顶。

参考：

- [Linux Memory Management APIs：Readahead](https://docs.kernel.org/core-api/mm-api.html#readahead)
- [Linux `mm/readahead.c`](https://github.com/torvalds/linux/blob/master/mm/readahead.c)

本项目保留这些窗口语义，但不复制 Linux 的 xarray/folio、backing-dev、THP 或启发式历史
扫描；这些基础设施尚不存在，照搬会制造无法履行的接口。

## 决策

### 纯策略与所有权

新增 `memory/file_readahead.*`。每个 `FileReadaheadPolicy` 实例表示一个未来打开文件流，
只保存固定宽度值和枚举，不分配内存、不访问 VFS、page cache、设备或 scheduler。5b 接入
时实例归共享 `FileDescription` 所有，因此 duplicate/fork 共享流状态，独立 open 互不影响。

5a 只输出 `FileReadaheadDecision`：窗口范围、真正需要预取的异步范围、触发页、generation
和有效上限。它不提交 WorkItem、不创建 Loading、不标记缓存页，也不宣称预读已经进入生产
路径。

### 触发与窗口

访问触发分为 `DemandHit`、`DemandMiss` 和 `PrefetchedHit`：

- 首次 demand miss、连续 demand miss 或回到文件第 0 页的 miss 建立初始窗口；
- demand hit 只推进流的期望页，不额外提交；
- `PrefetchedHit` 必须是连续访问，并且访问区间覆盖当前异步尾部的唯一触发页；否则输入
  无效且状态不变；
- 非连续访问重置当前窗口，但保留累计统计和反馈后的自适应上限；
- 所有窗口按当前文件页数裁剪，EOF 后不产生零页提交。

默认配置上限为 32 页，即 4 KiB 页下的 128 KiB。初始窗口复用 Linux 分档：请求数向上取
二次幂后，小于等于上限 1/32 时乘 4，小于等于 1/4 时乘 2，否则取上限。32 页配置下，
1..2 页请求得到 4 页窗口、3..4 得到 8 页、5..8 得到 16 页，更大请求最多产生 32 页
窗口。异步触发后的窗口按 4 倍/2 倍规则增长并封顶，典型单页流为 4、8、16、32。

demand miss 的窗口包含显式请求区间，decision 只把其后的异步尾部交给未来 worker；
`PrefetchedHit` 产生的下一窗口全部是异步区间。generation 每次提交递增，reset 不回绕，
未来任务可据此拒绝旧计划。

### 内存压力与反馈

有效上限取配置上限、自适应上限和压力上限的最小值：

| 压力 | 压力上限 |
| --- | --- |
| `Balanced` | 配置上限 |
| `BelowHigh` | 配置上限的 1/2，至少 1 页 |
| `BelowLow` | 配置上限的 1/4，至少 1 页 |
| `BelowMinimum` | 0，清除活动窗口且不提交 |

`RecordFeedback(useful, wasted)` 接收未来执行层归因后的页数。wasted 大于 useful 时，自适应
上限向上取整减半，最低 1 页；useful 大于 wasted 时翻倍恢复，但不超过配置上限；相等时只
记账。反馈不修改已经发布的窗口，只影响下一次 decision，避免计划范围在执行中漂移。

5a 不判断一个具体页何时成为 useful 或 wasted。5c 必须由缓存页的预读身份、首次 demand
命中、truncate/invalidate/reclaim 和 close 共同完成归因，不能仅凭 cache hit 总数猜测。

### 失败原子性与验证

公开操作先复制当前统计，在候选副本上完成全部范围和计数检查，再一次提交。非法配置、
零长度/越界访问、伪造 PrefetchedHit、空反馈、计数上溢或 generation 耗尽都不得改变原
状态。`Validate` 重新检查访问分类、触发分类、计划/generation、窗口范围、反馈和压力
关系。

## 不变量

- 状态属于一个打开文件流，不属于全局 vnode；
- `access = initial + sequential + random = demand hit + demand miss + prefetched hit`；
- `submission decision = generation`，reset 不复用旧 generation；
- 活动窗口必须有非零异步尾部，触发页恰为 `window end - async size`；
- 计划预取页数不大于计划窗口页数，窗口和请求永不越过 EOF；
- 自适应上限位于 `[1, configured maximum]`，有效上限还必须服从当前压力；
- BelowMinimum 下没有活动窗口；随机访问与显式 reset 都清除当前流；
- 所有失败保持 decision 清零且策略状态不变。

## 结果

第五增量获得一个可独立测试的、与 Linux 核心窗口规则对齐的策略边界。32 页默认上限只是
5a 的参考配置，5b 接入时仍由打开文件初始化明确写入，不通过宏或隐藏全局变量改变。

代价是 5b/5c 还必须实现 FileDescription 所有权、预读页标记、异步 WorkItem、EOF/close/
truncate 取消、实际反馈归因和与 FilePageLoad waiter 的组合。5a 通过不等于生产读路径已经
变快，QEMU 统计和系统行为本增量保持不变。

## 放弃的方案

- **每个 demand read 固定预取 N 页**：无法识别随机访问，也没有逐级增长和反馈。
- **把状态放在 FilePageCache address space**：独立 open 和交错顺序流会互相污染。
- **5a 直接提交同步 I/O**：会让用户线程替预测页付出延迟，违背异步预读目标。
- **完整复制 Linux folio/xarray 算法**：依赖本项目尚未实现的基础设施，接口不可验证。
- **压力到来后修改已发布窗口**：会让 worker 与策略对同一 generation 的范围理解不同。

## 验证

- unit 覆盖 4→8→16 增长、随机重置、连续重建、EOF、四级压力、反馈缩放、非法触发和
  `UINT64_MAX` 配置边界；
- integration 交错两个策略实例，验证窗口、generation、反馈和压力互不污染；
- 固定种子十万步 randomized oracle 交错访问、反馈和 reset，逐步比较完整 decision、统计
  与 `Validate`；
- 5a 不增加 QEMU marker；完整 4 GiB CAW 回归只负责证明模块加入没有改变生产行为。

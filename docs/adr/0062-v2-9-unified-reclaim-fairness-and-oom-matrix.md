# ADR 0062：V2.9 统一回收公平配额与 OOM 压力矩阵

状态：已接受（第六增量）

日期：2026-08-22

## 问题

第五增量的后台线程已经消费冷热候选，但 direct reclaim 仍使用独立的 clean-first 计划，
后台路径则手工执行 clean、writeback、anonymous。两条路径没有同一个可测试的匿名份额，
`swappiness` 也不能稳定解释为策略。匿名页在正常 unmap/exit 时还可能绕过 aging completion，
让已经释放的物理 frame 暂时保留旧身份。既有 QEMU 压力只证明 swap 发生，没有验证真实
OOM victim 的选择、SIGKILL wait 结果和 ATA/NVMe 后端一致性。

## 决策

`PlanMemoryReclaim` 同时供 direct 和 background 使用。目标页数为 `target` 时，匿名期望
配额为 `floor(target * swappiness / 200)`；当 file/anonymous 均有候选、`target >= 2`
且 swappiness 位于 1..199 时，两类至少各保留一页。先按期望配额取得可用页，某类不足的
未用配额再转赠另一类；file 配额内部仍保持 Clean 优先、Dirty/Error writeback 其次。
`swappiness=0` 禁止匿名 swap，`200` 优先全部匿名，候选不足仍允许转赠 file。

UserMemory 保存可在初始化前配置的 swappiness，并输出 direct 计划的 file/anonymous
累计预算。ProcessRuntime 后台批次使用 PageAging candidate、FilePageCache 状态和空闲
swap 槽构造同一输入。私有匿名 frame 的最后引用在归还物理分配器前通知 ProcessRuntime；
已存在或已经由 swap completion 删除的 aging 身份都能安全收束。

新增 `oom-pressure` 系统 profile：驻留上限为 12288 页，swappiness 为 0。`oom_probe`
从 `/proc/meminfo` 读取当前 allocated 与 resident limit，子进程占用剩余预算减 512 页，
父进程最多比子进程少 64 页，因此子建立时保留水位安全垫，父触发 OOM 时又始终不是最高
分候选。门禁要求子进程收到 SIGKILL、父进程 wait/reap 后继续退出、PID1 存活、OOM 与
no-progress 非零、anonymous swap 为零，并在 ATA/NVMe root+swap 上分别到达 READY。

## 不变量

- file budget + anonymous budget 不超过 target，计划各阶段之和等于总计划页数；
- 任一候选类别不足时配额可转赠，但 swappiness 0 不得产生匿名计划；
- direct/background 只能调用同一个 planner，不得各自复制权重算术；
- file 配额内 Clean 必须先于 writeback，I/O 失败不得转化为 OOM；
- 私有匿名 frame 最后释放后不得留在 PageAging hash 或四队列；
- OOM profile 必须真实触发一次 no-progress、一次 invocation 和一次 kill；
- OOM victim 必须是非当前子进程，PID1 与当前 fault 进程不得被该场景杀死；
- 最终 committed、active swap、PageAging、Process/Thread、VMA、页表和 frame 全部回到基线；
- 系统测试只使用 4 GiB `-mem-prealloc` 和真实 28 GiB swap，不引入稀疏内存语义。

## 后果

swappiness 现在是 direct/background 的统一、可预测策略，而不是某条路径的提示值。极端
配置和候选短缺仍能前进，匿名正常释放也不会依赖下一轮 aging 扫描清理。OOM 测试根据
来宾实时预算缩放，不把 ATA/NVMe 的缓存时序写成固定页数猜测；代价是该 profile 比普通
pressure 多一次完整用户工作负载和约数秒的真实匿名缺页。

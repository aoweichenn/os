# ADR 0060：V2.9 PTE Accessed 采样与四队列页面老化

状态：已接受（第四增量）

日期：2026-08-21

## 问题

direct reclaim 当前按地址和旧 access generation 选页，不知道一个用户 PTE 最近是否由
硬件实际访问。若直接启动后台回收，会把仍有热 alias 的 shared frame 当成冷页。按每个
虚拟地址建队列又会重复计算 fork/mmap alias，并在物理帧释放复用后留下旧身份。

## 决策

增加 `PageTableManager::TestAndClearAccessed`。接口只接受 4 KiB 对齐叶项，返回映射快照
和采样前 A 位，成功时仅清 A；目标 CR3 当前活动才 `invlpg`。大页、共享分支修改、损坏
表和非映射页继续返回类型化状态。

增加独立 `PageAgingManager`。身份键为 `(physical_address, File/Anonymous)`，存储由调用
方提供，开放寻址 hash 负责查找，四条侵入式 FIFO 表示 active/inactive × file/anonymous。
每轮相同身份的 Accessed 做 OR、eligibility 做 AND。新页先 Active；未访问 Active 降级；
未访问 Inactive 且 eligible 记录候选；访问 Inactive 提升。

跨轮若同一物理地址的旧 kind 本轮尚未观察，视为 frame 合法复用，删除旧 entry 后重分类；
同轮两种 kind 仍返回 KindConflict。未观察身份在轮末删除。候选只是统计，不由 manager
释放 frame。

ProcessRuntime 在现有常驻 Worker 上注册第二个周期 WorkItem，每秒先访问全部 file-cache
entry，再扫描所有拥有 CR3 的 Process/VMA。PID1、UserStack、COW alias、映射中的 file
page 和非 Clean file page不可成为候选。Zombie 结果槽的 CR3 已清零时跳过。

元数据使用真实 KernelPageAllocation：4 GiB 功能档 4096 entry/8192 hash，32 GiB 容量
档 32768/65536。allocation 在 Process 资源基线前建立并持续到关机，避免扩大 BSS，也
避免把宿主稀疏文件当 RAM。

## 不变量

- 每个 tracked entry 恰出现于一个 hash slot 和一条匹配 kind/state 的队列；
- free list、四队列和 hash 不重不漏，tracked+free 等于 capacity；
- 同一轮每个物理身份只有一个 kind，alias count 至少为一；
- 新 entry 不能在首次冷观察中直接成为候选；
- 清 A 不改变物理地址、R/W、U/S、COW、NX 或 cache 属性；
- aging operation 不在 timer IRQ、WorkQueue lock 或 FilePageCache mutation 中执行；
- Worker 停止后 tracked/active/inactive/current candidate 和 deadline 全为零；
- 第四增量不得改变 clean/writeback/swap/OOM 执行顺序。

## 后果

第五增量已按 [ADR 0061](0061-v2-9-background-watermark-reclaim.md) 消费冷候选，并把
candidate 固化为显式条目状态；file cache access generation 变化会撤销同类旧候选，
成功回收通过 Forget 删除身份。经典双队列比 MGLRU 信息少，但状态与 A 位因果关系可由
当前单核模型验证。后续若升级多代 LRU，物理身份、alias 聚合与页表采样接口仍可保持。

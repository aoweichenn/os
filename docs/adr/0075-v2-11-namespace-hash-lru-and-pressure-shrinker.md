# ADR 0075：V2.11 namespace hash、LRU 与压力 shrinker

状态：已接受（第六增量）

日期：2026-08-23

## 问题

线性扫描适合冻结语义，不适合 4096 dentry/2048 inode 的生产热路径。固定槽又不会像 Linux
slab 一样归还 backing page，因此必须区分“释放可重建条目”和“释放物理内存”，避免虚报
回收效果。

## 决策

调用方额外提供 hash entry 与 bucket 数组。生产配置为：

- 4096 dentry slot、4096 dentry hash entry、8192 bucket；
- 2048 inode slot、2048 inode hash entry、4096 bucket。

hash 覆盖完整 key/identity，bucket 使用槽索引链。只有 Cached 项进入 index；失效先摘除
index 再转 Stale，因此旧 token 可继续读取但新 lookup 不可见。未配置 index 的 host 模型
保留线性路径，用于对照；两个十万轮随机模型启用最小 bucket 配置强制碰撞、删除和复用。

LRU 仍使用单调 access generation。dentry 只回收 Cached 且零 external ref；inode 还要求
零 dentry ref、零 external ref，Loading metadata 不可回收。发布容量不足时先各尝试一个
dentry/inode LRU；仍不足则旁路缓存。

`Vfs::ReclaimNamespaceCache` 提供有界 shrinker。后台内存压力每批最多回收 64 个 dentry 和
64 个 inode 逻辑条目，不把数量计入 reclaimed physical page。固定 BSS backing 始终真实驻留；
shrink 的收益是缩短活跃生命周期并腾出槽，而不是伪造 RAM 下降。

## 不变量

- 每个 Cached 槽恰好出现于一个正确 bucket，每个 Free/Stale 槽不在 index；
- bucket 链索引有界、无环、hash 与完整 key 二次判等；碰撞不能产生错误命中；
- index 插入/摘除与 Cached 状态转换属于同一 cache spin-lock 事务；
- shrinker 不回收引用项、Loading 或 Stale；Stale 由最后 release 回收；
- hash/index backing 容量必须与 slot 容量匹配，bucket 不小于 slot；
- fixed backing 不计作物理页 reclaim，pressure page accounting 保持原值。

## 验证

- unit 覆盖非法/重复配置、发布、碰撞链、失效、LRU 和 Destroy；
- 两个固定种子十万轮模型逐轮 Validate hash/slot/reference 关系；
- production integration 显式 shrink 后要求下一次解析重新访问 backend；
- reclaim/OOM 系统矩阵验证 background worker 接入后 page accounting、预读、写回和 swap
  守恒不变。

## 后果

V2.11 的固定容量 namespace cache 已具备生产 lookup 复杂度和压力收缩入口。因为 backing
不动态释放，未来 slab 化属于新的内存分配阶段，不回写本阶段“固定真实内存”的规格。

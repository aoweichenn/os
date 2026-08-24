# ADR 0083：V2.18 采用 extent、分组 reservation 与 delayed allocation

- 状态：Accepted
- 日期：2026-08-24
- 影响：rootfs v5 extent 盘面、block-group allocator、writeback、范围操作与 journal ordered 语义

## 背景

v2.16 已冻结 4 KiB block group，v2.17 又建立 prepared/ordered/commit/checkpoint journal，
但文件仍没有从逻辑块映射到物理块的结构。若继续沿用 v4 的逐块指针树，连续文件会为每个
块保存指针，也无法区分预分配但尚未写入的空间。块位图若在 extent 插入失败前直接提交，
又会留下无法回收的块；过早选择物理位置则会削弱连续分配和组内局部性。

Linux ext4 使用 extent 压缩连续映射，使用 multi-block allocator 和 delayed allocation 改善
局部性，并用 unwritten extent 表示“已经占用物理空间、经文件系统读取仍应返回零”的范围。
V2 采用这些语义，但继续使用项目 magic、64 位字段、显式小端编码和自研算法。

## 决策

### extent 盘面与运行时树

extent leaf/index node 均为 4096 字节：128 字节 header、最多 123 个 32 字节 entry、28 字节
零保留区和末尾 CRC32C。header 保存版本、tree/inode generation、inode number、depth、entry
count 和文件系统 UUID。leaf 保存 64 位 logical start、physical start、block count 和
Initialized/Unwritten；index 保存 logical start、child block、child generation 和覆盖长度。

运行时验证模型最多保存 256 个 canonical extent。为在小几何测试中强制触发层级变化，内存
B+tree 使用 4 路节点：最多 64 leaf、16+4+1 index，共 85 node、最大深度 3。每次修改先在
canonical extent 集合完成 overlap/split/merge，再重建唯一树形；这不是生产路径的逐次原位
写盘算法，v2.20 mount 接线时必须通过 journal 提交实际 node block。

逻辑区间和物理区间都必须唯一。盘面 decode、运行时 Insert 和 Validate 三层都拒绝重复物理
所有权；相邻且逻辑/物理均连续、状态相同的 extent 必须合并。

### block-group allocator

allocator 不常驻复制 1024 张生产 bitmap，而是接收调用方提供的 descriptor、bitmap storage 和
每组 free count。搜索从 preferred group 开始，优先返回同组连续 run；没有完整请求时可在
满足 minimum 的前提下返回 partial，随后循环其他组。journal/protected range 永不参与候选。

一次分配先产生 reservation token 并临时置位。extent 插入成功后 Commit 保留位图；失败则
Abort 清位并恢复 free count。token 带 slot+generation，过期 token、重复释放、metadata/tail/
protected block 释放和未分配块释放均拒绝。ENOSPC 不得改变任何位或计数。

### delayed allocation 与范围状态

ReserveWrite 只登记逻辑脏范围，不分配物理块。BeginWriteback 才取得精确 reservation 并插入
Unwritten extent；数据成功稳定后 CompleteWriteback 将其转换为 Initialized 并提交 reservation；
失败时 AbortWriteback 删除临时 extent、回滚 bitmap，delayed range 保留以便重试。

```text
Delayed (page cache owns data, no physical block)
  → reservation + Unwritten (read as zero until data stable)
      → data stable
          → Initialized + reservation commit
```

Fallocate 创建 Unwritten extent；PunchHole 预检所有物理范围后删除映射并释放；Truncate shrink
同时清除 EOF 之后的 delayed、initialized、unwritten 和 keep-size 预分配。SEEK_DATA 把
Delayed/Initialized 视为 data，SEEK_HOLE 把 absent/Unwritten 视为 hole；FIEMAP 类 QueryRanges
分别返回 Delayed、Unwritten、Initialized，Delayed 的 physical block 为 NO_BLOCK。

### journal 与生产边界

extent metadata 作为 journal v2 metadata payload，文件数据作为 ordered data。64 个故障点必须
证明：只要恢复后的 extent node 已经引用 Initialized block，对应数据就已经稳定。allocator/
tree/delalloc 的 hosted 模型不替换生产 rootfs v4，也不新增用户 syscall；fallocate、punch、
SEEK_DATA/SEEK_HOLE 和范围查询目前是内部语义接口，生产 ABI 在 v2.20 mount 切换时一次冻结。

## 验收

- leaf/index 覆盖小端 round-trip、CRC32C、UUID、generation、逻辑/物理 overlap、非法状态、
  保留区和目标越界；
- 256 extent 形成 85 node/深度 3，删除后深度收缩；unwritten/initialized 中段转换可 split 后
  重新 merge；
- allocator 覆盖 locality、partial、group fallback、64 reservation、commit/abort、release、
  protected range、stale token、ENOSPC 和全量 Validate；
- delalloc 覆盖 begin/complete/abort writeback、fallocate、punch、truncate、keep-size EOF 清理、
  SEEK_DATA/SEEK_HOLE 和范围查询；
- extent capacity 失败必须回滚 reservation 且保留 delayed range；
- extent+journal 64 点故障矩阵满足 metadata-new implies data-new；
- 固定种子十万步逐逻辑块 oracle 比较 extent state、physical ownership、free count、file size 和
  seek 结果；
- fresh CAW 全构建、命名门禁和既有 4 GiB rootfs v4 矩阵通过。

## 依据

- Linux Kernel ext4 文档：[Extent Tree](https://docs.kernel.org/filesystems/ext4/dynamic.html)
- Linux Kernel ext4 文档：[Block and Inode Allocation Policy](https://docs.kernel.org/filesystems/ext4/allocators.html)
- Linux Kernel 文档：[FIEMAP](https://docs.kernel.org/filesystems/fiemap.html)

## 后果

v2.19 可以在稳定的文件块映射之上实现变长目录和 HTree，也可以让 xattr/quota 更新复用同一
journal/allocator。代价是当前树以重建方式验证 canonical 形状，allocator 没有并发 per-group
锁，existing initialized extent 的覆盖写仍沿既有 page-cache 路径而不进入 hole delalloc；真正
的 v5 mount、磁盘 node 分配、用户 ABI 和并发 writeback 留给 v2.20。

## 关联决策

- [ADR 0079：V2 小型 ext4 与 rootfs v5](0079-v2-mini-ext4-rootfs-v5-program.md)
- [ADR 0082：V2.17 journal v2](0082-v2-17-rootfs-v5-journal-v2.md)

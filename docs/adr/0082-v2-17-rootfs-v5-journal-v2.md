# ADR 0082：V2.17 采用有界多槽 journal v2

- 状态：Accepted
- 日期：2026-08-24
- 影响：rootfs v5 journal、ordered durability、revoke、checkpoint、orphan file 与恢复测试

## 背景

v2.16 已冻结 4 KiB、block-group 和 CRC32C 盘面，但尚无可保护后续 extent、allocator 和目录
更新的事务层。rootfs v4 的 journal 使用 512 字节块和单个 prepared/commit 区，只能在 commit
后立即 checkpoint；它没有 revoke、独立 checkpoint 或 orphan file，继续扩展会把 v4 盘面
语义混入 v5。

Linux JBD2 的关键失败语义是：descriptor 和 payload 先稳定，ordered 模式的数据先于 commit
稳定，有效 commit 才允许 replay；revoke 阻止旧 metadata 重放到已经释放或改作他用的块。
ext4 orphan file 则保存仍需在崩溃后截断或释放的 inode。V2 采用这些状态语义，但继续使用项目
magic、小端字段、CRC32C 和自研实现，不复制 JBD2/ext4 盘面。

## 决策

### 独立 v5 journal 区

journal v2 使用 81 个 4 KiB 文件系统块。第 0 块是 journal superblock，之后是 4 个固定槽；
每槽 20 块：1 个 descriptor、1 个 revoke、最多 16 个 metadata payload、1 个 commit 和
1 个 checkpoint。单事务另可携带最多 8 个 ordered data home write 和 32 个 revoke target。

journal inode 编号冻结为 8，orphan file inode 编号冻结为 15。v2.17 只建立独立格式和
BlockDevice 模型；v2.18 allocator/extent 完成前，不把这些编号映射进生产 v5 镜像，也不改变
v2.16 已冻结的 group metadata 偏移。

### 提交顺序

一次成功事务按以下顺序落盘：

```text
reserve sequence in journal superblock + Flush
  → descriptor + revoke + metadata payload + Flush
  → ordered data 写入 home block + Flush
  → commit record + Flush
  → committed slot 等待 checkpoint
```

没有有效 commit 的槽可以丢弃。ordered data 在 commit 前写入可能留下不可达的新字节，但旧
metadata 仍然有效；一旦 commit 可见，引用这些数据的 metadata 才允许 checkpoint/replay，
所以“新 metadata 可见”必然蕴含“数据已稳定”。

### revoke、checkpoint 与恢复

checkpoint 与 commit 分离。正常路径只按 sequence 从旧到新 checkpoint；home metadata 完成
并 Flush 后，先写 checkpoint record 并 Flush，再更新 journal superblock，最后清槽。任何中间
断电都允许下一次恢复重做，不会把未稳定的 home write 标成完成。

恢复先验证全部有效 commit 引用的 descriptor、revoke 和 payload，再按 sequence 重放。某目标
若出现在更晚的 committed revoke block 中，较早 payload 必须跳过；同事务中 StageMetadata 与
Revoke 采用最后操作生效，并在失败返回前保持原事务不变。commit 不存在或无效视为 incomplete
并丢弃；有效 commit 引用的 checksum、UUID、计数、目标、保留区或 payload 损坏视为 Corrupt，
不得静默丢弃。

### orphan file

orphan block 是独立 4 KiB CRC32C metadata，保存 UUID、generation、非零 entry count 和最多
503 个 64 位 inode number。add/remove 幂等，重复、越界、计数不守恒和满容量均有明确结果。
orphan block 与目标 inode/bitmap 更新通过普通 journal metadata payload 同事务提交；实际清理
策略留给 v2.18/v2.20 的 extent 和完整 fsck。

### 设备与生产边界

journal 每个 4 KiB I/O 被拆成连续 8 个 512 字节 `BlockDevice` 操作；只有成功 `Flush` 才构成
持久边界。测试设备分别保存 volatile 和 durable sector，Crash 会丢弃未 Flush 写入，因此断电
测试不是“写调用即持久”的弱模型。

rootfs v4、ABI v2.6.0、ATA/NVMe、4 GiB RAM、128 GiB 生产盘和 VGA 协议不变。v5 在 v2.20
前仍不挂载为生产根。

## 验收

- 所有 journal/orphan 记录逐字段小端编码并校验 magic、version、UUID、feature、CRC32C、计数、
  目标范围、重复项和零保留区；
- unit 覆盖状态、credit、sequence、四槽容量、ordered commit、checkpoint 和 orphan 满容量；
- 集成覆盖两个 committed transaction 的跨事务 revoke、orphan add/remove 与 inode metadata
  同事务重放；
- commit 路径至少 128 个故障点、recovery 路径至少 96 个故障点，最终满足 old/new 与 ordered
  蕴含关系，并验证二次恢复 Clean；
- 有效 commit 的 payload 损坏必须返回 Corrupt；
- 固定种子十万步模型比较 metadata/ordered/revoke/abort/commit/checkpoint 与独立 home oracle，
  且不得让重复 CRC 计算拖慢完整 verify；
- fresh CAW 全构建、命名门禁和既有 4 GiB rootfs v4 整机矩阵通过。

## 依据

- Linux Kernel ext4 文档：[Journal (jbd2)](https://docs.kernel.org/filesystems/ext4/journal.html)
- Linux Kernel ext4 文档：[Orphan file](https://docs.kernel.org/filesystems/ext4/orphan.html)
- Linux Kernel ext4 文档：[Special inodes](https://docs.kernel.org/filesystems/ext4/special_inodes.html)

这些资料只用于确定提交、revoke 和 orphan 失败语义；项目格式保持小端且使用自己的 magic。

## 后果

v2.18 可以在稳定事务边界内实现 extent split/merge、分组分配和 delayed allocation，不需要
再次发明断电协议。代价是当前 journal 容量和槽数有界，尚无后台并发 checkpoint，也没有
把 journal/orphan inode 写入生产 v5 镜像；这些边界必须在后续阶段显式消除，不能把独立模型
描述为已可挂载文件系统。

## 关联决策

- [ADR 0079：V2 小型 ext4 与 rootfs v5](0079-v2-mini-ext4-rootfs-v5-program.md)
- [ADR 0081：V2.16 block-group 盘面基础](0081-v2-16-rootfs-v5-block-group-format.md)

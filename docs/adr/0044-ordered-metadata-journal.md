# ADR 0044：固定区 ordered metadata journal 与幂等恢复

- 状态：已接受
- 适用版本：v1.17
- 决策日期：2026-07-28

## 背景

v1.6 的 rootfs v2 用 superblock transaction state 区分 Clean 与 Dirty。
这能拒绝一个已知不完整的事务，却不能回答断电后的三个关键问题：

1. 哪些元数据块属于同一个逻辑操作；
2. 哪些块已经稳定到设备、哪些仍只是 cache 中的写入；
3. mount 应恢复旧状态、应用新状态，还是拒绝镜像。

rename、truncate、create 和 remove 都可能同时改变 inode、目录数据块、inode
位图、data 位图、间接指针块与 superblock。若直接覆盖 home block，断电可能
留下“目录项已出现但 inode 未分配”或“位图已释放但旧 inode 仍引用该块”的
中间状态。仅靠 mount 时把 Dirty 改回 Clean 会把损坏伪装成恢复。

v1.16 已经建立 ATA FLUSH 稳定边界与文件页写回责任，因此 v1.17 可以把写前
日志、设备顺序和 mount replay 连接起来。本项目仍只使用 QEMU 模拟 ATA
硬件；journal、校验、恢复、故障注入与 oracle 都由项目自行实现。

## 决策

rootfs 升级到盘面格式 3，magic 为 `OSRFV003`。在原 256 MiB 分区中固定保留
256 个 512 B 块（128 KiB）作为单事务 metadata journal：

```text
journal[0]       header
journal[1..4]    四个 descriptor block
journal[5..128]  最多 124 个 metadata payload block
journal[129]     commit record
journal[130..]   保留，必须保持为零
```

每个 descriptor entry 占 16 字节：

```text
u64 target_relative_block
u32 payload_crc32
u32 reserved_zero
```

descriptor 块末尾保存自身 CRC32。header 保存 magic、格式版本、64 位 sequence、
entry count、descriptor count 和 CRC32。commit 保存独立 magic、格式版本、
相同 sequence、entry count、header CRC32 和自身 CRC32。所有整数使用固定
宽度小端编码，盘面结构不依赖编译器 padding 或宿主类型宽度。

四个 descriptor 块每块可描述 31 个 payload，因此最大 credit 数为 124。
事务开始时必须预留非零且不超过 124 的 credit。超过容量在 transaction
overlay 发布任何条目前失败；事务中发现不同目标块超过预留则失败并 abort，
不会把 staged metadata 写入 home 位置。

## 内存事务模型

`RootJournal` 维护固定容量 staged block 数组，不做动态分配。相同 target
再次 stage 会覆盖同一 overlay entry，不额外消耗 credit。rootfs 在事务期间
读取元数据时先查询 overlay，再查询 BlockCache/设备，因此同一操作后续步骤
能观察自己尚未 checkpoint 的修改。

事务开始时 rootfs 保存 superblock、统计和 allocation hint 快照。逻辑检查、
credit 或设备操作在 commit 前失败时：

1. 丢弃 journal overlay；
2. 恢复内存快照；
3. 保持 home metadata 不变。

普通文件数据不进入 journal。新增数据块和用户写入先经 BlockCache 落盘；
元数据 commit 前先执行 cache sync。释放块在 metadata commit 前不主动清零，
避免旧 inode 在 abort/断电后仍指向已被提前清空的数据块。

## 提交顺序

每次成功事务严格执行：

```text
1. 将相关普通数据写到其 home block
2. FLUSH data
3. 写 header、descriptor 和 metadata payload
4. FLUSH prepared transaction
5. 写 commit record
6. FLUSH commit
7. 将 payload 写到各自 metadata home block
8. FLUSH checkpoint
9. 清零 header 与 commit
10. FLUSH journal clear
```

第 4 步之前断电，不存在稳定 commit；恢复丢弃 incomplete transaction。
第 6 步之后断电，commit 已声明全部 payload 有效；恢复必须 replay。第 8 步
之后但清理前断电，replay 会把同一字节再次写到同一 home block，结果不变。
这就是幂等性要求。

ordered mode 的核心不是“写调用的先后顺序”，而是两个由 FLUSH 建立的稳定
关系：

```text
related data stable  happens-before  committed metadata reference
prepared log stable  happens-before  commit record stable
```

没有这两个关系，宿主或设备 cache 可以重排写入，让合法 commit 指向尚未稳定
的数据或尚未稳定的 payload。

## mount 恢复

恢复必须发生在读取、信任和校验 superblock 之前，因为需要修复的 home
superblock 本身可能仍是旧值。恢复分类如下：

| header | commit | 处理 |
| --- | --- | --- |
| 全零 | 任意旧保留区为零 | Clean |
| CRC 与字段有效 | 缺失或不匹配 | 清理并记 DiscardedIncomplete |
| CRC 与字段有效 | 完整匹配 | 验证全部 descriptor/payload 后 replay |
| magic 看似存在但字段/CRC 非法 | 任意 | Corrupt，拒绝 mount |

已提交事务只有在以下条件全部满足时才允许 replay：

- sequence 和 entry count 在 header/commit 中一致；
- entry count 为 1..124；
- descriptor count 等于冻结值 4；
- header、descriptor、payload 和 commit 的 CRC32 全部正确；
- target 位于 rootfs 范围内且不落入 journal 自身；
- 同一事务没有重复 target；
- descriptor reserved 字段为零。

验证阶段不得先写 home block。只有全部条目都验证通过后才 checkpoint，防止
“先恢复前半部分，再发现后半部分损坏”。恢复完成后执行 FLUSH、清空 journal
并再次 FLUSH。若 checkpoint/FLUSH 失败，文件系统保持冻结，commit 记录保留，
下一次 mount 可以重新 replay。

## 校验和与威胁边界

CRC32 用于检测随机损坏、截断、错块和故障注入，不是认证码，不能抵抗能主动
构造碰撞的攻击者。项目当前磁盘镜像属于可信本地介质；v1.18 的畸形输入审计
要求即使校验字段恶意构造，也只能拒绝 mount，不能越界、除零或 panic。

sequence 使用 64 位非零单调值。`UINT64_MAX` 不允许开始新事务；系统不会静默
回绕并把远古 commit 误认成当前事务。v2.0 的容量目标不需要 sequence 回绕
协议，未来若支持在线运行到耗尽，应通过格式升级处理。

## 并发和锁边界

当前 rootfs 用单写事务串行化 mutation，因此固定单事务 journal 与现有所有权
一致。`RootJournal` 不认识 VFS、Process、Thread、页表或 ATA 寄存器；它只
依赖 `FileSystemBlockDevice`。rootfs 负责决定哪些块是元数据、何时同步相关
数据、何时冻结文件系统。

本决策不声称 SMP-safe journal，也不引入通用 buffer-head 锁层。v2.0 仍为单
BSP；未来 SMP 必须重新冻结事务锁顺序、每 CPU reservation 与 checkpoint
并发，不能从当前固定数组推断正确性。

## 可观测性

逐块 stage、CRC 字节和 replay payload 不打印日志。Kernel 只输出低频边界：

- mount 后 `ROOTFS_JOURNAL_READY` 与 credit capacity；
- commit、replay、discard、checksum failure 的有界汇总；
- 真正失败时最接近根因的一条错误。

计数使用 64 位饱和/有界语义，由 QEMU 摘要和宿主测试读取。QEMU 日志包含
来宾单调时间；宿主 `[QEMU][T+...]` 仅用于定位停滞，不替代来宾时间。

## 测试与证据

决策由四层证据冻结：

1. 单元测试验证 credit、重复 stage、overlay read、abort、commit、损坏字段
   和 incomplete discard；
2. 集成故障设备在每次 Write/Flush 后断电，共运行 1000 个确定性 crash
   point，恢复结果只能是全部旧块或全部新块；
3. 对同一 committed 镜像连续恢复两次，第二次必须 Clean，证明 replay 幂等；
4. 固定种子随机模型运行 100000 步，检查 active/staged/durable 状态守恒。

既有独立 Python fsck 继续从根目录重算可达 inode/data 和位图。它不复用 Kernel
journal parser，避免生产实现与 oracle 共享同一个错误。三档 QEMU 和跨启动
persistence 使用同一个格式 3 镜像与 production `RootJournal`。

## 保证与非保证

本决策保证：

- metadata 事务在已覆盖断电点后是完整旧状态或完整新状态；
- commit 不会先于相关数据稳定；
- replay 可重复；
- 损坏记录不会导致越界 checkpoint；
- checkpoint 失败不会丢弃唯一恢复证据。

本决策不保证：

- 普通文件数据的旧内容可回滚；
- 多次覆盖写的应用级原子性；
- `fsync`、`fdatasync` 或 `msync` 的 POSIX 契约；
- 数据 journal、快照、在线扩容或热 journal relocation；
- 磁盘保密性、认证或恶意介质防篡改。

例如同一已分配数据块被覆盖后、metadata commit 前断电，文件系统结构仍一致，
但该数据块可能包含新内容。这是 metadata-only ordered mode 的明确边界，不
应在文档中描述成“所有写都可事务回滚”。

## 被拒绝的方案

### 继续只使用 Dirty/Clean superblock

它只能检测“可能中断”，不能提供回放材料，也不能区分多块事务的已完成部分。
拒绝。

### 只写 commit，不执行 FLUSH

QEMU/ATA cache 与真实设备都允许写入完成和稳定落盘不是同一事件。没有 FLUSH
的顺序只是 CPU 提交顺序，不是恢复契约。拒绝。

### 数据与元数据全部 journal

它能提供更强语义，但扩大写放大、日志容量、回收和教学面，不是 v2.0 收敛前
的必要条件。作为 v2.x 范围保留。

### copy-on-write 整棵文件系统

需要新的树、空间回收、root pointer 原子更新和更大盘面迁移，与当前 inode/
bitmap 格式不兼容。它是另一条文件系统设计路线，不作为本阶段补丁。

### 依赖宿主 qcow2 snapshot

snapshot 可以帮助测试，却不能成为来宾文件系统恢复机制。QEMU 只负责模拟
硬件，不能替内核实现事务。拒绝。

## 后果

正面后果：

- rootfs mutation 获得可解释、可故障注入的持久原子边界；
- 格式、恢复、日志与测试都可在宿主直接验证；
- v1.18 可以冻结一套不依赖 Dirty/Clean 猜测的文件系统 ABI。

代价：

- 256 个块永久保留，最大单事务只有 124 个不同 metadata target；
- 每次 mutation 最多需要四次 FLUSH，性能让位于教学可证明性；
- metadata-only 模式必须持续明确数据内容不回滚；
- 格式 2 镜像不会被格式 3 Kernel 静默挂载，必须重新 mkfs/打包。

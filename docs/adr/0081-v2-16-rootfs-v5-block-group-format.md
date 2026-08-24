# ADR 0081：V2.16 冻结 rootfs v5 block-group 盘面基础

- 状态：Accepted
- 日期：2026-08-24
- 影响：rootfs v5 盘面、freestanding 编解码、宿主 mkfs/fsck、后续 journal/extent

## 背景

ADR 0079 已决定 V2 以项目自研 rootfs v5 实现小型 ext4 的核心能力，但不兼容 ext4
二进制盘面。v2.15 又把前台缓存修改收束到每 inode I/O guard。journal、extent 和目录索引
开始前，必须先冻结不会依赖宿主结构体布局的基础盘面，并证明 128 GiB 参考盘与小几何镜像
使用同一套边界、校验和备份规则。

rootfs v4 仍是生产根。v5 若直接接入启动路径，会把尚未实现的 journal、extent、目录项和
迁移问题混入整机回归；因此本阶段只建立独立格式模型和工具，不改变 Kernel mount 选择。

## 决策

### 固定几何

- magic 为 `OSRFV005`，所有整数显式按小端编码；文件系统块固定 4096 字节，设备逻辑 sector
  固定 512 字节；
- 128 GiB 参考盘从 LBA 32768 开始，共 33550336 个文件系统块；每组 32768 块，即 128 MiB，
  共 1024 组；
- group descriptor 和 inode 均为 256 字节；每组 2048 inode，共 2097152 inode，约
  64 KiB 介质容量对应一个 inode；
- inode 1..15 预留，inode 2 是根目录；预留范围为后续 journal、quota 和升级保留稳定编号；
- primary superblock 位于相对块 0，primary descriptor table 位于块 1..64；每组另有 block
  bitmap、inode bitmap 和 128 块 inode table；
- 最后一组只有 28672 块。初始空格式共有 33416241 个空闲块和 2097137 个空闲 inode。

### 稀疏备份与特性协商

superblock 与完整 descriptor table 只复制到组 0、1，以及组号为 3、5、7 的纯幂的组；参考
几何共有 15 份。descriptor 明确记录 copy、两张 bitmap、inode table 和 data 区间，所有范围
必须落在所属组内且由几何唯一推导。

v5 定义 compat、read-only-compat 和 incompat 三组 feature。未知 compat 可忽略；当前只有可写
格式模型，没有只读 mount 降级入口，因此未知 read-only-compat 与未知 incompat 都 fail closed；
缺失 required feature 同样拒绝。后续若加入只读挂载，必须显式扩展 API，不能悄悄放宽现有
验证函数。

### 校验与保留区

superblock、group descriptor、inode 和两张 bitmap 使用 CRC32C Castagnoli。CRC 实现必须通过
`123456789 → 0xE3069283` 标准向量；跨语言固定 profile 的 superblock checksum 为
`0x9B5E7B2E`。未定义字段必须为零，错误 magic、checksum、feature、计数、尾部保留字节、
group 重叠或备份不一致均拒绝。

### 工具与生产边界

Kernel 侧提供 freestanding 的 profile 规划、几何验证以及 superblock/descriptor/inode
编解码，不分配内存、不依赖 libc。宿主侧提供独立 `mkfs-rootfs-v5`、`inspect-rootfs-v5`、
`fsck-rootfs-v5` 和 `corrupt-rootfs-v5`；创建已有路径或覆盖非零文件系统区域必须显式
`--force`。

本阶段 fsck 验证“新格式空镜像”的精确状态：全部备份、bitmap、预留 inode、根 inode、空
inode table 与全局计数。它不是 v2.20 的可达性 fsck，也不修复介质。生产构建、QEMU 和手机
继续使用 rootfs v4；v5 镜像只用于小几何工具测试和离线实验。

## 验收

- C++ 单元测试覆盖参考几何、尾组、稀疏备份、feature 协商、固定小端 checksum 和三类结构
  损坏；
- 固定种子模型执行十万组几何/descriptor 编解码，检查组区间、inode 范围、备份策略和重叠
  拒绝；
- Python 小几何镜像逐字节检查 primary/backup、两张 bitmap、预留/root inode、空 table、
  稀疏宿主占用与 JSON 摘要；
- 具名故障至少覆盖 superblock/descriptor/inode checksum、required feature、两张 bitmap、
  两类 backup、保留字节和 group overlap；每类都必须被只读检查拒绝；
- rootfs v4、ABI v2.6.0、4 GiB RAM、128 GiB 生产盘、ATA/NVMe 和 VGA 协议保持不变，完整
  fresh CAW 回归通过。

## 后果

v2.17 以后可以把 journal、extent 和 allocator 建在稳定的 4 KiB 分组盘面上，Python 工具与
freestanding Kernel 也共享可比较的字段语义。代价是 v4/v5 继续并存，v5 当前只能表示初始空
文件系统；任何把 `OSRFV005` 视为“已经可挂载、可写或已替换生产根”的说法都不成立。

## 关联决策

- [ADR 0079：V2 小型 ext4 与 rootfs v5](0079-v2-mini-ext4-rootfs-v5-program.md)
- [ADR 0080：每 inode I/O 协调](0080-v2-15-per-inode-io-coordination.md)

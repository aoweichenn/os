# ADR 0079：V2 以自研 rootfs v5 收敛小型 ext4 核心

- 状态：Accepted
- 日期：2026-08-24
- 影响：V2 总目标、rootfs 盘面、VFS、页缓存、journal、宿主工具与发布门禁

## 背景

rootfs v4 已覆盖 128 GiB 参考盘，支持五级稀疏指针树、metadata journal、硬链接、
符号链接、open-unlink、orphan 恢复、Unix metadata 与宿主 fsck。V2.7 至 V2.14 又建立
通用块设备、ATA/NVMe、动态文件页缓存、异步 writeback、命名空间缓存、目录句柄和
打开文件描述操作。当前系统已经能可靠运行本地类 Unix 工作负载，但盘面仍有四个
结构性限制：

1. 512 字节文件系统块与五级逐块指针树会放大大文件的映射元数据；
2. 全盘 bitmap 和固定 65536 inode 缺少分组局部性与小文件容量；
3. 定长目录项采用线性冷查找；
4. rootfs v4 单实例修改锁不能成为 extent、延迟分配和并发 journal 的长期基础。

把 Linux ext4 驱动、e2fsprogs 或宿主共享目录接入来宾会替代项目自研文件系统，
违反启动链与教学边界。另一方面，声称实现 ext4 盘面兼容会要求支持大量历史 feature
组合、升级路径和 Linux 互操作，远超 V2 的本地教学目标。

## 决策

### 能力兼容，不做盘面兼容

V2 的文件系统终态定义为项目自研 `rootfs v5`。它采用 ext4 的核心结构和失败语义，
但使用项目 magic、feature 位、显式小端编码和自研工具；Linux 不会把镜像识别为 ext4，
项目也不复制 ext4/JBD2 源码。

rootfs v4 保持只读格式定义和回归夹具。v5 由新 mkfs 创建；迁移工具必须读取 v4、
向另一份 v5 镜像写入并在切换前执行完整 fsck，不提供原地覆盖转换。

### V2 必须交付的核心

- 4 KiB 文件系统块；底层 `BlockDevice` 继续按设备逻辑 sector 执行 I/O；
- 约 128 MiB block group、group descriptor、组内 data/inode bitmap 和 inode table；
- 256 字节 inode，inode 密度由 mkfs profile 决定，phone-primary 默认约 64 KiB/inode；
- superblock、group descriptor、bitmap、inode、extent、目录块和 journal 的 CRC32C；
- extent tree、initialized/unwritten extent、split/merge、空洞与有界树深；
- multi-block allocator、按目录/文件分组的局部性和 delayed allocation；
- 变长目录项与 HTree 风格名称索引；
- ordered metadata journal、descriptor/revoke/commit/checkpoint、orphan file 和幂等 replay；
- 每 inode I/O 协调，覆盖 buffered write、append、truncate、共享 mmap、writeback 和 sync；
- `fallocate`、打洞、`SEEK_DATA/SEEK_HOLE` 与 FIEMAP 类范围查询；
- xattr、POSIX ACL 与用户/组 quota；
- 自研 mkfs、inspect、fsck、corrupt、v4→v5 copy migration 和小几何容量模型。

来宾稀疏文件仍是文件系统语义；手机运行镜像继续按现有规则物化为无宿主空洞的
128 GiB 文件。4 GiB `-mem-prealloc`、ATA/NVMe 双路径和 VGA 可见输出保持参考机门禁。

### 分阶段顺序

| 阶段 | 稳定边界 |
| --- | --- |
| v2.15 | 每 inode I/O 协调与页缓存一致性 |
| v2.16 | rootfs v5 的 4 KiB、block group、descriptor 与 CRC32C |
| v2.17 | journal v2、revoke、checkpoint 与 orphan file |
| v2.18 | extent、分组 allocator、delayed allocation 与范围操作 |
| v2.19 | HTree、可扩展 inode、xattr、ACL 与 quota |
| v2.20 | v4→v5 迁移、完整 fsck、故障/性能/持久化矩阵和生产根切换 |

v5 在 v2.20 前不替换生产根。早期增量使用独立格式模型、小几何镜像和实验后端，
rootfs v4 继续承担整机回归；只有 journal、fsck、迁移和双设备持久化全部通过后才切换。

### 非目标

V2 不实现 ext4 二进制兼容、flex_bg/meta_bg、bigalloc、inline data、fscrypt、fs-verity、
casefold、DAX、在线 resize、MMP、外部 journal、data=journal/writeback 模式、reflink、
快照、压缩、去重或多设备 RAID。`O_DIRECT` 与通用异步文件 API 在 buffered I/O 和
v5 持久化收口后另行决策。

## 验收

- extent、allocator、HTree、journal 与 fsck 各自具有单元、集成和十万步固定种子模型；
- 每种 metadata 结构均覆盖 CRC、越界、重复所有权、环、计数和保留位损坏；
- journal 断电矩阵覆盖 allocation、extent split/merge、rename、truncate、orphan 和 quota；
- 大目录冷 lookup 由索引深度/块读取计数约束，不以 dentry 热命中掩盖线性后端；
- 连续文件优先形成少量 extent，碎片场景仍保持所有权与 ENOSPC 短写语义；
- 4 GiB ATA/NVMe primary、reclaim、OOM、persistence 与故障矩阵全部使用同一 v5 镜像语义；
- 生产镜像物化、QMP screendump、资源守恒、教材、网站和公开发布闭环保持现有门禁。

## 依据

- Linux Kernel ext4 文档：[High Level Design](https://docs.kernel.org/filesystems/ext4/overview.html)
- Linux Kernel ext4 文档：[Block and Inode Allocation Policy](https://docs.kernel.org/filesystems/ext4/allocators.html)
- Linux Kernel ext4 文档：[Dynamic Structures](https://docs.kernel.org/filesystems/ext4/dynamic.html)
- Linux Kernel ext4 文档：[Directory Entries](https://docs.kernel.org/filesystems/ext4/directory.html)
- Linux Kernel ext4 文档：[Journal](https://docs.kernel.org/filesystems/ext4/journal.html)
- Linux Kernel ext4 文档：[Orphan File](https://docs.kernel.org/filesystems/ext4/orphan.html)

这些资料用于确定结构和语义，不授权复制 Linux 源码或 ext4 magic/盘面字段。

## 后果

正面后果是 V2 获得明确终点，后续版本不再零散补系统调用；4 KiB、分组、extent、HTree
和 journal 形成同一套可解释的本地文件系统。代价是 rootfs v5 必须一次性承担新格式、
迁移和长期兼容责任，v4 与 v5 在生产切换前要并行维护回归，且 V2 完成时间以后续六个
阶段的正确性证据为准，不能用“能启动”提前结束。

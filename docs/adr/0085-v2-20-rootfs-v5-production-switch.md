# ADR 0085：v2.20 rootfs v5 生产根切换

- 状态：Accepted
- 日期：2026-08-24
- 影响：生产根、启动镜像、BlockDevice、ATA、journal、fsck、迁移与 V2 退出门禁

## 背景

v2.16 至 v2.19 已分别冻结 block group、journal v2、extent/allocator 和
variable dirent/metadata 模型，但生产 Kernel 与启动镜像仍使用 rootfs v4。仅把 v5 magic
写进镜像、继续在内部承载 v4 payload，或只让 hosted model 通过，都不构成 V2 完成。

## 决策

### 同一个 `RootFileSystem` 挂载两代格式

生产 `RootFileSystem::Initialize` 先识别 `OSRFV005`，v5 路径使用 4 KiB 文件系统块、1024
block group、组内 bitmap/inode table、外部 extent leaf、变长目录块和 journal v2；v4 解码、
指针树和测试夹具保留为兼容回归分支。生产启动镜像只由 v5 mkfs/installer 创建，Kernel 根
vnode 为 v5 inode 2，不在 v5 内嵌套 v4 文件系统。

### 生产事务与栈边界

metadata 进入 journal v2，普通数据先走 ordered data。生产写使用 `CommitAndCheckpoint`：
commit 记录稳定后直接利用事务内存中的 metadata 写 home block；原 `Commit` 仍保留
“已提交、未 checkpoint”窗口供断电恢复测试。目录、extent 和 journal 的 4 KiB/大对象
scratch 全部属于文件系统实例，不进入 16 KiB Kernel stack。

### 4 KiB 块设备请求

rootfs v5 和 journal v2 向 `BlockDevice` 提交一次 4096-byte transfer。NVMe 继续使用原生多块
请求；ATA PIO 的 READ/WRITE SECTORS 上限提升为 8 sector，一个请求按 IRQ 逐 sector 搬运，
只进行一次 BlockIo 挂起/唤醒。同步 fallback 也按最多 8 sector 循环完成。

### 工具与迁移

- mkfs 创建 journal、根目录 extent/dirent 与动态计数一致的 v5 镜像；
- installer 从宿主 ELF 构造 v5 extent/目录树；
- fsck 重算 group 计数、CRC、extent 所有权、目录可达性、link 与 orphan；
- `migrate-rootfs-v5` 先完整检查 v4，再把目录、文件、符号链接、硬链接和 Unix metadata
  复制到另一份新 v5 镜像，拒绝原地覆盖；
- 启动故障盘从已检查的正常 v5 盘按宿主已分配 extent 稀疏克隆，只覆盖启动前缀。

## 后果

正面结果是 production mount、Ring 3 文件工作负载、journal recovery 与宿主 fsck 使用同一
盘面语义。代价是 v5 backend 仍由单实例 `RuntimeMutex` 串行 metadata transaction；HTree、
xattr/ACL/quota 的格式/策略已冻结，但当前 VFS ABI 没有通用 xattr/quota 系统调用。Linux ext4
二进制兼容仍不是目标。

## 验收

- v4 格式/后端回归与 v5 格式、journal、extent、目录、metadata 测试同时通过；
- 4 GiB ATA/NVMe success、reclaim、OOM、persistence 和失败路径使用 v5 启动盘；
- QEMU 必须出现 `ROOTFS_V5_MOUNTED`、可见 VGA、资源守恒和零禁止 marker；
- 完整 fsck、v4→v5 copy migration、真实 128 GiB 镜像物化规则通过；
- Kernel 热路径没有超过 16 KiB stack 的大对象临时量。

## 关联决策

- [ADR 0079：V2 小型 ext4 总体计划](0079-v2-mini-ext4-rootfs-v5-program.md)
- [ADR 0082：journal v2](0082-v2-17-rootfs-v5-journal-v2.md)
- [ADR 0083：extent 与 allocator](0083-v2-18-rootfs-v5-extents-allocation.md)
- [ADR 0084：目录与 inode metadata](0084-v2-19-directory-metadata-policy.md)

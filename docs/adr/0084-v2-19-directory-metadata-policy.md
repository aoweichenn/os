# ADR 0084：V2.19 采用变长目录、HTree 与独立 inode metadata block

- 状态：Accepted
- 日期：2026-08-24
- 影响：rootfs v5 directory、inode extension、xattr、ACL、quota 与 fsck

## 决策

目录 leaf 使用 4 KiB 小端变长记录，记录保存 inode number/generation、64 位 UUID-seeded name
hash、record/name length、类型和名称；record 按 8 字节对齐，尾部 CRC32C。HTree index node
使用 32 字节 hash→child entry。运行时模型最多 512 个名称，8 路、73 node、深度 2；lookup
必须按 tree 路径进入 leaf，hash 相同仍逐字节比较名称。

256 字节 inode 的 128 字节 mapping root 定义项目 extension v1，feature 位分别绑定 extent、
directory index、xattr、ACL 和 quota pointer/generation；未知位以及 feature/pointer 不一致均拒绝。

xattr block 使用变长 name/value record，按 namespace/name 排序，最多 16 项、单值 256 字节；
decode 必须先验证长度再复制。ACL 支持 owner、named user、group owner、named group、mask、other，
named 项必须有 mask。quota block 保存 48 个 user/group record，hard limit 立即拒绝，soft limit 在
grace 到期后拒绝；失败不得改变 usage/limit。

这些格式与算法保持独立模型，v2.20 production backend 才把 inode extension、directory/xattr/
quota block 经 journal 和 allocator 写入真实镜像。

## 验收

- variable record、HTree、xattr、quota 覆盖 CRC、长度、对齐、排序、重复、保留区和容量损坏；
- 512-entry HTree 形成 73 node/深度 2，删除后收缩，lookup node count 受深度约束；
- ACL 覆盖 owner/named/group/mask/other 和显式零权限 named user；
- quota 覆盖 hard/soft/grace、release、失败不变和 user/group identity；
- 固定种子十万步比较 directory/xattr/quota 独立 oracle；
- fresh CAW 全构建与既有整机回归通过。

## 依据

- Linux Kernel ext4 文档：[Directory Entries](https://docs.kernel.org/filesystems/ext4/directory.html)
- Linux Kernel ext4 文档：[Extended Attributes](https://docs.kernel.org/filesystems/ext4/attributes.html)
- Linux Kernel ext4 文档：[Inodes](https://docs.kernel.org/filesystems/ext4/inodes.html)

## 关联决策

- [ADR 0079：V2 小型 ext4 与 rootfs v5](0079-v2-mini-ext4-rootfs-v5-program.md)
- [ADR 0083：V2.18 extent 与 allocation](0083-v2-18-rootfs-v5-extents-allocation.md)

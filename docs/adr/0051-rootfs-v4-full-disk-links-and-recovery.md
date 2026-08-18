# ADR 0051：rootfs v4 完整参考盘、链接与 orphan 恢复

- 状态：Accepted（v2.3 本地候选）
- 日期：2026-08-18
- 影响：rootfs 盘面、VFS 路径、journal、ABI、宿主工具、测试与启动日志

## 背景

v2.1 已把参考盘固定为 128 GiB raw 稀疏镜像，末扇区正好是 ATA LBA28 上限
`0x0FFFFFFF`，但格式 3 rootfs 仍只使用从 LBA 32768 开始的 256 MiB。格式 3
还有四个限制阻止它成为后续权限和内存压力阶段的稳定基础：

1. inode、data bitmap 和 journal 都是针对 256 MiB 写死的规模；
2. 单文件上限 64 MiB，只有单/双/三级间接根；
3. link count 固定为 1，打开文件不能 unlink，也没有 orphan 恢复；
4. inode 没有时间戳，ABI `FileInformation` 不能表达持久文件时间。

简单把 `TOTAL_BLOCK_COUNT` 改大不可接受。128 GiB 数据区的完整 bitmap 约
32 MiB；若继续把验证 bitmap 嵌进 `RootFileSystem`，64 MiB 兼容机仅挂载就
消耗一半内存并扩大 Kernel 装载/BSS。若只扩宿主镜像、不真实访问末 LBA，
又会把宽字段误报为可用容量。

## 决策

### 一次性升级盘面

生产格式升级为 magic `OSRFV004`，不兼容格式 3。启动盘不引入分区表或外部
bootloader；rootfs 仍由项目代码从固定 LBA 32768 挂载。

```text
disk blocks                 268435456
rootfs start LBA                 32768
rootfs blocks               268402688
rootfs bytes             137422176256
last absolute LBA           0x0FFFFFFF
```

布局为：superblock 1 块、journal 4096 块、inode bitmap 16 块、inode table
32768 块、data bitmap 65504 块、data/pointer area 268300303 块。inode 数为
65536。superblock 显式保存所有区域起点/长度，解码器接受满足相同结构公式且
不超过生产上限的较小几何，使容量测试可以真实到达 ENOSPC。

Kernel mount 只读出固定位置 superblock 以取得并验证几何，再用该几何约束
journal recovery，恢复后重新读取 superblock。所有相对块进入 cache/device
前再次与已挂载几何比较。

### 五级稀疏块树

inode 保持 256 字节，保留八个直接块并加入 quadruple/quintuple 根。指针块
继续使用 63 个 64 位相对块号与 CRC32。生产单文件逻辑上限等于数据区字节数
137369755136；五级树覆盖能力大于该上限，实际分配仍受 data bitmap 限制。

truncate 扩展保持稀疏，写一个高 offset 只分配通往目标的树路径。范围释放按
子树跨度剪枝，从叶到根释放空指针块。格式、Kernel 与 Python fsck 都拒绝
文件末尾之后的非零指针、越界块和计数不一致。

### journal 扩容但保持 ordered metadata 模型

journal 起点移到 superblock 后，区域扩大到 4096 块；descriptor 从四块增为
八块，每块 31 项，因此单事务最多 248 个不同 metadata target。header、
descriptor、payload 与 commit 的 CRC、FLUSH 顺序和幂等 checkpoint 沿用
ADR 0044。

普通文件数据仍不进入 journal。承诺仍是 metadata 的全旧/全新和“数据稳定
happens-before metadata reference”，不是覆盖数据的应用级回滚。

### 链接、符号链接和路径 scratch

普通文件与符号链接允许多个目录项引用同一 inode，盘面 link count 必须等于
目录引用数。目录 link count 固定为 1，禁止硬链接，避免目录图形成额外环。

符号链接目标保存为 inode 的普通数据，长度 1..4096。VFS 跟随绝对与相对目标，
最多 40 跳。为避免在 16 KiB Kernel 栈上叠加多个 4096 字节路径缓冲，VFS
拥有两块常驻 scratch 并用独立 resolution lock 串行化重写；后端锁不包围
VFS 全局统计锁。

最终组件是否跟随由操作决定：普通 Resolve/Open/Stat 跟随，ReadSymbolicLink
与 unlink 不跟随。超长展开、控制字符目标、无 read-link 后端和环路都返回
明确状态。

### 打开后删除与 orphan

删除最后一个名称且 open count 非零时，事务先删除目录项，再把 inode 写为
`link_count=0, ORPHAN=1`，不释放数据。旧 OpenFile 仍凭 inode number 与
generation 读写。最后 close 在新事务中截断并释放 inode。

若在两者之间断电，新实例没有内存 open count。mount 在发布 root vnode 前
扫描已分配 inode，验证 orphan 的 link count 为零并逐个事务回收。普通不可达
inode、带 link 的 orphan、orphan 目录或损坏块树一律拒绝。rename replace
对打开普通文件使用相同路径；打开目录仍拒绝。

### 时间戳与 ABI

inode 保存 atime、mtime、ctime、btime 四个 64 位纳秒值。生产时间源在首次
使用时读取稳定 CMOS UTC，转为 Unix 纳秒基准，再叠加 Kernel 单调纳秒；溢出
饱和。宿主安装器使用源文件 mtime。普通 read 使用 noatime，避免只读热路径
产生 metadata journal 事务。

ABI 从 v2.1.0 升为 v2.2.0；系统调用编号和错误区间不变，`FileInformation`
从 64 字节扩为 96 字节并追加四个时间戳。旧用户 ELF 必须按当前统一 ABI
重新构建，不能把结构扩展伪装成二进制兼容的同一 minor。

### Kernel 有界验证与宿主完整 fsck

Kernel 保留最大 65536 inode 的可达 bitmap、link count 表、open count 表和
BFS 队列，但不常驻完整 data bitmap。superblock 保存 inode/data/metadata
分配摘要并随每个事务更新；Kernel 逐引用查询实际分配位、校验 inode 内部计数
并与摘要守恒，不在每次 mount 通过 ATA 顺序读取 65504 个 bitmap 块。正常写
路径与 journal 保证项目自身不会创建重复所有权。

独立 Python fsck 加载宿主侧 bitmap 并建立完整数据所有权集合，精确拒绝重复
引用、泄漏、link count、orphan、CRC、尾部保留位和高 LBA 错误。Kernel 校验
不是 fsck 的替代，fsck 也不是 mount recovery 的替代。

## 失败与恢复边界

- 格式 3、未知 feature、错误几何、CRC 或非零保留区：拒绝 mount；
- journal committed：完整验证后 replay；incomplete：丢弃；损坏：拒绝；
- unlink 在 commit 前失败：目录名和 inode 保持旧状态；commit 后断电：mount
  观察 orphan 并回收；
- last close 失败：OpenFile 保持可重试；设备进入 failed 时不伪报已释放；
- link/symlink 创建在发布目录项前预留 inode、目录块和目标数据路径容量；失败
  abort overlay，不留下半个名称；
- 128 GiB 镜像复制只复制非洞 extent；不支持 `SEEK_DATA/SEEK_HOLE` 的宿主
  不允许退化为扫描超大镜像。

## 验证

- 格式 round-trip/损坏单元测试覆盖 23 字段 superblock、五级 inode 与时间戳；
- rootfs 集成覆盖硬链接、符号链接、open-unlink、last close、断电后 mount
  orphan 回收、重挂载、只读和设备失败；
- Kernel 与 Python 各自从 LBA `0x0FFFFFFF` 读回普通文件；
- 小几何 capacity 真实填满后验证短写、零字节 ENOSPC 与 Validate；
- journal 1000 点矩阵覆盖 1..248 target，累计 374620 项断言；
- 128 GiB mkfs/install/fsck/copy/corrupt 保持稀疏并核对逻辑长度。

## 被拒绝的方案

### 只放大固定常量并保留 32 MiB Kernel 验证 bitmap

会破坏 64 MiB 兼容档和 Kernel 有界内存目标。拒绝。

### 引入 GPT/ext4 或宿主文件共享

这会让外部格式/运行时替代项目自研文件系统，违反启动链与学习目标。拒绝。

### 继续限制单文件 64 MiB

无法证明 64 位 offset 和高层块树，也使完整参考盘只能由大量小文件使用。拒绝。

### unlink 打开文件继续返回 Busy

无法形成现代本地类 Unix 生命周期，也会把删除语义拖到权限阶段。拒绝。

### 将全部文件数据写入 journal

能提供更强回滚，但会显著扩大写放大和日志回收面；v2.3 目标是可靠 metadata
与 ordered data，不是数据日志文件系统。拒绝。

## 后果

正面后果是 128 GiB 参考盘从“逻辑存在”变为真实可分配，链接、时间戳和
orphan 为 v2.4 权限模型提供稳定 inode 语义。代价是 Kernel BSS 增加有界
inode/link/open 表，mount 多一次恢复统计扫描，格式 3 镜像必须重建，且完整
重复所有权证明仍依赖发布时宿主 fsck。

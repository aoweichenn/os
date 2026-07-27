# ADR 0033：以版本化 rootfs v2、严格挂载和完整命名空间替换生产 legacy 根

- 状态：已接受
- 日期：2026-07-26

## 背景

v1.5 已把路径语义隔离到 VFS，并用 memfs 与 legacy-fs 两个后端证明
`Vnode`、`Path`、`Mount`、`FsContext` 和 `OpenFile` 不是某种磁盘 inode
接口的改名包装。但旧格式仍有根本容量和语义限制：

- 启动盘只有 2 MiB，文件系统区域只有 512 KiB；
- inode 只有直接块，单文件上限 5120 字节；
- 名称上限 40 字节；
- 没有 unlink、rename、非零 truncate 或 stat；
- normal Kernel 兼具 mount 与 format 职责；
- Python 工具只能检查镜像容器，不能独立重算文件系统可达性；
- 后续从磁盘 exec 至少需要可容纳多个 ELF 和参数数据的根文件系统。

直接在旧格式上继续加字段会让已有磁盘被相同 magic 错误解释，也会把路径、
容量、失败恢复和工具链变化混在一个隐式升级中。因此必须创建新的盘面版本，
明确迁移边界，并继续保留旧实现作为回归输入。

## 决策

### 生产根采用独立 `RootFileSystem`

v1.6 新增 `RootFileSystem`，通过既有 `BackendOperations` 接入 VFS。
正常 Kernel 只严格初始化它；`LegacyFileSystem` 和旧 `FileSystem` 继续
编译、测试，但不再挂到生产根。

该决策使三类职责保持分离：

| 层 | 职责 |
| --- | --- |
| VFS | 路径、mount、cwd、跨后端规则、打开实例 |
| RootFileSystem | inode、目录、块树、bitmap、事务和校验 |
| ATA/BlockCache | 512 字节块 I/O、缓存和 flush |

### 固定 1 GiB 稀疏盘与 256 MiB rootfs

启动盘逻辑长度固定为 1 GiB；低地址继续容纳 ROM/Stage 1/Kernel 布局，
v1.6 首次实现时 rootfs 从 LBA 2048 开始占用 256 MiB；v1.9 因启动载荷
增长迁移到 LBA 32768，盘内相对布局不变，并由启动布局契约统一。宿主文件
必须是 sparse：容量规格是
来宾可见地址范围，不应强迫每个构建产物实际占用 1 GiB。

rootfs 固定布局为一块 superblock、两块 inode bitmap、4096 块 inode
table、128 块 data bitmap 和其余 data 区。8192 个 inode 足以支持 v2.0
教学工作负载，同时让内核可用固定上限队列和验证 bitmap，不引入启动期动态
分配依赖。

当前不支持在线扩容。格式中的完整区域位置和数量仍逐项记录并验证，使工具
可以解释盘面，也为未来新版本提供显式迁移输入；Kernel 不接受任意自描述
布局，避免恶意字段把元数据区域指向启动链或盘外。

### 所有盘面字段显式编码

superblock magic 固定为 `OSRFV002`，format version 为 2。所有整数采用
little-endian、明确偏移和明确宽度，编码器先清零完整对象再写字段与 CRC。
不得把 C++ struct 直接 reinterpret 到磁盘，因为 padding、enum 宽度和宿主
字节序都不属于文件系统 ABI。

superblock、inode、目录项和 pointer block 均有 CRC32；每个未使用保留字节
必须为零。CRC 用于检测意外损坏，不提供抗恶意篡改的密码学认证。

### inode 使用 direct + single/double/triple indirect

项目选择 Unix 传统 inode 块树，而不是 extent tree：

- direct 块让小文件无需额外元数据 I/O；
- single/double/triple 逐级扩大容量；
- 每一级都能画出明确索引算式和所有权树，适合教学；
- truncate 可以从叶到根释放空 pointer block；
- fsck 可以独立遍历并检测重复块。

每个 inode 有八个 direct 指针；每个 pointer block 有 63 个 64 位相对块号。
公开文件上限冻结为 64 MiB，小于理论覆盖量。选择更大 64 位块号是为了避免
盘面 ABI 被当前 256 MiB 实例绑死，但每个块号仍必须落在本实例 data 区。

本阶段不选择 extent，是为了先建立清楚的多级寻址、空洞和递归验证基础。
未来若引入 extent，必须使用新 feature/version 并提供迁移与随机模型，不能
把现有 pointer block 静默改义。

### 稀疏文件是盘面语义

零数据指针表示未分配空洞。读空洞返回零；truncate 扩展不分配；越过文件尾
写入只分配触及区域。inode 分别记录逻辑 size、allocated data blocks 和
allocated metadata blocks。

这一选择避免 64 MiB truncate 扩展立即耗尽 256 MiB rootfs，也让后续 ELF
BSS、稀疏测试文件和容量统计具有真实语义。空洞读生成零，不能把旧磁盘残留
字节暴露给用户。

### ENOSPC 允许明确短写

普通 write 可以跨越多个逻辑块，而为后续块分配 pointer path 时可能耗尽
空间。本阶段采用与字节流 I/O 相符的前缀语义：

1. 每个逻辑块前先计算完整路径需要的块数；
2. 若足够则完成该块范围；
3. 若不足且已有前缀，提交前缀并返回实际写入数；
4. 若没有前缀，返回 `CapacityExhausted`；
5. 调用方只推进实际返回的 offset。

不采用“预留整个请求”是因为一个请求可达 256 字节 ABI 上限，后续接口可能
更大；允许短写能复用通用 I/O 循环。无论哪条结果，Validate/fsck 都必须
观察到一个完整已提交状态。

### 删除与打开引用采用 Busy 边界

Unix 允许 unlink 后打开文件继续存在，直到最后一个引用关闭。这要求 inode
进入“link count 为零但仍被打开”的 orphan 状态，并定义掉电后如何回收。
v1.6 尚无 journal/orphan list，因此不伪造这套语义。

RootFileSystem 为每个 inode 维护运行期打开计数。unlink、rmdir 或 rename
替换若会释放一个打开对象，返回 `Busy`。VFS 对 memfs 使用相同可观察规则，
使差分随机测试不因后端生命周期不同而分叉。未来实现 orphan inode 时需要
新的 ADR、盘面状态和崩溃测试矩阵。

### rename 在一个后端事务内完成

VFS 先解析 source/destination 父目录，拒绝 mount point、根和跨 Superblock。
后端在同一锁和同一磁盘事务内：

1. 校验 source 与可选 destination；
2. 校验类型、目标空目录、打开引用和祖先环；
3. 确认目标目录槽与需要的空间；
4. 发布目标项并清除源项；
5. 必要时释放被替换对象；
6. 移动目录时更新 parent inode；
7. trim 目录尾部并提交。

同名同对象是成功 no-op。跨设备返回 `CrossDevice`，不降级成 copy+unlink，
因为后者不具备原子 rename 语义。

### Dirty/Clean 只负责检测，不宣称恢复

事务顺序固定为 Dirty 持久化、数据/元数据 flush、Clean 持久化。设备错误
后实例永久 failed。下次挂载看到 Dirty 就返回 `IncompleteTransaction`。

该协议的承诺是：

- 已成功提交的事务以 Clean 结束；
- 未完成事务不会被当作一致文件系统继续使用；
- Kernel 不自动格式化或猜测修复；
- 独立 fsck 可以报告第一个不变量错误。

它不承诺掉电后选择旧状态或新状态，因此不是 journal。v2 路线中的 ordered
metadata journal 会增加预留 credit、日志 commit、replay 和每个断电点测试；
这些机制不能偷偷塞进本阶段。

### mkfs、inspector 与 fsck 必须独立于 Kernel

宿主 Python 工具逐字段实现相同冻结格式，但不调用 C++ RootFileSystem。
mkfs 是唯一正常格式化入口；inspect 只读取摘要；fsck 只读重建可达集合和
bitmap；corrupt 产生具名损坏。

“独立”意味着 Kernel 与工具可以互相发现编码/遍历错误。二者共享规范常量
值，但不共享同一个解析函数或状态机。

### 稀疏镜像操作必须保留逻辑尾部

所有镜像派生、QEMU 持久化临时副本和故障变体使用稀疏范围复制。实现必须：

- 先建立精确逻辑长度；
- 只复制非零范围；
- 正确处理 partial `pwrite`；
- 保留靠近 1 GiB 尾部的非零 extent；
- 允许不支持 hole 查询的宿主回退到有界块扫描。

审计镜像时只读取需要的前缀或具名 rootfs 区域，不能为检查一个描述符把整
1 GiB 文件读入内存。

## 被拒绝的方案

### 继续扩展 legacy 格式

会让相同 magic 同时表示不同 inode/目录布局，旧磁盘无法可靠区分，也无法
在保持 2 MiB 镜像的同时达到 256 MiB rootfs 和 64 MiB 文件规格。

### 使用 FAT、ext2 或现成用户态库

这会跳过本项目要学习的盘面编码、块分配、目录/inode、校验、故障和 fsck。
QEMU 只模拟硬件，文件系统必须由项目自行实现。

### 用 host directory、9p 或 virtiofs 作为根

这会把命名、持久性和部分缓存语义交给宿主，不符合“只让 QEMU 模拟硬件”的
边界，也不能证明 ATA PIO 到自研 rootfs 的整条链。

### 由 Kernel 自动格式化全零盘

生产 mount 与 destructive format 无法区分时，布局错误或镜像打包错误可能
清空证据。构建阶段显式 mkfs、Kernel 严格 mount 的职责更清楚。

### 直接实现 journal

没有先稳定块所有权、namespace mutation 和独立 fsck 时，journal 会同时
引入 credit、日志空间、replay 与幂等性，故障定位维度过多。v1.6 先冻结
持久对象和检测边界。

### 用 `std::filesystem` 或宿主数据库做 mkfs/fsck

宿主库不能表示项目的冻结盘面，也会让工具依赖平台行为。工具只使用
Python 3.11+ 标准库和显式二进制编码。

## 后果

### 正面

- 生产根容量达到 v2 路线需要的 256 MiB/64 MiB 规格；
- VFS 后端边界经第三个实现得到验证；
- namespace mutation 不依赖 Shell 或具体 inode 布局；
- Kernel 不再拥有隐式破坏性格式化能力；
- 稀疏、短写、ENOSPC 和损坏都具有明确可测试语义；
- 独立 fsck 形成目标实现之外的第二个一致性 oracle；
- legacy 格式仍可回归，不会被新布局静默解释。

### 代价

- 每次元数据修改至少需要 Dirty 与 Clean 两次强制 flush，性能不是最终形态；
- 全局锁使并发 rootfs 操作串行化；
- CRC 和完整 Validate 增加启动/测试成本；
- 打开文件不能 unlink，与完整 Unix 语义不同；
- Dirty 介质无法自动恢复，需要重建镜像或未来 journal；
- 固定布局暂时不能在线扩容。

## 验证

必须同时满足：

1. 盘面编码单元测试拒绝错误 magic/version/layout/CRC/reserved/type；
2. 集成测试跨 direct/single/double/triple 边界读写并在重挂载后一致；
3. 64 MiB 文件 truncate 释放全部树；
4. 设备写失败留下 Dirty，新的实例拒绝挂载；
5. 真实 256 MiB 近满格式产生短写与零字节 ENOSPC；
6. unlink/rmdir/rename/truncate/stat 的正常、边界和失败语义通过；
7. memfs 与 rootfs 对同种子 100000 步命名空间模型结果一致；
8. QEMU 同盘两次启动恢复旧载荷，宿主只读 fsck 成功；
9. 受保护元数据损坏后第三次启动拒绝，不进入 Ring 3、不格式化；
10. 稀疏镜像逻辑长度、尾部 extent 与宿主实际占用受测试约束。

## 关联

- [v1.6 发布记录](../releases/v1.6.md)
- [文件系统模块](../modules/file-system.md)
- [v1.6 路线](../roadmap.md#v16-rootfs-v2-与完整命名空间)
- [ADR 0032](0032-vfs-mount-namespace-and-memfs.md)

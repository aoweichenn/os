# File System 模块

## 职责

File System 模块分为两个层次：VFS 负责路径、挂载、vnode、每 Process
root/cwd、命名空间修改和打开实例；后端负责具体节点与存储。当前生产根目录
由 rootfs v4 把参考盘在 LBA 32768 之后的全部 512 字节 ATA 扇区组织为带校验的
64 位 superblock、可扩展 bitmap/inode/journal、稀疏文件和五级间接树；memfs
从 KernelHeap 动态拥有目录、文件与数据。legacy 后端只保留旧格式兼容与回归，
不再作为生产根。

依赖方向：

```text
用户包装器
  → 系统调用与每进程 FileTable
    → 共享 FileDescription
      → Vfs / OpenFile / FsContext
        → BackendOperations
          ├→ Memfs → KernelHeap
          ├→ RootFileSystem → BlockCache → FileSystemBlockDevice → AtaPioDevice
          └→ LegacyFileSystem → FileSystem（仅兼容回归）
```

宿主测试以 `MemoryBlockDevice` 替换最底层设备，但复用完整
`BlockCache`、格式解析、路径和文件操作实现。

## v0.11 legacy 固定布局（历史格式）

旧 2 MiB 磁盘的前 1 MiB 保留给启动链。文件系统起始 LBA 为 2048，总块数为
1024，块大小为 512 字节；最后 1024 个扇区暂留扩展。逻辑布局由编译期常量
固定，并同时接受宿主布局测试和目标启动检查。

superblock 保存：

- 8 字节 magic 与 64 位版本；
- 块大小、总块数和各元数据区域；
- inode 数量、数据块数量和根 inode；
- `Clean/Dirty` 状态与 64 位事务代次；
- 对前 508 字节计算的 CRC32。

inode 0 永不分配；inode 1 是根目录。inode 使用十个 64 位直接块地址，
因此普通文件与目录最大均为 5120 字节。

## legacy 核心不变量

- 从根 inode 1 的有界广度优先遍历必须恰好到达 inode bitmap 的所有已分配
  inode；拒绝孤儿、环和重复目录引用；
- data bitmap 中的已分配位必须与全部可达 inode 拥有的数据块一致；
- inode 类型只能是目录或普通文件；
- `size_bytes <= allocated_block_count × 512`；
- 目录大小必须是 64 字节目录项的整数倍；
- 目录项名称长度为 `1..40`，未使用尾部字节必须为零；名称拒绝斜杠、C0
  控制字符、`DEL`、`.` 和 `..`；
- 目录项引用的 inode 必须已分配且类型一致；
- 一个 inode 的直接块不得重复，也不得超出数据区；
- clean superblock、每个已分配 inode 都必须通过 CRC32；
- Dirty superblock 只能报告未完成事务，不能继续挂载。

当前格式没有硬链接，因此根以外 inode 的链接数必须为 1，且同一 inode
不能由两个目录项引用。这个约束以后若引入硬链接，必须通过新格式版本和新
一致性规则显式升级，不能静默放宽。

## legacy 缓存

八项 LRU 缓存使用单调访问代次选择最久未使用项。读取命中不访问设备；未命中
会淘汰旧项，若旧项为脏则先写回。`Sync()` 写回全部脏项并调用设备 flush。

缓存对象在初始化时接收块设备引用，不拥有设备生命周期，也不使用动态分配。

## legacy 路径与文件句柄

路径解析只接受绝对路径。`/` 直接解析到根 inode；其他路径逐个在父目录中
查找 64 字节目录项。

文件句柄保存 inode、当前位置、读写能力和打开状态。Read/Write 使用当前
位置并在成功后推进。截断把文件长度归零并释放全部直接块；Close 不删除 inode。

## legacy 修改提交

CreateDirectory、Open(Create/Truncate) 和 Write 都进入显式事务。superblock
先持久化为 Dirty；缓存中的数据和元数据全部写回后，superblock 才重新标记
Clean。任何设备错误都会使挂载状态转为失败，调用方不得继续假设磁盘一致。

## v1.4 FileDescription 适配边界

`FileSystemHandle` 不再位于按 fd 编号排列的 Process 平行数组，而是成为
RegularFile/Directory `FileDescription` 的共享 payload。它仍由本模块定义，
并继续保存 inode、offset、读写能力和 open 状态；对象层只负责引用生命周期，
不解释磁盘格式。

每次 `Open` 成功后，进程运行时先创建 FileDescription，再事务安装到动态
FileTable。若对象创建或 fd 安装失败，RAII 最后引用会调用
`FileSystem::Close`，不能泄漏已打开 handle。duplicate 不调用本模块的 Open，
只增加同一 FileDescription 的强引用，所以 offset 共享；独立 Open 才取得
新的 handle 和 offset。最后一个引用消失时 finalizer 恰调用一次 Close。

这段描述保留为 v1.4 历史边界。v1.5 已在不修改 FileTable 所有权语义的
前提下完成迁移，当前生产 `FileDescription` 不再保存 legacy handle。

## v1.5 VFS 与双后端

### 核心对象

| 对象 | 当前职责 |
| --- | --- |
| `Vnode` | Superblock 内对象的 identifier、generation 与类型 |
| `Path` | mount identifier 与 vnode 的组合 |
| `Superblock` | 根 vnode、后端操作、名称上限、只读与后端上下文 |
| `Mount` | 父挂载、父文件系统挂载点与子 Superblock |
| `FsContext` | 每 Process 的 root 和 cwd |
| `OpenFile` | Path、共享 offset、读写模式和 open 状态 |

`FileDescription` 保存 `Vfs*` 与 `OpenFile`。duplicate 共享整个 OpenFile，
因此共享 offset；独立 open 创建新的 FileDescription 与 OpenFile。最后一个
引用只调用一次 `Vfs::Close`。

### 路径契约

- 完整路径最多 4096 字节，组件最多 255 字节；
- 绝对路径从 Process root 开始，相对路径从 cwd 开始；
- 重复 `/` 折叠，`.` 保持当前对象，`..` 处理 root clamp 和 mount 退出；
- 普通组件只由所属后端执行单名称 lookup；
- 每次 lookup 后跟随子 Mount；
- 尾部 `/` 要求最终对象为目录；
- getcwd 从 vnode 父链反向重建，挂载根使用父文件系统中的挂载点名称；
- 解析期间不申请内存；V2.11 已接入固定 hash 的正/负 dentry 与 inode metadata cache。

当前 Mount 数组容量为 64，只在用户调度前发布；没有运行期 unmount。

### memfs

memfs 节点使用 64 位单调 identifier，保存父 identifier、类型、名称和可选
文件数据。节点与数据均从 KernelHeap 申请；数据采用有界二倍扩容，空洞和
truncate 扩展区域清零。目录枚举按 identifier 提供稳定游标。

`Validate` 会重新遍历节点链，核对：

- 根节点唯一且父指向自身；
- 每个非根节点具有有效目录父节点；
- 同一父目录下名称唯一；
- 祖先链最终到根且无环；
- 目录不持有文件数据；
- file size、capacity、数据地址和最大文件限制一致；
- 节点/目录/文件/数据容量统计与实际对象一致；
- KernelHeap requested/consumed/allocation 所有权统计一致。

内核把该后端挂载在 `/tmp`；宿主测试还会 Destroy 实例并要求 heap 活动
分配归零。

### legacy 适配器

legacy 适配器把旧 inode number 映射为 vnode identifier，并实现 lookup、
基础 create/mkdir、parent、read/write、零长度 truncate、readdir、
get-name、sync 和 validate。它直接复用旧格式的位图、事务、缓存和 ATA
逻辑，不复制第二套磁盘读写。

旧格式的名称上限仍为 40 字节、单文件上限仍为 5120 字节；这些限制通过
Superblock/后端状态返回，不缩小 VFS 和 memfs 的公共规格。全零新介质可以
格式化，非零未知或损坏介质继续拒绝。

### 锁和失败边界

VFS 统计锁不包围后端操作。运行期 mount 拓扑只读；memfs 使用自己的
SpinLock，legacy 后端复用 FileSystem 锁。FileDescription operation lock
串行化共享偏移，并在进入 VFS 前已经释放 FileTable 与 KernelObjectManager
全局锁。

create、truncate 和数据增长都先验证参数与容量，再由后端提交。memfs 增长
先准备新缓冲再发布；legacy 继续使用 Dirty/Clean 磁盘事务。失败不得发布
半个 vnode、推进 offset 或静默格式化磁盘。

## v1.6 rootfs v2

### 区域与盘面格式

v2.1 参考启动盘逻辑长度为 137438953472 字节，并以稀疏宿主文件构造。
rootfs v2 仍从 LBA 32768 开始，固定占用 524288 个 512 字节块：

| 相对块范围 | 块数 | 内容 |
| --- | ---: | --- |
| `0` | 1 | superblock |
| `1..2` | 2 | 8192 位 inode bitmap |
| `3..4098` | 4096 | 8192 个 256 字节 inode |
| `4099..4226` | 128 | data bitmap |
| `4227..4482` | 256 | ordered metadata journal |
| `4483..524287` | 519805 | 数据块与间接指针块 |

superblock magic 为 `OSRFV003`。全部多字节字段使用显式 little-endian
编码，不把 C++ struct 直接写盘；前 508 字节由 CRC32 保护，未知版本、
未知 required feature、非零保留字节或布局偏移不匹配都会拒绝。superblock
保存事务状态/代次、下一个 inode generation、完整布局和功能位。

inode 0 永不分配，inode 1 是根。每个 inode 保存类型、flags、逻辑大小、
generation、link count、已分配数据/元数据块数、父 inode、八个直接块和
单/双/三级间接根，最后带 CRC32。generation 进入目录项和 vnode，防止释放
后复用 inode number 时旧身份被误认。

目录项固定 320 字节；名称最长 255 字节，另有 256 字节存储槽。空槽必须
全零；活动项保存 inode number、generation、类型和精确名称长度。目录文件
本身也通过普通文件块树存储，所以大型目录不需要另一套分配器。

间接块固定 512 字节，保存 63 个 64 位相对块号、保留字段和 CRC32。相对块
号 0 表示“没有分配”，不能指向元数据区、区域外、已经被别处拥有的块。

### 块映射

设每块大小 `B=512`，每个间接块指针数 `P=63`，直接项数 `D=8`。逻辑块
映射区间为：

| 区间 | 逻辑块数 | 索引方法 |
| --- | ---: | --- |
| direct | `D` | inode 直接下标 |
| single | `P` | 一个指针块下标 |
| double | `P²` | 商/余数两级下标 |
| triple | `P³` | 两次商/余数三级下标 |

对外最大文件固定为 67108864 字节。这个限制小于树的理论覆盖量，避免把盘面
寻址余量误当作稳定 ABI。所有乘法、加法、offset+length 和块号换算先检查
64 位溢出与上限。

空洞由零叶指针表示。读取空洞直接填零；写入只分配实际触及的数据块及通往它
所必需的间接块。文件逻辑大小与已分配大小分开统计，`stat` 因而可以观察
稀疏程度。

### 创建、删除与重命名

create/mkdir 先验证父目录、名称、重复项、inode 位和至少一个目录数据槽所需
空间，再分配 inode 并发布目录项。失败时不会留下 bitmap 位或不可达 inode。

unlink/rmdir 的共同入口先由 VFS 拒绝根与挂载点，再由后端验证：

- unlink 目标必须是普通文件；
- rmdir 目标必须是空目录；
- 当前打开引用必须为零；
- 目录项 generation/type 必须与 inode 一致；
- 释放数据树、inode 与目录槽后再次保持可达性和 bitmap 等价。

rename 支持同目录、跨目录与显式替换，但 source/destination 必须属于同一
Superblock；跨挂载返回 `CrossDevice`。替换目标必须是同类型，目录目标还
必须为空且没有打开引用。移动目录前沿 destination 父链向根走，若遇到
source 则返回 `LoopDetected`。目录移动成功后同时更新 inode 的 parent；
VFS 的 cwd 保存 vnode 身份而非路径字符串，因此随后 `getcwd` 会重建新路径。

本阶段不支持“名字删除但打开文件继续存活”的 Unix orphan inode 语义。
为了不伪造延迟回收，打开引用非零时 unlink 或 replace 明确返回 `Busy`。

### truncate、短写与 ENOSPC

truncate 扩展只增加逻辑大小，不分配空洞块；缩小依次：

1. 若新文件尾落在已分配块中，清零尾后字节；
2. 释放完全位于新尾之后的数据块；
3. 自底向上释放已经全零的单/双/三级间接块；
4. 更新 inode 的逻辑大小与两类 allocated count。

范围释放根据每棵子树覆盖的逻辑块区间剪枝，未分配分支以常数时间跳过。

写入每个逻辑块前计算新叶和路径所需的总块数。磁盘满时，已经完成的连续
前缀会以短写成功返回并更新 offset/size；若尚未写入任何字节，则返回
`CapacityExhausted`。不会出现“返回失败但磁盘偷偷多了一段数据”，也不会
让 inode 计数与 bitmap 分叉。

### v1.17 ordered metadata journal

rootfs 使用固定容量 `BlockCache`。分配器保存 inode/data 搜索 hint，使
顺序构造大文件不必从 bitmap 起点反复扫描；hint 只影响性能，位图仍是唯一
分配事实。superblock 写入也经过缓存的具名块并立即 sync，避免每个事务把
所有热缓存项无条件失效。

每个事务在修改前预留最多 124 个 metadata credits；相同 home block 多次
修改只消耗一个 credit。inode、位图、间接指针块、目录内容和 superblock
进入 redo journal，普通文件数据不进入。修改顺序固定为：

```text
reserve credits
  -> 写相关普通文件数据
  -> data cache Sync + ATA FLUSH CACHE
  -> journal header + descriptor + payload; FLUSH
  -> journal commit; FLUSH
  -> checkpoint home blocks; FLUSH
  -> 清除 header/commit; FLUSH
```

descriptor 保存目标相对块和 payload CRC；每个 descriptor block、header
和 commit 又有独立 CRC。commit 同时绑定 sequence、entry count 与 header
checksum。挂载在读取 superblock 前检查 journal：没有有效 commit 的准备态
被丢弃；有效 commit 按目标块幂等重放并再次 FLUSH；commit 有效而 descriptor、
payload、目标范围或 CRC 无效时拒绝挂载，绝不越界猜测。

checkpoint 失败后实例进入 failed 状态，但已落盘 commit 保留；下次挂载完成
重放。credits 不足在发布任何元数据前失败，事务 snapshot 恢复 generation、
统计与 allocation hint。普通文件数据采用 ordered 而非 data journaling，
因此保证的是命名空间、分配和 inode 元数据的旧/新二选一，不承诺覆盖写的
旧数据内容可回滚。

### 全盘一致性

`Validate` 从根开始有界遍历，并独立重建 inode/data 两张可达 bitmap：

- 每个活动目录项引用已分配且 generation/type 匹配的 inode；
- 根以外每个 inode 恰有一个目录父边，父字段与实际目录一致；
- 目录父链无环，任何对象最终可达根；
- 数据块和指针块只能由一个位置拥有，范围与 CRC 有效；
- inode 的逻辑大小、数据/元数据计数与实际树一致；
- 目录大小是 320 字节的整数倍，空槽规范化为全零；
- 重建 bitmap 与盘面 bitmap 每一位相等；
- superblock 必须为 Clean，布局和 required feature 完全匹配；全盘遍历只在
  journal replay/丢弃完成后运行。

遍历时先在内存 bitmap 标记所有权，最后一次性比较盘面 bitmap；不会为每个
引用块重复复制整张 bitmap。队列、bitmap 和打开计数均使用固定上限存储，
Kernel 校验路径不依赖动态分配。

### 宿主工具

```bash
python3 tools/os.py mkfs-rootfs disk.img
python3 tools/os.py inspect-rootfs disk.img
python3 tools/os.py fsck-rootfs disk.img
python3 tools/os.py corrupt-rootfs disk.img superblock-checksum
```

mkfs 只创建新格式；inspect 输出版本、布局、事务、容量和使用量；fsck 只读
执行与 Kernel 独立的解码、CRC、树遍历和 bitmap 重算；corrupt 为测试翻转
受保护字节。镜像复制保留逻辑长度和非零稀疏 extent，不会把 1 GiB 空洞物化
为真实宿主存储。

## v1.7 rootfs 可执行文件与按需读取

rootfs v2 从 v1.6 起已经能够保存普通文件，v1.7 首次让这些字节成为正常启动
所依赖的程序映像。构建图在格式化生产镜像后显式创建 `/sbin` 与 `/bin`，
并安装：

```text
/sbin/init
/bin/sh
/bin/smoke
/bin/orphan_parent
/bin/orphan_child
/bin/argument_probe
/bin/exec_probe
/bin/exec_target
/bin/fs_probe
/bin/truncated.elf
```

最后一项是具名失败夹具；其余文件都经过宿主 ELF64 审计。离线安装命令按
精确前缀复制源文件，并由 Python rootfs 解析器重新查找 inode、遍历块树和
逐字节回读。构建依赖任何一个用户 ELF；程序变化会重新安装 rootfs、重组
启动盘并触发 QEMU，而不会让旧 ELF 留在磁盘。

Kernel 不把整份 ELF 先复制到固定大缓冲。VFS-backed `UserElfReader`
以 `(offset, length)` 从当前 `OpenFile` 读取，reader 形式的两遍 ELF
解析器先验证头、程序头、范围、重叠、W^X 与入口，再创建映射并逐段读取。
短读、设备错误和文件在读取期间不满足精确长度都返回 executable read
failure；语义错误单独返回 invalid executable，便于区分介质/I/O 故障和
不可信文件格式。

初始 `/sbin/init` 使用内核临时根 `FsContext` 解析；调度开始后的 spawn/exec
使用当前 Process 的 `FsContext`，所以相对路径遵守调用者 cwd 和 root。
reader 生命周期结束后一定关闭 OpenFile，不能把 loader 的临时打开引用泄漏
到新 Process。新 Process 自身取得独立 FsContext；spawn 不是 fork，不复制
父进程打开文件。

这一阶段没有增加磁盘格式、inode 或单文件上限：启动盘仍为逻辑 1 GiB，
rootfs 仍为 256 MiB，单文件仍最多 64 MiB。变化发生在“谁消费普通文件”与
构建依赖链，而不是引入 executable 专用 inode 类型。

## v2.3 rootfs v4 与完整参考盘

v2.3 把生产盘面一次性升级为 magic `OSRFV004`。128 GiB 参考盘共有
268435456 个扇区；启动前缀占 LBA `0..32767`，rootfs 使用余下
268402688 个块，即 137422176256 字节，并精确结束于 LBA `0x0FFFFFFF`。

| rootfs 相对块范围 | 块数 | 内容 |
| --- | ---: | --- |
| `0` | 1 | superblock 与 CRC32 |
| `1..4096` | 4096 | ordered metadata journal |
| `4097..4112` | 16 | 65536 位 inode bitmap |
| `4113..36880` | 32768 | 65536 个 256 字节 inode |
| `36881..102384` | 65504 | 数据块 bitmap |
| `102385..268402687` | 268300303 | 文件数据与间接指针块 |

superblock 新增显式 journal 起点/长度和 inode/data/metadata 三个分配摘要，全部
几何与计数字段继续使用 64 位小端编码。摘要随每个 metadata 事务提交，mount
无需顺序读取 65504 个 data bitmap 块；宿主 fsck 仍逐位重算并核对摘要。
Kernel 接受满足同一结构公式、且不超过参考上限的较小 v4 几何，容量测试因此能
在小型介质上真实跑到 ENOSPC；生产 mkfs 则只生成上述完整参考几何。格式 3
镜像不会被静默迁移或挂载，必须由宿主工具重新创建。

块树保留八个直接项，并从单/双/三级扩展到四/五级间接根。每个指针块仍保存
63 个 64 位相对块号和 CRC32。单文件逻辑上限等于生产数据区大小
137369755136 字节；稀疏 truncate 不预分配块，写到逻辑末尾只分配一条五级
路径和实际叶块。Kernel 与宿主测试都直接从绝对 LBA `0x0FFFFFFF` 读回文件，
避免把“字段是 64 位”误当成高 LBA 已经可用。

journal 区扩大到 4096 块，descriptor 从四块增至八块，单事务 credit 从 124
增至 248。header、八个 descriptor、最多 248 个 payload 和独立 commit 都有
CRC；其余 journal 块保留。1000 个确定性断电点现在覆盖 1..248 个 metadata
target，共完成 374620 项旧/新二选一和二次恢复幂等断言。journal 仍是项目内
实现，QEMU 只提供 ATA 扇区与 FLUSH。

inode v4 在原 256 字节内加入四/五级根、atime/mtime/ctime/btime 纳秒字段和
orphan 标志。ABI 兼容升级到 v2.2.0，`FileInformation` 从 64 字节扩为 96
字节，`stat` 输出四个时间戳。生产 Kernel 以稳定 CMOS UTC 秒为基准叠加单调
时钟；宿主离线安装保留源文件 mtime。当前采用 noatime 策略，普通 read 不为
更新 atime 额外制造 journal 事务。

VFS/rootfs 同时支持：

- 普通文件与符号链接硬链接，link count 必须等于目录引用数；目录禁止硬链接；
- 最长 4096 字节的绝对/相对符号链接目标、链式解析和 40 次跳转环路上限；
- unlink 后目录名立即消失，已有 OpenFile 继续读写；最后一个 close 在独立
  事务中释放 inode 和块；
- 若 orphan 事务提交后断电，下一次 mount 在发布根 superblock 前扫描并回收
  link count 为零的 orphan，再执行一致性检查；
- rename replace 对打开的普通文件使用同一 orphan 规则，打开目录仍返回 Busy。

Kernel 不再常驻一张约 32 MiB 的完整数据验证 bitmap。它以有界 inode bitmap、
链接计数表和队列遍历命名空间，对每个数据/指针引用核对盘面分配位，并以总数
守恒发现常规重复或泄漏；独立宿主 fsck 使用可扩展集合完整检查重复引用、泄漏、
硬链接计数、orphan、CRC 和高 LBA。启动不以宿主 fsck 替代 Kernel 恢复。

公开宿主入口是 `tools/os_tools/rootfs_v4.py`；历史 `rootfs_v2.py` 模块名只为
旧脚本导入兼容，实际编码、magic、输出与检查均为格式 4。mkfs、inspect、fsck、
损坏注入和稀疏复制都保持 128 GiB 逻辑长度，不顺序读取或复制数据区空洞。

## 当前限制

- rootfs 格式 4 已有 metadata journal 与 replay，但没有 data journal、
  在线修复或自动格式化；
- 文件权限、uid/gid、umask、设备节点和扩展属性进入 v2.4；
- v2.3 已冻结链接后端与路径语义，但用户态 `ln/readlink` 命令和权限检查进入 v2.4；
- V2.11 已缓存生产 dentry lookup 与 inode metadata；动态 unmount 和 mount namespace
  复制仍未实现；
- rootfs、legacy 和 memfs 都使用单实例锁串行化修改；
- ATA 运行期使用单飞 IRQ14 PIO 和显式 FLUSH CACHE，没有 DMA 或 tagged
  queue；early boot 仍采用有界轮询。

## v1.18 最小 devfs 与只读 procfs

v1.15 的专用控制台后端已经由通用最小 `Devfs` 替代。VFS 仍把它挂载到
`/dev`，但后端现在从调用者提供的固定 16 槽存储注册具名字符设备；生产实例
注册 `console`。注册项只保存 node identifier、generation、名称和 active，
不保存驱动指针。字符 read/write 仍由类型化 TerminalDevice
FileDescription 转发到 TTY。

devfs 命名空间只读；重名、空名、超长名称和容量耗尽在发布节点前失败。
lookup/readdir/stat/open/close 使用普通 vnode 契约，Validate 检查节点号、
槽位、generation、重名和打开统计。v1.18 不提供 unregister、权限、
major/minor 或热插拔。

`Procfs` 作为第四个后端挂载到 `/proc`，固定提供：

```text
version  uptime  meminfo  processes  resources  mounts
```

每次 read/stat 通过 callback 采集固定宽度数值；v2.5 扩展为 21 项并使用
512 字节局部缓冲
有界格式化，再遵守 offset/short-read/EOF。callback 执行时不持 procfs 锁；
procfs 锁只保护自己的 open/read/failure 统计，防止与 VFS、调度器、内存和
rootfs 形成锁环。所有节点只读，不支持 create/remove/rename/truncate。

VFS `ReadResourceUsage` 汇总四个后端的 heap/vnode 账本，并以 checked add
拒绝整数溢出。procfs 的资源回调只返回固定七个 vnode，不递归采集自己的
文本快照。单元、四后端挂载集成和 100000 步随机短读 oracle 共同冻结该语义。
详细决策见
[ADR 0045](../adr/0045-abi-v2-devfs-procfs-release-freeze.md)。

## v1.16 文件页写回与稳定 identity

VFS 新增不推进 `OpenFile` offset 的 `WriteAt`，供文件页缓存按后备 offset
写回。FilePageCache entry 使用 mount/superblock、inode identifier 与 inode
generation 作为稳定 key，并沿以下状态转移：

```text
Empty → Clean → Dirty → Writeback → Clean
                              └────→ Error → Writeback
```

Dirty/Error 页承担未落盘责任，不能作为 clean LRU 淘汰候选；达到 dirty hard
limit 时 MarkDirty 明确回压。Error 保留 frame 和 identity，后续 sync 从
同一页重试。

完整页、writable open file 的 `MAP_SHARED|PROT_WRITE` 映射以只读 PTE
开始。第一次写 fault 先 MarkDirty，再开放该共享 PTE；`MAP_PRIVATE` 仍走
COW。sync 先重新写保护全部 FileShared writable PTE，再通过 WriteAt 写回，
最后调用 VFS/rootfs sync 和 ATA FLUSH。这样写回快照之后的新写会再次 fault，
不会在 cache 已标 Clean 时静默发生。

rootfs 数据事务只增加盘面 transaction generation，不改变一次 mount 内的
VFS superblock generation。后者若随每次写变化，同一 inode 会形成两个
FilePageCache key，破坏 shared alias。集成测试冻结“mount generation 稳定、
transaction generation 增长”。

本阶段没有 `msync`、后台 flusher 或区间写回 ABI；显式全局 sync 是唯一正式
稳定边界。详细状态与失败语义见
[ADR 0043](../adr/0043-irq14-block-request-and-writeback-cache.md)。

## v2.4 Unix metadata 与 VFS DAC

rootfs v4 required feature `UNIX_METADATA` 激活 inode 的 uid32/gid32/mode32；旧
v4 镜像缺少该位时拒绝 mount。create/symlink 后端接收 VFS 已计算好的
`NodeCreationAttributes`，因此 owner、mode、inode 与目录项在同一 journal
事务中发布。chmod/chown 以独立 metadata 事务更新 ctime，chown 同时清除
setuid/setgid。

VFS 逐组件检查目录 execute/search，并在最终操作追加 read/write/execute 或
父目录 write+search。sticky 删除、setgid 目录组继承、root DAC 越权和普通
文件无执行位时 root 仍不可 exec 的规则与 Linux 一致。既有 OpenFile 不因之后
chmod 失效。

procfs 根/文件固定为 root:root 0555/0444；devfs 根为 root:root 0755，字符
设备为 root:tty 0660。详细盘面偏移、失败原子性和兼容边界见
[ADR 0052](../adr/0052-linux-compatible-local-credentials-permissions-and-rlimits.md)。

## v2.5 独立交换盘与目录引用释放

swap 已从 rootfs/VFS 移出，secondary IDE master 由 `SwapStorage` 直接管理；
rootfs 不再保存 `/.os-swap`，交换 I/O 不占用 OpenFile、FsContext、inode 或
journal credit。工程镜像的宿主稀疏性也不再代表来宾文件语义。

FsContext 的 root/cwd 都是已经 open 的目录引用。rootfs 目录不会进入 orphan，
持有 open reference 时 rmdir 必须返回 Busy，inode 不能被复用。因此目录 close
只做 vnode 结构、引用非零和总数守恒检查，然后递减引用；不得重新读取 inode。
普通文件 close 仍读取 inode，因为最后一个 orphan 引用可能需要启动回收事务。
该区分让进程退出不再因一次无关 ATA 读瞬态卡在 FsContext 释放，同时保留
open-unlink 文件的持久恢复语义。

## v2.11 VFS 命名空间缓存纯模型

第一增量新增 `fs/vfs_namespace_cache.*`，但不修改 `Vfs` 对象或 backend operation。dentry
key 保存 mount、parent superblock/node generation 和完整名称；inode identity 不保存
mount，使不同命名空间位置能共享同一 vnode 元数据身份。

Positive/Negative kind 与 Cached/Stale state 分开。失效只撤销 lookup 可见性，有外部引用
的旧项保留到最后 release；inode 失效级联目标 dentry 和全部 child 正负项。dentry/inode
分别按 access generation 回收，后者必须同时没有 external 和 dentry reference。线性扫描
仅用于验证第一增量语义，后续 hash、metadata 填充和 production lookup 不得改变 token、
引用或失效合同。设计见
[ADR 0072](../adr/0072-v2-11-vfs-namespace-cache-identity-and-lifecycle.md)。

## v2.11 inode metadata 生产缓存

第二增量扩展同一 `VfsNamespaceCache`，不建立第二套 identity/LRU。inode slot 的
Empty/Loading/Ready metadata 状态独立于 Cached/Stale 生命周期；load token 同时绑定 inode
与 metadata generation，使 chmod/write/rename 等失效后到达的旧 backend 结果无法提交。

`ReadNodeInformation` 是统一入口。Stat、DAC、open/exec、sticky/创建检查和打开文件 stat
复用 Ready 快照；owner 在锁外调用 backend，Loading/容量不足调用者正确旁路。
`StatOpenFileUncached` 保持直接后端语义，FilePageCache size hook 只修改返回副本。

修改成功后的失效范围为：create/symlink 的 parent；chmod/chown/write/truncate 的 target；
link 的 source/parent；rename 的 source、已有 destination 与两个 parent；remove 的
target/parent。失败的 backend operation 不失效。生产内核用 BSS 固定配置 4096/2048 槽，
没有 metadata 热路径分配或来宾逐项日志。并发 Loading waiter、Positive/Negative dentry
接线与 shrinker 仍属于后续增量。详见
[ADR 0073](../adr/0073-v2-11-inode-metadata-load-and-invalidation.md)。

## v2.11 生产 dentry、hash 与 shrinker

`ResolveInternal` 的普通组件统一进入 `LookupChild`。Positive/Negative 命中不访问 backend；
miss 在 resolution transaction 内查询并发布。只有 NotFound 能成为 Negative，设备错误和
权限错误保持下一次可重试。缓存不可发布时返回 backend 原结果。

create/link/rename/remove/symlink 的 backend commit、dentry key 和 inode metadata 失效使用
resolution→metadata 固定锁序。mount id 属于 key；命中 vnode 后仍执行 FollowMounts，cwd/
root 保持打开 Path，symlink target 仍从 backend 读取；rmdir 级联撤销旧目录 child。

生产 BSS 具有 4096 dentry、2048 inode、等量 hash entry 和 8192/4096 bucket。Cached 入
hash，Stale 只保留旧 token。LRU 与 background pressure 每批回收最多 64/64 个零引用逻辑
条目；固定 backing 不计入物理页。设计见
[ADR 0074](../adr/0074-v2-11-production-dentry-lookup-and-namespace-mutation.md) 与
[ADR 0075](../adr/0075-v2-11-namespace-hash-lru-and-pressure-shrinker.md)。

## v2.12 分片与页后备 namespace

`Vfs` 现在为 dentry lookup 和 inode metadata 各维护 64 个 `RuntimeMutex` shard。读侧按稳定
key/identity 选 shard；多 inode mutation 用 `MetadataLockGuard` 升序获取去重 shard。128 个
`VfsResolutionContext` 从真实页稳定区提供独立 path scratch，生产解析不再依赖全局
resolution lock。

namespace mutation 仍保持单写，并以偶/奇 sequence 发布一致性。resolver 若跨过 commit，
会撤销本轮缓存观察并最多重试八次；NotFound/EIO、mount/cwd/root、symlink、DAC、orphan 和
打开 vnode generation 的 V2.11 语义保持不变。

`vfs_namespace_backing.*` 只负责溢出安全的布局/视图，不自行分配。Kernel 用两个真实
`KernelPageAllocation` 实例提供稳定区和 preferred bucket 区。首次 pressure shrink 先把
cache 在线重建到稳定区的 compact bucket，再释放 preferred 页；实际 frame/buddy/KVA 差值
进入 VFS 资源账本。详见
[ADR 0076](../adr/0076-v2-12-scalable-page-backed-vfs-namespace.md)。

## v2.13 目录句柄与 `*at` 事务

`DirectoryHandle` 是从目录 `OpenFile` 临时 retain 的 VFS 能力，只保存 `Path` 和 active 状态。
它额外持有后端 open 引用，因此目录被 rename 后仍按原 vnode 解析；release 后对象清零。
`BuildAtContext` 让绝对路径忽略 handle，让 cwd 调用使用原 `FsContext`，并只对相对路径验证
目录类型。

单基准 `ResolveAt/OpenAt/OpenDirectoryAt/CreateDirectoryAt/RemoveFileAt/RemoveDirectoryAt/
StatAt/ReadSymbolicLinkAt/CreateSymbolicLinkAt` 复用既有 VFS 原语。`RenameAt/LinkAt` 分别建立
source/destination context，再进入一个 writer transaction；DAC 和 creation attributes 使用
进程 context，不能由目录 fd 替换凭据或 root。

namespace 修改锁现在覆盖首次解析到 backend commit。每个 writer 捕获 expected even
sequence，`NamespaceMutationGuard` 在提交前精确复验后发布 odd/even 状态。并发
`open(create)` 在锁内二次解析：若路径已出现则直接打开解析 vnode，避免锁重入。统计提供
at operation、directory retain/release/active/peak，`Validate` 检查差值守恒。详见
[ADR 0077](../adr/0077-v2-13-directory-handles-and-at-path-transactions.md)。

真实 rootfs writeback 可能从 pressure worker 的深调用链进入。initialize、superblock、
inode、pointer、bitmap、allocate、file read/write、truncate、orphan 与 validation 的 4096
字节块都不再放在栈上；RootFileSystem 由 `lock_` 串行，并为可能嵌套的 helper 分别持有实例
scratch，避免相互覆盖并保持物理 BSS 所有权可见。

## v2.14 打开 vnode 元数据与原子 append

`ChangeOpenFileMode/ChangeOpenFileOwner` 从 OpenFile 保存的 Path 进入公共 `ChangeNodeMode/
ChangeNodeOwner`，与路径版本共享 DAC、read-only、set-id 清理、metadata shard 和失效语义。
`StatOpenFile` 继续把 FilePageCache 的逻辑 size 覆盖到 inode snapshot，供 fstat 与 SEEK_END
观察尚未写回的正确 EOF。

V2.14 当时由 `Vfs::Append` 的共同 RuntimeMutex 把逻辑 EOF 查询、RLIMIT_FSIZE 裁剪和
`WriteAt` 放入同一临界区；它比 FileDescription operation lock 多覆盖独立 open。零长度、
部分写、FileTooLarge 和 backend/cache 失败都返回精确 written/offset，锁内不做 user-copy。
V2.15 已用每 inode guard 替换该全局串行点。V2.14 原始决策见
[ADR 0078](../adr/0078-v2-14-open-file-description-operations.md)。

## v2.15 每 inode I/O 协调

`inode_io_coordinator.*` 常驻 128 个活跃槽。identity 包含 superblock/node 的 identifier 与
generation；token 再绑定槽 generation。短 SpinLock 只负责查找、LRU、引用和统计，实际等待
发生在每槽独立 `RuntimeMutex`。零引用槽可替换，全部槽均被引用时返回 CapacityExhausted，
不会退回全 VFS mutex。

`RegularFileIoGuard` 当前覆盖 buffered Write/WriteAt、Append、truncate、共享 mmap 首次脏化、
fsync/fdatasync 和同步 msync。Append 在 guard 内读取 FilePageCache 逻辑 size，并调用不重复
加锁的内部 write helper；不同 inode 可以同时进入缓存层，同 inode 的独立 open 仍只会有一个
逻辑 size/mapping 提交者。

truncate prepare 现在由 ProcessRuntime 配置为 VFS hook。`TruncateNode` 取得 guard 后取消预读、
写保护 shared PTE、释放 EOF 外映射并等待 writeback，再提交 backend size、metadata 失效和
cache truncate。path truncate、O_TRUNC 和 ftruncate 共用该入口；prepare EIO 不触及后端或
缓存长度。

后台 writeback 保持页级 Dirty/Writeback/Error 与 waiter 状态机，不取得 inode guard。这样
truncate/fsync 能在持 guard 时主动完成既有脏页，而 `WriteUncachedAt` 不会递归锁死；前台
buffered/mmap writer 又不能在准备阶段新增同 inode 脏化。协调器统计由独立单元、十万步模型
和 VFS 强制交错集成读取，正常 VGA 不增加逐 inode 日志。设计见
[ADR 0080](../adr/0080-v2-15-per-inode-io-coordination.md)。

rootfs v4 自身仍由实例锁串行后端修改。V2.16 起的新 rootfs v5 才以 4 KiB block group、
extent、HTree 和 journal v2 拆分盘面并发；V2 总体边界见
[ADR 0079](../adr/0079-v2-mini-ext4-rootfs-v5-program.md)。

## v2.16 rootfs v5 格式模型

`root_file_system_v5_format.*` 定义独立于 rootfs v4 的 `OSRFV005`。生产 profile 使用 4 KiB
block、32768 block/group、1024 group、256 字节 descriptor/inode 和 2048 inode/group。
所有盘面读写逐字段显式小端编码，不能 `reinterpret_cast` 或写入宿主 struct padding。

格式模型当前提供：

- production profile 规划及 64 位算术溢出、LBA/块对齐、尾组容量验证；
- 组 0、1 和 3/5/7 纯幂组的 sparse superblock/GDT copy 定位；
- group 内 copy、bitmap、inode table、data 区间与初始空闲计数的唯一推导；
- superblock、descriptor、inode 的 CRC32C 编解码、feature 协商与保留字节拒绝；
- reserved inode 与根 inode 的类型、mode、parent、generation 和零 mapping root 验证。

该模块不持有 `BlockDevice`、Vnode、VFS mount 或动态内存，也不实现目录/数据块映射。Python
`rootfs_v5.py` 独立实现同一语义，额外生成/检查 bitmap、inode table 和所有 sparse backup；
二者由标准 CRC32C 和固定 production profile checksum 约束。v2.17 journal 不得改变已经冻结的
基础偏移，只能通过 feature 和未使用字段演进。完整决策见
[ADR 0081](../adr/0081-v2-16-rootfs-v5-block-group-format.md)。

## v2.17 journal v2 与 orphan file

`root_journal_v2_format.*` 只负责稳定盘面：journal superblock、descriptor tag、revoke array、
commit、checkpoint 和 orphan block 均逐字段小端编码并使用 CRC32C。descriptor 的 target、
payload index/checksum/flag 唯一绑定；commit 再绑定 descriptor/revoke checksum，不能通过重算
单个损坏块绕过事务一致性。

`RootJournalV2` 负责状态与 I/O。一个 active transaction 在内存中最多保留 16 个 metadata、
8 个 ordered data 和 32 个 revoke；四个 committed slot 可以在 checkpoint 前并存。StageMetadata
覆盖同 target 的旧 payload 并移除本事务 revoke；Revoke 移除本事务 staged metadata。credit
耗尽必须在改变事务前返回，不能出现“报告失败但已经删除旧操作”的部分提交。

```text
Begin: reserve durable sequence
Stage*: memory only
Commit: journal prepared Flush → ordered home Flush → commit Flush
Checkpoint: metadata home Flush → checkpoint Flush → superblock → clear slot
Recover: validate all → sort sequence → apply later revoke → replay/checkpoint
```

设备接口仍按 512B sector；Read/WriteFileSystemBlock 负责 8-sector 聚合。测试 device 把 volatile
sector 与 durable sector 分开，Flush 才合并，Crash 丢弃 volatile。这个模型用于证明 ordered
蕴含关系和恢复幂等，不是 NVMe/ATA 驱动替身。

orphan block 以 0 表示空槽，非零 inode 必须位于 user inode 范围且唯一；entry count 必须重算
一致。它本身不释放 extent，而是让后续 mount/recovery 知道哪些 inode 仍需完成 truncate 或
unlink。决策见 [ADR 0082](../adr/0082-v2-17-rootfs-v5-journal-v2.md)。

## v2.18 extent、allocator 与 delayed allocation

`root_extent_tree.*` 同时提供盘面 codec 和 runtime canonical tree。盘面 node 有 123 个 entry；
runtime 使用 4 路/256 extent/85 node/深度 3。Insert 同时检查 logical 和 physical ownership，
Convert 可把任意完整覆盖的 Unwritten/Initialized 子范围 split，再由 Normalize 合并相邻同态
连续映射；Remove 返回被删除交集的精确物理范围，供 allocator 释放。

`root_block_group_allocator.*` 借用调用方提供的 descriptor、bitmap 和 free-count storage。
Reserve 先置位并返回 slot+generation token；Commit 保留位，Abort 清位。扫描优先 preferred
group，返回 exact 或满足 minimum 的 partial run，再循环 fallback。protected range、metadata、
尾组越界位、active reservation 和 stale token 均不可释放。

`root_delayed_allocation.*` 管理 64 个逻辑脏范围和一个 active writeback：

```text
ReserveWrite       delayed only
BeginWriteback     allocator reserve + Unwritten insert
CompleteWriteback  Unwritten→Initialized + reservation commit + delayed remove
AbortWriteback     extent remove + reservation abort; delayed remains
```

Fallocate、PunchHole、Truncate 和 seek/query 复用相同状态。QueryRanges 最多输出 320 条，Delayed
返回 NO_BLOCK，Unwritten 保持物理 ownership 但按 hole 处理。当前模块不处理已映射 Initialized
extent 的覆盖写内容；那条路径继续由 FilePageCache 权威页和既有 writeback 协调。

extent node 经 RootJournalV2 metadata payload 提交，data block 经 ordered payload 先稳定。完整
边界见 [ADR 0083](../adr/0083-v2-18-rootfs-v5-extents-allocation.md)。

## v2.19 directory 与 inode policy metadata

`root_directory_index.*` 提供 variable dirent codec 和 8 路 HTree；排序键为 `(hash,name)`，冷
lookup 记录实际 node count。`root_inode_metadata.*` 提供 extension v1、sorted xattr、POSIX ACL
和 user/group quota。decode 在任何 name/value copy 前验证长度，ACL 显式零权限不会回退，quota
更新先验证候选再提交。盘面与策略见 [ADR 0084](../adr/0084-v2-19-directory-metadata-policy.md)。

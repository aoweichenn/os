# File System 模块

## 职责

File System 模块分为两个层次：VFS 负责路径、挂载、vnode、每 Process
root/cwd、命名空间修改和打开实例；后端负责具体节点与存储。当前生产根目录
由 rootfs v2 把 512 字节 ATA 扇区组织为带校验的 superblock、bitmap、
inode、目录项、稀疏文件和三级间接树；memfs 从 KernelHeap 动态拥有目录、
文件与数据。legacy 后端只保留旧格式兼容与回归，不再作为生产根。

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
- 解析期间不申请内存，不缓存正/负 dentry。

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

启动盘逻辑长度为 1073741824 字节，并以稀疏宿主文件构造。rootfs 从
LBA 32768 开始，固定占用 524288 个 512 字节块：

| 相对块范围 | 块数 | 内容 |
| --- | ---: | --- |
| `0` | 1 | superblock |
| `1..2` | 2 | 8192 位 inode bitmap |
| `3..4098` | 4096 | 8192 个 256 字节 inode |
| `4099..4226` | 128 | data bitmap |
| `4227..524287` | 520061 | 数据块与间接指针块 |

superblock magic 为 `OSRFV002`。全部多字节字段使用显式 little-endian
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

### 事务、缓存与失败

rootfs 使用固定容量 `BlockCache`。分配器保存 inode/data 搜索 hint，使
顺序构造大文件不必从 bitmap 起点反复扫描；hint 只影响性能，位图仍是唯一
分配事实。superblock 写入也经过缓存的具名块并立即 sync，避免每个事务把
所有热缓存项无条件失效。

修改顺序固定为：

```text
superblock = Dirty; flush
  -> 修改数据、pointer、inode、目录和 bitmap
  -> cache Sync + ATA FLUSH CACHE
  -> generation++; superblock = Clean; flush
```

任何设备错误使实例进入 failed 状态。Dirty 介质下次挂载返回
`IncompleteTransaction`；CRC、保留字节、可达性、重复所有权或 bitmap
不一致返回 `Corrupt`。生产 Kernel 对两者都只记录状态并停机，不调用 mkfs。
该协议保证“已知不可安全使用”，不保证掉电恢复；journal/replay 属于后续
版本。

### 全盘一致性

`Validate` 从根开始有界遍历，并独立重建 inode/data 两张可达 bitmap：

- 每个活动目录项引用已分配且 generation/type 匹配的 inode；
- 根以外每个 inode 恰有一个目录父边，父字段与实际目录一致；
- 目录父链无环，任何对象最终可达根；
- 数据块和指针块只能由一个位置拥有，范围与 CRC 有效；
- inode 的逻辑大小、数据/元数据计数与实际树一致；
- 目录大小是 320 字节的整数倍，空槽规范化为全零；
- 重建 bitmap 与盘面 bitmap 每一位相等；
- superblock 必须为 Clean，布局和 required feature 完全匹配。

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

## 当前限制

- rootfs v2 没有 journal、replay、在线修复或自动格式化，只检测并拒绝；
- 没有硬链接、符号链接、权限、uid/gid、时间戳、设备节点和扩展属性；
- 打开文件不能 unlink，尚无 orphan inode 与延迟回收；
- 没有 dentry/inode/page cache、动态 unmount 或 mount namespace 复制；
- rootfs、legacy 和 memfs 都使用单实例锁串行化修改；
- ATA 仍采用轮询单扇区 PIO 和显式 FLUSH CACHE，没有 IRQ/DMA/异步请求。

## v1.15 固定控制台字符设备

VFS 新增 `CharacterDevice` 节点类型，并把固定设备后端挂载到 `/dev`。
当前后端只含根目录和 `console`，命名空间只读；open/stat/readdir/retain/close
遵守普通 vnode 生命周期，read/write 由 TerminalDevice FileDescription
转发到 TTY。字符设备没有普通文件 offset，truncate 明确不支持。

这是通用 devfs 前的最小路径契约；动态设备注册、权限与热插拔留到 v1.18。

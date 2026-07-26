# File System 模块

## 职责

File System 模块分为两个层次：VFS 负责路径、挂载、vnode、每 Process
root/cwd 和打开实例；后端负责具体节点与存储。当前 legacy 后端把 512 字节
ATA 扇区组织为带校验的 superblock、bitmap、inode、目录项和普通文件，
memfs 后端则从 KernelHeap 动态拥有目录、文件与数据。

依赖方向：

```text
用户包装器
  → 系统调用与每进程 FileTable
    → 共享 FileDescription
      → Vfs / OpenFile / FsContext
        → BackendOperations
          ├→ Memfs → KernelHeap
          └→ LegacyFileSystem
              → FileSystem
                → BlockCache
                  → FileSystemBlockDevice
                    → AtaPioDevice
```

宿主测试以 `MemoryBlockDevice` 替换最底层设备，但复用完整
`BlockCache`、格式解析、路径和文件操作实现。

## 固定布局

2 MiB 磁盘的前 1 MiB 保留给启动链。文件系统起始 LBA 为 2048，总块数为
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

## 核心不变量

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

## 缓存

八项 LRU 缓存使用单调访问代次选择最久未使用项。读取命中不访问设备；未命中
会淘汰旧项，若旧项为脏则先写回。`Sync()` 写回全部脏项并调用设备 flush。

缓存对象在初始化时接收块设备引用，不拥有设备生命周期，也不使用动态分配。

## 路径与文件句柄

路径解析只接受绝对路径。`/` 直接解析到根 inode；其他路径逐个在父目录中
查找 64 字节目录项。

文件句柄保存 inode、当前位置、读写能力和打开状态。Read/Write 使用当前
位置并在成功后推进。截断把文件长度归零并释放全部直接块；Close 不删除 inode。

## 修改提交

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

## 当前限制

- 没有删除、重命名、硬链接、权限或时间戳；
- 没有间接块，单文件最大 5120 字节；
- 没有 Dirty 事务恢复，只检测并拒绝；
- legacy 后端仍由全局锁串行化，memfs 也使用单实例锁；
- ATA 仍采用轮询单扇区 PIO 和显式 FLUSH CACHE。
- 没有 dentry cache、符号链接、动态 unmount 或 mount namespace 复制。

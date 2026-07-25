# File System 模块

## 职责

File System 模块把 512 字节 ATA 扇区组织为带校验的 superblock、bitmap、
inode、目录项和普通文件，并向内核系统调用层提供有界文件句柄。

依赖方向：

```text
用户包装器
  → 系统调用与每进程 fd 表
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

## 当前限制

- 没有删除、重命名、硬链接、权限或时间戳；
- 没有间接块，单文件最大 5120 字节；
- 没有 Dirty 事务恢复，只检测并拒绝；
- 全局锁串行化文件系统操作；
- ATA 仍采用轮询单扇区 PIO 和显式 FLUSH CACHE。

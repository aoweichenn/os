# ADR 0032：以 VFS、挂载命名空间和 memfs 隔离路径语义与磁盘格式

- 状态：已接受
- 日期：2026-07-26

## 背景

v1.4 已经把进程局部 fd、共享 `FileDescription` 和底层打开状态分离，但
`FileDescription` 仍直接保存旧文件系统的 `FileSystemHandle`。路径解析、
inode 编号、目录格式和磁盘事务仍属于同一个具体实现。这会把后续演进锁在
旧磁盘布局上：

- 用户只能从根目录开始访问，Process 没有自己的 root/cwd；
- `.`、`..`、重复 `/` 和挂载点穿越没有统一语义；
- 内存文件系统与磁盘文件系统无法通过同一套测试；
- `FileDescription` 无法保存“属于哪个挂载、哪个 vnode”的稳定打开对象；
- 若直接升级 rootfs，路径算法错误和磁盘格式错误会同时出现，难以定位；
- 旧磁盘的兼容读取容易被新格式的自动格式化逻辑破坏。

Unix 的历史实现逐步把“目录中的名字”“文件系统内部对象”“挂载关系”和
“一次打开”拆成不同层次。现代内核通常以 vnode/inode 抽象底层对象，以
mount 连接多个文件系统，再由路径遍历把组件逐个解析成对象。项目不复制
Linux 的全部 dentry、RCU 和 namespace 复杂度，但必须先冻结同样清楚的职责
边界，才能在 v1.6 安全替换磁盘格式。

## 决策

### 建立五层命名与打开模型

v1.5 的文件访问链固定为：

```text
Process::FsContext
  -> Path { mount_identifier, Vnode }
  -> Mount
  -> Superblock
  -> BackendOperations
  -> memfs 或 legacy-fs

fd -> FileTable -> KernelObject<FileDescription> -> OpenFile -> Path
```

各层职责如下：

| 类型 | 职责 |
| --- | --- |
| `Vnode` | 表示某个 Superblock 中的对象身份、generation 与类型 |
| `Path` | 把 Vnode 与所在 Mount 绑定，避免只凭 inode 号跨文件系统混淆 |
| `Superblock` | 保存后端种类、根 Vnode、只读属性、名称上限和操作表 |
| `Mount` | 记录父挂载、父文件系统中的挂载点和子 Superblock |
| `FsContext` | 保存某个 Process 可见的 root 与当前工作目录 |
| `OpenFile` | 保存一次打开的 Path、共享偏移、读写模式和打开状态 |

`FileDescription` 继续承担 v1.4 已冻结的共享打开实例语义，但其 payload 从
`FileSystem* + FileSystemHandle` 改为 `Vfs* + OpenFile`。因此 duplicate
继续共享偏移，独立 open 继续获得独立偏移；fd 与对象生命周期完全不需要
回退或重写。

### 后端只通过显式操作表进入 VFS

`BackendOperations` 定义以下完整契约：

- `lookup`、`create`、`parent`；
- `read`、`write`、`truncate`；
- `read_directory`、`get_name`；
- `sync`、`validate`、`read_resource_usage`。

VFS 不读取 memfs 节点字段，也不读取旧 inode 布局。后端不解析含 `/` 的
完整路径，只接收已经切分并验证的单个名称。这样路径语义只有一个实现，
同一套契约测试可以分别挂在 memfs 和 legacy-fs 上。

操作表是常量对象，不使用宏或隐式注册。Superblock 必须具有非零 identifier
与 generation、目录类型根 vnode、有效上下文和全部非空操作；初始化时一次
验证，`Validate` 时重新验证。

### 路径长度和组件长度采用统一 64 位规格

VFS 的公开上限为：

- 完整路径最多 4096 字节；
- 单个组件最多 255 字节；
- 默认挂载表容量 64；
- 遍历步数最多 4096，达到上限返回 `LoopDetected`。

长度、偏移、identifier、统计和容量全部使用 `uint64_t`。ABI 不使用
`long`、`size_t` 等依赖宿主平台宽度的类型。底层 legacy-fs 仍只支持
40 字节名称；该较小限制只存在于其 Superblock，VFS 和 memfs 不被旧格式
反向限制。

路径不依赖 C 字符串终止符，调用方必须同时传入地址和精确长度。控制字符、
DEL 和组件内 `/` 被拒绝。空路径返回 `InvalidPath`；4097 字节路径返回
`PathTooLong`；256 字节组件返回 `NameTooLong`。尾部 `/` 明确要求最终对象
为目录，不能把 `/file/` 静默当作 `/file`，也不能以 create 模式误建普通
文件。

### 路径遍历逐组件执行并显式处理特殊组件

绝对路径从 `FsContext.root` 开始，相对路径从
`FsContext.current_working_directory` 开始。连续 `/` 被折叠；`.` 保持当前
Path；`..` 调用统一的 `MoveToParent`：

1. 当前 Path 等于进程 root 时保持不变并记录 root clamp；
2. 当前对象是某个子挂载的根时，先退出子挂载；
3. 返回父文件系统中挂载点的父目录；
4. 普通对象则调用后端 `parent`；
5. 每一步重新验证 mount、superblock、identifier、generation 和类型。

普通组件先要求当前 vnode 为目录，再调用所属 Superblock 的 `lookup`。
得到子 vnode 后立即检查该 Path 是否为挂载点；若是则进入子 Mount 的根，
并重复检查，直到没有子挂载或检测到循环。

当前版本不建立正/负 dentry cache。每次解析都真实进入后端。这样性能不是
最终形态，但查找结果没有缓存一致性、失效、引用回收或 RCU 的隐藏状态，
适合作为后续 rootfs v2 的差分基准。

### getcwd 从对象关系反向重建

`GetWorkingDirectory` 不保存一份可能过期的路径字符串。它从 cwd 开始反向
取得每个 vnode 的名称并移动到父目录，把组件从目标缓冲区尾部向前写入，
最后整体前移：

```text
/alpha/beta
       beta <- get_name(cwd)
       alpha <- get_name(parent)
       / <- reach FsContext.root
```

遇到挂载根时，名称来自父文件系统中的挂载点，而不是子 Superblock 的空根名。
因此进入 `/tmp` 的 memfs 后，`pwd` 仍返回 `/tmp`。目标容量不足时返回
`PathTooLong`，不会截断；最多重建 4096 字节。

### 挂载拓扑在调度开始前建立并保持不变

根 Mount 在 `Vfs::Initialize` 中占用 identifier 0。`MountAt` 解析一个现有
目录作为挂载点，拒绝以下状态：

- 替换当前进程可见 root；
- 同一 Superblock 被重复挂载；
- 同一挂载点已有子 Mount；
- 在现有子挂载根上堆叠另一层；
- 挂载表达到容量；
- 父链形成循环。

v1.5 没有 `unmount`，也不允许调度开始后修改拓扑。内核启动顺序固定为：
旧文件系统挂载或拒绝、建立 `/tmp`、初始化 legacy 适配器、初始化 memfs、
初始化 VFS、挂载 `/tmp`、完整校验、为 Process 初始化 FsContext，最后才
启用用户执行。

由于拓扑不可变，路径读侧无需在每个组件期间长期持有 VFS 全局锁。VFS 锁只
保护统计和启动期 mount 发布；后端各自保护节点或磁盘状态。未来若引入动态
mount/unmount，必须另写读侧生命周期、引用和失效 ADR，不能直接放宽此约束。

### memfs 是完整测试后端，不是硬编码假对象

memfs 从 `KernelHeap` 动态申请节点和文件数据。节点以非零单调 identifier
和 generation 标识，保存：

- 父 identifier、节点类型和最多 255 字节名称；
- 规则文件的 size、capacity 和数据地址；
- 侵入式全局节点链。

文件数据从 64 字节起按二倍增长，但绝不超过该实例的
`maximum_file_size_bytes`；若最大文件本身小于 64 字节，则第一次容量直接
取该最大值。增长先申请并清零新缓冲，复制旧内容，确认旧缓冲可释放后才发布
新地址。写入空洞会显式清零；truncate 扩展补零，缩小时清除被截断区域。

每次节点或数据申请都比较 KernelHeap 前后统计，记录该 memfs 当前独占的：

- consumed bytes；
- active requested bytes；
- allocation count；
- vnode count。

`Validate` 重新遍历节点链，检查父目录、名称唯一性、祖先无环、容量与数据
指针一致、目录不持有文件数据，并从实际节点反算全部资源统计。`Destroy`
逐项归还数据和节点，最终要求四项活动资源归零。

### legacy-fs 适配器保留磁盘格式而不复制路径解析

`LegacyFileSystem` 把旧 inode 与目录操作映射为 vnode 后端：

- 旧 inode number 成为 vnode identifier；
- 旧格式名称上限保持 40 字节；
- lookup、基础 create/mkdir、read/write、零长度 truncate、readdir、
  parent/get_name、sync 和 consistency check 接入操作表；
- 旧格式没有删除和重命名，因此适配器不伪造这些能力；
- 旧磁盘 superblock、inode、位图、目录项和事务格式不改变。

挂载仍由既有 `MountOrFormat` 执行：全零新介质可以格式化；具有未知或损坏
非零元数据的磁盘必须拒绝，不能为了让测试继续而自动清空。v1.5 的 QEMU
持久化测试在同一可写镜像上跨两次启动验证旧文件仍可读取，并继续保留损坏
拒绝证据。

### 每个 Process 持有独立 FsContext

`ProcessRuntimeProcess` 新增 `FsContext`。VFS 挂载完成后，所有已准备 Process
都从 VFS 根初始化 root/cwd；之后新建 Process 也执行相同初始化。`chdir`
只修改当前 Process 的 cwd，不修改全局 VFS 或其他 Process。

本阶段新增系统调用：

| 编号 | 名称 | 成功结果 |
| ---: | --- | --- |
| 29 | `ChangeDirectory` | 0 |
| 30 | `GetWorkingDirectory` | 实际路径字节数 |

目录项 ABI 的名称容量从 40 提升到 255 字节，结构大小固定为 280 字节，并
增加显式 `reserved` 字节，确保从内核复制给用户时不存在未初始化尾部填充。
新增错误结果 `-26..-29` 分别表示路径过长、名称过长、路径循环和只读文件
系统。

`GetWorkingDirectory` 对用户声明的大于 4096 的容量只验证并使用前 4096
字节，因为内核承诺最多写入这么多；它不会错误地把“缓冲区更大”判为路径
过长。所有用户地址仍先经过当前页表的可写范围验证，再执行复制。

### 持久 VFS 资源与 Process 泄漏证据分开核算

memfs 在用户进程退出后仍属于已挂载文件系统，节点和数据不应被当作 Process
泄漏。VFS 汇总所有后端的 `ResourceUsage`；Process 结束快照先把这些持久
heap/vnode 数量作为补充计数纳入完整校验，再从比较快照中精确扣除，只比较
Process、Thread、页表、栈、fd、FileDescription 等进程所有资源。

这不是关闭资源检查。若某个 FileDescription、frame、KVA、heap block 或
对象引用没有释放，扣除已登记 memfs 所有权后差异仍非零，QEMU 会以
`ResourceLeakDetected` 失败。memfs 自身则由 VFS/memfs `Validate` 和独立
Destroy 测试验证。

## 锁顺序

v1.5 的锁与生命周期顺序固定为：

```text
启动期 VFS mount 发布
  -> 运行期 VFS 路径遍历（拓扑只读）
  -> backend lock
      -> legacy block cache / ATA
      -> 或 memfs -> KernelHeap

FileTable
  -> KernelObjectManager（取得临时引用）
  -> 释放表锁和管理器锁
  -> FileDescription operation lock
  -> VFS
  -> backend
```

对象最后引用归零后，finalizer 在 FileTable、KernelObjectManager 和对象操作
锁之外调用 `Vfs::Close`。VFS 统计锁不包围后端调用，避免形成
`VFS -> backend -> VFS statistics` 的反向嵌套。

## 验证

新增三项直接证据：

1. `os_kernel_vfs_unit_tests` 覆盖绝对/相对路径、重复分隔符、点组件、根
   clamp、尾斜杠、4096/255 精确边界、挂载进入/退出、getcwd、独立偏移、
   目录枚举、容量与人为挂载环；
2. `os_kernel_vfs_backend_contract_integration_tests` 把同一基础契约分别运行
   在 memfs 和 legacy-fs 上，并重新挂载旧镜像验证持久化；
3. `os_kernel_vfs_namespace_randomized_tests` 用固定种子执行 100000 步目录、
   文件、cwd、父目录、枚举和缺失路径操作，每一步与独立宿主模型比较。

整机 functional QEMU 由真实 Shell 执行：

```text
pwd
cd /tmp
mkdir session
write session/message temporary
cat ./session/../session/message
ls .
cd ..
mkdir /demo
write /demo/message hello
sync
```

前半段证明 memfs、相对路径、`.`、`..`、挂载和 cwd；后半段证明 legacy
磁盘后端仍可创建、读取、同步和跨启动持久化。非法指令、用户页故障和非法
ELF 三条隔离路径也继续运行，以排除初始化顺序改变造成的故障回归。

## 后果

正面结果：

- 路径与挂载语义不再依赖旧磁盘布局；
- v1.6 可在不修改 Shell、fd、FileDescription 或路径遍历的情况下替换
  rootfs 后端；
- memfs 与 legacy-fs 由同一契约和同一整机路径比较；
- 每 Process cwd 已成为真实内核状态；
- 旧磁盘兼容和损坏拒绝继续保持；
- 持久文件系统资源与 Process 泄漏检查具有明确所有权边界。

代价与限制：

- 当前每个组件都会进入后端，legacy `get_name` 还需要扫描目录树；
- mount 表固定 64 项且只在启动期增长；
- 没有 dentry cache、unmount、bind mount、符号链接或权限模型；
- memfs 使用线性节点链，目的是可审计而不是高性能；
- legacy 适配器只承担迁移，不扩展成最终磁盘格式。

## 被否决的方案

### 直接把旧 FileSystem 改名为 VFS

这会保留完整路径解析、inode 和磁盘布局的耦合，无法让 memfs 与未来 rootfs
通过同一后端契约，因此否决。

### v1.5 同时引入 rootfs v2

同时改变 name walk、mount、FileDescription 和磁盘事务，会让错误来源不可
分离，也失去旧格式差分基准。rootfs v2 保留到 v1.6。

### 先加入 dentry cache

缓存需要正/负项、代次、失效、引用、并发和回收策略。当前没有动态 unmount
和 rename，先加入只会扩大未经证明的状态空间，因此否决。

### 让所有 Process 共享一个 cwd 字符串

字符串可能在挂载或重命名后失真，也无法表达进程隔离。cwd 必须保存 Path，
显示文本由 getcwd 从对象关系重建。

## 关联

- [v1.5 发布记录](../releases/v1.5.md)
- [v1.5 路线与验收](../roadmap.md#v15-vfsmemfs-与旧格式基础适配)
- [文件系统模块](../modules/file-system.md)
- [系统架构](../architecture.md)
- [测试策略](../testing.md)
- [日志协议](../logging.md)

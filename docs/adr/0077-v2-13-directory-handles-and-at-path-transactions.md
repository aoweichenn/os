# ADR 0077：V2.13 目录句柄与 `*at` 路径事务

状态：已接受

日期：2026-08-24

## 问题

V2.12 已让路径读取可分片并行，但用户态修改仍只能从进程 cwd 或绝对路径重新遍历。
这种接口无法稳定表达“先打开目录，再相对该目录操作”：目录被 rename 后，字符串 cwd
不能代替已打开目录的 vnode 身份；双路径 rename/link 还可能在两个不同 parent 下解析，
需要同一写事务和明确的目录 fd 生命周期。

## 决策

- `DirectoryHandle` 保存已打开目录的 `Path` 与 active 状态；从 `OpenFile` retain 时调用后端
  `open` 墫ლ加强引用，release 调用后端 `close`，因此目录 rename 后旧句柄仍指向原 vnode；
- `BuildAtContext` 统一解释路径基准：绝对路径忽略 `dirfd`，`AT_CWD` 使用进程 cwd，其他
  相对路径必须从类型为 Directory 的 FileDescription 临时 retain 出句柄；
- VFS 提供 `ResolveAt/OpenAt/OpenDirectoryAt/MkdirAt/RemoveAt/StatAt/ReadlinkAt`，以及拥有
  独立 source/destination context 的 `RenameAt/LinkAt/SymlinkAt`；权限仍使用调用进程的
  root、凭据和 creation mask，不从目录 fd 继承权限；
- 所有 namespace writer 在首次 path resolution 前取得单写 `RuntimeMutex`，捕获偶数
  namespace sequence，并在进入 backend commit 前按精确 expected sequence 复验；同名
  `open(create)` 的败者在锁内二次解析后直接复用胜出者 vnode，不递归取得写锁；
- ABI v2.5.0 只在 87 后追加 88..96。单路径简单调用直接使用寄存器，stat/readlink 和双路径
  调用使用固定宽度请求结构；`AT_CWD=UINT64_MAX`，remove 与 stat 只接受各自冻结 flag mask；
- 系统调用先完整复制并验证用户请求，再取得目录 FileDescription 的短期引用。双路径和
  readlink 使用单 BSP 抢占保护的 8 KiB 静态 scratch，避免两个最大路径压垮 16 KiB Kernel
  栈；热路径只增加 VFS 聚合计数，不输出逐调用日志。
- 新增 rootfs mutation 改变 reclaim 调度窗口后，RootFS 的 4 KiB read/write block scratch 改
  由串行文件系统实例持有；background reclaim 的大统计规划帧和通用 work 调度后处理也不再
  与设备提交嵌套，继续保留 16 KiB 动态 Kernel stack 与双 guard 规格。

## 不变量

- 相对路径必须拥有有效目录句柄；绝对路径与 `AT_CWD` 不读取无关 descriptor；
- 目录 fd close、duplicate、fork 或并发 syscall 不能让正在执行的 `*at` 看见悬空 payload；
- 已打开目录被 rename 后，旧句柄继续按 vnode 身份工作；被 rmdir 的目录仍受后端 busy/open
  约束；
- source/destination 的路径解析和 backend mutation 处于同一个 writer 临界区，提交前
  sequence 必须仍等于捕获值且为偶数；
- retain-release 差值等于 active，peak 不小于 active；计数溢出不得发布新句柄；
- reclaim 设备 I/O 不得继承 RootFS 4 KiB block、完整压力统计或 worker 后处理栈帧；ATA/NVMe
  pressure 都不得触碰 guard；
- 旧 1..87 编号、错误区间、`FileInformation`、rootfs v4 与磁盘格式保持不变。

## 验证

- ABI unit 冻结 88..96、四个请求结构、`AT_CWD` 和 flag mask；
- hosted VFS integration 覆盖绝对/相对路径、非法句柄、跨 parent rename、目录自身 rename 后
  旧句柄、并发不同名 create、并发同名 create 二次解析和完整 retain/release 守恒；
- Ring 3 `fs_probe` 在真实 rootfs 上执行 open/open-directory/mkdir/remove/stat/readlink/
  rename/link/symlink 的 `*at` 路径并核对数据、类型和符号链接目标；
- fresh CAW 的 focused、4 GiB ATA primary 和完整 verify 共同验收命名、构建、VGA 可见画面、
  文件持久路径与全系统资源归零。

## 后果

V2.13 建立了类似现代 Linux `dirfd + *at` 的基础路径能力，用户态可避免依赖可变 cwd，也为
后续 fd-based 权限和 namespace 演进提供稳定边界。当前不实现 `openat2` resolve flags、
`renameat2` 扩展 flag、目录能力沙箱、mount namespace、RCU/SMP 或动态目录句柄 slab；这些
都需要独立的安全和并发模型。

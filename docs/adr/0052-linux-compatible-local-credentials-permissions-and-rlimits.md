# ADR 0052：Linux 兼容的本地凭据、权限与资源限制

- 状态：Accepted（v2.4 本地候选）
- 日期：2026-08-18
- 影响：ABI、Process、VFS、rootfs v4、procfs/devfs、用户工具与整机验收

## 背景

v2.3 已冻结 128 GiB rootfs v4、链接、时间戳和 orphan 恢复，但所有进程仍隐式
拥有完全访问权，inode 也没有持久 owner/mode。继续自定义身份和权限位值会让用户
工具、错误行为和将来的程序移植形成第二套不必要的规则。

本阶段采用一条明确默认：项目规格没有更强约束时，使用 Linux/POSIX 的用户可见
语义、编号和默认值；不复制 Linux Kernel 源码，也不以 Linux 的 VFS、ext4、用户
空间或启动链替代项目实现。

## 决策

### 身份与 mode

UID、GID 和 mode 都使用 32 位无符号 ABI 字段。root UID/GID 为 0，`/dev` 的本地
tty 组使用 GID 5。mode 的类型、set-ID、sticky 和九个 rwx 位与 Linux UAPI 的
八进制值一致；默认 umask 为 `0022`，普通文件请求 `0666`，目录请求 `0777`，
符号链接固定为 `0777` 且不应用 umask。

进程凭据保存 real/effective/saved UID 与 GID，以及补充组。ABI 保留 Linux
`NGROUPS_MAX=65536` 的协议上限；当前固定内存 Kernel 每进程最多保存 32 个补充组，
超出时明确返回资源上限错误，不能截断。该 32 项是实现容量边界，不改变组匹配
语义。

`fork` 和普通 spawn 精确复制凭据、补充组、umask 和 rlimit。`exec` 保留它们；
若目标 mode 带 setuid/setgid，则只把 effective/saved ID 改为 inode owner，real ID
不变。失败的 exec 不改变任何凭据。

### VFS 访问检查

每个路径组件都要求当前目录的 execute/search 权限。最终对象和父目录按操作检查：

- 文件 open 按 requested read/write 检查，权限在成功 open 后随 FileDescription
  冻结；之后 chmod 不撤销既有 fd；
- exec 独立要求至少一个执行位，不把“Kernel 能读取 ELF”误当成用户有执行权；
- create、link、symlink、unlink、rmdir 和 rename 要求父目录 write+search；
- chdir 要求最终目录 search，目录枚举要求 read；truncate 要求文件 write；
- sticky 目录只允许 root、目录 owner 或目标 owner 删除/替换条目；
- setgid 目录让新节点继承组，新子目录继续继承 setgid；
- root 越过普通 DAC read/write；普通文件完全没有执行位时，root 也不能执行。

chmod 只允许 root 或 inode owner。chown 允许 root 选择任意 ID；非 root owner 只能
保持 UID 并把 GID 改为 effective/补充组中的值。chown 清除 setuid/setgid。

### rootfs v4 特性扩展

盘面 magic 和格式号继续是 `OSRFV004`。inode 预留区中的 200、204、208 字节偏移
分别保存 little-endian `uid32`、`gid32`、`mode32`，212..251 仍必须为零，CRC32
继续覆盖 0..251。superblock 增加 required feature bit `UNIX_METADATA=1<<8`。

因此这是 v4 的必需特性扩展，不是静默兼容：v2.3 的旧 v4 镜像缺少该位，会被
Kernel、mkfs/fsck 和宿主工具明确拒绝并要求重建。`/bin`、`/sbin` 的离线安装项
统一补齐三个执行位，包含故意损坏但必须进入 ELF 校验路径的测试样本。

### 资源限制

ABI 的资源编号精确采用 Linux `RLIMIT_*` 0..15，limit 使用两个 64 位字段，
infinity 为 `UINT64_MAX`。当前内核实际约束：

- `RLIMIT_FSIZE`：write 到边界时短写，边界后的写和超限 truncate 拒绝；
- `RLIMIT_DATA`：限制 program break；
- `RLIMIT_STACK`：限制按需栈增长，系统上限仍为 8 MiB；
- `RLIMIT_NPROC`：按 real UID 统计 spawn/fork；
- `RLIMIT_NOFILE`：与 FileTable soft limit 使用同一状态；
- `RLIMIT_AS`：限制新匿名/文件映射和下一次 exec 镜像；
- `RLIMIT_CORE`：固定为 0，因为系统没有 core dump。

其余 Linux 编号可查询稳定默认值，但对应设施尚不存在时不能伪称已执行限制。
非 root 可以降低 hard limit 或在 hard limit 内调整 soft limit，不能抬高 hard
limit；root 仍受项目的进程、fd、栈、堆和文件系统系统上限约束。

### 伪文件系统和用户接口

`/proc` 目录为 root:root `0555`，快照文件为 `0444`；`/dev` 目录为 root:root
`0755`，字符设备为 root:tty `0660`。生产 ABI 升为 v2.3.0，在 71 后追加
72..84 共 13 个身份、组、umask、chmod/chown、link/symlink/readlink 和 rlimit
调用。`FileInformation` 在原 96 字节尾部追加 mode/uid/gid/reserved，大小为
112 字节。

用户态新增 chmod、chown、ln、readlink；`id` 输出真实/有效 UID/GID 和组；
`stat` 输出 mode/uid/gid。umask 必须改变 Shell 自身，因而实现为 Shell builtin，
不能由一个立即退出的外部子进程伪装。

## 失败与安全边界

- user-copy 在取得 VFS/journal/backend 锁之前完成；双路径调用使用关闭抢占的 8 KiB
  单 BSP scratch，避免两条 PATH_MAX 缓冲压垮 16 KiB Kernel 栈；
- 权限失败不创建节点、不截断文件、不改变 owner/mode；rootfs chmod/chown 各自在
  单个 metadata journal 事务中更新 ctime；
- 新建 inode 的 owner/mode 与目录项在同一后端事务内发布，不采用“先创建、后
  chmod”的非原子补丁；
- 网络身份、密码数据库、登录、远程 shell、ACL、capabilities、LSM 和 user
  namespace 不进入 v2.4。

## 验证

- 纯凭据单元测试锁定 Linux mode 位值、owner/group/other、root execute 例外、
  umask、chown 和补充组容量；
- VFS 权限测试覆盖逐级 search、setgid 继承、sticky、fork context 和非 root
  chmod/chown；devfs/procfs 单元测试锁定 owner/mode；
- rootfs 格式、集成、重挂载、fsck 和高 LBA路径同时解码并校验 uid/gid/mode；
- Ring 3 安全探针真实执行组/身份切换、4 字节 `RLIMIT_FSIZE` 短写、fork 继承、
  清理和 setuid/setgid exec；functional QEMU 还实际运行 chmod/chown/ln/
  readlink/umask 命令链；
- caw 与手机 32 GiB/128 GiB 闭环仍由 v2.6 统一完成，本 ADR 只声明本地候选。

## 被拒绝的方案

### 自定义简化权限位

会让 stat、chmod、程序移植和错误预期产生第二套规则。拒绝。

### 把所有进程永久视为 root

只能显示元数据，无法证明访问边界、继承或失败原子性。拒绝。

### 在 VFS 创建后再补 owner/mode

第二步失败会留下错误权限的已发布 inode，也破坏 journal 原子性。拒绝。

### 引入 Linux Kernel、ext4 或宿主共享目录

会替代项目自研启动链、VFS 和 rootfs，不符合架构边界。拒绝。

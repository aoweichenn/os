# ADR 0080：V2.15 每 inode I/O 协调与页缓存提交边界

- 状态：Accepted（v2.15 工程候选）
- 日期：2026-08-24
- 影响：VFS、FileDescription、FilePageCache、共享 mmap、truncate、fsync/msync 与测试

## 背景

V2.14 为保证独立 open description 的 append 原子性，在整个 VFS 上使用一个
`RuntimeMutex` 串行“读取逻辑 EOF、按 RLIMIT_FSIZE 裁剪、写入”。这保证了正确性，
但两个无关 inode 的 append 也会互相等待。普通 buffered write、共享 mmap 首次写、
truncate 准备和 fsync 又分别位于 VFS、UserMemory 与 ProcessRuntime，无法给后续 extent
和 delayed allocation 提供一个稳定的 inode 级提交边界。

直接让 writeback 再取得同一个 inode mutex 不可行。truncate/fsync 会在持锁期间主动
等待并执行 writeback，而 writeback 最终通过 `WriteUncachedAt` 回到 VFS；递归取得非递归
`RuntimeMutex` 会死锁。后台页回写还必须允许在内存压力下独立前进。

## 决策

### 有界活跃 inode 槽

新增 `InodeIoCoordinator`。VFS 常驻 128 个槽，与 phone-primary 的 128 Thread 功能容量
一致；槽按 `{superblock id/generation, node id/generation}` 标识 inode。`Acquire` 在短
SpinLock 临界区中查找现有 identity 或选择零引用 LRU 槽，增加引用后才取得该槽独立的
`RuntimeMutex`。`Release` 先释放可睡眠 mutex，再减少槽引用。

槽 generation 防止复用后的旧 token 释放新 identity。全部槽被不同活跃 inode 引用时返回
`CapacityExhausted`，不回退到全局锁。统计分别记录 cached/referenced slot、活动引用、峰值、
identity reuse、LRU replacement 与容量拒绝；`Validate` 重算全部当前值并拒绝重复 identity。

### 前台 I/O 与后台 writeback 分工

`RegularFileIoGuard` 覆盖：

- `Vfs::Write`、`WriteAt` 和 `Append` 的逻辑 size、缓存脏化和 metadata 失效；
- path/open-file truncate 的准备、后端 size 提交和 cache truncate；
- writable shared mmap fault 的 `MarkDirty + PTE writable` 发布；
- fsync/fdatasync 与同步 msync 的映射写保护、范围 writeback 和最终 VFS sync。

append 在 guard 内读取缓存逻辑 EOF并调用不重复加锁的内部 write helper，旧全局 append
mutex 删除。同 identity 的独立 FileDescription 因而保持原子，不同 identity 使用不同槽。

后台 writeback 不取得 `RegularFileIoGuard`。它继续通过 FilePageCache 的 Dirty/Writeback/
Error、页 writeback waiter、`UserFileBackingManager` 引用和 rootfs metadata/backend lock
提交。前台 guard 禁止同 inode 在 truncate 准备期间新增 buffered/mmap 脏化；准备阶段先
写保护全部 shared PTE、等待既有 writeback，再提交 backend 和 cache size，因而不需要
writeback 递归进 guard。

### truncate 准备移入 VFS

ProcessRuntime 不再先 stat、在锁外调用 `PrepareRuntimeFileTruncate`，然后第二次进入 VFS。
VFS 新增一次性 truncate prepare hook；`TruncateNode` 取得 inode guard 后依次执行：

```text
cancel readahead / protect mappings / wait writeback
  → metadata shard
    → backend truncate
      → metadata cache invalidation
        → file page cache truncate
```

open(O_TRUNC)、path truncate 与 ftruncate 使用同一入口。prepare 失败时 backend size 和
cache 均不改变。

### 锁序

前台入口稳定顺序为：

```text
FileDescription operation lock → inode I/O guard          （fd write/ftruncate）
namespace mutation lock        → inode I/O guard          （open O_TRUNC）
inode I/O guard                → FilePageCache/backing    （buffered/mmap/prepare/sync）
inode I/O guard                → metadata shard → backend （uncached/truncate commit）

background writeback: UserFileBacking → metadata shard → backend
                      （永不取得 inode I/O guard）
```

fsync 先 retain 一个短期 OpenFile 后释放 FileDescription operation lock，再取得 inode guard；
guard 释放后才推进 FileDescription writeback-error cursor，避免形成
`FileDescription → inode` 与 `inode → FileDescription` 的锁环。user-copy 不进入上述锁。

## 失败边界

- 非法 identity、过期 token、引用/代次损坏：fail closed；
- 128 个不同 identity 同时活跃：返回容量错误，已有 guard 不受影响；
- truncate prepare EIO：不调用 backend/cache truncate；
- buffered write 部分成功：保持既有短写和逻辑 size 语义；
- writeback EIO：继续通过错误 sequence 向同期 fsync waiter 和打开实例报告；
- rootfs v4 后端实例锁本阶段保留；其 block-group 分片属于 rootfs v5，不伪报已经移除。

## 验证

- 单元测试覆盖非法初始化、identity/token、两槽耗尽、LRU 复用、同 inode 双线程串行和
  不同 inode 同时进入；
- 固定种子 `0x494E4F4445494F31` 执行十万步 acquire/release/reuse/reject oracle；
- VFS 集成测试让两个不同 inode 同时停在 cache callback，并证明同 inode write 在 truncate
  prepare 期间不能进入；prepare 故障后长度与 cache 计数保持不变；
- 既有双 FileDescription append、file-page writeback、mmap/fsync、Ring 3 `fs_probe`、
  4 GiB ATA/NVMe 与 persistence 继续作为跨层回归；
- NVMe persistence 强制保留 `SYSCALL_IRET_FALLBACKS>0`；返回前调度的 NativeSystemCall frame
  由 dispatcher 记录实际 IRET，不能以删除入口安全门禁消除时序失败。

## 后果

V2.15 删除了 VFS 全局 append 串行点，并把 extent/delayed allocation 需要的逻辑 inode
事务边界固定下来。代价是 VFS 常驻约百个可睡眠 mutex，后台 writeback 与前台 guard 仍是
两套互补机制；rootfs v5 必须继续保持本 ADR 的身份、锁序和失败语义。

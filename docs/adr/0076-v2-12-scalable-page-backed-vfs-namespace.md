# ADR 0076：V2.12 可扩展、页后备 VFS 命名空间

状态：已接受

日期：2026-08-23

## 问题

V2.11 已经具备正确的 dentry/inode cache，但全局 resolution/metadata 事务会串行化无关路径，
固定 BSS backing 也只能回收逻辑条目，不能向物理页分配器归还内存。rename/unlink 与并发
path walk 还需要一个不依赖全局路径锁的一致性判据。

## 决策

- path component miss 按完整 dentry key 分到 64 个 `RuntimeMutex`；同 shard 的 owner/waiter
  合并 fill，不同 shard 可并行进入 backend；inode metadata 同样按 identity 使用 64 分片；
- 128 个页后备 `VfsResolutionContext` 为并发 path walk 提供独立的双 4096 字节 scratch；未配置
  pool 的 host 对照仍保留单上下文 fallback；
- create/link/rename/remove/symlink/mount commit 由单写 mutation mutex 包围，并将 namespace
  sequence 从偶数推进到奇数再回到偶数；resolver 只接受起止相同的偶数序列，否则清理
  dentry 快照并有界重试；
- `VfsNamespaceBackingLayout` 用检查溢出的对齐运算，把 dentry/inode slot、hash entry、compact
  bucket 和 resolution context 放入稳定真实页；preferred 8192/4096 bucket 使用第二个页分配；
- 首次 pressure shrink 在回收零引用条目后在线重建到 4096/2048 compact bucket，再释放
  preferred 页；资源账本记录两次分配前后的 frame、buddy、KVA 页/描述符差值，不按数组大小
  猜测物理占用；
- namespace 的不可回收稳定页从用户 resident budget 中扣除；preferred 页释放时同步缩小该
  扣除，因而 9216 页压力规格保持原义，完整内核资源快照仍要求归一化后零差异。

## 不变量

- 同 key/shard miss 最多一个 backend owner；不同 shard 和不同 inode metadata miss 可并行；
- metadata 多对象写操作按 shard 编号升序加锁、逆序释放，不形成 ABBA；
- resolver 不返回跨过已提交 mutation 的旧结果，最多重试八次，无法稳定则返回 `Busy`；
- hash rebuild 前完整预检 slot/index，重建后每个 Cached 槽恰位于一个正确 bucket；
- preferred backing 只有在 cache 指针切换到 compact bucket 后才能释放，且只释放一次；
- user pressure 排除数始终不大于真实 allocated frame，释放页与排除数同步减少；
- 热路径不分配、不逐 lookup/stat/锁打印，IRQ 路径不进入 VFS。

## 验证

- layout unit 覆盖对齐、页取整、溢出、错位、空和不足 backing；cache unit 覆盖在线 hash
  rebuild；
- hosted integration 用八线程证明同 key 合并、不同 shard 并行、sequence 跨 mutation 重试，
  并验证 compact rebuild 与 release callback；四线程 metadata test 证明不同 inode 并行；
- 既有十万步 namespace oracle、命名门禁、4 GiB ATA primary 和 9216 页 reclaim pressure
  共同验证语义、可见画面、资源账本与真实页释放。

## 后果

V2.12 去除了 path/metadata 读侧的全局瓶颈，并让 namespace cache 第一次拥有真实页生命周期。
单写 namespace mutation、固定 64 shard/128 context、单 BSP 和固定 slot 上限仍是当前边界；
动态 slab、RCU walk、SMP、mount namespace 与网络不在本阶段。

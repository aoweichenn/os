# ADR 0073：V2.11 inode metadata 加载与失效事务

状态：已接受（第二增量）

日期：2026-08-23

## 问题

V2.11.1 已冻结 inode identity 和生命周期，但 `stat`、权限检查、`open`、`exec` 与打开文件
查询仍分别调用 backend `stat`。直接把最后一次 `BackendNodeInformation` 放入 inode slot
会产生两个问题：重复读取没有消除；chmod/write/rename 等修改与正在进行的 backend 读取
交错时，失效前读到的旧结果可能在失效后迟到提交，重新污染缓存。

缓存是加速层。容量耗尽、同 identity 已有加载者或 generation 耗尽时，VFS 必须仍能直接
读取 backend 并返回正确结果，不能把内部缓存状态变成新的用户可见失败。

## 决策

`VfsInodeSlot` 增加原始 `BackendNodeInformation`、独立 metadata generation 和三态：

```text
Empty --Prepare(owner ticket)--> Loading --Complete(ticket)--> Ready
  ^                                  |                            |
  +------------- Cancel -------------+---------- Invalidate ------+
```

load ticket 同时携带 inode slot/generation 与 metadata generation。`CompleteInodeMetadata`
只有在 inode 仍为 Cached、metadata 仍为 Loading 且两级 generation 都匹配时才提交；因此
失效、整 inode 撤销或槽复用之后到达的旧 backend 结果只能得到 `InvalidToken`。

第一个 miss 成为 owner 并在 cache lock 外调用 backend `stat`。顺序调用命中 Ready 后复制
快照，不取得长生命周期引用。并发看到 Loading 的调用者本增量直接旁路 backend，结果只
返回给自己且不能提交；容量、generation 或统计 counter 耗尽也采用相同旁路。并发 miss
等待与广播留给后续专门的协调器，不在 spin lock 内执行 I/O 或睡眠。

缓存保存 backend 原始字段：size、allocated size、link count、四类时间、uid、gid 和 mode。
FilePageCache 的逻辑 size 仍在每次 `Stat`/`StatOpenFile` 返回前覆盖，不能写回 inode metadata
slot，否则页缓存变化会绕过 generation 事务。

VFS 通过 `ConfigureNamespaceCache` 接入缓存。生产内核在 BSS 中固定提供 4096 个 dentry
slot 和 2048 个 inode slot；初始化会逐槽写零，路径热区不分配。当前共享范围包括 `Stat`、
`CheckAccess`、普通打开、目录打开、`OpenExecutable`、sticky/创建权限与 `StatOpenFile`；
`StatOpenFileUncached` 保持 backend 旁路，供页缓存和文件后备层避免递归。

成功修改后按 identity 失效：

- create/symlink 失效 parent；
- chmod/chown/write/truncate 失效目标；
- link 失效 source 与 destination parent；
- rename 失效 source、已存在 destination 和两个 parent；
- unlink/rmdir 失效 target 与 parent。

当前 backend read 不更新 atime；将来若启用 atime/relatime，read 成功路径必须同步刷新或
失效 metadata。本增量不接 Positive/Negative dentry lookup，不新增来宾 marker。

## 不变量

- Loading/Ready metadata 只属于 Cached inode，Stale inode 的 metadata 必须为 Empty；
- Empty/Loading 不得保留字段内容，Ready 的 mode 类型必须与 inode `NodeType` 一致；
- Loading 不能被 LRU 回收，Ready 只能随零引用 inode 回收；
- 同一个 ticket 最多完成或取消一次，失效后的迟到 completion 永远不能恢复 Ready；
- backend 失败必须取消 owner；取消或 metadata-only 失效后，零 dentry/external 引用 inode
  立即释放；
- cache hit、旁路或填充都必须先验证 backend mode 与 vnode 类型一致；
- mutation 只有 backend 已成功后才失效，backend 失败不得丢弃仍有效的缓存；
- 缓存不可用只能降低命中率，不得改变 backend 成功、EIO、权限或 NotFound 结果。

## 验证

- unit 覆盖 owner、Loading 冲突、非法 metadata、完成、命中、失效、取消、容量、LRU、
  dentry 保留和两级 ABA；
- production integration 用可计数 backend 验证 stat/access/exec/open-file 共享一次 fill，
  并逐项验证 chmod/write/truncate/link/rename/unlink 失效及 Loading/容量旁路；
- 固定种子 `0x494E4F44454D4554` 执行十万轮 load、竞争、完成、取消、失效和槽复用，每轮
  要求 active inode 归零并调用 `Validate`；
- 4 GiB QEMU primary 与最终 ATA/NVMe、reclaim、OOM、错误恢复、持久化矩阵继续验证生产
  接线不改变现有系统行为。

## 后果

重复 metadata 消费已经进入生产缓存，但路径组件 lookup 仍直接访问 backend；因此 11.2
减少的是同 vnode 的重复 `stat`，不是 dentry lookup。后续增量已复用同一 inode identity
与失效接口接入 Positive/Negative dentry、同组件 miss、hash 与 shrinker，详见 ADR 0074/
0075。

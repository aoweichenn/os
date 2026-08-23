# ADR 0074：V2.11 生产 dentry lookup 与命名空间修改事务

状态：已接受（第三至第五增量）

日期：2026-08-23

## 问题

11.1 的 Positive/Negative/Stale 模型尚未进入 path walk；11.2 只减少同 vnode 的重复
backend stat。若直接在 `ResolveInternal` 命中 dentry，却不同时冻结 miss owner、错误分类和
mutation 失效，Negative 会掩盖 EIO，rename/unlink 后也可能继续返回旧 vnode。

## 决策

`LookupChild` 成为路径组件的唯一生产入口。key 仍为 mount identifier、parent inode
identity 和完整名称。Positive 快照重建同 superblock 的 vnode；Negative 只返回 NotFound。
miss 才调用 backend lookup，Succeeded 发布 Positive，只有明确 NotFound 发布 Negative；
DeviceFailure、Corrupt、PermissionDenied 等错误从不缓存。

现有 `resolution_lock_` 覆盖整个 `ResolveInternal`，因此同一 key 的第一个 miss 是唯一 owner，
backend 返回并发布前第二个 resolver 不能进入；hosted 八线程测试要求 backend lookup 恰为
一次。缓存容量、generation 或统计耗尽只旁路发布并返回 backend 原结果。

命名空间修改在 `resolution_lock_ -> metadata_lock_` 顺序下提交 backend 与缓存失效：

- create/mkdir/symlink：目标 key 与 parent metadata；
- link：destination key、source metadata 与 destination parent metadata；
- rename：source/destination key、source/已有 destination 与两个 parent metadata；
- unlink/rmdir：目标 key、target 与 parent metadata；rmdir 另级联清除旧目录 identity 的 child；
- chmod/chown/write/truncate：只修改 target metadata。

mount crossing 仍在 dentry 命中后执行 `FollowMounts`。key 含 mount identifier，所以相同
parent/name 在不同 mount 隔离；cwd/root 保存打开的 Path，不依赖缓存槽。符号链接 dentry
可以命中，但 target 内容每次仍从 backend 读取并继续执行 40 次上限与绝对/相对规则。

## 不变量

- Cached key 最多一个，Stale 不参与 lookup；
- Positive inode identity/type 必须匹配当前 superblock，Negative 不持 inode；
- 只有 NotFound 可发布 Negative，任何其他 backend failure 必须在下一次继续访问 backend；
- backend mutation 成功到 dentry/metadata 失效之间不存在 resolver 可见窗口；
- mutation 失败不伪造成功，也不发布新 dentry；
- cache publication/reclaim 失败不能改写已经得到的合法 backend 结果，内部 Corrupt 除外；
- mount、cwd/root、symlink、DAC、orphan 与打开文件 generation 语义不得因 cache 改变。

## 验证

- production integration 计数 Positive/Negative/backend error、create、rename、remove；
- 八个 hosted Thread 同时解析同一冷 key，只允许一次 backend lookup；
- 十万步 memfs/rootfs 独立 namespace oracle 同时启用生产 cache 与 hash；
- 4 GiB QEMU 的工具、权限、链接、符号链接、mount、cwd、exec、reclaim、OOM 与 persistence
  继续使用真实 rootfs v4 和 ATA/NVMe。

## 后果

V2.11 已减少重复 component lookup。并发合并当前依赖 VFS 全局 resolution transaction；未来
若拆为 per-directory 锁，必须保留同 key 单 owner 合同并引入显式 waiter，不能退回重复 I/O。

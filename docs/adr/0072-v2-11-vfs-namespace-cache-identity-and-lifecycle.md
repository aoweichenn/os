# ADR 0072：V2.11 VFS 命名空间缓存身份与生命周期

状态：已接受（第一增量）

日期：2026-08-23

## 问题

V2.10 已让文件数据页、预读和写回共享异步 I/O 生命周期，但 VFS 每次解析路径组件仍直接
调用后端 lookup。下一阶段要增加 dentry/inode metadata cache，首先必须冻结身份、引用、
失效和回收语义，否则 create/rename/unlink、mount crossing 或 inode number 复用会把旧名字
解析为新对象。

名称不能只保存 hash。不同 mount 可以从相同 parent vnode 观察同名组件；同一 superblock
也可能通过不同路径引用同一个 inode。Negative 命中与后端 EIO 还必须区分，不能把设备失败
长期缓存成“不存在”。

## 决策

新增不接生产 VFS 的固定容量纯状态模块 `VfsNamespaceCache`。dentry key 为：

```text
mount identifier
+ parent {superblock identifier/generation, node identifier/generation}
+ exact name length and all name bytes
```

mount 只属于 dentry key；inode identity 不包含 mount，因此同一 superblock/node generation
可被多个 mount 的正 dentry 共享。名称长度固定为 1..255，拒绝控制字符、DEL、`/`、`.` 和
`..`，比较时先核对长度再比较全部字节。11.1 使用线性扫描验证语义，不把它宣称为生产性能
结构；后续生产接线会增加 hash bucket，而不会改变 key。

dentry 具有 Positive/Negative kind 和 Free/Cached/Stale state。Positive 保存 inode
slot+generation token，Negative 不持有 inode。失效的 Cached 项转成 Stale，从新 lookup 中
立即消失；若仍有外部引用则保留旧 identity 和结果，最后 release 才回收槽。这样旧 token
可与同 key 的新 Cached 项并存，槽复用后 generation 拒绝 ABA。

inode 具有 Free/Cached/Stale state，分别统计正 dentry 引用和外部引用。inode 失效会级联：

- 所有以它为目标的 Cached Positive dentry 变成 Stale；
- 所有以它为 parent 的 Cached Positive/Negative dentry 变成 Stale；
- 零外部引用 dentry 立即释放并归还 inode dentry reference；
- Stale inode 等 dentry/external 两类引用都归零后才释放。

Acquire 更新单调 access generation 并取得显式引用。dentry LRU 只选择 Cached 且零外部引用
项；inode LRU 只选择 Cached、零外部引用且零 dentry 引用项。淘汰 dentry 后 inode 可以继续
作为 metadata cache 留存。所有存储由调用方提供，热路径不分配、不访问后端、不进入
RuntimeMutex、设备或用户内存。

## 不变量

- 同一时刻同一个完整 dentry key 最多有一个 Cached 项，Stale 旧项可以并存；
- 同一 inode identity 最多有一个 Cached 项，Stale 旧 generation 可以并存；
- 每个非 Free Positive dentry 的 inode token 必须有效，Negative token 必须为空；
- Cached Positive 只能引用 Cached inode；Stale dentry 可以继续引用 Cached 或 Stale inode；
- inode `dentry_reference_count` 必须等于所有有效 Positive dentry token 的重算结果；
- Stale dentry 必须有外部引用，Stale inode 必须有 dentry 或外部引用；零引用 Stale 不留存；
- inode 变 Stale 后，不得保留指向它或以它为 parent 的 Cached dentry；
- LRU 不回收任何被引用项，generation 复用不能让旧 token 重新有效；
- 非法 key、类型冲突、容量拒绝和 generation/counter 耗尽不得发布半条目。

## 验证

- unit 覆盖正负发布、alias 共享 inode、完成引用、Stale 并存、父级级联、容量失败、255 字节
  名称、非法名称、类型冲突、LRU 和 ABA；
- integration 交错两个 mount，并模拟 rename 的“旧正项 Stale、新旧名称正负发布”顺序；
- 固定种子 `0x44454E5452594C52` 执行十万轮正负发布、0..3 引用、dentry/inode 失效、替换、
  乱序释放和两级回收，每轮要求缓存归零并调用 `Validate`；
- 11.1 不新增 QEMU marker；4 GiB ATA/NVMe 全量回归只证明未接生产路径时行为保持不变。

## 后果

后续增量可以在不重新定义身份和资源所有权的前提下加入 inode metadata、positive/negative
生产 lookup、miss 合并、命名空间修改失效和压力回收。11.1 自身没有减少任何一次后端
lookup，也不缓存 stat 字段；11.2 已在不改变本 ADR identity/lifecycle 的前提下接入 inode
metadata，详见 [ADR 0073](0073-v2-11-inode-metadata-load-and-invalidation.md)。纯模型统计仍不
写入来宾终端。

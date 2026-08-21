# ADR 0056：v2.8 动态文件缓存地址空间与稀疏页索引

## 状态

已接受；动态索引、rootfs/legacy buffered read/write、file fault、writable shared 和
truncate 一致性已实现；后台写回与按打开实例同步尚未迁移。

## 问题

现有 `FilePageCache` 由调用方提供 256 至 4096 项固定数组。按
`(FileIdentity, page index)` 查找、选择 LRU 和校验唯一性都需要扫描整个数组；
4 GiB 参考机也只能缓存 16 MiB 文件数据。普通 VFS read/write 仍进入 rootfs
八项 `BlockCache`，文件页 fault 则进入另一套缓存，因此二者不能自然共享页面、
Dirty 状态和回收顺序。

直接扩大固定数组会线性增加 Kernel BSS，仍不能处理稀疏大文件的 64 位页索引，
也不能快速定位 Dirty/Writeback/Error 页面。直接照搬 Linux XArray 又会提前引入
RCU、slab、SMP 原子与指针编码；这些机制超出当前单 BSP 阶段。

## 决策

### 稳定文件身份

`FileCacheIdentity` 统一保存 superblock identifier/generation 和 node
identifier/generation。旧 `FileIdentity` 保留为该类型的兼容别名，既有
`FilePageCache` 行为不变；新旧路径共享同一身份判定函数，避免迁移期间出现两种
有效性规则。

### 项目自研稀疏索引

`SparsePageIndex` 使用 64 路 radix tree：

- 每层解释 6 个 page-index bit；
- level 0 是叶节点，level 10 覆盖 64 位最高四位；
- 节点从 `KernelHeap` 按需申请，不为未出现的页或子树预留存储；
- 每个节点分别聚合 Present、Dirty、Writeback、Error 64 位 bitmap；
- Lookup 只沿最多 11 个节点前进，标记查找跳过没有对应 bitmap 的子树；
- 删除最后一个叶项时自底向上释放空分支，并在高层只剩 slot 0 时收缩 root。

插入先计算 root growth 和缺失分支需要的全部节点。所有申请成功后才连接现有树并
发布叶项；中途容量不足会逆序释放未发布节点，原 entry/node/root 计数保持不变。
累计失败与回滚计数只记录已经发生的尝试，不属于可见索引内容。

### 文件缓存地址空间

`FileCacheAddressSpace` 对应一个稳定文件身份，拥有动态页面元数据但不拥有物理页帧。
每个页面保存 64 位 page index、物理地址、映射引用、访问代次和状态。首个增量冻结
以下状态转换：

```text
Clean -> Dirty -> Writeback -> Clean
                       `-----> Error -> Dirty / Writeback
```

Dirty/Error 页面不得直接删除，Writeback 或有映射引用的页面保持 Busy。引用上溢、
下溢、物理地址不匹配和非法状态转换都在修改前失败。地址空间锁、索引锁与堆锁的
顺序固定为 `address space -> sparse index -> KernelHeap`；持锁路径不进入 VFS、
块设备、用户复制或调度。

### 迁移边界

第一增量只增加基础结构与目标自检。第二增量把 `FilePageCache` 改为动态文件身份
注册表，每个文件拥有一个 `FileCacheAddressSpace`；VFS rootfs/legacy 普通读取与
ELF/file fault 共用该缓存。填页必须调用 `ReadUncachedAt`，禁止 VFS cache hook
递归。memfs、procfs 和 devfs superblock 不声明普通文件读缓存能力，动态快照保持
直读。

第三增量把 capability 扩展为 read/write/size/truncate 四个 hook。普通写先保留按
FileIdentity 去重的 VFS open reference，再取得唯一缓存 frame、标记 Dirty、复制字节
并发布逻辑 EOF；公共 read、shared PTE 与后续 write 都观察该 frame。writeback 只走
`WriteUncachedAt`，末页长度受逻辑 EOF 限制，原 fd 关闭或 unlink 不会让脏页失去 inode。

truncate 先撤销文件偏移不小于新 EOF 的驻留映射；后端事务成功后，缓存允许显式
Discard 范围外 Clean/Dirty/Error 页，拒绝 Writeback/活动引用，并清零保留尾页。
增长不申请 frame，同时把已驻留的旧 EOF 到新 EOF 区间恢复为零。普通 write 不再
revoke/invalidate，`MAP_PRIVATE` 仍使用私有 COW frame。后续增量继续实现后台
writeback、按打开实例同步错误和统一内存回收。最终 payload 校验产生的 clean 页必须
在 storage shutdown 前 drain，确保 NVMe frame 基线仍能检测真实泄漏。

设计参考 Linux `address_space` 对页缓存、Dirty/Writeback 标记和内存压力的职责，
以及 XArray 对稀疏整数索引和 marks 的组织方式；实现不复制 Linux 源码，也不承诺
当前不存在的 RCU/SMP 语义：

- <https://docs.kernel.org/filesystems/vfs.html#the-address-space-object>
- <https://docs.kernel.org/core-api/xarray.html>

## 失败语义

- 非法文件身份、未对齐物理地址或无效初始状态：不申请元数据；
- 重复页：返回 `AlreadyExists`，原页面和 heap 统计不变；
- 节点或页面申请失败：释放本事务全部新对象，不发布部分路径；
- 通用 Remove 对引用不为零、Dirty/Error/Writeback 页面拒绝删除；
- truncate Discard 只接受零引用且非 Writeback 页面；预检失败不得先丢页，尾页清零后
  若底层 frame/metadata 释放失败则返回终止性错误；
- buffered write 在 Dirty limit 或 retained open 申请失败前不得修改页内字节；跨页
  失败只返回已经复制并发布长度的完整前缀；
- 写回失败把页面保留为 Error 并保留 writeback open reference，后续 sync 可重试；
- 堆释放或内部 bitmap/计数不一致：返回终止性错误，不继续伪装为可用缓存；
- `Destroy` 只接受零页面、零引用和零 radix 节点的地址空间。

## 测试

- 单元测试覆盖 0、63、64、4095、4096、`1 << 42`、`UINT64_MAX`、重复插入、三种
  mark、范围查找、空分支裁剪、状态转换、引用边界和 2 KiB 堆回滚；
- 生命周期集成测试动态插入并回收 8192 页，超过旧 4096 项上限；
- 固定种子 `0x5632385041474543` 执行十万步 insert/retain/release/transition/
  remove/lookup/find，并逐步对照有序参考模型；
- Kernel 启动自检执行相同高索引、状态重试和真实小堆失败路径，成功后只输出一次
  `FILE_CACHE_INDEX_SELF_TEST_PASSED`；
- 动态 `FilePageCache` 生命周期真实持有 8192 页，VFS 单元测试区分公共 cache hook、
  `ReadUncachedAt` 与 superblock 禁止缓存三条路径；
- 当前 QEMU 门禁只使用 4 GiB，要求 8192 页容量、32 MiB 专用 metadata arena，
  并验证 buffered read/write、shared cache hit 和 truncate 次数均非零；
- 第三增量单元测试覆盖 busy truncate、脏页丢弃、尾页清零、VFS 四 hook 与 uncached
  bypass；十万步 FilePageCache 随机模型加入 grow/shrink；QEMU memory probe 验证
  buffered write 与两个 shared alias 双向可见、private 隔离和 shrink/grow 零区间；
- 第一增量仍要求全部既有 ATA/NVMe、VFS、page cache、持久化与故障回归通过。

## 后果

页缓存元数据不再按最大容量进入 BSS。生产缓存从 buddy 取得按内存档位固定的连续
物理块，在 direct map 上建立专用 `KernelHeap`；该 block 是 ProcessRuntime 基线前
取得的持久内核资源。64 MiB、256 MiB、4 GiB 分别使用 2、8、32 MiB arena 和
256、4096、8192 页容量；当前自动验收只覆盖 4 GiB/8192 页分支。

代价是当前 cache miss 仍在全局 cache lock 内执行同步填页，地址空间注册表和
writeback backing 表也仍是有界线性表；单 BSP 保持唯一页，但还没有 Loading wait、
哈希化 mapping 表、异步 readahead 或后台 dirty throttling。这些并发与吞吐问题在
第四增量解决。

# ADR 0053：单域内存压力、稀疏 swap、overcommit 与 OOM

状态：已接受，本地候选实现中
日期：2026-08-20

## 背景

参考机向来宾提供 32768 MiB RAM，但手机宿主只有约 15.5 GiB 物理内存。QEMU
可以惰性建立 32 GiB 来宾地址空间；如果来宾一直按“尚有空闲页帧”分配并触碰
页面，宿主会在来宾到达自身低水位以前先发生换页风暴或 OOM。v2.4 已具备 buddy、
按需 VMA、COW、clean page cache、rootfs v4 和进程资源限制，v2.5 需要把这些
机制接成一条可恢复的内存压力路径。

Linux 对外公开 `min/low/high` 水位、`overcommit_memory=0/1/2`、默认 50% 的
严格 overcommit ratio、0..200 的 swappiness，以及按内存占用和
`oom_score_adj` 计算的 OOM 分数。本项目采用这些用户可识别的编号、默认值和
失败方向，算法与数据结构仍由项目自行实现。依据为 Linux Kernel 的
[VM sysctl 文档](https://docs.kernel.org/admin-guide/sysctl/vm.html)和
[/proc OOM 分数说明](https://docs.kernel.org/filesystems/proc.html)。

## 决策

### 一个 Normal 域与宿主驻留预算

v2.5 仍是单 BSP、无 NUMA，因此不伪造 DMA/Normal/Movable 多 zone。全部可管理
页帧属于一个 Normal 域。水位按驻留预算而不是来宾标称 RAM 计算：

```text
min  = clamp(2 * floor(sqrt(resident_limit_pages)), 256, 65536)
gap  = max(min / 4, resident_limit_pages * 10 / 10000, 1)
low  = min + gap
high = low + gap
```

32 GiB 参考机的宿主驻留预算固定为 1048576 页，即 4 GiB；64 MiB 和 256 MiB
兼容档使用各自全部可管理页。来宾仍报告 32 GiB RAM，4 GiB 只限制已触碰、可归
因于页帧分配器的驻留工作集。用户分配若会低于 low，先同步回收到 high；内核
分配可使用 low 到 min 的紧急保留；回收自身不递归触发普通回收。

### 回收顺序

回收先释放零映射引用的 clean file page，再安排脏文件页回写，最后把匿名、
program-break 和用户栈页写入 swap。COW 页在仍共享时不换出；fork 遇到已换出
的独占页时复制逻辑 swap 槽，父子后续分别换入，避免破坏私有写语义。

一次直接回收最多扫描 65536 个虚拟页，地址空间保存下一次扫描游标。热路径只
更新计数；不逐页写日志。页故障重试最多执行一次 OOM 回收，不能无限循环。

### 项目自有 swap 文件

rootfs 根目录保存 `/.os-swap`，owner 为 root:root，mode 为 0600，逻辑大小
268435456 字节。文件通过 sparse truncate 建立，只有实际换出槽分配 rootfs 数据
块；不引入分区表、Linux swap 格式或外部启动组件。交换区包含 65536 个 4 KiB
槽，映射键为 `{address_space_identifier, virtual_page}`。

每个槽保存 64 位 FNV-1a 校验。换出事务顺序固定为：

```text
write full swap slot -> publish slot metadata -> unmap PTE -> release physical frame
```

换入只有在整页读满且校验一致后才释放槽。短写、短读、设备错误或校验失败都
保留唯一 swap 映射；新分配页和临时 PTE 逆序回滚。unmap、exec、exit 释放未换入
槽。swap 不用于休眠恢复，重启时旧内容全部视为无效。

### overcommit

模式编号固定为 Linux 的 0、1、2：heuristic、always、never。默认模式 0 的
明显超额上限为 RAM + swap 减管理员保留；模式 1 只受 64 位计数溢出约束；模式
2 的上限为 swap + 50% RAM，再扣保留。普通匿名 mmap、brk 和 fork 预先提交，
失败返回 out-of-memory；unmap、缩小 brk、失败映射、exec 和 exit 成对撤销。

管理员保留为 `min(3% managed pages, 2048 pages)`，即最多 8 MiB。模式 2 的
用户恢复保留由策略结构显式保存；默认模式 0 不额外扣 128 MiB 用户保留。

### OOM

候选分数把 `resident_pages + swapped_pages` 相对驻留预算映射到 0..1000，再加
-1000..1000 的 adjustment。-1000、PID 1 和显式 protected 进程不可杀。相同
分数依次选择占用更大、PID 更小的进程，使固定输入得到固定牺牲者。

直接回收和 swap 仍不能满足 fault 时，ProcessRuntime 扫描 Alive 进程。非当前
牺牲者以 SIGKILL 语义关闭描述符、取消 futex、退出进程树、终止 Ready/Blocked
线程并销毁地址空间，然后只重试一次原 fault；当前进程被选中时走同一 SIGKILL
退出语义。PID 1 耗尽且无候选时保留原故障隔离，不 panic 整个内核。

## 不变量

- `free + allocated + reserved <= managed`；驻留计数不超过宿主预算。
- `active_swap + free_swap == swap_capacity`，同一地址空间虚拟页最多一个槽。
- swap 写失败前不撤 PTE；读/校验失败不释放槽。
- 全局 committed 等于各活动地址空间 committed 之和；失败事务必须撤销 charge。
- 正常整机结束时 committed、active swap、活动 COW 引用和 VMA 描述符均为零。
- panic 不读取 swap、不获取 VFS 锁、不分配内存。

## 后果与边界

正面影响是 32 GiB 来宾不再要求手机宿主物化同等 RSS，文件缓存、匿名页、swap、
OOM 和进程退出形成可观测闭环。代价是 v2.5 增加约 2 MiB 固定 swap 元数据，
启动链需要清零更多 BSS；QEMU TCG 的有界总超时必须保留合理裕量。

v2.5 不加入 NUMA、SMP reclaim worker、THP、zswap、swap readahead、mlock、
memory cgroup、可写 proc sysctl 或休眠恢复。swappiness 和 overcommit 模式先由
内核策略固定并由纯逻辑测试覆盖，用户可写调优接口留给后续版本。

# ADR 0053：4 GiB 实体内存、独立交换盘、overcommit 与 OOM

状态：已接受，候选实现中
日期：2026-08-20

## 背景

手机宿主约有 15.5 GiB 物理内存。旧参考机让 QEMU 暴露 32 GiB RAM，再用
4 GiB 内核驻留预算阻止页面全部物化；这能验证大地址，却不能证明 32 GiB
物理内存真实存在。用户要求内存和文件系统都不依赖稀疏容量，因此参考机改为
QEMU 启动时实际预分配的 4 GiB RAM，并用磁盘交换空间补足可分页容量。

Linux 对外公开 `min/low/high` 水位、`overcommit_memory=0/1/2`、默认 50% 的
严格 overcommit ratio、0..200 的 swappiness，以及按内存占用和
`oom_score_adj` 计算的 OOM 分数。本项目采用这些用户可识别的编号、默认值和
失败方向，算法、磁盘格式和驱动仍由项目自行实现。依据为 Linux Kernel 的
[VM sysctl 文档](https://docs.kernel.org/admin-guide/sysctl/vm.html)和
[/proc OOM 分数说明](https://docs.kernel.org/filesystems/proc.html)。

## 决策

### 4 GiB 客体物理内存

手机主规格固定为 4096 MiB，QEMU 命令必须带 `-mem-prealloc`。这会在进入自研
ROM 前触碰客体 RAM 的宿主后备页，避免仅建立 4 GiB 虚拟地址范围。QEMU PC
把 PCI hole 中的 RAM 重映射到 4 GiB 以上，因此内核仍必须分配、写回并释放
高于 `0x100000000` 的页帧。

v2.5 仍是单 BSP、无 NUMA，全部可管理页帧属于一个 Normal 域。水位按实际
1048576 页物理内存计算：

```text
min  = clamp(2 * floor(sqrt(resident_limit_pages)), 256, 65536)
gap  = max(min / 4, resident_limit_pages * 10 / 10000, 1)
low  = min + gap
high = low + gap
```

4 GiB 档得到 `min=2048, low=3096, high=4144`。用户分配若会低于 low，先同步
回收到 high；内核分配可使用 low 到 min 的紧急保留。64 MiB 和 256 MiB
测试档继续按各自真实 RAM 计算，不切换实现。

### 回收顺序

回收先释放零映射引用的 clean file page，再安排脏文件页回写，最后把匿名、
program-break 和用户栈页写入 swap。COW 页在仍共享时不换出；fork 遇到已换出
的独占页时复制逻辑 swap 槽，父子后续分别换入。

一次直接回收最多扫描 65536 个虚拟页，地址空间保存下一次扫描游标。热路径只
更新计数；不逐页写日志。页故障重试最多执行一次 OOM 回收。

### 28 GiB 独立交换盘

交换空间不再是 rootfs 中的稀疏文件。QEMU 把交换盘挂到 secondary IDE master；
项目 ATA PIO 驱动使用 `0x170..0x177` 和 `0x376` 轮询访问。交换盘提供
7340032 个 4 KiB 数据槽，即 28 GiB 可用交换数据；每槽另有 64 字节持久元数据，
其中页面校验和与元数据校验和相互独立。加 4 KiB superblock 后镜像长度为
30534537216 字节。

元数据项保存代次、地址空间标识、虚拟页和 64 位 FNV-1a 校验。槽以
`{address_space_identifier, virtual_page}` 的 FNV-1a 哈希为起点进行开放寻址；
删除写 tombstone，查找遇到本代从未使用的项才停止。启动只递增并提交 superblock
代次，旧代元数据立即失效，不扫描 28 GiB 数据区，也不在 Kernel BSS 建立
7340032 项数组。

换出事务顺序固定为：

```text
write 4 KiB data and flush
  -> publish metadata and flush
  -> unmap PTE
  -> release physical frame
```

换入只有在整页读满且校验一致、tombstone 已落盘后才释放槽。短写、短读、设备
错误、元数据提交失败或校验失败都保留唯一映射；新 frame/PTE 逆序回滚。
unmap、exec、exit 释放未换入槽。交换盘不用于休眠恢复。

### 工程镜像与实际运行镜像

构建和故障矩阵使用稀疏工程镜像，避免十余个故障副本各占 128 GiB；这些不是
手机运行产物。`tools/os.py materialize-image` 对唯一 rootfs 盘和交换盘执行
`posix_fallocate`，并以 `st_blocks * 512 >= st_size` 拒绝残留宿主空洞。
`qemu-display` 默认只接受这两个已物化镜像；显式
`--allow-sparse-engineering-images` 仅供调试。

### overcommit 与 OOM

模式编号固定为 Linux 的 0、1、2：heuristic、always、never。默认模式 0 的
明显超额上限为 RAM + swap 减管理员保留；4 GiB RAM 与 28 GiB swap 因而提供
约 32 GiB 可分页提交容量。模式 1 只受计数溢出约束；模式 2 的上限为 swap +
50% RAM，再扣保留。匿名 mmap、brk 和 fork 预先提交，失败返回 out-of-memory；
unmap、缩小 brk、失败映射、exec 和 exit 成对撤销。

OOM 候选分数把 `resident_pages + swapped_pages` 映射到 0..1000，再加
-1000..1000 adjustment。-1000、PID 1 和显式 protected 进程不可杀。相同分数
依次选择占用更大、PID 更小的进程。牺牲者完整关闭描述符、取消 futex、退出
进程树并销毁地址空间，原 fault 最多重试一次；没有候选时隔离原故障，不 panic。

## 不变量

- `free + allocated + reserved <= managed`；主规格 managed RAM 精确为 4 GiB。
- `active_swap + free_swap == 7340032`；同一地址空间虚拟页最多一个活动槽。
- 交换数据先于元数据提交；读/校验/提交失败不释放唯一槽。
- 全局 committed 等于各活动地址空间 committed 之和；失败事务撤销 charge。
- 正常整机结束时 committed、active swap、活动 COW 引用和 VMA 描述符均为零。
- panic 不读取交换盘、不获取 VFS 锁、不分配内存。

## 后果与边界

手机运行会真实占用约 4 GiB 宿主 RAM；QEMU 本身还有少量额外 RSS。128 GiB
rootfs 与 28.44 GiB 交换盘全部物化时，需要约 156.44 GiB 可用存储。磁盘换页
延迟远高于 RAM，持续工作集超过 4 GiB 时会明显变慢并增加闪存写入。

v2.5 不加入 NUMA、SMP reclaim worker、THP、zswap、swap readahead、mlock、
memory cgroup、可写 proc sysctl 或休眠恢复。32 GiB RAM 只保留为可选工程压力档，
不再是手机参考规格，也不得替代 4 GiB `-mem-prealloc` 验收。

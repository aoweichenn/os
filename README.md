# x86-64 OS Lab

这是一个从 CPU 复位向量开始自研的 x86-64 教学操作系统项目。QEMU 仅用于模拟硬件；固件、引导程序、模式切换、内核、运行时、驱动、用户空间和文件系统均由项目自行实现。

当前状态：`v2.6 集成冻结与正式发布` 已形成 caw 主工程候选，但公开发布已暂停；
工程从 v2.7 继续建立通用块设备层与自研 NVMe 路径。v2.1 已建立 Firmware、Stage 1、
Kernel、panic 以及 Ring 3 stdout/stderr 从 COM1 迁移到项目自研的 80×25 VGA
文本控制台，并把详细系统诊断分流到只追加内存日志。进入 Shell 时屏幕清空；
此后普通 Kernel 事件只进入宿主可导出的日志，TTY stdout/stderr 写 VGA，panic
始终双写。v2.2 进一步加入有界行编辑、16 条历史、Tab 补全、环境继承与展开、
glob、控制序列、完整重定向、CMOS UTC 和 43 个独立工具路径。v2.3 把生产根
升级为 `OSRFV004`：65536 inode、五级稀疏块树、248-credit journal、链接、
四时间戳和 orphan 恢复共同覆盖 LBA 32768 到 `0x0FFFFFFF`，128 GiB 稀疏盘
在启动前缀后的容量现已成为可分配 rootfs。ABI 兼容升级为 v2.2.0，仍为 71
个系统调用，`FileInformation` 追加四个纳秒时间戳。v2.3 的盘面、恢复边界与
本地证据见 [发布记录](docs/releases/v2.3.md) 和
[ADR 0051](docs/adr/0051-rootfs-v4-full-disk-links-and-recovery.md)。v2.4 随后加入
本地凭据、Unix mode、VFS DAC、set-ID exec、rlimit 与 47 个工具路径；v2.5
已加入单域水位、4 GiB QEMU 预分配 RAM、28 GiB 独立 ATA 交换盘、Linux 编号的
overcommit 0/1/2、确定性 OOM 和 `/proc/meminfo` 压力统计。工程故障镜像仍保持
稀疏，手机实际运行的 rootfs 与交换盘必须先完整物化；设计见
[ADR 0053](docs/adr/0053-memory-pressure-swap-overcommit-and-oom.md)，当前验证状态见
[v2.5 发布记录](docs/releases/v2.5.md)。
v2.6 不增加主要内核机制，新增跨消费者发布身份、结构化产物清单和已物化
4 GiB 三轮长稳门禁；设计见
[ADR 0054](docs/adr/0054-v2-6-release-identity-and-soak-gates.md)，候选状态见
[v2.6 发布记录](docs/releases/v2.6.md)。
v2.7 当前已把块设备协议上移到设备层，自行实现 PCI configuration、BAR/MMIO、
NVMe admin/I/O queue、PRP list 和 MSI-X，并在 QEMU namespace 末端用四个
outstanding 分别完成 64 KiB Write、Flush、Read 回验；EIO 与 CAP.TO 超时均会
reset 并重建队列。Kernel 运行期 rootfs/swap 已分别迁移到 Namespace 1/2，设备
缺失时自动回退 ATA；ROM 和 Stage 1 仍从 ATA 启动。当前边界见
[v2.7 记录](docs/releases/v2.7.md) 和
[ADR 0055](docs/adr/0055-v2-7-block-device-layer-and-nvme-path.md)。
v2.8 的六个核心增量已经实现：64 位动态 radix、动态文件缓存地址空间、统一数据
路径、有界后台写回、按文件/映射范围同步和统一内存压力回收。
rootfs/legacy 的 buffered read/write、ELF/file fault 与 `MAP_SHARED` 共享同一
frame；普通写直接脏化缓存页，`truncate` 只撤销 EOF 后映射、丢弃范围外页并清零
保留尾页。cache miss 以 Loading 占位后在锁外填页；Dirty 使用约 10%/20% 软硬
水位，返回用户态安全点每批最多写回 64 页，失败暂停自动重试。ABI 2.4.0 追加
`fsync`/`fdatasync`/`msync`；动态错误序列让独立 open 各报告一次写回失败，而
duplicate/fork 共享同一游标。direct reclaim 固定执行 clean file trim、dirty
writeback/trim、跨进程 anonymous swap，
回收后仍无法满足原请求才进入 OOM；PID1 和活动返回栈页不会被跨进程换出。边界见
[v2.8 记录](docs/releases/v2.8.md) 与
[ADR 0056](docs/adr/0056-v2-8-dynamic-file-cache-address-space.md)。
当前自动 QEMU 验收统一使用 4 GiB `-mem-prealloc`，不再重复运行 64/256 MiB 档。
v2.9 已加入协作式 Kernel Thread 和 generation WorkQueue。第三增量让一个常驻 Worker
与 User Thread 共用调度器：跨类型切换统一返回 dispatcher，常规 writeback 不再在
user-return 执行，而由 Worker 在锁外分批完成；低水位脏页按 Linux 默认 5 秒窗口老化，
软水位请求可即时提升延迟任务。IRQ 只处理 deadline/重调度，硬脏页压力仍保留同步
direct fallback。第四增量又以 x86 PTE Accessed 位建立 file/anonymous 的
active/inactive 四队列：每秒由同一 Worker 汇总物理帧 alias，新页连续两轮未访问才成为
候选。第五增量已加入 low/high 水位后台回收：low 到 min 留给 64 页 Worker 批次，min
以下才走 direct fallback；无进展、仅写回和失败按 deadline 退避。候选消费同时核对
file access generation，并在释放 frame 前删除 aging 身份。元数据由真实物理帧和 KVA
承载，4 GiB/32 GiB 分别采用 4096/32768 页身份容量。第六增量又统一 direct/background
的 0..200 swappiness 配额，候选不足会转赠预算；匿名 frame 在 unmap/exec/exit/OOM
最后释放前同步删除 aging 身份。ATA/NVMe `oom-pressure` 使用 swappiness 0 和
`/proc/meminfo` 动态预算，真实验证非当前 victim 的 SIGKILL、wait/reap 与资源归零。
边界见
[v2.9 记录](docs/releases/v2.9.md) 与
[ADR 0057](docs/adr/0057-v2-9-kernel-thread-lifecycle.md)、
[ADR 0058](docs/adr/0058-v2-9-work-queue-state-and-drain.md)、
[ADR 0059](docs/adr/0059-v2-9-mixed-worker-writeback.md)、
[ADR 0060](docs/adr/0060-v2-9-pte-accessed-page-aging.md)、
[ADR 0061](docs/adr/0061-v2-9-background-watermark-reclaim.md)、
[ADR 0062](docs/adr/0062-v2-9-unified-reclaim-fairness-and-oom-matrix.md)。
v2.10 六个增量已经完成：`BlockRequestQueue` 按实际解析顺序交付
completion，类型擦除
`AsynchronousBlockDevice` 统一 ATA/NVMe，BlockIo 协调器与常驻 Worker 负责非 IRQ 完成；
User Kernel stack 续体和 `RuntimeMutex` 又让生产 rootfs/swap 在真实 completion 上睡眠。
`FilePageLoadCoordinator` 现在把同页 Loading 冲突合并到一次来源读取，并在完成广播前为
waiter 预留真实页引用。第五增量 5a 又建立打开文件级 `FileReadaheadPolicy`：默认 32 页
上限、Linux 风格 4/2 倍窗口增长、随机重置、反馈缩放和压力收缩均已由纯模型冻结；5b 已
把它接入共享 FileDescription、VFS 页观测、有界 retained-OpenFile 请求队列、Kernel worker
和带 one-shot 预取身份的 FilePageCache。5c 又用 generation stream token 完成 producer
反馈、close/reset/压力/truncate 取消和 stale 隔离。
第六增量新增 per-page `FilePageWritebackCoordinator`：同页 `MarkDirty` 和同步 writeback
等待唯一 generation 结果，其他 Clean 页仍可并发 reclaim；成功、EIO、timeout、cancel
通过公共异步块终态与 ATA/NVMe 生产/持久化矩阵组合验证。已签发设备命令仍保持
best-effort cancel，不伪造硬件 abort。
边界见
[v2.10 记录](docs/releases/v2.10.md) 与
[ADR 0063](docs/adr/0063-v2-10-ordered-block-completion-channel.md)、
[ADR 0064](docs/adr/0064-v2-10-asynchronous-block-device-adapter.md)、
[ADR 0065](docs/adr/0065-v2-10-block-io-kernel-wait-and-migration-boundary.md)、
[ADR 0066](docs/adr/0066-v2-10-stackful-user-kernel-continuation-and-runtime-mutex.md)、
[ADR 0067](docs/adr/0067-v2-10-file-page-loading-waiter-and-reference-handoff.md)、
[ADR 0068](docs/adr/0068-v2-10-per-open-file-readahead-policy.md)、
[ADR 0069](docs/adr/0069-v2-10-production-readahead-execution.md)、
[ADR 0070](docs/adr/0070-v2-10-readahead-cancellation-and-feedback-ledger.md)、
[ADR 0071](docs/adr/0071-v2-10-file-page-writeback-wait-and-failure-matrix.md)。
v2.11 六个工程增量已建立生产 `VfsNamespaceCache`：完整 mount/parent/name dentry key、
Positive/Negative/Stale、inode metadata ticket、并发 miss 合并、mutation 事务失效、固定
hash、LRU 与 pressure shrinker 均已接入。重复 component lookup 与 stat 不再访问 backend，
EIO 不会变成 Negative；固定 BSS 不伪装物理页回收。边界见
[v2.11 记录](docs/releases/v2.11.md) 与
[ADR 0072](docs/adr/0072-v2-11-vfs-namespace-cache-identity-and-lifecycle.md)、
[ADR 0073](docs/adr/0073-v2-11-inode-metadata-load-and-invalidation.md)、
[ADR 0074](docs/adr/0074-v2-11-production-dentry-lookup-and-namespace-mutation.md) 与
[ADR 0075](docs/adr/0075-v2-11-namespace-hash-lru-and-pressure-shrinker.md)。
v2.12 已把上述缓存推进为可扩展页后备实现：dentry/inode 各 64 个 waiter shard、128 个独立
解析上下文、namespace sequence retry、真实稳定/preferred 页、在线 compact hash rebuild
和 preferred 页释放均已接入；长期页从 user resident budget 精确排除，不改变 9216 页压力
规格。边界见 [v2.12 记录](docs/releases/v2.12.md) 与
[ADR 0076](docs/adr/0076-v2-12-scalable-page-backed-vfs-namespace.md)。
v2.13 已在该命名空间之上建立稳定目录句柄与 `dirfd + *at` 事务：绝对路径忽略目录 fd，
相对路径在调用期间 retain 目录 vnode；目录 rename 后旧 fd 仍可解析。ABI v2.5.0 在旧
1..87 后追加 88..96，覆盖 open/open-directory/mkdir/remove/stat/readlink/rename/link/
symlink 的 `*at` 基础调用；namespace writer 从解析前串行到提交并按 expected sequence
复验，并发同名 create 不会重入写锁。边界见 [v2.13 记录](docs/releases/v2.13.md) 与
[ADR 0077](docs/adr/0077-v2-13-directory-handles-and-at-path-transactions.md)。
`v2.0 集成发布`仍是最近一次冻结发布，不回写本次设备变更。v2.0 不新增核心机制，而是把 v1.1 至
v1.18 已分别验收的资源、进程、虚拟内存、Unix I/O、线程、时间、信号、
TTY、异步块层、日志文件系统和 ABI v2 收束为同一条可复现发布基线。ABI
v2.0 发布时的 ABI v2.0.0、69 个系统调用、错误区间和关键结构偏移仍是历史冻结基线；`/dev` 使用通用
最小 devfs，`/proc` 提供六个只读快照文件，rootfs 中 32 个独立工具路径
均由真实 ELF、inode 和 QEMU 运行验证。后续新增机制进入 v2.x，不回写
v2.0 的冻结边界。v1.1 已
落地动态物理内存、
可回收内核堆、buddy 页帧分配器、固定尺寸类型缓存、KVA、动态内核栈、
页表空分支回收，以及通用引用计数、作用域回滚和 26 字段资源快照。自研
128 KiB ROM 从 `0xFFFFFFF0` 接管 CPU、自行初始化 VGA 文本模式，通过 IDE ATA PIO
读取并校验自研 Stage 1；Stage 1 随后完成 A20、保护模式、64 MiB 身份映射、
长模式切换、Kernel 容器校验、ELF64 装载和 BootInfo 交接，最终进入
freestanding C++20 内核。内核随即替换 Stage 1 的描述符状态，建立自己的
GDT、TSS、IDT、32 个异常入口和无动态分配的 panic 路径。Stage 1 还通过
QEMU PC 的 `fw_cfg` 硬件接口读取 `etc/e820`，自行规范化为 BootInfo v2；
内核读取 `CPUID.80000008H` 与 E820，按实际可用 RAM 动态放置 2-bit 页帧
元数据，并建立从 `0xFFFF888000000000` 开始、容量 64 TiB 的高半区物理
直映窗口。直映内部优先使用 2 MiB 页，边界退回 4 KiB 页；Stage 1 的低
64 MiB 身份映射只负责启动，不再限制正式页帧管理。手机主 QEMU 规格为
4 GiB `-mem-prealloc`，最小兼容规格仍为 64 MiB；QEMU PC 的 PCI hole 重映射
使 4 GiB 档仍必须在 `0x100000000` 以上分配、写回并回收页帧。内核同时建立
W^X/NX/WP 权限、guard page 和 512 KiB 高半区内核堆，
并真实切换 CR3。该堆现已支持 best-fit、二次幂对齐、释放、前后合并、非法
释放检测、完整一致性检查和生命周期统计；QEMU 启动自检完成真实写回后会
释放全部对象并确认活动数归零。固定尺寸类型缓存在该堆上用一次后备申请同时
保存活动位图和对齐槽位，空闲槽内保存 LIFO 索引链；申请/释放为常数时间，
重复释放、内部指针和活动对象销毁都会明确失败。目标自检把 32 个 64 字节
对齐对象完整耗尽、写回、交错释放并复用，最终 33 次申请与释放守恒，缓存
销毁后堆恢复进入前基线。KVA 又把 `0xFFFFC90000000000` 开始的 32 TiB
高半区窗口建模为有序软件所有权区间，当前 1024 个描述符按有序区间管理，不按
数十亿潜在页建立位图。QEMU 自检申请六页区间、保持首尾 guard not-present，
把中间四页映射到 buddy order 2 物理块并真实写回，随后按映射、物理页、虚拟
区间的逆序回收，最终只保留窗口首个永久保护页。页表层现在按
`Exclusive`、`KernelShared`、`Process` 三种根所有权回收空 PT/PD/PDPT：
内核共享根释放低两级但保留可能被进程 PML4 引用的 PDPT，映射途中分配失败
则恢复父项和 U/S 位并逆序释放新表。两段 QEMU 自检合计回收两张 PT 和两张
PD，只稳定保留一张共享 PDPT。在此基础上，内核严格
验证并装入自研 `ET_EXEC` 用户 ELF64。正常启动由 Kernel 从 rootfs 读取
`/sbin/init`，再由 PID1 建立八进程并发验收树并顺序执行三个 VM probe；
每个地址空间拥有独立 PML4、同址用户代码/数据、完整 8 MiB 栈 VMA 与只覆盖
当前需要范围的驻留栈页。每个 16 KiB Ring 0 栈从 KVA 取得六页
所有权，从 buddy 取得四个独立物理页，并在上下各保留一页 not-present
guard。8254 PIT
每四个 tick 触发一次单核 round-robin 决策，切换 CR3、TSS.RSP0、176 字节
通用用户现场和每 Thread 512 字节 FXSAVE 现场。默认用户 ABI 已使用
`SYSCALL`，`INT 0x80` 作为兼容与等价性验证入口；两者提供日志、退出和
PID/TID 查询。进程退出或用户
异常会释放其用户页与页表，汇编回到永久启动栈后再清零并释放 Ring 0 栈的
映射、物理页和 KVA，Ring 0 故障仍进入 panic。v0.10 又把 PCB
扩展为可解释的 `Blocked` 状态，以具名等待原因完成阻塞与定向唤醒；内核
实现带 acquire/release 语义的自旋锁和 64 字节有界管道。Ring 3 生产者向
消费者传输并逐字节验证 256 字节确定性数据，覆盖满/空阻塞、部分传输、
EOF、broken pipe、端点权限、重复关闭和异常退出自动关闭。v0.11 又在
2 MiB 原始 IDE 磁盘的独立 1 MiB 区域实现固定布局文件系统：显式小端
superblock/inode/目录项、CRC32、bitmap、十个直接块、八项 LRU 写回缓存、
Dirty/Clean 提交协议与 ATA PIO 写入/FLUSH CACHE。每个 PCB 拥有四个文件
描述符；生产者把 256 字节载荷持久化为 `/shared/payload.bin`，消费者从
文件和管道分别验证。系统测试使用同一磁盘连续启动两次证明跨实例持久化，
再破坏超级块证明损坏不会被自动格式化掩盖。

v1.0 进一步把控制台、文件、目录和启动期管道并入每进程八槽描述符表；
fd 0/1/2 是标准输入、输出和错误。PS/2 IRQ1 把 Set 1 make code 解码为
字符并提交到 256 字节 FIFO，Ring 3 Shell 通过通用 Try/Wait 系统调用阻塞
读取。没有 Ready 但仍有 Blocked 时，内核切回永久地址空间执行
同一汇编块内的 `sti; hlt; cli`，由真实键盘中断唤醒后恢复用户帧。Shell 使用固定容量
freestanding C++20 解析器提供 help、echo、pwd、cd、ls、mkdir、write、cat、
rm、rmdir、mv、truncate、stat、sync 和 exit。QEMU 系统测试在 Shell READY
后逐字产生完整命令序列，来宾自行完成 i8042、IRQ、解码、排队、唤醒、
文件操作与退出；完整回归中的 Clang AST 与 Python 词法门禁会拒绝不符合
约定的变量、函数和命名空间。

第二周期已经按可独立验收的依赖闭环优化为 v1.1–v1.18。v1.1 的完整范围是
buddy、kernel heap/type cache、KVA、动态内核栈和页表回收，并保留当前
四进程通路；动态物理内存、通用可回收 kernel heap、双位图 buddy、固定尺寸
type cache、32 TiB KVA、动态双 guard 内核栈与页表空分支回收均已通过十万步
随机模型和 QEMU 真实生命周期验收。通用 `ScopeRollback` 已接管动态栈创建
失败路径，`ReferenceCounter` 冻结强引用生命周期，`ResourceSnapshot` 同时
核对 frame、buddy、heap、KVA 与栈的当前所有权。目标启动和四进程退出各做
一次零差异验证；具名 256 MiB functional smoke 与 4 GiB 手机主规格共同通过，
因此 v1.1 已闭环。
v1.2 已删除旧 PCB 调度器，把 Process 固定为地址空间、描述符和文件系统
上下文的共享资源容器，把 Thread 固定为唯一调度实体。独立单调 PID/TID
不再等于槽位；Thread 拥有动态双 guard 内核栈、用户栈、TLS/signal-mask
位置、run queue/WaitQueue 关系以及 x87/SSE2 现场。统一 WaitQueue 对
condition、timeout、signal、close、cancel 使用单赢家 WakeReason；SpinLock、
IrqSaveSpinLock 和可睡眠 Mutex 的调用边界由测试冻结。运行时规格随同一镜像
按 RAM 选择：64 MiB 兼容档为 8/8/1，256 MiB 为 64 Process/128 Thread/单进程
32 Thread；4 GiB 手机档沿用 64/128/32 的轻量资源配置，32 GiB 可选压力档才使用
256/512/64。启动容量自检建立真实页表根、动态栈和
FXSAVE 区，再退出、reap 并用 ResourceSnapshot 验证零差异。四个 Ring 3
程序分别写入不同 XMM0、XMM15、MXCSR、x87 控制字和 ST0 模式，在抢占、
阻塞、唤醒和退出边界反复校验。宿主固定种子模型执行 100000 步状态迁移，
QEMU 另有 `-sse2` 故障配置证明缺少必需 CPUID 能力时会在架构初始化前停止。

v1.3 已建立单元素 `CpuLocal`，统一保存 current Thread、可信入口栈、
IRQ/抢占深度和延迟调度请求。启动期将 long mode、NX、FXSR、SSE、SSE2、
`SYSCALL` 与 40/48 位地址宽度冻结为显式处理器规格，并配置、回读
`EFER/STAR/LSTAR/FMASK/GS_BASE/KERNEL_GS_BASE`。原生入口先 `SWAPGS`，
再从 `CpuLocal` 取得可信 Ring 0 栈；原生与兼容入口都规范化为 176 字节
`UserContext`。返回路径联合验证现场所有权、用户映射、规范 RIP/RSP、段和
RFLAGS；满足快速集合时执行 `SYSRETQ`，带 DF/RF 等合法但不安全的状态改走
`IRETQ`，非法状态则终止对应用户进程。QEMU 已实际记录系统调用被 IRQ 打断、
返回前 reschedule、双入口等价、SYSRET 与 IRET 回退；`qemu64,-syscall`
必须在用户态之前明确失败。

v1.4 已删除固定八槽 `IoDescriptorTable`，用动态 `KernelObject`、共享
`FileDescription` 和每 Process 分块 `FileTable` 建立新的资源边界。对象
handle 同时保存地址与全局单调 generation，操作期间使用 RAII 强引用，最后
引用在对象管理器锁外执行文件、管道或控制台 finalizer。FileDescription
保存种类、file status flags 和文件偏移，因此 duplicate 共享偏移、独立 open
不共享；close-on-exec 等 fd flags 则独立保存在表项。FileTable 每 64 项按需
增长，64 MiB、256 MiB、32 GiB 配置分别使用 64、512、4096 hard limit，
分块申请采用锁外准备和锁内复验的两阶段提交。PID4 已在真实 Ring 3 中验证
duplicate、CLOEXEC、共享/独立偏移、soft-limit 失败和最低编号复用；
256 MiB/32 GiB 档使用 minimum 64，hard limit 仅为 64 的兼容档使用
minimum 8。退出后对象、引用、finalizer 和分块统计全部守恒。

v1.5 已建立 `Vnode`、`Path`、`Superblock`、`Mount` 和每 Process
`FsContext`，把 `FileDescription` 的底层载荷迁移为 `Vfs + OpenFile`。
根文件系统继续由 legacy 适配器读取旧磁盘，`/tmp` 挂载完整 memfs；绝对/
相对路径、`.`、`..`、根钳制、尾斜杠、挂载进入/退出和真实 `getcwd` 由同一
逐组件算法处理。路径与名称规格分别为 4096/255 字节；同一契约已在 memfs
和 legacy-fs 上通过，固定种子目录树模型完成 100000 步逐步对照。Shell
现在具有真实 `cd`、动态 cwd 提示符，并在一次 functional QEMU 会话中同时
操作 `/tmp` 和持久根目录。详细证据见
[v1.5 发布记录](docs/releases/v1.5.md) 与
[ADR 0032](docs/adr/0032-vfs-mount-namespace-and-memfs.md)。

v1.6 已把生产根目录从 legacy 后端切换到严格挂载的 rootfs v2。启动镜像是
逻辑 1 GiB 的稀疏文件，其中固定 256 MiB rootfs 使用版本化小端
superblock、8192 个 256 字节 inode、inode/data bitmap、320 字节目录项、
CRC32 和八个直接块加单/双/三级间接树；单文件规格为 64 MiB。普通文件支持
空洞、短写和明确 ENOSPC，目录支持 unlink/rmdir、同目录与跨目录 rename、
替换、非空/环路/挂载点保护，并新增 truncate/stat 和系统调用 31..35。
每次修改以 Dirty→数据/元数据 flush→Clean 的顺序提交；未完成事务和任何
元数据损坏只会拒绝挂载，内核绝不自动格式化。独立 Python
`mkfs-rootfs`、`inspect-rootfs`、`fsck-rootfs` 与 `corrupt-rootfs` 使用同一
冻结盘面格式。memfs/rootfs 使用同一种子各执行 100000 步模型，真实近满
256 MiB 镜像验证短写与 ENOSPC，QEMU 同盘两次启动及损坏第三次启动闭环。
详细证据见 [v1.6 发布记录](docs/releases/v1.6.md) 与
[ADR 0033](docs/adr/0033-rootfs-v2-namespace-mutations.md)。

v1.7 已把普通用户程序离线安装到 rootfs，正常 Kernel 不再内嵌 Shell、
worker、producer 或 consumer。Kernel 只从 `/sbin/init` 建立 PID1，随后
由 PID1 通过系统调用 36..38 执行磁盘 ELF spawn/exec/wait。独立进程树
保存父子、Alive/Zombie、退出结果和 reparent；PID1 回收六个直接子进程与
一个被收养孤儿。ELF 采用 reader 驱动的两遍校验/加载，exec 在候选页表、
256 KiB 用户栈和 `argc/argv/envp` 完整后才提交，失败保持旧映像。参数和
环境合计精确支持 128 KiB，并以 256 字节缓冲分批搬运，不增加 16 KiB
KernelStack。64 MiB QEMU 兼容档也完整运行八进程树，最终 ProcessTree、
Thread、页表、KVA、fd、VFS context 和对象统计全部守恒。详细证据见
[v1.7 发布记录](docs/releases/v1.7.md) 与
[ADR 0034](docs/adr/0034-pid1-process-tree-disk-exec-wait.md)。

v1.8 已为每个 AddressSpace 建立按地址递增的 VMA 图：全局 8192 个描述符
以 owner identifier 隔离，单 Process hard limit 为 4096；相同 kind/权限
的相邻区间自动合并，中段 unmap 在修改前预取 split 描述符。系统调用 39..42
提供 512 MiB 匿名窗口内的 first-fit/fixed 非覆盖 map、unmap、program
break 和 112 字节统计。map 与 break growth 只登记 VMA；合法用户
not-present `#PF` 才分配、清零并映射一页。栈完整预留 8 MiB，只允许紧邻
committed bottom 且与用户 RSP 邻近的 fault 增长，底部下一页永久没有 VMA。
撤销、exec 与 exit 同时回收实际驻留 frame、空页表分支和描述符。Ring 3
`UserHeap` 在最多 8 MiB program break 上实现 16 字节对齐、first-fit、
split、前后 coalesce 与完整结构校验。VMA 和 heap 各通过 100000 步参考
模型；64 MiB、256 MiB、32 GiB QEMU 均验证零填充、稀疏触页、unmap、
2 MiB break、栈增长、5000 步 heap、guard/protection fault 和最终资源守恒。
详细证据见 [v1.8 发布记录](docs/releases/v1.8.md) 与
[ADR 0035](docs/adr/0035-anonymous-vma-demand-paging-user-heap.md)。

v1.9 已把 ELF `PT_LOAD` 和普通文件映射都改为 VMA 先登记、首次访问再装
页。`FileBackingManager` 持有稳定的 VFS 打开实例或内存镜像，
`FilePageCache` 用 `(superblock id/generation, inode id/generation,
page index)` 唯一标识 clean 页，并以固定容量、引用计数和 LRU 回收限制
资源。完整只读文件页可被多个映射共享；可写 `MAP_PRIVATE` 和文件尾部部分
页使用私有帧，写入不会回写。只读 `MAP_SHARED` 可共享 clean 页，writable
shared 与 `msync` 明确不支持。write/truncate 会撤销相关只读映射并失效缓存，
关闭 fd 后映射仍由后备对象保持有效。Ring 3 已验证文件尾零填充、缓存命中、
写后重新 fault、private 隔离和完整回收。大 ELF 上限与 1 GiB 程序窗口对齐，
不再受旧 512 页工具限制。详细证据见
[v1.9 发布记录](docs/releases/v1.9.md) 与
[ADR 0036](docs/adr/0036-file-backed-vma-lazy-elf-clean-page-cache.md)。

v1.10 新增系统调用 44 `ForkProcess`。fork 只复制调用 Thread 的用户现场，
子进程从返回值 0 继续执行；父子继承 VMA、FsContext 和精确 fd 编号/标志，
共享的 FileDescription 因而继续共享文件偏移。匿名页与可写 private 文件页
在父子 PTE 中同时降为只读并标记软件 COW 位；真实用户写页故障与
`CopyToUser` 进入同一私有化路径。引用数为 1 时只恢复原页可写，为 2 以上
时分配、复制并替换单页。fork 采用候选子地址空间和父 PTE 提交两阶段事务，
任意失败都会逆序销毁子对象并恢复父页权限。Ring 3 已验证匿名页、文件 private
页、只读页、内核复制、cwd、共享 fd 偏移以及连续 32 次
fork/exec/wait 完整回收。详细证据见
[v1.10 发布记录](docs/releases/v1.10.md) 与
[ADR 0037](docs/adr/0037-fork-copy-on-write.md)。

v1.11 新增系统调用 45/46 `CreatePipe` 与 `DuplicateDescriptorTo`。
动态 Pipe 的逻辑容量为 64 KiB，4 KiB 数据页按首次写入申请；同一镜像按
64 MiB、256 MiB、32 GiB 选择 8、128、1024 条 Pipe 容量。reader/writer
通过 FileDescription 最后引用关闭，EOF、broken pipe、短读/短写和创建失败
回滚均有独立语义。Shell 只保留 `cd`/`exit`，其他十九个工具均作为 rootfs
多调用 ELF 经 fork/dup2/exec/wait 执行；解析器支持引号、转义、`<`、`>` 和
最多 16 级管线。functional QEMU 真实执行重定向和 16 个并发 child，最终
143 次 Pipe 创建/释放守恒且 Zombie 为零。详细证据见
[v1.11 发布记录](docs/releases/v1.11.md) 与
[ADR 0038](docs/adr/0038-dynamic-pipe-dup2-external-shell.md)。

v1.12 新增系统调用 47--53，开放同一 Process 内的用户 Thread、64 KiB
独立用户栈与 guard、FS-base TLS、Join 回收和 private futex。futex 以
`(AddressSpaceId, aligned user VA)` 隔离地址空间，并在调度器临界区内完成
compare-and-block；用户 Mutex、ConditionVariable 与 Once 只有竞争路径进入
Kernel。`munmap`、多线程 exec 和 ProcessExit 会取消旧地址 waiter，普通
ThreadExit 不关闭 Process 共享资源。64 MiB 验证单线程降级，256 MiB 真实
建立 32 Thread，32 GiB 建立 64 Thread 并拒绝第 65 个，最终 TLS、futex、
Join、KernelStack 和 Process 资源全部守恒。详细证据见
[v1.12 发布记录](docs/releases/v1.12.md) 与
[ADR 0039](docs/adr/0039-user-threads-fs-tls-private-futex.md)。

v1.13 新增系统调用 54--56，把 8254 PIT 的输入频率与实际除数换算成 64 位
单调纳秒。整数余数跨 tick 保留，边界饱和而不回绕；ThreadScheduler 内嵌
512 槽 deadline queue，以同一 irq-save 临界区裁决通知、超时、终止和取消。
用户运行时提供非忙等 Sleep、timed futex 和重新取得 Mutex 后返回强类型结果
的 timed ConditionVariable。`/bin/time_probe` 在三档 QEMU 中验证 sleep
不早醒、futex 超时、条件先赢、条件超时与 idle 唤醒，最终 deadline 账本
归零。详细证据见 [v1.13 发布记录](docs/releases/v1.13.md) 与
[ADR 0040](docs/adr/0040-monotonic-clock-deadline-timed-wait.md)。

v1.14 新增系统调用 57--63，把 Process disposition、Thread mask、普通
pending 合并、进程组和用户 handler 接入现有 Process/Thread 状态机。
Signal 通过统一 `WakeReason` 与 condition、deadline 和 close 竞争；可重启
阻塞调用在信号帧中恢复原 syscall 号并把 RIP 回退到两字节 `SYSCALL`。
Kernel 在用户栈构造 240 字节固定帧和 restorer 返回槽，Intel NASM
trampoline 进入严格 sigreturn；帧身份、cookie、canonical 地址、段、RFLAGS、
栈边界和页权限任一异常只隔离目标 Process。fork 复制 action/mask/group，
exec 重置 Handler，退出后信号状态归零。详细证据见
[v1.14 发布记录](docs/releases/v1.14.md) 与
[ADR 0041](docs/adr/0041-process-signals-user-frame-and-sigreturn.md)。

v1.15 新增系统调用 64--69，把单控制终端、canonical 行规程、SID/PGID、
前台所有权、停止/继续 wait 事件和 `/dev/console` 字符 vnode 接入现有
Process/Thread/VFS。PS/2 解码器真实跟踪左右 Ctrl，TTY 将 Ctrl-C/Ctrl-Z
定向为前台进程组信号；Shell 提供 `jobs`、`fg`、`bg` 与尾部 `&`，整条
16 级管线共享 PGID。停止期间地址空间、fd 和 CPU/FX 现场保持，继续后从原
Thread 恢复；Zombie 的组身份保留到 wait，消除极短命令的 `setpgid` 竞态。
详细证据见 [v1.15 发布记录](docs/releases/v1.15.md)、
[学习章](docs/learning/23-v1.15-tty-session-job-control.md) 与
[ADR 0042](docs/adr/0042-tty-session-and-job-control.md)。

v1.16 新增 64 槽 BlockRequest FIFO、ATA primary IRQ14 完成、PIT deadline
超时与 software reset，并以 `BlockIo` 等待让 I/O Thread 阻塞期间其他
Ready Thread 继续前进。文件页缓存新增 Clean/Dirty/Writeback/Error 状态、
脏页回压和失败重试；完整页 writable `MAP_SHARED` 通过初始只读 PTE 捕获
第一次写，`sync` 重新写保护、经 VFS `WriteAt` 回写并等待异步 ATA FLUSH。
共享 alias 可见、落盘读回和 private 不回写均由真实 Ring 3/QEMU 探针验证。
详细证据见 [v1.16 发布记录](docs/releases/v1.16.md)、
[学习章](docs/learning/24-v1.16-irq14-block-request-writeback.md) 与
[ADR 0043](docs/adr/0043-irq14-block-request-and-writeback-cache.md)。

v1.17 又把 rootfs 升级为盘面格式 3：固定 256 块 journal、124 个
transaction credits、descriptor/payload/commit CRC、ordered data flush、
checkpoint 与挂载期幂等 replay 已进入生产路径。1000 个确定性断电点、
10 万步随机状态模型、独立 rootfs 全盘校验和三档 QEMU 共同证明恢复结果
只能是完整旧状态或完整新状态。

v1.18 冻结 ABI v2.0.0：系统调用号 1--69、错误值 -1---57、ELF64/x86-64
身份和进程、线程、信号、文件、终端、虚拟内存结构布局由编译期断言与产物
测试共同锁定。只读 procfs 在每次 read 时采集一致的有界快照，公开 version、
uptime、meminfo、processes、resources 和 mounts；最小 devfs 用调用者提供的
固定存储注册字符设备，不向 VFS 泄露驱动指针。用户工具从 19 个补到 32 个，
QEMU 除验证 32 个唯一 inode/ELF 外，还在 functional Shell 中实际运行新增
工具。CTest 为所有 QEMU 用例设置同一资源锁，宿主模型可并行而虚拟机不会
争抢 TCG 与来宾内存。详细决策见
[ADR 0045](docs/adr/0045-abi-v2-devfs-procfs-release-freeze.md)。

v2.0 只集成已经冻结的机制，收敛为从自研文件系统启动 `/sbin/init` 与外部
Shell 的单 BSP、多进程、多线程类 Unix 教学系统。该发布当时使用 64 MiB、
256 MiB 和 64 GiB
分别承担启动兼容、完整功能和容量压力；完整宿主测试、目标产物审计、QEMU
成功/失败矩阵、教材、手机导出和公开网站共同形成发布证据。详细结论见
[v2.0 发布记录](docs/releases/v2.0.md) 和
[v2.0 集成发布学习章](docs/learning/27-v2.0-integration-release.md)，执行
模型、语义边界和取舍见
[ADR 0019](docs/adr/0019-v2-executable-program-baseline.md)。

## 最短构建与测试路径

在 Linux 环境安装 Python 3.11+、Clang、Clang-Tidy、LLD、NASM、QEMU、
GDB、CMake 和 Ninja 后执行：

```bash
python3 tools/os.py verify
```

构建完成后可在有图形会话的本机打开 VGA 控制台：

```bash
python3 tools/os.py qemu-display \
  build/developer/images/firmware.bin \
  build/developer/images/boot_disk.img \
  131072 137438953472
```

无桌面环境时可增加 `--display-backend curses`；从手机查看时使用
`--display-backend vnc`，它只监听宿主机回环地址，再通过 noVNC 与 Tailscale
Serve 提供经认证的浏览器页面。详细诊断默认写入
`build/developer/qemu-display.log`，不会在 Shell 前台持续刷屏；路径和手机操作
详见 [构建说明](docs/building.md)。

该命令会完成工具链检查、宿主机测试构建、x86-64 freestanding
交叉编译、自研 ROM 生成与审计、单元测试、集成测试、固定种子随机测试和
QEMU TCG 整机测试。详细说明见 [docs/building.md](docs/building.md) 和
[docs/testing.md](docs/testing.md)。

固件成功日志：

```text
[OS][FIRMWARE] RESET
[OS][FIRMWARE] VGA_READY
[OS][FIRMWARE] CLOCK_READY
[OS][FIRMWARE] STAGE1_HEADER_VALID
[OS][FIRMWARE] STAGE1_LOADED
[OS][STAGE1] A20_READY
[OS][STAGE1] ENTERED
[OS][STAGE1] GDT_READY
[OS][STAGE1] PROTECTED_MODE
[OS][STAGE1] PAGE_TABLES_READY
[OS][STAGE1] PAE_READY
[OS][STAGE1] LME_READY
[OS][STAGE1] PAGING_ENABLED
[OS][STAGE1] LONG_MODE
[OS][STAGE1] MEMORY_MAP_READY
[OS][STAGE1] KERNEL_HEADER_VALID
[OS][STAGE1] KERNEL_PAYLOAD_VALID
[OS][STAGE1] KERNEL_ELF_VALID
[OS][STAGE1] KERNEL_SEGMENTS_LOADED
[OS][STAGE1] BOOT_INFO_READY
[OS][STAGE1] KERNEL_TRANSFER
[OS][KERNEL] ENTERED
[OS][KERNEL] BOOT_INFO_VALID
[OS][KERNEL] BSS_ZEROED
[OS][KERNEL] CR3_VALID
[OS][KERNEL] PROCESSOR_FEATURES_READY
[OS][KERNEL] PROCESSOR_REQUIRED_FEATURES=0x000000000000003F
[OS][KERNEL] PROCESSOR_AVAILABLE_FEATURES=0x000000000000003F
[OS][KERNEL] PROCESSOR_PROFILE_PHYSICAL_ADDRESS_BITS=0x0000000000000028
[OS][KERNEL] PROCESSOR_PROFILE_VIRTUAL_ADDRESS_BITS=0x0000000000000030
[OS][KERNEL] EXTENDED_STATE_READY
[OS][KERNEL] EXTENDED_STATE_CR0=0x...
[OS][KERNEL] EXTENDED_STATE_CR4=0x...
[OS][KERNEL] EXTENDED_STATE_AVX_DISABLED=0x0000000000000001
[OS][KERNEL] GDT_READY
[OS][KERNEL] TSS_READY
[OS][KERNEL] IDT_READY
[OS][KERNEL] DESCRIPTOR_TABLES_VALID
[OS][KERNEL] CPU_LOCAL_READY
[OS][KERNEL] CPU_LOCAL_ADDRESS=0x...
[OS][KERNEL] NATIVE_SYSCALL_READY
[OS][KERNEL] NATIVE_SYSCALL_STAR=0x0010000800000000
[OS][KERNEL] NATIVE_SYSCALL_LSTAR=0x...
[OS][KERNEL] NATIVE_SYSCALL_FMASK=0x0000000000044700
[OS][KERNEL] NATIVE_SYSCALL_EFER=0x0000000000000501
[OS][KERNEL] BREAKPOINT_HANDLED
[OS][KERNEL] EXCEPTION_SELF_TEST_READY
[OS][KERNEL] MEMORY_MAP_VALID
[OS][KERNEL] MEMORY_MAP_ENTRIES=0x...
[OS][KERNEL] MEMORY_DESCRIBED_BYTES=0x...
[OS][KERNEL] MEMORY_USABLE_BYTES=0x...
[OS][KERNEL] MEMORY_MANAGED_BYTES=0x...
[OS][KERNEL] MEMORY_MANAGED_PHYSICAL_LIMIT=0x...
[OS][KERNEL] PHYSICAL_ADDRESS_BITS=0x...
[OS][KERNEL] VIRTUAL_ADDRESS_BITS=0x...
[OS][KERNEL] FIVE_LEVEL_PAGING_SUPPORTED=0x...
[OS][KERNEL] FRAME_STATE_STORAGE_ADDRESS=0x...
[OS][KERNEL] FRAME_STATE_STORAGE_BYTES=0x...
[OS][KERNEL] FRAME_ALLOCATOR_READY
[OS][KERNEL] FREE_FRAMES=0x...
[OS][KERNEL] ALLOCATED_FRAMES=0x...
[OS][KERNEL] RESERVED_FRAMES=0x...
[OS][KERNEL] PAGING_READY
[OS][KERNEL] PAGING_ROOT=0x...
[OS][KERNEL] PAGE_TABLE_RECLAIM_READY
[OS][KERNEL] PAGE_TABLE_RECLAIMED_LEVEL1_TABLES=0x0000000000000002
[OS][KERNEL] PAGE_TABLE_RECLAIMED_LEVEL2_TABLES=0x0000000000000002
[OS][KERNEL] PAGE_TABLE_RECLAIMED_LEVEL3_TABLES=0x0000000000000000
[OS][KERNEL] PAGE_TABLE_RETAINED_SHARED_LEVEL3_TABLES=0x0000000000000001
[OS][KERNEL] PAGE_TABLE_RECLAIM_SELF_TEST_PASSED
[OS][KERNEL] DIRECT_MAP_BASE=0xFFFF888000000000
[OS][KERNEL] DIRECT_MAP_MAPPED_BYTES=0x...
[OS][KERNEL] DIRECT_MAP_2M_PAGES=0x...
[OS][KERNEL] DIRECT_MAP_4K_PAGES=0x...
[OS][KERNEL] HIGH_MEMORY_TEST_ADDRESS=0x...
[OS][KERNEL] HIGH_MEMORY_VALIDATION_COMPLETE
[OS][KERNEL] MEMORY_PERMISSIONS_VALID
[OS][KERNEL] HEAP_READY
[OS][KERNEL] HEAP_CAPACITY_BYTES=0x0000000000080000
[OS][KERNEL] HEAP_ACTIVE_ALLOCATIONS=0x0000000000000000
[OS][KERNEL] HEAP_PEAK_CONSUMED_BYTES=0x...
[OS][KERNEL] HEAP_LARGEST_FREE_ALLOCATION_BYTES=0x...
[OS][KERNEL] HEAP_SELF_TEST_PASSED
[OS][KERNEL] TYPE_CACHE_READY
[OS][KERNEL] TYPE_CACHE_OBJECT_SIZE_BYTES=0x0000000000000040
[OS][KERNEL] TYPE_CACHE_OBJECT_ALIGNMENT_BYTES=0x0000000000000040
[OS][KERNEL] TYPE_CACHE_SLOT_STRIDE_BYTES=0x0000000000000040
[OS][KERNEL] TYPE_CACHE_CAPACITY=0x0000000000000020
[OS][KERNEL] TYPE_CACHE_BACKING_STORAGE_BYTES=0x0000000000000840
[OS][KERNEL] TYPE_CACHE_ACTIVE_OBJECTS=0x0000000000000000
[OS][KERNEL] TYPE_CACHE_FREE_OBJECTS=0x0000000000000020
[OS][KERNEL] TYPE_CACHE_SUCCESSFUL_ALLOCATIONS=0x0000000000000021
[OS][KERNEL] TYPE_CACHE_RELEASES=0x0000000000000021
[OS][KERNEL] TYPE_CACHE_PEAK_ACTIVE_OBJECTS=0x0000000000000020
[OS][KERNEL] TYPE_CACHE_SELF_TEST_PASSED
[OS][KERNEL] PROCESS_RUNTIME_READY
[OS][KERNEL] PIPE_READY
[OS][KERNEL] ROOTFS_V4_MOUNTED
[OS][KERNEL] FILE_SYSTEM_CONSISTENT
[OS][KERNEL] USER_ELF_VALID
[OS][KERNEL] USER_ENTRY=0x0000000040000000
[OS][KERNEL] USER_STACK_READY
[OS][KERNEL] PROCESS_ID=0x0000000000000001
[OS][KERNEL] PROCESS_CR3=0x...
[OS][KERNEL] USER_RING3_ENTER
[OS][KERNEL] SCHEDULER_STARTED
[OS][USER][SHELL] READY
[OS][USER][PIPE] PRODUCER_STARTED
[OS][USER][PIPE] CONSUMER_STARTED
[OS][USER][FS] FILE_WRITTEN
[OS][USER] DUAL_SYSCALL_ENTRY_EQUIVALENT
[OS][USER] SYSRET_RETURNED
[OS][USER] SYSCALL_IRET_FALLBACK_RETURNED
[OS][USER] UNKNOWN_SYSCALL_REJECTED
[OS][USER] HELLO_FROM_RING3
[OS][USER][PIPE] PRODUCER_COMPLETED
[OS][USER][PIPE] PAYLOAD_VERIFIED
[OS][USER][PIPE] EOF_OBSERVED
[OS][USER][FS] FILE_VERIFIED
[OS][USER][PID4] WORKER_STEP_1
[OS][USER] ADDRESS_SPACE_ISOLATED
[OS][USER][SHELL] COMMAND=HELP
[OS][USER][SHELL] COMMAND=ECHO
[OS][USER][SHELL] COMMAND=PWD
[OS][USER][SHELL] COMMAND=MKDIR
[OS][USER][SHELL] COMMAND=WRITE
[OS][USER][SHELL] COMMAND=STAT
[OS][USER][SHELL] COMMAND=MV
[OS][USER][SHELL] COMMAND=TRUNCATE
[OS][USER][SHELL] COMMAND=CAT
[OS][USER][SHELL] COMMAND=LS
[OS][USER][SHELL] COMMAND=RM
[OS][USER][SHELL] COMMAND=RMDIR
[OS][USER][SHELL] COMMAND=SYNC
[OS][USER][SHELL] UNKNOWN_COMMAND_REJECTED
[OS][USER][SHELL] COMMAND=EXIT
[OS][USER][SHELL] EXIT
[OS][KERNEL] SCHEDULER_CREATED_PROCESSES=0x0000000000000004
[OS][KERNEL] SCHEDULER_TERMINATED_PROCESSES=0x0000000000000004
[OS][KERNEL] SCHEDULER_PREEMPTIONS=0x...
[OS][KERNEL] SCHEDULER_BLOCKS=0x...
[OS][KERNEL] SCHEDULER_WAKEUPS=0x...
[OS][KERNEL] CPU_LOCAL_MAX_IRQ_DEPTH=0x...
[OS][KERNEL] CPU_LOCAL_MAX_PREEMPT_DEPTH=0x...
[OS][KERNEL] LEGACY_SYSCALL_ENTRIES=0x...
[OS][KERNEL] NATIVE_SYSCALL_ENTRIES=0x...
[OS][KERNEL] SYSCALL_IRQ_INTERRUPTS=0x...
[OS][KERNEL] SYSCALL_RETURN_RESCHEDULES=0x...
[OS][KERNEL] SYSRET_RETURNS=0x...
[OS][KERNEL] IRET_RETURNS=0x...
[OS][KERNEL] SYSCALL_IRET_FALLBACKS=0x...
[OS][KERNEL] REJECTED_USER_RETURNS=0x0000000000000000
[OS][KERNEL] PIPE_CAPACITY_BYTES=0x0000000000000040
[OS][KERNEL] PIPE_WRITTEN_BYTES=0x0000000000000100
[OS][KERNEL] PIPE_READ_BYTES=0x0000000000000100
[OS][KERNEL] PIPE_EOF_OBSERVATIONS=0x0000000000000001
[OS][KERNEL] CONSOLE_SUBMITTED_BYTES=0x...
[OS][KERNEL] CONSOLE_READ_BYTES=0x...
[OS][KERNEL] CONSOLE_DROPPED_BYTES=0x0000000000000000
[OS][KERNEL] CONSOLE_BUFFERED_BYTES=0x0000000000000000
[OS][KERNEL] OBJECT_ACTIVE_COUNT=0x0000000000000000
[OS][KERNEL] OBJECT_ACTIVE_REFERENCES=0x0000000000000000
[OS][KERNEL] OBJECT_CREATIONS=0x...
[OS][KERNEL] OBJECT_DESTRUCTIONS=0x...
[OS][KERNEL] FILE_DESCRIPTION_ACTIVE_COUNT=0x0000000000000000
[OS][KERNEL] FILE_DESCRIPTION_FINALIZATIONS=0x...
[OS][KERNEL] FILE_DESCRIPTION_FAILED_FINALIZATIONS=0x0000000000000000
[OS][KERNEL] FILE_TABLE_HARD_LIMIT=0x...
[OS][KERNEL] FILE_TABLE_CHUNK_ALLOCATIONS=0x...
[OS][KERNEL] FILE_TABLE_CHUNK_RELEASES=0x...
[OS][KERNEL] RUNTIME_STATE_VALIDATION=0x0000000000000001
[OS][KERNEL] SMOKE_STATE_VALIDATION=0x0000000000000001
[OS][KERNEL] PROCESS_RESOURCE_VALIDATION=0x0000000000000001
[OS][KERNEL] USER_EXIT_CODE=0x0000000000000000
[OS][KERNEL] PIPE_TRANSFER_VALID
[OS][KERNEL] PIPE_ENDPOINTS_CLOSED
[OS][KERNEL] FILE_DESCRIPTION_MODEL_VALID
[OS][KERNEL] FILE_SYSTEM_SYNCED
[OS][KERNEL] FILE_SYSTEM_PAYLOAD_VALID
[OS][KERNEL] FILE_SYSTEM_CONSISTENT
[OS][KERNEL] PROCESS_RESOURCES_RECLAIMED
[OS][KERNEL] SCHEDULER_COMPLETE
[OS][KERNEL] USER_RETURNED_TO_KERNEL
[OS][KERNEL] FILE_SIZE=0x...
[OS][KERNEL] LOAD_SEGMENTS=0x0000000000000003
[OS][KERNEL] READY
```

日志规范见 [docs/logging.md](docs/logging.md)：启动日志只记录阶段里程碑和故障原因，
不在轮询或逐字节路径中刷屏。

`build/developer/source/kernel/kernel.elf` 由 LLD 直接链接并保留完整
DWARF 调试信息；固定启动分区写入由 `llvm-objcopy --strip-debug` 生成的
`kernel.payload.elf`。这样 GDB 仍能使用完整符号，而启动载荷不会因调试段增长
覆盖 LBA 32768 开始的 rootfs 区域。两者入口都固定为 `0x00100000`，加载段
内容相同。当前产物包含严格分权的 `R E`、`R`、`RW/BSS` 三个
`PT_LOAD`；Stage 1 在目标机上以两遍算法先验证全部段，再复制文件内容并清零
BSS。成功交接后内核接管固件建立的 VGA 文本控制台，验证 104 字节 BootInfo v2、BSS 和
Stage 1 的 CR3，再加载自己的 GDTR、IDTR 和 TR。正常镜像执行一次可恢复
`INT3` 自检，随后验证内存图、分配器、页权限、堆和类型缓存。独立故障镜像分别执行
`UD2`、访问首个未映射地址，以及让 Ring 0 写入只读页；最后一项必须产生
错误码 `0x3` 的 #PF，证明 `CR0.WP` 和只读页权限真实生效。用户阶段另有
Ring 3 `#UD`、Ring 3 `#PF` 和截断 ELF 三条隔离/拒绝路径；用户错误不得
输出 `PANIC`，内核仍需继续到达 `READY`。

## 固定技术路线

- x86-64
- QEMU TCG
- freestanding C++20
- NASM Intel 语法
- Clang、LLD、GDB

项目不使用 SeaBIOS、OVMF、GRUB、Limine、Multiboot 或 QEMU `-kernel` 替代自研启动链。

## 目录结构

```text
source/          操作系统与 freestanding 基础模块
  kernel/        x86-64 Kernel；include/src 均按功能目录对称组织
tests/           单元、集成、随机和 QEMU 系统测试
tools/           Python 构建、检查、镜像和 QEMU 调度工具
docs/            需求、架构、分阶段学习、测试、调试和发布记录
books/           可独立构建的 LaTeX 系统教材
```

`source/abi` 保存用户态与内核共享的固定宽度 ABI，`source/user` 保存独立
用户 ELF 和系统调用包装。Kernel 的公开头文件位于
`source/kernel/include/os/kernel/<module>/`，实现位于
`source/kernel/src/<module>/`；两侧使用
`arch/boot/core/device/fs/io/ipc/memory/object/process/sync/time/user` 十三组对称目录，
禁止重新把文件堆到根目录。详细规则见
[Kernel 源码布局](source/kernel/README.md)，模块契约见
[docs/modules/kernel.md](docs/modules/kernel.md)。

从普通 C++ 与 PC 硬件前置知识开始、沿 v0.0 至 v1.0 第一周期逐阶段阅读，并
对照已经冻结的 v1.1–v2.0 第二周期实现与集成路线见
[docs/learning/README.md](docs/learning/README.md)。路线包含七册背景知识、
十四个第一周期阶段、v1.6 rootfs、v1.7 进程、v1.8 匿名虚拟内存与 v1.9
文件页缓存、v1.10 fork/COW、v1.11 Unix I/O、v1.12 用户线程、v1.13 时间
等待、v1.14 信号、v1.15 TTY/作业控制、v1.16 IRQ14/writeback 和 v1.17
ordered metadata journal、v1.18 ABI/devfs/procfs 深入章、v2.0 发布工程章，
以及一份 v1.1–v2.0 迁移地图；ROM、CPU、RAM、端口 I/O、
IRQ、ATA 磁盘与软件所有权的整体关系可先看
[整机硬件组装与连线图册](docs/learning/hardware-assembly-and-wiring.md)。
现实 N100 计算模组载板的十页原理图、三张逐引脚学习电路和 QEMU/实机边界见
[实体 x86-64 载板电路详解](docs/learning/physical-carrier-circuit-guide.md)。

完整教材入口见
[books/x86-64-os-from-reset/README.md](books/x86-64-os-from-reset/README.md)。
教材采用 6 部 16 个完整主题章；每章按“历史动机、机器事实、所有权、
正常提交、失败回滚、实现走读、验证证据、当前限制”的统一深度展开，并提供
跨章阅读地图与“解释、推导、观察、证伪”实验。构建时会自动统计仅进入目标
系统的 `.cpp`、`.hpp` 和 `.asm` 真实代码量。
可单独执行 `python3 tools/os.py source-metrics` 查看同一口径。
当前 v2.0 的精确统计由发布门禁生成；测试、工具、书籍、构建文件和
网站均不计入。
执行 `make -C books/x86-64-os-from-reset phone-export` 可按硬件教材相同规则
导出到手机书库的独立目录。

每个小版本只有在主工程、发布文档、教材、独立网站和 Sites 生产版本全部指向
同一份内容时才算完成。完整顺序、失败状态和交付证据见
[发布闭环](docs/releasing.md)；`web/` 始终是主仓忽略的独立 Git 仓库。

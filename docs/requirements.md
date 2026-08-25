# 项目需求

## 目标

通过从 CPU 复位向量开始实现一个 x86-64 教学操作系统，系统学习启动、处理器模式、内存管理、中断、设备、用户态、进程、同步、文件系统和用户环境。

## 固定约束

- 目标指令集为 x86-64。
- 使用 QEMU TCG 模拟硬件，不要求宿主机采用 x86-64 架构。
- QEMU 只提供硬件模型，不替代固件、引导程序或内核。
- 当前自动验收只使用 4 GiB `-mem-prealloc` 手机参考配置，不再把 64 MiB 或
  256 MiB 作为独立系统测试档；成功、持久化和故障 QEMU 均从同一规格取证。
- 4 GiB 是参考物理内存规格而不是实现上限；内核容量由 E820、处理器物理地址宽度
  和当前 direct-map 容量共同决定。
- 正式 QEMU CPU 型号与必需 CPUID 特性必须冻结并在启动时检查；v2.0 要求
  long mode、NX、SSE2 与 `SYSCALL/SYSRET`。
- 固件、磁盘加载、模式切换、ELF64 加载和运行时均由项目实现。
- COM1 不作为输出后端。启动诊断、用户终端和 panic 由显式路由决定：详细
  诊断保存在有界内存日志，TTY stdout/stderr 使用自研 VGA 文本控制台，
  panic 同时写两处。
- 高级语言使用 freestanding C++20，汇编使用 NASM Intel 语法。
- 宿主自动化使用 Python 3.11+ 标准库，构建图由 CMake 与 Ninja 管理。
- 不使用 SeaBIOS、OVMF、GRUB、Limine、Multiboot 或 QEMU `-kernel`。

## 质量要求

- 每个里程碑必须定义可自动化或可重复执行的验收标准。
- 正常、边界和失败路径必须具有明确的验证方式。
- 纯逻辑模块必须同时具备单元测试、模块集成测试和固定种子随机测试。
- 随机测试失败时必须报告种子、迭代位置和失败性质，确保故障可复现。
- 架构、重要决策、测试方案和复杂故障必须形成文档。
- 构建应当可复现，只追加内存日志和有界 QEMU 生命周期应当支持自动回归。
- 正常 QEMU 路径必须同时验证追加字节协议和非全黑可见 VGA 截图，不能用前者
  替代 DAC、字形与扫描输出的显示证据。
- 每个源码模块必须隔离公开头文件和私有实现，并由独立 CMake target
  强制单向依赖。
- Kernel 内部必须按功能所有权划分目录；`include/os/kernel/<module>` 与
  `src/<module>` 使用相同模块集合和相对头源路径，两个根目录不得重新堆放
  实现文件。生成与汇编例外必须具名登记并由完整回归检查。
- C++ 变量和参数的语义单词使用下划线分隔；普通函数使用大驼峰；命名空间
  每一层使用一个简短小写单词。完整回归必须通过 Clang AST 与命名空间词法
  门禁。

## v2.0 已冻结目标

v2.0 的目标不是成为完整 POSIX 或现代桌面系统，而是形成一个边界清晰、
能够解释核心机制的单处理器、多进程、多线程类 Unix 教学操作系统：

- 正常启动只由内核创建 PID1，PID1 从自研根文件系统启动 Shell 和其他程序；
- 用户程序以磁盘 ELF64 文件存在，通过 Spawn、Exec、Fork 和 Wait 形成
  父子进程树；
- Process 与 Thread 分离：Process 共享地址空间、文件表和文件系统上下文，
  调度器调度拥有独立 CPU 现场、栈、TLS 与信号掩码的 Thread；
- 内核采用“中断可进入、内核不可抢占”的单 BSP 执行模型；IRQ 不阻塞，
  调度只发生在显式阻塞/让出/退出和返回用户态前；
- WaitQueue 统一全部阻塞，条件、超时、信号、关闭和取消只允许一个
  WakeReason 获胜；spinlock、irq-save spinlock 和 sleep mutex 不混用；
- CpuLocal、`SYSCALL/SYSRET` 与 `INT 0x80` 共同进入统一 UserContext 和
  dispatcher；返回前验证 canonical 地址、RFLAGS 和特权状态；
- 每 Thread 使用 `FXSAVE/FXRSTOR` 隔离 x87/SSE2 现场；AVX/XSAVE 在 v2.0
  保持禁用；
- 物理内存、内核堆、进程栈、页表、描述符和 VFS 对象都具有可回收生命周期；
- VMA 描述地址空间意图，PTE 只表示当前驻留事实；用户地址空间支持按需
  ELF、匿名映射、`MAP_PRIVATE`、可写 `MAP_SHARED`、受控栈增长、写时
  复制和自研用户堆；
- VFS 统一根文件系统、设备和只读进程信息，根文件系统支持大文件、命名空间
  修改、同步、日志与崩溃重放；
- 描述符使用分块动态表，支持继承、dup、close-on-exec、动态管道和共享
  open-file description；默认 soft limit 为 256，hard limit 至少 4096；
- Shell 只保留必须修改自身状态的内建命令，其他命令从 `/bin` 执行，并支持
  流水线、重定向、环境、前后台任务和 Ctrl-C；
- 用户线程通过 TLS 和 futex 构造 mutex、condition variable 等同步原语，
  private futex 以 `(AddressSpaceId, aligned VA)` 为键，compare-and-block
  不允许丢失唤醒，unmap 必须取消相关等待；
- 信号、进程组、终端前台所有权和 sleep 使用阻塞/唤醒，不允许用户态或内核
  热路径忙等；
- 多线程语义固定：fork 只复制调用 Thread；exec 先构造候选映像，成功后才
  汇合兄弟 Thread；ThreadExit 与 ProcessExit 分离；信号处置属于 Process，
  signal mask 属于 Thread；
- clean page cache、dirty/writeback 和 ordered metadata journal 分阶段
  建立；事务必须预留 credits，并以 flush/commit/replay 证明恢复边界。

v2.0 发布时的历史功能矩阵如下；它只说明冻结发布证据，不再规定当前 QEMU 档位：

| 配置 | RAM | Process | Thread | 每 Process Thread | fd hard | Pipe |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| bootstrap | 64 MiB | 不规定 | 不规定 | 不规定 | 不规定 | 不规定 |
| functional | 256 MiB | 64 | 128 | 32 | 512 | 128 |
| capacity | 64 GiB | 256 | 512 | 64 | 4096 | 1024 |

capacity 另行验证 64 KiB pipe、1 GiB 稀疏磁盘、256 MiB rootfs、64 MiB
单文件、32 个独立用户 ELF 和 16 级流水线。`argv/envp` 合计 128 KiB 必须
用可回收页暂存，禁止放入大型内核栈缓冲。量化下限、运行频率和逐阶段验收见
[开发路线](roadmap.md)。

## v2.0 非目标

以下内容不进入 v2.0，以避免进程、内存、文件和用户环境主线被硬件广度稀释：

- SMP、多核调度和跨核 TLB shootdown；
- `msync`、swap、overcommit 和 OOM killer；
- 正/负 dentry cache、数据 journal、快照和在线扩容；
- AVX/XSAVE；
- 网络、图形模式/桌面、音频、USB、AHCI、NVMe 和通用 PCI 设备框架；
- 多用户权限模型、完整 POSIX、动态链接器、共享库和自举编译器。

这些边界不是永久放弃，而是后续版本的候选输入。v2.0 仍以 QEMU TCG 的
单个 x86-64 BSP 和传统 PC 设备为正式验收平台。

## v2.1 至 v2.6 固定范围

v2 后续严格拆为六个小版本，顺序与验收由 [开发路线](roadmap.md) 管理。
整个周期的最终目标是手机 QEMU 中可见、可操作、可持久化并能在资源压力下
恢复的离线本地类 Unix 环境。

- **v2.1**：4 GiB/128 GiB 参考机、VGA 前台、内存日志、PS/2 输入和手机
  noVNC 交互。
- **v2.2**：终端、Shell 组合语义和常用本地工具。
- **v2.3**：能使用完整参考盘的 rootfs v4、大文件和崩溃恢复。
- **v2.4**：本地身份、文件权限和资源限制。
- **v2.5**：回收、swap、overcommit 与 OOM。
- **v2.6**：只做集成、长稳、规范冻结和正式发布。

以下要求从 v2.1 开始生效：

- 手机参考 RAM 精确为 4096 MiB，并必须用 `-mem-prealloc` 实际提交；不得用
  32 GiB 惰性地址范围冒充物理内存。32 GiB 只保留为非手机可选压力档。
- 参考启动盘精确为 137438953472 字节，即 268435456 个 512 字节扇区，最后
  LBA 为 `0x0FFFFFFF`。工程镜像及故障副本保持稀疏；手机运行副本必须物化。
- v2.1 的 rootfs v2 仍固定为 256 MiB；完整使用 128 GiB 是 v2.3 的退出条件，
  不得提前宣传为已经可用的文件容量。
- VGA 共享输出头使用版本 3。终端激活前诊断可见；激活时清屏，激活后普通
  诊断只写内存日志，TTY 文本写屏幕，紧急输出无条件写屏幕。
- `qemu-display` 必须持续导出宿主日志文件；全部 QEMU 成功和失败路径同时
  验证内存记录与非黑 VGA 截图。
- 网络不属于 v2.1 至 v2.6。SMP、真机平台驱动、AHCI/NVMe、USB、GUI 和
  高分辨率 framebuffer 同样不进入本周期。

v2.2 的 Shell 组合要求：

- 一行最多包含 8 条控制命令，每条仍可包含最多 16 级管线；`;`、`&&`、`||`
  只在未引用、未转义位置生效，所有子命令执行前必须完成整行预解析。
- `>`、`>>`、`2>`、`2>>` 必须区分 stdout/stderr 与截断/追加；append 是
  FileDescription 状态，每次 write 都重新定位文件尾，duplicate 共享该状态。
- 单个解析计划必须小于 4 KiB；命令行 offset/length 的位宽由 512 字节上界
  静态证明，不能依赖隐式截断。
- rootfs 必须包含 43 个独立工具 inode；新增集合至少含 `/bin/err`、env、grep、
  find、sort、tail、df、du、hexdump、clear 和 date。
- Shell 环境最多 32 项、每项最多 127 个 `NAME=value` 字节；支持赋值、
  export/unset、`$NAME`、`${NAME}`、`$?` 和 exec 继承。未引用/未转义的 `*`、`?`
  才参与 glob，展开后仍服从每 stage 8 参数上限。
- ShellEditor 模式逐字节交付但不由 Kernel 回显；Shell 必须提供左右插入、
  16 条去重历史和命令共同前缀补全。外部前台作业必须切回 Canonical，任何
  失败路径都要把终端前台组和输入模式交还 Shell。
- ABI v2.1.0 是对 v2.0.0 的兼容尾部扩展：系统调用 70 切换受控输入模式，71
  返回 CMOS UTC 与 Unix 秒；既有 1..69 编号和 -1..-57 错误值不变。

v2.3 的 rootfs v4 要求：

- 生产 rootfs 从 LBA 32768 覆盖到 `0x0FFFFFFF`，总计 268402688 个 512 字节
  块；宿主 raw 文件的逻辑长度保持 137438953472 字节且空闲区保持稀疏。
- superblock magic 必须为 `OSRFV004`，以 64 位字段显式保存 journal、inode
  bitmap/table、data bitmap/area、inode 数、文件上限和 required features；
  格式 3 只能明确拒绝，不能静默迁移或重新格式化。
- 生产几何提供 65536 个 inode、65504 块数据 bitmap、4096 块 journal 和
  268300303 个数据/指针块；结构相同的较小几何只能用于有界容量测试。
- inode 保持 256 字节并加入四/五级间接根、atime/mtime/ctime/btime、链接与
  orphan 状态；单文件逻辑上限为 137369755136 字节，稀疏末块必须经五级树
  实际读写。
- 非目录硬链接维护精确 link count；符号链接支持绝对/相对目标和最多 40 次
  跳转。目录硬链接、损坏目标、超长展开和环路必须明确拒绝。
- unlink 或 rename replace 删除最后名称时，已有 OpenFile 继续有效；最后
  close 事务回收。若在两者之间断电，下一次 mount 必须先回收 orphan 再发布根。
- journal 至少提供 248 个 metadata credits；1000 个确定性断电点覆盖
  1..248 个 target，恢复只能得到完整旧状态或完整新状态，二次恢复必须 Clean。
- `mkfs-rootfs`、`inspect-rootfs`、`fsck-rootfs`、`corrupt-rootfs` 必须解析
  同一 v4；fsck 独立重建所有权/link/orphan 集合，高 LBA 用例必须从最后一个
  LBA 读回数据，故障与复制镜像不得物化 128 GiB 空洞。
- ABI 以 v2.2.0 兼容扩展 `FileInformation` 的四个 64 位纳秒时间戳；`stat`
  可见，普通 read 采用 noatime，不为读路径制造持久事务。
- Kernel 不得为完整数据 bitmap 常驻约 32 MiB BSS；Kernel 有界校验与宿主
  完整 fsck 分工必须写入模块文档和 ADR。

v2.4 的本地身份、安全与资源边界要求：

- 未被项目硬件或固定内存边界覆盖的用户可见规格默认采用 Linux/POSIX：32 位
  UID/GID/mode、root=0、umask 0022、mode 八进制位和 RLIMIT 0..15 编号；
- Process 保存 real/effective/saved UID/GID 与补充组；fork/spawn/exec 必须继承，
  set-ID exec 只改变 effective/saved，失败 exec 不得改变身份；
- VFS 必须逐组件检查目录 search；read/write/exec/chdir/readdir/truncate 和父目录
  mutation 各自检查所需权限，并覆盖 sticky 与 setgid 目录继承；
- rootfs v4 必须通过 required feature 激活 uid/gid/mode 字段，旧 v4 镜像明确
  拒绝；mkfs、Kernel 解码和完整 fsck 使用同一偏移与校验范围；
- RLIMIT_FSIZE/DATA/STACK/NPROC/NOFILE/AS 必须进入真实执行点，CORE 固定为 0；
  缺少对应设施的 Linux 编号不得伪报已约束；
- `/proc` 使用 root:root 0555/0444，`/dev` 使用 root:root 0755 和 root:tty 0660；
- ABI v2.3.0 只能在 71 后追加调用；FileInformation 扩为 112 字节。rootfs 至少
  提供 47 个独立工具 inode，并包含 chmod、chown、ln、readlink；umask 是改变
  Shell 自身状态的 builtin；
- 本版不得加入密码数据库、登录、网络身份、ACL、capabilities、LSM 或 user
  namespace。

v2.5 的内存压力与恢复要求：

- 单 BSP 只实现一个 Normal 内存域；按 min/low/high 三水位回收，不伪造 NUMA
  或多 zone。手机参考机报告并实际预分配 4 GiB RAM；
- 用户分配低于 low 前必须回收到 high，内核紧急分配可以使用 low..min 保留；
  先收缩未引用 clean page cache，再回写脏文件页，最后交换匿名/堆/栈页；
- secondary IDE master 提供 28 GiB 可用数据的独立交换盘，共 7340032 个 4 KiB 槽；
  每槽 64 字节磁盘元数据并独立校验，短写、短读、元数据提交失败或校验失败不得
  释放唯一槽；
- fork 必须保留已换出页的父子私有语义；unmap、exec、exit 和 OOM kill 必须释放
  未换入槽，正常整机结束 active swap 为 0；
- overcommit 模式编号采用 Linux 0/1/2，默认 0，严格模式为 swap + 50% RAM；
  匿名 mmap、brk 与 fork 提交，回滚和销毁成对撤销，正常整机 committed 为 0；
- OOM 基础分数按 resident+swap 占允许页比例映射到 0..1000，支持 -1000..1000
  adjustment；PID 1 与 -1000 不可杀，平分时按占用更大、PID 更小选择；
- `/proc/meminfo` 必须暴露 managed/free/allocated、resident limit、swap total/free、
  committed/commit limit 与 OOM kill 数；热路径不得逐页打印日志；
- 纯逻辑必须覆盖阈值、回收计划、commit 和 OOM oracle；swap 必须覆盖读写失败、
  校验损坏、容量、代次、clone 与释放；64/256 MiB 和 4 GiB QEMU 使用同一生产路径；
- 128 GiB rootfs 与 30534537216 字节交换盘的手机运行副本必须完整物化；
  `st_blocks * 512 < st_size` 时启动工具必须拒绝。故障矩阵副本可以保持稀疏；
- 本版不加入网络、SMP、NUMA、THP、zswap、休眠恢复、memory cgroup 或可写 VM
  sysctl。

v2.6 的集成冻结要求：

- 项目版本固定为 2.6.0；ABI 保持 2.3.0、84 个系统调用和错误区间 -1..-59；
  rootfs 保持格式 4，不为数字同步修改兼容边界；
- CMake、ABI/rootfs 常量、Kernel/PID1/Shell/探针 banner、QEMU marker、README
  和发布记录必须通过同一发布身份门禁；
- 发布清单必须绑定 40 位已推送主仓 SHA、目标源码规模、ROM/Kernel 完整 SHA-256、
  两块大盘结构化哈希、逻辑/已分配字节数和 sparse 状态；
- caw 候选必须在全新隔离目录通过全量分层、失败矩阵、三启动持久化和三轮
  4 GiB 已物化 soak；任一轮失败不得重试后忽略；
- 手机必须使用已物化 128 GiB rootfs 与 30534537216 字节交换盘，验证 noVNC
  横竖屏、输入、持久化、温度、存储余量和长稳；
- 教材、手机 PDF、主仓、独立网站、Sites 保存版本、生产部署和公网检查属于
  同一发布闭环；任一项未完成时不得声明 v2.6 正式发布。

## v2.7 通用块设备层与 NVMe 方向

v2.6 保留为未公开的主工程候选，后续开发从 v2.7 继续，不修改 2.6.0、ABI
2.3.0 或 rootfs v4 的冻结身份。v2.7 先建立设备无关块层，再实现一个自研 NVMe
运行驱动；不同时扩展 AHCI、virtio-blk 或其他存储控制器。

- `BlockDevice` 必须位于设备模块，VFS、rootfs、journal、swap 与页缓存不得识别
  ATA/NVMe 类型、端口、BAR 或命令格式；旧文件系统设备名只允许作为过渡别名；
- 驱动必须声明逻辑块大小、逻辑块数、单请求最大块数、最大 outstanding、写入与
  Flush 能力；通用队列不得引用 ATA 扇区或 LBA28 常量；
- 非整块传输、乘法溢出、末 LBA 越界、超传输上限、只读写入和不支持的 Flush
  必须在请求发布前失败，不得消耗 identifier 或队列槽；
- Queued 保持 FIFO 签发，Issued 数不超过设备深度，完成允许乱序；超时按最早
  deadline、再按 identifier 确定选择，迟到完成不得覆盖第一个终态；
- ATA PIO 继续负责 ROM、Stage 1、early Kernel 与回退，运行期声明 512 字节、
  LBA28、单块、单飞；该兼容路径的 IRQ14、超时复位和持久化证据不能退化；
- NVMe 首版只实现 QEMU NVMe 1.4 必选子集：单控制器、单 namespace、单 admin
  queue pair、单 I/O queue pair 和基本 Read/Write/Flush；PCI 枚举、BAR/MMIO、
  DMA 队列、doorbell、phase tag、超时复位与 MSI-X 均由项目实现；
- 不使用 virtio、宿主 passthrough、外部固件/驱动或 `qemu -kernel` 代替上述路径；
- rootfs/swap 只有在 NVMe 容量、Flush、故障、持久化和资源回收证据达到 ATA
  基线后才切换，接口存在或 Identify 成功不等于生产迁移完成。

第六增量已经满足上述迁移门禁：Kernel 运行期优先使用 NSID 1/2 的 rootfs/swap，
三档 QEMU、EIO/timeout reset、两次重启恢复、损坏拒绝和 ATA 自动回退均进入测试；
ROM 与 Stage 1 的 ATA 启动职责保持不变。

## v2.8 动态文件缓存地址空间要求

- 文件缓存身份必须包含 superblock/node 的 identifier 与 generation；fd 关闭、路径
  rename 或硬链接不得产生第二个页面身份；
- page index、计数、代次和物理地址均使用显式 `uint64_t`，索引必须覆盖
  `UINT64_MAX` 且 lookup 不扫描全部驻留页面；
- radix 节点只为实际出现的分支申请，插入失败不得发布部分 root/branch/leaf；删除
  最后一项必须释放空分支并在可行时收缩 root；
- Present、Dirty、Writeback、Error 标记必须在父节点聚合，范围查找不得进入没有
  对应标记的子树；
- 映射引用上溢/下溢、物理地址不匹配和非法状态转换必须在修改前失败；Dirty/Error
  不得直接删除，Writeback 和活动映射必须保持 Busy；
- 第一增量不得改变现有 VFS read/write、file fault、rootfs v4、ABI 或块设备行为；
- 单元、8192 页生命周期、十万步随机参考模型和目标 Kernel 小堆失败回滚必须通过，
  再开始生产读取路径迁移。
- 第二增量后 `FilePageCache` 不得再持有外部固定 entry 数组；VFS buffered read、
  ELF/file fault 与 `MAP_SHARED` 必须以相同 FileIdentity/page index 命中同一 frame；
- cache fill 必须使用显式 uncached 后端入口，不能递归进入 VFS read hook；
- 只有 superblock 声明 cacheable 的普通文件允许进入缓存；procfs 动态快照、devfs
  字符设备和当前 memfs 保持直读；
- 当前 4 GiB 验收必须报告 8192 页容量和 32 MiB metadata；metadata buddy block
  必须进入进程资源基线，来宾结束不得产生 frame 差异；
- metadata 申请失败必须同时回滚候选 frame、文件记录、page 和 radix 节点；
  buffered read 部分成功只允许返回已复制的完整前缀。
- 最终 payload 校验产生的 clean cache frame 必须在 storage shutdown 前归还，
  `FILE_CACHE_RECLAIMED` 之后才允许 NVMe 资源基线比较。
- 第三增量后，cacheable 普通文件的 `write`/`WriteAt` 必须先取得可跨 fd-close
  存活的后端引用，再修改唯一缓存页并标记 Dirty；写入不得撤销现有 shared PTE，
  同文件其他 fd 和 `MAP_SHARED` 必须立即观察到新字节；
- 写入跨页、部分页或越过旧 EOF 时必须预读仍有效的旧字节，并把洞和后端 EOF
  之后的区域保持为零；逻辑长度由缓存覆盖后端 `stat`，写回只提交 EOF 内字节；
- `MAP_PRIVATE` 的写时复制页不得被 buffered write 或 shared write 覆盖，也不得进入
  文件写回；
- `truncate` 缩小时必须先撤销文件偏移不小于新 EOF 的驻留映射；范围外 Clean、Dirty
  和 Error 页均可丢弃，Writeback 或活动引用返回 Busy，保留尾页从新 EOF 到页尾清零；
- `truncate` 扩大不得急切分配页面，旧 EOF 到新 EOF 的驻留缓存字节必须归零；
  后端 truncate 成功后，所有 FileBacking 长度与缓存逻辑长度必须同步更新；
- `sync` 必须先重新写保护 writable shared PTE，再把 Dirty/Error 页经
  `WriteUncachedAt` 写入，释放已无脏页的 writeback 打开引用，最后执行文件系统与
  设备 flush；失败页保持 Error，不能报告稳定成功。
- 第四增量后 cache miss 必须先发布唯一 Loading 身份并在全局 cache lock 外执行
  source read；成功只允许 Loading→Clean，失败必须移除 entry、frame 和空地址空间；
- Loading 页不得映射、脏化、淘汰、truncate 或 writeback；同页冲突不得启动第二次
  后端读取。early boot、受限 Kernel worker 或调度停止期可返回 Busy；合格 User Thread
  必须使用 v2.10.4 的 waiter，不得以单 BSP 为由退回重试；
- Dirty 后台水位、硬水位和回落目标分别采用约 10%、20%、5% 的容量比例，计算必须
  对小容量至少保留一个后台阈值，Dirty+Writeback+Error 共同计入硬水位；
- 软水位请求必须合并，worker 每次最多写回 64 页并持续到目标水位；worker 只能在
  非 IRQ 用户返回安全点运行，每批前写保护全部 writable shared alias；
- 普通 write 发现硬水位时必须先执行有界平衡；后台写回失败保留 Error 并暂停自动
  重试，不能在每次用户返回时忙循环，显式全局 sync 可以重新尝试。
- 第五增量的写回错误必须按稳定文件身份形成单调序列；每个独立 open 采样自己的
  游标，duplicate/fork 共享 FileDescription 游标。一次错误向同一打开实例最多报告
  一次，错误发生后才打开的实例不得继承该历史错误；
- `fsync`/`fdatasync` 必须只选择目标文件的 Dirty/Error 页，写回后检查并推进该
  FileDescription 的错误游标，再执行 VFS metadata 与设备 Flush。当前 fdatasync
  可以安全地多刷新 metadata，但不得少刷新文件大小和读取数据所需信息；
- `msync` 地址必须页对齐、长度非零且完整覆盖 file-backed VMA；flags 必须恰含一个
  ASYNC/SYNC，可附加 INVALIDATE。MAP_PRIVATE 不回写，MAP_SHARED 只写指定文件页范围；
- MS_ASYNC 只排队并由有界 safe-point worker 推进，不能把整个范围同步写完后伪称
  异步；MS_SYNC 返回成功前必须完成范围写回和设备 Flush；
- ABI 2.4.0 只在 84 后追加 85..87，旧系统调用编号、结构布局、rootfs v4 和错误区间
  -1..-59 均保持不变。
- 第六增量的 direct/background reclaim 必须共享同一 0..200 swappiness planner；
  file 配额内部固定执行 clean file trim、dirty/error writeback 后 trim，再按匿名配额
  swap。候选不足允许转赠配额，任一 I/O 阶段失败不得伪装成 OOM；
- 回收成功后必须从物理分配器重新同步 resident 账本并只重试一次原分配；仍无法满足
  时才允许 OOM。非当前 victim 完整释放后重试，当前 victim 只在 page-fault frame
  上走 SIGKILL；
- 跨进程匿名回收必须保存轮转游标和每地址空间扫描游标，单轮最多扫描 65536 页；
  PID1、当前 fault 页和活动用户返回栈页不得换出，多用户栈地址空间保守跳过；
- 4 GiB reclaim-pressure 门禁必须使用 `-mem-prealloc`、完整生产 rootfs 和真实交换盘；
  测试驻留 limit 只能在 PID1 建立并同步真实 resident 后降低，不能用伪造页计数；
- reclaim-pressure 必须观察 clean reclaimed、dirty written/reclaimed、anonymous swapped
  非零，no-progress、writeback failure、swap checksum failure 和最终 active swap 均为零；
- OOM pressure 必须使用 swappiness 0、真实非当前 victim 与 SIGKILL wait/reap，观察一次
  no-progress/invocation/kill、零 anonymous swap，并在 ATA/NVMe 上保持 PID1 和资源守恒。

## v2.9 内核后台执行要求

- ThreadScheduler 必须用强类型区分 User/Kernel Thread；Kernel Thread 不得进入
  Process 线程链、进程树、OOM 候选、SignalManager 或用户地址空间；
- User TID 必须保持从 1 开始且小于 `0x8000000000000000`；Kernel TID 从该边界开始，
  两类标识不得因槽位复用而重复；
- Kernel Thread 必须使用 KernelStackManager 动态栈和双 guard，初始/保存 RSP 始终
  位于活动栈内；恢复调度栈前不得销毁退出线程的栈；
- 协作切换必须保存 SysV callee-saved 寄存器、RFLAGS、RSP 和 FXSAVE 状态；切换提交
  期间关闭中断，CpuLocal/TSS 必须先指向下一线程；
- create、yield、block、wake、exit、reap 必须拥有类型化失败结果。创建中途失败必须
  撤销 scheduler entry、动态栈、KVA 和 frame；
- 独立批次 API 仍必须拒绝已有 User Thread；生产 dispatcher 必须能在 User/Kernel
  两类 Running 上下文之间切换，并成对收束用户 CR3、FS、FXSAVE、TSS、CpuLocal、
  SYSCALL 状态和 IRQ depth；Ring 0 仍为协作式，不伪称 timer 抢占；
- 单元测试必须覆盖非法栈、用户专属状态拒绝和完整生命周期；固定种子随机模型必须
  混合两类 Thread；4 GiB QEMU 必须真实执行 yield、block/wake、exit/reap 并最终归零。
- WorkQueue 必须使用 `(slot,generation)` 句柄拒绝槽位复用后的 stale 操作；非 Free
  条目必须恰处于 Idle/Delayed/Queued/Running/Completed/Cancelled 之一；
- 即时任务必须 FIFO；延迟任务必须用 deadline、提交 sequence 形成稳定最小堆，只有
  到期后才能进入 ready FIFO；worker 必须能读取最早 deadline，新的即时请求必须把同一
  handle 的 Delayed 项提升到 ready，而不是被旧 deadline 拖住；
- 相同 handle 已经 Delayed/Queued/Running 时必须合并，不得产生第二份执行；Queued/
  Delayed 可取消，Running 不得伪装成已取消；
- worker 必须先把条目提交为 Running、释放队列锁，再调用 operation；失败任务只能增加
  自身失败统计，不能停止或丢弃后继任务；
- drain 必须封闭新注册和 Idle 提交，且仅在 Delayed/Queued/Running 全为零后结束；
- WorkQueue 单元与十万步随机参考模型必须覆盖容量、stale、合并、deadline、取消、失败、
  reset/release 和 drain；4 GiB QEMU 最终必须 registered/queued/delayed/running 全为零。
- 常规文件回写必须由常驻 Kernel Worker 在 WorkQueue 锁外执行；低于后台软水位但已有
  Dirty 页时按 5 秒老化 deadline 提交，显式/软水位请求走即时提交；timer IRQ 只到期
  deadline 和请求重调度，不得调用 VFS 或设备 I/O；
- Worker 每个最多 64 页的 batch 后必须协作 yield；硬 Dirty limit 的同步回写保留为
  direct backpressure 前进保证，`fsync`/`fdatasync`/同步 `msync` 保持调用方同步语义；
- 最后一个 User Thread 退出时必须取消残余延迟项、唤醒并停止 Worker，再按
  exit→reap→stack destroy→handle release 顺序恢复 scheduler、wait queue 和资源快照。
- PTE Accessed 采样必须只修改 4 KiB leaf 的 A 位，保留物理地址、权限、COW 与 NX；
  目标 CR3 当前活动时必须 `invlpg`，非活动 CR3 依赖下次 CR3 装载刷新；
- aging 身份必须是 `(physical frame, File/Anonymous kind)`，同一轮所有 alias 的 Accessed
  取 OR、reclaim eligibility 取 AND；同轮分类冲突必须失败，跨轮已释放帧复用允许重分类；
- 必须维护 active/inactive × file/anonymous 四条无重复队列。新身份先 Active，一轮
  未访问降级，连续第二轮未访问且资格成立才成为候选，Inactive 被访问必须提升；
- PID1、UserStack、COW alias、映射中的 file page 和非 Clean file page 不得成为候选；
  Zombie 已销毁 CR3 时必须跳过，拥有 CR3 的活动进程 VMA/页表损坏必须失败；
- aging 元数据必须由真实物理帧和 KVA 映射承载，不得扩大 Kernel BSS 或使用稀疏宿主
  文件代替内存；4 GiB 功能档至少 4096 entry，32 GiB 容量档使用 32768 entry；
- 第四增量只统计 candidate，不得释放、writeback、swap 或改变 OOM；单元、十万步随机
  和 4 GiB QEMU 必须覆盖 alias、冷热转换、帧重分类、消失页、容量与最终队列归零。
- 第五增量必须把 candidate 保存为显式条目状态，刚由 Active 降级的页不得通过
  `Inactive && eligible` 反推为候选；文件缓存 access generation 改变时必须撤销旧候选，
  重新经过冷却后才能消费；
- 后台回收控制器必须在 free pages 低于 low watermark 时从 Sleeping 进入 Running，达到
  high watermark 后回到 Sleeping。low 到 min 之间用户分配只合并后台请求，低于 min
  才允许同步 direct reclaim；
- 后台 WorkItem 每批最多处理 64 页，并按 cold clean file、dirty/error writeback、cold
  anonymous swap 顺序消耗同一批 I/O 预算；回收与 I/O 不得在 IRQ 或 WorkQueue 锁内执行，
  每批后必须 yield；
- 没有候选、仅完成写回或执行失败时必须进入有 deadline 的退避，不能立即自旋重排；
  写回设备失败保留 Error/paused 语义，结构损坏必须使运行时验收失败；
- 文件页和匿名页成功回收前必须提交精确候选 completion，从 PageAging 删除旧物理身份；
  selection/completion 失败不得把未选页计入进展。停止时 writeback、aging、background
  三个 handle 必须取消、reset、release，controller、deadline、队列与 waiter 全部归零；
- 4 GiB ATA/NVMe pressure 门禁必须稳定观察后台 wake/sleep、batch、clean file、writeback
  和总回收非零、后台 failure 为零；第六增量已冻结 direct/background 共用的
  swappiness 配额，系统总 anonymous swap 仍必须非零。

## v2.10 异步块 I/O 与可等待页缓存要求

- BlockRequest 必须分别维护 submit FIFO 与 completion FIFO；完成 FIFO 只按首次解析
  发生顺序排列，不得按 identifier 或原提交顺序重排；
- IRQ、timeout、cancel 对同一请求仍只能有一个赢家。成功、设备错误、超时和取消必须
  恰产生一条 Completed 身份，重复解析只增加拒绝统计；
- `BlockCompletion` 必须携带 request id、operation、LBA、block count、owner thread 和
  terminal result；Take 后请求槽立即可复用，旧 id 必须失效；
- completion 为空必须返回 `available=false`，不得忙等、伪造错误或改变计数；
- 直接 Reap 已完成请求时必须从 completion FIFO 任意位置摘除，并保持其余顺序；
- `submission=active+reap`、`resolution=completed+reap`、`delivery<=reap` 必须由
  `Validate`、单元、集成和十万步随机 oracle 同时检查；
- 第一增量只建立完成交付契约并迁移 ATA 内部消费，不得提前宣称 rootfs 或 NVMe 已完成
  异步迁移；后续增量才能引入 BlockIo WaitQueue 和 FilePageCache Loading waiter；
- 第二增量的 `AsynchronousBlockDevice` 必须与同步 `BlockDevice` 并列，不得迫使同步调用方
  伪造 owner/deadline，也不得使用 virtual、RTTI、异常或动态分配实现分派；
- ATA 与 NVMe namespace 必须通过同一 geometry、submit、best-effort cancel、timeout、
  completion 接口工作；控制器端口、BAR、doorbell、phase 和 CID 不得泄漏给上层；
- NVMe 公共 64 位 request id 必须与 16 位 command id 分离。CID 回绕不得命中仍活动或
  待交付的旧请求，完成值只能携带公共 request id；
- NVMe IRQ 只能解析 CQE 和发布完成，不得在 IRQ 中执行最多 64 KiB DMA 回拷；Read 数据
  在非 IRQ TakeCompletion 阶段复制，复制完成前槽位和 caller buffer 所有权不得释放；
- NVMe timeout reset 必须让最早到期请求得到 TimedOut、其余未完成异步请求得到
  DeviceError，并保留 reset 前已排队的完成；
- cancel 是 best-effort：ATA queued 请求可以 Cancelled，已签发 ATA/NVMe 请求必须返回
  RequestInProgress 且保持原状态，不得宣称硬件命令已经撤销；
- 第三增量 3a 的 `BlockIoCoordinator` 必须同时匹配 owner 与 request id，并以 generation
  ticket 拒绝槽位复用后的旧等待；每个 owner 同时最多登记一个请求；
- completion-before-wait 必须使 `PrepareWait` 直接返回完成，wait-before-completion 必须只
  唤醒精确 Kernel Thread，abandon-before-completion 必须消费迟到完成但不唤醒旧 owner；
- IRQ14、IRQ15、MSI-X 与 timer 只允许解析终态并通知 completion Worker；
  `TakeCompletion`、NVMe Read DMA 回拷、协调器交付和 WaitQueue wake 必须在非 IRQ 上完成；
- completion Worker 扫描为空后必须在关中断区复核单调通知 generation，再提交阻塞；
  不得用无界轮询规避扫描与睡眠之间的丢通知窗口；
- `AwaitRuntimeBlockIo` 只允许具备独立 Kernel stack 的 Kernel Thread 或 User Kernel 续体
  调用。设备接受请求后，如果协调登记、等待提交、唤醒原因或结果提取损坏，必须 fail-stop，
  不能返回并释放仍归设备所有的 buffer；
- User Kernel 续体必须成对保存/恢复 FX、系统调用入口、双 GS 基址状态和 CR3 模式；恢复
  地址空间销毁路径时必须保持 Kernel page table，不能重新激活已释放的用户 CR3；
- rootfs、journal、BlockCache、VFS resolution、文件后备、swap 和打开文件 payload 的可睡眠
  临界区必须使用 `RuntimeMutex`；IRQ、持有其他 spin lock 和运行时未就绪路径不得睡眠；
- 新 User Thread 必须以不可调度的 `Initializing` 状态完成跨模块初始化，全部元数据提交后
  才能发布为 `Ready`，失败必须在从未运行过的前提下逆序丢弃；
- 第三增量 3a 的历史证据经 secondary ATA IRQ15 完成一次真实 Kernel wait；3b 生产证据
  必须要求 coordinator registration/wait/completion 非零且严格相等、root async operation
  非零，发生匿名换出的 profile 还必须要求 swap async operation 非零；
- 最终生产异步路径不得在 IRQ 中分配、阻塞或调用 VFS，EIO/timeout/cancel 必须唤醒原
  owner，退出和 unmap 必须能撤销或遗弃请求且不写入已释放 buffer。
- 生产 root/swap NVMe controller 是系统级持久资源，正常 READY/事件循环不得提前关闭；
  独立 probe、EIO/timeout recovery 的 shutdown 仍必须回收 DMA/MMIO/PCI/MSI-X。
- Process 退出必须先使 Scheduler 进入 `Zombie`，再发布 ProcessTree 退出事件并唤醒父进程；
  共享 ChildProcess 唤醒不得形成进程树已收集、调度器尚未回收的半提交。
- 第四增量的 `FilePageLoadCoordinator` 必须使用固定容量 load 槽、per-thread waiter 和
  generation token；同一 owner 同时最多登记一个 load，旧 token 不得命中复用槽；
- 同页冲突必须在观察 `Loading` 的同一 cache 临界区登记 waiter 后才解锁；等待状态与
  WaitQueue 入队必须在 scheduler lock 内一次提交，完成先于等待时不得阻塞；
- 成功 owner 必须在完成广播前为全部已登记 waiter 预留真实 mapping reference；waiter
  醒来接管该引用，owner 先释放时 invalidate、truncate 和 reclaim 仍必须返回 Busy；
- 失败 owner 必须先撤销 Loading entry、frame 和空地址空间，再向同期 waiter 广播同一
  终态；waiter 不得递归发起第二次来源读取；
- `begin=completion`、`waiter registration=result take`、`wait commit=broadcast wake` 必须
  由 coordinator `Validate`、固定种子随机模型和 QEMU 聚合 marker 联合验证；
- waiter 登记后的协议损坏必须 fail-stop。early boot、IRQ、调度停止期和受限 Kernel
  worker 不得假装可睡眠，仍保留明确 Busy 边界；
- 第四增量不得改变 ABI 2.4.0、rootfs v4、磁盘格式或 Loading 页的 writeback/reclaim/
  truncate/invalidate 禁止规则。
- 第五增量 5a 的预读策略必须是无分配纯状态，不得直接访问 VFS、page cache、设备、
  scheduler 或 WorkQueue；一个实例只表达一个未来打开文件流；
- 默认最大窗口必须明确为 32 个 4 KiB 页。初始窗口按请求页数向上取二次幂并执行 Linux
  1/32×4、1/4×2 分档，后续窗口按小窗口 4 倍、普通窗口 2 倍增长并封顶；
- 首次/连续 DemandMiss 可以建立窗口，DemandHit 只推进流；PrefetchedHit 必须连续且覆盖
  当前唯一触发页。随机访问必须清除窗口，EOF 必须裁剪且禁止零页提交；
- Balanced、BelowHigh、BelowLow、BelowMinimum 的压力上限必须分别为配置上限、1/2、
  1/4、0；非零压力上限至少保留 1 页，BelowMinimum 必须清除活动窗口；
- wasted 大于 useful 时自适应上限向上取整减半，useful 大于 wasted 时翻倍恢复且不越过
  配置上限；反馈不得修改已经发布的 generation 范围；
- 非法配置、零长度或越界访问、伪造 PrefetchedHit、空反馈、计数上溢和 generation 耗尽
  必须保持策略状态不变；`Validate` 必须独立重算分类、窗口、压力和统计守恒；
- 5a 只冻结策略；第五增量 5b 必须把策略实例放入共享 FileDescription，使 duplicate/fork
  共享 offset 与预读状态、独立 open 隔离，并从 VFS 缓存读取获得实际页 hit/miss 观测；
- 5b 预读任务必须使用固定容量 FIFO、generation token 和 retained OpenFile，由常驻 Kernel
  worker 异步执行；队列满只拒绝预测，不能回滚已成功的 demand read，退出前必须排空任务；
- FilePageCache 必须区分 Demand/Prefetch。新预取 Clean 页带 one-shot 标记，首次 demand
  获取消费为 useful；invalidate/truncate/reclaim/trim 丢弃未消费标记必须计为 waste；
- 预读 Kernel worker 只可成为新 Loading owner，不得等待已有同页 Loading；缓存满或
  BelowMinimum 时停止预测，不得为预读驱逐 demand 页；
- `enqueue=completion+active` 与 `successful prefetch=resident+useful+wasted` 必须由
  Validate、host 测试和 4 GiB QEMU 聚合 marker 联合验证；最终 active、failure 和
  prefetched resident 必须为零；
- 第五增量 5c 必须用固定容量 stream token 账本保存 producer 归因，不得把
  FileDescription 指针写入 page cache；duplicate/fork 共享 token，独立 open 隔离；
- 预读页必须保存 stream token 和 policy generation。首次 Demand 记录 useful，定向取消、
  truncate、invalidate、reclaim/trim 记录 waste；recorded、taken、stale、pending 必须守恒；
- queued cancellation 必须稳定摘除 FIFO 并交还 retained OpenFile 所有权；running 只置取消
  标志，每页前检查，已提交的单页 BlockIo 完成后再丢弃，不能伪造设备硬取消；
- random reset 只取消旧 generation，BelowMinimum 和最后共享描述 close 取消全部 generation；
  truncate 必须在修改映射/缓存前取消同文件任务；
- `enqueue=completion+queued cancellation+active`、`task retain=release+active`，关闭后迟到
  反馈只能计入 stale，不能命中复用 token 或访问已销毁策略；
- 第六增量必须用独立固定容量 coordinator 管理 per-page Writeback；token 必须绑定文件页、
  物理地址和 writeback generation，旧 token 不得命中复用槽；
- 同页 MarkDirty、fsync/fdatasync 和同步 msync 必须在观察 Writeback 的 cache 临界区登记
  waiter，再解锁进入 per-slot WaitQueue；completion-before-wait 不得丢失；
- 写回成功后 writer waiter 才能把 Clean 重新脏化，同步 waiter 必须继续扫描范围；EIO、
  timeout 或 cancel 映射出的失败必须原样广播，不得递归提交第二次 I/O；
- `begin=completion+writing`、`waiter registration=result take+active`、
  `wait commit=broadcast wake+waiting` 必须由 Validate、十万步随机模型和 QEMU 聚合验证；
- Clean reclaim 必须能越过 Writeback 回收其他候选，不能释放设备仍在读取的 frame；
  direct/background 脏页回收必须经公共 writeback 等待完成后再回收；
- Loading 仍禁止 writeback/reclaim；取消中的预读若已经提交 BlockIo，必须等待唯一终态后
  丢弃。已签发 ATA/NVMe 请求保持 best-effort cancel，不承诺硬件级 abort；
- 第六增量故障矩阵必须组合页级 success/failure 广播、公共 BlockIo 的
  Succeeded/DeviceError/TimedOut/Cancelled，以及 4 GiB ATA/NVMe primary、reclaim、OOM、
  EIO/timeout 恢复与三启动 persistence；ABI 2.4.0、rootfs v4 和磁盘格式保持不变。

## v2.11 VFS 元数据缓存要求

- 第一增量只能建立不接生产 VFS 的纯状态模型，不得声称已经减少后端 lookup/stat I/O；
- dentry key 必须包含 mount identifier、parent superblock/node identifier+generation、名称
  长度与全部名称字节；不得只用 hash、裸指针或 inode number 作为身份；
- inode identity 必须包含 superblock 和 node 两级 identifier+generation，且不包含 mount，
  允许多个 mount dentry 共享同一 inode；
- 名称只允许 1..255 字节，拒绝控制字符、DEL、`/`、`.` 与 `..`，未使用的固定存储必须为零；
- dentry 必须区分 Positive/Negative kind 与 Free/Cached/Stale state；Negative 不得持有 inode
  token，EIO/Corrupt/PermissionDenied 不得发布为 Negative；
- 失效必须先从 Cached lookup 集合删除。有外部引用的旧项进入 Stale，同 key 新 Cached 项可
  并存；最后 release 后槽位复用必须增加 generation，旧 token 永久无效；
- inode 必须分开统计 Positive dentry 引用和外部引用。inode 失效必须级联所有指向它以及以
  它为 parent 的 Cached 正负 dentry；零引用 Stale 不得留存；
- dentry LRU 只能回收 Cached 且零外部引用项；inode LRU 还必须要求零 dentry 引用；
- `Validate` 必须独立重算全部当前计数、Positive→inode 引用、Cached key 唯一性、Stale 引用
  合法性和父级失效闭包；
- unit、跨 mount/rename integration 与具名固定种子十万轮 randomized oracle 必须覆盖正常、
  边界、容量拒绝、类型冲突、ABA、失效和回收；
- 第一增量不新增运行期日志、QEMU marker、系统调用、盘面格式或网络/硬件能力。
- 第二增量必须缓存完整 backend inode metadata，并以 Empty/Loading/Ready 和独立 generation
  ticket 阻止失效后的迟到 fill；Loading 不得被 LRU 回收；
- `Stat`、权限检查、普通/目录/exec 打开、sticky/创建检查与打开文件 stat 必须共享 inode
  identity；显式 uncached API 必须继续旁路，FilePageCache 逻辑 size 不得污染原始 metadata；
- cache miss owner 只能在 spin lock 外访问 backend。并发 Loading、容量或 generation/counter
  耗尽必须正确旁路，缓存不可用不得改变 backend 结果；
- chmod/chown/write/truncate/create/link/rename/unlink/rmdir/symlink 成功后必须覆盖全部受影响
  target/parent identity；backend 修改失败不得提前失效；
- 生产内核必须使用调用方固定 BSS 槽，不得在 metadata 热路径分配、睡眠或逐项打印；
- 第二增量必须覆盖非法 mode/type、失败取消、ABA、Loading/容量旁路，以及全部生产修改失效
  矩阵；`Validate` 必须重算 Loading/Ready 计数和字段/状态相容性。
- 生产 component lookup 必须先查完整 dentry key；Positive/Negative 命中不得访问 backend，
  同 key 并发 miss 最多一个 backend owner；
- 只有明确 NotFound 可以发布 Negative，DeviceFailure、Corrupt、PermissionDenied 与未知错误
  不得改变下一次 lookup 的 backend 可达性；
- create/mkdir/symlink/link/rename/unlink/rmdir 的 backend commit 与全部受影响 dentry/metadata
  失效之间不得存在 resolver 可见窗口；锁序固定为 resolution 后 metadata；
- mount identifier 必须参与 key；mount crossing、cwd/root、符号链接、DAC、orphan 与打开 vnode
  generation 继续由 VFS 原语义决定；
- 生产 hash 必须使用调用方固定 storage，完整 key 二次判等；Cached 槽恰好进入一个 bucket，
  Free/Stale 不得入 index，碰撞、删除和复用必须由 Validate 覆盖；
- namespace shrinker 只能回收零引用 Cached 项，Loading/引用/Stale 不得参与；后台压力回收
  必须有界，固定 BSS backing 不得计入 reclaimed physical pages；
- 最终必须通过 production-cache 并发/错误/修改 integration、memfs/rootfs 十万步 oracle、
  ATA/NVMe primary、reclaim/OOM、EIO/timeout 与三启动 persistence。

## v2.12 可扩展、页后备命名空间要求

- dentry miss 和 inode metadata miss 必须各按 64 个稳定 identity shard 协调；同 key 只能有
  一个 backend owner，不同 shard 不得被全局 resolution/metadata 锁串行；
- 生产 path walk 必须从 128 槽独立 scratch pool 获取上下文，容量耗尽明确失败，路径热区
  不得动态分配；
- metadata 多对象写操作必须将 shard 去重、升序加锁、逆序释放；namespace mutation 继续
  单写，锁内不得等待用户指针或设备 IRQ；
- namespace sequence 必须以偶数表示稳定、奇数表示 commit/invalidation 进行中；resolver
  起止序列不同不得返回结果，重试必须有界；
- page-backed layout 必须检查乘法、加法、对齐和地址范围溢出；错位、空或不足 storage 必须
  fail closed；每个配置页都必须由真实 frame+KVA backing，不得使用稀疏宿主假容量；
- hash bucket 必须支持持锁在线重建，重建前预检全部 slot/index；切换 compact 指针后才可
  释放 preferred backing；
- 资源账本必须记录实际 frame、buddy block、KVA page/descriptor/allocation 差值；首次压力
  回收只允许释放一次 preferred tier，稳定 slot/context 页保留到 teardown；
- 用户 resident budget 必须排除长期不可回收 namespace frame，preferred 页释放时同步减少
  排除数；9216 页 reclaim profile 不得修改；
- 不新增逐 lookup/stat/lock VGA 日志；验证必须覆盖同 key 合并、不同 shard 并行、metadata
  并行、mutation retry、hash rebuild、release、溢出、十万步 oracle 和 4 GiB primary/reclaim。

## v2.13 目录句柄与 `*at` 路径事务要求

- `DirectoryHandle` 必须保留 mount+vnode identity 和后端打开引用；目录 rename 后旧句柄
  继续工作，release 后再次使用必须返回 InvalidHandle；
- 绝对路径必须忽略无关 `dirfd`，`AT_CWD` 使用 Process cwd；其他相对路径必须从类型为
  Directory 的 FileDescription retain，普通文件、关闭或越界 fd 必须失败；
- 单路径 `open/open-directory/mkdir/remove/stat/readlink` 和双路径 `rename/link/symlink`
  必须共用 VFS 解析、DAC、root clamp、mount crossing、symlink 与 namespace cache 语义；
- 双路径调用必须分别解释 source/destination 基准，但使用同一进程 root/credentials；两侧
  目录 lease 无论成功或失败都必须释放；
- namespace writer 必须在首次解析前取得单写锁，backend commit 前精确复验捕获的偶数
  sequence；并发同名 create 二次解析成功后必须复用 vnode，不得递归取得 writer；
- ABI v2.5.0 只追加 88..96，保留 1..87、-1..-59 和既有结构；复杂请求结构大小/偏移、
  `AT_CWD`、remove/stat flag mask 必须静态与单元双重冻结；
- 用户指针必须在进入 VFS 前完整复制/验证；两个最大路径不得同时放入 16 KiB Kernel 栈；
  descriptor/object 锁不得跨 VFS、后端 I/O 或 user-copy；
- VFS 必须统计 at operation、directory retain/release/active/peak 并在 `Validate` 守恒；不得
  逐 syscall、路径、fd 或 sequence 向 VGA 打印；
- hosted integration 必须覆盖非法/绝对/rename 后句柄、跨 parent、并发同名和不同名 create；
  4 GiB ATA primary 必须由 Ring 3 在真实 rootfs 执行九类调用并满足 VGA 可见性与资源归零。
- RootFS 4 KiB block scratch 与 pressure/worker 后处理帧必须在 writeback/swap 设备 I/O 链上
  分离；不得以扩大 16 KiB 动态 Kernel stack 或删除双 guard 掩盖 ATA/NVMe reclaim 栈不足。

## v2.14 打开文件描述与定位 I/O 要求

- regular file seek 必须支持 Beginning/Current/End；计算不得发生有符号溢出，负结果、超过
  `INT64_MAX` 的结果和未知 origin 必须失败；pipe、terminal 与目录必须返回 `NotSeekable`；
- duplicate/fork 必须共享 FileDescription offset；pread/pwrite 无论成功、部分完成或失败都
  不得改变 offset，普通顺序 read/write 必须从最新共享 offset 继续；
- append 必须在所有独立 open description 之间原子选择缓存逻辑 EOF 并写入；RLIMIT_FSIZE
  必须在该串行点内按实际 EOF 裁剪，不能在 ProcessRuntime 用过期 size 预计算；
- Linux 兼容的 append+pwrite 必须忽略调用 offset 写到 EOF，但不得推进共享 offset；关闭
  append 后 positioned write 才使用显式 offset；
- fstat/ftruncate/fchmod/fchown 必须针对打开 vnode 身份，不得重新解析原路径；truncate 必须
  先取消预读、写保护共享映射、释放 EOF 外映射并等待同文件 writeback，再修改后端和 cache；
  不得先提交盘面 size 后因 cache `EntryBusy` 向用户报告失败；权限与 owner 规则复用路径版本；
- 全局映射写保护/truncate 扫描只能遍历 `address_space_stable` 且 Alive/Stopped 的进程；
  owner 必须在退出/OOM Destroy 前清除 stable，不能以 active 或非零 CR3 替代；
- buffered write 必须在阻塞式 page Acquire 后 retain writeback backing，并在无阻塞窗口内发布
  Dirty；backing size 必须覆盖完整 write end，clean release 不得抢在 Dirty 之前关闭描述；
- file status get 必须返回 Readable/Writable/Append；set 必须保留 access bits，只允许 writable
  regular file 改变 Append，且不得混入 close-on-exec descriptor flag；
- ABI v2.6.0 只追加 97..105 和 -60，保留 1..96、旧请求布局、rootfs v4 与磁盘格式；所有用户
  指针必须在对象/VFS 锁外复制和验证；
- FileDescription 必须统计 seek、positioned read/write、metadata 与 status update；正常路径
  不得逐 fd/offset/syscall 输出到 VGA，失败路径只保留聚合诊断；
- hosted lifecycle 必须覆盖 shared offset、positioned offset 不变、append 与非法 flags；Ring 3
  必须在真实 rootfs 覆盖九项新调用，并通过 4 GiB ATA primary、ATA/NVMe reclaim 和完整
  fresh CAW verify。

## V2 小型 ext4 核心终态要求

- V2 生产根必须是自研 rootfs v5；采用 ext4 的 4 KiB block、block group、extent、HTree、
  ordered journal 和 xattr/ACL 等核心结构，但不得复制 Linux 源码或宣称 ext4 盘面兼容；
- v5 必须使用项目 magic、compat/ro-compat/incompat feature、显式小端字段和 CRC32C；未知
  required feature、错误几何、越界元数据、非零保留位或 checksum 错误必须拒绝 mount；
- 128 GiB phone-primary 必须按约 128 MiB group 划分，inode 密度由 mkfs profile 保存到盘面；
  Kernel 只按需读取 group/inode 元数据，不得常驻整盘 bitmap 或 inode table；
- 普通文件必须使用有界 extent tree，区分 hole、unwritten 与 initialized；分配器必须支持
  连续多块、组内局部性和 delayed allocation，ENOSPC 不得暴露旧介质字节；
- 目录必须使用变长记录与名称 hash 索引；冷 lookup 的后端块访问受索引深度约束，不能以
  VFS dentry 热缓存掩盖线性扫描；
- journal 必须支持 descriptor、revoke、commit、checkpoint 与 orphan file，ordered 模式下
  数据稳定先于新增 metadata 引用；replay 必须幂等且能恢复跨事务 truncate/orphan；
- VFS/ABI 必须提供 `fallocate`、打洞、`SEEK_DATA/SEEK_HOLE` 与 extent 查询，并保持 buffered
  read/write、shared mmap、writeback、fsync/fdatasync/msync 的同一缓存权威页；
- v5 必须实现 xattr、POSIX ACL 与用户/组 quota；fscrypt、fs-verity、DAX、在线 resize、
  reflink、快照、压缩、去重和多设备 RAID 不属于 V2；
- rootfs v4 不原地改义；迁移必须从旧镜像读取并向另一份 v5 镜像写入，切换前后分别 fsck；
- v5 切换生产根前必须通过小几何 ENOSPC、十万步 extent/allocator/HTree 模型、journal 断电
  矩阵、4 GiB ATA/NVMe primary/reclaim/OOM/persistence 和无宿主空洞手机镜像门禁。

完整范围与 v2.15..v2.20 顺序由
[ADR 0079](adr/0079-v2-mini-ext4-rootfs-v5-program.md) 冻结。

## v2.16 rootfs v5 block-group 盘面要求

- `OSRFV005` 必须使用 4096 字节 block、512 字节 sector 和显式小端字段；production profile
  必须固定 LBA 32768、33550336 block、32768 block/group、1024 group；
- group descriptor 与 inode 必须各为 256 字节，每组 2048 inode；inode 1..15 保留，inode 2
  必须是根目录，初始空盘计数必须为 33416241 free block、2097137 free inode；
- 每组必须拥有独立 block bitmap、inode bitmap 和 inode table；尾组与 bitmap 越界位必须按
  真实几何处理，任何 metadata/data 区间越界或重叠都拒绝；
- 组 0、1 和 3/5/7 的纯幂组必须保存相同的 superblock/GDT，共 15 份；丢失或分歧不得由
  primary 的成功掩盖；
- superblock、descriptor、inode 与 bitmap 必须使用 CRC32C，标准向量和固定 profile checksum
  必须跨 C++/Python 一致；未知 read-only/incompatible feature、缺失 required feature、非零
  保留区或错误 checksum 必须 fail closed；
- Kernel 侧格式库必须 freestanding、无动态分配并按字节编解码；宿主必须提供安全的
  mkfs/inspect/fsck/corrupt，覆盖显式覆盖许可、JSON 摘要和逐类损坏；
- v2.16 fsck 只验证初始空 v5 镜像；不得宣称已实现 v5 mount、可写文件、extent 或 journal；
  rootfs v4、ABI v2.6.0、ATA/NVMe、4 GiB/128 GiB 与 VGA 生产门禁保持不变。

盘面与失败语义由
[ADR 0081](adr/0081-v2-16-rootfs-v5-block-group-format.md) 冻结。

## v2.17 rootfs v5 journal v2 要求

- journal v2 必须使用 4 KiB 项目小端记录和 CRC32C，不得复制 JBD2 big-endian magic/盘面；
  journal superblock 必须绑定文件系统 UUID、几何、feature、sequence 和 generation；
- 实验 journal 必须有界为 4 槽，每槽明确 descriptor、revoke、最多 16 个 metadata payload、
  commit 和 checkpoint；单事务最多 8 个 ordered data、32 个 revoke；
- 4 KiB 文件系统块必须通过 8 个连续 512B `BlockDevice` sector 访问，不得假设驱动接受 4 KiB
  逻辑 sector；只有成功 Flush 才算稳定；
- 提交必须严格执行 prepared metadata Flush → ordered data home Flush → commit Flush；commit 可见
  时数据必须已经稳定，metadata 不得在 commit 前写入 home；
- checkpoint 必须在 home metadata Flush 后写 checkpoint record，再更新 superblock 和清槽；
  任一中间失败必须允许恢复重做；
- recovery 必须先验证全部 committed transaction，再按 sequence 重放；后续 committed revoke
  必须抑制旧 target，incomplete 可以丢弃，有效 commit 引用的损坏必须 Corrupt；
- orphan block 必须绑定 UUID/generation，容纳 503 个唯一 64 位 user inode，add/remove 幂等，
  满容量、越界、重复、计数和 checksum 均有验证；
- commit 与 recovery 分别覆盖至少 128/96 个持久化故障点，十万步固定种子模型不得因重复 4 KiB
  CRC 把完整 verify 拉长数十秒；
- v2.17 不接入生产 mount，不改变 rootfs v4、ABI v2.6.0、4 GiB/128 GiB、ATA/NVMe 和 VGA。

状态机与失败语义由 [ADR 0082](adr/0082-v2-17-rootfs-v5-journal-v2.md) 冻结。

## v2.18 extent、allocator 与 delayed allocation 要求

- extent leaf/index 必须使用 4 KiB、小端、64 位 logical/physical/count、UUID/generation 和
  CRC32C；leaf 必须区分 Initialized/Unwritten，hole 不保存 entry；
- logical overlap、physical duplicate ownership、非法 child、越界、未知 state、非零保留区和
  checksum 错误必须在 decode/Insert/Validate 三层拒绝；
- runtime 模型必须覆盖至少 256 extent、4 路、85 node 和深度 3，并验证 split/merge、树高增长/
  收缩、中段状态转换和 canonical 相邻合并；
- allocator 必须使用调用方 bitmap storage，优先 inode/preferred group 的连续 run，支持 minimum
  下的 partial 与循环 group fallback；journal/protected range 不得分配或释放；
- bitmap 修改必须先形成带 generation 的 reservation；extent 插入失败必须 Abort 并恢复 free
  count，成功后才 Commit；ENOSPC、stale token、double free 不得改变状态；
- hole buffered write 必须先登记 Delayed，writeback 时才取得物理块并插入 Unwritten；只有数据
  稳定后才能转换 Initialized，失败保留 delayed 以便重试；
- Fallocate 必须创建 Unwritten；PunchHole 不改变 file size；Truncate shrink 必须删除 EOF 外
  delayed/mapped/keep-size 预分配；
- SEEK_DATA 把 Delayed/Initialized 视为 data，SEEK_HOLE 把 absent/Unwritten 视为 hole；范围查询
  必须区分三态且 Delayed 不得伪造 physical block；
- extent+journal 故障矩阵必须证明 metadata-new implies ordered-data-new；固定种子十万步模型必须
  核对 mapping、ownership、free count、file size 和 seek；
- v2.18 不改变生产 rootfs v4、ABI v2.6.0、4 GiB/128 GiB、ATA/NVMe 和 VGA。

设计由 [ADR 0083](adr/0083-v2-18-rootfs-v5-extents-allocation.md) 冻结。

## v2.19 directory、xattr、ACL 与 quota 要求

- dirent 必须变长、8 字节对齐、绑定 inode generation/name hash 并带 block CRC32C；损坏长度必须
  在复制名称前拒绝；
- HTree lookup 必须走有界 index path，hash collision 继续逐名称比较，不能以热 dentry 代替；
- inode extension feature 必须与 extent/directory/xattr pointer 和 ACL/quota generation 一致；
- xattr 必须按 namespace/name 排序并覆盖 replace/remove/capacity/checksum；
- ACL 必须具备 owner/named user/group/mask/other，显式零权限不能错误回退到 group；
- user/group quota 必须覆盖 hard、soft grace、release 和失败不变；
- 固定种子十万步模型必须比较 directory/xattr/quota oracle；
- v2.19 不得宣称 production v5 mount 已完成。

## v2.15 每 inode I/O 协调要求

- VFS 必须按完整 superblock/node identity 管理 128 个有界活跃 I/O 槽；同 identity 共享一个
  `RuntimeMutex`，不同 identity 不得经过共同 append mutex；零引用槽按 LRU 安全复用；
- 槽 token 必须包含 generation；非法 identity、过期 token、重复 identity、引用/统计损坏
  必须 fail closed；所有槽活跃时返回 `CapacityExhausted`，不得退化为全局串行；
- `write/pwrite/append` 必须在 inode guard 内完成缓存逻辑 size、Dirty 发布、EOF 选择、
  RLIMIT_FSIZE 裁剪和 metadata 失效；append+pwrite 继续不改变 FileDescription offset；
- writable shared mmap 首次脏化必须在同一 inode guard 内完成 `MarkDirty` 与 writable PTE 发布；
  truncate 持 guard 时不得有同 inode buffered/mmap writer 越过准备边界；
- path truncate、open(O_TRUNC) 与 ftruncate 必须在 VFS guard 内调用统一 prepare hook，再提交
  backend 和 cache；prepare 失败不得改变盘面 size、缓存 size 或调用 cache truncate；
- fsync/fdatasync 与同步 msync 必须在 inode guard 内写保护映射、等待目标范围 writeback 并
  执行 VFS sync；writeback error cursor 只能在 guard 释放后于 FileDescription lock 下推进；
- 后台 writeback 不递归取得 inode guard，继续使用 Dirty/Writeback/Error、页 waiter、
  backing 引用与后端事务；其失败必须向已有错误 sequence 报告；
- 协调器必须统计 capacity、cached/referenced slot、活动引用、峰值、复用、替换和容量拒绝，
  `Validate` 必须重算当前状态；正常路径不得逐 inode/lock 向 VGA 输出；
- 测试必须覆盖非法初始化/token、两槽耗尽/LRU、同 inode 串行、不同 inode 并行、十万步固定
  种子模型、truncate/write 强制交错、prepare EIO、既有双 append、mmap/fsync 和 4 GiB
  ATA/NVMe/persistence 回归。

## v2.0 完成基线

第一周期已完成 `v1.0 用户环境`；第二周期的 v1.1 已完整闭合内存分配与资源
生命周期，v1.2 又完成 Process/Thread、WaitQueue、锁模型与完整扩展现场，
v1.3 已完成 CpuLocal、处理器能力冻结和原生系统调用安全边界；v1.4 又完成
类型化 KernelObject、共享 FileDescription 和动态 FileTable；v1.5 已完成
VFS、每 Process FsContext、memfs 与 legacy 文件系统适配；v1.6 已完成
rootfs v2、完整命名空间修改、稀疏大文件、独立 mkfs/fsck 和损坏拒绝。
v1.7 又完成 PID1、父子进程树、Zombie/reparent、磁盘 ELF spawn/exec/wait、
128 KiB `argv/envp` 和候选映像原子提交。v1.8 已完成非重叠 VMA、
匿名 `mmap/munmap`、按需零页、program break、8 MiB 受控用户栈、
自研用户 heap，以及 unmap/exec/exit 后的数据页、空页表分支与 VMA
描述符回收。v1.9 已进一步完成文件后备 VMA、按需 ELF、有界 clean page
cache、只读 shared、可写 private、文件修改失效和跨 fd-close 生命周期。
v1.10 又完成只复制调用 Thread 的 fork、匿名/private 页 COW、统一用户
`#PF`/Kernel `CopyToUser` 私有化、fd/FileDescription/FsContext/FileBacking
继承，以及失败时父状态完整恢复。v1.11 进一步完成 64 KiB 按需分配动态
管道、分档 PipeManager、`pipe/dup2` ABI、外部命令 Shell、16 级流水线、
输入输出重定向与 `/bin` 核心工具，并在成功、拒绝、退出和异常路径上闭合
描述符、端点和物理页生命周期。v1.12 又完成用户 Thread、FS-base TLS、
private futex、用户 Mutex/ConditionVariable/Once、32/64 Thread 三档整机
验收以及 `munmap`/`exec`/ProcessExit 取消。v1.13 已把 PIT 实际除数提升为
精确余数累计的 64 位单调纳秒，建立 512 槽 deadline queue，并完成非忙等
sleep、timed futex、timed condition、通知/超时单赢家与三档 QEMU 验收。
v1.14 又完成 Process disposition、Thread mask、普通 pending 合并、
进程组、用户 handler、安全 sigreturn，以及阻塞系统调用的 Signal 单赢家与
显式重启策略。v1.15 进一步完成 canonical TTY、SID/PGID、控制终端、
Stopped/Continued wait 事件、`/dev/console` 和 Shell 前后台作业控制。
v1.16 已完成 64 槽 BlockRequest 队列、ATA IRQ14 完成与超时恢复、
BlockIo 等待、可写共享文件映射，以及 clean/dirty/writeback/error 页状态机。
v1.17 ordered metadata journal 与崩溃恢复已经完成。v1.18 进一步冻结
ABI v2.0.0、69 个系统调用、错误区间和关键结构偏移，用最小 devfs 替换
专用 console 文件系统，挂载六文件只读 procfs，并把 rootfs 工具补到
32 个。v2.0 已完成纯集成发布：实现与 ABI 保持冻结，三档 QEMU、完整
测试图、目标产物、教材、手机导出、独立网站和公开生产路由统一到同一发布
证据。后续能力只能进入 v2.x 或更高版本。

## v1.9 文件虚拟内存冻结要求

- 文件映射身份必须包含 superblock/inode 的 identifier 与 generation，
  不得用 fd 或可复用对象地址作为 cache key。
- VMA 必须保存后备 generation、文件 offset 和有效数据长度；split/merge
  必须同步维护文件区间。
- ELF 结构在提交前完整校验，物理页允许延迟到用户访问；返回用户态前必须按
  executable VMA 解析入口页。
- clean page cache 必须有固定硬容量、共享映射引用和零引用 LRU；无候选时
  明确失败，不能无界等待或回收被 PTE 引用的页。
- 文件尾与 ELF BSS 未覆盖字节必须为零；不能读入相邻文件内容。
- v1.9 阶段只支持只读 `MAP_SHARED` 与可写 `MAP_PRIVATE`；v1.16 的冻结要求
  已显式取代“writable shared 不支持”的历史边界。
- fd 关闭后映射继续有效；unmap、exec 和 exit 释放最后后备引用。
- write/truncate 必须撤销旧只读文件 PTE 并失效 cache；private 修改不得
  回写文件。
- 64 MiB、256 MiB、64 GiB 必须运行相同文件映射探针并最终资源守恒。

## v1.10 fork/COW 冻结要求

- fork 子 Process 只包含调用 Thread；父返回子 PID，子返回 0，失败保持父
  Process 可观察状态不变。
- AddressSpace、VMA、FileBacking、FileTable、FsContext、用户现场、FXSAVE
  和 KernelStack 都必须有显式 clone/所有权/回滚路径。
- FileTable clone 必须保留精确 fd 与 fd flags；父子共享 FileDescription
  offset，但关闭 fd 和后续表结构修改相互独立。
- 只有原本可写的 private 驻留页可以标 COW；真正只读页必须继续产生保护
  fault，Writable+COW PTE 属于非法状态。
- 用户 present+write `#PF` 与 Kernel `CopyToUser` 必须复用同一 COW break；
  内核不得经 direct-map 绕过私有化修改共享 frame。
- 引用数为 1 时恢复原 frame 可写；引用数大于 1 时准备新 frame、复制完整页、
  替换 PTE 后再释放旧引用。
- fork 必须先完成候选 child，再提交父 PTE；任意中途失败必须销毁 child、
  恢复父权限并清除仅剩单引用的 COW 元数据。
- 100000 步引用模型、连续 32 次 fork/exec/wait 与 64 MiB、256 MiB、
  64 GiB QEMU 都必须结束于零活动 COW 引用、零 Zombie 和跨层资源守恒；
  64 MiB 兼容档不要求同时保留 32 个活跃 Process。

## v1.11 Unix I/O 冻结要求

- 历史 64 字节启动管道继续服务早期兼容探针；普通用户管道必须使用 64 KiB
  逻辑容量和 4 KiB 按需物理页，未触及区间不得提前占用页帧。
- PipeManager 在 bootstrap、functional、capacity 三档分别提供 8、128、
  1024 个槽；创建失败不得遗留半安装端点，最后一个读端和写端关闭后必须
  释放全部后备页并回收槽。
- FileTable 在 functional 档 hard limit 为 512；`dup2(oldfd, newfd)` 必须
  精确替换目标 fd、保持共享 FileDescription 语义，并对相同 fd、非法 fd、
  引用获取失败保持 destination 不变。替换一旦提交便不得回滚；旧 destination
  的 finalizer 在表锁外失败时返回显式 release failure，并把它提升为内核
  资源账本错误，不能把已经公开的新 fd 伪装成未提交。
- 系统调用 ABI 固定新增 `CreatePipe=45` 与 `DuplicateDescriptorTo=46`；
  用户包装不得把内核地址、宿主句柄或实现对象泄露到 Ring 3。
- Shell 只把 `cd` 和 `exit` 保留为内建命令；help、文件与目录操作、文本
  处理和状态工具必须作为 rootfs 中的外部 ELF 从 `/bin` 执行。
- 解析与执行必须分离。解析器支持单引号、双引号、反斜杠、`<`、`>` 和
  最多 16 级流水线；语法错误、参数超限、阶段超限和行长超限均不得产生
  Process、fd 或管道副作用。
- 执行器必须先准备管道和重定向，再逐个 spawn；任一中途失败都要关闭父端、
  回收已创建子进程，并等待所有已提交子进程，不能泄漏 Zombie。
- 关闭读端后的写入必须报告 broken pipe；关闭写端且缓冲耗尽后的读取必须
  返回 EOF；阻塞等待必须通过 WaitQueue，不得轮询刷日志。
- 单元测试必须覆盖跨页、回绕、EOF、broken pipe、`dup2` 替换和 16 级解析；
  固定种子随机测试至少覆盖 100000 次动态管道操作和 4096 条任意 Shell
  输入；bootstrap 与 functional QEMU 必须真实键入重定向和 16 级流水线。
- functional QEMU 结束时动态管道 active 为 0、peak 为 128、创建数等于
  释放数且至少出现一次容量拒绝；所有 fd、FileDescription、Process、
  Zombie 和物理页统计必须回到阶段基线。

## v1.12 用户 Thread 与 futex 冻结要求

- 系统调用 ABI 固定为 CreateThread=47、ExitThread=48、JoinThread=49、
  SetThreadLocalStorage=50、GetThreadId=51、WaitPrivateFutex=52、
  WakePrivateFutex=53；结构尺寸必须由 `static_assert` 冻结。
- Thread 必须拥有独立 TID、通用/FXSAVE 现场、KernelStack、用户栈、FS-base
  TLS、signal mask 预留和等待关系；AddressSpace、FileTable 与 FsContext
  仍属于 Process。
- Create 必须先验证和准备全部上下文，再把 Ready Thread 暴露给调度器；
  子 Thread 的 TLS TID 由子入口自行发布，不能依赖父 Thread 返回后的写入。
- functional 单 Process 支持 32 Thread，capacity 支持 64 Thread；到达上限
  后下一次创建必须明确失败并完整回滚 KernelStack、调度槽和用户映射。
- IA32_FS_BASE 必须在 Thread 切换时恢复，并跨 PIT 抢占、Blocked/Ready、
  SYSCALL/INT 兼容入口与返回保持；系统调用汇编不得装载 FS selector 覆盖 base。
- private futex key 必须是 `(AddressSpaceId, aligned uint32_t VA)`；不同地址
  空间的相同 VA 不得共享队列，COW/物理 frame 不得进入 key。
- wait 必须在 wake 使用的同一 irq-save 临界区内二次读取 word 并入队；
  值变化返回明确状态，不允许“比较后、入队前”丢失唤醒。
- `munmap`、成功 exec 提交、ProcessExit 和用户异常必须取消旧地址范围或旧
  AddressSpaceId 上的 waiter；活动 Thread 的 stack/TLS 不允许被撤销。
- ThreadExit 只发布 Thread 退出值；唯一 Join 回收 KernelStack/Thread 后，
  用户运行库释放用户栈/TLS。ProcessExit 才关闭共享资源并终止 sibling。
- 用户 Mutex、ConditionVariable 与 Once 必须以固定宽度编译器原子实现快
  路径，只有竞争路径进入 futex；失败不得静默退化成未持锁继续执行。
- 单元测试覆盖 key/容量/生命周期，随机测试至少执行 100000 步，QEMU 必须
  完成 64 MiB 单线程降级、256 MiB 32 Thread 和 64 GiB 64 Thread，并在
  结束时满足零 futex waiter、零 KernelStack、零 Zombie 和资源快照守恒。

## v1.13 单调时间与 deadline 冻结要求

- 单调时间 ABI 固定为无符号 64 位纳秒；它只表达经过时间，不允许混入 RTC、
  日期、时区、闰秒或宿主墙钟。
- PIT 换算必须使用输入频率和实际编程除数，并保留整数除法余数；禁止把每个
  tick 硬编码为 1 ms。所有乘加在回绕前检查，边界饱和为 `UINT64_MAX`。
- 内核所有 timeout 统一转换为绝对 deadline。相对时长只能在接口边界用
  饱和加法转换一次，重试和虚假唤醒继续使用原 deadline。
- 每个 Blocked Thread 最多有一个活动 deadline；队列必须按
  `(deadline, insertion sequence)` 稳定排序，并支持按 Thread 身份直接取消。
- DeadlineQueue 的 schedule、expire、cancel 与 WaitQueue 状态转换必须位于
  同一 scheduler irq-save 临界区。condition、timeout、terminate、unmap、
  exec 和 ProcessExit 最多让 Thread 进入一次 Ready。
- IRQ0 无论来自 Ring 3、Ring 0 还是 idle 都必须推进时钟并解析到期项。
  唤醒只设置 Ready/need-resched，不能在任意 IRQ 调用链直接切换 C++ 栈。
- sleep 必须阻塞；没有 Ready Thread 时由 `sti; hlt; cli` 等待 PIT，禁止
  轮询时间。deadline 已到达时必须立即返回，不登记短命队列项。
- timed futex 必须在与 wake 相同的锁内完成最终用户字比较、deadline 检查
  和 block；到期返回 `TIMED_OUT`，并释放已经为空的 futex key。
- ConditionVariable 的 timed wait 无论通知、超时还是失败都必须先重新取得
  Mutex 再返回；通知只表示谓词可能变化，调用者仍需循环检查。
- 系统调用 ABI 固定新增 GetMonotonicTime=54、SleepUntil=55、
  WaitPrivateFutexUntil=56，超时错误固定为 `-51`。
- 单元、集成和固定种子随机测试必须覆盖余数、饱和、不早醒、同 deadline
  稳定顺序、通知/超时单赢家与 100000 步队列模型。三档 QEMU 结束时
  active deadline 为零，且 schedules 等于 expirations、cancellations 与
  active 三者之和。

## v1.14 信号与 sigreturn 冻结要求

- disposition 与进程组属于 Process；mask、Thread pending 和活动 handler
  frame 属于 Thread；未选择普通信号属于 Process pending。
- 普通信号采用合并位语义。同一信号在 Process 或任一 Thread pending 时重复
  发送不得新建第二个所有者；多 Thread Process 只能选择一个未屏蔽 Thread。
- Kill 不可屏蔽、不可忽略且不可安装 Handler；Kernel 必须独立验证用户 action，
  不能依赖运行库过滤。
- condition、timeout、signal、close 与 cancel 必须通过同一个
  `ThreadScheduler::WakeThread` 单赢家完成，deadline 和 WaitQueue 所有权同步
  解除，Thread 最多进入一次 Ready。
- 阻塞调用必须显式声明 restartable。无部分进度时可返回 `INTERRUPTED`；
  只有 action restart flag 和策略表同时允许时才恢复原系统调用号并回退到
  `SYSCALL`，已有部分进度不得撤销。
- `SignalAction`、`SignalUserContext` 与 `SignalFrame` 固定为 40、176 和
  240 字节；系统调用 57--63 不得重编号。
- Kernel 必须在目标 Thread 用户栈构造 handler frame；主栈扩展只允许紧邻
  committed bottom 并复用 VMA/页故障权限与增长间距，用户 Thread 栈不得越界。
- sigreturn 必须匹配 Kernel 登记的 frame address、cookie、signal、restorer
  和 previous mask，并验证 magic/version/size、canonical RIP/RSP、selector、
  RFLAGS、Thread 栈边界和 R-X/RW-NX 页权限。
- 畸形 frame 只终止目标 Process，不 panic Kernel；父进程必须能以
  Exception/vector 13 回收该子进程。
- fork 复制 disposition/group 与调用 Thread mask，不复制 pending/active frame；
  exec 保留 Ignore、重置 Handler，保留 Process 与 survivor pending，清理
  active frame 和兄弟状态；exit 后活动信号 Process/Thread 计数必须为零。
- 单元、集成和 100000 步固定种子随机测试必须覆盖选择、合并、帧身份、生命周期
  和 Signal/condition/timeout 单赢家；三档 QEMU 必须运行同一 signal probe。

## v1.15 TTY、会话与作业控制冻结要求

- PS/2 层只解码 Set 1 与 Ctrl 修饰状态；canonical 编辑、EOF、退格和控制
  字符语义必须属于 TTY，不得塞入 IRQ handler 或 Shell。
- TTY 输入必须满足
  `submitted=read+buffered+editing+dropped+consumed`；输出必须满足
  `queued=written+pending`。所有共享索引使用 irq-save 锁，锁内不得睡眠。
- PID、PGID、SID 和 ProcessTree parent 必须是独立身份。fork 继承 SID/PGID，
  session leader 满足 PID=PGID=SID，跨 session 组迁移必须拒绝。
- 控制终端只允许 controlling session 的 foreground PGID 读取；后台读取
  返回 `-55` 且不能消费输入。
- Ctrl-C 与 Ctrl-Z 必须由 TTY 定向为发往 foreground PGID 的 SIGINT 与
  SIGTSTP。SIGSTOP 不可屏蔽；停止不得释放地址空间、fd、VMA 或 Thread 现场。
- scheduler 必须跳过 Stopped Process 的所有 Thread；SIGCONT 只把仍停止的
  Thread 恢复为 Ready，stop/continue 次数最终守恒。
- Stopped、Continued 和 Exited 事件必须分别可观察；只有 Exited 收集 Zombie。
  快速 stop→continue→exit 不得覆盖前两个 pending 事实。
- 进程级信号与 PGID 身份保留到 Zombie 被 wait 收集，避免快速 child exit
  使父 Shell 的 `setpgid` 或后续管线成员失去组锚点。
- `/dev/console` 必须作为 CharacterDevice vnode 通过 VFS 暴露；普通文件
  truncate/offset 语义不得错误套到字符设备。
- Shell 必须为整条管线建立同一 PGID，前台作业停止/退出后恢复自己的 TTY；
  `jobs`、`fg`、`bg` 与尾部 `&` 使用有界作业表并覆盖资源回滚。
- 系统调用 64--69、24 字节 TerminalInformation、56 字节 wait event 和错误
  -55..-57 不得重排；所有字段使用明确固定宽度。
- 单元、集成、100000 步固定种子随机测试和真实 QMP Ctrl-Z/Ctrl-C 系统测试
  必须同时通过；终态无 Stopped、Zombie、活动作业、残留输入编辑或输出字节。

## v1.16 IRQ 块层与 writeback page cache 冻结要求

- `BlockRequest` 必须保存 64 位单调 identifier、操作、LBA、逻辑块数、缓冲区、
  所有者 Thread、绝对 deadline、状态和结果；复用槽位不能复用仍可观察的
  identifier。v2.7 起几何和 Issued 深度由具体块设备声明。
- ATA primary channel 当前仍只允许一个 Issued 请求；Queued 必须 FIFO，完成
  请求必须经 Reap 才释放容量。容量耗尽、identifier 耗尽和非法参数均不得
  修改已有队列。
- ATA 适配器可以在复制冻结结果后立即 Reap，但唤醒必须同时匹配 owner
  Thread 槽与 identifier；Thread 已退出或槽位已复用的完成只能记为
  abandoned，不能唤醒后来占用该槽的 Thread。
- 请求只能沿 `Queued→Issued→Completed→Unused`，或
  `Queued→Completed→Unused` 取消；Succeeded、DeviceError、TimedOut、
  Cancelled 只允许一个最终结果，迟到 IRQ 不得覆盖超时。
- IRQ14 handler 不得睡眠、分配内存、遍历无界集合或访问用户地址。它只完成
  当前 PIO 数据阶段、读取状态、提交结果、EOI、唤醒所有者并尝试发出下一请求。
- PIT 使用绝对单调 deadline 解析超时。ATA 超时必须执行 software reset，
  使设备恢复到可接受下一命令的已知状态；不能只唤醒 Thread 后留下半条命令。
- early boot 的 ROM、Stage 1 和 Kernel 启动自检继续使用有界轮询适配器；
  调度器和 IDT 就绪后才开放 IRQ14。轮询与异步路径不得同时拥有同一命令。
- 文件页缓存状态固定为 Empty、Clean、Dirty、Writeback、Error。脏页数量有
  明确硬上限；Error 页保留原 frame、identity 和 dirty 责任，不能伪装为
  Clean，也不能作为 LRU 候选被静默淘汰。
- 只有完整页、以 writable 方式打开的 `MAP_SHARED` 文件区间可以变为可写
  shared 映射。第一次写通过只读 PTE fault 标记缓存页 Dirty 后再放开 PTE；
  `MAP_PRIVATE` 写入只走 COW。
- 显式 `sync` 必须先重新写保护所有可写 shared PTE，再写回 dirty/error 页，
  然后调用 VFS/rootfs sync 与 ATA FLUSH。失败向调用者传播，并保留可重试
  状态；本阶段不承诺独立 `msync` ABI 或后台 daemon。
- unmap、exec 和 exit 在释放可能是最后一个 writable file backing 前必须
  移交 Dirty/Writeback/Error 页，不能留下失去 VFS writer 的脏缓存页。
- 文件页 identity 使用稳定的 mount/superblock generation、inode identifier
  与 inode generation。普通数据事务不得改变 mount generation，否则同一
  inode 会被错误拆成两个 cache identity。
- 单元、集成、100000 步请求随机模型、100000 步页状态随机模型和三档 QEMU
  必须同时通过；QEMU 必须观察 IRQ14、请求提交/完成时间、共享写回读回、
  private 不回写，以及 I/O 阻塞期间其他 Thread 前进。
- v1.16 的 BlockIo 生产消费者冻结为显式 sync 的最终 ATA FLUSH；异步
  Read/Write 状态机必须可用，但不宣称当前 rootfs 普通扇区访问已经迁移。

## 通用启动与硬件要求

Stage 1 在自研长模式环境中通过 ATA PIO 读取
Kernel 描述符和 ELF 文件，自行执行 CRC32、扇区补零、ELF64、权限、对齐、
范围和段重叠检查。所有 `PT_LOAD` 先完整验证，再复制到恒等映射目标地址并
清零 BSS。Stage 1 通过 `fw_cfg` 端口发现 QEMU PC 提供的 `etc/e820`，
自行完成大端目录解析、20 字节条目到 24 字节 ABI 的转换和基址排序，再以
104 字节、全 64 位字段的 BootInfo v2 通过 System V AMD64
首参数寄存器交给 C++20 内核。内核不继续借用 Stage 1 的描述符状态：v0.5
先建立五槽 GDT、104 字节 TSS、256 槽 IDT 和 32 个架构异常汇编桩；
v0.8 加入 Ring 3 数据/代码段后扩为七槽，并把 TSS 选择子移到 `0x28`。

正常路径与 Kernel ATA 超时、ATA 设备错误、描述符损坏、负载损坏和 CRC 正确
但 ELF 语义损坏路径均有 QEMU TCG 回归。内核验证排序、不重叠且无溢出的
物理内存图，并读取 `CPUID.80000008H` 的物理地址宽度。页帧状态元数据按
最高可用 RAM 页动态计算，在启动身份映射内选址；高地址保留洞不会错误扩大
状态表。分配器以每帧 2 bit 管理全部受管 E820 type 1 RAM，保留平台、内核、
早期栈和自身元数据。正式地址空间从 `0xFFFF888000000000` 建立 64 TiB
高半区 direct-map，内部优先采用 2 MiB 页并只映射普通 RAM。页表页、用户页
清零和 ELF 复制在 CR3 切换后统一经 direct-map 访问，不再把物理地址直接当作
C++ 指针。

主系统测试以 64 GiB RAM 启动，要求完整管理 64 GiB 可用 RAM、报告至少
4 MiB 页帧元数据、实际使用 2 MiB direct-map 页，并在 4 GiB 以上页帧写入、
读回和回收两个 64 位模式。64 MiB 回归则证明高内存自检可有条件跳过而不
缩小通用实现。内核启用 `IA32_EFER.NXE` 与 `CR0.WP`，切换到自建四级页表，
并对代码、只读数据、可写数据、堆和 guard page 执行权限验证。真实 `INT3`、
`UD2`、not-present 页故障和写保护页故障覆盖恢复、错误码、CR2 与 panic。

内核现已为向量 32..47 安装独立 NASM 硬件 IRQ 桩，映射本地 APIC MMIO 页，
通过 SVR 与 LVT LINT0 建立 ExtINT virtual-wire，再把两片 8259A 重映射到
`0x20..0x2F`。8254
PIT 通道 0 以模式 2 提供约 1000 Hz IRQ0，内核用 64 位 tick 和实际除数换算
单调毫秒；PS/2 控制器启用 IRQ1 与扫描码集合 1 翻译，C++ 解码器区分 make、
break 和 `E0` 扩展序列。内核还用自写 ATA PIO 驱动重读 LBA 0 并校验
`OSSTAGE1`，证明设备访问不依赖加载器函数。

v0.7 首次用 QMP 键盘前端注入 `A` 键证明 IRQ1 链路；v1.0 已扩展为逐字
输入完整 Shell 命令。无论单键还是命令，扫描码从 i8042 端口、IRQ1、8259A、
IDT、汇编桩到 C++ 解码均由目标代码处理。宿主只负责产生外部输入和验证 VGA
协议。

v0.8 新增独立 ABI 与用户模块。用户程序由 freestanding C++20 和 NASM
Intel 汇编构建为链接基址 `0x40000000` 的 AMD64 `ET_EXEC`，以内嵌原始
文件形式交给内核解析。内核验证 ELF 全部结构、W^X、地址、对齐、重叠、页数
与入口后，分配带 U/S 的 RX 或 RW/NX 页面，并建立四页用户栈和未映射
guard。GDT、TSS.RSP0、IDT DPL3 gate 与五项 `IRETQ` 帧共同完成 Ring 3
进入；`INT 0x80` 提供 `WriteLog` 与 `ExitProcess`，所有用户地址先逐页
验证再复制。

v0.9 在该边界上加入固定容量进程表和单核抢占式 round-robin。每个进程拥有
独立 PML4、同址 ELF 页、用户栈及带保护页的 16 KiB Ring 0 栈；内核映射只
以 supervisor 子树共享。PIT 每四个 tick 形成时间片，入口保存完整通用寄存器
和用户 `SS:RSP:RFLAGS:CS:RIP`，调度器再切换 CR3 与 TSS.RSP0。

正常镜像同时创建一个系统调用验收进程和三个相同 worker。三个 worker 都在
`0x40000000` 程序窗口运行，并让同一 BSS 虚拟地址独立从零演进，证明地址
空间隔离。`GetProcessId`、`ExitProcess` 和用户异常都经过同一当前进程
生命周期；最后一个进程结束后，物理页 free/allocated 统计必须恢复到创建前。
v0.10 在此基础上引入 `Blocked` 状态、可审计的等待原因、阻塞与定向唤醒
统计。当前单核系统在 interrupt gate 关闭 IF 的系统调用窗口内完成条件检查
和调度转换；共享管道内部同时使用 acquire/release 自旋锁，明确未来 SMP
扩展时的内存顺序边界。自旋临界区不得调度、打印或执行用户复制。

内核提供一个启动期 64 字节有界字节流管道。ABI 把非阻塞 `TryRead/TryWrite`
与 `WaitReadable/WaitWritable` 分开，用户包装在 `WouldBlock` 后睡眠并循环
重试。管道支持环形回绕、部分读写、EOF、broken pipe、读写端关闭和进程异常
终止时的端点自动关闭；只有生产者拥有写权限，只有消费者拥有读权限。

正常镜像创建生产者、消费者和两个调度 worker。生产者写入 256 字节确定性
载荷，消费者用 31 字节缓冲逐段校验，要求真实出现满管道写阻塞和空管道读
阻塞；全部完成后字节统计相等、缓冲为空、端点均关闭、EOF 恰观察一次，创建
前后物理页统计一致。v0.10 的 64 项 CTest 覆盖调度/管道单元测试、组合测试、
固定种子随机模型、四线程同步压力、六个用户 ELF 审计、QEMU 真实阻塞/唤醒
与全部旧失败路径。v0.11 又实现固定布局、CRC32、bitmap/inode/目录、八项写回缓存、
Dirty/Clean 提交协议、ATA PIO 写入与 flush，以及每进程四槽普通文件描述符；
真实 QEMU 连续启动证明文件跨来宾实例持久化，损坏超级块必须拒绝挂载。
v1.0 在每个 PCB 中建立八槽统一描述符表：fd 0/1/2 是标准输入、输出和
错误，动态槽保存普通文件、目录、管道读端或管道写端。通用
TryRead/TryWrite、WaitReadable/WaitWritable 和 Close 统一资源命名与阻塞
包装；目录保留 OpenDirectory/ReadDirectory 的类型化迭代语义。历史专用
系统调用仍作为兼容入口存在，正常生产者和消费者已经迁移到通用路径。

PS/2 Set 1 解码器把真实 make code 转成单字节字符并提交到 256 字节控制台
FIFO。Ring 3 Shell 在 fd 0 为空时阻塞；如果此时没有 Ready 进程，内核回到
永久地址空间执行同一汇编块中的 `sti; hlt; cli`，由 IRQ1 提交字符并唤醒后再恢复用户帧。QEMU
测试只通过键盘前端逐字输入 help、文件、目录、同步、未知命令和退出，不把
命令预置到内核。

Shell 是独立 freestanding C++20 ELF，使用固定容量解析器实现 help、echo、
pwd、cd、ls、mkdir、write、cat、rm、rmdir、mv、truncate、stat、sync 和
exit。v1.0 收口时完整回归为 97 项；当前回归继续覆盖单元、集成、固定种子
随机、最终产物审计、真实交互、双启动持久化与历史失败路径。测试项数由
构建图自动产生，不在需求中冻结。v1.0 是第一周期 `13 / 13`
的完成基线。v1.1 已经完成动态物理
内存元数据、64 TiB direct-map、64 GiB 管理、4 GiB 以上页帧读写回收，
可释放、可合并并经过十万步模型验证的通用内核堆，以及支持连续块、错阶拒绝
和十万步模型的双位图 buddy。固定尺寸 type cache 也已经建立在通用堆之上：
一个后备块同时保存活动位图和对齐槽，空闲槽组成 LIFO 索引链；缓存必须拒绝
空指针、内部/外部指针、重复释放、活动对象销毁和计数溢出，耗尽时保持输出
不变，销毁后把后备块完整归还通用堆。单元、三缓存集成、十万步固定种子随机
和 64 MiB QEMU 真实写回共同验收该契约。KVA 当前以 1024 个有序区间描述符
管理 32 TiB 高半区窗口，必须区分保留、分配、元数据耗尽和连续地址耗尽；
单元、页帧/页表集成、十万步逐页模型以及 QEMU 双 guard 四页真实映射共同
验收申请与逆序回收。当前四个 Ring 0 栈也已迁移为六页 KVA 所有权区间：
中间四页使用独立物理后备和 supervisor RW/NX 映射，上下两页保持
not-present；进程终止后必须先回到永久启动栈安全点，再按叶映射、物理页、
KVA 的逆序清零回收。单元、独立 CR3 集成、十万步随机模型、正常四进程和
两个用户异常 QEMU 路径共同验收该契约。页表根现在显式区分独占、内核共享
与进程三种所有权；撤销最后一张叶映射会逆序回收独占的空 PT、PD 与 PDPT，
但不会释放仍可能由进程 PML4 借用的共享 PDPT。映射任一级失败必须恢复父项
原值并释放本事务新建的表帧；单元、集成、十万步随机模型和 QEMU 回收摘要
共同验收该契约。

v1.1 最后以三个互相组合的原语收口：`ScopeRollback` 由调用方提供固定动作
数组，失败时严格逆序执行且不会因单个清理失败而短路；动态内核栈创建已经
使用同一九项事务回收映射、清零物理页并释放 KVA。`ReferenceCounter` 使用
显式 `uint64_t`，只定义单 BSP 或外部锁保护下的强引用生命周期、最后引用与
上溢，不提前承诺 v1.4 的原子/弱引用策略。`ResourceSnapshot` 用 26 个字段
记录 frame、buddy、heap、KVA、kernel stack 和后续对象槽的当前所有权，
排除累计成功/释放计数。启动目标代码会真实创建并回滚一个动态栈，四进程全部
结束后再次比较完整快照；两次都必须得到零差异。

单元测试、跨 buddy/页表/KVA/栈集成测试、固定种子十万轮引用/回滚模型、
64 MiB bootstrap、具名 256 MiB functional smoke 与 64 GiB capacity 共同
构成阶段证据。正常运行会按内存档位先执行 Process/Thread 容量事务：
256 MiB 路径建立 64 个页表根和 128 个动态栈，64 GiB 路径建立 256 个
页表根和 512 个动态栈；随后四个用户 Thread 各建立一栈。统计必须精确反映
这些生命周期并最终回到零活动资源。旧四 PCB 调度器已经删除，四程序行为
迁移到新的 Process 资源容器和 Thread 调度实体。v2 路线按
[ADR 0019](adr/0019-v2-executable-program-baseline.md) 划分为 v1.1 至
v1.18，v2.0 只承担集成发布。

v1.3 将硬件入口要求提升为可测试需求：CPU 必须同时具备 long mode、NX、
FXSR、SSE、SSE2 和 `SYSCALL/SYSRET`，物理地址宽度必须在 36..52 位，
虚拟地址宽度冻结为 48 位。缺少能力时，内核必须输出缺失位图并在初始化 GDT
和用户态之前停止。每个 CPU 的本地状态必须保存当前 Thread、可信入口 RSP、
IRQ/抢占深度、`need_reschedule`、入口类型和有界累计证据；当前单 BSP 使用
一个 64 字节对齐实例，但接口不得把“只有一个 CPU”编码进用户现场。

默认用户系统调用入口必须使用 `SYSCALL`，兼容 `INT 0x80` 仍保留并与原生
入口进入同一分发器。`SYSCALL` 不自动换栈，因此入口必须先 `SWAPGS`，只从
内核写入的 `CpuLocal` 读取当前 Thread 的动态 Ring 0 栈，绝不能在用户 RSP
上压入内核数据。两条入口都必须形成同一个 176 字节 `UserContext`。返回
Ring 3 前必须同时验证现场属于当前 Thread、RIP/RSP 位于 48 位低半规范区、
代码与栈映射权限、CS/SS 和 RFLAGS。只有原生入口且标志位属于快速白名单时
允许 `SYSRETQ`；其余合法现场必须 `IRETQ`，非法现场不得触发带攻击者地址的
Ring 0 `#GP`，而要终止相应用户进程。

v1.4 要求 fd 与对象身份严格分离。KernelObject 必须具有类型、全局单调
generation、强引用和最后引用 finalizer；跨模块业务路径只能持有 RAII
reference，不能保存 payload 裸指针。FileDescription 必须拥有打开实例的
offset 和 file status flags；duplicate 必须共享它们，独立 open 必须隔离。
fd flags 只保存在 FileTableEntry，close-on-exec 不得反向修改共享对象或源
描述符。

FileTable 必须以 64 项分块按需增长，functional/capacity hard limit 分别为
256/4096。soft limit 只限制新安装，达到限制必须返回明确错误；关闭后的最低
编号必须可复用。分块堆申请使用两阶段提交，任何申请、复验或安装失败都必须
保持旧槽与传入引用不变。进程退出后活动对象、强引用和分块全部归零，创建/
销毁、finalizer 和分块申请/释放分别守恒。宿主 4096 fd 容量测试、十万步
固定种子模型和真实 Ring 3 共享偏移证明必须同时通过。

v1.5 要求路径语义与磁盘格式彻底分层。Vnode 必须由 Superblock、非零
identifier、generation 和类型共同识别；Path 必须同时保存 mount identity，
不能把跨文件系统相同 inode number 当作同一对象。每个 Process 必须持有
独立 root/cwd FsContext，FileDescription 必须保存 `Vfs + OpenFile`，Shell
和系统调用不得直接读取 legacy inode 或 ATA。

公共路径与组件上限分别为 4096 和 255 字节；绝对/相对路径、重复分隔符、
`.`、`..`、root clamp、尾部分隔符、挂载进入/退出和 getcwd 必须由统一算法
处理。达到长度、遍历或挂载容量时必须返回独立错误，不允许截断、回绕或误建
文件。目录项 ABI 必须完整初始化后再复制到用户态，不得泄露结构填充。

memfs 必须实现完整基础后端，并从 KernelHeap 动态拥有节点与文件数据；
增长、truncate、空洞清零、目录枚举、校验与 Destroy 必须有明确资源守恒。
legacy 适配器必须保留旧磁盘格式、基础创建、读取、同步与一致性检查；未知
非零损坏介质继续拒绝，禁止自动格式化。两个后端必须通过同一契约测试，路径
命名空间必须通过固定种子 100000 步独立参考模型。

挂载拓扑只允许在用户调度前建立，v1.5 不提供动态 unmount。锁顺序必须让
FileTable/KernelObject 锁在进入 FileDescription、VFS 和后端前释放。memfs
等持久挂载资源必须由 VFS 精确登记，并与 Process 最终资源快照分账；扣除
持久资源后任何 frame、KVA、heap、fd 或对象残留仍必须使整机验收失败。

## v1.8 虚拟内存冻结要求

- VMA 必须使用页对齐半开区间，按地址严格排序且互不重叠；相同 kind 与权限
  的相邻区域必须合并。
- 全局 VMA 描述符容量为 8192，单 Process hard limit 为 4096；池耗尽与
  单进程上限必须返回可区分错误。
- 中段 unmap 所需 split 描述符必须在修改前取得；失败时原 VMA 图逐字段不变。
- 匿名窗口固定为 `[0x60000000, 0x80000000)`；自动映射使用 first-fit，
  fixed 映射只接受页对齐空洞并不得覆盖现有区域。
- map 与 break growth 只建立 VMA，不提前分配数据 frame；首次合法读必须
  得到全零页，首次写后的内容在映射生命周期内保持。
- protection 只接受 `NONE`、`R`、`R|W`、`R|X`，未知位、缺少 read 的
  write/execute 和 W+X 必须拒绝。
- 用户 stack 必须预留 8 MiB、按需连续向低地址提交；只有紧邻 committed
  bottom 且与保存用户 RSP 邻近的页 fault 才可增长。栈底下一页永久没有
  VMA。
- page-fault dispatcher 必须先区分 U/S、RSVD、present、write 与 instruction
  位，再查询 VMA；权限 fault、guard、空洞和非法栈跳跃不得创建页面。
- unmap 与 break shrink 必须释放实际驻留 frame，并按页表根所有权回收空
  PT/PD/私有 PDPT；未触及 reservation 不产生虚构释放。
- Kernel 用户复制只可按需解析 Anonymous 与 ProgramBreak，不得在没有用户
  异常现场时伪造 stack growth。
- 用户 ABI 必须以固定宽度系统调用 39..42 提供 map、unmap、break 与
  112 字节统计；已有编号和错误值不得重排。
- `UserHeap` 必须是 freestanding C++20 头源分离组件，支持有界增长、
  16 字节对齐、first-fit、split、前后 coalesce、重复/外部指针拒绝和完整
  结构校验，不得调用 libc 或宿主 allocator。
- VMA 与 UserHeap 纯逻辑必须各有单元和 100000 步固定种子随机参考模型；
  页表/VMA 必须有重复生命周期集成测试；真实 QEMU 必须分别验证成功 fault、
  guard fault 与 protection fault。
- 64 MiB、256 MiB、64 GiB 三档必须运行同一 v1.8 PID1 工作负载；最终 VMA
  active 为零、free 等于 capacity、acquire/release 增量相同，并与既有
  frame、buddy、heap、KVA、stack、fd、object 和 VFS 守恒同时成立。

## v2.20 rootfs v5 生产切换要求

- 生产启动盘与 mount 必须使用 rootfs v5；禁止在 v5 容器内继续承载 v4 payload；
- v5 backend 必须通过现有 VFS 契约完成普通文件、目录、硬链接、符号链接、rename、truncate、
  权限、owner、open-unlink 与 sync；
- metadata 必须经过 journal v2，普通数据遵守 ordered barrier；恢复必须幂等；
- v5 的 4 KiB I/O 必须作为单次 BlockDevice transfer；ATA 最多 8 sector，NVMe 使用原生多块；
- 任何 4 KiB directory/extent/journal scratch 不得作为生产 Kernel stack 局部对象；
- mkfs、installer、完整 fsck、corrupt 和 v4→v5 非原地 copy migration 必须由项目工具实现；
- 4 GiB ATA/NVMe、VGA、persistence 与 failure-path 结果必须出现 `ROOTFS_V5_MOUNTED`；
- v4 格式与 backend 只保留只读兼容定义和 hosted 回归，不再作为发布身份。

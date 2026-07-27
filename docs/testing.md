# 测试策略

## 测试分层

### 宿主机单元测试

验证与硬件无关的纯逻辑，例如 ELF 解析、位字段、地址计算、容器和状态机。失败反馈应当在秒级完成。

### 模块集成测试

验证磁盘镜像、页表、启动信息和模块交接契约。测试既要覆盖正常输入，也要主动构造损坏、截断和越界输入。

### 固定种子随机测试

对纯逻辑模块生成大量有效与无效输入，验证不依赖具体样例的性质。随机测试必须：

- 使用代码中明确命名的固定种子。
- 保证相同提交、相同种子产生相同输入序列。
- 在失败时输出种子、迭代位置和失败性质。
- 同时生成合法输入和预期被拒绝的非法输入。
- 将稳定复现的随机故障沉淀为独立回归样例。

### QEMU 系统测试

从 CPU 复位向量启动，捕获串口输出和 QEMU 生命周期。每个里程碑至少包含
一条成功路径和一条失败路径。

## v2 演进测试配置契约

当前 v1.8 已具备具名 64 MiB bootstrap smoke、256 MiB functional smoke 和
64 GiB capacity 系统路径。三档使用同一个 ThreadScheduler、动态栈和页表
实现；v1.8 的正常启动链会累计注册 PID 1 和十个后续 Process，峰值并发仍为
八个，v1.2 的独立
容量测试继续覆盖完整 Process/Thread 上限。fd 与 pipe 的未来目标容量仍不得
伪造为已完成。

| 配置 | QEMU RAM | Process | Thread | 每 Process Thread | fd hard | Pipe | 测试职责 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| bootstrap | 64 MiB | 8 | 8 | 1 | 不规定 | 不规定 | 启动链、磁盘 PID1、异常、基础内存、全部历史故障镜像 |
| functional | 256 MiB | 64 | 128 | 32 | 256 | 128 | 完整用户功能、边界、失败回滚和小版本验收 |
| capacity | 64 GiB | 256 | 512 | 64 | 4096 | 1024 | 全 RAM、高地址、容量、soak 与长尾资源错误 |

三种配置必须走同一启动链、相同动态对象实现和相同 ABI。测试配置只改变内存
容量和运行时资源上限，不得通过条件编译换回固定 PCB、fd 或 pipe 表。

执行频率固定如下：

| 时机 | 门禁 |
| --- | --- |
| 每次提交 | 受影响单元/集成/固定种子随机测试；64 MiB boot；256 MiB smoke |
| 每个小版本 | 全部宿主测试与产物审计；完整 256 MiB functional |
| nightly / 候选发布 | 完整 64 GiB capacity；soak；故障和崩溃点矩阵 |
| v2.0 发布 | 三配置全量、全部故障镜像、教材/网站构建和发布溯源 |

capacity 测试必须解析来宾日志证明实际使用 4 GiB 以上物理页，不能以
“QEMU 参数写了 64G”替代高地址证据。functional/capacity 达到资源限制时，
必须验证调用返回明确错误、旧对象保持不变、本次资源全部回滚。

### 第二周期新增通用 oracle

后续每个版本除模块特有断言外，还必须复用以下性质：

- `ResourceSnapshot`：操作前后的 frame、KVA、heap、Thread、Process、
  FileDescription、Vnode、CachePage 和 BlockRequest 计数按契约守恒；
- `WaitQueue`：condition、deadline、signal、close、cancel 中恰有一个
  WakeReason 获胜，Thread 最多进入一次 Ready；
- `UserContext`：`INT 0x80` 与 `SYSCALL` 对相同请求产生相同结果，非法
  canonical 地址、RFLAGS 和特权状态被拒绝；
- `AddressSpace`：VMA 永不重叠，PTE 权限由 VMA 推导，`CopyToUser` 不得
  绕过 COW；
- `PageCache`：同一 `(Vnode, page index)` 只有一个权威页面，truncate/write
  后旧映射不可见；
- `Journal`：每个断电点恢复为旧事务或完整新事务，replay 幂等且 fsck 一致。

随机 oracle 失败时输出固定种子、精确迭代和最后一项成功操作。崩溃注入还要
输出断电点、镜像哈希、journal 序号和第一次不一致的 fsck 事实。

`v0.0` 尚无可执行固件，因此系统测试使用项目生成的空 ROM 和空磁盘，以
`-bios` 显式传入 ROM，并使用 `-S` 暂停 CPU。该测试只验证 QEMU TCG、PC
硬件模型、镜像尺寸和启动参数，不声明已经实现启动逻辑。

`v0.1` 使用两份由同一源码生成的 ROM：

- 正常镜像必须输出 `RESET` 和 `SERIAL_READY`。
- 故障注入镜像在第一条消息后屏蔽串口就绪状态，必须触发有界轮询超时，
  且不得输出 `SERIAL_READY`。

固件最终进入 `HLT`。测试进程逐行捕获串口；观察到当前用例最后一个必需
里程碑后保留短暂收尾窗口并主动回收 QEMU，尚未完成时最多等待五秒。这样既不
用固定两秒去猜慢速 CI 的调度延迟，也不会让已停机的失败用例白白耗尽预算。
QEMU 自行异常退出仍视为失败。

`v0.2` 在同一条真实复位路径上增加磁盘加载及其失败结果；`v0.3` 继续验证
从 A20 到 64 位入口的严格有序状态链：

- 正常磁盘必须依次出现 `STAGE1_HEADER_VALID`、`STAGE1_LOADED`、
  `A20_READY`、`ENTERED`、`GDT_READY`、`PROTECTED_MODE`、
  `PAGE_TABLES_READY`、`PAE_READY`、`LME_READY`、`PAGING_ENABLED` 和
  `LONG_MODE`。
- 固件必须在串口初始化后输出一次 `CLOCK_READY`；当前阶段不要求毫秒数，禁止输出
  没有 PIT 计数依据的伪造时间戳。
- IDE 永久忙必须在有界轮询后输出 `IDE_TIMEOUT`。
- ATA ERR 状态必须输出 `IDE_ERROR`。
- 描述符任意受保护字节损坏必须输出 `STAGE1_HEADER_INVALID`。
- 负载字节损坏必须输出 `STAGE1_CHECKSUM_INVALID`。
- 所有失败路径均禁止出现 `STAGE1_LOADED` 和 Stage 1 进入标记。

宿主单元测试验证格式编码、整扇区校验、截断、LBA 和加载范围；固定种子随机
测试完成 256 组有效负载往返和 256 组越界 LBA 拒绝。QEMU 测试验证的是 ROM
自身执行 ATA PIO 和远跳转，而不是宿主工具代替读取。

`v0.4` 完成内核 ELF64 审计与真实目标加载：

- 单元测试构造最小有效 ELF，并覆盖截断、程序头数量、权限、页对齐、目标
  窗口、段重叠、恒等装载和入口失败。
- BootInfo C++ 单元测试逐字段验证空指针、版本、结构大小、文件范围、入口、
  段数、页表根、映射大小和栈顶。
- 固定种子随机测试破坏 256 组 ELF 标识、256 组加载地址、256 组负载字节和
  128 组扇区补零，并完成 128 组不同文件长度往返。
- 集成测试审计真实写盘的 `kernel.payload.elf`、组合磁盘和 Stage 1/页表/
  暂存区/加载窗口/内核栈之间的物理布局，且通过 `llvm-nm` 保证没有未解析
  运行时符号；保留 DWARF 的 `kernel.elf` 不受启动暂存区文件长度约束。
- QEMU 成功路径必须从复位依次到达 Kernel 的 `BOOT_INFO_VALID`、
  `BSS_ZEROED`、`CR3_VALID` 和 `READY`。
- QEMU 失败路径分别注入 Kernel ATA 永久忙、ATA ERR、描述符损坏、负载
  损坏和 CRC 正确但 ELF 语义非法，证明失败来自目标代码而非宿主预检查。

`v0.5` 把描述符表和异常控制流纳入四层验证：

- C++ 单元测试逐字段构造并解码 64 位 TSS 描述符和 IDT gate，检查精确
  结构大小、TSS type/present、完整处理器地址、IST 三位掩码、保留字段和
  32 个向量的硬件错误码分类。
- 固定种子 `0xD35C71A05EED6405` 生成 4096 个完整 64 位地址，同时验证
  TSS descriptor 与 IDT gate 编码解码往返，共 8192 条性质断言。
- ELF 集成审计除入口、段和未解析符号外，还要求描述符装载、异常公共入口、
  C++ 分发器、异常桩表和向量 0..31 的 32 个独立符号全部存在。
- 正常 QEMU 路径必须回读并确认 GDTR、IDTR、CS、SS、TR 和 TSS，经过
  `INT3 → BREAKPOINT_HANDLED → IRETQ` 后才能输出 `READY`。
- 非法指令镜像必须在 `UD2` 后报告向量 6、错误码 0 和 `PANIC`，禁止
  页故障字段及 `READY`。
- 页故障镜像必须访问首个未映射地址 `0x04000000`，报告向量 14、错误码 0、
  同值 CR2 和 `PANIC`，禁止 `READY`。
- 两份故障镜像只替换入口选择，描述符、异常桩、分发器和 panic 均使用生产
  实现，避免“测试了一份并未交付的异常代码”。

`v0.6` 把物理所有权、地址翻译和权限执行纳入同一证据链：

- 内存图单元测试覆盖空指针、空图、零长度、地址溢出、乱序、重叠和受管范围
  无可用 RAM，并核对描述字节、可用字节和受管可用字节。
- 页帧单元测试覆盖初始化、保留、分配顺序、释放复用、重复释放、耗尽和释放
  保留页；跨越已分配页的保留操作必须整体失败，不能留下部分修改。
- 堆与页表布局单元测试覆盖对齐、失败不修改输出、耗尽、48 位 canonical
  地址、四级索引和叶表项权限往返。
- 集成测试用 QEMU 当前的内存图形状复现低端 RAM 与高端保留区，验证平台、
  内核、栈保留后的首个可分配帧和统计；交接布局同步覆盖 BootInfo v2、
  `fw_cfg` 暂存和内存图。
- 固定种子 `0x6D656D6F72793634` 生成 8192 个物理地址/权限组合，并执行
  4096 步随机分配/释放，与独立布尔所有权模型逐步比较。
- 正常 QEMU 路径必须完成 `MEMORY_MAP_VALID`、`FRAME_ALLOCATOR_READY`、
  `PAGING_READY`、`MEMORY_PERMISSIONS_VALID`、`HEAP_SELF_TEST_PASSED`
  后才能到达 `READY`。
- Stage 1 内存图失败镜像必须在 `LONG_MODE` 后输出 `MEMORY_MAP_INVALID`，
  禁止读取 Kernel 或进入内核。
- not-present 页故障继续验证错误码 0；新增 Ring 0 写只读页故障必须验证
  错误码 `0x3` 和 CR2=`0xFFFF800000100000`。后者是 `CR0.WP` 的执行证据，
  不能用软件查询页表项替代。

`v0.7` 把异步硬件事件与设备状态机纳入同一证据链：

- 设备模型单元测试覆盖 PIC 的 IRQ/向量双向映射、掩码失败原子性，PIT
  频率范围、除数舍入与时间溢出，扫描码 make/break/`E0` 序列，以及 ATA
  LBA28、缓冲区长度和启动描述符 magic。
- 启动集成测试按生产顺序开放 IRQ0、IRQ1，核对最终掩码 `0xFFFC`，并组合
  PIT 配置、键盘 `A` 键解码和 LBA 0 描述符校验。
- 固定种子 `0x1A7E22D3C4B5A697` 执行 4096 轮 IRQ 往返、PIT 有效参数和
  键盘按下/释放性质；每轮同时验证输出只在成功后改变。
- ELF 审计新增硬件 IRQ 公共入口、C++ 分发器、桩表和
  `os_kernel_hardware_interrupt_vector_32..47` 全部符号。
- 正常 QEMU 路径必须证明传统路由已接管、PIC/PIT/PS2/ATA 已初始化，等待
  至少 16 个真实 IRQ0 并输出单调毫秒，然后才到达 `READY`。
- 当时的 `v0.7` 成功路径在 `READY` 后由宿主通过 QMP 键盘前端注入 `A`。
  测试必须观察目标机 IRQ1 输出
  扫描码 `0x1E` 与 `A_PRESSED`；宿主不写端口、不写来宾内存，也不调用内核。
- 成功路径禁止 `DEVICE_INITIALIZATION_FAILED`、异常与 panic。IRQ 热路径
  不逐 tick 输出；宿主为每条串口行附加单调到达时间，便于判断停滞边界。

`v0.8` 把特权级、用户 ELF、系统调用和故障隔离纳入同一证据链：

- 用户 ELF 单元测试从最小合法文件出发，覆盖空指针、截断、标识、类型、
  机器、版本、头大小、程序头数量与范围、未知头、权限、对齐、文件范围、
  内存范围、重叠、总页数和入口失败。
- 固定种子 C++ 随机测试对 16,384 条地址范围性质断言，特别覆盖低地址、
  规范边界和无符号加法溢出；Python 随机测试独立破坏 ELF 字段并检查拒绝。
- 边界集成测试验证 Ring 3 扩展帧、四页栈与 guard、用户地址范围，以及
  `0x80`、调用 1/2 的 ABI 稳定性。
- 三个实际用户 ELF 分别由独立 Python 审计器检查 AMD64 `ET_EXEC`、入口、
  `PT_LOAD`、W^X、对齐和用户窗口，不以“链接成功”替代格式证据。
- 正常 QEMU 必须拒绝未映射用户指针和未知编号，输出 Ring 3 消息，以 0
  退出，报告六次系统调用，恢复内核后继续输出 `READY` 并处理键盘 IRQ。
- 用户非法指令镜像必须报告向量 6、错误码 0；用户页故障镜像必须报告向量
  14、错误码 `0x4` 和 CR2=`0x30000000`。两者必须输出
  `USER_TERMINATED` 和 `USER_RETURNED_TO_KERNEL`，禁止 `PANIC`。
- 截断用户 ELF 必须在 Ring 3 和设备初始化前报告验证状态 2；禁止出现用户
  入口、用户文本或 `READY`。
- Kernel ELF 审计新增系统调用入口/分发、用户进入/恢复和三个内嵌 ELF
  边界符号，防止链接图漏掉关键汇编路径。

`v0.9` 把调度策略、硬件切换和资源生命周期分层验证：

- 纯 `ProcessScheduler` 单元测试覆盖未初始化、零量子、容量、创建回滚、
  PID 单调、量子边界、终止和越界读取。v0.9 当时同文件中的静态栈布局检查
  已在动态栈迁移后删除，栈所有权改由独立的单元/集成/随机三层测试承担。
- 集成模型让三个进程执行 24 tick，断言每个恰得 8 tick，并核对抢占、
  终止交接和派发统计。
- 固定种子随机测试生成 4096 组 1..4 进程、1..8 tick 量子和 1..64 tick
  序列；每个 tick 后都要求恰有一个 Running，所有 run tick 之和守恒。
- QEMU 真实路径由 PIT IRQ0 抢占一个 smoke 和三个同址 worker，要求每个
  worker 按自己的 PID 完成三轮计算，三个 BSS 隔离标记全部出现。
- 内核在最后退出后比较创建前后页帧统计；宿主再对调度日志执行顺序、精确次数
  和十六进制下界验证。两层都通过才接受 `SCHEDULER_COMPLETE`。
- 既有 Ring 3 `#UD/#PF` 用例改走单进程调度生命周期，证明新增调度器没有
  把用户异常重新退化成内核 panic。

`v1.2` 在保留上述历史回归的同时，用新对象模型替换旧调度器证据：

- `ThreadScheduler` 单元测试覆盖 256 Process/512 Thread、单 Process
  64 Thread、无效内核栈槽、独立单调 PID/TID、两级退出/reap 和状态守恒；
- WaitQueue 测试让五种 WakeReason 按 FIFO 各自获胜，再对同一 Thread
  发起重复唤醒，必须得到 `WakeAlreadyResolved`；
- Mutex 测试覆盖竞争阻塞、直接 handoff、新 owner 确认、无 waiter 解锁，
  并证明持 spinlock 时阻塞被拒绝；
- 集成模型让三个 Process 的六个 Thread 执行 48 tick，每个恰得 8 tick，
  再完成阻塞、唤醒、全部退出和 Process/Thread 两级回收；
- 固定种子 `0x5448524541445632` 执行 100000 步 create、schedule、
  block、wake、exit、reap 与槽位复用，每一步验证三个 WaitQueue、当前
  Thread、状态集合和累计计数；
- FXSAVE 布局单元测试锁定 512 字节/16 字节对齐和 CPUID FXSR/SSE/SSE2
  合取；正常 QEMU 要求四个不同用户模式全部隔离且 save/restore 非零；
- `qemu64,-sse2` 失败用例要求同一镜像在 GDT、用户态和 READY 前明确停在
  `EXTENDED_STATE_UNSUPPORTED`。

上述两项是 v1.2 发布时的历史证据。v1.3 将能力检查统一为完整
`ProcessorFeatureProfile`，当前主动失败用例改为 `qemu64,-syscall`，并验证
`PROCESSOR_FEATURES_UNSUPPORTED` 与非零 missing mask。

`v1.3` 为架构入口增加四层证据：

- 处理器 profile 单元测试覆盖六项必需能力、36..52 位物理宽度和固定 48 位
  虚拟宽度；
- UserContext 单元测试覆盖初始、IRQ、`INT 0x80`、`SYSCALL` 四类来源，
  低/高规范地址边界、段、RFLAGS 和 SYSRET/IRET 选择；
- CpuLocal/MSR 布局集成测试覆盖初始化、Thread/可信栈同步、IRQ/抢占深度、
  延迟调度、非局部清理、STAR 选择子算术与回读差异；
- 固定种子 `0x5A17C011BADC0FFE` 对 100000 个现场执行 400000 项性质检查；
- 256 MiB QEMU 必须同时观察双入口等价、SYSRET、原生 IRET 回退、IRQ 打断
  系统调用和返回前 reschedule，且结束时深度/need-resched/拒绝返回为零；
- `qemu64,-syscall` 必须在扩展现场、GDT 和用户态前输出缺失能力位图并停止。

`v1.4` 为对象与动态描述符增加四层证据：

- FileTable 单元测试覆盖依赖和限额、精确/最低安装、所有权转移、临时 lookup、
  duplicate 共享 generation、独立 fd flags、编号复用、close-on-exec 和销毁；
- FileDescription 集成测试把真实 legacy FileSystem、Pipe 和 KernelHeap
  组合起来，验证共享/独立 offset、端点最后引用和 finalizer 守恒；
- capacity 集成测试实际安装 4096 个 fd 和 64 个分块，填满后再 duplicate
  必须原子返回 limit，随后关闭全部 fd 并回收对象和堆；
- 固定种子 `0x46445441424C4531` 执行 100000 步 open、duplicate、close、
  soft-limit 修改，与独立 4096 项参考表逐步比较；
- PID4 Ring 3 真实读写文件、duplicate、CLOEXEC、限额失败和最低编号复用；
  256 MiB/64 GiB 使用 minimum 64，64 MiB 兼容档使用 minimum 8，内核核对
  读取 9 字节、写入 8 字节；
- 256 MiB/64 GiB QEMU 分别要求 hard limit 精确为 256/4096，退出后 active
  对象/引用为零、finalizer 无失败、分块申请/释放相等及三层 validation 为一。

`v1.5` 为路径、挂载和双后端增加四层证据：

- VFS 单元测试覆盖绝对/相对路径、重复分隔符、`.`、`..`、root clamp、
  尾斜杠、4096/255 精确边界、独立打开偏移、目录枚举、getcwd、挂载进入/
  退出、挂载表耗尽和人为父链环；
- 双后端集成测试把同一创建、切换 cwd、文件读写、目录枚举、同步和校验契约
  分别运行在 memfs 与 legacy-fs 上；legacy 写入后由全新适配器重新挂载读取，
  memfs Destroy 后 KernelHeap 活动分配必须归零；
- 固定种子 `0x5646532026001500` 执行 100000 步创建、解析、chdir、父目录、
  文件 I/O、目录枚举和缺失路径，与独立目录树/字节模型逐步比较；结束时再
  检查 VFS、memfs 和 heap，形成 100001 项断言；
- 256 MiB functional QEMU 由真实 Shell 先进入 `/tmp` memfs，以相对路径和
  `./session/../session/message` 完成操作，再返回 legacy 根目录创建持久文件；
  VFS READY、mount、两次 validate、Shell `cd/pwd` 次数和资源快照均由宿主
  协议检查；
- 文件系统持久化仍使用同一可写磁盘的两个全新 QEMU 进程，并继续破坏
  superblock 验证非零损坏介质不被自动格式化；
- 用户非法指令、用户页故障和非法 ELF 三项隔离/拒绝路径继续运行，证明 VFS
  初始化没有改变故障发生顺序或资源清理边界。

`v1.6` 为生产 rootfs、完整命名空间和磁盘满增加五层直接证据：

- 格式单元测试逐字段覆盖 little-endian superblock、inode、目录项与
  pointer block 的 round-trip、CRC、保留字节、类型和边界拒绝；
- rootfs 集成测试覆盖稀疏 hole、direct/single/double/triple 寻址、
  64 MiB truncate、unlink/rmdir、rename 替换/跨目录/环拒绝、stat、重挂载
  和定点设备写失败；
- 容量测试使用真实 256 MiB 布局和稀疏宿主存储构造近满块树，先观察非零
  短写，再观察零字节 ENOSPC，并重新执行完整 Validate；
- 命名空间随机测试用同一种子分别驱动 memfs 和 rootfs 100000 步，覆盖
  create/mkdir/read/write/truncate/unlink/rmdir/rename/stat/枚举与预期失败；
- QEMU 持久化使用逻辑 1 GiB 稀疏盘：第一次写入并 flush，第二次严格挂载并
  恢复，宿主独立 fsck 成功，再损坏元数据，第三次必须拒绝且不能进入用户态。

`v0.10` 把共享状态、条件等待和 IPC 生命周期分层验证：

- 管道单元测试逐项覆盖未初始化、非法参数、空/满、部分读写、环形回绕、
  EOF、broken pipe、端点重复关闭和统计一致性。
- 同步集成测试启动四个宿主线程，每个线程执行 50,000 次
  `SpinLockGuard` 保护的递增，最终计数必须精确为 200,000。
- 固定种子管道随机测试执行 32,768 步读、写、关闭和查询操作，每一步都与
  独立字节队列模型比较内容、容量、索引效果、统计和端点状态。
- 调度单元/集成/随机测试加入 Blocked、读/写等待原因、定向唤醒、无 Ready
  后继和 block/wakeup 守恒；Blocked 永远不能被时间片路径选中。
- 生产者和消费者两个新用户 ELF 分别执行完整格式审计；Kernel ELF 还必须
  包含六个用户镜像边界，且不得出现动态初始化/析构区段。
- 正常 QEMU 必须观察 256 字节写入和读取、至少一次读写阻塞、block 与 wake
  相等、一次 EOF、空缓冲、端点均关闭、四份退出码 0 和页帧完全回收。
- 用户日志的生产者/消费者先后顺序不固定；各自内部里程碑顺序、出现次数和
  目标内统计才是稳定协议，避免把合法并发交错写死为测试。

`v0.11` 把磁盘格式、语义一致性与真实持久化分层验证：

- 格式单元测试直接对 512 字节缓冲执行 superblock、inode 和目录项
  编解码，逐类破坏受保护字段，证明 CRC32 与布局验证都实际生效。
- 生命周期集成测试在 4096 扇区内存块设备上执行首次格式化、嵌套目录、
  1300 字节跨块文件、关闭、同步、新实例重挂载和截断；随后分别制造孤儿
  inode、非法 `DEL` 名称和超级块 CRC 错误并要求拒绝。
- 固定种子随机测试执行 128 轮随机长度、随机内容的 truncate/rewrite，
  每轮销毁 `FileSystem` 与缓存对象、重新挂载，再与宿主参考数组逐字节比较。
- 正常 QEMU 既检查生产者/消费者文件里程碑，也解析每进程文件读写字节和
  superblock 代次；目标内还需完成同步、全盘一致性与独立载荷读回。
- 专用持久化测试稀疏复制一份已由构建系统格式化的真实启动盘并关闭 snapshot：
  第一次启动严格报告 `ROOTFS_V2_MOUNTED` 并写入，第二次启动必须先报告同一
  挂载标记与旧载荷恢复，再允许重写。
- 第二次启动后，宿主只翻转文件系统超级块中的一个受 CRC 保护字节。第三次
  必须报告 `FILE_SYSTEM_CORRUPT`，禁止进入 Ring 3、自动格式化或到达
  `READY`。

`v1.0` 把交互式用户环境、统一描述符和空闲唤醒纳入同一证据链：

- 控制台输入单元测试覆盖 FIFO 顺序、满缓冲拒绝、丢弃计数、空读和统计守恒；
  描述符表单元测试覆盖标准描述符、动态分配、端点权限、容量、关闭与槽位复用。
- Shell 解析器单元测试覆盖空白、引号、转义、参数上限、行长上限和错误原子性；
  固定种子随机测试执行 4096 轮任意字节输入，要求所有切片始终位于固定存储内。
- 文件系统生命周期集成测试通过 `OpenDirectory/ReadDirectory` 枚举目录，
  同时确认普通文件描述符不能当作目录读取。
- 调度器测试新增“唯一运行进程阻塞且没有 Ready 后继”的合法状态，并证明
  条件变化后按等待原因唤醒，用户包装会重新检查具体描述符。
- 第七个真实用户 ELF 为 Shell；每个用户 ELF 都执行 AMD64、入口、加载段、
  W^X 和符号审计，Kernel ELF 继续禁止 `.init_array` 等动态初始化区段；
  反汇编审计还要求空闲函数内的 `STI/HLT/CLI` 三条指令精确相邻。
- 正常 QEMU 在 Shell 输出 `READY` 后，通过 QMP 逐字符输入十条命令。109 个
  字符必须全部经过 i8042、IRQ1、控制台 FIFO、描述符 0 和 Shell，提交数与
  读取数精确相等，丢弃数和最终缓冲数均为零。
- 宿主精确检查每条 `COMMAND` 标记、文件内容、目录枚举、未知命令和退出，
  同时保留管道、抢占、资源回收与失败路径证据；持久化双启动也执行同一套
  Shell 脚本，避免另设绕过用户边界的测试入口。

`v1.1` 动态物理内存第一增量新增容量与高地址证据：

- 内存图单元测试区分“最高描述地址”和“最高可用 RAM 地址”，并覆盖启动
  元数据搜索的保留区跳过、对齐、容量不足和溢出保留区拒绝。
- 页帧单元测试精确验证 64 GiB 的 2-bit 状态容量为 4 MiB，并用稀疏图在
  4 GiB 以上执行范围分配、释放与未对齐拒绝。
- 集成模型构造 3--4 GiB 物理洞和总计 64 GiB 可用 RAM，要求全部
  16777216 个可用页进入状态机；随机测试再执行 1024 轮高地址窗口分配。
- 主系统用例明确传入 `--memory-mebibytes 65536`，而不是依赖工具默认值。
  宿主解析来宾十六进制统计，要求可用/受管/direct-map 字节均至少 64 GiB、
  物理/虚拟地址宽度至少 36/48、状态存储至少 4 MiB、至少一个 2 MiB
  direct-map 页，并且高内存自检地址不低于 4 GiB+4 KiB。
- 目标内高内存自检查询 direct-map 权限，写入并读回两个 64 位模式，再释放
  页帧；最终进程资源回收检查继续证明后续高地址页表没有泄漏。

`v1.1` 可回收内核堆增量把对象生命周期纳入同一证据链：

- 单元测试覆盖无效区间、重复初始化、1/16/64/256/4096 字节对齐、失败
  原子性、非法/内部/重复释放、向前/向后/三向合并、地址复用和全部统计；
- 集成测试使用目标同规格 64 KiB 缓冲，组合小对象、缓存行和页对齐对象，
  写入模式后乱序释放，并证明完整堆最大负载可以再次分配；
- 固定种子 `0x4845415056313031` 执行 100000 次随机申请/释放，逐步核对
  对齐、活动负载首尾模式、区间不重叠、请求字节、累计计数和失败输出，
  每 64 步执行完整物理块/空闲链交叉校验；
- 目标启动自检写回两个不同对齐对象后逆序释放，QEMU 要求活动分配精确为
  零、峰值和最大连续空闲负载非零，再接受 `HEAP_SELF_TEST_PASSED`。

`v1.1` buddy 增量把连续物理页生命周期纳入同一证据链：

- 单元测试精确核对 64 页需要 36 字节双位图，覆盖缺失/过小元数据、已有活动
  页拒绝初始化、最大对齐分解、分裂合并、无效阶、范围失败原子性、错阶、
  错位、reserved 页、重复释放、初始化后保留冻结，以及精确 order-0
  所有权查询不会接受大块内部页或已释放页；
- 集成测试构造 1024 页、256 页 E820 洞和 32 页启动保留，要求 order 5
  连续块完整落入指定高地址半开区间；order 0/3/5/6 混合乱序释放后页与块
  统计必须回到基线；
- 固定种子 `0x425544445936344D` 执行 100000 步随机申请、释放、耗尽和重复
  释放，与逐页布尔参考模型同步；每 257 步交叉核对每阶位图、页状态、父子
  不重叠和加权统计；
- 64 MiB 与 64 GiB 目标自检都申请 order 3 的 8 页连续块，经 direct-map
  在首尾页写回模式后释放。64 GiB 额外要求元数据至少 8 MiB、最大阶至少
  24、自检物理地址高于 4 GiB；
- 系统测试要求 `BUDDY_ACTIVE_BLOCKS` 反映真实页表和 heap 持有量，而不是
  错误要求为零；`BUDDY_SELF_TEST_PASSED` 证明自检相对进入前活动块数恢复
  基线。热路径不逐次写串口。

`v1.1` 固定尺寸类型缓存增量进一步分离每种对象的容量与生命周期：

- 单元测试覆盖未初始化、无效尺寸/对齐/容量、布局溢出、后备堆不足、重复
  初始化、耗尽输出保持、空/内部/外部指针、活动对象销毁、重复释放、LIFO
  复用和最终归还；1 字节对象与 9 槽配置额外验证 8 字节最小步长和非整
  字节位图；
- 集成测试让 8、64、256 字节对齐的三个缓存共享目标同规格 64 KiB 堆，
  交错申请/释放、保持其他缓存活动数据、拒绝提前销毁，最后乱序销毁并要求
  堆恢复单一连续空闲区；
- 固定种子 `0x5459504543414348` 执行 100000 步随机申请/释放，以独立记录
  核对活动指针唯一性、首尾数据模式、活动/空闲/累计统计；每 257 步运行
  位图与空闲链完整校验，并周期性尝试重复释放；
- 目标自检耗尽 32 个 64 字节对齐槽，验证第 33 次申请不改输出、全部模式
  可读回、偶/奇交错释放、重复释放拒绝和 LIFO 复用。QEMU 要求最终活动数
  精确为零、空闲数 32、成功申请/释放 33、峰值 32，再接受
  `TYPE_CACHE_SELF_TEST_PASSED`；
- 缓存销毁后比较通用堆进入前后的当前占用、活动申请和最大空闲块，证明唯一
  后备申请已经归还；申请/释放热路径不逐次打印串口。

### KVA 分配与映射生命周期

- 单元测试使用 64 页窗口和外部描述符数组，覆盖未初始化、空元数据、跨
  canonical 空洞、保留重叠、best-fit、绝对页对齐、输出失败原子性、错误
  页数、内部地址、重复释放、空洞复用、描述符耗尽、地址耗尽和元数据损坏；
- 集成测试把 `KernelVirtualAddressAllocator`、`PhysicalFrameAllocator` 与
  `PageTableManager` 串成真实六页生命周期：首尾两页保持 not-present，中间
  四页核对物理身份和 supervisor RW/NX 权限，撤销后数据帧与 KVA 回到基线；
- 固定种子 `0x4B564152414E444F` 在 512 页独立逐页模型上执行 100000 步。
  模型独立重算每个空闲段、绝对对齐和 best-fit 结果；每 257 步比较活动页、
  保留页、描述符、累计计数和最大空洞并运行 `Validate`；
- 目标自检在 32 TiB 窗口申请六页、八页对齐区间，从 buddy 取得 order 2
  后备，只映射中间四页并真实写回。清理顺序固定为 unmap、释放物理块、释放
  KVA；最终活动页为零、保留页为一、两次申请与两次释放守恒，才接受
  `KVA_SELF_TEST_PASSED`；
- 目标自检先用一页暖机验证共享边界：撤销时回收 PT 与 PD，保留可能仍由
  进程根引用的共享 PDPT；主事务最后一页撤销时再次回收 PT 与 PD。两段事务
  合计必须回收两张 PT、两张 PD、零张 PDPT，并精确保留一张共享 PDPT。

### 页表空分支所有权与回滚

- 单元测试分别建立 `Exclusive`、`KernelShared` 与 `Process` 根，覆盖重复
  初始化、单叶完整级联、相邻叶共享表、共享 PDPT 保留、进程程序/栈分支、
  借用分支拒绝、无效物理地址、查询祖先环、递归销毁祖先回指和 order-0
  所有权损坏；销毁拒绝后修复原项必须能够在同一环境中成功重试；
- 映射耗尽故障注入必须证明输出和既有父项不变，新建表帧全部逆序释放，
  因用户叶映射提升的祖先 U/S 位也恢复原值；
- 集成测试连续执行 128 次共享根生命周期与 64 次进程根生命周期；每轮都
  核对回收层级、表帧统计、共享边界和最终递归销毁，不允许用重建测试环境
  隐藏累积泄漏；
- 固定种子 `0x5047545245434C4D` 在 1024 个虚拟页上执行 100000 步随机
  映射/撤销，覆盖四个 PML4 分支、每分支两个 PDPT 和多个 PD/PT。独立模型
  从叶集合推导应存在的表数，每 257 步比较查询结果、回收层级、页帧统计和
  管理器完整性，最后排空；
- QEMU 协议精确解析四个回收计数和一个通过标记。映射/查询/撤销热路径不
  逐项打印日志，避免十万步模型和进程退出路径冲刷真正的阶段边界。

### 动态内核栈生命周期

- 单元测试覆盖管理器初始化、槽位边界、重复创建/销毁、六页布局、双 guard、
  四页清零、supervisor RW/NX、物理帧唯一性、精确地址包含、KVA/物理页
  耗尽和失败输出不变；
- 破坏测试分别制造“空闲 KVA 上残留 PTE”“活动栈丢失 KVA allocation”和
  “数据叶项被外部撤销”，要求管理器拒绝复用或销毁，并由完整校验明确报告
  损坏；
- 集成测试创建四个栈，把真实 176 字节用户特权帧放在每个栈顶，再从克隆的
  独立进程 CR3 查询共享高半映射，证明 guard 缺席、数据页身份和 supervisor
  权限不只在内核根中成立；逆序销毁后 frame 与 KVA 恢复空闲基线，共享
  根只保留一张仍可被进程 PML4 引用的 PDPT；
- 固定种子 `0x4B535441434B524E` 在 4096 页独立 best-fit 所有权模型上执行
  100000 步创建/销毁，使用 64 个槽位；每 257 步比较活动、累计、峰值、
  映射页、guard 页、KVA 页和物理帧统计并运行完整校验，最后排空；
- QEMU 正常路径要求 PID1 的 lower/top/upper 地址可观察，并由运行时聚合
  统计证明峰值至少八栈/三十二映射页；进程阶段前后的累计创建与销毁都必须
  精确增加十一次。最终活动数必须为零并输出
  `KERNEL_STACK_RESOURCES_RECLAIMED`。用户
  `#UD` 与 `#PF` 隔离镜像也必须创建并安全点回收单栈，证明异常路径不会
  留下资源。

### 通用资源生命周期与跨层快照

- `ReferenceCounter` 单元测试覆盖未启动、零初值、获取、释放、最后释放、
  零后禁止复活、上溢拒绝、空输出拒绝和失败时输出保持；它只描述单 BSP
  所有权语义，不把尚未定义的原子内存序伪装成并发实现；
- `ScopeRollback` 单元测试覆盖外部固定动作存储、容量耗尽、严格逆序回滚、
  提交后不执行、显式回滚幂等拒绝、析构安全网，以及一个动作失败后仍继续
  清理其余动作；动态内核栈创建复用同一实现登记九项补偿动作；
- 固定种子 `0x5245534F55524345` 执行 100000 个事务。独立模型随机组合动作
  注册、提交、显式回滚、容量耗尽和注入失败，逐轮核对动作次序、调用次数、
  最终状态与全部外部资源归零；
- `ResourceSnapshot` 单元测试覆盖 26 个字段、每一位差异掩码、变化字段数、
  完全相等、frame/buddy/heap/KVA/stack 守恒式损坏、空输出与失败输出不变；
  当前累计申请等历史量有意不进入快照；
- 集成测试用真实 frame allocator、buddy、页表、KVA 和栈管理器创建四个
  双 guard 栈。活动快照必须同时反映 4 个栈、16 个物理页、24 个 KVA 页和
  16 个映射页；逆序销毁后 26 字段差异掩码与变化字段数必须都为零；
- 目标内存初始化在最后一个保留槽创建一个真实动态栈，再由外层
  `ScopeRollback` 销毁并比较前后快照。八进程退出后的独立快照再次证明
  frame、buddy、heap、KVA 与栈全部恢复。只有两层检查都通过，才允许输出
  `RESOURCE_LIFECYCLE_SELF_TEST_PASSED` 和
  `PROCESS_RESOURCE_SNAPSHOT_MATCHED`；
- `os_qemu_functional_smoke` 明确使用 256 MiB RAM，执行与 64 GiB 主路径
  相同的磁盘 PID1、十一进程生命周期、Shell、exec、虚拟内存、文件系统、
  用户隔离和资源快照
  协议。它不是
  只验证启动标记的缩减镜像，也不通过条件编译切换资源实现。

### v1.7 PID1、进程树与磁盘程序映像

v1.7 把“内核预先嵌入普通程序并直接创建固定进程”替换为磁盘
`/sbin/init` 启动链。测试必须分别证明纯状态机、跨模块事务和真实目标机
行为，不能只看到一行 PID1 日志就宣布完成。

- `ProcessTree` 单元测试覆盖 PID 1 唯一性、父子注册、指定 PID/任意子进程
  wait、Alive→Zombie→Unused、退出状态、孤儿重设父进程、Init 最后回收、
  统计守恒和损坏检测；
- `ProgramArgumentPlan` 单元测试覆盖 16 字节 RSP 对齐、`argc/argv/envp` 指针
  排列、字符串 NUL、空向量、每类 256 项、128 KiB 总字符串边界、算术溢出、
  栈容量不足和失败不发布布局；
- reader 形式的 ELF 单元测试以短读、截断头、越界程序头、W+X、重叠段和
  底层读取失败构造输入，要求解析器区分 `ReadFailed` 与语义非法 ELF；
- 十四个合法用户 ELF 各自执行离线结构审计；审计器的 512 页上限由 Python
  单元测试与 Kernel 公开头文件交叉核对，避免宿主工具停留在旧 32 页规格；
- `ThreadScheduler::CommitProcessImage` 单元测试要求只有当前 Process 的唯一
  Running Thread 可以原子替换调度器记录的 CR3 与用户 RSP，非法状态不能
  改变旧值；RIP 和完整用户现场由目标运行时在提交后重建；
- 生命周期集成测试连续执行 4096 轮父进程、子进程、孤儿收养、退出与 wait，
  共完成 8192 个子 Process 生命周期；每轮都恢复空树与同一统计关系；
- 固定种子随机测试让 8192 个子进程按随机顺序退出和回收，并生成 4096 组
  参数/环境布局，与独立 64 位参考计算逐项比较；
- rootfs 工具测试把 ELF 安装到嵌套目录，重新打开真实镜像并逐字节回读，证明
  构建图不是只生成了宿主文件，而是把程序写入生产 rootfs。

正常 QEMU 路径必须从磁盘读取 `/sbin/init` 并把它注册为 PID 1。Init 再创建
六个直接子进程；其中 orphan parent 创建第七个后退出，使 orphan child 被
重设父进程到 PID 1。验收器要求观察并按数量检查：

```text
[OS][KERNEL][PROC] SPAWN_PID=0x0000000000000001
[OS][USER][INIT] STARTED
[OS][USER][INIT] ARGUMENTS_VALID
[OS][USER][PROC] ARG_ENV_128K_VERIFIED
[OS][USER][PROC] EXEC_FAILURE_PRESERVED_IMAGE
[OS][USER][PROC] EXEC_E2BIG_PRESERVED_IMAGE
[OS][USER][PROC] EXEC_COMMITTED
[OS][USER][INIT] ORPHAN_REAPED
[OS][USER][INIT] ALL_CHILDREN_REAPED
[OS][USER][INIT] NO_ZOMBIES
[OS][KERNEL] PROCESS_TREE_VALID
```

参数探针必须真实接收恰好 128 KiB 的字符串区；`exec` 探针必须先后证明截断
ELF 与超限参数失败后仍能在旧 RIP/CR3 映像继续执行，随后成功提交
`/bin/exec_target`。成功提交后旧地址空间、用户栈和 close-on-exec 描述符才
允许释放；提交前任一步失败都必须销毁候选映像并保持旧 Process 可运行。

结束汇总要求 registered/exited/collected 均为 8，reparented 为 1，
zombie 与 active 均为 0；wait success 为 7，并至少出现一次真实阻塞和一次
no-child。目标内 `ProcessTree::Validate`、调度器状态、26 字段资源快照与宿主
日志协议必须同时通过。4096 轮宿主模型用于放大状态组合，QEMU 用于证明真实
CR3、RSP、ELF、rootfs、系统调用与安全点回收；两者互补，不能互相冒充。

### v1.8 匿名 VMA、用户页故障与 heap

v1.8 的测试必须分别证明“地址已经承诺”和“物理页已经驻留”，不能只比较
系统调用返回地址。

- VMA 单元测试覆盖池初始化、严格排序、重叠拒绝、前后/双向合并、中段
  split、kind mismatch、first-gap、单进程 hard limit、元数据耗尽失败
  原子性和 destroy 后池守恒；
- 固定种子 VMA 参考模型执行 100000 步 map/unmap/split/merge。每一步逐项
  对照全部区间、area/page 统计、池 active/free 与结构 `Validate`；
- UserHeap 单元测试覆盖 program-break 增长、16 字节对齐、first-fit、
  split、释放复用、前后 coalesce、耗尽、错误 break、外部/内部指针、重复
  释放和失败时旧 allocation 保持；
- 固定种子 heap 模型执行 100000 步申请/释放，对每个活动 allocation 保存
  独立尺寸和字节模式，并在操作间逐字节复验；
- 页表/VMA 集成测试重复 128 个 Process 地址空间：四页 reservation 不得
  改变 frame 数，首次触页才建立 U/S RW/NX PTE，中段撤销必须形成两个 VMA
  并回收两级空页表，销毁后 frame 与 descriptor 回到精确基线；
- 用户边界集成测试冻结 2048 页/8 MiB stack reservation、永久 guard、
  匿名窗口和系统调用 39..42，防止工具或旧 64 页假设漂移；
- 三个新增用户 ELF 分别通过 AMD64、入口、W^X 和未解析符号审计。

真实 QEMU 的 memory probe 必须观察以下一次性标记：

```text
[OS][USER][VM] DEMAND_ZERO_VERIFIED
[OS][USER][VM] ANONYMOUS_UNMAP_RECLAIMED
[OS][USER][VM] PROGRAM_BREAK_VERIFIED
[OS][USER][VM] STACK_GROWTH_VERIFIED
[OS][USER][VM] USER_HEAP_RANDOMIZED_VERIFIED
[OS][USER][VM] COMPLETED
[OS][USER][INIT] MEMORY_PROBE_REAPED
[OS][USER][INIT] VM_FAULT_POLICIES_VERIFIED
```

guard 与 protection probe 必须各产生一个由 PID1 wait 观察到的用户 vector
14 退出，Kernel 不得 panic。demand/stack 内核日志按累计二次幂采样，因此
runner 只要求它们至少出现且数值非零，不错误冻结为恰好一行。

工作负载结束后 VMA capacity 必须为 8192，active 为 0，peak/acquire/release
非零且 acquire 等于 release。ProcessTree registered/exited/collected 为
11，wait success 为 10，Zombie 为 0；这些新字段与既有资源快照共同通过。

`os_qemu_bootstrap_smoke` 把 64 MiB 完整链固化为独立 CTest，而不是发布时
手工传参。256 MiB functional 与 64 GiB capacity 运行同一 VM probe；三档
只改变资源规格，不切换实现。

## 验收证据

- 固定构建命令与工具链版本。
- 可机器判断的串口标记和有界 QEMU 生命周期。
- 失败日志包含阶段、模块和错误类型。
- 关键数据结构可通过反汇编或 GDB 检查。
- 回归测试可以在无图形界面的环境中运行。

## 完成标准

实现、测试、文档、教材和调试方法必须在同一次阶段交付中保持一致。仅在本地
手工启动成功不能视为阶段完成。

每个小版本还必须完成独立网站验收：代码目录和发布清单由已推送主仓 SHA
重新生成，教材 PDF 哈希一致，网站测试与生产构建通过，精确 web commit
已经推送并保存为 Sites 版本，公开部署成功后从公网验证首页、当前发布文档、
新增代码路由、教材下载和 sitemap。任一步失败时，状态只能是“实现完成但公开
发布未完成”，不得开始下一阶段。详细顺序见 [发布闭环](releasing.md)。

## 运行方式

完整验证：

```bash
python3 tools/os.py verify
```

按测试层运行：

```bash
python3 tools/os.py test --layer unit
python3 tools/os.py test --layer integration
python3 tools/os.py test --layer randomized
python3 tools/os.py test --layer system
python3 tools/os.py test --layer failure-path
```

当前测试：

| 测试 | 层级 | 主要验证 |
| --- | --- | --- |
| `os_foundation_unit_tests` | 单元 | 地址类型、半开区间、空区间和溢出 |
| `os_foundation_resource_lifecycle_primitives_unit_tests` | 单元 | 引用计数状态机与固定存储逆序作用域回滚 |
| `os_foundation_integration_tests` | 集成 | ROM、复位向量、Stage 1 与内核区间关系 |
| `os_kernel_handoff_layout_integration_tests` | 集成 | 页表、描述符、BootInfo、暂存区、加载窗口与内核栈互不重叠 |
| `os_foundation_randomized_tests` | 随机 | 10,000 组区间性质与溢出拒绝 |
| `os_foundation_resource_lifecycle_primitives_randomized_tests` | 随机 | 固定种子 100000 个事务与独立动作/资源模型 |
| `os_kernel_boot_info_unit_tests` | 单元 | BootInfo 全字段、上下界和失败状态 |
| `os_kernel_descriptor_layout_unit_tests` | 单元 | TSS、IDT gate、异常错误码和恢复分类 |
| `os_kernel_descriptor_layout_randomized_tests` | 随机 | 4096 组 64 位 TSS/IDT 地址编码往返 |
| `os_kernel_extended_state_layout_unit_tests` | 单元 | FXSAVE 512/16 布局、CPUID FXSR/SSE/SSE2 解码与必需能力合取 |
| `os_kernel_processor_features_unit_tests` | 单元 | 完整 CPUID profile、必需能力位图与物理/虚拟地址宽度 |
| `os_kernel_user_context_unit_tests` | 单元 | 四类用户现场、规范地址、段、RFLAGS 与返回选择 |
| `os_kernel_native_system_call_integration_tests` | 集成 | CpuLocal 生命周期、深度/统计与原生系统调用 MSR 布局 |
| `os_kernel_user_context_randomized_tests` | 随机 | 固定种子 100000 个用户现场与 400000 项性质 |
| `os_kernel_physical_memory_map_unit_tests` | 单元 | 内存图结构、最高可用地址、元数据区搜索与溢出 |
| `os_kernel_physical_frame_allocator_unit_tests` | 单元 | 2-bit 帧状态、64 GiB 容量、高地址范围分配与回收 |
| `os_kernel_buddy_frame_allocator_unit_tests` | 单元 | 双位图尺寸、初始化、分裂合并、失败原子性与非法释放 |
| `os_kernel_heap_and_page_layout_unit_tests` | 单元 | 早期堆、canonical 地址、四级索引和页权限 |
| `os_kernel_heap_unit_tests` | 单元 | 可回收堆的对齐、原子失败、非法释放、合并、复用与统计 |
| `os_kernel_type_cache_unit_tests` | 单元 | 固定尺寸缓存布局、耗尽、精确释放、LIFO、最小槽和销毁 |
| `os_kernel_virtual_address_allocator_unit_tests` | 单元 | KVA 保留、best-fit、绝对对齐、精确所有权查询、释放、两类耗尽与损坏检测 |
| `os_kernel_resource_snapshot_unit_tests` | 单元 | 26 字段快照、守恒式、逐位差异与失败原子性 |
| `os_kernel_stack_manager_unit_tests` | 单元 | 动态栈双 guard、清零、权限、回滚、耗尽，以及物理/KVA/PTE 所有权破坏、修复和安全销毁 |
| `os_kernel_page_table_reclamation_unit_tests` | 单元 | 三种根所有权、精确空表、级联回收、借用拒绝、失败回滚与损坏检测 |
| `os_kernel_virtual_memory_area_unit_tests` | 单元 | VMA 排序、合并、拆分、kind guard、first-gap、耗尽事务与池守恒 |
| `os_kernel_memory_bootstrap_integration_tests` | 集成 | 64 MiB 基线、64 GiB 带洞内存图与高端帧 |
| `os_kernel_buddy_frame_allocator_lifecycle_integration_tests` | 集成 | E820 洞、范围连续块与混合阶生命周期恢复 |
| `os_kernel_heap_lifecycle_integration_tests` | 集成 | 64 KiB 回归堆的混合对象、数据保持、耗尽与完整恢复 |
| `os_kernel_type_cache_lifecycle_integration_tests` | 集成 | 三种对齐缓存共享堆、交错释放与乱序销毁恢复 |
| `os_kernel_virtual_address_mapping_lifecycle_integration_tests` | 集成 | KVA、物理帧、页表、双 guard 与逆序回收 |
| `os_kernel_stack_lifecycle_integration_tests` | 集成 | 四栈、用户特权帧、独立进程 CR3 共享高半映射与安全点式逆序回收 |
| `os_kernel_resource_snapshot_lifecycle_integration_tests` | 集成 | 四个真实动态栈的跨 frame/buddy/KVA/PTE/stack 活动与零差异恢复 |
| `os_kernel_page_table_reclamation_lifecycle_integration_tests` | 集成 | 128 次共享根、64 次进程根循环与递归销毁后的表帧守恒 |
| `os_kernel_user_virtual_memory_lifecycle_integration_tests` | 集成 | 128 轮 VMA 预留、首次触页、中段撤销、空页表与 frame/descriptor 基线 |
| `os_kernel_memory_management_randomized_tests` | 随机 | 表项、分配器模型和 1024 轮高地址窗口 |
| `os_kernel_buddy_frame_allocator_randomized_tests` | 随机 | 固定种子 100000 步 buddy 与逐页参考模型 |
| `os_kernel_heap_randomized_tests` | 随机 | 固定种子 100000 步分配/释放与独立活动对象模型 |
| `os_kernel_type_cache_randomized_tests` | 随机 | 固定种子 100000 步槽申请/释放与独立活动记录 |
| `os_kernel_virtual_address_allocator_randomized_tests` | 随机 | 固定种子 100000 步 KVA 与独立逐页 best-fit 模型 |
| `os_kernel_stack_manager_randomized_tests` | 随机 | 固定种子 100000 步动态栈创建/销毁与 4096 页独立所有权模型 |
| `os_kernel_page_table_reclamation_randomized_tests` | 随机 | 固定种子 100000 步映射/撤销与独立层级表数量模型 |
| `os_kernel_virtual_memory_area_randomized_tests` | 随机 | 固定种子 100000 步 VMA 与独立有序区间参考模型 |
| `os_kernel_device_model_unit_tests` | 单元 | PIC、PIT、扫描码和 ATA 纯状态机 |
| `os_kernel_device_bootstrap_integration_tests` | 集成 | IRQ 开放、时钟、键盘与启动盘设备闭环 |
| `os_kernel_interrupt_device_randomized_tests` | 随机 | 4096 轮 IRQ/PIT/键盘组合性质 |
| `os_kernel_user_elf_unit_tests` | 单元 | 用户 ELF 全字段、范围、W^X、重叠与入口 |
| `os_kernel_user_boundary_integration_tests` | 集成 | Ring 3 帧、2048 页/8 MiB 用户栈、guard、匿名窗口与系统调用 ABI |
| `os_kernel_user_elf_randomized_tests` | 随机 | 16,384 条用户地址范围与溢出性质 |
| `os_kernel_thread_scheduler_unit_tests` | 单元 | Process/Thread 容量、PID/TID、两级回收、WaitQueue、Mutex 与锁边界 |
| `os_kernel_process_tree_unit_tests` | 单元 | PID1、父子关系、Zombie、wait、孤儿收养、Init 回收与统计守恒 |
| `os_kernel_program_arguments_unit_tests` | 单元 | argc/argv/envp、16 字节栈对齐、256 项与 128 KiB 边界及失败原子性 |
| `os_kernel_thread_scheduling_integration_tests` | 集成 | 三 Process/六 Thread 公平 tick、阻塞唤醒、退出与两级回收 |
| `os_kernel_process_lifecycle_integration_tests` | 集成 | 4096 轮父子/孤儿退出、8192 个子进程生命周期与 wait 后空树 |
| `os_kernel_thread_scheduler_randomized_tests` | 随机 | 固定种子 100000 步状态、三 WaitQueue、身份与统计参考模型 |
| `os_kernel_process_models_randomized_tests` | 随机 | 8192 子进程随机回收与 4096 组参数/环境布局参考模型 |
| `os_kernel_pipe_unit_tests` | 单元 | 管道读写、回绕、关闭、EOF、broken pipe 与统计 |
| `os_kernel_pipe_randomized_tests` | 随机 | 32,768 步管道状态与独立字节队列模型对照 |
| `os_kernel_synchronization_integration_tests` | 集成 | 四线程、200,000 次受锁更新的互斥与可见性 |
| `os_kernel_console_input_unit_tests` | 单元 | 控制台 FIFO 顺序、容量、丢弃策略与统计守恒 |
| `os_kernel_file_table_unit_tests` | 单元 | 对象所有权、分块安装、duplicate、fd flags、limit、复用与 close-on-exec |
| `os_kernel_file_description_lifecycle_integration_tests` | 集成 | 真实文件共享/独立偏移、管道最后引用与 finalizer 守恒 |
| `os_kernel_file_table_capacity_integration_tests` | 集成 | 实际填满 4096 fd/64 分块、耗尽失败原子与完整回收 |
| `os_kernel_file_table_randomized_tests` | 随机 | 固定种子十万步 open/duplicate/close/limit 参考模型 |
| `os_kernel_vfs_unit_tests` | 单元 | 路径边界、尾斜杠、mount、getcwd、I/O、容量与循环 |
| `os_kernel_vfs_backend_contract_integration_tests` | 集成 | memfs/legacy 同契约、旧格式重挂载与 memfs 堆归还 |
| `os_kernel_vfs_namespace_randomized_tests` | 随机 | 固定种子十万步独立目录树与文件内容模型 |
| `os_kernel_file_system_format_unit_tests` | 单元 | superblock、inode、目录项显式编码、布局与 CRC32 |
| `os_kernel_file_system_lifecycle_integration_tests` | 集成 | 格式化、目录、跨块文件、重挂载、截断与语义损坏拒绝 |
| `os_kernel_file_system_randomized_tests` | 随机 | 128 轮随机 rewrite、重挂载与参考模型逐字节对照 |
| `os_user_shell_parser_unit_tests` | 单元 | Shell 空白、引号、转义、参数/行长上限与错误原子性 |
| `os_user_shell_parser_randomized_tests` | 随机 | 4096 轮随机字节输入的边界、切片归属与可重复性 |
| `os_user_heap_unit_tests` | 单元 | UserHeap 增长、对齐、split、复用、双向合并、耗尽与非法释放 |
| `os_user_heap_randomized_tests` | 随机 | 固定种子 100000 步 heap 与活动 allocation 字节模型 |
| `os_freestanding_symbol_audit` | 集成 | x86-64 ELF 与零未解析运行时符号 |
| `os_kernel_elf_layout` | 集成 | 真实内核的 ELF64 头、加载段、入口、权限、符号与相邻空闲指令 |
| `os_user_smoke_elf_layout` | 集成 | 正常用户 ELF 的 AMD64、段权限与入口 |
| `os_user_init_elf_layout` | 集成 | 磁盘 PID1 ELF 的结构、权限、入口与无宿主运行时依赖 |
| `os_user_orphan_parent_elf_layout` | 集成 | 创建孤儿的父进程 ELF 布局 |
| `os_user_orphan_child_elf_layout` | 集成 | 被 PID1 收养的子进程 ELF 布局 |
| `os_user_argument_probe_elf_layout` | 集成 | 128 KiB argv/envp 边界探针 ELF 布局 |
| `os_user_exec_probe_elf_layout` | 集成 | exec 失败回滚与成功提交探针 ELF 布局 |
| `os_user_exec_target_elf_layout` | 集成 | exec 新映像目标 ELF 布局 |
| `os_user_fs_probe_elf_layout` | 集成 | 磁盘程序文件系统读写探针 ELF 布局 |
| `os_user_memory_probe_elf_layout` | 集成 | 匿名页、break、栈与 heap 成功路径探针 ELF 布局 |
| `os_user_memory_guard_probe_elf_layout` | 集成 | 永久 stack guard 用户故障探针 ELF 布局 |
| `os_user_memory_protection_probe_elf_layout` | 集成 | 只读匿名页写保护故障探针 ELF 布局 |
| `os_user_invalid_opcode_elf_layout` | 集成 | 用户 `UD2` 测试 ELF 的结构与权限 |
| `os_user_page_fault_elf_layout` | 集成 | 用户越权访问测试 ELF 的结构与权限 |
| `os_user_scheduler_worker_elf_layout` | 集成 | 同址多进程 worker ELF 的结构、权限与入口 |
| `os_user_ipc_producer_elf_layout` | 集成 | 管道生产者 ELF 的结构、权限与入口 |
| `os_user_ipc_consumer_elf_layout` | 集成 | 管道消费者 ELF 的结构、权限与入口 |
| `os_user_shell_elf_layout` | 集成 | 交互式 Shell ELF 的结构、权限、入口与无动态运行时依赖 |
| `os_qemu_hardware_smoke` | 系统 | 自定义空 ROM、空磁盘与 QEMU TCG |
| `os_qemu_rejects_invalid_image_size` | 失败路径 | 错误镜像尺寸必须导致测试失败 |
| `os_firmware_rom_layout` | 集成 | ROM 大小、复位 near jump 与入口字节 |
| `os_stage1_disk_layout` | 集成 | 描述符、LBA、加载范围和负载校验 |
| `os_kernel_disk_layout` | 集成 | 真实启动磁盘的 Kernel 描述符、CRC32、范围与内嵌 ELF |
| `os_kernel_disk_rejects_invalid_header` | 集成/失败路径 | 损坏 Kernel 描述符必须被拒绝 |
| `os_kernel_disk_rejects_invalid_checksum` | 集成/失败路径 | 损坏 Kernel ELF 文件必须被拒绝 |
| `os_kernel_disk_rejects_invalid_elf` | 集成/失败路径 | CRC 正确但 ELF 语义非法仍必须被拒绝 |
| `os_stage1_rejects_invalid_header` | 集成/失败路径 | 损坏描述符必须被宿主审计拒绝 |
| `os_kernel_root_file_system_format_unit_tests` | 单元 | rootfs v2 四类盘面结构、CRC、保留区和边界 |
| `os_kernel_root_file_system_integration_tests` | 集成 | 稀疏、三级间接、完整命名空间、重挂载和设备失败 |
| `os_kernel_root_file_system_capacity_integration_tests` | 容量/集成 | 真实 256 MiB 近满镜像、短写、ENOSPC 与一致性 |
| `os_qemu_bootstrap_smoke` | 系统 | 64 MiB 完整 PID1、VM probe、故障策略与资源守恒 |
| `os_qemu_functional_smoke` | 系统 | 256 MiB 完整 Shell、IPC、文件系统、用户隔离和资源快照路径 |
| `os_qemu_stage1_load_success` | 系统 | 64 GiB 全量页管理、direct-map、高内存读写和完整用户环境 |
| `os_qemu_file_system_persistence` | 系统/失败路径 | 同盘双启动持久化与损坏 superblock 拒绝挂载 |
| `os_qemu_firmware_serial_timeout_failure` | 系统/失败路径 | 有界轮询超时和禁止标记 |
| `os_qemu_firmware_ide_busy_timeout_failure` | 系统/失败路径 | BSY 永久置位必须有界失败 |
| `os_qemu_firmware_ide_error_failure` | 系统/失败路径 | ATA ERR 必须进入设备错误分支 |
| `os_qemu_stage1_header_failure` | 系统/失败路径 | ROM 必须拒绝损坏描述符 |
| `os_qemu_stage1_checksum_failure` | 系统/失败路径 | ROM 必须拒绝损坏负载 |
| `os_qemu_stage1_memory_map_failure` | 系统/失败路径 | Stage 1 内存发现失败不得进入 Kernel |
| `os_qemu_kernel_header_failure` | 系统/失败路径 | Stage 1 必须拒绝损坏 Kernel 描述符 |
| `os_qemu_kernel_checksum_failure` | 系统/失败路径 | Stage 1 必须拒绝损坏 Kernel 文件 |
| `os_qemu_kernel_elf_failure` | 系统/失败路径 | Stage 1 必须拒绝 CRC 正确的非法 ELF |
| `os_qemu_kernel_ata_timeout_failure` | 系统/失败路径 | Kernel 读取采用可容忍 TCG 调度抖动的 `0x000FFFFF` 次预算，并对永久忙设备有界超时 |
| `os_qemu_kernel_ata_error_failure` | 系统/失败路径 | Kernel 读取必须识别 ATA ERR/DF |
| `os_qemu_kernel_invalid_opcode_panic` | 系统/失败路径 | UD2、向量 6、统一帧与 panic |
| `os_qemu_kernel_page_fault_panic` | 系统/失败路径 | 向量 14、错误码、CR2 与 panic |
| `os_qemu_kernel_write_protection_panic` | 系统/失败路径 | CR0.WP、错误码 3、只读页 CR2 与 panic |
| `os_qemu_user_invalid_opcode_isolation` | 系统/失败路径 | Ring 3 #UD 只终止用户并恢复内核 |
| `os_qemu_user_page_fault_isolation` | 系统/失败路径 | Ring 3 #PF、错误码 4、CR2 与内核存活 |
| `os_qemu_user_invalid_elf_rejection` | 系统/失败路径 | 截断用户 ELF 必须在降权前被拒绝 |
| `os_qemu_native_system_call_unsupported` | 系统/失败路径 | 禁用 SYSCALL 后必须输出缺失能力位图且不进入后续架构初始化 |
| `os_python_tooling_unit_tests` | 单元 | 镜像、ELF、ROM、串口协议、代码统计、Kernel 对称功能目录和手机教材导出工具 |
| `os_learning_diagram_geometry_check` | 单元 | 七张系统 SVG 的 marker/8px 安全区，以及三张实体电路 SVG 的显式 pin、net、NC 和导线连通性 |
| `os_cpp_identifier_naming_check` | 集成 | 全部 C++ 头/源文件的变量蛇形、函数大驼峰和单词级小写命名空间 |
| `os_firmware_randomized_tests` | 随机 | 256 组错误复位目标必须被拒绝 |
| `os_stage1_randomized_tests` | 随机 | 256 组有效镜像和 256 组越界 LBA 性质 |
| `os_kernel_randomized_tests` | 随机 | ELF 标识/地址破坏、长度往返、负载与补零破坏 |
| `os_book_source_check` | 集成 | 真实代码统计生成、LaTeX 输入图和主题章教材结构 |

顶层 CTest 数量由当前构建图自动生成，不在文档中冻结为长期常数。v1.8 新增
VMA 单元/随机、UserHeap 单元/随机、用户 VM 生命周期、三个 ELF 审计和
64 MiB bootstrap smoke，并扩展用户边界、QEMU 串口协议与资源快照；v1.7 新增
进程树、程序参数、4096 轮生命周期和固定种子进程模型四项直接测试，并把
reader ELF、rootfs 离线安装与真实 QEMU PID1 协议纳入既有测试；v1.6 新增
rootfs 格式、集成和真实容量三项直接测试，并扩展 VFS、随机、Python 工具与
QEMU 持久化；v1.5 新增 VFS 单元、memfs/legacy 双后端契约集成和
十万步命名空间随机模型三项；v1.4 删除旧固定描述符测试并新增 FileTable
单元、FileDescription 生命周期集成、4096 fd 容量集成和十万步随机模型四项；
v1.3 的处理器 profile、UserContext、CpuLocal/MSR 布局与十万现场随机测试，
以及主动 SYSCALL 缺失路径继续保留；
v1.2 的 Process/Thread 单元、集成和十万步随机测试及
全部历史成功、故障注入、持久化和产物审计用例继续保留。命名门禁使用编译
数据库和 Clang AST 区分标识符种类；
`os_python_tooling_unit_tests` 内的 Kernel 布局测试还会扫描真实源码树，
要求 include/src 拥有相同的十二组模块、根目录没有实现文件、每个公开头文件
具有同模块实现，并通过临时错误树证明扁平文件和缺失实现会被拒绝。它复用现有
Python 测试集合，因此加强结构证据而不虚增顶层 CTest 数量。学习图册另有
独立顶层 CTest，逐个检查系统图几何安全区与实体电路的显式引脚/网络连通性。
Python 词法检查只承担 AST 风格选项无法表达的命名空间单词约束，并在扫描前
屏蔽注释、普通/原始字符串和字符字面量。普通变量必须为小写蛇形，私有/受保护
成员允许尾部下划线，自研 C/汇编函数符号仍使用大驼峰。QEMU、ELF 审计和镜像
工具由 Python 标准库实现。QEMU 捕获器同时拥有“最终里程碑到达”和按内存
规格选择的有界总截止：普通配置为 15 秒，64 GiB 主规格因 Debug 构建需要
扫描 16777216 个页状态而使用 75 秒；外层 CTest 再以 85 秒作为独立保险。
三次启动的持久化用例仍对每次 QEMU 使用 15 秒内部截止，CTest 总预算为
60 秒；十万步 VFS 命名空间模型的硬上限为 180 秒，给正常约两分钟运行保留
宿主调度余量。两者都保持有限上界，不把扩大预算变成无限等待。
捕获器通过 `subprocess` 生命周期管理回收进程，不依赖宿主 Shell 的
`timeout` 或特殊退出码。
正常设备路径额外使用 QMP 的 Unix socket 与 `human-monitor-command/sendkey`
产生键盘前端事件；QMP 仅是测试输入通道，来宾仍完整执行 i8042 和 IRQ1 协议。

成功 QEMU 用例不只检查标记“至少出现一次”。当前路径对 PID1、六个直接
孩子和一个收养孤儿的八次 ELF/进程栈身份，Shell 命令、128 KiB 参数、
两次 exec 回滚、一次 exec 提交、文件探针和单次资源回收执行精确计数；
同时解析固定 16 位十六进制统计，要求创建/回收 Process 与 Thread 均为 8、
PIT 抢占至少为 1、阻塞/唤醒相等且均不为零、控制台提交/读取相等且无丢弃。
PID1 的动态栈 lower/top/upper 各出现一次；进程阶段使累计创建/销毁各增加
八次，聚合峰值至少八栈和三十二映射页，最终活动数为零。资源快照还要求
跟踪字段精确为 26、差异掩码与变化字段数均为零。
内核也独立验证同一组进程、描述符、管道、文件系统、页帧、KVA 和栈管理器
不变量，形成目标内自检与宿主协议检查两层证据。
持久化测试另用同一临时磁盘的两次全新 QEMU 进程，避免把缓存内读回误当作
跨启动持久化。

宿主 C++ 测试使用项目内显式 `TestContext`，不引入 GoogleTest。当前测试规模
不需要 fixture 或宏注册；避免 `TEST`、`EXPECT_*` 等宏也与项目的宏约束一致。
如果以后出现大量共享 fixture、参数化组合或外部报告格式需求，再通过 ADR
重新评估，不提前增加依赖。

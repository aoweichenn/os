# 项目需求

## 目标

通过从 CPU 复位向量开始实现一个 x86-64 教学操作系统，系统学习启动、处理器模式、内存管理、中断、设备、用户态、进程、同步、文件系统和用户环境。

## 固定约束

- 目标指令集为 x86-64。
- 使用 QEMU TCG 模拟硬件，不要求宿主机采用 x86-64 架构。
- QEMU 只提供硬件模型，不替代固件、引导程序或内核。
- 64 MiB、256 MiB 与 64 GiB 分别作为启动兼容、完整功能和主容量配置，
  不要求低内存配置承担高并发压力。
- 64 GiB 是测试规格而不是实现上限；内核容量由 E820、处理器物理地址宽度
  和当前 direct-map 容量共同决定。
- 正式 QEMU CPU 型号与必需 CPUID 特性必须冻结并在启动时检查；v2.0 要求
  long mode、NX、SSE2 与 `SYSCALL/SYSRET`。
- 固件、磁盘加载、模式切换、ELF64 加载和运行时均由项目实现。
- 高级语言使用 freestanding C++20，汇编使用 NASM Intel 语法。
- 宿主自动化使用 Python 3.11+ 标准库，构建图由 CMake 与 Ninja 管理。
- 不使用 SeaBIOS、OVMF、GRUB、Limine、Multiboot 或 QEMU `-kernel`。

## 质量要求

- 每个里程碑必须定义可自动化或可重复执行的验收标准。
- 正常、边界和失败路径必须具有明确的验证方式。
- 纯逻辑模块必须同时具备单元测试、模块集成测试和固定种子随机测试。
- 随机测试失败时必须报告种子、迭代位置和失败性质，确保故障可复现。
- 架构、重要决策、测试方案和复杂故障必须形成文档。
- 构建应当可复现，串口日志和有界 QEMU 生命周期应当支持自动回归。
- 每个源码模块必须隔离公开头文件和私有实现，并由独立 CMake target
  强制单向依赖。
- C++ 变量和参数的语义单词使用下划线分隔；普通函数使用大驼峰；命名空间
  每一层使用一个简短小写单词。完整回归必须通过 Clang AST 与命名空间词法
  门禁。

## v2.0 最终目标

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
  ELF、匿名映射、`MAP_PRIVATE`、只读 `MAP_SHARED`、受控栈增长、写时
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

正式功能矩阵如下；数字是运行时验收下限，不是固定数组长度：

| 配置 | RAM | Process | Thread | 每 Process Thread | fd hard | Pipe |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| bootstrap | 64 MiB | 不规定 | 不规定 | 不规定 | 不规定 | 不规定 |
| functional | 256 MiB | 64 | 128 | 32 | 256 | 128 |
| capacity | 64 GiB | 256 | 512 | 64 | 4096 | 1024 |

capacity 另行验证 64 KiB pipe、1 GiB 稀疏磁盘、256 MiB rootfs、64 MiB
单文件、32 个独立用户 ELF 和 16 级流水线。`argv/envp` 合计 128 KiB 必须
用可回收页暂存，禁止放入大型内核栈缓冲。量化下限、运行频率和逐阶段验收见
[开发路线](roadmap.md)。

## v2.0 非目标

以下内容不进入 v2.0，以避免进程、内存、文件和用户环境主线被硬件广度稀释：

- SMP、多核调度和跨核 TLB shootdown；
- writable `MAP_SHARED`、`msync`、swap、overcommit 和 OOM killer；
- 正/负 dentry cache、数据 journal、快照和在线扩容；
- AVX/XSAVE；
- 网络、图形、音频、USB、AHCI、NVMe 和通用 PCI 设备框架；
- 多用户权限模型、完整 POSIX、动态链接器、共享库和自举编译器。

这些边界不是永久放弃，而是 v2.x/v3.0 的候选输入。v2.0 仍以 QEMU TCG 的
单个 x86-64 BSP 和传统 PC 设备为正式验收平台。

## 当前 v1.1 增量基线

第一周期已完成 `v1.0 用户环境`；当前正在 v1.1 上完成内存分配与资源生命
周期。Stage 1 在自研长模式环境中通过 ATA PIO 读取
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
IDT、汇编桩到 C++ 解码均由目标代码处理。宿主只负责产生外部输入和验证串口
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
pwd、ls、mkdir、write、cat、sync 和 exit。当前完整回归为 92 项 CTest，
覆盖单元、集成、固定种子随机、最终产物审计、真实交互、双启动持久化与历史
失败路径。v1.0 是第一周期 `13 / 13` 的完成基线。v1.1 已经完成动态物理
内存元数据、64 TiB direct-map、64 GiB 管理、4 GiB 以上页帧读写回收，
可释放、可合并并经过十万步模型验证的通用内核堆，以及支持连续块、错阶拒绝
和十万步模型的双位图 buddy。固定尺寸 type cache 也已经建立在通用堆之上：
一个后备块同时保存活动位图和对齐槽，空闲槽组成 LIFO 索引链；缓存必须拒绝
空指针、内部/外部指针、重复释放、活动对象销毁和计数溢出，耗尽时保持输出
不变，销毁后把后备块完整归还通用堆。单元、三缓存集成、十万步固定种子随机
和 64 MiB QEMU 真实写回共同验收该契约。KVA 进一步以 256 个有序区间描述符
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
共同验收该契约。通用作用域回滚、引用计数与资源快照仍是本版本的剩余工作。
v1.1 明确
保留当前四 PCB
用户路径，直到 v1.2 的 Process/Thread 模型
通过对等测试后再迁移删除。v2 路线按
[ADR 0019](adr/0019-v2-executable-program-baseline.md) 划分为 v1.1 至
v1.18，v2.0 只承担集成发布。

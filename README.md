# x86-64 OS Lab

这是一个从 CPU 复位向量开始自研的 x86-64 教学操作系统项目。QEMU 仅用于模拟硬件；固件、引导程序、模式切换、内核、运行时、驱动、用户空间和文件系统均由项目自行实现。

当前状态：`v1.0 用户环境` 已完成，第二周期 `v1.1` 已落地动态物理内存、
可回收内核堆、buddy 页帧分配器和固定尺寸类型缓存四个增量。自研
128 KiB ROM 从 `0xFFFFFFF0` 接管 CPU、初始化 COM1，通过 IDE ATA PIO
读取并校验自研 Stage 1；Stage 1 随后完成 A20、保护模式、64 MiB 身份映射、
长模式切换、Kernel 容器校验、ELF64 装载和 BootInfo 交接，最终进入
freestanding C++20 内核。内核随即替换 Stage 1 的描述符状态，建立自己的
GDT、TSS、IDT、32 个异常入口和无动态分配的 panic 路径。Stage 1 还通过
QEMU PC 的 `fw_cfg` 硬件接口读取 `etc/e820`，自行规范化为 BootInfo v2；
内核读取 `CPUID.80000008H` 与 E820，按实际可用 RAM 动态放置 2-bit 页帧
元数据，并建立从 `0xFFFF888000000000` 开始、容量 64 TiB 的高半区物理
直映窗口。直映内部优先使用 2 MiB 页，边界退回 4 KiB 页；Stage 1 的低
64 MiB 身份映射只负责启动，不再限制正式页帧管理。主 QEMU 规格为 64 GiB，
最小兼容规格仍为 64 MiB；64 GiB 启动必须在 4 GiB 以上分配、写回并回收
页帧。内核同时建立 W^X/NX/WP 权限、guard page 和 64 KiB 高半区内核堆，
并真实切换 CR3。该堆现已支持 best-fit、二次幂对齐、释放、前后合并、非法
释放检测、完整一致性检查和生命周期统计；QEMU 启动自检完成真实写回后会
释放全部对象并确认活动数归零。固定尺寸类型缓存在该堆上用一次后备申请同时
保存活动位图和对齐槽位，空闲槽内保存 LIFO 索引链；申请/释放为常数时间，
重复释放、内部指针和活动对象销毁都会明确失败。目标自检把 32 个 64 字节
对齐对象完整耗尽、写回、交错释放并复用，最终 33 次申请与释放守恒，缓存
销毁后堆恢复进入前基线。在此基础上，内核严格
验证并装入自研 `ET_EXEC` 用户 ELF64。内核为四个进程分别建立 PML4、
同址用户代码/数据、四页用户栈、16 KiB Ring 0 栈和保护页；8254 PIT
每四个 tick 触发一次单核 round-robin 决策，切换 CR3、TSS.RSP0 和完整
176 字节用户现场。`INT 0x80` 提供日志、退出和 PID 查询；进程退出或用户
异常会释放其用户页与页表，Ring 0 故障仍进入 panic。v0.10 又把 PCB
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
freestanding C++20 解析器提供 help、echo、pwd、ls、mkdir、write、cat、
sync 和 exit。QEMU 系统测试在 Shell READY 后逐字产生十条命令，来宾自行
完成 i8042、IRQ、解码、排队、唤醒、文件操作与退出；完整回归共 83 项
CTest，其中 Clang AST 与 Python 词法门禁会拒绝不符合约定的变量、函数和
命名空间。

第二周期已经按可独立验收的依赖闭环优化为 v1.1–v1.18。v1.1 的完整范围是
buddy、kernel heap/type cache、KVA、动态内核栈和页表回收，并保留当前
四进程通路；其中动态物理内存、通用可回收 kernel heap 和双位图 buddy 已
完成，固定尺寸 type cache 也已通过十万步随机模型和 QEMU 真实生命周期
验收；KVA、动态内核栈和页表回收继续按独立闭环推进；
v1.2 再迁移到 Process/Thread、统一 WaitQueue/WakeReason 和完整 FXSAVE
现场，v1.3 独立建立 CpuLocal 与 `SYSCALL/SYSRET`。VFS、rootfs v2、
PID1/磁盘 exec 分三个版本完成；匿名 VMA、文件页缓存、fork/COW 与 Unix I/O
也分别验收。用户线程、时间、信号和 TTY 不再塞进同一阶段，异步块层与 ordered
metadata journal 同样分开，最后由 v1.18 冻结 ABI、加固边界并建立发布溯源。
v2.0 只集成已经冻结的机制，收敛为从自研文件系统启动 `/sbin/init` 与外部
Shell 的单 BSP、多进程、多线程类 Unix 教学系统。64 MiB、256 MiB 和 64 GiB
分别承担启动兼容、完整功能和容量压力。详细阶段见
[docs/roadmap.md](docs/roadmap.md)，执行模型、语义边界和取舍见
[ADR 0019](docs/adr/0019-v2-executable-program-baseline.md)。

## 最短构建与测试路径

在 Linux 环境安装 Python 3.11+、Clang、Clang-Tidy、LLD、NASM、QEMU、
GDB、CMake 和 Ninja 后执行：

```bash
python3 tools/os.py verify
```

该命令会完成工具链检查、宿主机测试构建、x86-64 freestanding
交叉编译、自研 ROM 生成与审计、单元测试、集成测试、固定种子随机测试和
QEMU TCG 整机测试。详细说明见 [docs/building.md](docs/building.md) 和
[docs/testing.md](docs/testing.md)。

固件成功日志：

```text
[OS][FIRMWARE] RESET
[OS][FIRMWARE] SERIAL_READY
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
[OS][KERNEL] GDT_READY
[OS][KERNEL] TSS_READY
[OS][KERNEL] IDT_READY
[OS][KERNEL] DESCRIPTOR_TABLES_VALID
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
[OS][KERNEL] DIRECT_MAP_BASE=0xFFFF888000000000
[OS][KERNEL] DIRECT_MAP_MAPPED_BYTES=0x...
[OS][KERNEL] DIRECT_MAP_2M_PAGES=0x...
[OS][KERNEL] DIRECT_MAP_4K_PAGES=0x...
[OS][KERNEL] HIGH_MEMORY_TEST_ADDRESS=0x...
[OS][KERNEL] HIGH_MEMORY_VALIDATION_COMPLETE
[OS][KERNEL] MEMORY_PERMISSIONS_VALID
[OS][KERNEL] HEAP_READY
[OS][KERNEL] HEAP_CAPACITY_BYTES=0x0000000000010000
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
[OS][KERNEL] FILE_SYSTEM_FORMATTED
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
[OS][USER][SHELL] COMMAND=CAT
[OS][USER][SHELL] COMMAND=LS
[OS][USER][SHELL] COMMAND=SYNC
[OS][USER][SHELL] UNKNOWN_COMMAND_REJECTED
[OS][USER][SHELL] COMMAND=EXIT
[OS][USER][SHELL] EXIT
[OS][KERNEL] SCHEDULER_CREATED_PROCESSES=0x0000000000000004
[OS][KERNEL] SCHEDULER_TERMINATED_PROCESSES=0x0000000000000004
[OS][KERNEL] SCHEDULER_PREEMPTIONS=0x...
[OS][KERNEL] SCHEDULER_BLOCKS=0x...
[OS][KERNEL] SCHEDULER_WAKEUPS=0x...
[OS][KERNEL] PIPE_CAPACITY_BYTES=0x0000000000000040
[OS][KERNEL] PIPE_WRITTEN_BYTES=0x0000000000000100
[OS][KERNEL] PIPE_READ_BYTES=0x0000000000000100
[OS][KERNEL] PIPE_EOF_OBSERVATIONS=0x0000000000000001
[OS][KERNEL] CONSOLE_SUBMITTED_BYTES=0x...
[OS][KERNEL] CONSOLE_READ_BYTES=0x...
[OS][KERNEL] CONSOLE_DROPPED_BYTES=0x0000000000000000
[OS][KERNEL] CONSOLE_BUFFERED_BYTES=0x0000000000000000
[OS][KERNEL] USER_EXIT_CODE=0x0000000000000000
[OS][KERNEL] PIPE_TRANSFER_VALID
[OS][KERNEL] PIPE_ENDPOINTS_CLOSED
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

`build/developer/source/kernel/kernel.elf` 由 LLD 直接链接，入口固定为
`0x00100000`。当前产物包含严格分权的 `R E`、`R`、`RW/BSS` 三个
`PT_LOAD`；Stage 1 在目标机上以两遍算法先验证全部段，再复制文件内容并清零
BSS。成功交接后内核重新初始化 COM1，验证 104 字节 BootInfo v2、BSS 和
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
tests/           单元、集成、随机和 QEMU 系统测试
tools/           Python 构建、检查、镜像和 QEMU 调度工具
docs/            需求、架构、模块、测试、调试和发布记录
books/           可独立构建的 LaTeX 系统教材
```

`source/abi` 保存用户态与内核共享的固定宽度 ABI，`source/user` 保存独立
用户 ELF 和系统调用包装；详细边界见
[docs/modules/user.md](docs/modules/user.md)。

完整教材入口见
[books/x86-64-os-from-reset/README.md](books/x86-64-os-from-reset/README.md)。
教材采用 5 部 10 个完整主题章；每章按“背景与历史约束、硬件或软件
状态、实现机制、失败路径、验证证据”的统一深度展开。构建时会自动统计仅进入
目标系统的 `.cpp`、`.hpp` 和 `.asm` 真实代码量。
可单独执行 `python3 tools/os.py source-metrics` 查看同一口径。
执行 `make -C books/x86-64-os-from-reset phone-export` 可按硬件教材相同规则
导出到手机书库的独立目录。

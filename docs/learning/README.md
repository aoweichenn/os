# 从零到 v1.0：分阶段学习指南

## 1. 这套文档解决什么问题

本目录以提交 `65b0e95` 的 v1.0 第一周期闭环为学习基线，并逐项对应生产
实现。当前 `main` 已推进到 v1.16：

- v1.1 建立可回收资源生命周期、动态物理内存、buddy、类型缓存、KVA、
  动态双 guard 内核栈和页表空分支回收；
- v1.2 把旧 PCB 调度器拆成 Process/Thread/WaitQueue，并保存完整
  x87/MMX/SSE/SSE2 扩展现场；
- v1.3 增加处理器能力契约、CpuLocal、`SYSCALL/SYSRET` 与安全返回；
- v1.4 增加类型化 KernelObject、共享 FileDescription 和动态 FileTable；
- v1.5 增加 VFS、挂载命名空间、每 Process FsContext、memfs 与 legacy
  文件系统适配；
- v1.6 增加 256 MiB rootfs v2、三级间接树、完整命名空间修改、独立
  mkfs/fsck 与严格损坏拒绝。
- v1.7 把普通用户程序移入 rootfs，从磁盘启动 PID1，并加入父子进程树、
  spawn/exec/wait、argc/argv/envp、孤儿收养和 Zombie 回收。
- v1.8 增加有序 VMA、匿名按需分页、`mmap/munmap/brk`、8 MiB 受控栈增长
  与自研 Ring 3 用户 heap。
- v1.9 增加文件后备 VMA、按需 ELF、有界 clean page cache、只读 shared、
  可写 private 与 write/truncate 失效。
- v1.10 增加只复制调用 Thread 的 fork、匿名/private 页 COW、统一
  `#PF`/`CopyToUser` 私有化以及 fd/cwd/后备继承。
- v1.11 增加 64 KiB 按需动态管道、`pipe/dup2`、外部命令 Shell、重定向、
  16 级流水线以及 rootfs `/bin` 核心工具。
- v1.12 增加用户 Thread、FS-base TLS、private futex、用户同步原语与
  32/64 Thread 整机容量验收。
- v1.13 增加基于 PIT 实际除数的单调纳秒、统一 deadline queue，以及
  sleep、private futex 和 ConditionVariable 的绝对截止期等待。
- v1.14 增加进程级信号处置、Thread 屏蔽字、普通信号合并、进程组投递、
  可中断/可重启阻塞，以及可验证的用户 signal frame 与 `sigreturn`。
- v1.15 增加 TTY canonical 行规程、session、控制终端、前台进程组、
  stop/continue wait 事件、`/dev/console` 和 Shell 前后台作业控制。
- v1.16 增加 BlockRequest FIFO、ATA IRQ14 单赢家完成与超时 reset、
  BlockIo 等待、可写 shared 映射和 dirty/writeback/error 文件页缓存。

第一周期文档仍按机制首次出现的顺序教学；涉及已替换实现时，会明确标记
“v1.0 历史模型”和“v1.16 当前模型”。当前阶段的权威验收分别见
[v1.1](../releases/v1.1.md)、[v1.2](../releases/v1.2.md)、
[v1.3](../releases/v1.3.md)、[v1.4](../releases/v1.4.md)、
[v1.5](../releases/v1.5.md)、[v1.6](../releases/v1.6.md)、
[v1.7](../releases/v1.7.md)、[v1.8](../releases/v1.8.md) 和
[v1.9](../releases/v1.9.md)、
[v1.10](../releases/v1.10.md) 与
[v1.11](../releases/v1.11.md) 与
[v1.12](../releases/v1.12.md) 与
[v1.13](../releases/v1.13.md) 与
[v1.14](../releases/v1.14.md) 与
[v1.15](../releases/v1.15.md) 与
[v1.16](../releases/v1.16.md) 发布记录。
整套路线不把项目讲成一组互不相关的源文件，而是沿 CPU 真正执行的因果链展开：

```text
电源、时钟与复位
  → x86 复位向量和自研 ROM
  → 端口 I/O、串口和 ATA PIO
  → Stage 1、A20、GDT 和 Long Mode
  → ELF64 Kernel 与 BootInfo
  → Kernel GDT/IDT/TSS、异常和 panic
  → E820、物理页、四级页表和堆
  → PIC/LAPIC/PIT/PS/2/ATA
  → Ring 3、用户 ELF 和系统调用
  → 独立地址空间、抢占调度和进程生命周期
  → 同步、阻塞/唤醒和管道 IPC
  → inode 文件系统、缓存和持久化
  → v1.0 固定描述符、控制台和交互式 Shell
  → v1.1 可回收资源生命周期
  → v1.2 Process/Thread、统一等待与扩展现场
  → v1.3 CpuLocal 与原生系统调用
  → v1.4 KernelObject、共享打开实例与动态 FileTable
  → v1.5 VFS、Mount、每 Process cwd 与双文件系统后端
  → v1.6 rootfs v2、完整命名空间与独立 fsck
  → v1.7 磁盘 PID1、进程树、spawn/exec/wait 与参数环境
  → v1.8 匿名 VMA、按需分页、栈增长与用户 heap
  → v1.9 文件 VMA、按需 ELF 与 clean page cache
  → v1.10 fork、private COW 与资源继承
  → v1.11 动态管道、外部命令、重定向与 16 级流水线
  → v1.12 用户 Thread、FS-base TLS 与 private futex
  → v1.13 单调时间、统一 deadline 与 timed wait
  → v1.14 信号处置、进程组、可中断等待与 sigreturn
  → v1.15 TTY、session、控制终端与前后台作业
  → v1.16 IRQ14 块请求、BlockIo 与 shared page writeback
```

目标读者可以只了解普通 C++，不必预先掌握操作系统、汇编或 PC 硬件。前置篇会
先建立必要的机器模型；后续每一阶段都回答六类问题：

1. 为什么需要这个机制，它弥补了上一阶段的什么缺口？
2. CPU、芯片组或设备向软件提供了什么硬件契约？
3. 项目选择了什么数据结构、地址布局、ABI 和所有权模型？
4. 控制流在汇编、C++、设备和用户态之间如何移动？
5. 正常路径、边界条件和失败路径分别怎样处理？
6. 如何通过构建产物、宿主测试、QEMU 日志和 GDB 证明理解正确？

本文档解释 v1.0 基线代码，并对当前主线的关键差异做显式注记；它不替代规范
文档。需求以
[requirements.md](../requirements.md) 为准，当前架构以
[architecture.md](../architecture.md) 为准，版本范围以
[roadmap.md](../roadmap.md) 为准，历史验收证据保存在
[releases](../releases/)。

## 2. 开始前应知道的项目边界

这个项目有意选择一条严格但清晰的技术路线：

- QEMU TCG 只模拟硬件，不替项目提供固件、引导器或内核入口。
- 不使用 SeaBIOS、OVMF、GRUB、Limine、Multiboot 或 QEMU `-kernel`。
- 固件、Stage 1、模式切换、ELF 装载、内核、驱动和用户空间全部自行实现。
- 高级语言只使用 freestanding C++20；不可避免的架构入口使用 NASM Intel
  语法。
- 目标程序不链接 libc、libstdc++、libc++ 或宿主运行时。
- Clang/LLD/NASM 生成目标产物；CMake/Ninja 组织构建；Python 标准库负责
  宿主工具；QEMU/GDB 负责整机运行和调试。
- v1.0 是第一周期的完整垂直切片，不等同于完整 Unix 或 Linux。

## 3. 背景知识专题

如果“知道名词，但还解释不清为什么会这样设计”，先进入
[背景知识专题入口](background/README.md)。七册专题分别讲：

1. 计算机、PC 平台与 x86 从 8086 到 AMD64 的兼容历史。
2. 位、补码、溢出、字节序、地址域、NASM、栈和 ABI。
3. 编译、重定位、链接脚本、ELF、ROM、磁盘镜像和 freestanding C++。
4. E820、页帧、分页、TLB、Ring、GDT/TSS/IDT 与异常。
5. Polling、IRQ、PIC/LAPIC、PIT、UART、PS/2、ATA 与 idle。
6. 进程、系统调用、调度、同步、管道、描述符、文件系统和 Shell。
7. 单元、随机、产物、QEMU、跨启动测试与 GDB 分层诊断。

背景专题按概念组织，下面的版本文档按项目实现顺序组织。每个版本页顶部都给出
对应背景先修；遇到不熟悉的概念时先跳到专题，再回到具体常量和源码。

## 4. 先看整机组装与连线图册

开始逐版本阅读前，先打开
[从 v0.0 到 v1.0：整机硬件组装与连线图册](hardware-assembly-and-wiring.md)。
它用一张低密度总览和六张可缩放专题 SVG，分别画出：

- ROM、CPU、RAM、MMU 和 Local APIC 的内存/MMIO 连接。
- COM1、PIC、PIT、PS/2、ATA、fw_cfg 的 Port I/O 连接。
- PIT IRQ0 与键盘 IRQ1 经 PIC、LAPIC 到 CPU IDT 的中断路径。
- Firmware、Stage 1、Kernel 和 Ring 3 的硬件所有权交接。
- 按键到 Shell、文件写入到 raw disk 的两条端到端数据流。

先建立这套“整机地图”，后面的阶段文档就是逐段放大每一根线，而不是重新认识
一台机器。

如果想把 QEMU 的硬件契约与现实 PCB 对照，再进入
[从虚拟 PC 到实体 x86-64：N100 载板电路详解](physical-carrier-circuit-guide.md)。
该章收录十页真实 KiCad 原理图，并提供电源、高速接口和低速控制三张器件级
学习 SVG。实体载板使用计算模组、UEFI、PCIe/USB/NVMe 等现实平台契约，
不能和当前自研 ROM + QEMU `pc` 机器直接互换。

## 5. 学习顺序

按顺序完成下表。后续阶段默认已经掌握前一阶段的契约，不重复从头解释。

| 顺序 | 文档 | 完成后能够解释 |
| ---: | --- | --- |
| 0 | [硬件、二进制与工具链前置](00-prerequisites-and-hardware.md) | CPU、地址、端口、特权级、分页、中断、PC 设备和 freestanding 构建 |
| 1 | [v0.0：工程基线](01-v0.0-engineering-baseline.md) | 为什么写第一条指令前先建立构建、测试、审计和文档闭环 |
| 2 | [v0.1：复位与串口](02-v0.1-reset-and-serial.md) | CPU 如何从 `0xFFFFFFF0` 进入自研 ROM 并输出第一行日志 |
| 3 | [v0.2：固件加载 Stage 1](03-v0.2-firmware-stage1-loader.md) | ROM 如何不用 BIOS，直接通过 ATA PIO 验证并加载磁盘程序 |
| 4 | [v0.3：进入 Long Mode](04-v0.3-long-mode.md) | A20、GDT、控制寄存器和临时页表如何共同完成 16→32→64 位切换 |
| 5 | [v0.4：ELF64 Kernel 与 BootInfo](05-v0.4-kernel-elf-and-boot-info.md) | Stage 1 如何安全装载内核并建立显式交接 ABI |
| 6 | [v0.5：描述符表、异常与 panic](06-v0.5-descriptor-tables-and-exceptions.md) | 内核如何接管 GDT/IDT/TSS，规范化异常现场并区分恢复与崩溃 |
| 7 | [v0.6：内存管理](07-v0.6-memory-management.md) | E820、页帧分配、四级页表、NX/WP、guard page 和早期堆 |
| 8 | [v0.7：中断与设备](08-v0.7-interrupts-and-devices.md) | PIC→LAPIC→CPU 路由，以及 PIT、PS/2、ATA 的真实驱动路径 |
| 9 | [v0.8：Ring 3 与系统调用](09-v0.8-ring3-and-system-calls.md) | 用户 ELF、U/S 页权限、IRETQ 降权、INT 0x80 和用户指针验证 |
| 10 | [v0.9：进程与抢占调度](10-v0.9-processes-and-scheduling.md) | PCB、独立 CR3、内核栈、PIT 抢占、上下文切换和资源回收 |
| 11 | [v0.10：同步、阻塞与管道](11-v0.10-synchronization-and-ipc.md) | 原子锁、等待条件、丢失唤醒、背压、EOF 和 broken pipe |
| 12 | [v0.11：文件系统](12-v0.11-file-system.md) | 磁盘格式、inode、位图、块缓存、事务状态和跨启动持久性 |
| 13 | [v1.0：用户环境与 Shell](13-v1.0-user-environment.md) | 统一描述符、控制台 FIFO、idle、交互式 Ring 3 Shell 和最终闭环 |
| 14 | [v1.6：rootfs v2](14-v1.6-rootfs-v2.md) | 256 MiB 盘面、三级间接树、稀疏文件、rename/unlink、事务与 fsck |
| 15 | [v1.7：PID1、进程树与磁盘 exec](15-v1.7-pid1-process-tree-exec.md) | 磁盘程序、argc/argv/envp、父子关系、孤儿收养、Zombie、spawn/exec/wait 与回滚 |
| 16 | [v1.8：匿名 VMA 与按需分页](16-v1.8-anonymous-vma-demand-paging.md) | VMA/PTE 分工、x86-64 `#PF`、匿名页、`brk`、受控栈、用户 heap 与资源回收 |
| 17 | [v1.9：文件页与按需 ELF](17-v1.9-file-backed-vma-lazy-elf-page-cache.md) | 稳定文件身份、FileBacking、clean cache、shared/private、尾零、失效与回收 |
| 18 | [v1.10：fork 与写时复制](18-v1.10-fork-copy-on-write.md) | fork 返回、PTE 软件位、稀疏引用、COW fault、CopyToUser、资源继承与回滚 |
| 19 | [v1.11：Unix I/O 与外部 Shell](19-v1.11-unix-io-external-shell.md) | 动态管道、pipe/dup2、解析与执行分离、重定向、16 级流水线与完整回收 |
| 20 | [v1.12：用户 Thread、TLS 与 futex](20-v1.12-user-threads-tls-private-futex.md) | Thread 生命周期、FS-base、AddressSpaceId、compare-and-block、同步原语、取消与 Join |
| 21 | [v1.13：单调时间、deadline 与 timed wait](21-v1.13-monotonic-clock-deadline-timed-wait.md) | PIT 有理数累计、绝对截止期、稳定队列、单赢家唤醒、sleep/futex/condition 超时与整机证据 |
| 22 | [v1.14：进程信号、用户 frame 与 sigreturn](22-v1.14-process-signals-sigreturn.md) | 信号历史、处置/屏蔽/pending 三层状态、进程组、可中断等待、用户 frame、返回验证与隔离 |
| 23 | [v1.15：TTY、session 与作业控制](23-v1.15-tty-session-job-control.md) | 终端历史、行规、控制字符、SID/PGID、前台所有权、停止/继续事件、`/dev/console` 与 Shell 作业表 |
| 24 | [v1.16：IRQ14 块请求与共享页写回](24-v1.16-irq14-block-request-writeback.md) | ATA/PIC 标志、单飞请求、IRQ/超时单赢家、BlockIo、write-notify、dirty/writeback/error 与稳定落盘 |

### 5.1 从第一周期过渡到当前 v1.16

完成上表后，不要把 v1.0 类型名直接套到当前源码。按下面顺序阅读第二周期：

| 当前阶段 | 替换或新增的关键模型 | 当前实现入口 |
| --- | --- | --- |
| [v1.1](../releases/v1.1.md) | 一次性分配 → 显式回收、回滚与跨层资源快照 | `source/foundation/reference_counter.*`、`scope_rollback.*`、Kernel `memory/*` |
| [v1.2](../releases/v1.2.md) | PCB/ProcessScheduler → Process、Thread、ThreadScheduler、WaitQueue | Kernel `process/*`、`arch/extended_state.*` |
| [v1.3](../releases/v1.3.md) | 仅 `INT 0x80` → CpuLocal + `SYSCALL/SYSRET`，保留兼容入口 | Kernel `arch/cpu_local.*`、`native_system_call.*`、`user/system_calls.*` |
| [v1.4](../releases/v1.4.md) | 固定 8 槽 IoDescriptorTable → KernelObject、FileDescription、动态 FileTable | Kernel `object/*`、`io/file_description.*`、`io/file_table.*` |
| [v1.5](../releases/v1.5.md) | legacy 完整路径/handle → Vnode、Path、Mount、FsContext、memfs/legacy 双后端 | Kernel `fs/vfs.*`、`fs/memfs.*`、`fs/legacy_file_system.*` |
| [v1.6](../releases/v1.6.md) | legacy 生产根 → 严格 rootfs v2、三级间接树、完整 namespace mutation 与独立 fsck | Kernel `fs/root_file_system*`、`tools/os_tools/rootfs_v2.py` |
| [v1.7](../releases/v1.7.md) | 内嵌固定程序 → 磁盘 PID1、父子进程树、spawn/exec/wait 与参数环境 | Kernel `process/process_tree.*`、`program_arguments.*`、`process_runtime.*` |
| [v1.8](../releases/v1.8.md) | PTE 即全部地址语义 → VMA 意图、匿名按需页、受控栈与用户 heap | Kernel `memory/virtual_memory_area.*`、`user/user_memory.*`，User `user_heap.*` |
| [v1.9](../releases/v1.9.md) | eager ELF/独占文件页 → FileBacking、按需 ELF 与共享 clean cache | Kernel `user/file_backing.*`、`memory/file_page_cache.*`、`user/user_memory.*` |
| [v1.10](../releases/v1.10.md) | eager process copy/无 fork → 调用 Thread clone、private COW 与两阶段失败回滚 | Kernel `memory/user_page_reference.*`、`user/user_memory.*`、`process/process_runtime.*` |
| [v1.11](../releases/v1.11.md) | 固定启动管道/内建 Shell → 动态管道、pipe/dup2、外部 `/bin`、重定向与 16 级流水线 | Kernel `ipc/pipe_manager.*`、`io/file_table.*`、User `shell_execution.*`、`core_tool.cpp` |
| [v1.12](../releases/v1.12.md) | 每 Process 单用户执行流 → 用户 Thread、FS-base TLS、private futex 与同步原语 | ABI `thread.hpp`、Kernel `sync/private_futex.*`、`process/process_runtime.*`、User `thread.*`、`synchronization.*` |
| [v1.13](../releases/v1.13.md) | 只有调度 tick → 单调纳秒、统一 deadline 与绝对时间等待 | ABI `time.hpp`、Kernel `time/*`、`process/thread_scheduler.*`、User `system_call.*`、`synchronization.*` |
| [v1.14](../releases/v1.14.md) | 只能同步等待子进程 → 异步信号、进程组、可中断等待与受控用户返回 | ABI `signal.hpp`、Kernel `process/signal_manager.*`、`process/process_runtime.*`、User `system_call.*`、`signal_probe.cpp` |
| [v1.15](../releases/v1.15.md) | 只有进程组信号 → TTY 前台所有权、session、停止/继续事件与 Shell 作业控制 | ABI `terminal.hpp`、Kernel `io/terminal.*`、`process/job_control.*`、`fs/console_device_file_system.*`、User `shell_execution.*` |
| [v1.16](../releases/v1.16.md) | 同步轮询/clean-only cache → IRQ14 BlockRequest、BlockIo 与 dirty/writeback/error shared page | Kernel `device/block_request.*`、`device/ata_pio.*`、`memory/file_page_cache.*`、`user/user_memory.*` |

二十四个阶段的架构结论已合并到
[architecture.md](../architecture.md)，当前 Kernel 的功能目录见
[source/kernel/README.md](../../source/kernel/README.md)。第一周期章节负责解释
机制为什么出现；发布记录和当前源码负责解释它后来怎样演化。

## 6. 每个阶段怎样学习

不要只顺序阅读正文。推荐使用同一套四遍方法：

### 第一遍：只建立边界

先读该阶段的“上一阶段缺口”“硬件契约”和“完成标准”，暂时不要钻入每个位。
目标是知道本阶段为什么存在，以及什么工作明确不属于本阶段。

### 第二遍：沿控制流读代码

按照文档给出的“源码阅读顺序”，从外部入口开始跟到最深处。每进入一个函数，
记录：

- 当前 CPU 模式和 CPL。
- 当前使用哪一套页表和栈。
- 参数通过寄存器、内存结构还是设备寄存器传递。
- 谁拥有正在访问的内存或设备状态。
- 失败后控制权返回哪里，还是进入不可返回路径。

### 第三遍：画布局和状态机

至少手画三类图：

- ROM、磁盘、物理内存和虚拟地址布局。
- CPU 模式、进程状态、管道状态或文件系统事务状态的转换图。
- 汇编入口形成的栈帧。

如果不能不看文档重画，就还没有掌握对应机制。

### 第四遍：用证据验证

先运行宿主测试，再看产物审计，最后运行 QEMU。修改学习实验时一次只改变一个
变量，并为预期失败设置明确日志或测试断言。不要把“QEMU 没退出”当作成功。

## 7. 从构建到运行的最短操作

工具检查：

```bash
python3 tools/os.py doctor
```

配置和构建：

```bash
cmake --preset developer
cmake --build --preset developer
```

完整验证：

```bash
python3 tools/os.py verify
```

只查看测试清单或按标签运行：

```bash
ctest --preset developer -N
ctest --preset developer -L unit --output-on-failure
ctest --preset developer -L integration --output-on-failure
ctest --preset developer -L randomized --output-on-failure
ctest --preset developer -L system --output-on-failure
```

构建、运行、测试和 GDB 的完整命令分别见
[building.md](../building.md)、[testing.md](../testing.md) 和
[debugging.md](../debugging.md)。

## 8. 最终启动主线

学习各阶段时始终把局部机制放回下面这条最终主线：

```text
reset vector
  source/firmware/src/reset_and_serial.asm
    ├─ 初始化段、栈、COM1、PIT
    ├─ 读取并验证 Stage 1 描述符与负载
    └─ far return 到 0000:8000

Stage 1
  source/boot/stage1/src/entry.asm
    ├─ A20
    ├─ GDT 与保护模式
    ├─ 临时 PML4/PDPT/PD 与 Long Mode
    ├─ fw_cfg E820
    └─ source/boot/stage1/src/kernel_loader.asm
         ├─ ATA 读取 Kernel 容器
         ├─ CRC32 与 ELF64 两遍验证
         ├─ 装载 PT_LOAD、清零 BSS
         └─ BootInfo(RDI) + CALL kernel entry

Kernel
  source/kernel/src/boot/entry.cpp
    └─ source/kernel/src/core/kernel_main.cpp::RunKernel
         ├─ BootInfo、BSS、CR3 验证
         ├─ GDT/TSS/IDT、扩展现场与处理器能力契约
         ├─ 物理内存、内核页表、堆与资源快照
         ├─ Process/Thread 运行时与用户 ELF
         ├─ LAPIC/PIC/PIT/PS2/ATA
         ├─ 单调纳秒、deadline queue 与 timed wait
         ├─ 文件系统 mount/format
         ├─ INT 0x80 和 SYSCALL 两条 Ring 3 入口
         ├─ KernelObject/FileDescription/FileTable
         ├─ 从 rootfs 读取 /sbin/init，注册 PID1
         ├─ spawn/exec/wait、进程树与孤儿收养
         ├─ Ring 3 抢占执行与安全返回
         ├─ 文件持久性与资源守恒检查
         └─ READY + HLT event loop

Ring 3
  source/user/programs/*.cpp
    ├─ INT 0x80 兼容入口与原生 SYSCALL
    ├─ fd 0/1/2 与动态 FileTable
    ├─ PID1 组织父子生命周期并回收 Zombie
    ├─ argc/argv/envp 与磁盘 ELF exec
    ├─ filesystem probes
    ├─ thread/time probes
    └─ interactive shell 与 rootfs 外部命令
```

## 9. 生产源码覆盖地图

下表给出每组生产源码第一次被完整讲解的阶段。一个文件可能在后续版本继续扩展；
后续文档会只解释新增职责和跨模块交互。

| 源码组 | 首次重点阶段 | 主要职责 |
| --- | --- | --- |
| `source/foundation/**` | v0.0 | 地址范围、溢出安全与宿主/目标复用 |
| 根 `CMakeLists.txt`、`CMakePresets.json`、`tools/os.py`、`tools/os_tools/**` | v0.0 | 工具链、构建编排、产物审计和 QEMU 自动化 |
| `source/firmware/linker/rom.ld` | v0.1 | 128 KiB ROM VMA/LMA 与复位向量布局 |
| `source/firmware/src/reset_and_serial.asm` | v0.1、v0.2 | 复位、COM1、PIT、ATA PIO 和 Stage 1 跳转 |
| `source/boot/stage1/src/entry.asm` | v0.2、v0.3 | Stage 1 入口、A20、GDT、模式切换和临时页表 |
| `source/boot/stage1/src/kernel_loader.asm`、`include/kernel_loader.inc` | v0.4 | Kernel 描述符、CRC32、ELF64 和 BootInfo |
| `source/boot/stage1/src/memory_map.asm` | v0.6 | fw_cfg 目录、E820 转换、排序和边界验证 |
| `source/kernel/linker/kernel.ld.in` | v0.4 | Kernel ELF 入口、三个 PT_LOAD 与 W^X |
| `boot/boot_info.*`、`boot/entry.*`、`device/serial_port.*`、`core/freestanding_memory.*` | v0.4 | 首次 C++ Kernel 交接和最小运行时 |
| `arch/descriptor_layout.*`、`descriptor_tables.*`、`exception_frame.*` | v0.5 | GDT/TSS/IDT 编码与异常现场 |
| `arch/architecture.asm`、`exceptions.*`、`panic.*` | v0.5 起 | 异常/IRQ/系统调用/调度汇编边界 |
| `memory/physical_memory_map.*`、`physical_frame_allocator.*` | v0.6 | E820 规范化与页帧所有权 |
| `memory/page_table.*`、`page_table_layout.cpp`、`memory_manager.*` | v0.6 | 四级页表、内核映射与 CR3 切换 |
| `memory/kernel_heap.*`、`arch/processor.*` | v0.6 起 | 早期分配与处理器控制 |
| `device/port_io.*`、`legacy_pic.*`、`programmable_interval_timer.*` | v0.7 | 端口访问、PIC 和 PIT |
| `arch/interrupt_runtime.*`、`device/device_model.*` | v0.7 | IRQ 分发、统计和可测试设备模型 |
| `device/ps2_keyboard.*`、`ata_pio.*` | v0.7 起 | 键盘与磁盘驱动 |
| `source/abi/**` | v0.8 起 | Kernel/User 共享系统调用 ABI |
| `user/user_elf.*`、`user_memory.*`、`user_program_images.*` | v0.8 | 用户 ELF 验证、用户页和嵌入镜像 |
| `user/system_calls.*`、`source/user/src/system_call.*` | v0.8 起 | `INT 0x80`/`SYSCALL` 两侧入口和派发 |
| `source/user/linker/user.ld.in`、`programs/smoke.cpp`、故障程序 | v0.8 | 用户 ELF、IRETQ 验收和异常隔离 |
| v1.0 历史 `process_memory_layout.*`/`process_scheduler.*`；当前 `memory/kernel_stack_manager.*`、`process/thread_scheduler.*`、`process_runtime.*` | v0.9、v1.2 | 独立地址空间、Process/Thread、调度、动态 Ring 0 栈和生命周期 |
| `programs/scheduler_worker.cpp` | v0.9 | PIT 抢占和公平性工作负载 |
| `sync/spin_lock.*`、`ipc/pipe.*`、`programs/ipc_*.cpp` | v0.10 | 同步、阻塞/唤醒和管道 |
| `fs/file_system_format.*`、`block_cache.*`、`file_system.*` | v0.11 | 磁盘格式、缓存、inode 和事务 |
| `io/console_input.*`、v1.0 历史 `io_descriptor.*` | v1.0 | 标准输入输出与第一版统一 fd |
| `source/user/src/shell_parser.cpp`、`shell.cpp`、`shell_entry.cpp` | v1.0 | Ring 3 命令解释器 |
| `core/kernel_main.*` | v0.4 起 | 各阶段整机装配、证据输出和最终验收 |
| `source/foundation/reference_counter.*`、`scope_rollback.*`、Kernel `memory/*` | v1.1 | 可回收对象、KVA、栈、页表生命周期与失败回滚 |
| `process/thread_scheduler.*`、`wait_queue.*`、`arch/extended_state.*` | v1.2 | Process/Thread、统一等待与完整扩展现场 |
| `arch/cpu_local.*`、`native_system_call.*`、`processor_features.*` | v1.3 | CPU 本地状态、处理器契约与原生系统调用 |
| `object/kernel_object.*`、`io/file_description.*`、`io/file_table.*` | v1.4 | 类型化对象、共享打开实例与动态 fd 表 |
| `fs/vfs.*`、`fs/memfs.*`、`fs/legacy_file_system.*` | v1.5 | 路径、挂载、cwd、内存文件系统与旧格式适配 |
| `fs/root_file_system*`、`tools/os_tools/rootfs_v2.py` | v1.6 | 生产根格式、三级间接树、完整命名空间与独立 fsck |
| `process/process_tree.*`、`program_arguments.*`、`programs/init.cpp` 与 exec/orphan probes | v1.7 | PID1、父子/Zombie、参数栈、磁盘 spawn/exec/wait 与失败回滚 |
| `memory/virtual_memory_area.*`、`user/user_memory.*`、User `user_heap.*` 与 memory probes | v1.8 | VMA、匿名 fault、`mmap/munmap/brk`、栈增长、用户堆与页表回收 |
| `ipc/pipe_manager.*`、`io/file_table.*`、User `shell_execution.*`、`programs/core_tool.cpp` | v1.11 | 动态管道、精确 fd 替换、外部命令、重定向和多级流水线 |
| `time/monotonic_clock.*`、`time/deadline_queue.*`、User `programs/time_probe.cpp` | v1.13 | PIT 精确换算、绝对 deadline、sleep 与 timed synchronization |
| `process/signal_manager.*`、ABI `signal.hpp`、User `programs/signal_probe.cpp` | v1.14 | 处置/屏蔽/pending、进程组、可重启阻塞、signal frame 与 sigreturn |

测试源码按同样领域命名分布在 `tests/unit/`、`tests/integration/`、
`tests/randomized/`、`tests/system/` 和 `tests/tooling/`。阅读生产实现后，应紧接
着阅读同名测试；测试通常比调用点更直接地表达边界条件。

## 10. 完成整套课程的判定

完成不等于“读完 14 个 Markdown 文件”。应能够独立完成以下任务：

- 从复位状态解释第一条 ROM 指令为什么可达。
- 不看代码画出 ROM、启动盘、低 64 MiB、Kernel 和用户地址空间布局。
- 解释每次 CPL、CR3、RSP 和控制流变化。
- 对任意异常说明 CPU 压入了什么、汇编补了什么、C++ 看到了什么。
- 解释一个按键如何唤醒阻塞 Shell，一个管道字节如何从 Producer 到 Consumer。
- 解释一个文件字节如何从 Ring 3 缓冲区到 ATA 扇区，并如何跨 QEMU 重启保存。
- 为一个正常路径和一个失败路径添加可重复测试。
- 在不使用 BIOS、第三方 bootloader、libc 或 QEMU `-kernel` 的条件下复现整机。

达到这些标准后，按 5.1 节进入已经完成的 v1.1–v1.16，再沿
[roadmap.md](../roadmap.md) 继续 ordered metadata journal 与 ABI 冻结。

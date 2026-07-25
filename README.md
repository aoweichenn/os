# x86-64 OS Lab

这是一个从 CPU 复位向量开始自研的 x86-64 教学操作系统项目。QEMU 仅用于模拟硬件；固件、引导程序、模式切换、内核、运行时、驱动、用户空间和文件系统均由项目自行实现。

当前状态：`v0.7 中断与设备`已完成，下一阶段为 `v0.8 用户边界`。自研
128 KiB ROM 从 `0xFFFFFFF0` 接管 CPU、初始化 COM1，通过 IDE ATA PIO
读取并校验自研 Stage 1；Stage 1 随后完成 A20、保护模式、64 MiB 身份映射、
长模式切换、Kernel 容器校验、ELF64 装载和 BootInfo 交接，最终进入
freestanding C++20 内核。内核随即替换 Stage 1 的描述符状态，建立自己的
GDT、TSS、IDT、32 个异常入口和无动态分配的 panic 路径。Stage 1 还通过
QEMU PC 的 `fw_cfg` 硬件接口读取 `etc/e820`，自行规范化为 BootInfo v2；
内核据此管理物理页帧，建立新的四级 4 KiB 页表、W^X/NX/WP 权限、四个
guard page、64 KiB 高半区早期堆，并真实切换 CR3。

## 最短构建与测试路径

在 Linux 环境安装 Python 3.11+、Clang、LLD、NASM、QEMU、GDB、CMake
和 Ninja 后执行：

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
[OS][KERNEL] FRAME_ALLOCATOR_READY
[OS][KERNEL] FREE_FRAMES=0x...
[OS][KERNEL] ALLOCATED_FRAMES=0x...
[OS][KERNEL] RESERVED_FRAMES=0x...
[OS][KERNEL] PAGING_READY
[OS][KERNEL] PAGING_ROOT=0x...
[OS][KERNEL] MEMORY_PERMISSIONS_VALID
[OS][KERNEL] HEAP_READY
[OS][KERNEL] HEAP_CAPACITY_BYTES=0x0000000000010000
[OS][KERNEL] HEAP_SELF_TEST_PASSED
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
`INT3` 自检，随后验证内存图、分配器、页权限和堆。独立故障镜像分别执行
`UD2`、访问首个未映射地址，以及让 Ring 0 写入只读页；最后一项必须产生
错误码 `0x3` 的 #PF，证明 `CR0.WP` 和只读页权限真实生效。

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

完整教材入口见
[books/x86-64-os-from-reset/README.md](books/x86-64-os-from-reset/README.md)。
教材采用 5 部 10 个完整主题章；每章按“背景与历史约束、硬件或软件
状态、实现机制、失败路径、验证证据”的统一深度展开。构建时会自动统计仅进入
目标系统的 `.cpp`、`.hpp` 和 `.asm` 真实代码量。
可单独执行 `python3 tools/os.py source-metrics` 查看同一口径。
执行 `make -C books/x86-64-os-from-reset phone-export` 可按硬件教材相同规则
导出到手机书库的独立目录。

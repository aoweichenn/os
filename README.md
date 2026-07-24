# x86-64 OS Lab

这是一个从 CPU 复位向量开始自研的 x86-64 教学操作系统项目。QEMU 仅用于模拟硬件；固件、引导程序、模式切换、内核、运行时、驱动、用户空间和文件系统均由项目自行实现。

当前状态：`v0.3 Long Mode` 已完成，`v0.4 内核加载`正在实施。自研
128 KiB ROM 从 `0xFFFFFFF0` 接管 CPU、初始化 COM1，通过 IDE ATA PIO
读取并校验自研 Stage 1；Stage 1 随后完成 A20、保护模式、分页和长模式切换。

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
```

日志规范见 [docs/logging.md](docs/logging.md)：启动日志只记录阶段里程碑和故障原因，
不在轮询或逐字节路径中刷屏。

当前 v0.4 已生成首个 freestanding C++20 ELF64 内核：
`build/developer/source/kernel/kernel.elf`。它由 LLD 直接链接，入口固定为
`0x00100000`，并由构建测试审计 ELF 头、加载段、入口和未解析符号。构建系统
还会把它放入 `boot_disk.img` 的自描述 Kernel 区域，以 CRC32、磁盘边界和
ELF64 双层规则独立审计；Stage 1 目标机装载将在下一增量实现。

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
教材现为 5 部 10 个完整主题章、98 页；每章按“背景与历史约束、硬件或软件
状态、实现机制、失败路径、验证证据”的统一深度展开。构建时会自动统计仅进入
目标系统的真实代码量。
可单独执行 `python3 tools/os.py source-metrics` 查看同一口径。
执行 `make -C books/x86-64-os-from-reset phone-export` 可按硬件教材相同规则
导出到手机书库的独立目录。

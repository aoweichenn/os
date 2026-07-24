# x86-64 OS Lab

这是一个从 CPU 复位向量开始自研的 x86-64 教学操作系统项目。QEMU 仅用于模拟硬件；固件、引导程序、模式切换、内核、运行时、驱动、用户空间和文件系统均由项目自行实现。

当前状态：`v0.0 工程基线`已完成，下一阶段为`v0.1 复位与串口`。

## 最短构建与测试路径

在 Linux 环境安装 Python 3.11+、Clang、LLD、NASM、QEMU、GDB、CMake
和 Ninja 后执行：

```bash
python3 tools/os.py verify
```

该命令会完成工具链检查、宿主机测试构建、x86-64 freestanding
交叉编译、空镜像生成、单元测试、集成测试、固定种子随机测试和 QEMU
TCG 整机冒烟测试。详细说明见 [docs/building.md](docs/building.md) 和
[docs/testing.md](docs/testing.md)。

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
```

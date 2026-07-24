# 构建说明

## 支持环境

当前基线支持 Linux 宿主机。宿主机可以不是 x86-64；Clang 负责生成 x86-64
目标文件，QEMU TCG 负责模拟 x86-64 CPU 与 PC 硬件。

必需工具：

- Clang 与 Clang++
- LLD
- NASM
- QEMU `qemu-system-x86_64`
- GDB
- Python 3.11 或更高版本
- CMake 3.28 或更高版本
- Ninja
- LLVM `nm`、`objdump` 与 `readelf`

Fedora：

```bash
sudo dnf install clang lld nasm qemu-system-x86-core gdb cmake ninja-build python3
```

Ubuntu：

```bash
sudo apt-get install clang lld nasm qemu-system-x86 gdb cmake ninja-build python3
```

QEMU 软件包可能连带安装 SeaBIOS 或 OVMF 文件，但项目运行命令始终通过
`-bios` 显式指定自研 ROM，不会使用这些固件。

## 一键验证

```bash
python3 tools/os.py verify
```

Python 入口依次执行：

1. 检查全部必要工具。
2. 使用 `developer` CMake preset 配置工程。
3. 构建宿主测试库和 x86-64 freestanding 库。
4. 生成 v0.0 空固件与空磁盘镜像。
5. 运行全部 CTest 测试。

## 手动构建

```bash
python3 tools/os.py doctor
python3 tools/os.py configure
python3 tools/os.py build
python3 tools/os.py test
```

## 构建产物

所有产物位于 `build/developer/`：

```text
source/foundation/libos_foundation_host.a
source/foundation/libos_foundation_x86_64.a
images/empty_firmware.bin
images/empty_disk.img
tests/os_foundation_unit_tests
tests/os_foundation_integration_tests
tests/os_foundation_randomized_tests
```

`libos_foundation_x86_64.a` 必须是 x86-64 ELF，且不能包含未解析的外部运行时符号。
`build/` 不进入 Git。

## 编译边界

x86-64 目标使用 freestanding C++20，并关闭：

- 编译器内建运行时假设
- 异常
- RTTI
- 位置无关代码
- 栈保护
- 线程安全局部静态初始化
- `__cxa_atexit`
- x86-64 红区、MMX 和 SSE
- 宿主 C++ 标准库头文件

这些限制由 CMake 目标 `os_foundation_x86_64` 集中管理，后续固件与内核目标必须复用同一策略。

## 构建职责

- CMake 描述模块、目标、源文件和依赖关系。
- Ninja 执行增量构建。
- Python 提供稳定命令入口并管理外部进程。
- CTest 保存测试注册、标签和完成判定。

Python 工具只使用标准库，不自行扫描 C++ 依赖，也不替代 CMake 生成构建图。

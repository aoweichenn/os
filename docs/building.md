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
- LLVM `nm`、`objdump`、`objcopy` 与 `readelf`

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
4. 生成自研 ROM、Stage 1、v0.11 ELF64 内核、六个用户 ELF，以及格式损坏、目标 ATA、
   内存图失败、非法指令、页故障和写保护注入镜像，同时保留 v0.0 空镜像
   回归基线。
5. 运行全部 CTest 测试。

## 手动构建

```bash
python3 tools/os.py doctor
python3 tools/os.py configure
python3 tools/os.py build
python3 tools/os.py test
python3 tools/os.py source-metrics
python3 tools/os.py phone-book-export
```

## 构建产物

所有产物位于 `build/developer/`：

```text
source/foundation/libos_foundation_host.a
source/foundation/libos_foundation_x86_64.a
source/firmware/generated/os_firmware.elf
source/firmware/generated/os_firmware_serial_failure.elf
source/firmware/generated/os_firmware_ide_busy_failure.elf
source/firmware/generated/os_firmware_ide_error_failure.elf
source/boot/stage1/generated/stage1.bin
source/boot/stage1/generated/stage1_memory_map_invalid.bin
source/kernel/kernel.elf
source/kernel/kernel_invalid_opcode.elf
source/kernel/kernel_page_fault.elf
source/kernel/kernel_write_protection.elf
images/firmware.bin
images/firmware_serial_failure.bin
images/firmware_ide_busy_failure.bin
images/firmware_ide_error_failure.bin
images/boot_disk.img
images/stage1_invalid_header_disk.img
images/stage1_invalid_checksum_disk.img
images/kernel_invalid_header_disk.img
images/kernel_invalid_checksum_disk.img
images/kernel_invalid_elf_disk.img
images/stage1_kernel_ata_timeout/boot_disk.img
images/stage1_kernel_ata_error/boot_disk.img
images/stage1_memory_map_invalid/boot_disk.img
images/kernel_invalid_opcode/boot_disk.img
images/kernel_page_fault/boot_disk.img
images/kernel_write_protection/boot_disk.img
images/empty_firmware.bin
images/empty_disk.img
tests/os_foundation_unit_tests
tests/os_foundation_integration_tests
tests/os_foundation_randomized_tests
tests/os_kernel_boot_info_unit_tests
tests/os_kernel_descriptor_layout_unit_tests
tests/os_kernel_descriptor_layout_randomized_tests
tests/os_kernel_handoff_layout_integration_tests
tests/os_kernel_physical_memory_map_unit_tests
tests/os_kernel_physical_frame_allocator_unit_tests
tests/os_kernel_heap_and_page_layout_unit_tests
tests/os_kernel_memory_bootstrap_integration_tests
tests/os_kernel_memory_management_randomized_tests
tests/os_kernel_device_model_unit_tests
tests/os_kernel_device_bootstrap_integration_tests
tests/os_kernel_interrupt_device_randomized_tests
```

`libos_foundation_x86_64.a` 必须是 x86-64 ELF，且不能包含未解析的外部运行时符号。
`kernel.elf` 必须是入口为 `0x00100000` 的 x86-64 `ET_EXEC`，入口位于可执行
`PT_LOAD` 段，且不能包含未解析符号。
`firmware.bin` 和失败路径变体必须都是精确 131072 字节。
全部启动磁盘镜像必须是精确 2097152 字节。
`build/` 不进入 Git。

## 固件生成链

```text
reset_and_serial.asm
  └─ NASM elf32 → .o
       └─ LLD + rom.ld → .elf
            └─ llvm-objcopy --gap-fill=0xff → 128 KiB .bin
```

`elf32` 是保存 16 位代码节和符号的目标文件容器，不表示 CPU 已进入 32 位模式。
链接脚本用 `ASSERT` 保证入口不侵占最后 16 字节的复位向量区域。

## Stage 1 生成链

```text
source/boot/stage1/src/entry.asm
source/boot/stage1/src/kernel_loader.asm
source/boot/stage1/src/memory_map.asm
  └─ NASM bin → stage1.bin
       └─ Python Stage 1 格式编码与校验
```

使用 `python3 tools/os.py audit-stage1 build/developer/images/boot_disk.img`
可以独立检查描述符、磁盘范围、加载范围和负载校验。Python 只负责宿主镜像
编码与审计；目标机上的 Stage 1 描述符解析、ATA PIO 和跳转全部由自研固件
执行。进入长模式后，`kernel_loader.asm` 用另一条自研 ATA PIO 路径读取
Kernel。

## Kernel ELF64 生成链

```text
source/kernel/src/*.cpp ─ Clang x86_64-unknown-none-elf ─┐
source/kernel/src/architecture.asm ─ NASM elf64 ─────────┤
                                                        └─ LLD elf_x86_64
                                                           + kernel.ld
                                                           → kernel.elf
            └─ Python ELF64 结构审计 + llvm-nm 符号审计
```

宿主为 ARM64 时不经过宿主 GCC 链接驱动，CMake 显式调用 LLD 的
`elf_x86_64` 模式。可独立执行：

```bash
python3 tools/os.py audit-kernel-elf build/developer/source/kernel/kernel.elf
```

四个 Ring 3 程序是独立 ELF64 产物，可分别审计：

```bash
python3 tools/os.py audit-user-elf build/developer/source/user/user_smoke.elf
python3 tools/os.py audit-user-elf build/developer/source/user/user_invalid_opcode.elf
python3 tools/os.py audit-user-elf build/developer/source/user/user_page_fault.elf
python3 tools/os.py audit-user-elf build/developer/source/user/scheduler_worker.elf
```

审计器要求 AMD64 `ET_EXEC`、入口位于可执行 `PT_LOAD`、段 4 KiB 对齐、
用户地址范围、W^X、无重叠和零未解析符号。它不替代内核解析器：宿主审计
证明“构建产生了预期文件”，QEMU 路径证明“目标内核自己拒绝或装入文件”。
正常镜像会从同一个 `scheduler_worker.elf` 创建三个进程；三者共享虚拟地址
布局但拥有不同 CR3，用 BSS 私有计数器证明地址空间隔离。

## Boot Disk 组合链

```text
stage1.bin ────────────────┐
                           ├─ boot_disk.img
kernel.elf ─ ELF64 审计 ───┘    ├─ Stage 1 描述符与负载
                                └─ Kernel 描述符、CRC32 与 ELF 文件
```

可分别审计同一磁盘中的两个阶段：

```bash
python3 tools/os.py audit-stage1 build/developer/images/boot_disk.img
python3 tools/os.py audit-kernel-image build/developer/images/boot_disk.img
```

构建同时生成 Kernel 描述符损坏、Kernel ELF 内容损坏、CRC 正确但 ELF
语义非法、目标 ATA 永久忙/设备错误、`fw_cfg` 内存图失败，内核
`UD2`/not-present 页故障/写保护页故障，以及用户 `#UD`、用户 `#PF` 和
截断用户 ELF 的失败镜像。宿主审计拒绝磁盘和 ELF 不可信输入，QEMU 测试
进一步证明 Stage 1 与 Kernel 自己走到对应边界。

## 教材构建

教材是独立工程，不强制 CI 安装完整 TeX Live：

```bash
make -C books/x86-64-os-from-reset check
make -C books/x86-64-os-from-reset pdf
```

PDF 生成在 `books/x86-64-os-from-reset/source/latex/main.pdf`，属于构建产物，
不进入 Git。

`source-metrics` 只统计 `source/` 下核心 `.asm`、`.cpp` 和 `.hpp` 的非空、
非纯注释行。汇编 include、测试、宿主工具、书稿、网站、构建描述和链接脚本
不计入操作系统本体代码量。教材构建会自动刷新同一统计结果。

## 手机教材导出

手机书库沿用硬件教材的分类和命名规则：

```bash
make -C books/x86-64-os-from-reset phone-export
```

目标文件为
`/mnt/sdcard/STU/BOOKS/按卷类型/原理卷/从复位向量到自研x8664操作系统/从复位向量到自研x8664操作系统.pdf`。
目录与文件名不含短横线、空格或 `+`，避免微信读书导入链路误处理。导出工具
只允许替换这个目录中的同名 PDF；若目录含有其他文件则拒绝覆盖。复制完成后
会核对 SHA-256。

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

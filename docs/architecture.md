# 项目架构

## 启动链

```text
CPU Reset Vector
       ↓
自研 ROM Firmware
       ↓
自研 Stage 1 / Stage 2
       ↓
ELF64 Kernel
       ↓
用户态与系统服务
```

QEMU 只模拟 CPU、内存、串口、IDE 等硬件。每个箭头都是显式交接契约，必须定义输入状态、内存布局、错误报告和下一阶段入口。

## 处理器模式

启动代码按照实模式、保护模式、长模式的顺序推进。GDT、控制寄存器、EFER 和页表均由项目代码建立，并通过反汇编、GDB 和串口标记验证。

## v0.1 ROM 与复位路径

```text
128 KiB ROM 文件偏移             x86 物理地址
0x00000                         0xFFFE0000
0x1F000  16 位入口              0xFFFFF000
0x1FFF0  E9 0D F0              0xFFFFFFF0  ← CPU 复位取指
0x1FFFF                         0xFFFFFFFF
```

复位时 CS 隐藏基址为 `0xFFFF0000`，IP 为 `0xFFF0`。复位向量使用 16 位
near jump，只修改 IP 为 `0xF000`，因此目标物理地址为 `0xFFFFF000`。
入口建立低端 RAM 栈，使用 CS override 从高端 ROM 读取常量。

```text
CPU Reset
   ↓ 物理地址 0xFFFFFFF0
near jump
   ↓
16 位入口（CLI、CLD、段寄存器、栈）
   ↓
COM1 初始化
   ↓
有界 THRE 轮询与串口协议
   ↓
HLT
```

## 模块边界

- `foundation` 提供地址、字节数和地址区间等不依赖运行时的基础类型。
- `firmware` 当前负责复位入口和最小串口初始化；v0.2 增加 Stage 1 加载。
- 引导阶段负责模式切换、内存探测与 ELF64 内核加载。
- 内核负责异常、中断、内存、调度、设备和系统调用。
- 用户空间只通过受控 ABI 使用内核能力。

操作系统目标代码与宿主机测试代码使用同一份模块实现。目标构建通过 Clang
交叉编译为 x86-64 ELF，并关闭异常、RTTI、栈保护、红区和宿主 C++ 标准库头文件。
宿主构建仅用于快速执行测试，不会进入最终镜像。

## 依赖方向

```text
tests ───────────────→ foundation
firmware ────────────→ foundation
boot stages ─────────→ foundation
kernel ──────────────→ foundation
```

`foundation` 不得反向依赖固件、引导阶段、内核或宿主测试框架。

## 源码组织与可见性

源码按领域模块组织，不按文件扩展名集中堆放。当前模块采用以下结构：

```text
source/foundation/
├── CMakeLists.txt
├── include/os/foundation/
│   └── address_range.hpp
└── src/
    └── address_range.cpp

source/firmware/
├── CMakeLists.txt
├── linker/
│   └── rom.ld
└── src/
    └── reset_and_serial.asm
```

`include/os/<模块>/` 只保存其他模块可以依赖的公开契约；`src/` 保存实现和
模块私有头文件。CMake 将公开目录标记为 `PUBLIC`、私有目录标记为 `PRIVATE`，
消费者不能通过传递依赖获得私有包含路径。

每个源码模块拥有独立 `CMakeLists.txt` 和 CMake target。根构建文件只负责
公共策略、模块组合、镜像产物和测试入口，不保存具体模块的源文件清单。

测试目录按测试层和被测领域镜像生产代码：

```text
tests/
├── unit/foundation/
├── integration/boot/
├── randomized/foundation/
├── tooling/
└── support/cpp/
```

尚未实现的固件、引导和内核模块不会预先创建空目录；开始对应最小增量时再按照
相同边界落地。

## 宿主工具边界

Python 只运行在宿主机，负责工具链检查、CMake/CTest 调度、镜像生成、ELF
与 ROM 审计和 QEMU 生命周期管理。Python 不进入操作系统镜像，也不解析或
替代 CMake 构建图。所有外部程序均以参数列表直接启动，不经过 Shell 字符串
求值。

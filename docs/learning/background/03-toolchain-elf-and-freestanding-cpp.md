# B3：工具链、ELF 与 freestanding C++

## 1. 源码不会直接出现在 CPU 面前

从一行 C++ 到 CPU 执行，至少经过：

```text
C++ source
  → preprocessing
  → compilation
  → assembly/machine code
  → relocatable object
  → linking
  → ELF or raw binary
  → image packaging
  → loader copies bytes
  → CPU fetches mapped memory
```

每层解决不同问题。很多操作系统故障并不在算法，而在：

- 编译成了错误架构。
- linker 把段放到错误地址。
- 文件 offset 与内存 address 混淆。
- BSS 没有清零。
- hidden runtime symbol 无人实现。
- raw image 中复位向量位置错误。
- loader 相信了损坏 ELF。

## 2. Host、build 与 target

### 2.1 Host

执行 CMake、Clang、NASM、Python、QEMU 的开发机。它可能是 AArch64 或
x86-64 Linux。

### 2.2 Target

最终机器代码运行的环境：x86-64 freestanding PC。

### 2.3 为什么不能相信编译器默认值

直接运行宿主 `c++` 往往：

- 生成 host ISA。
- 面向 Linux ABI。
- 默认链接启动文件和 libc。
- 使用宿主头文件。

本项目目标命令必须明确 target triple 和 freestanding flags，并用 ELF audit
再次检查 `e_machine`。构建配置是系统架构的一部分，不是辅助脚本。

## 3. 预处理

C/C++ 预处理器处理：

- `#include`
- 条件编译
- 宏展开
- pragma

预处理后，头文件文本被组合进 translation unit。

### 3.1 Header 不是已编译模块

同一 header 可能被多个 `.cpp` 编译。若在 header 定义非-inline 全局对象，会
导致 multiple definition；若每个 TU 持有内部副本，又可能破坏单一状态。

项目使用：

- `#pragma once`。
- `inline constexpr` 共享编译期常量。
- 头文件声明、源文件定义。

### 3.2 为什么限制宏

宏没有类型、命名空间和普通作用域，容易：

- 重复求值参数。
- 隐藏控制流。
- 产生难读诊断。
- 绕过现代类型检查。

硬件常量优先 `inline constexpr`，有限状态优先 `enum class`。

## 4. 编译

Clang 前端：

1. 解析 C++。
2. 做类型检查。
3. 生成中间表示。
4. 优化。
5. 生成目标 ISA 指令。

### 4.1 优化改变实现，不应改变契约

编译器可：

- 内联。
- 删除不可达代码。
- 合并常量。
- 重排普通内存访问。
- 把循环换成 runtime helper。

Kernel 代码不能依赖“debug build 恰好生成的指令顺序”。硬件交互需要正确的
`volatile`/asm clobber/atomic 语义，架构关键序列还应审计反汇编。

### 4.2 编译器可能生成你没写的调用

结构复制、清零或大整数操作可能被 lowering 成：

- `memcpy`
- `memset`
- `memmove`
- compiler runtime helper

freestanding 环境必须：

- 自己实现允许的最小符号，或
- 通过编译/优化约束避免生成，且
- 审计 undefined symbols。

当前 Kernel/User 都有自己的
`freestanding_memory.cpp`，不能链接宿主 libc 的实现。

## 5. 汇编

NASM 直接把 `.asm` 的指令和数据编码为：

- relocatable ELF object，或
- raw binary。

### 5.1 Symbol 仍未必有最终地址

在目标文件阶段，`call some_function` 的最终 displacement 可能未知。汇编器
生成 relocation，交给 linker 在布局确定后修补。

### 5.2 `org` 与链接地址

raw binary 的 `org` 帮助汇编器计算地址，但不会让平台自动把字节放到那里。
实际加载位置还由 ROM mapping、磁盘 loader 或 QEMU 参数决定。

## 6. Relocatable object

`.o` 文件通常包含：

- section。
- symbol table。
- relocation。
- machine type。
- debug information。

它不是可直接启动的完整程序，因为：

- 外部符号可能未解析。
- 各 section 尚无最终地址。
- entry point 尚未确定。
- 多个 object 尚未合并。

## 7. Symbol

symbol 把名称与某个定义或未定义引用关联：

```text
name
binding: local/global/weak
type: function/object/section
section index
value/address
size
```

### 7.1 C++ name mangling

C++ 支持重载，symbol 通常编码 namespace、类型和参数。汇编或硬件固定入口
需要稳定名字时使用：

```cpp
extern "C"
```

这只改变语言 linkage，不自动建立正确 ABI、栈或返回方式。

### 7.2 Undefined symbol

目标文件引用未定义符号并不一定错误；linker 最终找不到定义才失败。

freestanding audit 还要警惕链接器通过 compiler runtime 库悄悄满足某些符号，
让内核产生未计划依赖。

## 8. Relocation

relocation 记录：

```text
哪个位置
以何种编码
引用哪个 symbol
加多少 addend
```

linker 知道最终地址后计算值。

### 8.1 PC-relative

许多 x86-64 call/jump 和 RIP-relative data access 使用：

```text
target - next_instruction
```

若距离超出编码范围，linker 可能报 relocation overflow。

### 8.2 Absolute

启动代码有时需要绝对物理/虚拟地址。若镜像被装到另一位置，绝对值不会自动
工作，除非 loader 做动态重定位；当前 ET_EXEC 用户/Kernel 不做动态链接。

## 9. Static archive

`.a` 是多个 object 的索引集合。linker 通常只提取能满足当前 unresolved
symbols 的成员。

`libos_foundation_x86_64.a` 不是：

- shared library。
- ROM。
- Kernel。
- 运行时自动加载对象。

它只是构建和复用边界。

## 10. Linking

linker：

1. 收集 input sections。
2. 解析 symbol。
3. 选择并排列 output sections。
4. 分配 VMA/LMA/file offsets。
5. 应用 relocation。
6. 产生 program headers。
7. 决定 entry。

### 10.1 Linker script 是地址规格

项目 linker script 明确：

- ROM 从哪里开始。
- reset vector 落在哪个 offset。
- Kernel PT_LOAD 如何分权。
- 用户 ELF 的 text/data/stack 计划。
- entry symbol。
- alignment 与越界 assertion。

修改 linker script 等同修改运行时地址 ABI，必须与 loader、页表和测试同步。

## 11. VMA、LMA 与 file offset

三个概念不能混：

| 概念 | 问题 |
| --- | --- |
| file offset | 字节在 ELF/raw 文件哪里 |
| LMA | loader 把初始字节从哪里/放到哪个物理加载位置 |
| VMA | 代码运行时用什么地址引用 |

简单 identity-loaded ELF 可能让 VMA=LMA，数值相同不代表概念消失。

ROM 还会出现：

- ELF 自身供审计/debug。
- raw ROM 按平台固定物理窗口映射。
- reset vector 必须位于 raw file 对应末端 offset。

## 12. Section 与 segment

### 12.1 Section

主要服务编译、链接和调试：

- `.text`
- `.rodata`
- `.data`
- `.bss`
- `.symtab`
- `.debug_*`

### 12.2 Segment/program header

主要服务 runtime loader：

- `PT_LOAD`
- file offset。
- virtual/physical address。
- `p_filesz`。
- `p_memsz`。
- R/W/X flags。
- alignment。

加载器应按 program header，不是按 section header 装载。

### 12.3 为什么一个 segment 可含多个 sections

linker 可以把 `.text`、部分只读 section 合成一个 RX PT_LOAD。section 是构建
组织，segment 是加载与页权限组织。

## 13. ELF header

ELF64 header 包含：

- magic `0x7F 'E' 'L' 'F'`。
- class。
- endianness。
- type。
- machine。
- version。
- entry。
- program/section header table offset/count/size。

有 magic 只证明前四字节像 ELF，不证明：

- 表在文件范围内。
- count×entrySize 不溢出。
- machine 是 x86-64。
- entry 可执行。
- segment 不重叠。

## 14. Program header 与 `PT_LOAD`

对每个 loadable segment，要验证：

```text
p_offset + p_filesz within file
p_vaddr + p_memsz without overflow
p_filesz <= p_memsz
alignment valid
permissions known and W^X
target range allowed
segments do not overlap
entry lies in executable segment
```

### 14.1 两遍装载

当前 Stage 1：

1. 第一遍验证全部 header 和彼此关系。
2. 只有全体通过，第二遍才复制和清零。

若边验证边复制，后面的坏 header 会让目标内存留下“半个 Kernel”。两遍设计
建立失败前无目标副作用的边界。

## 15. BSS 与零初始化

`.bss` 表示内存中需要存在、文件中无需存储大量零的区域：

```text
p_memsz > p_filesz
```

loader 必须把尾部：

```text
[destination+p_filesz, destination+p_memsz)
```

清零。否则 C++ 的静态存储期零初始化前提不成立。

当前 Kernel 有 BSS probe；不是“变量碰巧为零”，而是对 loader 契约的整机
证据。

## 16. ELF type

### 16.1 ET_REL

可重定位 object，尚未最终布局。

### 16.2 ET_EXEC

固定虚拟布局的 executable。当前 Kernel/User loader 支持严格 ET_EXEC
子集。

### 16.3 ET_DYN

常用于 shared object 或 PIE，需要加载基址选择和 relocation/dynamic linker
语义。v1.0 基线和当前 v1.8 都不支持，遇到不认识的类型应拒绝。

## 17. Raw binary

raw binary 只有字节，没有：

- machine type。
- symbol。
- entry。
- section/segment table。
- relocation。

它适合硬件要求固定大小/位置的 ROM、扇区或 payload，但所有布局信息必须由
外部规格提供。

### 17.1 为什么还保留 ELF

常见流程：

```text
link ELF
  → audit symbols/sections/addresses
  → objcopy/extract raw bytes
  → pad to exact ROM size
  → audit final raw offsets
```

ELF 便于链接和调试，raw 便于硬件映射；两者各有职责。

## 18. ROM 镜像

当前 ROM 必须：

- 精确 128 KiB。
- 对应物理 `0xFFFE0000..0xFFFFFFFF`。
- reset vector bytes 位于最后 16-byte 区域。
- 无未解析 runtime 依赖。
- 入口跳转目标在 ROM。

仅看 ELF symbol 地址不够，因为：

- raw extraction 可能裁错。
- padding 可能错。
- reset bytes 可能被后处理覆盖。

所以最终 raw ROM 也要审计。

## 19. Disk image 与 container

磁盘 image 是扇区数组。当前所有者包括：

```text
boot descriptor/payload
Kernel descriptor/ELF
reserved boot region
filesystem region
reserved tail
```

### 19.1 Container 解决什么

raw payload 本身不携带可信边界。descriptor/container 提供：

- magic/version。
- payload LBA。
- byte/sector length。
- load address。
- entry。
- checksum/CRC。

ROM/Stage 1 先验证 descriptor，再读取 payload。长度、LBA 和地址计算都必须
防溢出。

### 19.2 镜像生成器也是可信计算基

宿主工具决定哪些字节写进什么扇区。它必须拒绝：

- payload 超容量。
- boot 与 filesystem 重叠。
- descriptor 与真实长度不一致。
- entry 越界。

“Guest 会再检查”不能成为 host 生成坏镜像的借口；两端检查能定位不同错误。

## 20. Freestanding 与 hosted

### 20.1 Hosted 程序的隐含前置

普通 C++ 程序通常由 runtime：

- 准备 argc/argv/env。
- 初始化 TLS。
- 运行全局构造。
- 建立异常/RTTI 支持。
- 调用 `main`。
- 运行析构。
- 通过 OS syscall 退出。

### 20.2 Freestanding 环境

标准只保证更小核心。具体内核必须自己定义：

- entry symbol。
- 栈。
- BSS 清零。
- 全局初始化策略。
- 内存函数。
- allocation。
- panic/termination。

“使用 C++20”不自动提供 C++ runtime。

## 21. 当前禁用或限制的语言机制

目标构建关闭或避免：

- exceptions。
- RTTI。
- stack protector。
- hosted standard library。
- thread-safe local static guard。
- `__cxa_atexit`。
- red zone。
- 未管理的 SSE/MMX 自动代码。
- 隐藏动态分配。

具体 flags 以根
[CMakeLists.txt](../../../CMakeLists.txt)
和构建审计为准，不要从背景文档复制成永远不变的清单。

## 22. 全局初始化

### 22.1 Constant initialization

编译期可放入 `.data/.rodata` 或零初始化 `.bss`，不需要执行代码。

### 22.2 Dynamic initialization

需要启动时调用 constructor，通常通过：

- `.init_array`
- runtime startup traversal

### 22.3 项目的选择

当前自研启动链不运行 hosted constructor machinery。全局对象必须：

- `constinit`/常量初始化，或
- 平凡存储，后续显式 `Initialize()` 完整覆盖。

v1.0 描述符表曾因类内数组初始化产生 `.init_array`，Kernel ELF audit 在镜像
生成前拒绝。最终移除隐式构造需求并显式初始化。

### 22.4 `constexpr`、`constinit`、`const`

- `constexpr` 强调可在常量表达式使用。
- `constinit` 要求静态对象进行静态初始化，不表示对象以后不可变。
- `const` 表示通过该名称不可修改，不保证没有运行时 constructor。

三者不能互相替代。

## 23. C++ 对象模型与硬件布局

C++ struct 可能有：

- padding。
- alignment。
- ABI-dependent enum size。
- 非平凡 constructor/destructor。
- vptr。

用于内存内部状态可以让编译器布局并 `static_assert` 必要大小；用于磁盘、网络、
CPU 硬件表时更应：

- 固定宽度字段。
- 明确 offset。
- 显式 encode/decode。
- 验证 reserved bits。

GDT/IDT/PTE 等硬件布局还要按架构手册拆位，不应通过未指定 bit-field layout
猜测。

## 24. W^X 从链接到页表

要真正做到 write xor execute，需要多层一致：

1. linker 把 text/rodata/data 分入合适 PT_LOAD。
2. ELF audit 拒绝 W+X segment。
3. loader 保留权限语义。
4. page-table builder 设置 RW/NX。
5. `IA32_EFER.NXE` 已启用。
6. `CR0.WP` 让 Ring 0 也服从 read-only。
7. fault injection 证明硬件实际拒绝。

只在源码注释写“read-only”或只看 ELF flags 都不够。

## 25. Build system 的职责

CMake/Ninja 负责依赖图：

```text
source change
  → affected object
  → affected ELF/raw image
  → affected audit/test
```

CMake preset 固定常用配置，Python `tools/os.py` 组合：

- doctor。
- configure/build。
- image generation。
- audit。
- QEMU runs。
- verify。

工具入口应保持薄编排，领域算法用可测试 Python/C++ 模块实现，避免所有逻辑
塞进一条 shell command。

## 26. 产物审计

源码测试不能证明 linker 最终做了什么。审计典型检查：

- ELF class/machine/type。
- entry。
- program headers。
- W^X。
- section/segment 地址。
- undefined symbols。
- `.init_array`。
- raw image size。
- reset bytes。
- disk region overlap。
- 特定指令序列。

审计发生在写磁盘前，可让结构错误快速失败，而不是启动后 silent reset。

## 27. Debug information 与 strip

ELF 的 `.debug_*`、symbol table 服务 GDB/工具，不需要装入目标内存。Loader 只
装 `PT_LOAD`。

因此：

- 可保留带 debug 的 ELF 供 GDB。
- raw/embedded payload 只包含必要 load bytes。
- 文件大小不能直接等于 runtime memory footprint。

## 28. Reproducibility

可复现不一定要求每个 bit 在所有工具版本下相同，但至少要求：

- 工具版本满足明确范围。
- 一条命令可重新生成全部产物。
- 固定随机种子。
- 不依赖当前目录外的隐式文件。
- QEMU 参数明确。
- 临时可写磁盘不污染基线。
- 日志协议稳定。

构建路径、timestamp 或 archive ordering 如果进入产物，也要评估是否影响
审计与缓存。

## 29. 常见误解

### 29.1 “编译成功就是可启动”

编译只证明单个 translation unit 可生成 object。链接、布局、镜像和硬件入口
仍可能错误。

### 29.2 “ELF 有 section 就按 section 加载”

runtime loader 看 program headers。section 可被 strip，也可能不映射。

### 29.3 “`.bss` 在文件里全是零”

它通常根本不占同等 file bytes；loader 依据 `p_memsz-p_filesz` 清零。

### 29.4 “freestanding 不能用 C++”

可以使用现代类型系统和编译期能力，但每项 feature 必须审计 runtime 依赖。

### 29.5 “`extern C` 让异常入口变成 C 函数”

它只稳定 symbol linkage。CPU 帧规范化、stack alignment、寄存器保存和
`iretq` 仍需汇编。

## 30. 对照项目阅读

1. [根 CMakeLists](../../../CMakeLists.txt)
2. [CMake presets](../../../CMakePresets.json)
3. [统一工具入口](../../../tools/os.py)
4. [ROM linker script](../../../source/firmware/linker/rom.ld)
5. [Kernel linker script](../../../source/kernel/linker/kernel.ld.in)
6. [User linker script](../../../source/user/linker/user.ld.in)
7. [Kernel ELF loader](../../../source/boot/stage1/src/kernel_loader.asm)
8. [User ELF loader](../../../source/kernel/src/user/user_elf.cpp)
9. [Kernel freestanding memory](../../../source/kernel/src/core/freestanding_memory.cpp)
10. [User freestanding memory](../../../source/user/src/freestanding_memory.cpp)

## 31. 练习

### 练习 A：产物分类

为 `.cpp`、`.o`、`.a`、Kernel ELF、ROM raw、disk raw 分别写出：

- 是否有 symbol table。
- 是否可被 CPU 直接从当前地址执行。
- 谁决定加载地址。

### 练习 B：PT_LOAD

给定：

```text
p_offset=0x1000
p_filesz=0x1800
p_memsz=0x2300
p_vaddr=0x200000
```

写出 file range、copy destination 和 zero range，并检查每次加法。

### 练习 C：Section/segment

解释为什么 `.text` 和 `.rodata` 可以处于一个 RX segment，以及为什么 `.bss`
可以在 RW segment 中占内存但不占相同文件字节。

### 练习 D：隐藏 runtime

假设加入一个有 constructor 的全局 `Logger`：

- object/ELF 可能新增什么。
- 当前启动链为什么不会自动运行。
- 如何改成 constant initialization 或显式 Initialize。
- 哪个 audit 应发现回归。

### 练习 E：W^X

列出从 linker script 到硬件 #PF 的全部证据。指出任何一层缺失后还剩下什么
只能被“相信”。

## 32. 通过标准

应能：

- 区分 preprocess、compile、assemble、archive、link、package、load。
- 解释 symbol、relocation、section、segment、VMA、LMA、file offset。
- 手工验证一个简单 PT_LOAD。
- 说明 BSS 为什么由 loader 清零。
- 说明 ROM/raw image 为什么需要最终字节审计。
- 列出 freestanding C++ 不可默认依赖的运行时能力。
- 解释 `.init_array`、red zone、compiler-generated memcpy 和 W^X 的风险。

下一册进入
[内存、分页、保护与异常](04-memory-paging-protection-and-exceptions.md)。

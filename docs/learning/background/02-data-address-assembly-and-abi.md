# B2：位、整数、地址、汇编与 ABI

## 1. 为什么这些基础会直接决定内核正确性

普通应用把许多底层事实交给语言运行时和操作系统处理。启动器、加载器和内核
必须自己回答：

- 设备寄存器的某一 bit 是 0 还是 1？
- 两个 `uint64_t` 相加是否回绕？
- 磁盘中的四个字节按什么顺序组成整数？
- `0x1000` 是物理地址、虚拟地址、端口还是 LBA？
- CPU 异常压栈和 C++ 函数栈帧是否相同？
- 汇编调用 C++ 时，参数在哪些寄存器？
- 为什么一次 `int 0x80` 能跨特权级，而 `call` 不能？

这些不是语法细节，而是跨硬件、汇编、C++、磁盘和用户态共享的契约。

## 2. Bit 是状态，不是“小数字”

一个 bit 只能是 0 或 1，但可以表示：

- 布尔状态，例如 PTE Present。
- 集合成员，例如 PIC mask 中某个 IRQ。
- 整数的一部分。
- 状态机编码。
- 权限组合。

### 2.1 位置与掩码

bit 位置从 0 开始：

```text
value:  b7 b6 b5 b4 b3 b2 b1 b0
mask:   0  0  0  0  0  1  0  0  = 1 << 2 = 0x04
```

常见操作：

```cpp
const bool set = (value & mask) != 0;
value |= mask;                         // set
value &= static_cast<T>(~mask);        // clear
```

设备寄存器常把不同 bit 分配给不同语义，写回前必须保留不属于当前模块的位。

### 2.2 Bit field 与状态枚举

不要把两类编码混淆：

- flags：多个 bit 可同时为 1，例如 ELF `PF_R|PF_X`。
- enum：一个字段只能取一个离散状态，例如 `Ready/Running/Blocked`。

如果用 flags 表示互斥状态，很容易出现两个状态位同时为 1；如果用 enum 表示可
组合权限，又会产生大量人为组合值。

## 3. 二进制与十六进制

十六进制每一位精确对应 4 bit：

```text
0xA5 = 1010 0101₂
```

所以硬件文档和地址常用十六进制：

- 低 12 bit 是 4 KiB 页内 offset → mask `0xFFF`。
- 4 KiB alignment → 地址末三位十六进制为 000。
- COM1 base port → `0x3F8`。
- x86 reset vector → `0xFFFFFFF0`。

学习时不要只把十六进制丢给计算器。应能快速识别：

```text
0x1000       = 4 KiB
0x10000      = 64 KiB
0x100000     = 1 MiB
0x40000000   = 1 GiB
0x100000000  = 4 GiB
```

## 4. 无符号整数

N bit 无符号整数范围：

```text
0 .. 2^N - 1
```

算术按模 `2^N`：

```text
uint8_t(255) + 1 = 0
```

这种回绕由语言为无符号类型定义，但内核不能因此把它当成合法地址计算。

### 4.1 地址加长度

危险写法：

```cpp
end = begin + size;
if (end <= limit) { ... }
```

如果加法先回绕到低地址，检查可能错误通过。正确思路：

```text
若 size > UINT64_MAX - begin → overflow
否则 end = begin + size
再检查 [begin,end)
```

项目的
[address_range.cpp](../../../source/foundation/src/address_range.cpp)
把这个边界变成可测试基础模块。

### 4.2 减法下溢

同样要先证明：

```text
end >= begin
```

再计算 `end - begin`。两个任意无符号数相减不会报告错误，只会模回绕。

## 5. 有符号整数与二进制补码

现代 x86 使用二进制补码表示有符号整数。N bit 范围：

```text
-2^(N-1) .. 2^(N-1)-1
```

以 8 bit 为例：

```text
  5 = 0000 0101
 -5 = 1111 1011  // invert + 1
```

最高 bit 参与符号解释，但同一组 bit 可按无符号或有符号读取：

```text
11111111₂ = uint8_t 255 = int8_t -1
```

CPU 只进行位运算并设置 flags；“这是 signed 还是 unsigned”常由具体指令或后续
条件跳转决定。

### 5.1 为什么 syscall 返回用 `int64_t`

本项目约定：

- 非负值是成功结果、字节数或 fd。
- 负值是错误码。

同一个 RAX 寄存器保存 bit pattern，C++ wrapper 按 `int64_t` 解释，避免另设
错误输出指针。ABI 必须固定宽度，否则 Kernel 和用户可能对返回值大小理解不同。

### 5.2 有符号溢出

C++ 有符号溢出是未定义行为，编译器可假定它不发生。地址、长度和计数边界通常
使用无符号固定宽度类型，并显式检查；不要依赖 signed overflow 回绕。

## 6. 宽度、截断与扩展

### 6.1 截断

把宽值转成窄值会丢高位：

```text
uint16_t 0x1234 → uint8_t 0x34
```

写 8-bit 端口前要先证明值适合，或明确只发送低字节。

### 6.2 Zero extension

无符号窄值扩到宽值，高位补 0。

### 6.3 Sign extension

有符号窄值扩到宽值，复制符号位：

```text
int8_t 0x80 (-128)
→ int64_t 0xFFFFFFFFFFFFFF80
```

x86 指令编码有时会把 immediate sign-extend。阅读反汇编时必须看 operand
size，不能只看十六进制常量。

## 7. 字节序

字节序描述多字节整数怎样放在连续地址。

### 7.1 Little-endian

x86 是 little-endian，最低有效字节放最低地址：

```text
uint32_t value = 0x12345678

address +0 : 0x78
address +1 : 0x56
address +2 : 0x34
address +3 : 0x12
```

### 7.2 字节序不改变位编号

一个寄存器内部的 bit 0 仍是最低有效 bit。字节序只在把多字节数映射到多个
地址时出现。

### 7.3 磁盘格式为何显式编码

即使当前 host 和 target 都可能是 little-endian，也不能把 C++ struct 直接
dump 到磁盘，因为还受：

- padding。
- alignment。
- enum underlying type。
- compiler ABI。
- 未来格式演进。

影响。项目的
[file_system_format.cpp](../../../source/kernel/src/fs/file_system_format.cpp)
按固定 offset 编解码 little-endian 字段，并验证 reserved bytes 与 CRC。

## 8. 对齐

若 `alignment` 是 2 的幂：

```text
aligned ⇔ (address & (alignment - 1)) == 0
```

向下对齐：

```text
down = value & ~(alignment - 1)
```

向上对齐前先检查加法溢出：

```text
up = (value + alignment - 1) & ~(alignment - 1)
```

### 8.1 为什么需要对齐

- 页表根要求 4 KiB 对齐。
- 页表项低位要复用作 flags。
- ELF segment 有 `p_align` 契约。
- 栈要满足 ABI alignment。
- 设备 DMA 或寄存器可能要求特定宽度；本项目当前 ATA 用 PIO，不依赖 DMA。

### 8.2 对齐不是边界验证

地址对齐只说明低位为零，不说明：

- 它落在 RAM。
- 整个对象不越界。
- 它属于当前进程。
- 它允许写入。

必须分别验证 range、ownership 和 permission。

## 9. 半开区间

项目统一使用：

```text
[begin,end)
```

性质：

```text
length = end - begin
empty  = begin == end
[a,b) 与 [b,c) 相邻但不重叠
最后有效字节 = end - 1（非空时）
```

对装载器尤其重要：

```text
file range   [p_offset, p_offset+p_filesz)
memory range [p_vaddr,  p_vaddr+p_memsz)
```

任何加法都要先检查溢出，再比较区间。

## 10. 地址不是一种整数

同样数值 `0x3F8` 可以被错误地解释为多种领域对象：

| 类别 | 含义 | 访问方式 |
| --- | --- | --- |
| physical address | RAM/MMIO 总线地址 | 页表/物理映射 |
| virtual address | 当前 CR3 下的线性地址 | 普通 load/store |
| I/O port | 独立端口空间 | `in/out` |
| LBA | 磁盘扇区编号 | ATA command protocol |
| file offset | 文件内字节位置 | 文件系统 |

把它们都写成裸 `uint64_t` 会让编译器无法阻止跨域误用。项目规范要求类型和名称
表达地址空间与单位，即使硬件 ABI 最终必须传整数，也应在边界处显式转换。

## 11. 物理地址、线性地址与虚拟地址

### 11.1 Physical address

送到内存控制器或设备总线的地址。它可能指向：

- usable RAM。
- ROM。
- MMIO。
- 保留洞。

物理地址存在不等于可由 allocator 分配。

### 11.2 Logical/linear

传统 x86：

```text
selector:offset → segmentation → linear
```

在 64 位 flat model 中，除 FS/GS 等少数情况外，普通段 base 近似 0，所以项目
常把程序产生的地址称为 virtual/linear address。

### 11.3 Virtual

分页把 linear address 翻译为 physical frame + page offset。相同 VA 在不同
CR3 下可以指向不同 PA。

## 12. Port I/O 与 MMIO

### 12.1 Port I/O

x86 独立 I/O address space 使用：

```asm
in  al, dx
out dx, al
```

普通指针不能访问 `0x3F8` 端口。项目在
[port_io.cpp](../../../source/kernel/src/device/port_io.cpp)
集中包装带 `volatile` 语义的端口指令。

### 12.2 MMIO

设备寄存器映射到物理地址空间，通过页表映射后用 load/store。LAPIC 是当前
项目的 MMIO 例子。

MMIO 页通常需要 cache policy，不能按普通 WB RAM 映射。编译器层面的
`volatile` 只约束访问生成，不能替代 CPU cache 类型、原子或锁。

## 13. 磁盘 LBA

LBA 是逻辑扇区编号，不是字节地址：

```text
byte offset = LBA × sector size
```

当前 ATA sector size 为 512 bytes。乘法也要检查溢出和设备容量。

文件系统 block 当前也为 512 bytes，数值恰好相同不代表两个概念可以混用：

```text
filesystem relative block
  → add filesystem start LBA
  → disk LBA
```

这一步体现磁盘区域所有权。

## 14. 汇编语言是什么

汇编语言是机器指令的人类表示，不是“比 C 更接近硬件所以自动正确”。

它仍需：

- 汇编器选择编码。
- 链接器解析符号和重定位。
- ABI 规定寄存器与栈。
- 程序员维护权限、模式和状态。

项目只在 C++ 无法直接表达的架构边界使用 NASM Intel 语法。

## 15. Intel 语法基本形式

```asm
instruction destination, source
```

示例：

```asm
mov rax, rbx
add rax, 8
mov [destination], rax
mov rcx, [source]
```

### 15.1 方括号

在 NASM Intel 语法中：

```asm
mov rax, symbol     ; 与 symbol 地址/立即数语义相关
mov rax, [symbol]   ; 从 symbol 指向的内存读取
```

是否带 `[]` 是地址与内容的根本区别。

### 15.2 Operand size

```asm
mov byte  [x], 1
mov word  [x], 1
mov dword [x], 1
mov qword [x], 1
```

若编码无法从寄存器推导宽度，要显式声明。

### 15.3 `bits 16/32/64`

NASM 的 `bits` 指令影响默认操作数、地址和编码解释。它不会替 CPU 切换模式。
源码写 `bits 64`，但 CPU 仍在实模式时执行这些字节，只会被按当前模式错误
译码。

硬件状态切换和汇编器编码必须一致。

## 16. 常用寄存器类别

### 16.1 通用寄存器

```text
RAX RBX RCX RDX
RSI RDI RBP RSP
R8..R15
```

硬件对部分寄存器有传统隐含语义，但 C++ ABI 又赋予 caller/callee-save 约定。

### 16.2 RIP

当前指令位置。普通代码不能用 `mov rip,...` 随意写它，控制流通过 jump、call、
ret、interrupt/exception return 等改变。

### 16.3 RFLAGS

包含：

- 算术条件位 CF/ZF/SF/OF。
- IF 可屏蔽中断使能。
- DF 字符串指令方向。
- IOPL 等权限状态。

进入 C++ 前清 DF 是 ABI 前提；否则 `memcpy` 风格字符串指令可能反向运行。

### 16.4 控制寄存器与 MSR

- CR0：PE、PG、WP 等。
- CR2：页错误线性地址。
- CR3：页表根物理地址。
- CR4：PAE 等扩展。
- IA32_EFER MSR：LME、LMA、NXE。

这些只能在足够特权执行；错误值可能立即让下一次取指失败。

## 17. Flags 与条件跳转

比较通常执行减法但不保留结果：

```asm
cmp rax, rbx
je  equal
jb  unsigned_below
jl  signed_less
```

同一 bit pattern 的 signed/unsigned 比较使用不同 flags 组合：

- `jb/ja` 关注 CF/ZF。
- `jl/jg` 关注 SF/OF/ZF。

边界代码如果选择错跳转，会把高地址当负数或把负错误码当巨大成功值。

## 18. Stack 的机器模型

x86 stack 向低地址增长：

```asm
push rax  ; RSP -= 8; [RSP] = RAX
pop  rax  ; RAX = [RSP]; RSP += 8
```

栈只是一段由 RSP 指向的内存，没有自动边界。越过底部会写入相邻页，除非用
not-present guard page 把错误变成 #PF。

## 19. `call/ret` 做了什么

64 位近调用可简化为：

```text
call target:
  push address_of_next_instruction
  RIP = target

ret:
  RIP = pop()
```

它不会：

- 自动保存所有通用寄存器。
- 改变 CPL。
- 自动换到 Kernel stack。
- 保存 RFLAGS/CS/SS。

这些由 ABI 或其他硬件入口机制处理。

## 20. ABI 是什么

Application Binary Interface 规定独立编译单元如何在机器级合作：

- 参数寄存器。
- 返回值。
- caller/callee-saved 寄存器。
- stack alignment。
- object layout。
- symbol naming。
- 可执行文件格式的一部分。

API 是源码层接口；ABI 是二进制层接口。函数声明相同但 ABI 不同，链接成功后
仍可能在运行时读错参数。

## 21. System V AMD64 函数调用

常见整数/指针参数顺序：

```text
RDI, RSI, RDX, RCX, R8, R9
```

返回值通常在 RAX。

### 21.1 Caller-saved

调用者若要保留这些寄存器，调用前自己保存：

```text
RAX RCX RDX RSI RDI R8 R9 R10 R11
```

### 21.2 Callee-saved

被调用函数使用后必须恢复：

```text
RBX RBP R12 R13 R14 R15
```

### 21.3 Stack alignment

进入 C++ 函数前必须满足 ABI 对齐。异常入口在保存硬件帧和寄存器后，要根据
精确字节数修正 RSP，不能凭肉眼判断。

### 21.4 Red zone

用户态 ABI 允许 RSP 下方 128 bytes red zone。中断可能在当前内核栈上压入
现场，所以 Kernel/boot 目标使用 `-mno-red-zone`。

## 22. CPU 异常帧不是函数帧

普通 `call` 只压返回 RIP。异常/中断可能由 CPU 压入：

```text
same privilege:
  RIP
  CS
  RFLAGS
  [error code for selected exceptions]

privilege change:
  old SS
  old RSP
  RFLAGS
  old CS
  old RIP
  [error code]
```

具体内存顺序要按 CPU push 方向解读。项目汇编桩为没有硬件 error code 的
向量补一个统一值，再保存通用寄存器，形成固定
[ExceptionFrame](../../../source/kernel/include/os/kernel/arch/exception_frame.hpp)。

返回必须用 `iretq`，普通 `ret` 无法恢复 CS、RFLAGS、SS 和特权级。

## 23. System call ABI 与函数 ABI

用户 C++ wrapper 先遵守 System V 调用：

```text
InvokeSystemCall(number,arg0,arg1,arg2,arg3)
RDI              RSI  RDX  RCX  R8
```

项目 syscall ABI 规定：

```text
RAX number
RDI arg0
RSI arg1
RDX arg2
R10 arg3
```

所以
[system_call.asm](../../../source/user/src/system_call.asm)
必须显式重排，包括 `R8 → R10`。这一步是 ABI adapter，不是业务逻辑。

`int 0x80` 随后由 IDT gate：

- 检查 DPL。
- 从 CPL3 进入 CPL0。
- 使用 TSS.RSP0 换栈。
- 保存硬件返回现场。

## 24. 汇编与 C++ 的边界规则

每个边界都应写出表格：

| 问题 | 必须明确 |
| --- | --- |
| 谁调用谁 | CPU、汇编还是 C++ |
| 入口模式 | 16/32/64 位与 CPL |
| 参数 | 寄存器/栈/结构地址 |
| 栈 | 当前 RSP、alignment、ownership |
| 保存集 | 哪些寄存器由谁恢复 |
| DF/IF | 进入与返回时状态 |
| 返回指令 | `ret`、`iretq`、far transfer 或不返回 |

没有这个表，仅靠“能打印日志”很难发现潜在 ABI 破坏。

## 25. Inline assembly

C++ inline asm 不是把字符串原样插入就结束。编译器必须知道：

- 输入 operands。
- 输出 operands。
- clobbered registers。
- 是否影响 memory。

例如：

```cpp
asm volatile("sti; hlt; cli" : : : "memory");
```

`volatile` 防止删除或任意合并；`"memory"` 告诉编译器内存可见顺序受影响。
它不自动成为跨 CPU 原子协议，也不替代硬件手册。

项目还审计最终反汇编，证明三条指令相邻；源代码字符串只是意图。

## 26. 二进制布局的手工阅读

面对一个结构或协议，按以下顺序写表：

```text
offset | width | endian | meaning | valid range
```

例如 64-byte DirectoryEntry：

```text
0   8  little  inode
8   8  little  type
16  8  little  name length
24 40  bytes   name
```

再检查：

- 总大小是否精确。
- 字段相加是否溢出。
- reserved 是否必须为零。
- 失败时输出是否保持稳定。
- 生产者与消费者是否共享同一 ABI header。

## 27. 常见误解

### 27.1 “指针就是物理地址”

分页开启后，C++ 指针是当前地址空间可解引用的虚拟地址。物理地址必须先映射。

### 27.2 “`volatile` 能解决并发”

`volatile` 主要约束编译器对特殊访问的处理，不提供互斥、原子 read-modify-write
或跨核 happens-before。

### 27.3 “结构大小等于字段大小之和”

编译器可能插入 padding。磁盘/ABI 结构需要固定宽度字段、显式布局约束和
`static_assert`；持久格式最好显式 encode/decode。

### 27.4 “汇编没有未定义行为”

汇编没有 C++ 语言 UB 这个术语，却可能违反 ABI、使用无效 selector、写错误
CR3、越栈或执行特权指令，后果更直接。

### 27.5 “地址能装进 uint64_t 就合法”

还需检查 canonical、映射、权限、ownership、alignment 和对象生命周期。

## 28. 对照项目阅读

1. [地址范围实现](../../../source/foundation/src/address_range.cpp)
2. [端口 I/O](../../../source/kernel/src/device/port_io.cpp)
3. [异常帧布局](../../../source/kernel/src/arch/exception_frame.cpp)
4. [架构汇编入口](../../../source/kernel/src/arch/architecture.asm)
5. [共享 syscall ABI](../../../source/abi/include/os/abi/system_call.hpp)
6. [用户 syscall adapter](../../../source/user/src/system_call.asm)
7. [文件系统显式格式](../../../source/kernel/src/fs/file_system_format.cpp)

## 29. 练习

### 练习 A：位掩码

给定 PTE flags：

```text
Present=bit0, RW=bit1, US=bit2, NX=bit63
```

写出 supervisor read-only executable 与 user read/write non-executable 的
64 位 mask。

### 练习 B：溢出

判断以下半开区间是否可构造：

```text
begin=0xFFFFFFFFFFFFFFF0, size=0x10
begin=0xFFFFFFFFFFFFFFF0, size=0x11
begin=0x1000,             size=0
```

不要先执行加法再判断。

### 练习 C：小端

手工写出 `0x0123456789ABCDEF` 在连续八个低到高地址中的字节。

### 练习 D：栈

RSP 初始为 `0x8000`，依次执行：

```asm
push rax
call target
```

计算 target 入口 RSP，并画出两个 8-byte slot 的含义。

### 练习 E：ABI adapter

给 `InvokeSystemCall(22,3,entryAddress,64,0)` 标注进入 adapter 前和执行
`int 0x80` 前各寄存器值。

### 练习 F：地址分类

解释以下值的领域与单位：

```text
0x3F8
0xFFFF888000001000
2048
0x40000000
512
```

同一数字可能需额外上下文，不允许只回答“一个整数”。

## 30. 通过标准

应能：

- 手工进行基本 bit mask、对齐、半开区间和溢出推导。
- 解释补码、截断、zero/sign extension。
- 区分 PA、VA、port、LBA 和 file offset。
- 读懂 NASM Intel 的寄存器/内存 operand。
- 解释 `call/ret`、`int/iretq` 和 far control transfer 的差异。
- 写出 System V 与项目 syscall ABI 的寄存器映射。
- 说明 CPU 异常帧为何必须由汇编规范化后再交给 C++。

下一册进入
[工具链、ELF 与 freestanding C++](03-toolchain-elf-and-freestanding-cpp.md)。

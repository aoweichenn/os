# Kernel 模块

## 职责

`kernel` 是 Stage 1 最终交接的 freestanding C++20 ELF64 可执行文件。v0.5
已经在真实交接之上建立内核自己的处理器基础：

- 由 Clang 以 `x86_64-unknown-none-elf` 目标编译。
- 由 LLD 的 `elf_x86_64` 模式直接链接，不经过 ARM64 宿主 GCC。
- 入口符号为 C ABI 的 `osKernelEntry`，链接地址为 `0x00100000`。
- 不链接 libc、C++ 标准库、异常、RTTI、栈保护或宿主运行时。
- 入口按 System V AMD64 ABI 从 RDI 接收 BootInfo。
- 内核独立初始化 COM1，不依赖 Stage 1 函数或隐藏状态。
- 入口验证 BootInfo、BSS 清零结果和当前 CR3，再接管 GDT、TSS 和 IDT。
- 32 个架构异常由 NASM 桩规范化为固定 C++ `ExceptionFrame`。
- 可恢复 breakpoint 经 `IRETQ` 返回，其他异常输出固定现场并 panic。
- 验收完成后进入明确的 `HLT` 循环，不返回 Stage 1。

## 文件布局

链接脚本使用独立的 `PT_LOAD` 权限设计：代码为 `R E`，只读数据为 `R`，
可写数据与 BSS 为 `RW`。空输出节不会产生多余加载段。各加载节以 4 KiB
边界对齐，入口必须位于可执行加载段。

## 审计不变量

宿主工具拒绝以下内核：

- ELF 头截断、magic、类别、端序、版本、类型或目标机器错误。
- 程序头表越界或程序头宽度不匹配。
- 没有 `PT_LOAD`、文件长度大于内存长度或文件区间越界。
- 段没有按 4 KiB 对齐，或文件偏移与虚拟地址的页内偏移不一致。
- 目标地址不是初期恒等装载、地址范围溢出或加载段相互重叠。
- 段缺少读取权限、包含未知权限位，或同时可写与可执行。
- 入口不是 `0x00100000`，或不在可执行加载段中。
- 存在未解析运行时符号。
- 缺少描述符装载入口、公共异常入口、C++ 分发器、异常桩表或任意
  `os_kernel_exception_vector_0..31` 符号。

宿主 ELF 审计和 Stage 1 目标机加载器各自实现这些不变量，避免构建工具通过
就被误认为目标代码也正确处理了不可信磁盘数据。

## BootInfo ABI

BootInfo 位于物理地址 `0x14000`，共 80 字节；每个字段都是明确的 64 位
小端整数：

| 偏移 | 字段 | 当前值或语义 |
| ---: | --- | --- |
| `0x00` | magic | `OSBOOT64` |
| `0x08` | version | `1` |
| `0x10` | structure size | `80` |
| `0x18` | Kernel 文件物理地址 | `0x20000` |
| `0x20` | Kernel 精确文件大小 | 来自已校验描述符 |
| `0x28` | Kernel 入口 | `0x100000` |
| `0x30` | `PT_LOAD` 数量 | `1..64` |
| `0x38` | 页表根物理地址 | `0x10000` |
| `0x40` | 恒等映射大小 | 64 MiB |
| `0x48` | 内核栈顶 | `0x3FFF000` |

结构体用 `static_assert` 固定为 80 字节；内核不会因为拿到了非空指针就信任
内容，而是逐字段验证版本、范围和当前启动契约。

## 描述符表契约

### GDT

| 索引 | 选择子 | 内容 | 关键属性 |
| ---: | ---: | --- | --- |
| 0 | `0x00` | 空描述符 | 必须全零 |
| 1 | `0x08` | Ring 0 代码段 | present、execute/read、L=1、DPL=0 |
| 2 | `0x10` | Ring 0 数据段 | present、read/write、DPL=0 |
| 3..4 | `0x18` | 64 位 TSS | available TSS，完整 64 位基址 |

`LGDT` 不会自动刷新 CS 的隐藏属性，因此装载后使用远返回重新载入代码段，
再写 DS、ES、FS、GS、SS。`LTR` 会把 available TSS 描述符硬件类型改为
busy；运行时验证选择子和 TSS 内容，不错误要求内存仍保持初始 type。

### TSS

TSS 精确为 104 字节，I/O bitmap offset 等于结构末端，使当前没有实际 I/O
权限位图。RSP0 取自 BootInfo。IST1、IST2、IST3 指向三个独立 16 KiB
BSS 栈，分别服务双重故障、NMI、机器检查；其余 RSP/IST 保持零。

### IDT

IDT 分配 256 个 16 字节门。向量 0..31 为 present interrupt gate，32..255
保持 not-present。双重故障、NMI、机器检查分别选择 IST1、IST2、IST3。
breakpoint 和 overflow 的门 DPL 为 3，为未来用户态合法软件触发保留架构
语义；其余门 DPL 为 0。

加载后必须同时满足：

- `SGDT` 的 base/limit 指向当前 GDT。
- `SIDT` 的 base/limit 指向完整 4096 字节 IDT。
- CS=`0x08`，SS=`0x10`，`STR`=`0x18`。
- TSS.RSP0、三个 IST 和 I/O bitmap offset 与构造值一致。

## 异常 ABI

处理器对向量 8、10、11、12、13、14、17、21、29、30 自动压入错误码；
其他向量不压。每个无错误码桩先压入 64 位零，随后所有桩压入向量号。公共入口
清 DF 并依次保存通用寄存器，形成 160 字节结构：

```text
低地址
R15 R14 R13 R12 R11 R10 R9 R8
RDI RSI RBP RDX RCX RBX RAX
vector error_code RIP CS RFLAGS
高地址
```

没有发生特权级切换时，硬件不会额外保存旧 RSP/SS；当前结构只声明所有
Ring 0 异常稳定拥有的字段。公共入口在调用 C++ 前向下对齐 RSP，并单独保存
原异常帧指针，返回后严格逆序恢复并执行 `IRETQ`。

## panic 契约

只有向量 3 且规范化错误码为零可以返回。其他异常：

1. 立即 `CLI`。
2. 用 BSS 状态位拒绝递归进入，避免重复日志。
3. 重新初始化 COM1，绕过可能不完整的上层日志状态。
4. 固定输出向量、错误码、RIP、CS、RFLAGS；页故障增加 CR2。
5. 进入 `CLI; HLT` 循环，绝不返回异常入口。

panic 不使用动态分配、格式化库、锁、异常、RTTI 或可失败的构造链。当前只输出
最小充分证据，不在未知栈健康状态下尝试回溯。

## 入口验收序列

成功启动必须依次输出：

```text
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
[OS][KERNEL] FILE_SIZE=0x...
[OS][KERNEL] LOAD_SEGMENTS=0x...
[OS][KERNEL] READY
```

未初始化的全局 64 位探针位于 BSS。只有加载器按 `p_memsz - p_filesz` 清零，
`BSS_ZEROED` 才能出现。CR3 读回值必须等于 BootInfo 中的页表根；这两项把
“段复制完成”和“处理器仍使用约定页表”变成目标机可观测证据。

故障镜像与生产内核共享所有实现，只替换 `osKernelEntry` 选择的注入模式。
`UD2` 必须得到向量 6、错误码 0；访问 `0x04000000` 必须得到向量 14、
错误码 0 和同值 CR2。两者必须输出一次 `PANIC`，且禁止出现文件统计和
`READY`。

## 已知边界

- 当前只处理同步异常，不开放 IF，不发送 PIC EOI；设备中断属于 v0.7。
- 当前 IST 栈位于恒等映射 BSS，没有 guard page；v0.6 建立新页表后补齐。
- panic 只支持单核早期环境；SMP 停核和崩溃转储尚未实现。
- 页故障当前全部 panic；按需映射、用户进程终止和写时复制要等内存与进程模块。

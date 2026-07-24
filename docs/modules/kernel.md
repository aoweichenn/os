# Kernel 模块

## 职责

`kernel` 是 Stage 1 最终交接的 freestanding C++20 ELF64 可执行文件。v0.4
已经完成从磁盘容器到 C++ 入口的真实交接：

- 由 Clang 以 `x86_64-unknown-none-elf` 目标编译。
- 由 LLD 的 `elf_x86_64` 模式直接链接，不经过 ARM64 宿主 GCC。
- 入口符号为 C ABI 的 `osKernelEntry`，链接地址为 `0x00100000`。
- 不链接 libc、C++ 标准库、异常、RTTI、栈保护或宿主运行时。
- 入口按 System V AMD64 ABI 从 RDI 接收 BootInfo。
- 内核独立初始化 COM1，不依赖 Stage 1 函数或隐藏状态。
- 入口验证 BootInfo、BSS 清零结果和当前 CR3，再输出结构化启动证据。
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

## 入口验收序列

成功启动必须依次输出：

```text
[OS][KERNEL] ENTERED
[OS][KERNEL] BOOT_INFO_VALID
[OS][KERNEL] BSS_ZEROED
[OS][KERNEL] CR3_VALID
[OS][KERNEL] FILE_SIZE=0x...
[OS][KERNEL] LOAD_SEGMENTS=0x...
[OS][KERNEL] READY
```

未初始化的全局 64 位探针位于 BSS。只有加载器按 `p_memsz - p_filesz` 清零，
`BSS_ZEROED` 才能出现。CR3 读回值必须等于 BootInfo 中的页表根；这两项把
“段复制完成”和“处理器仍使用约定页表”变成目标机可观测证据。

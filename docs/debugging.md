# 调试记录

## v0.0：工程基线故障记录

### 静态库符号审计误报

`llvm-nm --undefined-only` 会打印静态库成员名称，即使成员没有未解析符号。审计脚本必须过滤空行和以冒号结尾的成员标题，不能把“命令有输出”直接等价为“存在外部依赖”。

### QEMU 测试脚本语法错误

Bash 的 `[[ ... ]]` 条件表达式不能在比较运算符后随意换行。首次测试在执行 QEMU
前就因脚本语法失败。宿主自动化迁移到 Python 后，QEMU 参数、超时和退出状态由
`subprocess` 直接管理，并继续保留无效镜像尺寸的失败路径测试。

### GitHub Actions 缺少 llvm-nm

Ubuntu runner 中安装 `clang` 和 `lld` 不保证存在未版本化的 `llvm-nm`、
`llvm-readelf` 命令。CI 工作流必须同时安装 `llvm` 工具包。Python 工具链检查
应继续在构建前失败，不能静默跳过符号审计。

## v0.1：复位与串口

### 复位向量的地址错觉

复位时不能用普通实模式的 `CS << 4` 解释第一条取指。可见 CS 是
`0xF000`，但隐藏基址为 `0xFFFF0000`；与 IP `0xFFF0` 相加得到
`0xFFFFFFF0`。本项目用 near jump 保留隐藏基址，并把 IP 改为 `0xF000`。

检查最终字节：

```bash
xxd -g1 -s 0x1fff0 -l 16 build/developer/images/firmware.bin
python3 tools/os.py audit-firmware build/developer/images/firmware.bin
```

预期前三个字节是 `e9 0d f0`。

### ROM 中状态不可写

最初的故障注入尝试把“强制串口失败”标志写回 ROM。汇编指令执行了，但 ROM
是只读介质，状态没有改变，故障镜像仍输出 `SERIAL_READY`。修复后把测试状态
保存在 BP 寄存器中，轮询函数读取该寄存器，既不依赖 RAM 初始化，也不违反
ROM 只读属性。

### QEMU 固件不会自行退出

v0.1 没有电源管理和关机协议，完成后进入 `HLT` 是正常结果。自动化测试不能把
“进程未退出”直接当成成功，而是在有界时间后同时检查 QEMU 生命周期和串口
标记。若没有任何标记、缺少就绪标记或出现禁止标记，测试都会失败。

### 使用 GDB 检查第一条指令

```bash
qemu-system-x86_64 \
  -machine pc,accel=tcg -cpu qemu64 -m 64 \
  -nodefaults -display none -serial stdio -monitor none \
  -bios build/developer/images/firmware.bin \
  -S -s
```

另一终端执行：

```gdb
set architecture i386
target remote :1234
x/3bx 0xfffffff0
break *0xfffff000
continue
x/20i $pc
```

## v0.2：IDE PIO 与 Stage 1

### 区分格式错误与传输错误

`IDE_TIMEOUT` 表示状态轮询预算耗尽；`IDE_ERROR` 表示设备返回 ERR 或 DF。
`STAGE1_HEADER_INVALID` 表示扇区已经读入，但描述符字段、范围或整扇区校验
失败；`STAGE1_CHECKSUM_INVALID` 表示负载已经读入，但内容与描述符不一致。
四类错误不能合并，否则无法判断故障发生在硬件事务还是不可信输入验证。

宿主侧先审计镜像：

```bash
python3 tools/os.py audit-stage1 build/developer/images/boot_disk.img
xxd -g2 -l 32 build/developer/images/boot_disk.img
```

### GDB 检查装载结果

使用 v0.1 相同的 QEMU `-S -s` 参数，并把磁盘替换为
`build/developer/images/boot_disk.img`。在 GDB 中：

```gdb
set architecture i386
target remote :1234
break *0xfffff000
continue
x/16hx 0x500
x/32bx 0x8000
break *0x8000
continue
x/12i $pc
```

`0x500` 应以 `OSSTAGE1` 开头；`0x8000` 应与
`source/boot/stage1/generated/stage1.bin` 前缀一致。命中 `0x8000` 后，
CS 应为 `0x0800`、IP 为零，证明远控制转移已经刷新代码段状态。

### PIO 读取后 DI 的推进

`rep insw` 每扇区读取 256 个字，并把 ES:DI 推进 512 字节。v0.2 最多接受
64 个扇区，确保单次负载不让 16 位 DI 回绕。若后续需要更大 Stage 1，必须
显式推进 ES 或切换到更宽的地址模式，不能只放宽描述符上限。

## v0.4：Kernel 磁盘容器

宿主先分别检查 ELF 文件和组合磁盘：

```bash
python3 tools/os.py audit-kernel-elf build/developer/source/kernel/kernel.elf
python3 tools/os.py audit-kernel-image build/developer/images/boot_disk.img
xxd -g1 -s $((65 * 512)) -l 64 build/developer/images/boot_disk.img
```

LBA 65 应以 `OSKERN64` 开头；LBA 66 应以 ELF magic `7f 45 4c 46` 开头。
若文件审计通过但磁盘审计失败，优先比较描述符中的精确文件长度、扇区数和
CRC32；若二者都通过而目标机失败，再检查 ATA 状态、读取缓冲区和 Stage 1
自身 CRC32 实现。

### 区分 Kernel 装载失败

Stage 1 为目标读取和格式验证保留五条互斥失败边界：

| 日志 | 优先检查 |
| --- | --- |
| `KERNEL_ATA_TIMEOUT` | `0x1F7` 的 BSY/DRQ 与轮询预算 |
| `KERNEL_ATA_ERROR` | `0x1F7` 的 ERR/DF 和 `0x1F1` 错误寄存器 |
| `KERNEL_HEADER_INVALID` | `0x13000` 的字段、保留区和描述符 CRC |
| `KERNEL_CHECKSUM_INVALID` | `0x20000` 起精确文件 CRC 与最后扇区补零 |
| `KERNEL_ELF_INVALID` | ELF 头、程序头、地址、权限、重叠和入口 |

调试时不要先修改失败标记或放宽边界；先使用构建生成的对应失败镜像确认该分支
仍然可达。

### GDB 检查两遍 ELF 装载

以 `-S -s` 启动正常镜像后，可以检查固定物理布局：

```gdb
set architecture i386:x86-64
target remote :1234
x/8gx 0x13000
x/16bx 0x20000
x/10gx 0x14000
x/16i 0x100000
x/gx 0x102000
info registers cr3 rsp rdi rip
```

- `0x13000` 应以 `OSKERN64` 开头。
- `0x20000` 应以 ELF magic 开头。
- `0x14000` 的十个 64 位字段应与 BootInfo 文档一致。
- `0x100000` 是入口代码；当前 BSS 探针位于 RW 段，交接前必须为零。
- 内核入口处 CR3 应为 `0x10000`，RDI 应为 `0x14000`，RSP 位于独立栈区。

宿主侧可用以下命令对照程序头和入口：

```bash
llvm-readelf -h -l build/developer/source/kernel/kernel.elf
llvm-nm --undefined-only build/developer/source/kernel/kernel.elf
python3 tools/os.py qemu-firmware \
  build/developer/images/firmware.bin \
  build/developer/images/boot_disk.img \
  131072 1048576 --expected-outcome success
```

当前成功产物为 28,168 字节、56 个扇区，包含 3 个 `PT_LOAD`；这些数字会随
代码变化，因此调试判断应以构建产物和 BootInfo 日志为准，而不是硬编码到测试
逻辑。

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
- `0x14000` 的十三个 64 位字段应与 BootInfo v2 文档一致。
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

## v0.5：描述符表、异常帧与 panic

### 区分装载器 GDT 与内核 GDT

Stage 1 的 `GDT_READY` 只证明模式切换表有效；内核的 `GDT_READY`、
`TSS_READY`、`IDT_READY` 表示内核已构造自己的对象。最终
`DESCRIPTOR_TABLES_VALID` 还要求处理器回读状态与内存对象一致，不能用前三条
构造日志替代。

用 GDB 在 `osKernelDispatchException` 停止：

```gdb
set architecture i386:x86-64
target remote :1234
break osKernelDispatchException
continue
info registers rip rsp cs ss
x/20gx $rdi
```

正常镜像第一次命中来自 `INT3`。`$rdi` 指向 `ExceptionFrame`，从低地址开始
依次是 R15..RAX、vector、error code、RIP、CS、RFLAGS；第 16 个八字节应为
向量 3，第 17 个应为零。`INT3` 是 trap，保存 RIP 指向下一条指令，因此直接
`IRETQ` 可以继续。

检查表寄存器与 TSS：

```gdb
maintenance packet qRcmd,696e666f20726567697374657273
info registers
disassemble osKernelLoadGdtAndTss
disassemble osKernelExceptionDispatch
x/5gx &kernelGlobalDescriptorTable
x/32gx &kernelInterruptDescriptorTable
```

不同 GDB 版本不一定直接显示 GDTR、IDTR 和 TR；项目因此把 `SGDT`、`SIDT`、
`STR` 封装为可下断点的汇编函数，串口回读验证是跨版本的主要证据。

### 非法指令与页故障镜像

不使用宿主 `timeout` 直接运行，优先让项目工具管理 QEMU 生命周期：

```bash
python3 tools/os.py qemu-firmware \
  build/developer/images/firmware.bin \
  build/developer/images/kernel_invalid_opcode/boot_disk.img \
  131072 1048576 --expected-outcome kernel-invalid-opcode

python3 tools/os.py qemu-firmware \
  build/developer/images/firmware.bin \
  build/developer/images/kernel_page_fault/boot_disk.img \
  131072 1048576 --expected-outcome kernel-page-fault
```

非法指令的向量必须为 6。页故障必须同时满足：

```text
EXCEPTION_VECTOR=0x000000000000000E
EXCEPTION_ERROR_CODE=0x0000000000000000
PAGE_FAULT_ADDRESS=0x0000000004000000
PANIC
```

错误码零表示 supervisor read 访问了 not-present 页；`0x04000000` 正好是
64 MiB 恒等映射之后的首地址。如果向量正确而 CR2 不同，检查故障注入地址；
如果没有任何 panic 日志，优先检查 IDTR limit、门 present 位、代码选择子和
异常桩表地址；若 QEMU 直接复位，则检查异常路径自身是否又触发异常，尤其是
栈映射、TSS/IST 和 `IRETQ` 布局。

### 统一帧偏移错误的典型症状

| 症状 | 优先检查 |
| --- | --- |
| vector 看起来像寄存器值 | 汇编保存顺序与 C++ 字段顺序不一致 |
| 只有带错误码异常失败 | 桩错误地又压入一个占位错误码 |
| breakpoint 日志后立即 #GP | 恢复时没有丢弃 vector/error 两个槽 |
| C++ 入口行为随机 | 调用前 RSP 没有按 System V AMD64 对齐 |
| 页故障日志递归刷屏 | panic 路径再次访问无效内存，或不可重入状态未生效 |

生产内核仍包含 3 个 `PT_LOAD`。RW 段的文件长度可以小于内存长度，差值来自
IDT、TSS、IST 栈、页帧状态和其他 BSS；这是 ELF BSS 的正常表现，不是镜像
漏写。精确文件字节和扇区数随实现变化，应读取当前构建日志。

## v0.6：内存图、页帧与内核页表

### 检查 Stage 1 规范化的内存图

`fw_cfg` 文件目录的计数、size 和 selector 都是大端字段，而 `etc/e820`
内容按 x86 小端存放。若 `MEMORY_MAP_INVALID` 出现在 `LONG_MODE` 之后，
优先分别检查这两个端序边界，不要把整个目录按一种端序解释。

在 Kernel 交接前用 GDB 查看：

```gdb
set architecture i386:x86-64
target remote :1234
x/4gx 0x16000
x/32bx 0x17000
x/12gx 0x18000
x/13gx 0x14000
```

- `0x16000` 首个 64 位值是内存图条目数。
- `0x17000` 在目录遍历时保存当前 56 字节文件名，完成后内容不属于 ABI。
- `0x18000` 每 24 字节一项，顺序为 base、length、type/attributes。
- `0x14000` 的最后三项应为 `0x18000`、条目数和 `24`。

当前 `qemu64 -m 64` 可能同时报告低 64 MiB RAM 和一个很高的保留 MMIO
窗口，所以 `MEMORY_DESCRIBED_BYTES` 大于 64 MiB 并不表示分配器管理了同等
RAM。判断容量应分别看 `MEMORY_USABLE_BYTES` 和 `MEMORY_MANAGED_BYTES`。

### 在 CR3 切换前后检查页表

在 `activatePageTable` 下断点：

```gdb
break os::kernel::activatePageTable
continue
info registers cr0 cr3
x/512gx $rdi
stepi
info registers cr0 cr3
```

符号名若被 C++ 修饰，可先用
`llvm-nm -C build/developer/source/kernel/kernel.elf | rg activatePageTable`
找到地址。函数参数 RDI 是新 PML4 的物理地址；切换前 CR3 为 Stage 1 的
`0x10000`，切换后必须等于日志中的 `PAGING_ROOT`。CR0 位 16 应为 1，
IA32_EFER 位 11 应为 1。

页表项物理地址字段使用 `0x000FFFFFFFFFF000` 掩码。沿 PML4→PDPT→PD→PT
四次取索引后，叶项 bit 0 是 present、bit 1 是 writable、bit 2 是 user、
bit 63 是 NX。若切换 CR3 立刻 triple fault，优先检查当前 RIP、RSP、GDT、
IDT、页表页本身和串口端口访问所需地址是否在新表中，而不是先放宽权限。

### 写保护故障是权限执行证据

```bash
python3 tools/os.py qemu-firmware \
  build/developer/images/firmware.bin \
  build/developer/images/kernel_write_protection/boot_disk.img \
  131072 1048576 --expected-outcome kernel-write-protection
```

预期关键现场：

```text
FAULT_INJECTION=WRITE_PROTECTION
EXCEPTION_VECTOR=0x000000000000000E
EXCEPTION_ERROR_CODE=0x0000000000000003
PAGE_FAULT_ADDRESS=0xFFFF800000100000
PANIC
```

错误码 `0x3` 是 P=1 与 W/R=1：页存在，但 supervisor 写违反只读权限。
如果得到错误码 `0x2`，测试页没有 present；若写入成功，检查 CR0.WP 是否
确实为 1，以及上级和叶级 RW 位是否错误开放。只读取软件页表项不能替代这条
处理器执行测试。

### Guard page 与早期堆

初始栈 guard 位于 `kernelStackTop - 64 KiB`，每个 IST 存储块的第一页也是
guard。它们应由 `queryPage` 返回 `NotMapped`；IST 顶仍指向随后 16 KiB
可写栈的末端。高半区堆从 `0xFFFF800000000000` 开始，共 16 页，全部 RW/NX。

若 `HEAP_SELF_TEST_PASSED` 缺失：

1. 先看是否已有 `MEMORY_INITIALIZATION_FAILED=0x...`，按状态枚举定位阶段。
2. 检查 16 个后备帧是否均已分配和映射，首项物理地址不能为零。
3. 检查 16 字节与 4 KiB 对齐计算是否发生溢出或越过 64 KiB 容量。
4. 在两个分配地址观察写入模式 `0x13579BDF2468ACE0` 与
   `0xC001D00DC0FFEE11`，避免把页表成功误判为堆对象可写。

## v0.7：PIC、PIT、PS/2 与 ATA

### IF=1、HLT=1 但没有时钟

先抓取 QEMU monitor 的 `info registers`、`info pic` 和 `info irq`。若
RFLAGS.IF=1、CPU HLT=1、PIC IRR 的 bit0=1，但没有向量 32 入口，问题在
PIC 到 CPU 的路由，不在 PIT。自研固件没有传统 BIOS 替本地 APIC配置虚拟线；
内核必须先把 LAPIC MMIO 页映射为 RW/NX/PCD，保持全局启用，设置
SVR 软件启用，并把 LVT LINT0 配为未屏蔽的 ExtINT。正常串口应先出现
`LEGACY_INTERRUPT_ROUTING_READY`，否则检查 `IA32_APIC_BASE` 的 x2APIC
位、MMIO 映射权限以及 SVR/LINT0 回读。

若 PIC IRR 没有 bit0，检查：

1. `0x43` 是否写入 `0x34`（通道 0、低高字节、模式 2）。
2. `0x40` 是否按低字节、高字节顺序写入 `0x04A9`。
3. master IMR 是否为 `0xFC`，而不是仍为 `0xFF`。
4. IDT 向量 32 是否 present、selector 是否为 `0x08`。

`info irq` 证明设备产生过边沿，`info pic` 的 IRR/ISR/IMR 证明控制器状态，
串口 `TIMER_TICKS` 才证明来宾处理并确认了 IRQ。三类证据不能互相替代。

### 本地 QEMU 通过而 CI 停在 `INTERRUPTS_ENABLED`

先增加宿主截止时间只能检验“是否调度较慢”，不能修复来宾路由。本项目曾在
QEMU 10.2 通过直接关闭 LAPIC 的方案，但 QEMU 8.2 即使等待五秒仍停在相同
位置；PIC IRR、IF 和 HLT 状态进一步排除了 PIT、IDT 与宿主速度。最终修复是
显式建立 LAPIC LINT0 ExtINT virtual-wire。

测试运行器仍采用里程碑驱动收尾和五秒失败上界。这个机制让失败分类更准确，
但验收以真实 `TIMER_TICKS`、`READY` 与键盘事件为准，绝不能把延长超时当作
硬件路径正确的证据。

### IRQ 后 triple fault

用 `-d int,cpu_reset -D qemu.log` 观察最后一次向量。优先核对 IRQ 桩是否先压
零错误码、再压向量，公共入口是否保存/恢复 15 个寄存器并在 `IRETQ` 前丢弃
两个槽。异常和 IRQ 帧形状相同，但分发函数必须不同；若把 IRQ 送入 panic
分发器，会把正常外部事件误判为异常。

### 键盘初始化或注入失败

`PS2_KEYBOARD_READY` 缺失时按顺序检查 i8042：

- 写控制器命令前 status bit1 必须清零。
- 读配置或 ACK 前 status bit0 必须置一。
- 配置 byte 应打开 IRQ1/translation、关闭 IRQ12。
- `0xF4` 必须收到 `0xFA`，其他字节不能当作成功。

QEMU 系统测试在 `READY` 后才用 QMP `sendkey a`，以免把初始化 ACK 与扫描码
混在同一输出缓冲。看到 IRQ1 计数却没有 `A_PRESSED` 时，检查收到的是集合 1
`0x1E/0x9E`，还是翻译未开启导致的其他集合编码。

### ATA 自检失败

内核写 `nIEN` 后使用 alternate status 轮询，不接收 IRQ14。`BSY` 超预算、
`ERR/DF`、`DRQ` 缺失是不同状态；不要把所有失败折叠成“磁盘不可用”。若读取
成功但 magic 错误，检查目标是否为 primary master LBA 0，以及 256 个 16 位
DATA 字是否按小端拆成 512 字节。

### QEMU 捕获器为什么使用里程碑和总截止

先看带宿主时间戳的最后一行。一次固定墙钟预算耗尽不能单独判定 PIT 或 PIC
错误；但扩大预算后总在同一来宾标记停顿，就应检查硬件状态与模拟器版本差异，
不能继续用“runner 较慢”解释。无限延长超时同样会掩盖真实停滞。

QEMU 捕获器因此使用两个边界：

1. 逐行观察当前用例的最后一个必需里程碑；到达后保留短暂收尾窗口并回收进程。
2. 未到达时以五秒为总失败上界；QMP 等待 `READY` 使用同一预算。

协议校验仍在进程结束后检查所有必需标记的顺序和全部禁止标记。这个设计既移除
“所有宿主都同速”的假设，也让稳定停顿成为可重复诊断证据；不会把缺失 IRQ、
缺失按键或 panic 误判为成功。Python 工具单元测试另用一个输出 `READY` 后
睡眠的子进程，证明观察者能够提前、完整且无残留地结束捕获。

# 调试记录

## 当前 VGA 控制台

目标系统不再产生串口日志。交互观察使用 `python3 tools/os.py qemu-display ...`；
自动系统测试在 `-display none` 下仍实例化 VGA 设备，并通过 QMP 读取
`0x20000..0x9FFFF` 的追加式验收区。

无桌面宿主可使用 `--display-backend vnc`。该模式固定绑定回环地址，手机访问应
先经 noVNC 转成回环 HTTP/WebSocket，再通过 Tailscale Serve 等已认证的私有
转发完成；不能把无认证 VNC 或 noVNC 直接绑定到 `0.0.0.0`。

若窗口显示 `guest has not initialized the display`，依次检查：

1. QEMU 命令是否同时包含 `-device VGA` 和非 `none` 的 display backend；
2. ROM 是否到达 `os_firmware_initialize_vga_console`；
3. `0x3C0..0x3DF` 模式寄存器、字符平面和 `0xB8000` 文本页是否写入；
4. 共享区 magic 是否为字节 `OSVG`、版本是否为 2；
5. overflow 是否仍为零，trace length 是否不超过 `0x7FFE0`。

屏幕只有最后 25 行属于正常滚屏。若自动化报告缺少早期标记，应检查追加区是否
被 Stage 1 覆盖、Kernel 是否保留低端 supervisor identity mapping，以及
runner 的 `pmemsave` 是否从 16 KiB 起按需扩展、最终覆盖全部已提交字节。

若 VNC 已连接但画面全黑，先用 QMP `screendump` 判断 QEMU 原始扫描画面是否也
全黑，再读取 `0xB8000`。显存有属性 `0x07` 的文本而截图全黑时，检查 ROM 是否
通过 `0x3C6/0x3C8/0x3C9` 提交了 DAC mask、索引和 RGB 分量；不能依赖未执行的
VGA BIOS 遗留调色板。

Termux 同机访问时若完整 noVNC 控制栏在手机浏览器中不可见，使用
`tools/novnc_mobile.html`。该页面把普通 HTML 输入框中的 ASCII 命令转换成 RFB
按键并自动追加 Enter，控制按钮直接发送 PS/2 可识别的 Backspace、Ctrl-C 与
Ctrl-Z；不依赖隐藏侧栏或 Android WebView 自动弹出键盘。

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
| `KERNEL_ATA_TIMEOUT` | `0x1F7` 的 BSY/DRQ 与 `0x000FFFFF` 次轮询预算 |
| `KERNEL_ATA_ERROR` | `0x1F7` 的 ERR/DF 和 `0x1F1` 错误寄存器 |
| `KERNEL_HEADER_INVALID` | `0x13000` 的字段、保留区和描述符 CRC |
| `KERNEL_CHECKSUM_INVALID` | `0x03E00000` 起精确文件 CRC 与最后扇区补零 |
| `KERNEL_ELF_INVALID` | ELF 头、程序头、地址、权限、重叠和入口 |

调试时不要先修改失败标记或放宽边界；先使用构建生成的对应失败镜像确认该分支
仍然可达。

### GDB 检查两遍 ELF 装载

以 `-S -s` 启动正常镜像后，可以检查固定物理布局：

```gdb
set architecture i386:x86-64
target remote :1234
x/8gx 0x13000
x/16bx 0x03e00000
x/10gx 0x14000
x/16i 0x100000
x/gx 0x102000
info registers cr3 rsp rdi rip
```

- `0x13000` 应以 `OSKERN64` 开头。
- `0x03E00000` 应以 ELF magic 开头。
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
  131072 137438953472 --expected-outcome success
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

用 GDB 在 `OsKernelDispatchException` 停止：

```gdb
set architecture i386:x86-64
target remote :1234
break OsKernelDispatchException
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
disassemble OsKernelLoadGdtAndTss
disassemble OsKernelExceptionDispatch
x/5gx &kernel_global_descriptor_table
x/32gx &kernel_interrupt_descriptor_table
```

不同 GDB 版本不一定直接显示 GDTR、IDTR 和 TR；项目因此把 `SGDT`、`SIDT`、
`STR` 封装为可下断点的汇编函数，串口回读验证是跨版本的主要证据。

### 非法指令与页故障镜像

不使用宿主 `timeout` 直接运行，优先让项目工具管理 QEMU 生命周期：

```bash
python3 tools/os.py qemu-firmware \
  build/developer/images/firmware.bin \
  build/developer/images/kernel_invalid_opcode/boot_disk.img \
  131072 137438953472 --expected-outcome kernel-invalid-opcode

python3 tools/os.py qemu-firmware \
  build/developer/images/firmware.bin \
  build/developer/images/kernel_page_fault/boot_disk.img \
  131072 137438953472 --expected-outcome kernel-page-fault
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

在 `ActivatePageTable` 下断点：

```gdb
break os::kernel::ActivatePageTable
continue
info registers cr0 cr3
x/512gx $rdi
stepi
info registers cr0 cr3
```

符号名若被 C++ 修饰，可先用
`llvm-nm -C build/developer/source/kernel/kernel.elf | rg ActivatePageTable`
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
  131072 137438953472 --expected-outcome kernel-write-protection
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

### Buddy 初始化或连续块自检失败

若没有 `BUDDY_ALLOCATOR_READY`，先读取
`MEMORY_INITIALIZATION_FAILED=0x...` 并对照
`KernelMemoryInitializationStatus`：

1. `BuddyAllocatorConfigurationFailed` 表示合并元数据区虽然完成选址，但 buddy
   子区指针或长度没有被分配器接受；
2. `BuddyAllocatorInitializationFailed` 常见原因是双位图小于按最高 PFN 计算的
   精确尺寸，或在冻结启动保留前已经产生 legacy allocated 页；
3. 若 `FRAME_STATE_STORAGE_BYTES` 正常而 `BUDDY_STORAGE_BYTES` 明显小于两倍
   各阶位图和，检查页对齐、尺寸加法溢出和元数据区间切分；
4. 初始化前最后一个允许修改所有权的动作必须是整体
   `ReserveFrameAllocatorMetadata`，初始化后 `ReserveRange` 应明确返回冻结状态。

若已有 `BUDDY_ALLOCATOR_READY` 但没有 `BUDDY_SELF_TEST_PASSED`：

1. 检查 `BUDDY_SELF_TEST_ORDER` 是否为 3，地址是否按 32 KiB 对齐；
2. 32 GiB 配置的地址必须高于 4 GiB；64 MiB 配置允许位于普通低端 RAM；
3. 在 direct-map 的首页和末页观察模式 `0x4255444459464952` 与
   `0x42554444594C4153`，区分“找到连续 PFN”与“映射可真实写入”；
4. 若写回成功但释放失败，检查 allocated 位图是否在原 order 和原块首置位，
   不要把块内第二页当成 order 0 页释放；
5. 若 `ValidateBuddy` 返回 `CorruptedState`，依次检查同块 free/allocated
   双重置位、祖先/子块重叠、未合并同阶伙伴、块内 2-bit 页状态和加权计数。

`BUDDY_ACTIVE_BLOCKS` 在日志中非零是正常现象，它包含内核仍持有的页表和 heap
后备页。判断泄漏应比较某段生命周期前后的活动块与页数，而不是要求全局为零。
32 GiB 下完整校验会扫描最高 PFN 覆盖的位图和页状态，因此
`[QEMU][T+......ms]` 可显示这一阶段比 64 MiB 明显更长；这不应通过删除校验
或降低管理容量来掩盖。

### Guard page 与早期堆

初始栈 guard 位于 `kernel_stack_top - 64 KiB`，每个 IST 存储块的第一页也是
guard。它们应由 `QueryPage` 返回 `NotMapped`；IST 顶仍指向随后 16 KiB
可写栈的末端。高半区堆从 `0xFFFF800000000000` 开始，共 16 页，全部 RW/NX。

若 `HEAP_SELF_TEST_PASSED` 缺失：

1. 先看是否已有 `MEMORY_INITIALIZATION_FAILED=0x...`，按状态枚举定位阶段。
2. 检查 16 个后备帧是否均已分配和映射，首项物理地址不能为零。
3. 检查 16 字节与 4 KiB 对齐计算是否发生溢出或越过 64 KiB 容量。
4. 在两个分配地址观察写入模式 `0x13579BDF2468ACE0` 与
   `0xC001D00DC0FFEE11`，避免把页表成功误判为堆对象可写。

若类型缓存日志缺失，先注意日志的事务边界：`TYPE_CACHE_READY` 和全部统计只在
整个内存初始化成功后统一输出。因此缓存自检失败时通常只能看到
`MEMORY_INITIALIZATION_FAILED`，不会留下一个看似成功的 `TYPE_CACHE_READY`。
在 GDB 中对 `KernelFixedObjectCache::Initialize`、`TryAcquire`、
`TryRelease` 和 `Destroy` 设置断点，再按以下顺序定位：

1. 代表对象尺寸、对齐和槽步长应均为 `0x40`，容量为 `0x20`；位图需要 4
   字节，对象区向 64 字节对齐，因此后备总长应为 `0x840`。
2. 第一次成功申请应返回槽 0，连续 32 次申请得到互不相同且低 6 位为零的
   地址；第 33 次必须返回 `OutOfObjects`，不能改写传入输出指针。
3. 活动时在每槽索引 0/7 观察基于 `0x5459504543414348` 和
   `0x53454C4654455354` 的模式。释放后槽首会合法地改写为空闲链索引，不能
   再把该 8 字节当成旧对象数据。
4. 先释放偶数槽、再释放奇数槽后，链头应为槽 31；下一次申请必须复用槽 31。
   若返回其他槽，检查释放是否在验证位图前修改了链头。
5. `ObjectNotActive` 必须在重复释放写入空闲链前返回。若同一地址随后被交付
   两次，优先检查位图字节/bit 计算和释放提交顺序。
6. 销毁前应有 active=0、free=32、allocations=releases=33、peak=32；
   销毁后通用堆当前占用和活动申请应回到进入缓存自检前，最大空闲负载恢复
   `0xFFD0`。峰值是累计水位，不要求回落。

若 `Validate` 报告损坏，先区分位图观察数和空闲链观察数：前者错误通常来自
bit 索引或尾位，后者错误通常来自槽内 next 索引、断链或环。校验遍历以容量
为硬上界，不能通过移除上界来“修复”循环。

### KVA 初始化、保护页或回收失败

KVA 日志也遵循事务边界：只有虚拟区间分配器初始化、预留、自检和最终一致性
校验全部通过，`RunKernel` 才统一输出 `KVA_ALLOCATOR_READY`、统计与
`KVA_SELF_TEST_PASSED`。中途失败只会出现
`MEMORY_INITIALIZATION_FAILED=0x...`，不会留下一个可能被误读为已提交的
`KVA_ALLOCATOR_READY`。

先用 `KernelMemoryInitializationStatus` 区分两个边界：

1. `KvaInitializationFailed` 表示 32 TiB 窗口、1024 项描述符存储或永久预留
   首页没有形成合法初态；优先检查基址 `0xFFFFC90000000000`、容量
   `0x0000200000000000`、页对齐、canonical 上界和 BSS 描述符是否清零；
2. `KvaSelfTestFailed` 表示分配器已经建立，但“虚拟区间 → buddy 物理块 →
   页表映射 → 真实访存 → 逆序回收”的某一步没有闭环。此时在
   `KernelVirtualAddressAllocator::TryAllocate`、`MapPage`、`QueryPage`、
   `UnmapPage` 和 `TryReleaseBlock` 设置断点，比先扩大容量更有效。

整机自检有意先做一次单页暖机，但它现在不是泄漏规避。暖机地址是 KVA 窗口
第二页：映射会新建共享 PDPT、PD 和 PT，撤销必须回收 PT 与 PD，只保留仍
可能被进程 PML4 引用的共享 PDPT。预热数据帧和 KVA 区间也立即归还。随后
主事务申请 6 页、按 8 页对齐，因此地址必须为
`0xFFFFC90000008000`。相对页 0 与 5 始终是 not-present guard，中间 4 页
映射到一个 buddy order-2 连续块，权限必须为 supervisor、RW、NX。

按以下顺序缩小故障范围：

1. `TryAllocate` 失败时，检查活动描述符是否严格按地址递增、区间是否互不
   相交、best-fit 间隙和“虚拟页号”绝对对齐是否正确。对齐不能只计算相对
   窗口偏移；
2. guard 被 `QueryPage` 查为 present 时，检查映射循环是否错误地覆盖 6 页，
   或把“已分配 KVA”误解为“已经存在 PTE”。KVA 所有权本身绝不能设置页表项；
3. 中间页查询失败时，逐级检查 PML4E/PDPTE/PDE/PTE 的 present、RW、US 与
   NX；四个叶项物理地址必须连续且从同一 order-2 块首开始；
4. 数据写回失败时，先确认访问的是 direct-map 可达的物理后备和当前 CR3，
   再检查叶权限；不要用软件查询成功替代处理器真实 load/store；
5. 回收必须按“撤销四个叶映射 → 归还整个 order-2 块 → 释放六页 KVA”逆序
   执行。`UnmapPage` 后必须使本 CPU 对应 TLB 项失效，物理页不能在旧翻译
   仍可使用时提前交给 buddy；
6. 主事务结束时，buddy 活动块/页和 KVA 活动分配/页必须回到暖机后的基线。
   页表摘要必须是 PT=2、PD=2、PDPT=0、保留共享 PDPT=1；主事务相对 buddy
   基线的成功申请/释放差额是 3 次，即 order-2 数据块加一张 PT 和一张 PD。

若 `KernelVirtualAddressAllocator::Validate` 返回损坏，先从活动描述符前缀
重新计算 allocated、reserved、free 和 largest gap，再与统计比较。1024 项
容量中位于活动前缀之后的所有未用槽必须全零；相邻区间可以紧贴，但不能倒序
或重叠。释放只接受
原申请的精确起始地址与页数，内部地址、错误长度和 reservation 都必须在任何
状态修改前失败。

### 页表空分支回收或映射回滚失败

先确认管理器根类型与调用场景匹配。正式内核根必须是 `KernelShared`，临时
完全独占测试根使用 `Exclusive`，进程根只能是 `Process`。根类型错误不是
性能问题：把共享根误标为独占会释放仍被进程 PML4 项引用的 PDPT；把独占根
误标为共享则会留下本可回收的表帧。

`PageTableStatus` 将结构故障分开报告：

- `SharedBranchMutationDenied`：进程根试图修改复制来的内核或其他借用分支；
- `InvalidTableFrame`：表地址越界、未对齐、自引用或形成祖先环；
- `TableFrameNotOwned`：父项指向的帧不是分配器记录的精确 order-0 allocation；
- `FrameReleaseFailed`：预检通过后，分配器拒绝释放待回收表帧；
- `RollbackFailed`：映射失败后的父项恢复或新表释放没有完整闭环。

撤销采用“先只读预检、后提交”。若失败后 PTE 或页帧统计已经改变，故障在
预检和提交边界；若成功却没有回收，逐级检查目标叶之外的 511 个原始 64 位
表项是否全部为零。不能只查 Present 位，因为非 Present 项中的软件位或地址
仍然属于表状态。相邻叶共享 PT 时，只有撤销最后一个叶才应回收该 PT。

用 GDB 检查某个 4 KiB 映射时，按 `CalculatePageTableIndices` 得到
PML4/PDPT/PD/PT 四个索引，从 CR3 根逐级读取父项：

```text
parent entry address = table virtual address + index * 8
child physical       = entry & 0x000FFFFFFFFFF000
```

每深入一级都同时核对物理地址范围、4 KiB 对齐和 frame allocator 中的
order-0 ownership；不要只因为内存可读就认定它是页表。回收成功后，先确认
父项已经清零，再确认子表帧变回 free。TLB 只对目标叶执行一次 `INVLPG`，
中间表没有独立线性地址翻译可供失效。

映射故障注入若留下空表，检查 `TableMutation` 的记录顺序：父项原值必须在
修改之前保存，新表按创建反序释放，因 user 页而提升的既有父项 U/S 位必须
恢复。最终整机只在全部事务提交后输出：

```text
PAGE_TABLE_RECLAIMED_LEVEL1_TABLES=0x0000000000000002
PAGE_TABLE_RECLAIMED_LEVEL2_TABLES=0x0000000000000002
PAGE_TABLE_RECLAIMED_LEVEL3_TABLES=0x0000000000000000
PAGE_TABLE_RETAINED_SHARED_LEVEL3_TABLES=0x0000000000000001
PAGE_TABLE_RECLAIM_SELF_TEST_PASSED
```

缺失通过标记时先找 `MEMORY_INITIALIZATION_FAILED`，不要通过延长 QEMU
超时掩盖已经确定返回的页表状态。

### 动态内核栈创建、切换或安全点回收失败

动态栈管理器在 KVA 自检之后初始化，但只在进程运行时开始申请栈。若
`PROCESS_RUNTIME_READY` 之后没有 `KERNEL_STACK_MANAGER_READY`，先检查
`InitializeProcessRuntime` 是否因管理器完整校验或非零活动栈失败；若已出现
管理器配置而首个 `USER_ELF_VALID` 缺失，检查 `CreateProcess` 返回的
`KernelStackFailure`。

一个正常栈必须同时满足：

```text
lower guard              QueryPage == NotMapped
四个 data page           4 KiB / supervisor / writable / NX
upper guard == stack top QueryPage == NotMapped
KVA range                精确六页活动 Allocation
physical frames          四个非零、页对齐、互不重复的 order-0 帧
```

建议按以下顺序定位创建问题：

1. 在 `KernelStackManager::TryCreate` 观察六页候选 KVA。窗口首页永久保留，
   第一栈 lower guard 通常从窗口第二页开始；地址必须 canonical 且页对齐；
2. 若返回 `VirtualRangeNotClear`，逐页查询候选范围。KVA 已空闲但 PTE 仍
   present 表示上一个所有者违反了“先 unmap、后释放 KVA”的顺序，不能通过
   跳过预检继续复用；
3. 若返回 `FrameAllocationFailed`，确认失败前已取得的帧按逆序清零并释放，
   目标槽保持全零，KVA 活动页恢复原值；
4. 若返回 `PageMappingFailed` 或 `MappingValidationFailed`，逐级检查共享
   高半 PML4/PDPT/PD/PT 的 present、RW、US 和 NX。所有上级必须允许写，
   但 U/S 必须为零；
5. `OwnsAllocation` 为假而 PTE 正常，说明 KVA 所有权被外部提前释放。这是
   跨层损坏，`TryDestroy` 必须拒绝，不能先撤销映射再试图修补描述符；
6. 创建提交后统计应增加一栈、四映射页和两 guard 页；累计创建只在最终
   提交时增加，回滚失败不得伪装成成功。

能创建但第一次进入 Ring 3 即故障时，核对 `PROCESS_KERNEL_STACK_*` 三个
地址、TSS.RSP0 和保存帧：

- `stack_top` 与 `upper_guard` 数值相同；
- 176 字节 `UserPrivilegeFrame` 必须完整位于最后一个数据页；
- 当前进程 CR3 查询四个数据页必须得到与内核 CR3 相同的物理身份；
- 切换进程时 CR3 与 TSS.RSP0 必须作为同一次派发提交，不能只更新其一；
- 用户 RSP 永远不能作为 Ring 0 入口栈。

终止后出现 Ring 0 `#PF` 时，优先怀疑在当前栈上提前销毁。正确顺序是：

```text
标记 Terminated
  → 切回内核 CR3 / 销毁用户地址空间
  → 调度另一进程，或汇编恢复永久启动栈
  → C++ 安全点读取当前 RSP
  → 销毁所有不包含当前 RSP 的终止栈
```

在 `ReapTerminatedKernelStacks` 断点检查当前 RSP。它若仍落在目标四页映射区，
函数必须失败而非 unmap。`OsKernelEnterScheduledProcess` 返回后 CR3 必须是
永久内核根；否则即使当前 RSP 安全，也可能在进程独占页表已释放后继续访问。

正常 v1.10 四十五个进程生命周期结束摘要应为 active=0；相对进程启动前的
creations/destructions 增量均为 11；并发峰值由内存档位的 Process 容量
约束，peak-mapped 必须与每个活动栈四页的几何一致，并出现
`KERNEL_STACK_RESOURCES_RECLAIMED`。若创建/销毁相等但页帧或 KVA 基线不同，
分别检查数据页清理和区间精确释放；若活动栈不为零，检查无 Ready 的 idle
返回和最终完成路径是否都调用了安全点。用户 `#UD`、用户 `#PF` 镜像各有
一栈，也必须出现同一回收标记。

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

### IRQ14 请求提交后不完成

先区分 early polling 与 runtime IRQ 路径。若 `ATA_IRQ14_READY` 缺失，检查
请求队列是否在 STI 前初始化，以及 PIC mask 是否为 `0xBFF8`：master bit 2
和 slave bit 6 缺一都收不到 IRQ14。

若 IRQ14 计数增长但请求不完成，按操作检查状态阶段：

- Read 要求 IRQ 时 DRQ=1，再从 data port 读 256 个 word；
- Write 第一次 DRQ IRQ 负责写 256 个 word，之后还需完成 IRQ；
- Flush 没有数据阶段，只等待 BSY 清除且 ERR/DF 均为零。

若 PIT 报 TimedOut，确认 ResolveTimeout 先冻结结果再执行 SRST；不要在设备
仍 BSY 时直接签发下一请求。迟到 IRQ 返回 RequestAlreadyResolved 属于可诊断
竞争，不能覆盖 TimedOut。

若 shared alias 可见但 sync 后文件未更新，检查顺序是否为“重新写保护 PTE
→ Dirty/Error 写回 → VFS sync → ATA FLUSH”。Error 页不得被 Trim 淘汰。
若同一 inode 出现两个 cache identity，检查 rootfs 是否错误地把 transaction
generation 写回 VFS mount generation。

### QEMU 捕获器为什么使用里程碑和总截止

先看带宿主时间戳的最后一行。一次固定墙钟预算耗尽不能单独判定 PIT 或 PIC
错误；但扩大预算后总在同一来宾标记停顿，就应检查硬件状态与模拟器版本差异，
不能继续用“runner 较慢”解释。无限延长超时同样会掩盖真实停滞。

QEMU 捕获器因此使用两个边界：

1. 逐行观察当前用例的最后一个必需里程碑；到达后保留短暂收尾窗口并回收进程。
2. 未到达时，64 MiB 启动档以 35 秒、承担 90 个进程和时间探针验收的
   256 MiB 功能档以 120 秒、32 GiB 主规格以 240 秒为总失败上界；主规格要在
   Debug 构建中扫描约 8388608 个页状态。QMP 等待 `READY` 使用与当前内存规格
   相同的预算，外层 CTest 分别保留额外回收余量。

协议校验仍在进程结束后检查所有必需标记的顺序和全部禁止标记。这个设计既移除
“所有宿主都同速”的假设，也让稳定停顿成为可重复诊断证据；不会把缺失 IRQ、
缺失按键或 panic 误判为成功。Python 工具单元测试另用一个输出 `READY` 后
睡眠的子进程，证明观察者能够提前、完整且无残留地结束捕获。

## v0.8：用户 ELF、Ring 3 与系统调用

### `USER_ELF_REJECTED` 出现在降权前

状态值对应 `UserElfValidationStatus`，先用独立宿主审计缩小范围：

```bash
python3 tools/os.py audit-user-elf build/developer/source/user/user_smoke.elf
```

宿主通过而内核失败时，核对嵌入符号的 start/end 是否包住精确 ELF 文件，
不要把段对齐填充误算为文件长度。状态 2 是头不足 64 字节；范围或总页数失败
时重点检查所有加法/乘法是否在比较前发生无符号回绕。

### `IRETQ` 后立即 triple fault

按硬件依赖顺序检查：

1. GDT 用户数据/代码描述符 DPL 是否为 3，选择子是否为 `0x1B/0x23`。
2. TSS 是否已用 `LTR 0x28` 装载，RSP0 是否指向已映射 16 KiB 转换栈顶。
3. 用户 RIP 与 RSP 所在的 PML4E、PDPTE、PDE、PTE 是否每级 U/S=1。
4. 五项帧顺序是否为 SS、RSP、RFLAGS、CS、RIP，压栈方向不能按表格阅读
   顺序想当然地倒置。
5. 用户代码页必须 executable，栈页必须 RW/NX。

若首次 `INT 0x80` 才失败，观察 TSS.RSP0 附近是否覆盖了原内核调用帧。
v0.8 必须使用专用转换栈；把 BootInfo 启动栈顶重新写入 RSP0 会复现隐蔽损坏。

### 系统调用被停止而不是返回错误

未映射用户缓冲和未知调用编号属于普通输入错误，应返回 `-1/-2`。处理器直接
停止说明入口结构不变量失败：当前没有活跃用户执行，或 vector、CS、SS、
RIP、RSP、栈页 U/S/RW/NX 与约定不一致。用 GDB 检查 176 字节
`UserPrivilegeFrame`，不要先放宽校验；校验失败通常暴露的是汇编栈布局错误。

### 用户故障错误地进入 `PANIC`

异常分发必须先通过保存 CS 的 RPL 判断来源。Ring 3 `#UD` 应报告向量 6；
读取未映射 `0x30000000` 应报告向量 14、错误码 `0x4` 和同值 CR2。两者都
应出现 `USER_TERMINATED`、`USER_RETURNED_TO_KERNEL`，并继续到 `READY`。

若 CS 仍为 `0x08`，故障实际发生在内核入口而非用户程序；若 CS 为 `0x23`
却 panic，检查分发器是否在 panic 决策前调用了
`FrameOriginatedFromUser()`，以及汇编是否把 vector/error slots 压反。

### 用户指针跨页时偶发错误

地址检查必须使用完整半开区间 `[address, address+length)`，先拒绝 64 位
溢出，再从首叶页迭代到末叶页。只检查起点会让第二页的 supervisor 或
not-present 映射漏过。v0.9 在 interrupt gate 内仍不会并发解除当前进程
地址空间；若加入内核抢占或多线程后才出现偶发错误，应优先检查地址空间锁、
页固定和生命周期，而不是移除逐页检查。

## v0.9：CR3、TSS.RSP0 与抢占式上下文切换

### `SCHEDULER_STARTED` 后无任何用户日志

先在 GDB 同时检查当前 PCB 保存帧、CR3 和 TSS.RSP0。首帧必须位于 PID1
自己的 16 KiB 内核栈，RIP=`0x40000000`、CS=`0x23`、SS=`0x1B`，
RFLAGS.IF=1；CR3 必须等于该 PCB 的根物理地址。若 CR3 正确但 `IRETQ`
页故障，逐级检查 PML4[0]/PDPT[1] 的 U/S 和用户代码 PTE 的 RX 权限。

### 第一次 PIT 抢占后 triple fault

这通常不是调度策略错误，而是恢复帧或内核栈所有权错误：

1. IRQ0 入口的 176 字节帧必须完整落在旧进程 Ring 0 栈内。
2. C++ 返回值必须是新进程保存帧地址，汇编要用它替换 RSP，不能继续弹旧栈。
3. 新 CR3 必须继续映射汇编入口、旧/新内核栈和共享内核数据。
4. `TSS.RSP0` 必须在 `IRETQ` 前改为新进程栈顶，否则下一次入口覆盖旧现场。
5. PIC EOI 必须已完成；否则看似“只切换一次”可能是控制器仍保持 in-service。

不要在 IRQ0 中加入串口打印定位。串口轮询会改变时间片和嵌套边界；应使用 GDB
断点、PCB 内存和进程全部结束后的汇总日志。

### 多个 worker 的 BSS 互相污染

三个 worker 的 ELF 和虚拟地址完全相同，差别必须来自 CR3。检查每个 PML4
是否拥有不同的根帧、克隆 PDPT 和 PDPT[1] 子树。PML4[0] 可以共享
PDPT[0] 指向的 supervisor 内核子树，但不能直接共享整个低端 PDPT；否则
第一个进程建立的 `0x40000000` 叶页会泄漏给其他进程。

宿主可比较创建日志中的四个 `PROCESS_CR3`，再在 GDB 对同一用户虚拟地址执行
页表遍历。三个 worker 的 BSS PTE 应指向不同物理帧。仅给 worker 选择不同
虚拟地址会绕过这个故障，不能作为修复。

### `PROCESS_RESOURCES_RECLAIMED` 缺失

退出路径必须先激活内核 CR3，再销毁刚结束进程的根；页表接口会明确拒绝销毁
当前活动根。递归释放只允许遍历进程独占的 PDPT[1] 程序子树和 PML4[255]
用户栈子树，不能碰共享内核映射。比较创建前后的 free、allocated、reserved
三项：reserved 不应变化，free/allocated 的差值指出仍遗漏的叶页或中间表。

若只在地址空间创建失败时泄漏，重点检查“叶帧已分配但 MapPage 失败”和
“中间表已建立但后续栈映射失败”两条回滚；销毁整个未激活进程根应统一覆盖
二者。

## v0.10：阻塞/唤醒与管道

### `SCHEDULER_STARTED` 后所有进程停止

先看冷路径是否输出 `USER_EXECUTION_FAILED=NoReadyProcess`。若所有存活进程
都为 Blocked，逐个检查 PCB 的 `wait_reason`：生产者只能等待
`PipeWritable`，消费者只能等待 `PipeReadable`，worker 不应进入 Blocked。
还要检查一次读或写成功后是否调用了对侧定向唤醒，以及关闭端点是否同样唤醒。

不要通过让 Blocked 参与 Ready 搜索来“恢复运行”；这会掩盖丢失唤醒，并让
进程在条件未满足时错误返回用户态。

### 管道字节数不相等

对照 `PIPE_WRITTEN_BYTES`、`PIPE_READ_BYTES`、buffered count 和每进程方向
统计。环形不变量始终应满足：

```text
bytes_written - bytes_read = buffered_byte_count
0 <= buffered_byte_count <= 64
read_index, write_index < 64
```

若只在回绕后损坏，检查索引是否在每个字节提交后模 64，而不是在整块之后只
推进一次。若内核统计正确而消费者校验失败，检查 `CopyToUser` 的跨页可写
验证和用户端 `total_read_bytes + byte_index` 期望索引。

### EOF 永远不出现

EOF 的条件是“缓冲为空且写端关闭”，二者缺一不可。写端关闭时仍有残留数据，
读者应先继续读，最后一次空读才返回零。生产者正常退出、用户异常和显式 close
都必须收敛到同一端点关闭逻辑；双重关闭返回 `ENDPOINT_CLOSED`，不能再次改
统计或产生虚假的第二个 EOF。

### `PIPE_ENDPOINTS_CLOSED` 缺失

检查进程终止路径是否在销毁地址空间前根据用户程序选择关闭其端点。端点归属
不能通过“最后运行的 PID”推测；生产者/消费者权限由 `UserProgramSelection`
明确决定。关闭读端必须唤醒写者以得到 broken pipe，关闭写端必须唤醒读者以
得到 EOF。

### ELF 审计拒绝 `.init_array`

自研入口没有 CRT，不会自动遍历 C++ 动态初始化数组。使用：

```bash
llvm-readelf --section-headers --wide build/developer/source/kernel/kernel.elf
llvm-nm -n build/developer/source/kernel/kernel.elf
```

若出现 `.init_array` 或 `_GLOBAL__sub_I_...`，查找命名空间作用域下具有非
constexpr 构造器的对象。将状态改为常量初始化或显式初始化函数；不要为了
通过审计而删除区段，因为那只会留下未执行构造器的对象。

## v1.0：Shell、统一描述符与空闲唤醒

### Shell 已输出 `READY`，但输入后没有命令

先按因果链核对四组证据，不要直接把字符塞进来宾内存：

1. QMP `sendkey` 是否成功发给当前 QEMU 实例；
2. i8042 是否产生 IRQ1，Set 1 解码器是否把 make code 转成非零字符；
3. `CONSOLE_SUBMITTED_BYTES` 是否增长，fd 0 等待者是否从 Blocked 变为 Ready；
4. Shell 的通用描述符读取是否返回字符并最终遇到 Enter。

Shift 和 Caps Lock 只共同决定字母大小写；数字与标点只受 Shift 影响。如果
`CapsLock + 1` 错误地产生 `!`，说明解码器把 `shift XOR caps` 错用于整个
键盘，而不是仅用于字母行。

### 所有进程 Blocked 后 QEMU 看似卡住

v1.0 允许“无 Running、无 Ready、至少一个 Blocked”。此时
`BlockCurrentProcess` 成功并清除当前进程，执行循环切回永久 CR3、恢复默认
TSS.RSP0，再进入：

```asm
sti
hlt
cli
```

`STI` 后一条指令具有中断影子，保证不会在“检查无 Ready”和真正睡眠之间丢失
已经到达的可屏蔽中断。这三条指令必须位于同一个内联汇编块；若分别调用
`EnableInterrupts()` 和 `WaitForInterrupt()`，中间的 `RET/CALL` 会破坏紧邻
保证。被 IRQ 唤醒后先 `CLI` 再检查队列，避免在普通调度器状态转换中嵌套
硬件中断。若持续停顿，用 GDB 检查 RFLAGS.IF、PIC IRR/ISR、当前 PCB 状态和
`wait_reason=DescriptorReadable`；不要让 Blocked 进程参与 Ready 搜索来掩盖
唤醒缺失。

### 描述符调用返回权限或类型错误

每个 PCB 的八槽表先解析 fd，再按对象类型分派。fd 0 只读，fd 1/2 只写；
管道端点也保留方向；普通文件拒绝目录迭代，目录拒绝字节流读写。优先检查：

- fd 是否属于当前进程，而不是误用另一进程返回值；
- 槽位类型、对象索引和 readable/writable 位是否一致；
- `OpenDirectory` 是否建立目录游标，`ReadDirectory` 是否使用 64 字节 ABI；
- 进程退出是否先逐槽关闭，再释放文件句柄和地址空间。

系统调用第四参数在 C++ System V 入口是 R8，但执行 `INT 0x80` 前必须搬到
约定的 R10。目录项缓冲长度或用户可写范围错误都应返回稳定错误，不应触发
用户异常或内核 panic。

### 控制台统计不守恒

正常自动化脚本提交 109 字节，因此最终应满足：

```text
submitted = read = 109
dropped = buffered = 0
```

`submitted > read` 通常表示 Shell 在完整消费前退出或唤醒丢失；
`dropped > 0` 表示宿主输入速度超过 256 字节 FIFO 的消费能力；
`buffered > 0` 表示结束条件过早。调试时可以降低 QMP 逐键发送速度，但不能
扩大 FIFO 来掩盖错误，也不能逐字符写串口，因为那会改变生产/消费速度。

## v1.1：资源快照与跨目录镜像依赖

### 256 MiB 通过，但 32 GiB 报资源快照账本错误

先区分 `managed_frame_count` 与可用 RAM 页数。前者由最高受管物理地址除以
4 KiB 得到，包含 E820 保留洞、MMIO 和 PCI 窗口；后者只包含能进入 free、
allocated 或 reserved 三态的页。正确关系是：

```text
accounted = free + allocated + reserved
accounted <= managed
unavailable = managed - accounted
```

32 GiB QEMU 在 3–4 GiB 附近存在明显 PCI 洞，因此错误地检查
`accounted == managed` 会只在容量档稳定失败。修复不能通过忽略快照错误或
修改 QEMU 内存图完成；应保留 2-bit `Unavailable` 状态，把差值作为不可用页
推导出来，同时继续比较 managed/free/allocated/reserved 四个稳定字段。
单元样例必须显式包含不可用间隙，256 MiB functional 与 32 GiB capacity
必须共同进入门禁。QEMU 捕获器在看到禁止失败标记时会立即结束实例并报告该
标记，不再等待 32 GiB 档的 240 秒总截止时间。

40 秒旧预算曾在共享宿主负载约 16 时于 `EXCEPTION_SELF_TEST_READY` 后耗尽，
来宾尚在执行 32 GiB 页状态、buddy、direct-map 与跨层自检，串口因提交后才
输出统计而没有中间行。这不是放宽来宾轮询：64 MiB 启动档为 30 秒，
256 MiB 功能档因完整用户工作负载与时间探针使用 120 秒，禁止失败标记
仍会立即收尾；容量档把宿主外部保险调整为有明确上界的 240/250 秒，并由工具
单元测试证明一个睡眠子进程会在短预算后被终止而不是遗留后台进程。

### 全量构建偶发报告 Kernel 或 User ELF 不存在

Stage 1 镜像命令读取七个 Kernel ELF，Kernel 的用户镜像对象又读取七个 User
ELF。文件路径出现在自定义命令参数中，不代表 CMake 一定能跨目录推导目标级
构建顺序；增量构建恰好已有文件时问题会被掩盖，干净构建或不同生成器才暴露。

`os_stage1_images` 必须显式依赖全部 Kernel ELF 目标，
`os_kernel_user_images_object` 必须显式依赖全部 User ELF 目标；Kernel
链接命令还必须直接依赖生成的 `architecture.o` 和 `user_images.o` 文件。
目标依赖保证跨目录生产顺序，真实文件依赖负责把时间戳变化传播给重新链接和
重打包。只依赖 phony 自定义目标时，生成对象可以更新而旧 Kernel 仍被判定
为最新。不要用重复执行构建或在 Python 脚本里等待文件出现来绕过依赖图；
生产者—消费者关系属于 CMake 构建图。

## v1.2–v1.3：Process/Thread、FXSAVE 与原生系统调用

### 停在处理器能力门禁

先确认测试是否有意使用：

```bash
--cpu-model 'qemu64,-syscall'
```

正常 `qemu64` 应在 `CR3_VALID` 后先输出
`PROCESSOR_FEATURES_READY/REQUIRED/AVAILABLE/PROFILE_*`，再输出
`EXTENDED_STATE_READY/CR0/CR4/AVX_DISABLED`。若普通配置报告
`PROCESSOR_FEATURES_UNSUPPORTED`，在 `ReadProcessorFeatureProfile()` 同时
检查：

```text
bit 24 FXSR = 1
bit 25 SSE  = 1
bit 26 SSE2 = 1
CPUID.80000001H:EDX bit 11 SYSCALL = 1
CPUID.80000001H:EDX bit 20 NX      = 1
CPUID.80000001H:EDX bit 29 LM      = 1
CPUID.80000008H:EAX physical=36..52, virtual=48
```

不要跳过检查或把失败改成警告；后续用户汇编会真实执行 XMM/x87 指令。若三位
都存在但初始化失败，检查回读是否满足 CR0.MP/NE=1、EM/TS=0，
CR4.OSFXSR/OSXMMEXCPT=1、OSXSAVE=0，并检查初始 `FxSaveArea` 地址低四位
是否为零。

### `NATIVE_SYSCALL_READY` 缺失

先区分 `CPU_LOCAL_INITIALIZATION_FAILED` 与
`NATIVE_SYSCALL_INITIALIZATION_FAILED`。前者检查永久栈是否非零、16 字节
对齐，以及 self/深度/入口状态是否一致。后者在
`BuildNativeSystemCallRegisterValues()` 检查 STAR 的选择子顺序，再比较
RDMSR 回读：

```text
STAR  = 0x0010000800000000
FMASK = 0x0000000000044700
EFER.SCE = 1
LSTAR = OsKernelNativeSystemCallEntry
GS_BASE = 0
KERNEL_GS_BASE = CpuLocal address
```

不要把回读失败改写成 CPUID unsupported；两类故障的根因不同。

### 原生入口后 #GP、三重故障或无日志

在 `OsKernelNativeSystemCallEntry` 的第一段逐条检查：必须先 `SWAPGS`，只把
用户 RSP 写到 `[gs:24]`，再从 `[gs:16]` 取得可信栈；在完整 UserContext
形成前不得 `STI` 或调用 C++。如果故障只发生在最后一个 Thread 阻塞/退出后，
检查 `OsKernelReturnFromUserMode` 是否根据捕获的原生入口状态补做 SWAPGS，
而不是在 `ClearCurrentThread()` 后重新查询已经清空的状态。

若 `USER_RETURN_REJECTED` 出现，随后几行会给出 ownership、status、mapping、
entry、vector、RIP、RSP 和 RFLAGS。先按 status 定位，不要直接放宽 SYSRET
条件。合法 DF/RF 应选择 IRET，不应被拒绝。

### 第一次抢占或阻塞后 `EXTENDED_STATE_ISOLATED` 缺失

按保存链定位：

1. 当前 Thread 的 `runtime_thread.saved_frame` 与 `extended_state` 是否属于
   同一 `thread_index`；
2. 抢占、Block 和退出是否在修改 scheduler 当前项前执行 `SaveFxState`；
3. `ActivateThread` 是否先验证 Process/Thread/stack，再切 CR3、写 TSS.RSP0
   并 `RestoreFxState`；
4. 用户 C++ 是否仍带 `-mno-sse -mno-sse2`，模式操作是否只在
   `extended_state.asm`；
5. XMM15 偏移是否落在 FXSAVE 的 64 位高 XMM 区，保存区是否恰为 512 字节。

若 save/restore 累计为零，说明执行边界未接线；若累计非零但只有某个 PID
失败，优先检查 Thread 索引与 Process 索引是否被混用。不要只移除用户校验
日志来让系统继续，因为这通常是跨 Thread 信息泄漏。

### `PROCESS_THREAD_CAPACITY_SELF_TEST_PASSED` 缺失

容量事务按当前 RAM 档一次占有全部目标对象。先看最后出现的日志，再用 GDB
检查三组数量：

```text
64 MiB  : process=4,   thread=4,   per-process=1
256 MiB : process=64,  thread=128, per-process=32
32 GiB  : process=256, thread=512, per-process=64
```

常见次级上限是 KVA 描述符或内核栈槽；当前必须分别为 1024 和 512。创建
页表根后必须立即登记到 `capacity_process_roots`，创建栈后必须立即设置
`capacity_stack_active`。正常销毁则先清对应回滚标志，再 reap scheduler
对象；顺序颠倒会分别造成泄漏或二次销毁。

在全部 Thread 创建后，中间 `ResourceSnapshot` 必须观察到档位对应的
Process、Thread 和活动栈数量。最终快照失败时查看
`changed_fields_mask`，不要把 supplemental count 固定成零来掩盖对象泄漏。

### WaitQueue 中的 Thread 永远不再 Ready

读取 Thread 的四个字段：`state`、`wait_condition`、`wake_reason` 和
`wait_queue`。合法阻塞项必须是：

```text
Blocked + condition!=None + wake_reason=None + wait_queue!=nullptr
```

合法唤醒后必须是：

```text
Ready + condition=None + concrete wake_reason + wait_queue=nullptr
```

再检查队列 `enqueue_count - wake_count == waiting_thread_count`，从 head
沿 `next_wait_thread_index` 应恰好走到 tail 且无环。对象关闭必须先置
closed，再以 ObjectClosed 唤醒全部 waiter。重复完成返回
`WakeAlreadyResolved` 是正确结果，不应再次把 Thread 插入 Ready。

### 32 GiB 容量测试像“卡住”

本阶段新增 256 个真实页表根和 512 个六页动态栈，Debug/TCG 下会放大启动
成本。QEMU 捕获器仍使用 75 秒总截止、CTest 使用 85 秒外层保险；禁止失败
标记到达时会立即终止，不会等待截止。先确认后台没有旧 QEMU：

```bash
pgrep -af qemu-system-x86_64
```

若进程仍在推进，不要把来宾热路径加逐对象日志；它会通过 115200 波特串口
进一步改变时序。应在 GDB 观察容量循环索引，或单独运行
`os_kernel_thread_scheduler_*` 宿主测试缩小问题。超过有界截止后工具会
终止并回收 QEMU；持续存在的后台进程属于捕获器缺陷，不能靠手工长期清理
作为正常流程。

## v1.4：KernelObject 与动态 FileTable

### `FILE_DESCRIPTION_MODEL_OK` 缺失

先看 PID4 是否非零退出，再按验证顺序检查：

1. `/fdv14.bin` 的首次 write 是否返回 8；
2. 首次 read open 是否得到最低动态 fd 3；
3. hard limit 大于 64 时 minimum 64 的 duplicate 是否得到不小于 64 的 fd；
   hard limit 等于 64 时，是否改用 minimum 8 并得到不小于 8 的 fd；
4. 独立 open 是否得到 fd 4；
5. 三次 read 是否分别得到 `ABC`、`DEF`、`ABC`；
6. close-on-exec 是否只存在于 duplicate，源 fd 初始 flags 是否为零；
7. soft limit 降至选定 minimum 后，新 duplicate 是否精确返回 `-24`；
8. 关闭 fd 4 后的新 open 是否复用 4。

若前两次读取都得到 `ABC`，offset 被错误放进 FileTableEntry 或 duplicate
复制了 `FileSystemHandle`。若独立 open 得到 `DEF`，则两个 open 错误共享
同一 FileDescription。不要修改用户期望来匹配错误偏移。

### 活动对象或强引用没有归零

按所有权层逐级检查：

```text
FileTable active descriptors
  → KernelObject active strong references
    → active FileDescription count
      → KernelHeap active allocations
```

`OBJECT_CREATIONS != OBJECT_DESTRUCTIONS` 表示对象未走最后引用；
`FILE_DESCRIPTION_FINALIZATIONS < OBJECT_DESTRUCTIONS` 表示 finalizer 路径未
登记；`FILE_DESCRIPTION_FAILED_FINALIZATIONS != 0` 则表示底层文件或管道
关闭失败。`FILE_TABLE_CHUNK_ALLOCATIONS != FILE_TABLE_CHUNK_RELEASES`
通常表示 Process 退出未调用 `FileTable::Destroy`。

lookup 返回的 `KernelObjectReference` 是 RAII 临时引用。提前 `DetachReference`
或让引用跨越 Process reap 都会延长生命周期；业务函数中不要缓存 payload
裸指针。close 应先摘除 table handle，再在表锁外 release。

### fd 耗尽后状态改变

运行：

```bash
ctest --test-dir build/developer \
  -R 'os_kernel_file_table_(capacity|randomized)' --output-on-failure
```

capacity 测试会实际填满 4096 项。失败返回后，destination 必须仍为
`UINT64_MAX`，源强引用数和活动 fd 数必须不变。若只多出一个空分块，检查
`EnsureChunk` 的锁外候选是否在复验失败时归还；若传入 reference 消失，检查
安装是否在确认槽为空和 limit 允许之前调用了 `DetachReference`。

### 对象测试看似停住

随机模型只有 100000 步，正常在秒级完成；QEMU functional 正常在几秒内完成。
所有正式命令均有捕获器总截止与 CTest 外层截止，不应产生无限等待。先单独运行
对应宿主测试；若宿主通过而 QEMU 停在某一日志，检查是否在持 FileTable、
KernelObject 或 FileDescription operation lock 时调用串口、阻塞或 finalizer。
高频 acquire/release 禁止逐项打印，避免日志本身制造超时。

## v1.5：VFS、挂载命名空间与 memfs

### `VFS_STATUS` 或 `VFS_VALID` 缺失

内核启动时先建立旧磁盘根文件系统，再把 memfs 挂载到 `/tmp`。串口中应看到
带时间戳的 VFS 初始化、根后端、`/tmp` 挂载和验证摘要；不会逐路径、逐字节
打印高频日志。若缺少完成标记，按以下顺序定位：

1. 检查旧磁盘后端是否成功打开并验证根目录；
2. 检查 `/tmp` 挂载点是否已经存在且类型为目录；
3. 检查 memfs 根 vnode、父引用和资源计数；
4. 检查挂载表中 root、mount point 与 mounted root 的三元关系；
5. 最后检查 `Vfs::Validate` 报告的首个失败状态。

启动阶段不会在旧磁盘元数据损坏时自动格式化。自动格式化会把“读坏盘失败”
变成破坏性写入，并掩盖持久化回归；测试应显式制作新镜像或注入预期故障。

### `/file/` 被当成普通文件打开

路径尾部斜杠是一项类型约束，不是可随意删除的字符：

```text
/directory/   允许，最终 vnode 必须是目录
/file/        NotDirectory
/missing/     即使带 create，也不能创建普通文件
```

若 `/file/` 成功，检查规范化阶段是否在记录“请求目录”之前吞掉了尾部斜杠；
若 `/missing/` 创建了文件，检查 `Open` 是否在创建候选节点前验证了该约束。
回归命令：

```bash
ctest --test-dir build/developer \
  -R 'os_kernel_vfs_(unit|backend_contract|namespace_randomized)' \
  --output-on-failure
```

### `..` 在挂载根无法离开后端

挂载根的父目录不是 memfs 内部根节点的普通父引用。解析
`/tmp/..` 时，VFS 必须先识别“当前 vnode 是 mounted root”，跳回宿主挂载点，
再对宿主挂载点执行一次 parent：

```text
legacy-root/tmp -> mount transition -> memfs-root
memfs-root/..   -> leave mount       -> legacy-root
```

若结果仍在 `/tmp`，检查挂载表反向查找；若越过 `/`，检查 root clamp。当前
v1.5 在启动完成后冻结挂载拓扑，因此解析过程不需要处理并发卸载；不要把这个
边界误写成已经支持动态 `mount`/`umount`。

### 相对路径落到错误进程的目录

cwd 属于 Process 的 `FsContext`，不是 VFS 全局变量，也不是 Shell 本地字符串。
先比较两个 Process 的 cwd vnode，再检查：

- 系统调用是否从当前 Process 取得 `FsContext`；
- `chdir` 是否只在最终 vnode 为目录后提交新 cwd；
- 失败的 `chdir` 是否保持原 cwd；
- Shell 提示符是否来自 `getcwd`，而不是自行拼接用户输入；
- Process 销毁时是否释放其 cwd 引用。

`getcwd` 通过 vnode 的 parent/name 关系逆向重建路径；输出缓冲不足应返回
`Range`，且不得写出半条路径或泄漏未初始化 padding。

### memfs 写失败后内容或资源发生变化

扩容写是一个小事务：先计算新长度与几何容量，再分配候选缓冲并复制旧内容，
最后一次性替换 vnode 的 data/capacity。分配失败前不得修改 offset、size、
旧缓冲或资源计数。重点检查：

```text
旧 data + 旧 capacity + 旧 size
  -> 分配候选
  -> 复制旧内容并填零间隙
  -> 提交 vnode 字段
  -> 释放旧缓冲并更新 ResourceUsage
```

长度小于 64 字节时仍必须保证容量可以增长，不能因为整数除法或对齐把候选容量
算成零。使用后端契约测试验证失败原子性，再用 100000 步随机命名空间模型检查
创建、截断、读写和目录遍历组合。

### Process 退出后报告 KernelHeap 泄漏

挂载的 memfs 是内核全局持久资源，不随某个 Process 退出。生命周期快照比较
时，应从 KernelHeap 总量中精确扣除 `memfs::ResourceUsage` 所拥有的节点、
名称和数据缓冲，而不是放宽所有堆泄漏检查。若仍有差异：

1. 先运行 `Vfs::Validate` 与 memfs 后端验证；
2. 比较 memfs 节点数（含内联名称）、目录数、文件数和数据容量；
3. 再检查 FileDescription、KernelObject 和 FileTable 是否归零；
4. 确认失败 open/create 没有遗留候选节点；
5. 确认关闭文件不会错误销毁仍由命名空间拥有的 vnode。

禁止用固定常量“减掉一块内存”；资源折扣必须来自后端当前精确统计，否则真实
泄漏会随文件数量变化而被掩盖。

### 100000 步 VFS 随机测试像是卡住

随机模型具有固定种子、固定 100000 步和 CTest 外层超时，正常应在秒级完成。
先单独执行 `os_kernel_vfs_namespace_randomized`；若宿主测试超时，记录最后一步
操作和规范化路径，检查父链或挂载反向遍历是否成环。若只有 QEMU 超时，检查
是否新增了逐 lookup、逐目录项或逐字节串口日志。工具层必须在超时后终止并
回收 QEMU，不允许留下后台模拟器长期占用 CPU。

## v1.7：磁盘 PID1、exec 与 wait

### Kernel 完成 rootfs 挂载后没有 PID1 日志

先区分“文件不存在”“文件读取失败”“ELF 语义非法”和“Process 注册失败”：

```bash
python3 tools/os.py inspect-rootfs build/developer/images/boot_disk.img
python3 tools/os.py fsck-rootfs build/developer/images/boot_disk.img
python3 tools/os.py audit-user-elf build/developer/source/user/user_init.elf
```

rootfs 中必须存在 `/sbin/init`，文件大小与构建产物一致。若宿主审计通过而
目标机失败，在 `LoadExecutableFromPath` 检查：

1. 临时根 `FsContext` 是否已在 rootfs mount 后初始化；
2. `Stat` 得到的节点是否为普通文件且长度非零；
3. VFS reader 是否对每次短读循环到精确长度；
4. `ValidateUserElf(reader)` 返回 `ReadFailed` 还是格式状态；
5. 候选 AddressSpace 失败后是否保持根地址为零或被完整销毁；
6. `RegisterInit` 是否收到 PID 1 和无父索引。

正常第一条进程事件为
`[OS][KERNEL][PROC] SPAWN_PID=0x0000000000000001`，随后才是
`[OS][USER][INIT] STARTED`。前者存在而后者缺失时，问题已经越过磁盘读取，
优先检查初始 RIP、RSP、argc/argv/envp 和用户页权限。

### PID1 一进入用户态就 #PF 或参数校验失败

用户栈范围固定为 64 页；`UserAddressSpace::stack_top_virtual_address` 表示
几何栈顶，不能被改写成当前 RSP。实际入口值来自
`ProgramArgumentPlan::Layout().stack_pointer`。检查以下布局：

```text
RDI = argc
RSI = argv vector
RDX = envp vector
RSP % 16 = 0
argv[argc] = nullptr
envp[envc] = nullptr
所有字符串地址位于用户栈且以 NUL 结束
```

若小参数通过而 128 KiB 探针失败，核对上限计算是否把每个 NUL 计入总量，
以及 metadata 指针区是否另外占用栈容量。不要通过放宽上限绕过：

```bash
ctest --test-dir build/developer \
  -R 'os_kernel_program_arguments_unit_tests|os_kernel_process_models_randomized_tests' \
  --output-on-failure
```

### 失败 exec 之后旧程序无法继续

截断 `/bin/truncated.elf` 必须返回 `INVALID_EXECUTABLE`，超出 128 KiB 的参数
必须返回 `ARGUMENT_LIST_TOO_LARGE`；两次之后 exec probe 分别输出：

```text
[OS][USER][PROC] EXEC_FAILURE_PRESERVED_IMAGE
[OS][USER][PROC] EXEC_E2BIG_PRESERVED_IMAGE
```

缺失时按提交点向前检查。旧 AddressSpace 在以下全部步骤完成前不得销毁：

- 请求结构和所有用户字符串可读；
- VFS 文件可读且 ELF 第一遍验证完成；
- 候选段、BSS、64 页栈和参数元数据完成；
- 候选 CR3 可试激活并能切回旧 CR3；
- `CommitProcessImage` 接受当前唯一 Running Thread。

失败路径应重新激活旧 CR3、销毁候选 AddressSpace 并原样返回旧 frame。
close-on-exec 也只能在成功提交后执行。若失败日志出现但资源快照最终不一致，
比较 candidate 的 mapped page、页表 frame 和用户栈 frame 回收计数。

### 成功 exec 又返回了旧调用点

成功 `ExecProcess` 不返回。dispatcher 返回的 frame 必须已经被重建为新
UserContext：新 ELF RIP、新用户 RSP、`RDI=argc`、`RSI=argv`、
`RDX=envp`，段选择子和 RFLAGS 恢复到允许的用户值。若 Kernel 打印
`EXEC_PID` 后 exec probe 的失败出口仍执行，检查：

- `DispatchExecProcess` 是否返回被修改的同一个 frame；
- 汇编出口是否错误地恢复了调用前 RIP；
- `runtime_threads[thread_index].saved_frame` 是否仍指向当前 frame；
- CR3 是否在返回前切到 candidate root；
- `exec_target` ELF 入口是否位于可执行用户映射。

### `NO_ZOMBIES` 缺失或进程树统计不守恒

先看结束摘要的第一个不一致字段：

```text
registered = exited = collected = 8
reparented = 1
wait successes = 7
wait blocks >= 1
wait no-child = 1
active = alive = zombies = 0
```

`exited > collected` 通常表示父进程没有 wait；`reparented=0` 表示 orphan
parent 退出时没有迁移孩子；`zombies>0` 表示 PID1 提前退出；调度器
reaped=8 但 tree collected<8 表示两个状态机提交顺序损坏。运行：

```bash
ctest --test-dir build/developer \
  -R 'os_kernel_(process_tree_unit|process_lifecycle_integration|process_models_randomized)' \
  --output-on-failure
```

不要在孩子退出时直接删除树项。退出只产生 Zombie；wait 才消费退出记录并
回收。普通父进程退出时要迁移 Alive 和 Zombie 两类孩子，不能只处理仍运行者。

### wait 看似永久卡住

先判断是真阻塞还是日志拖慢。默认路径不会逐次打印 wait 扫描；runner 会为
每行加宿主到达时间并使用阶段 deadline。若 PID1 已输出
`CHILDREN_STARTED`，但没有 `WAIT_REAPED_PID`：

1. 检查至少一个孩子是否仍能得到 Ready 时间片；
2. 检查孩子最后一个 Thread 退出是否调用 `MarkExited`；
3. 检查 wait queue 登记是否发生在条件复查之前，避免 lost wakeup；
4. 检查退出路径是否用正确父索引定向唤醒；
5. 检查系统无 Ready Thread 时是否进入 `sti; hlt; cli`，而非关闭中断忙等。

所有 QEMU 命令必须由 `tools/os.py`/runner 管理；阶段超时和 CTest 外层超时
都会 terminate、wait 并清理 QMP socket。调试时也不要启动无期限后台 QEMU；
若手工运行，使用 `timeout` 包裹并在结束后确认：

```bash
pgrep -a qemu-system-x86_64
```

正常完成后不应留下本项目 QEMU 进程。

## v1.8：VMA、匿名页故障、栈增长与 UserHeap

### `mmap` 成功但第一次触页立刻终止

先读取 ProcessRuntime 记录的 vector、error code 和 CR2。合法匿名首次读的
关键事实应为：

```text
vector = 14
error.P = 0
error.U/S = 1
error.W/R = 0
CR2 page belongs to Anonymous VMA
```

在 `HandleUserPageFault` 断点依次检查：

1. `FindContaining(AlignDown(CR2))` 是否找到预期 VMA；
2. VMA kind 是否为 `Anonymous`，权限是否包含 read；
3. `AllocateAndMapUserPage` 是否取得 frame 和所需页表页；
4. 新 PTE 是否同时具有 P、U/S 与正确 RW/NX；
5. `ZeroPhysicalPage` 是否通过 direct-map 得到非零虚拟地址；
6. 返回前 `mapped_page_count` 与 `demand_page_fault_count` 是否各增加一。

若 error.P=1，不要把它改成 demand fault。它是已有 PTE 的权限问题，应检查
用户请求权限和 PTE 推导。

### 预留匿名范围后物理页立刻减少很多

`MapAnonymousMemory` 的成功路径只能执行 gap 查找与 VMA `Insert`，不应调用
frame allocator。比较调用前后的 VM 统计：

```text
anonymous_page_count increases
virtual_page_count increases
resident_page_count unchanged
```

若 resident 增加，搜索 map 路径中是否错误调用 `MapDemandPage`。若只是全局
free frame 减少，确认变化不是同时发生的 Process/KernelStack 创建；用
`os_kernel_user_virtual_memory_lifecycle_integration_tests` 隔离 VMA 与页表。

### 中段 `munmap` 后 VMA 图损坏

从一个区域中间删除需要额外描述符。`VirtualMemoryMap::Remove` 必须先完成
kind 预检并 Acquire split descriptor，再修改原节点。检查：

- pool active 是否在 split 后净增一；
- 左区域 end 是否等于 remove begin；
- 右区域 begin 是否等于 remove end；
- previous/next 与 owner identifier 是否一致；
- `Validate()` 是否报告第一个结构错误。

元数据耗尽时旧区域必须完全不变。直接运行：

```bash
ctest --test-dir build/developer \
  -R '^os_kernel_virtual_memory_area_unit_tests$' \
  --output-on-failure
```

随机模型失败会报告固定种子和迭代；不要通过增加描述符容量掩盖 split 事务
次序错误。

### unmap 后 resident 恢复但 frame 基线仍不一致

先区分数据 frame 和页表 frame。`ReleaseUserPage` 会返回
`reclaimed_table_frame_count`；相邻用户页通常共享 PT/PD，删除一页不保证
立即回收所有中间表。检查：

1. 每个实际 PTE 的数据 frame 是否释放一次；
2. not-present reservation 是否被错误当成已分配 frame；
3. 最后一个叶项清除后 PT 是否为空；
4. 进程私有 PD/PDPT 是否按所有权回收；
5. 共享 Kernel PDPT 是否被错误释放；
6. `page_table_reclaimed_frame_count` 是否只累计真实返回值。

128 轮组合用例必须同时恢复 allocator baseline 与 descriptor baseline：

```bash
ctest --test-dir build/developer \
  -R '^os_kernel_user_virtual_memory_lifecycle_integration_tests$' \
  --output-on-failure
```

### 正常递归却被判为非法栈增长

v1.8 栈不是“VMA 内任意地址都可出现”。检查 fault page：

```text
fault_page + 4096 == stack_committed_bottom
fault_page >= stack_bottom
fault_address <= saved_user_rsp + 4096
saved_user_rsp <= fault_address + 64 KiB
```

参数页由 `PrepareUserStack` 在用户入口前预提交；入口后才由 `#PF` 降低
committed bottom。若一开始 committed bottom 仍等于 stack top，检查参数
布局最低地址是否传入准备函数。若一次跨过多页，检查编译器生成的栈探测行为
和测试局部块尺寸；不要放宽为“整个 8 MiB 都合法”。

永久 guard 位于 stack VMA 底部再下一页，必须让 `FindContaining` 返回
NotMapped。若 guard probe 被恢复，说明栈 VMA begin 错误包含了 guard。

### 只读匿名页写入没有产生 protection fault

第一次读会建立 U/S、R、NX PTE；随后写应得到 error.P=1、W/R=1、U/S=1。
检查 `DecodeProtectionFlags` 是否把用户 write 请求错误默认打开，以及
`AllocateAndMapUserPage` 的 writable 参数是否只来自 VMA。

Kernel 不允许为 present fault 再建一张可写页。PID1 只有在 probe 的
termination reason 是 Exception 且 vector 为 14 时才输出
`VM_FAULT_POLICIES_VERIFIED`。

### UserHeap 随机测试报告载荷变化

优先检查最近一次 split 或 coalesce：

- split 后新 header 是否覆盖旧 payload；
- remainder 是否至少包含 64 字节 header 与 16 字节 payload；
- next physical block 的 `previous_block_offset` 是否更新；
- 合并前是否从 free list 删除将消失的节点；
- allocation 的 `requested_size_bytes` 与内部 aligned capacity 是否混用；
- grow 后最后一个 free block 是否直接扩大而没有重复插链。

固定种子宿主测试可以快速复现：

```bash
ctest --test-dir build/developer \
  -R '^(os_user_heap_unit_tests|os_user_heap_randomized_tests)$' \
  --output-on-failure
```

若只在 QEMU 失败，再检查 `SetProgramBreak` 返回值是否等于请求地址，以及
break 页首次写是否进入相同匿名 fault 路径。

### QEMU 已到 READY，但 runner 报 demand 日志次数错误

demand 与 stack 日志按每 Process 的二次幂累计采样，同一整机中可能出现多行。
它们属于最小数值断言，不属于精确 multiplicity。一次性用户 marker 与最终
VMA 汇总才使用精确次数。

若修改 runner 后卡住，先单独执行三个有界配置：

```bash
ctest --test-dir build/developer -R '^os_qemu_bootstrap_smoke$' --output-on-failure
ctest --test-dir build/developer -R '^os_qemu_functional_smoke$' --output-on-failure
ctest --test-dir build/developer -R '^os_qemu_stage1_load_success$' --output-on-failure
```

失败后确认 `pgrep -a qemu-system-x86_64` 没有残留。工具应在总截止或静默截止
到达后 terminate 并 wait，不能通过无限延长 timeout 隐藏页故障循环。

## v1.11：外部命令、重定向与动态管道

### Shell READY 后外部命令立即失败

先区分 parser、rootfs lookup、spawn 和 wait 四层。确认解析计划中的 program
是绝对 `/bin/...` 路径；再用 `ls /bin` 和 `stat /bin/<name>` 验证 rootfs
安装。若出现 exec 拒绝，审计 `user_core_tool.elf` 的 PT_LOAD、入口和 W^X；
若 spawn 成功但结果异常，检查 multi-call 分派使用的 `argv[0]` 是否保留
basename。

### 流水线一直不返回

最常见原因不是调度器超时，而是某个进程仍持有不需要的写端，导致末端永远
看不到 EOF。按 stage 检查：

1. 子进程 stdin/stdout 是否经 `DuplicateDescriptorTo` 安装到 0/1；
2. 替换后是否关闭原始 pipe fd；
3. 父 Shell 是否在 spawn 后立刻关闭已经转交的端点；
4. 失败路径是否仍关闭全部未消费端点并 wait 已发布孩子；
5. Pipe 的 writers 是否最终为零，buffered 为零后 reader 才收到 EOF。

单独运行 `os_user_shell_execution_unit_tests` 排除解析图错误，再运行
`os_qemu_functional_smoke`。两层都有截止时间；不要通过扩大 timeout 掩盖
端点泄漏。

### functional 动态管道统计不守恒

正常摘要应满足 capacity=128、active=0、peak=128、create=release，且容量
自检至少产生一次 rejection。若 active 非零，沿 FileTable →
FileDescription finalizer → PipeManager close 查最后引用；若物理页未恢复，
检查跨页写失败回滚和最后端点释放。`ReleaseDynamicPages` 只有成功归还页面
后才能清除地址，否则会把“仍拥有页面”伪装成已释放。

## v1.12：用户 Thread、TLS 与 private futex

### CreateThread 偶发进入未初始化 RIP 或栈

这是 Ready 发布顺序错误，不是 QEMU 随机故障。创建者必须在 scheduler lock
保护下完成 Thread 槽、KernelStack、UserContext、FXSAVE、用户栈和运行时
元数据初始化，最后才把新 Thread 接入 Ready 队列。若先解锁再补
`saved_frame`，PIT 可以立即选择该槽。检查失败现场的 TID、RIP、RSP、
`runtime_thread.active` 与 Ready queue 是否出现“已 Ready 但 inactive”。

### 子 Thread 读到零 TID

新 Thread 可能在 `CreateThread` 返回父 Thread 之前运行。不能由父 Thread
在返回后把 TID 写进共享启动参数；子入口应调用 `GetThreadIdentifier` 自行
发布身份。任何“父先写、子后读”的假设都必须由同步原语建立，而不能依赖创建
系统调用的表面顺序。

### TLS 在第一次系统调用后变回零或串到别的 Thread

依次检查：

1. `SetThreadLocalStorage` 是否验证 16 字节对齐与可写用户地址；
2. ThreadEntry 与运行时元数据是否同时保存同一 FS-base；
3. 每次 `ActivateThread` 是否写 `IA32_FS_BASE`；
4. SYSCALL/IRET/调度汇编路径是否错误执行 `mov fs, ...`；
5. exec、Thread 退出和 Process 终止是否清除或回收正确所有者的 TLS。

长模式下 selector 看似仍为用户数据段也不能证明 FS-base 正确；应直接在切换
前后读取 MSR，并让两个 Thread 在相同 TLS offset 写入不同哨兵值。

### private futex 偶发永远睡眠

确认比较用户字与登记 WaitQueue 在同一 scheduler lock 内完成。若先比较、
解锁后再阻塞，wake 可以落入中间窗口。其次确认 key 同时包含
AddressSpaceId 与四字节对齐地址，waiter 被唤醒、取消、异常终止或 exec
移除时都释放空表项。调试日志只在二次幂累计点输出；不要在原子快速路径逐次
打印，否则串口本身会改写调度时序。

快速复现局部状态机：

```bash
ctest --test-dir build/developer \
  -R '^(os_kernel_private_futex_unit_tests|os_kernel_private_futex_randomized_tests|os_kernel_thread_scheduler_unit_tests)$' \
  --output-on-failure
```

最后运行 `os_qemu_bootstrap_smoke` 和 `os_qemu_functional_smoke`。两个系统用例
均受总截止与静默截止约束；出现半发布竞态时应修复提交顺序，不能延长超时。

## v1.13：单调时间、deadline 与 timed wait

### sleep 在系统进入 idle 后永远不醒

检查 deadline 解析是否错误依赖 `CurrentThreadIndex` 有效或 IRQ 来源为 Ring 3。
最后一个 Running Thread 阻塞后 current Thread 可以为空，但 PIT IRQ0 仍必须
推进 MonotonicClock、解析队首并设置 need-resched。`sti; hlt; cli` 是正确
空闲路径；用忙等 Thread 掩盖问题会破坏本阶段验收。

### 条件通知和 timeout 让同一 Thread Ready 两次

确认 `WakeThread` 与 `ExpireNextDeadline` 使用同一 scheduler irq-save lock，
并以 Thread 的 Blocked 状态作为唯一提交点。普通 wake 必须取消 deadline，
timeout 必须从原 WaitQueue 摘除。失败方看到已经 Ready 的 Thread 后只能返回
未完成，不能覆盖 wake reason。

快速复现：

```bash
ctest --test-dir build/developer \
  -R '^(os_kernel_deadline_queue_unit_tests|os_kernel_deadline_scheduling_integration_tests|os_kernel_deadline_queue_randomized_tests)$' \
  --output-on-failure
```

### 单调时间缓慢漂移或突然变小

漂移通常表示每次 tick 的整数余数被丢弃；突然变小通常表示
`divisor * 1e9`、余数相加或累计纳秒发生 64 位回绕。检查时同时记录输入
频率、实际除数、余数和饱和位，不要把目标 1000 Hz 当成实际硬件周期。

### QEMU 偶发达到墙钟预算

先查看最后一条来宾里程碑和是否存在遗留调试模拟器：

```bash
pgrep -a qemu-system-x86_64
ctest --test-dir build/developer \
  -R '^os_qemu_(functional_smoke|stage1_load_success)$' \
  --output-on-failure
```

32 GiB TCG 初始化和全 RAM 管理天然比 256 MiB 慢，因此 runner 使用分档但
有界预算，CTest 外层预算略大。若日志在同一 guest 状态停住，应修复等待或
资源问题；只有日志持续前进但被宿主负载截断时才调整有界预算。任何退出路径
都必须 terminate、wait，发布前不得残留后台 QEMU。

## v1.14：进程信号、可中断等待与 sigreturn

### 信号已经 pending，但用户 handler 始终不执行

先分清 Process pending、Thread pending 和 Thread mask。普通 Process 信号只
应交给一个未屏蔽该信号的 Thread；所有 Thread 都屏蔽时仍保留 pending，不能
提前丢弃，也不能广播给所有 Thread。检查用户返回边界是否同时覆盖系统调用和
硬件中断，并确认选中 Thread 在安装 signal frame 后才提交 pending 消费。

局部状态机可用下面三层证据复现：

```bash
ctest --test-dir build/developer \
  -R '^(os_kernel_signal_manager_unit_tests|os_kernel_signal_wait_integration_tests|os_kernel_signal_manager_randomized_tests)$' \
  --output-on-failure
```

### descriptor wait 收到信号后返回位置错误

可重启等待必须保存原系统调用号，并在 handler frame 中把恢复 RIP 精确回退
到两字节 `syscall` 指令；不可重启等待只返回 `Interrupted`。不能对 sleep、
futex timeout 等带时间语义的调用盲目重启，否则相对等待会被延长。若 handler
返回后陷入非法系统调用，检查 `RAX` 恢复、RIP 回退和阻塞 wake reason 是否
由同一个提交者一次性决定。

### 伪造 signal frame 能恢复任意 RIP/RSP

`SignalReturn` 不能只相信用户栈上的 magic。它还必须匹配 Kernel 保存的精确
frame 地址、一次性 cookie、信号号和 restorer，并重新验证 canonical 地址、
RFLAGS 白名单、用户段、RIP 的 R-X 映射和 RSP 的 RW-NX 映射。失败时只终止
提交坏 frame 的 Process；若整机 panic 或旁系 Process 消失，应检查错误是否
错误穿透到 Ring 0 异常路径。

整机探针必须依次出现 `RESTART_WAIT_VERIFIED`、`MASK_COALESCE_VERIFIED`、
`PROCESS_GROUP_VERIFIED`、`FORK_INHERITANCE_VERIFIED`、
`BAD_FRAME_ISOLATED` 与 `DEFAULT_TERMINATION_VERIFIED`。缺少哪一项，就沿该
项前最后一条带宿主到达时间的 QEMU 日志定位，不能只延长超时。

## v1.15：TTY、前台组与作业控制

### Ctrl-Z 只输入了字母 z，没有停止前台程序

QMP `send-key ctrl-z` 产生 Set 1 Ctrl make、Z make/break、Ctrl break，不会
直接写入 `0x1A`。先运行 `os_kernel_device_model_unit_tests`，确认左右 Ctrl
和扩展 `E0 1D` 都维护修饰状态，再检查 TTY 是否收到
`TerminalInputAction::StopForeground`。若字母可见而 `TTY_STOPS` 为零，
故障在扫描码修饰翻译；若 `TTY_STOPS` 增加但 `DEFAULT_STOPS` 为零，故障在
foreground PGID 或组信号。

### Ctrl-Z 后整机无 Ready Thread，CPU 忙转或永久卡住

停止当前 Process 时，scheduler 必须跳过该 Process 所有 Ready 节点，并选择
仍可运行的 Shell。检查 `HasSchedulableReadyThread`，不能只检查 run queue
非空；队列可能只含 stopped 成员。没有可运行 Thread 但仍有可唤醒等待时，应
进入既有 `sti; hlt; cli` idle，而不是反复 yield。

### `fg` 后程序没有继续，或 Shell 再也读不到输入

按顺序检查：

1. SIGCONT 是否产生 `DEFAULT_CONTINUES` 和 Continued event；
2. stopped Thread 是否重新成为 Ready；
3. `fg` 是否先把 TTY foreground PGID 交给作业；
4. 作业退出/停止的所有路径是否把 TTY 恢复为 Shell PGID。

`FOREGROUND_JOB_WAITING` 是 QEMU 控制键注入屏障，不等于完成标记。只有新的
`DEFAULT_CONTINUE_DELIVERED` 才允许注入 Ctrl-C，只有新的 `COMMAND_COMPLETE`
才允许发送下一条命令。

若已显示 `^C`/`^Z` 却没有投递记录，检查组信号入口是否错误依赖
`thread_scheduler.IsActive()`：所有用户线程阻塞时 CPU 会合法进入 idle，但键盘
IRQ 仍必须向现有进程组排队信号并唤醒目标。另需在把线程放入 wait queue 后、
释放 scheduler lock 前复查 eligible pending signal，封闭“已排队但尚未阻塞”的
lost-wakeup 窗口。

### 极短命令偶发显示“操作失败”，未知命令 marker 丢失

这是 fork 后 parent/child `setpgid` 的生命周期竞态。child 可能在 parent
设置组前已经退出。进程级 signal/PGID 身份应保留到 Zombie 被 wait 收集；
Thread signal 状态仍在退出时释放。若退出时提前删除组身份，单个 `unknown`
就可能随机复现。不要用 sleep 调整父子先后。

### TTY 统计 submitted 比 read 大

Ctrl-C、Ctrl-Z、Ctrl-D 和退格本来就不会全部交给用户。检查精确等式：

```text
submitted = read + buffered + editing + dropped + consumed
```

若只比较 submitted/read，控制键测试必然误报；若等式本身不成立，则逐分支
检查编辑字节被撤销时是否同时计入 consumed。

## v1.17：journal、checkpoint 与 mount replay

### mount 在读取 superblock 前报告 journal Corrupt

这是预期顺序：格式 3 的 home superblock 也可能处在待 replay 状态，不能先
信任它。先用宿主工具检查固定 journal 区的 header/commit：

1. magic/version 是否分别匹配 journal format 1；
2. sequence 和 entry count 是否一致且 count 在 1..124；
3. header、descriptor、payload、commit CRC 是否正确；
4. target 是否越界、落入 journal 或重复；
5. descriptor reserved 字段是否仍为零。

不要把 Corrupt 自动当成 incomplete 并清空。只有完整有效 header 且缺少匹配
commit 的 prepared transaction 才允许 discard；看似存在但字段损坏的记录
必须拒绝，避免伪造旧/新选择。

### checkpoint 失败后同一个文件系统实例不再接受写入

commit 一旦稳定，新事务已经成为恢复权威。若 checkpoint 或 FLUSH 失败，
当前实例必须冻结，且不得清除 header/commit。重新创建设备与 rootfs 实例后
mount 应 replay 并报告 `replay_count=1`；第二次 mount 应为 Clean。

若第一次失败后 journal 被清空，检查错误路径是否错误调用
`ClearPersistentState`。若当前实例继续 mutation，检查
`FailDeviceOperation` 是否遗漏 frozen 状态。

### 断电矩阵出现部分旧、部分新 home block

先定位 fail point 位于哪次 Write/Flush：

- commit FLUSH 前出现部分新 home，说明 commit 前错误覆盖了 metadata home；
- commit FLUSH 后仍部分旧，说明 replay 没有在验证全部 payload 后 checkpoint；
- 第二次 Recover 再次报告 Replayed，说明 clear 或最后 FLUSH 失败却被吞掉；
- 相关数据未稳定但 inode 已提交，检查 rootfs cache sync 是否在 prepared
  journal 之前。

测试必须从同一基线镜像重建每个 fail point，不能把上一个 crash 的残留状态
继续用于下一个点。

### transaction 报 CreditsExhausted

首先区分 Begin 拒绝和 Stage 中耗尽。Begin 的 reservation 必须为 1..124；
Stage 对相同 target 的再次写不应新增 credit。若普通小操作耗尽，检查 target
是否误用绝对 LBA、同一 metadata block 是否因地址换算错误被当成多个 target。

耗尽后 home metadata 必须保持不变，superblock/statistics/allocation hint
恢复到事务前快照。metadata 结构一致但文件旧数据内容改变不一定是 journal
错误：ordered metadata 模式不提供 data rollback。

### QEMU 有 journal 日志但跨启动内容丢失

依次检查：

1. `ROOTFS_JOURNAL_READY` 是否两次启动都出现；
2. 第一次启动 commit count 是否增加；
3. ATA FLUSH 是否成功而不是只完成 BlockCache copy；
4. runner 第二次启动是否复用同一个 raw disk，而非重新打包；
5. 第二次启动 replay/discard/checksum 摘要；
6. Python fsck 是否从根重算到目标 inode/data。

不要用增加超时掩盖缺失的第二次启动 marker；跨启动 runner 的每个阶段都应有
有限 deadline 并在失败时保留最后一条宿主到达时间。

局部回归命令：

```bash
ctest --test-dir build/developer \
  -R '^(os_kernel_terminal_unit_tests|os_kernel_job_control_unit_tests|os_kernel_terminal_job_control_integration_tests|os_kernel_job_control_randomized_tests|os_qemu_functional_smoke)$' \
  --output-on-failure
```

## v2.2：控制序列、append 与用户栈

### `&&`/`||` 在引号内仍被切开，或后段语法错却执行了前段

先单独运行 Shell execution 单元测试。顶层切分器必须与管线解析器使用相同的
单引号、双引号和反斜杠状态；它只记录源行 span，不解析单个 `|`。随后确认
`ParseAndExecute` 先遍历全部 span 调用验证 helper，第二次遍历才按退出码执行。
不要把 8 个完整计划一次性放到用户栈上，也不要边解析边执行。

### `>>` 第二次写覆盖第一次内容

确认 append flag 同时经过 ABI open mask、VFS OpenOptions、Process open 和
FileDescription status flag。只在 open 时把 offset 设到文件尾是不够的；每次
write 都必须重新读取当前 vnode 长度。若同一个 fd 的 duplicate 表现不同，
说明 append 或 offset 被错误放进 FileTable descriptor flags，而没有保存在共享
FileDescription 中。

### 宿主单元测试通过，但 QEMU 报 `USER_RETURN_REJECTED`

先查看失败时 RSP 是否紧邻用户栈已提交页的下边界。调试构建不会消除大型局部
对象：如果 caller 同时保留 `ShellExecutionPlan`、4096 字节路径和 child 参数，
嵌套调用可能跨过按需栈增长允许的单页窗口。当前约束是单个计划小于 4096 字节、
argument offset/length 使用 16 位、可执行路径缓冲精确为 518 字节，并用独立
helper 分隔验证计划与执行计划的生命周期。不要通过放宽返回地址校验掩盖栈布局
错误。

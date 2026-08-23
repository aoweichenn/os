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
请求队列是否在 STI 前初始化，以及 PIC mask 是否为 `0x3FF8`：master bit 2、slave bit 6
和 bit 7 分别决定级联、IRQ14 与 IRQ15。primary 请求看 IRQ14，secondary 请求看 IRQ15。

若 IRQ14 计数增长但请求不完成，按操作检查状态阶段：

- Read 要求 IRQ 时 DRQ=1，再从 data port 读 256 个 word；
- Write 在命令后轮询到 DRQ 就立即写 256 个 word，之后的 IRQ 负责完成；
- Flush 没有数据阶段，只等待 BSY 清除且 ERR/DF 均为零。

若 PIT 报 TimedOut，确认 ResolveTimeout 先冻结结果再执行 SRST；不要在设备
仍 BSY 时直接签发下一请求。迟到 IRQ 返回 RequestAlreadyResolved 属于可诊断
竞争，不能覆盖 TimedOut。

若 shared alias 可见但 sync 后文件未更新，检查顺序是否为“重新写保护 PTE
→ Dirty/Error 写回 → VFS sync → ATA FLUSH”。Error 页不得被 Trim 淘汰。
若同一 inode 出现两个 cache identity，检查 rootfs 是否错误地把 transaction
generation 写回 VFS mount generation。

### BlockIo Worker 或 IRQ15 probe 不完成

先看 `PIC_MASK=...3FF8` 与 secondary ATA IRQ15 计数，再区分设备解析和 Worker 交付。
IRQ15 只允许把 completion 发布到设备队列并递增通知 generation；DMA 回拷、协调器 Complete
与 WaitQueue wake 都应出现在 completion Kernel Thread。若设备已有 completion 而 Worker
仍睡眠，检查它是否在关中断区复核 notification generation 后才提交
`BlockIoCompletion` 阻塞。

若 coordinator 报 owner/request 不匹配，比较提交时的 Kernel Thread index、64 位 request
id 与 ticket generation，不能用槽位或 ATA 通道号代替身份。3b 后普通 primary 必须看到
非零 `ROOT_ASYNC_OPERATIONS`，发生匿名换出的 reclaim profile 必须看到非零
`SWAP_ASYNC_OPERATIONS`；registration/wait/completion 必须非零且严格相等。early boot 或
受限 Kernel worker 的同步回退不应伪增这些计数。

地址空间销毁失败现在额外输出 stage、virtual address、physical address 与 detail status；
`ReleaseBacking` 阶段的 detail 会保留 file backing close status。先修复最早非零的精确
阶段；不要把后续资源快照不一致当作根因。

### QEMU 捕获器为什么使用里程碑和总截止

先看带宿主时间戳的最后一行。一次固定墙钟预算耗尽不能单独判定 PIT 或 PIC
错误；但扩大预算后总在同一来宾标记停顿，就应检查硬件状态与模拟器版本差异，
不能继续用“runner 较慢”解释。无限延长超时同样会掩盖真实停滞。

QEMU 捕获器因此使用两个边界：

1. 逐行观察当前用例的最后一个必需里程碑；到达后保留短暂收尾窗口并回收进程。
2. 未到达时，64 MiB、256 MiB 与 4 GiB 预分配档分别使用 150、300、420 秒
   总失败上界；QMP 建立另有 30 秒上界，外层 CTest 保留进程回收余量。

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

若修改 runner 后卡住，先单独执行唯一 4 GiB 主规格和高内存交付路径：

```bash
ctest --test-dir build/developer -R '^os_qemu_primary_smoke$' --output-on-failure
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
`os_qemu_primary_smoke`。两层都有截止时间；不要通过扩大 timeout 掩盖
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

最后运行 `os_qemu_primary_smoke`。系统用例受总截止与静默截止约束；出现半发布
竞态时应修复提交顺序，不能延长超时。

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
  -R '^(os_kernel_terminal_unit_tests|os_kernel_job_control_unit_tests|os_kernel_terminal_job_control_integration_tests|os_kernel_job_control_randomized_tests|os_qemu_primary_smoke)$' \
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

### 方向键无效，或前台 `cat` 逐字节读取而不等 Enter

方向键应由 Set 1 decoder 产生 key event，再由 IRQ 路径提交 `ESC [ A/B/C/D`
三个字节。Shell 等待输入时模式必须是 ShellEditor；执行外部管线前必须是
Canonical。若 `SetTerminalInputMode` 返回权限错误，检查 caller session/PGID
是否同时等于 controlling session/foreground group；若返回参数错误，检查输入
FIFO、canonical edit 和 EOF pending 是否尚未清空。

### 历史重画后出现残字，或 clear 只打印转义字符

确认 stdout 仍经过 TerminalDevice 到 VgaTextConsole，CSI parser 必须消费
`ESC [`、有界十进制参数与最终字节。Shell 重画使用 `CR + CSI 2K`，clear 使用
`CSI 2J + CSI H`。转义字节仍进入宿主转录用于复现，但不能占 VGA 字符单元。

### date 报设备失败或日期跳变

先检查 CMOS status A 的 update-in-progress 是否在有限轮询内清零，再比较两份
完整快照。status B 决定 BCD/binary 和 12/24 小时，12 小时 PM 位不属于小时
数值。世纪寄存器为零时项目只对 QEMU PC 回退到 20；任何非法月/日/时都必须
返回设备失败，不能输出部分日期。RTC 失败不能影响 monotonic deadline。

## v2.5 内存压力与 swap

### 启动停在 `USER_EXECUTION_FAILED=0x11`

同时读取 `SWAP_INITIALIZATION_STAGE`：1 表示 `SwapStorage` 已验证 `OSSWAP01`
superblock 并提交新代次，2..5 依次表示 manager、pressure、overcommit 或 ATA
槽自检完成。值仍为 0 时，先确认 QEMU 命令含 secondary IDE 的交换盘，镜像精确
为 30534537216 字节；再检查 `0x170..0x177`、`0x376` 和 superblock FNV-1a。
不能降级为 rootfs 中的稀疏交换文件。

成功启动必须依次看到 `MEMORY_PRESSURE_READY`、`SWAP_READY` 和
`SWAP_SELF_TEST_PASSED`。后两者缺失时不要继续启动 PID 1，也不要把 swap 静默
降级为关闭状态。

### 缺页在 low 水位附近反复失败

先读 `/proc/meminfo` 的 `resident_limit_bytes`、`swap_free_bytes`、
`committed_bytes` 和 `commit_limit_bytes`。再比较最终聚合的 clean cache、swap
active、OOM invocation/victim。用户分配会回收到 high；内核紧急分配只保证
min 保留，不保证用户 fault 一定成功。

若 active swap 为 0，`FindSlot` 必须立即返回 MappingNotFound。非空时从身份哈希
桶开始探测，tombstone 继续、当前代的 Empty 停止；不得顺序扫描 7340032 个槽。
若重启后仍找到旧槽，检查 superblock 代次是否先于 manager 初始化落盘。

### swap 损坏后进程退出

`SwapManager::LoadAndRelease` 返回 ChecksumMismatch 时槽必须仍为 active。候选
frame/PTE 由 user_memory 逆序释放，相关 Process 随后被隔离；unmap/exit 再释放
该槽。若损坏后 active 直接减少，说明唯一副本被静默丢弃。

### 读取 uptime/free 时出现 `#DF`，RIP 落在 `CpuLocal::IncrementCounter`

这通常不是 CpuLocal 计数器损坏，而是 IRQ 进入时 KernelStack 已越过 guard。
不要在 procfs snapshot 回调中按值构造含数组的 `ProcessRuntimeStatistics`；只取
轻量 `ProcessObservationSnapshot`。本项目曾为读取一个 OOM 计数把完整统计对象
放入 16 KiB 栈，随后 PIT 入栈触发 guard fault 并升级为 double fault。

### QEMU 仍有进度但撞到总超时

先看带来宾单调时间的最后 marker。持续产生 fork/exec/wait、工具输出或管线
marker 说明是宿主 TCG 降频，不是来宾死锁；无新 marker 才按逐步进度超时处理。
4 GiB `-mem-prealloc` 会在 QMP greeting 前实际触碰后备页；QMP 启动预算为 30 秒，
与后续来宾总预算分离。若进程 RSS 没有接近 4 GiB，先检查命令是否缺少
`-mem-prealloc`。非黑 VGA、最终 READY、资源守恒和单步进度门禁不能删除。

### 子进程打印 `EXIT_PID` 后系统停止

检查 `[OS][KERNEL][FATAL] EXIT_STAGE`：1=FXSAVE，2=futex 取消，3=shared file
writeback，4=FsContext，5=scheduler，6=signal thread，7=UserAddressSpace，
8=RSP0，9=CpuLocal，10=下一个 Thread 激活。带状态的阶段同时打印 `EXIT_STATUS`。
这些日志写入内存系统日志，不向活动 TTY 逐进程输出。

## v2.6 发布冻结

### `audit-release-identity` 报部分升级

错误会指出实际消费者和预期值。不要只改 CMake 绕过：Kernel terminal banner、
PID1/参数探针 `OS_STAGE`、Shell banner、QEMU 环境输出、README 和 v2.6 发布记录
必须一起更新。ABI 和 rootfs 版本只有真实兼容变化才能调整。

### `qemu-soak` 在启动前拒绝镜像

长稳只接受 `st_blocks * 512 >= st_size` 的 rootfs 与交换盘。先用
`materialize-image` 创建不同目标路径；工程 `boot_disk.img`/`swap_disk.img`
保持稀疏是正常的，不能传给发布 soak。若 `posix_fallocate` 返回 ENOSPC，检查
实际挂载点而不是只看容器中的另一个 `df`。

### 发布清单 SHA 与最终提交不一致

清单只接受 40 位小写 SHA。最终文档提交会改变 `HEAD`，所以主仓推送后必须用
新 SHA 重新生成清单，再让网站同步；不得手改 JSON 中的 SHA。

## v2.7 通用块设备层与 NVMe

### 块请求在提交前返回 InvalidGeometry/InvalidRequest

先读取驱动声明的 logical block size/count、maximum transfer、maximum
outstanding、write 和 Flush 能力。buffer size 必须是逻辑块大小的整数倍；
`LBA + block_count` 用减法边界检查，不能依赖可能溢出的直接加法。ATA 适配应
固定为 512 字节、`0x10000000` 块、单块和深度 1。

若队列仍有空槽但 `IssueNext` 没有签发，比较当前 Issued 数与设备深度；这不是
容量耗尽。若多个请求同时到期，`ResolveTimeout` 每次只返回 deadline 最早、再按
identifier 最小的一项，调用者必须继续处理其他到期请求。

### 请求已 Completed 但 owner 没收到通知

先比较 `completed_request_count`、`completion_delivery_count` 和 `reap_count`。Completed
请求必须在 completion FIFO 恰出现一次；IRQ、timeout 或 cancel 不能只改状态而漏掉
入队。`TakeCompletion` 按解析顺序交付并同时 Reap；恢复路径若按 id 直接 Reap，必须从
FIFO 中间摘除。若 owner 已退出，后续增量应走显式 abandoned/cancel 语义，不能把旧
buffer 地址继续交给驱动。

### 异步设备 Submit 成功但 ATA 没有发命令

公共 Submit 只取得请求所有权；ATA adapter 会尝试启动首项，后续 queued 请求由
`InterruptRuntime::ServiceAtaRequests` 在完成或 timer 路径继续启动。若 queue 中有 Queued
但 Issued 为零，检查 Service 是否先 TakeCompletion 再调用 StartNext；先启动后回收会让
单深度设备一直看到旧 active 请求。

### NVMe completion 找到错误 owner 或 CID 回绕后串单

公共 request identifier 是 64 位单调身份，CQE 中的 16 位 CID 只用于查硬件槽。检查
`FindIoCommandSlot` 和 `FindIoRequestSlot` 是否被混用，以及 free slot 是否同时清空两种
身份。上层、WaitQueue 和日志不得把 CID 当 request id 保存。

### NVMe Read 完成但 buffer 仍是旧数据

IRQ 只把 slot 标成 Completed 并追加完成链，不执行大块复制。确认 TakeCompletion 在非
IRQ 上下文调用、slot 尚未复用，并在发布 completion 前把驱动 DMA 页复制到 caller
buffer。若 reset 发生，成功完成必须保留；未完成 Read 只能交付 DeviceError，不能复制
不完整 DMA 内容。

### NVMe 负载完成后停在 FILE_CACHE_RECLAIMED

生产 root/swap controller 不应在这里关机；它是事件循环仍可使用的系统级持久资源。
确认 KernelMain 没有在 `FILE_CACHE_RECLAIMED` 后调用 `ShutdownNvmeStorageRuntime`。独立
probe 或 EIO/timeout recovery 才负责 shutdown，并继续要求 DMA/MMIO/PCI/MSI-X 回收。

### Cancel 返回 RequestInProgress

这是已签发硬件请求的 best-effort 拒绝，不是队列损坏。只有 ATA queued 请求可直接变为
Cancelled；ATA active 和已经写入 NVMe SQ 的请求仍由正常 IRQ、timeout 或 reset 解析。
调用方必须继续保留 buffer，直到收到唯一终态 completion。

### wait 已看到退出，但 ReapZombieProcess 返回 ProcessNotZombie

这是退出事件发布顺序错误，不是可重试的普通竞态。若 ProcessTree 先标记 Zombie，而
Scheduler 的当前 Thread 还没有完成 `TerminateCurrentThread`，共享 ChildProcess WaitQueue
上任意其他子进程的唤醒都可能让父进程提前收集树条目；重试随后只会得到 NoChild，形成
信号、作业控制或调度器资源残留。

检查 `CompleteCurrentThread` 顺序必须是：完成 writeback/close/address-space 清理，提交
Scheduler Zombie，发布 ProcessTree exit，最后 wake parent。失败日志中的
`WAIT_EVENT_CLEANUP_STAGE=1` 与 `ProcessNotZombie` 表明该不变量被破坏，不能在 shell 中
循环重试掩盖。

### User Kernel 续体恢复后立即页故障或 SWAPGS 漂移

同时核对四种所有权：目标 Kernel RSP 必须落在该 Thread 的 stack，FX state 必须已保存，
原生系统调用入口方法与 IA32_GS_BASE/IA32_KERNEL_GS_BASE 必须成对恢复，CR3 模式必须匹配
续体阶段。退出/exec 在地址空间销毁后恢复时只能使用 Kernel page table；普通 I/O 返回才
重新激活用户 CR3。不要在前任 User Thread 的 Kernel stack 上准备后任返回，应先回到
dispatcher。

### ATA 异步 Write 提交后没有首个 IRQ

PIO Write 的设备握手与 Read 不对称。写命令后先轮询 DRQ，并在 DRQ 有效时立即向 data port
传输扇区；设备通常只在数据阶段结束后产生完成 IRQ。若实现等待“首个写 IRQ”再传数据，
QEMU 会永久等待 host data，BlockIo owner 也不会被唤醒。

### 上层代码开始判断 ATA 或 NVMe

这是分层错误。VFS、rootfs、journal、swap 和页缓存只能持有 `BlockDevice` 并
使用逻辑块契约。PCI BDF、BAR、doorbell、command identifier、phase tag 与控制器
复位只能出现在 PCI/NVMe 驱动；发现上层控制器分支时先修依赖边界，不继续扩散。

### PCI configuration 读到全 `1` 或 BAR 为零

先验证 mechanism #1 地址的 bit31=1、bus 位 23:16、device 位 15:11、function 位
10:8、DWORD offset 位 7:2，低两位必须为零。vendor `0xFFFF` 表示该 function
不存在，不应继续读 class/BAR。

没有外部 BIOS 时 BAR 复位为零是预期状态，不能把零当 MMIO 地址访问。后续资源
分配必须保存原 command/BAR，禁用 memory decode，写全 `1` 读取 aperture mask，
恢复或写入对齐地址，再启用 memory/bus-master；任一步失败都要恢复旧配置。

### NVME_IDENTIFY_FAILED

先按状态值区分 PCI 扫描、BAR、MMIO、CAP/版本、enable timeout、command timeout、
Identify 数据和资源回收。BAR 应回读为 16 KiB 对齐 memory BAR；doorbell 末地址
必须落在 aperture 内。CSTS.CFS 置位时立即停止提交，不能继续等待 phase。

若 command timeout，检查 SQE 为 64 字节、CQE 为 16 字节，AQA 深度使用零基值，
ASQ/ACQ/PRP1 是 4 KiB 对齐物理地址；提交先写 SQE 再执行 DMA barrier、更新 tail
doorbell。新 CQ 从 phase 1 开始，只有 phase、CID 和 status 同时匹配才算完成。
`NVME_IO_READY` 之前必须出现 `NVME_RESOURCES_RECLAIMED`；缺失时检查控制器
是否先清 CC.EN 并等 RDY=0，再解除 MMIO 和释放 DMA 页，最后恢复原 BAR/command。

### NVME_IO_QUEUE_READY 之后没有 NVME_IO_READY

先检查 Set Features `07h` 的 completion Dword 0 是否至少分配一对 SQ/CQ，再检查
Create CQ1 先于 Create SQ1；QSIZE 是零基值，CQ/SQ 的 PC 必须为 1，SQ 的 CQID
必须为 1。QID 1 的 submission/completion doorbell 索引分别是 2 和 3。

若 Write/Read completion 失败，核对 opcode `01h/02h`、NSID 1、CDW10/11 的
64 位 SLBA、CDW12 的零基 NLB 和 4 KiB 对齐 PRP1。当前最大传输就是一页；超过
8 个 512 字节块必须在提交前拒绝，不能临时填 PRP2 指向不相关内存。Flush opcode
为 `00h` 且不带数据指针。任一 timeout、错误 status、CID 或 SQID 不匹配都会冻结
I/O；随后必须 reset 控制器，不能继续复用可能仍被 DMA 访问的队列。

### MSI-X 到达但系统停在 Identify

先检查 MSI-X function mask 和 entry vector mask。admin queue 仍可能使用 vector 0；
在 CQ1/SQ1 创建前解除屏蔽会让中断入口看见尚未初始化的 I/O CQ。正确顺序是映射
table、写入 LAPIC address/data、保持两级 mask，完成 Identify 和 Create CQ/SQ 后
注册活动控制器并 unmask。ISR 必须 drain 到 phase 不匹配、更新 CQ1HDBL，再写 LAPIC
EOI。

### EIO 或 timeout 后仍有 DMA 资源

错误 completion 和 timeout 都必须先冻结新提交。reset 顺序固定为 mask MSI-X、
CC.EN=0、等待 CSTS.RDY=0、清 admin/I/O queue 和 CID 槽位、重新 enable、Set
Features、Create CQ/SQ、最后 unmask。若 RDY 无法归零，宁可保留 DMA 页并报告失败，
不能释放仍可能被控制器访问的 PRP list。`NVME_RESET_READY` 只在 reset 计数和资源
回收都成立后输出。

### 双 namespace 存在但 STORAGE_BACKEND 仍是 ATA

先确认 controller 使用显式 `nvme-ns`，root 为 NSID 1、swap 为 NSID 2；简单
`-device nvme,drive=...` 只创建一个隐式 namespace，只会进入驱动自检后回退。
检查两次 Identify 的逻辑块数：root 应为 `0x10000000`，当前 swap 镜像应为
`0x038E0008`。任何初始化失败都必须先 shutdown 并证明资源回收；`ResourceLeak`
不得继续 ATA 回退。

### NVMe 工作负载结束时报进程资源泄漏

进程资源基线最初建立在 NVMe 初始化之前时，73 个 DMA 页和 MMIO KVA 会被误算为
进程泄漏。双 namespace 选定后、首个用户进程创建前必须刷新持久内核资源基线；
进程结束仍与该基线比较，最终文件系统 sync 后再 shutdown controller，并与 storage
初始化前的 frame/KVA 统计比较。不能简单从最终计数中减去固定常量。

## v2.8 动态文件缓存地址空间

### SparsePageIndex Validate 返回 Corrupt

先从叶节点重算 present 与三个状态 bitmap，再逐层比较父 slot 是否等于“子树中
至少有一个对应项”。新分支常见错误是先连接空节点、最后写叶项，却没有从叶向 root
重新传播摘要；Lookup 仍可能成功，但 Validate 和 FindNext 会发现父 bitmap 缺位。

检查最高 level 只能使用 slot 0..15。`UINT64_MAX` 的 root level 应为 10，不能执行
64 位或更大的移位。若 root level 大于 0 且只剩 slot 0，删除路径应提升该子节点并
释放旧 root。

### 插入返回 AllocationFailed 后 Heap 仍有活动块

插入必须先收集本事务申请的全部节点，失败时按逆序释放，不得先把新 root 或缺失
branch 写入现有树。比较 `entry_count/node_count/root_level` 应与调用前一致；累计
`allocation_failure_count` 和 `rollback_node_release_count` 增长是预期诊断。

若 `FileCacheAddressSpace` 已先申请 Page 元数据，index 插入失败后还必须释放 Page；
tiny-heap 用例最终要求两层 `Validate` 成功且 Heap `allocation_count=0`。

### 状态 mark 与页面状态不一致

地址空间状态变更顺序是清旧 mark、设新 mark、更新状态计数、最后发布 Page state；
设置新 mark 失败时先恢复旧 mark。Clean 没有独立 mark，FindNext(Clean) 通过 Present
遍历后过滤。Dirty/Error 不能直接 Remove，Writeback 或引用非零返回 Busy。

### VFS buffered read 递归直到栈耗尽

cache miss 的 reader 不能调用 `Vfs::Read` 或 `ReadAt`，否则再次进入同一 cache hook。
检查 FileBacking 和 VFS page reader 都必须调用 `ReadUncachedAt`。公开 read 只负责向
调用者交付字节和统计；uncached 入口只调用 superblock backend，不再次统计。

### Loading 长期不消失或同一页读取两次

miss 必须先在锁内插入 Loading，再执行 `ReadUncachedAt`。reader 回调中检查
`CurrentSpinLockDepth()` 应为零。未配置运行时回调或 early/受限上下文中的同页递归 Acquire
仍应返回 EntryBusy；合格 User Thread 的冲突必须登记 `FilePageLoadToken`，不能再次进入
reader。

若发生 lost wakeup，检查登记是否仍在 cache lock 内、`PrepareWait + BlockCurrentThread`
是否共用 scheduler lock，以及 owner 是否在同一 cache 临界区完成。成功广播前还必须先按
登记数 Retain；否则 owner 先 Release 后，reclaim 可能让 waiter 再次 miss。核对最终
`BEGINS=COMPLETIONS`、`WAITERS=RESULT_TAKES`、`WAIT_COMMITS=BROADCAST_WAKES` 和
`ACTIVE=0`。

source read 失败时按 entry metadata、frame、空 address-space 的逆序清理，再广播同一
failure；任一计数、token、WaitQueue 或预留引用不归零都会让 coordinator、
`FilePageCache::Validate` 或 ProcessRuntime 资源门禁失败。登记后的等待/完成协议损坏属于
不可恢复内核错误，不应返回到可继续释放 buffer 的调用链。

### 预读窗口意外放大或随机读仍持续预取

5a 先检查输入 trigger，而不是检查设备：DemandHit 不提交；DemandMiss 只在初始、连续或
回到第 0 页时建窗；PrefetchedHit 必须覆盖当前 `trigger_page_index`。若随机读仍保留旧窗，
核对 `first_page_index` 是否错误地等于 `next_expected_page_index`，以及调用方是否把普通
cache hit 误标为 PrefetchedHit。

32 页默认配置的单页流应为 4、8、16、32。增长异常时对照 decision 的 window 与 prefetch
范围：首个 demand 窗口包含请求页，真正预取数会少一个；后续全异步窗口两者相等。EOF
附近窗口必须按 `file_page_count` 裁剪，裁剪到只剩 demand 区间时 action 为 None。

窗口在压力下不收缩时检查传入的 `MemoryPressureLevel` 和 effective/adaptive/configured 三个
上限；BelowMinimum 必须为 0 并清窗。feedback 只影响下一计划，不会改写当前 generation；
若测试期待当前窗口立即缩短，测试本身违反提交稳定性。

5b 生产路径无提交时依次检查 VFS observation 的 `cache_used/requested_page_count`、共享
FileDescription 的 schedule 统计、64 槽请求 FIFO 和第四个持久 WorkHandle。队列满只应
增加 rejection 并关闭刚 retained 的 OpenFile；成功入队后无法 queue/wake 属于 fail-stop。
若 worker 卡在同页 Loading，检查 owner availability 是否允许当前预读 Kernel Thread，
waiter availability 是否仍只允许 User Thread，不能把两者重新合并。

命中统计异常时检查新 fill 是否在广播前标记 prefetched、首次 Demand 是否原子消费、对
既有页的 Prefetch 是否错误重标。最终应满足 `successful prefetch = resident + hit + waste`；
trim 后 resident 必须为零。逐页日志会改变调度，诊断只能使用最终聚合和强制重叠 host 测试。

5c 若出现 close 后任务泄漏，先核对 `enqueue=completion+queued cancellation+active`，再检查
每个 cancelled queued request 是否同时 close retained OpenFile 和 release stream task。
running 只能置标志，不能从槽中复制出来释放。stream 长期停在 Retiring 时检查 worker 的
完成、取消后定向 `DiscardPrefetched` 和 `ReleaseTask` 顺序。

策略没有收到 waste 时对照 cache 页的 stream/generation tag、ledger recorded/taken/stale/
pending 守恒，以及 FileDescription 是否在 Retire 前执行 cancel→take。独立 open 的 token
不同；把 consumer 的 token 写回 producer 页会让 feedback 错投。truncate 返回 Busy 时先
确认是否仍有已提交的单页 Loading；5c 不伪造设备硬取消，完成后重试才是合法路径。

### Dirty 超过软水位但 worker 不运行

4 GiB 当前容量 8192 页，hard limit 约 1638 页，后台阈值约 819 页，目标约 409 页。
检查 `background_writeback_requested` 是否在首次越过软水位时置位，以及
`OsKernelPrepareUserReturn` 是否调用 `ScheduleRuntimeFileWritebackWorker`、WorkItem 是否
Queued/Running，以及 `KernelWork` waiter 是否被唤醒。user-return 只提交；实际 VFS I/O
必须在 Kernel Worker 上运行。timer IRQ 只能到期 scheduler deadline 和请求重调度。

每批最多 64 页。写回失败后 requested 清零、paused 置位且页面保持 Error；这是防止
每次用户返回都重试的设计，不是 worker 丢失。显式 sync 成功后才解除暂停。硬水位
仍不下降时，下一次普通 write 应返回设备/容量错误，不能无限等待。

### `/proc` 内容读取后不再变化

检查对应 Superblock 的 `cache_regular_file_data`。当前只允许 rootfs/legacy
为 true；procfs、devfs、memfs 必须为 false。不能在通用回调里按 BackendKind 猜测，
能力应由 superblock 显式声明并由 VFS 执行。

### buffered write 后 shared 映射仍是旧内容

普通写不得再调用全文件 revoke/invalidate。确认 VFS 进入 write cache hook，Acquire
返回的物理地址与 shared PTE 相同，并且 MarkDirty 在复制前成功。若 partial write 使用
写-only fd，fill 仍可通过内部 `ReadUncachedAt` 读取旧页；公共 Read/ReadAt 自身继续
检查 readable 权限。

若原 fd 关闭后 sync 返回 FileWriteFailed，检查 `RetainWritebackFile` 是否在修改页面前
建立 VfsWriteback 描述符，以及 `WritePage` 是否调用 `WriteUncachedAt`。让 writeback
再次进入公共 WriteAt 会递归脏化同一页；让描述符过早关闭则会破坏 unlink/orphan
期间的 inode 生命周期。

### truncate 后尾页出现旧数据或 cache 返回 Busy

缩小前应对每个仍存活地址空间调用 `TruncateUserFileMappings`，只释放文件 page offset
不小于新 EOF 的驻留页。VMA 与 FileBacking 保留，但其 size 在后端 truncate 成功后
统一更新；再次 fault 到 EOF 外必须失败。

`FilePageCache::Truncate` 先扫描待丢弃范围。仍有引用或处于 Writeback 时返回 Busy，
不能先删一部分页；Clean/Dirty/Error 通过 Discard 移除。保留尾页必须从新 EOF 清到
页末，扩大时还要清旧 EOF 到新 EOF 的已驻留字节。遍历有限 page-index 区间时，处理
到 `last_page_index` 必须立即结束，不能用 `last + 1` 形成 InvalidPage。

### 4 GiB 启动在 FilePageCache 初始化时耗尽

当前唯一验收期望为：4 GiB、order 13、32 MiB metadata、8192 页。metadata block
必须从 buddy 取得并用 direct-map 虚拟地址初始化专用 KernelHeap；不能重新塞回
通用 512 KiB Heap或恢复 BSS 数组。低内存自适应分支不再作为系统门禁。

### NVMe 工作负载完成但没有 STORAGE_SHUTDOWN_READY

如果最后停在 buffered read 统计之后，检查 `FinalizeKernelFileSystem` 的 payload
校验是否重新装入 clean 页。进程资源检查发生得更早，不能替代 storage shutdown
前的 cache drain。必须先 `TrimUserFilePageCache`、确认 resident 为零并输出
`FILE_CACHE_RECLAIMED`，再比较 NVMe 初始化前后的 frame/KVA。

### fsync 每次都重复报告同一个写回错误

先区分 FileTableEntry 与 FileDescription。duplicate/fork 共享后者，所以错误报告后必须
调用 `AdvanceWritebackErrorCursor` 更新共享游标；独立 open 才有不同游标。若新打开的
fd 立即看到旧错误，检查 Register 是否采样 tracker 当前 sequence，而不是固定写零。
最后一个独立实例关闭后 active record 应归零。

### msync 返回成功但指定 shared 范围没有落盘

检查 VA 到文件偏移的换算是否包含 `backing_file_offset_bytes`，末页必须用
`first_offset + length - 1` 求闭 page-index。MS_SYNC 的顺序是全局写保护 writable
shared alias、范围 WritebackFile、VFS Sync/设备 Flush；MAP_PRIVATE 只能验证范围，
不能进入写回。MS_ASYNC 只挂 forced pending，若低于普通软水位却没有 worker，检查
`forced_background_writeback_requested` 是否被普通 target 判断提前清掉。

### 新同步 syscall 返回 InvalidArgument

msync 地址必须 4 KiB 对齐、长度非零并完整落在 file-backed VMA 中；flags 必须恰好
包含 ASYNC 或 SYNC 之一，可再带 INVALIDATE。fsync/fdatasync 当前只接受 regular-file
FileDescription；管道、终端和目录返回 Unsupported，坏 fd 返回 InvalidHandle。

### reclaim-pressure 在 PID1 创建前报告 ExecutableReadFailure

先检查压力磁盘是否使用完整 `OS_STAGE1_ROOTFS_INSTALL_ARGUMENTS`。故障内核镜像生成器
只放 Stage1/Kernel，空 rootfs 会在 3 秒左右失败，却让 runner 等到 deadline。若 rootfs
完整，再检查 resident limit 的应用时点：必须先建立 ProcessRuntime/PID1、同步真实
allocated frame，再降低 controller limit；不能在 32 MiB cache metadata 计入前冻结。

### 压力回收后出现 USER_RETURN_REJECTED

若 stack page status 为 NotMapped，检查跨进程 swap 是否换出了阻塞线程保存现场对应的
用户栈页。PID1 必须排除；单线程进程保护 saved frame 的 RSP 页；存在多个不同活动用户
栈时本轮跳过整个地址空间。不能在 user-return 路径临时分配一页掩盖错误，因为原栈数据
仍在 swap 中。

### 回收统计已输出但 runner 报 FILE_SYSTEM_PAYLOAD_VALID 缺失

检查验收器的最终完成标记是否仍为 `READY`。`runQemuFirmwareBoot` 会在
`requiredMarkers` 的最后一项出现时停止捕获；精确的 9216 页限额应作为一次性计数断言，
不能追加到有序列表末尾。否则捕获会在内存统计处提前结束，后续 payload 校验本身尚未
执行，看起来就像文件系统损坏。

### 内存低于水位却直接 OOM

检查三阶段统计：先有 clean trim，再有 written/reclaimed file，最后才是 anonymous
swap。FileWritebackFailed 或 AnonymousSwapFailed 必须直接返回设备错误；只有执行成功或
NoProgress 且重试仍不足时才调用 OOM。`MEMORY_RECLAIM_NO_PROGRESS` 持续增长通常表示
候选全被引用、活动栈保护或 swap 已满，需要看具体阶段而不是增加无限重试。

OOM profile 中 `MEMORY_RECLAIM_NO_PROGRESS=1` 是预期证据，不应套用普通成功路径的零值
断言。若 `oom_probe` 杀到 requester，先比较 victim/requester 的 resident 页数；测试必须
保证非当前 victim 更大。若日志停在 `OOM_PROBE_REAPED`，检查 runner 是否误把该中间
标记追加成协议终点；最终终点仍必须是 Kernel `READY`。

## v2.9 Kernel Thread 生命周期

### 首次进入 Kernel Thread 后立即 #GP 或 #PF

先核对预构造栈的九个 64 位槽：r15..rbx、RFLAGS、bootstrap RIP、alignment sentinel。
恢复六个寄存器和 flags 后执行 `ret`，C++ bootstrap 入口的 RSP 必须满足 SysV 的
`RSP % 16 == 8`。saved RSP 和 64 字节恢复窗口都必须落在同一活动 KernelStack 内。

### yield 后恢复了错误线程或 CpuLocal 不一致

切换期间必须先关闭中断，保存当前 FXSAVE，再让 ThreadScheduler 提交 Running/Ready，
最后更新目标 TSS.RSP0、CpuLocal、FS base 和 FX state 后更换 RSP。若先开中断，timer
可能在旧栈上观察新 current thread。第一增量不从 Ring 0 timer IRQ 发起抢占。

### Kernel Thread 已 Exited 但动态栈无法销毁

退出函数仍运行在目标栈上，不能原地 `TryDestroy`。有下一线程时先切走；最后一个退出
时恢复 dispatcher stack。只有 `ExecuteReadyKernelThreads` 重新取得控制权后，才按
scheduler reap、KernelStack destroy、runtime slot 清零的顺序处理。

### PID1 的 TID 变成 3

这表示 Kernel/User 共用了同一个低位 TID 游标。User TID 必须小于
`0x8000000000000000` 并继续从 1 开始；Kernel TID 使用独立高位游标。不能通过修改
PID1 验收常量掩盖身份空间污染。

### 全量偶发在 FILE_PARTIAL_ACCESS 前只写回 247 页

V2.9.1 首轮全量的重复 `os_qemu_stage1_load_success` 曾输出 writeback status `0xD`
和 written pages `0xF7`，随后 memory probe 报 `FILE_PARTIAL_ACCESS`；同轮的 primary、
ATA/NVMe pressure 和两条三启动持久化均通过。先检查 Kernel Thread 的九项 marker、
动态栈 active、CpuLocal current 和资源快照，确认均归零后再隔离重跑该测试。

本次隔离重跑与第二轮完整回归都通过，未得到可稳定复现的 Kernel Thread 根因。不要
删除失败记录或放宽禁止 marker；后续把 writeback 迁移到 WorkQueue 时，应重点记录
第一个后端失败的 `fs::Status`、文件身份和 journal transaction 状态，以区分设备波动、
元数据 credit 与 retained backing 生命周期。

### WorkQueue Validate 报 ready 或 delayed 重复

Queued entry 必须只在 ready FIFO 出现一次，Delayed entry 必须只在 heap 出现一次。
取消即时任务用双向链接 O(1) 移除；取消延迟任务按 entry 保存的 heap index 移除并重新
bubble。不要只改 state 而留下旧索引，Validate 会把它识别为重复所有权。

### delayed work 顺序不稳定

最小堆键是 `(deadline_nanoseconds, enqueue_sequence)`。仅比较 deadline 会让相同到期
时间的任务随 heap swap 改变顺序。到期项从 heap 逐个弹出并追加 ready FIFO，不能按
slot index 扫描后直接执行。

### worker 执行失败后 drain 永远不结束

失败 operation 仍必须调用 `Complete(handle, Failed)`，把 Running 提交为 Completed；
失败只进入统计，不暂停通用 worker。若回调异常路径直接返回，running_count 会保持 1，
`EndDrain` 正确返回 DrainIncomplete。

### 回调内观察到 spinlock depth 非零

worker 只能执行 `AcquireNext` 返回的 operation/context 快照。队列锁在 Acquire 返回前
已经释放；禁止新增“便利 ExecuteOne”并在锁内调用回调。生产 VFS writeback 会访问
设备，持 spinlock 执行会导致不可恢复的锁/IRQ 反转。

### User→Kernel 后下一次 SYSCALL 立即失败

检查跨类型分支是否在恢复 user dispatcher stack 前调用 `ClearCurrentThread`。原生
SYSCALL 尾部被放弃后不会再到 `OsKernelSelectUserReturn`，因此必须由 clear 路径收束
system-call depth，并按进入方式决定是否 `swapgs`。最终聚合的
`CPU_LOCAL_CURRENT_THREAD`、`CPU_LOCAL_IRQ_DEPTH` 和 `system_call_active` 必须归零。

### timer 唤醒 Worker 后 IRQ depth 留为 1

User timer IRQ 中若调度器选中 Kernel，C++ 会直接返回 dispatcher，不再回到硬件中断
函数的普通尾部。该分支必须先显式 `LeaveInterrupt`；系统调用触发的返回前重调度则不能
多减一次。用 CpuLocal 当前 interrupt depth 区分，不要在汇编里无条件修改。

### Worker 有即时请求却仍等待 5 秒

读取 WorkItem 状态。即时 `Queue` 遇到 Delayed 必须从 heap 移除、清 deadline、追加到
ready FIFO，并增加 expedited 计数；只返回 AlreadyPending 会保留旧 deadline。提升后还要
唤醒 `KernelWork` WaitQueue 并请求重调度。

### 用户进程全部退出后调度器一直 idle

blocked Worker 仍属于 live Thread，scheduler 不会自行宣告 complete。dispatcher 必须在
没有 live User Thread 时设置 stop，取消残余 Queued/Delayed 项并唤醒 Worker；Worker
exit/reap 后下一次 Start 才能完成。不要直接销毁仍 blocked/running 的动态栈。

### 加入 aging 数组后 rootfs 在挂载前失败

先看 Kernel BSS 终点与 frame-state/buddy metadata 物理起点。最大 32768 entry 与 hash
直接放 BSS 会增加数 MiB，可能越过自研链接/物理布局边界。PageAging backing 必须通过
KernelPageAllocation 使用真实 frame+KVA，并在 Process 资源基线前建立；不能靠提高 QEMU
超时或修改 rootfs 状态码处理。

### Aging 报 KindConflict，但物理帧已经换了用途

比较 entry 的 `observed_round`。旧 kind 在当前 round 尚未观察，表示 frame 在两轮之间
被释放并复用，应 remove 后 reclassify；旧 kind 已在本轮观察则说明同一帧同时被解释为
File/Anonymous，必须保留 KindConflict。失败日志会输出 physical address、existing kind、
observed kind、PID、虚拟地址、VMA kind 和 COW 位。

### Aging 在进程退出附近报 CorruptedState

ProcessRuntime 的 `active` 还承担 Zombie 结果槽寿命，不等于 CR3 存活。最后线程退出后
地址空间先销毁，父进程稍后 wait 才释放结果槽；`root_physical_address==0` 的 active
Zombie 必须跳过。非零 CR3 的 VMA Validate、页表 walk 或用户权限失败仍属于损坏。

### Pressure 档在 PID1 启动前进入 OOM

检查 `AGING CAPACITY/HASH_CAPACITY` 和 `MEMORY_PRESSURE_RESIDENT_PAGES`。4 GiB 档若误用
32768/65536 最大元数据，会消耗数百额外 frame 并挤掉 `0x2400` 压力预算。功能档必须用
4096/8192，32 GiB 才使用最大档；禁止从 resident 账本中隐藏这些真实 frame。

### 后台回收 wake 非零但 reclaimed 始终为零

先比较 aging 的 `CANDIDATE_OBSERVATIONS` 与后台 `BACKGROUND_BATCHES`。刚从 Active 降为
Inactive 的页还不是 candidate，必须再完成一轮冷观察；没有候选时 controller 应进入
BackingOff，而不是立即重复 WorkItem。若 file cache 持续命中，access generation 会变化，
旧候选会被撤销，这是保护热页，不应通过删掉 generation 检查“修复”。

### 后台回收后出现同一物理页重复释放

检查 selection 和 completion 的顺序。FilePageCache completion 必须在精确 eviction 前
调用 `PageAgingManager::Forget`；匿名 completion 位于 swap Store/PTE unmap 之后、frame
release 之前。若仅减少 candidate 统计而不删除 hash/队列条目，同一批次会再次选择已经
复用的物理地址。正常终态要求 tracked、四队列和 candidate 分类都为零。

### 后台回收占满 CPU 或 deadline 无法归零

读取 `BACKGROUND_NO_PROGRESS`、WorkQueue Delayed 和 scheduler active deadline。无候选、
仅写回和失败都必须排到 `now+1s`，不能即时自重排；实际回收后才允许下一批即时执行。
最后一个 User Thread 退出时 background handle 与 aging/writeback 一起取消，controller
reset 到 Sleeping。只停止 Worker 而遗漏 delayed handle 会让 scheduler 永远不能完成。

### pressure 测试的后台 anonymous 计数偶尔为零

第五增量按 clean、writeback、anonymous 顺序共享 64 页预算；前两阶段可占满一批，而
min watermark 的 direct fallback 仍可能完成匿名换出。因此稳定门禁要求后台 clean、
writeback、total 和系统总 anonymous 非零，不要求每次后台 anonymous 非零。若需要冻结
后台匿名份额，应运行第六增量的公平性专用矩阵，不能把普通协作调度时序设成精确断言。
第六增量已冻结 planner 的配额算法，但实际完成数仍取决于当轮 PageAging candidate；
因此 QEMU 稳定门禁检查统一计划、系统总匿名回收和 OOM 极端值，不要求每个后台批次都
实际交换匿名页。

### fsync 在 FILE_PAGE_WRITEBACK completion 前返回

先比较 `BEGINS/COMPLETIONS` 和 `ACTIVE`。范围扫描不能只搜索 Dirty/Error；找不到候选时
还要搜索范围内 Writeback，在仍持 cache lock 时按 identity、frame、access generation 登记
waiter。若先解锁再登记，owner 可能完成并释放 coordinator 槽，随后出现 lost wakeup。

等待提交必须在 scheduler lock 内连续执行 PrepareWait 和 BlockCurrentThread。完成先行时
PrepareWait 返回无需阻塞，随后仍要 TakeResult。不要用轮询 page state 或重复发起
Writeback 修复，这会破坏唯一设备请求和错误序列。

### buffered write 或共享映射写故障得到 WritebackWaitUnavailable

确认 owner 是否在进入 Dirty/Error→Writeback 前调用 Begin，并检查当前 Thread 是否满足
运行时 writeback owner availability。若页面已经发布 Writeback 却没有 coordinator token，
另一个 writer 无法安全等待。early boot/IRQ 中出现该状态属于调用边界错误；正常 User 或
Kernel Thread 路径必须可登记。

同时检查锁序：cache lock 可以短暂进入 scheduler lock 登记/完成，scheduler lock 路径不能
反向访问 cache、VFS 或设备。wait 回调必须在 cache 解锁后执行。最终应满足 active=0、
registration=take、commit=wake。

### 写回失败后 Worker 反复占用 CPU

SourceWriteFailed 必须把页留在 Error，清除 background request 并设置 paused；同期 waiter
领取同一失败后返回，不能自行立即重试。只有显式 fsync/fdatasync/msync 或新的 writer 把
Error 重新标为 Dirty 时才恢复。后台 controller 对失败批次进入 deadline backoff，不能用
放宽 failure marker 隐藏忙循环。

### NamespaceCache 同名项命中错误 mount

先打印或在 GDB 检查完整 `VfsDentryKey`，不能只比较 parent inode number 和名称。key 必须
包含 mount identifier、parent superblock identifier/generation、node identifier/generation
以及精确名称长度/全部字节。hash 相同不等于 key 相同；11.1 线性模型也必须执行完整比较。

### inode 失效后 child Negative 仍命中

`InvalidateInode` 不只撤销“指向该 inode”的正项，还必须撤销所有 parent identity 等于该
inode 的正负 child。检查 `cascaded_dentry_invalidation_count` 和 Validate 的父级失效闭包。
若只清 Positive，旧 Negative 会在目录修改后继续伪造 NotFound。

### Stale 项零引用但容量仍耗尽

Stale 只为旧引用保留。最后 `ReleaseDentry` 要释放槽并归还 inode dentry reference；Stale
inode 的 dentry/external 两类引用都为零时也必须释放。若直接把 state 改成 Free 而不保留
slot generation，旧 token 可能在复用后重新有效。用 unit 的容量场景和 randomized 每轮
active=0 定位该问题。

### LRU 回收了仍被路径引用的对象

dentry candidate 必须是 Cached 且 external reference 为零；inode 还要满足 dentry reference
和 external reference 都为零。Stale 不参与 LRU，因为零引用 Stale 应在 release/invalidating
事务中立即回收。第一增量没有生产 shrinker，不能从 QEMU I/O 数推断 LRU 已接入。

### chmod 后 stat 仍返回旧 mode

先确认 backend `change_mode` 已成功，再检查 `InvalidateNodeInformation` 是否使用相同的
superblock/node identifier+generation。失效发生在 backend commit 之前会让失败操作错误
丢缓存；完全没有失效则 Ready 快照持续命中。integration 的 chmod 分段断言会在下一次
`StatOpenFile` 未增加 backend stat 次数或 mode 未更新时失败。

### 失效后 metadata 又恢复为旧值

检查 completion ticket 的 inode generation 和 metadata generation，不能只比较 slot。
Loading 期间发生 invalidate 会把状态改为 Empty，必要时释放 inode；迟到 completion 必须
返回 `InvalidToken`。若直接按 identity 发布 backend 结果，就会重新引入 invalidate/fill
竞态。

### metadata cache 满后 stat 返回错误

缓存容量不是用户 ABI 资源。`PrepareInodeMetadata` 的 CapacityExhausted、GenerationExhausted
和 CounterOverflow，以及已有 Loading，都应让当前调用直接读取 backend 且不提交。只有
身份/type 冲突或内部不变量损坏才映射为 VFS Corrupt。integration 会填满全部 inode slot
并要求根目录 stat 仍成功。

### Destroy 报告 EntriesRemain

先检查 `loading_inode_metadata_count`。Loading 不能参加 LRU；owner 必须 Complete、Cancel，
或由 mutation Invalidate 后才能销毁。Ready metadata-only inode 可以正常 Evict，带 dentry/
external 引用的 inode 则必须由对应所有者先释放。

### QEMU 最终 loaded/waste 非零但 useful 为零

这表示预读请求执行了，但 demand 先于 worker completion 到达，不能删除 useful 非零门禁
把它伪装成成功。`/bin/tool_probe` 的首个 4 字节 miss 用于提交预读，随后可睡眠 20 ms，
再读取后续页；这验证真实 prefetched hit，同时不使用忙等或伪造统计。最终 readahead 聚合
在守恒判断前输出，因此失败时先核对 useful/waste、enqueue/completion/cancellation 和
active stream/task，再判断是调度窗口还是生命周期泄漏。

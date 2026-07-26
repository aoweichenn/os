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
  131072 2097152 --expected-outcome success
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
  131072 2097152 --expected-outcome kernel-invalid-opcode

python3 tools/os.py qemu-firmware \
  build/developer/images/firmware.bin \
  build/developer/images/kernel_page_fault/boot_disk.img \
  131072 2097152 --expected-outcome kernel-page-fault
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
  131072 2097152 --expected-outcome kernel-write-protection
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
2. 64 GiB 配置的地址必须高于 4 GiB；64 MiB 配置允许位于普通低端 RAM；
3. 在 direct-map 的首页和末页观察模式 `0x4255444459464952` 与
   `0x42554444594C4153`，区分“找到连续 PFN”与“映射可真实写入”；
4. 若写回成功但释放失败，检查 allocated 位图是否在原 order 和原块首置位，
   不要把块内第二页当成 order 0 页释放；
5. 若 `ValidateBuddy` 返回 `CorruptedState`，依次检查同块 free/allocated
   双重置位、祖先/子块重叠、未合并同阶伙伴、块内 2-bit 页状态和加权计数。

`BUDDY_ACTIVE_BLOCKS` 在日志中非零是正常现象，它包含内核仍持有的页表和 heap
后备页。判断泄漏应比较某段生命周期前后的活动块与页数，而不是要求全局为零。
64 GiB 下完整校验会扫描最高 PFN 覆盖的位图和页状态，因此
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

正常四进程结束摘要应为 active=0、creations=4、destructions=4、
peak-active=4、peak-mapped=16，并出现
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

### QEMU 捕获器为什么使用里程碑和总截止

先看带宿主时间戳的最后一行。一次固定墙钟预算耗尽不能单独判定 PIT 或 PIC
错误；但扩大预算后总在同一来宾标记停顿，就应检查硬件状态与模拟器版本差异，
不能继续用“runner 较慢”解释。无限延长超时同样会掩盖真实停滞。

QEMU 捕获器因此使用两个边界：

1. 逐行观察当前用例的最后一个必需里程碑；到达后保留短暂收尾窗口并回收进程。
2. 未到达时，普通配置以 15 秒、64 GiB 主规格以 75 秒为总失败上界；后者要在
   Debug 构建中扫描 16777216 个页状态。QMP 等待 `READY` 使用与当前内存规格
   相同的预算，外层 CTest 对主规格另设 85 秒硬上界。

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

### 256 MiB 通过，但 64 GiB 报资源快照账本错误

先区分 `managed_frame_count` 与可用 RAM 页数。前者由最高受管物理地址除以
4 KiB 得到，包含 E820 保留洞、MMIO 和 PCI 窗口；后者只包含能进入 free、
allocated 或 reserved 三态的页。正确关系是：

```text
accounted = free + allocated + reserved
accounted <= managed
unavailable = managed - accounted
```

64 GiB QEMU 在 3–4 GiB 附近存在明显 PCI 洞，因此错误地检查
`accounted == managed` 会只在容量档稳定失败。修复不能通过忽略快照错误或
修改 QEMU 内存图完成；应保留 2-bit `Unavailable` 状态，把差值作为不可用页
推导出来，同时继续比较 managed/free/allocated/reserved 四个稳定字段。
单元样例必须显式包含不可用间隙，256 MiB functional 与 64 GiB capacity
必须共同进入门禁。QEMU 捕获器在看到禁止失败标记时会立即结束实例并报告该
标记，不再等待 64 GiB 档的 75 秒总截止时间。

40 秒旧预算曾在共享宿主负载约 16 时于 `EXCEPTION_SELF_TEST_READY` 后耗尽，
来宾尚在执行 64 GiB 页状态、buddy、direct-map 与跨层自检，串口因提交后才
输出统计而没有中间行。这不是放宽来宾轮询：普通配置仍为 15 秒，禁止失败标记
仍会立即收尾；容量档只把宿主外部保险调整为有明确上界的 75/85 秒，并由工具
单元测试证明一个睡眠子进程会在短预算后被终止而不是遗留后台进程。

### 全量构建偶发报告 Kernel 或 User ELF 不存在

Stage 1 镜像命令读取七个 Kernel ELF，Kernel 的用户镜像对象又读取七个 User
ELF。文件路径出现在自定义命令参数中，不代表 CMake 一定能跨目录推导目标级
构建顺序；增量构建恰好已有文件时问题会被掩盖，干净构建或不同生成器才暴露。

`os_stage1_images` 必须显式依赖全部 Kernel ELF 目标，
`os_kernel_user_images_object` 必须显式依赖全部 User ELF 目标。这样
Ninja/Make 会先完成生产目标，再运行打包命令。不要用重复执行构建或在 Python
脚本里等待文件出现来绕过依赖图；生产者—消费者关系属于 CMake 构建图。

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
64 GiB  : process=256, thread=512, per-process=64
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

### 64 GiB 容量测试像“卡住”

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

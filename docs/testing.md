# 测试策略

## 测试分层

### 宿主机单元测试

验证与硬件无关的纯逻辑，例如 ELF 解析、位字段、地址计算、容器和状态机。失败反馈应当在秒级完成。

### 模块集成测试

验证磁盘镜像、页表、启动信息和模块交接契约。测试既要覆盖正常输入，也要主动构造损坏、截断和越界输入。

### 固定种子随机测试

对纯逻辑模块生成大量有效与无效输入，验证不依赖具体样例的性质。随机测试必须：

- 使用代码中明确命名的固定种子。
- 保证相同提交、相同种子产生相同输入序列。
- 在失败时输出种子、迭代位置和失败性质。
- 同时生成合法输入和预期被拒绝的非法输入。
- 将稳定复现的随机故障沉淀为独立回归样例。

### QEMU 系统测试

从 CPU 复位向量启动，捕获串口输出和 QEMU 生命周期。每个里程碑至少包含
一条成功路径和一条失败路径。

`v0.0` 尚无可执行固件，因此系统测试使用项目生成的空 ROM 和空磁盘，以
`-bios` 显式传入 ROM，并使用 `-S` 暂停 CPU。该测试只验证 QEMU TCG、PC
硬件模型、镜像尺寸和启动参数，不声明已经实现启动逻辑。

`v0.1` 使用两份由同一源码生成的 ROM：

- 正常镜像必须输出 `RESET` 和 `SERIAL_READY`。
- 故障注入镜像在第一条消息后屏蔽串口就绪状态，必须触发有界轮询超时，
  且不得输出 `SERIAL_READY`。

固件最终进入 `HLT`。测试进程逐行捕获串口；观察到当前用例最后一个必需
里程碑后保留短暂收尾窗口并主动回收 QEMU，尚未完成时最多等待五秒。这样既不
用固定两秒去猜慢速 CI 的调度延迟，也不会让已停机的失败用例白白耗尽预算。
QEMU 自行异常退出仍视为失败。

`v0.2` 在同一条真实复位路径上增加磁盘加载及其失败结果；`v0.3` 继续验证
从 A20 到 64 位入口的严格有序状态链：

- 正常磁盘必须依次出现 `STAGE1_HEADER_VALID`、`STAGE1_LOADED`、
  `A20_READY`、`ENTERED`、`GDT_READY`、`PROTECTED_MODE`、
  `PAGE_TABLES_READY`、`PAE_READY`、`LME_READY`、`PAGING_ENABLED` 和
  `LONG_MODE`。
- 固件必须在串口初始化后输出一次 `CLOCK_READY`；当前阶段不要求毫秒数，禁止输出
  没有 PIT 计数依据的伪造时间戳。
- IDE 永久忙必须在有界轮询后输出 `IDE_TIMEOUT`。
- ATA ERR 状态必须输出 `IDE_ERROR`。
- 描述符任意受保护字节损坏必须输出 `STAGE1_HEADER_INVALID`。
- 负载字节损坏必须输出 `STAGE1_CHECKSUM_INVALID`。
- 所有失败路径均禁止出现 `STAGE1_LOADED` 和 Stage 1 进入标记。

宿主单元测试验证格式编码、整扇区校验、截断、LBA 和加载范围；固定种子随机
测试完成 256 组有效负载往返和 256 组越界 LBA 拒绝。QEMU 测试验证的是 ROM
自身执行 ATA PIO 和远跳转，而不是宿主工具代替读取。

`v0.4` 完成内核 ELF64 审计与真实目标加载：

- 单元测试构造最小有效 ELF，并覆盖截断、程序头数量、权限、页对齐、目标
  窗口、段重叠、恒等装载和入口失败。
- BootInfo C++ 单元测试逐字段验证空指针、版本、结构大小、文件范围、入口、
  段数、页表根、映射大小和栈顶。
- 固定种子随机测试破坏 256 组 ELF 标识、256 组加载地址、256 组负载字节和
  128 组扇区补零，并完成 128 组不同文件长度往返。
- 集成测试审计真实 `kernel.elf`、组合磁盘和 Stage 1/页表/暂存区/加载窗口/
  内核栈之间的物理布局，且通过 `llvm-nm` 保证没有未解析运行时符号。
- QEMU 成功路径必须从复位依次到达 Kernel 的 `BOOT_INFO_VALID`、
  `BSS_ZEROED`、`CR3_VALID` 和 `READY`。
- QEMU 失败路径分别注入 Kernel ATA 永久忙、ATA ERR、描述符损坏、负载
  损坏和 CRC 正确但 ELF 语义非法，证明失败来自目标代码而非宿主预检查。

`v0.5` 把描述符表和异常控制流纳入四层验证：

- C++ 单元测试逐字段构造并解码 64 位 TSS 描述符和 IDT gate，检查精确
  结构大小、TSS type/present、完整处理器地址、IST 三位掩码、保留字段和
  32 个向量的硬件错误码分类。
- 固定种子 `0xD35C71A05EED6405` 生成 4096 个完整 64 位地址，同时验证
  TSS descriptor 与 IDT gate 编码解码往返，共 8192 条性质断言。
- ELF 集成审计除入口、段和未解析符号外，还要求描述符装载、异常公共入口、
  C++ 分发器、异常桩表和向量 0..31 的 32 个独立符号全部存在。
- 正常 QEMU 路径必须回读并确认 GDTR、IDTR、CS、SS、TR 和 TSS，经过
  `INT3 → BREAKPOINT_HANDLED → IRETQ` 后才能输出 `READY`。
- 非法指令镜像必须在 `UD2` 后报告向量 6、错误码 0 和 `PANIC`，禁止
  页故障字段及 `READY`。
- 页故障镜像必须访问首个未映射地址 `0x04000000`，报告向量 14、错误码 0、
  同值 CR2 和 `PANIC`，禁止 `READY`。
- 两份故障镜像只替换入口选择，描述符、异常桩、分发器和 panic 均使用生产
  实现，避免“测试了一份并未交付的异常代码”。

`v0.6` 把物理所有权、地址翻译和权限执行纳入同一证据链：

- 内存图单元测试覆盖空指针、空图、零长度、地址溢出、乱序、重叠和受管范围
  无可用 RAM，并核对描述字节、可用字节和受管可用字节。
- 页帧单元测试覆盖初始化、保留、分配顺序、释放复用、重复释放、耗尽和释放
  保留页；跨越已分配页的保留操作必须整体失败，不能留下部分修改。
- 堆与页表布局单元测试覆盖对齐、失败不修改输出、耗尽、48 位 canonical
  地址、四级索引和叶表项权限往返。
- 集成测试用 QEMU 当前的内存图形状复现低端 RAM 与高端保留区，验证平台、
  内核、栈保留后的首个可分配帧和统计；交接布局同步覆盖 BootInfo v2、
  `fw_cfg` 暂存和内存图。
- 固定种子 `0x6D656D6F72793634` 生成 8192 个物理地址/权限组合，并执行
  4096 步随机分配/释放，与独立布尔所有权模型逐步比较。
- 正常 QEMU 路径必须完成 `MEMORY_MAP_VALID`、`FRAME_ALLOCATOR_READY`、
  `PAGING_READY`、`MEMORY_PERMISSIONS_VALID`、`HEAP_SELF_TEST_PASSED`
  后才能到达 `READY`。
- Stage 1 内存图失败镜像必须在 `LONG_MODE` 后输出 `MEMORY_MAP_INVALID`，
  禁止读取 Kernel 或进入内核。
- not-present 页故障继续验证错误码 0；新增 Ring 0 写只读页故障必须验证
  错误码 `0x3` 和 CR2=`0xFFFF800000100000`。后者是 `CR0.WP` 的执行证据，
  不能用软件查询页表项替代。

`v0.7` 把异步硬件事件与设备状态机纳入同一证据链：

- 设备模型单元测试覆盖 PIC 的 IRQ/向量双向映射、掩码失败原子性，PIT
  频率范围、除数舍入与时间溢出，扫描码 make/break/`E0` 序列，以及 ATA
  LBA28、缓冲区长度和启动描述符 magic。
- 启动集成测试按生产顺序开放 IRQ0、IRQ1，核对最终掩码 `0xFFFC`，并组合
  PIT 配置、键盘 `A` 键解码和 LBA 0 描述符校验。
- 固定种子 `0x1A7E22D3C4B5A697` 执行 4096 轮 IRQ 往返、PIT 有效参数和
  键盘按下/释放性质；每轮同时验证输出只在成功后改变。
- ELF 审计新增硬件 IRQ 公共入口、C++ 分发器、桩表和
  `os_kernel_hardware_interrupt_vector_32..47` 全部符号。
- 正常 QEMU 路径必须证明传统路由已接管、PIC/PIT/PS2/ATA 已初始化，等待
  至少 16 个真实 IRQ0 并输出单调毫秒，然后才到达 `READY`。
- `READY` 后宿主通过 QMP 键盘前端注入 `A`。测试必须观察目标机 IRQ1 输出
  扫描码 `0x1E` 与 `A_PRESSED`；宿主不写端口、不写来宾内存，也不调用内核。
- 成功路径禁止 `DEVICE_INITIALIZATION_FAILED`、异常与 panic。IRQ 热路径
  不逐 tick 输出；宿主为每条串口行附加单调到达时间，便于判断停滞边界。

`v0.8` 把特权级、用户 ELF、系统调用和故障隔离纳入同一证据链：

- 用户 ELF 单元测试从最小合法文件出发，覆盖空指针、截断、标识、类型、
  机器、版本、头大小、程序头数量与范围、未知头、权限、对齐、文件范围、
  内存范围、重叠、总页数和入口失败。
- 固定种子 C++ 随机测试对 16,384 条地址范围性质断言，特别覆盖低地址、
  规范边界和无符号加法溢出；Python 随机测试独立破坏 ELF 字段并检查拒绝。
- 边界集成测试验证 Ring 3 扩展帧、四页栈与 guard、用户地址范围，以及
  `0x80`、调用 1/2 的 ABI 稳定性。
- 三个实际用户 ELF 分别由独立 Python 审计器检查 AMD64 `ET_EXEC`、入口、
  `PT_LOAD`、W^X、对齐和用户窗口，不以“链接成功”替代格式证据。
- 正常 QEMU 必须拒绝未映射用户指针和未知编号，输出 Ring 3 消息，以 0
  退出，报告六次系统调用，恢复内核后继续输出 `READY` 并处理键盘 IRQ。
- 用户非法指令镜像必须报告向量 6、错误码 0；用户页故障镜像必须报告向量
  14、错误码 `0x4` 和 CR2=`0x30000000`。两者必须输出
  `USER_TERMINATED` 和 `USER_RETURNED_TO_KERNEL`，禁止 `PANIC`。
- 截断用户 ELF 必须在 Ring 3 和设备初始化前报告验证状态 2；禁止出现用户
  入口、用户文本或 `READY`。
- Kernel ELF 审计新增系统调用入口/分发、用户进入/恢复和三个内嵌 ELF
  边界符号，防止链接图漏掉关键汇编路径。

`v0.9` 把调度策略、硬件切换和资源生命周期分层验证：

- 纯 `ProcessScheduler` 单元测试覆盖未初始化、零量子、容量、创建回滚、
  PID 单调、量子边界、终止和越界读取；内核栈布局同时验证对齐、guard 与
  相邻槽位不重叠。
- 集成模型让三个进程执行 24 tick，断言每个恰得 8 tick，并核对抢占、
  终止交接和派发统计。
- 固定种子随机测试生成 4096 组 1..4 进程、1..8 tick 量子和 1..64 tick
  序列；每个 tick 后都要求恰有一个 Running，所有 run tick 之和守恒。
- QEMU 真实路径由 PIT IRQ0 抢占一个 smoke 和三个同址 worker，要求每个
  worker 按自己的 PID 完成三轮计算，三个 BSS 隔离标记全部出现。
- 内核在最后退出后比较创建前后页帧统计；宿主再对调度日志执行顺序、精确次数
  和十六进制下界验证。两层都通过才接受 `SCHEDULER_COMPLETE`。
- 既有 Ring 3 `#UD/#PF` 用例改走单进程调度生命周期，证明新增调度器没有
  把用户异常重新退化成内核 panic。

`v0.10` 把共享状态、条件等待和 IPC 生命周期分层验证：

- 管道单元测试逐项覆盖未初始化、非法参数、空/满、部分读写、环形回绕、
  EOF、broken pipe、端点重复关闭和统计一致性。
- 同步集成测试启动四个宿主线程，每个线程执行 50,000 次
  `SpinLockGuard` 保护的递增，最终计数必须精确为 200,000。
- 固定种子管道随机测试执行 32,768 步读、写、关闭和查询操作，每一步都与
  独立字节队列模型比较内容、容量、索引效果、统计和端点状态。
- 调度单元/集成/随机测试加入 Blocked、读/写等待原因、定向唤醒、无 Ready
  后继和 block/wakeup 守恒；Blocked 永远不能被时间片路径选中。
- 生产者和消费者两个新用户 ELF 分别执行完整格式审计；Kernel ELF 还必须
  包含六个用户镜像边界，且不得出现动态初始化/析构区段。
- 正常 QEMU 必须观察 256 字节写入和读取、至少一次读写阻塞、block 与 wake
  相等、一次 EOF、空缓冲、端点均关闭、四份退出码 0 和页帧完全回收。
- 用户日志的生产者/消费者先后顺序不固定；各自内部里程碑顺序、出现次数和
  目标内统计才是稳定协议，避免把合法并发交错写死为测试。

`v0.11` 把磁盘格式、语义一致性与真实持久化分层验证：

- 格式单元测试直接对 512 字节缓冲执行 superblock、inode 和目录项
  编解码，逐类破坏受保护字段，证明 CRC32 与布局验证都实际生效。
- 生命周期集成测试在 4096 扇区内存块设备上执行首次格式化、嵌套目录、
  1300 字节跨块文件、关闭、同步、新实例重挂载和截断；随后分别制造孤儿
  inode、非法 `DEL` 名称和超级块 CRC 错误并要求拒绝。
- 固定种子随机测试执行 128 轮随机长度、随机内容的 truncate/rewrite，
  每轮销毁 `FileSystem` 与缓存对象、重新挂载，再与宿主参考数组逐字节比较。
- 正常 QEMU 既检查生产者/消费者文件里程碑，也解析每进程文件读写字节和
  superblock 代次；目标内还需完成同步、全盘一致性与独立载荷读回。
- 专用持久化测试复制一份真实启动盘并关闭 snapshot：第一次启动格式化和写入，
  第二次启动必须先报告 `FILE_SYSTEM_MOUNTED` 与旧载荷恢复，再允许重写。
- 第二次启动后，宿主只翻转文件系统超级块中的一个受 CRC 保护字节。第三次
  必须报告 `FILE_SYSTEM_CORRUPT`，禁止进入 Ring 3、自动格式化或到达
  `READY`。

## 验收证据

- 固定构建命令与工具链版本。
- 可机器判断的串口标记和有界 QEMU 生命周期。
- 失败日志包含阶段、模块和错误类型。
- 关键数据结构可通过反汇编或 GDB 检查。
- 回归测试可以在无图形界面的环境中运行。

## 完成标准

实现、测试、文档和调试方法必须在同一次变更中保持一致。仅在本地手工启动成功不能视为阶段完成。

## 运行方式

完整验证：

```bash
python3 tools/os.py verify
```

按测试层运行：

```bash
python3 tools/os.py test --layer unit
python3 tools/os.py test --layer integration
python3 tools/os.py test --layer randomized
python3 tools/os.py test --layer system
python3 tools/os.py test --layer failure-path
```

当前测试：

| 测试 | 层级 | 主要验证 |
| --- | --- | --- |
| `os_foundation_unit_tests` | 单元 | 地址类型、半开区间、空区间和溢出 |
| `os_foundation_integration_tests` | 集成 | ROM、复位向量、Stage 1 与内核区间关系 |
| `os_kernel_handoff_layout_integration_tests` | 集成 | 页表、描述符、BootInfo、暂存区、加载窗口与内核栈互不重叠 |
| `os_foundation_randomized_tests` | 随机 | 10,000 组区间性质与溢出拒绝 |
| `os_kernel_boot_info_unit_tests` | 单元 | BootInfo 全字段、上下界和失败状态 |
| `os_kernel_descriptor_layout_unit_tests` | 单元 | TSS、IDT gate、异常错误码和恢复分类 |
| `os_kernel_descriptor_layout_randomized_tests` | 随机 | 4096 组 64 位 TSS/IDT 地址编码往返 |
| `os_kernel_physical_memory_map_unit_tests` | 单元 | 内存图结构、排序、重叠、溢出与汇总 |
| `os_kernel_physical_frame_allocator_unit_tests` | 单元 | 2-bit 帧状态、保留原子性、分配释放与耗尽 |
| `os_kernel_heap_and_page_layout_unit_tests` | 单元 | 早期堆、canonical 地址、四级索引和页权限 |
| `os_kernel_memory_bootstrap_integration_tests` | 集成 | QEMU 内存图、启动保留范围与首个空闲帧 |
| `os_kernel_memory_management_randomized_tests` | 随机 | 8192 组表项和 4096 步分配器模型对照 |
| `os_kernel_device_model_unit_tests` | 单元 | PIC、PIT、扫描码和 ATA 纯状态机 |
| `os_kernel_device_bootstrap_integration_tests` | 集成 | IRQ 开放、时钟、键盘与启动盘设备闭环 |
| `os_kernel_interrupt_device_randomized_tests` | 随机 | 4096 轮 IRQ/PIT/键盘组合性质 |
| `os_kernel_user_elf_unit_tests` | 单元 | 用户 ELF 全字段、范围、W^X、重叠与入口 |
| `os_kernel_user_boundary_integration_tests` | 集成 | Ring 3 帧、用户栈、地址窗口与系统调用 ABI |
| `os_kernel_user_elf_randomized_tests` | 随机 | 16,384 条用户地址范围与溢出性质 |
| `os_kernel_process_scheduler_unit_tests` | 单元 | PID、容量、创建回滚、时间片、终止与 Ring 0 栈 guard |
| `os_kernel_process_scheduling_integration_tests` | 集成 | 多进程公平 tick、轮转次序、终止交接与统计守恒 |
| `os_kernel_process_scheduler_randomized_tests` | 随机 | 4096 组量子/进程/tick 组合的单 Running 与计数守恒 |
| `os_kernel_pipe_unit_tests` | 单元 | 管道读写、回绕、关闭、EOF、broken pipe 与统计 |
| `os_kernel_pipe_randomized_tests` | 随机 | 32,768 步管道状态与独立字节队列模型对照 |
| `os_kernel_synchronization_integration_tests` | 集成 | 四线程、200,000 次受锁更新的互斥与可见性 |
| `os_kernel_file_system_format_unit_tests` | 单元 | superblock、inode、目录项显式编码、布局与 CRC32 |
| `os_kernel_file_system_lifecycle_integration_tests` | 集成 | 格式化、目录、跨块文件、重挂载、截断与语义损坏拒绝 |
| `os_kernel_file_system_randomized_tests` | 随机 | 128 轮随机 rewrite、重挂载与参考模型逐字节对照 |
| `os_freestanding_symbol_audit` | 集成 | x86-64 ELF 与零未解析运行时符号 |
| `os_kernel_elf_layout` | 集成 | 真实内核的 ELF64 头、加载段、入口、权限与符号 |
| `os_user_smoke_elf_layout` | 集成 | 正常用户 ELF 的 AMD64、段权限与入口 |
| `os_user_invalid_opcode_elf_layout` | 集成 | 用户 `UD2` 测试 ELF 的结构与权限 |
| `os_user_page_fault_elf_layout` | 集成 | 用户越权访问测试 ELF 的结构与权限 |
| `os_user_scheduler_worker_elf_layout` | 集成 | 同址多进程 worker ELF 的结构、权限与入口 |
| `os_user_ipc_producer_elf_layout` | 集成 | 管道生产者 ELF 的结构、权限与入口 |
| `os_user_ipc_consumer_elf_layout` | 集成 | 管道消费者 ELF 的结构、权限与入口 |
| `os_qemu_hardware_smoke` | 系统 | 自定义空 ROM、空磁盘与 QEMU TCG |
| `os_qemu_rejects_invalid_image_size` | 失败路径 | 错误镜像尺寸必须导致测试失败 |
| `os_firmware_rom_layout` | 集成 | ROM 大小、复位 near jump 与入口字节 |
| `os_stage1_disk_layout` | 集成 | 描述符、LBA、加载范围和负载校验 |
| `os_kernel_disk_layout` | 集成 | 真实启动磁盘的 Kernel 描述符、CRC32、范围与内嵌 ELF |
| `os_kernel_disk_rejects_invalid_header` | 集成/失败路径 | 损坏 Kernel 描述符必须被拒绝 |
| `os_kernel_disk_rejects_invalid_checksum` | 集成/失败路径 | 损坏 Kernel ELF 文件必须被拒绝 |
| `os_kernel_disk_rejects_invalid_elf` | 集成/失败路径 | CRC 正确但 ELF 语义非法仍必须被拒绝 |
| `os_stage1_rejects_invalid_header` | 集成/失败路径 | 损坏描述符必须被宿主审计拒绝 |
| `os_qemu_stage1_load_success` | 系统 | 真实 ATA PIO 加载、校验、远跳转和 Stage 1 入口 |
| `os_qemu_file_system_persistence` | 系统/失败路径 | 同盘双启动持久化与损坏 superblock 拒绝挂载 |
| `os_qemu_firmware_serial_timeout_failure` | 系统/失败路径 | 有界轮询超时和禁止标记 |
| `os_qemu_firmware_ide_busy_timeout_failure` | 系统/失败路径 | BSY 永久置位必须有界失败 |
| `os_qemu_firmware_ide_error_failure` | 系统/失败路径 | ATA ERR 必须进入设备错误分支 |
| `os_qemu_stage1_header_failure` | 系统/失败路径 | ROM 必须拒绝损坏描述符 |
| `os_qemu_stage1_checksum_failure` | 系统/失败路径 | ROM 必须拒绝损坏负载 |
| `os_qemu_stage1_memory_map_failure` | 系统/失败路径 | Stage 1 内存发现失败不得进入 Kernel |
| `os_qemu_kernel_header_failure` | 系统/失败路径 | Stage 1 必须拒绝损坏 Kernel 描述符 |
| `os_qemu_kernel_checksum_failure` | 系统/失败路径 | Stage 1 必须拒绝损坏 Kernel 文件 |
| `os_qemu_kernel_elf_failure` | 系统/失败路径 | Stage 1 必须拒绝 CRC 正确的非法 ELF |
| `os_qemu_kernel_ata_timeout_failure` | 系统/失败路径 | Kernel 读取的 ATA 轮询必须有界超时 |
| `os_qemu_kernel_ata_error_failure` | 系统/失败路径 | Kernel 读取必须识别 ATA ERR/DF |
| `os_qemu_kernel_invalid_opcode_panic` | 系统/失败路径 | UD2、向量 6、统一帧与 panic |
| `os_qemu_kernel_page_fault_panic` | 系统/失败路径 | 向量 14、错误码、CR2 与 panic |
| `os_qemu_kernel_write_protection_panic` | 系统/失败路径 | CR0.WP、错误码 3、只读页 CR2 与 panic |
| `os_qemu_user_invalid_opcode_isolation` | 系统/失败路径 | Ring 3 #UD 只终止用户并恢复内核 |
| `os_qemu_user_page_fault_isolation` | 系统/失败路径 | Ring 3 #PF、错误码 4、CR2 与内核存活 |
| `os_qemu_user_invalid_elf_rejection` | 系统/失败路径 | 截断用户 ELF 必须在降权前被拒绝 |
| `os_python_tooling_unit_tests` | 单元 | 镜像、ELF、ROM、串口协议、代码统计和手机教材导出工具 |
| `os_firmware_randomized_tests` | 随机 | 256 组错误复位目标必须被拒绝 |
| `os_stage1_randomized_tests` | 随机 | 256 组有效镜像和 256 组越界 LBA 性质 |
| `os_kernel_randomized_tests` | 随机 | ELF 标识/地址破坏、长度往返、负载与补零破坏 |
| `os_book_source_check` | 集成 | 真实代码统计生成、LaTeX 输入图和 10 个主题章教材结构 |

当前共 68 项 CTest。QEMU、ELF 审计和镜像工具由 Python 标准库实现。QEMU
捕获器同时拥有“最终里程碑到达”和“五秒总截止”两个终止条件，并通过
`subprocess` 生命周期管理回收进程，不依赖宿主 Shell 的 `timeout` 或特殊退出码。
正常设备路径额外使用 QMP 的 Unix socket 与 `human-monitor-command/sendkey`
产生键盘前端事件；QMP 仅是测试输入通道，来宾仍完整执行 i8042 和 IRQ1 协议。

成功 QEMU 用例不只检查标记“至少出现一次”。v0.11 对四次 ELF/栈创建、
生产者/消费者里程碑、两个 worker 的进度与地址隔离、四份终止结果和单次资源
回收执行精确计数；同时解析固定 16 位十六进制统计，要求创建/终止为 4、
PIT 抢占至少为 1、阻塞/唤醒相等且均不为零、管道写入/读取均为 256。内核
也独立验证同一组进程、管道、文件系统和页帧不变量，形成目标内自检与宿主
协议检查两层证据。持久化测试另用同一临时磁盘的两次全新 QEMU 进程，避免
把缓存内读回误当作跨启动持久化。

宿主 C++ 测试使用项目内显式 `TestContext`，不引入 GoogleTest。当前测试规模
不需要 fixture 或宏注册；避免 `TEST`、`EXPECT_*` 等宏也与项目的宏约束一致。
如果以后出现大量共享 fixture、参数化组合或外部报告格式需求，再通过 ADR
重新评估，不提前增加依赖。

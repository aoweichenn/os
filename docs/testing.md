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

固件最终进入 `HLT`，测试进程以两秒预算运行 QEMU，预算结束后验证捕获的
串口协议。异常提前退出视为失败。

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
| `os_freestanding_symbol_audit` | 集成 | x86-64 ELF 与零未解析运行时符号 |
| `os_kernel_elf_layout` | 集成 | 真实内核的 ELF64 头、加载段、入口、权限与符号 |
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
| `os_qemu_firmware_serial_timeout_failure` | 系统/失败路径 | 有界轮询超时和禁止标记 |
| `os_qemu_firmware_ide_busy_timeout_failure` | 系统/失败路径 | BSY 永久置位必须有界失败 |
| `os_qemu_firmware_ide_error_failure` | 系统/失败路径 | ATA ERR 必须进入设备错误分支 |
| `os_qemu_stage1_header_failure` | 系统/失败路径 | ROM 必须拒绝损坏描述符 |
| `os_qemu_stage1_checksum_failure` | 系统/失败路径 | ROM 必须拒绝损坏负载 |
| `os_qemu_kernel_header_failure` | 系统/失败路径 | Stage 1 必须拒绝损坏 Kernel 描述符 |
| `os_qemu_kernel_checksum_failure` | 系统/失败路径 | Stage 1 必须拒绝损坏 Kernel 文件 |
| `os_qemu_kernel_elf_failure` | 系统/失败路径 | Stage 1 必须拒绝 CRC 正确的非法 ELF |
| `os_qemu_kernel_ata_timeout_failure` | 系统/失败路径 | Kernel 读取的 ATA 轮询必须有界超时 |
| `os_qemu_kernel_ata_error_failure` | 系统/失败路径 | Kernel 读取必须识别 ATA ERR/DF |
| `os_python_tooling_unit_tests` | 单元 | 镜像、ELF、ROM、串口协议、代码统计和手机教材导出工具 |
| `os_firmware_randomized_tests` | 随机 | 256 组错误复位目标必须被拒绝 |
| `os_stage1_randomized_tests` | 随机 | 256 组有效镜像和 256 组越界 LBA 性质 |
| `os_kernel_randomized_tests` | 随机 | ELF 标识/地址破坏、长度往返、负载与补零破坏 |
| `os_book_source_check` | 集成 | 真实代码统计生成、LaTeX 输入图和 10 个主题章教材结构 |

当前共 32 项 CTest。QEMU、ELF 审计和镜像工具由 Python 标准库实现。QEMU 超时通过
`subprocess` 生命周期管理判断，不依赖宿主 Shell 的 `timeout` 或特殊退出码。

宿主 C++ 测试使用项目内显式 `TestContext`，不引入 GoogleTest。当前测试规模
不需要 fixture 或宏注册；避免 `TEST`、`EXPECT_*` 等宏也与项目的宏约束一致。
如果以后出现大量共享 fixture、参数化组合或外部报告格式需求，再通过 ADR
重新评估，不提前增加依赖。

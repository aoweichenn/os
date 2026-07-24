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
| `os_foundation_randomized_tests` | 随机 | 10,000 组区间性质与溢出拒绝 |
| `os_freestanding_symbol_audit` | 集成 | x86-64 ELF 与零未解析运行时符号 |
| `os_qemu_hardware_smoke` | 系统 | 自定义空 ROM、空磁盘与 QEMU TCG |
| `os_qemu_rejects_invalid_image_size` | 失败路径 | 错误镜像尺寸必须导致测试失败 |
| `os_firmware_rom_layout` | 集成 | ROM 大小、复位 near jump 与入口字节 |
| `os_qemu_firmware_reset_serial_success` | 系统 | 真实复位和两阶段串口协议 |
| `os_qemu_firmware_serial_timeout_failure` | 系统/失败路径 | 有界轮询超时和禁止标记 |
| `os_python_tooling_unit_tests` | 单元 | 镜像、ELF、ROM 和串口协议工具 |
| `os_firmware_randomized_tests` | 随机 | 256 组错误复位目标必须被拒绝 |
| `os_book_source_check` | 集成 | 真实代码统计生成、LaTeX 输入图和 18 章教材结构 |

QEMU、ELF 审计和镜像工具由 Python 标准库实现。QEMU 超时通过
`subprocess` 生命周期管理判断，不依赖宿主 Shell 的 `timeout` 或特殊退出码。

宿主 C++ 测试使用项目内显式 `TestContext`，不引入 GoogleTest。当前测试规模
不需要 fixture 或宏注册；避免 `TEST`、`EXPECT_*` 等宏也与项目的宏约束一致。
如果以后出现大量共享 fixture、参数化组合或外部报告格式需求，再通过 ADR
重新评估，不提前增加依赖。

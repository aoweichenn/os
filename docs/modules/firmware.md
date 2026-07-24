# Firmware 模块

## 职责

`firmware` 是 CPU 复位后执行的第一份项目代码。v0.2 的职责包括：

- 提供 128 KiB ROM 布局和 `0xFFFFFFF0` 复位向量。
- 进入 `0xFFFFF000` 的 16 位入口并建立段、栈和方向标志状态。
- 初始化 COM1，提供有界字节与字符串发送。
- 通过 IDE ATA PIO 读取并验证 Stage 1 描述符。
- 验证 LBA、扇区数、加载范围、入口和两级校验。
- 把 Stage 1 装入低端 RAM，并通过远控制转移重载 CS:IP。

A20、GDT 和模式切换不属于本阶段。

## 目录

```text
source/firmware/
├── CMakeLists.txt
├── linker/rom.ld
└── src/reset_and_serial.asm
```

## 关键不变量

| 不变量 | 证据 |
| --- | --- |
| ROM 恰为 128 KiB | `audit-firmware` 与 QEMU 前置校验 |
| 复位向量文件偏移为 `0x1FFF0` | 链接脚本与二进制审计 |
| near jump 目标 IP 为 `0xF000` | 有符号位移解码 |
| UART 等待有界 | `0xFFFF` 轮询上限与失败镜像 |
| ATA 状态等待有界 | `0xFFFF` 次 BSY/DRQ 轮询 |
| 每次只传输一个扇区 | 扇区计数为 1，数据端口读取 256 个字 |
| 描述符不覆盖栈 | 缓冲区 `0x0500..0x06FF`，栈顶 `0x7000` |
| Stage 1 位于约定 RAM | `[0x8000, 0x9FC00)` 范围检查 |
| 磁盘损坏不能执行 | 描述符整扇区校验与负载校验 |

## 串口协议

成功路径：

```text
[OS][FIRMWARE] RESET\r\n
[OS][FIRMWARE] SERIAL_READY\r\n
[OS][FIRMWARE] STAGE1_HEADER_VALID\r\n
[OS][FIRMWARE] STAGE1_LOADED\r\n
[OS][STAGE1] ENTERED\r\n
```

IDE 超时、IDE 错误、描述符错误和负载校验错误各有独立标记。错误发生后固件
进入 `HLT`，不会跳转到未验证内存。串口自身超时时不能继续依赖同一通道输出，
宿主测试通过 `SERIAL_READY` 缺失识别失败。

## 测试变体

生产和故障镜像来自同一 NASM 源文件。串口失败变体在第一条日志后设置 BP；
IDE 失败变体分别让状态观测保持 BSY 或返回 ERR。轮询次数、状态分支、错误
传播和停机路径仍执行生产控制流。三个测试宏只存在于构建边界。

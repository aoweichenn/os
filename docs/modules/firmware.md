# Firmware 模块

## 职责

`firmware` 是 CPU 复位后执行的第一份项目代码。v0.1 的职责严格限制为：

- 提供 128 KiB ROM 布局。
- 在 `0xFFFFFFF0` 放置复位向量。
- 进入 `0xFFFFF000` 的 16 位入口。
- 建立方向标志、段寄存器和低端 RAM 栈。
- 初始化 COM1 并提供有界字节/字符串发送。
- 输出机器可判断的启动标记，随后进入 `HLT`。

磁盘加载、A20、GDT 和模式切换不属于本阶段。

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
| 入口物理地址为 `0xFFFFF000` | QEMU 实际串口输出 |
| UART 等待有界 | `0xFFFF` 轮询上限与失败镜像 |
| 正常和失败可区分 | 必需/禁止串口标记 |

## 串口协议

正常路径：

```text
[OS][FIRMWARE] RESET\r\n
[OS][FIRMWARE] SERIAL_READY\r\n
```

串口超时路径只保证第一行存在。由于发送通道本身已经失效，错误不能继续依赖
同一通道输出；宿主测试通过第二行缺失识别失败。

## 测试变体

生产和故障镜像来自同一 NASM 源文件。CMake 仅为故障镜像定义
`OS_FIRMWARE_TEST_FORCE_SERIAL_FAILURE`，使入口在第一条日志后设置 BP。
轮询函数看到非零 BP 后仍执行完整的有界循环，但不读取真实就绪位。这一宏只
存在于构建边界，不用于 C++ 领域逻辑。

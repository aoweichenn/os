# Firmware 模块

## 职责

`firmware` 是 CPU 复位后执行的第一份项目代码。v0.2 的职责包括：

- 提供 128 KiB ROM 布局和 `0xFFFFFFF0` 复位向量。
- 进入 `0xFFFFF000` 的 16 位入口并建立段、栈和方向标志状态。
- 初始化 80×25 VGA 文本模式、16 色 DAC、项目字形并提供字节与字符串输出。
- 在 `0x20000..0x9FFFF` 保存共享光标、输出模式和只追加日志，供后续阶段接管。
- 通过 IDE ATA PIO 读取并验证 Stage 1 描述符。
- 验证 LBA、扇区数、加载范围、入口和两级校验。
- 把 Stage 1 装入低端 RAM，并通过远控制转移重载 CS:IP。

A20、GDT 和模式切换不属于本阶段。

## 目录

```text
source/firmware/
├── CMakeLists.txt
├── include/font8x8_basic.inc
├── linker/rom.ld
└── src/reset_and_vga.asm
```

## 关键不变量

| 不变量 | 证据 |
| --- | --- |
| ROM 恰为 128 KiB | `audit-firmware` 与 QEMU 前置校验 |
| 复位向量文件偏移为 `0x1FFF0` | 链接脚本与二进制审计 |
| near jump 目标 IP 为 `0xF000` | 有符号位移解码 |
| VGA 字形位于 `0xFFFFE000` | 链接脚本断言字形不侵入 `0xFFFFF000` 入口 |
| VGA 模式、DAC 与字形由项目建立 | 寄存器表、16 色调色板、Public Domain 字形和显存自检 |
| 内存日志只追加且有界 | 512 KiB 固定区、版本 3、长度字段与溢出标志 |
| ATA 状态等待有界 | `0xFFFF` 次 BSY/DRQ 轮询 |
| 每次只传输一个扇区 | 扇区计数为 1，数据端口读取 256 个字 |
| 描述符不覆盖栈 | 缓冲区 `0x0500..0x06FF`，栈顶 `0x7000` |
| Stage 1 位于约定 RAM | `[0x8000, 0x9FC00)` 范围检查，并拒绝覆盖 `0x20000..0x9FFFF` |
| 磁盘损坏不能执行 | 描述符整扇区校验与负载校验 |

## VGA 文本协议

成功路径：

```text
[OS][FIRMWARE] RESET\r\n
[OS][FIRMWARE] VGA_READY\r\n
[OS][FIRMWARE] STAGE1_HEADER_VALID\r\n
[OS][FIRMWARE] STAGE1_LOADED\r\n
[OS][STAGE1] ENTERED\r\n
```

IDE 超时、IDE 错误、描述符错误和负载校验错误各有独立标记。错误发生后固件
进入 `HLT`，不会跳转到未验证内存。宿主通过 QMP 读取项目维护的内存日志，
按顺序检查启动字节；屏幕滚动不会删除自动化证据。Firmware 阶段尚未激活
用户终端，因此这些早期诊断同时可见。

## 测试变体

生产和故障镜像来自同一 NASM 源文件。VGA 失败变体在第一条日志后强制自检失败；
IDE 失败变体分别让状态观测保持 BSY 或返回 ERR。轮询次数、状态分支、错误
传播和停机路径仍执行生产控制流。三个测试宏只存在于构建边界。

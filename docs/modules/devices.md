# Interrupt 与 Devices 模块

## 职责与边界

v0.7 的设备子系统负责单核启动阶段的传统 PC 中断与最小设备闭环：

- `port_io` 封装精确 8/16 位 `IN`、`OUT` 和端口延时。
- `device_model` 保存不访问硬件的 PIC、PIT、扫描码与 ATA 纯逻辑。
- `legacy_pic` 拥有两片 8259A 的初始化、屏蔽、ISR 查询和 EOI。
- `programmable_interval_timer` 配置 8254 通道 0。
- `ps2_keyboard` 拥有 i8042 与键盘命令握手。
- `ata_pio` 提供 LBA28 单扇区同步读取。
- `interrupt_runtime` 组合设备、处理 IRQ、同步统计与延后日志事件。
- `architecture.asm` 只负责处理器入口帧，不访问具体设备寄存器。

硬件驱动依赖纯模型，运行时依赖各驱动；纯模型不反向依赖端口 I/O。这使端口
访问只能在 QEMU 验收，而算法和失败原子性可以在宿主快速测试。

## 启动次序

```text
映射 LAPIC MMIO 页为 RW/NX/PCD
  → SVR 软件启用、LVT LINT0=ExtINT 且未屏蔽
  → 8259A 重映射、全屏蔽
  → PIT 通道 0 编程
  → i8042/键盘握手
  → ATA PIO 重读并校验 LBA 0
  → 只开放 IRQ0、IRQ1
  → 软件 INT 0x27 验证虚假 IRQ7
  → STI
  → HLT 等待至少 16 个 IRQ0
  → 输出一次时间统计
  → 进入 HLT 事件循环
```

在所有处理函数、EOI 规则和设备输入都可用前不执行 `STI`。任一步失败都返回
强类型状态，由内核记录一次 `DEVICE_INITIALIZATION_FAILED=<状态>` 后停机。

## IRQ ABI

PIC 向量固定为 32..47。硬件不压入错误码，因此每个桩先压入 64 位零，再压入
向量；公共入口保存 15 个通用寄存器并满足 System V AMD64 调用对齐。C++
分发器把向量还原成 IRQ，处理设备后再确认 PIC。未知向量、未初始化分发或
非法 EOI 状态都属于内核不变量破坏，立即停机。

IRQ0 先增加 timer tick、完成 PIC EOI，再把当前 176 字节保存现场交给进程
运行时。只有中断来自 CPL3、当前时间片耗尽且存在另一个 Ready 进程时，
round-robin 调度器才返回不同的现场地址；汇编公共入口随后直接从该现场恢复。
这条热路径不写串口。IRQ1 从 `0x60` 取一个扫描码，解码器处理 make、break
与 `0xE0` 前缀；运行时保存首个待消费事件，串口写入发生在中断返回后的事件
循环中。IRQ7/IRQ15 会读取 ISR：虚假 IRQ7 不发 EOI，虚假 IRQ15 只向主片
确认级联。

## 时间语义

PIT 输入为 1193182 Hz，目标频率为 1000 Hz，实际除数为 `0x04A9`。对外时间
不使用“tick 等于 1 ms”的近似，而按实际除数换算。统计读取先保存 IF 并
`CLI`，复制共享字段后恢复原状态，避免 64 位状态在中断边界看到不一致快照。

串口中的 `[QEMU][T+......ms]` 是宿主接收时间，只用于定位阶段延迟；来宾
`MONOTONIC_MILLISECONDS` 才来自自写 PIT/IRQ 路径。两者的起点、调度环境和
用途不同，不能互相替代。

## 测试证据

- 单元：PIC 向量与掩码失败原子性、PIT 范围/舍入/溢出、扫描码状态机、
  ATA LBA/缓冲区/magic。
- 集成：把 PIC 开放顺序、PIT 参数、键盘解码和 ATA 描述符校验组合为启动
  契约。
- 随机：固定种子执行 4096 轮 IRQ 往返、PIT 有效频率和键盘 make/break。
- ELF：要求 16 个硬件 IRQ 符号、公共入口、桩表和 C++ 分发器完整。
- 系统：QEMU 真实产生 IRQ0；QMP 注入 `A` 键，来宾必须输出扫描码 `0x1E`
  和 `A_PRESSED`，同时禁止设备失败、异常与 panic。

## 当前限制

- 单核、PIC 经 LAPIC virtual-wire 交付；没有 I/O APIC、LAPIC timer、
  MSI/MSI-X 或 SMP 路由。
- ATA 是同步 PIO 单扇区读取，未启用 IRQ14、DMA、队列与写入。
- PS/2 已解码左右 Ctrl 的 Set 1 make/break，并将 Ctrl-C/Z 转为 C0 控制码；
  尚无 Shift/Alt/CapsLock 完整布局、重复键和通用键事件环形缓冲。
- PIT 是调度时钟与 v1.13 单调纳秒的基础，不是 RTC 墙钟；已有 deadline
  queue 与非忙等 sleep，但尚无 tickless 或高精度 timer。
- 当前为单 BSP 动态 Process/Thread 调度器，仍使用固定四 tick 时间片；设备层
  尚未提供按最早 deadline 重新编程的 one-shot 时钟事件接口。

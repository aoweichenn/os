# B5：中断、时间与 PC 设备

## 1. 设备不是普通函数

CPU 可以在纳秒级执行指令，串口、磁盘和人类键盘慢得多。设备还可能在软件没有
调用函数的时刻产生事件。

因此设备驱动围绕三类契约：

```text
register layout
command/status state machine
completion notification
```

“向端口写一个值”通常只是启动状态机，不等于高级操作完成。

## 2. 设备寄存器

典型设备提供：

- data register。
- status register。
- command/control register。
- configuration register。

一个寄存器可能读写语义不同。例如 i8042 的 `0x64`：

- read：controller status。
- write：controller command。

驱动必须按方向和时序解释，不能把它当普通变量。

## 3. Port I/O 与 MMIO

### 3.1 Port I/O

x86 独立 16-bit port address space，通过 `in/out`：

```asm
in  al, dx
out dx, al
```

当前 UART、PIC、PIT、i8042、ATA 和 fw_cfg 都使用 port I/O。

### 3.2 MMIO

寄存器占据 physical address，映射后以 load/store 访问。Local APIC 属于
MMIO。

MMIO 需要：

- 页表映射。
- 正确 cache type。
- 合适宽度与顺序。
- `volatile` 访问语义。

### 3.3 不能混用

port `0x60` 和 physical `0x60` 是两个地址空间。对
`reinterpret_cast<uint8_t*>(0x60)` 解引用不会执行 `in 0x60`。

## 4. Polling

Polling 反复读取 status，直到条件成立：

```text
repeat:
  status = read
  if error → fail
  if ready → continue operation
  if budget exhausted → timeout
```

优点：

- 控制流直观。
- 启动早期不依赖 IDT/PIC/中断栈。
- 适合短而同步的单次操作。

缺点：

- CPU 忙等。
- 慢设备浪费时间。
- 无上界会永久挂住。

当前 ROM/Stage 1 的 UART/ATA 先采用 bounded polling，因为那时中断系统尚未
建立。

## 5. Timeout 是协议的一部分

设备可能：

- 不存在。
- 永不清 busy。
- 报告 error。
- 返回错误 ACK。
- 模型或连接异常。

无限循环会让自动测试只能靠宿主超时猜测原因。固定 poll budget 让驱动返回
明确状态，故障镜像可验证预期里程碑停止位置。

timeout 不是现实毫秒的精确保证；纯计数预算受 CPU/QEMU 速度影响。早期阶段
它首先保证有界失败，建立 PIT 后才能定义来宾时间预算。

## 6. Interrupt

设备完成或有数据时通过 IRQ 请求 CPU。CPU 在允许条件下：

1. 结束/暂停当前指令边界。
2. 根据 vector 查 IDT。
3. 必要时换栈。
4. 保存返回状态。
5. 进入 handler。
6. 软件处理设备并确认控制器。
7. `iretq` 返回。

### 6.1 Interrupt 不等于线程

handler 借用当前 CPU 立即运行，不是普通可调度进程。它必须：

- 使用受控栈。
- 保存被中断上下文。
- 有界执行。
- 避免可能永久阻塞的操作。

## 7. 从设备 IRQ 到 CPU vector

当前 legacy 路径：

```text
device
  → IRQ line
  → 8259A PIC
  → PIC INTR output
  → LAPIC LINT0 ExtINT
  → CPU interrupt acknowledge
  → vector
  → IDT gate
```

这条链任何一环配置错误，设备 status 可能已变化，CPU 却从未进入 handler。

## 8. 8259A PIC

传统 PC 有 master/slave 两片 8259A：

- master IRQ0..7。
- slave IRQ8..15，经 master IRQ2 cascade。

### 8.1 为什么 remap

默认 vector 可能与 CPU exception `0..31` 冲突。Kernel 初始化 ICW，把 IRQ
映射到独立 vector 区域。

### 8.2 IMR、IRR、ISR

- IMR：mask register，bit=1 禁止交付。
- IRR：已请求、等待处理。
- ISR：已被接受、尚未 EOI。

看到 IRR=1 只说明 PIC 收到边沿，不证明 CPU 收到 vector。

### 8.3 EOI

handler 完成后发送 End Of Interrupt，让 PIC 清 in-service 状态并允许后续
请求。

slave IRQ 一般要：

1. 向 slave EOI。
2. 向 master cascade EOI。

EOI 过早可能让同一设备在状态未清时重入；遗漏则后续 IRQ 被阻塞。

### 8.4 Spurious IRQ

电气/时序竞争可能让 acknowledge 时请求已消失，经典为 IRQ7/IRQ15。PIC driver
需按 ISR 判断，并遵循不同 EOI 规则，不能把它当普通设备事件。

## 9. Local APIC virtual-wire

现代 QEMU PC CPU 有 LAPIC。没有商业 BIOS 时，PIC 输出不一定自动到 CPU。

当前 Kernel：

- 映射 LAPIC MMIO。
- 启用 APIC software bit。
- 配置 LINT0 delivery mode=ExtINT。
- 保持 unmasked。

于是 legacy PIC 经 virtual wire 交付。

将来 I/O APIC 模式会变成：

```text
device/GSI → I/O APIC redirection → target LAPIC → vector
```

当前阶段不应同时维护两套未验证路由。

## 10. RFLAGS.IF、CLI 与 STI

- IF=1：允许普通 maskable interrupt。
- `cli`：清 IF。
- `sti`：置 IF。

还要同时满足：

- PIC 未 mask。
- LAPIC 路由有效。
- IDT gate present。
- 当前不在 interrupt shadow/更高优先条件。

所以 IF=1 不是“中断一定到达”的充分条件。

### 10.1 保存并恢复，不要无条件 STI

函数进入前 IF 可能本来为 0。临界函数应：

1. 读取旧 RFLAGS。
2. CLI。
3. 执行。
4. 仅旧 IF=1 时恢复 STI。

否则内层 helper 会意外打开外层仍要求关闭的中断。

## 11. Interrupt gate 的 IF

x86 interrupt gate 进入时自动清 IF。handler 返回时 `iretq` 恢复保存的
RFLAGS。

这让单核普通 IRQ 不会默认嵌套，但不能替代：

- NMI 处理。
- SMP lock。
- 主动重新 STI 后的嵌套设计。
- 共享数据原子。

## 12. HLT 与 idle

`hlt` 让 CPU 等待事件，减少无 Ready 工作时的忙等。

### 12.1 丢失空闲唤醒

危险：

```text
检查 no ready
事件到来并被处理
执行 hlt
```

可能在工作已经 Ready 时睡眠。

### 12.2 `sti; hlt`

STI 后 CPU 对 maskable interrupt 有一条指令的 interrupt shadow：

```asm
sti
hlt
```

保证 HLT 先进入等待，再响应 pending IRQ。

当前调度器返回后还要恢复关中断临界区：

```asm
sti
hlt
cli
```

三条必须是相邻机器指令，所以放在同一 inline asm 并审计反汇编。

## 13. 时间有多种含义

不要把以下概念混为一个计数：

- oscillator frequency。
- PIT input clock。
- divisor。
- actual interrupt frequency。
- timer tick count。
- monotonic elapsed milliseconds。
- process quantum ticks。
- host wall-clock log timestamp。

## 14. 8254 PIT

PIT channel 0 输入频率约为固定平台值。配置 divisor：

```text
actual frequency = input frequency / divisor
```

divisor 是整数，因此请求 1000 Hz 未必精确得到 1000.000 Hz。项目保存实际
frequency/divisor，再用整数公式计算单调毫秒，避免把“目标频率”冒充硬件结果。

### 14.1 Channel、mode 与 latch

PIT 通过 command port 选择 channel/access/mode，再向 data port 写 divisor
低/高字节。写入顺序是协议。

### 14.2 Tick 不是 scheduler

IRQ0 只报告时间事件。ProcessScheduler 决定：

- 是否消耗 quantum。
- 是否有其他 Ready。
- 是否切换。

硬件机制与策略分开，纯状态机才可在 host 测试。

## 15. 串口 UART

当前使用 16550-compatible COM1 base `0x3F8`。

典型 offset：

| offset | DLAB=0 | DLAB=1 |
| ---: | --- | --- |
| 0 | THR/RBR | divisor low |
| 1 | IER | divisor high |
| 2 | IIR/FCR | IIR/FCR |
| 3 | LCR | LCR |
| 4 | MCR | MCR |
| 5 | LSR | LSR |

### 15.1 DLAB

LCR bit 7 改变 offset 0/1 的含义。初始化 baud divisor 时必须：

1. set DLAB。
2. 写 divisor low/high。
3. clear DLAB 并设置数据位/停止位/校验。

忘记清 DLAB 后，“输出字符”会继续改 divisor。

### 15.2 115200 8N1

常见：

- baud 115200。
- 8 data bits。
- no parity。
- 1 stop bit。

串行线上一个字节通常还需 start/stop bit，实际吞吐低于 115200 bytes/s。

### 15.3 THRE

Line Status Register 的 Transmitter Holding Register Empty 表示可接受下一
byte。它不是“字符已到达宿主终端”的精确时刻。

早期驱动 bounded poll THRE，失败时停止里程碑。

### 15.4 为什么串口是最早观测通道

- 固定端口。
- 状态机小。
- QEMU 可映射到 stdout/file。
- 不依赖 framebuffer、字体、内存 allocator。

但日志过多会显著改变时序，所以 IRQ hot path 只计数，低频汇总。

## 16. i8042 与 PS/2

端口：

```text
0x60 data
0x64 status/command
```

status：

- output buffer full：CPU 可读。
- input buffer full：CPU 暂不可写。

### 16.1 Controller 与 device 是两层

CPU 可：

- 向 controller 发命令。
- 向 keyboard device 发命令。

某些写入都经过 `0x60`，但前序 controller command 决定解释。初始化需要等待
input/output 状态并验证 `0xFA` ACK。

### 16.2 扫描码

键盘报告物理键 make/break，不直接报告 Unicode：

```text
scan code
  → decoder state
  → key event
  → modifier policy
  → ASCII character
```

Shift/Caps 状态跨多个 IRQ 保存。break code 更新 modifier，但不提交字符。

### 16.3 为什么 IRQ handler 不做行编辑

驱动只应提交 character/event。退格是否擦屏、引号如何解析属于 Ring 3 Shell
策略。这样键盘也可供未来 TTY/raw mode 复用。

## 17. ATA PIO

当前 primary IDE 常见端口：

```text
0x1F0 data
0x1F1 error/features
0x1F2 sector count
0x1F3 LBA low
0x1F4 LBA mid
0x1F5 LBA high
0x1F6 drive/head + high LBA bits
0x1F7 status/command
```

### 17.1 状态位

- BSY：设备忙。
- DRQ：data request，可传输。
- ERR：命令错误。
- DF：device fault。

驱动等待时不能只看 DRQ：

```text
若 ERR/DF → fail
若 BSY → continue bounded wait
若 !BSY && DRQ → transfer
超预算 → timeout
```

### 17.2 LBA28

LBA 拆到：

- low/mid/high 3 bytes。
- drive/head low nibble 高 4 bits。

还要检查：

- LBA 未超过 28-bit。
- sector count 合法。
- image capacity。
- `lba+count` 无溢出。

### 17.3 PIO data

一个 512-byte sector 通过 data port 传 256 个 16-bit word。buffer alignment
和 byte assembly 要明确，不能把设备 word count 当 byte count。

### 17.4 Read/Write/Flush

- READ SECTORS `0x20`。
- WRITE SECTORS `0x30`。
- FLUSH CACHE `0xE7`。

write data 进入设备/QEMU cache 不等于持久介质顺序已完成。文件系统 Sync
最终需要 FLUSH。

### 17.5 为什么当前关闭 ATA IRQ

当前 block I/O 采用同步 PIO polling，命令完成事件由调用者独占。若同时启用
IRQ14，又没有异步 request ownership，完成可能被两条路径竞争。异步块层应在
后续单独建立 request queue、completion 和 wakeup。

## 18. `fw_cfg`

QEMU fw_cfg 是 Guest 可通过端口读取的配置设备。它提供：

- selector。
- data stream。
- file directory。
- `etc/e820`。

### 18.1 为什么它不违反自研启动链

它与 UART/ATA 一样只提供硬件协议。Stage 1 自己：

- 读取 directory。
- 处理字段字节序。
- 找目标 name。
- 验证大小。
- 转换 E820。

QEMU 没有替 Guest 进入 long mode 或创建 allocator。

## 19. MMIO LAPIC

LAPIC register 通过 32-bit MMIO offset 访问。映射要求：

- physical base 来自架构状态/约定。
- page present supervisor。
- NX。
- cache-disabled/合适 cache type。

读取 register 值只能证明 local state，例如 LINT0 mode；还需结合 PIC IRR、
IF 和实际 IRQ count 证明端到端路由。

## 20. Interrupt handler 设计原则

### 20.1 有界

不能在 IRQ 中：

- 无限轮询。
- 等待另一个进程。
- 做大规模文件操作。
- 打印每个高频事件。

### 20.2 先取得事件事实

例如键盘必须读取 `0x60` 清设备 output buffer，否则 IRQ 条件可能保持。

### 20.3 更新最小状态

- tick++。
- byte 入 FIFO。
- completion flag/state。
- 唤醒符合 wait reason 的进程。

### 20.4 正确 EOI

处理设备状态后按控制器协议确认。若 handler 可能切换 frame，EOI 与调度顺序
要明确。

### 20.5 不信任用户

IRQ handler 不能直接写任意用户指针。先进入 Kernel-owned buffer，再由 syscall
copy 边界交付。

## 21. 单核也有并发

虽然只有一个 CPU，以下上下文仍会交错：

- Ring 3 正常执行。
- syscall。
- timer IRQ。
- keyboard IRQ。
- Kernel idle。

关中断区可以在本 CPU 阻止普通 maskable IRQ 插入，但：

- 不解决 NMI。
- 不解决 future SMP。
- 不能替代对象状态机。
- 临界区过长会增加延迟。

所以数据结构文档要明确“由 IF=0 单核串行化”而不是笼统写 thread-safe。

## 22. 设备模型与纯测试

直接在 host unit test 执行 `out` 会触发权限错误。项目把：

- 端口编码/解码。
- PIC mask/IRR 模型。
- PIT divisor 计算。
- keyboard scan-code decoder。

尽量拆成纯状态逻辑测试，再用 QEMU 证明真实 I/O instruction 和中断入口。

纯模型不能证明 QEMU wiring；整机测试又不适合穷举全部扫描码。两层必须结合。

## 23. QMP 输入

QMP `sendkey`：

```text
host command
  → QEMU keyboard device
  → i8042 scan code
  → IRQ1
  → Guest decoder/FIFO
```

它不向 Guest console buffer 直接写字符。相比人工图形窗口，它提供可重复输入。

测试仍需确认：

- 只在 Shell READY 后发送。
- 每个字符有受控间隔。
- unsupported host key 映射明确失败。
- Guest submitted/read/dropped/buffered 守恒。

## 24. 常见误解

### 24.1 “读到设备 ready 就说明高级操作成功”

Ready 只说明能进行下一协议步骤。还需传输、completion、校验和上层不变量。

### 24.2 “STI 后中断一定立刻来”

还受 mask、route、pending、priority 和 interrupt shadow 影响。

### 24.3 “IRQ handler 可以调用任何 Kernel 函数”

必须考虑是否阻塞、拿锁、分配、打印或依赖可被中断上下文。

### 24.4 “QEMU sendkey 绕过了键盘驱动”

它操作虚拟硬件；只要 Guest 仍从 i8042/IRQ 解码，就没有绕过。

### 24.5 “ATA 写命令返回就是跨重启持久”

还需 writeback ordering、FLUSH、snapshot=off 和同一可写磁盘重新启动。

## 25. 对照项目阅读

1. [端口 I/O](../../../source/kernel/src/device/port_io.cpp)
2. [PIC](../../../source/kernel/src/device/legacy_pic.cpp)
3. [PIT](../../../source/kernel/src/device/programmable_interval_timer.cpp)
4. [设备模型](../../../source/kernel/src/device/device_model.cpp)
5. [PS/2 driver](../../../source/kernel/src/device/ps2_keyboard.cpp)
6. [ATA PIO](../../../source/kernel/src/device/ata_pio.cpp)
7. [Interrupt runtime](../../../source/kernel/src/arch/interrupt_runtime.cpp)
8. [Processor idle](../../../source/kernel/src/arch/processor.cpp)
9. [QEMU runner](../../../tools/os_tools/qemu_runner.py)
10. [硬件寄存器清单](../../hardware/chips.md)

## 26. 练习

### 练习 A：状态机

为 ATA read 画出：

```text
Idle → RegistersProgrammed → CommandIssued → Busy → DataReady
     → 256 words transferred → Completed
```

为每个状态列出 ERR/DF/timeout 边。

### 练习 B：IRQ 路由

从 PIT output 到 `HandleProcessTimerInterrupt` 画全链，并指出：

- 哪一层用 IRQ number。
- 哪一层用 vector。
- 哪一层需要 EOI。

### 练习 C：UART

解释 DLAB=1 时写 COM1+0/1 与 DLAB=0 时的差异。预测忘记清 DLAB 后第一条
日志会怎样。

### 练习 D：空闲竞态

画出事件在 no-ready check、STI、HLT、IRQ handler 四个时间点到达时的结果，
说明 interrupt shadow。

### 练习 E：持久性

分别说明：

- FileSystem::Write 返回。
- BlockCache::Sync 返回。
- ATA FLUSH 返回。
- 全新 QEMU mount/read 成功。

每项比前一项新增了什么证据。

## 27. 通过标准

应能：

- 区分 polling、interrupt 和尚未实现的 DMA/asynchronous completion。
- 解释 PIC mask/IRR/ISR/EOI 与 LAPIC ExtINT。
- 解释 IF、interrupt gate、`sti; hlt; cli`。
- 从 divisor 推导 PIT tick 与来宾时间。
- 描述 UART、i8042 和 ATA 的关键寄存器状态机。
- 说明 IRQ hot path 为什么只能做有界最小工作。
- 说明 QMP sendkey 如何保持真实 Guest 硬件路径。

下一册进入
[进程、系统调用、并发、文件与 Shell](06-processes-syscalls-files-and-shell.md)。

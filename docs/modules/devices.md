# Interrupt 与 Devices 模块

## 职责与边界

v0.7 的设备子系统负责单核启动阶段的传统 PC 中断与最小设备闭环：

- `port_io` 封装精确 8/16 位 `IN`、`OUT` 和端口延时。
- `device_model` 保存不访问硬件的 PIC、PIT、扫描码与 ATA 纯逻辑。
- `legacy_pic` 拥有两片 8259A 的初始化、屏蔽、ISR 查询和 EOI。
- `programmable_interval_timer` 配置 8254 通道 0。
- `ps2_keyboard` 拥有 i8042 与键盘命令握手。
- `pci_model` 负责 BDF/configuration address、NVMe class code 和 memory BAR 纯逻辑；
- `pci` 以 32 位端口访问 PCI configuration mechanism #1；
- `nvme_model` 负责 CAP、CC/AQA、Identify、CQE 和 queue cursor 纯协议；
- `nvme` 负责 BAR/MMIO、DMA admin queue、doorbell 与真实控制器生命周期；
- `block_device` 定义不依赖文件系统和控制器类型的块设备协议与几何。
- `block_request` 提供 64 位请求身份、FIFO 签发、多深度乱序完成与 Reap 生命周期。
- `ata_pio` 保留 early boot 的有界 LBA28 PIO 轮询适配器，并为 primary IRQ14、secondary
  IRQ15 提供 Read/Write/Flush 异步请求；运行期 User rootfs/swap 走异步适配器。
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
  → 初始化 64 槽 BlockRequestQueue
  → 开放 IRQ0、IRQ1、master IRQ2 cascade 与 slave IRQ14/IRQ15
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

IRQ0 先增加 timer tick、解析 deadline 与 ATA 请求超时、完成 PIC EOI，
再把当前 176 字节保存现场交给进程
运行时。只有中断来自 CPL3、当前时间片耗尽且存在另一个 Ready 进程时，
round-robin 调度器才返回不同的现场地址；汇编公共入口随后直接从该现场恢复。
这条热路径不写系统日志或 VGA。IRQ1 从 `0x60` 取一个扫描码，解码器处理 make、break
与 `0xE0` 前缀；运行时保存首个待消费事件，诊断记录发生在中断返回后的事件
循环中，终端激活后不会移动前台光标。IRQ14/IRQ15 分别处理 primary/secondary ATA
当前单飞请求的数据/状态阶段，提交唯一完成、先确认 slave 再确认 master、通知 BlockIo
completion Worker 并启动下一请求；它们不睡眠、不分配也不进入 VFS。IRQ7/IRQ15 会读取
ISR：虚假 IRQ7 不发 EOI，虚假 IRQ15 只向主片确认级联。

## 时间语义

PIT 输入为 1193182 Hz，目标频率为 1000 Hz，实际除数为 `0x04A9`。对外时间
不使用“tick 等于 1 ms”的近似，而按实际除数换算。统计读取先保存 IF 并
`CLI`，复制共享字段后恢复原状态，避免 64 位状态在中断边界看到不一致快照。

`[QEMU][T+......ms]` 是宿主首次在内存日志观察到该行的时间，只用于定位阶段延迟；来宾
`MONOTONIC_MILLISECONDS` 才来自自写 PIT/IRQ 路径。两者的起点、调度环境和
用途不同，不能互相替代。

## 测试证据

- 单元：PIC 向量与掩码失败原子性、PIT 范围/舍入/溢出、扫描码状态机、
  ATA LBA/缓冲区/magic，以及 BlockRequest 几何、多块、设备能力和容量生命周期。
- 集成：把 PIC 开放顺序、PIT 参数、键盘解码和 ATA 描述符校验组合为启动
  契约。
- 随机：固定种子执行 4096 轮 IRQ 往返、PIT 有效频率和键盘 make/break；块请求
  另执行十万步多深度提交、FIFO 签发、乱序完成、超时、取消与回收模型。
- ELF：要求 16 个硬件 IRQ 符号、公共入口、桩表和 C++ 分发器完整。
- 系统：QEMU 真实产生 IRQ0；QMP 注入 `A` 键，来宾必须输出扫描码 `0x1E`
  和 `A_PRESSED`，同时禁止设备失败、异常与 panic。

## 当前限制

- 单核、PIC 经 LAPIC virtual-wire 交付；NVMe 使用单个 MSI-X 向量 `0x50`，尚无
  I/O APIC、LAPIC timer、MSI-X 多向量亲和性或 SMP 路由。
- ATA 运行期已启用 IRQ14 与 64 槽 FIFO，但硬件通道仍是单飞 PIO；没有 ATA
  DMA、tagged queue 或 AHCI。Kernel 已有单控制器、单 namespace 的轮询 NVMe
  I/O、16 页 PRP、四 outstanding 和单向量 MSI-X，但没有多 I/O queue、多控制器或
  调度器异步接口。ROM、Stage 1、early Kernel
  自检及当前 rootfs 普通扇区适配器仍使用有界轮询；异步生产路径当前由
  显式 sync 的最终 FLUSH 验证。
- PS/2 已解码左右 Shift/Ctrl、CapsLock、Basic Latin 和方向键；Ctrl-C/Z 转为
  C0 控制码，方向键转为 CSI。尚无 Alt、重复键和通用键事件环形缓冲。
- PIT 是调度时钟与 v1.13 单调纳秒的基础，不是 RTC 墙钟；已有 deadline
  queue 与非忙等 sleep，但尚无 tickless 或高精度 timer。
- v2.2 只读 CMOS RTC 已支持 UIP 稳定快照、BCD/binary、12/24 小时和 UTC
  Gregorian 换算；没有设置接口、时区数据库、闰秒表或持久校时服务。
- 当前为单 BSP 动态 Process/Thread 调度器，仍使用固定四 tick 时间片；设备层
  尚未提供按最早 deadline 重新编程的 one-shot 时钟事件接口。

## v2.7 通用块设备层

`BlockDevice` 已从文件系统缓存头上移到设备模块。旧
`FileSystemBlockDevice`/`FileSystemBlockDeviceStatus` 暂时是兼容别名，现有
rootfs、journal、swap 和宿主测试设备继续工作，但新驱动只实现设备层类型。
接口通过静态函数表和 `BlockDeviceAdapter<DriverType>` 分派，不使用 C++ virtual、
RTTI 或 `__cxa_pure_virtual`，并保持 `constinit` ATA 运行时无全局构造依赖。

通用队列不再读取 ATA 常量。驱动初始化队列时提交 `BlockDeviceGeometry`；ATA
声明 512 字节、LBA28、单块和单 outstanding，后续 NVMe 根据 Identify/MDTS 与
创建成功的 I/O queue 深度声明自己的几何。多个请求可处于 Issued，完成可以
乱序；超时按 deadline/identifier 稳定选择一个请求，调用者可循环处理同一时刻
到期的其余请求。

V2.10.1 为同一存储增加 completion FIFO。请求由 IRQ、timeout 或 cancel 首次解析后按
发生顺序入队；`TakeCompletion` 返回不含内部链指针的 owner/result 快照并回收槽。
按 identifier 的直接 Reap 仍用于恢复，但会从完成链任意位置摘除。ATA completion 已
迁移到该出口。设计见
[ADR 0063](../adr/0063-v2-10-ordered-block-completion-channel.md)。

V2.10.2 在同步 `BlockDevice` 旁增加独立 `AsynchronousBlockDevice` 静态函数表。ATA 与
NVMe namespace 都暴露 geometry、submit、best-effort cancel、timeout service 和 ordered
completion；上层不再识别 ATA active slot、NVMe CID 或 doorbell。ATA queued 请求可以
取消，已经签发的 ATA/NVMe 请求返回 RequestInProgress。设计见
[ADR 0064](../adr/0064-v2-10-asynchronous-block-device-adapter.md)。

V2.10.3a 增加 `BlockIoCoordinator`、独立 completion WaitQueue 和常驻 Kernel Thread。
IRQ/timer 只解析完成并递增通知 generation；Worker 在非 IRQ 上调用 `TakeCompletion`，再按
owner/request id 精确发布结果与唤醒。secondary ATA Flush probe 已真实穿过 IRQ15 和
Kernel wait。3a 时 rootfs/swap 的 `BlockIoDevice` 明确关闭异步开关：现有 VFS/cache/swap
调用链会持有 spin lock，必须先经浅层 I/O worker 委托和锁拆分后才能迁移。设计见
[ADR 0065](../adr/0065-v2-10-block-io-kernel-wait-and-migration-boundary.md)。

V2.10.3b 以 User Kernel stack 续体和 `RuntimeMutex` 完成上述安全前提，root/swap
`BlockIoDevice` 已打开异步等待。ATA Write 在命令后轮询 DRQ 并立即传输数据，最终 IRQ
发布 completion；NVMe 仍在非 IRQ `TakeCompletion` 阶段回拷 Read DMA。early boot 与
受限 Kernel worker 保留同步回退。承载 root/swap 的 NVMe controller 是系统级持久资源，
正常 `READY` 后仍保持活动；独立 probe 与 EIO/timeout recovery 继续验证 shutdown 后
DMA/MMIO/PCI/MSI-X 全部回收。设计见
[ADR 0066](../adr/0066-v2-10-stackful-user-kernel-continuation-and-runtime-mutex.md)。

`pci_model` 已支持 256 bus、每 bus 32 device、每 device 8 function 的 mechanism #1
地址，识别 class code `01/08/02`，并解析 32/64 位 memory BAR 和尺寸探测掩码。
`PciConfigurationSpace` 使用同一个 irq-save lock 保护 `0xCF8` 地址选择与 `0xCFC`
数据读写，防止中断路径夹入另一笔访问。

第三增量扫描总线，探测并分配 BAR0，在 KVA 建立 cache-disabled MMIO，完成
CAP/VS、CC/CSTS、admin SQ/CQ、Identify Controller 与 Namespace 1。第四增量用
Set Features 请求一对队列，创建深度 64 的 CQ1/SQ1，并经 `BlockDeviceAdapter`
完成单页 Read/Write/Flush。真实 QEMU 证据识别 128 GiB、512 字节逻辑块，在末端
8 个 LBA 回验 4 KiB 数据，退出时关闭控制器、解除 MMIO、释放六个 DMA 页并恢复
原 BAR。

当前每槽 16 页把单请求限制为 64 KiB，四槽 completion 按 CQ 实际顺序交付；64 位
request id 与 16 位 CID 分离。IRQ 只标记完成，Read DMA 回拷在非 IRQ take 阶段执行；
EIO 与超时都会 reset 并保留已产生的公共完成。Namespace 1/2 已分别承载 Kernel
rootfs/swap，缺失设备自动回退 ATA；ROM/Stage 1 仍从 ATA 启动。尚未实现链式多页 PRP
list、多 I/O queue 或 MSI-X 多向量。V2.10.3b 已把生产 User rootfs/swap 接入 BlockIo
Kernel WaitQueue 与 completion Worker。

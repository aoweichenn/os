# ADR 0030：以 CpuLocal、统一 UserContext 和返回白名单建立原生系统调用边界

- 状态：已接受
- 日期：2026-07-26

## 背景

v1.2 已经把 Process 与 Thread 分开。每个 Thread 拥有独立动态 Ring 0 栈和
完整执行现场，调度器也能在切换 CR3 后同步 TSS.RSP0。这使 v1.3 可以只解决
架构入口问题，而不再同时迁移调度对象。

此前用户态只能执行 `INT 0x80`。长模式 interrupt gate 会在从 Ring 3 进入
Ring 0 时自动查询 TSS.RSP0、换栈，并压入 `SS:RSP:RFLAGS:CS:RIP`。这条路径
容易解释，但每次都要查 IDT、执行门权限检查和通用中断语义。x86-64 提供更短的
`SYSCALL/SYSRET` 路径，却刻意省略了操作系统相关策略：

- `SYSCALL` 不读取 TSS，也不自动切换 RSP；
- 用户 RIP 和 RFLAGS 只被复制到 RCX 与 R11；
- `SYSCALL` 只从 STAR 选择内核 CS/SS，从 LSTAR 取得入口 RIP；
- `SYSRET` 对非规范 RCX 等状态的故障行为发生在 Ring 0，不能直接接受未经
  校验的用户现场；
- GS 当前基址和内核每 CPU 基址必须由软件显式交换；
- 中断可能在系统调用 C++ 分发期间到达，但调度不得在任意 Ring 0 调用链中
  直接换栈。

如果只把用户包装器的 `int 0x80` 改成 `syscall`，第一条指令仍运行在攻击者
控制的用户 RSP 上。任何 `push`、函数调用或异常都会把 Ring 0 数据写入用户页，
这不是性能问题，而是特权边界错误。

## 决策

### 启动时冻结完整处理器规格

内核先读取以下 CPUID 叶，再初始化依赖这些能力的架构状态：

| 叶与字段 | 必需能力 |
| --- | --- |
| `CPUID.01H:EDX[24]` | FXSR |
| `CPUID.01H:EDX[25]` | SSE |
| `CPUID.01H:EDX[26]` | SSE2 |
| `CPUID.80000001H:EDX[11]` | SYSCALL/SYSRET |
| `CPUID.80000001H:EDX[20]` | NX |
| `CPUID.80000001H:EDX[29]` | long mode |
| `CPUID.80000008H:EAX[7:0]` | 36..52 位物理地址 |
| `CPUID.80000008H:EAX[15:8]` | 当前必须为 48 位虚拟地址 |

`ProcessorFeatureProfile` 同时保存逐项布尔值、available mask、missing mask
和地址宽度。只有完整 profile 验证成功后才允许继续。缺失能力时串口输出
`PROCESSOR_FEATURES_UNSUPPORTED` 与 `PROCESSOR_MISSING_FEATURES`，随后停止；
不得靠执行一条可能不存在的指令来探测。

### 使用单元素 CpuLocal 表达每 CPU 入口状态

当前系统只有一个 BSP，但入口接口按每 CPU 状态设计。`CpuLocal` 按 64 字节
对齐，前四个 64 位字段是汇编 ABI：

| 偏移 | 字段 | 所有者 |
| ---: | --- | --- |
| 0 | self address | 初始化后只读，用于完整性校验 |
| 8 | current Thread slot | 调度器写，入口和诊断读 |
| 16 | trusted kernel entry RSP | 调度器与 TSS.RSP0 同步写 |
| 24 | transient user RSP | 原生汇编入口在换栈前写 |

其余字段保存 IRQ 深度与峰值、抢占禁止深度与峰值、`need_reschedule`、活动
系统调用入口类型，以及兼容/原生入口、IRQ 打断、返回前调度、SYSRET、IRET、
拒绝返回和可信栈校验计数。

`CpuLocal::SetCurrentThread` 与 TSS.RSP0 更新属于同一个 Thread 激活事务。
结束全部用户执行时，两者都恢复永久启动栈。`CpuPreemptionGuard` 用 RAII
维护抢占深度；当前单 BSP 不因此宣称已经具备 SMP 安全性。

### 配置并回读六个 MSR

通过验证后，内核构造并写入：

| MSR | 地址 | 当前值或规则 |
| --- | ---: | --- |
| IA32_EFER | `0xC0000080` | 保留原值并置 SCE bit 0 |
| IA32_STAR | `0xC0000081` | kernel base selector `0x08`，SYSRET base selector `0x10` |
| IA32_LSTAR | `0xC0000082` | `OsKernelNativeSystemCallEntry` |
| IA32_FMASK | `0xC0000084` | 清 TF、IF、DF、NT、AC |
| IA32_GS_BASE | `0xC0000101` | 用户 GS 基址，当前为零 |
| IA32_KERNEL_GS_BASE | `0xC0000102` | 当前 `CpuLocal` 地址 |

STAR 的高 16 位不是直接写用户 CS。64 位 `SYSRET` 从该基值加 16 得到 CS，
加 8 得到 SS，并把 RPL 设为 3。因此当前 GDT 顺序必须满足：

```text
STAR user base = 0x10
SYSRET SS       = 0x10 + 8  | 3 = 0x1B
SYSRET CS       = 0x10 + 16 | 3 = 0x23
```

所有写入都立即 `RDMSR` 回读。任何布局构造或回读差异进入独立的
`NATIVE_SYSCALL_INITIALIZATION_FAILED`，不能误报为 CPUID 缺失。

### 两条入口统一为 176 字节 UserContext

`UserContext` 由原 160 字节 `ExceptionFrame` 加用户 RSP、SS 两项组成：

```text
15 个通用寄存器
vector
normalized error code
RIP
CS
RFLAGS
RSP
SS
```

总大小固定 176 字节，并以 `static_assert` 锁定首地址与偏移。入口来源通过
规范化 vector 区分：

| 来源 | vector |
| --- | ---: |
| 首次进入用户态 | 0 |
| 硬件 IRQ | 32..47 |
| `INT 0x80` | `0x80` |
| `SYSCALL` | `0x81` |

兼容入口继续由硬件在 TSS.RSP0 上形成五项返回帧，再补 vector/error code。
原生入口严格按以下顺序执行：

```text
SYSCALL
  → SWAPGS
  → [GS:24] = untrusted user RSP
  → RSP = [GS:16] trusted Thread kernel stack
  → 用 RCX/R11/暂存 RSP 构造与 INT 0x80 相同的 UserContext
  → 保存通用寄存器并切换内核数据段
  → STI，仅在完整可信现场形成后允许 IRQ
  → 同一个 C++ syscall dispatcher
```

汇编入口本身不解释系统调用号，也不复制用户内存。C++ 分发器先证明 frame
属于当前 Thread 的活动动态内核栈、RIP/RSP 映射权限正确，并且原生 frame
中的 RSP 与 GS 暂存值一致，然后才处理 ABI。

### IRQ 只提交重调度请求，返回用户态前执行

IRQ 进入和离开都更新 `CpuLocal.interrupt_depth`。如果 IRQ 在活动系统调用
中到达，则累计一次 `interrupt_during_system_call`。Ring 0 定时器 IRQ
只设置 `need_reschedule`，不在被打断的任意 C++ 调用链里直接换栈。

系统调用分发结束、关闭中断后，统一返回准备函数消费该标志，并调用现有
Thread 调度器选择最终 resume frame。这保证所有换栈都发生在一个已知、可审计
的返回边界。调度可能选中由初始进入、IRQ、兼容系统调用或原生系统调用保存的
任意合法 `UserContext`；返回选择因此不能只看当前汇编入口。

### 返回使用“先证明合法，再选择最快路径”

所有返回都先验证：

1. frame 位于当前 Thread 拥有的动态内核栈；
2. RIP 与 RSP 是当前 48 位模型的低半规范地址；
3. RIP 位于用户 RX、非写映射；
4. 栈探测地址位于用户 RW/NX 映射；
5. CS=`0x23`、SS=`0x1B`；
6. RFLAGS bit 1 必须为 1，且 IOPL、NT、VM、AC、ID 等危险/未支持位为零。

验证失败时不执行 `SYSRET` 或带伪造现场的 `IRET`。内核记录低频安全诊断，
把该用户进程按异常终止，再对新选中的 frame 重复验证。

合法现场按白名单选择：

- 只有来源为原生 `SYSCALL`，且 RFLAGS 不包含快速集合外的合法位，才执行
  `SYSRETQ`；
- `INT 0x80`、初始现场、硬件 IRQ 现场一律 `IRETQ`；
- 原生现场若带 DF 或 RF 等合法但不适合当前 SYSRET 路径的状态，也回退
  `IRETQ`。

用户测试包装器在 `SYSCALL` 前置 DF，确认入口 `CLD` 不污染保存的用户
RFLAGS，返回选择确实走 IRET，包装器恢复后再 `CLD` 维护 C++ ABI。

### SWAPGS 状态跨越非局部返回

当最后一个可运行 Thread 在原生系统调用内退出或阻塞时，控制流可能直接恢复
`ExecuteProcesses` 的永久内核调用链，不再经过普通系统调用汇编尾部。此时
若遗漏第二次 `SWAPGS`，后续第一次原生入口会把 GS 交换到错误一侧。

因此在清理 `CpuLocal` 活动入口状态前先捕获“当前是否来自原生入口”，并把
该布尔值传给 `OsKernelReturnFromUserMode`。永久栈恢复桩在需要时先执行
`SWAPGS`，再恢复内核段和保存的调用链。这一状态不能由待恢复 Thread 的
vector 推断，因为调度可能已经换了 frame。

### NMI 使用最小 fail-stop 路径

NMI 可在 `SWAPGS` 和换栈之间的极小窗口到达，不能假设普通 C++ 入口所需的
栈与 GS 状态都已稳定。当前 v1.3 不尝试从 NMI 恢复：向量 2 使用 TSS.IST2，
只写入一个固定 BSS 观察位，关闭中断并停机。它不打印、不分配、不取得锁、
不访问 `CpuLocal`。后续若引入可恢复 NMI，必须另写 ADR 定义 paranoid
SWAPGS 判定和嵌套语义。

## 验证

### 宿主测试

- 处理器能力单元测试覆盖完整 profile、逐项能力缺失、物理宽度上下界与
  非 48 位虚拟宽度；
- UserContext 单元测试覆盖四类入口、规范地址边界、段、RFLAGS、SYSRET
  白名单和 IRET 回退；
- CpuLocal/MSR 布局集成测试覆盖初始化、线程所有权、深度平衡、延迟调度、
  计数、非局部清理、STAR 选择子关系和回读差异；
- 固定种子 `0x5A17C011BADC0FFE` 对 100000 个现场执行 400000 项地址、
  验证、返回选择和非法 RIP 性质检查。

### QEMU 测试

- 256 MiB functional 路径要求双入口返回值和副作用一致；
- 正常路径必须观察到至少一次原生 SYSRET 和一次原生 IRET 回退；
- 系统调用分发主动等待一次 PIT IRQ，证明 IRQ 真正打断活动系统调用；
- Ring 0 IRQ 只置重调度请求，最终统计必须出现非零返回前 reschedule；
- 可信栈校验次数必须覆盖全部兼容与原生入口，拒绝返回数必须为零；
- `-cpu qemu64,-syscall` 必须输出 non-zero missing mask，且禁止出现
  `PROCESSOR_FEATURES_READY`、GDT、用户态、panic 或 `READY`。

Kernel ELF 审计同时要求存在原生汇编入口、返回准备和返回选择三个符号。

## 后果

### 正面

- 用户默认入口已使用 x86-64 原生机制，但兼容入口仍可用于差分回归；
- 用户 RSP 从未成为系统调用入口栈，Thread 栈所有权可被直接审计；
- 入口、IRQ 和返回调度拥有统一状态机，而不是三个隐式全局标志；
- `SYSRET` 的危险输入被白名单挡在指令之前；
- v1.4 可以在不修改架构入口的前提下替换系统调用背后的对象与 fd 实现。

### 代价与边界

- 当前 `CpuLocal` 只有一个实例，没有 AP 启动、per-CPU 分配或跨核同步；
- 用户 GS/TLS 尚未开放，IA32_GS_BASE 当前固定为零；
- 当前虚拟地址规格冻结为四级 48 位，不接受 LA57；
- NMI 是 fail-stop，不是可恢复诊断设施；
- `INT 0x80` 仍占用 DPL3 interrupt gate，移除时间由 ABI 冻结阶段决定；
- SYSRET 与 IRET 统计是诊断事件计数，不是性能基准。

## 未采用方案

### 在用户 RSP 上临时压栈后再切换

会在 Ring 0 向用户页写入数据，且用户可把 RSP 指向只读、未映射或共享区域，
不能形成安全入口。

### 删除 INT 0x80

会失去双入口差分证据，也会把 ABI 迁移与入口实现绑定在同一个不可回退改动中。

### 所有原生返回都使用 SYSRET

无法安全承载 DF/RF 等合法现场，也会让非规范地址在 Ring 0 产生难以恢复的
异常。快速路径必须是验证后的选择，不是来源为 SYSCALL 就无条件使用。

### 所有返回都使用 IRET

安全但回避了本阶段要学习和验证的 STAR/LSTAR/FMASK/SWAPGS/SYSRET 语义，
也无法证明原生快速路径真的可用。

### 在 IRQ 中立即调度

会让任意内核函数都成为潜在非局部换栈点，锁、临时对象和 GS 状态难以推理。
当前不可抢占 Ring 0 契约要求只在返回用户态前消费调度请求。

## 相关文档

- [开发路线](../roadmap.md)
- [v1.3 发布记录](../releases/v1.3.md)
- [架构说明](../architecture.md)
- [内核模块](../modules/kernel.md)
- [测试策略](../testing.md)
- [芯片与寄存器结构](../hardware/chips.md)


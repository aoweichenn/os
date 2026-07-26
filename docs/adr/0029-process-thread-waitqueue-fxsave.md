# ADR 0029：分离 Process/Thread，以 WaitQueue 和 FXSAVE 冻结单 BSP 执行模型

- 状态：已接受
- 日期：2026-07-26

## 背景

v0.9 到 v1.1 使用一个四槽 PCB 同时表示地址空间所有者、文件描述符所有者和
可调度执行流。这条过渡路径足以证明 Ring 3、CR3/TSS 切换、抢占、阻塞和资源
回收，却不能继续承载多线程语义：

1. 一个 PCB 只有一个执行现场，无法表达多个 Thread 共享同一 AddressSpace；
2. PID 同时被当作执行身份和资源身份，ThreadExit 与 ProcessExit 无法区分；
3. 阻塞只保存枚举原因，没有对象化等待队列，condition、timeout、signal、
   close 和 cancel 可能重复把同一执行流放回 Ready；
4. 原调度只保存通用寄存器帧。x87、MMX、SSE/SSE2 和 MXCSR 仍属于 CPU，
   抢占后会被下一个用户执行流继承；
5. 资源规格仍被“四个用户程序”掩盖，无法证明 64/128 和 256/512 的目标
   容量走的是同一实现。

本阶段仍保留 `INT 0x80`。若同时引入 CpuLocal、`SYSCALL/SYSRET`、
`SWAPGS` 和新的对象模型，入口错误与调度所有权错误会难以隔离；这些架构入口
留给 v1.3。

## 决策

### Process 只拥有共享资源，Thread 才能进入 run queue

`ProcessEntry` 保存：

- 独立单调 `ProcessId`；
- `Alive` 或 `Zombie` 状态；
- 地址空间根物理地址；
- Thread 单向所有权链；
- Thread 总数、live 数和 exited 数。

`ThreadEntry` 保存：

- 独立单调 `ThreadId` 与所属 Process 槽；
- `Ready`、`Running`、`Blocked`、`Exited` 状态；
- 动态内核栈槽、用户栈、TLS base 和 signal mask 位置；
- run tick、dispatch、block 和 wake 统计；
- Process Thread 链、双向 run queue 链和单向 WaitQueue 链；
- 当前 WaitCondition、最终 WakeReason 和 WaitQueue 归属。

PID/TID 的零值无效；有效标识不等于数组下标，槽位回收后新对象继续取得更大
标识。固定数组当前只是无动态分配的存储后端，不进入公开身份语义。

状态守恒式为：

```text
owned_thread
  = ready + running + blocked + exited

owned_process
  = alive + zombie

process.thread_count
  = process.live_thread_count + process.exited_thread_count
```

全局最多一个 Running Thread。最后一个 live Thread 退出时 Process 才从
Alive 进入 Zombie；Exited Thread 必须先逐个 reap，`thread_count==0` 后
才能 reap Zombie Process。

### 使用显式侵入式队列，不在调度热路径分配

run queue 采用 ThreadEntry 中的前后索引形成 FIFO 双向链；Process 的 Thread
列表和 WaitQueue 采用单向链。所有索引使用 `UINT64_MAX` 作为无节点哨兵，
而 PID/TID 使用零值作为无身份哨兵，两类概念不混用。

选择侵入式队列是因为：

- 调度、IRQ 唤醒和退出路径不能因 heap 耗尽而失去前进能力；
- 队列成员关系与 Thread 生命周期同步，避免额外节点的双重所有权；
- 固定容量模型可以逐槽扫描验证每个 Thread 恰好属于一个状态集合；
- 未来替换对象存储后，队列协议不需要改变。

本阶段的“动态 run queue”指成员随 Thread 生命周期动态插入/移除，不表示
使用标准库容器或在 IRQ 中动态分配。

### WaitQueue 只允许一个 WakeReason 获胜

每个阻塞对象持有独立 `WaitQueueId`、FIFO 头尾、当前等待数、累计入队/唤醒/
关闭数和 closed 状态。阻塞事务在调度锁内一次提交：

```text
Running
  → 写入 WaitCondition，WakeReason=None
  → 关联 WaitQueue 并入队
  → Blocked
  → 选择下一个 Ready
```

唤醒事务同样在调度锁内一次提交：

```text
Blocked + WakeReason=None
  → 从且仅从原 WaitQueue 移除
  → 写入 ConditionSatisfied | Timeout | Signal |
           ObjectClosed | Cancelled
  → Ready queue 尾部
```

第二个竞争完成者观察到 Thread 已不是该队列中的
`Blocked + WakeReason=None`，得到 `WakeAlreadyResolved`，不得再次入队。
关闭队列先提交 closed，再以 `ObjectClosed` FIFO 唤醒全部 waiter；后续阻塞
得到 `WaitQueueClosed`。

### 冻结三类锁的调用环境

| 原语 | 允许环境 | 是否可调度 | 本阶段语义 |
| --- | --- | ---: | --- |
| `SpinLock` | 单 BSP Thread 上下文短提交区 | 否 | acquire/release，忙等使用 `pause` |
| `IrqSaveSpinLock` | 与本 BSP IRQ 共享的短状态 | 否 | 保存 IF、关中断、取锁、解锁后恢复原 IF |
| `Mutex` | 可阻塞 Thread 上下文 | 是 | 无竞争快速取得；竞争者进入 FIFO WaitQueue |

`CurrentSpinLockDepth` 是单 BSP 的运行时防误用门禁。`Mutex::Lock` 与
`Mutex::Unlock` 在深度非零时返回 `SpinLockHeld`，因此不会在持有 spinlock
的调用链中进入调度器。IRQ 路径不使用 Mutex。

Mutex 解锁存在 waiter 时采用直接交接：队首 Thread 被唤醒后立即成为逻辑
owner，其他 Thread 不能在它实际运行前插队。新 owner 恢复后用一次
`TryLock` 确认 handoff；这次确认不会重复改变队列。

### 每 Thread 使用 512 字节 FXSAVE64 现场

目标 CPU 必须在 `CPUID.01H:EDX` 同时报告：

| 位 | 名称 | 作用 |
| ---: | --- | --- |
| 24 | FXSR | 支持 FXSAVE/FXRSTOR |
| 25 | SSE | 支持 XMM 与 MXCSR |
| 26 | SSE2 | x86-64 用户基线需要的整数/双精度扩展 |

初始化按顺序设置：

- `CR0.MP=1`、`CR0.NE=1`；
- `CR0.EM=0`、`CR0.TS=0`；
- `CR4.OSFXSR=1`、`CR4.OSXMMEXCPT=1`；
- `CR4.OSXSAVE=0`，明确保持 AVX/XSAVE 禁用。

随后 `FNINIT`，载入默认 `MXCSR=0x1F80`，用 `FXSAVE64` 生成确定性初始模板。
每个 Thread 创建时复制模板；抢占、阻塞和退出前保存当前现场，激活下一个
Thread 时在切换其 CR3/TSS.RSP0 后执行 `FXRSTOR64`。

`FxSaveArea` 使用 `alignas(16)`，大小固定 512 字节。这里保存的是完整
x87/MMX/XMM0..XMM15/MXCSR 架构区域；不是只复制用户测试涉及的几个寄存器。
缺少任一 CPUID 能力时输出 `EXTENDED_STATE_UNSUPPORTED` 并停止，禁止静默
退化为不隔离的上下文。

### 三档容量走同一目标实现

运行时按受管可用内存选择限制，不通过条件编译替换调度器：

| 档位 | RAM | Process | Thread | 单 Process Thread |
| --- | ---: | ---: | ---: | ---: |
| bootstrap | 64 MiB | 4 | 4 | 1 |
| functional | 256 MiB | 64 | 128 | 32 |
| capacity | 64 GiB | 256 | 512 | 64 |

启动容量事务不是只填控制块。它为每个 Process 建立真实用户页表根，为每个
Thread 建立真实双 guard 动态内核栈并初始化 FXSAVE 区；至少一个 Process
实际达到档位的单进程线程上限。全部 Thread 经调度进入 Exited，随后按
Thread→stack、Process→page-table 的所有权逆序 reap。事务前后比较 frame、
buddy、KVA、页表、stack 和补充 Process/Thread 计数，必须零差异。

KVA 描述符容量因此从 256 提升为 1024，动态栈槽从 256 提升为 512。前者
必须同时容纳 512 个六页栈区、永久保护区和其他启动事务，不能把 KVA 描述符
耗尽误报为 Thread 上限。

### 迁移现有四程序并删除旧调度器

正常目标仍运行 PID1 Shell、PID2 producer、PID3 consumer 和 PID4 worker，
但每个进程现在由 Process 资源对象和唯一初始 Thread 组成。CR3 属于 Process，
动态内核栈和 FXSAVE 区属于 Thread。管道与控制台阻塞统一进入四个具名
WaitQueue；系统调用层只提交 WaitCondition，不直接操作 run queue。

旧 `process_scheduler.hpp/.cpp` 及其测试已经删除。保留同名进程级系统调用
函数只是当前用户 ABI 兼容入口，不表示调度实体仍是 Process。

## 验证

### 宿主模型

- 单元测试覆盖未初始化、非法容量、256/512 完整容量、单进程第 65 个 Thread
  拒绝、PID/TID 槽复用不复用身份、Thread 两级回收和 Zombie；
- WaitQueue 测试按 FIFO 交付五类 WakeReason，并对每个 waiter 主动发起
  第二次唤醒，必须得到 `WakeAlreadyResolved`；
- close 测试一次唤醒全部 waiter，验证 closed 后阻塞和二次 close 均失败；
- Mutex 测试验证竞争睡眠、直接 handoff、owner 确认和无 waiter 解锁；
- spin/irq-save 测试验证深度、acquire/release 与进入前 IF 恢复；
- 集成测试让三个 Process 的六个 Thread 运行 48 tick，每 Thread 精确得到
  8 tick，然后经过阻塞、唤醒、全部退出和两级 reap；
- 固定种子 `0x5448524541445632` 执行 100000 步创建、调度、阻塞、唤醒、
  退出、Thread reap、Process reap 和槽复用；每一步都验证完整状态集合、
  队列、标识与累计/当前计数守恒。

### 目标机

四个用户程序分别安装不同的 XMM0、XMM15、MXCSR、x87 control word 和 ST0
模式。用户 C++ 以 `-mno-sse -mno-sse2` 编译，只有具名 NASM 验收桩修改这些
寄存器，因此普通代码不会意外重写模式。每个程序在自己的抢占或阻塞边界后
校验，并在退出前后输出一次 `EXTENDED_STATE_ISOLATED`；正常 QEMU 要求精确
四次。

256 MiB functional 与 64 GiB capacity 解析精确容量标记、ThreadId、
FXSAVE save/restore 非零计数、动态栈高水位和零资源差异。额外使用
`-cpu qemu64,-sse2` 启动同一镜像，必须只到达
`EXTENDED_STATE_UNSUPPORTED`，不得进入 GDT 后续初始化、用户态或 READY。

## 后果

### 正面

- 共享资源与执行现场的所有权已经分开，v1.3 可以把 CpuLocal.current 指向
  Thread，而不再迁移 PCB；
- 所有阻塞设施拥有统一单赢家协议，后续 deadline、signal 和 futex 可以复用；
- x87/SSE2 污染从隐式风险变成可重复整机证据；
- 64/128 与 256/512 是实际资源生命周期，不是只写在规格表中的常数；
- 旧 PCB 执行模型彻底删除，不存在双调度器长期漂移。

### 代价与边界

- 当前目标用户 ABI 尚未暴露 CreateThread/ThreadExit；多 Thread 生命周期先由
  宿主和目标容量事务证明，用户线程 API 留到 v1.13；
- 正常四程序仍各有一个 Thread，ProcessExit 兼容入口会结束这个初始 Thread；
- `CurrentSpinLockDepth` 与 scheduler lock 是单 BSP 设计，不承诺 SMP；
- FXSAVE 不保存 AVX/YMM 上半部，`CR4.OSXSAVE` 明确关闭；v2.0 也不扩展
  XSAVE；
- Process/Thread 存储仍有编译期上界，但公开容量由运行时档位选择，后续对象
  分配器接入不改变身份和状态机。

## 未采用方案

### 继续让 PCB 同时表示 Process 和 Thread

改动较小，但 ThreadExit、共享地址空间和每 Thread FXSAVE 都只能继续堆入
含义冲突的对象，无法形成长期接口。

### 使用“唤醒标志”而不建立 WaitQueue

不能证明 waiter 只入队一次，也无法处理 timeout、signal 和 close 的竞争；
丢失唤醒会被推迟到更复杂的 futex 阶段才暴露。

### 使用惰性 FPU 切换

通过 `CR0.TS/#NM` 推迟保存曾用于早期处理器性能优化，但会增加异常状态机和
安全边界。当前单 BSP 教学内核优先选择每次真实切换都 eager save/restore，
语义更直接且足以满足 QEMU TCG 性能预算。

### 直接启用 XSAVE/AVX

XSAVE 区大小与 XCR0、CPUID leaf 0xD 协商相关，会扩大本阶段的架构面；v2.0
目标明确不需要 AVX。关闭 OSXSAVE 能让“未保存状态”成为不可用能力，而不是
静默泄漏。

## 相关文档

- [开发路线](../roadmap.md)
- [项目架构](../architecture.md)
- [Kernel 模块](../modules/kernel.md)
- [测试策略](../testing.md)
- [日志规范](../logging.md)
- [v1.2 发布记录](../releases/v1.2.md)

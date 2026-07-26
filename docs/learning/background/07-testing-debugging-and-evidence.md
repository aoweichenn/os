# B7：测试、调试与证据

## 1. “启动了”不是可验证结论

操作系统错误常表现为：

- 黑屏。
- 串口停在某一行。
- QEMU reset。
- CPU HLT。
- 偶尔成功。
- 正常路径看似完成，资源或持久性已损坏。

因此不能把“我看到 READY”当作唯一证据。要先明确命题，再选择能区分正确与错误
实现的观察。

## 2. 测试的基本结构

每项测试至少包含：

```text
arrange: 建立输入和前置状态
act:     执行一个明确动作
assert:  检查结果、不变量和副作用
```

对错误路径还要检查：

- 返回了哪个错误。
- 输出对象是否保持规定状态。
- 已获得资源是否回滚。
- 后续操作是否仍可用。
- 禁止日志是否未出现。

## 3. Oracle

Oracle 是判断结果正确的依据。

### 3.1 精确值

例如：

- CRC。
- frame count。
- syscall error。
- ELF entry。

### 3.2 Reference model

用更简单 host 数据结构模拟：

- byte-array 文件。
- ring buffer。
- scheduler state。

随机输入下把目标结果与 reference 逐步比较。

### 3.3 Invariant

不依赖具体输出序列：

```text
0 <= buffered <= capacity
allocated + free + reserved = managed
written - read = buffered
all allocated inode reachable
no W+X PT_LOAD
```

### 3.4 Protocol marker

整机通过稳定串口标记证明控制流边界。marker 只能证明它代表的命题，不应一条
`READY` 代替所有内部不变量。

## 4. 为什么测试要分层

| 层 | 擅长 | 不能单独证明 |
| --- | --- | --- |
| unit | 小状态机、边界、错误 | 真硬件入口和链接布局 |
| integration | 多模块组合 | 最终 CPU/设备路径 |
| randomized/property | 大量组合、不变量 | 未建模硬件事实 |
| artifact audit | ELF/raw/反汇编事实 | runtime 动态行为 |
| QEMU system | 全启动链与设备 | 穷举内部输入 |
| multi-boot | 真持久性 | 所有局部格式边界 |

“完整测试”不是全部放进 QEMU，而是让每个命题落到最便宜且足够强的层。

## 5. Unit test

适合：

- 地址 range。
- ELF parser。
- page-table entry encoder。
- scheduler。
- pipe/FIFO。
- shell parser。

### 5.1 让核心逻辑脱离硬件

例如扫描码 decoder 只接收 byte、输出 event，不直接执行 `in 0x60`。这样：

- host 可穷举。
- IRQ handler 只负责读取与转交。
- QEMU 再证明接线。

### 5.2 Unit 不应伪装系统测试

在 host 手工调用 `HandleKeyboardInterrupt()` 并传 `'a'`，不能证明：

- i8042 配置。
- IRQ1 route。
- scan set。
- EOI。

命名和文档要准确说明证明范围。

## 6. Integration test

组合多个真实模块但仍可在 host：

- filesystem + memory block device。
- ELF loader + constructed image。
- image layout + region ownership。
- allocator + page table mock/direct memory。

它能发现接口假设不一致，例如 boot area 增长后覆盖 filesystem start，而单个
filesystem unit 不知道最终 Kernel 大小。

## 7. Randomized/property test

随机测试不是“随机点几下”，而是：

1. 固定 seed。
2. 定义输入 generator。
3. 定义 reference 或 invariant。
4. 记录最小可复现上下文。
5. 多轮执行。

### 7.1 固定 seed

失败必须能在开发机/CI 重放。可以定期新增 seed 集合，但单次验证不能依赖不可
记录的系统随机。

### 7.2 Generator 要覆盖结构

纯均匀 64-bit 随机几乎不会命中“刚好页边界前 1”。应混合：

- 0、1、max。
- alignment±1。
- capacity±1。
- known valid。
- known invalid。
- 大范围随机。

### 7.3 Failure atomicity

随机 invalid input 还应检查：

- parser output 没有 partial args。
- allocator stats 不变。
- file transaction 未半占 bitmap。
- ELF target memory 未部分写。

## 8. Artifact audit

最终二进制可能与源码直觉不同。

### 8.1 ELF audit

检查：

- machine/class/type。
- entry。
- program headers。
- W^X。
- `.init_array`。
- undefined/runtime symbols。
- section/segment ranges。

### 8.2 Raw image audit

检查：

- exact size。
- reset vector offset。
- descriptor fields。
- CRC。
- padding。
- disk region overlap。

### 8.3 Disassembly audit

检查必须是机器指令事实的约束：

- `sti/hlt/cli` adjacency。
- 特定入口指令。
- 不出现意外 SSE。
- vector stub 形状。

源文件相邻、函数标 `inline`、注释写“不会调用”都不构成最终事实。

## 9. QEMU system test

QEMU 从 reset vector 执行完整 Guest：

- ROM。
- ATA。
- mode switch。
- Kernel。
- IRQ/device。
- Ring 3。

### 9.1 Required markers

按顺序出现：

```text
RESET
SERIAL_READY
...
KERNEL...
USER...
READY
```

顺序断言比“所有字符串某处出现”更强。

### 9.2 Forbidden markers

例如：

- PANIC。
- unexpected reset。
- USER_RESULT_INVALID。
- fault image 中不应出现的后续 READY。

成功协议要同时定义不得发生什么。

### 9.3 Exact count

Shell command markers、process result、输入 byte 等要求精确次数，可发现重复执行、
重启或遗漏。

### 9.4 Timeout

宿主 timeout 是测试预算，不是 Guest 正确性结果。超时时要保存：

- serial output。
- QEMU return code。
- timed output。
- 当前阶段标记。

避免只报“test failed after 2s”。

## 10. Failure injection

一个机制只有失败可重复，才说明边界真实存在。

当前例子：

- UART 永不 ready。
- IDE timeout/error。
- descriptor/payload corruption。
- invalid ELF。
- `UD2`。
- unmapped page。
- Kernel write read-only page。
- Ring 3 #UD/#PF。
- corrupt superblock。

### 10.1 故障要尽量单一

故障镜像只改变一个条件，并要求：

- 之前 marker 出现。
- 对应失败 marker 出现。
- 之后成功 marker 不出现。

如果同时损坏三处，测试无法定位真正被验证的检查。

## 11. 正路径与负路径不对称

成功只说明一个输入通过。拒绝测试要覆盖不同错误分类：

- magic。
- version。
- length。
- overflow。
- overlap。
- permission。
- checksum。
- semantic consistency。

一个统一 `Invalid` 返回也许适合外部 ABI，但内部测试仍应检查具体 status 以
定位哪项不变量生效。

## 12. 持久性测试

同一 QEMU 内 write→read 可能只命中 RAM cache。

强证据：

```text
copy clean disk to temp
boot QEMU #1 snapshot=off
write + sync + exit
destroy QEMU #1
boot QEMU #2 using same temp disk
mount existing + read/verify
destroy QEMU #2
corrupt disk bytes on host
boot QEMU #3
must reject before Ring 3
```

两个新进程排除：

- Guest RAM。
- Kernel object。
- block cache。
- QEMU process state。

corruption boot 证明 mount 不会用 auto-format 掩盖损坏。

## 13. Resource conservation test

功能输出正确仍可能泄漏。

建立：

```text
before
→ create/run/fail/exit
→ after
```

比较：

- physical frames。
- page-table frames。
- descriptors。
- file handles。
- pipe endpoints。
- dirty cache entries。

对 failure path 特别重要：第 5 步失败时，前 4 步取得的资源必须回滚。

## 14. Model 与实现交叉验证

例如 scheduler：

- pure model 接收 tick/block/wake/exit。
- real runtime 接收 IRQ/syscall 并调用同一状态机。
- QEMU 最终统计与 model invariant 一致。

若 QEMU 自己另写一套判断逻辑，测试可能只验证测试代码的假设，不是生产模块。

## 15. 可观测性设计

### 15.1 日志是协议

稳定 marker：

```text
[OS][LAYER] EVENT
```

应：

- 单行。
- 无模糊前缀。
- 成功/失败边界明确。
- 高频事件聚合。

### 15.2 Log 与 state

日志记录结果，不应成为唯一状态存储。不能通过“打印过某行”决定是否释放资源。

### 15.3 热路径

不要逐：

- PIT tick。
- keyboard scan。
- pipe Try。
- cache hit。

打印。它会改变时序和性能。用固定宽度 counter，阶段结束汇总。

## 16. Host time 与 Guest time

宿主给每行串口加相对毫秒，只说明观察到输出的 wall-clock delay。

Guest monotonic time 来自 PIT tick/divisor。两者不同：

- QEMU 调度慢可能让 host 时间变长。
- Guest tick 语义仍按虚拟硬件交付。

测试不能用 host timestamp 代替 Guest timer correctness。

## 17. Debugging 的基本策略

不要在黑屏后随机加日志。先问：

1. 最后一个可靠事实是什么？
2. 下一项本应改变什么硬件/内存状态？
3. 能用哪个独立观察验证？
4. 是未执行、执行失败还是结果被覆盖？

## 18. 二分控制流

在两个 marker 之间插入一个边界：

```text
A exists
B missing
```

把区间分为：

```text
A → M
M → B
```

但早期 UART 本身可能故障，必要时使用：

- QEMU trace。
- GDB breakpoint。
- memory inspection。
- I/O log。

避免让同一个坏模块既执行功能又报告自己成功。

## 19. GDB 的观察层

### 19.1 Register

检查：

- RIP/CS/RSP。
- RFLAGS.IF。
- CR0/CR2/CR3/CR4。
- segment selectors。

### 19.2 Memory

区分：

- 当前 GDB 命令读 VA 还是 PA。
- 当前 CR3。
- endian。
- structure layout。

### 19.3 Disassembly

检查 CPU 将执行的真实字节：

- 当前 `bits` 是否匹配 mode。
- far jump target。
- stack adjustment。
- `iretq` frame。

### 19.4 Hardware tables

用内存/辅助命令检查：

- GDT/IDT descriptors。
- page-table entries。
- PIC/APIC state。

## 20. Triple fault 定位

现象常是 QEMU reset，没有 panic：

```text
first fault
→ handler entry itself faults
→ double fault
→ double-fault handling also fails
→ triple fault/reset
```

定位：

1. 关闭自动 reboot 或启用 QEMU debug 选项。
2. 在设置 CR0.PG、far jump、lidt、iretq 前断点。
3. 检查当前 code/stack mapping。
4. 检查 IDT limit/gate/selector。
5. 检查 TSS/IST/RSP0。
6. 单步最早 fault，而不是追 reset 后的新 RIP。

## 21. Page fault 定位

记录：

- CR2。
- error code。
- RIP/CPL。
- current CR3。
- page-table path。
- intended VA owner。

先分类：

```text
not present vs protection
read vs write vs fetch
user vs supervisor
reserved bit
```

再分析高级原因。只看到 vector 14 就补 map 会掩盖权限 bug。

## 22. Interrupt 不到达

从源到 CPU 逐层验证：

1. device 是否产生状态。
2. PIC IRR 是否 pending。
3. IMR 是否 unmasked。
4. LAPIC LINT route。
5. IF。
6. IDT vector present。
7. handler count。
8. EOI 后是否有第二次。

项目 v0.7 的 PIC IRR 已有请求但 CPU 无 handler，最终定位到 virtual-wire，
说明必须验证链而非单个寄存器。

## 23. Build success、boot success 与 semantic success

三层不能互相替代：

```text
build:
  syntax/types/links/layout audits pass

boot:
  CPU reaches milestones

semantic:
  ownership/permissions/results/persistence invariants pass
```

Shell 十条命令跑完，旧管道统计仍可能错误；这正是 final invariant 的价值。

## 24. Test naming

名称应表达命题：

```text
ConsoleInputDropsNewByteWhenCapacityIsExhausted
SchedulerKeepsCompletionFalseWhenBlockedProcessExists
MountRejectsNonZeroCorruptSuperblock
```

避免：

```text
Test1
Works
EdgeCase
```

失败输出要让开发者知道输入和违反的不变量。

## 25. 测试数据所有权

系统测试必须：

- 基线 image 只读。
- 每次复制到独立 temp。
- socket/log 路径唯一。
- 进程退出后清理。
- 并行测试不共享可写状态。

否则 flaky 可能来自宿主测试互相污染，不是 Guest。

## 26. 测试不能破坏生产设计

不应为了测试：

- 增加 Guest 内存后门。
- 绕过 IRQ 直接塞字符。
- 让 Kernel 暴露任意 physical read/write。
- 在 release path 永久保留高频 debug。

优先：

- 纯状态模块。
- fault image。
- 稳定统计。
- QMP hardware action。
- artifact inspection。

## 27. 文档也是验证

写文档时若无法明确：

- owner。
- units。
- state transition。
- failure。
- evidence。

通常表示设计本身仍含糊。版本完成时同时更新 requirement、architecture、
module、test、release，使后来者能从证据重建决策。

## 28. 推荐调试工作流

```text
1. 复现，保存完整命令和日志
2. 确认最近的可靠 marker
3. 将故障归类：build/layout/mode/memory/interrupt/device/user/fs
4. 运行最小相关 unit/audit
5. 读取对应状态，不先修改
6. 构造单一假设
7. 添加最小临时观察或 breakpoint
8. 修复根因
9. 增加能在旧代码失败的回归测试
10. 移除噪声观察，更新文档
```

## 29. 常见误解

### 29.1 “随机测试跑得多就能替代设计用例”

随机很难稳定命中语义转折；边界表和 property 都需要。

### 29.2 “QEMU 通过就不需要 unit”

QEMU 不能快速穷举 parser、overflow、bitmap corruption 等组合。

### 29.3 “Unit 通过就说明硬件正确”

模型无法证明端口、vector、栈和实际指令。

### 29.4 “测试 timeout 表示 Guest HLT”

可能是 deadlock、无限 poll、日志捕获、QMP socket 或宿主资源问题，需保留
证据分类。

### 29.5 “只要错误被拒绝，具体在哪拒绝无所谓”

若 malformed ELF 因碰巧 disk read 失败被拒绝，并未证明 ELF validator。
故障构造要让前置层正常、目标层单独失败。

## 30. 对照项目阅读

1. [测试总览](../../testing.md)
2. [调试指南](../../debugging.md)
3. [日志协议](../../logging.md)
4. [统一工具入口](../../../tools/os.py)
5. [QEMU runner](../../../tools/os_tools/qemu_runner.py)
6. `tests/unit/`
7. `tests/integration/`
8. `tests/randomized/`
9. `tests/tooling/`
10. `tests/system/`

## 31. 练习

### 练习 A：证据强度

为“用户不能写 Kernel text”设计：

- unit/model。
- artifact audit。
- QEMU fault injection。

分别写出每层能和不能证明什么。

### 练习 B：坏 ELF

构造只破坏一个 `p_filesz > p_memsz` 条件的 ELF。列出必须保持正常的：

- container CRC。
- header magic。
- table range。
- load target。

说明怎样证明拒绝来自目标检查。

### 练习 C：持久性

解释为什么：

- 同一 FileSystem object read。
- 新 FileSystem object remount memory device。
- 新 QEMU process same raw disk。

证据逐级增强。

### 练习 D：中断诊断

已知：

```text
PIT counter changes
PIC IRR bit0=1
IMR bit0=0
IF=1
handler count=0
```

列出下一步应检查的路由和 IDT，不要先修改 scheduler。

### 练习 E：回归设计

任选一个真实 bug（`.init_array`、STI/HLT 不相邻、boot/FS overlap），设计一个
旧实现必失败、修复后通过且不会依赖日志措辞的测试。

## 32. 通过标准

应能：

- 为一个命题选择 unit/integration/random/artifact/QEMU/multi-boot 层。
- 区分 exact oracle、reference model、invariant 和 marker protocol。
- 设计单一故障注入并证明在目标层被拒绝。
- 解释为什么最终 ELF/raw/disassembly 必须审计。
- 使用 CR2/error、CR3/page walk、PIC IRR/IMR/LAPIC/IF 分层诊断。
- 设计资源守恒和跨启动持久性测试。
- 说明日志、宿主时间和 Guest 状态的观察边界。

完成本册后，回到
[分阶段学习路线](../README.md)，按 v0.0→v1.0 将背景概念逐个映射到真实
实现、故障镜像和测试证据。

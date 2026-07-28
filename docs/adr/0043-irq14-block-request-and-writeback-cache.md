# ADR 0043：以单飞 IRQ14 请求和显式页状态建立写回边界

- 状态：接受
- 日期：2026-07-28
- 适用版本：v1.16

## 背景

v1.15 以前，ATA PIO 的调用者在发出命令后轮询 BSY、DRQ、ERR 和 DF。
这对 ROM、Stage 1 和中断尚未开启的 Kernel 启动阶段是合理的：执行环境没有
可阻塞 Thread，也没有能接收 IRQ14 的完整 IDT/PIC/scheduler。但是运行期仍
沿用轮询，会让一个慢设备占住 CPU；文件页缓存也只有 Empty/Clean，无法表达
共享映射已修改、正在写回或写回失败。

v1.16 必须同时回答四个所有权问题：

1. 发出 ATA 命令后，谁拥有设备、缓冲区和最终结果？
2. 设备 IRQ 与 PIT 超时同时发生时，谁能提交结果？
3. 用户通过 writable `MAP_SHARED` 修改页后，谁负责把它写回文件？
4. 写回失败时，缓存页能否被淘汰或误报为 clean？

这些问题不能通过“打开 IRQ14”或增加一个 dirty 布尔值独立解决。请求状态、
Thread 等待、PTE 权限、缓存 identity、VFS 写入和设备 flush 必须形成同一条
可审计状态机。

## 决策

### 请求模型

引入固定存储、无动态分配的 `BlockRequestQueue`。请求具有 64 位单调
identifier，并保存操作、LBA、目标缓冲区、缓冲长度、所有者 Thread、绝对
单调 deadline、状态和最终结果。

队列状态只允许：

```text
Unused
  └─Submit──> Queued
                ├─Cancel──> Completed(Cancelled)
                └─Issue───> Issued
                               ├─IRQ───> Completed(Succeeded/DeviceError)
                               └─PIT───> Completed(TimedOut)
Completed ──Reap──> Unused
```

ATA primary channel 采用 FIFO、单飞请求。协议本身没有命令 tag；在不引入
AHCI/NVMe 前允许多个 in-flight 会失去“本次 IRQ 属于哪个请求”的可靠答案。
本阶段冻结 64 槽请求容量，但容量是运行时拒绝边界，不是 identifier 宽度。

`Complete` 是唯一结果提交点。已完成请求上的迟到 IRQ、重复完成或重复超时
只增加重复解析统计并返回强类型失败，不能覆盖原结果。通用队列的消费方必须
显式 Reap，槽位才重新可用。ATA 适配器在 IRQ/超时路径中先复制冻结的完成
记录再 Reap，并把 owner Thread 与 identifier 一起交给调度器；等待 Thread
不持有可被复用的槽位引用。

### ATA 所有权与中断

ROM、Stage 1 与 Kernel early boot 保留有界轮询。运行期异步队列初始化后，
轮询适配器在持有同一设备所有权时临时设置 nIEN；两种路径不并行发命令。

运行期开放 PIC IRQ14，同时解除 master IRQ2 级联。IRQ14 handler 的工作有界：

- 读取 alternate/status，分类 BSY、DRQ、ERR、DF；
- Read 请求搬运一个 512 字节扇区；
- Write 请求在首个 DRQ IRQ 搬运数据，再等待设备完成 IRQ；
- Flush 请求等待完成 IRQ；
- 提交唯一结果、向 slave 后再向 master 发送 EOI；
- 定向唤醒请求所有者并尝试启动下一条队列请求。

IRQ handler 不分配、不睡眠、不访问用户地址，也不调用可能获取睡眠锁的 VFS。
请求缓冲区必须是 Kernel 可访问、在 Completed/Reap 前保持存活的存储。

PIT 以绝对 deadline 检查当前 Issued 请求。超时获胜时先把请求解析为
TimedOut，再执行 ATA software reset，最后才允许下一请求进入设备。这样
Thread 被唤醒时，设备也已经回到可继续工作的已知边界。
若 reset 的有界 BSY 检查失败，控制器永久标记为 unavailable；已排队请求
逐个冻结为 DeviceError，后续提交直接失败，不再向状态未知的硬件发命令。

### Thread 等待

增加 `WaitCondition::BlockIo`。同步系统调用提交异步 Flush 后保存请求
identifier，将当前 Thread 阻塞；IRQ14 或 PIT 超时只唤醒该请求的所有者。
完成路径必须同时匹配 owner Thread 槽与 identifier。若 Thread 已退出或槽位
已复用，完成被标记为 abandoned 并安全丢弃，不能唤醒新 Thread。
普通 signal 不把 BlockIo 误当作可中断字符输入。调度仍只在显式阻塞和
返回用户态前发生，IRQ handler 不直接切换任意内核调用栈。

### 文件页状态

`FilePageCacheEntryState` 固定为：

```text
Empty ──Acquire──> Clean
Clean ──write fault──> Dirty
Dirty ──Writeback begin──> Writeback
Writeback ──success──> Clean
Writeback ──failure──> Error
Error ──retry begin──> Writeback
```

Dirty 与 Error 都承担尚未稳定写入文件的责任，不能作为 clean LRU 淘汰候选。
缓存初始化时接收 dirty page hard limit；达到上限时新的 MarkDirty 明确返回
回压错误。Writeback 使用调用方提供的逐页 writer；失败页保留 frame、
identity 与错误状态，后续 `sync` 可以重试。

### writable shared 映射

只允许完整页、writable open file 的 `MAP_SHARED|PROT_WRITE`。映射第一次
驻留时使用只读 PTE；用户写触发 present+write page fault，Kernel 先按稳定
FileBacking identity 找到 cache entry 并执行 MarkDirty，成功后才把该 PTE
改为 writable。若 dirty limit 已满，fault 失败，不能出现“页已可写但缓存
不知道它脏了”的窗口。

fork 对 FileShared 页保持真正共享，不加 COW。`MAP_PRIVATE` 仍使用 COW，
其修改不进入 FilePageCache dirty 状态。

显式 `sync` 先遍历活动地址空间，把 writable FileShared PTE 重新改为只读，
再写回 Dirty/Error 页。重新写保护保证写回快照之后的新写入会再次 fault 并
重新标脏。写回通过 VFS `WriteAt` 按文件偏移提交，随后 VFS/rootfs sync 和
ATA FLUSH 建立设备稳定边界。

页写回需要仍然有效的 VFS file backing 作为 writer。unmap、exec 与 exit
在释放可能是最后一个后备引用前检查全局页缓存；若仍有 Dirty、Writeback 或
Error 页，先执行同一写保护与 VFS 写回步骤。这样脏页不会在后备描述符关闭后
变成“仍欠写回、却再也找不到 writer”的孤儿。

### 稳定 identity

文件页 key 继续由 mount/superblock identity、inode identifier 和 inode
generation 构成。rootfs 的普通事务 generation 只描述盘面事务次序，不得
写入已经挂载的 VFS superblock generation；后者在一次 mount 生命周期内
保持稳定。否则同一 inode 在一次普通写事务前后会得到两个 cache key，使
映射 alias 看不到同一物理页。

## 被拒绝的方案

### 全部继续轮询

实现简单，但 Thread 无法在 I/O 等待时让出 CPU，也不能验证中断完成、超时
竞争和定向唤醒，不满足 v2.0 的阻塞模型。

### IRQ14 handler 直接执行 VFS 写回

VFS/rootfs 可能持锁、遍历块树并执行更多 I/O；在 IRQ 上下文调用会扩大无界
临界区并形成锁反转。IRQ 只完成已提交请求，策略留给 Thread 上下文。

### 以 dirty 布尔值代替状态机

布尔值无法区分“写回进行中”和“写回失败仍负债”。失败时清 dirty 会丢数据；
失败时不清又无法说明是否允许另一写回者进入。显式状态让转移和统计可验证。

### 第一次写时直接开放所有 shared PTE

这会绕过 dirty limit 和 MarkDirty 失败语义；也使 sync 后的新修改无法再次
产生可观察边界。初始只读和写回前重新写保护是必要的 write-notify 机制。

### 在 v1.16 同时实现后台 flusher 和 `msync`

本阶段的核心是请求、状态和失败语义。显式全局 `sync` 已能闭合写回与 flush；
独立区间 `msync`、后台 daemon 和页老化策略会增加 ABI/策略而不改变基础
状态机，留待后续版本。

## 后果

正面后果：

- I/O 等待不再必然占住 CPU，IRQ 与超时有唯一结果；
- 请求和页缓存都能报告容量、峰值、成功、错误、超时、取消与重试；
- writable shared、alias 可见性、private 隔离和落盘读回形成一条真实证据；
- v1.17 journal 可以在同一 BlockRequest/Flush 边界上定义 ordered commit。

代价和限制：

- ATA PIO 仍然单飞且由 CPU 搬运数据，没有 DMA 或 tagged queue；
- 只有一个 primary master 正式设备；
- 异步 Read/Write 状态机已经存在，但 rootfs 扇区消费者仍走有界同步适配器；
  v1.16 真正接入 BlockIo 的生产路径是显式 sync 的最终 ATA FLUSH；
- 写回由显式 `sync` 驱动，没有后台 flusher 和 `msync`；
- shared writable 暂只接受完整页区间；
- 错误页占用缓存直到重试成功，持续设备故障会产生明确回压。

## 验证

- 单元测试验证非法请求、FIFO、单飞、单赢家、取消、超时、Reap 与统计；
- 集成测试连接提交、IRQ/超时完成和下一请求签发；
- 固定种子请求模型执行 100000 步，逐步核对参考队列；
- FilePageCache 单元和 100000 步随机模型验证 dirty limit、Error 保留与重试；
- rootfs 集成测试验证 mount generation 在数据事务前后稳定；
- QEMU 验证 PIC mask、IRQ14、异步 Flush、超时门限、共享 alias、落盘读回、
  private 不回写及 I/O 等待期间其他 Thread 前进；
- 全阶段构建图、产物审计、文档、教材和网站必须同步通过。

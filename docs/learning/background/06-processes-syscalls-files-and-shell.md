# B6：进程、系统调用、并发、文件与 Shell

## 1. 操作系统在建立什么

裸硬件提供：

- 指令。
- RAM。
- 特权。
- 中断。
- 扇区。
- 输入输出字节。

用户希望使用：

- 程序和进程。
- 标准输入输出。
- 文件和目录。
- 等待与并发。
- 命令。

Kernel 的核心工作是把不稳定、共享、危险的硬件能力组织成带身份、所有权、
权限、失败和生命周期的软件对象。

## 2. Kernel 与 User

### 2.1 为什么要隔离

如果所有程序都在 Ring 0：

- 任意 bug 可写页表。
- 可 CLI 永久关闭中断。
- 可读写磁盘任意扇区。
- 可覆盖其他程序。
- 无法建立可靠权限。

Ring 3 程序只能访问 user pages 和允许的控制转移；Kernel 通过 syscall 提供受控
服务。

### 2.2 隔离不是“代码放在另一个目录”

必须由硬件事实共同证明：

- CS.CPL=3。
- 用户 PTE U/S=1。
- Kernel PTE U/S=0。
- IDT syscall gate DPL=3。
- 其他敏感 gate/指令仍受限。
- TSS.RSP0 提供可信 Kernel stack。

## 3. Program 与 process

### 3.1 Program

静态程序是 ELF 文件/嵌入字节：

- code。
- initialized data。
- BSS size。
- entry。
- segment permissions。

### 3.2 Process

进程是一次运行实例及其资源：

```text
PID
address-space root
user pages
Ring 0 stack
saved CPU frame
state
exit result
descriptor table
file handles
statistics
```

同一 ELF 可创建多个 process；相同 VA 通过不同 CR3 映射到不同 frames。

### 3.3 PCB

Process Control Block 是 Kernel 对进程身份和所有权的记录。字段不应只是方便
缓存，而应回答：

- 谁拥有资源。
- 当前可否调度。
- 从哪里恢复。
- 等待什么。
- 终止后需要回收什么。

## 4. Process state

当前主要状态：

```text
Unused
Ready
Running
Blocked
Terminated
```

合法转换：

```text
create:      Unused → Ready
dispatch:    Ready → Running
preempt:     Running → Ready
wait:        Running → Blocked
wake:        Blocked → Ready
exit/fault:  Running → Terminated
```

状态转换要与 current index、wait reason、统计和 frame ownership 原子一致。

## 5. Thread 与 process 的区别

现代系统：

- Process 拥有 address space、file table、fs context 等共享资源。
- Thread 拥有执行现场、Kernel stack、user stack、TID 和调度状态。

v1.0 每个 process 只有一个执行流，PCB 同时承担 process/thread 角色。后续 v2
路线会分离；当前文档不能把“一个 PCB 一个执行现场”的实现说成线程模型已经
完成。

## 6. CPU context

抢占后恢复必须保存所有可能被用户观察的状态。

当前保存：

- 通用寄存器。
- RIP/CS/RFLAGS/RSP/SS。
- vector/error normalized fields。

当前用户程序不使用：

- x87。
- SSE/AVX。
- extended XSAVE state。

所以编译 flags 禁止自动生成相关指令。将来开放 SIMD 后必须保存/恢复，不可只
扩大一个注释。

## 7. Context switch

进程 A → B 至少切换：

```text
save A frame
A state Running→Ready/Blocked/Terminated
choose B
B state Ready→Running
CR3 = B address space
TSS.RSP0 = B kernel stack top
return using B saved frame
```

顺序错误会导致：

- 用 B 的 VA 解释 A 的 stack。
- 下次 B syscall 压到 A Kernel stack。
- 当前页表被提前释放。

## 8. Scheduler：机制与策略

### 8.1 机制

- 保存/恢复 frame。
- timer 提供 preemption point。
- 切 CR3/RSP0。
- state transition。

### 8.2 策略

- round-robin。
- quantum 4 ticks。
- 从当前后继寻找 Ready。

未来可换 priority/fair scheduler，但不能破坏机制不变量。

### 8.3 Cooperative 与 preemptive

- cooperative：进程主动 yield/wait/exit。
- preemptive：timer 可在普通用户指令间夺回 CPU。

抢占改善响应，但要求任意时刻的 frame 与 Kernel entry 都可保存，资源临界区
也要防并发。

## 9. No Ready 不等于完成

交互系统可能：

```text
no Running
no Ready
some Blocked waiting keyboard/pipe
```

系统仍存活，应 idle 等中断。

只有：

```text
no Ready
no Blocked
all created processes Terminated
```

才完成本轮用户工作负载。

## 10. System call

syscall 是受控 privilege transition：

```text
user wrapper
  → ABI registers
  → int 0x80
  → IDT/TSS hardware transition
  → assembly saves frame
  → C++ dispatcher
  → validate/copy
  → service
  → return value in RAX
  → iretq
```

它不是普通 function call，因为需要：

- 权限检查。
- 换可信栈。
- 保存 user return state。
- 处理不可信参数。

## 11. API 与 ABI

用户看到 C++ wrapper：

```cpp
ReadDescriptor(fd, buffer, capacity)
```

机器边界看到：

```text
RAX=number, RDI/RSI/RDX/R10=args
negative result=error
```

API 可以增加 convenience loop；ABI 编号、布局和错误码一旦被独立 ELF 使用，
改变就需版本策略。

## 12. 用户内存 copy

Kernel 不能直接长期使用用户指针：

```text
validate range/pages/permissions
→ CopyFromUser into fixed Kernel buffer
→ perform operation
→ CopyToUser result
```

好处：

- 设备不会访问任意用户地址。
- Kernel service 处理稳定 buffer。
- transfer size 有上限。
- 错误可映射成 ABI result。

代价是 copy；零拷贝需要 page pinning、lifetime 和 DMA 等更复杂协议。

## 13. Error 是接口的一部分

不能只返回 success/failure。调用者要区分：

- InvalidArgument。
- InvalidUserMemory。
- WouldBlock。
- EOF（通常成功且 0 bytes）。
- BrokenPipe。
- PermissionDenied。
- CapacityExhausted。
- NotFound/AlreadyExists。
- Corrupt。

稳定错误语义让用户 wrapper 知道是重试、等待、退出还是报错。

## 14. Busy wait、block 与 wake

### 14.1 Busy wait

进程反复 Try：

```text
while WouldBlock:
  retry
```

浪费 CPU，仍被 scheduler 当 Ready。

### 14.2 Block

进程：

- state=Blocked。
- waitReason=某条件。
- 不再被 scheduler 选择。

### 14.3 Wake

事件改变条件后，把匹配 waitReason 的进程变 Ready。Wake 不表示资源已保留，只
表示“现在值得重新检查”。

## 15. 条件变量思想与丢失唤醒

危险时间线：

```text
reader checks empty
writer writes and wakes nobody
reader marks itself blocked
```

reader 永久错过事件。

正确协议让：

```text
check condition
register/block
modify condition
wake
```

在同一锁或不可分割调度窗口下协调。

当前单核 `int 0x80` entry IF=0，Wait syscall 再次检查 can-progress，再 Block。
未来 SMP 必须有共享 wait queue lock；CLI 只影响本 CPU。

## 16. SpinLock

spinlock 用 atomic read-modify-write 竞争所有权：

```text
acquire:
  loop exchange/CAS
  pause while held

release:
  store unlocked
```

### 16.1 Acquire/release

- acquire：锁后读写不能被搬到获取前。
- release：临界区写不能被搬到解锁后。

它建立 happens-before，不只是把值设为 1。

### 16.2 何时不该 spin

若持锁者可能：

- 被调度出去。
- 等磁盘很久。
- 等用户输入。

长时间 spin 浪费 CPU。当前 spinlock 只保护短内核临界数据；资源等待用 Blocked。

### 16.3 Interrupt 与 lock

单核若进程上下文持锁时被 IRQ 打断，IRQ handler 再获取同一锁，会自锁。需要：

- 禁中断并保存旧 IF，或
- IRQ handler 不拿该锁，或
- 设计 interrupt-safe lock。

## 17. Pipe

pipe 是有界 FIFO：

```text
writer → bytes[capacity] → reader
```

当前容量 64 bytes，工作负载 256 bytes，必然经历 partial transfer 和
backpressure。

### 17.1 Ring buffer

状态：

```text
readIndex
writeIndex
bufferedByteCount
readerOpen
writerOpen
```

稳定不变量：

```text
0 <= buffered <= capacity
written - read = buffered
```

### 17.2 Read truth table

| buffered | writer open | 结果 |
| ---: | --- | --- |
| >0 | 任意 | 读取 |
| 0 | 是 | WouldBlock |
| 0 | 否 | EOF |

### 17.3 Write truth table

| free space | reader open | 结果 |
| ---: | --- | --- |
| >0 | 是 | 写入 |
| 0 | 是 | WouldBlock |
| 任意 | 否 | BrokenPipe |

WouldBlock 是暂时条件；EOF/BrokenPipe 是 endpoint 生命周期形成的稳定结果。

### 17.4 Close 必须 wake

- 最后 writer close：reader 要从 waiting 转为观察 EOF。
- 最后 reader close：writer 要观察 BrokenPipe。

关闭会改变 progress predicate，所以是唤醒事件。

## 18. Descriptor

fd 是进程局部小整数，不是对象本身：

```text
fd table slot
  → kind/reference
  → underlying object/handle
```

v1.0 每进程 8 slots：

- 0 stdin。
- 1 stdout。
- 2 stderr。
- 3..7 dynamic。

kind：

- console。
- regular file。
- directory。
- pipe endpoint。

## 19. Unix 中更完整的 fd 模型

成熟设计通常分三层：

```text
per-process fd table
  → open file description
  → vnode/inode/socket/pipe object
```

open file description 持有：

- current offset。
- status flags。
- reference count。

因此 `dup` 后两个 fd 共享 offset；fork 后引用计数延长 endpoint 生命周期。

v1.0 简化为固定表和平行 FileSystemHandle，没有 dup/refcount/inheritance。
学习时应理解目标模型，但不能把尚未实现的语义写进当前验收。

## 20. 统一描述符不等于所有对象同一种数据

通用 byte stream：

- ConsoleInput。
- ConsoleOutput/Error。
- RegularFile。
- PipeReader/Writer。

Directory 返回结构化 entry：

- 使用同一 fd namespace。
- 使用同一 Close。
- 但由 ReadDirectory，不由 generic byte read。

统一应收束共同语义，不应强行抹掉对象差异。

## 21. 文件系统分层

```text
path
  → directory traversal
  → inode
  → logical file blocks
  → block cache
  → block device
  → ATA sector
```

每层回答不同问题：

- path：名称与层级。
- directory：name→inode。
- inode：type/size/block ownership。
- cache：RAM 中的 block copy 与 dirty。
- device：LBA read/write/flush。

## 22. Inode

inode 保存对象元数据，不保存文件名：

- node type。
- size。
- block pointers。
- generation/link count。

文件名存在 parent directory entry：

```text
name → inode number + expected type
```

因此 rename 理论上主要改目录，而 hard link 可让多个 name 指向同 inode。
当前文件系统刻意限制为树，checker 拒绝额外 hard link/cycle。

## 23. Bitmap

inode/data bitmap 记录 allocation ownership。正确性要求双向一致：

```text
inode references data block
  ⇔ corresponding data bit allocated
```

只检查 bit 被设不够，还要确保：

- 一个 data block 不被两个 inode 引用。
- allocated inode 从 root 可达。
- 未引用 block 不保持 allocated。

## 24. Path

当前只支持 absolute path：

```text
/demo/message
```

解析要验证：

- 以 `/` 开始。
- 长度。
- component 长度。
- empty component。
- `.`/`..`。
- control/NUL。
- trailing separator。

Path 是不可信字节序列，不是“反正以 NUL 结尾”的宿主 string。

## 25. File offset 与 handle

Open 创建 handle，保存：

- inode identity。
- current offset。
- read/write permission。
- open state。

Read/Write 更新 offset。两个独立 Open 通常有独立 offset；dup 是否共享取决于
更高层 open-file-description 模型，当前尚无 dup。

## 26. Block cache

访问文件不应每个 byte 都发 ATA 命令。当前 8-entry LRU write-back cache：

- 命中返回 RAM block。
- 未命中读设备。
- dirty victim 先写回。
- Sync 写所有 dirty 并 Flush。

### 26.1 Write-through 与 write-back

- write-through：每次修改同步设备，简单但慢。
- write-back：先改 cache，延后写，需 dirty/lifetime/order。

write-back 的“操作成功”与“持久成功”必须区分。

## 27. Transaction

当前最小协议：

```text
Clean
  → write Dirty superblock + flush
  → mutate/write data and metadata
  → cache sync + flush
  → write Clean superblock + flush
```

### 27.1 它保证什么

若 crash 发生在中间，下次看到 Dirty，拒绝把部分更新当一致文件系统。

### 27.2 它不保证什么

- 不自动 rollback。
- 不 redo。
- 不恢复旧版本。
- 不是 journal。

检测未完成事务与恢复事务是不同能力。

## 28. Mount 与 format

安全规则：

- superblock 整块全零：允许首次 format。
- 非零且有效 Clean：mount + checker。
- 非零无效：Corrupt，拒绝自动 format。
- Dirty：IncompleteTransaction。

若任何 invalid magic 都自动 format，一次 bit flip 会被伪装成“新盘”，造成
不可逆数据丢失。

## 29. Consistency checker

局部 CRC 证明某个编码块未意外改变，但不证明跨对象关系：

- directory 指向 allocated inode。
- entry type 与 inode type 一致。
- 所有 allocated inode 从 root 可达。
- 无 cycle/orphan。
- data block 唯一拥有。
- bitmap 与引用精确相等。

因此 checker 本质上验证一个有限图和资源守恒。

## 30. Persistence 的证据层级

```text
Write returned
  < cache dirty
  < block writes completed
  < ATA flush completed
  < new QEMU process mounts same disk
  < independent read verifies payload
```

宿主测试必须使用：

- 同一临时可写 disk。
- `snapshot=off`。
- 两个全新 QEMU process。

否则可能只证明 RAM/cache/snapshot 保留。

## 31. Console 与 terminal

ConsoleInput 当前是 Kernel byte FIFO。Terminal/TTY 更完整语义还包括：

- canonical/raw mode。
- echo policy。
- line discipline。
- signals。
- foreground process group。
- terminal control。

v1.0 行编辑和 echo 在 Shell 用户程序，不是完整 TTY。

## 32. Shell

Shell 是用户态命令解释器：

```text
read line
→ tokenize/quote/escape
→ resolve command
→ invoke syscalls
→ print result
```

它不应在 Kernel 解析命令；否则不能证明用户 ABI 和隔离。

## 33. Parser

无分配 parser 要处理：

- whitespace。
- single/double quote。
- escape。
- empty quoted arg。
- max line/args。
- unterminated quote。
- dangling escape。
- failure atomicity。

offset+length 比内部 raw pointer 更适合自包含固定结构和随机测试。

## 34. Built-in 与 external command

v1.0 命令全是 Shell ELF 内 built-in：

- help/echo/pwd。
- ls/mkdir/write/cat/sync。
- exit。

成熟 Shell 的外部命令需要：

```text
fork/clone
→ redirection/pipe fd setup
→ exec ELF from filesystem
→ wait/job control
```

当前没有 fork/exec/wait，所以不能把 built-in 环境称作完整 Unix shell。

## 35. Redirection 为什么需要对象模型

想实现：

```text
echo hello > /x
```

临时在 echo 分支特判 OpenFile 可以输出结果，但不能形成通用组合。

可组合设计通常：

1. Open `/x` 得 fd。
2. `dup2(fd,1)` 让 stdout 引用同一 open file description。
3. close 临时 fd。
4. exec 命令。

这要求 fd reference count、inheritance、close-on-exec 和 object lifetime。

## 36. End-to-end input

一个按键：

```text
PS/2 IRQ
→ scan decoder
→ ConsoleInput FIFO
→ wake DescriptorReadable
→ Shell Wait returns
→ TryRead fd0
→ line buffer
→ parser
→ builtin
→ syscall
→ output fd1
→ COM1
```

每层拥有自己的状态；IRQ 不应知道 parser，parser 不应读 i8042。

## 37. Resource lifetime

进程结束必须清理：

- dynamic fd。
- file handles。
- pipe endpoint。
- user frames。
- page-table frames。
- Kernel stack。

清理还可能改变其他对象条件：

- writer close → reader EOF。
- reader close → writer broken pipe。
- last fd reference → object destroy。

所以 termination 不是只写 exit code。

## 38. 安全边界

当前教学系统仍要坚持：

- 用户长度有上限。
- 用户 range 全验证。
- fd 查当前 PCB。
- kind 检查权限。
- path 不信任。
- file format 解码不信任。
- capacity exhaustion 明确返回。
- 用户错误只终止进程，不 panic Kernel。

安全首先是“错误局限在正确所有者”，不是增加密码功能。

## 39. 常见误解

### 39.1 “一个 ELF 就是一个进程”

ELF 是程序输入，进程还包含动态地址空间、frame、PID、fd 和状态。

### 39.2 “Blocked 是 Running 的一种标记”

Blocked 不应被 scheduler 选择；wait reason 和 wake transition 是可调度性
契约。

### 39.3 “fd 就是 inode”

fd 是进程局部 slot，可能指 console、pipe、directory 或 file handle。

### 39.4 “EOF 等于当前没数据”

空但 writer open 是 WouldBlock；空且 writer closed 才 EOF。

### 39.5 “文件系统有 CRC 就一致”

CRC 不证明引用图、bitmap ownership 和可达性。

### 39.6 “Shell 出提示符就证明用户环境完成”

还需真实键盘、阻塞唤醒、fd、syscall、文件、退出和资源回收的端到端证据。

## 40. 对照项目阅读

1. [用户 ELF](../../../source/kernel/src/user/user_elf.cpp)
2. [系统调用](../../../source/kernel/src/user/system_calls.cpp)
3. [Thread 调度器](../../../source/kernel/src/process/thread_scheduler.cpp)
4. [进程运行时](../../../source/kernel/src/process/process_runtime.cpp)
5. [自旋锁](../../../source/kernel/src/sync/spin_lock.cpp)
6. [管道](../../../source/kernel/src/ipc/pipe.cpp)
7. [动态 FileTable](../../../source/kernel/src/io/file_table.cpp)
8. [共享 FileDescription](../../../source/kernel/src/io/file_description.cpp)
9. [类型化 KernelObject](../../../source/kernel/src/object/kernel_object.cpp)
10. [文件系统](../../../source/kernel/src/fs/file_system.cpp)
11. [块缓存](../../../source/kernel/src/fs/block_cache.cpp)
12. [Shell parser](../../../source/user/src/shell_parser.cpp)
13. [Shell](../../../source/user/src/shell.cpp)

## 41. 练习

### 练习 A：进程状态

画出 Shell 等键盘、被 IRQ 唤醒、执行 cat、退出的状态序列，并标注每次 frame
在哪里保存。

### 练习 B：丢失唤醒

写出一个错误的 Try→标 Blocked 实现时间线，再说明 Wait 二次检查为何修复单核
场景、为何 SMP 仍需 queue lock。

### 练习 C：Pipe

容量 4，依次：

```text
write ABC
read 2
write DEFG
close writer
read until end
```

列出每步 indices、buffered、返回长度和最终 EOF。

### 练习 D：fd

画 Shell 的 8-slot table，执行 OpenFile、OpenDirectory、Close、再次 Open。
说明 first-free reuse 与底层 handle cleanup。

### 练习 E：一致性

分别设计：

- allocated orphan inode。
- 两 inode 共用 data block。
- directory type 不匹配。

指出 CRC 是否一定发现、checker 哪一步发现。

### 练习 F：Shell 扩展

为 `cat /a | cat > /b` 列出尚缺：

- process API。
- fd/object API。
- pipe count/lifetime。
- parser AST。
- exec/wait。
- 系统测试。

## 42. 通过标准

应能：

- 区分 program/process/thread/PCB。
- 解释 context switch 中 frame、CR3、TSS.RSP0 和 state 的顺序。
- 解释 syscall ABI、用户 copy 和错误语义。
- 解释 spin、block、wake、lost wakeup 和 acquire/release。
- 推导 pipe 的 WouldBlock/EOF/BrokenPipe 真值表。
- 区分 fd、file handle、inode 和未来 open file description。
- 解释 inode、directory、bitmap、cache、transaction 和 persistence。
- 说明 v1.0 Shell 是 Ring 3 built-in 环境而非完整 Unix shell。

下一册进入
[测试、调试与证据](07-testing-debugging-and-evidence.md)。

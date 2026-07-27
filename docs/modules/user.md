# User 与 ABI 模块

## 模块职责

`source/abi` 保存内核与用户态共同依赖的稳定数值契约；它不能依赖 Kernel 或
User。`source/user` 保存 Ring 3 可执行程序和最小系统调用包装；它是
freestanding 目标，不拥有标准库、线程局部存储或宿主系统调用。v1.8 开始
提供建立在自研 `brk` 上的可选 `UserHeap`，但不链接宿主或 libc allocator。

```text
abi/system_call.hpp
       ↑           ↑
user 包装与程序    kernel 分发与校验
       ↓           ↓
 system_call.asm → SYSCALL（INT 0x80 兼容）→ system_calls.cpp
```

## 系统调用 ABI

| 寄存器 | 含义 |
| --- | --- |
| `RAX` | 调用编号；返回后保存有符号 64 位结果 |
| `RDI` | 参数 0 |
| `RSI` | 参数 1 |
| `RDX` | 参数 2 |
| `R10` | 参数 3；由四参数调用实际使用 |
| `SYSCALL` | 默认进入内核的指令；入口由 LSTAR 指定 |
| `INT 0x80` | 兼容与等价性测试使用的门向量 |

| 编号 | 接口 | 参数 | 结果 |
| --- | --- | --- | --- |
| 1 | `WriteLog` | `RDI=地址`，`RSI=字节数` | 已写字节数或负错误码 |
| 2 | `ExitProcess` | `RDI=有符号退出码` | 不返回用户态 |
| 3 | `GetProcessId` | 无 | 当前进程的 64 位 PID |
| 4 | `TryReadPipe` | `RDI=可写地址`，`RSI=容量` | 读取字节数、EOF 0 或负错误码 |
| 5 | `TryWritePipe` | `RDI=只读地址`，`RSI=长度` | 写入字节数或负错误码 |
| 6 | `WaitPipeReadable` | 无 | 唤醒后返回 0，调用者重新尝试 |
| 7 | `WaitPipeWritable` | 无 | 唤醒后返回 0，调用者重新尝试 |
| 8 | `ClosePipeReader` | 无 | 成功 0 或负错误码 |
| 9 | `ClosePipeWriter` | 无 | 成功 0 或负错误码 |
| 10 | `OpenFile` | `RDI=路径`，`RSI=长度`，`RDX=标志` | fd 或负错误码 |
| 11 | `ReadFile` | `RDI=fd`，`RSI=可写地址`，`RDX=容量` | 字节数、EOF 0 或负错误码 |
| 12 | `WriteFile` | `RDI=fd`，`RSI=只读地址`，`RDX=长度` | 字节数或负错误码 |
| 13 | `CloseFile` | `RDI=fd` | 成功 0 或负错误码 |
| 14 | `CreateDirectory` | `RDI=路径`，`RSI=长度` | 成功 0 或负错误码 |
| 15 | `SyncFileSystem` | 无 | 成功 0 或负错误码 |
| 16 | `TryReadDescriptor` | `RDI=fd`，`RSI=可写地址`，`RDX=容量` | 字节数、EOF 0、would block 或错误 |
| 17 | `TryWriteDescriptor` | `RDI=fd`，`RSI=只读地址`，`RDX=长度` | 字节数、would block 或错误 |
| 18 | `WaitDescriptorReadable` | `RDI=fd` | 唤醒后 0 或错误；调用者重试 |
| 19 | `WaitDescriptorWritable` | `RDI=fd` | 唤醒后 0 或错误；调用者重试 |
| 20 | `CloseDescriptor` | `RDI=fd` | 成功 0 或错误 |
| 21 | `OpenDirectory` | `RDI=路径`，`RSI=长度` | fd 或错误 |
| 22 | `ReadDirectory` | `RDI=fd`，`RSI=目录项地址`，`RDX=64` | 一项为 1，末尾为 0，失败为负值 |
| 23 | `DuplicateDescriptor` | `RDI=源 fd`，`RSI=最低新 fd`，`RDX=新 fd flags` | 新 fd 或错误 |
| 24 | `GetDescriptorFlags` | `RDI=fd` | 当前 fd flags 或错误 |
| 25 | `SetDescriptorFlags` | `RDI=fd`，`RSI=fd flags` | 成功 0 或错误 |
| 26 | `SetDescriptorSoftLimit` | `RDI=新 soft limit` | 成功 0 或错误 |
| 27 | `GetDescriptorSoftLimit` | 无 | 当前 soft limit |
| 28 | `GetDescriptorHardLimit` | 无 | 当前 hard limit |
| 29 | `ChangeDirectory` | `RDI=路径`，`RSI=长度` | 成功 0 或错误 |
| 30 | `GetWorkingDirectory` | `RDI=缓冲地址`，`RSI=容量` | 路径字节数或错误 |
| 31 | `UnlinkFile` | `RDI=路径`，`RSI=长度` | 成功 0 或错误 |
| 32 | `RemoveDirectory` | `RDI=路径`，`RSI=长度` | 成功 0 或错误 |
| 33 | `Rename` | `RDI/RSI=源路径`，`RDX/R10=目标路径` | 成功 0 或错误 |
| 34 | `TruncateFile` | `RDI=路径`，`RSI=长度`，`RDX=新长度` | 成功 0 或错误 |
| 35 | `StatFile` | `RDI/RSI=路径`，`RDX=结果地址`，`R10=结构大小` | 成功 0 或错误 |
| 36 | `SpawnProcess` | `RDI=ProcessLaunchRequest 地址`，`RSI=48` | 新 PID 或错误 |
| 37 | `ExecProcess` | `RDI=ProcessLaunchRequest 地址`，`RSI=48` | 失败返回；成功进入新映像 |
| 38 | `WaitProcess` | `RDI=PID/ANY`，`RSI=结果地址`，`RDX=40` | 已回收 PID、would block 或错误 |
| 39 | `MapAnonymousMemory` | `RDI=地址`，`RSI=长度`，`RDX=权限`，`R10=flags` | 映射起点或错误 |
| 40 | `UnmapMemory` | `RDI=页对齐地址`，`RSI=长度` | 成功 0 或错误 |
| 41 | `SetProgramBreak` | `RDI=0 查询或新 break` | 当前 break 或错误 |
| 42 | `GetVirtualMemoryStatistics` | `RDI=结果地址`，`RSI=112` | 成功 0 或错误 |

错误值为 `-1` 非法用户内存、`-2` 未知编号、`-3` 写入过长、`-4` 串口失败。
v0.10 又定义 `-5` would block、`-6` broken pipe、`-7` 端点权限、
`-8` 端点已关闭、`-9` 无 Ready 后继、`-10` 非法参数和 `-11` 管道传输
过长。所有值使用显式 `int64_t`；ABI 不使用与平台宽度相关的 `long`、
`size_t` 或枚举底层默认类型。

v0.11 再定义 `-12..-21`，分别表示非法 fd、文件不存在、文件已存在、
非目录、目标是目录、文件系统容量耗尽、文件过大、文件系统损坏、文件系统
未初始化和文件权限拒绝。Open 标志为 read、write、create、truncate 四个
具名位；未知位与没有 read/write 能力的组合均被拒绝。

v1.0 再定义 `-22` 描述符能力拒绝与 `-23` 描述符单次传输过长。通用描述符
单次最多传输 256 字节，fd 0/1/2 固定为标准输入/输出/错误，动态 fd 从 3
开始。目录项固定 280 字节，ABI 通过 `static_assert` 和独立 ELF/系统调用
测试锁定布局。

v1.4–v1.6 追加 `-24..-33`，覆盖描述符限额、KernelObject、路径、目录、
跨设备、busy 与暂不支持。v1.7 再追加 `-34..-38`，分别表示 Process 容量、
非法可执行文件、参数环境过大、没有子进程和候选映像构造失败。已有编号和
错误值永不重排。v1.8 追加 `-39..-42`，分别表示物理/地址空间容量耗尽、
地址已占用、非法内存范围和 VMA 元数据耗尽。

## 代码走读

1. `programs/smoke.cpp` 只调用公开包装，不直接依赖内核符号。
2. `src/system_call.cpp` 把类型化枚举转换为稳定编号。
3. `src/system_call.asm` 按 ABI 把 C++ 参数移到系统调用寄存器并执行
   `INT 0x80`。
4. CPU 根据 IDT gate 的 DPL 允许 CPL3 触发该向量，根据 TSS.RSP0 离开
   用户栈，再压入特权帧。
5. 内核汇编公共入口保存寄存器，C++ 分发器验证完整来源和地址。
6. 普通调用通过 `IRETQ` 回 Ring 3；`ExitProcess` 和用户异常退出当前
   Thread，返回调度器选择的下一 Thread 帧；最后一个 Thread 退出时 Process
   才进入 Zombie。

## v0.9 调度验收程序

`programs/scheduler_worker.cpp` 同一份 ELF 被创建三次，三个实例都链接到
`0x40000000`，并在同一 BSS 虚拟地址维护 `worker_counter`。每个 worker
先通过 `GetProcessId` 选择 PID2/PID3/PID4 的固定进度标记，再执行三轮
有界计算。若地址空间错误共享，后运行实例会看到其他进程写过的计数并以非零
状态退出；三个实例都输出 `ADDRESS_SPACE_ISOLATED` 才能证明独立物理叶页。

worker 不自行访问 PIT、CR3、TSS 或 PIC。它只制造足够长的纯计算区间，让真实
IRQ0 在 CPL3 打断执行；抢占证据来自内核的 run tick 与 dispatch 统计。每个
worker 恰好执行六次系统调用：PID 查询、三次进度日志、一次隔离日志和退出。
QEMU 协议检查每条固定日志的精确出现次数，避免某个进程重复输出掩盖另一个
进程缺失。

## v0.10 管道验收程序

历史 v0.10 镜像中，`programs/ipc_producer.cpp` 作为 PID1，只拥有写端。它先验证读权限被拒绝、
非法写指针被拒绝和未知系统调用仍被拒绝，再按
`(index × 37 + 11) & 0xFF` 生成 256 字节载荷。`WritePipe` 在部分写或
would block 后继续推进，写完关闭端点，并验证第二次关闭得到稳定错误。

历史 v0.10 镜像中，`programs/ipc_consumer.cpp` 作为 PID2，只拥有读端。它先验证写权限和非法
读指针，再以 31 字节缓冲循环 `ReadPipe`。每个返回字节都在用户态独立重算
期望值；零长度只接受为写端关闭后的 EOF。最终要求总量恰为 256 字节，再关闭
读端并验证重复关闭。

31 字节消费块与 64 字节内核容量故意不整除，使环形索引反复回绕并产生部分
传输。用户包装不直接忙等：Try 返回 `-5` 后调用对应 Wait，唤醒后重新检查
条件。这条控制流把用户 API、地址校验、系统调用帧、调度状态和管道条件连成
真实 Ring 3 端到端证据。

## v0.11 文件验收程序

PID1 生产者先创建 `/shared`，再以 write/create/truncate 打开
`/shared/payload.bin`，写入与管道相同的 256 字节确定性载荷，关闭并显式
同步。只有这些步骤全部成功，才输出 `FILE_WRITTEN` 并继续管道发送。

PID2 消费者先从管道读到 EOF。因为生产者在关闭管道写端前已经关闭文件并同步，
该顺序形成稳定的 happens-before 边界；消费者随后只读打开文件，逐字节验证
256 字节，再读一次确认 EOF，关闭后输出 `FILE_VERIFIED`。内核为每个 PCB
汇总文件读写字节，PID1 必须只写 256，PID2 必须只读 256，worker 必须全零。

每个 PCB 固定拥有四个普通文件句柄槽；fd 是当前进程的槽索引，不是 inode。
退出和用户异常路径会关闭全部仍开放的文件句柄。管道端点目前仍使用编号 4..9
的专用 ABI，尚未伪装为统一 VFS 描述符。

## v1.0 统一描述符与 Shell

当前正常镜像把 Shell 安排为 PID1，生产者、消费者和唯一调度 worker 依次为
PID2、PID3、PID4。生产者 fd 3 为管道写端，消费者 fd 3 为管道读端；两者
使用通用 Try/Wait/Close 包装传输原有 256 字节载荷。OpenFile 仍负责按路径
创建打开对象，但后续读写和关闭走同一描述符分发。

`programs/shell_entry.cpp` 是普通 Ring 3 入口。`src/shell.cpp` 在标准输入
fd 0 上阻塞读取并向 fd 1 回显，解析后执行 help、echo、pwd、ls、mkdir、
write、cat、sync、exit。`src/shell_parser.cpp` 只使用固定数组、offset 和
length，支持单双引号与反斜杠，不链接标准库或堆。

目录通过 OpenDirectory 获得动态 fd，再以 ReadDirectory 逐项读取 64 字节
ABI 结构；名称按显式长度输出，不假定 NUL 终止。所有动态 fd 无论正常退出
还是用户异常都会由进程运行时关闭。完整数据流和空闲唤醒见
[用户环境模块](user-environment.md)，架构理由见
[ADR 0015](../adr/0015-unified-descriptors-interactive-shell-and-idle.md)。

## v1.2 扩展现场隔离走读

用户目标继续以 `-mno-sse -mno-sse2` 编译普通 C++，避免编译器在测试代码
不知情时把 XMM 寄存器用于复制或临时值。扩展现场的安装和检查集中在
`src/extended_state.asm`，采用 NASM Intel 语法；`src/extended_state.cpp`
只提供类型化包装和稳定日志。

每个 PID 从只读表取得一组不同模式：

| 状态 | 验收内容 |
| --- | --- |
| XMM0 | 两个独立 64 位分量 |
| XMM15 | 另一组两个 64 位分量，覆盖 x86-64 FXSAVE 高 XMM 区 |
| MXCSR | 四组有效控制位组合 |
| x87 control word | 四组不同舍入控制 |
| ST0 | 精确可表示的 1.0、2.0、3.0、4.0 |

`InitializeExtendedStateIsolationTest()` 先安装再立即校验，排除表索引或汇编
本身错误。Shell 随后跨越键盘 Blocked/Ready，producer/consumer 跨越管道
满空等待，worker 跨越真实 PIT 抢占；各程序在关键操作之间调用
`ValidateExtendedStateIsolationTest()`，退出前调用
`CompleteExtendedStateIsolationTest()`。

完成函数先校验，输出一次 `[OS][USER] EXTENDED_STATE_ISOLATED`，再校验一次。
第二次校验能发现日志系统调用自身错误破坏扩展现场。QEMU 必须精确观察四行，
并同时看到 Kernel 的 FXSAVE/FXRSTOR 非零累计量；少一行、重复一行或任一
程序非零退出都失败。

该测试不声称用户态已经开放任意浮点 ABI。它只证明 x87/SSE2 是 Thread 私有
 现场，并冻结当前 FXSAVE 边界；AVX/XSAVE 仍由 CR4 配置明确禁用。

## v1.3 双系统调用入口与返回走读

普通 C++ 包装继续使用项目 ABI：RAX 是系统调用号，RDI、RSI、RDX、R10 是
前四个参数，RAX 返回结果。变化只发生在最小 NASM 边界：

- `OsUserInvokeSystemCall` 默认执行 `SYSCALL`；
- `OsUserInvokeLegacySystemCall` 只为兼容与等价性测试执行 `INT 0x80`；
- `OsUserInvokeSystemCallWithDirectionFlag` 在原生入口前设置 DF，用来验证合法
  非快速现场由 IRET 恢复，返回后立即 `CLD` 维护 System V C++ ABI。

worker 对同一个无副作用 PID 查询分别走两条入口，要求返回值相同；随后用默认
入口验证一条普通 SYSRET 和一条 DF→IRET 回退，并输出三个一次性标记。其他
Shell、管道、文件和退出包装全部走原生入口，因此 QEMU 中数百次
`NATIVE_SYSCALL_ENTRIES` 来自真实用户工作负载，不是内核伪造计数。

用户模块不配置 STAR/LSTAR，不读取 CpuLocal，也不选择返回指令。所有用户
RIP/RSP、段、RFLAGS 和映射验证都属于 Kernel 安全边界；用户包装器只遵守
ABI。详细入口流程见
[ADR 0030](../adr/0030-cpu-local-native-system-call.md)。

## v1.4 动态描述符用户态走读

用户 ABI 继续把 fd 表实现封装在 Kernel 内。`system_call.hpp/.cpp` 只新增
六个固定宽度包装：

```text
DuplicateDescriptor(source_fd, minimum_fd, fd_flags)
GetDescriptorFlags(fd)
SetDescriptorFlags(fd, fd_flags)
SetDescriptorSoftLimit(limit)
GetDescriptorSoftLimit()
GetDescriptorHardLimit()
```

`DuplicateDescriptor` 更接近 Unix `F_DUPFD_CLOEXEC` 的最小教学接口：它从
minimum 起选择最低可用 fd，并允许只为新 fd 设置 close-on-exec。用户态看
不到 `KernelObjectHandle`、generation 或 FileDescription payload。

PID4 的 `ValidateFileDescriptionModel()` 是当前端到端证明。它只在第四个
scheduler worker 中运行，其他 worker 继续承担相同地址空间和扩展现场验证：

```text
write /fdv14.bin = ABCDEFGH
open -> fd 3
duplicate(fd 3, minimum selected, CLOEXEC) -> fd >= minimum selected
independent open -> fd 4

read(fd 3, 3)       -> ABC
read(duplicate, 3)  -> DEF
read(fd 4, 3)       -> ABC
```

前两次读取连续推进同一个共享 offset，独立 open 则从零开始。随后程序检查
源 fd flags 为零、副本为 close-on-exec，再只修改源 fd flags，证明 flags
属于 fd 而不属于共享 FileDescription。

`minimum selected` 在 256 MiB/64 GiB 档为 64，在 hard limit 仅为 64 的
兼容档为 8。把 soft limit 降为同一个 minimum 后，再从该 minimum duplicate
必须得到
`OS_ABI_SYSTEM_CALL_RESULT_DESCRIPTOR_LIMIT_EXCEEDED`；已有高编号副本仍能
关闭。恢复 hard limit、关闭 fd 4 后，再次 open 必须得到 fd 4，证明最低
可用编号复用。只有读取内容、限额错误、flags 和全部 close 都正确，才输出：

```text
[OS][USER][PID4] FILE_DESCRIPTION_MODEL_OK
```

用户程序不把某个 fd 硬编码为复制结果，只要求结果不小于查询规格后选定的
minimum；表的精确最低编号性质由宿主模型逐槽验证。详细所有权见
[ADR 0031](../adr/0031-typed-kernel-object-dynamic-file-table.md)。

## v1.5 cwd、路径与双后端用户态走读

用户态新增两个固定宽度包装：

```text
ChangeDirectory(path, path_length_bytes)
GetWorkingDirectory(destination, capacity_bytes)
```

它们分别使用系统调用号 29 和 30，并继续经过默认 `SYSCALL` 汇编入口。
用户代码只传路径字节，不看 Vnode、Mount、Superblock 或 legacy inode。

Shell 新增 `cd <path>` 内建命令。`cd` 必须由 Shell 自身执行，因为 cwd
属于当前 Process 的 FsContext；若由将来的外部子进程执行，只会改变子进程。
`pwd` 调用真实 getcwd，提示符每轮显示 `[os:<cwd>]$`，无参数 `ls` 默认使用
相对路径 `.`。

functional QEMU 先执行：

```text
cd /tmp
mkdir session
write session/message temporary
cat ./session/../session/message
ls .
```

这组命令进入 memfs 并同时证明 cwd、相对路径、`.`、`..` 和挂载遍历。随后
`cd ..` 回到 legacy 根目录，再执行 `/demo` 的创建、读写、枚举与 sync，
证明同一用户 ABI 没有后端特例。

公共路径容量为 4096 字节，目录项名称容量为 255 字节。ABI
`DirectoryEntry` 固定为 280 字节，并显式保存一个清零 reserved byte；
用户态不能依赖编译器隐式 padding。新增错误 `-26..-29` 分别表示路径过长、
名称过长、路径循环和只读文件系统。

详细路径语义见
[ADR 0032](../adr/0032-vfs-mount-namespace-and-memfs.md) 与
[v1.5 发布记录](../releases/v1.5.md)。

## v1.7 PID1、spawn/exec/wait 与参数栈

正常镜像不再把 Shell、IPC 和普通功能程序链接进 Kernel。构建系统把
`/sbin/init`、`/bin/sh`、`/bin/smoke` 和各验收程序作为彼此独立的
freestanding ELF64 文件安装到 rootfs；Kernel 中只保留必须在不同启动模式
直接选取的 smoke、用户 `#UD` 和用户 `#PF` 故障夹具。

`ProcessLaunchRequest` 是 48 字节固定宽度结构，保存路径、`ProcessString`
向量和各自计数。每个 `ProcessString` 只携带用户地址与精确字节长度，不依赖
宿主 `size_t`，Kernel 会先复制并验证整份描述，再通过 VFS 读取 ELF。参数和
环境分别最多 256 项，所有字符串连同 NUL 的总量最多 128 KiB。

新用户栈固定为 64 个 4 KiB 页。字符串从栈顶以下连续放置，随后放置
`envp[]`、终止空指针、`argv[]`、终止空指针和 `argc`；最终 RSP 向下对齐到
16 字节。用户入口仍是普通 C ABI 包装，但现在接收：

```text
RDI = argc
RSI = argv
RDX = envp
RSP = 16 字节对齐后的用户栈指针
```

`spawn` 解析路径、参数和 ELF，构造全新的 AddressSpace、用户栈、
FileTable/FsContext、Process、唯一 Thread 和进程树边，然后一次性发布；
任一步失败都按逆序释放候选资源，父进程仍可继续。它不是 `fork`：不会复制
调用者内存，也不共享调用者打开描述符。

`exec` 保留当前 PID、父子关系、FsContext 与非 close-on-exec 描述符，只替换
当前 Process 的程序 AddressSpace、入口和唯一 Thread 的用户现场。实现先在
旁路候选映像中完成文件读取、ELF 验证、段映射、参数栈和新现场；只有
`CommitProcessImage` 成功后才关闭 close-on-exec 描述符并回收旧 CR3。
因此截断 ELF、W+X、读失败或 `E2BIG` 都会返回旧映像，不能留下“半个 exec”。

`wait` 支持指定直接子 PID 和 `UINT64_MAX` 表示任意直接子进程。Alive 子进程
使调用 Thread 进入具名 WaitQueue；Zombie 子进程返回 40 字节
`ProcessWaitResult` 并完成调度器安全回收；没有匹配子进程返回 `-37`。
父进程先退出时，所有仍存活或 Zombie 的孩子都改挂到 PID 1。PID 1 必须把
七个直接/收养孩子全部回收后才允许自身退出。

用户态验收由 `/sbin/init` 组织：它验证自身 PID/argv/envp，创建六个直接
子进程；orphan parent 再创建一个孩子，形成总计八个 Process 的完整树。
argument probe 验证恰好 128 KiB 边界；exec probe 先验证截断 ELF 和 E2BIG
回滚，再提交 `/bin/exec_target`；fs probe 和 Shell 继续覆盖磁盘与交互链。
详细状态机见 [进程模块](process.md)、[ADR 0034](../adr/0034-pid1-process-tree-disk-exec-wait.md)
和 [v1.7 发布记录](../releases/v1.7.md)。

## v1.8 匿名内存与 UserHeap 走读

共享头 `abi/virtual_memory.hpp` 冻结页大小、R/W/X、FIXED、匿名窗口、
8 MiB stack/heap 上限和 112 字节统计结构。所有地址、长度和计数均为
`uint64_t`，返回值为 `int64_t`；ABI 不使用宿主 `long` 或 `size_t`。

用户包装只负责寄存器 ABI，不在 Ring 3 复制 Kernel VMA 逻辑：

```text
MapAnonymousMemory(address, length, protection, flags)
UnmapMemory(address, length)
SetProgramBreak(address_or_zero)
GetVirtualMemoryStatistics(statistics)
```

`programs/memory_probe.cpp` 是成功路径的端到端消费者。它先建立 32 MiB
匿名 VMA 并证明 resident 不变，再触及两个远隔页验证 demand zero 与写入
保持；中段 unmap/remap 证明 split 和 fixed 不覆盖，全部撤销后检查 resident
与页表回收。随后验证 2 MiB break、递归栈增长和 5000 步真实用户 heap。

两个独立 ELF 负责失败语义：

- `memory_guard_probe.cpp` 写永久栈 guard，必须以用户 vector 14 结束；
- `memory_protection_probe.cpp` 读取只读匿名零页后尝试写入，也必须以
  vector 14 结束。

把失败路径放入独立 Process 很重要：若在 memory probe 内触发预期异常，
后续回收与统计代码永远无法执行，PID1 也无法区分“预期保护成立”和“测试程序
意外崩溃”。PID1 使用 wait result 同时核对 termination reason 与 vector。

`UserHeap` 位于 `include/os/user/user_heap.hpp` 与 `src/user_heap.cpp`。
调用者注入 program-break 函数、页大小、增长 quantum 和最大容量。分配器
使用 64 字节 header、16 字节对齐、first-fit、split 和前后 coalesce；
物理块关系和空闲链均保存显式 offset。`Validate()` 会交叉核对两张图，重复
释放、外部/内部指针、容量耗尽和 break 失败都有独立状态。

v1.8 不把 `UserHeap` 命名为完整 `malloc/free` ABI：它没有线程安全、arena、
size class、尾部自动缩 break 或 libc 兼容承诺。详细背景与源码顺序见
[v1.8 学习章](../learning/16-v1.8-anonymous-vma-demand-paging.md)，设计边界见
[ADR 0035](../adr/0035-anonymous-vma-demand-paging-user-heap.md)。

## 依赖与命名

- 公开头位于 `source/user/include/os/user/` 和
  `source/abi/include/os/abi/`，实现位于各自 `src/`。
- 普通 C++ 函数使用大驼峰，例如 `InvokeSystemCall()`、`WriteLog()` 和
  `ExitProcess()`。
- `OsUserEntry` 与 `OsUserInvokeSystemCall` 是 C/汇编 ABI 符号，因链接契约
  保留既定前缀，不作为普通函数命名的例外扩散到业务代码。
- 所有协议数字和字符串均使用“项目 + 模块 + 功能”的全大写命名常量。

# 用户环境、控制台与统一描述符

## 模块职责

v1.0 用户环境由四条边界组成：

```text
QEMU PC 键盘前端
  → i8042 数据端口 → IRQ1 → Set 1 解码
  → ConsoleInput 固定 FIFO
  → fd 0 / TryReadDescriptor / WaitDescriptorReadable
  → Ring 3 Shell 解析器与命令
  → fd 1、普通文件、目录句柄和 Sync
```

QEMU 只产生硬件输入。`source/kernel` 负责扫描码、输入排队、对象分发和调度；
`source/abi` 负责稳定编号与固定布局；`source/user` 负责阻塞包装、命令解析
和用户可见行为。

## 描述符布局

| fd | 初始对象 | 能力 |
| ---: | --- | --- |
| 0 | ConsoleInput | 读；空时可等待 |
| 1 | ConsoleOutput | 写到 COM1 |
| 2 | ConsoleError | 写到 COM1 |
| 3..7 | Closed | 文件、目录或启动期管道端点 |

生产者的 fd 3 是 PipeWriter，消费者的 fd 3 是 PipeReader。OpenFile 与
OpenDirectory 从首个 Closed 动态槽开始分配。普通文件、目录和管道只能关闭
动态槽；关闭 0..2 返回描述符权限错误。进程退出会遍历全部动态槽，先释放底层
对象并唤醒可能受关闭影响的等待者，再把槽标为 Closed。

## 通用 I/O 的 Try/Wait 契约

用户包装遵循同一循环：

```text
TryReadDescriptor
  ├─ >0：返回字节
  ├─ 0：EOF
  ├─ WouldBlock：WaitDescriptorReadable → 重新 Try
  └─ 其他负值：返回错误
```

写包装按 256 字节 ABI 上限切块，并在部分写后推进用户指针。等待只表示条件
可能改变，不预留字节，也不隐式重放前一个系统调用。用户指针在访问底层对象
前逐页验证，实际数据先经过有界内核栈缓冲；文件系统锁和管道锁内不访问
Ring 3 地址。

对象分发保持明确权限：

| 对象 | 通用读 | 通用写 | 等待读 | 等待写 |
| --- | --- | --- | --- | --- |
| 控制台输入 | 是 | 否 | FIFO 非空 | 否 |
| 控制台输出/错误 | 否 | 是 | 否 | 立即可推进 |
| 普通文件 | 按打开权限 | 按打开权限 | 立即可推进 | 立即可推进 |
| 目录 | 否；使用 ReadDirectory | 否 | 不需要 | 否 |
| 管道读端 | 是 | 否 | 数据或 EOF | 否 |
| 管道写端 | 否 | 是 | 否 | 空间或 broken pipe |

## 第四个系统调用参数

C++ System V AMD64 调用 `OsUserInvokeSystemCall(number, arg0..arg3)` 时，第
五个函数参数位于 R8。NASM 桩把 number 从 RDI 移到 RAX，把 arg0..arg2
依次移到 RDI、RSI、RDX，并把 arg3 从 R8 移到 R10。当前目录读取包装把
280 字节结构大小作为第三个系统调用参数传给内核；rename/stat 已实际使用
R10 传递第四个参数。

## 目录 ABI

`DirectoryEntry` 精确为 280 字节：

| 偏移 | 长度 | 字段 |
| ---: | ---: | --- |
| 0 | 8 | inode_number |
| 8 | 8 | DirectoryEntryType |
| 16 | 8 | name_length_bytes |
| 24 | 255 | name |
| 279 | 1 | reserved，必须为零 |

OpenDirectory 只接受现存目录。ReadDirectory 每次按句柄偏移读取一个磁盘目录
项并转换为 ABI 类型；结果 1 表示有效项，0 表示末尾。名称不是 C 字符串，
Shell 必须使用显式长度输出。CloseDescriptor 同时关闭底层
VFS OpenFile。

## 控制台输入与空闲唤醒

Set 1 解码器只在 make code 上产生字符。左右 Shift 分别跟踪，Caps Lock
在按下时翻转；字母使用 `shift XOR caps`，数字和标点只使用 Shift。字符进入
256 字节环形 FIFO，满时增加 dropped 而不覆盖未读数据。

Shell 对空 fd 0 执行 WaitDescriptorReadable。若没有其他 Ready 进程，
调度器保存 Shell 帧并清空当前进程，运行时回到永久内核页表执行
同一个汇编块中的 `sti; hlt; cli`。键盘 IRQ 提交字符，并按等待原因唤醒
所有 `DescriptorReadable` 等待者；返回后运行时重新扫描 Ready，用户包装
重新检查具体 fd 并恢复 Shell。IRQ0 也可唤醒 HLT，但不改变键盘条件；循环
会继续空闲。

## Shell 代码走读

1. `programs/shell_entry.cpp` 只调用 `RunShell()`，再用 `ExitProcess()` 结束。
2. `src/shell.cpp` 输出 banner 与 READY，随后在 fd 0 上逐字符阻塞读取并回显。
3. 行缓冲只接受可打印 ASCII、Tab、Backspace 和 Enter；超出 128 字节后
   丢弃到本行 Enter，并报告稳定错误。
4. `src/shell_parser.cpp` 把输入复制到固定存储，参数只保存 offset 与 length，
   不保存悬空指针，也不分配内存。
5. `ResolveShellCommand()` 返回强类型命令；未知文本只产生一次拒绝标记并回到
   prompt。
6. help/echo/pwd 只访问标准输出；cd/ls/mkdir/write/cat/rm/rmdir/mv/truncate/
   stat/sync 通过公开 VFS ABI；exit 返回零，由普通进程退出路径统一关闭资源。

## 命令与当前范围

| 命令 | 语义 |
| --- | --- |
| `help` | 显示十五条命令及参数 |
| `echo [text...]` | 以空格连接参数并换行 |
| `pwd` | 通过 getcwd 输出当前 Process 的 cwd |
| `cd <path>` | 修改当前 Shell Process 的 cwd |
| `ls [path]` | 用目录句柄列出名称，目录追加 `/` |
| `mkdir <path>` | 创建目录，已存在时明确提示 |
| `write <path> <text...>` | 创建或截断文件并写入最多 256 字节 |
| `cat <path>` | 循环读取文件到 EOF |
| `rm <path>` | 删除未打开的普通文件 |
| `rmdir <path>` | 删除未打开的空目录 |
| `mv <src> <dst>` | 同一 Superblock 内移动或替换路径 |
| `truncate <path> <n>` | 设置文件逻辑长度 |
| `stat <path>` | 显示 inode、generation、长度与分配空间 |
| `sync` | 刷新文件系统事务与 ATA 缓存 |
| `exit` | 正常退出 Shell |

v1.10 已有磁盘 `spawn/exec/fork/wait`、匿名/文件映射、按需 ELF、
program break、
按需栈与
Ring 3 用户堆，但 Shell 的十五条命令仍全部是内建命令。这是刻意保留的阶段
边界：v1.10 已验证 fork/COW 和资源继承；命令查找、外部程序、管道连接与前后台
作业留到后续 Unix I/O/终端阶段。命令始终位于用户态；“内建”不等于由
Kernel 解释文本。

## 失败语义

- 未映射用户指针：`INVALID_USER_MEMORY`；
- fd 越界、关闭或对象类型不匹配：`INVALID_FILE_DESCRIPTOR`；
- 对 stdin 写、对 stdout 读、关闭标准 fd：`DESCRIPTOR_PERMISSION_DENIED`；
- 单次描述符传输超过 256 字节：`DESCRIPTOR_TRANSFER_TOO_LARGE`；
- 控制台空或管道暂不可推进：`WOULD_BLOCK`；
- 目录结构大小不是 280 字节：`INVALID_ARGUMENT`；
- 文件系统错误继续映射为 v0.11 的稳定错误值。

进程接口另外保证：

- `ProcessLaunchRequest` 或 `ProcessWaitResult` 大小不匹配：
  `INVALID_ARGUMENT`；
- 可执行文件不存在、截断或违反 ELF/W^X：对应 file error 或
  `INVALID_EXECUTABLE`；
- argv/envp 超过 256 项或 128 KiB：`ARGUMENT_LIST_TOO_LARGE`；
- wait 没有匹配的直接子进程：`NO_CHILD_PROCESS`；
- wait 的匹配子进程仍 Alive：内部阻塞并在唤醒后由用户包装重试。

## 可观测性

Shell 只为 READY、成功识别的命令、未知命令拒绝和 EXIT 输出稳定用户标记；
每次 Try、每个扫描码和每个输入字符不另写日志。全部进程结束后，内核汇总：

- 控制台 submitted、read、dropped、buffered；
- 每进程 console read/write；
- 描述符路径下的 pipe 与 file read/write；
- block、wakeup、dispatch、preemption 和系统调用次数。

正常 QEMU 验收要求输入提交数等于读取数、丢弃与残留都为零；Shell 退出后由
PID1 执行 wait 并确认没有 Zombie。宿主捕获器仍为每一串口行添加
`[QEMU][T+......ms]`，但这只是宿主单调到达时间。

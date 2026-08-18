# 用户环境、控制台、动态管道与外部 Shell

## 模块职责

v1.15 用户环境在 v1.11 Unix I/O 组合层上加入 TTY 与作业控制：

```text
QEMU PC 键盘前端
  → i8042 数据端口 → IRQ1 → Set 1 解码
  → Terminal canonical 行规程与前台 PGID
  → fd 0 / TryReadDescriptor / WaitDescriptorReadable
  → Ring 3 ShellExecutionPlan
  → fork / pipe / dup2 / redirection / exec / wait
  → fd 1、动态 Pipe、普通文件、目录句柄和 Sync
```

QEMU 只产生硬件输入。`source/kernel` 负责扫描码、输入排队、对象分发和调度；
`source/abi` 负责稳定编号与固定布局；`source/user` 负责阻塞包装、命令解析
和用户可见行为。

## 描述符布局

| fd | 初始对象 | 能力 |
| ---: | --- | --- |
| 0 | TerminalDevice stdin | 仅前台组可读；空时可等待 |
| 1 | TerminalDevice stdout | 经 TTY 输出环写到 VGA，并追加宿主可导出的终端转录 |
| 2 | TerminalDevice stderr | 经 TTY 输出环写到 VGA，并追加宿主可导出的终端转录 |
| 3..hard limit-1 | Closed | 文件、目录或动态管道端点 |

FileTable 使用按需 chunk；bootstrap、functional 与 capacity 的 hard limit
分别为 64、512、4096。OpenFile、OpenDirectory 与 CreatePipe 从首个 Closed
动态槽开始分配。普通文件、目录和管道只能关闭动态槽；关闭 0..2 返回描述符
权限错误。进程退出会遍历全部动态槽，释放 FileDescription 强引用；最后引用
触发文件或管道 finalizer。

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
1024 字节环形 FIFO，满时增加 dropped 而不覆盖未读数据。

Shell 对空 fd 0 执行 WaitDescriptorReadable。若没有其他 Ready 进程，
调度器保存 Shell 帧并清空当前进程，运行时回到永久内核页表执行
同一个汇编块中的 `sti; hlt; cli`。键盘 IRQ 提交字符，并按等待原因唤醒
所有 `DescriptorReadable` 等待者；返回后运行时重新扫描 Ready，用户包装
重新检查具体 fd 并恢复 Shell。IRQ0 也可唤醒 HLT，但不改变键盘条件；循环
会继续空闲。

## Shell 代码走读

1. `programs/shell_entry.cpp` 只调用 `RunShell()`，再用 `ExitProcess()` 结束。
2. `src/shell.cpp` 输出 banner 与 READY，把 TTY 切到 ShellEditor，随后在 fd 0
   上逐字符阻塞读取并自行回显；执行外部作业时临时切回 Canonical。
3. `ShellLineEditor` 接受可打印 ASCII、Backspace、方向键和 Tab；维护 512 字节
   行、cursor、16 条历史与命令补全，容量满后拒绝继续插入。
4. `src/shell_execution.cpp` 先把整行切成最多 8 条 `;`/`&&`/`||` 控制命令，
   再把每条命令复制到固定存储；参数只保存 16 位 offset/length，解析引号、
   转义、`|`、`<`、`>`、`>>`、`2>`、`2>>`，产生最多 16 个 stage。
5. `src/shell.cpp` 在任何命令执行前预检整行，再按实际退出码短路；单条管线只在
   解析完全成功后创建 N-1 根 Pipe，再 fork N 个 child；
   child 用 dup2 接线并 exec，parent 关闭端点并 wait 全部 child。
6. `cd`、`exit`、export/unset 与作业命令改变 Shell 自身状态，因此留作
   builtin；其他命令从 `/bin` 执行 `core_tool` multi-call ELF。

## 命令与当前范围

| 命令 | 语义 |
| --- | --- |
| `help` | 显示外部工具及参数 |
| `echo [text...]` | 以空格连接参数并换行 |
| `err [text...]` | 把参数写到 stderr，用于错误重定向与管线组合验证 |
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
| `wc` | 从 stdin 统计字节、单词和行 |
| `head` | 从 stdin 输出首行 |
| `tee [path]` | 同时复制 stdin 到 stdout 和文件 |
| `touch <path>` | 创建文件或保持现有文件 |
| `true` / `false` | 返回成功 / 失败状态 |
| `basename <path>` | 去除末尾斜杠并输出最后一个路径组件 |
| `dirname <path>` | 输出最后一个路径组件之前的目录部分 |
| `cp <src> <dst>` | 通过通用描述符循环复制普通文件 |
| `seq [first] <last>` | 输出有界十进制序列，最多 100000 项 |
| `uptime` | 读取 `/proc/uptime` 的来宾单调纳秒 |
| `ps` | 读取活动 Process/Thread 与容量快照 |
| `free` | 读取 managed/free/allocated 物理内存字节 |
| `uname` | 读取 ABI 版本与 `x86_64` 架构 |
| `mounts` | 读取 VFS mount 数 |
| `resources` | 读取 heap、fd、pipe、vnode 与 journal 快照 |
| `sleep <ms>` | 经已有 deadline syscall 非忙等等待 |
| `kill <pid> <signal>` | 经已有 signal syscall 投递进程信号 |
| `id` | 输出 real/effective UID/GID 与补充组 |
| `env` | 输出当前导出环境；Shell 支持赋值、export/unset 与变量展开 |
| `grep <pattern> [file]` | 有界逐行查找，匹配行写 stdout，无匹配返回失败 |
| `find [path]` | 用最多 128 个 512 字节路径槽迭代遍历 |
| `sort [file]` | 排序最多 64 行，每行最多 256 字节 |
| `tail [file]` | 用 10 行环保存最后十行，每行最多 256 字节 |
| `df` | 输出 rootfs v4 的 128 GiB 区域及可达文件分配字节估算 |
| `du [path]` | 迭代累计可达 vnode 的 allocated size |
| `hexdump [file]` | 按 16 字节行输出 8 位 offset 与十六进制字节 |
| `clear` | 输出 VGA 支持的 CSI 清屏与归位序列 |
| `date` | 经 CMOS RTC 输出 UTC 日期时间 |
| `chmod <mode> <path>` | 按 Linux 四位八进制 mode 修改权限 |
| `chown <uid[:gid]> <path>` | 按数值 UID/GID 修改 owner |
| `ln [-s] <target> <path>` | 创建硬链接或符号链接 |
| `readlink <path>` | 输出符号链接的原始目标 |
| `umask [mode]` | 查询或修改 Shell 自身创建掩码 |
| `exit` | 正常退出 Shell |

v1.11 已把普通命令移出 Shell，并支持输入/输出重定向与 16 级流水线；v1.18
已把 rootfs 独立工具路径从 19 个补齐到 32 个，并通过 `/bin/tool_probe`
验证 32 个不同 inode、regular-file 类型和 ELF magic。256 MiB functional
QEMU 还会实际运行新增 13 个工具，检查唯一输出、cp 回读、procfs 文本、
deadline sleep、信号投递和 PID 查询。v1.15
已经提供 session、前后台 PGID、控制终端、`jobs/fg/bg`、尾部 `&`，以及
TTY 生成的 Ctrl-C/Ctrl-Z 组信号。v2.2 进一步提供完整控制/重定向、32 项环境、
引用感知 glob、左右行编辑、16 条历史、Tab 补全和 43 个工具路径。v2.4 又把
独立工具路径增至 47，并加入本地身份、权限和 Shell builtin umask。完整 termios、
任意 raw mode、路径补全、多个终端、时区数据库和完整 POSIX job spec 仍未实现。
所有命令解释始终位于用户态。

Shell 的 16 项作业表按成员事件归约 Running/Stopped/Done。外部管线全部 stage
使用同一 PGID；前台事务交出并最终收回 TTY，后台事务不改变前台组。child 在
exec 前恢复 SIGINT/SIGTSTP 默认动作，避免继承 Shell 的保护 handler。

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
- pipe manager 容量耗尽：`PIPE_LIMIT_EXCEEDED`；
- 流水线空 stage、空控制命令、悬空 `&&`/`||`、重复重定向、未闭合引号或
  悬空转义：Shell 解析失败且
  不创建任何资源；
- child 接线失败：以 126 退出；exec/命令查找失败：以 127 退出。

## 可观测性

Shell 只为 READY、成功解析的命令、未知命令拒绝、重定向、16 级流水线和
EXIT 输出稳定用户标记；
每次 Try、每个扫描码和每个输入字符不另写日志。全部进程结束后，内核汇总：

- 控制台 submitted、read、dropped、buffered；
- 每进程 console read/write；
- 描述符路径下的 pipe 与 file read/write；
- 动态 Pipe capacity/active/peak/create/release/rejection；
- block、wakeup、dispatch、preemption 和系统调用次数。

正常 QEMU 验收要求输入提交数等于读取数、丢弃与残留都为零；Shell 退出后由
PID1 执行 wait 并确认没有 Zombie。宿主捕获器仍为每一内存日志行添加
`[QEMU][T+......ms]`，但这只是宿主首次观察到该行的单调时间。

详细代码走读见
[v1.11 学习章](../learning/19-v1.11-unix-io-external-shell.md)，事务边界见
[ADR 0038](../adr/0038-dynamic-pipe-dup2-external-shell.md)。

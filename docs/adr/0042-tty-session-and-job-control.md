# ADR 0042：TTY、会话与前后台作业控制

- 状态：已接受
- 日期：2026-07-28
- 目标版本：v1.15

## 背景

v1.14 已经提供进程组、组信号、可中断等待和安全用户返回，但控制台仍只是
“字符进入 FIFO、用户读取字符”的字节设备。它不知道谁拥有输入，也不知道
`Ctrl-C` 和 `Ctrl-Z` 是控制动作。若由 Shell 自己解释控制键，会产生三个错误：

- 前台程序读取 stdin 时，Shell 根本看不到该字节；
- Shell 无法把一条多进程管线当作同一个信号目标；
- 后台程序可以和 Shell 竞争输入，输入归属取决于调度时序。

传统 Unix 用 session、process group 和 controlling terminal 分离这几个职责。
session 是终端登录边界，process group 是一个作业的平坦成员集合，TTY 只记录
一个 foreground process group。Shell 负责创建作业和切换前台组；TTY 负责
把控制字符翻译为发往当前前台组的信号。

## 决策

### TTY 是有锁的行规程，不是键盘 IRQ 的附属缓冲

新增 `Terminal`，拥有三段固定容量状态：

- 1024 字节已提交输入环；
- 512 字节 canonical 编辑缓冲；
- 2048 字节同步输出环。

PS/2 IRQ 只完成 Set 1 扫描码、左右 Ctrl 修饰状态与字符翻译，再调用
`SubmitCharacter`。普通字符进入编辑缓冲；换行把整行原子提交；退格删除最后
一个编辑字符；`Ctrl-D` 提交当前编辑内容或产生一次 EOF；`Ctrl-C` 和
`Ctrl-Z` 清空未提交编辑内容并返回具名控制动作。

TTY 状态会被 IRQ 提交路径和系统调用读写路径共同访问，因此使用
`IrqSaveSpinLock`。主机模型注入无操作的中断保存/恢复函数，目标 Kernel 注入
真实 `DisableInterrupts`/`RestoreInterrupts`。输出也经过同一锁保护，防止
用户系统调用写串口时被键盘 IRQ 日志重入并破坏环形索引。

输入账本满足精确守恒：

```text
submitted
  = read + buffered + editing + dropped + consumed
```

`consumed` 表示被行规程有意消费而不会交给用户的控制字节，以及被退格或控制键
撤销的编辑字节。这样 `Ctrl-C`/`Ctrl-Z` 不再使“提交数等于读取数”的旧假设
误报失败。

### 会话、进程组与进程树分离

新增 `JobControlManager`，每个活动进程保存 PID、PGID、SID、是否 session
leader。PID1 初始满足 `SID=PGID=PID=1`；fork 继承 SID/PGID；普通进程可建立
以自身 PID 为 PGID 的组，或加入同 session 已存在的组；session leader
不能被移动到其他组。

进程树继续只表达 parent/wait/reparent。进程组不是子树，session 也不是
调度队列。三种身份使用相同进程槽索引关联，但公开 ABI 始终使用单调 PID。

进程级信号/进程组身份保留到 Zombie 被父进程收集，而不是在退出瞬间删除。
这是必要的生命周期约束：极短命令可能在父 Shell 执行 `setpgid` 前退出；
保留 Zombie 的 PGID 锚点可避免管线组长过早消失。Thread 信号状态仍在退出时
释放，最终 wait 同时回收 scheduler、signal、job-control 和 process-tree
四层状态。

### 控制终端只允许前台组读取

`Terminal` 固定保存 terminal id、controlling SID、session leader PID 和
foreground PGID。PID1 启动时取得唯一控制终端。只有同 controlling session
且 PGID 等于 foreground PGID 的进程可以读取；其他读取返回
`BACKGROUND_TERMINAL_READ=-55`，不偷取字节也不进入无界阻塞。

v1.15 只建立一个 `/dev/console` 字符设备 vnode，并挂载于 `/dev`。VFS
`NodeType` 和公共目录项 ABI 增加 `CharacterDevice`，`FileDescription`
增加 `TerminalDevice`。路径解析、open、retain、close、stat 和 readdir
仍走统一 VFS；实际 read/write 在文件描述层转入唯一 `Terminal`。通用动态
devfs、设备注册和权限策略留到 v1.18。

### 控制键由 TTY 定向为前台组信号

PS/2 Set 1 解码器维护左右 Ctrl make/break 状态。Ctrl 与字母组合按 C0
控制码转换，因此 C 产生 `0x03`，Z 产生 `0x1A`。TTY 将它们分别解释为：

```text
Ctrl-C → SIGINT(2)  → foreground PGID
Ctrl-Z → SIGTSTP(20) → foreground PGID
```

SIGTSTP 和 SIGSTOP 的默认动作是停止；SIGCONT 的默认动作是继续。SIGSTOP
与 Kill 一样不可屏蔽、不可忽略、不可安装 handler。停止会保存当前用户通用
现场和 FXSAVE 现场，把 Process/Thread 转入 Stopped，并从可运行选择中排除；
继续重新发布原 Thread。停止不是退出，不释放地址空间、fd、VMA 或内核栈。

### wait 从“只等退出”扩展为有序事件

新增 56 字节 `ProcessWaitEventResult` 和 exited/stopped/continued/no-hang
flags。ProcessTree 为停止、继续和退出分别保存一次 pending 事件，父进程观察
事件不会提前回收子进程。退出事件最终仍是唯一收集点。

若停止、继续、退出在父进程获得 CPU 前连续发生，事件按
Stopped → Continued → Exited 可观察。统计分别记录产生与观察次数，阶段终态
要求两者一致。

### Shell 拥有作业表与终端切换事务

Shell 启动后建立自己的进程组并取得前台终端。每条外部命令或管线获得一个
PGID；管线全部 stage 加入同一组。最多保存 16 个作业，每个作业记录 job id、
PGID、成员 PID、成员状态和最后 stage 的退出码。

- 前台作业：TTY 交给作业 PGID，Shell 等待 stopped/continued/exited，
  最后无论成功或失败都把 TTY 收回；
- `command &`：建立作业后立即返回提示符；
- `jobs`：非阻塞收割后台事件并显示 Running/Stopped/Done；
- `fg`：把作业交回前台，必要时发送 SIGCONT，再阻塞等待；
- `bg`：对停止作业发送 SIGCONT，但不转移终端。

Shell 自身安装 SIGINT/SIGTSTP handler，避免控制动作意外终止交互环境；
fork child 在 exec 前恢复默认动作。父子双方都设置 PGID，以缩小 fork 后的
调度竞态；Zombie 身份保留则覆盖仍可能发生的极短退出窗口。

## ABI

新增系统调用 64--69：

1. `CreateSession`；
2. `GetSession`；
3. `SetProcessGroupFor`；
4. `GetTerminalInformation`；
5. `SetTerminalForegroundGroup`；
6. `WaitProcessEvent`。

新增错误 `BACKGROUND_TERMINAL_READ=-55`、
`NOT_CONTROLLING_TERMINAL=-56` 和
`SESSION_PERMISSION_DENIED=-57`。已有 0--63 编号不重排。

新增具名信号 INT=2、CONT=18、STOP=19、TSTP=20。公共结构和枚举底层均为
固定 64 位，不暴露 Kernel 指针、槽位或平台相关整数。

## 失败语义

- 非 session leader 重复取得终端：权限错误；
- 跨 session 移动进程、设置其他 session 的前台组：权限错误；
- 非法 PGID、已回收 PID、未知 wait flag：明确参数或进程错误；
- 后台组读取控制终端：立即返回专用错误，输入保持不变；
- TTY 输入/输出容量耗尽：不越界、不覆盖未读数据，并增加 dropped 或返回
  device failure；
- fork、pipe、PGID 设置或终端交接失败：Shell 关闭描述符、终止已建成员、
  收集事件并恢复自己的前台终端；
- SIGSTOP/SIGTSTP 不能释放资源；SIGCONT 对非停止进程保持幂等；
- 只有 Exited 事件回收 Zombie；Stopped/Continued 观察不能复用槽位。

## 不选择的方案

### 由 Shell 直接读取和解释 Ctrl-C

前台程序拥有 stdin 时 Shell 不在读取，且该方案无法覆盖任意前台管线。

### 每个命令建立一个 session

session 是登录/控制终端边界，作业只是同一 session 内的进程组。为每个命令
建 session 会使 Shell 无权切换同一控制终端。

### 停止时把进程当作 Zombie

这会错误释放地址空间和 fd，使 `fg` 无法从原指令继续，也破坏 wait 事件顺序。

### 在 IRQ 中直接遍历进程并调度

IRQ 只提交字符和具名 TTY 动作。信号选择、停止转换与调度切换复用现有
ProcessRuntime 安全边界，避免在设备中断栈上执行无界进程逻辑。

## 后果

系统得到可组合的单终端 Unix 作业控制基线：前后台管线、控制键、停止/继续、
字符设备路径和可观察 wait 事件已经贯通。代价是 Process 生命周期新增
Stopped 状态，进程级信号身份延长到 wait 收集，Shell 必须维护有界作业表。

当前仍不提供 termios、raw mode、多个虚拟终端、终端窗口尺寸、SIGHUP、
`tcsetpgrp` 的完整 POSIX 权限细节和通用 devfs。它们不会阻塞 v1.16 的
IRQ 块层与 writeback page cache。


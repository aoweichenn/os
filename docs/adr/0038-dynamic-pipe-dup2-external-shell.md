# ADR 0038：用动态管道、精确描述符复制和外部程序替换内嵌 Shell 命令

状态：已接受并在 v1.11 实现。

## 背景

v1.10 已经具备 fork、exec、wait、继承 FileTable 和共享
FileDescription。它能建立独立磁盘进程，却还不能表达 Unix Shell 最重要的
组合关系：

```text
producer stdout
  → pipe
  → transformer stdin
  → file redirection
```

第一周期的 64 字节管道只服务固定生产者/消费者探针；v1.0 Shell 的命令直接
编译在 Shell 进程内。继续在这种结构上增加命令会造成三类问题：

1. Shell 既解析命令又实现文件工具，无法证明 exec 与 fd 继承真的可组合；
2. 固定单管道不能支撑多级流水线，也没有系统级容量和生命周期所有者；
3. 普通 duplicate 只能选择“某个不小于下界的空 fd”，不能把端点精确接到
   fd 0、1 或 2。

v1.11 必须建立最小但完整的 Unix I/O 闭环，同时不能引入 libc、第三方 Shell、
现成启动器或 QEMU 半主机服务。

## 决策

### 1. 保留引导管道，新增独立动态管道管理器

已有 64 字节 `process_pipe` 保留为历史 IPC 回归路径。新的用户 `pipe()` 由
`PipeManager` 管理：

- 每条逻辑容量固定为 64 KiB；
- 存储按 4 KiB 页首次写入时申请，最多 16 页；
- 环形读写索引和字节计数使用 `uint64_t`；
- 读端全部关闭后写入返回 broken pipe；
- 写端全部关闭且缓冲为空后读取返回 EOF；
- 两端 FileDescription 的最后强引用释放后，管理器归还缓冲页和 pipe slot。

管道容量按同一镜像的 RAM 配置选择：

| 配置 | 来宾内存 | pipe slot |
| --- | ---: | ---: |
| bootstrap | 64 MiB | 8 |
| functional | 256 MiB | 128 |
| capacity | 64 GiB | 1024 |

管理器在进程运行时初始化阶段真实耗尽所选容量、验证下一次创建被拒绝，再关闭
全部端点。该自检不为每条空管道申请数据页，因此验证的是控制对象容量，不把
空管道误算成 64 KiB 物理承诺。

### 2. 缓冲页由物理页分配器提供

`Pipe` 不直接依赖全局 buddy。它接收固定宽度的
`PipePageAllocator` 回调：

```text
allocate → PhysicalFrameAllocator → physical address → direct-map pointer
release  → PhysicalFrameAllocator
```

这样宿主测试可以提供确定性页池和故障注入，目标 Kernel 则使用真实 frame。
一次写入若需要多个新页，任一申请失败会逆序归还本次已经申请的页，写索引、
缓冲字节数和可见数据均不提交。关闭释放失败时保留未释放页的地址，避免把
“未归还”伪装成“已经丢失所有权”。

### 3. pipe 端点是 FileDescription，不是裸 fd 特例

`CreatePipe` 创建一读一写两个 FileDescription：

- reader 只带 readable status；
- writer 只带 writable status；
- 两者都保存同一 Pipe 和拥有它的 PipeManager；
- fd duplicate/fork 只增加 FileDescription 强引用；
- FileDescription 最后引用的 finalizer 才关闭对应逻辑端点。

因此复制一个读 fd 不会提前制造第二个 Pipe reader 对象；关闭任意一个副本也
不会产生 EOF，只有共享 reader FileDescription 的最后强引用消失才关闭读端。

### 4. 新增精确 duplicate-to

系统调用 46 `DuplicateDescriptorTo` 对应 `dup2` 所需语义：

- source 无效时不修改 destination；
- source 与 destination 相同时成功且不关闭自身；
- destination 已打开时，以新强引用原子替换表项；
- 被替换对象在 FileTable 锁外释放和执行 finalizer；
- source 强引用获取失败时 destination 保持不变；替换提交后的旧对象
  finalizer 失败不回滚已公开表项，而是返回显式 release failure；
- fd flags 属于新表项，FileDescription status flags 仍由共享对象持有；
- destination 超出 soft limit 时明确失败。

普通 `DuplicateDescriptor` 继续提供“从 minimum 开始选择最低空槽”的
`dup`/`F_DUPFD` 风格能力。

### 5. Shell 只保留改变自身状态的 builtin

`cd` 必须修改 Shell 自己的 FsContext；`exit` 必须结束 Shell 自己。因此仅
保留这两个 builtin。其他命令统一执行：

```text
parse complete plan
  → create N-1 pipes
  → fork N children
  → child dup2 stdin/stdout and apply redirection
  → close every temporary pipe fd
  → exec /bin/<command>
  → parent close every temporary pipe fd
  → wait every created child
```

Shell 不搜索宿主 PATH。没有 `/` 的命令只映射到 `/bin/<command>`；含 `/`
的命令按用户给出的目标路径执行。

### 6. 解析计划必须先完整成功

执行解析器使用固定容量 `ShellExecutionPlan`：

- 输入行最多 512 字节；
- 最多 16 个 stage；
- 每 stage 最多 8 个参数；
- 支持空白、反斜杠、单双引号、`|`、`<`、`>`；
- 路径和参数复制到计划自有存储，不保留输入行悬空指针；
- 语法错误时输出计划保持全零，不创建 pipe 或 child。

计划大小由 `static_assert` 限制在 4096 字节以内。原因不是宿主栈偏好，而是
用户栈按相邻页故障增长；单个超页栈帧可能跨过尚未提交的中间页，形成
stack-clash 式故障。

### 7. 工具使用自研多调用 ELF

rootfs 安装十九个 `/bin/*` 路径，它们指向同一份自研 freestanding C++20
多调用程序内容：

```text
help echo cat wc head tee true false pwd ls stat
mkdir write touch rm rmdir mv truncate sync
```

程序从 `argv[0]` 的最后一个路径组件选择工具。多调用设计减少实现重复，但每个
路径仍经 rootfs 读取、ELF 校验和 exec；Kernel 与 Shell 均不内嵌这些工具。

## 失败语义

### 创建 pipe

Pipe slot、reader object、writer object、reader fd、writer fd 按顺序准备。
任一步失败都通过 RAII reference 或显式 close 逆序释放；不得把只安装了一端
的 pipe 返回用户态。

### 建立流水线

父进程先创建全部 pipe，再逐 stage fork。失败时：

1. 父进程关闭所有仍持有的 pipe fd；
2. 等待此前已经成功创建的 child；
3. 不继续创建后续 stage；
4. 返回明确失败，不保留 Zombie 或动态 pipe。

child 的 dup、open 或 exec 失败使用固定退出码结束，不能返回 Shell 主循环。
parent 即使最后一个 stage 失败，也必须等待全部已创建 child。

### 关闭和唤醒

关闭 reader 后唤醒可能阻塞的 writer，使其观察 broken pipe；关闭 writer 后
唤醒 reader，使其在排空缓冲后观察 EOF。FileDescription finalizer 在对象
管理器锁外执行，避免页释放、等待队列和对象锁形成递归锁序。

## 测试决策

v1.11 必须同时保留四层证据：

- 单元：按需跨页、申请回滚、EOF/broken pipe、dup2 替换、解析边界；
- 集成：functional 128 pipe、capacity 1024 pipe、slot 复用与零活动；
- 随机：动态环形字节流 100000 步；任意 Shell 字节输入 4096 轮；
- QEMU：functional 真实执行重定向、所有核心工具类型和 16 级流水线。

功能 QEMU 的 16 级流水线包含 `echo/cat/tee/head/wc`，真实建立 15 条 pipe、
fork/exec 16 个 child。结束后必须满足：

```text
DYNAMIC_PIPE_ACTIVE=0
DYNAMIC_PIPE_CREATIONS=DYNAMIC_PIPE_RELEASES
PROCESS_TREE_ZOMBIES=0
RUNTIME_STATE_VALIDATION=1
PROCESS_RESOURCE_VALIDATION=1
```

## 被否决的方案

### 把原 64 字节管道直接扩大成全局静态 64 KiB

这会让 1024 pipe 在没有数据时也永久占用 64 MiB，并把引导探针和用户动态
对象混为一个生命周期。

### 在 Shell 中继续实现所有命令

这只能验证函数调用，不能验证 rootfs ELF、fork、fd 继承、dup2 和 exec。

### fork 后在 parent 串行等待每个 stage

这会把流水线退化为顺序执行，并在上游写满 pipe、下游尚未启动时必然死锁。

### 使用宿主 shell、libc、BusyBox 或 QEMU 文件转发

这些方案违反“QEMU 只模拟硬件”的项目边界，也会绕过自研 ABI、VFS 和进程
资源模型。

## 后果

正面后果：

- fd 成为真正可组合的 Unix I/O 接线点；
- Shell 从命令实现容器变为进程图编排器；
- pipe 数据页、slot、FileDescription 与 fd 的所有权层次明确；
- v1.12 用户 Thread 可以复用同一 FileTable/WaitQueue 基础。

代价与边界：

- 当前没有 job control、后台管线、进程组或信号；
- 没有 PATH 搜索、环境展开、通配符、命令替换和追加重定向；
- 每个 stage 最多 8 个参数，这是当前单页用户栈提交模型的显式约束；
- writable shared mapping、socket、poll/select/epoll 不在 v1.11 范围。

## 关联

- [v1.11 发布记录](../releases/v1.11.md)
- [v1.11 学习章](../learning/19-v1.11-unix-io-external-shell.md)
- [v1.10 fork/COW ADR](0037-fork-copy-on-write.md)
- [路线图](../roadmap.md)

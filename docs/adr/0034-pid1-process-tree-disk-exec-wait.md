# ADR 0034：以 PID1、独立进程树和候选地址空间提交替换正常内嵌程序启动

- 状态：已接受
- 日期：2026-07-26

## 背景

v1.6 已提供严格挂载的 rootfs v2、VFS、每 Process FsContext、动态
FileTable 和足以保存多个 ELF 的 1 GiB 稀疏启动盘，但正常 Kernel 仍把
Shell、scheduler worker、pipe producer 和 pipe consumer 作为字节数组链接
进内核，再由 Kernel 逐个创建。这种脚手架适合早期证明 Ring 3、抢占、管道
和系统调用，却无法继续表达类 Unix 程序生命周期：

- 程序来源不是文件，VFS 与 ELF 加载没有连接；
- Kernel 决定全部用户程序，而不是只建立 PID1；
- PCB 没有父子关系，退出后只能由 Kernel 全局扫尾；
- 没有 Zombie，父进程无法取得子进程退出结果；
- 没有 exec 的失败原子性或 close-on-exec 真正调用点；
- 用户入口只收到项目测试用 PID，不能收到 `argc/argv/envp`；
- 后续 fork、COW、Shell 外部命令和信号缺少稳定的 Process 树基础。

直接增加一个“运行某文件”的系统调用而不先确定父子关系、候选映像和失败
边界，会把调度槽位、PID、文件表、页表和 VFS 生命周期耦合在一个不可验证
函数中。因此本阶段先冻结进程树与 exec 事务，再让 PID1 接管正常启动。

## 决策

### Kernel 正常路径只创建 PID1

Kernel 完成设备、rootfs 和 VFS 初始化后，以普通 VFS open/read 路径读取
`/sbin/init`。它构造 `argv[0]="/sbin/init"` 和
`envp[0]="OS_STAGE=v1.7"`，建立唯一初始 Process，然后进入调度器。

普通程序必须作为 rootfs 文件存在。正常 Kernel 不再嵌入 Shell、worker、
producer 或 consumer。内嵌 smoke/异常 ELF 只服务明确选择的故障镜像，
不参与正常用户空间。

该边界使启动责任清晰：

| 层 | 责任 |
| --- | --- |
| Kernel | 建立硬件、内存、VFS、PID1 和最小系统调用 |
| PID1 | 启动用户服务、形成进程树、回收直接子进程和孤儿 |
| Shell/程序 | 使用用户 ABI 请求文件、进程和控制台服务 |

### PID 与调度槽位保持分离

`ProcessTree` 使用调度器 `process_index` 定位内部槽位，同时保存对外稳定的
64 位 PID。PID 单调增长，槽位可在回收后复用；系统调用、日志和 wait 结果
绝不把槽位号冒充 PID。

进程树使用固定容量外部存储，与运行时选择的 8/64/256 Process 配置一致。
当前使用线性扫描是有意选择：最大 256 项时成本有界，状态和验证逻辑更容易
审计。未来若容量显著增加，可以在不改变公开 ABI 的情况下增加 PID 索引。

### 独立建模 Alive、Zombie 与 Unused

Process 最后一个 Thread 退出后，调度器 Process 与进程树项都进入 Zombie。
用户地址空间、KernelStack、FileTable、FileDescription 和 FsContext 可以
先释放；退出原因、退出码、异常向量、PID 和父关系继续存在，直到父进程 wait。

wait 成功依次执行：

1. 回收该 Process 已退出的 Thread 与 Ring 0 栈；
2. 在进程树中确认调用者是当前父进程；
3. 取得并清除 Zombie 退出结果；
4. 回收调度器 Process 槽位；
5. 向用户结果结构复制固定宽度字段。

结果地址和结构尺寸在第 2 步之前验证，坏用户指针不能“先回收、后复制失败”。
调度器回收是已验证状态下无分配的确定操作；若内部不变量仍失败，运行时将其
视为 Kernel 缺陷而不是可继续的用户错误。

### PID1 是孤儿收养者和最终回收边界

任意非 PID1 Process 退出时，其全部子项立即把父槽位改为 PID1。Alive 与
Zombie 子项都适用，否则“父先退出、子已退出但尚未 wait”的 Zombie 会失去
可达父进程。

PID1 仍有任何子项时不能退出。所有子进程回收后，PID1 可正常退出；没有用户
父进程可 wait PID1，因此 Kernel 在调度完成边界调用 `CollectInit`。进程树
最终必须为空。

本阶段不引入 subreaper、daemon 双 fork 或命名空间；唯一收养者就是全局
PID1。

### wait 使用统一 WaitQueue，而不是轮询

`WaitCondition::ChildProcess` 接入 v1.2 的统一 WaitQueue。若存在匹配的
Alive 子进程，wait 系统调用把当前 Thread 阻塞；任一子进程退出时唤醒等待者。
用户包装器从被唤醒的系统调用返回后再次尝试收集，以处理：

- 多个父进程共享全局 child-exit queue；
- 一次退出可能唤醒不相关父进程；
- 被唤醒到真正收集之间状态可能已由另一个合法等待者改变；
- `wait(any)` 与 `wait(pid)` 使用相同状态机。

当前每 Process 只有一个 Thread，因此同一父进程不会并发 wait 同一子进程。
多线程 wait 的竞争语义在用户线程阶段另行冻结。

### ELF 加载改为 reader 驱动的两遍流程

`UserElfReader` 只暴露精确 offset/length 读取。校验器第一遍读取 ELF 头和
程序头，在分配任何用户页之前验证完整元数据；地址空间加载器第二遍按页读取
每个 `PT_LOAD`。

选择 reader 而不是“把整个 ELF 读入 Kernel heap”有三个原因：

1. 单文件公开上限是 64 MiB，连续内核缓冲不应成为 exec 前置条件；
2. 内核栈只有 16 KiB，绝不能保存大型文件或参数；
3. 后续按需 ELF 可以继续复用文件来源抽象，而无需改变校验规则。

reader 必须提供稳定 `image_size_bytes`，每次读取必须完整成功。短读和设备
错误返回 `ReadFailed/ImageReadFailed`；候选地址空间必须逆序回滚。内存
故障夹具也包装为 reader，避免维护第二套 ELF 规则。

### argv/envp 采用“规划后分块复制”

Kernel 先复制最多 256 个 `ProcessString` 描述符，只保存固定 64 位长度，
计算字符串总量、指针元数据和最终栈地址。总字符串区域上限 128 KiB，用户栈
固定 256 KiB。

规划成功后才创建候选地址空间，并用 256 字节临时缓冲把用户字符串分块复制
到候选页表对应的物理页。这样：

- 内核栈占用与参数总量无关；
- 没有依赖 libc 的 NUL 扫描；
- 每次用户地址访问都经过映射验证；
- 精确 128 KiB 和第一个越界字节可分别测试；
- 元数据和字符串放不下时，候选映像仍未提交。

`ProgramArgumentPlan` 是单核、不可重入内核执行路径中的共享工作区。当前
系统调用进入后内核不可抢占，且每个 Process 只有一个 Thread，因此不会并发
覆盖。引入内核抢占或多核前，必须把工作区改为每次调用所有权或显式锁保护。

### exec 使用候选地址空间和单一提交点

exec 在旧地址空间仍有效时完成：

- 复制路径、参数描述符和字符串；
- 打开并校验 ELF；
- 创建页表、映射段和栈；
- 写入 `argc/argv/envp`；
- 临时加载候选 CR3，证明硬件能够激活，再切回旧 CR3。

只有这些步骤全部成功后，调度器才通过 `CommitProcessImage` 同时更新 Process
CR3 与当前 Thread 用户 RSP。运行时随后转移地址空间所有权、执行
`CloseOnExec`、销毁旧页表、清零并建立新 `UserContext`。

提交前失败保持：

- PID 与父子关系；
- 旧 CR3、RIP、RSP 和用户映像；
- FileTable、FsContext 和非候选资源；
- 当前进程继续发起系统调用的能力。

提交后任何内部失败都属于无法安全回滚的 Kernel 不变量破坏并停机，不能尝试
同时运行半个新映像和半个旧映像。

当前只允许 Process 拥有一个 live Thread。未来多线程 exec 必须先定义兄弟
Thread 的汇合、取消、锁和阻塞点，不在本阶段隐式处理。

### spawn 不是 fork

spawn 从当前 FsContext 解析可执行路径，但新 Process 当前获得：

- 新地址空间；
- 新用户栈；
- 新标准描述符集合；
- 新初始化的根目录 FsContext；
- 父进程 PID 关系。

它不复制父地址空间、文件表、cwd 或 Thread。这一语义足以让 PID1 启动绝对
路径程序，但不能称为 fork。v2 路线中的 fork/COW 会单独定义 FileTable 和
FsContext 继承。

### rootfs 用户程序在构建期离线安装

构建系统在 mkfs 后使用独立 Python 安装器写入 ELF。安装器使用冻结的 rootfs
v2 encoder 创建目录项、inode、直接/间接块和 bitmap，再由独立 inspector
完整验证。

安装器只接受空的、刚格式化介质。这样构建结果可复现，也避免一个通用离线
修改器与 Kernel 的事务语义竞争。运行期文件修改仍必须通过 Kernel VFS 和
RootFileSystem。

## 被拒绝的方案

### 继续把普通程序 incbin 到 Kernel

会绕过 VFS、rootfs 和磁盘错误，Kernel 仍承担用户服务编排，也无法证明
普通文件与可执行文件共享同一命名空间。

### 使用 GRUB、Limine、initrd、9p 或宿主目录

这些方案会把固件/加载或程序来源交给外部组件，违反“QEMU 只模拟硬件”的
项目边界。rootfs 和 ATA PIO 已经存在，应由它们承载程序。

### 一次把整个 ELF 读入 Kernel heap

64 MiB 单文件会超过当前 512 KiB heap，也会制造不必要的连续分配要求。
reader 两遍算法在更小内存下更稳定，并为后续按需页故障保留接口。

### exec 先销毁旧地址空间再加载新文件

任一 I/O、ELF、参数或页分配失败都会让调用 Process 无处返回。Unix 的 exec
失败必须返回旧程序；候选映像和单一提交点是必要条件。

### exec 创建一个新 PID

这会破坏父进程等待目标、打开文件、cwd 和进程身份。exec 替换映像，不创建
进程；spawn 才创建新 PID。

### 退出即立即删除 Process 项

父进程将丢失退出码和异常原因，wait 无法区分“不存在”与“已退出但尚未取走”。
Zombie 是父子生命周期的必要状态，不是可省略的垃圾。

### 用户态循环调用非阻塞 wait

会浪费 TCG 和真实 CPU 时间，并让日志/时序掩盖状态错误。统一 WaitQueue
已经具备阻塞与唤醒语义，应直接复用。

### 为参数使用 128 KiB 内核栈数组

当前 Ring 0 栈只有 16 KiB，该方案必然越界。即使扩大栈，也会让每 Thread
永久成本绑定 ABI 上限。固定小块搬运缓冲与用户页才是正确所有权。

## 后果

### 正面

- 正常用户空间第一次真正来自自研磁盘文件系统；
- PID1 成为唯一用户启动与孤儿回收边界；
- PID、槽位、Thread 和退出结果的职责分开；
- wait 不忙等，Zombie 与 reparent 可以独立验证；
- exec 失败保持旧映像，成功保持 PID 和 Process 资源身份；
- 128 KiB 参数不增加内核栈上限；
- close-on-exec 从模型能力进入真实系统调用路径；
- 后续 fork/COW、外部 Shell 命令、信号和进程组有稳定基础。

### 代价

- eager ELF 每次 spawn/exec 都同步读取并映射全部段；
- 全局 child-exit queue 可能产生无关唤醒；
- ProcessTree 当前按容量线性扫描；
- spawn 暂不继承 cwd 和文件表；
- 参数规划工作区依赖当前单核、内核不可抢占边界；
- 64 MiB 兼容档的并发容量从 4 调整到 8，以容纳完整 PID1 验收树。

## 验证

必须同时满足：

1. ProcessTree 单元测试覆盖初始化、父子、Zombie、wait、reparent、PID1 和
   最终计数守恒；
2. 参数布局单元测试精确接受 128 KiB，并原子拒绝下一字节、项目数和栈容量
   越界；
3. reader ELF 单元测试证明只读取头/程序头、短读失败且不覆盖输出布局；
4. scheduler 测试证明只有当前单 Thread Process 可以提交新 CR3/RSP；
5. 4096 轮集成模型完成 8192 个 Process 生命周期并复用槽位；
6. 固定种子随机模型覆盖 8192 个子进程和 4096 个参数布局；
7. rootfs 工具测试读回跨直接/间接边界的已安装文件并拒绝重复安装；
8. 64 MiB 与 256 MiB QEMU 启动均由磁盘 PID1 完成八进程树；
9. QEMU 逐字节验证精确 128 KiB、截断 ELF/E2BIG 失败保持、成功 exec、
   孤儿收养和 no-child；
10. 最终 ProcessTree、scheduler、frame、页表、KVA、KernelStack、对象、
    FileDescription、FileTable 和 FsContext 资源守恒；
11. 既有 CPU、ATA、异常、非法 ELF、rootfs 持久化/损坏和 64 GiB 容量回归
    继续通过；
12. C++ 命名、头源分离、固定宽度、中文注释、具名常量和 freestanding
    undefined-symbol 审计通过。

## 关联

- [v1.7 发布记录](../releases/v1.7.md)
- [v1.7 学习章](../learning/15-v1.7-pid1-process-tree-exec.md)
- [进程模块](../modules/process.md)
- [用户环境模块](../modules/user-environment.md)
- [v1.7 路线](../roadmap.md#v17-pid1进程树与磁盘-execwait)
- [ADR 0031：KernelObject 与 FileTable](0031-typed-kernel-object-dynamic-file-table.md)
- [ADR 0033：rootfs v2](0033-rootfs-v2-namespace-mutations.md)

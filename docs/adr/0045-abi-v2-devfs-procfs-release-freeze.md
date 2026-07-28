# ADR 0045：冻结 ABI v2、以 devfs/procfs 收口观察面并建立发布门禁

- 状态：已接受
- 适用版本：v1.18
- 决策日期：2026-07-28

## 背景

v1.1--v1.17 已经完成 Process/Thread、虚拟内存、fork/COW、文件描述符、
VFS、rootfs、信号、TTY、异步块 I/O 与 ordered metadata journal。若继续
增加系统调用或基础机制，任何一个改动都可能同时改变用户程序、Kernel
dispatcher、信号返回帧、磁盘 ELF、教材和网站，v2.0 将永远处于“最后再改
一次”的状态。

冻结不是停止检查，而是把隐含假设改成可执行契约。v1.18 之前仍有三个缺口：

1. ABI 号值和结构虽然分散存在于头文件，却没有统一版本和偏移门禁；
2. `/dev/console` 由专用后端提供，无法表达“设备命名空间”和“驱动实现”
   是两层责任；
3. 资源统计主要在启动结束时打印，用户程序不能通过普通 VFS 读取系统状态。

此外，v1.11 的 19 个核心工具足以验证重定向与流水线，却不足以让读者在
Shell 中观察进程、内存、挂载、时间和资源。发布过程也必须把主仓、网站、
Sites 版本和教材哈希连成同一份证据，不能只说“代码已经推送”。

## 决策一：ABI v2.0.0 是显式线协议

新增 `source/abi/include/os/abi/version.hpp`、`elf.hpp` 和 `layout.hpp`。
ABI v2 冻结以下事实：

- major/minor/patch 为 2.0.0；
- 系统调用有效号为 1--69，最后一项是 `WaitProcessEvent`；
- 公共错误区间为 -1---57；
- 目标 ELF 是小端 ELF64、x86-64、`ET_EXEC`；
- `PipeDescriptorPair`、`DirectoryEntry`、`FileInformation`、
  `ProcessLaunchRequest`、`ProcessWaitResult`、`ThreadCreateRequest`、
  `SignalAction`、`SignalFrame`、`FileMemoryMapRequest`、
  `ProcessWaitEventResult` 和 `TerminalInformation` 的字段偏移固定。

`layout.hpp` 由 Kernel 与用户程序共同编译，`abi_v2_contract_test.cpp`
独立检查版本、数量、大小、对齐、枚举和错误边界。Kernel ELF 解析器改为
消费共享 `elf.hpp`，不再维护第二套魔法值。

冻结不等于承诺完整 POSIX。ABI v2 仍是本项目自己的 64 位线协议，不保证
Linux syscall number、errno、结构体或调用约定兼容。所有地址、大小、计数、
标志和身份使用固定宽度类型；不得把宿主指针、C++ 对象地址、编译器 padding
或平台相关整数类型暴露给 Ring 3。

ABI v2 之后若需要破坏兼容的字段或语义，必须提高 ABI major；仅增加保持旧
语义的能力也要先写 ADR、增加协商或明确最小版本，不能静默复用已有号值。

## 决策二：devfs 只管理设备名称，不拥有驱动

删除专用 `ConsoleDeviceFileSystem`，由最小 `Devfs` 替代。`Devfs` 接受调用者
提供的固定容量 `DevfsDevice` 数组，当前默认容量为 16；初始化后可注册具名
字符设备，当前生产实例注册 `console`。

每个注册项只保存：

- 稳定 node identifier 和 generation；
- 最多 255 字节的名称及精确长度；
- active 状态。

它不保存 `Terminal`、端口号、函数对象或任意驱动指针。打开
`/dev/console` 后，字符流仍由类型化 `FileDescription` 与终端设备实现。
因此 VFS 负责路径和 vnode 生命周期，devfs 负责设备名称，驱动负责 I/O；
任何一层都不能绕过相邻公开契约。

命名空间只读。普通用户不能 create/remove/rename/truncate 设备项；注册重名、
空名称、超长名称和容量耗尽均在发布任何新节点前失败。readdir 按固定槽位
产生稳定顺序，stat 不伪造普通文件大小。open/close 和目录读取只更新有界
统计，不逐操作打印串口。

v1.18 不实现设备权限、uid/gid、major/minor 号、热插拔、注销、udev 或真实
设备 inode 持久化。这些能力需要新的生命周期和权限模型，不能从当前固定
注册表推断出来。

## 决策三：procfs 是每次读取生成的只读快照

`Procfs` 固定提供六个文件：

| 路径 | 内容 |
| --- | --- |
| `/proc/version` | ABI major/minor 与 `x86_64` 架构 |
| `/proc/uptime` | PIT 单调纳秒 |
| `/proc/meminfo` | managed/free/allocated 物理内存字节 |
| `/proc/processes` | 活动 Process/Thread、容量与当前 PID |
| `/proc/resources` | heap、FileDescription、Pipe、vnode、journal commit |
| `/proc/mounts` | 当前 mount 数 |

每次 `read` 或 `stat` 调用 snapshot provider，先取得数值快照，再在 256 字节
栈缓冲中格式化十进制文本。格式化不分配内存、不返回 Kernel 指针、不持有
procfs 自旋锁调用其他子系统。统计锁只保护 procfs 自己的 open/read/failure
计数，避免形成 `procfs lock → VFS/process/memory lock` 的反向依赖。

读取遵守普通文件的 offset/short-read/EOF 契约。一个 read 得到同一份快照；
两次独立 read 允许看到不同时间和资源状态。这与 Linux procfs 的动态观察面
类似，但本项目没有声称字段名或文本格式兼容 Linux。文本字段是教学接口，
不是可无限扩展的内核 ABI。

provider 失败返回明确设备失败，缓冲不足返回容量耗尽；都不会输出部分伪快照。
100000 步随机测试用独立字符串 oracle 验证随机 offset、容量和变化中的数值，
集成测试再经 VFS 同时挂载 rootfs、memfs、devfs 和 procfs。

## 决策四：32 个工具必须同时满足产物和运行证据

rootfs 中冻结 32 个独立路径：

```text
help echo cat wc head tee true false pwd ls stat mkdir write touch
rm rmdir mv truncate sync basename dirname cp seq uptime ps free uname
mounts resources sleep kill id
```

这些路径当前可以共享同一份 multi-call ELF 字节，但离线安装必须为每个路径
分配独立 inode。`/bin/tool_probe` 在真实 Ring 3 启动中逐项执行 stat，验证
regular-file 类型、唯一 inode 和 ELF magic；任一缺失、重复或非 ELF 都使
PID1 验收失败。

256 MiB functional QEMU 还会从 Shell 实际执行新增 13 个工具。basename、
dirname、seq 和 procfs 工具使用唯一输出标记；cp 的结果由 cat 回读；sleep
走真实 deadline；kill 向 PID1 发送默认忽略的 SIGCHLD；id 读取当前 PID。
进程创建、退出、wait、FileDescription、Pipe、VFS 与资源快照必须最终守恒。
这防止“构建图里列了 32 个名字”被误当成用户功能完成。

工具不得为凑数量要求新 Kernel 机制。它们只组合已经冻结的系统调用；参数
数量、十进制溢出、序列上限、sleep 纳秒乘法和描述符关闭都有明确失败路径。

## 锁、整数、用户输入与日志审计

冻结期执行下列审计：

- 用户地址只经统一 copy-in/copy-out，不直接解引用 Ring 3 指针；
- offset+length、页对齐、地址范围、十进制和时间换算在加法或乘法前检查；
- ELF header/program header、路径、signal frame、rootfs/journal 字段先完整
  验证，后提交可见状态；
- VFS 资源汇总用 checked add，失败清空输出；
- procfs callback 不持有 procfs 锁，devfs 注册表没有注销竞态；
- QEMU 热路径只保留计数和幂次采样，禁止逐 tick、逐页、逐字节和逐锁日志；
- 启动只输出 `DEVFS_READY`、`PROCFS_READY`、`ABI_V2_FROZEN` 等低频边界，
  结束时输出有界汇总。

本项目是单 BSP、内核不可抢占模型。此审计证明当前锁方向与退出资源守恒，
不声称已经获得 SMP 正确性。

## 测试调度决策

宿主单元、集成和随机测试允许按 CPU 数并行。所有会启动
`qemu-system-x86_64` 的 CTest 使用同一个 `RESOURCE_LOCK`，即使调用者执行
`ctest --parallel 20`，QEMU 也必须串行。

原因不是功能依赖，而是 TCG 和来宾内存属于共享机器资源。并行启动多个 QEMU
会让原本 2--10 秒的故障路径撞上有界超时，产生虚假失败并可能触发宿主 swap。
资源锁把这一机器约束固化进测试图，不再依赖执行者记住特殊命令。

三档规格保持：

- 64 MiB：bootstrap 兼容；
- 256 MiB：完整 Shell/工具功能；
- 64 GiB：capacity，并实际触及 4 GiB 以上物理地址。

64 GiB 是来宾可见规格，不要求宿主预分配 64 GiB；QEMU TCG 使用稀疏按需
页，测试仍必须从来宾日志证明高地址页被真实分配、写入、读回和回收。

## 发布溯源

v1.18 和 v2.0 发布清单必须记录：

- 已推送的主仓 commit SHA；
- 已推送的独立 web 仓 commit SHA；
- Sites 保存版本、部署 ID 与公网 URL；
- 教材 PDF 页数、字节数和 SHA-256；
- 只统计 `source/` 中 `.cpp/.hpp/.asm` 的真实代码量；
- CTest 数量、随机步数、崩溃点、三档 QEMU 结果；
- QEMU 版本、CPU 模型与冻结的来宾 CPUID/地址宽度。

网站只展示主项目源码与文档，不能展示 `web/` 自身代码。保存 Sites 版本前
必须推送生成该版本的精确 web SHA；部署后实际请求首页、发布记录、学习章、
ADR、关键源码和同哈希 PDF，不能只相信部署 API 返回成功。

## 后果

正面后果：

- 用户边界有统一版本、共享 ELF 定义和编译期布局证据；
- `/dev` 与 `/proc` 都经普通 VFS 访问，观察工具不需要新系统调用；
- 32 个工具具有“路径、inode、ELF、实际执行”四层证据；
- QEMU 并发约束进入测试图，避免机器资源造成伪回归；
- v2.0 可以只做集成、版本和发布，不再承担新机制风险。

代价与限制：

- procfs 是固定六文件快照，不提供每 PID 目录、权限或稳定跨 read 事务；
- devfs 只有注册和只读枚举，没有热插拔、注销或设备权限；
- multi-call 工具共享实现，功能范围小于 POSIX/GNU 工具；
- ABI v2 的冻结会使后续破坏性改动必须显式升级版本；
- 单 BSP 的锁审计不能替代未来 SMP 的重新设计。

上述限制是 v2.0 的范围边界，不是隐藏的未完成项。新增核心机制进入 v2.x。

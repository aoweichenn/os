# Kernel 源码布局

Kernel 按功能所有权分为十二组。公开头文件与实现使用完全对称的相对路径：

```text
include/os/kernel/<module>/<name>.hpp
src/<module>/<name>.cpp
```

例如页表接口和实现固定为：

```text
include/os/kernel/memory/page_table.hpp
src/memory/page_table.cpp
```

## 模块职责

| 目录 | 职责 |
| --- | --- |
| `arch/` | x86-64 描述符表、异常/IRQ、CpuLocal、SYSCALL、处理器现场和 panic |
| `boot/` | BootInfo 校验与 C ABI 内核入口 |
| `core/` | Kernel 主流程和 freestanding 内存运行时 |
| `device/` | 端口 I/O、串口、PIC、PIT、PS/2 与 ATA |
| `fs/` | 磁盘格式、块缓存和文件系统 |
| `io/` | 控制台输入、共享 FileDescription 与动态 FileTable |
| `ipc/` | 有界管道和端点生命周期 |
| `memory/` | 物理页、buddy、页表、heap、KVA、动态栈、VMA 与资源快照 |
| `object/` | 类型化 KernelObject、generation 与强引用生命周期 |
| `process/` | Process/Thread 状态机、run queue、WaitQueue 和目标机生命周期 |
| `sync/` | SpinLock、IrqSaveSpinLock 与可睡眠 Mutex |
| `user/` | 用户 ELF、用户内存、系统调用和内嵌程序镜像边界 |

目录表达“谁负责维护这个文件”，不额外制造冗长 C++ 命名空间。当前公开类型
仍位于简短的 `os::kernel`；将来只有在同名概念或独立子系统 API 确实需要时，
才引入 `os::kernel::<module>`。

## 结构约束

- Kernel 根目录不允许再堆放 `.hpp`、`.tpp`、`.cpp` 或 `.asm`。
- 每个公开 `.hpp` 必须在同名模块下具有对应 `.cpp`。
- 模板实现 `.tpp` 必须与同名 `.hpp` 同目录。
- 只有 `arch/architecture.asm`、`memory/page_table_layout.cpp` 和
  `user/user_images.asm.in` 是记录在案的非一一配对实现。
- `source/kernel/CMakeLists.txt` 按同一模块集合维护清单，再组合目标；新增文件
  不得直接插入无分组的总列表。
- `tests/tooling/test_kernel_layout.py` 自动验证当前树和上述配对关系。

模块之间可以通过公开头文件组合，但 `core/kernel_main.cpp` 只负责编排，不承载
可独立测试的算法；算法应下沉到所属模块，使 host 模型目标仍能独立构建。

v1.2 的执行模型文件固定归属如下：

```text
arch/extended_state.*          CPUID、CR0/CR4 与目标机 FXSAVE/FXRSTOR
arch/extended_state_layout.*   可在宿主测试的 512/16 布局和能力解码
process/thread_scheduler.*     Process/Thread 状态与侵入式队列
process/wait_queue.*           等待对象的身份、计数与关闭状态
process/process_tree.*         PID1、父子身份、Zombie、wait 与孤儿收养
process/program_arguments.*    argc/argv/envp 的纯 64 位用户栈布局
process/process_runtime.*      CR3、TSS.RSP0、动态栈、用户帧和现场组合
sync/spin_lock.*               acquire/release 与 irq-save 短临界区
sync/mutex.*                   基于 WaitQueue 的可睡眠直接交接互斥
```

`ThreadScheduler` 不允许依赖 `memory/`、`device/` 或串口；硬件切换只存在于
`process_runtime.cpp`。`extended_state_layout.cpp` 不执行特权指令，因此
宿主单元测试可以验证 CPUID 位和结构布局；`extended_state.cpp` 只进入
freestanding Kernel 目标。新增执行机制时应保持这条“纯策略—目标机落实”
边界。

v1.3 的架构入口文件固定归属如下：

```text
arch/processor_features.*          CPUID 能力 profile 的纯解码与验证
arch/cpu_local.*                   每 CPU 当前 Thread、可信入口栈与深度/统计
arch/user_context.*                统一用户现场、返回校验和 SYSRET/IRET 选择
arch/native_system_call_layout.*   六个 MSR 的纯布局与回读比较
arch/native_system_call.*          目标机 MSR 初始化
arch/architecture.asm              SWAPGS、两类入口、统一返回和 NMI 最小桩
user/system_calls.*                共同 C++ dispatcher 与用户返回准备
```

`processor_features`、`user_context` 与 `native_system_call_layout` 必须保持
可由宿主测试直接链接；RDMSR/WRMSR 和汇编入口只能出现在目标机层。CpuLocal
虽然当前是单元素，也必须继续位于 `arch/`，不能塞入 ProcessRuntime 的私有
全局状态。

v1.4 的对象与描述符文件固定归属如下：

```text
object/kernel_object.*         类型、代次、强引用、活动链与最后引用 finalizer
io/file_description.*         共享偏移、file status flags 和统一资源操作
io/file_table.*               分块 fd 表、fd flags、limit 与两阶段安装
process/process_runtime.*      每 Process FileTable 和目标机等待/唤醒组合
```

`object/` 不依赖某一种文件系统、管道或设备；具体 payload 和 finalizer 由
`io/` 提供。FileTable 只持有私有 handle，不能包含 FileDescription 裸指针。

v1.5/v1.6 的文件系统文件固定归属如下：

```text
fs/vfs.*                       路径、Mount、FsContext 与 namespace mutation
fs/memfs.*                     /tmp 内存后端和差分模型
fs/legacy_file_system.*        旧盘面 VFS 适配，仅保留兼容回归
fs/file_system.*               v0.11 旧格式实现
fs/root_file_system_format.*   rootfs v2 冻结小端盘面编码
fs/root_file_system.*          生产根、三级块树、事务和全盘校验
fs/block_cache.*               固定容量写回缓存
```

`root_file_system_format` 只负责字节布局，不访问设备、VFS 或全局 Kernel
状态，因此宿主单元测试可直接链接；`root_file_system` 组合块设备、缓存和
VFS 操作表。Python mkfs/fsck 是独立工具实现，不进入 Kernel 目标。

v1.7 的磁盘程序与进程树继续归属已有 `process/` 和 `user/` 边界：

```text
process/process_tree.*         与调度槽分离的父子关系、退出状态与 wait
process/program_arguments.*    不访问页表的参数/环境布局规划
process/process_runtime.*      VFS ELF reader、spawn/exec/wait 与失败展开
user/user_elf.*                内存/文件共用的 reader 两遍 ELF 验证
user/user_memory.*             候选 AddressSpace、8 MiB 用户栈预留与段复制
user/system_calls.*            36/37/38 号 ABI 的用户复制和结果映射
```

`process_tree` 与 `program_arguments` 必须保持宿主可链接，不得反向依赖
VFS、页表或设备。磁盘读取和真实资源提交只进入 `process_runtime`；
ELF parser 只通过 `UserElfReader` 观察 `(offset, length)`，不认识 inode。
普通用户程序属于 rootfs，不得重新加入 `user_program_images`；后者只保留
启动模式必须直接选择的最小 smoke/异常夹具。

v1.8 在相同目录边界内增加用户虚拟内存策略：

```text
memory/virtual_memory_area.*   有序 VMA、描述符池、split/merge/gap 与守恒
user/user_memory.*             匿名 fault、brk、unmap、栈增长与统计
user/system_calls.*            39..42 号 VM ABI 校验和当前 Process 分发
```

VMA 容器不访问 CR2、页表或 frame allocator；它保持宿主可链接。实际缺页
解析只进入 `user_memory`，异常入口仍归属 `arch/`，当前 Process 与退出/
exec 生命周期仍归属 `process/`。这种依赖方向防止区间算法、x86 fault 机制
和 Process 调度重新堆进一个不可测试文件。

v1.9 继续复用同一模块边界：

- `memory/file_page_cache.*` 管理有界 clean 页、引用与 LRU；
- `user/file_backing.*` 管理 VFS/内存来源和稳定 generation；
- `memory/virtual_memory_area.*` 保存文件区间意图；
- `user/user_memory.*` 组合 fault、PTE、cache、失效与回收；
- `process/process_runtime.*` 只负责 fd/进程生命周期与系统调用编排。

文件系统代码不直接建立 PTE，页缓存也不解析 fd；跨模块依赖保持单向。

v1.10 的 fork/COW 继续保持相同所有权边界：

```text
memory/user_page_reference.*  共享 private frame 的稀疏引用状态机
memory/page_table.*           COW 软件位编码、leaf 查询与 ReplacePage
user/user_memory.*            AddressSpace clone、COW break、失败恢复
io/file_table.*               精确 fd/flags clone，共享 FileDescription 引用
fs/vfs.*                      FsContext clone
user/file_backing.*           文件后备 clone
process/process_runtime.*      Process/Thread 候选事务、发布与回收
```

引用表不认识 VMA、Process 或文件；页表不决定某页是否允许 COW；只有
`user_memory` 联合 VMA、PTE 与引用状态。`process_runtime` 负责跨资源事务，
不重新实现页复制。设计理由见
[ADR 0037](../../docs/adr/0037-fork-copy-on-write.md)。

v1.11 在这些所有权边界上补齐 Unix I/O 组合：

```text
ipc/pipe.*                   64 KiB 环形流、4 KiB 按需页与端点状态
ipc/pipe_manager.*           8/128/1024 分档 slot、创建和最终回收
io/file_description.*        把 Pipe 端点纳入共享打开文件语义
io/file_table.*              512 functional hard limit 与定点 DuplicateTo
user/system_calls.*          CreatePipe/DuplicateDescriptorTo ABI 分发
process/process_runtime.*    当前进程 pipe/dup2 编排和页分配回调
```

Pipe 不认识 fd，FileTable 不解析 Shell，ProcessRuntime 只组合对象和当前
Process。`DuplicateTo` 在表锁内提交新强引用，在锁外释放旧对象；提交前失败
保持目标不变，提交后的 finalizer 失败显式升级为资源账本错误。外部 Shell 和
十九个 `/bin` 路径属于 `source/user/`，Kernel 不内嵌命令实现。设计理由见
[ADR 0038](../../docs/adr/0038-dynamic-pipe-dup2-external-shell.md)。

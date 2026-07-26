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
| `memory/` | 物理页、buddy、页表、heap、KVA、动态栈与资源快照 |
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

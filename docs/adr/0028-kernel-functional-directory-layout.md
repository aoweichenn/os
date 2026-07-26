# ADR 0028：Kernel 按功能所有权组织对称头源目录

- 状态：已接受
- 日期：2026-07-26

## 背景

v1.1 完成时，Kernel 已包含处理器描述符、异常与 IRQ、内存分配、设备、
文件系统、进程、IPC、用户边界等多条独立机制。虽然公开头文件与实现分别位于
`include/os/kernel/` 和 `src/`，但四十余组文件仍直接堆放在这两个根目录。

这种布局在文件较少时便于浏览，规模增长后会产生四类问题：

1. 文件名只能说明“是什么”，目录不能说明“由哪个子系统维护”；
2. 头文件与实现文件的相对位置没有可检查的对应关系，移动一侧时容易遗漏另一侧；
3. CMake 的总源文件清单混合多个职责，评审无法快速识别跨模块变更；
4. v1.2 以后增加 Thread、WaitQueue、VMA、VFS 和动态对象时，扁平目录会继续
   放大命名冲突与依赖方向不清的问题。

目录不能替代接口设计，但它应当表达稳定的所有权边界。模块划分必须服务于
当前代码，而不是提前创建尚不存在的复杂层次。

## 决策

### 使用十一组功能目录

Kernel 公开头文件和实现采用相同模块集合：

| 目录 | 所有权范围 |
| --- | --- |
| `arch/` | x86-64 描述符、异常/IRQ 入口、处理器状态与 panic |
| `boot/` | BootInfo 校验和 C ABI Kernel 入口 |
| `core/` | Kernel 主流程与 freestanding 基础运行时 |
| `device/` | 端口 I/O、串口、PIC、PIT、PS/2 与 ATA |
| `fs/` | 固定磁盘格式、块缓存与文件系统 |
| `io/` | 控制台输入和统一 I/O 描述符 |
| `ipc/` | 管道及其端点生命周期 |
| `memory/` | 页帧、buddy、页表、heap、KVA、动态栈与资源快照 |
| `process/` | 进程运行时与调度模型 |
| `sync/` | 不依赖具体资源的同步原语 |
| `user/` | 用户 ELF、用户内存、系统调用与内嵌用户镜像边界 |

公开路径和实现路径必须对称：

```text
source/kernel/include/os/kernel/<module>/<name>.hpp
source/kernel/src/<module>/<name>.cpp
```

例如：

```text
source/kernel/include/os/kernel/memory/page_table.hpp
source/kernel/src/memory/page_table.cpp
```

Kernel 的两个根目录不再直接保存 `.hpp`、`.tpp`、`.cpp` 或 `.asm` 实现文件。
模板 `.tpp` 与同名 `.hpp` 保存在同一模块目录。

### 目录表达所有权，命名空间表达语言级作用域

本次迁移不机械地把目录名复制成 C++ 命名空间。公开类型继续位于简短的
`os::kernel`，原因是：

- 目录首先回答源文件的维护归属；
- 当前类型名在 `os::kernel` 内没有冲突；
- 为每个目录增加一层命名空间会扩大公开 API 和调用点变化，却不能自动形成
  更好的依赖边界。

只有当两个子系统出现同名概念、需要独立公开 API，或能够通过 target 明确
限制依赖时，才另行引入 `os::kernel::<module>`。命名空间仍遵守“每层一个
简短小写单词”的全局规范。

### CMake 清单按同一模块分组

`source/kernel/CMakeLists.txt` 分别维护十一组头文件和源文件变量，再组合
Kernel 目标。宿主模型目标只选择其测试所需的模块实现，freestanding 目标组合
全部共享实现。汇编入口、用户镜像模板和测试专用入口继续作为明确的特殊输入，
不伪装成普通头源配对。

目录分组目前不拆成十一个独立生产静态库。Kernel 各子系统仍存在紧密的
freestanding 链接关系，过早制造 target 会产生循环链接或人为接口。后续应在
依赖能够保持单向时逐个拆出，而不是让目录迁移同时改变链接语义。

### 用结构测试防止回退

`tools/os_tools/kernel_layout.py` 验证：

- 头文件树与源文件树拥有完全相同的十一组模块；
- 两个根目录没有扁平实现文件；
- 每个公开 `.hpp` 在同一模块中存在对应 `.cpp`；
- 每个 `.tpp` 存在同名 `.hpp`；
- 非一一配对实现只允许记录在案的
  `arch/architecture.asm`、`memory/page_table_layout.cpp` 和
  `user/user_images.asm.in`。

`tests/tooling/test_kernel_layout.py` 同时验证当前仓库和两个失败样例。它由
既有 `os_python_tooling_unit_tests` CTest 入口执行，因此结构规则进入完整
回归，但不为同一 Python 测试集合虚增顶层 CTest 数量。

## 迁移结果

- 所有 Kernel 公开头文件和实现文件按上述模块移动；
- 全部 C++ include、CMake 输入、测试、系统故障入口、文档、教材和网站代码
  走读路径同步更新；
- 全新配置后的 986 个构建步骤成功，所有宿主与 x86-64 freestanding 目标
  均从新路径编译和链接；
- `source/kernel/README.md` 成为新增文件选址的短入口，本 ADR 保留决策理由。

## 后果

### 正面

- 从路径即可判断文件所有者和主要评审者；
- 头源配对拥有机械可验证的相对路径；
- CMake 变更能够按功能组审查；
- 在线代码浏览器自然形成与生产树一致的层级；
- v1.2 以后扩展 Process/Thread、等待和对象层时已有稳定落点。

### 代价

- 一次迁移会产生大量 Git rename 和 include 路径变化；
- 目录边界本身不阻止跨模块 include，依赖方向仍需通过接口、target 和评审
  逐步收紧；
- 少量生成或汇编文件无法与公开头文件一一配对，必须维护明确例外清单。

## 未采用方案

### 继续保留扁平 Kernel 目录

避免一次迁移，但每个后续阶段都会继续增加定位、命名和评审成本，且无法自动
检查头源归属。

### 只划分源文件，不划分头文件

调用者从 include 路径看不到模块所有权，头源也无法形成对称关系，不能解决
维护问题。

### 按文件类型建立 `drivers/`、`headers/`、`implementation/`

这仍然让同一功能的接口、实现和测试相距过远。目录应按变化原因聚合，而不是按
扩展名聚合。

### 同时重写全部 C++ 命名空间并拆分十一组静态库

会把物理整理、公开 API 迁移和链接依赖重构混成一次高风险变化。当前先固定
文件所有权和自动门禁；语言级与链接级边界在出现真实依赖收益时单独决策。

## 相关文档

- [项目架构](../architecture.md)
- [Kernel 模块](../modules/kernel.md)
- [测试策略](../testing.md)
- [v1.1 发布记录](../releases/v1.1.md)
- [Kernel 源码布局](../../source/kernel/README.md)

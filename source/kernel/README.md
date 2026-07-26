# Kernel 源码布局

Kernel 按功能所有权分为十一组。公开头文件与实现使用完全对称的相对路径：

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
| `arch/` | x86-64 描述符表、异常/IRQ 入口、处理器状态和 panic |
| `boot/` | BootInfo 校验与 C ABI 内核入口 |
| `core/` | Kernel 主流程和 freestanding 内存运行时 |
| `device/` | 端口 I/O、串口、PIC、PIT、PS/2 与 ATA |
| `fs/` | 磁盘格式、块缓存和文件系统 |
| `io/` | 控制台输入与统一 I/O 描述符 |
| `ipc/` | 有界管道和端点生命周期 |
| `memory/` | 物理页、buddy、页表、heap、KVA、动态栈与资源快照 |
| `process/` | 进程状态机、调度和目标机生命周期 |
| `sync/` | 不依赖具体资源的同步原语 |
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

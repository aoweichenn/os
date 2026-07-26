# Foundation 模块

## 职责

`foundation` 提供固件、引导阶段和内核共同使用的 freestanding 基础类型与
不依赖运行时的资源生命周期原语。当前实现：

- `PhysicalAddress`：显式表达物理地址。
- `ByteCount`：显式表达字节数量。
- `AddressRange`：表达半开地址区间 `[begin, end)`。
- `AddressRangeCreationStatus`：表达区间创建成功或地址溢出。
- `ReferenceCounter`：表达不可复活、可检测溢出与下溢的 64 位强引用计数。
- `ScopeRollback`：用调用方提供的固定容量存储记录撤销动作，并按注册顺序的
  逆序执行。

## 公开接口

公开声明位于：

```text
source/foundation/include/os/foundation/address_range.hpp
source/foundation/include/os/foundation/reference_counter.hpp
source/foundation/include/os/foundation/scope_rollback.hpp
```

实现位于：

```text
source/foundation/src/address_range.cpp
source/foundation/src/reference_counter.cpp
source/foundation/src/scope_rollback.cpp
```

## 不变量

- 地址值使用 64 位无符号整数。
- 地址区间采用半开语义。
- 空区间不包含任何地址，也不与任何区间重叠。
- 无法用 64 位结束地址表示的区间必须返回 `AddressOverflow`。
- 创建失败时不得修改调用方提供的输出区间。
- 引用计数为零后不得重新获取引用；增加到 `UINT64_MAX` 时必须报告溢出。
- 引用计数释放到零的动作只能发生一次；零值继续释放必须报告下溢。
- 回滚动作只能在事务尚未提交时执行，且必须严格逆序。
- 某个回滚动作失败时，后续回滚动作仍必须继续执行，避免次生泄漏。
- `ScopeRollback` 不分配内存，不捕获 lambda，也不依赖异常展开。

### 为什么使用半开区间

对于 `[begin, end)`：

```text
size = end - begin
```

空区间可直接表示为 `[address, address)`，相邻区间 `[a, b)` 与 `[b, c)`
不会被误判为重叠。该语义与数组下标、内存复制长度和大多数页区间算法一致。

### 可表示范围

当前 `AddressRange` 保存 64 位起始和结束地址。如果 `begin + size`
无法由 64 位结束地址表示，创建失败。因此它不能表达结束点数学意义上恰好为
`2^64` 的非空区间。这是当前阶段的显式限制，后续若需要表达完整物理地址空间边界，应引入更适合的端点类型，而不是让无符号加法静默回绕。

## 依赖

目标实现只依赖 freestanding C 头文件 `<stdint.h>`，不依赖 libc、libstdc++、
libc++、异常、RTTI 或动态分配。

## 目录与可见性

- `include/os/foundation/` 是其他模块唯一允许包含的公开接口目录。
- `src/` 保存实现和未来的私有头文件，不通过 CMake 传递给消费者。
- `source/foundation/CMakeLists.txt` 独立维护模块源文件和 host、x86-64
  两个构建目标。

## 失败语义

`AddressRange::TryCreate` 在 `begin + size` 超出可表示地址时返回
`AddressRangeCreationStatus::AddressOverflow`。调用方必须检查返回值，不得假定输入合法。

接口使用状态返回值和输出参数，是因为目标环境当前不引入标准库
`std::optional` 或异常。输出参数只在成功时提交新值，使失败路径具有原子性。

`ReferenceCounter::TryAcquire` 与 `TryRelease` 同样使用状态返回值。失败时计数和
输出参数都保持不变，因此调用方不会在“操作失败但状态已经改变”的模糊状态中
继续执行。计数从一释放到零时，`became_zero` 才为 `true`，资源所有者应在该
唯一时刻触发最终销毁。

`ScopeRollback::TryPush` 在容量不足时拒绝登记动作，不会覆盖已有记录。
`Rollback` 会执行所有已登记动作并汇总首个失败状态；`Commit` 则清空待撤销
动作并永久关闭当前事务。析构函数只作为安全网调用回滚，正常控制流仍应显式
调用 `Commit` 或 `Rollback`，使错误能够被上层观察。

## 资源事务模型

典型资源创建路径先取得资源，再立即登记对应撤销动作：

```text
取得 KVA 区间       → 登记释放 KVA
取得物理页帧 0      → 登记释放页帧 0
建立页表映射 0      → 登记撤销映射 0
……
全部步骤完成         → Commit
任一步骤失败         → Rollback（严格逆序）
```

逆序不是编码风格，而是依赖关系要求：映射依赖页帧，页帧对应的栈对象依赖 KVA
描述符。只有先撤销映射、再归还页帧、最后释放虚拟区间，才不会让仍可访问的页表
项指向已经重新分配的物理页。

Foundation 只提供“如何撤销”的机制，不知道页帧、页表或进程是什么。具体资源
所有权和快照比较属于 Kernel 模块；这种依赖方向保证通用原语可以在 host 测试
和 freestanding 目标中复用。

## 测试

- 单元测试覆盖空区间、普通区间、半开边界和最大地址溢出。
- 集成测试验证计划中的 ROM、复位向量、Stage 1 和内核加载区间。
- 随机测试使用固定种子验证 10,000 组合法与溢出输入。
- 生命周期原语单元测试覆盖引用计数边界、登记容量、提交、显式回滚、析构回滚、
  逆序和“某个动作失败后继续回滚”。
- 生命周期随机测试执行 100,000 步固定种子状态机，并逐步与独立参考模型比较。
- freestanding 符号审计验证目标架构及运行时独立性。

地址区间随机测试每组最多验证十项性质，总计执行 100,000 次断言。所有随机
测试的失败输出都包含固定种子和迭代位置。

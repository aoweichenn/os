# Foundation 模块

## 职责

`foundation` 提供固件、引导阶段和内核共同使用的 freestanding 基础类型。当前实现：

- `PhysicalAddress`：显式表达物理地址。
- `ByteCount`：显式表达字节数量。
- `AddressRange`：表达半开地址区间 `[begin, end)`。
- `AddressRangeCreationStatus`：表达区间创建成功或地址溢出。

## 公开接口

公开声明位于：

```text
source/foundation/include/os/foundation/address_range.hpp
```

实现位于：

```text
source/foundation/src/address_range.cpp
```

## 不变量

- 地址值使用 64 位无符号整数。
- 地址区间采用半开语义。
- 空区间不包含任何地址，也不与任何区间重叠。
- 无法用 64 位结束地址表示的区间必须返回 `AddressOverflow`。
- 创建失败时不得修改调用方提供的输出区间。

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

## 测试

- 单元测试覆盖空区间、普通区间、半开边界和最大地址溢出。
- 集成测试验证计划中的 ROM、复位向量、Stage 1 和内核加载区间。
- 随机测试使用固定种子验证 10,000 组合法与溢出输入。
- freestanding 符号审计验证目标架构及运行时独立性。

随机测试每组最多验证十项性质，总计执行 100,000 次断言。失败输出包含固定种子和迭代位置。

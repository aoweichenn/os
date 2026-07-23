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
source/foundation/address_range.cpp
```

## 不变量

- 地址值使用 64 位无符号整数。
- 地址区间采用半开语义。
- 空区间不包含任何地址，也不与任何区间重叠。
- 无法用 64 位结束地址表示的区间必须返回 `AddressOverflow`。
- 创建失败时不得修改调用方提供的输出区间。

## 依赖

目标实现只依赖 freestanding C 头文件 `<stdint.h>`，不依赖 libc、libstdc++、
libc++、异常、RTTI 或动态分配。

## 失败语义

`AddressRange::tryCreate` 在 `begin + size` 超出可表示地址时返回
`AddressRangeCreationStatus::AddressOverflow`。调用方必须检查返回值，不得假定输入合法。

## 测试

- 单元测试覆盖空区间、普通区间、半开边界和最大地址溢出。
- 集成测试验证计划中的 ROM、复位向量、Stage 1 和内核加载区间。
- 随机测试使用固定种子验证 10,000 组合法与溢出输入。
- freestanding 符号审计验证目标架构及运行时独立性。

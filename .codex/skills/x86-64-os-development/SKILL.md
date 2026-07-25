---
name: x86-64-os-development
description: Enforce this repository's architecture, learning, documentation, naming, implementation, testing, and delivery rules whenever planning, designing, coding, reviewing, testing, or documenting the custom x86-64 educational operating system.
---

# x86-64 教学操作系统开发

## 遵守技术边界

将 QEMU 仅作为硬件模拟器。自行实现 ROM 固件、引导程序、模式切换、运行时、内核、驱动、用户空间和文件系统。

固定以下技术选择：

- 使用 x86-64 指令集和 QEMU TCG。
- 使用 freestanding C++20 作为唯一高级语言。
- 使用 NASM Intel 语法编写不可避免的汇编。
- 使用 Clang、LLD、NASM、QEMU 和 GDB 作为开发工具。
- 不使用 SeaBIOS、OVMF、GRUB、Limine、Multiboot 或 QEMU `-kernel` 代替自研启动链。
- 不链接 libc、libstdc++、libc++ 或外部运行时。
- 同时遵守全局 `cpp-engineering-standards`；项目规则更严格时执行项目规则。

## 作为大型学习项目建设

优先保证知识可解释、设计可追溯、实现可验证。不得只追求“运行成功”而跳过原理、失败路径、测试或文档。

实现每个机制时：

1. 写明要解决的问题、硬件依据和设计约束。
2. 拆分最小可验证增量。
3. 实现正常路径和失败路径。
4. 添加主机单元测试或 QEMU 集成测试。
5. 记录调试方法、关键观察和设计取舍。
6. 达到阶段验收标准后再进入下一阶段。

## 维护完整文档

将文档视为正式交付物，与代码在同一提交中更新。逐步维护：

```text
README.md
docs/requirements.md
docs/architecture.md
docs/roadmap.md
docs/building.md
docs/testing.md
docs/debugging.md
docs/glossary.md
docs/adr/
docs/modules/
docs/releases/
```

文档职责：

- `README.md`：项目定位、当前能力和最短构建运行路径。
- `requirements.md`：范围、约束和可验证需求。
- `architecture.md`：启动链、模块边界、依赖方向和关键数据流。
- `roadmap.md`：阶段、依赖和验收标准。
- `building.md`：工具版本、构建产物和复现步骤。
- `testing.md`：测试分层、运行方式和结果判定。
- `debugging.md`：GDB、QEMU 日志、故障定位和常见问题。
- `glossary.md`：统一项目术语。
- `adr/`：记录重要架构决策及其原因。
- `modules/`：记录模块职责、接口、不变量和失败语义。
- `releases/`：保存各里程碑的实现范围、证据和已知限制。

不得让文档长期描述已经失效的实现。改变接口、流程、架构、构建方式或验收行为时同步更新对应文档。

## 统一命名

当前项目名标识使用 `OS`；正式更名时一次性更新项目常量前缀和文档。

使用以下命名规则：

- 类型、类、结构体、枚举和 concepts：`PascalCase`。
- 函数和成员函数：统一使用 `PascalCase`，不得以小写字母开头，也不得用下划线连接
  普通函数名；外部 ABI 强制的入口符号除外。
- 变量和参数：`lowerCamelCase`。
- 命名空间、目录和文件：`lower_snake_case`。
- NASM 标签：`lower_snake_case`。
- 常量：`OS_<模块名>_<功能名>`，模块名和功能名全部大写。
- 类内非静态成员访问：始终显式使用 `this->`。

名称必须完整表达领域含义、单位、地址空间、所有权或状态。避免 `data`、`tmp`、`obj`、`val` 等脱离上下文无法理解的名称。仅允许使用项目词汇表或硬件规范中的通用缩写，例如 GDT、IDT、TSS、CR3 和 LBA。

## 让代码本身表达设计

将“代码即文档”落实为：

- 让模块保持单一职责和清晰依赖方向。
- 让函数名表达动作和结果，让类型名表达领域概念。
- 使用强类型区分物理地址、虚拟地址、端口、页号、字节数和状态。
- 使用 RAII 表达锁、中断状态和资源生命周期。
- 使用返回类型表达错误，不隐藏失败。
- 使用 `constexpr`、concepts、枚举和类型系统表达约束。
- 保持函数聚焦，避免依赖长注释才能理解的控制流。
- 让接口暴露必要语义，不泄露实现细节。

注释使用清晰中文，重点解释原因、硬件约束、不变量和非直观行为；不得逐句翻译代码。代码、注释和外部文档必须互相一致。

## 控制宏和历史写法

优先使用现代 C++20 特性。不得用宏定义常量、函数、类型、日志流程或泛型逻辑。仅在编译边界或编译器接口确实无法替代时使用宏，并集中管理、添加项目前缀和中文说明。

在 freestanding 环境中使用现代特性前检查生成代码，不得引入未实现的运行库符号、异常、RTTI、隐藏分配或不可控的全局初始化。

## 完成标准

只有同时满足以下条件才完成任务：

- 干净环境能够复现构建。
- 编译零警告并通过相关测试。
- 正常路径、边界条件和失败路径均有验证。
- 命名、常量、成员访问、注释和头源分离符合规范。
- 没有不必要的宏或历史 C++ 写法。
- 代码无需依赖解释表面行为的注释即可读懂。
- 需求、架构、模块、测试和调试文档已同步更新。
- `main` 分支仍可构建、启动并运行既有回归测试。

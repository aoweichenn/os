# 代码走读指南

项目提供独立的在线代码阅读器：

```text
https://x86-64-os-lab.aoweichenn.chatgpt.site/code/
```

它不是 GitHub 文件列表的复制品，而是围绕学习顺序组织的源码入口。

## 阅读器提供什么

- 按真实目录层级展示项目文件。
- 使用 `/` 快捷键搜索文件路径。
- 每个文件拥有可分享的独立 URL。
- 源码按语言高亮并显示可定位行号。
- 支持一键复制路径和完整源码。
- 支持前后文件连续阅读。
- 关键文件附带中文角色说明和阅读关注点。
- 首页提供跨文件的推荐阅读顺序。

## 收录边界

构建脚本只扫描明确允许的项目目录：

```text
.github/
app/
components/
docs/
lib/
scripts/
site/
source/
tests/
```

同时收录 CMake、TypeScript、包配置和格式规范等根目录文件。

以下内容不会发布：

- `.openai/` 部署元数据。
- `.codex/` 内部辅助配置。
- `node_modules/` 外部依赖源码。
- `build/`、`.next/`、`.open-next/` 和 `out/` 生成产物。
- `package-lock.json` 依赖锁定明细。
- 超过目录生成器上限的异常大文件。

这个边界同时解决“完整展示项目代码”和“不能把部署信息或生成垃圾公开出去”两个问题。

## 推荐路线一：理解工程如何运行

### 1. `scripts/build_and_test.sh`

先看项目对开发者暴露的最短入口。它只负责编排工具链检查、CMake 配置、构建和 CTest。

[在线阅读](/code/scripts/build_and_test.sh/)

### 2. `scripts/check_toolchain.sh`

理解项目为何把 Clang、LLD、NASM、QEMU、GDB 和 LLVM 检查工具都视为显式依赖。

[在线阅读](/code/scripts/check_toolchain.sh/)

### 3. `CMakePresets.json`

观察统一构建目录、编译器和 Debug 配置如何被命名为可复用 preset。

[在线阅读](/code/CMakePresets.json/)

### 4. `CMakeLists.txt`

最后进入完整构建图，理解 host 测试目标、x86-64 freestanding
目标、空镜像和测试子目录之间的关系。

[在线阅读](/code/CMakeLists.txt/)

## 推荐路线二：理解第一个 C++ 模块

### 1. 先读接口

```text
source/foundation/include/os/foundation/address_range.hpp
```

[在线阅读](/code/source/foundation/include/os/foundation/address_range.hpp/)

重点观察：

- `PhysicalAddress` 与 `ByteCount` 为什么是不同类型。
- `AddressRangeCreationStatus` 如何表达失败。
- 头文件为什么只保留声明。

### 2. 再读实现

```text
source/foundation/address_range.cpp
```

[在线阅读](/code/source/foundation/address_range.cpp/)

重点观察：

- 如何在加法前检查地址溢出。
- 半开区间的包含和重叠条件。
- 创建失败为什么不会修改输出对象。
- 类成员访问如何始终显式使用 `this->`。

### 3. 最后读模块文档

[Foundation 模块文档](/docs/foundation-module/)

文档解释代码背后的不变量、可表示范围和当前限制。

## 推荐路线三：理解测试为什么分层

依次阅读：

1. [单元测试](/code/tests/unit/address_range_test.cpp/)
2. [启动内存布局集成测试](/code/tests/integration/boot_memory_layout_test.cpp/)
3. [固定种子随机测试](/code/tests/randomized/address_range_randomized_test.cpp/)
4. [QEMU 系统测试](/code/tests/system/qemu_hardware_smoke.sh/)

阅读时不要只看“断言了什么”，还要问：

- 这个结论为什么属于当前层？
- 更低一层能否更快、更精确地证明它？
- 哪些事实必须等到真实 QEMU 硬件模型才能验证？
- 失败时能够得到哪些定位信息？

## 推荐路线四：理解网站如何保持纯静态

依次阅读：

1. `scripts/generate_code_catalog.mjs`
2. `lib/code_catalog.ts`
3. `app/code/[...path]/page.tsx`
4. `components/code_directory_tree.tsx`
5. `site/worker.mjs`

源码目录和文件内容在构建期生成，语法高亮也在静态页面生成阶段完成。生产
Worker 不读取 Git、不访问文件系统，也不运行 Next.js 服务端。

## 行号与分享

点击源码行会把地址更新为：

```text
/code/<file-path>/#L42
```

该地址可直接分享，打开后浏览器定位到对应行。行号只是源码阅读锚点，不改变仓库文件。

## 后续演进

随着项目进入固件和内核阶段，代码阅读器会继续增加：

- NASM Intel 汇编高亮。
- 链接脚本阅读路线。
- 从复位向量到 C++ 入口的跨文件调用链。
- 类型、常量和中断向量的交叉引用。
- 每个里程碑对应的“实现—测试—文档”导航。

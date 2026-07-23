# 调试记录

## v0.0：工程基线故障记录

### 静态库符号审计误报

`llvm-nm --undefined-only` 会打印静态库成员名称，即使成员没有未解析符号。审计脚本必须过滤空行和以冒号结尾的成员标题，不能把“命令有输出”直接等价为“存在外部依赖”。

### QEMU 测试脚本语法错误

Bash 的 `[[ ... ]]` 条件表达式不能在比较运算符后随意换行。首次测试在执行 QEMU
前就因脚本语法失败。修复后对全部 Shell 脚本执行 `bash -n`，并保留无效镜像尺寸的失败路径测试。

### 门户扫描 CMake 生成文件

Next.js 的 TypeScript 配置曾扫描 `build/` 中由 CMake 生成的
`compiler_depend.ts`。该文件不是 TypeScript，导致门户构建失败。`tsconfig.json`
现已排除整个 `build/` 目录。

### GitHub Actions 缺少 llvm-nm

Ubuntu runner 中安装 `clang` 和 `lld` 不保证存在未版本化的 `llvm-nm`、
`llvm-readelf` 命令。CI 工作流必须同时安装 `llvm` 工具包。工具链检查脚本应继续在构建前失败，不能静默跳过符号审计。

## 项目门户：Cloudflare Worker 1101

### 现象

公开访问项目门户时，Cloudflare 返回错误 1101，生产 Worker 抛出异常：

```text
ReferenceError: require is not defined
```

### 定位

第一版生产日志显示 Next.js 16 的服务端产物加载了开发期浏览器日志模块。该模块在启动时调用 CommonJS `require("fs")` 和 `require("path")`。

尝试切换到 Next.js 15 后，开发期模块不再存在，但 Next 服务端初始化 `node-crypto` 时仍需 CommonJS `require`。由此确认根因是：静态项目门户错误地部署了完整 Next 服务端，而 Sites 的 ESM Worker 运行环境不提供这些 CommonJS 调用。

### 处理

- 使用 Next.js 静态导出生成 HTML、CSS 和浏览器脚本。
- 使用原生 ESM Worker 将所有请求交给 Cloudflare 静态资源绑定。
- 生产 Worker 不再包含 Next 服务端、Node.js 运行时或 CommonJS `require`。
- 重新执行生产依赖审计、Worker 构建和公网 HTTP 冒烟测试。

### 预防

- 升级 Next.js 后必须重新生成静态 Worker，而不能只验证本地 Next.js 服务。
- 发布后必须从公开 URL 请求根路径和图标资源。
- 生产异常优先按 Ray ID 查询 Worker 日志，再根据真实调用栈修复。

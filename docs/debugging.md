# 调试记录

## 项目门户：Cloudflare Worker 1101

### 现象

公开访问项目门户时，Cloudflare 返回错误 1101，生产 Worker 抛出异常：

```text
ReferenceError: require is not defined
```

### 定位

生产日志显示 Next.js 16 的服务端产物加载了开发期浏览器日志模块。该模块在启动时调用 CommonJS `require("fs")` 和 `require("path")`，而部署目标使用 ESM Worker 运行环境。

### 处理

- 将 Next.js 固定到 OpenNext 明确支持的 `15.5.21`。
- 保留 React 19 和当前 OpenNext Cloudflare 适配器。
- 重新生成并检查 Worker bundle，确认开发期浏览器日志模块不再存在。
- 重新执行生产依赖审计、Worker 构建和公网 HTTP 冒烟测试。

### 预防

- 升级 Next.js 或 OpenNext 后必须重新构建 Worker，而不能只验证本地 Next.js 服务。
- 发布后必须从公开 URL 请求根路径和图标资源。
- 生产异常优先按 Ray ID 查询 Worker 日志，再根据真实调用栈修复。

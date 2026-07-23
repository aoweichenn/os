# x86-64 OS Lab

这是一个从 CPU 复位向量开始自研的 x86-64 教学操作系统项目。QEMU 仅用于模拟硬件；固件、引导程序、模式切换、内核、运行时、驱动、用户空间和文件系统均由项目自行实现。

当前状态：工程规划与项目门户建设。

## 项目门户

本地开发：

```bash
npm install
npm run dev
```

生产构建：

```bash
npm run build
npm run build:worker
```

项目门户采用 Next.js 静态导出，生产 Worker 仅负责转发静态资源请求，不依赖 Node.js 服务端运行时。

门户按主题拆分为独立路由：

- `/`：项目首页与全局概览
- `/architecture/`：自研启动链与模块交接契约
- `/roadmap/`：13 个开发阶段及其验收标准
- `/engineering/`：C++20、测试与交付规范
- `/docs/`：项目文档索引与维护规则

## 固定技术路线

- x86-64
- QEMU TCG
- freestanding C++20
- NASM Intel 语法
- Clang、LLD、GDB

项目不使用 SeaBIOS、OVMF、GRUB、Limine、Multiboot 或 QEMU `-kernel` 替代自研启动链。

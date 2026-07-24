# 从复位向量到自研操作系统

本目录是 x86-64 OS Lab 的正式 LaTeX 教材工程。书稿不是项目文档的汇编，
而是沿“硬件契约 → 二进制布局 → 启动链 → 内核机制 → 用户环境”的学习顺序，
解释为什么这样设计、代码如何落地、证据如何形成。

## 目录

- `source/latex/main.tex`：全书入口。
- `source/latex/chapters/`：十个完整主题章的入口，负责组织一章的学习主线。
- `source/latex/topics/`：主题章中的基础材料，记录背景、历史和已有实现。
- `source/latex/deepening/`：机制级深入内容，记录状态转换、数据结构、失败路径
  和验证方法。
- `source/latex/frontmatter/`：书名页与前言。
- `source/latex/backmatter/`：验收清单与参考资料。
- `source/latex/preamble/`：排版、颜色和教材环境。
- `source/latex/scripts/check_inputs.py`：书稿输入图检查。

## 构建

```bash
make check
make pdf
make phone-export
```

`make check` 更新生产目标代码统计，并检查所有 `\input` 文件、章节数量、
XeLaTeX 和 latexmk；`make pdf` 生成 `source/latex/main.pdf`。
`make phone-export` 重新构建 PDF，并导出到手机书库的独立目录
`按卷类型/原理卷/从复位向量到自研x8664操作系统/`。

生产代码统计只扫描仓库根目录的 `source/`，计入 `.asm`、`.cpp`、`.hpp`、
`.inc` 和 `.tpp` 中的非空、非纯注释行。测试、宿主工具、书稿、网站、构建
描述和链接脚本均不计入该数字。

当前书稿为 5 部 10 个完整主题章、91 页。每章遵循同一解释深度：先建立历史
背景和前置状态，再展开寄存器、位布局、数据结构或控制流，随后说明失败模式、
调试方法与可重复验收证据。小主题不会独立占用一章，而是作为主题材料进入一条
连续的因果链。

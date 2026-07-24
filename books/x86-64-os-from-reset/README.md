# 从复位向量到自研操作系统

本目录是 x86-64 OS Lab 的正式 LaTeX 教材工程。书稿不是项目文档的汇编，
而是沿“硬件契约 → 二进制布局 → 启动链 → 内核机制 → 用户环境”的学习顺序，
解释为什么这样设计、代码如何落地、证据如何形成。

## 目录

- `source/latex/main.tex`：全书入口。
- `source/latex/chapters/`：按硬件基础、汇编与二进制、启动实现、内核机制和
  工程度量组织的正文。
- `source/latex/frontmatter/`：书名页与前言。
- `source/latex/backmatter/`：验收清单与参考资料。
- `source/latex/preamble/`：排版、颜色和教材环境。
- `source/latex/scripts/check_inputs.py`：书稿输入图检查。

## 构建

```bash
make check
make pdf
```

`make check` 更新生产目标代码统计，并检查所有 `\input` 文件、章节数量、
XeLaTeX 和 latexmk；`make pdf` 生成 `source/latex/main.pdf`。

生产代码统计只扫描仓库根目录的 `source/`，计入 `.asm`、`.cpp`、`.hpp`、
`.inc` 和 `.tpp` 中的非空、非纯注释行。测试、宿主工具、书稿、网站、构建
描述和链接脚本均不计入该数字。

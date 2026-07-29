# 从复位向量到自研操作系统

普通程序可以从 `main()` 开始，因为操作系统已经准备好了内存、栈和运行
环境。操作系统自己没有这些条件。CPU 刚刚复位时，只会从硬件规定的位置读取
第一条指令。

这本书从那条指令开始。配套项目自行实现 ROM、Stage 1、长模式转换、
freestanding C++20 内核、驱动、用户程序和文件系统；QEMU 只负责模拟
x86-64 CPU 和外围硬件。书中的代码都来自仓库根目录 `source/` 下的同一个
可运行项目。

前四章不要求读者学过电路或计算机组成。从 bit、byte 和地址开始，依次介绍
电压、引脚、逻辑门、触发器、寄存器、教学 CPU、RAM、ROM、总线和第一次
取指。正文保留数制手算、电路公式、波形、setup/hold、基础 CDC、逐周期指令、
总线事务和项目连接；测量误差、器件选型、板级工程、高级 CDC、DRAM 训练与
现代平台等参考材料留在硬件深化附录。后续章节沿项目实际执行顺序加入串口、
磁盘、分页、异常、中断、进程、文件系统和用户环境。

## 构建

在本目录运行：

```bash
make check
make pdf
make phone-export
```

- `make check` 检查 LaTeX 输入、章节结构、矢量图片和实线表格。
- `make pdf` 生成 `source/latex/main.pdf`。
- `make phone-export` 重新检查并把同一份 PDF 复制到手机书库。

构建需要 XeLaTeX、latexmk 和 Python 3。项目仍可生成维护用源码统计，但这些
数字不进入教材正文。

## 目录

- `source/latex/main.tex`：全书入口与阅读顺序。
- `source/latex/chapters/`：31 个主线章和 4 个硬件附录的入口文件。
- `source/latex/foundations/`：前四章的完整硬件正文。
- `source/latex/foundations/expanded/`：由正文或附录按依赖顺序引入的细化材料。
- `source/latex/foundations/mainline/`：保留的简明素材，不直接作为成书入口。
- `source/latex/topics/`：进入机制前需要的背景材料。
- `source/latex/deepening/`：代码走读、状态变化和实验记录。
- `source/latex/figures/`：原理图、结构图和连线图。
- `source/latex/frontmatter/`：前言与阅读路线。
- `source/latex/backmatter/`：实验、术语、检查清单和参考资料。
- `STRUCTURE.md`：章节依赖、素材归属和排版边界。
- `EDITORIAL.md`：书稿的文字编辑与技术审校方法。

## 阅读方式

第一次阅读可以按目录顺序进行。每完成一个阶段，先运行对应实验并记录输出，
再回到代码中追踪那条输出经过的汇编、C++ 和硬件状态。遇到地址、位字段或
容量公式时，建议停下来手算一次。

只想先看到系统启动时，可以从“CPU 怎样读到第一条指令”读到“从磁盘进入
64 位内核”；遇到电气或逻辑概念再回看前四章。想学习进程、虚拟内存和文件
系统，则需要先理解异常、中断、页表和用户态入口。

书稿的编辑原则是先讲问题和实验，再引出抽象概念。版本发布、PDF 哈希和网站
同步属于维护流程，不放进面向初学者的正文。

正文、标题和代码统一使用 Maple Mono NF CN；字号、字重、颜色、间距和
代码框负责建立阅读层级。

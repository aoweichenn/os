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

生产代码统计只扫描仓库根目录的 `source/`，计入核心 `.asm`、`.cpp`、
`.hpp` 中的非空、非纯注释行。汇编 include、测试、宿主工具、书稿、网站、构建
描述和链接脚本均不计入该数字。

当前书稿为 5 部 10 个完整主题章、173 页。当前生产目标包含 101 个核心
`.asm`、`.cpp`、`.hpp` 文件，共 16,435 行有效代码；该口径严格排除测试、
宿主工具、书稿和网站。每章遵循同一解释深度：先建立历史
背景和前置状态，再展开寄存器、位布局、数据结构或控制流，随后说明失败模式、
调试方法与可重复验收证据。小主题不会独立占用一章，而是作为主题材料进入一条
连续的因果链。当前实现内容与 v1.0 对齐：中断/设备/用户边界章包含用户 ELF、
四级 U/S 权限、TSS.RSP0、五项 IRETQ 帧、INT 0x80 ABI、用户指针复制和
异常隔离；进程章进一步完整展开 PCB、独立 CR3、每进程 Ring 0 栈、176
字节保存现场、round-robin 抢占、退出回收和多进程整机证据，并深入解释
Blocked/Ready 状态转换、丢失唤醒、x86 原子操作与 C++ 内存顺序、64 字节
环形管道、持久文件系统、统一 fd、idle 和交互式 Shell。第十章进一步冻结
ADR 0019 的 v2 演进基线：单 BSP 内核执行模型、Process/Thread/CpuLocal、
双系统调用入口、WaitQueue 单赢家、匿名与文件 VM、COW、private futex、
TTY、异步块层、ordered metadata journal、三档容量和测试频率。
第七章已同步 v1.1 可回收内核堆，完整解释 boundary tag、best-fit、对齐
分裂、双向合并、统计守恒和十万步固定种子验证；同章还从 PFN 二进制结构、
XOR 伙伴、双位图尺寸、E820 分解、范围申请、错阶释放、完整校验到
64 MiB/64 GiB QEMU 首尾写回，展开已实现的 buddy 页帧分配器。最新增补的
固定大小类型缓存继续解释 slab 思想的历史来源、位图与槽内空闲链表的分工、
单次堆后备布局、精确指针验证、常数时间申请/释放、失败原子性、销毁约束，
以及单元、集成、十万步随机测试和 QEMU 整机自检如何共同形成证据链。

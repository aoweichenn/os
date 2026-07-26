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

当前书稿为 5 部 10 个完整主题章、225 页。当前生产目标包含 134 个核心
`.asm`、`.cpp`、`.hpp` 文件，共 25,681 行有效代码；该口径严格排除测试、
宿主工具、书稿和网站。每章遵循同一解释深度：先建立历史
背景和前置状态，再展开寄存器、位布局、数据结构或控制流，随后说明失败模式、
调试方法与可重复验收证据。小主题不会独立占用一章，而是作为主题材料进入一条
连续的因果链。当前实现内容与 v1.4 对齐：中断/设备/用户边界章包含用户 ELF、
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
以及单元、集成、十万步随机测试和 QEMU 整机自检如何共同形成证据链。KVA
增量进一步区分 not-present 与 free，逐项展开 32 TiB 高半区布局、PML4 槽、
逐页位图成本、有序描述符、绝对页对齐、best-fit、精确释放、TLB 回收顺序，
以及双 guard 的跨 KVA/buddy/页表整机事务。最新动态内核栈增量继续解释
TSS.RSP0 的历史与硬件职责、六页双 guard 布局、虚拟连续与物理离散、
四帧事务回滚、精确 KVA 所有权、进程 CR3 高半区共享、176 字节首次现场，
以及为何必须回到永久启动栈安全点后才能撤映射、清零并回收。页表回收主节
又从硬件四级遍历与 PML4 共享讲到三种根所有权、逐字空表判定、两阶段撤销、
映射失败事务回滚、进程递归销毁与十万步独立层级模型。v1.1 收束内容继续
解释不可复活强引用、固定存储 ScopeRollback、九动作动态栈事务、26 字段稳定
资源快照和所有权守恒方程。v1.2 进一步把 Process 收束为资源容器、Thread
确立为唯一调度实体，完整展开独立 PID/TID、侵入式运行/等待队列、WaitQueue
单赢家唤醒、SpinLock/IrqSaveSpinLock/Mutex 的分层职责，以及每 Thread
512 字节、16 字节对齐的 FXSAVE64/FXRSTOR64 x87/SSE2 现场。v1.3 新增
CPUID 处理器规格门禁、CpuLocal、STAR/LSTAR/FMASK/GS MSR 配置、
SWAPGS 可信换栈、统一 176 字节 UserContext、原生 SYSCALL/SYSRET、
IRET 安全回退、系统调用期间 IRQ 与返回前延迟调度，并保留 INT 0x80 作为
差分基准。v1.4 继续完整展开带 generation 的类型化 KernelObject、不可复制
临时 Reference、长期 Handle、共享 FileDescription、64 槽动态 FileTable、
descriptor/file status flags 分层、soft/hard limit、两阶段安装、最后引用
析构和 PID 4 的真实 Ring 3 证明。完整回归现为 106 项 CTest；动态栈、页表
回收、资源快照、Thread 调度、双系统调用入口、文件表十万步模型、256 MiB
functional、64 GiB capacity、缺失 SSE2 与缺失 SYSCALL 的 QEMU 路径共同
证明容量、隔离、失败边界与最终回收。

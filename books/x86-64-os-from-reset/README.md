# 从复位向量到自研操作系统

本目录是 x86-64 OS Lab 的正式 LaTeX 教材工程。书稿不是项目文档的汇编，
而是沿“硬件契约 → 二进制布局 → 启动链 → 内核机制 → 用户环境”的学习顺序，
解释为什么这样设计、代码如何落地、证据如何形成。读者不需要预先学习硬件史
或数字电路；第一章从 bit、byte、电压、电流、地址、CPU、寄存器、ROM、RAM
和磁盘开始，每个重要概念都沿“是什么、为什么出现、怎样工作、由谁初始化、
失败怎样观察”的链条展开。

## 目录

- `source/latex/main.tex`：全书入口。
- `source/latex/chapters/`：十个完整主题章的入口，负责组织一章的学习主线。
- `source/latex/topics/`：主题章中的基础材料，记录背景、历史和已有实现。
- `source/latex/deepening/`：机制级深入内容，记录状态转换、数据结构、失败路径
  和验证方法。
- `source/latex/frontmatter/`：书名页与前言。
- `source/latex/backmatter/`：详细术语表、验收清单与参考资料。
- `source/latex/figures/`：全机链路、实体载板连线和原始参考原理图的矢量 PDF。
- `source/latex/preamble/`：排版、颜色和教材环境。
- `source/latex/scripts/check_inputs.py`：书稿输入、章节数量和图片资源检查。

## 构建

```bash
make check
make pdf
make phone-export
```

`make check` 更新生产目标代码统计，并检查所有 `\input` 文件、图片资源、
章节数量、XeLaTeX 和 latexmk；检查脚本会拒绝书稿直接引用 PNG/JPG 位图，
并拒绝生成的学习图 PDF 内嵌栅格 image XObject。
`make pdf` 生成 `source/latex/main.pdf`。
`make phone-export` 重新构建 PDF，并导出到手机书库的独立目录
`按卷类型/原理卷/从复位向量到自研x8664操作系统/`。

生产代码统计只扫描仓库根目录的 `source/`，计入核心 `.asm`、`.cpp`、
`.hpp` 中的非空、非纯注释行。汇编 include、测试、宿主工具、书稿、网站、构建
描述和链接脚本均不计入该数字。

当前书稿为 5 部 10 个完整主题章。当前生产目标包含 167 个核心
`.asm`、`.cpp`、`.hpp` 文件，共 40,712 行有效代码；该口径严格排除测试、
宿主工具、书稿和网站。每章遵循同一解释深度：先建立历史
背景和前置状态，再展开寄存器、位布局、数据结构或控制流，随后说明失败模式、
调试方法与可重复验收证据。小主题不会独立占用一章，而是作为主题材料进入一条
连续的因果链。

全书已把 `docs/learning` 的总路线、全机连线、启动与内存、Port I/O、
中断路由、键盘到 Shell、存储持久化 7 张系统图逐张纳入相应章节；附录再把
00--13 阶段、B1--B7 背景、硬件装配文档和每张图映射到正文落点，避免资料
只是复制进书却没有进入解释链。7 张宽图均使用独立横向整页，正文逐线说明
实物电气连接、虚拟平台边界、控制流与数据/所有权流。

前两章另加入一个完整实体硬件学习案例：LattePanda Mu N100/N305 计算模块与
DFR1142 Lite Carrier V2。正文直接嵌入 10 页真实 KiCad 参考原理图、1 页
纯 TikZ 模块正反面与 260 触点方向图，以及 3 张简化但不省连接的矢量图，
逐根解释三路电源输入、VDC 汇流、
12 V/5 V/3.3 V 电源树、260-pin 模块接口、HDMI、USB、PCIe x4、M.2、
RTL8111H 千兆网、I2C、UART、SPI、RTC、风扇和四层 PCB。章节同时明确
QEMU 自研复位 ROM 与实体 UEFI 平台的边界，并列出 ACPI、PCIe 枚举、NVMe、
GOP/xHCI 等实机适配缺口，不声称当前内核已经能在该板裸机启动。

以上合计 21 页系统/硬件图面：7 页系统链路、10 页参考原理图、3 页实体连线
和 1 页模块方向图。外部插图统一为矢量 PDF，模块方向图由 TikZ 直接绘制；
因此 PDF 放大时线条与文字不会按位图像素变糊。宽图同时限制最大宽高并保持
纵横比，最终构建还要逐页检查文字、框、连线、图注和纸张边界没有重叠或裁切。
十页上游 KiCad PDF 原样保留其小型标题标识图像，但原理图符号、文字、网络和
导线本身均为矢量；其余十张外部学习图经对象扫描确认不含 image XObject。

当前实现内容与 v1.9 对齐：中断/设备/用户边界章包含用户 ELF、
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
析构和 PID 4 的真实 Ring 3 证明。v1.5 又完整展开 Vnode、Path、Superblock、
Mount、每 Process FsContext、逐组件路径状态机、挂载进入/退出、反向
getcwd、memfs 节点与数据事务、legacy 旧格式适配、目录项 ABI、持久 VFS
资源分账，以及双后端契约、十万步命名空间模型和真实 Shell `cd` 证明。
v1.6 继续深入逻辑 1 GiB 稀疏盘、256 MiB rootfs、冻结小端盘面、8192 个
inode、八个 direct 与 single/double/triple 索引、64 MiB 稀疏文件、
truncate 反向释放、完整 unlink/rmdir/rename/stat、真实短写和 ENOSPC、
Dirty/Data/Clean 失败边界、独立 mkfs/fsck、同种子双后端十万步与 QEMU
跨启动损坏拒绝。v1.7 再完整展开 PID1 的历史与最小职责、ProcessTree
状态机、Zombie 与孤儿重归属、ATA→rootfs→VFS→ELF→CR3 的生产启动链、
按偏移读取的两遍 ELF、256 KiB `argc/argv/envp` 栈、spawn 多资源事务、
exec 候选映像与唯一提交点、close-on-exec、wait 唯一回收和八进程验收树。
v1.8 在同章继续解释 VMA 与 PTE 为何分别表示区间意图和驻留事实、8192
描述符共享池、排序/合并/拆分、匿名 first-gap、零填充按需缺页、8 MiB
连续增长栈和永久 guard、program break、撤映射时的数据页/空页表回收，以及
Ring 3 UserHeap 的 boundary tag、first-fit、split 和双向 coalesce。
VMA 与 UserHeap 各用十万步固定种子模型验证，128 轮组合测试连接真实页表
和 frame；三个磁盘用户 probe 再验证 32 MiB 稀疏映射、2 MiB break、
递归栈、5000 步分配与 vector 14 保护边界。
v1.9 在同章继续建立稳定文件 identity、FileBacking generation、文件 VMA
offset 几何、按需 ELF、文件尾/BSS 零填充、有界 clean page cache、共享
引用与 LRU、只读 shared、可写 private，以及 write/truncate 撤销 PTE 后
失效 cache 的完整控制流；单元、共享页集成、两个十万步模型和三档 QEMU
共同证明生命周期闭环。
正常 Kernel 不再内嵌普通用户程序；十七个合法用户 ELF 逐个审计后由离线
安装器写入 rootfs，截断 ELF 只作为明确失败夹具。完整回归由当前构建图自动
产生；动态栈、VMA、按需分页、页表
回收、资源快照、Thread 调度、双系统调用入口、文件表十万步模型、256 MiB
functional、64 GiB capacity、缺失 SSE2 与缺失 SYSCALL 的 QEMU 路径共同
证明容量、隔离、失败边界与最终回收。

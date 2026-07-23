export const projectFacts = [
  ["目标架构", "x86-64"],
  ["高级语言", "C++20"],
  ["汇编语法", "NASM / Intel"],
  ["硬件模型", "QEMU TCG"],
] as const;

export const bootStages = [
  {
    id: "01",
    title: "CPU RESET",
    detail: "0xFFFFFFF0",
    note: "CPU 从我们自己的 ROM 复位向量开始执行。",
  },
  {
    id: "02",
    title: "FIRMWARE",
    detail: "16-bit",
    note: "初始化串口、A20，并从 IDE 读取 Stage 1。",
  },
  {
    id: "03",
    title: "STAGE 1 / 2",
    detail: "16 → 32 → 64",
    note: "建立 GDT 与页表，亲手进入 Long Mode。",
  },
  {
    id: "04",
    title: "KERNEL",
    detail: "ELF64",
    note: "解析内核镜像，交接启动信息并进入 C++20。",
  },
] as const;

export const roadmap = [
  {
    version: "v0.0",
    title: "工程基线",
    summary: "建立可复现的构建、测试、持续集成和文档结构。",
    acceptance: ["一条命令完成构建", "空镜像自动测试通过", "开发环境文档可复现"],
  },
  {
    version: "v0.1",
    title: "复位与串口",
    summary: "自定义 ROM 从 CPU 复位向量开始执行并输出串口日志。",
    acceptance: ["复位地址可由反汇编验证", "串口输出稳定可捕获", "失败时 QEMU 自动退出"],
  },
  {
    version: "v0.2",
    title: "固件加载",
    summary: "通过 IDE PIO 从磁盘加载自研 Stage 1。",
    acceptance: ["识别目标磁盘", "校验读取扇区内容", "读盘错误具有明确诊断"],
  },
  {
    version: "v0.3",
    title: "Long Mode",
    summary: "独立完成 16 位实模式到 32 位保护模式，再到 64 位长模式的切换。",
    acceptance: ["GDT 与页表结构可检查", "EFER 与控制寄存器状态正确", "64 位串口标记输出"],
  },
  {
    version: "v0.4",
    title: "内核加载",
    summary: "校验并解析 ELF64 内核，将启动信息交给 C++ 入口。",
    acceptance: ["拒绝损坏 ELF 镜像", "正确加载多个段", "BootInfo 契约有版本与测试"],
  },
  {
    version: "v0.5",
    title: "内核基础",
    summary: "建立内核 GDT、IDT、TSS、异常处理和 panic 机制。",
    acceptance: ["异常向量全覆盖", "错误码与寄存器可诊断", "panic 输出可用于回归"],
  },
  {
    version: "v0.6",
    title: "内存管理",
    summary: "实现物理页分配、虚拟地址空间管理和内核堆。",
    acceptance: ["检测内存重叠与越界", "页表映射可查询", "分配器压力测试通过"],
  },
  {
    version: "v0.7",
    title: "中断与设备",
    summary: "驱动 PIC、PIT、PS/2 键盘和 IDE，形成统一设备边界。",
    acceptance: ["定时中断持续运行", "键盘输入可解码", "块设备读写回归通过"],
  },
  {
    version: "v0.8",
    title: "用户边界",
    summary: "进入 Ring 3，加载用户 ELF 并实现受控系统调用。",
    acceptance: ["用户代码无法访问内核页", "非法系统调用被拒绝", "用户异常不拖垮内核"],
  },
  {
    version: "v0.9",
    title: "进程调度",
    summary: "实现抢占式调度、上下文切换和完整进程生命周期。",
    acceptance: ["多个进程公平运行", "退出与回收无泄漏", "调度状态可追踪"],
  },
  {
    version: "v0.10",
    title: "同步与 IPC",
    summary: "实现锁、睡眠与唤醒机制，以及进程间管道。",
    acceptance: ["临界区压力测试通过", "无丢失唤醒", "管道读写与关闭语义明确"],
  },
  {
    version: "v0.11",
    title: "文件系统",
    summary: "实现 inode、目录、普通文件和磁盘持久化。",
    acceptance: ["断电重启后数据存在", "路径解析覆盖异常情况", "一致性检查可发现损坏"],
  },
  {
    version: "v1.0",
    title: "用户环境",
    summary: "提供 Shell、基础命令和覆盖整条启动链的系统回归。",
    acceptance: ["Shell 可交互执行命令", "基础工具形成最小闭环", "全量自动回归稳定通过"],
  },
] as const;

export const engineeringRules = [
  {
    tag: "TYPE",
    title: "现代 C++",
    text: "优先强类型、RAII、constexpr、concepts 与明确的错误类型。",
  },
  {
    tag: "ZERO",
    title: "零外部运行时",
    text: "不链接 libc、libstdc++、libc++；所需运行时全部自研。",
  },
  {
    tag: "DOCS",
    title: "文档即交付物",
    text: "需求、架构、ADR、测试与调试记录必须和代码同步演进。",
  },
  {
    tag: "TEST",
    title: "每步可验证",
    text: "每个阶段都有正常路径、失败路径、自动测试和退出标准。",
  },
] as const;

export const documents = [
  ["需求", "范围、约束、可验证目标", "docs/requirements.md"],
  ["架构", "启动链、模块边界、数据流", "docs/architecture.md"],
  ["路线图", "阶段、依赖、验收标准", "docs/roadmap.md"],
  ["测试", "单元、集成、系统回归", "docs/testing.md"],
  ["调试", "GDB、QEMU 日志、故障档案", "docs/debugging.md"],
  ["决策", "重要取舍和历史背景", "docs/adr/"],
] as const;

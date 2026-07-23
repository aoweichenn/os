const projectFacts = [
  ["目标架构", "x86-64"],
  ["高级语言", "C++20"],
  ["汇编语法", "NASM / Intel"],
  ["硬件模型", "QEMU TCG"],
] as const;

const bootStages = [
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

const roadmap = [
  ["v0.0", "工程基线", "构建、测试、CI、文档结构"],
  ["v0.1", "复位与串口", "自定义 ROM 从复位向量打印日志"],
  ["v0.2", "固件加载", "IDE PIO 加载自研 Stage 1"],
  ["v0.3", "Long Mode", "独立完成 16 → 32 → 64 位切换"],
  ["v0.4", "内核加载", "校验并解析 ELF64 内核"],
  ["v0.5", "内核基础", "GDT、IDT、TSS、异常与 panic"],
  ["v0.6", "内存管理", "物理页、页表与内核堆"],
  ["v0.7", "中断设备", "PIC、PIT、键盘与 IDE"],
  ["v0.8", "用户边界", "Ring 3、用户 ELF 与系统调用"],
  ["v0.9", "进程调度", "抢占调度与进程生命周期"],
  ["v0.10", "同步 IPC", "锁、睡眠唤醒与管道"],
  ["v0.11", "文件系统", "inode、目录、文件与持久化"],
  ["v1.0", "用户环境", "Shell、基础命令与完整回归"],
] as const;

const engineeringRules = [
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

const documents = [
  ["需求", "范围、约束、可验证目标", "requirements.md"],
  ["架构", "启动链、模块边界、数据流", "architecture.md"],
  ["路线图", "阶段、依赖、验收标准", "roadmap.md"],
  ["测试", "单元、集成、系统回归", "testing.md"],
  ["调试", "GDB、QEMU 日志、故障档案", "debugging.md"],
  ["决策", "重要取舍和历史背景", "docs/adr/"],
] as const;

function ArrowMark() {
  return (
    <svg aria-hidden="true" viewBox="0 0 20 20">
      <path d="M3 10h12M11 5l5 5-5 5" />
    </svg>
  );
}

export default function HomePage() {
  return (
    <main>
      <nav className="topbar">
        <a className="brand" href="#top" aria-label="返回顶部">
          <span className="brandMark">OS</span>
          <span>x86-64 LAB</span>
        </a>
        <div className="navLinks">
          <a href="#architecture">启动链</a>
          <a href="#roadmap">路线图</a>
          <a href="#engineering">工程规范</a>
        </div>
        <span className="statusPill">
          <i />
          PLANNING
        </span>
      </nav>

      <section className="hero" id="top">
        <div className="heroGrid">
          <div className="heroCopy">
            <div className="eyebrow">
              <span>BUILDING FROM RESET VECTOR</span>
              <span className="line" />
            </div>
            <h1>
              不借启动器。
              <br />
              <em>从第一条指令开始。</em>
            </h1>
            <p className="heroLead">
              一个以软件工程方式推进的 x86-64
              教学操作系统。从固件、模式切换到内核、用户态和文件系统，每一层都亲手实现。
            </p>
            <div className="heroActions">
              <a className="primaryButton" href="#roadmap">
                查看开发路线
                <ArrowMark />
              </a>
              <a className="textButton" href="#engineering">
                阅读工程原则
              </a>
            </div>
          </div>

          <div className="terminal" aria-label="启动终端预览">
            <div className="terminalBar">
              <div>
                <span />
                <span />
                <span />
              </div>
              <code>serial0 — 115200 baud</code>
            </div>
            <div className="terminalBody">
              <p>
                <b>qemu-system-x86_64</b>
                <span> -machine pc,accel=tcg</span>
              </p>
              <p className="terminalMuted">[cpu] reset @ 0xFFFFFFF0</p>
              <p>
                <strong>[firmware]</strong> reset vector reached
              </p>
              <p>
                <strong>[firmware]</strong> serial initialized
              </p>
              <p>
                <strong>[stage1]</strong> waiting for first milestone...
              </p>
              <div className="terminalRule" />
              <p className="cursorLine">
                <span>_</span>
              </p>
            </div>
          </div>
        </div>

        <div className="factStrip">
          {projectFacts.map(([label, value]) => (
            <div className="fact" key={label}>
              <span>{label}</span>
              <strong>{value}</strong>
            </div>
          ))}
        </div>
      </section>

      <section className="section architecture" id="architecture">
        <div className="sectionHeading">
          <div>
            <span className="sectionIndex">01 / ARCHITECTURE</span>
            <h2>我们自己拥有整条启动链</h2>
          </div>
          <p>
            QEMU 只负责模拟硬件。ROM 固件、磁盘加载、模式切换和 ELF
            解析都属于项目代码。
          </p>
        </div>

        <div className="bootFlow">
          {bootStages.map((stage, index) => (
            <article className="bootCard" key={stage.id}>
              <div className="bootCardTop">
                <span>{stage.id}</span>
                <code>{stage.detail}</code>
              </div>
              <h3>{stage.title}</h3>
              <p>{stage.note}</p>
              {index < bootStages.length - 1 && (
                <span className="flowArrow" aria-hidden="true">
                  →
                </span>
              )}
            </article>
          ))}
        </div>

        <div className="boundaryNote">
          <span>PROJECT BOUNDARY</span>
          <p>
            NO SeaBIOS · NO OVMF · NO GRUB · NO Limine · NO Multiboot · NO
            QEMU -kernel
          </p>
        </div>
      </section>

      <section className="section roadmapSection" id="roadmap">
        <div className="sectionHeading">
          <div>
            <span className="sectionIndex">02 / ROADMAP</span>
            <h2>13 个可验收的工程阶段</h2>
          </div>
          <div className="progressBlock">
            <span>当前总进度</span>
            <strong>0 / 13</strong>
            <div className="progressTrack">
              <i />
            </div>
          </div>
        </div>

        <div className="roadmapList">
          {roadmap.map(([version, title, acceptance], index) => (
            <article className={index === 0 ? "roadmapItem active" : "roadmapItem"} key={version}>
              <span className="roadmapNumber">
                {String(index + 1).padStart(2, "0")}
              </span>
              <code>{version}</code>
              <h3>{title}</h3>
              <p>{acceptance}</p>
              <span className="phaseState">
                {index === 0 ? "NEXT" : "QUEUED"}
              </span>
            </article>
          ))}
        </div>
      </section>

      <section className="section engineering" id="engineering">
        <div className="sectionHeading">
          <div>
            <span className="sectionIndex">03 / ENGINEERING</span>
            <h2>学习项目，也按生产工程要求建设</h2>
          </div>
          <p>
            能启动只是开始。设计可追溯、失败可诊断、行为可测试，才算真正掌握。
          </p>
        </div>

        <div className="ruleGrid">
          {engineeringRules.map((rule) => (
            <article className="ruleCard" key={rule.tag}>
              <span>{rule.tag}</span>
              <h3>{rule.title}</h3>
              <p>{rule.text}</p>
            </article>
          ))}
        </div>

        <div className="documentationPanel">
          <div className="documentationIntro">
            <span className="sectionIndex">DOCUMENTATION MATRIX</span>
            <h3>代码表达“是什么”，文档解释“为什么”</h3>
            <p>
              每次架构、接口和行为变化都必须同步更新文档。文档不是补写的总结，而是实现的一部分。
            </p>
          </div>
          <div className="documentList">
            {documents.map(([title, purpose, path]) => (
              <div className="documentRow" key={title}>
                <strong>{title}</strong>
                <span>{purpose}</span>
                <code>{path}</code>
              </div>
            ))}
          </div>
        </div>
      </section>

      <footer>
        <div>
          <span className="brandMark">OS</span>
          <p>
            x86-64 OS LAB
            <br />
            From reset vector to user space.
          </p>
        </div>
        <p className="footerNote">
          当前状态：工程规划
          <br />
          下一目标：v0.0 工程基线
        </p>
      </footer>
    </main>
  );
}


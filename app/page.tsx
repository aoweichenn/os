import Link from "next/link";
import { projectFacts } from "@/lib/project_data";

const routeCards = [
  {
    index: "01",
    href: "/architecture/",
    title: "启动架构",
    description: "查看从 CPU 复位向量、ROM 固件到 ELF64 内核的完整自研启动链。",
  },
  {
    index: "02",
    href: "/roadmap/",
    title: "开发路线",
    description: "按 13 个可验收里程碑逐步实现内核、用户态、文件系统和 Shell。",
  },
  {
    index: "03",
    href: "/engineering/",
    title: "工程规范",
    description: "以现代 C++20、清晰边界、自动测试和同步文档约束整个项目。",
  },
  {
    index: "04",
    href: "/docs/",
    title: "文档中心",
    description: "集中维护需求、架构、决策记录、测试方案和调试档案。",
  },
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
      <section className="hero">
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
              一个以软件工程方式推进的 x86-64 教学操作系统。从固件、模式切换到
              内核、用户态和文件系统，每一层都亲手实现。
            </p>
            <div className="heroActions">
              <Link className="primaryButton" href="/roadmap/">
                查看开发路线
                <ArrowMark />
              </Link>
              <Link className="textButton" href="/architecture/">
                理解启动架构
              </Link>
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

      <section className="section homeRoutes">
        <div className="sectionHeading">
          <div>
            <span className="sectionIndex">PROJECT INDEX</span>
            <h2>一个入口，一页一个问题</h2>
          </div>
          <p>
            首页保留全局定位；架构、路线、规范与文档各自独立，后续内容增长时仍能快速定位。
          </p>
        </div>

        <div className="routeGrid">
          {routeCards.map((route) => (
            <Link className="routeCard" href={route.href} key={route.href}>
              <span className="routeCardIndex">{route.index}</span>
              <h3>{route.title}</h3>
              <p>{route.description}</p>
              <span className="routeCardAction">OPEN →</span>
            </Link>
          ))}
        </div>
      </section>
    </main>
  );
}

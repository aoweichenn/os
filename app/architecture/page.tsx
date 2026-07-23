import type { Metadata } from "next";
import { PageHero } from "@/components/page_hero";
import { bootStages } from "@/lib/project_data";

export const metadata: Metadata = {
  title: "启动架构",
  description: "从 x86-64 CPU 复位向量到 C++20 内核入口的完整自研启动链。",
};

const modeTransitions = [
  ["REAL MODE", "16-bit", "CPU 复位后的初始环境；只使用最小固件能力。"],
  ["PROTECTED MODE", "32-bit", "加载 GDT，打开保护模式，为长模式准备页表。"],
  ["LONG MODE", "64-bit", "设置 PAE、EFER.LME 与分页，跳转到 64 位代码段。"],
] as const;

const contracts = [
  ["固件 → Stage 1", "磁盘位置、加载地址、扇区数量与错误状态必须明确。"],
  ["Stage 2 → 内核", "ELF64 段、入口地址和内存布局必须经过边界校验。"],
  ["汇编 → C++", "栈、参数 ABI、对象初始化与运行时能力必须显式定义。"],
] as const;

export default function ArchitecturePage() {
  return (
    <main>
      <PageHero
        index="01"
        eyebrow="ARCHITECTURE"
        title="我们自己拥有整条启动链"
        description="QEMU 只负责模拟硬件。ROM 固件、磁盘加载、模式切换、ELF 解析和内核入口全部属于项目代码。"
      />

      <section className="section">
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

      <section className="section alternateSection">
        <div className="sectionHeading">
          <div>
            <span className="sectionIndex">MODE TRANSITION</span>
            <h2>每一次模式切换都能解释、检查和复现</h2>
          </div>
          <p>
            不把启动看成黑盒。控制寄存器、描述符表、页表和跳转条件都进入设计文档与调试记录。
          </p>
        </div>
        <div className="modeTimeline">
          {modeTransitions.map(([title, mode, description], index) => (
            <article className="modeStep" key={title}>
              <span>{String(index + 1).padStart(2, "0")}</span>
              <code>{mode}</code>
              <h3>{title}</h3>
              <p>{description}</p>
            </article>
          ))}
        </div>
      </section>

      <section className="section">
        <div className="sectionHeading">
          <div>
            <span className="sectionIndex">HANDOFF CONTRACTS</span>
            <h2>模块交接必须是显式契约</h2>
          </div>
          <p>
            每一层只依赖上一层承诺的数据和状态，错误必须在最靠近来源的位置被报告。
          </p>
        </div>
        <div className="contractGrid">
          {contracts.map(([title, description]) => (
            <article className="contractCard" key={title}>
              <code>CONTRACT</code>
              <h3>{title}</h3>
              <p>{description}</p>
            </article>
          ))}
        </div>
      </section>
    </main>
  );
}

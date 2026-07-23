import type { Metadata } from "next";
import { PageHero } from "@/components/page_hero";
import { engineeringRules } from "@/lib/project_data";

export const metadata: Metadata = {
  title: "工程规范",
  description: "x86-64 OS Lab 的 C++20、测试、文档和交付工程规范。",
};

const codeRules = [
  ["成员访问", "类成员函数访问成员变量或成员函数时，必须显式使用 this->。"],
  ["常量命名", "禁止魔法数字和魔法字符串；常量按项目、模块、功能全大写命名。"],
  ["文件组织", "头文件与源文件分离，边界清晰，依赖方向可见。"],
  ["命名与注释", "命名准确表达意图；注释使用中文，解释约束、原因和边界。"],
  ["现代特性", "优先使用现代 C++20 特性，避免宏和不必要的历史包袱。"],
  ["自研运行时", "freestanding 环境不链接宿主 C/C++ 标准运行时。"],
] as const;

const doneCriteria = [
  "实现与阶段目标一致，未引入超出范围的隐式能力。",
  "正常、边界和失败路径均有自动测试或可重复验证方法。",
  "串口日志和错误类型能够定位失败发生在哪个模块。",
  "需求、架构、测试、调试或 ADR 文档已同步更新。",
  "构建结果可复现，并通过格式、静态检查和系统回归。",
] as const;

export default function EngineeringPage() {
  return (
    <main>
      <PageHero
        index="03"
        eyebrow="ENGINEERING"
        title="学习项目，也按生产工程要求建设"
        description="能启动只是开始。设计可追溯、失败可诊断、行为可测试，才算真正掌握。"
      />

      <section className="section">
        <div className="ruleGrid">
          {engineeringRules.map((rule) => (
            <article className="ruleCard" key={rule.tag}>
              <span>{rule.tag}</span>
              <h3>{rule.title}</h3>
              <p>{rule.text}</p>
            </article>
          ))}
        </div>
      </section>

      <section className="section alternateSection">
        <div className="sectionHeading">
          <div>
            <span className="sectionIndex">C++20 RULES</span>
            <h2>代码本身应当足够接近设计文档</h2>
          </div>
          <p>
            通过类型、命名、文件边界和明确常量减少隐藏知识，让实现意图能够直接被审查。
          </p>
        </div>
        <div className="codeRuleGrid">
          {codeRules.map(([title, description], index) => (
            <article className="codeRuleCard" key={title}>
              <span>{String(index + 1).padStart(2, "0")}</span>
              <h3>{title}</h3>
              <p>{description}</p>
            </article>
          ))}
        </div>
      </section>

      <section className="section">
        <div className="sectionHeading">
          <div>
            <span className="sectionIndex">TEST STRATEGY</span>
            <h2>从纯逻辑到整机启动，逐层收紧反馈环</h2>
          </div>
          <p>能在宿主机验证的逻辑不等待 QEMU；必须依赖硬件状态的行为再进入系统测试。</p>
        </div>
        <div className="testingLayers">
          <article className="testingLayer">
            <code>L1 / HOST</code>
            <h3>单元测试</h3>
            <p>验证解析、位运算、容器和状态机等与硬件无关的纯逻辑。</p>
          </article>
          <article className="testingLayer">
            <code>L2 / MODULE</code>
            <h3>集成测试</h3>
            <p>验证镜像、ELF、页表和模块交接契约，并主动覆盖损坏输入。</p>
          </article>
          <article className="testingLayer">
            <code>L3 / QEMU</code>
            <h3>系统回归</h3>
            <p>捕获串口与退出码，验证从复位向量到目标里程碑的完整路径。</p>
          </article>
        </div>
      </section>

      <section className="section alternateSection">
        <div className="sectionHeading">
          <div>
            <span className="sectionIndex">DEFINITION OF DONE</span>
            <h2>完成，不等于“在我的机器上启动了”</h2>
          </div>
        </div>
        <ol className="doneList">
          {doneCriteria.map((criterion) => (
            <li key={criterion}>{criterion}</li>
          ))}
        </ol>
      </section>
    </main>
  );
}

import type { Metadata } from "next";
import { PageHero } from "@/components/page_hero";
import { documents } from "@/lib/project_data";

export const metadata: Metadata = {
  title: "文档中心",
  description: "x86-64 OS Lab 的需求、架构、路线、测试、调试和决策文档索引。",
};

const documentFlow = [
  ["WHY", "需求", "明确为什么做、什么不做，以及怎样才算成功。"],
  ["HOW", "架构与 ADR", "记录模块边界、关键契约和不可逆技术取舍。"],
  ["PROVE", "测试与调试", "保留自动验收方式、失败现场和问题根因。"],
] as const;

export default function DocsPage() {
  return (
    <main>
      <PageHero
        index="04"
        eyebrow="DOCUMENTATION"
        title="让每个结论都能追溯"
        description="文档不用于事后补写，而是和代码一起定义问题、解释选择并保存可重复的证据。"
      />

      <section className="section">
        <div className="docsIndex">
          {documents.map(([title, description, path]) => (
            <article className="docCard" key={path}>
              <code>{path}</code>
              <h2>{title}</h2>
              <p>{description}</p>
              <span>REPOSITORY DOCUMENT</span>
            </article>
          ))}
        </div>
      </section>

      <section className="section alternateSection">
        <div className="sectionHeading">
          <div>
            <span className="sectionIndex">KNOWLEDGE FLOW</span>
            <h2>从目标到证据，形成闭环</h2>
          </div>
          <p>每次重要改动都应该回答三个问题：为什么做、为什么这样做、如何证明它正确。</p>
        </div>
        <div className="knowledgeFlow">
          {documentFlow.map(([tag, title, description], index) => (
            <article key={tag}>
              <span>{tag}</span>
              <h3>{title}</h3>
              <p>{description}</p>
              {index < documentFlow.length - 1 && <i aria-hidden="true">→</i>}
            </article>
          ))}
        </div>
      </section>

      <section className="section">
        <div className="documentationPanel">
          <div className="documentationIntro">
            <span className="sectionIndex">MAINTENANCE RULE</span>
            <h3>代码改变事实，文档必须在同一次提交中更新</h3>
            <p>
              新增约束进入需求或规范；改变模块边界更新架构；重要取舍新增
              ADR；修复复杂故障沉淀到调试档案。
            </p>
          </div>
          <div className="documentList">
            {documents.map(([title, description, path]) => (
              <div className="documentRow" key={path}>
                <strong>{title}</strong>
                <span>{description}</span>
                <code>{path}</code>
              </div>
            ))}
          </div>
        </div>
      </section>
    </main>
  );
}

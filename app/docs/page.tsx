import type { Metadata } from "next";
import Link from "next/link";
import { PageHero } from "@/components/page_hero";
import { projectDocuments } from "@/lib/document_catalog";

export const metadata: Metadata = {
  title: "文档中心",
  description: "x86-64 OS Lab 的背景知识、架构、构建、测试、模块和发布记录。",
};

const documentFlow = [
  ["LEARN", "建立背景", "先理解 CPU、工具链和运行环境，再开始实现机制。"],
  ["DESIGN", "明确契约", "记录目标、模块边界、失败语义和关键决策。"],
  ["PROVE", "保存证据", "用测试、调试记录和发布档案证明阶段结论。"],
] as const;

export default function DocsPage() {
  const featuredDocument = projectDocuments[0];
  const remainingDocuments = projectDocuments.slice(1);

  return (
    <main>
      <PageHero
        index="04"
        eyebrow="DOCUMENTATION"
        title="让每个结论都能追溯"
        description="仓库 Markdown 直接生成网页正文：一份内容、一次维护，同时服务开发和学习。"
      />

      <section className="section">
        <Link
          className="featuredDocument"
          href={`/docs/${featuredDocument.slug}/`}
        >
          <div>
            <span className="sectionIndex">START HERE / 入门必读</span>
            <h2>{featuredDocument.title}</h2>
          </div>
          <div>
            <p>{featuredDocument.description}</p>
            <code>{featuredDocument.repositoryPath}</code>
            <span>开始阅读 →</span>
          </div>
        </Link>

        <div className="docsIndex docsLibrary">
          {remainingDocuments.map((document) => (
            <Link
              className="docCard"
              href={`/docs/${document.slug}/`}
              key={document.slug}
            >
              <div>
                <span className="docCategory">{document.category}</span>
                <code>{document.repositoryPath}</code>
              </div>
              <h2>{document.title}</h2>
              <p>{document.description}</p>
              <span>READ DOCUMENT →</span>
            </Link>
          ))}
        </div>
      </section>

      <section className="section alternateSection">
        <div className="sectionHeading">
          <div>
            <span className="sectionIndex">KNOWLEDGE FLOW</span>
            <h2>从背景到证据，形成学习闭环</h2>
          </div>
          <p>每次重要改动都回答三个问题：机制是什么、为什么这样设计、如何证明它正确。</p>
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
    </main>
  );
}

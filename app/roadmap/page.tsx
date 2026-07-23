import type { Metadata } from "next";
import { PageHero } from "@/components/page_hero";
import { roadmap } from "@/lib/project_data";

export const metadata: Metadata = {
  title: "开发路线",
  description: "x86-64 教学操作系统的 13 个可验收工程阶段。",
};

export default function RoadmapPage() {
  return (
    <main>
      <PageHero
        index="02"
        eyebrow="ROADMAP"
        title="13 个可验收的工程阶段"
        description="每个版本只跨越一个主要知识边界，并同时交付实现、测试、文档和可重复的验收证据。"
      />

      <section className="section roadmapSection">
        <div className="roadmapSummary">
          <div>
            <span>当前总进度</span>
            <strong>1 / 13</strong>
          </div>
          <div className="progressTrack">
            <i />
          </div>
          <p>下一阶段：v0.1 复位与串口</p>
        </div>

        <div className="detailRoadmap">
          {roadmap.map((phase, index) => {
            const phaseState =
              index === 0 ? "DONE" : index === 1 ? "NEXT" : "QUEUED";
            const phaseClassName =
              phaseState === "DONE"
                ? "detailRoadmapItem done"
                : phaseState === "NEXT"
                  ? "detailRoadmapItem active"
                  : "detailRoadmapItem";

            return (
              <article className={phaseClassName} key={phase.version}>
                <div className="phaseIdentity">
                  <span>{String(index + 1).padStart(2, "0")}</span>
                  <code>{phase.version}</code>
                  <span className="phaseState">{phaseState}</span>
                </div>
                <div className="phaseDescription">
                  <h2>{phase.title}</h2>
                  <p>{phase.summary}</p>
                </div>
                <div>
                  <span className="acceptanceTitle">ACCEPTANCE</span>
                  <ul className="acceptanceList">
                    {phase.acceptance.map((item) => (
                      <li key={item}>{item}</li>
                    ))}
                  </ul>
                </div>
              </article>
            );
          })}
        </div>
      </section>
    </main>
  );
}

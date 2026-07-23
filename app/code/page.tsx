import Link from "next/link";
import { codeFiles, countCodeDirectories } from "@/lib/code_catalog";
import { codeFileHref } from "@/lib/code_paths";
import { codeWalkthroughs } from "@/lib/code_walkthroughs";

export default function CodeIndexPage() {
  const totalLineCount = codeFiles.reduce(
    (lineCount, codeFile) => lineCount + codeFile.lineCount,
    0,
  );
  const directoryCount = countCodeDirectories();

  return (
    <div className="codeLanding">
      <section className="codeLandingHero">
        <span className="pageKicker">SOURCE / WALKTHROUGH</span>
        <h1>不是找文件。是理解代码为什么这样组织。</h1>
        <p>
          从目录树浏览整个项目，通过固定阅读路线和关键文件说明，把构建、领域模型、测试和发布串成一条完整思路。
        </p>
        <div className="codeCatalogStats">
          <div>
            <strong>{codeFiles.length}</strong>
            <span>项目文件</span>
          </div>
          <div>
            <strong>{totalLineCount.toLocaleString("zh-CN")}</strong>
            <span>收录行数</span>
          </div>
          <div>
            <strong>{directoryCount}</strong>
            <span>目录节点</span>
          </div>
        </div>
      </section>

      <section className="codeWalkthroughSection">
        <div className="codeSectionHeading">
          <span>RECOMMENDED READING ORDER</span>
          <h2>按依赖方向阅读，而不是随机点击</h2>
        </div>
        <div className="codeWalkthroughGrid">
          {codeWalkthroughs.map((walkthrough) => (
            <article key={walkthrough.index}>
              <span>{walkthrough.index}</span>
              <h3>{walkthrough.title}</h3>
              <p>{walkthrough.description}</p>
              <ol>
                {walkthrough.files.map((filePath) => (
                  <li key={filePath}>
                    <Link href={codeFileHref(filePath)}>{filePath}</Link>
                  </li>
                ))}
              </ol>
            </article>
          ))}
        </div>
      </section>

      <section className="codeScopeNote">
        <span>CATALOG BOUNDARY</span>
        <p>
          收录项目源码、测试、构建脚本、配置和文档；主动排除依赖锁、生成目录、部署元数据及内部辅助配置。
        </p>
      </section>
    </div>
  );
}

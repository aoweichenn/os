import type { Metadata } from "next";
import Link from "next/link";
import { notFound } from "next/navigation";
import { CodeViewer } from "@/components/code_viewer";
import {
  codeFiles,
  findCodeFile,
  formatCodeFileSize,
} from "@/lib/code_catalog";
import { codeFileHref } from "@/lib/code_paths";
import { codeFileGuides } from "@/lib/code_walkthroughs";
import { highlightCodeFile } from "@/lib/highlight_code";

interface CodeFilePageProperties {
  params: Promise<{
    path: string[];
  }>;
}

export const dynamicParams = false;

export function generateStaticParams() {
  return codeFiles.map((codeFile) => ({
    path: codeFile.path.split("/"),
  }));
}

export async function generateMetadata({
  params,
}: CodeFilePageProperties): Promise<Metadata> {
  const { path } = await params;
  const codeFile = findCodeFile(path.join("/"));

  if (!codeFile) {
    return {};
  }

  return {
    title: codeFile.path,
    description: `阅读 ${codeFile.path} 的源码、目录位置和中文走读说明。`,
  };
}

export default async function CodeFilePage({
  params,
}: CodeFilePageProperties) {
  const { path } = await params;
  const filePath = path.join("/");
  const codeFile = findCodeFile(filePath);

  if (!codeFile) {
    notFound();
  }

  const highlightedCode = await highlightCodeFile(codeFile);
  const codeFileGuide = codeFileGuides[codeFile.path];
  const fileIndex = codeFiles.findIndex(
    (candidateFile) => candidateFile.path === codeFile.path,
  );
  const previousFile = codeFiles[fileIndex - 1];
  const nextFile = codeFiles[fileIndex + 1];

  return (
    <article className="codeFilePage">
      <header className="codeFileHeader">
        <div className="codeBreadcrumbs">
          <Link href="/code/">code</Link>
          {codeFile.path.split("/").map((pathSegment, segmentIndex) => (
            <span key={`${segmentIndex}-${pathSegment}`}>
              <i>/</i>
              {pathSegment}
            </span>
          ))}
        </div>
        <h1>{codeFile.name}</h1>
        <div className="codeFileMeta">
          <span>{codeFile.language}</span>
          <span>{codeFile.lineCount} 行</span>
          <span>{formatCodeFileSize(codeFile.sizeBytes)}</span>
        </div>
      </header>

      {codeFileGuide && (
        <section className="codeReadingGuide">
          <div>
            <span>CODE WALKTHROUGH</span>
            <h2>这个文件在系统中的作用</h2>
            <p>{codeFileGuide.role}</p>
          </div>
          <ul>
            {codeFileGuide.focusPoints.map((focusPoint) => (
              <li key={focusPoint}>{focusPoint}</li>
            ))}
          </ul>
        </section>
      )}

      <CodeViewer
        filePath={codeFile.path}
        highlightedCode={highlightedCode}
        sourceCode={codeFile.content}
      />

      <nav className="codeFilePagination" aria-label="相邻文件">
        {previousFile ? (
          <Link href={codeFileHref(previousFile.path)}>
            <span>← PREVIOUS</span>
            {previousFile.path}
          </Link>
        ) : (
          <span />
        )}
        {nextFile ? (
          <Link href={codeFileHref(nextFile.path)}>
            <span>NEXT →</span>
            {nextFile.path}
          </Link>
        ) : (
          <span />
        )}
      </nav>
    </article>
  );
}

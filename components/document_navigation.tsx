import Link from "next/link";
import { projectDocuments } from "@/lib/document_catalog";

interface DocumentNavigationProperties {
  activeSlug: string;
}

export function DocumentNavigation({
  activeSlug,
}: DocumentNavigationProperties) {
  return (
    <aside className="documentSidebar">
      <Link className="documentBackLink" href="/docs/">
        ← 返回文档中心
      </Link>
      <nav aria-label="文档目录">
        {projectDocuments.map((document) => (
          <Link
            className={document.slug === activeSlug ? "active" : undefined}
            href={`/docs/${document.slug}/`}
            key={document.slug}
          >
            <span>{document.category}</span>
            {document.title}
          </Link>
        ))}
      </nav>
    </aside>
  );
}

import type { Metadata } from "next";
import { notFound } from "next/navigation";
import { DocumentNavigation } from "@/components/document_navigation";
import { MarkdownArticle } from "@/components/markdown_article";
import {
  findProjectDocument,
  projectDocuments,
} from "@/lib/document_catalog";
import { readProjectDocument } from "@/lib/read_document";

interface DocumentPageProperties {
  params: Promise<{
    slug: string;
  }>;
}

export const dynamicParams = false;

export function generateStaticParams() {
  return projectDocuments.map((document) => ({
    slug: document.slug,
  }));
}

export async function generateMetadata({
  params,
}: DocumentPageProperties): Promise<Metadata> {
  const { slug } = await params;
  const document = findProjectDocument(slug);

  if (!document) {
    return {};
  }

  return {
    title: document.title,
    description: document.description,
  };
}

export default async function DocumentPage({
  params,
}: DocumentPageProperties) {
  const { slug } = await params;
  const document = findProjectDocument(slug);

  if (!document) {
    notFound();
  }

  const markdownSource = await readProjectDocument(document);

  return (
    <main>
      <section className="documentHero">
        <div>
          <span className="pageKicker">
            {document.category} / DOCUMENT
          </span>
          <h1>{document.title}</h1>
          <p>{document.description}</p>
          <code>{document.repositoryPath}</code>
        </div>
      </section>
      <section className="documentShell">
        <DocumentNavigation activeSlug={document.slug} />
        <MarkdownArticle source={markdownSource} />
      </section>
    </main>
  );
}

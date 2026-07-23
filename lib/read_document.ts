import { readFile } from "node:fs/promises";
import { join } from "node:path";
import type { ProjectDocument } from "@/lib/document_catalog";

const OS_DOCUMENT_REPOSITORY_PREFIX = "docs/";
const OS_DOCUMENT_SOURCE_ROOT = join(process.cwd(), "docs");

export async function readProjectDocument(document: ProjectDocument) {
  const relativeDocumentPath = document.repositoryPath.slice(
    OS_DOCUMENT_REPOSITORY_PREFIX.length,
  );
  const absoluteDocumentPath = join(
    OS_DOCUMENT_SOURCE_ROOT,
    relativeDocumentPath,
  );
  const markdownSource = await readFile(absoluteDocumentPath, "utf8");

  return markdownSource.replace(/^# .+\r?\n+/, "");
}

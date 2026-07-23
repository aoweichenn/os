import generatedCodeCatalog from "@/generated/code_catalog.json";

export interface CodeFile {
  path: string;
  name: string;
  directory: string;
  language: string;
  lineCount: number;
  sizeBytes: number;
  content: string;
}

interface CodeCatalog {
  files: CodeFile[];
}

const codeCatalog = generatedCodeCatalog as CodeCatalog;

export const codeFiles = codeCatalog.files;

export function findCodeFile(filePath: string) {
  return codeFiles.find((codeFile) => codeFile.path === filePath);
}

export function formatCodeFileSize(sizeBytes: number) {
  if (sizeBytes < 1024) {
    return `${sizeBytes} B`;
  }

  return `${(sizeBytes / 1024).toFixed(1)} KiB`;
}

export function countCodeDirectories() {
  return new Set(
    codeFiles
      .map((codeFile) => codeFile.directory)
      .filter((directory) => directory !== "."),
  ).size;
}

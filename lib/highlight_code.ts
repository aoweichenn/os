import { createHighlighter } from "shiki";
import type { CodeFile } from "@/lib/code_catalog";

const OS_CODE_HIGHLIGHT_THEME = "github-dark-default";
const OS_CODE_HIGHLIGHT_LANGUAGES = [
  "asm",
  "bash",
  "cmake",
  "cpp",
  "css",
  "ini",
  "javascript",
  "json",
  "jsonc",
  "markdown",
  "tsx",
  "typescript",
  "xml",
  "yaml",
] as const;

const codeHighlighterPromise = createHighlighter({
  themes: [OS_CODE_HIGHLIGHT_THEME],
  langs: [...OS_CODE_HIGHLIGHT_LANGUAGES],
});

export async function highlightCodeFile(codeFile: CodeFile) {
  const codeHighlighter = await codeHighlighterPromise;

  return codeHighlighter.codeToHtml(codeFile.content, {
    lang: codeFile.language,
    theme: OS_CODE_HIGHLIGHT_THEME,
    transformers: [
      {
        line(lineNode, lineNumber) {
          lineNode.properties.id = `L${lineNumber}`;
          lineNode.properties["data-line"] = String(lineNumber);
        },
      },
    ],
  });
}

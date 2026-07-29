#!/usr/bin/env python3

import re
from pathlib import Path


OS_BOOK_ROOT = Path(__file__).resolve().parents[1]
OS_BOOK_MAIN_FILE = OS_BOOK_ROOT / "main.tex"
OS_BOOK_INPUT_PATTERN = re.compile(r"\\input\{([^}]+)\}")
OS_BOOK_COMMENT_PATTERN = re.compile(r"(?<!\\)%.*$")
OS_BOOK_SENTENCE_PATTERN = re.compile(r"[。！？]")
OS_BOOK_CONTRAST_PATTERN = re.compile(r"不是[^。；\n]{0,80}而是")
OS_BOOK_ADDITIVE_CONTRAST_PATTERN = re.compile(r"不只[^。；\n]{0,80}还")
OS_BOOK_MAXIMUM_CONTRAST_COUNT = 50
OS_BOOK_MAXIMUM_SENTENCES_PER_PARAGRAPH = 6
OS_BOOK_FORBIDDEN_PHRASES = (
    "完整展开",
    "进一步展开",
    "继续解释",
    "逐项解释",
    "逐层连接",
    "共同证明",
    "形成证据链",
    "形成闭环",
    "事实底座",
    "严格边界",
    "当前承诺范围",
    "发布身份",
    "可反驳证据",
    "资源账本",
    "生命周期闭环",
    "能力闭环",
    "冻结基线",
    "完整地",
    "严格地",
    "系统地",
    "深入地",
    "真正地",
    "全面地",
    "清晰地",
    "精确地",
    "本章将",
    "本节将",
    "下面将从",
    "书稿",
)
OS_BOOK_REPEATED_IDEA_PATTERN = re.compile(r"\\begin\{keyidea\}")
OS_BOOK_VERSION_HEADING_PATTERN = re.compile(
    r"\\topic\{[^}\n]*v[0-9]+\.[0-9]+"
)
OS_BOOK_PROJECT_VERSION_PATTERN = re.compile(
    r"(?<![A-Za-z0-9_])v(?:0|1)\.[0-9]+(?:\.[0-9]+)?"
)
OS_BOOK_DEEPENING_REPORT_PHRASES = (
    "验收",
    "冻结",
    "证据层",
    "完成不是",
    "完整结论",
    "垂直闭环",
)
OS_BOOK_STRUCTURAL_PARAGRAPH_PATTERN = re.compile(
    r"^\\(?:begin|end|section|chapter|part|input|includegraphics|"
    r"label|caption|item|clearpage|newcommand)\b"
)


def resolveInput(inputName: str) -> Path:
    inputPath = OS_BOOK_ROOT / inputName
    if inputPath.suffix == "":
        inputPath = inputPath.with_suffix(".tex")
    return inputPath.resolve()


def collectActiveFiles() -> list[Path]:
    visitedFiles: set[Path] = set()
    orderedFiles: list[Path] = []

    def visit(inputPath: Path) -> None:
        if inputPath in visitedFiles:
            return
        visitedFiles.add(inputPath)
        orderedFiles.append(inputPath)
        sourceText = inputPath.read_text(encoding="utf-8")
        for inputName in OS_BOOK_INPUT_PATTERN.findall(sourceText):
            visit(resolveInput(inputName))

    visit(OS_BOOK_MAIN_FILE.resolve())
    return orderedFiles


def stripComments(sourceText: str) -> str:
    return "\n".join(
        OS_BOOK_COMMENT_PATTERN.sub("", sourceLine)
        for sourceLine in sourceText.splitlines()
    )


def collectLongParagraphs(
    sourcePath: Path,
    sourceText: str,
) -> list[tuple[Path, int, int]]:
    longParagraphs: list[tuple[Path, int, int]] = []
    sourceLines = sourceText.splitlines()
    paragraphLines: list[str] = []
    paragraphStart = 1

    def finishParagraph() -> None:
        nonlocal paragraphLines
        if not paragraphLines:
            return
        paragraphText = " ".join(paragraphLines)
        paragraphLines = []
        if (
            "\\begin{" in paragraphText
            or "\\end{" in paragraphText
            or "\\[" in paragraphText
            or "\\]" in paragraphText
            or OS_BOOK_STRUCTURAL_PARAGRAPH_PATTERN.match(
                paragraphText.lstrip()
            )
        ):
            return
        sentenceCount = len(OS_BOOK_SENTENCE_PATTERN.findall(paragraphText))
        if sentenceCount > OS_BOOK_MAXIMUM_SENTENCES_PER_PARAGRAPH:
            longParagraphs.append(
                (sourcePath, paragraphStart, sentenceCount)
            )

    for lineNumber, sourceLine in enumerate(sourceLines, start=1):
        if sourceLine.strip() == "":
            finishParagraph()
            continue
        if not paragraphLines:
            paragraphStart = lineNumber
        paragraphLines.append(sourceLine)
    finishParagraph()
    return longParagraphs


def main() -> int:
    activeFiles = collectActiveFiles()
    violations: list[str] = []
    contrastCount = 0
    additiveContrastCount = 0
    longParagraphs: list[tuple[Path, int, int]] = []

    for sourcePath in activeFiles:
        sourceText = stripComments(sourcePath.read_text(encoding="utf-8"))
        relativePath = sourcePath.relative_to(OS_BOOK_ROOT)

        for phrase in OS_BOOK_FORBIDDEN_PHRASES:
            for match in re.finditer(re.escape(phrase), sourceText):
                lineNumber = sourceText.count("\n", 0, match.start()) + 1
                violations.append(
                    f"{relativePath}:{lineNumber}: 高风险表达“{phrase}”"
                )

        for match in OS_BOOK_REPEATED_IDEA_PATTERN.finditer(sourceText):
            lineNumber = sourceText.count("\n", 0, match.start()) + 1
            violations.append(
                f"{relativePath}:{lineNumber}: "
                "重复 keyidea 框会把不同材料压成同一种章法"
            )

        for match in OS_BOOK_VERSION_HEADING_PATTERN.finditer(sourceText):
            lineNumber = sourceText.count("\n", 0, match.start()) + 1
            violations.append(
                f"{relativePath}:{lineNumber}: "
                "段首标题应描述问题或机制，版本号只作为正文中的历史坐标"
            )

        if relativePath.parts[0] == "deepening":
            for match in OS_BOOK_PROJECT_VERSION_PATTERN.finditer(sourceText):
                lineNumber = sourceText.count("\n", 0, match.start()) + 1
                violations.append(
                    f"{relativePath}:{lineNumber}: "
                    "项目版本标签应进入发布记录，机制正文直接描述动作与状态"
                )
            for phrase in OS_BOOK_DEEPENING_REPORT_PHRASES:
                for match in re.finditer(re.escape(phrase), sourceText):
                    lineNumber = sourceText.count("\n", 0, match.start()) + 1
                    violations.append(
                        f"{relativePath}:{lineNumber}: "
                        f"deepening 正文仍含验收报告表达“{phrase}”"
                    )

        contrastCount += len(OS_BOOK_CONTRAST_PATTERN.findall(sourceText))
        additiveContrastCount += len(
            OS_BOOK_ADDITIVE_CONTRAST_PATTERN.findall(sourceText)
        )
        longParagraphs.extend(
            collectLongParagraphs(relativePath, sourceText)
        )

    if contrastCount > OS_BOOK_MAXIMUM_CONTRAST_COUNT:
        violations.append(
            "活跃书稿含有 "
            f"{contrastCount} 处“不是……而是……”句式，"
            f"上限为 {OS_BOOK_MAXIMUM_CONTRAST_COUNT}"
        )

    if violations:
        raise SystemExit(
            "书稿编辑检查失败：\n  " + "\n  ".join(violations)
        )

    print(
        "书稿编辑检查通过："
        f"{len(activeFiles)} 个活跃文件，"
        f"{contrastCount} 处“不是……而是……”，"
        f"{additiveContrastCount} 处“不只……还……”，"
        f"{len(longParagraphs)} 个超过 "
        f"{OS_BOOK_MAXIMUM_SENTENCES_PER_PARAGRAPH} 句的待复核段落。"
    )
    for sourcePath, lineNumber, sentenceCount in longParagraphs[:10]:
        print(
            "  长段落提示："
            f"{sourcePath}:{lineNumber}，{sentenceCount} 句"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

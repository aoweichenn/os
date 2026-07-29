#!/usr/bin/env python3

import re
from pathlib import Path


OS_BOOK_ROOT = Path(__file__).resolve().parents[1]
OS_BOOK_MAIN_FILE = OS_BOOK_ROOT / "main.tex"
OS_BOOK_INPUT_PATTERN = re.compile(r"\\input\{([^}]+)\}")
OS_BOOK_GRAPHIC_PATTERN = re.compile(
    r"\\includegraphics(?:\[[^\]]*\])?\s*\{([^}]+)\}"
)
OS_BOOK_CHAPTER_PATTERN = re.compile(r"\\chapter\{")
OS_BOOK_EXPECTED_CHAPTER_COUNT = 20
OS_BOOK_MAIN_CHAPTER_PATTERN = re.compile(r"^\\chapter\{([^}]+)\}")
OS_BOOK_SECTION_PATTERN = re.compile(r"^\\section\{")
OS_BOOK_LOCAL_HEADING_PATTERN = re.compile(
    r"^\\(?:subsection|subsubsection|paragraph)\*?\{"
)
OS_BOOK_MINIMUM_SECTION_COUNT = 4
OS_BOOK_MAXIMUM_SECTION_COUNT = 6
OS_BOOK_GRAPHIC_EXTENSIONS = (".pdf",)
OS_BOOK_RASTER_IMAGE_PATTERN = re.compile(rb"/Subtype\s*/Image\b")
OS_BOOK_UPSTREAM_SCHEMATIC = "reference_schematic.pdf"
OS_BOOK_RAW_TABLE_PATTERN = re.compile(
    r"\\begin\{(?:tabular|tabularx|longtable)\}"
)
OS_BOOK_GRID_TABLE_BEGIN_PATTERN = re.compile(
    r"\\begin\{(osgrid(?:tabular|tabularx|longtable))\}"
)
OS_BOOK_GRID_TABLE_END_PATTERN = re.compile(
    r"\\end\{(osgrid(?:tabular|tabularx|longtable))\}"
)
OS_BOOK_TABLE_ROW_PATTERN = re.compile(
    r"\\\\(?:\s+\\hline)?\s*(?:%.*)?$"
)
OS_BOOK_GRID_ROW_PATTERN = re.compile(r"\\\\\s+\\hline\s*(?:%.*)?$")


def resolveInput(inputName: str) -> Path:
    inputPath = OS_BOOK_ROOT / inputName
    if inputPath.suffix == "":
        inputPath = inputPath.with_suffix(".tex")
    return inputPath.resolve()


def resolveGraphic(graphicName: str) -> Path:
    graphicPath = (OS_BOOK_ROOT / graphicName).resolve()
    if graphicPath.suffix != "":
        return graphicPath
    for extension in OS_BOOK_GRAPHIC_EXTENSIONS:
        candidatePath = graphicPath.with_suffix(extension)
        if candidatePath.is_file():
            return candidatePath
    return graphicPath


def checkGridTables(sourcePath: Path) -> tuple[int, int]:
    tableCount = 0
    rowCount = 0
    activeGridTable: str | None = None
    sourceText = sourcePath.read_text(encoding="utf-8")

    for lineNumber, sourceLine in enumerate(sourceText.splitlines(), start=1):
        if OS_BOOK_RAW_TABLE_PATTERN.search(sourceLine):
            raise SystemExit(
                "书稿表格必须使用统一实线网格环境，"
                f"禁止原始表格环境：{sourcePath}:{lineNumber}"
            )

        tableBegin = OS_BOOK_GRID_TABLE_BEGIN_PATTERN.search(sourceLine)
        if tableBegin is not None:
            if activeGridTable is not None:
                raise SystemExit(
                    f"书稿表格不能嵌套：{sourcePath}:{lineNumber}"
                )
            activeGridTable = tableBegin.group(1)
            tableCount += 1

        if (
            activeGridTable is not None
            and OS_BOOK_TABLE_ROW_PATTERN.search(sourceLine)
        ):
            if not OS_BOOK_GRID_ROW_PATTERN.search(sourceLine):
                raise SystemExit(
                    "书稿表格每一行都必须以实线分隔，"
                    "请使用“\\\\ \\hline”结束该行："
                    f"{sourcePath}:{lineNumber}"
                )
            rowCount += 1

        tableEnd = OS_BOOK_GRID_TABLE_END_PATTERN.search(sourceLine)
        if tableEnd is not None:
            if activeGridTable != tableEnd.group(1):
                raise SystemExit(
                    f"书稿表格环境起止不匹配：{sourcePath}:{lineNumber}"
                )
            activeGridTable = None

    if activeGridTable is not None:
        raise SystemExit(
            f"书稿表格环境没有结束：{sourcePath}：{activeGridTable}"
        )

    return tableCount, rowCount


def checkChapterStructure() -> list[tuple[str, int]]:
    activeFiles: list[Path] = []
    chapterSections: list[tuple[str, int]] = []
    activeChapterTitle: str | None = None
    activeSectionCount = 0

    def finishChapter() -> None:
        nonlocal activeChapterTitle, activeSectionCount
        if activeChapterTitle is None:
            return
        if not (
            OS_BOOK_MINIMUM_SECTION_COUNT
            <= activeSectionCount
            <= OS_BOOK_MAXIMUM_SECTION_COUNT
        ):
            raise SystemExit(
                f"章节“{activeChapterTitle}”包含 {activeSectionCount} 个主节，"
                f"必须保持在 {OS_BOOK_MINIMUM_SECTION_COUNT}--"
                f"{OS_BOOK_MAXIMUM_SECTION_COUNT} 个"
            )
        chapterSections.append((activeChapterTitle, activeSectionCount))
        activeChapterTitle = None
        activeSectionCount = 0

    def scan(inputPath: Path) -> None:
        nonlocal activeChapterTitle, activeSectionCount
        if inputPath in activeFiles:
            raise SystemExit(f"书稿输入形成循环：{inputPath}")
        activeFiles.append(inputPath)
        sourceLines = inputPath.read_text(encoding="utf-8").splitlines()

        for lineNumber, sourceLine in enumerate(sourceLines, start=1):
            if sourceLine.strip() == r"\backmatter":
                finishChapter()

            chapterMatch = OS_BOOK_MAIN_CHAPTER_PATTERN.match(sourceLine)
            if chapterMatch is not None:
                finishChapter()
                activeChapterTitle = chapterMatch.group(1)

            if activeChapterTitle is not None:
                if OS_BOOK_LOCAL_HEADING_PATTERN.match(sourceLine):
                    raise SystemExit(
                        "正文章节不再使用二级、三级或独立 paragraph 标题，"
                        "请把局部主题改为连续正文中的 \\topic："
                        f"{inputPath}:{lineNumber}"
                    )
                if OS_BOOK_SECTION_PATTERN.match(sourceLine):
                    activeSectionCount += 1

            for inputName in OS_BOOK_INPUT_PATTERN.findall(sourceLine):
                scan(resolveInput(inputName))

        activeFiles.pop()

    scan(OS_BOOK_MAIN_FILE.resolve())
    finishChapter()
    if len(chapterSections) != OS_BOOK_EXPECTED_CHAPTER_COUNT:
        raise SystemExit(
            "章节结构检查数量不一致："
            f"实际 {len(chapterSections)}，"
            f"预期 {OS_BOOK_EXPECTED_CHAPTER_COUNT}"
        )
    return chapterSections


def main() -> int:
    visitedFiles: set[Path] = set()
    activeFiles: list[Path] = []
    graphicFiles: set[Path] = set()
    chapterCount = 0
    gridTableCount = 0
    gridRowCount = 0
    for sourcePath in sorted(OS_BOOK_ROOT.rglob("*.tex")):
        sourceTableCount, sourceRowCount = checkGridTables(sourcePath)
        gridTableCount += sourceTableCount
        gridRowCount += sourceRowCount

    def visit(inputPath: Path) -> None:
        nonlocal chapterCount
        if inputPath in activeFiles:
            raise SystemExit(f"书稿输入形成循环：{inputPath}")
        if inputPath in visitedFiles:
            return
        if not inputPath.is_file():
            raise SystemExit(f"缺少书稿文件：{inputPath}")
        try:
            inputPath.relative_to(OS_BOOK_ROOT)
        except ValueError as error:
            raise SystemExit(f"书稿输入越过源码根目录：{inputPath}") from error

        activeFiles.append(inputPath)
        sourceText = inputPath.read_text(encoding="utf-8")
        chapterCount += len(OS_BOOK_CHAPTER_PATTERN.findall(sourceText))
        for graphicName in OS_BOOK_GRAPHIC_PATTERN.findall(sourceText):
            graphicPath = resolveGraphic(graphicName)
            if not graphicPath.is_file():
                raise SystemExit(f"缺少书稿图片：{graphicPath}")
            try:
                graphicPath.relative_to(OS_BOOK_ROOT)
            except ValueError as error:
                raise SystemExit(f"书稿图片越过源码根目录：{graphicPath}") from error
            if graphicPath.suffix.lower() != ".pdf":
                raise SystemExit(
                    "书稿图片必须使用可无损缩放的矢量 PDF，"
                    f"禁止直接引用位图：{graphicPath}"
                )
            if (
                graphicPath.name != OS_BOOK_UPSTREAM_SCHEMATIC
                and OS_BOOK_RASTER_IMAGE_PATTERN.search(graphicPath.read_bytes())
            ):
                raise SystemExit(
                    "书稿学习图 PDF 含有栅格 image XObject，"
                    f"无法保证无损放大：{graphicPath}"
                )
            graphicFiles.add(graphicPath)
        for inputName in OS_BOOK_INPUT_PATTERN.findall(sourceText):
            visit(resolveInput(inputName))
        activeFiles.pop()
        visitedFiles.add(inputPath)

    visit(OS_BOOK_MAIN_FILE.resolve())
    chapterSections = checkChapterStructure()
    if chapterCount != OS_BOOK_EXPECTED_CHAPTER_COUNT:
        raise SystemExit(
            f"正文章节数量不符合主题章设计：实际 {chapterCount}，"
            f"预期 {OS_BOOK_EXPECTED_CHAPTER_COUNT}"
        )

    print(
        f"书稿输入检查通过：{len(visitedFiles)} 个文件，"
        f"{chapterCount} 个正文章节，{len(graphicFiles)} 个图片资源，"
        f"{gridTableCount} 张实线网格表，{gridRowCount} 个实线分隔行；"
        "每章主节数量为 "
        f"{min(count for _, count in chapterSections)}--"
        f"{max(count for _, count in chapterSections)}。"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

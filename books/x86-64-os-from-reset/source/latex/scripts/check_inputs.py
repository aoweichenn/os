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
OS_BOOK_EXPECTED_CHAPTER_COUNT = 10
OS_BOOK_GRAPHIC_EXTENSIONS = (".pdf",)
OS_BOOK_RASTER_IMAGE_PATTERN = re.compile(rb"/Subtype\s*/Image\b")
OS_BOOK_UPSTREAM_SCHEMATIC = "reference_schematic.pdf"


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


def main() -> int:
    visitedFiles: set[Path] = set()
    activeFiles: list[Path] = []
    graphicFiles: set[Path] = set()
    chapterCount = 0

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
    if chapterCount != OS_BOOK_EXPECTED_CHAPTER_COUNT:
        raise SystemExit(
            f"正文章节数量不符合主题章设计：实际 {chapterCount}，"
            f"预期 {OS_BOOK_EXPECTED_CHAPTER_COUNT}"
        )

    print(
        f"书稿输入检查通过：{len(visitedFiles)} 个文件，"
        f"{chapterCount} 个正文章节，{len(graphicFiles)} 个图片资源。"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

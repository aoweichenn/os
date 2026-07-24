#!/usr/bin/env python3

import re
from pathlib import Path


OS_BOOK_ROOT = Path(__file__).resolve().parents[1]
OS_BOOK_MAIN_FILE = OS_BOOK_ROOT / "main.tex"
OS_BOOK_INPUT_PATTERN = re.compile(r"\\input\{([^}]+)\}")
OS_BOOK_CHAPTER_PATTERN = re.compile(r"\\chapter\{")
OS_BOOK_MINIMUM_CHAPTER_COUNT = 12


def resolveInput(inputName: str) -> Path:
    inputPath = OS_BOOK_ROOT / inputName
    if inputPath.suffix == "":
        inputPath = inputPath.with_suffix(".tex")
    return inputPath.resolve()


def main() -> int:
    visitedFiles: set[Path] = set()
    activeFiles: list[Path] = []
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
        for inputName in OS_BOOK_INPUT_PATTERN.findall(sourceText):
            visit(resolveInput(inputName))
        activeFiles.pop()
        visitedFiles.add(inputPath)

    visit(OS_BOOK_MAIN_FILE.resolve())
    if chapterCount < OS_BOOK_MINIMUM_CHAPTER_COUNT:
        raise SystemExit(
            f"正文章节不足：实际 {chapterCount}，"
            f"至少 {OS_BOOK_MINIMUM_CHAPTER_COUNT}"
        )

    print(
        f"书稿输入检查通过：{len(visitedFiles)} 个文件，"
        f"{chapterCount} 个正文章节。"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

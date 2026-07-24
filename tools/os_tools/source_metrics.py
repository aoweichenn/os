from dataclasses import dataclass
from pathlib import Path

from .errors import OsToolError


OS_SOURCE_METRICS_PRODUCTION_DIRECTORY = "source"
OS_SOURCE_METRICS_CPP_EXTENSIONS = frozenset(
    (".cpp", ".hpp")
)
OS_SOURCE_METRICS_ASSEMBLY_EXTENSIONS = frozenset((".asm",))
OS_SOURCE_METRICS_SUPPORTED_EXTENSIONS = (
    OS_SOURCE_METRICS_CPP_EXTENSIONS
    | OS_SOURCE_METRICS_ASSEMBLY_EXTENSIONS
)


@dataclass(frozen=True)
class SourceMetrics:
    fileCount: int
    physicalLineCount: int
    codeLineCount: int
    cppCodeLineCount: int
    assemblyCodeLineCount: int


def countCppCodeLines(sourceText: str) -> int:
    codeLineCount = 0
    insideBlockComment = False

    for sourceLine in sourceText.splitlines():
        remainingLine = sourceLine.strip()
        lineContainsCode = False

        while remainingLine:
            if insideBlockComment:
                commentEnd = remainingLine.find("*/")
                if commentEnd < 0:
                    remainingLine = ""
                else:
                    insideBlockComment = False
                    remainingLine = remainingLine[commentEnd + 2:].strip()
                continue

            if remainingLine.startswith("//"):
                remainingLine = ""
                continue

            blockCommentStart = remainingLine.find("/*")
            lineCommentStart = remainingLine.find("//")
            commentStarts = [
                commentStart
                for commentStart in (
                    blockCommentStart,
                    lineCommentStart,
                )
                if commentStart >= 0
            ]
            if not commentStarts:
                lineContainsCode = True
                remainingLine = ""
                continue

            firstCommentStart = min(commentStarts)
            if firstCommentStart > 0:
                lineContainsCode = True

            if firstCommentStart == lineCommentStart:
                remainingLine = ""
            else:
                insideBlockComment = True
                remainingLine = remainingLine[
                    firstCommentStart + 2:
                ].strip()

        if lineContainsCode:
            codeLineCount += 1

    return codeLineCount


def countAssemblyCodeLines(sourceText: str) -> int:
    return sum(
        1
        for sourceLine in sourceText.splitlines()
        if sourceLine.strip()
        and not sourceLine.lstrip().startswith(";")
    )


def collectSourceMetrics(projectRoot: Path) -> SourceMetrics:
    productionDirectory = (
        projectRoot / OS_SOURCE_METRICS_PRODUCTION_DIRECTORY
    )
    if not productionDirectory.is_dir():
        raise OsToolError(
            f"真实代码目录不存在：{productionDirectory}"
        )

    sourcePaths = sorted(
        sourcePath
        for sourcePath in productionDirectory.rglob("*")
        if sourcePath.is_file()
        and sourcePath.suffix in OS_SOURCE_METRICS_SUPPORTED_EXTENSIONS
    )

    physicalLineCount = 0
    cppCodeLineCount = 0
    assemblyCodeLineCount = 0
    for sourcePath in sourcePaths:
        sourceText = sourcePath.read_text(encoding="utf-8")
        physicalLineCount += len(sourceText.splitlines())
        if sourcePath.suffix in OS_SOURCE_METRICS_CPP_EXTENSIONS:
            cppCodeLineCount += countCppCodeLines(sourceText)
        else:
            assemblyCodeLineCount += countAssemblyCodeLines(sourceText)

    return SourceMetrics(
        fileCount=len(sourcePaths),
        physicalLineCount=physicalLineCount,
        codeLineCount=cppCodeLineCount + assemblyCodeLineCount,
        cppCodeLineCount=cppCodeLineCount,
        assemblyCodeLineCount=assemblyCodeLineCount,
    )


def renderLatexSourceMetrics(sourceMetrics: SourceMetrics) -> str:
    return "\n".join(
        (
            "% 该文件由 tools/os.py source-metrics 自动生成。",
            (
                "\\newcommand{\\OsProductionFileCount}"
                f"{{{sourceMetrics.fileCount}}}"
            ),
            (
                "\\newcommand{\\OsProductionPhysicalLineCount}"
                f"{{{sourceMetrics.physicalLineCount}}}"
            ),
            (
                "\\newcommand{\\OsProductionCodeLineCount}"
                f"{{{sourceMetrics.codeLineCount}}}"
            ),
            (
                "\\newcommand{\\OsProductionCppCodeLineCount}"
                f"{{{sourceMetrics.cppCodeLineCount}}}"
            ),
            (
                "\\newcommand{\\OsProductionAssemblyCodeLineCount}"
                f"{{{sourceMetrics.assemblyCodeLineCount}}}"
            ),
            "",
        )
    )


def reportSourceMetrics(
    projectRoot: Path,
    latexOutputPath: Path | None,
) -> None:
    sourceMetrics = collectSourceMetrics(projectRoot)
    print("真实代码统计：")
    print(f"  文件：{sourceMetrics.fileCount}")
    print(f"  物理行：{sourceMetrics.physicalLineCount}")
    print(f"  非空非纯注释代码行：{sourceMetrics.codeLineCount}")
    print(f"  C++：{sourceMetrics.cppCodeLineCount}")
    print(f"  NASM 汇编：{sourceMetrics.assemblyCodeLineCount}")
    print(
        "  口径：仅 source/ 下目标代码；排除构建描述、链接脚本、"
        "测试、工具、书籍和网站。"
    )

    if latexOutputPath is not None:
        latexOutputPath.parent.mkdir(parents=True, exist_ok=True)
        latexOutputPath.write_text(
            renderLatexSourceMetrics(sourceMetrics),
            encoding="utf-8",
        )

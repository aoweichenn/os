from hashlib import sha256
from pathlib import Path
import shutil
import tempfile

from .errors import OsToolError


OS_BOOK_EXPORT_SOURCE_RELATIVE_PATH = Path(
    "books/x86-64-os-from-reset/source/latex/main.pdf"
)
OS_BOOK_EXPORT_PHONE_ROOT = Path("/mnt/sdcard/STU/BOOKS")
OS_BOOK_EXPORT_PHONE_DIRECTORY = (
    OS_BOOK_EXPORT_PHONE_ROOT
    / "按卷类型"
    / "原理卷"
    / "从复位向量到自研x8664操作系统"
)
OS_BOOK_EXPORT_PHONE_FILE_NAME = "从复位向量到自研x8664操作系统.pdf"
OS_BOOK_EXPORT_COPY_BUFFER_SIZE_BYTES = 1024 * 1024
OS_BOOK_EXPORT_HYPHEN_LIKE_CHARACTERS = (
    "-",
    "‐",
    "‑",
    "‒",
    "–",
    "—",
    "―",
    "－",
    "−",
)
OS_BOOK_EXPORT_FORBIDDEN_FILE_NAME_CHARACTERS = (" ", "+", "/", "\\")


def calculateFileSha256(filePath: Path) -> str:
    fileHash = sha256()
    with filePath.open("rb") as file:
        while data := file.read(OS_BOOK_EXPORT_COPY_BUFFER_SIZE_BYTES):
            fileHash.update(data)
    return fileHash.hexdigest()


def validateExportDestination(
    destinationDirectory: Path,
    allowedRoot: Path,
    expectedFileName: str,
) -> None:
    if not expectedFileName.endswith(".pdf"):
        raise OsToolError(f"手机教材必须使用 PDF 文件名：{expectedFileName}")
    if any(
        character in expectedFileName
        for character in OS_BOOK_EXPORT_FORBIDDEN_FILE_NAME_CHARACTERS
    ):
        raise OsToolError(
            f"手机教材文件名包含不兼容字符：{expectedFileName}"
        )
    if any(
        character in expectedFileName
        for character in OS_BOOK_EXPORT_HYPHEN_LIKE_CHARACTERS
    ):
        raise OsToolError(
            f"手机教材文件名不能包含短横线类字符：{expectedFileName}"
        )

    resolvedDestination = destinationDirectory.resolve()
    resolvedAllowedRoot = allowedRoot.resolve()
    if (
        resolvedDestination == resolvedAllowedRoot
        or resolvedAllowedRoot not in resolvedDestination.parents
    ):
        raise OsToolError(
            f"拒绝向手机书库范围外导出：{destinationDirectory}"
        )

    if not destinationDirectory.exists():
        return

    unexpectedEntries = sorted(
        entry.name
        for entry in destinationDirectory.iterdir()
        if entry.name != expectedFileName
    )
    if unexpectedEntries:
        raise OsToolError(
            "独立书籍目录包含非预期内容，拒绝覆盖："
            + "、".join(unexpectedEntries)
        )


def exportBookPdf(
    sourcePdfPath: Path,
    destinationDirectory: Path,
    exportFileName: str,
    allowedRoot: Path,
) -> Path:
    if not sourcePdfPath.is_file():
        raise OsToolError(f"教材 PDF 不存在：{sourcePdfPath}")

    validateExportDestination(
        destinationDirectory,
        allowedRoot,
        exportFileName,
    )
    destinationDirectory.mkdir(parents=True, exist_ok=True)
    destinationPdfPath = destinationDirectory / exportFileName

    with tempfile.NamedTemporaryFile(
        dir=destinationDirectory,
        prefix=".os_book_export_",
        suffix=".pdf",
        delete=False,
    ) as temporaryFile:
        temporaryPdfPath = Path(temporaryFile.name)

    try:
        shutil.copy2(sourcePdfPath, temporaryPdfPath)
        if calculateFileSha256(sourcePdfPath) != calculateFileSha256(
            temporaryPdfPath
        ):
            raise OsToolError("教材复制后的 SHA-256 与源文件不一致")
        temporaryPdfPath.replace(destinationPdfPath)
    finally:
        temporaryPdfPath.unlink(missing_ok=True)

    return destinationPdfPath


def exportBookToPhone(projectRoot: Path) -> None:
    sourcePdfPath = (
        projectRoot / OS_BOOK_EXPORT_SOURCE_RELATIVE_PATH
    )
    destinationPdfPath = exportBookPdf(
        sourcePdfPath,
        OS_BOOK_EXPORT_PHONE_DIRECTORY,
        OS_BOOK_EXPORT_PHONE_FILE_NAME,
        OS_BOOK_EXPORT_PHONE_ROOT,
    )
    print(f"手机教材已导出：{destinationPdfPath}")
    print(f"文件大小：{destinationPdfPath.stat().st_size} 字节")
    print(f"SHA-256：{calculateFileSha256(destinationPdfPath)}")

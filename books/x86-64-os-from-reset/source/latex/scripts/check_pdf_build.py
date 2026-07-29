#!/usr/bin/env python3

from pathlib import Path
import shutil
import subprocess


OS_BOOK_ROOT = Path(__file__).resolve().parents[1]
OS_BOOK_PDF_FILE = OS_BOOK_ROOT / "main.pdf"
OS_BOOK_LOG_FILE = OS_BOOK_ROOT / "main.log"
OS_BOOK_OUTLINE_FILE = OS_BOOK_ROOT / "main.out"
OS_BOOK_FORBIDDEN_LOG_FRAGMENTS = (
    "Overfull \\hbox",
    "Overfull \\vbox",
    "undefined references",
    "undefined citations",
    "destination with the same identifier",
)
OS_BOOK_REQUIRED_PDF_FONTS = (
    "MapleMono",
)


def main() -> int:
    for artifactPath in (
        OS_BOOK_PDF_FILE,
        OS_BOOK_LOG_FILE,
        OS_BOOK_OUTLINE_FILE,
    ):
        if not artifactPath.is_file():
            raise SystemExit(f"缺少书稿构建产物：{artifactPath}")

    logText = OS_BOOK_LOG_FILE.read_text(encoding="utf-8", errors="replace")
    for forbiddenFragment in OS_BOOK_FORBIDDEN_LOG_FRAGMENTS:
        if forbiddenFragment.lower() in logText.lower():
            raise SystemExit(
                "书稿构建日志包含未处理问题："
                f"{forbiddenFragment}"
            )

    mutoolPath = shutil.which("mutool")
    if mutoolPath is None:
        raise SystemExit("缺少 PDF 检查工具：mutool")

    fontResult = subprocess.run(
        [mutoolPath, "info", "-F", str(OS_BOOK_PDF_FILE)],
        check=True,
        capture_output=True,
        text=True,
    )
    fontText = fontResult.stdout
    for fontName in OS_BOOK_REQUIRED_PDF_FONTS:
        if fontName not in fontText:
            raise SystemExit(
                "书稿 PDF 没有嵌入预期字体："
                f"{fontName}"
            )

    outlineResult = subprocess.run(
        [mutoolPath, "show", str(OS_BOOK_PDF_FILE), "outline"],
        check=True,
        capture_output=True,
        text=True,
    )
    outlineText = outlineResult.stdout
    topicBookmarkCount = outlineText.count("#nameddest=os-topic-")
    if topicBookmarkCount < 100:
        raise SystemExit(
            "章内 PDF 书签数量异常："
            f"实际 {topicBookmarkCount}，至少需要 100"
        )

    print(
        "书稿 PDF 检查通过："
        f"{OS_BOOK_PDF_FILE.stat().st_size} 字节，"
        f"{topicBookmarkCount} 个章内 topic 书签，"
        "正文、标题和代码所用 Maple Mono 字体已嵌入，构建日志无越界。"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

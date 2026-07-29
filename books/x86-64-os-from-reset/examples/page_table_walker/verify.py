#!/usr/bin/env python3

from pathlib import Path
import shutil
import subprocess
import tempfile


OS_BOOK_PAGING_EXAMPLE_ROOT = Path(__file__).resolve().parent
OS_BOOK_PAGING_INCLUDE_DIRECTORY = OS_BOOK_PAGING_EXAMPLE_ROOT / "include"
OS_BOOK_PAGING_SOURCE_DIRECTORY = OS_BOOK_PAGING_EXAMPLE_ROOT / "source"
OS_BOOK_PAGING_REQUIRED_OUTPUT = (
    "scenario=4k-read status=translated pa=0x12345234",
    "scenario=2m-read status=translated pa=0x5abcde",
    "scenario=nx-execute status=execute-denied",
    "error=0x15",
    "scenario=parent-read-only-write status=write-denied",
    "error=0x7",
    "scenario=not-present-user-read status=not-present",
    "error=0x4",
    "level=4",
    "level=1",
)


def FindCompiler() -> str:
    for compiler_name in ("clang++", "g++"):
        compiler_path = shutil.which(compiler_name)
        if compiler_path is not None:
            return compiler_path
    raise SystemExit("页表遍历器检查失败：没有找到 clang++ 或 g++")


def main() -> int:
    compiler_path = FindCompiler()
    with tempfile.TemporaryDirectory(prefix="os-book-page-table-") as directory:
        executable_path = Path(directory) / "page_table_walker"
        compile_command = (
            compiler_path,
            "-std=c++20",
            "-Wall",
            "-Wextra",
            "-Wpedantic",
            "-Werror",
            f"-I{OS_BOOK_PAGING_INCLUDE_DIRECTORY}",
            str(OS_BOOK_PAGING_SOURCE_DIRECTORY / "page_table_walker.cpp"),
            str(OS_BOOK_PAGING_SOURCE_DIRECTORY / "main.cpp"),
            "-o",
            str(executable_path),
        )
        subprocess.run(compile_command, check=True)
        execution = subprocess.run(
            (str(executable_path),),
            check=True,
            capture_output=True,
            text=True,
        )

    for required_output in OS_BOOK_PAGING_REQUIRED_OUTPUT:
        if required_output not in execution.stdout:
            raise SystemExit(
                "页表遍历器检查失败：输出缺少 "
                f"{required_output!r}"
            )

    print(
        "页表遍历器检查通过："
        "覆盖 4 KiB、2 MiB、父级只读、NX 和 not-present。"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3

from pathlib import Path
import shutil
import subprocess
import tempfile


OS_BOOK_CPU_EXAMPLE_ROOT = Path(__file__).resolve().parent
OS_BOOK_CPU_INCLUDE_DIRECTORY = OS_BOOK_CPU_EXAMPLE_ROOT / "include"
OS_BOOK_CPU_SOURCE_DIRECTORY = OS_BOOK_CPU_EXAMPLE_ROOT / "source"
OS_BOOK_CPU_REQUIRED_OUTPUT = (
    "result status=halted",
    "scenario=ready-delay",
    "result status=illegal-opcode",
    "result status=bus-no-response",
    "ready=0",
    "phase=stack-write",
    "phase=stack-read",
)


def FindCompiler() -> str:
    for compiler_name in ("clang++", "g++"):
        compiler_path = shutil.which(compiler_name)
        if compiler_path is not None:
            return compiler_path
    raise SystemExit("教学 CPU 检查失败：没有找到 clang++ 或 g++")


def main() -> int:
    compiler_path = FindCompiler()
    with tempfile.TemporaryDirectory(prefix="os-book-teaching-cpu-") as directory:
        executable_path = Path(directory) / "teaching_cpu"
        compile_command = (
            compiler_path,
            "-std=c++20",
            "-Wall",
            "-Wextra",
            "-Wpedantic",
            "-Werror",
            f"-I{OS_BOOK_CPU_INCLUDE_DIRECTORY}",
            str(OS_BOOK_CPU_SOURCE_DIRECTORY / "teaching_cpu.cpp"),
            str(OS_BOOK_CPU_SOURCE_DIRECTORY / "main.cpp"),
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

    for required_output in OS_BOOK_CPU_REQUIRED_OUTPUT:
        if required_output not in execution.stdout:
            raise SystemExit(
                "教学 CPU 检查失败：输出缺少 "
                f"{required_output!r}"
            )

    trace_line_count = execution.stdout.count("cycle=")
    if trace_line_count < 100:
        raise SystemExit(
            "教学 CPU 检查失败：逐周期 trace 过短，"
            f"实际只有 {trace_line_count} 行"
        )

    print(
        "教学 CPU 检查通过："
        f"{trace_line_count} 行逐周期 trace，"
        "覆盖条件跳转、CALL/RET、ready 延迟、非法操作码和总线无响应。"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

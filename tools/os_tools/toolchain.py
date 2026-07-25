from dataclasses import dataclass
from pathlib import Path
import shutil

from .errors import OsToolError
from .process import runCommand


@dataclass(frozen=True)
class ToolRequirement:
    displayName: str
    executableName: str


OS_TOOLCHAIN_REQUIRED_TOOLS = (
    ToolRequirement("Python", "python3"),
    ToolRequirement("Clang", "clang++"),
    ToolRequirement("Clang-Tidy", "clang-tidy"),
    ToolRequirement("LLD", "ld.lld"),
    ToolRequirement("NASM", "nasm"),
    ToolRequirement("QEMU", "qemu-system-x86_64"),
    ToolRequirement("GDB", "gdb"),
    ToolRequirement("CMake", "cmake"),
    ToolRequirement("Ninja", "ninja"),
    ToolRequirement("CTest", "ctest"),
    ToolRequirement("LLVM nm", "llvm-nm"),
    ToolRequirement("LLVM objdump", "llvm-objdump"),
    ToolRequirement("LLVM objcopy", "llvm-objcopy"),
    ToolRequirement("LLVM readelf", "llvm-readelf"),
)
OS_TOOLCHAIN_VERSION_ARGUMENT = "--version"


def firstOutputLine(output: str) -> str:
    lines = output.splitlines()
    return lines[0] if lines else "无法读取版本"


def checkToolchain(projectRoot: Path) -> None:
    missingTools = [
        requirement.displayName
        for requirement in OS_TOOLCHAIN_REQUIRED_TOOLS
        if shutil.which(requirement.executableName) is None
    ]

    if missingTools:
        missingToolList = "、".join(missingTools)
        raise OsToolError(f"缺少必要工具：{missingToolList}")

    print("工具链检查通过：")
    for requirement in OS_TOOLCHAIN_REQUIRED_TOOLS:
        completedProcess = runCommand(
            [
                requirement.executableName,
                OS_TOOLCHAIN_VERSION_ARGUMENT,
            ],
            projectRoot,
            captureOutput=True,
        )
        print(
            f"  {requirement.displayName}: "
            f"{firstOutputLine(completedProcess.stdout)}"
        )

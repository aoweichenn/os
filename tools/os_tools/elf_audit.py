from pathlib import Path

from .errors import OsToolError
from .process import runCommand


OS_ELF_AUDIT_EXPECTED_MACHINE = "Advanced Micro Devices X86-64"


def parseUndefinedSymbols(llvmNmOutput: str) -> tuple[str, ...]:
    return tuple(
        line.strip()
        for line in llvmNmOutput.splitlines()
        if line.strip() and not line.rstrip().endswith(":")
    )


def isExpectedMachine(llvmReadelfOutput: str) -> bool:
    return OS_ELF_AUDIT_EXPECTED_MACHINE in llvmReadelfOutput


def auditFreestandingLibrary(projectRoot: Path, libraryPath: Path) -> None:
    undefinedSymbolResult = runCommand(
        ["llvm-nm", "--undefined-only", str(libraryPath)],
        projectRoot,
        captureOutput=True,
    )
    undefinedSymbols = parseUndefinedSymbols(undefinedSymbolResult.stdout)
    if undefinedSymbols:
        formattedSymbols = "\n".join(undefinedSymbols)
        raise OsToolError(f"发现未解析运行时符号：\n{formattedSymbols}")

    fileHeaderResult = runCommand(
        ["llvm-readelf", "--file-header", str(libraryPath)],
        projectRoot,
        captureOutput=True,
    )
    if not isExpectedMachine(fileHeaderResult.stdout):
        raise OsToolError("freestanding 产物不是 x86-64 ELF。")

    print(f"freestanding 符号审计通过：{libraryPath}")

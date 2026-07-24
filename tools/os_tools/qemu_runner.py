from pathlib import Path
import subprocess

from .errors import OsToolError
from .process import runCommand


OS_QEMU_SMOKE_TIMEOUT_SECONDS = 2.0
OS_QEMU_GUEST_MEMORY_MEBIBYTES = 64


def validateImageSize(
    imagePath: Path,
    expectedSizeBytes: int,
    imageDescription: str,
) -> None:
    actualSizeBytes = imagePath.stat().st_size
    if actualSizeBytes != expectedSizeBytes:
        raise OsToolError(
            f"{imageDescription}大小不正确："
            f"实际 {actualSizeBytes} 字节，预期 {expectedSizeBytes} 字节"
        )


def runQemuHardwareSmoke(
    projectRoot: Path,
    firmwareImagePath: Path,
    diskImagePath: Path,
    expectedFirmwareSizeBytes: int,
    expectedDiskSizeBytes: int,
) -> None:
    validateImageSize(
        firmwareImagePath,
        expectedFirmwareSizeBytes,
        "空固件镜像",
    )
    validateImageSize(
        diskImagePath,
        expectedDiskSizeBytes,
        "空磁盘镜像",
    )

    command = [
        "qemu-system-x86_64",
        "-machine",
        "pc,accel=tcg",
        "-cpu",
        "qemu64",
        "-m",
        str(OS_QEMU_GUEST_MEMORY_MEBIBYTES),
        "-nodefaults",
        "-display",
        "none",
        "-serial",
        "none",
        "-monitor",
        "none",
        "-S",
        "-no-reboot",
        "-no-shutdown",
        "-bios",
        str(firmwareImagePath),
        "-drive",
        f"file={diskImagePath},format=raw,if=ide",
    ]

    try:
        completedProcess = runCommand(
            command,
            projectRoot,
            check=False,
            timeoutSeconds=OS_QEMU_SMOKE_TIMEOUT_SECONDS,
        )
    except subprocess.TimeoutExpired:
        print("QEMU TCG 已使用自定义空固件与空磁盘稳定启动。")
        return

    raise OsToolError(
        f"QEMU 硬件冒烟测试异常提前退出：{completedProcess.returncode}"
    )

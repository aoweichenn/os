from pathlib import Path
import subprocess

from .errors import OsToolError
from .process import runCommand


OS_QEMU_SMOKE_TIMEOUT_SECONDS = 2.0
OS_QEMU_GUEST_MEMORY_MEBIBYTES = 64
OS_QEMU_FIRMWARE_RESET_MARKER = "[OS][FIRMWARE] RESET"
OS_QEMU_FIRMWARE_SERIAL_READY_MARKER = "[OS][FIRMWARE] SERIAL_READY"


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


def createQemuFirmwareCommand(
    firmwareImagePath: Path,
    diskImagePath: Path,
) -> list[str]:
    return [
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
        "stdio",
        "-monitor",
        "none",
        "-no-reboot",
        "-no-shutdown",
        "-bios",
        str(firmwareImagePath),
        "-drive",
        f"file={diskImagePath},format=raw,if=ide",
    ]


def normalizeCapturedOutput(output: str | bytes | None) -> str:
    if output is None:
        return ""
    if isinstance(output, bytes):
        return output.decode("utf-8", errors="replace")
    return output


def validateSerialProtocol(
    serialOutput: str,
    requiredMarkers: tuple[str, ...],
    forbiddenMarkers: tuple[str, ...],
) -> None:
    for requiredMarker in requiredMarkers:
        if requiredMarker not in serialOutput:
            raise OsToolError(
                f"串口输出缺少必需标记：{requiredMarker!r}"
            )

    for forbiddenMarker in forbiddenMarkers:
        if forbiddenMarker in serialOutput:
            raise OsToolError(
                f"串口输出包含禁止标记：{forbiddenMarker!r}"
            )


def runQemuFirmwareBoot(
    projectRoot: Path,
    firmwareImagePath: Path,
    diskImagePath: Path,
    expectedFirmwareSizeBytes: int,
    expectedDiskSizeBytes: int,
    requiredMarkers: tuple[str, ...],
    forbiddenMarkers: tuple[str, ...],
) -> None:
    validateImageSize(
        firmwareImagePath,
        expectedFirmwareSizeBytes,
        "固件 ROM",
    )
    validateImageSize(
        diskImagePath,
        expectedDiskSizeBytes,
        "空磁盘镜像",
    )

    command = createQemuFirmwareCommand(
        firmwareImagePath,
        diskImagePath,
    )
    try:
        completedProcess = runCommand(
            command,
            projectRoot,
            captureOutput=True,
            check=False,
            timeoutSeconds=OS_QEMU_SMOKE_TIMEOUT_SECONDS,
        )
    except subprocess.TimeoutExpired as error:
        serialOutput = normalizeCapturedOutput(error.stdout)
        validateSerialProtocol(
            serialOutput,
            requiredMarkers,
            forbiddenMarkers,
        )
        print(serialOutput, end="")
        print("QEMU 固件串口协议验收通过。")
        return

    raise OsToolError(
        f"QEMU 固件测试异常提前退出：{completedProcess.returncode}"
    )

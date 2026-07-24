from pathlib import Path
import shlex
import subprocess
import threading
import time

from .errors import OsToolError
from .process import runCommand


OS_QEMU_SMOKE_TIMEOUT_SECONDS = 2.0
OS_QEMU_TERMINATION_TIMEOUT_SECONDS = 1.0
OS_QEMU_GUEST_MEMORY_MEBIBYTES = 64
OS_QEMU_FIRMWARE_RESET_MARKER = "[OS][FIRMWARE] RESET"
OS_QEMU_FIRMWARE_SERIAL_READY_MARKER = "[OS][FIRMWARE] SERIAL_READY"
OS_QEMU_FIRMWARE_CLOCK_READY_MARKER = "[OS][FIRMWARE] CLOCK_READY"
OS_QEMU_FIRMWARE_STAGE1_HEADER_VALID_MARKER = (
    "[OS][FIRMWARE] STAGE1_HEADER_VALID"
)
OS_QEMU_FIRMWARE_STAGE1_LOADED_MARKER = "[OS][FIRMWARE] STAGE1_LOADED"
OS_QEMU_FIRMWARE_IDE_TIMEOUT_MARKER = "[OS][FIRMWARE] IDE_TIMEOUT"
OS_QEMU_FIRMWARE_IDE_ERROR_MARKER = "[OS][FIRMWARE] IDE_ERROR"
OS_QEMU_FIRMWARE_STAGE1_HEADER_INVALID_MARKER = (
    "[OS][FIRMWARE] STAGE1_HEADER_INVALID"
)
OS_QEMU_FIRMWARE_STAGE1_CHECKSUM_INVALID_MARKER = (
    "[OS][FIRMWARE] STAGE1_CHECKSUM_INVALID"
)
OS_QEMU_STAGE1_ENTERED_MARKER = "[OS][STAGE1] ENTERED"
OS_QEMU_STAGE1_A20_READY_MARKER = "[OS][STAGE1] A20_READY"
OS_QEMU_STAGE1_A20_INVALID_MARKER = "[OS][STAGE1] A20_INVALID"
OS_QEMU_STAGE1_GDT_READY_MARKER = "[OS][STAGE1] GDT_READY"
OS_QEMU_STAGE1_PROTECTED_MODE_MARKER = "[OS][STAGE1] PROTECTED_MODE"
OS_QEMU_STAGE1_PAGE_TABLES_READY_MARKER = (
    "[OS][STAGE1] PAGE_TABLES_READY"
)
OS_QEMU_STAGE1_PAGE_TABLES_INVALID_MARKER = (
    "[OS][STAGE1] PAGE_TABLES_INVALID"
)
OS_QEMU_STAGE1_PAE_READY_MARKER = "[OS][STAGE1] PAE_READY"
OS_QEMU_STAGE1_PAE_INVALID_MARKER = "[OS][STAGE1] PAE_INVALID"
OS_QEMU_STAGE1_LME_READY_MARKER = "[OS][STAGE1] LME_READY"
OS_QEMU_STAGE1_LME_INVALID_MARKER = "[OS][STAGE1] LME_INVALID"
OS_QEMU_STAGE1_PAGING_ENABLED_MARKER = "[OS][STAGE1] PAGING_ENABLED"
OS_QEMU_STAGE1_PAGING_INVALID_MARKER = "[OS][STAGE1] PAGING_INVALID"
OS_QEMU_STAGE1_LONG_MODE_MARKER = "[OS][STAGE1] LONG_MODE"
OS_QEMU_STAGE1_KERNEL_HEADER_VALID_MARKER = (
    "[OS][STAGE1] KERNEL_HEADER_VALID"
)
OS_QEMU_STAGE1_KERNEL_PAYLOAD_VALID_MARKER = (
    "[OS][STAGE1] KERNEL_PAYLOAD_VALID"
)
OS_QEMU_STAGE1_KERNEL_ELF_VALID_MARKER = (
    "[OS][STAGE1] KERNEL_ELF_VALID"
)
OS_QEMU_STAGE1_KERNEL_SEGMENTS_LOADED_MARKER = (
    "[OS][STAGE1] KERNEL_SEGMENTS_LOADED"
)
OS_QEMU_STAGE1_BOOT_INFO_READY_MARKER = (
    "[OS][STAGE1] BOOT_INFO_READY"
)
OS_QEMU_STAGE1_KERNEL_TRANSFER_MARKER = (
    "[OS][STAGE1] KERNEL_TRANSFER"
)
OS_QEMU_STAGE1_KERNEL_HEADER_INVALID_MARKER = (
    "[OS][STAGE1] KERNEL_HEADER_INVALID"
)
OS_QEMU_STAGE1_KERNEL_CHECKSUM_INVALID_MARKER = (
    "[OS][STAGE1] KERNEL_CHECKSUM_INVALID"
)
OS_QEMU_STAGE1_KERNEL_ELF_INVALID_MARKER = (
    "[OS][STAGE1] KERNEL_ELF_INVALID"
)
OS_QEMU_STAGE1_KERNEL_ATA_TIMEOUT_MARKER = (
    "[OS][STAGE1] KERNEL_ATA_TIMEOUT"
)
OS_QEMU_STAGE1_KERNEL_ATA_ERROR_MARKER = (
    "[OS][STAGE1] KERNEL_ATA_ERROR"
)
OS_QEMU_STAGE1_KERNEL_RETURNED_MARKER = (
    "[OS][STAGE1] KERNEL_RETURNED"
)
OS_QEMU_KERNEL_ENTERED_MARKER = "[OS][KERNEL] ENTERED"
OS_QEMU_KERNEL_BOOT_INFO_VALID_MARKER = (
    "[OS][KERNEL] BOOT_INFO_VALID"
)
OS_QEMU_KERNEL_BOOT_INFO_INVALID_MARKER = (
    "[OS][KERNEL] BOOT_INFO_INVALID"
)
OS_QEMU_KERNEL_BSS_ZEROED_MARKER = "[OS][KERNEL] BSS_ZEROED"
OS_QEMU_KERNEL_BSS_INVALID_MARKER = "[OS][KERNEL] BSS_INVALID"
OS_QEMU_KERNEL_CR3_VALID_MARKER = "[OS][KERNEL] CR3_VALID"
OS_QEMU_KERNEL_CR3_INVALID_MARKER = "[OS][KERNEL] CR3_INVALID"
OS_QEMU_KERNEL_GDT_READY_MARKER = "[OS][KERNEL] GDT_READY"
OS_QEMU_KERNEL_TSS_READY_MARKER = "[OS][KERNEL] TSS_READY"
OS_QEMU_KERNEL_IDT_READY_MARKER = "[OS][KERNEL] IDT_READY"
OS_QEMU_KERNEL_DESCRIPTOR_TABLES_VALID_MARKER = (
    "[OS][KERNEL] DESCRIPTOR_TABLES_VALID"
)
OS_QEMU_KERNEL_DESCRIPTOR_TABLES_INVALID_MARKER = (
    "[OS][KERNEL] DESCRIPTOR_TABLES_INVALID"
)
OS_QEMU_KERNEL_BREAKPOINT_HANDLED_MARKER = (
    "[OS][KERNEL] BREAKPOINT_HANDLED"
)
OS_QEMU_KERNEL_EXCEPTION_SELF_TEST_READY_MARKER = (
    "[OS][KERNEL] EXCEPTION_SELF_TEST_READY"
)
OS_QEMU_KERNEL_INVALID_OPCODE_INJECTION_MARKER = (
    "[OS][KERNEL] FAULT_INJECTION=INVALID_OPCODE"
)
OS_QEMU_KERNEL_PAGE_FAULT_INJECTION_MARKER = (
    "[OS][KERNEL] FAULT_INJECTION=PAGE_FAULT"
)
OS_QEMU_KERNEL_EXCEPTION_MARKER = "[OS][KERNEL] EXCEPTION\n"
OS_QEMU_KERNEL_INVALID_OPCODE_VECTOR_MARKER = (
    "[OS][KERNEL] EXCEPTION_VECTOR=0x0000000000000006"
)
OS_QEMU_KERNEL_PAGE_FAULT_VECTOR_MARKER = (
    "[OS][KERNEL] EXCEPTION_VECTOR=0x000000000000000E"
)
OS_QEMU_KERNEL_EXCEPTION_ZERO_ERROR_CODE_MARKER = (
    "[OS][KERNEL] EXCEPTION_ERROR_CODE=0x0000000000000000"
)
OS_QEMU_KERNEL_PAGE_FAULT_ADDRESS_MARKER = (
    "[OS][KERNEL] PAGE_FAULT_ADDRESS=0x0000000004000000"
)
OS_QEMU_KERNEL_PANIC_MARKER = "[OS][KERNEL] PANIC"
OS_QEMU_KERNEL_FILE_SIZE_MARKER = "[OS][KERNEL] FILE_SIZE=0x"
OS_QEMU_KERNEL_LOAD_SEGMENTS_MARKER = (
    "[OS][KERNEL] LOAD_SEGMENTS=0x"
)
OS_QEMU_KERNEL_READY_MARKER = "[OS][KERNEL] READY"


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
        f"file={diskImagePath},format=raw,if=ide,snapshot=on",
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
        f"file={diskImagePath},format=raw,if=ide,snapshot=on",
    ]


def normalizeCapturedOutput(output: str | bytes | None) -> str:
    if output is None:
        return ""
    if isinstance(output, bytes):
        return output.decode("utf-8", errors="replace")
    return output


def runQemuWithTimedSerial(
    command: list[str],
    projectRoot: Path,
    timeoutSeconds: float,
) -> tuple[str, str, bool, int]:
    """逐行捕获串口，并用宿主单调时钟记录每行抵达时间。"""
    normalizedCommand = [str(argument) for argument in command]
    print(f"+ {shlex.join(normalizedCommand)}", flush=True)

    startTime = time.monotonic()
    serialLines: list[str] = []
    timedLines: list[str] = []
    qemuProcess = subprocess.Popen(
        normalizedCommand,
        cwd=projectRoot,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
        bufsize=1,
    )

    def captureSerialLines() -> None:
        if qemuProcess.stdout is None:
            return
        for line in qemuProcess.stdout:
            elapsedMilliseconds = int(
                (time.monotonic() - startTime) * 1000
            )
            serialLines.append(line)
            timedLines.append(
                f"[QEMU][T+{elapsedMilliseconds:06d}ms] "
                f"{line.rstrip('\r\n')}\n"
            )

    captureThread = threading.Thread(
        target=captureSerialLines,
        name="os-qemu-serial-capture",
        daemon=True,
    )
    captureThread.start()

    timedOut = False
    try:
        try:
            returnCode = qemuProcess.wait(timeout=timeoutSeconds)
        except subprocess.TimeoutExpired:
            timedOut = True
            qemuProcess.terminate()
            try:
                returnCode = qemuProcess.wait(
                    timeout=OS_QEMU_TERMINATION_TIMEOUT_SECONDS
                )
            except subprocess.TimeoutExpired:
                qemuProcess.kill()
                returnCode = qemuProcess.wait()
    finally:
        if qemuProcess.poll() is None:
            qemuProcess.kill()
            qemuProcess.wait()
        captureThread.join()
        if qemuProcess.stdout is not None:
            qemuProcess.stdout.close()

    return (
        "".join(serialLines),
        "".join(timedLines),
        timedOut,
        returnCode,
    )


def validateSerialProtocol(
    serialOutput: str,
    requiredMarkers: tuple[str, ...],
    forbiddenMarkers: tuple[str, ...],
) -> None:
    previousMarkerPosition = 0
    for requiredMarker in requiredMarkers:
        markerPosition = serialOutput.find(
            requiredMarker,
            previousMarkerPosition,
        )
        if markerPosition < 0:
            raise OsToolError(
                "串口输出缺少必需标记或标记顺序错误："
                f"{requiredMarker!r}"
            )
        previousMarkerPosition = markerPosition + len(requiredMarker)

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
        "启动磁盘镜像",
    )

    command = createQemuFirmwareCommand(
        firmwareImagePath,
        diskImagePath,
    )
    serialOutput, timedSerialOutput, timedOut, returnCode = (
        runQemuWithTimedSerial(
            command,
            projectRoot,
            OS_QEMU_SMOKE_TIMEOUT_SECONDS,
        )
    )
    if timedOut:
        validateSerialProtocol(
            serialOutput,
            requiredMarkers,
            forbiddenMarkers,
        )
        print(timedSerialOutput, end="")
        print("QEMU 固件串口协议验收通过。")
        return

    raise OsToolError(
        f"QEMU 固件测试异常提前退出：{returnCode}"
    )

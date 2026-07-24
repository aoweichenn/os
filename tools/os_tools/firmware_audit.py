from pathlib import Path

from .errors import OsToolError


OS_FIRMWARE_AUDIT_ROM_SIZE_BYTES = 128 * 1024
OS_FIRMWARE_AUDIT_ENTRY_FILE_OFFSET = 0x1F000
OS_FIRMWARE_AUDIT_RESET_VECTOR_FILE_OFFSET = 0x1FFF0
OS_FIRMWARE_AUDIT_RESET_VECTOR_RUNTIME_OFFSET = 0xFFF0
OS_FIRMWARE_AUDIT_ENTRY_RUNTIME_OFFSET = 0xF000
OS_FIRMWARE_AUDIT_NEAR_JUMP_OPCODE = 0xE9
OS_FIRMWARE_AUDIT_NEAR_JUMP_SIZE_BYTES = 3
OS_FIRMWARE_AUDIT_RUNTIME_OFFSET_MASK = 0xFFFF
OS_FIRMWARE_AUDIT_ENTRY_PREFIX = bytes((0xFA, 0xFC))


def decodeResetVectorTarget(firmwareImage: bytes) -> int:
    resetVectorOffset = OS_FIRMWARE_AUDIT_RESET_VECTOR_FILE_OFFSET
    opcode = firmwareImage[resetVectorOffset]
    if opcode != OS_FIRMWARE_AUDIT_NEAR_JUMP_OPCODE:
        raise OsToolError(
            "复位向量不是 16 位 near jump："
            f"实际操作码 0x{opcode:02X}，"
            f"预期 0x{OS_FIRMWARE_AUDIT_NEAR_JUMP_OPCODE:02X}"
        )

    displacementStart = resetVectorOffset + 1
    displacementEnd = (
        resetVectorOffset + OS_FIRMWARE_AUDIT_NEAR_JUMP_SIZE_BYTES
    )
    displacement = int.from_bytes(
        firmwareImage[displacementStart:displacementEnd],
        byteorder="little",
        signed=True,
    )
    nextInstructionOffset = (
        OS_FIRMWARE_AUDIT_RESET_VECTOR_RUNTIME_OFFSET
        + OS_FIRMWARE_AUDIT_NEAR_JUMP_SIZE_BYTES
    )
    return (
        nextInstructionOffset + displacement
    ) & OS_FIRMWARE_AUDIT_RUNTIME_OFFSET_MASK


def auditFirmwareImageBytes(firmwareImage: bytes) -> None:
    actualSizeBytes = len(firmwareImage)
    if actualSizeBytes != OS_FIRMWARE_AUDIT_ROM_SIZE_BYTES:
        raise OsToolError(
            "固件 ROM 大小不正确："
            f"实际 {actualSizeBytes} 字节，"
            f"预期 {OS_FIRMWARE_AUDIT_ROM_SIZE_BYTES} 字节"
        )

    resetTarget = decodeResetVectorTarget(firmwareImage)
    if resetTarget != OS_FIRMWARE_AUDIT_ENTRY_RUNTIME_OFFSET:
        raise OsToolError(
            "复位向量目标不正确："
            f"实际 0x{resetTarget:04X}，"
            f"预期 0x{OS_FIRMWARE_AUDIT_ENTRY_RUNTIME_OFFSET:04X}"
        )

    entryStart = OS_FIRMWARE_AUDIT_ENTRY_FILE_OFFSET
    entryEnd = entryStart + len(OS_FIRMWARE_AUDIT_ENTRY_PREFIX)
    if firmwareImage[entryStart:entryEnd] != OS_FIRMWARE_AUDIT_ENTRY_PREFIX:
        raise OsToolError("固件入口没有以 CLI、CLD 建立确定的处理器状态")


def auditFirmwareImage(firmwareImagePath: Path) -> None:
    auditFirmwareImageBytes(firmwareImagePath.read_bytes())
    print(
        "固件 ROM 审计通过："
        "128 KiB 布局、0xFFFFFFF0 复位向量和 0xFFFFF000 入口一致。"
    )

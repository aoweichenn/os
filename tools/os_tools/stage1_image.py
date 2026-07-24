from dataclasses import dataclass
from pathlib import Path
import struct

from .errors import OsToolError


OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES = 512
OS_STAGE1_IMAGE_DESCRIPTOR_LBA = 0
OS_STAGE1_IMAGE_DEFAULT_PAYLOAD_LBA = 1
OS_STAGE1_IMAGE_MAGIC = b"OSSTAGE1"
OS_STAGE1_IMAGE_VERSION = 1
OS_STAGE1_IMAGE_HEADER_SIZE_BYTES = 28
OS_STAGE1_IMAGE_MAXIMUM_PAYLOAD_SECTORS = 64
OS_STAGE1_IMAGE_LBA28_MAXIMUM = 0x0FFF_FFFF
OS_STAGE1_IMAGE_MINIMUM_LOAD_ADDRESS = 0x0000_8000
OS_STAGE1_IMAGE_MAXIMUM_LOAD_END_ADDRESS = 0x0009_FC00
OS_STAGE1_IMAGE_DEFAULT_LOAD_SEGMENT = 0x0800
OS_STAGE1_IMAGE_DEFAULT_ENTRY_OFFSET = 0
OS_STAGE1_IMAGE_FLAGS_NONE = 0
OS_STAGE1_IMAGE_UINT16_MAXIMUM = 0xFFFF
OS_STAGE1_IMAGE_CORRUPTION_BIT = 0x01
OS_STAGE1_IMAGE_HEADER_STRUCT_FORMAT = "<8sHHHHHHIHH"

OS_STAGE1_IMAGE_MAGIC_OFFSET = 0
OS_STAGE1_IMAGE_VERSION_OFFSET = 8
OS_STAGE1_IMAGE_HEADER_SIZE_OFFSET = 10
OS_STAGE1_IMAGE_LOAD_SEGMENT_OFFSET = 12
OS_STAGE1_IMAGE_ENTRY_OFFSET_OFFSET = 14
OS_STAGE1_IMAGE_PAYLOAD_SECTOR_COUNT_OFFSET = 16
OS_STAGE1_IMAGE_FLAGS_OFFSET = 18
OS_STAGE1_IMAGE_PAYLOAD_LBA_OFFSET = 20
OS_STAGE1_IMAGE_PAYLOAD_CHECKSUM_OFFSET = 24
OS_STAGE1_IMAGE_HEADER_CHECKSUM_OFFSET = 26

@dataclass(frozen=True)
class Stage1Descriptor:
    loadSegment: int
    entryOffset: int
    payloadSectorCount: int
    payloadLba: int
    payloadChecksum: int


def calculateWordChecksum(imageBytes: bytes) -> int:
    if len(imageBytes) % 2 != 0:
        raise OsToolError("校验数据长度必须是 16 位字的整数倍")

    checksum = 0
    for offset in range(0, len(imageBytes), 2):
        checksum = (
            checksum
            + int.from_bytes(
                imageBytes[offset:offset + 2],
                byteorder="little",
            )
        ) & OS_STAGE1_IMAGE_UINT16_MAXIMUM
    return checksum


def calculatePayloadSectorCount(payloadSizeBytes: int) -> int:
    if payloadSizeBytes <= 0:
        raise OsToolError("Stage 1 目标代码不能为空")
    return (
        payloadSizeBytes + OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES - 1
    ) // OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES


def validateStage1Layout(
    diskSizeBytes: int,
    payloadSectorCount: int,
    payloadLba: int,
    loadSegment: int,
    entryOffset: int,
) -> None:
    if (
        diskSizeBytes <= 0
        or diskSizeBytes % OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES != 0
    ):
        raise OsToolError("Stage 1 磁盘大小必须是非零整扇区")
    if not (
        1
        <= payloadSectorCount
        <= OS_STAGE1_IMAGE_MAXIMUM_PAYLOAD_SECTORS
    ):
        raise OsToolError(
            "Stage 1 负载扇区数超出固件单段加载能力："
            f"{payloadSectorCount}"
        )
    if not (
        OS_STAGE1_IMAGE_DEFAULT_PAYLOAD_LBA
        <= payloadLba
        <= OS_STAGE1_IMAGE_LBA28_MAXIMUM
    ):
        raise OsToolError(f"Stage 1 负载 LBA 无效：{payloadLba}")

    diskSectorCount = (
        diskSizeBytes // OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES
    )
    payloadEndLba = payloadLba + payloadSectorCount
    if payloadEndLba > diskSectorCount:
        raise OsToolError(
            "Stage 1 负载超出磁盘镜像："
            f"结束 LBA {payloadEndLba}，磁盘扇区数 {diskSectorCount}"
        )

    if not 0 <= loadSegment <= OS_STAGE1_IMAGE_UINT16_MAXIMUM:
        raise OsToolError(f"Stage 1 加载段不可由实模式段寄存器表示：{loadSegment}")
    loadAddress = loadSegment << 4
    payloadSizeBytes = (
        payloadSectorCount * OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES
    )
    loadEndAddress = loadAddress + payloadSizeBytes
    if (
        loadAddress < OS_STAGE1_IMAGE_MINIMUM_LOAD_ADDRESS
        or loadEndAddress > OS_STAGE1_IMAGE_MAXIMUM_LOAD_END_ADDRESS
    ):
        raise OsToolError(
            "Stage 1 加载范围不在约定 RAM 窗口："
            f"0x{loadAddress:08X}..0x{loadEndAddress:08X}"
        )
    if not 0 <= entryOffset < payloadSizeBytes:
        raise OsToolError(
            "Stage 1 入口偏移不在负载范围："
            f"{entryOffset}"
        )


def createStage1DiskImageBytes(
    stage1Binary: bytes,
    diskSizeBytes: int,
    payloadLba: int = OS_STAGE1_IMAGE_DEFAULT_PAYLOAD_LBA,
    loadSegment: int = OS_STAGE1_IMAGE_DEFAULT_LOAD_SEGMENT,
    entryOffset: int = OS_STAGE1_IMAGE_DEFAULT_ENTRY_OFFSET,
) -> bytes:
    payloadSectorCount = calculatePayloadSectorCount(len(stage1Binary))
    validateStage1Layout(
        diskSizeBytes,
        payloadSectorCount,
        payloadLba,
        loadSegment,
        entryOffset,
    )
    if entryOffset >= len(stage1Binary):
        raise OsToolError("Stage 1 入口不能指向填充区域")

    paddedPayloadSizeBytes = (
        payloadSectorCount * OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES
    )
    paddedPayload = stage1Binary.ljust(paddedPayloadSizeBytes, b"\x00")
    payloadChecksum = calculateWordChecksum(paddedPayload)

    descriptor = bytearray(OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES)
    struct.pack_into(
        OS_STAGE1_IMAGE_HEADER_STRUCT_FORMAT,
        descriptor,
        OS_STAGE1_IMAGE_MAGIC_OFFSET,
        OS_STAGE1_IMAGE_MAGIC,
        OS_STAGE1_IMAGE_VERSION,
        OS_STAGE1_IMAGE_HEADER_SIZE_BYTES,
        loadSegment,
        entryOffset,
        payloadSectorCount,
        OS_STAGE1_IMAGE_FLAGS_NONE,
        payloadLba,
        payloadChecksum,
        0,
    )
    descriptorChecksum = (
        -calculateWordChecksum(descriptor)
    ) & OS_STAGE1_IMAGE_UINT16_MAXIMUM
    struct.pack_into(
        "<H",
        descriptor,
        OS_STAGE1_IMAGE_HEADER_CHECKSUM_OFFSET,
        descriptorChecksum,
    )

    diskImage = bytearray(diskSizeBytes)
    diskImage[:OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES] = descriptor
    payloadOffset = payloadLba * OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES
    diskImage[
        payloadOffset:payloadOffset + paddedPayloadSizeBytes
    ] = paddedPayload
    return bytes(diskImage)


def parseAndValidateStage1DiskImage(
    diskImage: bytes,
) -> Stage1Descriptor:
    if len(diskImage) < OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES:
        raise OsToolError("Stage 1 磁盘镜像被截断，无法读取描述符扇区")
    if len(diskImage) % OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES != 0:
        raise OsToolError("Stage 1 磁盘镜像不是整扇区长度")

    descriptorBytes = diskImage[:OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES]
    (
        magic,
        version,
        headerSizeBytes,
        loadSegment,
        entryOffset,
        payloadSectorCount,
        flags,
        payloadLba,
        payloadChecksum,
        _,
    ) = struct.unpack_from(
        OS_STAGE1_IMAGE_HEADER_STRUCT_FORMAT,
        descriptorBytes,
        OS_STAGE1_IMAGE_MAGIC_OFFSET,
    )

    if magic != OS_STAGE1_IMAGE_MAGIC:
        raise OsToolError(f"Stage 1 描述符魔数不正确：{magic!r}")
    if version != OS_STAGE1_IMAGE_VERSION:
        raise OsToolError(f"Stage 1 描述符版本不受支持：{version}")
    if headerSizeBytes != OS_STAGE1_IMAGE_HEADER_SIZE_BYTES:
        raise OsToolError(
            "Stage 1 描述符头长度不正确："
            f"{headerSizeBytes}"
        )
    if flags != OS_STAGE1_IMAGE_FLAGS_NONE:
        raise OsToolError(f"Stage 1 描述符包含未知标志：0x{flags:04X}")
    if calculateWordChecksum(descriptorBytes) != 0:
        raise OsToolError("Stage 1 描述符整扇区校验失败")

    validateStage1Layout(
        len(diskImage),
        payloadSectorCount,
        payloadLba,
        loadSegment,
        entryOffset,
    )
    payloadOffset = payloadLba * OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES
    payloadSizeBytes = (
        payloadSectorCount * OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES
    )
    payload = diskImage[payloadOffset:payloadOffset + payloadSizeBytes]
    if len(payload) != payloadSizeBytes:
        raise OsToolError("Stage 1 负载被截断")
    if calculateWordChecksum(payload) != payloadChecksum:
        raise OsToolError("Stage 1 负载校验失败")

    return Stage1Descriptor(
        loadSegment=loadSegment,
        entryOffset=entryOffset,
        payloadSectorCount=payloadSectorCount,
        payloadLba=payloadLba,
        payloadChecksum=payloadChecksum,
    )


def auditStage1DiskImage(diskImagePath: Path) -> None:
    descriptor = parseAndValidateStage1DiskImage(
        diskImagePath.read_bytes()
    )
    print(
        "Stage 1 磁盘审计通过："
        f"LBA {descriptor.payloadLba}，"
        f"{descriptor.payloadSectorCount} 个扇区，"
        f"加载段 0x{descriptor.loadSegment:04X}，"
        f"入口偏移 0x{descriptor.entryOffset:04X}。"
    )

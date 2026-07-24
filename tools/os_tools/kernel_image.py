from dataclasses import dataclass
import binascii
from pathlib import Path
import struct

from .errors import OsToolError
from .kernel_elf import parseKernelLoadSegments, validateKernelEntry
from .stage1_image import (
    OS_STAGE1_IMAGE_LBA28_MAXIMUM,
    OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES,
    parseAndValidateStage1DiskImage,
)


OS_KERNEL_IMAGE_DESCRIPTOR_LBA = 65
OS_KERNEL_IMAGE_DESCRIPTOR_SECTOR_COUNT = 1
OS_KERNEL_IMAGE_DEFAULT_PAYLOAD_LBA = 66
OS_KERNEL_IMAGE_MAGIC = b"OSKERN64"
OS_KERNEL_IMAGE_VERSION = 1
OS_KERNEL_IMAGE_HEADER_SIZE_BYTES = 48
OS_KERNEL_IMAGE_FLAGS_NONE = 0
OS_KERNEL_IMAGE_HEADER_STRUCT_FORMAT = "<8sHHIQQQII"
OS_KERNEL_IMAGE_UINT32_FORMAT = "<I"
OS_KERNEL_IMAGE_CORRUPTION_BIT = 0x01

OS_KERNEL_IMAGE_MAGIC_OFFSET = 0
OS_KERNEL_IMAGE_VERSION_OFFSET = 8
OS_KERNEL_IMAGE_HEADER_SIZE_OFFSET = 10
OS_KERNEL_IMAGE_FLAGS_OFFSET = 12
OS_KERNEL_IMAGE_PAYLOAD_LBA_OFFSET = 16
OS_KERNEL_IMAGE_FILE_SIZE_OFFSET = 24
OS_KERNEL_IMAGE_PAYLOAD_SECTOR_COUNT_OFFSET = 32
OS_KERNEL_IMAGE_PAYLOAD_CHECKSUM_OFFSET = 40
OS_KERNEL_IMAGE_HEADER_CHECKSUM_OFFSET = 44


@dataclass(frozen=True)
class KernelImageDescriptor:
    payloadLba: int
    fileSizeBytes: int
    payloadSectorCount: int
    payloadChecksum: int
    headerChecksum: int


def calculateCrc32(content: bytes) -> int:
    return binascii.crc32(content)


def calculateKernelPayloadSectorCount(fileSizeBytes: int) -> int:
    if fileSizeBytes <= 0:
        raise OsToolError("Kernel ELF 文件不能为空。")
    return (
        fileSizeBytes + OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES - 1
    ) // OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES


def validateKernelImageLayout(
    diskSizeBytes: int,
    fileSizeBytes: int,
    payloadSectorCount: int,
    payloadLba: int,
) -> None:
    if (
        diskSizeBytes <= 0
        or diskSizeBytes % OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES != 0
    ):
        raise OsToolError("Kernel 磁盘大小必须是非零整扇区。")
    expectedSectorCount = calculateKernelPayloadSectorCount(fileSizeBytes)
    if payloadSectorCount != expectedSectorCount:
        raise OsToolError("Kernel ELF 文件长度与负载扇区数不一致。")
    if payloadLba != OS_KERNEL_IMAGE_DEFAULT_PAYLOAD_LBA:
        raise OsToolError("Kernel ELF 负载 LBA 不符合当前磁盘格式。")
    if payloadLba > OS_STAGE1_IMAGE_LBA28_MAXIMUM:
        raise OsToolError("Kernel ELF 负载 LBA 超出当前 ATA LBA28 能力。")

    diskSectorCount = (
        diskSizeBytes // OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES
    )
    payloadEndLba = payloadLba + payloadSectorCount
    if payloadEndLba > diskSectorCount:
        raise OsToolError("Kernel ELF 负载超出磁盘镜像。")
    expectedPayloadLba = (
        OS_KERNEL_IMAGE_DESCRIPTOR_LBA
        + OS_KERNEL_IMAGE_DESCRIPTOR_SECTOR_COUNT
    )
    if payloadLba != expectedPayloadLba:
        raise OsToolError("Kernel 描述符与 ELF 负载顺序无效。")


def validateStage1KernelSeparation(
    stage1PayloadLba: int,
    stage1PayloadSectorCount: int,
) -> None:
    stage1PayloadEndLba = (
        stage1PayloadLba + stage1PayloadSectorCount
    )
    if stage1PayloadEndLba > OS_KERNEL_IMAGE_DESCRIPTOR_LBA:
        raise OsToolError("Stage 1 负载与 Kernel 描述符区域重叠。")


def createKernelDiskImageBytes(
    stage1DiskImage: bytes,
    kernelElf: bytes,
) -> bytes:
    stage1Descriptor = parseAndValidateStage1DiskImage(stage1DiskImage)
    validateStage1KernelSeparation(
        stage1Descriptor.payloadLba,
        stage1Descriptor.payloadSectorCount,
    )
    entryAddress, loadSegments = parseKernelLoadSegments(kernelElf)
    validateKernelEntry(entryAddress, loadSegments)

    payloadSectorCount = calculateKernelPayloadSectorCount(len(kernelElf))
    validateKernelImageLayout(
        len(stage1DiskImage),
        len(kernelElf),
        payloadSectorCount,
        OS_KERNEL_IMAGE_DEFAULT_PAYLOAD_LBA,
    )

    descriptorOffset = (
        OS_KERNEL_IMAGE_DESCRIPTOR_LBA
        * OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES
    )
    payloadOffset = (
        OS_KERNEL_IMAGE_DEFAULT_PAYLOAD_LBA
        * OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES
    )
    payloadRegionSizeBytes = (
        payloadSectorCount * OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES
    )
    reservedRegion = stage1DiskImage[
        descriptorOffset:payloadOffset + payloadRegionSizeBytes
    ]
    if any(reservedRegion):
        raise OsToolError("Kernel 磁盘区域已被其他启动数据占用。")

    payloadChecksum = calculateCrc32(kernelElf)
    descriptor = bytearray(OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES)
    struct.pack_into(
        OS_KERNEL_IMAGE_HEADER_STRUCT_FORMAT,
        descriptor,
        OS_KERNEL_IMAGE_MAGIC_OFFSET,
        OS_KERNEL_IMAGE_MAGIC,
        OS_KERNEL_IMAGE_VERSION,
        OS_KERNEL_IMAGE_HEADER_SIZE_BYTES,
        OS_KERNEL_IMAGE_FLAGS_NONE,
        OS_KERNEL_IMAGE_DEFAULT_PAYLOAD_LBA,
        len(kernelElf),
        payloadSectorCount,
        payloadChecksum,
        0,
    )
    headerChecksum = calculateCrc32(descriptor)
    struct.pack_into(
        OS_KERNEL_IMAGE_UINT32_FORMAT,
        descriptor,
        OS_KERNEL_IMAGE_HEADER_CHECKSUM_OFFSET,
        headerChecksum,
    )

    diskImage = bytearray(stage1DiskImage)
    diskImage[
        descriptorOffset:
        descriptorOffset + OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES
    ] = descriptor
    diskImage[
        payloadOffset:payloadOffset + len(kernelElf)
    ] = kernelElf
    return bytes(diskImage)


def parseAndValidateKernelDiskImage(
    diskImage: bytes,
) -> tuple[KernelImageDescriptor, bytes]:
    stage1Descriptor = parseAndValidateStage1DiskImage(diskImage)
    validateStage1KernelSeparation(
        stage1Descriptor.payloadLba,
        stage1Descriptor.payloadSectorCount,
    )
    descriptorOffset = (
        OS_KERNEL_IMAGE_DESCRIPTOR_LBA
        * OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES
    )
    descriptorEnd = (
        descriptorOffset + OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES
    )
    if descriptorEnd > len(diskImage):
        raise OsToolError("Kernel 磁盘镜像被截断，无法读取描述符。")
    descriptorBytes = diskImage[descriptorOffset:descriptorEnd]
    (
        magic,
        version,
        headerSizeBytes,
        flags,
        payloadLba,
        fileSizeBytes,
        payloadSectorCount,
        payloadChecksum,
        headerChecksum,
    ) = struct.unpack_from(
        OS_KERNEL_IMAGE_HEADER_STRUCT_FORMAT,
        descriptorBytes,
        OS_KERNEL_IMAGE_MAGIC_OFFSET,
    )

    if magic != OS_KERNEL_IMAGE_MAGIC:
        raise OsToolError("Kernel 描述符 magic 不正确。")
    if version != OS_KERNEL_IMAGE_VERSION:
        raise OsToolError("Kernel 描述符版本不受支持。")
    if headerSizeBytes != OS_KERNEL_IMAGE_HEADER_SIZE_BYTES:
        raise OsToolError("Kernel 描述符头长度不正确。")
    if flags != OS_KERNEL_IMAGE_FLAGS_NONE:
        raise OsToolError("Kernel 描述符包含未知标志。")
    if any(descriptorBytes[OS_KERNEL_IMAGE_HEADER_SIZE_BYTES:]):
        raise OsToolError("Kernel 描述符保留区域必须为零。")

    checksumInput = bytearray(descriptorBytes)
    struct.pack_into(
        OS_KERNEL_IMAGE_UINT32_FORMAT,
        checksumInput,
        OS_KERNEL_IMAGE_HEADER_CHECKSUM_OFFSET,
        0,
    )
    if calculateCrc32(checksumInput) != headerChecksum:
        raise OsToolError("Kernel 描述符 CRC32 校验失败。")

    validateKernelImageLayout(
        len(diskImage),
        fileSizeBytes,
        payloadSectorCount,
        payloadLba,
    )
    payloadOffset = payloadLba * OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES
    payloadEnd = payloadOffset + fileSizeBytes
    kernelElf = diskImage[payloadOffset:payloadEnd]
    if len(kernelElf) != fileSizeBytes:
        raise OsToolError("Kernel ELF 文件被截断。")
    if calculateCrc32(kernelElf) != payloadChecksum:
        raise OsToolError("Kernel ELF 文件 CRC32 校验失败。")

    entryAddress, loadSegments = parseKernelLoadSegments(kernelElf)
    validateKernelEntry(entryAddress, loadSegments)
    return (
        KernelImageDescriptor(
            payloadLba=payloadLba,
            fileSizeBytes=fileSizeBytes,
            payloadSectorCount=payloadSectorCount,
            payloadChecksum=payloadChecksum,
            headerChecksum=headerChecksum,
        ),
        kernelElf,
    )


def auditKernelDiskImage(diskImagePath: Path) -> None:
    descriptor, _kernelElf = parseAndValidateKernelDiskImage(
        diskImagePath.read_bytes()
    )
    print(
        "Kernel 磁盘审计通过："
        f"LBA {descriptor.payloadLba}，"
        f"{descriptor.fileSizeBytes} 字节，"
        f"{descriptor.payloadSectorCount} 个扇区，"
        f"CRC32 0x{descriptor.payloadChecksum:08X}。"
    )

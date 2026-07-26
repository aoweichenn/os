from pathlib import Path
import struct

from .kernel_elf import OS_KERNEL_ELF_IDENT_CLASS_OFFSET, auditKernelElf
from .kernel_image import (
    OS_KERNEL_IMAGE_CORRUPTION_BIT,
    OS_KERNEL_IMAGE_DEFAULT_PAYLOAD_LBA,
    OS_KERNEL_IMAGE_DESCRIPTOR_LBA,
    OS_KERNEL_IMAGE_HEADER_CHECKSUM_OFFSET,
    OS_KERNEL_IMAGE_PAYLOAD_CHECKSUM_OFFSET,
    OS_KERNEL_IMAGE_MAGIC_OFFSET,
    OS_KERNEL_IMAGE_UINT32_FORMAT,
    calculateCrc32,
    createKernelDiskImageBytes,
)
from .rootfs_v2 import (
    OS_ROOTFS_V2_BLOCK_SIZE_BYTES,
    OS_ROOTFS_V2_MINIMUM_DISK_SIZE_BYTES,
    OS_ROOTFS_V2_START_LBA,
    formatRootfsV2,
)
from .errors import OsToolError
from .sparse_image import writeSparseImage
from .stage1_image import (
    OS_STAGE1_IMAGE_CORRUPTION_BIT,
    OS_STAGE1_IMAGE_DEFAULT_PAYLOAD_LBA,
    OS_STAGE1_IMAGE_MAGIC_OFFSET,
    OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES,
    createStage1DiskImageBytes,
)


OS_BOOT_IMAGE_VALID_FILE_NAME = "boot_disk.img"
OS_BOOT_IMAGE_INVALID_STAGE1_HEADER_FILE_NAME = (
    "stage1_invalid_header_disk.img"
)
OS_BOOT_IMAGE_INVALID_STAGE1_CHECKSUM_FILE_NAME = (
    "stage1_invalid_checksum_disk.img"
)
OS_BOOT_IMAGE_INVALID_KERNEL_HEADER_FILE_NAME = (
    "kernel_invalid_header_disk.img"
)
OS_BOOT_IMAGE_INVALID_KERNEL_CHECKSUM_FILE_NAME = (
    "kernel_invalid_checksum_disk.img"
)
OS_BOOT_IMAGE_INVALID_KERNEL_ELF_FILE_NAME = (
    "kernel_invalid_elf_disk.img"
)
OS_BOOT_IMAGE_INVALID_KERNEL_ELF_CLASS = 0
OS_BOOT_IMAGE_CONSTRUCTION_SIZE_BYTES = (
    OS_ROOTFS_V2_START_LBA * OS_ROOTFS_V2_BLOCK_SIZE_BYTES
)


def writeBootImage(
    imagePath: Path,
    imagePrefix: bytes | bytearray,
    diskSizeBytes: int,
) -> None:
    writeSparseImage(imagePath, imagePrefix, diskSizeBytes)
    formatRootfsV2(imagePath)


def createInvalidKernelElfDiskImage(
    validImage: bytes,
    kernelElfSizeBytes: int,
) -> bytes:
    invalidKernelElfImage = bytearray(validImage)
    kernelPayloadOffset = (
        OS_KERNEL_IMAGE_DEFAULT_PAYLOAD_LBA
        * OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES
    )
    invalidKernelElfImage[
        kernelPayloadOffset + OS_KERNEL_ELF_IDENT_CLASS_OFFSET
    ] = OS_BOOT_IMAGE_INVALID_KERNEL_ELF_CLASS

    descriptorOffset = (
        OS_KERNEL_IMAGE_DESCRIPTOR_LBA
        * OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES
    )
    payloadChecksum = calculateCrc32(
        invalidKernelElfImage[
            kernelPayloadOffset:
            kernelPayloadOffset + kernelElfSizeBytes
        ]
    )
    struct.pack_into(
        OS_KERNEL_IMAGE_UINT32_FORMAT,
        invalidKernelElfImage,
        descriptorOffset + OS_KERNEL_IMAGE_PAYLOAD_CHECKSUM_OFFSET,
        payloadChecksum,
    )
    struct.pack_into(
        OS_KERNEL_IMAGE_UINT32_FORMAT,
        invalidKernelElfImage,
        descriptorOffset + OS_KERNEL_IMAGE_HEADER_CHECKSUM_OFFSET,
        0,
    )
    descriptorEnd = (
        descriptorOffset + OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES
    )
    descriptorChecksum = calculateCrc32(
        invalidKernelElfImage[descriptorOffset:descriptorEnd]
    )
    struct.pack_into(
        OS_KERNEL_IMAGE_UINT32_FORMAT,
        invalidKernelElfImage,
        descriptorOffset + OS_KERNEL_IMAGE_HEADER_CHECKSUM_OFFSET,
        descriptorChecksum,
    )
    return bytes(invalidKernelElfImage)


def writeBootDiskImages(
    stage1BinaryPath: Path,
    kernelElfPath: Path,
    outputDirectory: Path,
    diskSizeBytes: int,
) -> None:
    if diskSizeBytes < OS_ROOTFS_V2_MINIMUM_DISK_SIZE_BYTES:
        raise OsToolError(
            "启动磁盘不足以容纳固定 256 MiB rootfs v2 区域。"
        )
    auditKernelElf(kernelElfPath.parent, kernelElfPath)
    stage1DiskImage = createStage1DiskImageBytes(
        stage1BinaryPath.read_bytes(),
        OS_BOOT_IMAGE_CONSTRUCTION_SIZE_BYTES,
    )
    validImage = createKernelDiskImageBytes(
        stage1DiskImage,
        kernelElfPath.read_bytes(),
    )
    outputDirectory.mkdir(parents=True, exist_ok=True)
    writeBootImage(
        outputDirectory / OS_BOOT_IMAGE_VALID_FILE_NAME,
        validImage,
        diskSizeBytes,
    )

    invalidStage1HeaderImage = bytearray(validImage)
    invalidStage1HeaderImage[
        OS_STAGE1_IMAGE_MAGIC_OFFSET
    ] ^= OS_STAGE1_IMAGE_CORRUPTION_BIT
    writeBootImage(
        outputDirectory /
        OS_BOOT_IMAGE_INVALID_STAGE1_HEADER_FILE_NAME,
        invalidStage1HeaderImage,
        diskSizeBytes,
    )

    invalidStage1ChecksumImage = bytearray(validImage)
    stage1PayloadOffset = (
        OS_STAGE1_IMAGE_DEFAULT_PAYLOAD_LBA
        * OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES
    )
    invalidStage1ChecksumImage[
        stage1PayloadOffset
    ] ^= OS_STAGE1_IMAGE_CORRUPTION_BIT
    writeBootImage(
        outputDirectory /
        OS_BOOT_IMAGE_INVALID_STAGE1_CHECKSUM_FILE_NAME,
        invalidStage1ChecksumImage,
        diskSizeBytes,
    )

    invalidKernelHeaderImage = bytearray(validImage)
    kernelDescriptorOffset = (
        OS_KERNEL_IMAGE_DESCRIPTOR_LBA
        * OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES
    )
    invalidKernelHeaderImage[
        kernelDescriptorOffset + OS_KERNEL_IMAGE_MAGIC_OFFSET
    ] ^= OS_KERNEL_IMAGE_CORRUPTION_BIT
    writeBootImage(
        outputDirectory /
        OS_BOOT_IMAGE_INVALID_KERNEL_HEADER_FILE_NAME,
        invalidKernelHeaderImage,
        diskSizeBytes,
    )

    invalidKernelChecksumImage = bytearray(validImage)
    kernelPayloadOffset = (
        OS_KERNEL_IMAGE_DEFAULT_PAYLOAD_LBA
        * OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES
    )
    invalidKernelChecksumImage[
        kernelPayloadOffset
    ] ^= OS_KERNEL_IMAGE_CORRUPTION_BIT
    writeBootImage(
        outputDirectory /
        OS_BOOT_IMAGE_INVALID_KERNEL_CHECKSUM_FILE_NAME,
        invalidKernelChecksumImage,
        diskSizeBytes,
    )

    invalidKernelElfImage = createInvalidKernelElfDiskImage(
        validImage,
        kernelElfPath.stat().st_size,
    )
    writeBootImage(
        outputDirectory /
        OS_BOOT_IMAGE_INVALID_KERNEL_ELF_FILE_NAME,
        invalidKernelElfImage,
        diskSizeBytes,
    )

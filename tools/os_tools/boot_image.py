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
    auditKernelElf(kernelElfPath.parent, kernelElfPath)
    stage1DiskImage = createStage1DiskImageBytes(
        stage1BinaryPath.read_bytes(),
        diskSizeBytes,
    )
    validImage = createKernelDiskImageBytes(
        stage1DiskImage,
        kernelElfPath.read_bytes(),
    )
    outputDirectory.mkdir(parents=True, exist_ok=True)
    (outputDirectory / OS_BOOT_IMAGE_VALID_FILE_NAME).write_bytes(validImage)

    invalidStage1HeaderImage = bytearray(validImage)
    invalidStage1HeaderImage[
        OS_STAGE1_IMAGE_MAGIC_OFFSET
    ] ^= OS_STAGE1_IMAGE_CORRUPTION_BIT
    (
        outputDirectory
        / OS_BOOT_IMAGE_INVALID_STAGE1_HEADER_FILE_NAME
    ).write_bytes(invalidStage1HeaderImage)

    invalidStage1ChecksumImage = bytearray(validImage)
    stage1PayloadOffset = (
        OS_STAGE1_IMAGE_DEFAULT_PAYLOAD_LBA
        * OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES
    )
    invalidStage1ChecksumImage[
        stage1PayloadOffset
    ] ^= OS_STAGE1_IMAGE_CORRUPTION_BIT
    (
        outputDirectory
        / OS_BOOT_IMAGE_INVALID_STAGE1_CHECKSUM_FILE_NAME
    ).write_bytes(invalidStage1ChecksumImage)

    invalidKernelHeaderImage = bytearray(validImage)
    kernelDescriptorOffset = (
        OS_KERNEL_IMAGE_DESCRIPTOR_LBA
        * OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES
    )
    invalidKernelHeaderImage[
        kernelDescriptorOffset + OS_KERNEL_IMAGE_MAGIC_OFFSET
    ] ^= OS_KERNEL_IMAGE_CORRUPTION_BIT
    (
        outputDirectory
        / OS_BOOT_IMAGE_INVALID_KERNEL_HEADER_FILE_NAME
    ).write_bytes(invalidKernelHeaderImage)

    invalidKernelChecksumImage = bytearray(validImage)
    kernelPayloadOffset = (
        OS_KERNEL_IMAGE_DEFAULT_PAYLOAD_LBA
        * OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES
    )
    invalidKernelChecksumImage[
        kernelPayloadOffset
    ] ^= OS_KERNEL_IMAGE_CORRUPTION_BIT
    (
        outputDirectory
        / OS_BOOT_IMAGE_INVALID_KERNEL_CHECKSUM_FILE_NAME
    ).write_bytes(invalidKernelChecksumImage)

    invalidKernelElfImage = createInvalidKernelElfDiskImage(
        validImage,
        kernelElfPath.stat().st_size,
    )
    (
        outputDirectory
        / OS_BOOT_IMAGE_INVALID_KERNEL_ELF_FILE_NAME
    ).write_bytes(invalidKernelElfImage)

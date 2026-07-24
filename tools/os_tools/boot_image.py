from pathlib import Path

from .kernel_elf import auditKernelElf
from .kernel_image import (
    OS_KERNEL_IMAGE_CORRUPTION_BIT,
    OS_KERNEL_IMAGE_DEFAULT_PAYLOAD_LBA,
    OS_KERNEL_IMAGE_DESCRIPTOR_LBA,
    OS_KERNEL_IMAGE_MAGIC_OFFSET,
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

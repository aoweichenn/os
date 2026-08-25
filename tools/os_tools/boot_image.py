from pathlib import Path
import errno
import os
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
from .rootfs_v5 import (
    OS_ROOTFS_V5_DEVICE_SIZE_BYTES,
    OS_ROOTFS_V5_FILE_SYSTEM_START_LBA,
    OS_ROOTFS_V5_SECTOR_SIZE_BYTES,
    RootfsV5InstallFile,
    formatRootfsV5,
    installRootfsV5Files,
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
    OS_ROOTFS_V5_FILE_SYSTEM_START_LBA * OS_ROOTFS_V5_SECTOR_SIZE_BYTES
)


def writeBootImage(
    imagePath: Path,
    imagePrefix: bytes | bytearray,
    diskSizeBytes: int,
    rootfsFiles: tuple[RootfsV5InstallFile, ...] = (),
) -> None:
    writeSparseImage(imagePath, imagePrefix, diskSizeBytes)
    # writeSparseImage 已先截断为全零新文件；跳过 1024 组 inode table 的重复清零物化。
    formatRootfsV5(
        imagePath,
        zeroInodeTables=False,
        verifyUnallocatedInodes=False,
        verifyResult=False,
    )
    installRootfsV5Files(
        imagePath,
        rootfsFiles,
        verifyUnallocatedInodes=False,
        verifyExisting=False,
    )


def cloneBootImageWithPrefix(
    sourcePath: Path,
    destinationPath: Path,
    imagePrefix: bytes | bytearray,
    diskSizeBytes: int,
) -> None:
    """只复制源盘已分配 extent，再覆盖启动前缀，保留 128 GiB 宿主空洞。"""
    with sourcePath.open("rb", buffering=0) as sourceFile:
        with destinationPath.open("w+b", buffering=0) as destinationFile:
            destinationFile.truncate(diskSizeBytes)
            sourceDescriptor = sourceFile.fileno()
            destinationDescriptor = destinationFile.fileno()
            cursor = 0
            while cursor < diskSizeBytes:
                try:
                    dataOffset = os.lseek(sourceDescriptor, cursor, os.SEEK_DATA)
                except OSError as error:
                    if error.errno == errno.ENXIO:
                        break
                    raise OsToolError("宿主文件系统不支持稀疏 extent clone。") from error
                holeOffset = os.lseek(sourceDescriptor, dataOffset, os.SEEK_HOLE)
                sourceFile.seek(dataOffset)
                destinationFile.seek(dataOffset)
                remainingBytes = min(holeOffset, diskSizeBytes) - dataOffset
                while remainingBytes > 0:
                    content = sourceFile.read(min(1024 * 1024, remainingBytes))
                    if not content:
                        raise OsToolError("v5 稀疏启动盘 clone 遇到短读。")
                    destinationFile.write(content)
                    remainingBytes -= len(content)
                cursor = holeOffset
            destinationFile.seek(0)
            destinationFile.write(imagePrefix)
            destinationFile.flush()


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
    rootfsFiles: tuple[RootfsV5InstallFile, ...] = (),
) -> None:
    if diskSizeBytes != OS_ROOTFS_V5_DEVICE_SIZE_BYTES:
        raise OsToolError(
            "启动磁盘必须精确匹配 128 GiB rootfs v5 生产 profile。"
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
    validImagePath = outputDirectory / OS_BOOT_IMAGE_VALID_FILE_NAME
    writeBootImage(
        validImagePath,
        validImage,
        diskSizeBytes,
        rootfsFiles,
    )

    invalidStage1HeaderImage = bytearray(validImage)
    invalidStage1HeaderImage[
        OS_STAGE1_IMAGE_MAGIC_OFFSET
    ] ^= OS_STAGE1_IMAGE_CORRUPTION_BIT
    cloneBootImageWithPrefix(
        validImagePath,
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
    cloneBootImageWithPrefix(
        validImagePath,
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
    cloneBootImageWithPrefix(
        validImagePath,
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
    cloneBootImageWithPrefix(
        validImagePath,
        outputDirectory /
        OS_BOOT_IMAGE_INVALID_KERNEL_CHECKSUM_FILE_NAME,
        invalidKernelChecksumImage,
        diskSizeBytes,
    )

    invalidKernelElfImage = createInvalidKernelElfDiskImage(
        validImage,
        kernelElfPath.stat().st_size,
    )
    cloneBootImageWithPrefix(
        validImagePath,
        outputDirectory /
        OS_BOOT_IMAGE_INVALID_KERNEL_ELF_FILE_NAME,
        invalidKernelElfImage,
        diskSizeBytes,
    )

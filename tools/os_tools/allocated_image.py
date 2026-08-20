import os
from pathlib import Path

from .errors import OsToolError
from .sparse_image import copySparseImage


OS_ALLOCATED_IMAGE_BLOCK_ACCOUNTING_SIZE_BYTES = 512


def allocatedImageBytes(imagePath: Path) -> int:
    return (
        imagePath.stat().st_blocks
        * OS_ALLOCATED_IMAGE_BLOCK_ACCOUNTING_SIZE_BYTES
    )


def requireAllocatedImage(imagePath: Path) -> None:
    if not imagePath.is_file():
        raise OsToolError(f"已物化镜像不存在：{imagePath}")
    logicalSizeBytes = imagePath.stat().st_size
    allocatedSizeBytes = allocatedImageBytes(imagePath)
    if logicalSizeBytes <= 0 or allocatedSizeBytes < logicalSizeBytes:
        raise OsToolError(
            "镜像仍含宿主稀疏区："
            f"逻辑 {logicalSizeBytes} 字节，已分配 {allocatedSizeBytes} 字节。"
        )


def materializeImage(sourcePath: Path, destinationPath: Path) -> None:
    if sourcePath.resolve() == destinationPath.resolve():
        raise OsToolError("已物化镜像必须写入不同目标路径。")
    if not hasattr(os, "posix_fallocate"):
        raise OsToolError("宿主 Python 缺少 posix_fallocate，拒绝伪造非稀疏镜像。")
    copySparseImage(sourcePath, destinationPath)
    descriptor = os.open(destinationPath, os.O_RDWR)
    try:
        imageSizeBytes = destinationPath.stat().st_size
        os.posix_fallocate(descriptor, 0, imageSizeBytes)
        os.fsync(descriptor)
    except OSError as error:
        raise OsToolError(f"镜像物化失败：{error}") from error
    finally:
        os.close(descriptor)
    requireAllocatedImage(destinationPath)

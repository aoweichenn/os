import errno
import os
from pathlib import Path

from .errors import OsToolError


OS_SPARSE_IMAGE_COPY_CHUNK_SIZE_BYTES = 1024 * 1024


def _writeAllAt(
    descriptor: int,
    content: bytes,
    offsetBytes: int,
) -> None:
    writtenBytes = 0
    while writtenBytes < len(content):
        currentWrittenBytes = os.pwrite(
            descriptor,
            content[writtenBytes:],
            offsetBytes + writtenBytes,
        )
        if currentWrittenBytes <= 0:
            raise OsToolError("复制稀疏镜像数据区间时发生短写。")
        writtenBytes += currentWrittenBytes


def writeSparseImage(
    imagePath: Path,
    prefix: bytes | bytearray,
    logicalSizeBytes: int,
) -> None:
    if logicalSizeBytes < len(prefix):
        raise OsToolError(
            "稀疏镜像逻辑长度小于必须写入的前缀数据。"
        )
    imagePath.parent.mkdir(parents=True, exist_ok=True)
    with imagePath.open("wb") as imageFile:
        imageFile.write(prefix)
        imageFile.truncate(logicalSizeBytes)


def readImagePrefix(
    imagePath: Path,
    maximumPrefixSizeBytes: int,
    alignmentBytes: int,
) -> bytes:
    if not imagePath.is_file():
        raise OsToolError(f"磁盘镜像不存在：{imagePath}")
    imageSizeBytes = imagePath.stat().st_size
    if (
        imageSizeBytes <= 0
        or alignmentBytes <= 0
        or imageSizeBytes % alignmentBytes != 0
    ):
        raise OsToolError("磁盘镜像必须具有非零且对齐的逻辑长度。")
    prefixSizeBytes = min(imageSizeBytes, maximumPrefixSizeBytes)
    with imagePath.open("rb") as imageFile:
        prefix = imageFile.read(prefixSizeBytes)
    if len(prefix) != prefixSizeBytes:
        raise OsToolError("读取磁盘镜像启动前缀时发生短读。")
    return prefix


def _copyExtent(
    sourceDescriptor: int,
    destinationDescriptor: int,
    startOffsetBytes: int,
    endOffsetBytes: int,
) -> None:
    currentOffsetBytes = startOffsetBytes
    while currentOffsetBytes < endOffsetBytes:
        chunkSizeBytes = min(
            OS_SPARSE_IMAGE_COPY_CHUNK_SIZE_BYTES,
            endOffsetBytes - currentOffsetBytes,
        )
        content = os.pread(
            sourceDescriptor,
            chunkSizeBytes,
            currentOffsetBytes,
        )
        if len(content) != chunkSizeBytes:
            raise OsToolError("复制稀疏镜像数据区间时发生短读。")
        _writeAllAt(
            destinationDescriptor,
            content,
            currentOffsetBytes,
        )
        currentOffsetBytes += len(content)


def _copySparseByScanning(
    sourceDescriptor: int,
    destinationDescriptor: int,
    imageSizeBytes: int,
) -> None:
    currentOffsetBytes = 0
    while currentOffsetBytes < imageSizeBytes:
        chunkSizeBytes = min(
            OS_SPARSE_IMAGE_COPY_CHUNK_SIZE_BYTES,
            imageSizeBytes - currentOffsetBytes,
        )
        content = os.pread(
            sourceDescriptor,
            chunkSizeBytes,
            currentOffsetBytes,
        )
        if len(content) != chunkSizeBytes:
            raise OsToolError("扫描复制稀疏镜像时发生短读。")
        if any(content):
            _writeAllAt(
                destinationDescriptor,
                content,
                currentOffsetBytes,
            )
        currentOffsetBytes += chunkSizeBytes


def copySparseImage(sourcePath: Path, destinationPath: Path) -> None:
    if not sourcePath.is_file():
        raise OsToolError(f"稀疏镜像源文件不存在：{sourcePath}")
    imageSizeBytes = sourcePath.stat().st_size
    destinationPath.parent.mkdir(parents=True, exist_ok=True)
    sourceFlags = os.O_RDONLY
    destinationFlags = os.O_RDWR | os.O_CREAT | os.O_TRUNC
    sourceDescriptor = os.open(sourcePath, sourceFlags)
    try:
        destinationDescriptor = os.open(
            destinationPath,
            destinationFlags,
            0o600,
        )
        try:
            os.ftruncate(destinationDescriptor, imageSizeBytes)
            currentOffsetBytes = 0
            sparseSeekingSupported = hasattr(os, "SEEK_DATA") and hasattr(
                os, "SEEK_HOLE"
            )
            if sparseSeekingSupported:
                try:
                    while currentOffsetBytes < imageSizeBytes:
                        dataOffsetBytes = os.lseek(
                            sourceDescriptor,
                            currentOffsetBytes,
                            os.SEEK_DATA,
                        )
                        holeOffsetBytes = os.lseek(
                            sourceDescriptor,
                            dataOffsetBytes,
                            os.SEEK_HOLE,
                        )
                        _copyExtent(
                            sourceDescriptor,
                            destinationDescriptor,
                            dataOffsetBytes,
                            min(holeOffsetBytes, imageSizeBytes),
                        )
                        currentOffsetBytes = holeOffsetBytes
                except OSError as error:
                    if error.errno == errno.ENXIO:
                        return
                    if error.errno not in (
                        errno.EINVAL,
                        errno.ENOTSUP,
                        errno.EOPNOTSUPP,
                    ):
                        raise
                    os.ftruncate(destinationDescriptor, 0)
                    os.ftruncate(destinationDescriptor, imageSizeBytes)
                    _copySparseByScanning(
                        sourceDescriptor,
                        destinationDescriptor,
                        imageSizeBytes,
                    )
                return
            _copySparseByScanning(
                sourceDescriptor,
                destinationDescriptor,
                imageSizeBytes,
            )
        finally:
            os.close(destinationDescriptor)
    finally:
        os.close(sourceDescriptor)

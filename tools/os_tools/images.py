from pathlib import Path

from .errors import OsToolError


OS_IMAGES_EMPTY_FIRMWARE_FILE_NAME = "empty_firmware.bin"
OS_IMAGES_EMPTY_DISK_FILE_NAME = "empty_disk.img"


def validateImageSize(sizeBytes: int, imageDescription: str) -> None:
    if sizeBytes < 0:
        raise OsToolError(f"{imageDescription}大小不能为负数：{sizeBytes}")


def createSizedImage(imagePath: Path, sizeBytes: int) -> None:
    with imagePath.open("wb") as imageFile:
        imageFile.truncate(sizeBytes)


def createEmptyImages(
    outputDirectory: Path,
    firmwareSizeBytes: int,
    diskSizeBytes: int,
) -> None:
    validateImageSize(firmwareSizeBytes, "固件镜像")
    validateImageSize(diskSizeBytes, "磁盘镜像")
    outputDirectory.mkdir(parents=True, exist_ok=True)

    createSizedImage(
        outputDirectory / OS_IMAGES_EMPTY_FIRMWARE_FILE_NAME,
        firmwareSizeBytes,
    )
    createSizedImage(
        outputDirectory / OS_IMAGES_EMPTY_DISK_FILE_NAME,
        diskSizeBytes,
    )

from pathlib import Path
import struct

from .errors import OsToolError
from .sparse_image import writeSparseImage


OS_SWAP_IMAGE_SECTOR_SIZE_BYTES = 512
OS_SWAP_IMAGE_PAGE_SIZE_BYTES = 4096
OS_SWAP_IMAGE_DATA_SIZE_BYTES = 28 * 1024 * 1024 * 1024
OS_SWAP_IMAGE_SLOT_CAPACITY = (
    OS_SWAP_IMAGE_DATA_SIZE_BYTES // OS_SWAP_IMAGE_PAGE_SIZE_BYTES
)
OS_SWAP_IMAGE_ENTRY_SIZE_BYTES = 64
OS_SWAP_IMAGE_SUPERBLOCK_SECTOR_COUNT = 8
OS_SWAP_IMAGE_METADATA_START_LBA = OS_SWAP_IMAGE_SUPERBLOCK_SECTOR_COUNT
OS_SWAP_IMAGE_METADATA_SIZE_BYTES = (
    OS_SWAP_IMAGE_SLOT_CAPACITY * OS_SWAP_IMAGE_ENTRY_SIZE_BYTES
)
OS_SWAP_IMAGE_METADATA_SECTOR_COUNT = (
    OS_SWAP_IMAGE_METADATA_SIZE_BYTES // OS_SWAP_IMAGE_SECTOR_SIZE_BYTES
)
OS_SWAP_IMAGE_DATA_START_LBA = (
    OS_SWAP_IMAGE_METADATA_START_LBA + OS_SWAP_IMAGE_METADATA_SECTOR_COUNT
)
OS_SWAP_IMAGE_PAGE_SECTOR_COUNT = (
    OS_SWAP_IMAGE_PAGE_SIZE_BYTES // OS_SWAP_IMAGE_SECTOR_SIZE_BYTES
)
OS_SWAP_IMAGE_SECTOR_COUNT = (
    OS_SWAP_IMAGE_DATA_START_LBA
    + OS_SWAP_IMAGE_SLOT_CAPACITY * OS_SWAP_IMAGE_PAGE_SECTOR_COUNT
)
OS_SWAP_IMAGE_SIZE_BYTES = (
    OS_SWAP_IMAGE_SECTOR_COUNT * OS_SWAP_IMAGE_SECTOR_SIZE_BYTES
)
OS_SWAP_IMAGE_MAGIC = b"OSSWAP01"
OS_SWAP_IMAGE_FORMAT_VERSION = 1
OS_SWAP_IMAGE_INITIAL_GENERATION = 1
OS_SWAP_IMAGE_CHECKSUM_OFFSET_BYTES = 504
OS_SWAP_IMAGE_FNV1A_OFFSET_BASIS = 14_695_981_039_346_656_037
OS_SWAP_IMAGE_FNV1A_PRIME = 1_099_511_628_211


def calculateSwapImageChecksum(content: bytes | bytearray) -> int:
    checksum = OS_SWAP_IMAGE_FNV1A_OFFSET_BASIS
    for value in content:
        checksum ^= value
        checksum = (checksum * OS_SWAP_IMAGE_FNV1A_PRIME) & 0xFFFF_FFFF_FFFF_FFFF
    return checksum


def encodeSwapImageSuperblock(
    generation: int = OS_SWAP_IMAGE_INITIAL_GENERATION,
) -> bytes:
    if not 0 < generation < 0xFFFF_FFFF_FFFF_FFFF:
        raise OsToolError("swap 盘代次必须位于 1..UINT64_MAX-1。")
    sector = bytearray(OS_SWAP_IMAGE_SECTOR_SIZE_BYTES)
    sector[:len(OS_SWAP_IMAGE_MAGIC)] = OS_SWAP_IMAGE_MAGIC
    fields = (
        OS_SWAP_IMAGE_FORMAT_VERSION,
        OS_SWAP_IMAGE_SECTOR_SIZE_BYTES,
        OS_SWAP_IMAGE_PAGE_SIZE_BYTES,
        OS_SWAP_IMAGE_SLOT_CAPACITY,
        OS_SWAP_IMAGE_ENTRY_SIZE_BYTES,
        OS_SWAP_IMAGE_METADATA_START_LBA,
        OS_SWAP_IMAGE_DATA_START_LBA,
        generation,
    )
    for fieldIndex, value in enumerate(fields):
        struct.pack_into("<Q", sector, 8 + fieldIndex * 8, value)
    struct.pack_into(
        "<Q",
        sector,
        OS_SWAP_IMAGE_CHECKSUM_OFFSET_BYTES,
        calculateSwapImageChecksum(
            sector[:OS_SWAP_IMAGE_CHECKSUM_OFFSET_BYTES]
        ),
    )
    return bytes(sector)


def writeSwapImage(imagePath: Path) -> None:
    writeSparseImage(
        imagePath,
        encodeSwapImageSuperblock(),
        OS_SWAP_IMAGE_SIZE_BYTES,
    )


def auditSwapImage(imagePath: Path) -> None:
    if not imagePath.is_file():
        raise OsToolError(f"swap 盘镜像不存在：{imagePath}")
    if imagePath.stat().st_size != OS_SWAP_IMAGE_SIZE_BYTES:
        raise OsToolError(
            "swap 盘镜像长度不正确："
            f"{imagePath.stat().st_size} != {OS_SWAP_IMAGE_SIZE_BYTES}"
        )
    with imagePath.open("rb") as imageFile:
        sector = imageFile.read(OS_SWAP_IMAGE_SECTOR_SIZE_BYTES)
    expected = encodeSwapImageSuperblock()
    if sector != expected:
        raise OsToolError("swap 盘 superblock 格式、几何或校验和不正确。")

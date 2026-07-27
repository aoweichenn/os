from dataclasses import dataclass, field
import binascii
import json
from pathlib import Path
import struct

from .errors import OsToolError


OS_ROOTFS_V2_BLOCK_SIZE_BYTES = 512
OS_ROOTFS_V2_START_LBA = 2048
OS_ROOTFS_V2_TOTAL_BLOCK_COUNT = 524_288
OS_ROOTFS_V2_REGION_SIZE_BYTES = (
    OS_ROOTFS_V2_TOTAL_BLOCK_COUNT * OS_ROOTFS_V2_BLOCK_SIZE_BYTES
)
OS_ROOTFS_V2_MINIMUM_DISK_SIZE_BYTES = (
    OS_ROOTFS_V2_START_LBA * OS_ROOTFS_V2_BLOCK_SIZE_BYTES
    + OS_ROOTFS_V2_REGION_SIZE_BYTES
)
OS_ROOTFS_V2_CAPACITY_IMAGE_SIZE_BYTES = 1024 * 1024 * 1024
OS_ROOTFS_V2_MAGIC = b"OSRFV002"
OS_ROOTFS_V2_VERSION = 2
OS_ROOTFS_V2_SUPERBLOCK_RELATIVE_BLOCK = 0
OS_ROOTFS_V2_INODE_BITMAP_START_RELATIVE_BLOCK = 1
OS_ROOTFS_V2_INODE_BITMAP_BLOCK_COUNT = 2
OS_ROOTFS_V2_INODE_TABLE_START_RELATIVE_BLOCK = 3
OS_ROOTFS_V2_INODE_TABLE_BLOCK_COUNT = 4096
OS_ROOTFS_V2_DATA_BITMAP_START_RELATIVE_BLOCK = 4099
OS_ROOTFS_V2_DATA_BITMAP_BLOCK_COUNT = 128
OS_ROOTFS_V2_DATA_START_RELATIVE_BLOCK = 4227
OS_ROOTFS_V2_DATA_BLOCK_COUNT = (
    OS_ROOTFS_V2_TOTAL_BLOCK_COUNT
    - OS_ROOTFS_V2_DATA_START_RELATIVE_BLOCK
)
OS_ROOTFS_V2_INODE_COUNT = 8192
OS_ROOTFS_V2_ROOT_INODE_NUMBER = 1
OS_ROOTFS_V2_INODE_SIZE_BYTES = 256
OS_ROOTFS_V2_INODES_PER_BLOCK = 2
OS_ROOTFS_V2_DIRECTORY_ENTRY_SIZE_BYTES = 320
OS_ROOTFS_V2_MAXIMUM_NAME_LENGTH_BYTES = 255
OS_ROOTFS_V2_NAME_STORAGE_SIZE_BYTES = 256
OS_ROOTFS_V2_DIRECT_BLOCK_COUNT = 8
OS_ROOTFS_V2_POINTERS_PER_INDIRECT_BLOCK = 63
OS_ROOTFS_V2_MAXIMUM_FILE_SIZE_BYTES = 64 * 1024 * 1024
OS_ROOTFS_V2_TRANSACTION_STATE_CLEAN = 1
OS_ROOTFS_V2_TRANSACTION_STATE_DIRTY = 2
OS_ROOTFS_V2_NODE_TYPE_UNUSED = 0
OS_ROOTFS_V2_NODE_TYPE_REGULAR_FILE = 1
OS_ROOTFS_V2_NODE_TYPE_DIRECTORY = 2
OS_ROOTFS_V2_INITIAL_TRANSACTION_GENERATION = 1
OS_ROOTFS_V2_INITIAL_ROOT_GENERATION = 1
OS_ROOTFS_V2_INITIAL_NEXT_INODE_GENERATION = 2
OS_ROOTFS_V2_ROOT_LINK_COUNT = 1
OS_ROOTFS_V2_FEATURE_SPARSE_FILES = 1 << 0
OS_ROOTFS_V2_FEATURE_CHECKSUMMED_POINTER_BLOCKS = 1 << 1
OS_ROOTFS_V2_REQUIRED_FEATURES = (
    OS_ROOTFS_V2_FEATURE_SPARSE_FILES
    | OS_ROOTFS_V2_FEATURE_CHECKSUMMED_POINTER_BLOCKS
)
OS_ROOTFS_V2_SUPERBLOCK_CHECKSUM_OFFSET_BYTES = 508
OS_ROOTFS_V2_INODE_CHECKSUM_OFFSET_BYTES = 252
OS_ROOTFS_V2_POINTER_BLOCK_CHECKSUM_OFFSET_BYTES = 504
OS_ROOTFS_V2_POINTER_BLOCK_RESERVED_OFFSET_BYTES = 508
OS_ROOTFS_V2_DIRECTORY_ENTRY_CHECKSUM_OFFSET_BYTES = 288
OS_ROOTFS_V2_DIRECTORY_ENTRY_RESERVED_OFFSET_BYTES = 292
OS_ROOTFS_V2_DIRECTORY_ENTRY_RESERVED_SIZE_BYTES = 28
OS_ROOTFS_V2_UINT64_SIZE_BYTES = 8
OS_ROOTFS_V2_UINT32_SIZE_BYTES = 4
OS_ROOTFS_V2_CORRUPTION_MASK = 0x01
OS_ROOTFS_V2_CORRUPTION_KINDS = (
    "superblock-checksum",
    "root-inode-checksum",
    "inode-bitmap",
    "data-bitmap",
    "transaction-dirty",
)


@dataclass(frozen=True)
class RootfsV2Superblock:
    transactionState: int
    transactionGeneration: int
    nextInodeGeneration: int


@dataclass(frozen=True)
class RootfsV2Inode:
    nodeType: int
    sizeBytes: int
    generation: int
    linkCount: int
    allocatedDataBlockCount: int
    allocatedMetadataBlockCount: int
    parentInodeNumber: int
    directBlocks: tuple[int, ...]
    singleIndirectBlock: int
    doubleIndirectBlock: int
    tripleIndirectBlock: int


@dataclass(frozen=True)
class RootfsV2DirectoryEntry:
    inodeNumber: int
    inodeGeneration: int
    nodeType: int
    name: bytes


@dataclass(frozen=True)
class RootfsV2Inspection:
    reachableInodeCount: int
    directoryCount: int
    regularFileCount: int
    allocatedDataBlockCount: int
    allocatedMetadataBlockCount: int
    logicalFileBytes: int
    transactionGeneration: int


@dataclass(frozen=True)
class RootfsV2InstallFile:
    imagePath: str
    sourcePath: Path


@dataclass
class _RootfsV2BuildNode:
    name: bytes
    nodeType: int
    content: bytes = b""
    parent: "_RootfsV2BuildNode | None" = None
    children: dict[bytes, "_RootfsV2BuildNode"] = field(
        default_factory=dict
    )
    inodeNumber: int = 0
    generation: int = 0


def calculateRootfsV2Crc32(content: bytes | bytearray) -> int:
    return binascii.crc32(content) & 0xFFFF_FFFF


def rootfsV2AbsoluteOffset(relativeBlock: int) -> int:
    return (
        OS_ROOTFS_V2_START_LBA + relativeBlock
    ) * OS_ROOTFS_V2_BLOCK_SIZE_BYTES


def validateRootfsV2StaticLayout() -> None:
    if (
        OS_ROOTFS_V2_INODE_BITMAP_START_RELATIVE_BLOCK
        + OS_ROOTFS_V2_INODE_BITMAP_BLOCK_COUNT
        != OS_ROOTFS_V2_INODE_TABLE_START_RELATIVE_BLOCK
    ):
        raise OsToolError("rootfs v2 inode 位图与 inode 表不连续。")
    if (
        OS_ROOTFS_V2_INODE_TABLE_START_RELATIVE_BLOCK
        + OS_ROOTFS_V2_INODE_TABLE_BLOCK_COUNT
        != OS_ROOTFS_V2_DATA_BITMAP_START_RELATIVE_BLOCK
    ):
        raise OsToolError("rootfs v2 inode 表与数据位图不连续。")
    if (
        OS_ROOTFS_V2_DATA_BITMAP_START_RELATIVE_BLOCK
        + OS_ROOTFS_V2_DATA_BITMAP_BLOCK_COUNT
        != OS_ROOTFS_V2_DATA_START_RELATIVE_BLOCK
    ):
        raise OsToolError("rootfs v2 数据位图与数据区不连续。")
    if (
        OS_ROOTFS_V2_DATA_START_RELATIVE_BLOCK
        + OS_ROOTFS_V2_DATA_BLOCK_COUNT
        != OS_ROOTFS_V2_TOTAL_BLOCK_COUNT
    ):
        raise OsToolError("rootfs v2 数据区没有覆盖格式声明的末尾。")
    if (
        OS_ROOTFS_V2_INODE_COUNT * OS_ROOTFS_V2_INODE_SIZE_BYTES
        != OS_ROOTFS_V2_INODE_TABLE_BLOCK_COUNT
        * OS_ROOTFS_V2_BLOCK_SIZE_BYTES
    ):
        raise OsToolError("rootfs v2 inode 数量与 inode 表容量不一致。")
    if (
        OS_ROOTFS_V2_DATA_BLOCK_COUNT
        > OS_ROOTFS_V2_DATA_BITMAP_BLOCK_COUNT
        * OS_ROOTFS_V2_BLOCK_SIZE_BYTES
        * 8
    ):
        raise OsToolError("rootfs v2 数据位图无法覆盖全部数据块。")
    addressableBlocks = (
        OS_ROOTFS_V2_DIRECT_BLOCK_COUNT
        + OS_ROOTFS_V2_POINTERS_PER_INDIRECT_BLOCK
        + OS_ROOTFS_V2_POINTERS_PER_INDIRECT_BLOCK**2
        + OS_ROOTFS_V2_POINTERS_PER_INDIRECT_BLOCK**3
    )
    if (
        addressableBlocks * OS_ROOTFS_V2_BLOCK_SIZE_BYTES
        < OS_ROOTFS_V2_MAXIMUM_FILE_SIZE_BYTES
    ):
        raise OsToolError("rootfs v2 三级间接树无法覆盖单文件规格。")


def encodeRootfsV2Superblock(
    transactionState: int = OS_ROOTFS_V2_TRANSACTION_STATE_CLEAN,
    transactionGeneration: int = OS_ROOTFS_V2_INITIAL_TRANSACTION_GENERATION,
    nextInodeGeneration: int = OS_ROOTFS_V2_INITIAL_NEXT_INODE_GENERATION,
) -> bytes:
    if transactionState not in (
        OS_ROOTFS_V2_TRANSACTION_STATE_CLEAN,
        OS_ROOTFS_V2_TRANSACTION_STATE_DIRTY,
    ):
        raise OsToolError("rootfs v2 事务状态无效。")
    if transactionGeneration <= 0 or nextInodeGeneration <= 0:
        raise OsToolError("rootfs v2 代次必须为正数。")
    block = bytearray(OS_ROOTFS_V2_BLOCK_SIZE_BYTES)
    block[0:len(OS_ROOTFS_V2_MAGIC)] = OS_ROOTFS_V2_MAGIC
    fields = (
        OS_ROOTFS_V2_VERSION,
        OS_ROOTFS_V2_BLOCK_SIZE_BYTES,
        OS_ROOTFS_V2_TOTAL_BLOCK_COUNT,
        OS_ROOTFS_V2_INODE_BITMAP_START_RELATIVE_BLOCK,
        OS_ROOTFS_V2_INODE_BITMAP_BLOCK_COUNT,
        OS_ROOTFS_V2_INODE_TABLE_START_RELATIVE_BLOCK,
        OS_ROOTFS_V2_INODE_TABLE_BLOCK_COUNT,
        OS_ROOTFS_V2_DATA_BITMAP_START_RELATIVE_BLOCK,
        OS_ROOTFS_V2_DATA_BITMAP_BLOCK_COUNT,
        OS_ROOTFS_V2_DATA_START_RELATIVE_BLOCK,
        OS_ROOTFS_V2_DATA_BLOCK_COUNT,
        OS_ROOTFS_V2_INODE_COUNT,
        OS_ROOTFS_V2_ROOT_INODE_NUMBER,
        OS_ROOTFS_V2_MAXIMUM_FILE_SIZE_BYTES,
        transactionState,
        transactionGeneration,
        nextInodeGeneration,
        OS_ROOTFS_V2_REQUIRED_FEATURES,
    )
    for fieldIndex, value in enumerate(fields):
        struct.pack_into("<Q", block, 8 + fieldIndex * 8, value)
    checksum = calculateRootfsV2Crc32(
        block[:OS_ROOTFS_V2_SUPERBLOCK_CHECKSUM_OFFSET_BYTES]
    )
    struct.pack_into(
        "<I",
        block,
        OS_ROOTFS_V2_SUPERBLOCK_CHECKSUM_OFFSET_BYTES,
        checksum,
    )
    return bytes(block)


def decodeRootfsV2Superblock(block: bytes) -> RootfsV2Superblock:
    if len(block) != OS_ROOTFS_V2_BLOCK_SIZE_BYTES:
        raise OsToolError("rootfs v2 superblock 长度不正确。")
    if block[:len(OS_ROOTFS_V2_MAGIC)] != OS_ROOTFS_V2_MAGIC:
        raise OsToolError("rootfs v2 superblock magic 不正确。")
    storedChecksum = struct.unpack_from(
        "<I",
        block,
        OS_ROOTFS_V2_SUPERBLOCK_CHECKSUM_OFFSET_BYTES,
    )[0]
    calculatedChecksum = calculateRootfsV2Crc32(
        block[:OS_ROOTFS_V2_SUPERBLOCK_CHECKSUM_OFFSET_BYTES]
    )
    if storedChecksum != calculatedChecksum:
        raise OsToolError("rootfs v2 superblock CRC32 校验失败。")
    fields = tuple(
        struct.unpack_from("<Q", block, 8 + fieldIndex * 8)[0]
        for fieldIndex in range(18)
    )
    expectedFields = (
        OS_ROOTFS_V2_VERSION,
        OS_ROOTFS_V2_BLOCK_SIZE_BYTES,
        OS_ROOTFS_V2_TOTAL_BLOCK_COUNT,
        OS_ROOTFS_V2_INODE_BITMAP_START_RELATIVE_BLOCK,
        OS_ROOTFS_V2_INODE_BITMAP_BLOCK_COUNT,
        OS_ROOTFS_V2_INODE_TABLE_START_RELATIVE_BLOCK,
        OS_ROOTFS_V2_INODE_TABLE_BLOCK_COUNT,
        OS_ROOTFS_V2_DATA_BITMAP_START_RELATIVE_BLOCK,
        OS_ROOTFS_V2_DATA_BITMAP_BLOCK_COUNT,
        OS_ROOTFS_V2_DATA_START_RELATIVE_BLOCK,
        OS_ROOTFS_V2_DATA_BLOCK_COUNT,
        OS_ROOTFS_V2_INODE_COUNT,
        OS_ROOTFS_V2_ROOT_INODE_NUMBER,
        OS_ROOTFS_V2_MAXIMUM_FILE_SIZE_BYTES,
    )
    if fields[:len(expectedFields)] != expectedFields:
        raise OsToolError("rootfs v2 superblock 布局或版本不受支持。")
    transactionState = fields[14]
    transactionGeneration = fields[15]
    nextInodeGeneration = fields[16]
    featureFlags = fields[17]
    if transactionState not in (
        OS_ROOTFS_V2_TRANSACTION_STATE_CLEAN,
        OS_ROOTFS_V2_TRANSACTION_STATE_DIRTY,
    ):
        raise OsToolError("rootfs v2 superblock 事务状态无效。")
    if transactionGeneration <= 0 or nextInodeGeneration <= 0:
        raise OsToolError("rootfs v2 superblock 代次无效。")
    if featureFlags != OS_ROOTFS_V2_REQUIRED_FEATURES:
        raise OsToolError("rootfs v2 包含不受支持的格式特性。")
    if any(
        block[
            8 + len(fields) * 8:
            OS_ROOTFS_V2_SUPERBLOCK_CHECKSUM_OFFSET_BYTES
        ]
    ):
        raise OsToolError("rootfs v2 superblock 保留区域必须为零。")
    return RootfsV2Superblock(
        transactionState=transactionState,
        transactionGeneration=transactionGeneration,
        nextInodeGeneration=nextInodeGeneration,
    )


def encodeRootfsV2Inode(inode: RootfsV2Inode) -> bytes:
    if inode.nodeType not in (
        OS_ROOTFS_V2_NODE_TYPE_REGULAR_FILE,
        OS_ROOTFS_V2_NODE_TYPE_DIRECTORY,
    ):
        raise OsToolError("rootfs v2 inode 类型无效。")
    if len(inode.directBlocks) != OS_ROOTFS_V2_DIRECT_BLOCK_COUNT:
        raise OsToolError("rootfs v2 inode 直接块数量无效。")
    if (
        inode.sizeBytes < 0
        or inode.sizeBytes > OS_ROOTFS_V2_MAXIMUM_FILE_SIZE_BYTES
        or inode.generation <= 0
        or inode.linkCount <= 0
        or inode.parentInodeNumber <= 0
        or inode.parentInodeNumber > OS_ROOTFS_V2_INODE_COUNT
    ):
        raise OsToolError("rootfs v2 inode 字段无效。")
    encoded = bytearray(OS_ROOTFS_V2_INODE_SIZE_BYTES)
    scalarFields = (
        inode.nodeType,
        0,
        inode.sizeBytes,
        inode.generation,
        inode.linkCount,
        inode.allocatedDataBlockCount,
        inode.allocatedMetadataBlockCount,
        inode.parentInodeNumber,
    )
    for fieldIndex, value in enumerate(scalarFields):
        struct.pack_into("<Q", encoded, fieldIndex * 8, value)
    for blockIndex, relativeBlock in enumerate(inode.directBlocks):
        struct.pack_into("<Q", encoded, 64 + blockIndex * 8, relativeBlock)
    struct.pack_into("<Q", encoded, 128, inode.singleIndirectBlock)
    struct.pack_into("<Q", encoded, 136, inode.doubleIndirectBlock)
    struct.pack_into("<Q", encoded, 144, inode.tripleIndirectBlock)
    checksum = calculateRootfsV2Crc32(
        encoded[:OS_ROOTFS_V2_INODE_CHECKSUM_OFFSET_BYTES]
    )
    struct.pack_into(
        "<I",
        encoded,
        OS_ROOTFS_V2_INODE_CHECKSUM_OFFSET_BYTES,
        checksum,
    )
    return bytes(encoded)


def decodeRootfsV2Inode(encoded: bytes) -> RootfsV2Inode:
    if len(encoded) != OS_ROOTFS_V2_INODE_SIZE_BYTES:
        raise OsToolError("rootfs v2 inode 长度不正确。")
    storedChecksum = struct.unpack_from(
        "<I",
        encoded,
        OS_ROOTFS_V2_INODE_CHECKSUM_OFFSET_BYTES,
    )[0]
    calculatedChecksum = calculateRootfsV2Crc32(
        encoded[:OS_ROOTFS_V2_INODE_CHECKSUM_OFFSET_BYTES]
    )
    if storedChecksum != calculatedChecksum:
        raise OsToolError("rootfs v2 inode CRC32 校验失败。")
    values = tuple(
        struct.unpack_from("<Q", encoded, fieldIndex * 8)[0]
        for fieldIndex in range(8)
    )
    nodeType, flags, sizeBytes, generation, linkCount = values[:5]
    if flags != 0:
        raise OsToolError("rootfs v2 inode 包含未知标志。")
    inode = RootfsV2Inode(
        nodeType=nodeType,
        sizeBytes=sizeBytes,
        generation=generation,
        linkCount=linkCount,
        allocatedDataBlockCount=values[5],
        allocatedMetadataBlockCount=values[6],
        parentInodeNumber=values[7],
        directBlocks=tuple(
            struct.unpack_from("<Q", encoded, 64 + blockIndex * 8)[0]
            for blockIndex in range(OS_ROOTFS_V2_DIRECT_BLOCK_COUNT)
        ),
        singleIndirectBlock=struct.unpack_from("<Q", encoded, 128)[0],
        doubleIndirectBlock=struct.unpack_from("<Q", encoded, 136)[0],
        tripleIndirectBlock=struct.unpack_from("<Q", encoded, 144)[0],
    )
    if (
        inode.nodeType not in (
            OS_ROOTFS_V2_NODE_TYPE_REGULAR_FILE,
            OS_ROOTFS_V2_NODE_TYPE_DIRECTORY,
        )
        or inode.sizeBytes > OS_ROOTFS_V2_MAXIMUM_FILE_SIZE_BYTES
        or inode.generation <= 0
        or inode.linkCount <= 0
        or not (
            1
            <= inode.parentInodeNumber
            <= OS_ROOTFS_V2_INODE_COUNT
        )
        or any(encoded[152:OS_ROOTFS_V2_INODE_CHECKSUM_OFFSET_BYTES])
    ):
        raise OsToolError("rootfs v2 inode 内容无效。")
    return inode


def decodeRootfsV2PointerBlock(block: bytes) -> tuple[int, ...]:
    if len(block) != OS_ROOTFS_V2_BLOCK_SIZE_BYTES:
        raise OsToolError("rootfs v2 间接块长度不正确。")
    storedChecksum = struct.unpack_from(
        "<I",
        block,
        OS_ROOTFS_V2_POINTER_BLOCK_CHECKSUM_OFFSET_BYTES,
    )[0]
    calculatedChecksum = calculateRootfsV2Crc32(
        block[:OS_ROOTFS_V2_POINTER_BLOCK_CHECKSUM_OFFSET_BYTES]
    )
    if storedChecksum != calculatedChecksum:
        raise OsToolError("rootfs v2 间接块 CRC32 校验失败。")
    if any(block[OS_ROOTFS_V2_POINTER_BLOCK_RESERVED_OFFSET_BYTES:]):
        raise OsToolError("rootfs v2 间接块保留区域必须为零。")
    return tuple(
        struct.unpack_from("<Q", block, pointerIndex * 8)[0]
        for pointerIndex in range(
            OS_ROOTFS_V2_POINTERS_PER_INDIRECT_BLOCK
        )
    )


def encodeRootfsV2PointerBlock(pointers: tuple[int, ...]) -> bytes:
    if len(pointers) != OS_ROOTFS_V2_POINTERS_PER_INDIRECT_BLOCK:
        raise OsToolError("rootfs v2 间接块指针数量无效。")
    block = bytearray(OS_ROOTFS_V2_BLOCK_SIZE_BYTES)
    for pointerIndex, relativeBlock in enumerate(pointers):
        if (
            relativeBlock != 0
            and not (
                OS_ROOTFS_V2_DATA_START_RELATIVE_BLOCK
                <= relativeBlock
                < OS_ROOTFS_V2_TOTAL_BLOCK_COUNT
            )
        ):
            raise OsToolError("rootfs v2 间接块包含越界指针。")
        struct.pack_into(
            "<Q",
            block,
            pointerIndex * OS_ROOTFS_V2_UINT64_SIZE_BYTES,
            relativeBlock,
        )
    checksum = calculateRootfsV2Crc32(
        block[:OS_ROOTFS_V2_POINTER_BLOCK_CHECKSUM_OFFSET_BYTES]
    )
    struct.pack_into(
        "<I",
        block,
        OS_ROOTFS_V2_POINTER_BLOCK_CHECKSUM_OFFSET_BYTES,
        checksum,
    )
    return bytes(block)


def encodeRootfsV2DirectoryEntry(
    entry: RootfsV2DirectoryEntry,
) -> bytes:
    if (
        not 1 <= entry.inodeNumber <= OS_ROOTFS_V2_INODE_COUNT
        or entry.inodeGeneration <= 0
        or entry.nodeType not in (
            OS_ROOTFS_V2_NODE_TYPE_REGULAR_FILE,
            OS_ROOTFS_V2_NODE_TYPE_DIRECTORY,
        )
        or not 1 <= len(entry.name)
        <= OS_ROOTFS_V2_MAXIMUM_NAME_LENGTH_BYTES
        or entry.name in (b".", b"..")
        or b"/" in entry.name
        or any(
            character <= 0x1F or character == 0x7F
            for character in entry.name
        )
    ):
        raise OsToolError("rootfs v2 目录项字段无效。")
    encoded = bytearray(OS_ROOTFS_V2_DIRECTORY_ENTRY_SIZE_BYTES)
    scalarFields = (
        entry.inodeNumber,
        entry.inodeGeneration,
        entry.nodeType,
        len(entry.name),
    )
    for fieldIndex, value in enumerate(scalarFields):
        struct.pack_into(
            "<Q",
            encoded,
            fieldIndex * OS_ROOTFS_V2_UINT64_SIZE_BYTES,
            value,
        )
    encoded[
        32:32 + len(entry.name)
    ] = entry.name
    checksum = calculateRootfsV2Crc32(
        encoded[:OS_ROOTFS_V2_DIRECTORY_ENTRY_CHECKSUM_OFFSET_BYTES]
    )
    struct.pack_into(
        "<I",
        encoded,
        OS_ROOTFS_V2_DIRECTORY_ENTRY_CHECKSUM_OFFSET_BYTES,
        checksum,
    )
    return bytes(encoded)


def decodeRootfsV2DirectoryEntry(
    encoded: bytes,
) -> RootfsV2DirectoryEntry | None:
    if len(encoded) != OS_ROOTFS_V2_DIRECTORY_ENTRY_SIZE_BYTES:
        raise OsToolError("rootfs v2 目录项长度不正确。")
    inodeNumber, inodeGeneration, nodeType, nameLengthBytes = (
        struct.unpack_from("<Q", encoded, fieldIndex * 8)[0]
        for fieldIndex in range(4)
    )
    if inodeNumber == 0:
        if any(encoded):
            raise OsToolError("rootfs v2 空目录项必须全零。")
        return None
    storedChecksum = struct.unpack_from(
        "<I",
        encoded,
        OS_ROOTFS_V2_DIRECTORY_ENTRY_CHECKSUM_OFFSET_BYTES,
    )[0]
    calculatedChecksum = calculateRootfsV2Crc32(
        encoded[:OS_ROOTFS_V2_DIRECTORY_ENTRY_CHECKSUM_OFFSET_BYTES]
    )
    if storedChecksum != calculatedChecksum:
        raise OsToolError("rootfs v2 目录项 CRC32 校验失败。")
    if (
        not 1 <= inodeNumber <= OS_ROOTFS_V2_INODE_COUNT
        or inodeGeneration <= 0
        or nodeType not in (
            OS_ROOTFS_V2_NODE_TYPE_REGULAR_FILE,
            OS_ROOTFS_V2_NODE_TYPE_DIRECTORY,
        )
        or not 1 <= nameLengthBytes <= OS_ROOTFS_V2_MAXIMUM_NAME_LENGTH_BYTES
        or any(
            encoded[
                32 + nameLengthBytes:
                32 + OS_ROOTFS_V2_NAME_STORAGE_SIZE_BYTES
            ]
        )
        or any(
            encoded[
                OS_ROOTFS_V2_DIRECTORY_ENTRY_RESERVED_OFFSET_BYTES:
                OS_ROOTFS_V2_DIRECTORY_ENTRY_SIZE_BYTES
            ]
        )
    ):
        raise OsToolError("rootfs v2 目录项内容无效。")
    name = encoded[32:32 + nameLengthBytes]
    if (
        name in (b".", b"..")
        or b"/" in name
        or any(character <= 0x1F or character == 0x7F for character in name)
    ):
        raise OsToolError("rootfs v2 目录项名称无效。")
    return RootfsV2DirectoryEntry(
        inodeNumber=inodeNumber,
        inodeGeneration=inodeGeneration,
        nodeType=nodeType,
        name=name,
    )


def readRootfsV2Block(imageFile, relativeBlock: int) -> bytes:
    if not 0 <= relativeBlock < OS_ROOTFS_V2_TOTAL_BLOCK_COUNT:
        raise OsToolError(f"rootfs v2 相对块越界：{relativeBlock}。")
    imageFile.seek(rootfsV2AbsoluteOffset(relativeBlock))
    block = imageFile.read(OS_ROOTFS_V2_BLOCK_SIZE_BYTES)
    if len(block) != OS_ROOTFS_V2_BLOCK_SIZE_BYTES:
        raise OsToolError(f"rootfs v2 块 {relativeBlock} 被截断。")
    return block


def writeRootfsV2Block(
    imageFile,
    relativeBlock: int,
    block: bytes,
) -> None:
    if len(block) != OS_ROOTFS_V2_BLOCK_SIZE_BYTES:
        raise OsToolError("rootfs v2 写入块长度不正确。")
    imageFile.seek(rootfsV2AbsoluteOffset(relativeBlock))
    imageFile.write(block)


def formatRootfsV2(
    imagePath: Path,
    imageSizeBytes: int | None = None,
    createImage: bool = False,
    force: bool = False,
) -> None:
    validateRootfsV2StaticLayout()
    if createImage:
        if imageSizeBytes is None:
            imageSizeBytes = OS_ROOTFS_V2_CAPACITY_IMAGE_SIZE_BYTES
        if imageSizeBytes < OS_ROOTFS_V2_MINIMUM_DISK_SIZE_BYTES:
            raise OsToolError(
                "rootfs v2 磁盘镜像不足以容纳 256 MiB 文件系统。"
            )
        if imageSizeBytes % OS_ROOTFS_V2_BLOCK_SIZE_BYTES != 0:
            raise OsToolError("rootfs v2 磁盘镜像必须是整扇区长度。")
        imagePath.parent.mkdir(parents=True, exist_ok=True)
        with imagePath.open("wb") as imageFile:
            imageFile.truncate(imageSizeBytes)
    if not imagePath.is_file():
        raise OsToolError(f"rootfs v2 目标镜像不存在：{imagePath}")
    diskSizeBytes = imagePath.stat().st_size
    if diskSizeBytes < OS_ROOTFS_V2_MINIMUM_DISK_SIZE_BYTES:
        raise OsToolError(
            "rootfs v2 目标镜像不足以容纳固定文件系统区域。"
        )

    with imagePath.open("r+b") as imageFile:
        existingSuperblock = readRootfsV2Block(
            imageFile,
            OS_ROOTFS_V2_SUPERBLOCK_RELATIVE_BLOCK,
        )
        if any(existingSuperblock) and not force:
            raise OsToolError(
                "rootfs v2 区域并非全零；需要显式 --force 才能重新格式化。"
            )

        zeroBlock = bytes(OS_ROOTFS_V2_BLOCK_SIZE_BYTES)
        metadataEndBlock = OS_ROOTFS_V2_DATA_START_RELATIVE_BLOCK
        for relativeBlock in range(
            OS_ROOTFS_V2_INODE_BITMAP_START_RELATIVE_BLOCK,
            metadataEndBlock,
        ):
            writeRootfsV2Block(imageFile, relativeBlock, zeroBlock)

        inodeBitmap = bytearray(OS_ROOTFS_V2_BLOCK_SIZE_BYTES)
        inodeBitmap[0] = 0x01
        writeRootfsV2Block(
            imageFile,
            OS_ROOTFS_V2_INODE_BITMAP_START_RELATIVE_BLOCK,
            bytes(inodeBitmap),
        )
        rootInode = RootfsV2Inode(
            nodeType=OS_ROOTFS_V2_NODE_TYPE_DIRECTORY,
            sizeBytes=0,
            generation=OS_ROOTFS_V2_INITIAL_ROOT_GENERATION,
            linkCount=OS_ROOTFS_V2_ROOT_LINK_COUNT,
            allocatedDataBlockCount=0,
            allocatedMetadataBlockCount=0,
            parentInodeNumber=OS_ROOTFS_V2_ROOT_INODE_NUMBER,
            directBlocks=(0,) * OS_ROOTFS_V2_DIRECT_BLOCK_COUNT,
            singleIndirectBlock=0,
            doubleIndirectBlock=0,
            tripleIndirectBlock=0,
        )
        inodeBlock = bytearray(OS_ROOTFS_V2_BLOCK_SIZE_BYTES)
        inodeBlock[:OS_ROOTFS_V2_INODE_SIZE_BYTES] = (
            encodeRootfsV2Inode(rootInode)
        )
        writeRootfsV2Block(
            imageFile,
            OS_ROOTFS_V2_INODE_TABLE_START_RELATIVE_BLOCK,
            bytes(inodeBlock),
        )
        writeRootfsV2Block(
            imageFile,
            OS_ROOTFS_V2_SUPERBLOCK_RELATIVE_BLOCK,
            encodeRootfsV2Superblock(),
        )
        imageFile.flush()


class _RootfsV2ImageBuilder:
    def __init__(self, imageFile) -> None:
        self.imageFile = imageFile
        self.nextDataBlock = OS_ROOTFS_V2_DATA_START_RELATIVE_BLOCK
        self.allocatedDataBlocks: list[int] = []
        self.inodes: dict[int, RootfsV2Inode] = {}

    def allocateBlock(self, content: bytes) -> int:
        if len(content) > OS_ROOTFS_V2_BLOCK_SIZE_BYTES:
            raise OsToolError("rootfs v2 单块写入内容过长。")
        if self.nextDataBlock >= OS_ROOTFS_V2_TOTAL_BLOCK_COUNT:
            raise OsToolError("rootfs v2 数据块容量耗尽。")
        relativeBlock = self.nextDataBlock
        self.nextDataBlock += 1
        block = bytearray(OS_ROOTFS_V2_BLOCK_SIZE_BYTES)
        block[:len(content)] = content
        writeRootfsV2Block(self.imageFile, relativeBlock, bytes(block))
        self.allocatedDataBlocks.append(relativeBlock)
        return relativeBlock

    def buildPointerTree(
        self,
        level: int,
        dataBlocks: tuple[int, ...],
    ) -> tuple[int, int]:
        if not dataBlocks:
            return 0, 0
        capacity = OS_ROOTFS_V2_POINTERS_PER_INDIRECT_BLOCK**level
        if len(dataBlocks) > capacity:
            raise OsToolError("rootfs v2 间接树输入超过层级容量。")
        pointers: list[int] = []
        metadataBlockCount = 0
        if level == 1:
            pointers.extend(dataBlocks)
        else:
            childCapacity = (
                OS_ROOTFS_V2_POINTERS_PER_INDIRECT_BLOCK
                ** (level - 1)
            )
            for childOffset in range(0, len(dataBlocks), childCapacity):
                childRoot, childMetadataCount = self.buildPointerTree(
                    level - 1,
                    dataBlocks[childOffset:childOffset + childCapacity],
                )
                pointers.append(childRoot)
                metadataBlockCount += childMetadataCount
        paddedPointers = tuple(
            pointers
            + [0] * (
                OS_ROOTFS_V2_POINTERS_PER_INDIRECT_BLOCK
                - len(pointers)
            )
        )
        rootBlock = self.allocateBlock(
            encodeRootfsV2PointerBlock(paddedPointers)
        )
        return rootBlock, metadataBlockCount + 1

    def writeFileContent(
        self,
        node: _RootfsV2BuildNode,
    ) -> RootfsV2Inode:
        if len(node.content) > OS_ROOTFS_V2_MAXIMUM_FILE_SIZE_BYTES:
            raise OsToolError(
                f"rootfs v2 文件超过 64 MiB：{node.name!r}。"
            )
        dataBlocks: list[int] = []
        for contentOffset in range(
            0,
            len(node.content),
            OS_ROOTFS_V2_BLOCK_SIZE_BYTES,
        ):
            dataBlocks.append(
                self.allocateBlock(
                    node.content[
                        contentOffset:
                        contentOffset + OS_ROOTFS_V2_BLOCK_SIZE_BYTES
                    ]
                )
            )

        directCount = min(
            len(dataBlocks),
            OS_ROOTFS_V2_DIRECT_BLOCK_COUNT,
        )
        directBlocks = tuple(
            dataBlocks[:directCount]
            + [0] * (OS_ROOTFS_V2_DIRECT_BLOCK_COUNT - directCount)
        )
        nextDataIndex = directCount
        metadataBlockCount = 0
        indirectRoots: list[int] = []
        for level in (1, 2, 3):
            levelCapacity = (
                OS_ROOTFS_V2_POINTERS_PER_INDIRECT_BLOCK**level
            )
            levelBlocks = tuple(
                dataBlocks[
                    nextDataIndex:
                    nextDataIndex + levelCapacity
                ]
            )
            rootBlock, levelMetadataCount = self.buildPointerTree(
                level,
                levelBlocks,
            )
            indirectRoots.append(rootBlock)
            metadataBlockCount += levelMetadataCount
            nextDataIndex += len(levelBlocks)
        if nextDataIndex != len(dataBlocks):
            raise OsToolError("rootfs v2 三级间接树容量不足。")
        if node.parent is None:
            parentInodeNumber = node.inodeNumber
        else:
            parentInodeNumber = node.parent.inodeNumber
        return RootfsV2Inode(
            nodeType=node.nodeType,
            sizeBytes=len(node.content),
            generation=node.generation,
            linkCount=1,
            allocatedDataBlockCount=len(dataBlocks),
            allocatedMetadataBlockCount=metadataBlockCount,
            parentInodeNumber=parentInodeNumber,
            directBlocks=directBlocks,
            singleIndirectBlock=indirectRoots[0],
            doubleIndirectBlock=indirectRoots[1],
            tripleIndirectBlock=indirectRoots[2],
        )

    def writeInode(self, inodeNumber: int, inode: RootfsV2Inode) -> None:
        inodeOffset = (
            inodeNumber - 1
        ) * OS_ROOTFS_V2_INODE_SIZE_BYTES
        relativeBlock = (
            OS_ROOTFS_V2_INODE_TABLE_START_RELATIVE_BLOCK
            + inodeOffset // OS_ROOTFS_V2_BLOCK_SIZE_BYTES
        )
        byteOffset = inodeOffset % OS_ROOTFS_V2_BLOCK_SIZE_BYTES
        block = bytearray(readRootfsV2Block(self.imageFile, relativeBlock))
        block[
            byteOffset:byteOffset + OS_ROOTFS_V2_INODE_SIZE_BYTES
        ] = encodeRootfsV2Inode(inode)
        writeRootfsV2Block(self.imageFile, relativeBlock, bytes(block))
        self.inodes[inodeNumber] = inode

    def writeBitmaps(self, inodeCount: int) -> None:
        inodeBitmap = bytearray(
            OS_ROOTFS_V2_INODE_BITMAP_BLOCK_COUNT
            * OS_ROOTFS_V2_BLOCK_SIZE_BYTES
        )
        for inodeIndex in range(inodeCount):
            inodeBitmap[inodeIndex // 8] |= 1 << (inodeIndex % 8)
        for blockIndex in range(
            OS_ROOTFS_V2_INODE_BITMAP_BLOCK_COUNT
        ):
            blockOffset = (
                blockIndex * OS_ROOTFS_V2_BLOCK_SIZE_BYTES
            )
            writeRootfsV2Block(
                self.imageFile,
                OS_ROOTFS_V2_INODE_BITMAP_START_RELATIVE_BLOCK
                + blockIndex,
                bytes(
                    inodeBitmap[
                        blockOffset:
                        blockOffset + OS_ROOTFS_V2_BLOCK_SIZE_BYTES
                    ]
                ),
            )

        dataBitmap = bytearray(
            OS_ROOTFS_V2_DATA_BITMAP_BLOCK_COUNT
            * OS_ROOTFS_V2_BLOCK_SIZE_BYTES
        )
        for relativeBlock in self.allocatedDataBlocks:
            blockIndex = (
                relativeBlock - OS_ROOTFS_V2_DATA_START_RELATIVE_BLOCK
            )
            dataBitmap[blockIndex // 8] |= 1 << (blockIndex % 8)
        for bitmapBlockIndex in range(
            OS_ROOTFS_V2_DATA_BITMAP_BLOCK_COUNT
        ):
            blockOffset = (
                bitmapBlockIndex * OS_ROOTFS_V2_BLOCK_SIZE_BYTES
            )
            writeRootfsV2Block(
                self.imageFile,
                OS_ROOTFS_V2_DATA_BITMAP_START_RELATIVE_BLOCK
                + bitmapBlockIndex,
                bytes(
                    dataBitmap[
                        blockOffset:
                        blockOffset + OS_ROOTFS_V2_BLOCK_SIZE_BYTES
                    ]
                ),
            )


def _validateRootfsV2InstallPath(imagePath: str) -> tuple[bytes, ...]:
    if (
        not imagePath.startswith("/")
        or imagePath == "/"
        or imagePath.endswith("/")
        or "//" in imagePath
    ):
        raise OsToolError(
            f"rootfs v2 安装路径必须是规范绝对文件路径：{imagePath}"
        )
    try:
        components = tuple(
            component.encode("utf-8")
            for component in imagePath[1:].split("/")
        )
    except UnicodeEncodeError as error:
        raise OsToolError(
            f"rootfs v2 安装路径不是 UTF-8：{imagePath}"
        ) from error
    for component in components:
        if (
            not component
            or component in (b".", b"..")
            or len(component)
            > OS_ROOTFS_V2_MAXIMUM_NAME_LENGTH_BYTES
            or any(
                character <= 0x1F or character == 0x7F
                for character in component
            )
        ):
            raise OsToolError(
                f"rootfs v2 安装路径包含非法组件：{imagePath}"
            )
    return components


def installRootfsV2Files(
    imagePath: Path,
    files: tuple[RootfsV2InstallFile, ...],
) -> None:
    """把构建产物离线安装到刚格式化的 rootfs v2。"""
    if not files:
        return
    inspection = inspectRootfsV2(imagePath)
    if (
        inspection.reachableInodeCount != 1
        or inspection.directoryCount != 1
        or inspection.regularFileCount != 0
    ):
        raise OsToolError(
            "rootfs v2 离线安装只接受刚格式化的空文件系统。"
        )

    root = _RootfsV2BuildNode(
        name=b"",
        nodeType=OS_ROOTFS_V2_NODE_TYPE_DIRECTORY,
    )
    seenPaths: set[str] = set()
    for installFile in files:
        if installFile.imagePath in seenPaths:
            raise OsToolError(
                f"rootfs v2 安装路径重复：{installFile.imagePath}"
            )
        seenPaths.add(installFile.imagePath)
        if not installFile.sourcePath.is_file():
            raise OsToolError(
                f"rootfs v2 安装源文件不存在：{installFile.sourcePath}"
            )
        components = _validateRootfsV2InstallPath(
            installFile.imagePath
        )
        parent = root
        for component in components[:-1]:
            child = parent.children.get(component)
            if child is None:
                child = _RootfsV2BuildNode(
                    name=component,
                    nodeType=OS_ROOTFS_V2_NODE_TYPE_DIRECTORY,
                    parent=parent,
                )
                parent.children[component] = child
            elif (
                child.nodeType
                != OS_ROOTFS_V2_NODE_TYPE_DIRECTORY
            ):
                raise OsToolError(
                    "rootfs v2 安装路径的父组件已是普通文件。"
                )
            parent = child
        fileName = components[-1]
        if fileName in parent.children:
            raise OsToolError(
                f"rootfs v2 安装目标已存在：{installFile.imagePath}"
            )
        content = installFile.sourcePath.read_bytes()
        if len(content) > OS_ROOTFS_V2_MAXIMUM_FILE_SIZE_BYTES:
            raise OsToolError(
                f"rootfs v2 安装文件超过 64 MiB：{installFile.sourcePath}"
            )
        parent.children[fileName] = _RootfsV2BuildNode(
            name=fileName,
            nodeType=OS_ROOTFS_V2_NODE_TYPE_REGULAR_FILE,
            content=content,
            parent=parent,
        )

    orderedNodes: list[_RootfsV2BuildNode] = []

    def appendNodes(node: _RootfsV2BuildNode) -> None:
        orderedNodes.append(node)
        for childName in sorted(node.children):
            appendNodes(node.children[childName])

    appendNodes(root)
    if len(orderedNodes) > OS_ROOTFS_V2_INODE_COUNT:
        raise OsToolError("rootfs v2 安装内容耗尽 inode 容量。")
    for inodeIndex, node in enumerate(orderedNodes):
        node.inodeNumber = inodeIndex + 1
        node.generation = inodeIndex + 1
    for node in orderedNodes:
        if node.nodeType != OS_ROOTFS_V2_NODE_TYPE_DIRECTORY:
            continue
        directoryEntries = bytearray()
        for childName in sorted(node.children):
            child = node.children[childName]
            directoryEntries.extend(
                encodeRootfsV2DirectoryEntry(
                    RootfsV2DirectoryEntry(
                        inodeNumber=child.inodeNumber,
                        inodeGeneration=child.generation,
                        nodeType=child.nodeType,
                        name=child.name,
                    )
                )
            )
        node.content = bytes(directoryEntries)

    with imagePath.open("r+b") as imageFile:
        superblock = decodeRootfsV2Superblock(
            readRootfsV2Block(
                imageFile,
                OS_ROOTFS_V2_SUPERBLOCK_RELATIVE_BLOCK,
            )
        )
        dirtyGeneration = superblock.transactionGeneration + 1
        nextInodeGeneration = len(orderedNodes) + 1
        writeRootfsV2Block(
            imageFile,
            OS_ROOTFS_V2_SUPERBLOCK_RELATIVE_BLOCK,
            encodeRootfsV2Superblock(
                transactionState=OS_ROOTFS_V2_TRANSACTION_STATE_DIRTY,
                transactionGeneration=dirtyGeneration,
                nextInodeGeneration=nextInodeGeneration,
            ),
        )
        builder = _RootfsV2ImageBuilder(imageFile)
        for node in orderedNodes:
            inode = builder.writeFileContent(node)
            builder.writeInode(node.inodeNumber, inode)
        builder.writeBitmaps(len(orderedNodes))
        writeRootfsV2Block(
            imageFile,
            OS_ROOTFS_V2_SUPERBLOCK_RELATIVE_BLOCK,
            encodeRootfsV2Superblock(
                transactionState=OS_ROOTFS_V2_TRANSACTION_STATE_CLEAN,
                transactionGeneration=dirtyGeneration,
                nextInodeGeneration=nextInodeGeneration,
            ),
        )
        imageFile.flush()
    inspectRootfsV2(imagePath)


class RootfsV2Reader:
    def __init__(self, imagePath: Path) -> None:
        if not imagePath.is_file():
            raise OsToolError(f"rootfs v2 镜像不存在：{imagePath}")
        if imagePath.stat().st_size < OS_ROOTFS_V2_MINIMUM_DISK_SIZE_BYTES:
            raise OsToolError("rootfs v2 镜像被截断。")
        self.imagePath = imagePath
        self.imageFile = imagePath.open("rb")
        try:
            self.superblock = decodeRootfsV2Superblock(
                readRootfsV2Block(
                    self.imageFile,
                    OS_ROOTFS_V2_SUPERBLOCK_RELATIVE_BLOCK,
                )
            )
            self.inodeBitmap = self.readRegion(
                OS_ROOTFS_V2_INODE_BITMAP_START_RELATIVE_BLOCK,
                OS_ROOTFS_V2_INODE_BITMAP_BLOCK_COUNT,
            )
            self.dataBitmap = self.readRegion(
                OS_ROOTFS_V2_DATA_BITMAP_START_RELATIVE_BLOCK,
                OS_ROOTFS_V2_DATA_BITMAP_BLOCK_COUNT,
            )
        except Exception:
            self.imageFile.close()
            raise

    def close(self) -> None:
        self.imageFile.close()

    def readRegion(self, startBlock: int, blockCount: int) -> bytes:
        self.imageFile.seek(rootfsV2AbsoluteOffset(startBlock))
        content = self.imageFile.read(
            blockCount * OS_ROOTFS_V2_BLOCK_SIZE_BYTES
        )
        if len(content) != blockCount * OS_ROOTFS_V2_BLOCK_SIZE_BYTES:
            raise OsToolError("rootfs v2 元数据区域被截断。")
        return content

    @staticmethod
    def bitmapBitIsSet(bitmap: bytes, bitIndex: int) -> bool:
        return (
            bitmap[bitIndex // 8] & (1 << (bitIndex % 8))
        ) != 0

    def inodeAllocated(self, inodeNumber: int) -> bool:
        return self.bitmapBitIsSet(self.inodeBitmap, inodeNumber - 1)

    def dataBlockAllocated(self, relativeBlock: int) -> bool:
        return self.bitmapBitIsSet(
            self.dataBitmap,
            relativeBlock - OS_ROOTFS_V2_DATA_START_RELATIVE_BLOCK,
        )

    def readInode(self, inodeNumber: int) -> RootfsV2Inode:
        if not 1 <= inodeNumber <= OS_ROOTFS_V2_INODE_COUNT:
            raise OsToolError(f"rootfs v2 inode 号越界：{inodeNumber}。")
        if not self.inodeAllocated(inodeNumber):
            raise OsToolError(
                f"rootfs v2 inode {inodeNumber} 未在位图中分配。"
            )
        inodeOffset = (inodeNumber - 1) * OS_ROOTFS_V2_INODE_SIZE_BYTES
        relativeBlock = (
            OS_ROOTFS_V2_INODE_TABLE_START_RELATIVE_BLOCK
            + inodeOffset // OS_ROOTFS_V2_BLOCK_SIZE_BYTES
        )
        byteOffset = inodeOffset % OS_ROOTFS_V2_BLOCK_SIZE_BYTES
        block = readRootfsV2Block(self.imageFile, relativeBlock)
        return decodeRootfsV2Inode(
            block[byteOffset:byteOffset + OS_ROOTFS_V2_INODE_SIZE_BYTES]
        )

    def readPointerBlock(
        self,
        relativeBlock: int,
        allocatedBlocks: set[int],
    ) -> tuple[int, ...]:
        self.validateAllocatedDataBlock(relativeBlock, allocatedBlocks)
        return decodeRootfsV2PointerBlock(
            readRootfsV2Block(self.imageFile, relativeBlock)
        )

    def validateAllocatedDataBlock(
        self,
        relativeBlock: int,
        allocatedBlocks: set[int],
    ) -> None:
        if not (
            OS_ROOTFS_V2_DATA_START_RELATIVE_BLOCK
            <= relativeBlock
            < OS_ROOTFS_V2_TOTAL_BLOCK_COUNT
        ):
            raise OsToolError(
                f"rootfs v2 inode 引用了越界数据块：{relativeBlock}。"
            )
        if not self.dataBlockAllocated(relativeBlock):
            raise OsToolError(
                f"rootfs v2 inode 引用了位图中的空闲块：{relativeBlock}。"
            )
        if relativeBlock in allocatedBlocks:
            raise OsToolError(
                f"rootfs v2 数据块被重复引用：{relativeBlock}。"
            )
        allocatedBlocks.add(relativeBlock)

    def collectIndirectBlocks(
        self,
        relativeBlock: int,
        level: int,
        logicalStart: int,
        logicalBlockLimit: int,
        blockMap: dict[int, int],
        allocatedBlocks: set[int],
    ) -> tuple[int, int]:
        if relativeBlock == 0:
            return 0, 0
        pointers = self.readPointerBlock(relativeBlock, allocatedBlocks)
        metadataCount = 1
        dataCount = 0
        childSpan = OS_ROOTFS_V2_POINTERS_PER_INDIRECT_BLOCK ** (level - 1)
        for pointerIndex, childBlock in enumerate(pointers):
            childLogicalStart = logicalStart + pointerIndex * childSpan
            if childLogicalStart >= logicalBlockLimit:
                if childBlock != 0:
                    raise OsToolError(
                        "rootfs v2 inode 在逻辑文件末尾之后仍保存块指针。"
                    )
                continue
            if childBlock == 0:
                continue
            if level == 1:
                self.validateAllocatedDataBlock(
                    childBlock,
                    allocatedBlocks,
                )
                blockMap[childLogicalStart] = childBlock
                dataCount += 1
            else:
                childMetadataCount, childDataCount = (
                    self.collectIndirectBlocks(
                        childBlock,
                        level - 1,
                        childLogicalStart,
                        logicalBlockLimit,
                        blockMap,
                        allocatedBlocks,
                    )
                )
                metadataCount += childMetadataCount
                dataCount += childDataCount
        return metadataCount, dataCount

    def buildFileBlockMap(
        self,
        inode: RootfsV2Inode,
        allocatedBlocks: set[int],
    ) -> dict[int, int]:
        logicalBlockLimit = (
            inode.sizeBytes + OS_ROOTFS_V2_BLOCK_SIZE_BYTES - 1
        ) // OS_ROOTFS_V2_BLOCK_SIZE_BYTES
        blockMap: dict[int, int] = {}
        dataCount = 0
        metadataCount = 0
        for logicalBlock, relativeBlock in enumerate(inode.directBlocks):
            if logicalBlock >= logicalBlockLimit:
                if relativeBlock != 0:
                    raise OsToolError(
                        "rootfs v2 inode 在文件末尾之后保存直接块。"
                    )
                continue
            if relativeBlock == 0:
                continue
            self.validateAllocatedDataBlock(relativeBlock, allocatedBlocks)
            blockMap[logicalBlock] = relativeBlock
            dataCount += 1

        logicalStart = OS_ROOTFS_V2_DIRECT_BLOCK_COUNT
        for level, rootBlock in (
            (1, inode.singleIndirectBlock),
            (2, inode.doubleIndirectBlock),
            (3, inode.tripleIndirectBlock),
        ):
            childMetadataCount, childDataCount = self.collectIndirectBlocks(
                rootBlock,
                level,
                logicalStart,
                logicalBlockLimit,
                blockMap,
                allocatedBlocks,
            )
            metadataCount += childMetadataCount
            dataCount += childDataCount
            logicalStart += (
                OS_ROOTFS_V2_POINTERS_PER_INDIRECT_BLOCK**level
            )
        if (
            dataCount != inode.allocatedDataBlockCount
            or metadataCount != inode.allocatedMetadataBlockCount
        ):
            raise OsToolError(
                "rootfs v2 inode 的已分配块计数与指针树不一致。"
            )
        return blockMap

    def readFileBytes(
        self,
        inode: RootfsV2Inode,
        blockMap: dict[int, int],
        offsetBytes: int,
        lengthBytes: int,
    ) -> bytes:
        if (
            offsetBytes < 0
            or lengthBytes < 0
            or offsetBytes + lengthBytes > inode.sizeBytes
        ):
            raise OsToolError("rootfs v2 文件读取范围越界。")
        result = bytearray(lengthBytes)
        copiedBytes = 0
        while copiedBytes < lengthBytes:
            absoluteOffset = offsetBytes + copiedBytes
            logicalBlock = (
                absoluteOffset // OS_ROOTFS_V2_BLOCK_SIZE_BYTES
            )
            blockOffset = (
                absoluteOffset % OS_ROOTFS_V2_BLOCK_SIZE_BYTES
            )
            chunkBytes = min(
                lengthBytes - copiedBytes,
                OS_ROOTFS_V2_BLOCK_SIZE_BYTES - blockOffset,
            )
            relativeBlock = blockMap.get(logicalBlock)
            if relativeBlock is not None:
                block = readRootfsV2Block(
                    self.imageFile,
                    relativeBlock,
                )
                result[copiedBytes:copiedBytes + chunkBytes] = block[
                    blockOffset:blockOffset + chunkBytes
                ]
            copiedBytes += chunkBytes
        return bytes(result)


def readRootfsV2File(imagePath: Path, filePath: str) -> bytes:
    components = _validateRootfsV2InstallPath(filePath)
    reader = RootfsV2Reader(imagePath)
    try:
        allocatedBlocks: set[int] = set()
        inode = reader.readInode(OS_ROOTFS_V2_ROOT_INODE_NUMBER)
        for componentIndex, component in enumerate(components):
            if inode.nodeType != OS_ROOTFS_V2_NODE_TYPE_DIRECTORY:
                raise OsToolError(
                    f"rootfs v2 路径父组件不是目录：{filePath}"
                )
            blockMap = reader.buildFileBlockMap(
                inode,
                allocatedBlocks,
            )
            matchingEntry: RootfsV2DirectoryEntry | None = None
            for entryOffset in range(
                0,
                inode.sizeBytes,
                OS_ROOTFS_V2_DIRECTORY_ENTRY_SIZE_BYTES,
            ):
                entry = decodeRootfsV2DirectoryEntry(
                    reader.readFileBytes(
                        inode,
                        blockMap,
                        entryOffset,
                        OS_ROOTFS_V2_DIRECTORY_ENTRY_SIZE_BYTES,
                    )
                )
                if entry is not None and entry.name == component:
                    matchingEntry = entry
                    break
            if matchingEntry is None:
                raise OsToolError(
                    f"rootfs v2 文件不存在：{filePath}"
                )
            inode = reader.readInode(matchingEntry.inodeNumber)
            if componentIndex + 1 == len(components):
                if (
                    inode.nodeType
                    != OS_ROOTFS_V2_NODE_TYPE_REGULAR_FILE
                ):
                    raise OsToolError(
                        f"rootfs v2 路径不是普通文件：{filePath}"
                    )
                fileBlockMap = reader.buildFileBlockMap(
                    inode,
                    allocatedBlocks,
                )
                return reader.readFileBytes(
                    inode,
                    fileBlockMap,
                    0,
                    inode.sizeBytes,
                )
        raise OsToolError(f"rootfs v2 文件路径无效：{filePath}")
    finally:
        reader.close()


def inspectRootfsV2(imagePath: Path) -> RootfsV2Inspection:
    validateRootfsV2StaticLayout()
    reader = RootfsV2Reader(imagePath)
    try:
        if (
            reader.superblock.transactionState
            != OS_ROOTFS_V2_TRANSACTION_STATE_CLEAN
        ):
            raise OsToolError(
                "rootfs v2 处于未完成事务状态；只读 fsck 拒绝继续。"
            )
        if not reader.inodeAllocated(OS_ROOTFS_V2_ROOT_INODE_NUMBER):
            raise OsToolError("rootfs v2 根 inode 未分配。")

        reachableInodes: set[int] = set()
        allocatedBlocks: set[int] = set()
        pendingInodes = [OS_ROOTFS_V2_ROOT_INODE_NUMBER]
        directoryCount = 0
        regularFileCount = 0
        allocatedDataBlockCount = 0
        allocatedMetadataBlockCount = 0
        logicalFileBytes = 0

        while pendingInodes:
            inodeNumber = pendingInodes.pop()
            if inodeNumber in reachableInodes:
                raise OsToolError(
                    f"rootfs v2 inode 被多个目录项引用：{inodeNumber}。"
                )
            reachableInodes.add(inodeNumber)
            inode = reader.readInode(inodeNumber)
            blockMap = reader.buildFileBlockMap(inode, allocatedBlocks)
            allocatedDataBlockCount += inode.allocatedDataBlockCount
            allocatedMetadataBlockCount += (
                inode.allocatedMetadataBlockCount
            )
            logicalFileBytes += inode.sizeBytes

            if inodeNumber == OS_ROOTFS_V2_ROOT_INODE_NUMBER:
                if (
                    inode.nodeType != OS_ROOTFS_V2_NODE_TYPE_DIRECTORY
                    or inode.parentInodeNumber
                    != OS_ROOTFS_V2_ROOT_INODE_NUMBER
                ):
                    raise OsToolError("rootfs v2 根 inode 语义无效。")
            if inode.nodeType == OS_ROOTFS_V2_NODE_TYPE_REGULAR_FILE:
                regularFileCount += 1
                continue

            directoryCount += 1
            if (
                inode.sizeBytes
                % OS_ROOTFS_V2_DIRECTORY_ENTRY_SIZE_BYTES
                != 0
            ):
                raise OsToolError(
                    f"rootfs v2 目录 inode {inodeNumber} 长度未按目录项对齐。"
                )
            names: set[bytes] = set()
            for entryOffset in range(
                0,
                inode.sizeBytes,
                OS_ROOTFS_V2_DIRECTORY_ENTRY_SIZE_BYTES,
            ):
                encodedEntry = reader.readFileBytes(
                    inode,
                    blockMap,
                    entryOffset,
                    OS_ROOTFS_V2_DIRECTORY_ENTRY_SIZE_BYTES,
                )
                entry = decodeRootfsV2DirectoryEntry(encodedEntry)
                if entry is None:
                    continue
                if entry.name in names:
                    raise OsToolError(
                        f"rootfs v2 目录 inode {inodeNumber} 包含重名项。"
                    )
                names.add(entry.name)
                child = reader.readInode(entry.inodeNumber)
                if (
                    child.generation != entry.inodeGeneration
                    or child.nodeType != entry.nodeType
                    or child.parentInodeNumber != inodeNumber
                ):
                    raise OsToolError(
                        "rootfs v2 目录项与目标 inode 身份或父关系不一致。"
                    )
                pendingInodes.append(entry.inodeNumber)

        allocatedInodes = {
            inodeNumber
            for inodeNumber in range(1, OS_ROOTFS_V2_INODE_COUNT + 1)
            if reader.inodeAllocated(inodeNumber)
        }
        if allocatedInodes != reachableInodes:
            unreachable = sorted(allocatedInodes - reachableInodes)
            missing = sorted(reachableInodes - allocatedInodes)
            raise OsToolError(
                "rootfs v2 inode 位图与根可达集合不一致："
                f"不可达={unreachable[:8]}，未分配={missing[:8]}。"
            )
        bitmapAllocatedBlocks = {
            OS_ROOTFS_V2_DATA_START_RELATIVE_BLOCK + blockIndex
            for blockIndex in range(OS_ROOTFS_V2_DATA_BLOCK_COUNT)
            if RootfsV2Reader.bitmapBitIsSet(reader.dataBitmap, blockIndex)
        }
        if bitmapAllocatedBlocks != allocatedBlocks:
            leaked = sorted(bitmapAllocatedBlocks - allocatedBlocks)
            missing = sorted(allocatedBlocks - bitmapAllocatedBlocks)
            raise OsToolError(
                "rootfs v2 数据位图与可达指针集合不一致："
                f"泄漏={leaked[:8]}，未登记={missing[:8]}。"
            )
        return RootfsV2Inspection(
            reachableInodeCount=len(reachableInodes),
            directoryCount=directoryCount,
            regularFileCount=regularFileCount,
            allocatedDataBlockCount=allocatedDataBlockCount,
            allocatedMetadataBlockCount=allocatedMetadataBlockCount,
            logicalFileBytes=logicalFileBytes,
            transactionGeneration=reader.superblock.transactionGeneration,
        )
    finally:
        reader.close()


def inspectionAsJson(inspection: RootfsV2Inspection) -> str:
    return json.dumps(
        {
            "format": "rootfs-v2",
            "version": OS_ROOTFS_V2_VERSION,
            "block_size_bytes": OS_ROOTFS_V2_BLOCK_SIZE_BYTES,
            "rootfs_size_bytes": OS_ROOTFS_V2_REGION_SIZE_BYTES,
            "maximum_file_size_bytes": (
                OS_ROOTFS_V2_MAXIMUM_FILE_SIZE_BYTES
            ),
            "reachable_inode_count": inspection.reachableInodeCount,
            "directory_count": inspection.directoryCount,
            "regular_file_count": inspection.regularFileCount,
            "allocated_data_block_count": (
                inspection.allocatedDataBlockCount
            ),
            "allocated_metadata_block_count": (
                inspection.allocatedMetadataBlockCount
            ),
            "logical_file_bytes": inspection.logicalFileBytes,
            "transaction_generation": inspection.transactionGeneration,
        },
        ensure_ascii=False,
        indent=2,
        sort_keys=True,
    )


def corruptRootfsV2(imagePath: Path, corruptionKind: str) -> None:
    corruptionOffsets = {
        "superblock-checksum": rootfsV2AbsoluteOffset(
            OS_ROOTFS_V2_SUPERBLOCK_RELATIVE_BLOCK
        )
        + OS_ROOTFS_V2_SUPERBLOCK_CHECKSUM_OFFSET_BYTES,
        "root-inode-checksum": rootfsV2AbsoluteOffset(
            OS_ROOTFS_V2_INODE_TABLE_START_RELATIVE_BLOCK
        )
        + OS_ROOTFS_V2_INODE_CHECKSUM_OFFSET_BYTES,
        "inode-bitmap": rootfsV2AbsoluteOffset(
            OS_ROOTFS_V2_INODE_BITMAP_START_RELATIVE_BLOCK
        ),
        "data-bitmap": rootfsV2AbsoluteOffset(
            OS_ROOTFS_V2_DATA_BITMAP_START_RELATIVE_BLOCK
        ),
    }
    if corruptionKind == "transaction-dirty":
        with imagePath.open("r+b") as imageFile:
            block = bytearray(
                readRootfsV2Block(
                    imageFile,
                    OS_ROOTFS_V2_SUPERBLOCK_RELATIVE_BLOCK,
                )
            )
            struct.pack_into(
                "<Q",
                block,
                8 + 14 * 8,
                OS_ROOTFS_V2_TRANSACTION_STATE_DIRTY,
            )
            struct.pack_into(
                "<I",
                block,
                OS_ROOTFS_V2_SUPERBLOCK_CHECKSUM_OFFSET_BYTES,
                calculateRootfsV2Crc32(
                    block[:OS_ROOTFS_V2_SUPERBLOCK_CHECKSUM_OFFSET_BYTES]
                ),
            )
            writeRootfsV2Block(
                imageFile,
                OS_ROOTFS_V2_SUPERBLOCK_RELATIVE_BLOCK,
                bytes(block),
            )
        return
    if corruptionKind not in OS_ROOTFS_V2_CORRUPTION_KINDS:
        raise OsToolError(
            f"不支持的 rootfs v2 损坏类型：{corruptionKind}"
        )
    with imagePath.open("r+b") as imageFile:
        imageFile.seek(corruptionOffsets[corruptionKind])
        original = imageFile.read(1)
        if len(original) != 1:
            raise OsToolError("rootfs v2 损坏注入位置被截断。")
        imageFile.seek(corruptionOffsets[corruptionKind])
        imageFile.write(
            bytes((original[0] ^ OS_ROOTFS_V2_CORRUPTION_MASK,))
        )

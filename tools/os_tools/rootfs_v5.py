"""rootfs v5 block-group 盘面基础、格式化与只读一致性检查。"""

from dataclasses import asdict, dataclass, field, replace
import json
from pathlib import Path
import struct
import time
import uuid

from .rootfs_v2 import (
    OS_ROOTFS_V2_DIRECTORY_ENTRY_SIZE_BYTES,
    OS_ROOTFS_V2_NODE_TYPE_DIRECTORY,
    OS_ROOTFS_V2_NODE_TYPE_REGULAR_FILE,
    OS_ROOTFS_V2_NODE_TYPE_SYMBOLIC_LINK,
    OS_ROOTFS_V2_ROOT_INODE_NUMBER,
    RootfsV2Reader,
    decodeRootfsV2DirectoryEntry,
    inspectRootfsV2,
)


OS_ROOTFS_V5_BLOCK_SIZE_BYTES = 4096
OS_ROOTFS_V5_SECTOR_SIZE_BYTES = 512
OS_ROOTFS_V5_FILE_SYSTEM_START_LBA = 32768
OS_ROOTFS_V5_DEVICE_SECTOR_COUNT = 268435456
OS_ROOTFS_V5_DEVICE_SIZE_BYTES = (
    OS_ROOTFS_V5_DEVICE_SECTOR_COUNT * OS_ROOTFS_V5_SECTOR_SIZE_BYTES
)
OS_ROOTFS_V5_SECTORS_PER_BLOCK = (
    OS_ROOTFS_V5_BLOCK_SIZE_BYTES // OS_ROOTFS_V5_SECTOR_SIZE_BYTES
)
OS_ROOTFS_V5_TOTAL_BLOCK_COUNT = (
    OS_ROOTFS_V5_DEVICE_SECTOR_COUNT - OS_ROOTFS_V5_FILE_SYSTEM_START_LBA
) // OS_ROOTFS_V5_SECTORS_PER_BLOCK
OS_ROOTFS_V5_BLOCKS_PER_GROUP = 32768
OS_ROOTFS_V5_GROUP_DESCRIPTOR_SIZE_BYTES = 256
OS_ROOTFS_V5_GROUP_COUNT = (
    OS_ROOTFS_V5_TOTAL_BLOCK_COUNT + OS_ROOTFS_V5_BLOCKS_PER_GROUP - 1
) // OS_ROOTFS_V5_BLOCKS_PER_GROUP
OS_ROOTFS_V5_GROUP_DESCRIPTOR_TABLE_START_BLOCK = 1
OS_ROOTFS_V5_GROUP_DESCRIPTOR_TABLE_BLOCK_COUNT = (
    OS_ROOTFS_V5_GROUP_COUNT * OS_ROOTFS_V5_GROUP_DESCRIPTOR_SIZE_BYTES
    + OS_ROOTFS_V5_BLOCK_SIZE_BYTES
    - 1
) // OS_ROOTFS_V5_BLOCK_SIZE_BYTES
OS_ROOTFS_V5_INODE_SIZE_BYTES = 256
OS_ROOTFS_V5_INODES_PER_GROUP = 2048
OS_ROOTFS_V5_INODE_COUNT = (
    OS_ROOTFS_V5_GROUP_COUNT * OS_ROOTFS_V5_INODES_PER_GROUP
)
OS_ROOTFS_V5_ROOT_INODE_NUMBER = 2
OS_ROOTFS_V5_FIRST_USER_INODE_NUMBER = 16
OS_ROOTFS_V5_RESERVED_INODE_COUNT = OS_ROOTFS_V5_FIRST_USER_INODE_NUMBER - 1
OS_ROOTFS_V5_INODE_MAPPING_ROOT_SIZE_BYTES = 128
OS_ROOTFS_V5_INODE_FLAG_ORPHAN = 1 << 0
OS_ROOTFS_V5_FORMAT_VERSION = 5
OS_ROOTFS_V5_SUPERBLOCK_HEADER_SIZE_BYTES = 256
OS_ROOTFS_V5_SUPERBLOCK_CHECKSUM_OFFSET_BYTES = 4092
OS_ROOTFS_V5_GROUP_DESCRIPTOR_CHECKSUM_OFFSET_BYTES = 252
OS_ROOTFS_V5_INODE_CHECKSUM_OFFSET_BYTES = 252
OS_ROOTFS_V5_NO_BLOCK = (1 << 64) - 1
OS_ROOTFS_V5_CRC32C_ALGORITHM = 1
OS_ROOTFS_V5_SPARSE_BACKUP_POLICY = 1
OS_ROOTFS_V5_STATE_CLEAN = 1

OS_ROOTFS_V5_COMPAT_SPARSE_BACKUP = 1 << 0
OS_ROOTFS_V5_REQUIRED_COMPAT_FEATURES = OS_ROOTFS_V5_COMPAT_SPARSE_BACKUP
OS_ROOTFS_V5_READ_ONLY_COMPAT_METADATA_CRC32C = 1 << 0
OS_ROOTFS_V5_SUPPORTED_READ_ONLY_COMPAT_FEATURES = (
    OS_ROOTFS_V5_READ_ONLY_COMPAT_METADATA_CRC32C
)
OS_ROOTFS_V5_INCOMPAT_64_BIT_GEOMETRY = 1 << 0
OS_ROOTFS_V5_INCOMPAT_BLOCK_GROUPS = 1 << 1
OS_ROOTFS_V5_INCOMPAT_BASE_INODE = 1 << 2
OS_ROOTFS_V5_REQUIRED_INCOMPAT_FEATURES = (
    OS_ROOTFS_V5_INCOMPAT_64_BIT_GEOMETRY
    | OS_ROOTFS_V5_INCOMPAT_BLOCK_GROUPS
    | OS_ROOTFS_V5_INCOMPAT_BASE_INODE
)
OS_ROOTFS_V5_SUPPORTED_INCOMPAT_FEATURES = (
    OS_ROOTFS_V5_REQUIRED_INCOMPAT_FEATURES
)

OS_ROOTFS_V5_GROUP_FLAG_HAS_SUPERBLOCK_COPY = 1 << 0
OS_ROOTFS_V5_GROUP_FLAG_BLOCK_BITMAP_INITIALIZED = 1 << 1
OS_ROOTFS_V5_GROUP_FLAG_INODE_BITMAP_INITIALIZED = 1 << 2
OS_ROOTFS_V5_GROUP_FLAG_INODE_TABLE_ZEROED = 1 << 3
OS_ROOTFS_V5_REQUIRED_GROUP_FLAGS = (
    OS_ROOTFS_V5_GROUP_FLAG_BLOCK_BITMAP_INITIALIZED
    | OS_ROOTFS_V5_GROUP_FLAG_INODE_BITMAP_INITIALIZED
    | OS_ROOTFS_V5_GROUP_FLAG_INODE_TABLE_ZEROED
)
OS_ROOTFS_V5_SUPPORTED_GROUP_FLAGS = (
    OS_ROOTFS_V5_REQUIRED_GROUP_FLAGS
    | OS_ROOTFS_V5_GROUP_FLAG_HAS_SUPERBLOCK_COPY
)

OS_ROOTFS_V5_NODE_TYPE_UNUSED = 0
OS_ROOTFS_V5_NODE_TYPE_RESERVED = 1
OS_ROOTFS_V5_NODE_TYPE_REGULAR_FILE = 2
OS_ROOTFS_V5_NODE_TYPE_DIRECTORY = 3
OS_ROOTFS_V5_NODE_TYPE_SYMBOLIC_LINK = 4
OS_ROOTFS_V5_MODE_TYPE_MASK = 0o170000
OS_ROOTFS_V5_MODE_REGULAR = 0o100000
OS_ROOTFS_V5_MODE_DIRECTORY = 0o040000
OS_ROOTFS_V5_MODE_SYMBOLIC_LINK = 0o120000
OS_ROOTFS_V5_MODE_CHANGEABLE_MASK = 0o7777

OS_ROOTFS_V5_SUPERBLOCK_MAGIC = b"OSRFV005"
OS_ROOTFS_V5_GROUP_DESCRIPTOR_MAGIC = b"OSGDV005"
OS_ROOTFS_V5_INODE_MAGIC = b"OSINV005"
OS_ROOTFS_V5_INITIAL_FORMAT_GENERATION = 1
OS_ROOTFS_V5_INITIAL_METADATA_GENERATION = 1
OS_ROOTFS_V5_MINIMUM_BLOCKS_PER_GROUP = 256
OS_ROOTFS_V5_BITS_PER_BYTE = 8
OS_ROOTFS_V5_CRC32C_POLYNOMIAL = 0x82F63B78
OS_ROOTFS_V5_CRC32C_MASK = 0xFFFFFFFF
OS_ROOTFS_V5_CORRUPTION_KINDS = (
    "superblock-checksum",
    "required-feature",
    "descriptor-checksum",
    "block-bitmap-checksum",
    "inode-bitmap-checksum",
    "backup-superblock",
    "backup-descriptor",
    "root-inode-checksum",
    "reserved-byte",
    "group-overlap",
)

OS_ROOTFS_V5_INODE_EXTENSION_MAGIC = b"OSIEV001"
OS_ROOTFS_V5_INODE_EXTENSION_FORMAT_VERSION = 1
OS_ROOTFS_V5_INODE_EXTENSION_FLAG_EXTENTS = 1 << 0
OS_ROOTFS_V5_INODE_EXTENSION_FLAG_DIRECTORY_INDEX = 1 << 1
OS_ROOTFS_V5_EXTENT_LEAF_MAGIC = b"OSEXL001"
OS_ROOTFS_V5_EXTENT_FORMAT_VERSION = 1
OS_ROOTFS_V5_EXTENT_STATE_INITIALIZED = 1
OS_ROOTFS_V5_DIRECTORY_BLOCK_MAGIC = b"OSDRV001"
OS_ROOTFS_V5_DIRECTORY_INDEX_MAGIC = b"OSDXV001"
OS_ROOTFS_V5_DIRECTORY_FORMAT_VERSION = 1
OS_ROOTFS_V5_DIRECTORY_ENTRY_HEADER_SIZE_BYTES = 32
OS_ROOTFS_V5_DIRECTORY_ENTRIES_START_BYTES = 128
OS_ROOTFS_V5_DIRECTORY_CHECKSUM_OFFSET_BYTES = 4092
OS_ROOTFS_V5_EXTENT_ENTRIES_START_BYTES = 128
OS_ROOTFS_V5_EXTENT_ENTRY_SIZE_BYTES = 32
OS_ROOTFS_V5_EXTENT_CHECKSUM_OFFSET_BYTES = 4092
OS_ROOTFS_V5_JOURNAL_SUPERBLOCK_MAGIC = b"OSJV2SB1"
OS_ROOTFS_V5_JOURNAL_FORMAT_VERSION = 2
OS_ROOTFS_V5_JOURNAL_BLOCK_COUNT = 81
OS_ROOTFS_V5_JOURNAL_SLOT_COUNT = 4
OS_ROOTFS_V5_JOURNAL_SLOT_BLOCK_COUNT = 20
OS_ROOTFS_V5_JOURNAL_MAXIMUM_METADATA_BLOCK_COUNT = 16
OS_ROOTFS_V5_JOURNAL_MAXIMUM_ORDERED_DATA_BLOCK_COUNT = 8
OS_ROOTFS_V5_JOURNAL_MAXIMUM_REVOKE_COUNT = 32
OS_ROOTFS_V5_JOURNAL_CHECKSUM_OFFSET_BYTES = 4092
OS_ROOTFS_V5_JOURNAL_COMPAT_ORPHAN_FILE = 1 << 0
OS_ROOTFS_V5_JOURNAL_REQUIRED_INCOMPAT_FEATURES = (1 << 0) | (1 << 1) | (1 << 2)
OS_ROOTFS_V5_JOURNAL_INITIAL_SEQUENCE = 1
OS_ROOTFS_V5_JOURNAL_INITIAL_GENERATION = 1
OS_ROOTFS_V5_DIRECTORY_FNV_OFFSET_BASIS = 14695981039346656037
OS_ROOTFS_V5_DIRECTORY_FNV_PRIME = 1099511628211
OS_ROOTFS_V5_UINT64_MASK = (1 << 64) - 1

OS_ROOTFS_V5_SUPERBLOCK_OFFSETS = {
    "version": 8,
    "headerSizeBytes": 16,
    "blockSizeBytes": 24,
    "sectorSizeBytes": 32,
    "fileSystemStartLba": 40,
    "deviceSectorCount": 48,
    "totalBlockCount": 56,
    "blocksPerGroup": 64,
    "groupCount": 72,
    "groupDescriptorSizeBytes": 80,
    "groupDescriptorTableStartBlock": 88,
    "groupDescriptorTableBlockCount": 96,
    "inodeSizeBytes": 104,
    "inodesPerGroup": 112,
    "inodeCount": 120,
    "rootInodeNumber": 128,
    "firstUserInodeNumber": 136,
    "reservedInodeCount": 144,
    "state": 152,
    "compatibleFeatures": 160,
    "readOnlyCompatibleFeatures": 168,
    "incompatibleFeatures": 176,
    "checksumAlgorithm": 184,
    "backupPolicy": 192,
    "creationTimeNanoseconds": 200,
    "formatGeneration": 208,
    "freeBlockCount": 216,
    "freeInodeCount": 224,
    "allocatedDirectoryCount": 232,
    "uuidLow": 240,
    "uuidHigh": 248,
}

OS_ROOTFS_V5_GROUP_OFFSETS = {
    "groupIndex": 8,
    "firstBlock": 16,
    "blockCount": 24,
    "flags": 32,
    "superblockCopyBlock": 40,
    "groupDescriptorCopyStartBlock": 48,
    "groupDescriptorCopyBlockCount": 56,
    "blockBitmapBlock": 64,
    "inodeBitmapBlock": 72,
    "inodeTableStartBlock": 80,
    "inodeTableBlockCount": 88,
    "dataStartBlock": 96,
    "dataBlockCount": 104,
    "inodeStartNumber": 112,
    "inodeCount": 120,
    "freeBlockCount": 128,
    "freeInodeCount": 136,
    "usedDirectoryCount": 144,
    "metadataGeneration": 152,
    "blockBitmapChecksum": 160,
    "inodeBitmapChecksum": 164,
}
OS_ROOTFS_V5_GROUP_RESERVED_START_BYTES = 168

OS_ROOTFS_V5_INODE_OFFSETS = {
    "inodeNumber": 8,
    "generation": 16,
    "nodeType": 24,
    "flags": 32,
    "sizeBytes": 40,
    "allocatedBlockCount": 48,
    "linkCount": 56,
    "parentInodeNumber": 64,
    "accessTimeNanoseconds": 72,
    "modificationTimeNanoseconds": 80,
    "changeTimeNanoseconds": 88,
    "birthTimeNanoseconds": 96,
    "ownerUserIdentifier": 104,
    "ownerGroupIdentifier": 108,
    "mode": 112,
    "projectIdentifier": 116,
    "mappingRoot": 120,
}
OS_ROOTFS_V5_INODE_RESERVED_OFFSET_BYTES = 248


@dataclass(frozen=True)
class RootfsV5Uuid:
    low: int
    high: int


@dataclass(frozen=True)
class RootfsV5FormatProfile:
    sectorSizeBytes: int
    blockSizeBytes: int
    fileSystemStartLba: int
    deviceSectorCount: int
    blocksPerGroup: int
    groupDescriptorSizeBytes: int
    inodeSizeBytes: int
    inodesPerGroup: int
    creationTimeNanoseconds: int
    fileSystemUuid: RootfsV5Uuid


@dataclass(frozen=True)
class RootfsV5Superblock:
    version: int
    headerSizeBytes: int
    blockSizeBytes: int
    sectorSizeBytes: int
    fileSystemStartLba: int
    deviceSectorCount: int
    totalBlockCount: int
    blocksPerGroup: int
    groupCount: int
    groupDescriptorSizeBytes: int
    groupDescriptorTableStartBlock: int
    groupDescriptorTableBlockCount: int
    inodeSizeBytes: int
    inodesPerGroup: int
    inodeCount: int
    rootInodeNumber: int
    firstUserInodeNumber: int
    reservedInodeCount: int
    state: int
    compatibleFeatures: int
    readOnlyCompatibleFeatures: int
    incompatibleFeatures: int
    checksumAlgorithm: int
    backupPolicy: int
    creationTimeNanoseconds: int
    formatGeneration: int
    freeBlockCount: int
    freeInodeCount: int
    allocatedDirectoryCount: int
    fileSystemUuid: RootfsV5Uuid


@dataclass(frozen=True)
class RootfsV5GroupDescriptor:
    groupIndex: int
    firstBlock: int
    blockCount: int
    flags: int
    superblockCopyBlock: int
    groupDescriptorCopyStartBlock: int
    groupDescriptorCopyBlockCount: int
    blockBitmapBlock: int
    inodeBitmapBlock: int
    inodeTableStartBlock: int
    inodeTableBlockCount: int
    dataStartBlock: int
    dataBlockCount: int
    inodeStartNumber: int
    inodeCount: int
    freeBlockCount: int
    freeInodeCount: int
    usedDirectoryCount: int
    metadataGeneration: int
    blockBitmapChecksum: int
    inodeBitmapChecksum: int


@dataclass(frozen=True)
class RootfsV5Inode:
    inodeNumber: int
    generation: int
    nodeType: int
    flags: int
    sizeBytes: int
    allocatedBlockCount: int
    linkCount: int
    parentInodeNumber: int
    accessTimeNanoseconds: int
    modificationTimeNanoseconds: int
    changeTimeNanoseconds: int
    birthTimeNanoseconds: int
    ownerUserIdentifier: int
    ownerGroupIdentifier: int
    mode: int
    projectIdentifier: int
    mappingRoot: bytes


@dataclass(frozen=True)
class RootfsV5Inspection:
    version: int
    blockSizeBytes: int
    totalBlockCount: int
    blocksPerGroup: int
    groupCount: int
    inodeCount: int
    reservedInodeCount: int
    freeBlockCount: int
    freeInodeCount: int
    allocatedDirectoryCount: int
    sparseBackupGroupCount: int
    groupDescriptorTableBlockCount: int
    rootInodeNumber: int
    highestMetadataBlock: int
    logicalImageSizeBytes: int
    allocatedImageSizeBytes: int
    uuidLow: int
    uuidHigh: int
    reachableInodeCount: int = 0
    regularFileCount: int = 0
    symbolicLinkCount: int = 0
    journalStartBlock: int = 0


@dataclass(frozen=True)
class RootfsV5InstallFile:
    imagePath: str
    sourcePath: Path


@dataclass
class _RootfsV5BuildNode:
    name: bytes
    nodeType: int
    content: bytes = b""
    parent: "_RootfsV5BuildNode | None" = None
    children: dict[bytes, "_RootfsV5BuildNode"] = field(default_factory=dict)
    inodeNumber: int = 0
    generation: int = 1
    extentRootBlock: int = 0
    dataBlocks: list[int] = field(default_factory=list)
    hardlinkTarget: "_RootfsV5BuildNode | None" = None
    ownerUserIdentifier: int = 0
    ownerGroupIdentifier: int = 0
    mode: int = 0
    accessTimeNanoseconds: int = 0
    modificationTimeNanoseconds: int = 0
    changeTimeNanoseconds: int = 0
    birthTimeNanoseconds: int = 0
    linkCount: int = 1


def _buildRootfsV5Crc32cTable() -> tuple[int, ...]:
    values = []
    for tableIndex in range(256):
        crc = tableIndex
        for _ in range(OS_ROOTFS_V5_BITS_PER_BYTE):
            crc = (
                (crc >> 1) ^ OS_ROOTFS_V5_CRC32C_POLYNOMIAL
                if crc & 1
                else crc >> 1
            )
        values.append(crc & OS_ROOTFS_V5_CRC32C_MASK)
    return tuple(values)


OS_ROOTFS_V5_CRC32C_TABLE = _buildRootfsV5Crc32cTable()


def calculateRootfsV5Crc32c(content: bytes | bytearray) -> int:
    crc = OS_ROOTFS_V5_CRC32C_MASK
    for value in content:
        crc = OS_ROOTFS_V5_CRC32C_TABLE[(crc ^ value) & 0xFF] ^ (crc >> 8)
    return (crc ^ OS_ROOTFS_V5_CRC32C_MASK) & OS_ROOTFS_V5_CRC32C_MASK


def _isPowerOfTwo(value: int) -> bool:
    return value > 0 and value & (value - 1) == 0


def _isPurePower(value: int, base: int) -> bool:
    if value < base:
        return False
    while value % base == 0:
        value //= base
    return value == 1


def rootfsV5GroupHasSuperblockCopy(groupIndex: int) -> bool:
    return (
        groupIndex in (0, 1)
        or _isPurePower(groupIndex, 3)
        or _isPurePower(groupIndex, 5)
        or _isPurePower(groupIndex, 7)
    )


def _newUuid() -> RootfsV5Uuid:
    value = uuid.uuid4().int
    return RootfsV5Uuid(
        low=value & ((1 << 64) - 1),
        high=value >> 64,
    )


def makeProductionRootfsV5FormatProfile(
    creationTimeNanoseconds: int | None = None,
    fileSystemUuid: RootfsV5Uuid | None = None,
) -> RootfsV5FormatProfile:
    return RootfsV5FormatProfile(
        sectorSizeBytes=OS_ROOTFS_V5_SECTOR_SIZE_BYTES,
        blockSizeBytes=OS_ROOTFS_V5_BLOCK_SIZE_BYTES,
        fileSystemStartLba=OS_ROOTFS_V5_FILE_SYSTEM_START_LBA,
        deviceSectorCount=OS_ROOTFS_V5_DEVICE_SECTOR_COUNT,
        blocksPerGroup=OS_ROOTFS_V5_BLOCKS_PER_GROUP,
        groupDescriptorSizeBytes=(
            OS_ROOTFS_V5_GROUP_DESCRIPTOR_SIZE_BYTES
        ),
        inodeSizeBytes=OS_ROOTFS_V5_INODE_SIZE_BYTES,
        inodesPerGroup=OS_ROOTFS_V5_INODES_PER_GROUP,
        creationTimeNanoseconds=(
            time.time_ns()
            if creationTimeNanoseconds is None
            else creationTimeNanoseconds
        ),
        fileSystemUuid=(
            _newUuid() if fileSystemUuid is None else fileSystemUuid
        ),
    )


def _validateProfile(profile: RootfsV5FormatProfile) -> None:
    if profile.blockSizeBytes != OS_ROOTFS_V5_BLOCK_SIZE_BYTES:
        raise ValueError("rootfs v5 block size 必须为 4096")
    if profile.sectorSizeBytes != OS_ROOTFS_V5_SECTOR_SIZE_BYTES:
        raise ValueError("rootfs v5 sector size 必须为 512")
    if (
        profile.groupDescriptorSizeBytes
        != OS_ROOTFS_V5_GROUP_DESCRIPTOR_SIZE_BYTES
        or profile.inodeSizeBytes != OS_ROOTFS_V5_INODE_SIZE_BYTES
        or not _isPowerOfTwo(profile.blocksPerGroup)
        or profile.blocksPerGroup < OS_ROOTFS_V5_MINIMUM_BLOCKS_PER_GROUP
        or profile.blocksPerGroup
        > profile.blockSizeBytes * OS_ROOTFS_V5_BITS_PER_BYTE
        or profile.inodesPerGroup < OS_ROOTFS_V5_FIRST_USER_INODE_NUMBER
        or profile.inodesPerGroup
        > profile.blockSizeBytes * OS_ROOTFS_V5_BITS_PER_BYTE
        or profile.blockSizeBytes % profile.sectorSizeBytes != 0
        or (profile.fileSystemUuid.low == 0 and profile.fileSystemUuid.high == 0)
    ):
        raise ValueError("rootfs v5 profile 布局无效")
    sectorsPerBlock = profile.blockSizeBytes // profile.sectorSizeBytes
    if (
        profile.fileSystemStartLba % sectorsPerBlock != 0
        or profile.fileSystemStartLba >= profile.deviceSectorCount
        or (
            profile.deviceSectorCount - profile.fileSystemStartLba
        ) % sectorsPerBlock
        != 0
    ):
        raise ValueError("rootfs v5 LBA 几何无效")


def _buildGroupDescriptorGeometry(
    superblock: RootfsV5Superblock,
    groupIndex: int,
) -> RootfsV5GroupDescriptor:
    if groupIndex < 0 or groupIndex >= superblock.groupCount:
        raise ValueError("rootfs v5 group index 越界")
    firstBlock = groupIndex * superblock.blocksPerGroup
    blockCount = min(
        superblock.blocksPerGroup,
        superblock.totalBlockCount - firstBlock,
    )
    hasCopy = rootfsV5GroupHasSuperblockCopy(groupIndex)
    cursor = firstBlock
    superblockCopyBlock = OS_ROOTFS_V5_NO_BLOCK
    descriptorCopyStartBlock = OS_ROOTFS_V5_NO_BLOCK
    descriptorCopyBlockCount = 0
    if hasCopy:
        superblockCopyBlock = cursor
        cursor += 1
        descriptorCopyStartBlock = cursor
        descriptorCopyBlockCount = (
            superblock.groupDescriptorTableBlockCount
        )
        cursor += descriptorCopyBlockCount
    blockBitmapBlock = cursor
    cursor += 1
    inodeBitmapBlock = cursor
    cursor += 1
    inodeTableStartBlock = cursor
    inodeTableBlockCount = (
        superblock.inodesPerGroup * superblock.inodeSizeBytes
        + superblock.blockSizeBytes
        - 1
    ) // superblock.blockSizeBytes
    cursor += inodeTableBlockCount
    groupEndBlock = firstBlock + blockCount
    if cursor > groupEndBlock:
        raise ValueError("rootfs v5 group 元数据超过组边界")
    reservedInodes = superblock.reservedInodeCount if groupIndex == 0 else 0
    return RootfsV5GroupDescriptor(
        groupIndex=groupIndex,
        firstBlock=firstBlock,
        blockCount=blockCount,
        flags=(
            OS_ROOTFS_V5_REQUIRED_GROUP_FLAGS
            | (
                OS_ROOTFS_V5_GROUP_FLAG_HAS_SUPERBLOCK_COPY
                if hasCopy
                else 0
            )
        ),
        superblockCopyBlock=superblockCopyBlock,
        groupDescriptorCopyStartBlock=descriptorCopyStartBlock,
        groupDescriptorCopyBlockCount=descriptorCopyBlockCount,
        blockBitmapBlock=blockBitmapBlock,
        inodeBitmapBlock=inodeBitmapBlock,
        inodeTableStartBlock=inodeTableStartBlock,
        inodeTableBlockCount=inodeTableBlockCount,
        dataStartBlock=cursor,
        dataBlockCount=groupEndBlock - cursor,
        inodeStartNumber=groupIndex * superblock.inodesPerGroup + 1,
        inodeCount=superblock.inodesPerGroup,
        freeBlockCount=groupEndBlock - cursor,
        freeInodeCount=superblock.inodesPerGroup - reservedInodes,
        usedDirectoryCount=1 if groupIndex == 0 else 0,
        metadataGeneration=OS_ROOTFS_V5_INITIAL_METADATA_GENERATION,
        blockBitmapChecksum=0,
        inodeBitmapChecksum=0,
    )


def planRootfsV5Superblock(
    profile: RootfsV5FormatProfile,
) -> RootfsV5Superblock:
    _validateProfile(profile)
    sectorsPerBlock = profile.blockSizeBytes // profile.sectorSizeBytes
    totalBlockCount = (
        profile.deviceSectorCount - profile.fileSystemStartLba
    ) // sectorsPerBlock
    groupCount = (
        totalBlockCount + profile.blocksPerGroup - 1
    ) // profile.blocksPerGroup
    descriptorTableBlockCount = (
        groupCount * profile.groupDescriptorSizeBytes
        + profile.blockSizeBytes
        - 1
    ) // profile.blockSizeBytes
    if groupCount == 0 or descriptorTableBlockCount == 0:
        raise ValueError("rootfs v5 group geometry 为空")
    superblock = RootfsV5Superblock(
        version=OS_ROOTFS_V5_FORMAT_VERSION,
        headerSizeBytes=OS_ROOTFS_V5_SUPERBLOCK_HEADER_SIZE_BYTES,
        blockSizeBytes=profile.blockSizeBytes,
        sectorSizeBytes=profile.sectorSizeBytes,
        fileSystemStartLba=profile.fileSystemStartLba,
        deviceSectorCount=profile.deviceSectorCount,
        totalBlockCount=totalBlockCount,
        blocksPerGroup=profile.blocksPerGroup,
        groupCount=groupCount,
        groupDescriptorSizeBytes=profile.groupDescriptorSizeBytes,
        groupDescriptorTableStartBlock=(
            OS_ROOTFS_V5_GROUP_DESCRIPTOR_TABLE_START_BLOCK
        ),
        groupDescriptorTableBlockCount=descriptorTableBlockCount,
        inodeSizeBytes=profile.inodeSizeBytes,
        inodesPerGroup=profile.inodesPerGroup,
        inodeCount=groupCount * profile.inodesPerGroup,
        rootInodeNumber=OS_ROOTFS_V5_ROOT_INODE_NUMBER,
        firstUserInodeNumber=OS_ROOTFS_V5_FIRST_USER_INODE_NUMBER,
        reservedInodeCount=OS_ROOTFS_V5_RESERVED_INODE_COUNT,
        state=OS_ROOTFS_V5_STATE_CLEAN,
        compatibleFeatures=OS_ROOTFS_V5_REQUIRED_COMPAT_FEATURES,
        readOnlyCompatibleFeatures=(
            OS_ROOTFS_V5_SUPPORTED_READ_ONLY_COMPAT_FEATURES
        ),
        incompatibleFeatures=OS_ROOTFS_V5_REQUIRED_INCOMPAT_FEATURES,
        checksumAlgorithm=OS_ROOTFS_V5_CRC32C_ALGORITHM,
        backupPolicy=OS_ROOTFS_V5_SPARSE_BACKUP_POLICY,
        creationTimeNanoseconds=profile.creationTimeNanoseconds,
        formatGeneration=OS_ROOTFS_V5_INITIAL_FORMAT_GENERATION,
        freeBlockCount=0,
        freeInodeCount=0,
        allocatedDirectoryCount=1,
        fileSystemUuid=profile.fileSystemUuid,
    )
    descriptors = tuple(
        _buildGroupDescriptorGeometry(superblock, groupIndex)
        for groupIndex in range(groupCount)
    )
    return RootfsV5Superblock(
        **{
            **asdict(superblock),
            "fileSystemUuid": superblock.fileSystemUuid,
            "freeBlockCount": sum(
                descriptor.freeBlockCount for descriptor in descriptors
            ),
            "freeInodeCount": sum(
                descriptor.freeInodeCount for descriptor in descriptors
            ),
        }
    )


def _validateFeatures(superblock: RootfsV5Superblock) -> None:
    if (
        superblock.compatibleFeatures
        & OS_ROOTFS_V5_REQUIRED_COMPAT_FEATURES
    ) != OS_ROOTFS_V5_REQUIRED_COMPAT_FEATURES:
        raise ValueError("rootfs v5 缺少 required compatible feature")
    if superblock.readOnlyCompatibleFeatures & ~(
        OS_ROOTFS_V5_SUPPORTED_READ_ONLY_COMPAT_FEATURES
    ):
        raise ValueError("rootfs v5 含未知 read-only compatible feature")
    if (
        superblock.readOnlyCompatibleFeatures
        & OS_ROOTFS_V5_READ_ONLY_COMPAT_METADATA_CRC32C
    ) == 0:
        raise ValueError("rootfs v5 缺少 metadata CRC32C feature")
    if superblock.incompatibleFeatures & ~(
        OS_ROOTFS_V5_SUPPORTED_INCOMPAT_FEATURES
    ):
        raise ValueError("rootfs v5 含未知 incompatible feature")
    if (
        superblock.incompatibleFeatures
        & OS_ROOTFS_V5_REQUIRED_INCOMPAT_FEATURES
    ) != OS_ROOTFS_V5_REQUIRED_INCOMPAT_FEATURES:
        raise ValueError("rootfs v5 缺少 required incompatible feature")


def validateRootfsV5Superblock(superblock: RootfsV5Superblock) -> None:
    if superblock.version != OS_ROOTFS_V5_FORMAT_VERSION:
        raise ValueError("rootfs v5 version 无效")
    if superblock.headerSizeBytes != OS_ROOTFS_V5_SUPERBLOCK_HEADER_SIZE_BYTES:
        raise ValueError("rootfs v5 header size 无效")
    _validateFeatures(superblock)
    if superblock.checksumAlgorithm != OS_ROOTFS_V5_CRC32C_ALGORITHM:
        raise ValueError("rootfs v5 checksum algorithm 无效")
    if (
        superblock.state != OS_ROOTFS_V5_STATE_CLEAN
        or superblock.backupPolicy != OS_ROOTFS_V5_SPARSE_BACKUP_POLICY
    ):
        raise ValueError("rootfs v5 state/backup policy 无效")
    profile = RootfsV5FormatProfile(
        sectorSizeBytes=superblock.sectorSizeBytes,
        blockSizeBytes=superblock.blockSizeBytes,
        fileSystemStartLba=superblock.fileSystemStartLba,
        deviceSectorCount=superblock.deviceSectorCount,
        blocksPerGroup=superblock.blocksPerGroup,
        groupDescriptorSizeBytes=superblock.groupDescriptorSizeBytes,
        inodeSizeBytes=superblock.inodeSizeBytes,
        inodesPerGroup=superblock.inodesPerGroup,
        creationTimeNanoseconds=superblock.creationTimeNanoseconds,
        fileSystemUuid=superblock.fileSystemUuid,
    )
    expected = planRootfsV5Superblock(profile)
    comparableFields = (
        "totalBlockCount",
        "groupCount",
        "groupDescriptorTableStartBlock",
        "groupDescriptorTableBlockCount",
        "inodeCount",
        "rootInodeNumber",
        "firstUserInodeNumber",
        "reservedInodeCount",
    )
    if any(
        getattr(superblock, field) != getattr(expected, field)
        for field in comparableFields
    ):
        raise ValueError("rootfs v5 superblock geometry/count 不一致")
    if (
        superblock.formatGeneration <= 0
        or superblock.freeBlockCount < 0
        or superblock.freeBlockCount > expected.freeBlockCount
        or superblock.freeInodeCount < 0
        or superblock.freeInodeCount > expected.freeInodeCount
        or superblock.allocatedDirectoryCount <= 0
        or superblock.allocatedDirectoryCount
        > superblock.inodeCount - superblock.freeInodeCount
    ):
        raise ValueError("rootfs v5 superblock 动态计数无效")


def buildInitialRootfsV5GroupDescriptor(
    superblock: RootfsV5Superblock,
    groupIndex: int,
) -> RootfsV5GroupDescriptor:
    validateRootfsV5Superblock(superblock)
    return _buildGroupDescriptorGeometry(superblock, groupIndex)


def validateRootfsV5GroupDescriptor(
    superblock: RootfsV5Superblock,
    descriptor: RootfsV5GroupDescriptor,
) -> None:
    validateRootfsV5Superblock(superblock)
    if (
        descriptor.flags & ~OS_ROOTFS_V5_SUPPORTED_GROUP_FLAGS
        or descriptor.flags & OS_ROOTFS_V5_REQUIRED_GROUP_FLAGS
        != OS_ROOTFS_V5_REQUIRED_GROUP_FLAGS
    ):
        raise ValueError("rootfs v5 group flags 无效")
    expected = _buildGroupDescriptorGeometry(
        superblock,
        descriptor.groupIndex,
    )
    fields = (
        "groupIndex",
        "firstBlock",
        "blockCount",
        "flags",
        "superblockCopyBlock",
        "groupDescriptorCopyStartBlock",
        "groupDescriptorCopyBlockCount",
        "blockBitmapBlock",
        "inodeBitmapBlock",
        "inodeTableStartBlock",
        "inodeTableBlockCount",
        "dataStartBlock",
        "dataBlockCount",
        "inodeStartNumber",
        "inodeCount",
    )
    if any(
        getattr(descriptor, field) != getattr(expected, field)
        for field in fields
    ):
        raise ValueError("rootfs v5 group descriptor 几何无效")
    if (
        descriptor.freeBlockCount < 0
        or descriptor.freeBlockCount > descriptor.dataBlockCount
        or descriptor.freeInodeCount < 0
        or descriptor.freeInodeCount > descriptor.inodeCount
        or descriptor.usedDirectoryCount < 0
        or descriptor.usedDirectoryCount
        > descriptor.inodeCount - descriptor.freeInodeCount
        or descriptor.metadataGeneration <= 0
    ):
        raise ValueError("rootfs v5 group descriptor 计数无效")


def _expectedModeType(nodeType: int) -> int:
    return {
        OS_ROOTFS_V5_NODE_TYPE_REGULAR_FILE: OS_ROOTFS_V5_MODE_REGULAR,
        OS_ROOTFS_V5_NODE_TYPE_DIRECTORY: OS_ROOTFS_V5_MODE_DIRECTORY,
        OS_ROOTFS_V5_NODE_TYPE_SYMBOLIC_LINK: OS_ROOTFS_V5_MODE_SYMBOLIC_LINK,
    }.get(nodeType, 0)


def validateRootfsV5Inode(
    superblock: RootfsV5Superblock,
    inode: RootfsV5Inode,
) -> None:
    validateRootfsV5Superblock(superblock)
    if inode.nodeType == OS_ROOTFS_V5_NODE_TYPE_UNUSED:
        if any(
            (
                inode.inodeNumber,
                inode.generation,
                inode.flags,
                inode.sizeBytes,
                inode.allocatedBlockCount,
                inode.linkCount,
                inode.parentInodeNumber,
                inode.accessTimeNanoseconds,
                inode.modificationTimeNanoseconds,
                inode.changeTimeNanoseconds,
                inode.birthTimeNanoseconds,
                inode.ownerUserIdentifier,
                inode.ownerGroupIdentifier,
                inode.mode,
                inode.projectIdentifier,
            )
        ) or any(inode.mappingRoot):
            raise ValueError("rootfs v5 unused inode 含非零字段")
        return
    if (
        inode.inodeNumber <= 0
        or inode.inodeNumber > superblock.inodeCount
        or inode.generation <= 0
        or inode.flags & ~OS_ROOTFS_V5_INODE_FLAG_ORPHAN
    ):
        raise ValueError("rootfs v5 inode 基础字段无效")
    rootInode = inode.inodeNumber == superblock.rootInodeNumber
    reservedInode = inode.inodeNumber < superblock.firstUserInodeNumber
    if reservedInode and not rootInode:
        if (
            inode.nodeType != OS_ROOTFS_V5_NODE_TYPE_RESERVED
            or inode.linkCount != 0
            or inode.parentInodeNumber != 0
            or inode.ownerUserIdentifier != 0
            or inode.ownerGroupIdentifier != 0
            or inode.mode != 0
        ):
            raise ValueError("rootfs v5 reserved inode 无效")
        return
    if inode.allocatedBlockCount == 0:
        if inode.sizeBytes != 0 or any(inode.mappingRoot):
            raise ValueError("rootfs v5 空 inode 映射无效")
    elif not any(inode.mappingRoot):
        raise ValueError("rootfs v5 已分配 inode 缺少 mapping root")
    expectedMode = _expectedModeType(inode.nodeType)
    if (
        expectedMode == 0
        or inode.linkCount <= 0
        or inode.mode & OS_ROOTFS_V5_MODE_TYPE_MASK != expectedMode
        or inode.mode
        & ~(OS_ROOTFS_V5_MODE_TYPE_MASK | OS_ROOTFS_V5_MODE_CHANGEABLE_MASK)
        or inode.parentInodeNumber <= 0
        or inode.parentInodeNumber > superblock.inodeCount
        or (
            rootInode
            and (
                inode.nodeType != OS_ROOTFS_V5_NODE_TYPE_DIRECTORY
                or inode.parentInodeNumber != superblock.rootInodeNumber
            )
        )
    ):
        raise ValueError("rootfs v5 inode type/mode/parent 无效")


def _packU64(buffer: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<Q", buffer, offset, value)


def _packU32(buffer: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<I", buffer, offset, value)


def _packU16(buffer: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<H", buffer, offset, value)


def _unpackU64(content: bytes, offset: int) -> int:
    return struct.unpack_from("<Q", content, offset)[0]


def _unpackU32(content: bytes, offset: int) -> int:
    return struct.unpack_from("<I", content, offset)[0]


def _unpackU16(content: bytes, offset: int) -> int:
    return struct.unpack_from("<H", content, offset)[0]


def _setBitmapBit(bitmap: bytearray, bitIndex: int, allocated: bool) -> None:
    mask = 1 << (bitIndex % OS_ROOTFS_V5_BITS_PER_BYTE)
    byteIndex = bitIndex // OS_ROOTFS_V5_BITS_PER_BYTE
    if allocated:
        bitmap[byteIndex] |= mask
    else:
        bitmap[byteIndex] &= ~mask


def _bitmapBitIsSet(bitmap: bytes | bytearray, bitIndex: int) -> bool:
    return bool(
        bitmap[bitIndex // OS_ROOTFS_V5_BITS_PER_BYTE]
        & (1 << (bitIndex % OS_ROOTFS_V5_BITS_PER_BYTE))
    )


def _encodeRootfsV5InodeExtension(extentRootBlock: int) -> bytes:
    if extentRootBlock <= 0:
        raise ValueError("rootfs v5 extent root block 无效")
    extension = bytearray(OS_ROOTFS_V5_INODE_MAPPING_ROOT_SIZE_BYTES)
    extension[:8] = OS_ROOTFS_V5_INODE_EXTENSION_MAGIC
    _packU64(extension, 8, OS_ROOTFS_V5_INODE_EXTENSION_FORMAT_VERSION)
    _packU64(extension, 16, OS_ROOTFS_V5_INODE_MAPPING_ROOT_SIZE_BYTES)
    _packU64(extension, 24, OS_ROOTFS_V5_INODE_EXTENSION_FLAG_EXTENTS)
    _packU64(extension, 32, extentRootBlock)
    return bytes(extension)


def _decodeRootfsV5InodeExtension(content: bytes) -> int:
    if len(content) != OS_ROOTFS_V5_INODE_MAPPING_ROOT_SIZE_BYTES:
        raise ValueError("rootfs v5 inode extension 大小无效")
    if content[:8] != OS_ROOTFS_V5_INODE_EXTENSION_MAGIC:
        raise ValueError("rootfs v5 inode extension magic 无效")
    if (
        _unpackU64(content, 8) != OS_ROOTFS_V5_INODE_EXTENSION_FORMAT_VERSION
        or _unpackU64(content, 16) != OS_ROOTFS_V5_INODE_MAPPING_ROOT_SIZE_BYTES
        or _unpackU64(content, 24) != OS_ROOTFS_V5_INODE_EXTENSION_FLAG_EXTENTS
        or any(content[40:])
    ):
        raise ValueError("rootfs v5 inode extension 字段无效")
    extentRootBlock = _unpackU64(content, 32)
    if extentRootBlock <= 0:
        raise ValueError("rootfs v5 inode extension extent root 无效")
    return extentRootBlock


def _encodeRootfsV5ExtentLeaf(
    superblock: RootfsV5Superblock,
    inodeNumber: int,
    inodeGeneration: int,
    physicalBlocks: list[int],
) -> bytes:
    runs: list[tuple[int, int, int]] = []
    for logicalBlock, physicalBlock in enumerate(physicalBlocks):
        if runs and runs[-1][0] + runs[-1][2] == logicalBlock and (
            runs[-1][1] + runs[-1][2] == physicalBlock
        ):
            logicalStart, physicalStart, blockCount = runs[-1]
            runs[-1] = (logicalStart, physicalStart, blockCount + 1)
        else:
            runs.append((logicalBlock, physicalBlock, 1))
    if len(runs) > 123:
        raise ValueError("rootfs v5 单叶 extent 容量不足")
    block = bytearray(superblock.blockSizeBytes)
    block[:8] = OS_ROOTFS_V5_EXTENT_LEAF_MAGIC
    _packU64(block, 8, OS_ROOTFS_V5_EXTENT_FORMAT_VERSION)
    _packU64(block, 16, 128)
    _packU64(block, 24, 1)
    _packU64(block, 32, inodeNumber)
    _packU64(block, 40, inodeGeneration)
    _packU64(block, 48, 0)
    _packU64(block, 56, len(runs))
    _packU64(block, 64, superblock.fileSystemUuid.low)
    _packU64(block, 72, superblock.fileSystemUuid.high)
    for extentIndex, (logicalStart, physicalStart, blockCount) in enumerate(runs):
        offset = OS_ROOTFS_V5_EXTENT_ENTRIES_START_BYTES + (
            extentIndex * OS_ROOTFS_V5_EXTENT_ENTRY_SIZE_BYTES
        )
        _packU64(block, offset, logicalStart)
        _packU64(block, offset + 8, physicalStart)
        _packU64(block, offset + 16, blockCount)
        _packU64(block, offset + 24, OS_ROOTFS_V5_EXTENT_STATE_INITIALIZED)
    _packU32(
        block,
        OS_ROOTFS_V5_EXTENT_CHECKSUM_OFFSET_BYTES,
        calculateRootfsV5Crc32c(block[:OS_ROOTFS_V5_EXTENT_CHECKSUM_OFFSET_BYTES]),
    )
    return bytes(block)


def _decodeRootfsV5ExtentLeaf(
    superblock: RootfsV5Superblock,
    content: bytes,
    inodeNumber: int,
    inodeGeneration: int,
) -> list[int]:
    if (
        len(content) != superblock.blockSizeBytes
        or content[:8] != OS_ROOTFS_V5_EXTENT_LEAF_MAGIC
        or _unpackU64(content, 8) != OS_ROOTFS_V5_EXTENT_FORMAT_VERSION
        or _unpackU64(content, 16) != 128
        or _unpackU64(content, 32) != inodeNumber
        or _unpackU64(content, 40) != inodeGeneration
        or _unpackU64(content, 48) != 0
        or _unpackU64(content, 64) != superblock.fileSystemUuid.low
        or _unpackU64(content, 72) != superblock.fileSystemUuid.high
        or _unpackU32(content, OS_ROOTFS_V5_EXTENT_CHECKSUM_OFFSET_BYTES)
        != calculateRootfsV5Crc32c(content[:OS_ROOTFS_V5_EXTENT_CHECKSUM_OFFSET_BYTES])
    ):
        raise ValueError("rootfs v5 extent leaf 无效")
    entryCount = _unpackU64(content, 56)
    if entryCount > 123 or any(content[80:128]) or any(content[4064:4092]):
        raise ValueError("rootfs v5 extent leaf 保留区或数量无效")
    blocks: list[int] = []
    expectedLogical = 0
    for extentIndex in range(entryCount):
        offset = 128 + extentIndex * 32
        logicalStart = _unpackU64(content, offset)
        physicalStart = _unpackU64(content, offset + 8)
        blockCount = _unpackU64(content, offset + 16)
        state = _unpackU64(content, offset + 24)
        if logicalStart != expectedLogical or blockCount <= 0 or state != 1:
            raise ValueError("rootfs v5 extent leaf entry 无效")
        if physicalStart + blockCount > superblock.totalBlockCount:
            raise ValueError("rootfs v5 extent leaf 越界")
        blocks.extend(range(physicalStart, physicalStart + blockCount))
        expectedLogical += blockCount
    usedEnd = 128 + entryCount * 32
    if any(content[usedEnd:4064]):
        raise ValueError("rootfs v5 extent leaf 未用项非零")
    return blocks


def _calculateRootfsV5DirectoryNameHash(
    fileSystemUuid: RootfsV5Uuid,
    name: bytes,
) -> int:
    hashValue = OS_ROOTFS_V5_DIRECTORY_FNV_OFFSET_BASIS ^ fileSystemUuid.low
    hashValue ^= fileSystemUuid.high
    hashValue = (hashValue * OS_ROOTFS_V5_DIRECTORY_FNV_PRIME) & OS_ROOTFS_V5_UINT64_MASK
    for value in name:
        hashValue ^= value
        hashValue = (hashValue * OS_ROOTFS_V5_DIRECTORY_FNV_PRIME) & OS_ROOTFS_V5_UINT64_MASK
    return hashValue or 1


def _encodeRootfsV5DirectoryBlock(
    superblock: RootfsV5Superblock,
    node: _RootfsV5BuildNode,
) -> bytes:
    entries = sorted(
        node.children.values(),
        key=lambda child: (
            _calculateRootfsV5DirectoryNameHash(superblock.fileSystemUuid, child.name),
            child.name,
        ),
    )
    block = bytearray(superblock.blockSizeBytes)
    block[:8] = OS_ROOTFS_V5_DIRECTORY_BLOCK_MAGIC
    _packU64(block, 8, OS_ROOTFS_V5_DIRECTORY_FORMAT_VERSION)
    _packU64(block, 16, 128)
    _packU64(block, 24, node.inodeNumber)
    _packU64(block, 32, node.generation)
    _packU64(block, 40, 1)
    _packU64(block, 48, len(entries))
    _packU64(block, 64, superblock.fileSystemUuid.low)
    _packU64(block, 72, superblock.fileSystemUuid.high)
    cursor = OS_ROOTFS_V5_DIRECTORY_ENTRIES_START_BYTES
    for child in entries:
        recordLength = (
            OS_ROOTFS_V5_DIRECTORY_ENTRY_HEADER_SIZE_BYTES + len(child.name) + 7
        ) // 8 * 8
        if cursor + recordLength > OS_ROOTFS_V5_DIRECTORY_CHECKSUM_OFFSET_BYTES:
            raise ValueError("rootfs v5 单目录块容量不足")
        _packU64(block, cursor, child.inodeNumber)
        _packU64(block, cursor + 8, child.generation)
        _packU64(
            block,
            cursor + 16,
            _calculateRootfsV5DirectoryNameHash(superblock.fileSystemUuid, child.name),
        )
        _packU32(block, cursor + 24, recordLength)
        _packU16(block, cursor + 28, len(child.name))
        _packU16(block, cursor + 30, child.nodeType)
        block[cursor + 32:cursor + 32 + len(child.name)] = child.name
        cursor += recordLength
    _packU64(block, 56, cursor)
    _packU32(
        block,
        OS_ROOTFS_V5_DIRECTORY_CHECKSUM_OFFSET_BYTES,
        calculateRootfsV5Crc32c(block[:OS_ROOTFS_V5_DIRECTORY_CHECKSUM_OFFSET_BYTES]),
    )
    return bytes(block)


def _decodeRootfsV5DirectoryBlock(
    superblock: RootfsV5Superblock,
    content: bytes,
    inode: RootfsV5Inode,
) -> tuple[tuple[int, int, int, bytes], ...]:
    if (
        len(content) != superblock.blockSizeBytes
        or content[:8] != OS_ROOTFS_V5_DIRECTORY_BLOCK_MAGIC
        or _unpackU64(content, 8) != OS_ROOTFS_V5_DIRECTORY_FORMAT_VERSION
        or _unpackU64(content, 16) != 128
        or _unpackU64(content, 24) != inode.inodeNumber
        or _unpackU64(content, 32) != inode.generation
        or _unpackU64(content, 64) != superblock.fileSystemUuid.low
        or _unpackU64(content, 72) != superblock.fileSystemUuid.high
        or _unpackU32(content, OS_ROOTFS_V5_DIRECTORY_CHECKSUM_OFFSET_BYTES)
        != calculateRootfsV5Crc32c(content[:OS_ROOTFS_V5_DIRECTORY_CHECKSUM_OFFSET_BYTES])
    ):
        raise ValueError("rootfs v5 directory block 无效")
    entryCount = _unpackU64(content, 48)
    usedSize = _unpackU64(content, 56)
    if usedSize < 128 or usedSize > 4092 or any(content[80:128]) or any(content[usedSize:4092]):
        raise ValueError("rootfs v5 directory block 边界无效")
    cursor = 128
    entries = []
    previousKey: tuple[int, bytes] | None = None
    while cursor < usedSize:
        recordLength = _unpackU32(content, cursor + 24)
        nameLength = _unpackU16(content, cursor + 28)
        nodeType = _unpackU16(content, cursor + 30)
        if recordLength < 32 or recordLength % 8 or cursor + recordLength > usedSize:
            raise ValueError("rootfs v5 directory record 长度无效")
        name = content[cursor + 32:cursor + 32 + nameLength]
        nameHash = _unpackU64(content, cursor + 16)
        key = (nameHash, name)
        if (
            not name
            or b"/" in name
            or b"\0" in name
            or 32 + nameLength > recordLength
            or nameHash != _calculateRootfsV5DirectoryNameHash(superblock.fileSystemUuid, name)
            or (previousKey is not None and key <= previousKey)
            or any(content[cursor + 32 + nameLength:cursor + recordLength])
        ):
            raise ValueError("rootfs v5 directory record 无效")
        entries.append(
            (_unpackU64(content, cursor), _unpackU64(content, cursor + 8), nodeType, name)
        )
        previousKey = key
        cursor += recordLength
    if cursor != usedSize or len(entries) != entryCount:
        raise ValueError("rootfs v5 directory entry count 无效")
    return tuple(entries)


def _encodeRootfsV5JournalSuperblock(
    superblock: RootfsV5Superblock,
    journalStartBlock: int,
) -> bytes:
    block = bytearray(superblock.blockSizeBytes)
    block[:8] = OS_ROOTFS_V5_JOURNAL_SUPERBLOCK_MAGIC
    values = (
        (8, OS_ROOTFS_V5_JOURNAL_FORMAT_VERSION),
        (16, 192),
        (24, superblock.blockSizeBytes),
        (32, superblock.sectorSizeBytes),
        (40, superblock.totalBlockCount),
        (48, superblock.inodeCount),
        (56, journalStartBlock),
        (64, OS_ROOTFS_V5_JOURNAL_BLOCK_COUNT),
        (72, OS_ROOTFS_V5_JOURNAL_SLOT_COUNT),
        (80, OS_ROOTFS_V5_JOURNAL_SLOT_BLOCK_COUNT),
        (88, OS_ROOTFS_V5_JOURNAL_MAXIMUM_METADATA_BLOCK_COUNT),
        (96, OS_ROOTFS_V5_JOURNAL_MAXIMUM_ORDERED_DATA_BLOCK_COUNT),
        (104, OS_ROOTFS_V5_JOURNAL_MAXIMUM_REVOKE_COUNT),
        (112, OS_ROOTFS_V5_CRC32C_ALGORITHM),
        (120, OS_ROOTFS_V5_JOURNAL_COMPAT_ORPHAN_FILE),
        (128, OS_ROOTFS_V5_JOURNAL_REQUIRED_INCOMPAT_FEATURES),
        (136, OS_ROOTFS_V5_JOURNAL_INITIAL_SEQUENCE),
        (144, 0),
        (152, superblock.creationTimeNanoseconds),
        (160, OS_ROOTFS_V5_JOURNAL_INITIAL_GENERATION),
        (168, superblock.fileSystemUuid.low),
        (176, superblock.fileSystemUuid.high),
    )
    for offset, value in values:
        _packU64(block, offset, value)
    _packU32(
        block,
        OS_ROOTFS_V5_JOURNAL_CHECKSUM_OFFSET_BYTES,
        calculateRootfsV5Crc32c(block[:OS_ROOTFS_V5_JOURNAL_CHECKSUM_OFFSET_BYTES]),
    )
    return bytes(block)


def _validateRootfsV5JournalSuperblock(
    superblock: RootfsV5Superblock,
    content: bytes,
    journalStartBlock: int,
) -> None:
    expectedValues = (
        (8, OS_ROOTFS_V5_JOURNAL_FORMAT_VERSION),
        (16, 192),
        (24, superblock.blockSizeBytes),
        (32, superblock.sectorSizeBytes),
        (40, superblock.totalBlockCount),
        (48, superblock.inodeCount),
        (56, journalStartBlock),
        (64, OS_ROOTFS_V5_JOURNAL_BLOCK_COUNT),
        (72, OS_ROOTFS_V5_JOURNAL_SLOT_COUNT),
        (80, OS_ROOTFS_V5_JOURNAL_SLOT_BLOCK_COUNT),
        (88, OS_ROOTFS_V5_JOURNAL_MAXIMUM_METADATA_BLOCK_COUNT),
        (96, OS_ROOTFS_V5_JOURNAL_MAXIMUM_ORDERED_DATA_BLOCK_COUNT),
        (104, OS_ROOTFS_V5_JOURNAL_MAXIMUM_REVOKE_COUNT),
        (112, OS_ROOTFS_V5_CRC32C_ALGORITHM),
        (120, OS_ROOTFS_V5_JOURNAL_COMPAT_ORPHAN_FILE),
        (128, OS_ROOTFS_V5_JOURNAL_REQUIRED_INCOMPAT_FEATURES),
        (168, superblock.fileSystemUuid.low),
        (176, superblock.fileSystemUuid.high),
    )
    if (
        len(content) != superblock.blockSizeBytes
        or content[:8] != OS_ROOTFS_V5_JOURNAL_SUPERBLOCK_MAGIC
        or any(_unpackU64(content, offset) != value for offset, value in expectedValues)
        or _unpackU64(content, 136) <= 0
        or _unpackU64(content, 160) <= 0
        or any(content[184:OS_ROOTFS_V5_JOURNAL_CHECKSUM_OFFSET_BYTES])
        or _unpackU32(content, OS_ROOTFS_V5_JOURNAL_CHECKSUM_OFFSET_BYTES)
        != calculateRootfsV5Crc32c(
            content[:OS_ROOTFS_V5_JOURNAL_CHECKSUM_OFFSET_BYTES]
        )
    ):
        raise ValueError("rootfs v5 journal superblock 无效")


def encodeRootfsV5Superblock(superblock: RootfsV5Superblock) -> bytes:
    validateRootfsV5Superblock(superblock)
    block = bytearray(OS_ROOTFS_V5_BLOCK_SIZE_BYTES)
    block[:len(OS_ROOTFS_V5_SUPERBLOCK_MAGIC)] = OS_ROOTFS_V5_SUPERBLOCK_MAGIC
    values = {
        **asdict(superblock),
        "uuidLow": superblock.fileSystemUuid.low,
        "uuidHigh": superblock.fileSystemUuid.high,
    }
    values.pop("fileSystemUuid")
    for field, offset in OS_ROOTFS_V5_SUPERBLOCK_OFFSETS.items():
        _packU64(block, offset, values[field])
    _packU32(
        block,
        OS_ROOTFS_V5_SUPERBLOCK_CHECKSUM_OFFSET_BYTES,
        calculateRootfsV5Crc32c(
            block[:OS_ROOTFS_V5_SUPERBLOCK_CHECKSUM_OFFSET_BYTES]
        ),
    )
    return bytes(block)


def decodeRootfsV5Superblock(block: bytes) -> RootfsV5Superblock:
    if len(block) != OS_ROOTFS_V5_BLOCK_SIZE_BYTES:
        raise ValueError("rootfs v5 superblock 大小无效")
    if block[:len(OS_ROOTFS_V5_SUPERBLOCK_MAGIC)] != OS_ROOTFS_V5_SUPERBLOCK_MAGIC:
        raise ValueError("rootfs v5 magic 无效")
    if _unpackU32(block, OS_ROOTFS_V5_SUPERBLOCK_CHECKSUM_OFFSET_BYTES) != (
        calculateRootfsV5Crc32c(
            block[:OS_ROOTFS_V5_SUPERBLOCK_CHECKSUM_OFFSET_BYTES]
        )
    ):
        raise ValueError("rootfs v5 superblock checksum 无效")
    if any(
        block[
            OS_ROOTFS_V5_SUPERBLOCK_HEADER_SIZE_BYTES:
            OS_ROOTFS_V5_SUPERBLOCK_CHECKSUM_OFFSET_BYTES
        ]
    ):
        raise ValueError("rootfs v5 superblock reserved 非零")
    values = {
        field: _unpackU64(block, offset)
        for field, offset in OS_ROOTFS_V5_SUPERBLOCK_OFFSETS.items()
    }
    superblock = RootfsV5Superblock(
        **{
            **{
                field: value
                for field, value in values.items()
                if field not in ("uuidLow", "uuidHigh")
            },
            "fileSystemUuid": RootfsV5Uuid(
                low=values["uuidLow"],
                high=values["uuidHigh"],
            ),
        }
    )
    validateRootfsV5Superblock(superblock)
    return superblock


def encodeRootfsV5GroupDescriptor(
    superblock: RootfsV5Superblock,
    descriptor: RootfsV5GroupDescriptor,
) -> bytes:
    validateRootfsV5GroupDescriptor(superblock, descriptor)
    encoded = bytearray(OS_ROOTFS_V5_GROUP_DESCRIPTOR_SIZE_BYTES)
    encoded[:len(OS_ROOTFS_V5_GROUP_DESCRIPTOR_MAGIC)] = (
        OS_ROOTFS_V5_GROUP_DESCRIPTOR_MAGIC
    )
    values = asdict(descriptor)
    for field, offset in OS_ROOTFS_V5_GROUP_OFFSETS.items():
        if field in ("blockBitmapChecksum", "inodeBitmapChecksum"):
            _packU32(encoded, offset, values[field])
        else:
            _packU64(encoded, offset, values[field])
    _packU32(
        encoded,
        OS_ROOTFS_V5_GROUP_DESCRIPTOR_CHECKSUM_OFFSET_BYTES,
        calculateRootfsV5Crc32c(
            encoded[:OS_ROOTFS_V5_GROUP_DESCRIPTOR_CHECKSUM_OFFSET_BYTES]
        ),
    )
    return bytes(encoded)


def decodeRootfsV5GroupDescriptor(
    superblock: RootfsV5Superblock,
    encoded: bytes,
) -> RootfsV5GroupDescriptor:
    if len(encoded) != OS_ROOTFS_V5_GROUP_DESCRIPTOR_SIZE_BYTES:
        raise ValueError("rootfs v5 descriptor 大小无效")
    if encoded[:len(OS_ROOTFS_V5_GROUP_DESCRIPTOR_MAGIC)] != (
        OS_ROOTFS_V5_GROUP_DESCRIPTOR_MAGIC
    ):
        raise ValueError("rootfs v5 descriptor magic 无效")
    if _unpackU32(
        encoded,
        OS_ROOTFS_V5_GROUP_DESCRIPTOR_CHECKSUM_OFFSET_BYTES,
    ) != calculateRootfsV5Crc32c(
        encoded[:OS_ROOTFS_V5_GROUP_DESCRIPTOR_CHECKSUM_OFFSET_BYTES]
    ):
        raise ValueError("rootfs v5 descriptor checksum 无效")
    if any(
        encoded[
            OS_ROOTFS_V5_GROUP_RESERVED_START_BYTES:
            OS_ROOTFS_V5_GROUP_DESCRIPTOR_CHECKSUM_OFFSET_BYTES
        ]
    ):
        raise ValueError("rootfs v5 descriptor reserved 非零")
    values = {}
    for field, offset in OS_ROOTFS_V5_GROUP_OFFSETS.items():
        values[field] = (
            _unpackU32(encoded, offset)
            if field in ("blockBitmapChecksum", "inodeBitmapChecksum")
            else _unpackU64(encoded, offset)
        )
    descriptor = RootfsV5GroupDescriptor(**values)
    validateRootfsV5GroupDescriptor(superblock, descriptor)
    return descriptor


def encodeRootfsV5Inode(
    superblock: RootfsV5Superblock,
    inode: RootfsV5Inode,
) -> bytes:
    validateRootfsV5Inode(superblock, inode)
    encoded = bytearray(OS_ROOTFS_V5_INODE_SIZE_BYTES)
    if inode.nodeType == OS_ROOTFS_V5_NODE_TYPE_UNUSED:
        return bytes(encoded)
    encoded[:len(OS_ROOTFS_V5_INODE_MAGIC)] = OS_ROOTFS_V5_INODE_MAGIC
    values = asdict(inode)
    for field, offset in OS_ROOTFS_V5_INODE_OFFSETS.items():
        if field == "mappingRoot":
            if len(inode.mappingRoot) != OS_ROOTFS_V5_INODE_MAPPING_ROOT_SIZE_BYTES:
                raise ValueError("rootfs v5 inode mapping root 大小无效")
            encoded[offset:offset + len(inode.mappingRoot)] = inode.mappingRoot
        elif field in (
            "ownerUserIdentifier",
            "ownerGroupIdentifier",
            "mode",
            "projectIdentifier",
        ):
            _packU32(encoded, offset, values[field])
        else:
            _packU64(encoded, offset, values[field])
    _packU32(
        encoded,
        OS_ROOTFS_V5_INODE_CHECKSUM_OFFSET_BYTES,
        calculateRootfsV5Crc32c(
            encoded[:OS_ROOTFS_V5_INODE_CHECKSUM_OFFSET_BYTES]
        ),
    )
    return bytes(encoded)


def decodeRootfsV5Inode(
    superblock: RootfsV5Superblock,
    encoded: bytes,
) -> RootfsV5Inode:
    if len(encoded) != OS_ROOTFS_V5_INODE_SIZE_BYTES:
        raise ValueError("rootfs v5 inode 大小无效")
    if not any(encoded):
        return RootfsV5Inode(
            inodeNumber=0,
            generation=0,
            nodeType=OS_ROOTFS_V5_NODE_TYPE_UNUSED,
            flags=0,
            sizeBytes=0,
            allocatedBlockCount=0,
            linkCount=0,
            parentInodeNumber=0,
            accessTimeNanoseconds=0,
            modificationTimeNanoseconds=0,
            changeTimeNanoseconds=0,
            birthTimeNanoseconds=0,
            ownerUserIdentifier=0,
            ownerGroupIdentifier=0,
            mode=0,
            projectIdentifier=0,
            mappingRoot=bytes(OS_ROOTFS_V5_INODE_MAPPING_ROOT_SIZE_BYTES),
        )
    if encoded[:len(OS_ROOTFS_V5_INODE_MAGIC)] != OS_ROOTFS_V5_INODE_MAGIC:
        raise ValueError("rootfs v5 inode magic 无效")
    if _unpackU32(encoded, OS_ROOTFS_V5_INODE_CHECKSUM_OFFSET_BYTES) != (
        calculateRootfsV5Crc32c(
            encoded[:OS_ROOTFS_V5_INODE_CHECKSUM_OFFSET_BYTES]
        )
    ):
        raise ValueError("rootfs v5 inode checksum 无效")
    if _unpackU32(encoded, OS_ROOTFS_V5_INODE_RESERVED_OFFSET_BYTES) != 0:
        raise ValueError("rootfs v5 inode reserved 非零")
    values = {}
    for field, offset in OS_ROOTFS_V5_INODE_OFFSETS.items():
        if field == "mappingRoot":
            values[field] = encoded[
                offset:offset + OS_ROOTFS_V5_INODE_MAPPING_ROOT_SIZE_BYTES
            ]
        elif field in (
            "ownerUserIdentifier",
            "ownerGroupIdentifier",
            "mode",
            "projectIdentifier",
        ):
            values[field] = _unpackU32(encoded, offset)
        else:
            values[field] = _unpackU64(encoded, offset)
    inode = RootfsV5Inode(**values)
    validateRootfsV5Inode(superblock, inode)
    return inode


def _markBitmapRange(bitmap: bytearray, beginBit: int, endBit: int) -> None:
    if beginBit < 0 or endBit < beginBit or endBit > len(bitmap) * 8:
        raise ValueError("rootfs v5 bitmap range 无效")
    for bitIndex in range(beginBit, endBit):
        bitmap[bitIndex // 8] |= 1 << (bitIndex % 8)


def buildInitialRootfsV5BlockBitmap(
    superblock: RootfsV5Superblock,
    descriptor: RootfsV5GroupDescriptor,
) -> bytes:
    validateRootfsV5GroupDescriptor(superblock, descriptor)
    bitmap = bytearray(superblock.blockSizeBytes)
    metadataBlockCount = descriptor.dataStartBlock - descriptor.firstBlock
    _markBitmapRange(bitmap, 0, metadataBlockCount)
    _markBitmapRange(bitmap, descriptor.blockCount, superblock.blocksPerGroup)
    return bytes(bitmap)


def buildInitialRootfsV5InodeBitmap(
    superblock: RootfsV5Superblock,
    descriptor: RootfsV5GroupDescriptor,
) -> bytes:
    validateRootfsV5GroupDescriptor(superblock, descriptor)
    bitmap = bytearray(superblock.blockSizeBytes)
    if descriptor.groupIndex == 0:
        _markBitmapRange(bitmap, 0, superblock.reservedInodeCount)
    _markBitmapRange(
        bitmap,
        descriptor.inodeCount,
        superblock.blockSizeBytes * OS_ROOTFS_V5_BITS_PER_BYTE,
    )
    return bytes(bitmap)


def _withBitmapChecksums(
    descriptor: RootfsV5GroupDescriptor,
    blockBitmap: bytes,
    inodeBitmap: bytes,
) -> RootfsV5GroupDescriptor:
    return RootfsV5GroupDescriptor(
        **{
            **asdict(descriptor),
            "blockBitmapChecksum": calculateRootfsV5Crc32c(blockBitmap),
            "inodeBitmapChecksum": calculateRootfsV5Crc32c(inodeBitmap),
        }
    )


def _rootfsV5AbsoluteOffset(
    superblock: RootfsV5Superblock,
    relativeBlock: int,
) -> int:
    if relativeBlock < 0 or relativeBlock >= superblock.totalBlockCount:
        raise ValueError("rootfs v5 block 越界")
    return (
        superblock.fileSystemStartLba * superblock.sectorSizeBytes
        + relativeBlock * superblock.blockSizeBytes
    )


def _readBlock(imageFile, superblock: RootfsV5Superblock, relativeBlock: int) -> bytes:
    imageFile.seek(_rootfsV5AbsoluteOffset(superblock, relativeBlock))
    block = imageFile.read(superblock.blockSizeBytes)
    if len(block) != superblock.blockSizeBytes:
        raise ValueError("rootfs v5 block 短读")
    return block


def _writeBlock(
    imageFile,
    superblock: RootfsV5Superblock,
    relativeBlock: int,
    content: bytes,
) -> None:
    if len(content) != superblock.blockSizeBytes:
        raise ValueError("rootfs v5 block 写入大小无效")
    imageFile.seek(_rootfsV5AbsoluteOffset(superblock, relativeBlock))
    imageFile.write(content)


def _makeReservedInode(
    superblock: RootfsV5Superblock,
    inodeNumber: int,
) -> RootfsV5Inode:
    if inodeNumber == superblock.rootInodeNumber:
        return RootfsV5Inode(
            inodeNumber=inodeNumber,
            generation=1,
            nodeType=OS_ROOTFS_V5_NODE_TYPE_DIRECTORY,
            flags=0,
            sizeBytes=0,
            allocatedBlockCount=0,
            linkCount=1,
            parentInodeNumber=inodeNumber,
            accessTimeNanoseconds=superblock.creationTimeNanoseconds,
            modificationTimeNanoseconds=superblock.creationTimeNanoseconds,
            changeTimeNanoseconds=superblock.creationTimeNanoseconds,
            birthTimeNanoseconds=superblock.creationTimeNanoseconds,
            ownerUserIdentifier=0,
            ownerGroupIdentifier=0,
            mode=OS_ROOTFS_V5_MODE_DIRECTORY | 0o755,
            projectIdentifier=0,
            mappingRoot=bytes(OS_ROOTFS_V5_INODE_MAPPING_ROOT_SIZE_BYTES),
        )
    return RootfsV5Inode(
        inodeNumber=inodeNumber,
        generation=1,
        nodeType=OS_ROOTFS_V5_NODE_TYPE_RESERVED,
        flags=0,
        sizeBytes=0,
        allocatedBlockCount=0,
        linkCount=0,
        parentInodeNumber=0,
        accessTimeNanoseconds=0,
        modificationTimeNanoseconds=0,
        changeTimeNanoseconds=0,
        birthTimeNanoseconds=0,
        ownerUserIdentifier=0,
        ownerGroupIdentifier=0,
        mode=0,
        projectIdentifier=0,
        mappingRoot=bytes(OS_ROOTFS_V5_INODE_MAPPING_ROOT_SIZE_BYTES),
    )


def _encodeDescriptorTable(
    superblock: RootfsV5Superblock,
    descriptors: tuple[RootfsV5GroupDescriptor, ...],
) -> bytes:
    table = bytearray(
        superblock.groupDescriptorTableBlockCount
        * superblock.blockSizeBytes
    )
    for descriptor in descriptors:
        begin = descriptor.groupIndex * superblock.groupDescriptorSizeBytes
        end = begin + superblock.groupDescriptorSizeBytes
        table[begin:end] = encodeRootfsV5GroupDescriptor(
            superblock,
            descriptor,
        )
    return bytes(table)


def formatRootfsV5(
    imagePath: Path,
    *,
    profile: RootfsV5FormatProfile | None = None,
    createImage: bool = False,
    force: bool = False,
    zeroInodeTables: bool = True,
    verifyUnallocatedInodes: bool = True,
    verifyResult: bool = True,
) -> RootfsV5Superblock:
    selectedProfile = (
        makeProductionRootfsV5FormatProfile()
        if profile is None
        else profile
    )
    superblock = planRootfsV5Superblock(selectedProfile)
    imageSizeBytes = (
        selectedProfile.deviceSectorCount * selectedProfile.sectorSizeBytes
    )
    if createImage:
        if imagePath.exists() and not force:
            raise ValueError("rootfs v5 --create 拒绝覆盖已有路径；使用 --force")
        imagePath.parent.mkdir(parents=True, exist_ok=True)
        with imagePath.open("w+b") as imageFile:
            imageFile.truncate(imageSizeBytes)
    if not imagePath.exists():
        raise ValueError("rootfs v5 镜像不存在；使用 --create 创建")
    if imagePath.stat().st_size != imageSizeBytes:
        raise ValueError("rootfs v5 镜像逻辑长度与 profile 不符")

    primaryOffset = (
        selectedProfile.fileSystemStartLba * selectedProfile.sectorSizeBytes
    )
    with imagePath.open("r+b") as imageFile:
        imageFile.seek(primaryOffset)
        existing = imageFile.read(selectedProfile.blockSizeBytes)
        if len(existing) != selectedProfile.blockSizeBytes:
            raise ValueError("rootfs v5 superblock 短读")
        if any(existing) and not force:
            raise ValueError("rootfs v5 区域非零；使用 --force 明确覆盖")

        descriptors = []
        bitmaps = []
        for groupIndex in range(superblock.groupCount):
            descriptor = _buildGroupDescriptorGeometry(
                superblock,
                groupIndex,
            )
            blockBitmap = buildInitialRootfsV5BlockBitmap(
                superblock,
                descriptor,
            )
            inodeBitmap = buildInitialRootfsV5InodeBitmap(
                superblock,
                descriptor,
            )
            descriptor = _withBitmapChecksums(
                descriptor,
                blockBitmap,
                inodeBitmap,
            )
            descriptors.append(descriptor)
            bitmaps.append((blockBitmap, inodeBitmap))

        journalStartBlock = descriptors[0].dataStartBlock
        rootExtentBlock = journalStartBlock + OS_ROOTFS_V5_JOURNAL_BLOCK_COUNT
        rootDirectoryBlock = rootExtentBlock + 1
        runtimeBlocks = range(journalStartBlock, rootDirectoryBlock + 1)
        groupZeroBlockBitmap = bytearray(bitmaps[0][0])
        for relativeBlock in runtimeBlocks:
            _setBitmapBit(
                groupZeroBlockBitmap,
                relativeBlock - descriptors[0].firstBlock,
                True,
            )
        bitmaps[0] = (bytes(groupZeroBlockBitmap), bitmaps[0][1])
        descriptors[0] = replace(
            descriptors[0],
            freeBlockCount=(
                descriptors[0].freeBlockCount - len(tuple(runtimeBlocks))
            ),
            metadataGeneration=descriptors[0].metadataGeneration + 1,
        )
        descriptors[0] = _withBitmapChecksums(
            descriptors[0],
            bitmaps[0][0],
            bitmaps[0][1],
        )
        superblock = replace(
            superblock,
            freeBlockCount=(
                superblock.freeBlockCount - len(tuple(runtimeBlocks))
            ),
            formatGeneration=superblock.formatGeneration + 1,
        )

        zeroBlock = bytes(superblock.blockSizeBytes)
        for descriptor, (blockBitmap, inodeBitmap) in zip(
            descriptors,
            bitmaps,
            strict=True,
        ):
            _writeBlock(
                imageFile,
                superblock,
                descriptor.blockBitmapBlock,
                blockBitmap,
            )
            _writeBlock(
                imageFile,
                superblock,
                descriptor.inodeBitmapBlock,
                inodeBitmap,
            )
            if not createImage and zeroInodeTables:
                for blockOffset in range(descriptor.inodeTableBlockCount):
                    _writeBlock(
                        imageFile,
                        superblock,
                        descriptor.inodeTableStartBlock + blockOffset,
                        zeroBlock,
                    )

        firstInodeBlock = bytearray(superblock.blockSizeBytes)
        for inodeNumber in range(1, superblock.firstUserInodeNumber):
            inode = _makeReservedInode(superblock, inodeNumber)
            if inodeNumber == superblock.rootInodeNumber:
                inode = replace(
                    inode,
                    sizeBytes=superblock.blockSizeBytes,
                    allocatedBlockCount=2,
                    linkCount=2,
                    mappingRoot=_encodeRootfsV5InodeExtension(rootExtentBlock),
                )
            offset = (inodeNumber - 1) * superblock.inodeSizeBytes
            firstInodeBlock[offset:offset + superblock.inodeSizeBytes] = (
                encodeRootfsV5Inode(superblock, inode)
            )
        _writeBlock(
            imageFile,
            superblock,
            descriptors[0].inodeTableStartBlock,
            bytes(firstInodeBlock),
        )

        for journalBlockOffset in range(OS_ROOTFS_V5_JOURNAL_BLOCK_COUNT):
            _writeBlock(
                imageFile,
                superblock,
                journalStartBlock + journalBlockOffset,
                zeroBlock,
            )
        _writeBlock(
            imageFile,
            superblock,
            journalStartBlock,
            _encodeRootfsV5JournalSuperblock(superblock, journalStartBlock),
        )
        _writeBlock(
            imageFile,
            superblock,
            rootExtentBlock,
            _encodeRootfsV5ExtentLeaf(
                superblock,
                superblock.rootInodeNumber,
                1,
                [rootDirectoryBlock],
            ),
        )
        rootNode = _RootfsV5BuildNode(
            name=b"",
            nodeType=OS_ROOTFS_V5_NODE_TYPE_DIRECTORY,
            inodeNumber=superblock.rootInodeNumber,
            generation=1,
            extentRootBlock=rootExtentBlock,
            dataBlocks=[rootDirectoryBlock],
        )
        _writeBlock(
            imageFile,
            superblock,
            rootDirectoryBlock,
            _encodeRootfsV5DirectoryBlock(superblock, rootNode),
        )

        descriptorTable = _encodeDescriptorTable(
            superblock,
            tuple(descriptors),
        )
        encodedSuperblock = encodeRootfsV5Superblock(superblock)
        for descriptor in descriptors:
            if not descriptor.flags & OS_ROOTFS_V5_GROUP_FLAG_HAS_SUPERBLOCK_COPY:
                continue
            _writeBlock(
                imageFile,
                superblock,
                descriptor.superblockCopyBlock,
                encodedSuperblock,
            )
            imageFile.seek(
                _rootfsV5AbsoluteOffset(
                    superblock,
                    descriptor.groupDescriptorCopyStartBlock,
                )
            )
            imageFile.write(descriptorTable)
        imageFile.flush()
    if verifyResult:
        inspectRootfsV5(
            imagePath,
            fileSystemStartLba=selectedProfile.fileSystemStartLba,
            sectorSizeBytes=selectedProfile.sectorSizeBytes,
            verifyUnallocatedInodes=verifyUnallocatedInodes,
        )
    return superblock


def _readRootfsV5Inode(
    imageFile,
    superblock: RootfsV5Superblock,
    descriptors: tuple[RootfsV5GroupDescriptor, ...] | list[RootfsV5GroupDescriptor],
    inodeNumber: int,
) -> RootfsV5Inode:
    if inodeNumber <= 0 or inodeNumber > superblock.inodeCount:
        raise ValueError("rootfs v5 inode number 越界")
    groupIndex = (inodeNumber - 1) // superblock.inodesPerGroup
    groupOffset = (inodeNumber - 1) % superblock.inodesPerGroup
    byteOffset = groupOffset * superblock.inodeSizeBytes
    tableBlock = descriptors[groupIndex].inodeTableStartBlock + (
        byteOffset // superblock.blockSizeBytes
    )
    blockOffset = byteOffset % superblock.blockSizeBytes
    block = _readBlock(imageFile, superblock, tableBlock)
    return decodeRootfsV5Inode(
        superblock,
        block[blockOffset:blockOffset + superblock.inodeSizeBytes],
    )


def _writeRootfsV5Inode(
    imageFile,
    superblock: RootfsV5Superblock,
    descriptors: tuple[RootfsV5GroupDescriptor, ...] | list[RootfsV5GroupDescriptor],
    inode: RootfsV5Inode,
) -> None:
    groupIndex = (inode.inodeNumber - 1) // superblock.inodesPerGroup
    groupOffset = (inode.inodeNumber - 1) % superblock.inodesPerGroup
    byteOffset = groupOffset * superblock.inodeSizeBytes
    tableBlock = descriptors[groupIndex].inodeTableStartBlock + (
        byteOffset // superblock.blockSizeBytes
    )
    blockOffset = byteOffset % superblock.blockSizeBytes
    block = bytearray(_readBlock(imageFile, superblock, tableBlock))
    block[blockOffset:blockOffset + superblock.inodeSizeBytes] = encodeRootfsV5Inode(
        superblock,
        inode,
    )
    _writeBlock(imageFile, superblock, tableBlock, bytes(block))


def _writeRootfsV5MetadataCopies(
    imageFile,
    superblock: RootfsV5Superblock,
    descriptors: tuple[RootfsV5GroupDescriptor, ...] | list[RootfsV5GroupDescriptor],
) -> None:
    descriptorTable = _encodeDescriptorTable(superblock, tuple(descriptors))
    encodedSuperblock = encodeRootfsV5Superblock(superblock)
    for descriptor in descriptors:
        if not descriptor.flags & OS_ROOTFS_V5_GROUP_FLAG_HAS_SUPERBLOCK_COPY:
            continue
        _writeBlock(
            imageFile,
            superblock,
            descriptor.superblockCopyBlock,
            encodedSuperblock,
        )
        imageFile.seek(
            _rootfsV5AbsoluteOffset(
                superblock,
                descriptor.groupDescriptorCopyStartBlock,
            )
        )
        imageFile.write(descriptorTable)


def _buildRootfsV5InstallTree(
    files: tuple[RootfsV5InstallFile, ...],
) -> _RootfsV5BuildNode:
    root = _RootfsV5BuildNode(
        name=b"",
        nodeType=OS_ROOTFS_V5_NODE_TYPE_DIRECTORY,
        inodeNumber=OS_ROOTFS_V5_ROOT_INODE_NUMBER,
    )
    for installFile in files:
        if not installFile.sourcePath.is_file():
            raise ValueError(f"rootfs v5 安装源不存在：{installFile.sourcePath}")
        try:
            encodedPath = installFile.imagePath.encode("utf-8")
        except UnicodeEncodeError as error:
            raise ValueError("rootfs v5 安装路径不是 UTF-8") from error
        if not encodedPath.startswith(b"/") or encodedPath.endswith(b"/"):
            raise ValueError("rootfs v5 安装路径必须是绝对文件路径")
        components = encodedPath.split(b"/")[1:]
        if not components or any(
            not component
            or component in (b".", b"..")
            or len(component) > 255
            or b"\0" in component
            for component in components
        ):
            raise ValueError("rootfs v5 安装路径组件无效")
        parent = root
        for component in components[:-1]:
            child = parent.children.get(component)
            if child is None:
                child = _RootfsV5BuildNode(
                    name=component,
                    nodeType=OS_ROOTFS_V5_NODE_TYPE_DIRECTORY,
                    parent=parent,
                )
                parent.children[component] = child
            elif child.nodeType != OS_ROOTFS_V5_NODE_TYPE_DIRECTORY:
                raise ValueError("rootfs v5 安装路径穿过普通文件")
            parent = child
        fileName = components[-1]
        if fileName in parent.children:
            raise ValueError("rootfs v5 安装路径重复")
        parent.children[fileName] = _RootfsV5BuildNode(
            name=fileName,
            nodeType=OS_ROOTFS_V5_NODE_TYPE_REGULAR_FILE,
            content=installFile.sourcePath.read_bytes(),
            parent=parent,
        )
    return root


def installRootfsV5Files(
    imagePath: Path,
    files: tuple[RootfsV5InstallFile, ...],
    *,
    fileSystemStartLba: int = OS_ROOTFS_V5_FILE_SYSTEM_START_LBA,
    sectorSizeBytes: int = OS_ROOTFS_V5_SECTOR_SIZE_BYTES,
    verifyUnallocatedInodes: bool = True,
    verifyExisting: bool = True,
    _buildRoot: _RootfsV5BuildNode | None = None,
) -> RootfsV5Inspection:
    if verifyExisting:
        inspectRootfsV5(
            imagePath,
            fileSystemStartLba=fileSystemStartLba,
            sectorSizeBytes=sectorSizeBytes,
            verifyUnallocatedInodes=verifyUnallocatedInodes,
        )
    root = _buildRootfsV5InstallTree(files) if _buildRoot is None else _buildRoot
    with imagePath.open("r+b") as imageFile:
        _, superblock = _readPrimarySuperblock(
            imageFile,
            fileSystemStartLba,
            sectorSizeBytes,
        )
        descriptorTable = _readDescriptorTable(
            imageFile,
            superblock,
            superblock.groupDescriptorTableStartBlock,
        )
        descriptors = list(_decodeDescriptorTable(superblock, descriptorTable))
        blockBitmaps = [
            bytearray(_readBlock(imageFile, superblock, descriptor.blockBitmapBlock))
            for descriptor in descriptors
        ]
        inodeBitmaps = [
            bytearray(_readBlock(imageFile, superblock, descriptor.inodeBitmapBlock))
            for descriptor in descriptors
        ]
        rootInode = _readRootfsV5Inode(
            imageFile,
            superblock,
            descriptors,
            superblock.rootInodeNumber,
        )
        root.extentRootBlock = _decodeRootfsV5InodeExtension(rootInode.mappingRoot)
        root.dataBlocks = _decodeRootfsV5ExtentLeaf(
            superblock,
            _readBlock(imageFile, superblock, root.extentRootBlock),
            root.inodeNumber,
            root.generation,
        )
        if len(root.dataBlocks) != 1 or _decodeRootfsV5DirectoryBlock(
            superblock,
            _readBlock(imageFile, superblock, root.dataBlocks[0]),
            rootInode,
        ):
            raise ValueError("rootfs v5 安装要求空目标根目录")

        def allocateInode() -> int:
            for groupIndex, descriptor in enumerate(descriptors):
                firstBit = (
                    superblock.firstUserInodeNumber - descriptor.inodeStartNumber
                    if groupIndex == 0
                    else 0
                )
                firstBit = max(firstBit, 0)
                for bitIndex in range(firstBit, descriptor.inodeCount):
                    if _bitmapBitIsSet(inodeBitmaps[groupIndex], bitIndex):
                        continue
                    _setBitmapBit(inodeBitmaps[groupIndex], bitIndex, True)
                    descriptors[groupIndex] = replace(
                        descriptors[groupIndex],
                        freeInodeCount=descriptors[groupIndex].freeInodeCount - 1,
                        metadataGeneration=descriptors[groupIndex].metadataGeneration + 1,
                    )
                    return descriptor.inodeStartNumber + bitIndex
            raise ValueError("rootfs v5 inode 已耗尽")

        def allocateBlock(preferredGroup: int) -> int:
            groupOrder = list(range(preferredGroup, len(descriptors))) + list(
                range(0, preferredGroup)
            )
            for groupIndex in groupOrder:
                descriptor = descriptors[groupIndex]
                beginBit = descriptor.dataStartBlock - descriptor.firstBlock
                for bitIndex in range(beginBit, descriptor.blockCount):
                    if _bitmapBitIsSet(blockBitmaps[groupIndex], bitIndex):
                        continue
                    _setBitmapBit(blockBitmaps[groupIndex], bitIndex, True)
                    descriptors[groupIndex] = replace(
                        descriptor,
                        freeBlockCount=descriptor.freeBlockCount - 1,
                        metadataGeneration=descriptor.metadataGeneration + 1,
                    )
                    return descriptor.firstBlock + bitIndex
            raise ValueError("rootfs v5 data block 已耗尽")

        nodes = [root]
        nodeIndex = 0
        while nodeIndex < len(nodes):
            nodes.extend(nodes[nodeIndex].children.values())
            nodeIndex += 1
        for node in nodes[1:]:
            if node.hardlinkTarget is not None:
                continue
            node.inodeNumber = allocateInode()
            inodeGroup = (node.inodeNumber - 1) // superblock.inodesPerGroup
            node.extentRootBlock = allocateBlock(inodeGroup)
            dataBlockCount = (
                1
                if node.nodeType == OS_ROOTFS_V5_NODE_TYPE_DIRECTORY
                else (len(node.content) + superblock.blockSizeBytes - 1)
                // superblock.blockSizeBytes
            )
            node.dataBlocks = [allocateBlock(inodeGroup) for _ in range(dataBlockCount)]
            if node.nodeType == OS_ROOTFS_V5_NODE_TYPE_DIRECTORY:
                descriptors[inodeGroup] = replace(
                    descriptors[inodeGroup],
                    usedDirectoryCount=descriptors[inodeGroup].usedDirectoryCount + 1,
                )

        for node in nodes[1:]:
            if node.hardlinkTarget is not None:
                if node.hardlinkTarget.inodeNumber == 0:
                    raise ValueError("rootfs v5 hardlink target 尚未分配")
                node.inodeNumber = node.hardlinkTarget.inodeNumber
                node.generation = node.hardlinkTarget.generation

        timestamp = superblock.creationTimeNanoseconds
        for node in nodes:
            if node.hardlinkTarget is not None:
                continue
            if node.nodeType == OS_ROOTFS_V5_NODE_TYPE_DIRECTORY:
                if len(node.dataBlocks) != 1:
                    raise ValueError("rootfs v5 目录块布局无效")
                _writeBlock(
                    imageFile,
                    superblock,
                    node.dataBlocks[0],
                    _encodeRootfsV5DirectoryBlock(superblock, node),
                )
                sizeBytes = superblock.blockSizeBytes
                mode = node.mode or (OS_ROOTFS_V5_MODE_DIRECTORY | 0o755)
                linkCount = 2 + sum(
                    child.nodeType == OS_ROOTFS_V5_NODE_TYPE_DIRECTORY
                    for child in node.children.values()
                )
            else:
                for logicalBlock, relativeBlock in enumerate(node.dataBlocks):
                    begin = logicalBlock * superblock.blockSizeBytes
                    content = node.content[begin:begin + superblock.blockSizeBytes]
                    _writeBlock(
                        imageFile,
                        superblock,
                        relativeBlock,
                        content + bytes(superblock.blockSizeBytes - len(content)),
                    )
                sizeBytes = len(node.content)
                mode = node.mode or (
                    (OS_ROOTFS_V5_MODE_SYMBOLIC_LINK | 0o777)
                    if node.nodeType == OS_ROOTFS_V5_NODE_TYPE_SYMBOLIC_LINK
                    else (OS_ROOTFS_V5_MODE_REGULAR | 0o755)
                )
                linkCount = node.linkCount
            _writeBlock(
                imageFile,
                superblock,
                node.extentRootBlock,
                _encodeRootfsV5ExtentLeaf(
                    superblock,
                    node.inodeNumber,
                    node.generation,
                    node.dataBlocks,
                ),
            )
            inode = RootfsV5Inode(
                inodeNumber=node.inodeNumber,
                generation=node.generation,
                nodeType=node.nodeType,
                flags=0,
                sizeBytes=sizeBytes,
                allocatedBlockCount=1 + len(node.dataBlocks),
                linkCount=linkCount,
                parentInodeNumber=(
                    node.inodeNumber if node.parent is None else node.parent.inodeNumber
                ),
                accessTimeNanoseconds=node.accessTimeNanoseconds or timestamp,
                modificationTimeNanoseconds=node.modificationTimeNanoseconds or timestamp,
                changeTimeNanoseconds=node.changeTimeNanoseconds or timestamp,
                birthTimeNanoseconds=node.birthTimeNanoseconds or timestamp,
                ownerUserIdentifier=node.ownerUserIdentifier,
                ownerGroupIdentifier=node.ownerGroupIdentifier,
                mode=mode,
                projectIdentifier=0,
                mappingRoot=_encodeRootfsV5InodeExtension(node.extentRootBlock),
            )
            _writeRootfsV5Inode(imageFile, superblock, descriptors, inode)

        for groupIndex, descriptor in enumerate(descriptors):
            descriptors[groupIndex] = _withBitmapChecksums(
                descriptor,
                bytes(blockBitmaps[groupIndex]),
                bytes(inodeBitmaps[groupIndex]),
            )
            _writeBlock(
                imageFile,
                superblock,
                descriptor.blockBitmapBlock,
                bytes(blockBitmaps[groupIndex]),
            )
            _writeBlock(
                imageFile,
                superblock,
                descriptor.inodeBitmapBlock,
                bytes(inodeBitmaps[groupIndex]),
            )
        superblock = replace(
            superblock,
            formatGeneration=superblock.formatGeneration + 1,
            freeBlockCount=sum(descriptor.freeBlockCount for descriptor in descriptors),
            freeInodeCount=sum(descriptor.freeInodeCount for descriptor in descriptors),
            allocatedDirectoryCount=sum(
                descriptor.usedDirectoryCount for descriptor in descriptors
            ),
        )
        _writeRootfsV5MetadataCopies(imageFile, superblock, descriptors)
        imageFile.flush()
    return inspectRootfsV5(
        imagePath,
        fileSystemStartLba=fileSystemStartLba,
        sectorSizeBytes=sectorSizeBytes,
        verifyUnallocatedInodes=verifyUnallocatedInodes,
    )


def migrateRootfsV4ToV5(
    sourceImagePath: Path,
    destinationImagePath: Path,
    *,
    force: bool = False,
) -> RootfsV5Inspection:
    """把一份已通过 v4 fsck 的树复制到新建 v5 镜像，不原地覆盖源盘。"""
    if sourceImagePath.resolve() == destinationImagePath.resolve():
        raise ValueError("rootfs v4→v5 migration 拒绝原地覆盖")
    inspectRootfsV2(sourceImagePath)
    reader = RootfsV2Reader(sourceImagePath)
    try:
        allocatedBlocks: set[int] = set()
        firstNodesByInode: dict[int, _RootfsV5BuildNode] = {}

        def copyNode(
            inodeNumber: int,
            name: bytes,
            parent: _RootfsV5BuildNode | None,
        ) -> _RootfsV5BuildNode:
            inode = reader.readInode(inodeNumber)
            existing = firstNodesByInode.get(inodeNumber)
            if existing is not None:
                if inode.nodeType == OS_ROOTFS_V2_NODE_TYPE_DIRECTORY:
                    raise ValueError("rootfs v4 migration 拒绝目录硬链接或环")
                return _RootfsV5BuildNode(
                    name=name,
                    nodeType=existing.nodeType,
                    parent=parent,
                    hardlinkTarget=existing,
                )
            nodeType = {
                OS_ROOTFS_V2_NODE_TYPE_REGULAR_FILE: OS_ROOTFS_V5_NODE_TYPE_REGULAR_FILE,
                OS_ROOTFS_V2_NODE_TYPE_DIRECTORY: OS_ROOTFS_V5_NODE_TYPE_DIRECTORY,
                OS_ROOTFS_V2_NODE_TYPE_SYMBOLIC_LINK: OS_ROOTFS_V5_NODE_TYPE_SYMBOLIC_LINK,
            }.get(inode.nodeType)
            if nodeType is None:
                raise ValueError("rootfs v4 migration 遇到未知 inode type")
            node = _RootfsV5BuildNode(
                name=name,
                nodeType=nodeType,
                parent=parent,
                generation=inode.generation,
                ownerUserIdentifier=inode.ownerUserIdentifier,
                ownerGroupIdentifier=inode.ownerGroupIdentifier,
                mode=inode.mode,
                accessTimeNanoseconds=inode.accessTimeNanoseconds,
                modificationTimeNanoseconds=inode.modificationTimeNanoseconds,
                changeTimeNanoseconds=inode.changeTimeNanoseconds,
                birthTimeNanoseconds=inode.birthTimeNanoseconds,
                linkCount=inode.linkCount,
            )
            firstNodesByInode[inodeNumber] = node
            blockMap = reader.buildFileBlockMap(inode, allocatedBlocks)
            if inode.nodeType == OS_ROOTFS_V2_NODE_TYPE_DIRECTORY:
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
                    if entry is None:
                        continue
                    if entry.name in node.children:
                        raise ValueError("rootfs v4 migration 遇到重复目录名")
                    node.children[entry.name] = copyNode(
                        entry.inodeNumber,
                        entry.name,
                        node,
                    )
            else:
                node.content = reader.readFileBytes(
                    inode,
                    blockMap,
                    0,
                    inode.sizeBytes,
                )
            return node

        root = copyNode(OS_ROOTFS_V2_ROOT_INODE_NUMBER, b"", None)
        root.inodeNumber = OS_ROOTFS_V5_ROOT_INODE_NUMBER
        root.generation = 1
    finally:
        reader.close()
    formatRootfsV5(
        destinationImagePath,
        createImage=True,
        force=force,
        zeroInodeTables=False,
        verifyUnallocatedInodes=False,
        verifyResult=False,
    )
    inspection = installRootfsV5Files(
        destinationImagePath,
        (),
        verifyUnallocatedInodes=False,
        verifyExisting=False,
        _buildRoot=root,
    )
    inspectRootfsV5(destinationImagePath)
    return inspection


def _readPrimarySuperblock(
    imageFile,
    fileSystemStartLba: int,
    sectorSizeBytes: int,
) -> tuple[bytes, RootfsV5Superblock]:
    imageFile.seek(fileSystemStartLba * sectorSizeBytes)
    encoded = imageFile.read(OS_ROOTFS_V5_BLOCK_SIZE_BYTES)
    if len(encoded) != OS_ROOTFS_V5_BLOCK_SIZE_BYTES:
        raise ValueError("rootfs v5 primary superblock 短读")
    superblock = decodeRootfsV5Superblock(encoded)
    if (
        superblock.fileSystemStartLba != fileSystemStartLba
        or superblock.sectorSizeBytes != sectorSizeBytes
    ):
        raise ValueError("rootfs v5 primary superblock 位置与自身几何不符")
    return encoded, superblock


def _readDescriptorTable(
    imageFile,
    superblock: RootfsV5Superblock,
    startBlock: int,
) -> bytes:
    imageFile.seek(_rootfsV5AbsoluteOffset(superblock, startBlock))
    sizeBytes = (
        superblock.groupDescriptorTableBlockCount * superblock.blockSizeBytes
    )
    content = imageFile.read(sizeBytes)
    if len(content) != sizeBytes:
        raise ValueError("rootfs v5 descriptor table 短读")
    return content


def _decodeDescriptorTable(
    superblock: RootfsV5Superblock,
    content: bytes,
) -> tuple[RootfsV5GroupDescriptor, ...]:
    descriptors = []
    for groupIndex in range(superblock.groupCount):
        begin = groupIndex * superblock.groupDescriptorSizeBytes
        end = begin + superblock.groupDescriptorSizeBytes
        descriptor = decodeRootfsV5GroupDescriptor(
            superblock,
            content[begin:end],
        )
        if descriptor.groupIndex != groupIndex:
            raise ValueError("rootfs v5 descriptor table 顺序错误")
        descriptors.append(descriptor)
    if any(
        content[
            superblock.groupCount * superblock.groupDescriptorSizeBytes:
        ]
    ):
        raise ValueError("rootfs v5 descriptor table 尾部非零")
    return tuple(descriptors)


def inspectRootfsV5(
    imagePath: Path,
    *,
    fileSystemStartLba: int = OS_ROOTFS_V5_FILE_SYSTEM_START_LBA,
    sectorSizeBytes: int = OS_ROOTFS_V5_SECTOR_SIZE_BYTES,
    verifyUnallocatedInodes: bool = True,
) -> RootfsV5Inspection:
    if not imagePath.exists():
        raise ValueError("rootfs v5 镜像不存在")
    with imagePath.open("rb") as imageFile:
        encodedSuperblock, superblock = _readPrimarySuperblock(
            imageFile,
            fileSystemStartLba,
            sectorSizeBytes,
        )
        expectedImageSize = (
            superblock.deviceSectorCount * superblock.sectorSizeBytes
        )
        if imagePath.stat().st_size != expectedImageSize:
            raise ValueError("rootfs v5 镜像长度与 superblock 不符")
        descriptorTable = _readDescriptorTable(
            imageFile,
            superblock,
            superblock.groupDescriptorTableStartBlock,
        )
        descriptors = _decodeDescriptorTable(superblock, descriptorTable)
        freeBlockCount = 0
        freeInodeCount = 0
        directoryCount = 0
        sparseBackupGroupCount = 0
        highestMetadataBlock = 0
        blockBitmaps: list[bytes] = []
        inodeBitmaps: list[bytes] = []
        allocatedInodes: dict[int, RootfsV5Inode] = {}
        for descriptor in descriptors:
            blockBitmap = _readBlock(
                imageFile,
                superblock,
                descriptor.blockBitmapBlock,
            )
            inodeBitmap = _readBlock(
                imageFile,
                superblock,
                descriptor.inodeBitmapBlock,
            )
            if calculateRootfsV5Crc32c(blockBitmap) != (
                descriptor.blockBitmapChecksum
            ):
                raise ValueError("rootfs v5 block bitmap checksum 无效")
            if calculateRootfsV5Crc32c(inodeBitmap) != (
                descriptor.inodeBitmapChecksum
            ):
                raise ValueError("rootfs v5 inode bitmap checksum 无效")
            initialBlockBitmap = buildInitialRootfsV5BlockBitmap(superblock, descriptor)
            initialInodeBitmap = buildInitialRootfsV5InodeBitmap(superblock, descriptor)
            if any(
                baseline & ~current
                for baseline, current in zip(
                    initialBlockBitmap,
                    blockBitmap,
                    strict=True,
                )
            ):
                raise ValueError("rootfs v5 固定 metadata block 被释放")
            if any(
                baseline & ~current
                for baseline, current in zip(
                    initialInodeBitmap,
                    inodeBitmap,
                    strict=True,
                )
            ):
                raise ValueError("rootfs v5 reserved inode 被释放")
            countedFreeBlocks = sum(
                not _bitmapBitIsSet(blockBitmap, bitIndex)
                for bitIndex in range(descriptor.blockCount)
            )
            countedFreeInodes = sum(
                not _bitmapBitIsSet(inodeBitmap, bitIndex)
                for bitIndex in range(descriptor.inodeCount)
            )
            if (
                countedFreeBlocks != descriptor.freeBlockCount
                or countedFreeInodes != descriptor.freeInodeCount
            ):
                raise ValueError("rootfs v5 group bitmap 与 free count 不一致")
            blockBitmaps.append(blockBitmap)
            inodeBitmaps.append(inodeBitmap)
            inodeTable = b""
            if verifyUnallocatedInodes:
                imageFile.seek(
                    _rootfsV5AbsoluteOffset(
                        superblock,
                        descriptor.inodeTableStartBlock,
                    )
                )
                inodeTable = imageFile.read(
                    descriptor.inodeTableBlockCount * superblock.blockSizeBytes
                )
                if len(inodeTable) != (
                    descriptor.inodeTableBlockCount * superblock.blockSizeBytes
                ):
                    raise ValueError("rootfs v5 inode table 短读")
            cachedInodeBlockIndex = -1
            cachedInodeBlock = b""
            for inodeOffset in range(descriptor.inodeCount):
                begin = inodeOffset * superblock.inodeSizeBytes
                allocated = _bitmapBitIsSet(inodeBitmap, inodeOffset)
                if verifyUnallocatedInodes:
                    encodedInode = inodeTable[
                        begin:begin + superblock.inodeSizeBytes
                    ]
                else:
                    if not allocated:
                        continue
                    inodeBlockIndex = begin // superblock.blockSizeBytes
                    if inodeBlockIndex != cachedInodeBlockIndex:
                        cachedInodeBlock = _readBlock(
                            imageFile,
                            superblock,
                            descriptor.inodeTableStartBlock + inodeBlockIndex,
                        )
                        cachedInodeBlockIndex = inodeBlockIndex
                    blockOffset = begin % superblock.blockSizeBytes
                    encodedInode = cachedInodeBlock[
                        blockOffset:blockOffset + superblock.inodeSizeBytes
                    ]
                if not allocated:
                    if any(encodedInode):
                        raise ValueError("rootfs v5 未分配 inode 记录非零")
                    continue
                inode = decodeRootfsV5Inode(superblock, encodedInode)
                expectedInodeNumber = descriptor.inodeStartNumber + inodeOffset
                if inode.inodeNumber != expectedInodeNumber:
                    raise ValueError("rootfs v5 inode table 编号错位")
                if (
                    expectedInodeNumber < superblock.firstUserInodeNumber
                    and expectedInodeNumber != superblock.rootInodeNumber
                    and inode != _makeReservedInode(superblock, expectedInodeNumber)
                ):
                    raise ValueError("rootfs v5 reserved inode 状态无效")
                allocatedInodes[expectedInodeNumber] = inode
            freeBlockCount += descriptor.freeBlockCount
            freeInodeCount += descriptor.freeInodeCount
            directoryCount += descriptor.usedDirectoryCount
            highestMetadataBlock = max(
                highestMetadataBlock,
                descriptor.inodeTableStartBlock
                + descriptor.inodeTableBlockCount
                - 1,
            )
            if descriptor.flags & OS_ROOTFS_V5_GROUP_FLAG_HAS_SUPERBLOCK_COPY:
                sparseBackupGroupCount += 1
                if _readBlock(
                    imageFile,
                    superblock,
                    descriptor.superblockCopyBlock,
                ) != encodedSuperblock:
                    raise ValueError("rootfs v5 backup superblock 不一致")
                if _readDescriptorTable(
                    imageFile,
                    superblock,
                    descriptor.groupDescriptorCopyStartBlock,
                ) != descriptorTable:
                    raise ValueError("rootfs v5 backup descriptor table 不一致")

        if (
            freeBlockCount != superblock.freeBlockCount
            or freeInodeCount != superblock.freeInodeCount
            or directoryCount != superblock.allocatedDirectoryCount
        ):
            raise ValueError("rootfs v5 全局与 group 计数不守恒")
        rootInode = allocatedInodes.get(superblock.rootInodeNumber)
        if rootInode is None or rootInode.nodeType != OS_ROOTFS_V5_NODE_TYPE_DIRECTORY:
            raise ValueError("rootfs v5 root inode 缺失")
        journalStartBlock = descriptors[0].dataStartBlock
        _validateRootfsV5JournalSuperblock(
            superblock,
            _readBlock(imageFile, superblock, journalStartBlock),
            journalStartBlock,
        )
        ownedBlocks: set[int] = set()
        for descriptor in descriptors:
            ownedBlocks.update(range(descriptor.firstBlock, descriptor.dataStartBlock))
        journalBlocks = set(
            range(
                journalStartBlock,
                journalStartBlock + OS_ROOTFS_V5_JOURNAL_BLOCK_COUNT,
            )
        )
        if ownedBlocks & journalBlocks:
            raise ValueError("rootfs v5 journal 与固定 metadata 重叠")
        ownedBlocks.update(journalBlocks)
        for relativeBlock in journalBlocks:
            groupIndex = relativeBlock // superblock.blocksPerGroup
            bitIndex = relativeBlock - descriptors[groupIndex].firstBlock
            if not _bitmapBitIsSet(blockBitmaps[groupIndex], bitIndex):
                raise ValueError("rootfs v5 journal block 未分配")
        inodeDataBlocks: dict[int, tuple[int, ...]] = {}
        for inodeNumber, inode in allocatedInodes.items():
            if inodeNumber < superblock.firstUserInodeNumber and inodeNumber != superblock.rootInodeNumber:
                continue
            if inode.allocatedBlockCount == 0:
                inodeDataBlocks[inodeNumber] = ()
                continue
            extentRootBlock = _decodeRootfsV5InodeExtension(inode.mappingRoot)
            if extentRootBlock in ownedBlocks:
                raise ValueError("rootfs v5 extent root 重复所有权")
            ownedBlocks.add(extentRootBlock)
            dataBlocks = tuple(
                _decodeRootfsV5ExtentLeaf(
                    superblock,
                    _readBlock(imageFile, superblock, extentRootBlock),
                    inodeNumber,
                    inode.generation,
                )
            )
            if inode.allocatedBlockCount != 1 + len(dataBlocks):
                raise ValueError("rootfs v5 inode allocated block count 无效")
            if inode.sizeBytes > len(dataBlocks) * superblock.blockSizeBytes:
                raise ValueError("rootfs v5 inode size 超过 extent")
            for relativeBlock in (extentRootBlock, *dataBlocks):
                groupIndex = relativeBlock // superblock.blocksPerGroup
                bitIndex = relativeBlock - descriptors[groupIndex].firstBlock
                if not _bitmapBitIsSet(blockBitmaps[groupIndex], bitIndex):
                    raise ValueError("rootfs v5 inode 引用未分配 block")
            for relativeBlock in dataBlocks:
                if relativeBlock in ownedBlocks:
                    raise ValueError("rootfs v5 data block 重复所有权")
                ownedBlocks.add(relativeBlock)
            inodeDataBlocks[inodeNumber] = dataBlocks

        reachable = {superblock.rootInodeNumber}
        queue = [superblock.rootInodeNumber]
        observedLinkCounts = {superblock.rootInodeNumber: 1}
        regularFileCount = 0
        symbolicLinkCount = 0
        reachableDirectoryCount = 0
        while queue:
            inodeNumber = queue.pop(0)
            inode = allocatedInodes[inodeNumber]
            if inode.nodeType == OS_ROOTFS_V5_NODE_TYPE_REGULAR_FILE:
                regularFileCount += 1
                continue
            if inode.nodeType == OS_ROOTFS_V5_NODE_TYPE_SYMBOLIC_LINK:
                symbolicLinkCount += 1
                continue
            if inode.nodeType != OS_ROOTFS_V5_NODE_TYPE_DIRECTORY:
                raise ValueError("rootfs v5 可达 inode type 无效")
            reachableDirectoryCount += 1
            dataBlocks = inodeDataBlocks[inodeNumber]
            if not dataBlocks and inode.sizeBytes == 0:
                continue
            if len(dataBlocks) != 1:
                raise ValueError("rootfs v5 当前目录必须由一个变长目录块承载")
            entries = _decodeRootfsV5DirectoryBlock(
                superblock,
                _readBlock(imageFile, superblock, dataBlocks[0]),
                inode,
            )
            for childNumber, childGeneration, childType, _ in entries:
                child = allocatedInodes.get(childNumber)
                if (
                    child is None
                    or child.generation != childGeneration
                    or child.nodeType != childType
                    or (
                        child.nodeType == OS_ROOTFS_V5_NODE_TYPE_DIRECTORY
                        and child.parentInodeNumber != inodeNumber
                    )
                ):
                    raise ValueError("rootfs v5 directory 可达关系无效")
                observedLinkCounts[childNumber] = (
                    observedLinkCounts.get(childNumber, 0) + 1
                )
                if childNumber in reachable:
                    if child.nodeType == OS_ROOTFS_V5_NODE_TYPE_DIRECTORY:
                        raise ValueError("rootfs v5 directory 被重复引用")
                    continue
                reachable.add(childNumber)
                queue.append(childNumber)
        expectedReachable = {
            inodeNumber
            for inodeNumber, inode in allocatedInodes.items()
            if (
                inodeNumber == superblock.rootInodeNumber
                or (
                    inodeNumber >= superblock.firstUserInodeNumber
                    and not inode.flags & OS_ROOTFS_V5_INODE_FLAG_ORPHAN
                )
            )
        }
        if reachable != expectedReachable:
            raise ValueError("rootfs v5 存在不可达已分配 inode")
        if reachableDirectoryCount != superblock.allocatedDirectoryCount:
            raise ValueError("rootfs v5 directory count 与可达树不一致")
        for inodeNumber in reachable:
            inode = allocatedInodes[inodeNumber]
            if inode.nodeType != OS_ROOTFS_V5_NODE_TYPE_DIRECTORY and (
                inode.linkCount != observedLinkCounts.get(inodeNumber, 0)
            ):
                raise ValueError("rootfs v5 link count 与目录引用不一致")
        for inodeNumber, inode in allocatedInodes.items():
            if inode.flags & OS_ROOTFS_V5_INODE_FLAG_ORPHAN and (
                inodeNumber < superblock.firstUserInodeNumber
                or inode.linkCount != 0
                or inodeNumber in reachable
            ):
                raise ValueError("rootfs v5 orphan inode 状态无效")

    return RootfsV5Inspection(
        version=superblock.version,
        blockSizeBytes=superblock.blockSizeBytes,
        totalBlockCount=superblock.totalBlockCount,
        blocksPerGroup=superblock.blocksPerGroup,
        groupCount=superblock.groupCount,
        inodeCount=superblock.inodeCount,
        reservedInodeCount=superblock.reservedInodeCount,
        freeBlockCount=superblock.freeBlockCount,
        freeInodeCount=superblock.freeInodeCount,
        allocatedDirectoryCount=superblock.allocatedDirectoryCount,
        sparseBackupGroupCount=sparseBackupGroupCount,
        groupDescriptorTableBlockCount=(
            superblock.groupDescriptorTableBlockCount
        ),
        rootInodeNumber=superblock.rootInodeNumber,
        highestMetadataBlock=highestMetadataBlock,
        logicalImageSizeBytes=imagePath.stat().st_size,
        allocatedImageSizeBytes=imagePath.stat().st_blocks * 512,
        uuidLow=superblock.fileSystemUuid.low,
        uuidHigh=superblock.fileSystemUuid.high,
        reachableInodeCount=len(reachable),
        regularFileCount=regularFileCount,
        symbolicLinkCount=symbolicLinkCount,
        journalStartBlock=journalStartBlock,
    )


def rootfsV5InspectionAsJson(inspection: RootfsV5Inspection) -> str:
    return json.dumps(asdict(inspection), indent=2, sort_keys=True)


def _rewriteChecksum(buffer: bytearray, checksumOffset: int) -> None:
    _packU32(
        buffer,
        checksumOffset,
        calculateRootfsV5Crc32c(buffer[:checksumOffset]),
    )


def corruptRootfsV5(
    imagePath: Path,
    corruptionKind: str,
    *,
    fileSystemStartLba: int = OS_ROOTFS_V5_FILE_SYSTEM_START_LBA,
    sectorSizeBytes: int = OS_ROOTFS_V5_SECTOR_SIZE_BYTES,
) -> None:
    if corruptionKind not in OS_ROOTFS_V5_CORRUPTION_KINDS:
        raise ValueError("未知 rootfs v5 corruption kind")
    with imagePath.open("r+b") as imageFile:
        encodedSuperblock, superblock = _readPrimarySuperblock(
            imageFile,
            fileSystemStartLba,
            sectorSizeBytes,
        )
        descriptorTable = _readDescriptorTable(
            imageFile,
            superblock,
            superblock.groupDescriptorTableStartBlock,
        )
        descriptors = _decodeDescriptorTable(superblock, descriptorTable)
        primaryOffset = fileSystemStartLba * sectorSizeBytes
        if corruptionKind == "superblock-checksum":
            imageFile.seek(primaryOffset + 64)
            imageFile.write(bytes([encodedSuperblock[64] ^ 0x40]))
            return
        if corruptionKind == "required-feature":
            modified = bytearray(encodedSuperblock)
            features = _unpackU64(
                modified,
                OS_ROOTFS_V5_SUPERBLOCK_OFFSETS["incompatibleFeatures"],
            )
            _packU64(
                modified,
                OS_ROOTFS_V5_SUPERBLOCK_OFFSETS["incompatibleFeatures"],
                features & ~OS_ROOTFS_V5_INCOMPAT_BLOCK_GROUPS,
            )
            _rewriteChecksum(
                modified,
                OS_ROOTFS_V5_SUPERBLOCK_CHECKSUM_OFFSET_BYTES,
            )
            imageFile.seek(primaryOffset)
            imageFile.write(modified)
            return
        if corruptionKind == "reserved-byte":
            modified = bytearray(encodedSuperblock)
            modified[300] = 1
            _rewriteChecksum(
                modified,
                OS_ROOTFS_V5_SUPERBLOCK_CHECKSUM_OFFSET_BYTES,
            )
            imageFile.seek(primaryOffset)
            imageFile.write(modified)
            return
        if corruptionKind in ("descriptor-checksum", "group-overlap"):
            modified = bytearray(
                descriptorTable[:superblock.groupDescriptorSizeBytes]
            )
            if corruptionKind == "descriptor-checksum":
                modified[64] ^= 0x40
            else:
                dataStartOffset = OS_ROOTFS_V5_GROUP_OFFSETS["dataStartBlock"]
                _packU64(
                    modified,
                    dataStartOffset,
                    _unpackU64(modified, dataStartOffset) + 1,
                )
                _rewriteChecksum(
                    modified,
                    OS_ROOTFS_V5_GROUP_DESCRIPTOR_CHECKSUM_OFFSET_BYTES,
                )
            imageFile.seek(
                _rootfsV5AbsoluteOffset(
                    superblock,
                    superblock.groupDescriptorTableStartBlock,
                )
            )
            imageFile.write(modified)
            return
        if corruptionKind in (
            "block-bitmap-checksum",
            "inode-bitmap-checksum",
        ):
            targetBlock = (
                descriptors[0].blockBitmapBlock
                if corruptionKind == "block-bitmap-checksum"
                else descriptors[0].inodeBitmapBlock
            )
            block = bytearray(_readBlock(imageFile, superblock, targetBlock))
            block[0] ^= 0x80
            _writeBlock(imageFile, superblock, targetBlock, bytes(block))
            return
        if corruptionKind == "backup-superblock":
            backup = next(
                descriptor
                for descriptor in descriptors
                if descriptor.groupIndex != 0
                and descriptor.flags
                & OS_ROOTFS_V5_GROUP_FLAG_HAS_SUPERBLOCK_COPY
            )
            block = bytearray(
                _readBlock(
                    imageFile,
                    superblock,
                    backup.superblockCopyBlock,
                )
            )
            block[64] ^= 0x20
            _writeBlock(
                imageFile,
                superblock,
                backup.superblockCopyBlock,
                bytes(block),
            )
            return
        if corruptionKind == "backup-descriptor":
            backup = next(
                descriptor
                for descriptor in descriptors
                if descriptor.groupIndex != 0
                and descriptor.flags
                & OS_ROOTFS_V5_GROUP_FLAG_HAS_SUPERBLOCK_COPY
            )
            imageFile.seek(
                _rootfsV5AbsoluteOffset(
                    superblock,
                    backup.groupDescriptorCopyStartBlock,
                )
            )
            value = imageFile.read(1)
            imageFile.seek(-1, 1)
            imageFile.write(bytes([value[0] ^ 0x10]))
            return
        if corruptionKind == "root-inode-checksum":
            rootOffset = (
                _rootfsV5AbsoluteOffset(
                    superblock,
                    descriptors[0].inodeTableStartBlock,
                )
                + (superblock.rootInodeNumber - 1)
                * superblock.inodeSizeBytes
            )
            imageFile.seek(rootOffset + 64)
            value = imageFile.read(1)
            imageFile.seek(-1, 1)
            imageFile.write(bytes([value[0] ^ 0x08]))
            return
    raise ValueError("rootfs v5 corruption kind 未处理")

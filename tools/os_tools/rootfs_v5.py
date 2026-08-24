"""rootfs v5 block-group 盘面基础、格式化与只读一致性检查。"""

from dataclasses import asdict, dataclass
import json
from pathlib import Path
import struct
import time
import uuid


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


def calculateRootfsV5Crc32c(content: bytes | bytearray) -> int:
    crc = OS_ROOTFS_V5_CRC32C_MASK
    for value in content:
        crc ^= value
        for _ in range(OS_ROOTFS_V5_BITS_PER_BYTE):
            crc = (
                (crc >> 1) ^ OS_ROOTFS_V5_CRC32C_POLYNOMIAL
                if crc & 1
                else crc >> 1
            )
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
        "formatGeneration",
        "freeBlockCount",
        "freeInodeCount",
        "allocatedDirectoryCount",
    )
    if any(
        getattr(superblock, field) != getattr(expected, field)
        for field in comparableFields
    ):
        raise ValueError("rootfs v5 superblock geometry/count 不一致")


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
        "freeBlockCount",
        "freeInodeCount",
        "usedDirectoryCount",
        "metadataGeneration",
    )
    if any(
        getattr(descriptor, field) != getattr(expected, field)
        for field in fields
    ):
        raise ValueError("rootfs v5 group descriptor 几何无效")


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
        or inode.flags != 0
        or inode.sizeBytes != 0
        or inode.allocatedBlockCount != 0
        or any(inode.mappingRoot)
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


def _unpackU64(content: bytes, offset: int) -> int:
    return struct.unpack_from("<Q", content, offset)[0]


def _unpackU32(content: bytes, offset: int) -> int:
    return struct.unpack_from("<I", content, offset)[0]


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
            if not createImage:
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
    inspectRootfsV5(
        imagePath,
        fileSystemStartLba=selectedProfile.fileSystemStartLba,
        sectorSizeBytes=selectedProfile.sectorSizeBytes,
    )
    return superblock


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
        firstInodeBlock = b""
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
            if blockBitmap != buildInitialRootfsV5BlockBitmap(
                superblock,
                descriptor,
            ):
                raise ValueError("rootfs v5 block bitmap 与初始几何不符")
            if inodeBitmap != buildInitialRootfsV5InodeBitmap(
                superblock,
                descriptor,
            ):
                raise ValueError("rootfs v5 inode bitmap 与初始几何不符")
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
            if descriptor.groupIndex == 0:
                reservedBytes = (
                    superblock.reservedInodeCount * superblock.inodeSizeBytes
                )
                if any(inodeTable[reservedBytes:]):
                    raise ValueError("rootfs v5 group 0 未分配 inode 记录非零")
                firstInodeBlock = inodeTable[:superblock.blockSizeBytes]
            elif any(inodeTable):
                raise ValueError("rootfs v5 未分配 inode table 非零")
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
        if len(firstInodeBlock) != superblock.blockSizeBytes:
            raise ValueError("rootfs v5 root inode block 缺失")
        for inodeNumber in range(1, superblock.firstUserInodeNumber):
            offset = (inodeNumber - 1) * superblock.inodeSizeBytes
            inode = decodeRootfsV5Inode(
                superblock,
                firstInodeBlock[offset:offset + superblock.inodeSizeBytes],
            )
            if inode != _makeReservedInode(superblock, inodeNumber):
                raise ValueError("rootfs v5 初始 reserved/root inode 状态不一致")

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

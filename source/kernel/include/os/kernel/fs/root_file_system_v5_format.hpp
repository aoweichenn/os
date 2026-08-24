#pragma once

#include <os/abi/security.hpp>

#include <stdint.h>

namespace os::kernel::fs {

inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES = 4096ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_SECTOR_SIZE_BYTES = 512ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_FILE_SYSTEM_START_LBA = 32768ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_DEVICE_SECTOR_COUNT = 268435456ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_SECTORS_PER_BLOCK =
    OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES / OS_KERNEL_ROOTFS_V5_SECTOR_SIZE_BYTES;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_TOTAL_BLOCK_COUNT =
    (OS_KERNEL_ROOTFS_V5_DEVICE_SECTOR_COUNT - OS_KERNEL_ROOTFS_V5_FILE_SYSTEM_START_LBA) /
    OS_KERNEL_ROOTFS_V5_SECTORS_PER_BLOCK;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_BLOCKS_PER_GROUP = 32768ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_GROUP_COUNT =
    (OS_KERNEL_ROOTFS_V5_TOTAL_BLOCK_COUNT + OS_KERNEL_ROOTFS_V5_BLOCKS_PER_GROUP - 1ULL) /
    OS_KERNEL_ROOTFS_V5_BLOCKS_PER_GROUP;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_GROUP_DESCRIPTOR_SIZE_BYTES = 256ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_GROUP_DESCRIPTOR_TABLE_START_BLOCK = 1ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_GROUP_DESCRIPTOR_TABLE_BLOCK_COUNT =
    (OS_KERNEL_ROOTFS_V5_GROUP_COUNT * OS_KERNEL_ROOTFS_V5_GROUP_DESCRIPTOR_SIZE_BYTES +
     OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES - 1ULL) /
    OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_INODE_SIZE_BYTES = 256ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_INODES_PER_GROUP = 2048ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_INODE_COUNT =
    OS_KERNEL_ROOTFS_V5_GROUP_COUNT * OS_KERNEL_ROOTFS_V5_INODES_PER_GROUP;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_ROOT_INODE_NUMBER = 2ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_FIRST_USER_INODE_NUMBER = 16ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_RESERVED_INODE_COUNT =
    OS_KERNEL_ROOTFS_V5_FIRST_USER_INODE_NUMBER - 1ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_INODE_MAPPING_ROOT_SIZE_BYTES = 128ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_FORMAT_VERSION = 5ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_SUPERBLOCK_HEADER_SIZE_BYTES = 256ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_SUPERBLOCK_CHECKSUM_OFFSET_BYTES = 4092ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_GROUP_DESCRIPTOR_CHECKSUM_OFFSET_BYTES = 252ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_INODE_CHECKSUM_OFFSET_BYTES = 252ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_NO_BLOCK = UINT64_MAX;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_CRC32C_ALGORITHM = 1ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_SPARSE_BACKUP_POLICY = 1ULL;

inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_COMPAT_SPARSE_BACKUP = 1ULL << 0ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_REQUIRED_COMPAT_FEATURES =
    OS_KERNEL_ROOTFS_V5_COMPAT_SPARSE_BACKUP;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_READ_ONLY_COMPAT_METADATA_CRC32C = 1ULL << 0ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_SUPPORTED_READ_ONLY_COMPAT_FEATURES =
    OS_KERNEL_ROOTFS_V5_READ_ONLY_COMPAT_METADATA_CRC32C;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_INCOMPAT_64_BIT_GEOMETRY = 1ULL << 0ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_INCOMPAT_BLOCK_GROUPS = 1ULL << 1ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_INCOMPAT_BASE_INODE = 1ULL << 2ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_REQUIRED_INCOMPAT_FEATURES =
    OS_KERNEL_ROOTFS_V5_INCOMPAT_64_BIT_GEOMETRY | OS_KERNEL_ROOTFS_V5_INCOMPAT_BLOCK_GROUPS |
    OS_KERNEL_ROOTFS_V5_INCOMPAT_BASE_INODE;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_SUPPORTED_INCOMPAT_FEATURES =
    OS_KERNEL_ROOTFS_V5_REQUIRED_INCOMPAT_FEATURES;

inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_GROUP_FLAG_HAS_SUPERBLOCK_COPY = 1ULL << 0ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_GROUP_FLAG_BLOCK_BITMAP_INITIALIZED = 1ULL << 1ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_GROUP_FLAG_INODE_BITMAP_INITIALIZED = 1ULL << 2ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_GROUP_FLAG_INODE_TABLE_ZEROED = 1ULL << 3ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_REQUIRED_GROUP_FLAGS =
    OS_KERNEL_ROOTFS_V5_GROUP_FLAG_BLOCK_BITMAP_INITIALIZED |
    OS_KERNEL_ROOTFS_V5_GROUP_FLAG_INODE_BITMAP_INITIALIZED |
    OS_KERNEL_ROOTFS_V5_GROUP_FLAG_INODE_TABLE_ZEROED;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_SUPPORTED_GROUP_FLAGS =
    OS_KERNEL_ROOTFS_V5_REQUIRED_GROUP_FLAGS | OS_KERNEL_ROOTFS_V5_GROUP_FLAG_HAS_SUPERBLOCK_COPY;

static_assert(OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES % OS_KERNEL_ROOTFS_V5_SECTOR_SIZE_BYTES == 0ULL);
static_assert(OS_KERNEL_ROOTFS_V5_FILE_SYSTEM_START_LBA % OS_KERNEL_ROOTFS_V5_SECTORS_PER_BLOCK ==
              0ULL);
static_assert(OS_KERNEL_ROOTFS_V5_TOTAL_BLOCK_COUNT == 33550336ULL);
static_assert(OS_KERNEL_ROOTFS_V5_GROUP_COUNT == 1024ULL);
static_assert(OS_KERNEL_ROOTFS_V5_GROUP_DESCRIPTOR_TABLE_BLOCK_COUNT == 64ULL);
static_assert(OS_KERNEL_ROOTFS_V5_INODE_COUNT == 2097152ULL);
static_assert(OS_KERNEL_ROOTFS_V5_BLOCKS_PER_GROUP <= OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES * 8ULL);
static_assert(OS_KERNEL_ROOTFS_V5_INODES_PER_GROUP <= OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES * 8ULL);

enum class RootV5FileSystemState : uint64_t {
    Clean = 1ULL,
};

enum class RootV5NodeType : uint64_t {
    Unused,
    Reserved,
    RegularFile,
    Directory,
    SymbolicLink,
};

enum class RootV5FormatStatus : uint64_t {
    Succeeded,
    NullBuffer,
    InvalidBufferSize,
    InvalidMagic,
    InvalidVersion,
    InvalidHeaderSize,
    InvalidBlockSize,
    InvalidSectorSize,
    InvalidLayout,
    InvalidFeatures,
    UnsupportedReadOnlyFeature,
    UnsupportedRequiredFeature,
    InvalidChecksumAlgorithm,
    InvalidChecksum,
    InvalidState,
    InvalidGroup,
    InvalidInode,
    NonZeroReservedBytes,
    ArithmeticOverflow,
};

struct RootV5Uuid final {
    uint64_t low;
    uint64_t high;
};

struct RootV5FormatProfile final {
    uint64_t sector_size_bytes;
    uint64_t block_size_bytes;
    uint64_t file_system_start_lba;
    uint64_t device_sector_count;
    uint64_t blocks_per_group;
    uint64_t group_descriptor_size_bytes;
    uint64_t inode_size_bytes;
    uint64_t inodes_per_group;
    uint64_t creation_time_nanoseconds;
    RootV5Uuid uuid;
};

struct RootV5Superblock final {
    uint64_t version;
    uint64_t header_size_bytes;
    uint64_t block_size_bytes;
    uint64_t sector_size_bytes;
    uint64_t file_system_start_lba;
    uint64_t device_sector_count;
    uint64_t total_block_count;
    uint64_t blocks_per_group;
    uint64_t group_count;
    uint64_t group_descriptor_size_bytes;
    uint64_t group_descriptor_table_start_block;
    uint64_t group_descriptor_table_block_count;
    uint64_t inode_size_bytes;
    uint64_t inodes_per_group;
    uint64_t inode_count;
    uint64_t root_inode_number;
    uint64_t first_user_inode_number;
    uint64_t reserved_inode_count;
    RootV5FileSystemState state;
    uint64_t compatible_features;
    uint64_t read_only_compatible_features;
    uint64_t incompatible_features;
    uint64_t checksum_algorithm;
    uint64_t backup_policy;
    uint64_t creation_time_nanoseconds;
    uint64_t format_generation;
    uint64_t free_block_count;
    uint64_t free_inode_count;
    uint64_t allocated_directory_count;
    RootV5Uuid uuid;
};

struct RootV5GroupDescriptor final {
    uint64_t group_index;
    uint64_t first_block;
    uint64_t block_count;
    uint64_t flags;
    uint64_t superblock_copy_block;
    uint64_t group_descriptor_copy_start_block;
    uint64_t group_descriptor_copy_block_count;
    uint64_t block_bitmap_block;
    uint64_t inode_bitmap_block;
    uint64_t inode_table_start_block;
    uint64_t inode_table_block_count;
    uint64_t data_start_block;
    uint64_t data_block_count;
    uint64_t inode_start_number;
    uint64_t inode_count;
    uint64_t free_block_count;
    uint64_t free_inode_count;
    uint64_t used_directory_count;
    uint64_t metadata_generation;
    uint32_t block_bitmap_checksum;
    uint32_t inode_bitmap_checksum;
};

struct RootV5Inode final {
    uint64_t inode_number;
    uint64_t generation;
    RootV5NodeType type;
    uint64_t flags;
    uint64_t size_bytes;
    uint64_t allocated_block_count;
    uint64_t link_count;
    uint64_t parent_inode_number;
    uint64_t access_time_nanoseconds;
    uint64_t modification_time_nanoseconds;
    uint64_t change_time_nanoseconds;
    uint64_t birth_time_nanoseconds;
    os::abi::UserIdentifier owner_user_identifier;
    os::abi::GroupIdentifier owner_group_identifier;
    os::abi::FileMode mode;
    uint32_t project_identifier;
    uint8_t mapping_root[OS_KERNEL_ROOTFS_V5_INODE_MAPPING_ROOT_SIZE_BYTES];
};

[[nodiscard]] uint32_t CalculateRootV5Crc32c(const uint8_t *bytes, uint64_t length_bytes) noexcept;
[[nodiscard]] RootV5FormatProfile
MakeProductionRootV5FormatProfile(uint64_t creation_time_nanoseconds, RootV5Uuid uuid) noexcept;
[[nodiscard]] RootV5FormatStatus PlanRootV5Superblock(const RootV5FormatProfile &profile,
                                                      RootV5Superblock &superblock) noexcept;
[[nodiscard]] bool RootV5GroupHasSuperblockCopy(uint64_t group_index) noexcept;
[[nodiscard]] RootV5FormatStatus
BuildInitialRootV5GroupDescriptor(const RootV5Superblock &superblock, uint64_t group_index,
                                  RootV5GroupDescriptor &descriptor) noexcept;
[[nodiscard]] RootV5FormatStatus
ValidateRootV5Superblock(const RootV5Superblock &superblock) noexcept;
[[nodiscard]] RootV5FormatStatus
ValidateRootV5GroupDescriptor(const RootV5Superblock &superblock,
                              const RootV5GroupDescriptor &descriptor) noexcept;
[[nodiscard]] RootV5FormatStatus ValidateRootV5Inode(const RootV5Superblock &superblock,
                                                     const RootV5Inode &inode) noexcept;
[[nodiscard]] RootV5FormatStatus EncodeRootV5Superblock(const RootV5Superblock &superblock,
                                                        uint8_t *block,
                                                        uint64_t block_size_bytes) noexcept;
[[nodiscard]] RootV5FormatStatus DecodeRootV5Superblock(const uint8_t *block,
                                                        uint64_t block_size_bytes,
                                                        RootV5Superblock &superblock) noexcept;
[[nodiscard]] RootV5FormatStatus
EncodeRootV5GroupDescriptor(const RootV5Superblock &superblock,
                            const RootV5GroupDescriptor &descriptor, uint8_t *bytes,
                            uint64_t byte_count) noexcept;
[[nodiscard]] RootV5FormatStatus
DecodeRootV5GroupDescriptor(const RootV5Superblock &superblock, const uint8_t *bytes,
                            uint64_t byte_count, RootV5GroupDescriptor &descriptor) noexcept;
[[nodiscard]] RootV5FormatStatus EncodeRootV5Inode(const RootV5Superblock &superblock,
                                                   const RootV5Inode &inode, uint8_t *bytes,
                                                   uint64_t byte_count) noexcept;
[[nodiscard]] RootV5FormatStatus DecodeRootV5Inode(const RootV5Superblock &superblock,
                                                   const uint8_t *bytes, uint64_t byte_count,
                                                   RootV5Inode &inode) noexcept;
[[nodiscard]] bool RootV5BytesAreZero(const uint8_t *bytes, uint64_t byte_count) noexcept;

}

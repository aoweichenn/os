#pragma once

#include <stdint.h>

namespace os::kernel::fs {

inline constexpr uint64_t OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES = 512ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_START_LBA = 32768ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_DISK_BLOCK_COUNT = 268435456ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_TOTAL_BLOCK_COUNT =
    OS_KERNEL_ROOTFS_DISK_BLOCK_COUNT - OS_KERNEL_ROOTFS_START_LBA;
inline constexpr uint64_t OS_KERNEL_ROOTFS_REGION_SIZE_BYTES =
    OS_KERNEL_ROOTFS_TOTAL_BLOCK_COUNT * OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES;
inline constexpr uint64_t OS_KERNEL_ROOTFS_SUPERBLOCK_RELATIVE_BLOCK = 0ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_JOURNAL_START_RELATIVE_BLOCK = 1ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_JOURNAL_BLOCK_COUNT = 4096ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_INODE_BITMAP_START_RELATIVE_BLOCK = 4097ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_INODE_BITMAP_BLOCK_COUNT = 16ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_INODE_TABLE_START_RELATIVE_BLOCK = 4113ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_INODE_TABLE_BLOCK_COUNT = 32768ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_DATA_BITMAP_START_RELATIVE_BLOCK = 36881ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_DATA_BITMAP_BLOCK_COUNT = 65504ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_DATA_START_RELATIVE_BLOCK =
    OS_KERNEL_ROOTFS_DATA_BITMAP_START_RELATIVE_BLOCK + OS_KERNEL_ROOTFS_DATA_BITMAP_BLOCK_COUNT;
inline constexpr uint64_t OS_KERNEL_ROOTFS_DATA_BLOCK_COUNT =
    OS_KERNEL_ROOTFS_TOTAL_BLOCK_COUNT - OS_KERNEL_ROOTFS_DATA_START_RELATIVE_BLOCK;
inline constexpr uint64_t OS_KERNEL_ROOTFS_INODE_COUNT = 65536ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_ROOT_INODE_NUMBER = 1ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_INODE_SIZE_BYTES = 256ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_INODES_PER_BLOCK = 2ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_DIRECTORY_ENTRY_SIZE_BYTES = 320ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_MAXIMUM_NAME_LENGTH_BYTES = 255ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_MAXIMUM_SYMBOLIC_LINK_LENGTH_BYTES = 4096ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_NAME_STORAGE_SIZE_BYTES = 256ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_DIRECT_BLOCK_COUNT = 8ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK = 63ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_BITMAP_BITS_PER_BYTE = 8ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_MAXIMUM_FILE_SIZE_BYTES =
    OS_KERNEL_ROOTFS_DATA_BLOCK_COUNT * OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES;
inline constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_VERSION = 4ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_FEATURE_SPARSE_FILES = 1ULL << 0ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_FEATURE_CHECKSUMMED_POINTER_BLOCKS = 1ULL << 1ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_FEATURE_ORDERED_METADATA_JOURNAL = 1ULL << 2ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_FEATURE_64_BIT_GEOMETRY = 1ULL << 3ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_FEATURE_LINKS = 1ULL << 4ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_FEATURE_TIMESTAMPS = 1ULL << 5ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_FEATURE_ORPHAN_RECOVERY = 1ULL << 6ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_FEATURE_FIVE_LEVEL_BLOCK_TREE = 1ULL << 7ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_REQUIRED_FEATURES =
    OS_KERNEL_ROOTFS_FEATURE_SPARSE_FILES | OS_KERNEL_ROOTFS_FEATURE_CHECKSUMMED_POINTER_BLOCKS |
    OS_KERNEL_ROOTFS_FEATURE_ORDERED_METADATA_JOURNAL | OS_KERNEL_ROOTFS_FEATURE_64_BIT_GEOMETRY |
    OS_KERNEL_ROOTFS_FEATURE_LINKS | OS_KERNEL_ROOTFS_FEATURE_TIMESTAMPS |
    OS_KERNEL_ROOTFS_FEATURE_ORPHAN_RECOVERY | OS_KERNEL_ROOTFS_FEATURE_FIVE_LEVEL_BLOCK_TREE;
inline constexpr uint64_t OS_KERNEL_ROOTFS_INODE_FLAG_ORPHAN = 1ULL << 0ULL;

static_assert(OS_KERNEL_ROOTFS_JOURNAL_START_RELATIVE_BLOCK +
                  OS_KERNEL_ROOTFS_JOURNAL_BLOCK_COUNT ==
              OS_KERNEL_ROOTFS_INODE_BITMAP_START_RELATIVE_BLOCK);
static_assert(OS_KERNEL_ROOTFS_INODE_BITMAP_START_RELATIVE_BLOCK +
                  OS_KERNEL_ROOTFS_INODE_BITMAP_BLOCK_COUNT ==
              OS_KERNEL_ROOTFS_INODE_TABLE_START_RELATIVE_BLOCK);
static_assert(OS_KERNEL_ROOTFS_INODE_TABLE_START_RELATIVE_BLOCK +
                  OS_KERNEL_ROOTFS_INODE_TABLE_BLOCK_COUNT ==
              OS_KERNEL_ROOTFS_DATA_BITMAP_START_RELATIVE_BLOCK);
static_assert(OS_KERNEL_ROOTFS_DATA_BITMAP_START_RELATIVE_BLOCK +
                  OS_KERNEL_ROOTFS_DATA_BITMAP_BLOCK_COUNT ==
              OS_KERNEL_ROOTFS_DATA_START_RELATIVE_BLOCK);
static_assert(OS_KERNEL_ROOTFS_DATA_START_RELATIVE_BLOCK + OS_KERNEL_ROOTFS_DATA_BLOCK_COUNT ==
              OS_KERNEL_ROOTFS_TOTAL_BLOCK_COUNT);
static_assert(OS_KERNEL_ROOTFS_INODE_COUNT * OS_KERNEL_ROOTFS_INODE_SIZE_BYTES ==
              OS_KERNEL_ROOTFS_INODE_TABLE_BLOCK_COUNT * OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES);
static_assert(OS_KERNEL_ROOTFS_DATA_BLOCK_COUNT <= OS_KERNEL_ROOTFS_DATA_BITMAP_BLOCK_COUNT *
                                                       OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES *
                                                       OS_KERNEL_ROOTFS_BITMAP_BITS_PER_BYTE);
static_assert(
    (OS_KERNEL_ROOTFS_DIRECT_BLOCK_COUNT + OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK +
     OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK * OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK +
     OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK * OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK *
         OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK +
     OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK * OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK *
         OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK *
         OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK +
     OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK * OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK *
         OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK *
         OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK *
         OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK) *
        OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES >=
    OS_KERNEL_ROOTFS_MAXIMUM_FILE_SIZE_BYTES);

enum class RootNodeType : uint64_t {
    Unused,
    RegularFile,
    Directory,
    SymbolicLink,
};

enum class RootTransactionState : uint64_t {
    Clean = 1ULL,
    Dirty = 2ULL,
};

enum class RootFormatStatus : uint64_t {
    Succeeded,
    NullBuffer,
    InvalidBufferSize,
    InvalidMagic,
    InvalidVersion,
    InvalidLayout,
    InvalidTransactionState,
    InvalidChecksum,
    InvalidNodeType,
    InvalidInode,
    InvalidDirectoryEntry,
    InvalidPointerBlock,
    NonZeroReservedBytes,
};

struct RootSuperblock final {
    uint64_t version;
    uint64_t block_size_bytes;
    uint64_t total_block_count;
    uint64_t journal_start_relative_block;
    uint64_t journal_block_count;
    uint64_t inode_bitmap_start_relative_block;
    uint64_t inode_bitmap_block_count;
    uint64_t inode_table_start_relative_block;
    uint64_t inode_table_block_count;
    uint64_t data_bitmap_start_relative_block;
    uint64_t data_bitmap_block_count;
    uint64_t data_start_relative_block;
    uint64_t data_block_count;
    uint64_t inode_count;
    uint64_t root_inode_number;
    uint64_t maximum_file_size_bytes;
    RootTransactionState transaction_state;
    uint64_t transaction_generation;
    uint64_t next_inode_generation;
    uint64_t feature_flags;
    uint64_t allocated_inode_count;
    uint64_t allocated_data_block_count;
    uint64_t allocated_metadata_block_count;
};

struct RootInode final {
    RootNodeType type;
    uint64_t flags;
    uint64_t size_bytes;
    uint64_t generation;
    uint64_t link_count;
    uint64_t allocated_data_block_count;
    uint64_t allocated_metadata_block_count;
    uint64_t parent_inode_number;
    uint64_t direct_blocks[OS_KERNEL_ROOTFS_DIRECT_BLOCK_COUNT];
    uint64_t single_indirect_block;
    uint64_t double_indirect_block;
    uint64_t triple_indirect_block;
    uint64_t quadruple_indirect_block;
    uint64_t quintuple_indirect_block;
    uint64_t access_time_nanoseconds;
    uint64_t modification_time_nanoseconds;
    uint64_t change_time_nanoseconds;
    uint64_t birth_time_nanoseconds;
};

struct RootDirectoryEntry final {
    uint64_t inode_number;
    uint64_t inode_generation;
    RootNodeType type;
    uint64_t name_length_bytes;
    uint8_t name[OS_KERNEL_ROOTFS_NAME_STORAGE_SIZE_BYTES];
};

struct RootPointerBlock final {
    uint64_t pointers[OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK];
};

[[nodiscard]] uint32_t CalculateRootCrc32(const uint8_t *bytes, uint64_t length_bytes) noexcept;
[[nodiscard]] RootFormatStatus DecodeRootSuperblock(const uint8_t *block, uint64_t block_size_bytes,
                                                    RootSuperblock &superblock) noexcept;
[[nodiscard]] RootFormatStatus EncodeRootSuperblock(const RootSuperblock &superblock,
                                                    uint8_t *block,
                                                    uint64_t block_size_bytes) noexcept;
[[nodiscard]] RootFormatStatus DecodeRootInode(const uint8_t *bytes, uint64_t byte_count,
                                               RootInode &inode) noexcept;
[[nodiscard]] RootFormatStatus EncodeRootInode(const RootInode &inode, uint8_t *bytes,
                                               uint64_t byte_count) noexcept;
[[nodiscard]] RootFormatStatus DecodeRootDirectoryEntry(const uint8_t *bytes, uint64_t byte_count,
                                                        RootDirectoryEntry &entry) noexcept;
[[nodiscard]] RootFormatStatus EncodeRootDirectoryEntry(const RootDirectoryEntry &entry,
                                                        uint8_t *bytes,
                                                        uint64_t byte_count) noexcept;
[[nodiscard]] RootFormatStatus DecodeRootPointerBlock(const uint8_t *block,
                                                      uint64_t block_size_bytes,
                                                      RootPointerBlock &pointer_block) noexcept;
[[nodiscard]] RootFormatStatus EncodeRootPointerBlock(const RootPointerBlock &pointer_block,
                                                      uint8_t *block,
                                                      uint64_t block_size_bytes) noexcept;
[[nodiscard]] bool RootBlockIsZero(const uint8_t *block, uint64_t block_size_bytes) noexcept;

}

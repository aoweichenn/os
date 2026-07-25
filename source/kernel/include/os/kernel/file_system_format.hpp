#pragma once

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES = 512ULL;
inline constexpr uint64_t OS_KERNEL_FILE_SYSTEM_SINGLE_BLOCK_COUNT = 1ULL;
inline constexpr uint64_t OS_KERNEL_FILE_SYSTEM_START_LBA = 2048ULL;
inline constexpr uint64_t OS_KERNEL_FILE_SYSTEM_TOTAL_BLOCK_COUNT = 1024ULL;
inline constexpr uint64_t OS_KERNEL_FILE_SYSTEM_END_LBA =
    OS_KERNEL_FILE_SYSTEM_START_LBA + OS_KERNEL_FILE_SYSTEM_TOTAL_BLOCK_COUNT;
inline constexpr uint64_t OS_KERNEL_FILE_SYSTEM_SUPERBLOCK_RELATIVE_BLOCK = 0ULL;
inline constexpr uint64_t OS_KERNEL_FILE_SYSTEM_INODE_BITMAP_RELATIVE_BLOCK = 1ULL;
inline constexpr uint64_t OS_KERNEL_FILE_SYSTEM_INODE_TABLE_START_RELATIVE_BLOCK = 2ULL;
inline constexpr uint64_t OS_KERNEL_FILE_SYSTEM_INODE_TABLE_BLOCK_COUNT = 8ULL;
inline constexpr uint64_t OS_KERNEL_FILE_SYSTEM_DATA_BITMAP_RELATIVE_BLOCK = 10ULL;
inline constexpr uint64_t OS_KERNEL_FILE_SYSTEM_DATA_START_RELATIVE_BLOCK = 11ULL;
inline constexpr uint64_t OS_KERNEL_FILE_SYSTEM_DATA_BLOCK_COUNT = 1013ULL;
inline constexpr uint64_t OS_KERNEL_FILE_SYSTEM_INODE_COUNT = 32ULL;
inline constexpr uint64_t OS_KERNEL_FILE_SYSTEM_ROOT_INODE_NUMBER = 1ULL;
inline constexpr uint64_t OS_KERNEL_FILE_SYSTEM_INODE_SIZE_BYTES = 128ULL;
inline constexpr uint64_t OS_KERNEL_FILE_SYSTEM_INODES_PER_BLOCK = 4ULL;
inline constexpr uint64_t OS_KERNEL_FILE_SYSTEM_DIRECT_BLOCK_COUNT = 10ULL;
inline constexpr uint64_t OS_KERNEL_FILE_SYSTEM_MAXIMUM_FILE_SIZE_BYTES =
    OS_KERNEL_FILE_SYSTEM_DIRECT_BLOCK_COUNT * OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES;
inline constexpr uint64_t OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES = 64ULL;
inline constexpr uint64_t OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRIES_PER_BLOCK = 8ULL;
inline constexpr uint64_t OS_KERNEL_FILE_SYSTEM_MAXIMUM_NAME_LENGTH_BYTES = 40ULL;
inline constexpr uint64_t OS_KERNEL_FILE_SYSTEM_MAXIMUM_PATH_LENGTH_BYTES = 128ULL;
inline constexpr uint64_t OS_KERNEL_FILE_SYSTEM_FORMAT_VERSION = 1ULL;

static_assert(OS_KERNEL_FILE_SYSTEM_INODE_TABLE_START_RELATIVE_BLOCK +
                  OS_KERNEL_FILE_SYSTEM_INODE_TABLE_BLOCK_COUNT ==
              OS_KERNEL_FILE_SYSTEM_DATA_BITMAP_RELATIVE_BLOCK);
static_assert(OS_KERNEL_FILE_SYSTEM_DATA_BITMAP_RELATIVE_BLOCK +
                  OS_KERNEL_FILE_SYSTEM_SINGLE_BLOCK_COUNT ==
              OS_KERNEL_FILE_SYSTEM_DATA_START_RELATIVE_BLOCK);
static_assert(OS_KERNEL_FILE_SYSTEM_DATA_START_RELATIVE_BLOCK +
                  OS_KERNEL_FILE_SYSTEM_DATA_BLOCK_COUNT ==
              OS_KERNEL_FILE_SYSTEM_TOTAL_BLOCK_COUNT);
static_assert(OS_KERNEL_FILE_SYSTEM_INODE_COUNT * OS_KERNEL_FILE_SYSTEM_INODE_SIZE_BYTES ==
              OS_KERNEL_FILE_SYSTEM_INODE_TABLE_BLOCK_COUNT *
                  OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES);
static_assert(OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES *
                  OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRIES_PER_BLOCK ==
              OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES);

enum class FileSystemNodeType : uint64_t {
    Unused,
    RegularFile,
    Directory,
};

enum class FileSystemTransactionState : uint64_t {
    Clean = 1ULL,
    Dirty = 2ULL,
};

enum class FileSystemFormatStatus : uint64_t {
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
};

struct FileSystemSuperblock final {
    uint64_t version;
    uint64_t block_size_bytes;
    uint64_t total_block_count;
    uint64_t inode_bitmap_relative_block;
    uint64_t inode_table_start_relative_block;
    uint64_t inode_table_block_count;
    uint64_t data_bitmap_relative_block;
    uint64_t data_start_relative_block;
    uint64_t inode_count;
    uint64_t data_block_count;
    uint64_t root_inode_number;
    FileSystemTransactionState transaction_state;
    uint64_t transaction_generation;
};

struct FileSystemInode final {
    FileSystemNodeType type;
    uint64_t size_bytes;
    uint64_t generation;
    uint64_t link_count;
    uint64_t allocated_block_count;
    uint64_t direct_blocks[OS_KERNEL_FILE_SYSTEM_DIRECT_BLOCK_COUNT];
};

struct FileSystemDirectoryEntry final {
    uint64_t inode_number;
    FileSystemNodeType type;
    uint64_t name_length_bytes;
    uint8_t name[OS_KERNEL_FILE_SYSTEM_MAXIMUM_NAME_LENGTH_BYTES];
};

[[nodiscard]] uint32_t CalculateFileSystemCrc32(const uint8_t *bytes,
                                                uint64_t length_bytes) noexcept;
[[nodiscard]] bool FileSystemBlockIsZero(const uint8_t *block, uint64_t block_size_bytes) noexcept;
[[nodiscard]] FileSystemSuperblock CreateFileSystemSuperblock() noexcept;
[[nodiscard]] FileSystemFormatStatus
EncodeFileSystemSuperblock(const FileSystemSuperblock &superblock, uint8_t *block,
                           uint64_t block_size_bytes) noexcept;
[[nodiscard]] FileSystemFormatStatus
DecodeFileSystemSuperblock(const uint8_t *block, uint64_t block_size_bytes,
                           FileSystemSuperblock &superblock) noexcept;
[[nodiscard]] FileSystemFormatStatus
EncodeFileSystemInode(const FileSystemInode &inode, uint8_t *bytes, uint64_t byte_count) noexcept;
[[nodiscard]] FileSystemFormatStatus
DecodeFileSystemInode(const uint8_t *bytes, uint64_t byte_count, FileSystemInode &inode) noexcept;
[[nodiscard]] FileSystemFormatStatus
EncodeFileSystemDirectoryEntry(const FileSystemDirectoryEntry &entry, uint8_t *bytes,
                               uint64_t byte_count) noexcept;
[[nodiscard]] FileSystemFormatStatus
DecodeFileSystemDirectoryEntry(const uint8_t *bytes, uint64_t byte_count,
                               FileSystemDirectoryEntry &entry) noexcept;
}

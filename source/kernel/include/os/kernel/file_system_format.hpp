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
static_assert(OS_KERNEL_FILE_SYSTEM_INODE_COUNT *
                  OS_KERNEL_FILE_SYSTEM_INODE_SIZE_BYTES ==
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
    uint64_t blockSizeBytes;
    uint64_t totalBlockCount;
    uint64_t inodeBitmapRelativeBlock;
    uint64_t inodeTableStartRelativeBlock;
    uint64_t inodeTableBlockCount;
    uint64_t dataBitmapRelativeBlock;
    uint64_t dataStartRelativeBlock;
    uint64_t inodeCount;
    uint64_t dataBlockCount;
    uint64_t rootInodeNumber;
    FileSystemTransactionState transactionState;
    uint64_t transactionGeneration;
};

struct FileSystemInode final {
    FileSystemNodeType type;
    uint64_t sizeBytes;
    uint64_t generation;
    uint64_t linkCount;
    uint64_t allocatedBlockCount;
    uint64_t directBlocks[OS_KERNEL_FILE_SYSTEM_DIRECT_BLOCK_COUNT];
};

struct FileSystemDirectoryEntry final {
    uint64_t inodeNumber;
    FileSystemNodeType type;
    uint64_t nameLengthBytes;
    uint8_t name[OS_KERNEL_FILE_SYSTEM_MAXIMUM_NAME_LENGTH_BYTES];
};

[[nodiscard]] uint32_t CalculateFileSystemCrc32(const uint8_t *bytes,
                                                uint64_t lengthBytes) noexcept;
[[nodiscard]] bool FileSystemBlockIsZero(const uint8_t *block,
                                         uint64_t blockSizeBytes) noexcept;
[[nodiscard]] FileSystemSuperblock CreateFileSystemSuperblock() noexcept;
[[nodiscard]] FileSystemFormatStatus
EncodeFileSystemSuperblock(const FileSystemSuperblock &superblock, uint8_t *block,
                           uint64_t blockSizeBytes) noexcept;
[[nodiscard]] FileSystemFormatStatus
DecodeFileSystemSuperblock(const uint8_t *block, uint64_t blockSizeBytes,
                           FileSystemSuperblock &superblock) noexcept;
[[nodiscard]] FileSystemFormatStatus
EncodeFileSystemInode(const FileSystemInode &inode, uint8_t *bytes,
                      uint64_t byteCount) noexcept;
[[nodiscard]] FileSystemFormatStatus
DecodeFileSystemInode(const uint8_t *bytes, uint64_t byteCount,
                      FileSystemInode &inode) noexcept;
[[nodiscard]] FileSystemFormatStatus
EncodeFileSystemDirectoryEntry(const FileSystemDirectoryEntry &entry, uint8_t *bytes,
                               uint64_t byteCount) noexcept;
[[nodiscard]] FileSystemFormatStatus
DecodeFileSystemDirectoryEntry(const uint8_t *bytes, uint64_t byteCount,
                               FileSystemDirectoryEntry &entry) noexcept;

}

#pragma once

#include "os/kernel/fs/block_cache.hpp"
#include "os/kernel/fs/file_system_format.hpp"
#include "os/kernel/sync/spin_lock.hpp"

#include <stdint.h>

namespace os::kernel::fs {
class LegacyFileSystem;
}

namespace os::kernel {

struct FileSystemOpenOptions final {
    bool readable;
    bool writable;
    bool create;
    bool truncate;
};

struct FileSystemHandle final {
    uint64_t inode_number;
    uint64_t offset_bytes;
    FileSystemNodeType node_type;
    bool readable;
    bool writable;
    bool open;
};

enum class FileSystemStatus : uint64_t {
    Succeeded,
    NotInitialized,
    AlreadyInitialized,
    InvalidArgument,
    InvalidPath,
    PathTooLong,
    NameTooLong,
    NotFound,
    AlreadyExists,
    NotDirectory,
    IsDirectory,
    PermissionDenied,
    InvalidHandle,
    InodeCapacityExhausted,
    DataCapacityExhausted,
    DirectoryCapacityExhausted,
    FileTooLarge,
    Corrupt,
    IncompleteTransaction,
    DeviceFailure,
    ReadOnly,
    MountCapacityExhausted,
    LoopDetected,
    Unsupported,
};

struct FileSystemStatistics final {
    BlockCacheStatistics cache;
    uint64_t transaction_generation;
    uint64_t allocated_inode_count;
    uint64_t allocated_data_block_count;
    uint64_t mounted_file_count;
    uint64_t mounted_directory_count;
    uint64_t bytes_read;
    uint64_t bytes_written;
    bool formatted_during_mount;
};

class FileSystem final {
  public:
    FileSystem() noexcept = default;

    [[nodiscard]] FileSystemStatus MountOrFormat(FileSystemBlockDevice &device,
                                                 bool &formatted) noexcept;
    [[nodiscard]] FileSystemStatus CreateDirectory(const uint8_t *path,
                                                   uint64_t path_length_bytes) noexcept;
    [[nodiscard]] FileSystemStatus Open(const uint8_t *path, uint64_t path_length_bytes,
                                        const FileSystemOpenOptions &options,
                                        FileSystemHandle &handle) noexcept;
    [[nodiscard]] FileSystemStatus OpenDirectory(const uint8_t *path, uint64_t path_length_bytes,
                                                 FileSystemHandle &handle) noexcept;
    [[nodiscard]] FileSystemStatus ReadDirectory(FileSystemHandle &handle,
                                                 FileSystemDirectoryEntry &entry,
                                                 bool &end_of_directory) noexcept;
    [[nodiscard]] FileSystemStatus Read(FileSystemHandle &handle, uint8_t *destination,
                                        uint64_t capacity_bytes, uint64_t &read_bytes) noexcept;
    [[nodiscard]] FileSystemStatus Write(FileSystemHandle &handle, const uint8_t *source,
                                         uint64_t length_bytes, uint64_t &written_bytes) noexcept;
    [[nodiscard]] FileSystemStatus Close(FileSystemHandle &handle) noexcept;
    [[nodiscard]] FileSystemStatus Sync() noexcept;
    [[nodiscard]] FileSystemStatus CheckConsistency() noexcept;
    [[nodiscard]] FileSystemStatistics Statistics() const noexcept;

  private:
    friend class fs::LegacyFileSystem;

    struct PathComponent final {
        uint8_t bytes[OS_KERNEL_FILE_SYSTEM_MAXIMUM_NAME_LENGTH_BYTES];
        uint64_t length_bytes;
    };

    struct DirectoryEntryLocation final {
        uint64_t entry_index;
        FileSystemDirectoryEntry entry;
    };

    [[nodiscard]] FileSystemStatus Format(FileSystemBlockDevice &device) noexcept;
    [[nodiscard]] FileSystemStatus BeginTransaction() noexcept;
    [[nodiscard]] FileSystemStatus CommitTransaction() noexcept;
    [[nodiscard]] FileSystemStatus FailDeviceOperation() noexcept;
    [[nodiscard]] FileSystemStatus WriteSuperblockDirect() noexcept;
    [[nodiscard]] FileSystemStatus ReadRelativeBlock(uint64_t relative_block,
                                                     uint8_t *block) noexcept;
    [[nodiscard]] FileSystemStatus WriteRelativeBlock(uint64_t relative_block,
                                                      const uint8_t *block) noexcept;
    [[nodiscard]] FileSystemStatus ReadInode(uint64_t inode_number,
                                             FileSystemInode &inode) noexcept;
    [[nodiscard]] FileSystemStatus WriteInode(uint64_t inode_number,
                                              const FileSystemInode &inode) noexcept;
    [[nodiscard]] FileSystemStatus ReadBitmap(bool inode_bitmap, uint8_t *block) noexcept;
    [[nodiscard]] FileSystemStatus WriteBitmap(bool inode_bitmap, const uint8_t *block) noexcept;
    [[nodiscard]] bool BitmapBitIsSet(const uint8_t *bitmap, uint64_t bit_index) const noexcept;
    void SetBitmapBit(uint8_t *bitmap, uint64_t bit_index, bool allocated) const noexcept;
    [[nodiscard]] FileSystemStatus FindFreeBitmapBit(const uint8_t *bitmap, uint64_t first_bit,
                                                     uint64_t bit_count,
                                                     uint64_t &bit_index) const noexcept;
    [[nodiscard]] FileSystemStatus AllocateInode(uint64_t &inode_number) noexcept;
    [[nodiscard]] FileSystemStatus AllocateDataBlock(uint64_t &relative_block) noexcept;
    [[nodiscard]] FileSystemStatus ReleaseInodeDataBlocks(FileSystemInode &inode) noexcept;
    [[nodiscard]] FileSystemStatus ParsePath(const uint8_t *path, uint64_t path_length_bytes,
                                             PathComponent *components, uint64_t component_capacity,
                                             uint64_t &component_count) const noexcept;
    [[nodiscard]] FileSystemStatus FindDirectoryEntry(const FileSystemInode &directory,
                                                      const PathComponent &name,
                                                      DirectoryEntryLocation &location) noexcept;
    [[nodiscard]] FileSystemStatus ResolvePath(const PathComponent *components,
                                               uint64_t component_count, uint64_t &inode_number,
                                               FileSystemInode &inode) noexcept;
    [[nodiscard]] FileSystemStatus ResolveParent(const PathComponent *components,
                                                 uint64_t component_count,
                                                 uint64_t &parent_inode_number,
                                                 FileSystemInode &parent_inode) noexcept;
    [[nodiscard]] FileSystemStatus
    AppendDirectoryEntry(uint64_t directory_inode_number, FileSystemInode &directory,
                         const FileSystemDirectoryEntry &entry) noexcept;
    [[nodiscard]] FileSystemStatus
    ReadDirectoryEntryAt(const FileSystemInode &directory, uint64_t entry_index,
                         FileSystemDirectoryEntry &entry) noexcept;
    [[nodiscard]] FileSystemStatus
    FindParentNode(uint64_t child_inode_number, uint64_t &parent_inode_number,
                   FileSystemInode &parent_inode, PathComponent &child_name) noexcept;
    [[nodiscard]] FileSystemStatus
    CreateChildNode(uint64_t parent_inode_number, FileSystemInode &parent_inode,
                    const PathComponent &name, FileSystemNodeType type,
                    uint64_t &inode_number) noexcept;
    [[nodiscard]] FileSystemStatus CreateNode(const PathComponent *components,
                                              uint64_t component_count, FileSystemNodeType type,
                                              uint64_t &inode_number) noexcept;
    [[nodiscard]] FileSystemStatus ReadFileBytes(const FileSystemInode &inode,
                                                 uint64_t offset_bytes, uint8_t *destination,
                                                 uint64_t capacity_bytes,
                                                 uint64_t &read_bytes) noexcept;
    [[nodiscard]] FileSystemStatus WriteFileBytes(uint64_t inode_number, FileSystemInode &inode,
                                                  uint64_t offset_bytes, const uint8_t *source,
                                                  uint64_t length_bytes,
                                                  uint64_t &written_bytes) noexcept;
    [[nodiscard]] FileSystemStatus
    ValidateAllocatedInode(uint64_t inode_number, const uint8_t *inode_bitmap,
                           const uint8_t *data_bitmap, uint8_t *seen_data_bitmap,
                           uint64_t &file_count, uint64_t &directory_count) noexcept;
    [[nodiscard]] FileSystemStatus CheckConsistencyUnlocked() noexcept;

    FileSystemBlockDevice *device_{nullptr};
    BlockCache cache_{};
    FileSystemSuperblock superblock_{};
    FileSystemStatistics statistics_{};
    mutable SpinLock lock_{};
    bool initialized_{false};
    bool failed_{false};
};

}

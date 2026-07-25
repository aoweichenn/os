#pragma once

#include "os/kernel/block_cache.hpp"
#include "os/kernel/file_system_format.hpp"
#include "os/kernel/spin_lock.hpp"

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_FILE_SYSTEM_PROCESS_DESCRIPTOR_COUNT = 4ULL;

struct FileSystemOpenOptions final {
    bool readable;
    bool writable;
    bool create;
    bool truncate;
};

struct FileSystemHandle final {
    uint64_t inodeNumber;
    uint64_t offsetBytes;
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
};

struct FileSystemStatistics final {
    BlockCacheStatistics cache;
    uint64_t transactionGeneration;
    uint64_t allocatedInodeCount;
    uint64_t allocatedDataBlockCount;
    uint64_t mountedFileCount;
    uint64_t mountedDirectoryCount;
    uint64_t bytesRead;
    uint64_t bytesWritten;
    bool formattedDuringMount;
};

class FileSystem final {
  public:
    FileSystem() noexcept = default;

    [[nodiscard]] FileSystemStatus MountOrFormat(FileSystemBlockDevice &device,
                                                 bool &formatted) noexcept;
    [[nodiscard]] FileSystemStatus CreateDirectory(const uint8_t *path,
                                                   uint64_t pathLengthBytes) noexcept;
    [[nodiscard]] FileSystemStatus Open(const uint8_t *path, uint64_t pathLengthBytes,
                                        const FileSystemOpenOptions &options,
                                        FileSystemHandle &handle) noexcept;
    [[nodiscard]] FileSystemStatus Read(FileSystemHandle &handle, uint8_t *destination,
                                        uint64_t capacityBytes, uint64_t &readBytes) noexcept;
    [[nodiscard]] FileSystemStatus Write(FileSystemHandle &handle, const uint8_t *source,
                                         uint64_t lengthBytes,
                                         uint64_t &writtenBytes) noexcept;
    [[nodiscard]] FileSystemStatus Close(FileSystemHandle &handle) noexcept;
    [[nodiscard]] FileSystemStatus Sync() noexcept;
    [[nodiscard]] FileSystemStatus CheckConsistency() noexcept;
    [[nodiscard]] FileSystemStatistics Statistics() const noexcept;

  private:
    struct PathComponent final {
        uint8_t bytes[OS_KERNEL_FILE_SYSTEM_MAXIMUM_NAME_LENGTH_BYTES];
        uint64_t lengthBytes;
    };

    struct DirectoryEntryLocation final {
        uint64_t entryIndex;
        FileSystemDirectoryEntry entry;
    };

    [[nodiscard]] FileSystemStatus Format(FileSystemBlockDevice &device) noexcept;
    [[nodiscard]] FileSystemStatus BeginTransaction() noexcept;
    [[nodiscard]] FileSystemStatus CommitTransaction() noexcept;
    [[nodiscard]] FileSystemStatus FailDeviceOperation() noexcept;
    [[nodiscard]] FileSystemStatus WriteSuperblockDirect() noexcept;
    [[nodiscard]] FileSystemStatus ReadRelativeBlock(uint64_t relativeBlock,
                                                     uint8_t *block) noexcept;
    [[nodiscard]] FileSystemStatus WriteRelativeBlock(uint64_t relativeBlock,
                                                      const uint8_t *block) noexcept;
    [[nodiscard]] FileSystemStatus ReadInode(uint64_t inodeNumber,
                                            FileSystemInode &inode) noexcept;
    [[nodiscard]] FileSystemStatus WriteInode(uint64_t inodeNumber,
                                             const FileSystemInode &inode) noexcept;
    [[nodiscard]] FileSystemStatus ReadBitmap(bool inodeBitmap, uint8_t *block) noexcept;
    [[nodiscard]] FileSystemStatus WriteBitmap(bool inodeBitmap,
                                               const uint8_t *block) noexcept;
    [[nodiscard]] bool BitmapBitIsSet(const uint8_t *bitmap, uint64_t bitIndex) const noexcept;
    void SetBitmapBit(uint8_t *bitmap, uint64_t bitIndex, bool allocated) const noexcept;
    [[nodiscard]] FileSystemStatus FindFreeBitmapBit(const uint8_t *bitmap,
                                                     uint64_t firstBit,
                                                     uint64_t bitCount,
                                                     uint64_t &bitIndex) const noexcept;
    [[nodiscard]] FileSystemStatus AllocateInode(uint64_t &inodeNumber) noexcept;
    [[nodiscard]] FileSystemStatus AllocateDataBlock(uint64_t &relativeBlock) noexcept;
    [[nodiscard]] FileSystemStatus ReleaseInodeDataBlocks(FileSystemInode &inode) noexcept;
    [[nodiscard]] FileSystemStatus ParsePath(const uint8_t *path, uint64_t pathLengthBytes,
                                             PathComponent *components,
                                             uint64_t componentCapacity,
                                             uint64_t &componentCount) const noexcept;
    [[nodiscard]] FileSystemStatus FindDirectoryEntry(
        const FileSystemInode &directory, const PathComponent &name,
        DirectoryEntryLocation &location) noexcept;
    [[nodiscard]] FileSystemStatus ResolvePath(const PathComponent *components,
                                               uint64_t componentCount,
                                               uint64_t &inodeNumber,
                                               FileSystemInode &inode) noexcept;
    [[nodiscard]] FileSystemStatus ResolveParent(const PathComponent *components,
                                                 uint64_t componentCount,
                                                 uint64_t &parentInodeNumber,
                                                 FileSystemInode &parentInode) noexcept;
    [[nodiscard]] FileSystemStatus AppendDirectoryEntry(
        uint64_t directoryInodeNumber, FileSystemInode &directory,
        const FileSystemDirectoryEntry &entry) noexcept;
    [[nodiscard]] FileSystemStatus CreateNode(const PathComponent *components,
                                              uint64_t componentCount,
                                              FileSystemNodeType type,
                                              uint64_t &inodeNumber) noexcept;
    [[nodiscard]] FileSystemStatus ReadFileBytes(const FileSystemInode &inode,
                                                 uint64_t offsetBytes,
                                                 uint8_t *destination,
                                                 uint64_t capacityBytes,
                                                 uint64_t &readBytes) noexcept;
    [[nodiscard]] FileSystemStatus WriteFileBytes(uint64_t inodeNumber,
                                                  FileSystemInode &inode,
                                                  uint64_t offsetBytes,
                                                  const uint8_t *source,
                                                  uint64_t lengthBytes,
                                                  uint64_t &writtenBytes) noexcept;
    [[nodiscard]] FileSystemStatus ValidateAllocatedInode(
        uint64_t inodeNumber, const uint8_t *inodeBitmap, const uint8_t *dataBitmap,
        uint8_t *seenDataBitmap, uint64_t &fileCount,
        uint64_t &directoryCount) noexcept;
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

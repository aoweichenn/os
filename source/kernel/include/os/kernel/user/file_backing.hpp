#pragma once

#include <os/kernel/fs/vfs.hpp>
#include <os/kernel/memory/file_page_cache.hpp>
#include <os/kernel/sync/spin_lock.hpp>

#include <stdint.h>

namespace os::kernel {

using UserFileBackingWritebackRequiredOperation = bool (*)(
    void *context, const FileIdentity &identity, bool &writeback_required) noexcept;

enum class UserFileBackingKind : uint64_t {
    None,
    MemoryImage,
    VfsFile,
    VfsWriteback,
};

struct UserFileBackingDescriptor final {
    UserFileBackingKind kind;
    uint64_t generation;
    uint64_t owner_identifier;
    FileIdentity identity;
    uint64_t size_bytes;
    const uint8_t *memory_image;
    fs::Vfs *vfs;
    fs::OpenFile open_file;
    bool active;
};

enum class UserFileBackingStatus : uint64_t {
    Succeeded,
    NotInitialized,
    AlreadyInitialized,
    InvalidStorage,
    InvalidCapacity,
    InvalidSource,
    CapacityExhausted,
    InvalidDescriptor,
    OwnershipMismatch,
    ReadFailed,
    WriteFailed,
    CloseFailed,
    Corrupt,
};

class UserFileBackingManager final {
  public:
    UserFileBackingManager() noexcept = default;
    UserFileBackingManager(const UserFileBackingManager &) = delete;
    UserFileBackingManager &operator=(const UserFileBackingManager &) = delete;

    [[nodiscard]] UserFileBackingStatus
    Initialize(UserFileBackingDescriptor *descriptors,
               uint64_t capacity) noexcept;
    [[nodiscard]] UserFileBackingStatus
    AcquireMemoryImage(uint64_t owner_identifier, const uint8_t *image,
                       uint64_t image_size_bytes, uint64_t &descriptor_index,
                       uint64_t &generation) noexcept;
    [[nodiscard]] UserFileBackingStatus
    AcquireVfsFile(uint64_t owner_identifier, fs::Vfs &vfs,
                   const fs::OpenFile &open_file, uint64_t &descriptor_index,
                   uint64_t &generation) noexcept;
    [[nodiscard]] UserFileBackingStatus
    Clone(uint64_t owner_identifier, uint64_t source_descriptor_index,
          uint64_t source_generation, uint64_t &descriptor_index,
          uint64_t &generation) noexcept;
    [[nodiscard]] UserFileBackingStatus
    Release(uint64_t owner_identifier, uint64_t descriptor_index,
            uint64_t generation) noexcept;
    [[nodiscard]] UserFileBackingStatus
    Read(uint64_t descriptor_index, uint64_t generation,
         uint64_t offset_bytes, uint8_t *destination,
         uint64_t length_bytes) noexcept;
    [[nodiscard]] UserFileBackingStatus
    WritePage(const FilePageIdentity &identity, const uint8_t *source,
              uint64_t length_bytes) noexcept;
    [[nodiscard]] UserFileBackingStatus RetainWritebackFile(fs::Vfs &vfs,
                                                            const fs::OpenFile &open_file,
                                                            uint64_t size_bytes) noexcept;
    [[nodiscard]] UserFileBackingStatus
    ReleaseCleanWritebackFiles(void *context,
                               UserFileBackingWritebackRequiredOperation operation) noexcept;
    [[nodiscard]] UserFileBackingStatus
    ReadDescriptor(uint64_t descriptor_index, uint64_t generation,
                   UserFileBackingDescriptor &descriptor) const noexcept;
    [[nodiscard]] UserFileBackingStatus
    UpdateFileSize(const FileIdentity &identity,
                   uint64_t size_bytes) noexcept;
    [[nodiscard]] UserFileBackingStatus Validate() const noexcept;
    [[nodiscard]] uint64_t ActiveDescriptorCount() const noexcept;
    [[nodiscard]] fs::Status LastCloseStatus() const noexcept;

  private:
    [[nodiscard]] bool IsIndexValid(uint64_t descriptor_index) const noexcept;
    [[nodiscard]] bool IdentitiesEqual(const FileIdentity &left,
                                       const FileIdentity &right) const noexcept;
    [[nodiscard]] uint64_t NextGeneration() noexcept;

    UserFileBackingDescriptor *descriptors_{nullptr};
    uint64_t capacity_{};
    uint64_t active_descriptor_count_{};
    uint64_t next_generation_{};
    fs::Status last_close_status_{fs::Status::Succeeded};
    mutable SpinLock lock_{};
    bool initialized_{};
};

[[nodiscard]] bool ReadUserFileBackingPage(
    void *context, const FilePageIdentity &identity, uint8_t *destination,
    uint64_t capacity_bytes) noexcept;
[[nodiscard]] bool WriteUserFileBackingPage(
    void *context, const FilePageIdentity &identity, const uint8_t *source,
    uint64_t length_bytes) noexcept;

}

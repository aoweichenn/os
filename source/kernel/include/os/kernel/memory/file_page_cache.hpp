#pragma once

#include <os/kernel/memory/physical_frame_allocator.hpp>
#include <os/kernel/sync/spin_lock.hpp>

#include <stdint.h>

namespace os::kernel {

struct FileIdentity final {
    uint64_t superblock_identifier;
    uint64_t superblock_generation;
    uint64_t node_identifier;
    uint64_t node_generation;
};

struct FilePageIdentity final {
    FileIdentity file;
    uint64_t page_index;
};

using FilePageAccessOperation = uint8_t *(*)(void *context, uint64_t physical_address) noexcept;
using FilePageReadOperation = bool (*)(void *context, const FilePageIdentity &identity,
                                       uint8_t *destination, uint64_t capacity_bytes) noexcept;
using FilePageWriteOperation = bool (*)(void *context, const FilePageIdentity &identity,
                                        const uint8_t *source, uint64_t length_bytes) noexcept;

enum class FilePageCacheEntryState : uint64_t {
    Empty,
    Clean,
    Dirty,
    Writeback,
    Error,
};

struct FilePageCacheEntry final {
    FilePageIdentity identity;
    uint64_t physical_address;
    uint64_t mapping_reference_count;
    uint64_t access_generation;
    FilePageCacheEntryState state;
};

struct FilePageCacheStatistics final {
    uint64_t capacity;
    uint64_t resident_page_count;
    uint64_t referenced_page_count;
    uint64_t active_mapping_reference_count;
    uint64_t peak_resident_page_count;
    uint64_t hit_count;
    uint64_t miss_count;
    uint64_t successful_load_count;
    uint64_t failed_load_count;
    uint64_t eviction_count;
    uint64_t invalidation_count;
    uint64_t successful_acquire_count;
    uint64_t release_count;
    uint64_t dirty_page_limit;
    uint64_t dirty_page_count;
    uint64_t writeback_page_count;
    uint64_t error_page_count;
    uint64_t peak_outstanding_writeback_page_count;
    uint64_t mark_dirty_count;
    uint64_t dirty_limit_rejection_count;
    uint64_t writeback_attempt_count;
    uint64_t successful_writeback_count;
    uint64_t failed_writeback_count;
};

enum class FilePageCacheStatus : uint64_t {
    Succeeded,
    NotInitialized,
    AlreadyInitialized,
    InvalidStorage,
    InvalidCapacity,
    InvalidDependency,
    InvalidIdentity,
    InvalidReader,
    InvalidWriter,
    FrameAllocationFailed,
    FrameAccessFailed,
    SourceReadFailed,
    SourceWriteFailed,
    CapacityExhausted,
    DirtyLimitReached,
    DirtyPagesRemain,
    MappingNotFound,
    ReferenceUnderflow,
    EntryBusy,
    FrameReleaseFailed,
    Corrupt,
};

class FilePageCache final {
  public:
    FilePageCache() noexcept = default;
    FilePageCache(const FilePageCache &) = delete;
    FilePageCache &operator=(const FilePageCache &) = delete;

    [[nodiscard]] FilePageCacheStatus
    Initialize(FilePageCacheEntry *entries, uint64_t capacity, uint64_t dirty_page_limit,
               PhysicalFrameAllocator &frame_allocator, void *page_access_context,
               FilePageAccessOperation page_access_operation) noexcept;
    [[nodiscard]] FilePageCacheStatus Acquire(const FilePageIdentity &identity,
                                              void *reader_context,
                                              FilePageReadOperation read_operation,
                                              uint64_t &physical_address, bool &cache_hit) noexcept;
    [[nodiscard]] FilePageCacheStatus Release(const FilePageIdentity &identity,
                                              uint64_t physical_address) noexcept;
    [[nodiscard]] FilePageCacheStatus MarkDirty(const FilePageIdentity &identity,
                                                uint64_t physical_address) noexcept;
    [[nodiscard]] FilePageCacheStatus Writeback(void *writer_context,
                                                FilePageWriteOperation write_operation,
                                                uint64_t maximum_page_count,
                                                uint64_t &written_page_count) noexcept;
    [[nodiscard]] FilePageCacheStatus Invalidate(const FileIdentity &identity) noexcept;
    [[nodiscard]] FilePageCacheStatus Trim(uint64_t target_resident_page_count) noexcept;
    [[nodiscard]] FilePageCacheStatus Trim(uint64_t target_resident_page_count,
                                           uint64_t &reclaimed_page_count) noexcept;
    [[nodiscard]] FilePageCacheStatus Validate() const noexcept;
    [[nodiscard]] FilePageCacheStatistics Statistics() const noexcept;
    [[nodiscard]] FilePageCacheStatus Destroy() noexcept;

  private:
    [[nodiscard]] bool IdentitiesEqual(const FileIdentity &left,
                                       const FileIdentity &right) const noexcept;
    [[nodiscard]] bool PageIdentitiesEqual(const FilePageIdentity &left,
                                           const FilePageIdentity &right) const noexcept;
    [[nodiscard]] bool IdentityIsValid(const FileIdentity &identity) const noexcept;
    [[nodiscard]] uint64_t NextAccessGeneration() noexcept;
    [[nodiscard]] FilePageCacheEntry *FindEntry(const FilePageIdentity &identity) noexcept;
    [[nodiscard]] FilePageCacheEntry *SelectLoadEntry() noexcept;
    [[nodiscard]] FilePageCacheStatus ReleaseEntry(FilePageCacheEntry &entry) noexcept;

    FilePageCacheEntry *entries_{nullptr};
    uint64_t capacity_{};
    uint64_t dirty_page_limit_{};
    PhysicalFrameAllocator *frame_allocator_{nullptr};
    void *page_access_context_{nullptr};
    FilePageAccessOperation page_access_operation_{nullptr};
    uint64_t access_generation_{};
    FilePageCacheStatistics statistics_{};
    mutable SpinLock lock_{};
    bool initialized_{};
};

}

#pragma once

#include <os/kernel/memory/file_cache_address_space.hpp>
#include <os/kernel/memory/file_cache_identity.hpp>
#include <os/kernel/memory/kernel_heap.hpp>
#include <os/kernel/memory/physical_frame_allocator.hpp>
#include <os/kernel/sync/spin_lock.hpp>

#include <stdint.h>

namespace os::kernel {

using FileIdentity = FileCacheIdentity;

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
    Loading,
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
    bool prefetched;
};

enum class FilePageAcquireIntent : uint64_t {
    Demand,
    Prefetch,
};

using FilePageCacheVisitOperation = bool (*)(void *context,
                                             const FilePageCacheEntry &entry) noexcept;
using FilePageCacheReclaimSelectionOperation = bool (*)(void *context,
                                                        const FilePageCacheEntry &entry,
                                                        bool &selected) noexcept;
using FilePageCacheReclaimCompletionOperation = bool (*)(void *context,
                                                         const FilePageCacheEntry &entry) noexcept;

struct FilePageCacheStatistics final {
    uint64_t capacity;
    uint64_t resident_page_count;
    uint64_t referenced_page_count;
    uint64_t active_mapping_reference_count;
    uint64_t peak_resident_page_count;
    uint64_t loading_page_count;
    uint64_t loading_collision_count;
    uint64_t unlocked_fill_count;
    uint64_t hit_count;
    uint64_t miss_count;
    uint64_t successful_load_count;
    uint64_t failed_load_count;
    uint64_t eviction_count;
    uint64_t invalidation_count;
    uint64_t successful_acquire_count;
    uint64_t release_count;
    uint64_t prefetch_acquire_count;
    uint64_t successful_prefetch_load_count;
    uint64_t prefetch_existing_page_count;
    uint64_t prefetched_page_count;
    uint64_t prefetched_hit_count;
    uint64_t wasted_prefetched_page_count;
    uint64_t background_dirty_page_threshold;
    uint64_t background_dirty_page_target;
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
    uint64_t address_space_count;
    uint64_t peak_address_space_count;
    uint64_t metadata_allocation_failure_count;
    uint64_t truncate_count;
    uint64_t truncated_page_count;
    uint64_t truncated_tail_zero_count;
    uint64_t background_writeback_request_count;
    uint64_t explicit_background_writeback_request_count;
    uint64_t dirty_backpressure_count;
    bool background_writeback_requested;
    bool background_writeback_paused;
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
    InvalidVisitor,
    FrameAllocationFailed,
    MetadataAllocationFailed,
    MetadataReleaseFailed,
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
    LoadingWaitUnavailable,
    LoadingWaitFailed,
    Corrupt,
};

struct FilePageLoadToken final {
    uint64_t slot_index;
    uint64_t generation;
};

using FilePageLoadWaitAvailableOperation = bool (*)(void *context) noexcept;
using FilePageLoadBeginOperation = bool (*)(void *context, const FilePageIdentity &identity,
                                            uint64_t physical_address, uint64_t load_generation,
                                            FilePageLoadToken &token) noexcept;
using FilePageLoadRegisterWaiterOperation = bool (*)(void *context,
                                                     const FilePageIdentity &identity,
                                                     uint64_t physical_address,
                                                     uint64_t load_generation,
                                                     FilePageLoadToken &token) noexcept;
using FilePageLoadWaitOperation = bool (*)(void *context, FilePageLoadToken token,
                                           FilePageCacheStatus &result) noexcept;
using FilePageLoadWaiterCountOperation = bool (*)(void *context, FilePageLoadToken token,
                                                  uint64_t &waiter_count) noexcept;
using FilePageLoadCompleteOperation = bool (*)(void *context, FilePageLoadToken token,
                                               FilePageCacheStatus result) noexcept;

struct FilePageLoadWaitOperations final {
    void *context;
    FilePageLoadWaitAvailableOperation owner_available;
    FilePageLoadWaitAvailableOperation available;
    FilePageLoadBeginOperation begin;
    FilePageLoadRegisterWaiterOperation register_waiter;
    FilePageLoadWaitOperation wait;
    FilePageLoadWaiterCountOperation waiter_count;
    FilePageLoadCompleteOperation complete;
};

class FilePageCache final {
  public:
    FilePageCache() noexcept = default;
    FilePageCache(const FilePageCache &) = delete;
    FilePageCache &operator=(const FilePageCache &) = delete;

    [[nodiscard]] FilePageCacheStatus
    Initialize(KernelHeap &metadata_heap, uint64_t capacity, uint64_t dirty_page_limit,
               PhysicalFrameAllocator &frame_allocator, void *page_access_context,
               FilePageAccessOperation page_access_operation) noexcept;
    [[nodiscard]] FilePageCacheStatus
    ConfigureLoadingWait(const FilePageLoadWaitOperations &operations) noexcept;
    [[nodiscard]] FilePageCacheStatus Acquire(const FilePageIdentity &identity,
                                              void *reader_context,
                                              FilePageReadOperation read_operation,
                                              uint64_t &physical_address, bool &cache_hit) noexcept;
    [[nodiscard]] FilePageCacheStatus
    Acquire(const FilePageIdentity &identity, void *reader_context,
            FilePageReadOperation read_operation, FilePageAcquireIntent intent,
            uint64_t &physical_address, bool &cache_hit, bool &prefetched_hit) noexcept;
    [[nodiscard]] FilePageCacheStatus Release(const FilePageIdentity &identity,
                                              uint64_t physical_address) noexcept;
    [[nodiscard]] FilePageCacheStatus MarkDirty(const FilePageIdentity &identity,
                                                uint64_t physical_address) noexcept;
    [[nodiscard]] FilePageCacheStatus Writeback(void *writer_context,
                                                FilePageWriteOperation write_operation,
                                                uint64_t maximum_page_count,
                                                uint64_t &written_page_count) noexcept;
    [[nodiscard]] FilePageCacheStatus
    WritebackFile(const FileIdentity &identity, uint64_t first_page_index, uint64_t last_page_index,
                  void *writer_context, FilePageWriteOperation write_operation,
                  uint64_t maximum_page_count, uint64_t &written_page_count) noexcept;
    [[nodiscard]] FilePageCacheStatus RequestBackgroundWriteback() noexcept;
    [[nodiscard]] FilePageCacheStatus Invalidate(const FileIdentity &identity) noexcept;
    [[nodiscard]] FilePageCacheStatus ObserveFileSize(const FileIdentity &identity,
                                                      uint64_t size_bytes) noexcept;
    [[nodiscard]] FilePageCacheStatus UpdateFileSize(const FileIdentity &identity,
                                                     uint64_t size_bytes) noexcept;
    [[nodiscard]] FilePageCacheStatus ResolveFileSize(const FileIdentity &identity,
                                                      uint64_t backend_size_bytes,
                                                      uint64_t &size_bytes) const noexcept;
    [[nodiscard]] FilePageCacheStatus Truncate(const FileIdentity &identity,
                                               uint64_t size_bytes) noexcept;
    [[nodiscard]] FilePageCacheStatus
    ReadAddressSpaceStatistics(const FileIdentity &identity,
                               FileCacheAddressSpaceStatistics &statistics) const noexcept;
    [[nodiscard]] bool BackgroundWritebackRequested() const noexcept;
    [[nodiscard]] bool DirtyBackpressureRequired() const noexcept;
    [[nodiscard]] bool BackgroundWritebackPaused() const noexcept;
    [[nodiscard]] FilePageCacheStatus Trim(uint64_t target_resident_page_count) noexcept;
    [[nodiscard]] FilePageCacheStatus Trim(uint64_t target_resident_page_count,
                                           uint64_t &reclaimed_page_count) noexcept;
    [[nodiscard]] FilePageCacheStatus
    ReclaimCleanPages(uint64_t maximum_page_count, void *context,
                      FilePageCacheReclaimSelectionOperation selection_operation,
                      FilePageCacheReclaimCompletionOperation completion_operation,
                      uint64_t &reclaimed_page_count) noexcept;
    [[nodiscard]] FilePageCacheStatus ReadEntry(const FilePageIdentity &identity,
                                                FilePageCacheEntry &entry) const noexcept;
    [[nodiscard]] FilePageCacheStatus
    VisitEntries(void *context, FilePageCacheVisitOperation operation) const noexcept;
    [[nodiscard]] FilePageCacheStatus Validate() const noexcept;
    [[nodiscard]] FilePageCacheStatistics Statistics() const noexcept;
    [[nodiscard]] FilePageCacheStatus Destroy() noexcept;

  private:
    struct AddressSpaceRecord;

    [[nodiscard]] uint64_t NextAccessGeneration() noexcept;
    [[nodiscard]] AddressSpaceRecord *FindAddressSpace(const FileIdentity &identity) noexcept;
    [[nodiscard]] const AddressSpaceRecord *
    FindAddressSpace(const FileIdentity &identity) const noexcept;
    [[nodiscard]] FilePageCacheStatus EnsureAddressSpace(const FileIdentity &identity,
                                                         AddressSpaceRecord *&record) noexcept;
    [[nodiscard]] FilePageCacheStatus DestroyAddressSpace(AddressSpaceRecord &record) noexcept;
    [[nodiscard]] FilePageCacheStatus
    DestroyAddressSpaceIfEmpty(AddressSpaceRecord &record) noexcept;
    [[nodiscard]] FilePageCacheStatus
    SelectEvictionCandidate(void *context,
                            FilePageCacheReclaimSelectionOperation selection_operation,
                            AddressSpaceRecord *&record, FileCachePageSnapshot &page) noexcept;
    [[nodiscard]] FilePageCacheStatus Evict(AddressSpaceRecord &record,
                                            const FileCachePageSnapshot &page, bool release_frame,
                                            uint64_t &physical_address) noexcept;
    [[nodiscard]] FilePageCacheStatus
    SelectWritebackCandidate(AddressSpaceRecord *&record, FileCachePageSnapshot &page) noexcept;
    [[nodiscard]] FilePageCacheStatus
    SelectWritebackCandidateInRange(const FileIdentity &identity, uint64_t first_page_index,
                                    uint64_t last_page_index, AddressSpaceRecord *&record,
                                    FileCachePageSnapshot &page) noexcept;
    [[nodiscard]] FilePageCacheStatus
    WritebackInternal(const FileIdentity *identity, uint64_t first_page_index,
                      uint64_t last_page_index, void *writer_context,
                      FilePageWriteOperation write_operation, uint64_t maximum_page_count,
                      uint64_t &written_page_count) noexcept;
    [[nodiscard]] bool OutstandingDirtyPageCount(uint64_t &page_count) const noexcept;
    [[nodiscard]] bool LoadingOwnerAvailable() const noexcept;
    [[nodiscard]] bool LoadingWaitAvailable() const noexcept;
    [[nodiscard]] bool LoadingWaiterCount(FilePageLoadToken token, uint64_t &waiter_count) noexcept;
    [[nodiscard]] bool CompleteLoadingWait(FilePageLoadToken token,
                                           FilePageCacheStatus result) noexcept;
    [[nodiscard]] bool ConsumePrefetchedIfDemand(AddressSpaceRecord &record,
                                                 const FileCachePageSnapshot &page,
                                                 FilePageAcquireIntent intent,
                                                 bool &prefetched_hit) noexcept;
    [[nodiscard]] bool RecordPrefetchedDiscard(const FileCachePageSnapshot &page) noexcept;
    void RefreshBackgroundWritebackRequest() noexcept;
    [[nodiscard]] FilePageCacheStatus
    MapAddressSpaceStatus(FileCacheAddressSpaceStatus status) const noexcept;
    [[nodiscard]] static FilePageCacheEntryState MapPageState(FileCachePageState state) noexcept;
    [[nodiscard]] static FilePageCacheEntry Snapshot(const FileIdentity &identity,
                                                     const FileCachePageSnapshot &page) noexcept;

    KernelHeap *metadata_heap_{nullptr};
    AddressSpaceRecord *address_spaces_{nullptr};
    uint64_t capacity_{};
    uint64_t background_dirty_page_threshold_{};
    uint64_t background_dirty_page_target_{};
    uint64_t dirty_page_limit_{};
    PhysicalFrameAllocator *frame_allocator_{nullptr};
    void *page_access_context_{nullptr};
    FilePageAccessOperation page_access_operation_{nullptr};
    FilePageLoadWaitOperations load_wait_operations_{};
    uint64_t access_generation_{};
    FilePageCacheStatistics statistics_{};
    bool background_writeback_requested_{};
    bool background_writeback_paused_{};
    bool forced_background_writeback_requested_{};
    bool load_wait_operations_configured_{};
    mutable SpinLock lock_{};
    bool initialized_{};
};

}

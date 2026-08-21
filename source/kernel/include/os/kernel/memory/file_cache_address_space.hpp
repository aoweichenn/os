#pragma once

#include <os/kernel/memory/file_cache_identity.hpp>
#include <os/kernel/memory/sparse_page_index.hpp>
#include <os/kernel/sync/spin_lock.hpp>

#include <stdint.h>

namespace os::kernel {

enum class FileCachePageState : uint64_t {
    Clean,
    Dirty,
    Writeback,
    Error,
};

struct FileCachePageSnapshot final {
    uint64_t page_index;
    uint64_t physical_address;
    uint64_t mapping_reference_count;
    uint64_t access_generation;
    FileCachePageState state;
};

struct FileCacheAddressSpaceStatistics final {
    FileCacheIdentity identity;
    uint64_t resident_page_count;
    uint64_t referenced_page_count;
    uint64_t active_mapping_reference_count;
    uint64_t peak_resident_page_count;
    uint64_t peak_active_mapping_reference_count;
    uint64_t clean_page_count;
    uint64_t dirty_page_count;
    uint64_t writeback_page_count;
    uint64_t error_page_count;
    uint64_t successful_insertion_count;
    uint64_t removal_count;
    uint64_t retain_count;
    uint64_t release_count;
    uint64_t state_transition_count;
    uint64_t failed_insertion_count;
    SparsePageIndexStatistics index;
};

enum class FileCacheAddressSpaceStatus : uint64_t {
    Succeeded,
    NotInitialized,
    AlreadyInitialized,
    InvalidIdentity,
    InvalidPage,
    InvalidState,
    AlreadyExists,
    NotFound,
    AllocationFailed,
    MappingReferenceOverflow,
    MappingReferenceUnderflow,
    PageBusy,
    DirtyPagesRemain,
    MetadataReleaseFailed,
    PagesRemain,
    Corrupt,
};

class FileCacheAddressSpace final {
  public:
    FileCacheAddressSpace() noexcept = default;
    FileCacheAddressSpace(const FileCacheAddressSpace &) = delete;
    FileCacheAddressSpace &operator=(const FileCacheAddressSpace &) = delete;

    [[nodiscard]] FileCacheAddressSpaceStatus Initialize(const FileCacheIdentity &identity,
                                                         KernelHeap &heap) noexcept;
    [[nodiscard]] FileCacheAddressSpaceStatus Insert(uint64_t page_index, uint64_t physical_address,
                                                     FileCachePageState state) noexcept;
    [[nodiscard]] FileCacheAddressSpaceStatus Lookup(uint64_t page_index,
                                                     FileCachePageSnapshot &page) const noexcept;
    [[nodiscard]] FileCacheAddressSpaceStatus Retain(uint64_t page_index,
                                                     uint64_t physical_address) noexcept;
    [[nodiscard]] FileCacheAddressSpaceStatus Release(uint64_t page_index,
                                                      uint64_t physical_address) noexcept;
    [[nodiscard]] FileCacheAddressSpaceStatus Touch(uint64_t page_index, uint64_t physical_address,
                                                    uint64_t access_generation) noexcept;
    [[nodiscard]] FileCacheAddressSpaceStatus Transition(uint64_t page_index,
                                                         uint64_t physical_address,
                                                         FileCachePageState expected_state,
                                                         FileCachePageState new_state) noexcept;
    [[nodiscard]] FileCacheAddressSpaceStatus Remove(uint64_t page_index,
                                                     uint64_t physical_address) noexcept;
    [[nodiscard]] FileCacheAddressSpaceStatus Discard(uint64_t page_index,
                                                      uint64_t physical_address) noexcept;
    [[nodiscard]] FileCacheAddressSpaceStatus FindNext(uint64_t first_page_index,
                                                       uint64_t last_page_index,
                                                       FileCachePageState state,
                                                       FileCachePageSnapshot &page) const noexcept;
    [[nodiscard]] FileCacheAddressSpaceStatus Validate() const noexcept;
    [[nodiscard]] FileCacheAddressSpaceStatistics Statistics() const noexcept;
    [[nodiscard]] FileCacheAddressSpaceStatus Destroy() noexcept;

  private:
    struct Page;

    [[nodiscard]] static bool StateIsValid(FileCachePageState state) noexcept;
    [[nodiscard]] static bool TransitionIsValid(FileCachePageState current_state,
                                                FileCachePageState new_state) noexcept;
    [[nodiscard]] static bool StateHasMark(FileCachePageState state) noexcept;
    [[nodiscard]] static SparsePageIndexMark MarkForState(FileCachePageState state) noexcept;
    [[nodiscard]] static FileCachePageSnapshot Snapshot(const Page &page) noexcept;
    [[nodiscard]] FileCacheAddressSpaceStatus LookupPage(uint64_t page_index,
                                                         Page *&page) const noexcept;
    [[nodiscard]] FileCacheAddressSpaceStatus SetStateMark(uint64_t page_index,
                                                           FileCachePageState state) noexcept;
    [[nodiscard]] FileCacheAddressSpaceStatus ClearStateMark(uint64_t page_index,
                                                             FileCachePageState state) noexcept;
    void IncrementStateCount(FileCachePageState state) noexcept;
    [[nodiscard]] bool DecrementStateCount(FileCachePageState state) noexcept;
    [[nodiscard]] FileCacheAddressSpaceStatus
    MapIndexStatus(SparsePageIndexStatus status) const noexcept;

    KernelHeap *heap_{nullptr};
    SparsePageIndex index_{};
    FileCacheAddressSpaceStatistics statistics_{};
    uint64_t access_generation_{};
    mutable SpinLock lock_{};
    bool initialized_{};
};

}

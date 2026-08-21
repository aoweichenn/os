#include <os/kernel/memory/file_cache_address_space.hpp>
#include <os/kernel/memory/physical_frame_allocator.hpp>

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_FILE_CACHE_ADDRESS_SPACE_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_FILE_CACHE_ADDRESS_SPACE_SINGLE_UNIT = 1ULL;
constexpr uint64_t OS_KERNEL_FILE_CACHE_ADDRESS_SPACE_PAGE_ALIGNMENT_BYTES = 64ULL;

}

struct alignas(OS_KERNEL_FILE_CACHE_ADDRESS_SPACE_PAGE_ALIGNMENT_BYTES)
    FileCacheAddressSpace::Page final {
    uint64_t page_index;
    uint64_t physical_address;
    uint64_t mapping_reference_count;
    uint64_t access_generation;
    FileCachePageState state;
};

FileCacheAddressSpaceStatus FileCacheAddressSpace::Initialize(const FileCacheIdentity &identity,
                                                              KernelHeap &heap) noexcept {
    SpinLockGuard guard{this->lock_};
    if (this->initialized_) {
        return FileCacheAddressSpaceStatus::AlreadyInitialized;
    }
    if (!FileCacheIdentityIsValid(identity)) {
        return FileCacheAddressSpaceStatus::InvalidIdentity;
    }
    const SparsePageIndexStatus index_status = this->index_.Initialize(heap);
    if (index_status != SparsePageIndexStatus::Succeeded) {
        return index_status == SparsePageIndexStatus::InvalidHeap
                   ? FileCacheAddressSpaceStatus::AllocationFailed
                   : FileCacheAddressSpaceStatus::Corrupt;
    }
    this->heap_ = &heap;
    this->statistics_ = FileCacheAddressSpaceStatistics{};
    this->statistics_.identity = identity;
    this->access_generation_ = OS_KERNEL_FILE_CACHE_ADDRESS_SPACE_EMPTY_VALUE;
    this->initialized_ = true;
    return FileCacheAddressSpaceStatus::Succeeded;
}

FileCacheAddressSpaceStatus FileCacheAddressSpace::Insert(const uint64_t page_index,
                                                          const uint64_t physical_address,
                                                          const FileCachePageState state) noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_ || this->heap_ == nullptr) {
        return FileCacheAddressSpaceStatus::NotInitialized;
    }
    if ((physical_address &
         (OS_KERNEL_MEMORY_PAGE_SIZE_BYTES - OS_KERNEL_FILE_CACHE_ADDRESS_SPACE_SINGLE_UNIT)) !=
        OS_KERNEL_FILE_CACHE_ADDRESS_SPACE_EMPTY_VALUE) {
        return FileCacheAddressSpaceStatus::InvalidPage;
    }
    if (state != FileCachePageState::Loading && state != FileCachePageState::Clean &&
        state != FileCachePageState::Dirty) {
        return FileCacheAddressSpaceStatus::InvalidState;
    }
    Page *existing_page = nullptr;
    const FileCacheAddressSpaceStatus existing_status = this->LookupPage(page_index, existing_page);
    if (existing_status == FileCacheAddressSpaceStatus::Succeeded) {
        return FileCacheAddressSpaceStatus::AlreadyExists;
    }
    if (existing_status != FileCacheAddressSpaceStatus::NotFound) {
        return existing_status;
    }
    if (this->access_generation_ == UINT64_MAX) {
        return FileCacheAddressSpaceStatus::Corrupt;
    }

    void *allocation = nullptr;
    if (this->heap_->TryAllocate(sizeof(Page), alignof(Page), allocation) !=
        KernelHeapStatus::Succeeded) {
        ++this->statistics_.failed_insertion_count;
        return FileCacheAddressSpaceStatus::AllocationFailed;
    }
    Page *const page = static_cast<Page *>(allocation);
    ++this->access_generation_;
    *page = Page{
        .page_index = page_index,
        .physical_address = physical_address,
        .mapping_reference_count = OS_KERNEL_FILE_CACHE_ADDRESS_SPACE_EMPTY_VALUE,
        .access_generation = this->access_generation_,
        .state = state,
    };
    const SparsePageIndexStatus insertion_status = this->index_.Insert(page_index, page);
    if (insertion_status != SparsePageIndexStatus::Succeeded) {
        ++this->statistics_.failed_insertion_count;
        if (this->heap_->TryRelease(page) != KernelHeapStatus::Succeeded) {
            return FileCacheAddressSpaceStatus::MetadataReleaseFailed;
        }
        return this->MapIndexStatus(insertion_status);
    }
    if (FileCacheAddressSpace::StateHasMark(state)) {
        const FileCacheAddressSpaceStatus mark_status = this->SetStateMark(page_index, state);
        if (mark_status != FileCacheAddressSpaceStatus::Succeeded) {
            void *removed_entry = nullptr;
            const SparsePageIndexStatus erase_status =
                this->index_.Erase(page_index, removed_entry);
            const KernelHeapStatus release_status = this->heap_->TryRelease(page);
            ++this->statistics_.failed_insertion_count;
            return erase_status == SparsePageIndexStatus::Succeeded && removed_entry == page &&
                           release_status == KernelHeapStatus::Succeeded
                       ? mark_status
                       : FileCacheAddressSpaceStatus::Corrupt;
        }
    }
    ++this->statistics_.resident_page_count;
    ++this->statistics_.successful_insertion_count;
    this->IncrementStateCount(state);
    if (this->statistics_.peak_resident_page_count < this->statistics_.resident_page_count) {
        this->statistics_.peak_resident_page_count = this->statistics_.resident_page_count;
    }
    return FileCacheAddressSpaceStatus::Succeeded;
}

FileCacheAddressSpaceStatus
FileCacheAddressSpace::Lookup(const uint64_t page_index,
                              FileCachePageSnapshot &page) const noexcept {
    SpinLockGuard guard{this->lock_};
    page = FileCachePageSnapshot{};
    if (!this->initialized_) {
        return FileCacheAddressSpaceStatus::NotInitialized;
    }
    Page *stored_page = nullptr;
    const FileCacheAddressSpaceStatus status = this->LookupPage(page_index, stored_page);
    if (status != FileCacheAddressSpaceStatus::Succeeded) {
        return status;
    }
    page = FileCacheAddressSpace::Snapshot(*stored_page);
    return FileCacheAddressSpaceStatus::Succeeded;
}

FileCacheAddressSpaceStatus
FileCacheAddressSpace::Retain(const uint64_t page_index, const uint64_t physical_address) noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_) {
        return FileCacheAddressSpaceStatus::NotInitialized;
    }
    Page *page = nullptr;
    const FileCacheAddressSpaceStatus status = this->LookupPage(page_index, page);
    if (status != FileCacheAddressSpaceStatus::Succeeded) {
        return status;
    }
    if (page->physical_address != physical_address) {
        return FileCacheAddressSpaceStatus::InvalidPage;
    }
    if (page->state == FileCachePageState::Loading) {
        return FileCacheAddressSpaceStatus::PageBusy;
    }
    if (page->mapping_reference_count == UINT64_MAX ||
        this->statistics_.active_mapping_reference_count == UINT64_MAX) {
        return FileCacheAddressSpaceStatus::MappingReferenceOverflow;
    }
    if (this->access_generation_ == UINT64_MAX) {
        return FileCacheAddressSpaceStatus::Corrupt;
    }
    if (page->mapping_reference_count == OS_KERNEL_FILE_CACHE_ADDRESS_SPACE_EMPTY_VALUE) {
        ++this->statistics_.referenced_page_count;
    }
    ++page->mapping_reference_count;
    ++this->statistics_.active_mapping_reference_count;
    ++this->statistics_.retain_count;
    ++this->access_generation_;
    page->access_generation = this->access_generation_;
    if (this->statistics_.peak_active_mapping_reference_count <
        this->statistics_.active_mapping_reference_count) {
        this->statistics_.peak_active_mapping_reference_count =
            this->statistics_.active_mapping_reference_count;
    }
    return FileCacheAddressSpaceStatus::Succeeded;
}

FileCacheAddressSpaceStatus
FileCacheAddressSpace::Release(const uint64_t page_index,
                               const uint64_t physical_address) noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_) {
        return FileCacheAddressSpaceStatus::NotInitialized;
    }
    Page *page = nullptr;
    const FileCacheAddressSpaceStatus status = this->LookupPage(page_index, page);
    if (status != FileCacheAddressSpaceStatus::Succeeded) {
        return status;
    }
    if (page->physical_address != physical_address) {
        return FileCacheAddressSpaceStatus::InvalidPage;
    }
    if (page->mapping_reference_count == OS_KERNEL_FILE_CACHE_ADDRESS_SPACE_EMPTY_VALUE ||
        this->statistics_.active_mapping_reference_count ==
            OS_KERNEL_FILE_CACHE_ADDRESS_SPACE_EMPTY_VALUE) {
        return FileCacheAddressSpaceStatus::MappingReferenceUnderflow;
    }
    --page->mapping_reference_count;
    --this->statistics_.active_mapping_reference_count;
    ++this->statistics_.release_count;
    if (page->mapping_reference_count == OS_KERNEL_FILE_CACHE_ADDRESS_SPACE_EMPTY_VALUE) {
        if (this->statistics_.referenced_page_count ==
            OS_KERNEL_FILE_CACHE_ADDRESS_SPACE_EMPTY_VALUE) {
            return FileCacheAddressSpaceStatus::Corrupt;
        }
        --this->statistics_.referenced_page_count;
    }
    return FileCacheAddressSpaceStatus::Succeeded;
}

FileCacheAddressSpaceStatus
FileCacheAddressSpace::Touch(const uint64_t page_index, const uint64_t physical_address,
                             const uint64_t access_generation) noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_) {
        return FileCacheAddressSpaceStatus::NotInitialized;
    }
    if (access_generation == OS_KERNEL_FILE_CACHE_ADDRESS_SPACE_EMPTY_VALUE) {
        return FileCacheAddressSpaceStatus::InvalidPage;
    }
    Page *page = nullptr;
    const FileCacheAddressSpaceStatus status = this->LookupPage(page_index, page);
    if (status != FileCacheAddressSpaceStatus::Succeeded) {
        return status;
    }
    if (page->physical_address != physical_address) {
        return FileCacheAddressSpaceStatus::InvalidPage;
    }
    page->access_generation = access_generation;
    return FileCacheAddressSpaceStatus::Succeeded;
}

FileCacheAddressSpaceStatus
FileCacheAddressSpace::Transition(const uint64_t page_index, const uint64_t physical_address,
                                  const FileCachePageState expected_state,
                                  const FileCachePageState new_state) noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_) {
        return FileCacheAddressSpaceStatus::NotInitialized;
    }
    if (!FileCacheAddressSpace::StateIsValid(expected_state) ||
        !FileCacheAddressSpace::StateIsValid(new_state)) {
        return FileCacheAddressSpaceStatus::InvalidState;
    }
    Page *page = nullptr;
    const FileCacheAddressSpaceStatus lookup_status = this->LookupPage(page_index, page);
    if (lookup_status != FileCacheAddressSpaceStatus::Succeeded) {
        return lookup_status;
    }
    if (page->physical_address != physical_address) {
        return FileCacheAddressSpaceStatus::InvalidPage;
    }
    if (page->state != expected_state) {
        return FileCacheAddressSpaceStatus::InvalidState;
    }
    if (expected_state == new_state) {
        return FileCacheAddressSpaceStatus::Succeeded;
    }
    if (!FileCacheAddressSpace::TransitionIsValid(expected_state, new_state)) {
        return FileCacheAddressSpaceStatus::InvalidState;
    }
    const FileCacheAddressSpaceStatus clear_status =
        this->ClearStateMark(page_index, expected_state);
    if (clear_status != FileCacheAddressSpaceStatus::Succeeded) {
        return clear_status;
    }
    const FileCacheAddressSpaceStatus set_status = this->SetStateMark(page_index, new_state);
    if (set_status != FileCacheAddressSpaceStatus::Succeeded) {
        const FileCacheAddressSpaceStatus rollback_status =
            this->SetStateMark(page_index, expected_state);
        return rollback_status == FileCacheAddressSpaceStatus::Succeeded
                   ? set_status
                   : FileCacheAddressSpaceStatus::Corrupt;
    }
    if (!this->DecrementStateCount(expected_state)) {
        return FileCacheAddressSpaceStatus::Corrupt;
    }
    this->IncrementStateCount(new_state);
    page->state = new_state;
    ++this->statistics_.state_transition_count;
    return FileCacheAddressSpaceStatus::Succeeded;
}

FileCacheAddressSpaceStatus
FileCacheAddressSpace::Remove(const uint64_t page_index, const uint64_t physical_address) noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_ || this->heap_ == nullptr) {
        return FileCacheAddressSpaceStatus::NotInitialized;
    }
    Page *page = nullptr;
    const FileCacheAddressSpaceStatus lookup_status = this->LookupPage(page_index, page);
    if (lookup_status != FileCacheAddressSpaceStatus::Succeeded) {
        return lookup_status;
    }
    if (page->physical_address != physical_address) {
        return FileCacheAddressSpaceStatus::InvalidPage;
    }
    if (page->mapping_reference_count != OS_KERNEL_FILE_CACHE_ADDRESS_SPACE_EMPTY_VALUE) {
        return FileCacheAddressSpaceStatus::PageBusy;
    }
    if (page->state == FileCachePageState::Loading ||
        page->state == FileCachePageState::Writeback) {
        return FileCacheAddressSpaceStatus::PageBusy;
    }
    if (page->state != FileCachePageState::Clean) {
        return FileCacheAddressSpaceStatus::DirtyPagesRemain;
    }
    void *removed_entry = nullptr;
    const SparsePageIndexStatus erase_status = this->index_.Erase(page_index, removed_entry);
    if (erase_status != SparsePageIndexStatus::Succeeded || removed_entry != page) {
        return erase_status == SparsePageIndexStatus::Succeeded
                   ? FileCacheAddressSpaceStatus::Corrupt
                   : this->MapIndexStatus(erase_status);
    }
    if (this->heap_->TryRelease(page) != KernelHeapStatus::Succeeded) {
        return FileCacheAddressSpaceStatus::MetadataReleaseFailed;
    }
    if (this->statistics_.resident_page_count == OS_KERNEL_FILE_CACHE_ADDRESS_SPACE_EMPTY_VALUE ||
        !this->DecrementStateCount(FileCachePageState::Clean)) {
        return FileCacheAddressSpaceStatus::Corrupt;
    }
    --this->statistics_.resident_page_count;
    ++this->statistics_.removal_count;
    return FileCacheAddressSpaceStatus::Succeeded;
}

FileCacheAddressSpaceStatus
FileCacheAddressSpace::Discard(const uint64_t page_index,
                               const uint64_t physical_address) noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_ || this->heap_ == nullptr) {
        return FileCacheAddressSpaceStatus::NotInitialized;
    }
    Page *page = nullptr;
    const FileCacheAddressSpaceStatus lookup_status = this->LookupPage(page_index, page);
    if (lookup_status != FileCacheAddressSpaceStatus::Succeeded) {
        return lookup_status;
    }
    if (page->physical_address != physical_address) {
        return FileCacheAddressSpaceStatus::InvalidPage;
    }
    if (page->mapping_reference_count != OS_KERNEL_FILE_CACHE_ADDRESS_SPACE_EMPTY_VALUE ||
        page->state == FileCachePageState::Writeback) {
        return FileCacheAddressSpaceStatus::PageBusy;
    }
    const FileCachePageState discarded_state = page->state;
    void *removed_entry = nullptr;
    const SparsePageIndexStatus erase_status = this->index_.Erase(page_index, removed_entry);
    if (erase_status != SparsePageIndexStatus::Succeeded || removed_entry != page) {
        return erase_status == SparsePageIndexStatus::Succeeded
                   ? FileCacheAddressSpaceStatus::Corrupt
                   : this->MapIndexStatus(erase_status);
    }
    if (this->heap_->TryRelease(page) != KernelHeapStatus::Succeeded) {
        return FileCacheAddressSpaceStatus::MetadataReleaseFailed;
    }
    if (this->statistics_.resident_page_count ==
            OS_KERNEL_FILE_CACHE_ADDRESS_SPACE_EMPTY_VALUE ||
        !this->DecrementStateCount(discarded_state)) {
        return FileCacheAddressSpaceStatus::Corrupt;
    }
    --this->statistics_.resident_page_count;
    ++this->statistics_.removal_count;
    return FileCacheAddressSpaceStatus::Succeeded;
}

FileCacheAddressSpaceStatus
FileCacheAddressSpace::FindNext(const uint64_t first_page_index, const uint64_t last_page_index,
                                const FileCachePageState state,
                                FileCachePageSnapshot &page) const noexcept {
    SpinLockGuard guard{this->lock_};
    page = FileCachePageSnapshot{};
    if (!this->initialized_) {
        return FileCacheAddressSpaceStatus::NotInitialized;
    }
    if (first_page_index > last_page_index) {
        return FileCacheAddressSpaceStatus::InvalidPage;
    }
    if (!FileCacheAddressSpace::StateIsValid(state)) {
        return FileCacheAddressSpaceStatus::InvalidState;
    }
    uint64_t search_page_index = first_page_index;
    const SparsePageIndexMark mark = FileCacheAddressSpace::StateHasMark(state)
                                         ? FileCacheAddressSpace::MarkForState(state)
                                         : SparsePageIndexMark::Present;
    while (search_page_index <= last_page_index) {
        uint64_t found_page_index = OS_KERNEL_FILE_CACHE_ADDRESS_SPACE_EMPTY_VALUE;
        void *entry = nullptr;
        const SparsePageIndexStatus find_status = this->index_.FindNext(
            search_page_index, last_page_index, mark, found_page_index, entry);
        if (find_status != SparsePageIndexStatus::Succeeded) {
            return this->MapIndexStatus(find_status);
        }
        if (entry == nullptr) {
            return FileCacheAddressSpaceStatus::Corrupt;
        }
        const Page &candidate = *static_cast<const Page *>(entry);
        if (candidate.page_index != found_page_index) {
            return FileCacheAddressSpaceStatus::Corrupt;
        }
        if (candidate.state == state) {
            page = FileCacheAddressSpace::Snapshot(candidate);
            return FileCacheAddressSpaceStatus::Succeeded;
        }
        if (found_page_index == UINT64_MAX) {
            break;
        }
        search_page_index = found_page_index + OS_KERNEL_FILE_CACHE_ADDRESS_SPACE_SINGLE_UNIT;
    }
    return FileCacheAddressSpaceStatus::NotFound;
}

FileCacheAddressSpaceStatus FileCacheAddressSpace::Validate() const noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_ || this->heap_ == nullptr ||
        !FileCacheIdentityIsValid(this->statistics_.identity) ||
        this->index_.Validate() != SparsePageIndexStatus::Succeeded) {
        return this->initialized_ ? FileCacheAddressSpaceStatus::Corrupt
                                  : FileCacheAddressSpaceStatus::NotInitialized;
    }
    uint64_t resident_page_count = OS_KERNEL_FILE_CACHE_ADDRESS_SPACE_EMPTY_VALUE;
    uint64_t referenced_page_count = OS_KERNEL_FILE_CACHE_ADDRESS_SPACE_EMPTY_VALUE;
    uint64_t active_mapping_reference_count = OS_KERNEL_FILE_CACHE_ADDRESS_SPACE_EMPTY_VALUE;
    uint64_t loading_page_count = OS_KERNEL_FILE_CACHE_ADDRESS_SPACE_EMPTY_VALUE;
    uint64_t clean_page_count = OS_KERNEL_FILE_CACHE_ADDRESS_SPACE_EMPTY_VALUE;
    uint64_t dirty_page_count = OS_KERNEL_FILE_CACHE_ADDRESS_SPACE_EMPTY_VALUE;
    uint64_t writeback_page_count = OS_KERNEL_FILE_CACHE_ADDRESS_SPACE_EMPTY_VALUE;
    uint64_t error_page_count = OS_KERNEL_FILE_CACHE_ADDRESS_SPACE_EMPTY_VALUE;
    uint64_t search_page_index = OS_KERNEL_FILE_CACHE_ADDRESS_SPACE_EMPTY_VALUE;
    while (true) {
        uint64_t found_page_index = OS_KERNEL_FILE_CACHE_ADDRESS_SPACE_EMPTY_VALUE;
        void *entry = nullptr;
        const SparsePageIndexStatus find_status = this->index_.FindNext(
            search_page_index, UINT64_MAX, SparsePageIndexMark::Present, found_page_index, entry);
        if (find_status == SparsePageIndexStatus::NotFound) {
            break;
        }
        if (find_status != SparsePageIndexStatus::Succeeded || entry == nullptr) {
            return FileCacheAddressSpaceStatus::Corrupt;
        }
        const Page &stored_page = *static_cast<const Page *>(entry);
        if (stored_page.page_index != found_page_index ||
            (stored_page.physical_address &
             (OS_KERNEL_MEMORY_PAGE_SIZE_BYTES - OS_KERNEL_FILE_CACHE_ADDRESS_SPACE_SINGLE_UNIT)) !=
                OS_KERNEL_FILE_CACHE_ADDRESS_SPACE_EMPTY_VALUE ||
            stored_page.access_generation == OS_KERNEL_FILE_CACHE_ADDRESS_SPACE_EMPTY_VALUE ||
            !FileCacheAddressSpace::StateIsValid(stored_page.state)) {
            return FileCacheAddressSpaceStatus::Corrupt;
        }
        bool dirty_mark = false;
        bool writeback_mark = false;
        bool error_mark = false;
        if (this->index_.IsMarked(found_page_index, SparsePageIndexMark::Dirty, dirty_mark) !=
                SparsePageIndexStatus::Succeeded ||
            this->index_.IsMarked(found_page_index, SparsePageIndexMark::Writeback,
                                  writeback_mark) != SparsePageIndexStatus::Succeeded ||
            this->index_.IsMarked(found_page_index, SparsePageIndexMark::Error, error_mark) !=
                SparsePageIndexStatus::Succeeded ||
            dirty_mark != (stored_page.state == FileCachePageState::Dirty) ||
            writeback_mark != (stored_page.state == FileCachePageState::Writeback) ||
            error_mark != (stored_page.state == FileCachePageState::Error)) {
            return FileCacheAddressSpaceStatus::Corrupt;
        }
        ++resident_page_count;
        active_mapping_reference_count += stored_page.mapping_reference_count;
        if (stored_page.mapping_reference_count != OS_KERNEL_FILE_CACHE_ADDRESS_SPACE_EMPTY_VALUE) {
            ++referenced_page_count;
        }
        switch (stored_page.state) {
        case FileCachePageState::Loading:
            ++loading_page_count;
            break;
        case FileCachePageState::Clean:
            ++clean_page_count;
            break;
        case FileCachePageState::Dirty:
            ++dirty_page_count;
            break;
        case FileCachePageState::Writeback:
            ++writeback_page_count;
            break;
        case FileCachePageState::Error:
            ++error_page_count;
            break;
        }
        if (found_page_index == UINT64_MAX) {
            break;
        }
        search_page_index = found_page_index + OS_KERNEL_FILE_CACHE_ADDRESS_SPACE_SINGLE_UNIT;
    }
    const SparsePageIndexStatistics index_statistics = this->index_.Statistics();
    return resident_page_count == this->statistics_.resident_page_count &&
                   referenced_page_count == this->statistics_.referenced_page_count &&
                   active_mapping_reference_count ==
                       this->statistics_.active_mapping_reference_count &&
                   loading_page_count == this->statistics_.loading_page_count &&
                   clean_page_count == this->statistics_.clean_page_count &&
                   dirty_page_count == this->statistics_.dirty_page_count &&
                   writeback_page_count == this->statistics_.writeback_page_count &&
                   error_page_count == this->statistics_.error_page_count &&
                   resident_page_count == index_statistics.entry_count &&
                   dirty_page_count == index_statistics.dirty_entry_count &&
                   writeback_page_count == index_statistics.writeback_entry_count &&
                   error_page_count == index_statistics.error_entry_count &&
                   this->statistics_.peak_resident_page_count >= resident_page_count &&
                   this->statistics_.peak_active_mapping_reference_count >=
                       active_mapping_reference_count
               ? FileCacheAddressSpaceStatus::Succeeded
               : FileCacheAddressSpaceStatus::Corrupt;
}

FileCacheAddressSpaceStatistics FileCacheAddressSpace::Statistics() const noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_) {
        return FileCacheAddressSpaceStatistics{};
    }
    FileCacheAddressSpaceStatistics statistics = this->statistics_;
    statistics.index = this->index_.Statistics();
    return statistics;
}

FileCacheAddressSpaceStatus FileCacheAddressSpace::Destroy() noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_) {
        return FileCacheAddressSpaceStatus::NotInitialized;
    }
    if (this->statistics_.resident_page_count != OS_KERNEL_FILE_CACHE_ADDRESS_SPACE_EMPTY_VALUE ||
        this->statistics_.active_mapping_reference_count !=
            OS_KERNEL_FILE_CACHE_ADDRESS_SPACE_EMPTY_VALUE) {
        return FileCacheAddressSpaceStatus::PagesRemain;
    }
    const SparsePageIndexStatus index_status = this->index_.Destroy();
    if (index_status != SparsePageIndexStatus::Succeeded) {
        return this->MapIndexStatus(index_status);
    }
    this->heap_ = nullptr;
    this->statistics_ = FileCacheAddressSpaceStatistics{};
    this->access_generation_ = OS_KERNEL_FILE_CACHE_ADDRESS_SPACE_EMPTY_VALUE;
    this->initialized_ = false;
    return FileCacheAddressSpaceStatus::Succeeded;
}

bool FileCacheAddressSpace::StateIsValid(const FileCachePageState state) noexcept {
    return static_cast<uint64_t>(state) <= static_cast<uint64_t>(FileCachePageState::Error);
}

bool FileCacheAddressSpace::TransitionIsValid(const FileCachePageState current_state,
                                              const FileCachePageState new_state) noexcept {
    return (current_state == FileCachePageState::Loading &&
            new_state == FileCachePageState::Clean) ||
           (current_state == FileCachePageState::Clean && new_state == FileCachePageState::Dirty) ||
           (current_state == FileCachePageState::Dirty &&
            new_state == FileCachePageState::Writeback) ||
           (current_state == FileCachePageState::Error &&
            (new_state == FileCachePageState::Dirty ||
             new_state == FileCachePageState::Writeback)) ||
           (current_state == FileCachePageState::Writeback &&
            (new_state == FileCachePageState::Clean || new_state == FileCachePageState::Error));
}

bool FileCacheAddressSpace::StateHasMark(const FileCachePageState state) noexcept {
    return state == FileCachePageState::Dirty || state == FileCachePageState::Writeback ||
           state == FileCachePageState::Error;
}

SparsePageIndexMark FileCacheAddressSpace::MarkForState(const FileCachePageState state) noexcept {
    switch (state) {
    case FileCachePageState::Loading:
        return SparsePageIndexMark::Present;
    case FileCachePageState::Dirty:
        return SparsePageIndexMark::Dirty;
    case FileCachePageState::Writeback:
        return SparsePageIndexMark::Writeback;
    case FileCachePageState::Error:
        return SparsePageIndexMark::Error;
    case FileCachePageState::Clean:
        return SparsePageIndexMark::Present;
    }
    return SparsePageIndexMark::Present;
}

FileCachePageSnapshot FileCacheAddressSpace::Snapshot(const Page &page) noexcept {
    return FileCachePageSnapshot{
        .page_index = page.page_index,
        .physical_address = page.physical_address,
        .mapping_reference_count = page.mapping_reference_count,
        .access_generation = page.access_generation,
        .state = page.state,
    };
}

FileCacheAddressSpaceStatus FileCacheAddressSpace::LookupPage(const uint64_t page_index,
                                                              Page *&page) const noexcept {
    page = nullptr;
    void *entry = nullptr;
    const SparsePageIndexStatus status = this->index_.Lookup(page_index, entry);
    if (status != SparsePageIndexStatus::Succeeded) {
        return this->MapIndexStatus(status);
    }
    if (entry == nullptr) {
        return FileCacheAddressSpaceStatus::Corrupt;
    }
    page = static_cast<Page *>(entry);
    return page->page_index == page_index ? FileCacheAddressSpaceStatus::Succeeded
                                          : FileCacheAddressSpaceStatus::Corrupt;
}

FileCacheAddressSpaceStatus
FileCacheAddressSpace::SetStateMark(const uint64_t page_index,
                                    const FileCachePageState state) noexcept {
    if (!FileCacheAddressSpace::StateHasMark(state)) {
        return FileCacheAddressSpaceStatus::Succeeded;
    }
    return this->MapIndexStatus(
        this->index_.SetMark(page_index, FileCacheAddressSpace::MarkForState(state)));
}

FileCacheAddressSpaceStatus
FileCacheAddressSpace::ClearStateMark(const uint64_t page_index,
                                      const FileCachePageState state) noexcept {
    if (!FileCacheAddressSpace::StateHasMark(state)) {
        return FileCacheAddressSpaceStatus::Succeeded;
    }
    return this->MapIndexStatus(
        this->index_.ClearMark(page_index, FileCacheAddressSpace::MarkForState(state)));
}

void FileCacheAddressSpace::IncrementStateCount(const FileCachePageState state) noexcept {
    switch (state) {
    case FileCachePageState::Loading:
        ++this->statistics_.loading_page_count;
        break;
    case FileCachePageState::Clean:
        ++this->statistics_.clean_page_count;
        break;
    case FileCachePageState::Dirty:
        ++this->statistics_.dirty_page_count;
        break;
    case FileCachePageState::Writeback:
        ++this->statistics_.writeback_page_count;
        break;
    case FileCachePageState::Error:
        ++this->statistics_.error_page_count;
        break;
    }
}

bool FileCacheAddressSpace::DecrementStateCount(const FileCachePageState state) noexcept {
    uint64_t *state_count = nullptr;
    switch (state) {
    case FileCachePageState::Loading:
        state_count = &this->statistics_.loading_page_count;
        break;
    case FileCachePageState::Clean:
        state_count = &this->statistics_.clean_page_count;
        break;
    case FileCachePageState::Dirty:
        state_count = &this->statistics_.dirty_page_count;
        break;
    case FileCachePageState::Writeback:
        state_count = &this->statistics_.writeback_page_count;
        break;
    case FileCachePageState::Error:
        state_count = &this->statistics_.error_page_count;
        break;
    }
    if (state_count == nullptr || *state_count == OS_KERNEL_FILE_CACHE_ADDRESS_SPACE_EMPTY_VALUE) {
        return false;
    }
    --(*state_count);
    return true;
}

FileCacheAddressSpaceStatus
FileCacheAddressSpace::MapIndexStatus(const SparsePageIndexStatus status) const noexcept {
    switch (status) {
    case SparsePageIndexStatus::Succeeded:
        return FileCacheAddressSpaceStatus::Succeeded;
    case SparsePageIndexStatus::NotInitialized:
        return FileCacheAddressSpaceStatus::NotInitialized;
    case SparsePageIndexStatus::AlreadyInitialized:
        return FileCacheAddressSpaceStatus::AlreadyInitialized;
    case SparsePageIndexStatus::InvalidEntry:
    case SparsePageIndexStatus::InvalidRange:
    case SparsePageIndexStatus::InvalidMark:
        return FileCacheAddressSpaceStatus::InvalidPage;
    case SparsePageIndexStatus::AlreadyExists:
        return FileCacheAddressSpaceStatus::AlreadyExists;
    case SparsePageIndexStatus::NotFound:
        return FileCacheAddressSpaceStatus::NotFound;
    case SparsePageIndexStatus::AllocationFailed:
    case SparsePageIndexStatus::InvalidHeap:
        return FileCacheAddressSpaceStatus::AllocationFailed;
    case SparsePageIndexStatus::MetadataReleaseFailed:
        return FileCacheAddressSpaceStatus::MetadataReleaseFailed;
    case SparsePageIndexStatus::EntriesRemain:
        return FileCacheAddressSpaceStatus::PagesRemain;
    case SparsePageIndexStatus::Corrupt:
        return FileCacheAddressSpaceStatus::Corrupt;
    }
    return FileCacheAddressSpaceStatus::Corrupt;
}

}

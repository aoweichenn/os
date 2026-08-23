#include <os/kernel/memory/file_page_cache.hpp>

// freestanding 目标不提供 <new>；这里只声明标准 placement 形式以启动动态 record 生命周期。
inline void *operator new(decltype(sizeof(0)), void *const storage) noexcept { return storage; }
inline void operator delete(void *, void *) noexcept {}

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_FILE_PAGE_CACHE_SINGLE_UNIT = 1ULL;
constexpr uint64_t OS_KERNEL_FILE_PAGE_CACHE_ZERO_BYTE = 0ULL;
constexpr uint64_t OS_KERNEL_FILE_PAGE_CACHE_RECORD_ALIGNMENT_BYTES = 64ULL;
constexpr uint64_t OS_KERNEL_FILE_PAGE_CACHE_RATIO_HALF_DIVISOR = 2ULL;

}

struct alignas(OS_KERNEL_FILE_PAGE_CACHE_RECORD_ALIGNMENT_BYTES)
    FilePageCache::AddressSpaceRecord final {
    FileIdentity identity;
    FileCacheAddressSpace address_space;
    uint64_t size_bytes;
    bool size_known;
    AddressSpaceRecord *next;
};

FilePageCacheStatus
FilePageCache::Initialize(KernelHeap &metadata_heap, const uint64_t capacity,
                          const uint64_t dirty_page_limit, PhysicalFrameAllocator &frame_allocator,
                          void *const page_access_context,
                          const FilePageAccessOperation page_access_operation) noexcept {
    SpinLockGuard guard{this->lock_};
    if (this->initialized_) {
        return FilePageCacheStatus::AlreadyInitialized;
    }
    if (metadata_heap.Validate() != KernelHeapStatus::Succeeded) {
        return FilePageCacheStatus::InvalidStorage;
    }
    if (capacity == OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE ||
        dirty_page_limit == OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE || dirty_page_limit > capacity) {
        return FilePageCacheStatus::InvalidCapacity;
    }
    if (page_access_operation == nullptr) {
        return FilePageCacheStatus::InvalidDependency;
    }
    this->metadata_heap_ = &metadata_heap;
    this->address_spaces_ = nullptr;
    this->capacity_ = capacity;
    this->background_dirty_page_threshold_ =
        dirty_page_limit / OS_KERNEL_FILE_PAGE_CACHE_RATIO_HALF_DIVISOR;
    if (this->background_dirty_page_threshold_ == OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE) {
        this->background_dirty_page_threshold_ = OS_KERNEL_FILE_PAGE_CACHE_SINGLE_UNIT;
    }
    this->background_dirty_page_target_ =
        this->background_dirty_page_threshold_ / OS_KERNEL_FILE_PAGE_CACHE_RATIO_HALF_DIVISOR;
    this->dirty_page_limit_ = dirty_page_limit;
    this->frame_allocator_ = &frame_allocator;
    this->page_access_context_ = page_access_context;
    this->page_access_operation_ = page_access_operation;
    this->load_wait_operations_ = FilePageLoadWaitOperations{};
    this->readahead_feedback_operations_ = FilePageReadaheadFeedbackOperations{};
    this->access_generation_ = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
    this->statistics_ = FilePageCacheStatistics{};
    this->statistics_.capacity = capacity;
    this->statistics_.background_dirty_page_threshold = this->background_dirty_page_threshold_;
    this->statistics_.background_dirty_page_target = this->background_dirty_page_target_;
    this->statistics_.dirty_page_limit = dirty_page_limit;
    this->background_writeback_requested_ = false;
    this->background_writeback_paused_ = false;
    this->forced_background_writeback_requested_ = false;
    this->load_wait_operations_configured_ = false;
    this->readahead_feedback_operations_configured_ = false;
    this->initialized_ = true;
    return FilePageCacheStatus::Succeeded;
}

FilePageCacheStatus FilePageCache::ConfigureReadaheadFeedback(
    const FilePageReadaheadFeedbackOperations &operations) noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_) {
        return FilePageCacheStatus::NotInitialized;
    }
    if (this->readahead_feedback_operations_configured_) {
        return FilePageCacheStatus::AlreadyInitialized;
    }
    if (operations.context == nullptr || operations.record == nullptr) {
        return FilePageCacheStatus::InvalidDependency;
    }
    this->readahead_feedback_operations_ = operations;
    this->readahead_feedback_operations_configured_ = true;
    return FilePageCacheStatus::Succeeded;
}

FilePageCacheStatus
FilePageCache::ConfigureLoadingWait(const FilePageLoadWaitOperations &operations) noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_) {
        return FilePageCacheStatus::NotInitialized;
    }
    if (this->load_wait_operations_configured_) {
        return FilePageCacheStatus::AlreadyInitialized;
    }
    if (operations.owner_available == nullptr || operations.available == nullptr ||
        operations.begin == nullptr || operations.register_waiter == nullptr ||
        operations.wait == nullptr || operations.waiter_count == nullptr ||
        operations.complete == nullptr) {
        return FilePageCacheStatus::InvalidDependency;
    }
    this->load_wait_operations_ = operations;
    this->load_wait_operations_configured_ = true;
    return FilePageCacheStatus::Succeeded;
}

FilePageCacheStatus FilePageCache::Acquire(const FilePageIdentity &identity,
                                           void *const reader_context,
                                           const FilePageReadOperation read_operation,
                                           uint64_t &physical_address, bool &cache_hit) noexcept {
    bool prefetched_hit = false;
    return this->Acquire(identity, reader_context, read_operation, FilePageAcquireIntent::Demand,
                         FileReadaheadPageTag{}, physical_address, cache_hit, prefetched_hit);
}

FilePageCacheStatus FilePageCache::Acquire(const FilePageIdentity &identity,
                                           void *const reader_context,
                                           const FilePageReadOperation read_operation,
                                           const FilePageAcquireIntent intent,
                                           const FileReadaheadPageTag &readahead_tag,
                                           uint64_t &physical_address, bool &cache_hit,
                                           bool &prefetched_hit) noexcept {
    physical_address = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
    cache_hit = false;
    prefetched_hit = false;
    this->lock_.Lock();
    const auto unlock_and_return = [this](const FilePageCacheStatus status) noexcept {
        this->lock_.Unlock();
        return status;
    };
    if (!this->initialized_ || this->metadata_heap_ == nullptr ||
        this->frame_allocator_ == nullptr || this->page_access_operation_ == nullptr) {
        return unlock_and_return(FilePageCacheStatus::NotInitialized);
    }
    if (!FileCacheIdentityIsValid(identity.file)) {
        return unlock_and_return(FilePageCacheStatus::InvalidIdentity);
    }
    if (reader_context == nullptr || read_operation == nullptr) {
        return unlock_and_return(FilePageCacheStatus::InvalidReader);
    }
    if (intent != FilePageAcquireIntent::Demand && intent != FilePageAcquireIntent::Prefetch) {
        return unlock_and_return(FilePageCacheStatus::InvalidDependency);
    }
    if ((intent == FilePageAcquireIntent::Demand && !FileReadaheadPageTagIsEmpty(readahead_tag)) ||
        (intent == FilePageAcquireIntent::Prefetch &&
         (!FileReadaheadPageTagIsValid(readahead_tag) ||
          !this->readahead_feedback_operations_configured_))) {
        return unlock_and_return(FilePageCacheStatus::InvalidDependency);
    }
    if (intent == FilePageAcquireIntent::Prefetch) {
        if (this->statistics_.prefetch_acquire_count == UINT64_MAX) {
            return unlock_and_return(FilePageCacheStatus::Corrupt);
        }
        ++this->statistics_.prefetch_acquire_count;
    }

    AddressSpaceRecord *record = this->FindAddressSpace(identity.file);
    if (record != nullptr) {
        FileCachePageSnapshot page{};
        const FileCacheAddressSpaceStatus lookup_status =
            record->address_space.Lookup(identity.page_index, page);
        if (lookup_status == FileCacheAddressSpaceStatus::Succeeded) {
            if (page.state == FileCachePageState::Loading) {
                ++this->statistics_.loading_collision_count;
                if (!this->LoadingWaitAvailable()) {
                    return unlock_and_return(FilePageCacheStatus::EntryBusy);
                }
                FilePageLoadToken waiter_token{};
                if (!this->load_wait_operations_.register_waiter(
                        this->load_wait_operations_.context, identity, page.physical_address,
                        page.access_generation, waiter_token)) {
                    return unlock_and_return(FilePageCacheStatus::LoadingWaitUnavailable);
                }
                this->lock_.Unlock();
                FilePageCacheStatus load_result = FilePageCacheStatus::LoadingWaitFailed;
                if (!this->load_wait_operations_.wait(this->load_wait_operations_.context,
                                                      waiter_token, load_result)) {
                    return FilePageCacheStatus::LoadingWaitFailed;
                }
                if (load_result != FilePageCacheStatus::Succeeded) {
                    return load_result;
                }
                this->lock_.Lock();
                record = this->FindAddressSpace(identity.file);
                FileCachePageSnapshot completed_page{};
                if (record == nullptr ||
                    record->address_space.Lookup(identity.page_index, completed_page) !=
                        FileCacheAddressSpaceStatus::Succeeded ||
                    completed_page.physical_address != page.physical_address ||
                    completed_page.state == FileCachePageState::Loading ||
                    completed_page.mapping_reference_count ==
                        OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE) {
                    return unlock_and_return(FilePageCacheStatus::Corrupt);
                }
                if (intent == FilePageAcquireIntent::Prefetch &&
                    this->statistics_.prefetch_existing_page_count == UINT64_MAX) {
                    const FileCacheAddressSpaceStatus release_status =
                        record->address_space.Release(identity.page_index, page.physical_address);
                    if (release_status == FileCacheAddressSpaceStatus::Succeeded &&
                        this->statistics_.active_mapping_reference_count !=
                            OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE) {
                        --this->statistics_.active_mapping_reference_count;
                        if (completed_page.mapping_reference_count ==
                                OS_KERNEL_FILE_PAGE_CACHE_SINGLE_UNIT &&
                            this->statistics_.referenced_page_count !=
                                OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE) {
                            --this->statistics_.referenced_page_count;
                        }
                    }
                    return unlock_and_return(FilePageCacheStatus::Corrupt);
                }
                const uint64_t generation = this->NextAccessGeneration();
                if (generation == OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE ||
                    record->address_space.Touch(identity.page_index, page.physical_address,
                                                generation) !=
                        FileCacheAddressSpaceStatus::Succeeded ||
                    !this->ConsumePrefetchedIfDemand(*record, completed_page, intent,
                                                     prefetched_hit)) {
                    const FileCacheAddressSpaceStatus release_status =
                        record->address_space.Release(identity.page_index, page.physical_address);
                    if (release_status == FileCacheAddressSpaceStatus::Succeeded &&
                        this->statistics_.active_mapping_reference_count !=
                            OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE) {
                        --this->statistics_.active_mapping_reference_count;
                        if (completed_page.mapping_reference_count ==
                                OS_KERNEL_FILE_PAGE_CACHE_SINGLE_UNIT &&
                            this->statistics_.referenced_page_count !=
                                OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE) {
                            --this->statistics_.referenced_page_count;
                        }
                    }
                    return unlock_and_return(FilePageCacheStatus::Corrupt);
                }
                ++this->statistics_.hit_count;
                ++this->statistics_.successful_acquire_count;
                if (intent == FilePageAcquireIntent::Prefetch) {
                    ++this->statistics_.prefetch_existing_page_count;
                }
                physical_address = page.physical_address;
                cache_hit = true;
                return unlock_and_return(FilePageCacheStatus::Succeeded);
            }
            const uint64_t generation = this->NextAccessGeneration();
            if (generation == OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE ||
                (intent == FilePageAcquireIntent::Prefetch &&
                 this->statistics_.prefetch_existing_page_count == UINT64_MAX)) {
                return unlock_and_return(FilePageCacheStatus::Corrupt);
            }
            const FileCacheAddressSpaceStatus retain_status =
                record->address_space.Retain(identity.page_index, page.physical_address);
            if (retain_status != FileCacheAddressSpaceStatus::Succeeded) {
                return unlock_and_return(this->MapAddressSpaceStatus(retain_status));
            }
            if (record->address_space.Touch(identity.page_index, page.physical_address,
                                            generation) != FileCacheAddressSpaceStatus::Succeeded ||
                !this->ConsumePrefetchedIfDemand(*record, page, intent, prefetched_hit)) {
                static_cast<void>(
                    record->address_space.Release(identity.page_index, page.physical_address));
                return unlock_and_return(FilePageCacheStatus::Corrupt);
            }
            if (page.mapping_reference_count == OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE) {
                ++this->statistics_.referenced_page_count;
            }
            ++this->statistics_.active_mapping_reference_count;
            ++this->statistics_.hit_count;
            ++this->statistics_.successful_acquire_count;
            if (intent == FilePageAcquireIntent::Prefetch) {
                ++this->statistics_.prefetch_existing_page_count;
            }
            physical_address = page.physical_address;
            cache_hit = true;
            return unlock_and_return(FilePageCacheStatus::Succeeded);
        }
        if (lookup_status != FileCacheAddressSpaceStatus::NotFound) {
            return unlock_and_return(this->MapAddressSpaceStatus(lookup_status));
        }
    }

    ++this->statistics_.miss_count;
    uint64_t candidate_physical_address = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
    bool reusable_frame_available = false;
    if (this->statistics_.resident_page_count >= this->capacity_) {
        AddressSpaceRecord *eviction_record = nullptr;
        FileCachePageSnapshot eviction_page{};
        const FilePageCacheStatus select_status =
            this->SelectEvictionCandidate(nullptr, nullptr, eviction_record, eviction_page);
        if (select_status == FilePageCacheStatus::EntryBusy ||
            select_status == FilePageCacheStatus::DirtyPagesRemain) {
            return unlock_and_return(FilePageCacheStatus::CapacityExhausted);
        }
        if (select_status != FilePageCacheStatus::Succeeded || eviction_record == nullptr) {
            return unlock_and_return(select_status);
        }
        const FilePageCacheStatus eviction_status =
            this->Evict(*eviction_record, eviction_page, false, candidate_physical_address);
        if (eviction_status != FilePageCacheStatus::Succeeded) {
            return unlock_and_return(eviction_status);
        }
        reusable_frame_available = true;
        record = this->FindAddressSpace(identity.file);
    }
    if (!reusable_frame_available) {
        PhysicalFrame frame{};
        if (this->frame_allocator_->Allocate(frame) != PhysicalFrameAllocatorStatus::Succeeded) {
            return unlock_and_return(FilePageCacheStatus::FrameAllocationFailed);
        }
        candidate_physical_address = frame.physical_address;
    }
    if (record == nullptr) {
        const FilePageCacheStatus record_status = this->EnsureAddressSpace(identity.file, record);
        if (record_status != FilePageCacheStatus::Succeeded || record == nullptr) {
            const PhysicalFrameAllocatorStatus release_status = this->frame_allocator_->Release(
                PhysicalFrame{.physical_address = candidate_physical_address});
            ++this->statistics_.failed_load_count;
            return unlock_and_return(release_status == PhysicalFrameAllocatorStatus::Succeeded
                                         ? record_status
                                         : FilePageCacheStatus::FrameReleaseFailed);
        }
    }
    const FileCacheAddressSpaceStatus insertion_status = record->address_space.Insert(
        identity.page_index, candidate_physical_address, FileCachePageState::Loading);
    if (insertion_status != FileCacheAddressSpaceStatus::Succeeded) {
        const PhysicalFrameAllocatorStatus release_status = this->frame_allocator_->Release(
            PhysicalFrame{.physical_address = candidate_physical_address});
        static_cast<void>(this->DestroyAddressSpaceIfEmpty(*record));
        ++this->statistics_.failed_load_count;
        if (insertion_status == FileCacheAddressSpaceStatus::AllocationFailed) {
            ++this->statistics_.metadata_allocation_failure_count;
        }
        return unlock_and_return(release_status != PhysicalFrameAllocatorStatus::Succeeded
                                     ? FilePageCacheStatus::FrameReleaseFailed
                                 : insertion_status == FileCacheAddressSpaceStatus::AllocationFailed
                                     ? FilePageCacheStatus::MetadataAllocationFailed
                                     : FilePageCacheStatus::Corrupt);
    }

    ++this->statistics_.resident_page_count;
    ++this->statistics_.loading_page_count;
    if (this->statistics_.peak_resident_page_count < this->statistics_.resident_page_count) {
        this->statistics_.peak_resident_page_count = this->statistics_.resident_page_count;
    }
    FilePageLoadToken owner_token{};
    if (this->LoadingOwnerAvailable()) {
        FileCachePageSnapshot loading_page{};
        if (record->address_space.Lookup(identity.page_index, loading_page) !=
                FileCacheAddressSpaceStatus::Succeeded ||
            loading_page.physical_address != candidate_physical_address ||
            loading_page.state != FileCachePageState::Loading ||
            !this->load_wait_operations_.begin(this->load_wait_operations_.context, identity,
                                               candidate_physical_address,
                                               loading_page.access_generation, owner_token)) {
            const FileCacheAddressSpaceStatus discard_status =
                record->address_space.Discard(identity.page_index, candidate_physical_address);
            const PhysicalFrameAllocatorStatus release_status = this->frame_allocator_->Release(
                PhysicalFrame{.physical_address = candidate_physical_address});
            if (discard_status != FileCacheAddressSpaceStatus::Succeeded ||
                release_status != PhysicalFrameAllocatorStatus::Succeeded ||
                this->statistics_.resident_page_count == OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE ||
                this->statistics_.loading_page_count == OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE) {
                return unlock_and_return(FilePageCacheStatus::FrameReleaseFailed);
            }
            --this->statistics_.resident_page_count;
            --this->statistics_.loading_page_count;
            ++this->statistics_.failed_load_count;
            static_cast<void>(this->DestroyAddressSpaceIfEmpty(*record));
            return unlock_and_return(FilePageCacheStatus::LoadingWaitUnavailable);
        }
    }
    this->lock_.Unlock();

    uint8_t *const page_bytes =
        this->page_access_operation_(this->page_access_context_, candidate_physical_address);
    bool source_read_succeeded = page_bytes != nullptr;
    if (page_bytes != nullptr) {
        for (uint64_t byte_index = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
             byte_index < OS_KERNEL_MEMORY_PAGE_SIZE_BYTES; ++byte_index) {
            page_bytes[byte_index] = static_cast<uint8_t>(OS_KERNEL_FILE_PAGE_CACHE_ZERO_BYTE);
        }
        source_read_succeeded =
            read_operation(reader_context, identity, page_bytes, OS_KERNEL_MEMORY_PAGE_SIZE_BYTES);
    }

    this->lock_.Lock();
    ++this->statistics_.unlocked_fill_count;
    record = this->FindAddressSpace(identity.file);
    FileCachePageSnapshot loading_page{};
    if (record == nullptr ||
        record->address_space.Lookup(identity.page_index, loading_page) !=
            FileCacheAddressSpaceStatus::Succeeded ||
        loading_page.physical_address != candidate_physical_address ||
        loading_page.state != FileCachePageState::Loading ||
        loading_page.mapping_reference_count != OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE ||
        this->statistics_.loading_page_count == OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE) {
        static_cast<void>(this->CompleteLoadingWait(owner_token, FilePageCacheStatus::Corrupt));
        return unlock_and_return(FilePageCacheStatus::Corrupt);
    }
    if (!source_read_succeeded) {
        const FileCacheAddressSpaceStatus discard_status =
            record->address_space.Discard(identity.page_index, candidate_physical_address);
        const PhysicalFrameAllocatorStatus release_status = this->frame_allocator_->Release(
            PhysicalFrame{.physical_address = candidate_physical_address});
        if (discard_status != FileCacheAddressSpaceStatus::Succeeded ||
            release_status != PhysicalFrameAllocatorStatus::Succeeded ||
            this->statistics_.resident_page_count == OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE) {
            static_cast<void>(
                this->CompleteLoadingWait(owner_token, FilePageCacheStatus::FrameReleaseFailed));
            return unlock_and_return(FilePageCacheStatus::FrameReleaseFailed);
        }
        --this->statistics_.resident_page_count;
        --this->statistics_.loading_page_count;
        ++this->statistics_.failed_load_count;
        const FilePageCacheStatus destroy_status = this->DestroyAddressSpaceIfEmpty(*record);
        if (destroy_status != FilePageCacheStatus::Succeeded) {
            static_cast<void>(this->CompleteLoadingWait(owner_token, destroy_status));
            return unlock_and_return(destroy_status);
        }
        const FilePageCacheStatus failure_status = page_bytes == nullptr
                                                       ? FilePageCacheStatus::FrameAccessFailed
                                                       : FilePageCacheStatus::SourceReadFailed;
        if (!this->CompleteLoadingWait(owner_token, failure_status)) {
            return unlock_and_return(FilePageCacheStatus::LoadingWaitFailed);
        }
        return unlock_and_return(failure_status);
    }

    const uint64_t generation = this->NextAccessGeneration();
    if (generation == OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE ||
        record->address_space.Transition(identity.page_index, candidate_physical_address,
                                         FileCachePageState::Loading, FileCachePageState::Clean) !=
            FileCacheAddressSpaceStatus::Succeeded ||
        record->address_space.Retain(identity.page_index, candidate_physical_address) !=
            FileCacheAddressSpaceStatus::Succeeded ||
        record->address_space.Touch(identity.page_index, candidate_physical_address, generation) !=
            FileCacheAddressSpaceStatus::Succeeded) {
        FileCachePageSnapshot completed_page{};
        if (record->address_space.Lookup(identity.page_index, completed_page) ==
                FileCacheAddressSpaceStatus::Succeeded &&
            completed_page.mapping_reference_count != OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE) {
            static_cast<void>(
                record->address_space.Release(identity.page_index, candidate_physical_address));
        }
        static_cast<void>(this->CompleteLoadingWait(owner_token, FilePageCacheStatus::Corrupt));
        return unlock_and_return(FilePageCacheStatus::Corrupt);
    }
    bool newly_prefetched = false;
    if (intent == FilePageAcquireIntent::Prefetch &&
        (this->statistics_.prefetched_page_count == UINT64_MAX ||
         this->statistics_.successful_prefetch_load_count == UINT64_MAX ||
         record->address_space.MarkPrefetched(identity.page_index, candidate_physical_address,
                                              readahead_tag, newly_prefetched) !=
             FileCacheAddressSpaceStatus::Succeeded ||
         !newly_prefetched)) {
        static_cast<void>(
            record->address_space.Release(identity.page_index, candidate_physical_address));
        --this->statistics_.loading_page_count;
        ++this->statistics_.successful_load_count;
        static_cast<void>(this->CompleteLoadingWait(owner_token, FilePageCacheStatus::Corrupt));
        return unlock_and_return(FilePageCacheStatus::Corrupt);
    }
    --this->statistics_.loading_page_count;
    ++this->statistics_.referenced_page_count;
    ++this->statistics_.active_mapping_reference_count;
    ++this->statistics_.successful_load_count;
    if (newly_prefetched) {
        ++this->statistics_.prefetched_page_count;
        ++this->statistics_.successful_prefetch_load_count;
    }
    uint64_t waiter_count = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
    if (!this->LoadingWaiterCount(owner_token, waiter_count) ||
        this->statistics_.active_mapping_reference_count > UINT64_MAX - waiter_count) {
        static_cast<void>(
            record->address_space.Release(identity.page_index, candidate_physical_address));
        if (newly_prefetched) {
            FileReadaheadPageTag consumed_tag{};
            bool consumed_prefetched = false;
            static_cast<void>(record->address_space.ConsumePrefetched(
                identity.page_index, candidate_physical_address, consumed_tag,
                consumed_prefetched));
            if (consumed_prefetched) {
                --this->statistics_.prefetched_page_count;
                --this->statistics_.successful_prefetch_load_count;
            }
        }
        --this->statistics_.referenced_page_count;
        --this->statistics_.active_mapping_reference_count;
        static_cast<void>(this->CompleteLoadingWait(owner_token, FilePageCacheStatus::Corrupt));
        return unlock_and_return(FilePageCacheStatus::LoadingWaitFailed);
    }
    uint64_t reserved_waiter_count = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
    while (reserved_waiter_count < waiter_count &&
           record->address_space.Retain(identity.page_index, candidate_physical_address) ==
               FileCacheAddressSpaceStatus::Succeeded) {
        ++reserved_waiter_count;
    }
    if (reserved_waiter_count != waiter_count) {
        while (reserved_waiter_count != OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE) {
            static_cast<void>(
                record->address_space.Release(identity.page_index, candidate_physical_address));
            --reserved_waiter_count;
        }
        static_cast<void>(
            record->address_space.Release(identity.page_index, candidate_physical_address));
        if (newly_prefetched) {
            FileReadaheadPageTag consumed_tag{};
            bool consumed_prefetched = false;
            static_cast<void>(record->address_space.ConsumePrefetched(
                identity.page_index, candidate_physical_address, consumed_tag,
                consumed_prefetched));
            if (consumed_prefetched) {
                --this->statistics_.prefetched_page_count;
                --this->statistics_.successful_prefetch_load_count;
            }
        }
        --this->statistics_.referenced_page_count;
        --this->statistics_.active_mapping_reference_count;
        static_cast<void>(this->CompleteLoadingWait(owner_token, FilePageCacheStatus::Corrupt));
        return unlock_and_return(FilePageCacheStatus::Corrupt);
    }
    this->statistics_.active_mapping_reference_count += waiter_count;
    ++this->statistics_.successful_acquire_count;
    physical_address = candidate_physical_address;
    if (!this->CompleteLoadingWait(owner_token, FilePageCacheStatus::Succeeded)) {
        return unlock_and_return(FilePageCacheStatus::LoadingWaitFailed);
    }
    return unlock_and_return(FilePageCacheStatus::Succeeded);
}

FilePageCacheStatus FilePageCache::Release(const FilePageIdentity &identity,
                                           const uint64_t physical_address) noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_) {
        return FilePageCacheStatus::NotInitialized;
    }
    AddressSpaceRecord *const record = this->FindAddressSpace(identity.file);
    if (record == nullptr) {
        return FilePageCacheStatus::MappingNotFound;
    }
    FileCachePageSnapshot page{};
    const FileCacheAddressSpaceStatus lookup_status =
        record->address_space.Lookup(identity.page_index, page);
    if (lookup_status != FileCacheAddressSpaceStatus::Succeeded ||
        page.physical_address != physical_address) {
        return lookup_status == FileCacheAddressSpaceStatus::NotFound
                   ? FilePageCacheStatus::MappingNotFound
                   : FilePageCacheStatus::Corrupt;
    }
    if (page.mapping_reference_count == OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE ||
        this->statistics_.active_mapping_reference_count == OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE) {
        return FilePageCacheStatus::ReferenceUnderflow;
    }
    const FileCacheAddressSpaceStatus release_status =
        record->address_space.Release(identity.page_index, physical_address);
    if (release_status != FileCacheAddressSpaceStatus::Succeeded) {
        return this->MapAddressSpaceStatus(release_status);
    }
    --this->statistics_.active_mapping_reference_count;
    ++this->statistics_.release_count;
    if (page.mapping_reference_count == OS_KERNEL_FILE_PAGE_CACHE_SINGLE_UNIT) {
        if (this->statistics_.referenced_page_count == OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE) {
            return FilePageCacheStatus::Corrupt;
        }
        --this->statistics_.referenced_page_count;
    }
    return FilePageCacheStatus::Succeeded;
}

FilePageCacheStatus FilePageCache::MarkDirty(const FilePageIdentity &identity,
                                             const uint64_t physical_address) noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_) {
        return FilePageCacheStatus::NotInitialized;
    }
    AddressSpaceRecord *const record = this->FindAddressSpace(identity.file);
    if (record == nullptr) {
        return FilePageCacheStatus::MappingNotFound;
    }
    FileCachePageSnapshot page{};
    const FileCacheAddressSpaceStatus lookup_status =
        record->address_space.Lookup(identity.page_index, page);
    if (lookup_status != FileCacheAddressSpaceStatus::Succeeded ||
        page.physical_address != physical_address) {
        return lookup_status == FileCacheAddressSpaceStatus::NotFound
                   ? FilePageCacheStatus::MappingNotFound
                   : FilePageCacheStatus::Corrupt;
    }
    if (page.state == FileCachePageState::Dirty) {
        return FilePageCacheStatus::Succeeded;
    }
    if (page.state == FileCachePageState::Loading || page.state == FileCachePageState::Writeback) {
        return FilePageCacheStatus::EntryBusy;
    }
    uint64_t outstanding_dirty_page_count = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
    if (!this->OutstandingDirtyPageCount(outstanding_dirty_page_count)) {
        return FilePageCacheStatus::Corrupt;
    }
    if (page.state == FileCachePageState::Clean &&
        outstanding_dirty_page_count >= this->dirty_page_limit_) {
        ++this->statistics_.dirty_limit_rejection_count;
        ++this->statistics_.dirty_backpressure_count;
        return FilePageCacheStatus::DirtyLimitReached;
    }
    const FileCachePageState expected_state = page.state;
    if (expected_state != FileCachePageState::Clean &&
        expected_state != FileCachePageState::Error) {
        return FilePageCacheStatus::Corrupt;
    }
    if (record->address_space.Transition(identity.page_index, physical_address, expected_state,
                                         FileCachePageState::Dirty) !=
        FileCacheAddressSpaceStatus::Succeeded) {
        return FilePageCacheStatus::Corrupt;
    }
    if (expected_state == FileCachePageState::Error) {
        if (this->statistics_.error_page_count == OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE) {
            return FilePageCacheStatus::Corrupt;
        }
        --this->statistics_.error_page_count;
    }
    ++this->statistics_.dirty_page_count;
    ++this->statistics_.mark_dirty_count;
    this->RefreshBackgroundWritebackRequest();
    return FilePageCacheStatus::Succeeded;
}

FilePageCacheStatus FilePageCache::Writeback(void *const writer_context,
                                             const FilePageWriteOperation write_operation,
                                             const uint64_t maximum_page_count,
                                             uint64_t &written_page_count) noexcept {
    return this->WritebackInternal(nullptr, OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE, UINT64_MAX,
                                   writer_context, write_operation, maximum_page_count,
                                   written_page_count);
}

FilePageCacheStatus FilePageCache::WritebackFile(
    const FileIdentity &identity, const uint64_t first_page_index, const uint64_t last_page_index,
    void *const writer_context, const FilePageWriteOperation write_operation,
    const uint64_t maximum_page_count, uint64_t &written_page_count) noexcept {
    written_page_count = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
    if (!FileCacheIdentityIsValid(identity)) {
        return FilePageCacheStatus::InvalidIdentity;
    }
    if (first_page_index > last_page_index) {
        return FilePageCacheStatus::MappingNotFound;
    }
    return this->WritebackInternal(&identity, first_page_index, last_page_index, writer_context,
                                   write_operation, maximum_page_count, written_page_count);
}

FilePageCacheStatus FilePageCache::RequestBackgroundWriteback() noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_) {
        return FilePageCacheStatus::NotInitialized;
    }
    uint64_t outstanding_dirty_page_count = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
    if (!this->OutstandingDirtyPageCount(outstanding_dirty_page_count)) {
        return FilePageCacheStatus::Corrupt;
    }
    if (outstanding_dirty_page_count == OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE) {
        return FilePageCacheStatus::Succeeded;
    }
    if (this->background_writeback_paused_) {
        return FilePageCacheStatus::SourceWriteFailed;
    }
    this->forced_background_writeback_requested_ = true;
    this->background_writeback_requested_ = true;
    this->statistics_.background_writeback_requested = true;
    if (this->statistics_.explicit_background_writeback_request_count != UINT64_MAX) {
        ++this->statistics_.explicit_background_writeback_request_count;
    }
    return FilePageCacheStatus::Succeeded;
}

FilePageCacheStatus FilePageCache::WritebackInternal(const FileIdentity *const filter_identity,
                                                     const uint64_t first_page_index,
                                                     const uint64_t last_page_index,
                                                     void *const writer_context,
                                                     const FilePageWriteOperation write_operation,
                                                     const uint64_t maximum_page_count,
                                                     uint64_t &written_page_count) noexcept {
    written_page_count = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
    if (!this->initialized_ || this->page_access_operation_ == nullptr) {
        return FilePageCacheStatus::NotInitialized;
    }
    if (writer_context == nullptr || write_operation == nullptr) {
        return FilePageCacheStatus::InvalidWriter;
    }
    {
        SpinLockGuard guard{this->lock_};
        this->background_writeback_paused_ = false;
        this->statistics_.background_writeback_paused = false;
    }
    while (written_page_count < maximum_page_count) {
        FilePageIdentity identity{};
        uint64_t physical_address = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
        uint64_t write_length_bytes = OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
        {
            SpinLockGuard guard{this->lock_};
            AddressSpaceRecord *record = nullptr;
            FileCachePageSnapshot page{};
            const FilePageCacheStatus select_status =
                filter_identity == nullptr
                    ? this->SelectWritebackCandidate(record, page)
                    : this->SelectWritebackCandidateInRange(*filter_identity, first_page_index,
                                                            last_page_index, record, page);
            if (select_status == FilePageCacheStatus::MappingNotFound) {
                this->RefreshBackgroundWritebackRequest();
                return FilePageCacheStatus::Succeeded;
            }
            if (select_status != FilePageCacheStatus::Succeeded || record == nullptr) {
                return select_status;
            }
            if (record->size_known) {
                if (page.page_index > UINT64_MAX / OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) {
                    return FilePageCacheStatus::Corrupt;
                }
                const uint64_t page_offset_bytes =
                    page.page_index * OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
                if (page_offset_bytes >= record->size_bytes) {
                    return FilePageCacheStatus::Corrupt;
                }
                write_length_bytes = record->size_bytes - page_offset_bytes;
                if (write_length_bytes > OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) {
                    write_length_bytes = OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
                }
            }
            if (record->address_space.Transition(page.page_index, page.physical_address, page.state,
                                                 FileCachePageState::Writeback) !=
                FileCacheAddressSpaceStatus::Succeeded) {
                return FilePageCacheStatus::Corrupt;
            }
            if (page.state == FileCachePageState::Dirty) {
                if (this->statistics_.dirty_page_count == OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE) {
                    return FilePageCacheStatus::Corrupt;
                }
                --this->statistics_.dirty_page_count;
            } else {
                if (this->statistics_.error_page_count == OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE) {
                    return FilePageCacheStatus::Corrupt;
                }
                --this->statistics_.error_page_count;
            }
            ++this->statistics_.writeback_page_count;
            ++this->statistics_.writeback_attempt_count;
            if (this->statistics_.peak_outstanding_writeback_page_count <
                this->statistics_.writeback_page_count) {
                this->statistics_.peak_outstanding_writeback_page_count =
                    this->statistics_.writeback_page_count;
            }
            identity = FilePageIdentity{
                .file = record->identity,
                .page_index = page.page_index,
            };
            physical_address = page.physical_address;
        }

        const uint8_t *const page_bytes =
            this->page_access_operation_(this->page_access_context_, physical_address);
        const bool write_succeeded =
            page_bytes != nullptr &&
            write_operation(writer_context, identity, page_bytes, write_length_bytes);
        {
            SpinLockGuard guard{this->lock_};
            AddressSpaceRecord *const record = this->FindAddressSpace(identity.file);
            FileCachePageSnapshot page{};
            if (record == nullptr ||
                record->address_space.Lookup(identity.page_index, page) !=
                    FileCacheAddressSpaceStatus::Succeeded ||
                page.physical_address != physical_address ||
                page.state != FileCachePageState::Writeback ||
                this->statistics_.writeback_page_count == OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE ||
                record->address_space.Transition(
                    identity.page_index, physical_address, FileCachePageState::Writeback,
                    write_succeeded ? FileCachePageState::Clean : FileCachePageState::Error) !=
                    FileCacheAddressSpaceStatus::Succeeded) {
                return FilePageCacheStatus::Corrupt;
            }
            --this->statistics_.writeback_page_count;
            if (write_succeeded) {
                ++this->statistics_.successful_writeback_count;
                ++written_page_count;
                this->RefreshBackgroundWritebackRequest();
            } else {
                ++this->statistics_.error_page_count;
                ++this->statistics_.failed_writeback_count;
                this->background_writeback_paused_ = true;
                this->background_writeback_requested_ = false;
                this->forced_background_writeback_requested_ = false;
                this->statistics_.background_writeback_paused = true;
                this->statistics_.background_writeback_requested = false;
                return FilePageCacheStatus::SourceWriteFailed;
            }
        }
    }
    return FilePageCacheStatus::Succeeded;
}

FilePageCacheStatus FilePageCache::Invalidate(const FileIdentity &identity) noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_) {
        return FilePageCacheStatus::NotInitialized;
    }
    AddressSpaceRecord *record = this->FindAddressSpace(identity);
    if (record == nullptr) {
        return FilePageCacheStatus::Succeeded;
    }
    const FileCachePageState states[] = {
        FileCachePageState::Loading,   FileCachePageState::Clean, FileCachePageState::Dirty,
        FileCachePageState::Writeback, FileCachePageState::Error,
    };
    for (const FileCachePageState state : states) {
        uint64_t cursor = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
        while (true) {
            FileCachePageSnapshot page{};
            const FileCacheAddressSpaceStatus status =
                record->address_space.FindNext(cursor, UINT64_MAX, state, page);
            if (status == FileCacheAddressSpaceStatus::NotFound) {
                break;
            }
            if (status != FileCacheAddressSpaceStatus::Succeeded) {
                return this->MapAddressSpaceStatus(status);
            }
            if (page.mapping_reference_count != OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE ||
                page.state == FileCachePageState::Loading) {
                return FilePageCacheStatus::EntryBusy;
            }
            if (page.page_index == UINT64_MAX) {
                break;
            }
            cursor = page.page_index + OS_KERNEL_FILE_PAGE_CACHE_SINGLE_UNIT;
        }
    }
    const FileCacheAddressSpaceStatistics address_statistics = record->address_space.Statistics();
    if (address_statistics.dirty_page_count != OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE ||
        address_statistics.writeback_page_count != OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE ||
        address_statistics.error_page_count != OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE) {
        return FilePageCacheStatus::DirtyPagesRemain;
    }

    while (record->address_space.Statistics().resident_page_count !=
           OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE) {
        FileCachePageSnapshot page{};
        if (record->address_space.FindNext(OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE, UINT64_MAX,
                                           FileCachePageState::Clean,
                                           page) != FileCacheAddressSpaceStatus::Succeeded) {
            return FilePageCacheStatus::Corrupt;
        }
        if (record->address_space.Remove(page.page_index, page.physical_address) !=
                FileCacheAddressSpaceStatus::Succeeded ||
            this->frame_allocator_->Release(
                PhysicalFrame{.physical_address = page.physical_address}) !=
                PhysicalFrameAllocatorStatus::Succeeded ||
            this->statistics_.resident_page_count == OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE) {
            return FilePageCacheStatus::FrameReleaseFailed;
        }
        if (!this->RecordPrefetchedDiscard(page)) {
            return FilePageCacheStatus::Corrupt;
        }
        --this->statistics_.resident_page_count;
        ++this->statistics_.invalidation_count;
    }
    return this->DestroyAddressSpace(*record);
}

FilePageCacheStatus FilePageCache::ObserveFileSize(const FileIdentity &identity,
                                                   const uint64_t size_bytes) noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_) {
        return FilePageCacheStatus::NotInitialized;
    }
    if (!FileCacheIdentityIsValid(identity)) {
        return FilePageCacheStatus::InvalidIdentity;
    }
    AddressSpaceRecord *const record = this->FindAddressSpace(identity);
    if (record == nullptr) {
        return FilePageCacheStatus::MappingNotFound;
    }
    if (!record->size_known) {
        record->size_bytes = size_bytes;
        record->size_known = true;
    }
    return FilePageCacheStatus::Succeeded;
}

FilePageCacheStatus FilePageCache::UpdateFileSize(const FileIdentity &identity,
                                                  const uint64_t size_bytes) noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_) {
        return FilePageCacheStatus::NotInitialized;
    }
    if (!FileCacheIdentityIsValid(identity)) {
        return FilePageCacheStatus::InvalidIdentity;
    }
    AddressSpaceRecord *const record = this->FindAddressSpace(identity);
    if (record == nullptr) {
        return FilePageCacheStatus::MappingNotFound;
    }
    record->size_bytes = size_bytes;
    record->size_known = true;
    return FilePageCacheStatus::Succeeded;
}

FilePageCacheStatus FilePageCache::ResolveFileSize(const FileIdentity &identity,
                                                   const uint64_t backend_size_bytes,
                                                   uint64_t &size_bytes) const noexcept {
    SpinLockGuard guard{this->lock_};
    size_bytes = backend_size_bytes;
    if (!this->initialized_) {
        return FilePageCacheStatus::NotInitialized;
    }
    if (!FileCacheIdentityIsValid(identity)) {
        return FilePageCacheStatus::InvalidIdentity;
    }
    const AddressSpaceRecord *const record = this->FindAddressSpace(identity);
    if (record != nullptr && record->size_known) {
        size_bytes = record->size_bytes;
    }
    return FilePageCacheStatus::Succeeded;
}

FilePageCacheStatus FilePageCache::Truncate(const FileIdentity &identity,
                                            const uint64_t size_bytes) noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_ || this->frame_allocator_ == nullptr ||
        this->page_access_operation_ == nullptr) {
        return FilePageCacheStatus::NotInitialized;
    }
    if (!FileCacheIdentityIsValid(identity)) {
        return FilePageCacheStatus::InvalidIdentity;
    }
    AddressSpaceRecord *const record = this->FindAddressSpace(identity);
    if (record == nullptr) {
        return FilePageCacheStatus::Succeeded;
    }

    const uint64_t tail_offset_bytes =
        size_bytes & (OS_KERNEL_MEMORY_PAGE_SIZE_BYTES - OS_KERNEL_FILE_PAGE_CACHE_SINGLE_UNIT);
    const uint64_t first_discarded_page_index =
        size_bytes / OS_KERNEL_MEMORY_PAGE_SIZE_BYTES +
        (tail_offset_bytes == OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE
             ? OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE
             : OS_KERNEL_FILE_PAGE_CACHE_SINGLE_UNIT);
    const FileCachePageState states[] = {
        FileCachePageState::Loading,   FileCachePageState::Clean, FileCachePageState::Dirty,
        FileCachePageState::Writeback, FileCachePageState::Error,
    };
    for (const FileCachePageState state : states) {
        uint64_t cursor = first_discarded_page_index;
        while (true) {
            FileCachePageSnapshot page{};
            const FileCacheAddressSpaceStatus status =
                record->address_space.FindNext(cursor, UINT64_MAX, state, page);
            if (status == FileCacheAddressSpaceStatus::NotFound) {
                break;
            }
            if (status != FileCacheAddressSpaceStatus::Succeeded) {
                return this->MapAddressSpaceStatus(status);
            }
            if (page.mapping_reference_count != OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE ||
                page.state == FileCachePageState::Loading ||
                page.state == FileCachePageState::Writeback) {
                return FilePageCacheStatus::EntryBusy;
            }
            if (page.page_index == UINT64_MAX) {
                break;
            }
            cursor = page.page_index + OS_KERNEL_FILE_PAGE_CACHE_SINGLE_UNIT;
        }
    }

    const uint64_t previous_size_bytes = record->size_known ? record->size_bytes : size_bytes;
    const uint64_t zero_begin_bytes =
        previous_size_bytes < size_bytes ? previous_size_bytes : size_bytes;
    uint64_t zero_end_bytes = size_bytes;
    if (tail_offset_bytes != OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE) {
        const uint64_t tail_length_bytes = OS_KERNEL_MEMORY_PAGE_SIZE_BYTES - tail_offset_bytes;
        zero_end_bytes = size_bytes > UINT64_MAX - tail_length_bytes
                             ? UINT64_MAX
                             : size_bytes + tail_length_bytes;
    }
    if (zero_begin_bytes < zero_end_bytes) {
        const uint64_t first_zero_page_index = zero_begin_bytes / OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
        const uint64_t last_zero_page_index =
            (zero_end_bytes - OS_KERNEL_FILE_PAGE_CACHE_SINGLE_UNIT) /
            OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
        for (const FileCachePageState state : states) {
            uint64_t cursor = first_zero_page_index;
            while (true) {
                FileCachePageSnapshot page{};
                const FileCacheAddressSpaceStatus status =
                    record->address_space.FindNext(cursor, last_zero_page_index, state, page);
                if (status == FileCacheAddressSpaceStatus::NotFound) {
                    break;
                }
                if (status != FileCacheAddressSpaceStatus::Succeeded ||
                    page.state == FileCachePageState::Writeback) {
                    return status == FileCacheAddressSpaceStatus::Succeeded
                               ? FilePageCacheStatus::EntryBusy
                               : this->MapAddressSpaceStatus(status);
                }
                uint8_t *const page_bytes =
                    this->page_access_operation_(this->page_access_context_, page.physical_address);
                if (page_bytes == nullptr) {
                    return FilePageCacheStatus::FrameAccessFailed;
                }
                const uint64_t page_begin_bytes =
                    page.page_index * OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
                const uint64_t page_zero_begin_bytes = zero_begin_bytes > page_begin_bytes
                                                           ? zero_begin_bytes - page_begin_bytes
                                                           : OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
                const uint64_t page_zero_end_bytes =
                    zero_end_bytes - page_begin_bytes < OS_KERNEL_MEMORY_PAGE_SIZE_BYTES
                        ? zero_end_bytes - page_begin_bytes
                        : OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
                for (uint64_t byte_index = page_zero_begin_bytes; byte_index < page_zero_end_bytes;
                     ++byte_index) {
                    page_bytes[byte_index] =
                        static_cast<uint8_t>(OS_KERNEL_FILE_PAGE_CACHE_ZERO_BYTE);
                }
                ++this->statistics_.truncated_tail_zero_count;
                if (page.page_index == last_zero_page_index || page.page_index == UINT64_MAX) {
                    break;
                }
                cursor = page.page_index + OS_KERNEL_FILE_PAGE_CACHE_SINGLE_UNIT;
            }
        }
    }

    for (const FileCachePageState state : states) {
        while (true) {
            FileCachePageSnapshot page{};
            const FileCacheAddressSpaceStatus status =
                record->address_space.FindNext(first_discarded_page_index, UINT64_MAX, state, page);
            if (status == FileCacheAddressSpaceStatus::NotFound) {
                break;
            }
            if (status != FileCacheAddressSpaceStatus::Succeeded ||
                record->address_space.Discard(page.page_index, page.physical_address) !=
                    FileCacheAddressSpaceStatus::Succeeded ||
                this->frame_allocator_->Release(
                    PhysicalFrame{.physical_address = page.physical_address}) !=
                    PhysicalFrameAllocatorStatus::Succeeded ||
                this->statistics_.resident_page_count == OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE) {
                return FilePageCacheStatus::FrameReleaseFailed;
            }
            if (!this->RecordPrefetchedDiscard(page)) {
                return FilePageCacheStatus::Corrupt;
            }
            --this->statistics_.resident_page_count;
            if (state == FileCachePageState::Dirty) {
                if (this->statistics_.dirty_page_count == OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE) {
                    return FilePageCacheStatus::Corrupt;
                }
                --this->statistics_.dirty_page_count;
            } else if (state == FileCachePageState::Error) {
                if (this->statistics_.error_page_count == OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE) {
                    return FilePageCacheStatus::Corrupt;
                }
                --this->statistics_.error_page_count;
            }
            ++this->statistics_.invalidation_count;
            ++this->statistics_.truncated_page_count;
        }
    }
    record->size_bytes = size_bytes;
    record->size_known = true;
    ++this->statistics_.truncate_count;
    this->RefreshBackgroundWritebackRequest();
    return record->address_space.Statistics().resident_page_count ==
                   OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE
               ? this->DestroyAddressSpace(*record)
               : FilePageCacheStatus::Succeeded;
}

FilePageCacheStatus FilePageCache::ReadAddressSpaceStatistics(
    const FileIdentity &identity, FileCacheAddressSpaceStatistics &statistics) const noexcept {
    SpinLockGuard guard{this->lock_};
    statistics = FileCacheAddressSpaceStatistics{};
    if (!this->initialized_) {
        return FilePageCacheStatus::NotInitialized;
    }
    if (!FileCacheIdentityIsValid(identity)) {
        return FilePageCacheStatus::InvalidIdentity;
    }
    const AddressSpaceRecord *const record = this->FindAddressSpace(identity);
    if (record == nullptr) {
        return FilePageCacheStatus::MappingNotFound;
    }
    statistics = record->address_space.Statistics();
    return FilePageCacheStatus::Succeeded;
}

bool FilePageCache::BackgroundWritebackRequested() const noexcept {
    SpinLockGuard guard{this->lock_};
    return this->initialized_ && this->background_writeback_requested_;
}

bool FilePageCache::DirtyBackpressureRequired() const noexcept {
    SpinLockGuard guard{this->lock_};
    uint64_t outstanding_dirty_page_count = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
    return this->initialized_ && this->OutstandingDirtyPageCount(outstanding_dirty_page_count) &&
           outstanding_dirty_page_count >= this->dirty_page_limit_;
}

bool FilePageCache::BackgroundWritebackPaused() const noexcept {
    SpinLockGuard guard{this->lock_};
    return this->initialized_ && this->background_writeback_paused_;
}

FilePageCacheStatus FilePageCache::Trim(const uint64_t target_resident_page_count) noexcept {
    uint64_t reclaimed_page_count = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
    return this->Trim(target_resident_page_count, reclaimed_page_count);
}

FilePageCacheStatus FilePageCache::Trim(const uint64_t target_resident_page_count,
                                        uint64_t &reclaimed_page_count) noexcept {
    SpinLockGuard guard{this->lock_};
    reclaimed_page_count = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
    if (!this->initialized_) {
        return FilePageCacheStatus::NotInitialized;
    }
    if (target_resident_page_count > this->capacity_) {
        return FilePageCacheStatus::InvalidCapacity;
    }
    while (this->statistics_.resident_page_count > target_resident_page_count) {
        AddressSpaceRecord *record = nullptr;
        FileCachePageSnapshot page{};
        const FilePageCacheStatus select_status =
            this->SelectEvictionCandidate(nullptr, nullptr, record, page);
        if (select_status != FilePageCacheStatus::Succeeded || record == nullptr) {
            return select_status;
        }
        uint64_t released_physical_address = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
        const FilePageCacheStatus eviction_status =
            this->Evict(*record, page, true, released_physical_address);
        if (eviction_status != FilePageCacheStatus::Succeeded) {
            return eviction_status;
        }
        ++reclaimed_page_count;
    }
    return FilePageCacheStatus::Succeeded;
}

FilePageCacheStatus
FilePageCache::ReclaimCleanPages(const uint64_t maximum_page_count, void *const context,
                                 const FilePageCacheReclaimSelectionOperation selection_operation,
                                 const FilePageCacheReclaimCompletionOperation completion_operation,
                                 uint64_t &reclaimed_page_count) noexcept {
    SpinLockGuard guard{this->lock_};
    reclaimed_page_count = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
    if (!this->initialized_) {
        return FilePageCacheStatus::NotInitialized;
    }
    if (selection_operation == nullptr || completion_operation == nullptr) {
        return FilePageCacheStatus::InvalidVisitor;
    }
    while (reclaimed_page_count < maximum_page_count) {
        AddressSpaceRecord *record = nullptr;
        FileCachePageSnapshot page{};
        const FilePageCacheStatus select_status =
            this->SelectEvictionCandidate(context, selection_operation, record, page);
        if (select_status == FilePageCacheStatus::EntryBusy ||
            select_status == FilePageCacheStatus::DirtyPagesRemain) {
            return FilePageCacheStatus::Succeeded;
        }
        if (select_status != FilePageCacheStatus::Succeeded || record == nullptr) {
            return select_status;
        }
        const FilePageCacheEntry entry = FilePageCache::Snapshot(record->identity, page);
        // completion 在 frame 仍归 cache 时撤销老化身份；失败时不改变 cache。
        if (!completion_operation(context, entry)) {
            return FilePageCacheStatus::InvalidVisitor;
        }
        uint64_t released_physical_address = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
        const FilePageCacheStatus eviction_status =
            this->Evict(*record, page, true, released_physical_address);
        if (eviction_status != FilePageCacheStatus::Succeeded ||
            released_physical_address != entry.physical_address) {
            return eviction_status == FilePageCacheStatus::Succeeded ? FilePageCacheStatus::Corrupt
                                                                     : eviction_status;
        }
        ++reclaimed_page_count;
    }
    return FilePageCacheStatus::Succeeded;
}

FilePageCacheStatus FilePageCache::DiscardPrefetched(const FileReadaheadStreamToken stream,
                                                     const uint64_t maximum_policy_generation,
                                                     uint64_t &discarded_page_count) noexcept {
    SpinLockGuard guard{this->lock_};
    discarded_page_count = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
    if (!this->initialized_) {
        return FilePageCacheStatus::NotInitialized;
    }
    if (!FileReadaheadStreamTokenIsValid(stream) ||
        maximum_policy_generation == OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE) {
        return FilePageCacheStatus::InvalidIdentity;
    }
    while (true) {
        AddressSpaceRecord *selected_record = nullptr;
        FileCachePageSnapshot selected_page{};
        for (AddressSpaceRecord *record = this->address_spaces_;
             record != nullptr && selected_record == nullptr; record = record->next) {
            uint64_t cursor = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
            while (true) {
                FileCachePageSnapshot page{};
                const FileCacheAddressSpaceStatus status = record->address_space.FindNext(
                    cursor, UINT64_MAX, FileCachePageState::Clean, page);
                if (status == FileCacheAddressSpaceStatus::NotFound) {
                    break;
                }
                if (status != FileCacheAddressSpaceStatus::Succeeded) {
                    return this->MapAddressSpaceStatus(status);
                }
                if (page.prefetched &&
                    FileReadaheadStreamTokensEqual(page.readahead_tag.stream, stream) &&
                    page.readahead_tag.policy_generation <= maximum_policy_generation &&
                    page.mapping_reference_count == OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE) {
                    selected_record = record;
                    selected_page = page;
                    break;
                }
                if (page.page_index == UINT64_MAX) {
                    break;
                }
                cursor = page.page_index + OS_KERNEL_FILE_PAGE_CACHE_SINGLE_UNIT;
            }
        }
        if (selected_record == nullptr) {
            return FilePageCacheStatus::Succeeded;
        }
        uint64_t released_physical_address = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
        const FilePageCacheStatus eviction_status =
            this->Evict(*selected_record, selected_page, true, released_physical_address);
        if (eviction_status != FilePageCacheStatus::Succeeded ||
            released_physical_address != selected_page.physical_address ||
            discarded_page_count == UINT64_MAX) {
            return eviction_status == FilePageCacheStatus::Succeeded ? FilePageCacheStatus::Corrupt
                                                                     : eviction_status;
        }
        ++discarded_page_count;
    }
}

FilePageCacheStatus FilePageCache::ReadEntry(const FilePageIdentity &identity,
                                             FilePageCacheEntry &entry) const noexcept {
    SpinLockGuard guard{this->lock_};
    entry = FilePageCacheEntry{};
    if (!this->initialized_) {
        return FilePageCacheStatus::NotInitialized;
    }
    const AddressSpaceRecord *const record = this->FindAddressSpace(identity.file);
    if (record == nullptr) {
        return FilePageCacheStatus::MappingNotFound;
    }
    FileCachePageSnapshot page{};
    const FileCacheAddressSpaceStatus status =
        record->address_space.Lookup(identity.page_index, page);
    if (status != FileCacheAddressSpaceStatus::Succeeded) {
        return this->MapAddressSpaceStatus(status);
    }
    entry = FilePageCache::Snapshot(identity.file, page);
    return FilePageCacheStatus::Succeeded;
}

FilePageCacheStatus
FilePageCache::VisitEntries(void *const context,
                            const FilePageCacheVisitOperation operation) const noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_) {
        return FilePageCacheStatus::NotInitialized;
    }
    if (operation == nullptr) {
        return FilePageCacheStatus::InvalidVisitor;
    }
    constexpr FileCachePageState OS_KERNEL_FILE_PAGE_CACHE_VISIT_STATES[] = {
        FileCachePageState::Loading,   FileCachePageState::Clean, FileCachePageState::Dirty,
        FileCachePageState::Writeback, FileCachePageState::Error,
    };
    for (const AddressSpaceRecord *record = this->address_spaces_; record != nullptr;
         record = record->next) {
        for (const FileCachePageState state : OS_KERNEL_FILE_PAGE_CACHE_VISIT_STATES) {
            uint64_t first_page_index = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
            while (true) {
                FileCachePageSnapshot page{};
                const FileCacheAddressSpaceStatus find_status =
                    record->address_space.FindNext(first_page_index, UINT64_MAX, state, page);
                if (find_status == FileCacheAddressSpaceStatus::NotFound) {
                    break;
                }
                if (find_status != FileCacheAddressSpaceStatus::Succeeded) {
                    return this->MapAddressSpaceStatus(find_status);
                }
                if (!operation(context, FilePageCache::Snapshot(record->identity, page))) {
                    return FilePageCacheStatus::InvalidVisitor;
                }
                if (page.page_index == UINT64_MAX) {
                    break;
                }
                first_page_index = page.page_index + OS_KERNEL_FILE_PAGE_CACHE_SINGLE_UNIT;
            }
        }
    }
    return FilePageCacheStatus::Succeeded;
}

FilePageCacheStatus FilePageCache::Validate() const noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_ || this->metadata_heap_ == nullptr ||
        this->frame_allocator_ == nullptr || this->page_access_operation_ == nullptr ||
        this->metadata_heap_->Validate() != KernelHeapStatus::Succeeded ||
        this->capacity_ == OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE ||
        this->background_dirty_page_threshold_ == OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE ||
        this->background_dirty_page_threshold_ > this->dirty_page_limit_ ||
        this->background_dirty_page_target_ >= this->background_dirty_page_threshold_ ||
        this->dirty_page_limit_ == OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE ||
        this->dirty_page_limit_ > this->capacity_) {
        return FilePageCacheStatus::NotInitialized;
    }
    uint64_t address_space_count = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
    uint64_t resident_page_count = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
    uint64_t referenced_page_count = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
    uint64_t active_mapping_reference_count = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
    uint64_t loading_page_count = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
    uint64_t dirty_page_count = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
    uint64_t writeback_page_count = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
    uint64_t error_page_count = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
    uint64_t prefetched_page_count = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
    for (const AddressSpaceRecord *record = this->address_spaces_; record != nullptr;
         record = record->next) {
        if (!FileCacheIdentityIsValid(record->identity) ||
            record->address_space.Validate() != FileCacheAddressSpaceStatus::Succeeded) {
            return FilePageCacheStatus::Corrupt;
        }
        for (const AddressSpaceRecord *comparison = record->next; comparison != nullptr;
             comparison = comparison->next) {
            if (FileCacheIdentitiesEqual(record->identity, comparison->identity)) {
                return FilePageCacheStatus::Corrupt;
            }
        }
        const FileCacheAddressSpaceStatistics statistics = record->address_space.Statistics();
        if (!FileCacheIdentitiesEqual(record->identity, statistics.identity) ||
            statistics.resident_page_count == OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE ||
            resident_page_count > UINT64_MAX - statistics.resident_page_count ||
            referenced_page_count > UINT64_MAX - statistics.referenced_page_count ||
            active_mapping_reference_count >
                UINT64_MAX - statistics.active_mapping_reference_count ||
            loading_page_count > UINT64_MAX - statistics.loading_page_count ||
            dirty_page_count > UINT64_MAX - statistics.dirty_page_count ||
            writeback_page_count > UINT64_MAX - statistics.writeback_page_count ||
            error_page_count > UINT64_MAX - statistics.error_page_count ||
            prefetched_page_count > UINT64_MAX - statistics.prefetched_page_count) {
            return FilePageCacheStatus::Corrupt;
        }
        resident_page_count += statistics.resident_page_count;
        referenced_page_count += statistics.referenced_page_count;
        active_mapping_reference_count += statistics.active_mapping_reference_count;
        loading_page_count += statistics.loading_page_count;
        dirty_page_count += statistics.dirty_page_count;
        writeback_page_count += statistics.writeback_page_count;
        error_page_count += statistics.error_page_count;
        prefetched_page_count += statistics.prefetched_page_count;
        ++address_space_count;
    }
    uint64_t outstanding_dirty_page_count = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
    if (!this->OutstandingDirtyPageCount(outstanding_dirty_page_count)) {
        return FilePageCacheStatus::Corrupt;
    }
    if (this->statistics_.prefetched_page_count >
            UINT64_MAX - this->statistics_.prefetched_hit_count ||
        this->statistics_.prefetched_page_count + this->statistics_.prefetched_hit_count >
            UINT64_MAX - this->statistics_.wasted_prefetched_page_count) {
        return FilePageCacheStatus::Corrupt;
    }
    const uint64_t resolved_prefetched_page_count = this->statistics_.prefetched_page_count +
                                                    this->statistics_.prefetched_hit_count +
                                                    this->statistics_.wasted_prefetched_page_count;
    return address_space_count == this->statistics_.address_space_count &&
                   resident_page_count == this->statistics_.resident_page_count &&
                   loading_page_count == this->statistics_.loading_page_count &&
                   referenced_page_count == this->statistics_.referenced_page_count &&
                   active_mapping_reference_count ==
                       this->statistics_.active_mapping_reference_count &&
                   dirty_page_count == this->statistics_.dirty_page_count &&
                   writeback_page_count == this->statistics_.writeback_page_count &&
                   error_page_count == this->statistics_.error_page_count &&
                   prefetched_page_count == this->statistics_.prefetched_page_count &&
                   resolved_prefetched_page_count ==
                       this->statistics_.successful_prefetch_load_count &&
                   this->statistics_.readahead_feedback_record_count ==
                       this->statistics_.prefetched_hit_count +
                           this->statistics_.wasted_prefetched_page_count &&
                   resident_page_count <= this->capacity_ &&
                   outstanding_dirty_page_count <= this->dirty_page_limit_ &&
                   this->statistics_.background_dirty_page_threshold ==
                       this->background_dirty_page_threshold_ &&
                   this->statistics_.background_dirty_page_target ==
                       this->background_dirty_page_target_ &&
                   this->statistics_.background_writeback_requested ==
                       this->background_writeback_requested_ &&
                   this->statistics_.background_writeback_paused ==
                       this->background_writeback_paused_ &&
                   (!this->background_writeback_paused_ ||
                    !this->background_writeback_requested_) &&
                   (!this->forced_background_writeback_requested_ ||
                    this->background_writeback_requested_) &&
                   this->statistics_.peak_resident_page_count >= resident_page_count &&
                   this->statistics_.peak_address_space_count >= address_space_count &&
                   this->statistics_.successful_acquire_count ==
                       this->statistics_.release_count + active_mapping_reference_count
               ? FilePageCacheStatus::Succeeded
               : FilePageCacheStatus::Corrupt;
}

FilePageCacheStatistics FilePageCache::Statistics() const noexcept {
    SpinLockGuard guard{this->lock_};
    return this->initialized_ ? this->statistics_ : FilePageCacheStatistics{};
}

FilePageCacheStatus FilePageCache::Destroy() noexcept {
    if (!this->initialized_) {
        return FilePageCacheStatus::NotInitialized;
    }
    const FilePageCacheStatus trim_status = this->Trim(OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE);
    if (trim_status != FilePageCacheStatus::Succeeded) {
        return trim_status;
    }
    SpinLockGuard guard{this->lock_};
    while (this->address_spaces_ != nullptr) {
        const FilePageCacheStatus destroy_status =
            this->DestroyAddressSpace(*this->address_spaces_);
        if (destroy_status != FilePageCacheStatus::Succeeded) {
            return destroy_status;
        }
    }
    this->metadata_heap_ = nullptr;
    this->capacity_ = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
    this->background_dirty_page_threshold_ = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
    this->background_dirty_page_target_ = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
    this->dirty_page_limit_ = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
    this->frame_allocator_ = nullptr;
    this->page_access_context_ = nullptr;
    this->page_access_operation_ = nullptr;
    this->load_wait_operations_ = FilePageLoadWaitOperations{};
    this->readahead_feedback_operations_ = FilePageReadaheadFeedbackOperations{};
    this->access_generation_ = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
    this->statistics_ = FilePageCacheStatistics{};
    this->background_writeback_requested_ = false;
    this->background_writeback_paused_ = false;
    this->forced_background_writeback_requested_ = false;
    this->load_wait_operations_configured_ = false;
    this->readahead_feedback_operations_configured_ = false;
    this->initialized_ = false;
    return FilePageCacheStatus::Succeeded;
}

uint64_t FilePageCache::NextAccessGeneration() noexcept {
    if (this->access_generation_ == UINT64_MAX) {
        return OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
    }
    ++this->access_generation_;
    return this->access_generation_;
}

FilePageCache::AddressSpaceRecord *
FilePageCache::FindAddressSpace(const FileIdentity &identity) noexcept {
    for (AddressSpaceRecord *record = this->address_spaces_; record != nullptr;
         record = record->next) {
        if (FileCacheIdentitiesEqual(record->identity, identity)) {
            return record;
        }
    }
    return nullptr;
}

const FilePageCache::AddressSpaceRecord *
FilePageCache::FindAddressSpace(const FileIdentity &identity) const noexcept {
    for (const AddressSpaceRecord *record = this->address_spaces_; record != nullptr;
         record = record->next) {
        if (FileCacheIdentitiesEqual(record->identity, identity)) {
            return record;
        }
    }
    return nullptr;
}

FilePageCacheStatus FilePageCache::EnsureAddressSpace(const FileIdentity &identity,
                                                      AddressSpaceRecord *&record) noexcept {
    record = this->FindAddressSpace(identity);
    if (record != nullptr) {
        return FilePageCacheStatus::Succeeded;
    }
    void *allocation = nullptr;
    if (this->metadata_heap_->TryAllocate(sizeof(AddressSpaceRecord), alignof(AddressSpaceRecord),
                                          allocation) != KernelHeapStatus::Succeeded) {
        ++this->statistics_.metadata_allocation_failure_count;
        return FilePageCacheStatus::MetadataAllocationFailed;
    }
    record = new (allocation) AddressSpaceRecord{};
    record->identity = identity;
    if (record->address_space.Initialize(identity, *this->metadata_heap_) !=
        FileCacheAddressSpaceStatus::Succeeded) {
        static_cast<void>(this->metadata_heap_->TryRelease(record));
        record = nullptr;
        ++this->statistics_.metadata_allocation_failure_count;
        return FilePageCacheStatus::MetadataAllocationFailed;
    }
    record->next = this->address_spaces_;
    this->address_spaces_ = record;
    ++this->statistics_.address_space_count;
    if (this->statistics_.peak_address_space_count < this->statistics_.address_space_count) {
        this->statistics_.peak_address_space_count = this->statistics_.address_space_count;
    }
    return FilePageCacheStatus::Succeeded;
}

FilePageCacheStatus FilePageCache::DestroyAddressSpace(AddressSpaceRecord &record) noexcept {
    AddressSpaceRecord *previous = nullptr;
    AddressSpaceRecord *current = this->address_spaces_;
    while (current != nullptr && current != &record) {
        previous = current;
        current = current->next;
    }
    if (current == nullptr ||
        this->statistics_.address_space_count == OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE ||
        record.address_space.Statistics().resident_page_count !=
            OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE ||
        record.address_space.Destroy() != FileCacheAddressSpaceStatus::Succeeded) {
        return FilePageCacheStatus::Corrupt;
    }
    AddressSpaceRecord *const next = record.next;
    record.~AddressSpaceRecord();
    if (this->metadata_heap_->TryRelease(&record) != KernelHeapStatus::Succeeded) {
        return FilePageCacheStatus::MetadataReleaseFailed;
    }
    if (previous == nullptr) {
        this->address_spaces_ = next;
    } else {
        previous->next = next;
    }
    --this->statistics_.address_space_count;
    return FilePageCacheStatus::Succeeded;
}

FilePageCacheStatus FilePageCache::DestroyAddressSpaceIfEmpty(AddressSpaceRecord &record) noexcept {
    return record.address_space.Statistics().resident_page_count ==
                   OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE
               ? this->DestroyAddressSpace(record)
               : FilePageCacheStatus::Succeeded;
}

FilePageCacheStatus FilePageCache::SelectEvictionCandidate(
    void *const context, const FilePageCacheReclaimSelectionOperation selection_operation,
    AddressSpaceRecord *&record, FileCachePageSnapshot &page) noexcept {
    record = nullptr;
    page = FileCachePageSnapshot{};
    for (AddressSpaceRecord *candidate_record = this->address_spaces_; candidate_record != nullptr;
         candidate_record = candidate_record->next) {
        uint64_t cursor = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
        while (true) {
            FileCachePageSnapshot candidate_page{};
            const FileCacheAddressSpaceStatus status = candidate_record->address_space.FindNext(
                cursor, UINT64_MAX, FileCachePageState::Clean, candidate_page);
            if (status == FileCacheAddressSpaceStatus::NotFound) {
                break;
            }
            if (status != FileCacheAddressSpaceStatus::Succeeded) {
                return this->MapAddressSpaceStatus(status);
            }
            bool selected = true;
            if (candidate_page.mapping_reference_count == OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE &&
                selection_operation != nullptr &&
                !selection_operation(
                    context, FilePageCache::Snapshot(candidate_record->identity, candidate_page),
                    selected)) {
                return FilePageCacheStatus::InvalidVisitor;
            }
            if (candidate_page.mapping_reference_count == OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE &&
                selected &&
                (record == nullptr || candidate_page.access_generation < page.access_generation)) {
                record = candidate_record;
                page = candidate_page;
            }
            if (candidate_page.page_index == UINT64_MAX) {
                break;
            }
            cursor = candidate_page.page_index + OS_KERNEL_FILE_PAGE_CACHE_SINGLE_UNIT;
        }
    }
    if (record != nullptr) {
        return FilePageCacheStatus::Succeeded;
    }
    return this->statistics_.dirty_page_count != OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE ||
                   this->statistics_.writeback_page_count !=
                       OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE ||
                   this->statistics_.error_page_count != OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE
               ? FilePageCacheStatus::DirtyPagesRemain
               : FilePageCacheStatus::EntryBusy;
}

FilePageCacheStatus FilePageCache::Evict(AddressSpaceRecord &record,
                                         const FileCachePageSnapshot &page,
                                         const bool release_frame,
                                         uint64_t &physical_address) noexcept {
    physical_address = page.physical_address;
    if (record.address_space.Remove(page.page_index, page.physical_address) !=
            FileCacheAddressSpaceStatus::Succeeded ||
        this->statistics_.resident_page_count == OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE) {
        return FilePageCacheStatus::Corrupt;
    }
    if (release_frame &&
        this->frame_allocator_->Release(PhysicalFrame{.physical_address = page.physical_address}) !=
            PhysicalFrameAllocatorStatus::Succeeded) {
        return FilePageCacheStatus::FrameReleaseFailed;
    }
    if (!this->RecordPrefetchedDiscard(page)) {
        return FilePageCacheStatus::Corrupt;
    }
    --this->statistics_.resident_page_count;
    ++this->statistics_.eviction_count;
    return this->DestroyAddressSpaceIfEmpty(record);
}

FilePageCacheStatus FilePageCache::SelectWritebackCandidate(AddressSpaceRecord *&record,
                                                            FileCachePageSnapshot &page) noexcept {
    record = nullptr;
    page = FileCachePageSnapshot{};
    for (AddressSpaceRecord *candidate = this->address_spaces_; candidate != nullptr;
         candidate = candidate->next) {
        FileCacheAddressSpaceStatus status = candidate->address_space.FindNext(
            OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE, UINT64_MAX, FileCachePageState::Dirty, page);
        if (status == FileCacheAddressSpaceStatus::Succeeded) {
            record = candidate;
            return FilePageCacheStatus::Succeeded;
        }
        if (status != FileCacheAddressSpaceStatus::NotFound) {
            return this->MapAddressSpaceStatus(status);
        }
    }
    for (AddressSpaceRecord *candidate = this->address_spaces_; candidate != nullptr;
         candidate = candidate->next) {
        const FileCacheAddressSpaceStatus status = candidate->address_space.FindNext(
            OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE, UINT64_MAX, FileCachePageState::Error, page);
        if (status == FileCacheAddressSpaceStatus::Succeeded) {
            record = candidate;
            return FilePageCacheStatus::Succeeded;
        }
        if (status != FileCacheAddressSpaceStatus::NotFound) {
            return this->MapAddressSpaceStatus(status);
        }
    }
    return FilePageCacheStatus::MappingNotFound;
}

FilePageCacheStatus FilePageCache::SelectWritebackCandidateInRange(
    const FileIdentity &identity, const uint64_t first_page_index, const uint64_t last_page_index,
    AddressSpaceRecord *&record, FileCachePageSnapshot &page) noexcept {
    record = this->FindAddressSpace(identity);
    page = FileCachePageSnapshot{};
    if (record == nullptr) {
        return FilePageCacheStatus::MappingNotFound;
    }
    FileCacheAddressSpaceStatus status = record->address_space.FindNext(
        first_page_index, last_page_index, FileCachePageState::Dirty, page);
    if (status == FileCacheAddressSpaceStatus::Succeeded) {
        return FilePageCacheStatus::Succeeded;
    }
    if (status != FileCacheAddressSpaceStatus::NotFound) {
        return this->MapAddressSpaceStatus(status);
    }
    status = record->address_space.FindNext(first_page_index, last_page_index,
                                            FileCachePageState::Error, page);
    return status == FileCacheAddressSpaceStatus::Succeeded  ? FilePageCacheStatus::Succeeded
           : status == FileCacheAddressSpaceStatus::NotFound ? FilePageCacheStatus::MappingNotFound
                                                             : this->MapAddressSpaceStatus(status);
}

bool FilePageCache::OutstandingDirtyPageCount(uint64_t &page_count) const noexcept {
    page_count = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
    if (this->statistics_.dirty_page_count > UINT64_MAX - this->statistics_.writeback_page_count ||
        this->statistics_.dirty_page_count + this->statistics_.writeback_page_count >
            UINT64_MAX - this->statistics_.error_page_count) {
        return false;
    }
    page_count = this->statistics_.dirty_page_count + this->statistics_.writeback_page_count +
                 this->statistics_.error_page_count;
    return true;
}

bool FilePageCache::LoadingOwnerAvailable() const noexcept {
    return this->load_wait_operations_configured_ &&
           this->load_wait_operations_.owner_available != nullptr &&
           this->load_wait_operations_.owner_available(this->load_wait_operations_.context);
}

bool FilePageCache::LoadingWaitAvailable() const noexcept {
    return this->load_wait_operations_configured_ &&
           this->load_wait_operations_.available != nullptr &&
           this->load_wait_operations_.available(this->load_wait_operations_.context);
}

bool FilePageCache::LoadingWaiterCount(const FilePageLoadToken token,
                                       uint64_t &waiter_count) noexcept {
    waiter_count = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
    return token.generation == OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE ||
           (this->load_wait_operations_configured_ &&
            this->load_wait_operations_.waiter_count != nullptr &&
            this->load_wait_operations_.waiter_count(this->load_wait_operations_.context, token,
                                                     waiter_count));
}

bool FilePageCache::CompleteLoadingWait(const FilePageLoadToken token,
                                        const FilePageCacheStatus result) noexcept {
    return token.generation == OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE ||
           (this->load_wait_operations_configured_ &&
            this->load_wait_operations_.complete != nullptr &&
            this->load_wait_operations_.complete(this->load_wait_operations_.context, token,
                                                 result));
}

bool FilePageCache::ConsumePrefetchedIfDemand(AddressSpaceRecord &record,
                                              const FileCachePageSnapshot &page,
                                              const FilePageAcquireIntent intent,
                                              bool &prefetched_hit) noexcept {
    prefetched_hit = false;
    if (intent == FilePageAcquireIntent::Prefetch) {
        return true;
    }
    if (page.prefetched &&
        (this->statistics_.prefetched_page_count == OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE ||
         this->statistics_.prefetched_hit_count == UINT64_MAX)) {
        return false;
    }
    if (page.prefetched &&
        !this->RecordReadaheadFeedback(
            page.readahead_tag, FileReadaheadFeedback{
                                    .useful_page_count = OS_KERNEL_FILE_PAGE_CACHE_SINGLE_UNIT,
                                    .wasted_page_count = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE,
                                })) {
        return false;
    }
    FileReadaheadPageTag consumed_tag{};
    if (record.address_space.ConsumePrefetched(page.page_index, page.physical_address, consumed_tag,
                                               prefetched_hit) !=
        FileCacheAddressSpaceStatus::Succeeded) {
        return false;
    }
    if (prefetched_hit) {
        if (!FileReadaheadStreamTokensEqual(consumed_tag.stream, page.readahead_tag.stream) ||
            consumed_tag.policy_generation != page.readahead_tag.policy_generation) {
            return false;
        }
        --this->statistics_.prefetched_page_count;
        ++this->statistics_.prefetched_hit_count;
    }
    return true;
}

bool FilePageCache::RecordPrefetchedDiscard(const FileCachePageSnapshot &page) noexcept {
    if (!page.prefetched) {
        return true;
    }
    if (this->statistics_.prefetched_page_count == OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE ||
        this->statistics_.wasted_prefetched_page_count == UINT64_MAX) {
        return false;
    }
    if (!this->RecordReadaheadFeedback(
            page.readahead_tag, FileReadaheadFeedback{
                                    .useful_page_count = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE,
                                    .wasted_page_count = OS_KERNEL_FILE_PAGE_CACHE_SINGLE_UNIT,
                                })) {
        return false;
    }
    --this->statistics_.prefetched_page_count;
    ++this->statistics_.wasted_prefetched_page_count;
    return true;
}

bool FilePageCache::RecordReadaheadFeedback(const FileReadaheadPageTag &tag,
                                            const FileReadaheadFeedback &feedback) noexcept {
    if (!FileReadaheadPageTagIsValid(tag) || !this->readahead_feedback_operations_configured_ ||
        this->readahead_feedback_operations_.record == nullptr ||
        this->statistics_.readahead_feedback_record_count == UINT64_MAX ||
        !this->readahead_feedback_operations_.record(this->readahead_feedback_operations_.context,
                                                     tag, feedback)) {
        return false;
    }
    ++this->statistics_.readahead_feedback_record_count;
    return true;
}

void FilePageCache::RefreshBackgroundWritebackRequest() noexcept {
    uint64_t outstanding_dirty_page_count = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
    if (!this->OutstandingDirtyPageCount(outstanding_dirty_page_count)) {
        this->background_writeback_paused_ = true;
        this->background_writeback_requested_ = false;
        this->forced_background_writeback_requested_ = false;
    } else if (this->background_writeback_paused_) {
        this->background_writeback_requested_ = false;
        this->forced_background_writeback_requested_ = false;
    } else {
        const bool request_writeback =
            this->forced_background_writeback_requested_
                ? outstanding_dirty_page_count != OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE
            : this->background_writeback_requested_
                ? outstanding_dirty_page_count > this->background_dirty_page_target_
                : outstanding_dirty_page_count >= this->background_dirty_page_threshold_;
        if (this->forced_background_writeback_requested_ && !request_writeback) {
            this->forced_background_writeback_requested_ = false;
        }
        if (request_writeback && !this->background_writeback_requested_) {
            if (this->statistics_.background_writeback_request_count != UINT64_MAX) {
                ++this->statistics_.background_writeback_request_count;
            }
        }
        this->background_writeback_requested_ = request_writeback;
    }
    this->statistics_.background_writeback_requested = this->background_writeback_requested_;
    this->statistics_.background_writeback_paused = this->background_writeback_paused_;
}

FilePageCacheStatus
FilePageCache::MapAddressSpaceStatus(const FileCacheAddressSpaceStatus status) const noexcept {
    switch (status) {
    case FileCacheAddressSpaceStatus::Succeeded:
        return FilePageCacheStatus::Succeeded;
    case FileCacheAddressSpaceStatus::NotInitialized:
        return FilePageCacheStatus::NotInitialized;
    case FileCacheAddressSpaceStatus::AlreadyInitialized:
    case FileCacheAddressSpaceStatus::AlreadyExists:
    case FileCacheAddressSpaceStatus::Corrupt:
        return FilePageCacheStatus::Corrupt;
    case FileCacheAddressSpaceStatus::InvalidIdentity:
        return FilePageCacheStatus::InvalidIdentity;
    case FileCacheAddressSpaceStatus::InvalidPage:
    case FileCacheAddressSpaceStatus::NotFound:
        return FilePageCacheStatus::MappingNotFound;
    case FileCacheAddressSpaceStatus::InvalidState:
        return FilePageCacheStatus::Corrupt;
    case FileCacheAddressSpaceStatus::AllocationFailed:
        return FilePageCacheStatus::MetadataAllocationFailed;
    case FileCacheAddressSpaceStatus::MappingReferenceOverflow:
        return FilePageCacheStatus::Corrupt;
    case FileCacheAddressSpaceStatus::MappingReferenceUnderflow:
        return FilePageCacheStatus::ReferenceUnderflow;
    case FileCacheAddressSpaceStatus::PageBusy:
        return FilePageCacheStatus::EntryBusy;
    case FileCacheAddressSpaceStatus::DirtyPagesRemain:
        return FilePageCacheStatus::DirtyPagesRemain;
    case FileCacheAddressSpaceStatus::MetadataReleaseFailed:
        return FilePageCacheStatus::MetadataReleaseFailed;
    case FileCacheAddressSpaceStatus::PagesRemain:
        return FilePageCacheStatus::EntryBusy;
    }
    return FilePageCacheStatus::Corrupt;
}

FilePageCacheEntryState FilePageCache::MapPageState(const FileCachePageState state) noexcept {
    switch (state) {
    case FileCachePageState::Loading:
        return FilePageCacheEntryState::Loading;
    case FileCachePageState::Clean:
        return FilePageCacheEntryState::Clean;
    case FileCachePageState::Dirty:
        return FilePageCacheEntryState::Dirty;
    case FileCachePageState::Writeback:
        return FilePageCacheEntryState::Writeback;
    case FileCachePageState::Error:
        return FilePageCacheEntryState::Error;
    }
    return FilePageCacheEntryState::Empty;
}

FilePageCacheEntry FilePageCache::Snapshot(const FileIdentity &identity,
                                           const FileCachePageSnapshot &page) noexcept {
    return FilePageCacheEntry{
        .identity =
            {
                .file = identity,
                .page_index = page.page_index,
            },
        .physical_address = page.physical_address,
        .mapping_reference_count = page.mapping_reference_count,
        .access_generation = page.access_generation,
        .state = FilePageCache::MapPageState(page.state),
        .readahead_tag = page.readahead_tag,
        .prefetched = page.prefetched,
    };
}

}

#include "os/kernel/memory/file_page_cache.hpp"

#include <stdint.h>

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_FILE_PAGE_CACHE_SINGLE_UNIT = 1ULL;
constexpr uint64_t OS_KERNEL_FILE_PAGE_CACHE_GENERATION_REBASE_DIVISOR = 2ULL;
constexpr uint8_t OS_KERNEL_FILE_PAGE_CACHE_ZERO_BYTE = 0U;

}

FilePageCacheStatus
FilePageCache::Initialize(FilePageCacheEntry *const entries,
                          const uint64_t capacity,
                          const uint64_t dirty_page_limit,
                          PhysicalFrameAllocator &frame_allocator,
                          void *const page_access_context,
                          const FilePageAccessOperation page_access_operation) noexcept {
    if (this->initialized_) {
        return FilePageCacheStatus::AlreadyInitialized;
    }
    if (entries == nullptr) {
        return FilePageCacheStatus::InvalidStorage;
    }
    if (capacity == OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE ||
        dirty_page_limit == OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE ||
        dirty_page_limit > capacity) {
        return FilePageCacheStatus::InvalidCapacity;
    }
    if (page_access_operation == nullptr ||
        frame_allocator.Statistics().managed_frame_count ==
            OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE) {
        return FilePageCacheStatus::InvalidDependency;
    }

    for (uint64_t entry_index = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
         entry_index < capacity; ++entry_index) {
        entries[entry_index] = FilePageCacheEntry{};
    }
    this->entries_ = entries;
    this->capacity_ = capacity;
    this->dirty_page_limit_ = dirty_page_limit;
    this->frame_allocator_ = &frame_allocator;
    this->page_access_context_ = page_access_context;
    this->page_access_operation_ = page_access_operation;
    this->access_generation_ = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
    this->statistics_ = FilePageCacheStatistics{};
    this->statistics_.capacity = capacity;
    this->statistics_.dirty_page_limit = dirty_page_limit;
    this->lock_ = SpinLock{};
    this->initialized_ = true;
    return FilePageCacheStatus::Succeeded;
}

FilePageCacheStatus
FilePageCache::Acquire(const FilePageIdentity &identity,
                       void *const reader_context,
                       const FilePageReadOperation read_operation,
                       uint64_t &physical_address,
                       bool &cache_hit) noexcept {
    physical_address = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
    cache_hit = false;
    if (!this->initialized_ || this->entries_ == nullptr ||
        this->frame_allocator_ == nullptr ||
        this->page_access_operation_ == nullptr) {
        return FilePageCacheStatus::NotInitialized;
    }
    if (!this->IdentityIsValid(identity.file)) {
        return FilePageCacheStatus::InvalidIdentity;
    }
    if (reader_context == nullptr || read_operation == nullptr) {
        return FilePageCacheStatus::InvalidReader;
    }

    SpinLockGuard guard{this->lock_};
    FilePageCacheEntry *entry = this->FindEntry(identity);
    if (entry != nullptr) {
        if (entry->mapping_reference_count == UINT64_MAX ||
            this->statistics_.active_mapping_reference_count == UINT64_MAX) {
            return FilePageCacheStatus::Corrupt;
        }
        if (entry->mapping_reference_count ==
            OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE) {
            ++this->statistics_.referenced_page_count;
        }
        ++entry->mapping_reference_count;
        entry->access_generation = this->NextAccessGeneration();
        ++this->statistics_.active_mapping_reference_count;
        ++this->statistics_.hit_count;
        ++this->statistics_.successful_acquire_count;
        physical_address = entry->physical_address;
        cache_hit = true;
        return FilePageCacheStatus::Succeeded;
    }

    ++this->statistics_.miss_count;
    entry = this->SelectLoadEntry();
    if (entry == nullptr) {
        return FilePageCacheStatus::CapacityExhausted;
    }

    uint64_t candidate_physical_address =
        OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
    if (entry->state != FilePageCacheEntryState::Empty) {
        candidate_physical_address = entry->physical_address;
        *entry = FilePageCacheEntry{};
        --this->statistics_.resident_page_count;
        ++this->statistics_.eviction_count;
    } else {
        PhysicalFrame frame{};
        if (this->frame_allocator_->Allocate(frame) !=
            PhysicalFrameAllocatorStatus::Succeeded) {
            return FilePageCacheStatus::FrameAllocationFailed;
        }
        candidate_physical_address = frame.physical_address;
    }

    uint8_t *const page = this->page_access_operation_(
        this->page_access_context_, candidate_physical_address);
    if (page == nullptr) {
        static_cast<void>(this->frame_allocator_->Release(
            PhysicalFrame{.physical_address = candidate_physical_address}));
        ++this->statistics_.failed_load_count;
        return FilePageCacheStatus::FrameAccessFailed;
    }
    for (uint64_t byte_index = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
         byte_index < OS_KERNEL_MEMORY_PAGE_SIZE_BYTES; ++byte_index) {
        page[byte_index] = OS_KERNEL_FILE_PAGE_CACHE_ZERO_BYTE;
    }
    if (!read_operation(reader_context, identity, page,
                        OS_KERNEL_MEMORY_PAGE_SIZE_BYTES)) {
        const PhysicalFrameAllocatorStatus release_status =
            this->frame_allocator_->Release(
                PhysicalFrame{.physical_address = candidate_physical_address});
        ++this->statistics_.failed_load_count;
        return release_status == PhysicalFrameAllocatorStatus::Succeeded
                   ? FilePageCacheStatus::SourceReadFailed
                   : FilePageCacheStatus::FrameReleaseFailed;
    }

    *entry = FilePageCacheEntry{
        .identity = identity,
        .physical_address = candidate_physical_address,
        .mapping_reference_count = OS_KERNEL_FILE_PAGE_CACHE_SINGLE_UNIT,
        .access_generation = this->NextAccessGeneration(),
        .state = FilePageCacheEntryState::Clean,
    };
    ++this->statistics_.resident_page_count;
    ++this->statistics_.referenced_page_count;
    ++this->statistics_.active_mapping_reference_count;
    ++this->statistics_.successful_load_count;
    ++this->statistics_.successful_acquire_count;
    if (this->statistics_.resident_page_count >
        this->statistics_.peak_resident_page_count) {
        this->statistics_.peak_resident_page_count =
            this->statistics_.resident_page_count;
    }
    physical_address = candidate_physical_address;
    return FilePageCacheStatus::Succeeded;
}

FilePageCacheStatus
FilePageCache::Release(const FilePageIdentity &identity,
                       const uint64_t physical_address) noexcept {
    if (!this->initialized_ || this->entries_ == nullptr) {
        return FilePageCacheStatus::NotInitialized;
    }
    SpinLockGuard guard{this->lock_};
    FilePageCacheEntry *const entry = this->FindEntry(identity);
    if (entry == nullptr || entry->physical_address != physical_address) {
        return FilePageCacheStatus::MappingNotFound;
    }
    if (entry->mapping_reference_count ==
            OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE ||
        this->statistics_.active_mapping_reference_count ==
            OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE) {
        return FilePageCacheStatus::ReferenceUnderflow;
    }
    --entry->mapping_reference_count;
    --this->statistics_.active_mapping_reference_count;
    ++this->statistics_.release_count;
    if (entry->mapping_reference_count ==
        OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE) {
        --this->statistics_.referenced_page_count;
    }
    entry->access_generation = this->NextAccessGeneration();
    return FilePageCacheStatus::Succeeded;
}

FilePageCacheStatus
FilePageCache::MarkDirty(const FilePageIdentity &identity,
                         const uint64_t physical_address) noexcept {
    if (!this->initialized_ || this->entries_ == nullptr) {
        return FilePageCacheStatus::NotInitialized;
    }
    if (!this->IdentityIsValid(identity.file)) {
        return FilePageCacheStatus::InvalidIdentity;
    }
    SpinLockGuard guard{this->lock_};
    FilePageCacheEntry *const entry = this->FindEntry(identity);
    if (entry == nullptr || entry->physical_address != physical_address) {
        return FilePageCacheStatus::MappingNotFound;
    }
    if (entry->state == FilePageCacheEntryState::Dirty) {
        return FilePageCacheStatus::Succeeded;
    }
    if (entry->state == FilePageCacheEntryState::Writeback) {
        return FilePageCacheStatus::EntryBusy;
    }
    if (entry->state == FilePageCacheEntryState::Error) {
        if (this->statistics_.error_page_count ==
            OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE) {
            return FilePageCacheStatus::Corrupt;
        }
        --this->statistics_.error_page_count;
        ++this->statistics_.dirty_page_count;
        ++this->statistics_.mark_dirty_count;
        entry->state = FilePageCacheEntryState::Dirty;
        return FilePageCacheStatus::Succeeded;
    }
    if (entry->state != FilePageCacheEntryState::Clean) {
        return FilePageCacheStatus::Corrupt;
    }
    const uint64_t outstanding_page_count =
        this->statistics_.dirty_page_count +
        this->statistics_.writeback_page_count +
        this->statistics_.error_page_count;
    if (outstanding_page_count >= this->dirty_page_limit_) {
        ++this->statistics_.dirty_limit_rejection_count;
        return FilePageCacheStatus::DirtyLimitReached;
    }
    entry->state = FilePageCacheEntryState::Dirty;
    ++this->statistics_.dirty_page_count;
    ++this->statistics_.mark_dirty_count;
    const uint64_t next_outstanding_page_count =
        outstanding_page_count + OS_KERNEL_FILE_PAGE_CACHE_SINGLE_UNIT;
    if (next_outstanding_page_count >
        this->statistics_.peak_outstanding_writeback_page_count) {
        this->statistics_.peak_outstanding_writeback_page_count =
            next_outstanding_page_count;
    }
    return FilePageCacheStatus::Succeeded;
}

FilePageCacheStatus FilePageCache::Writeback(
    void *const writer_context, const FilePageWriteOperation write_operation,
    const uint64_t maximum_page_count, uint64_t &written_page_count) noexcept {
    written_page_count = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
    if (!this->initialized_ || this->entries_ == nullptr ||
        this->page_access_operation_ == nullptr) {
        return FilePageCacheStatus::NotInitialized;
    }
    if (writer_context == nullptr || write_operation == nullptr) {
        return FilePageCacheStatus::InvalidWriter;
    }
    if (maximum_page_count == OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE) {
        return FilePageCacheStatus::Succeeded;
    }

    while (written_page_count < maximum_page_count) {
        FilePageIdentity identity{};
        uint64_t physical_address = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
        bool selected = false;
        {
            SpinLockGuard guard{this->lock_};
            for (uint64_t entry_index = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
                 entry_index < this->capacity_; ++entry_index) {
                FilePageCacheEntry &entry = this->entries_[entry_index];
                if (entry.state != FilePageCacheEntryState::Dirty &&
                    entry.state != FilePageCacheEntryState::Error) {
                    continue;
                }
                if (entry.state == FilePageCacheEntryState::Dirty) {
                    if (this->statistics_.dirty_page_count ==
                        OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE) {
                        return FilePageCacheStatus::Corrupt;
                    }
                    --this->statistics_.dirty_page_count;
                } else {
                    if (this->statistics_.error_page_count ==
                        OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE) {
                        return FilePageCacheStatus::Corrupt;
                    }
                    --this->statistics_.error_page_count;
                }
                entry.state = FilePageCacheEntryState::Writeback;
                ++this->statistics_.writeback_page_count;
                ++this->statistics_.writeback_attempt_count;
                identity = entry.identity;
                physical_address = entry.physical_address;
                selected = true;
                break;
            }
        }
        if (!selected) {
            return FilePageCacheStatus::Succeeded;
        }

        const uint8_t *const page =
            this->page_access_operation_(this->page_access_context_,
                                         physical_address);
        const bool write_succeeded =
            page != nullptr &&
            write_operation(writer_context, identity, page,
                            OS_KERNEL_MEMORY_PAGE_SIZE_BYTES);
        {
            SpinLockGuard guard{this->lock_};
            FilePageCacheEntry *const entry = this->FindEntry(identity);
            if (entry == nullptr || entry->physical_address != physical_address ||
                entry->state != FilePageCacheEntryState::Writeback ||
                this->statistics_.writeback_page_count ==
                    OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE) {
                return FilePageCacheStatus::Corrupt;
            }
            --this->statistics_.writeback_page_count;
            if (write_succeeded) {
                entry->state = FilePageCacheEntryState::Clean;
                ++this->statistics_.successful_writeback_count;
                ++written_page_count;
            } else {
                entry->state = FilePageCacheEntryState::Error;
                ++this->statistics_.error_page_count;
                ++this->statistics_.failed_writeback_count;
                return FilePageCacheStatus::SourceWriteFailed;
            }
        }
    }
    return FilePageCacheStatus::Succeeded;
}

FilePageCacheStatus
FilePageCache::Invalidate(const FileIdentity &identity) noexcept {
    if (!this->initialized_ || this->entries_ == nullptr) {
        return FilePageCacheStatus::NotInitialized;
    }
    if (!this->IdentityIsValid(identity)) {
        return FilePageCacheStatus::InvalidIdentity;
    }
    SpinLockGuard guard{this->lock_};
    for (uint64_t entry_index = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
         entry_index < this->capacity_; ++entry_index) {
        const FilePageCacheEntry &entry = this->entries_[entry_index];
        if (entry.state != FilePageCacheEntryState::Empty &&
            this->IdentitiesEqual(entry.identity.file, identity) &&
            entry.mapping_reference_count !=
                OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE) {
            return FilePageCacheStatus::EntryBusy;
        }
        if (entry.state != FilePageCacheEntryState::Empty &&
            entry.state != FilePageCacheEntryState::Clean &&
            this->IdentitiesEqual(entry.identity.file, identity)) {
            return FilePageCacheStatus::DirtyPagesRemain;
        }
    }
    for (uint64_t entry_index = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
         entry_index < this->capacity_; ++entry_index) {
        FilePageCacheEntry &entry = this->entries_[entry_index];
        if (entry.state == FilePageCacheEntryState::Empty ||
            !this->IdentitiesEqual(entry.identity.file, identity)) {
            continue;
        }
        const FilePageCacheStatus release_status =
            this->ReleaseEntry(entry);
        if (release_status != FilePageCacheStatus::Succeeded) {
            return release_status;
        }
        ++this->statistics_.invalidation_count;
    }
    return FilePageCacheStatus::Succeeded;
}

FilePageCacheStatus
FilePageCache::Trim(const uint64_t target_resident_page_count) noexcept {
    if (!this->initialized_ || this->entries_ == nullptr) {
        return FilePageCacheStatus::NotInitialized;
    }
    if (target_resident_page_count > this->capacity_) {
        return FilePageCacheStatus::InvalidCapacity;
    }
    SpinLockGuard guard{this->lock_};
    while (this->statistics_.resident_page_count >
           target_resident_page_count) {
        FilePageCacheEntry *candidate = nullptr;
        for (uint64_t entry_index = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
             entry_index < this->capacity_; ++entry_index) {
            FilePageCacheEntry &entry = this->entries_[entry_index];
            if (entry.state != FilePageCacheEntryState::Clean ||
                entry.mapping_reference_count !=
                    OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE) {
                continue;
            }
            if (candidate == nullptr ||
                entry.access_generation < candidate->access_generation) {
                candidate = &entry;
            }
        }
        if (candidate == nullptr) {
            for (uint64_t entry_index = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
                 entry_index < this->capacity_; ++entry_index) {
                if (this->entries_[entry_index].state !=
                        FilePageCacheEntryState::Empty &&
                    this->entries_[entry_index].state !=
                        FilePageCacheEntryState::Clean) {
                    return FilePageCacheStatus::DirtyPagesRemain;
                }
            }
            return FilePageCacheStatus::EntryBusy;
        }
        const FilePageCacheStatus release_status =
            this->ReleaseEntry(*candidate);
        if (release_status != FilePageCacheStatus::Succeeded) {
            return release_status;
        }
        ++this->statistics_.eviction_count;
    }
    return FilePageCacheStatus::Succeeded;
}

FilePageCacheStatus FilePageCache::Validate() const noexcept {
    if (!this->initialized_ || this->entries_ == nullptr ||
        this->frame_allocator_ == nullptr ||
        this->page_access_operation_ == nullptr) {
        return FilePageCacheStatus::NotInitialized;
    }
    SpinLockGuard guard{this->lock_};
    uint64_t resident_page_count =
        OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
    uint64_t referenced_page_count =
        OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
    uint64_t mapping_reference_count =
        OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
    uint64_t dirty_page_count = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
    uint64_t writeback_page_count = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
    uint64_t error_page_count = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
    for (uint64_t entry_index = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
         entry_index < this->capacity_; ++entry_index) {
        const FilePageCacheEntry &entry = this->entries_[entry_index];
        if (entry.state == FilePageCacheEntryState::Empty) {
            if (entry.mapping_reference_count !=
                OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE) {
                return FilePageCacheStatus::Corrupt;
            }
            continue;
        }
        if (!this->IdentityIsValid(entry.identity.file) ||
            entry.access_generation ==
                OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE ||
            !this->frame_allocator_->OwnsAllocation(
                PhysicalFrame{.physical_address = entry.physical_address})) {
            return FilePageCacheStatus::Corrupt;
        }
        if (entry.state == FilePageCacheEntryState::Dirty) {
            ++dirty_page_count;
        } else if (entry.state == FilePageCacheEntryState::Writeback) {
            ++writeback_page_count;
        } else if (entry.state == FilePageCacheEntryState::Error) {
            ++error_page_count;
        } else if (entry.state != FilePageCacheEntryState::Clean) {
            return FilePageCacheStatus::Corrupt;
        }
        for (uint64_t comparison_index =
                 entry_index + OS_KERNEL_FILE_PAGE_CACHE_SINGLE_UNIT;
             comparison_index < this->capacity_; ++comparison_index) {
            const FilePageCacheEntry &comparison_entry =
                this->entries_[comparison_index];
            if (comparison_entry.state != FilePageCacheEntryState::Empty &&
                this->PageIdentitiesEqual(entry.identity,
                                          comparison_entry.identity)) {
                return FilePageCacheStatus::Corrupt;
            }
        }
        ++resident_page_count;
        if (entry.mapping_reference_count !=
            OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE) {
            ++referenced_page_count;
        }
        if (mapping_reference_count >
            UINT64_MAX - entry.mapping_reference_count) {
            return FilePageCacheStatus::Corrupt;
        }
        mapping_reference_count += entry.mapping_reference_count;
    }
    return resident_page_count == this->statistics_.resident_page_count &&
                   referenced_page_count ==
                       this->statistics_.referenced_page_count &&
                   mapping_reference_count ==
                       this->statistics_.active_mapping_reference_count &&
                   this->statistics_.resident_page_count <= this->capacity_ &&
                   this->statistics_.referenced_page_count <=
                       this->statistics_.resident_page_count &&
                   dirty_page_count == this->statistics_.dirty_page_count &&
                   writeback_page_count ==
                       this->statistics_.writeback_page_count &&
                   error_page_count == this->statistics_.error_page_count &&
                   dirty_page_count + writeback_page_count + error_page_count <=
                       this->dirty_page_limit_
               ? FilePageCacheStatus::Succeeded
               : FilePageCacheStatus::Corrupt;
}

FilePageCacheStatistics FilePageCache::Statistics() const noexcept {
    if (!this->initialized_) {
        return FilePageCacheStatistics{};
    }
    SpinLockGuard guard{this->lock_};
    return this->statistics_;
}

FilePageCacheStatus FilePageCache::Destroy() noexcept {
    if (!this->initialized_) {
        return FilePageCacheStatus::NotInitialized;
    }
    const FilePageCacheStatus trim_status =
        this->Trim(OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE);
    if (trim_status != FilePageCacheStatus::Succeeded) {
        return trim_status;
    }
    SpinLockGuard guard{this->lock_};
    this->entries_ = nullptr;
    this->capacity_ = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
    this->dirty_page_limit_ = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
    this->frame_allocator_ = nullptr;
    this->page_access_context_ = nullptr;
    this->page_access_operation_ = nullptr;
    this->access_generation_ = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
    this->statistics_ = FilePageCacheStatistics{};
    this->initialized_ = false;
    return FilePageCacheStatus::Succeeded;
}

bool FilePageCache::IdentitiesEqual(const FileIdentity &left,
                                    const FileIdentity &right) const noexcept {
    return left.superblock_identifier == right.superblock_identifier &&
           left.superblock_generation == right.superblock_generation &&
           left.node_identifier == right.node_identifier &&
           left.node_generation == right.node_generation;
}

bool FilePageCache::PageIdentitiesEqual(
    const FilePageIdentity &left,
    const FilePageIdentity &right) const noexcept {
    return this->IdentitiesEqual(left.file, right.file) &&
           left.page_index == right.page_index;
}

bool FilePageCache::IdentityIsValid(
    const FileIdentity &identity) const noexcept {
    return identity.superblock_identifier !=
               OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE &&
           identity.superblock_generation !=
               OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE &&
           identity.node_identifier != UINT64_MAX &&
           identity.node_generation !=
               OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
}

uint64_t FilePageCache::NextAccessGeneration() noexcept {
    if (this->access_generation_ == UINT64_MAX) {
        for (uint64_t entry_index = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
             entry_index < this->capacity_; ++entry_index) {
            this->entries_[entry_index].access_generation /=
                OS_KERNEL_FILE_PAGE_CACHE_GENERATION_REBASE_DIVISOR;
        }
        this->access_generation_ /=
            OS_KERNEL_FILE_PAGE_CACHE_GENERATION_REBASE_DIVISOR;
    }
    ++this->access_generation_;
    return this->access_generation_;
}

FilePageCacheEntry *FilePageCache::FindEntry(
    const FilePageIdentity &identity) noexcept {
    for (uint64_t entry_index = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
         entry_index < this->capacity_; ++entry_index) {
        FilePageCacheEntry &entry = this->entries_[entry_index];
        if (entry.state != FilePageCacheEntryState::Empty &&
            this->PageIdentitiesEqual(entry.identity, identity)) {
            return &entry;
        }
    }
    return nullptr;
}

FilePageCacheEntry *FilePageCache::SelectLoadEntry() noexcept {
    FilePageCacheEntry *least_recently_used_entry = nullptr;
    for (uint64_t entry_index = OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE;
         entry_index < this->capacity_; ++entry_index) {
        FilePageCacheEntry &entry = this->entries_[entry_index];
        if (entry.state == FilePageCacheEntryState::Empty) {
            return &entry;
        }
        if (entry.state != FilePageCacheEntryState::Clean) {
            continue;
        }
        if (entry.mapping_reference_count !=
            OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE) {
            continue;
        }
        if (least_recently_used_entry == nullptr ||
            entry.access_generation <
                least_recently_used_entry->access_generation) {
            least_recently_used_entry = &entry;
        }
    }
    return least_recently_used_entry;
}

FilePageCacheStatus
FilePageCache::ReleaseEntry(FilePageCacheEntry &entry) noexcept {
    if (entry.state == FilePageCacheEntryState::Empty) {
        return FilePageCacheStatus::Succeeded;
    }
    if (entry.state != FilePageCacheEntryState::Clean) {
        return FilePageCacheStatus::DirtyPagesRemain;
    }
    if (entry.mapping_reference_count !=
        OS_KERNEL_FILE_PAGE_CACHE_EMPTY_VALUE) {
        return FilePageCacheStatus::EntryBusy;
    }
    if (this->frame_allocator_->Release(
            PhysicalFrame{.physical_address = entry.physical_address}) !=
        PhysicalFrameAllocatorStatus::Succeeded) {
        return FilePageCacheStatus::FrameReleaseFailed;
    }
    entry = FilePageCacheEntry{};
    --this->statistics_.resident_page_count;
    return FilePageCacheStatus::Succeeded;
}

}

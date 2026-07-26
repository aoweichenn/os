#include "os/kernel/io/file_table.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_FILE_TABLE_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_FILE_TABLE_CHUNK_ALIGNMENT_BYTES = 64ULL;
constexpr uint64_t OS_KERNEL_FILE_TABLE_INVALID_DESCRIPTOR = UINT64_MAX;

struct FileTableEntry final {
    KernelObjectHandle handle;
    uint64_t descriptor_flags;
};

[[nodiscard]] uint64_t Maximum(const uint64_t left, const uint64_t right) noexcept {
    return left > right ? left : right;
}

[[nodiscard]] uint64_t ChunkBaseForDescriptor(const uint64_t descriptor) noexcept {
    return descriptor - descriptor % OS_KERNEL_FILE_TABLE_CHUNK_DESCRIPTOR_COUNT;
}

[[nodiscard]] uint64_t ChunkEntryIndex(const uint64_t descriptor) noexcept {
    return descriptor % OS_KERNEL_FILE_TABLE_CHUNK_DESCRIPTOR_COUNT;
}

}

struct alignas(OS_KERNEL_FILE_TABLE_CHUNK_ALIGNMENT_BYTES) FileTableChunk final {
    uint64_t base_descriptor;
    FileTableChunk *next;
    FileTableEntry entries[OS_KERNEL_FILE_TABLE_CHUNK_DESCRIPTOR_COUNT];
};

FileTableStatus FileTable::Initialize(KernelHeap &heap, KernelObjectManager &object_manager,
                                      const uint64_t soft_limit,
                                      const uint64_t hard_limit) noexcept {
    if (this->initialized_) {
        return FileTableStatus::AlreadyInitialized;
    }
    if (heap.Validate() != KernelHeapStatus::Succeeded ||
        object_manager.Validate() != KernelObjectStatus::Succeeded) {
        return FileTableStatus::InvalidDependency;
    }
    const FileTableStatus limit_status = this->ValidateLimits(soft_limit, hard_limit);
    if (limit_status != FileTableStatus::Succeeded) {
        return limit_status;
    }
    this->heap_ = &heap;
    this->object_manager_ = &object_manager;
    this->chunk_head_ = nullptr;
    this->lock_ = SpinLock{};
    this->statistics_ = FileTableStatistics{
        .soft_limit = soft_limit,
        .hard_limit = hard_limit,
        .active_descriptor_count = OS_KERNEL_FILE_TABLE_EMPTY_VALUE,
        .allocated_chunk_count = OS_KERNEL_FILE_TABLE_EMPTY_VALUE,
        .peak_active_descriptor_count = OS_KERNEL_FILE_TABLE_EMPTY_VALUE,
        .peak_allocated_chunk_count = OS_KERNEL_FILE_TABLE_EMPTY_VALUE,
        .successful_installation_count = OS_KERNEL_FILE_TABLE_EMPTY_VALUE,
        .successful_lookup_count = OS_KERNEL_FILE_TABLE_EMPTY_VALUE,
        .successful_duplicate_count = OS_KERNEL_FILE_TABLE_EMPTY_VALUE,
        .successful_close_count = OS_KERNEL_FILE_TABLE_EMPTY_VALUE,
        .close_on_exec_count = OS_KERNEL_FILE_TABLE_EMPTY_VALUE,
        .chunk_allocation_count = OS_KERNEL_FILE_TABLE_EMPTY_VALUE,
        .chunk_release_count = OS_KERNEL_FILE_TABLE_EMPTY_VALUE,
        .limit_rejection_count = OS_KERNEL_FILE_TABLE_EMPTY_VALUE,
        .two_phase_rollback_count = OS_KERNEL_FILE_TABLE_EMPTY_VALUE,
    };
    this->initialized_ = true;
    return FileTableStatus::Succeeded;
}

FileTableStatus FileTable::Install(KernelObjectReference &reference,
                                   const uint64_t minimum_descriptor,
                                   const uint64_t descriptor_flags, uint64_t &descriptor) noexcept {
    descriptor = OS_KERNEL_FILE_TABLE_INVALID_DESCRIPTOR;
    if (!this->initialized_) {
        return FileTableStatus::NotInitialized;
    }
    if (!reference.IsActive() || reference.manager_ != this->object_manager_) {
        return FileTableStatus::ObjectFailure;
    }
    if (!this->AreDescriptorFlagsValid(descriptor_flags)) {
        return FileTableStatus::InvalidFlags;
    }

    while (true) {
        uint64_t missing_chunk_base_descriptor = OS_KERNEL_FILE_TABLE_INVALID_DESCRIPTOR;
        this->lock_.Lock();
        if (minimum_descriptor >= this->statistics_.soft_limit) {
            ++this->statistics_.limit_rejection_count;
            this->lock_.Unlock();
            return FileTableStatus::SoftLimitExceeded;
        }
        const bool found = this->FindLowestAvailableDescriptor(minimum_descriptor, descriptor,
                                                               missing_chunk_base_descriptor);
        if (!found) {
            ++this->statistics_.limit_rejection_count;
            this->lock_.Unlock();
            return FileTableStatus::SoftLimitExceeded;
        }
        if (missing_chunk_base_descriptor != OS_KERNEL_FILE_TABLE_INVALID_DESCRIPTOR) {
            this->lock_.Unlock();
            const FileTableStatus chunk_status = this->EnsureChunk(missing_chunk_base_descriptor);
            if (chunk_status != FileTableStatus::Succeeded) {
                descriptor = OS_KERNEL_FILE_TABLE_INVALID_DESCRIPTOR;
                return chunk_status;
            }
            continue;
        }

        FileTableChunk *const chunk = this->FindChunk(ChunkBaseForDescriptor(descriptor));
        if (chunk == nullptr) {
            this->lock_.Unlock();
            descriptor = OS_KERNEL_FILE_TABLE_INVALID_DESCRIPTOR;
            return FileTableStatus::CorruptedState;
        }
        FileTableEntry &entry = chunk->entries[ChunkEntryIndex(descriptor)];
        if (!entry.handle.IsEmpty()) {
            this->lock_.Unlock();
            continue;
        }
        KernelObjectHandle handle{};
        if (this->object_manager_->DetachReference(reference, handle) !=
            KernelObjectStatus::Succeeded) {
            this->lock_.Unlock();
            descriptor = OS_KERNEL_FILE_TABLE_INVALID_DESCRIPTOR;
            return FileTableStatus::ObjectFailure;
        }
        entry.handle = handle;
        entry.descriptor_flags = descriptor_flags;
        ++this->statistics_.active_descriptor_count;
        ++this->statistics_.successful_installation_count;
        this->statistics_.peak_active_descriptor_count =
            Maximum(this->statistics_.peak_active_descriptor_count,
                    this->statistics_.active_descriptor_count);
        this->lock_.Unlock();
        return FileTableStatus::Succeeded;
    }
}

FileTableStatus FileTable::InstallExact(KernelObjectReference &reference, const uint64_t descriptor,
                                        const uint64_t descriptor_flags) noexcept {
    if (!this->initialized_) {
        return FileTableStatus::NotInitialized;
    }
    if (!reference.IsActive() || reference.manager_ != this->object_manager_) {
        return FileTableStatus::ObjectFailure;
    }
    if (!this->AreDescriptorFlagsValid(descriptor_flags)) {
        return FileTableStatus::InvalidFlags;
    }

    this->lock_.Lock();
    if (descriptor >= this->statistics_.soft_limit) {
        ++this->statistics_.limit_rejection_count;
        this->lock_.Unlock();
        return FileTableStatus::SoftLimitExceeded;
    }
    this->lock_.Unlock();

    const uint64_t chunk_base_descriptor = ChunkBaseForDescriptor(descriptor);
    const FileTableStatus chunk_status = this->EnsureChunk(chunk_base_descriptor);
    if (chunk_status != FileTableStatus::Succeeded) {
        return chunk_status;
    }

    this->lock_.Lock();
    if (descriptor >= this->statistics_.soft_limit) {
        ++this->statistics_.limit_rejection_count;
        this->lock_.Unlock();
        return FileTableStatus::SoftLimitExceeded;
    }
    FileTableChunk *const chunk = this->FindChunk(chunk_base_descriptor);
    if (chunk == nullptr) {
        this->lock_.Unlock();
        return FileTableStatus::CorruptedState;
    }
    FileTableEntry &entry = chunk->entries[ChunkEntryIndex(descriptor)];
    if (!entry.handle.IsEmpty()) {
        this->lock_.Unlock();
        return FileTableStatus::DescriptorOccupied;
    }
    KernelObjectHandle handle{};
    if (this->object_manager_->DetachReference(reference, handle) !=
        KernelObjectStatus::Succeeded) {
        this->lock_.Unlock();
        return FileTableStatus::ObjectFailure;
    }
    entry.handle = handle;
    entry.descriptor_flags = descriptor_flags;
    ++this->statistics_.active_descriptor_count;
    ++this->statistics_.successful_installation_count;
    this->statistics_.peak_active_descriptor_count = Maximum(
        this->statistics_.peak_active_descriptor_count, this->statistics_.active_descriptor_count);
    this->lock_.Unlock();
    return FileTableStatus::Succeeded;
}

FileTableStatus FileTable::Lookup(const uint64_t descriptor,
                                  KernelObjectReference &reference) noexcept {
    if (!this->initialized_) {
        return FileTableStatus::NotInitialized;
    }
    if (reference.IsActive()) {
        return FileTableStatus::ObjectFailure;
    }
    this->lock_.Lock();
    if (descriptor >= this->statistics_.hard_limit) {
        this->lock_.Unlock();
        return FileTableStatus::InvalidDescriptor;
    }
    FileTableChunk *const chunk = this->FindChunk(ChunkBaseForDescriptor(descriptor));
    if (chunk == nullptr) {
        this->lock_.Unlock();
        return FileTableStatus::InvalidDescriptor;
    }
    const FileTableEntry &entry = chunk->entries[ChunkEntryIndex(descriptor)];
    if (entry.handle.IsEmpty()) {
        this->lock_.Unlock();
        return FileTableStatus::InvalidDescriptor;
    }
    const KernelObjectStatus acquire_status =
        this->object_manager_->AcquireHandle(entry.handle, reference);
    if (acquire_status != KernelObjectStatus::Succeeded) {
        this->lock_.Unlock();
        return FileTableStatus::ObjectFailure;
    }
    ++this->statistics_.successful_lookup_count;
    this->lock_.Unlock();
    return FileTableStatus::Succeeded;
}

FileTableStatus FileTable::Duplicate(const uint64_t source_descriptor,
                                     const uint64_t minimum_descriptor,
                                     const uint64_t descriptor_flags,
                                     uint64_t &destination_descriptor) noexcept {
    destination_descriptor = OS_KERNEL_FILE_TABLE_INVALID_DESCRIPTOR;
    if (!this->AreDescriptorFlagsValid(descriptor_flags)) {
        return FileTableStatus::InvalidFlags;
    }
    KernelObjectReference reference{};
    const FileTableStatus lookup_status = this->Lookup(source_descriptor, reference);
    if (lookup_status != FileTableStatus::Succeeded) {
        return lookup_status;
    }
    const FileTableStatus install_status =
        this->Install(reference, minimum_descriptor, descriptor_flags, destination_descriptor);
    if (install_status != FileTableStatus::Succeeded) {
        return install_status;
    }
    this->lock_.Lock();
    ++this->statistics_.successful_duplicate_count;
    this->lock_.Unlock();
    return FileTableStatus::Succeeded;
}

FileTableStatus FileTable::Close(const uint64_t descriptor,
                                 KernelObjectReleaseResult &release_result) noexcept {
    release_result = KernelObjectReleaseResult{};
    if (!this->initialized_) {
        return FileTableStatus::NotInitialized;
    }
    this->lock_.Lock();
    if (descriptor >= this->statistics_.hard_limit) {
        this->lock_.Unlock();
        return FileTableStatus::InvalidDescriptor;
    }
    FileTableChunk *const chunk = this->FindChunk(ChunkBaseForDescriptor(descriptor));
    if (chunk == nullptr) {
        this->lock_.Unlock();
        return FileTableStatus::InvalidDescriptor;
    }
    FileTableEntry &entry = chunk->entries[ChunkEntryIndex(descriptor)];
    if (entry.handle.IsEmpty()) {
        this->lock_.Unlock();
        return FileTableStatus::InvalidDescriptor;
    }
    if (this->statistics_.active_descriptor_count == OS_KERNEL_FILE_TABLE_EMPTY_VALUE) {
        this->lock_.Unlock();
        return FileTableStatus::CorruptedState;
    }
    KernelObjectHandle handle = entry.handle;
    entry.handle = KernelObjectHandle{};
    entry.descriptor_flags = OS_KERNEL_FILE_TABLE_EMPTY_VALUE;
    --this->statistics_.active_descriptor_count;
    ++this->statistics_.successful_close_count;
    this->lock_.Unlock();

    const KernelObjectStatus release_status =
        this->object_manager_->ReleaseHandle(handle, release_result);
    if (release_status == KernelObjectStatus::Succeeded) {
        return FileTableStatus::Succeeded;
    }
    return FileTableStatus::ReleaseFailed;
}

FileTableStatus FileTable::GetDescriptorFlags(const uint64_t descriptor,
                                              uint64_t &descriptor_flags) noexcept {
    descriptor_flags = OS_KERNEL_FILE_TABLE_EMPTY_VALUE;
    if (!this->initialized_) {
        return FileTableStatus::NotInitialized;
    }
    this->lock_.Lock();
    if (descriptor >= this->statistics_.hard_limit) {
        this->lock_.Unlock();
        return FileTableStatus::InvalidDescriptor;
    }
    FileTableChunk *const chunk = this->FindChunk(ChunkBaseForDescriptor(descriptor));
    if (chunk == nullptr) {
        this->lock_.Unlock();
        return FileTableStatus::InvalidDescriptor;
    }
    const FileTableEntry &entry = chunk->entries[ChunkEntryIndex(descriptor)];
    if (entry.handle.IsEmpty()) {
        this->lock_.Unlock();
        return FileTableStatus::InvalidDescriptor;
    }
    descriptor_flags = entry.descriptor_flags;
    this->lock_.Unlock();
    return FileTableStatus::Succeeded;
}

FileTableStatus FileTable::SetDescriptorFlags(const uint64_t descriptor,
                                              const uint64_t descriptor_flags) noexcept {
    if (!this->initialized_) {
        return FileTableStatus::NotInitialized;
    }
    if (!this->AreDescriptorFlagsValid(descriptor_flags)) {
        return FileTableStatus::InvalidFlags;
    }
    this->lock_.Lock();
    if (descriptor >= this->statistics_.hard_limit) {
        this->lock_.Unlock();
        return FileTableStatus::InvalidDescriptor;
    }
    FileTableChunk *const chunk = this->FindChunk(ChunkBaseForDescriptor(descriptor));
    if (chunk == nullptr) {
        this->lock_.Unlock();
        return FileTableStatus::InvalidDescriptor;
    }
    FileTableEntry &entry = chunk->entries[ChunkEntryIndex(descriptor)];
    if (entry.handle.IsEmpty()) {
        this->lock_.Unlock();
        return FileTableStatus::InvalidDescriptor;
    }
    entry.descriptor_flags = descriptor_flags;
    this->lock_.Unlock();
    return FileTableStatus::Succeeded;
}

FileTableStatus FileTable::SetSoftLimit(const uint64_t soft_limit) noexcept {
    if (!this->initialized_) {
        return FileTableStatus::NotInitialized;
    }
    this->lock_.Lock();
    if (soft_limit == OS_KERNEL_FILE_TABLE_EMPTY_VALUE ||
        soft_limit > this->statistics_.hard_limit) {
        this->lock_.Unlock();
        return FileTableStatus::InvalidLimit;
    }
    this->statistics_.soft_limit = soft_limit;
    this->lock_.Unlock();
    return FileTableStatus::Succeeded;
}

FileTableStatus FileTable::CloseOnExec(uint64_t &closed_descriptor_count) noexcept {
    closed_descriptor_count = OS_KERNEL_FILE_TABLE_EMPTY_VALUE;
    if (!this->initialized_) {
        return FileTableStatus::NotInitialized;
    }
    const uint64_t hard_limit = this->statistics_.hard_limit;
    for (uint64_t descriptor = OS_KERNEL_FILE_TABLE_EMPTY_VALUE; descriptor < hard_limit;
         ++descriptor) {
        uint64_t descriptor_flags = OS_KERNEL_FILE_TABLE_EMPTY_VALUE;
        if (this->GetDescriptorFlags(descriptor, descriptor_flags) != FileTableStatus::Succeeded ||
            (descriptor_flags & OS_KERNEL_FILE_DESCRIPTOR_CLOSE_ON_EXEC_FLAG) ==
                OS_KERNEL_FILE_TABLE_EMPTY_VALUE) {
            continue;
        }
        KernelObjectReleaseResult release_result{};
        if (this->Close(descriptor, release_result) != FileTableStatus::Succeeded) {
            return FileTableStatus::ReleaseFailed;
        }
        ++closed_descriptor_count;
    }
    this->lock_.Lock();
    this->statistics_.close_on_exec_count += closed_descriptor_count;
    this->lock_.Unlock();
    return FileTableStatus::Succeeded;
}

FileTableStatus FileTable::Destroy() noexcept {
    if (!this->initialized_) {
        return FileTableStatus::NotInitialized;
    }
    this->lock_.Lock();
    FileTableChunk *chunk = this->chunk_head_;
    this->chunk_head_ = nullptr;
    this->initialized_ = false;
    this->lock_.Unlock();

    bool release_succeeded = true;
    while (chunk != nullptr) {
        FileTableChunk *const next = chunk->next;
        for (uint64_t entry_index = OS_KERNEL_FILE_TABLE_EMPTY_VALUE;
             entry_index < OS_KERNEL_FILE_TABLE_CHUNK_DESCRIPTOR_COUNT; ++entry_index) {
            FileTableEntry &entry = chunk->entries[entry_index];
            if (entry.handle.IsEmpty()) {
                continue;
            }
            KernelObjectReleaseResult release_result{};
            if (this->object_manager_->ReleaseHandle(entry.handle, release_result) !=
                KernelObjectStatus::Succeeded) {
                release_succeeded = false;
            }
            entry.descriptor_flags = OS_KERNEL_FILE_TABLE_EMPTY_VALUE;
        }
        if (this->heap_->TryRelease(chunk) != KernelHeapStatus::Succeeded) {
            release_succeeded = false;
        } else {
            ++this->statistics_.chunk_release_count;
        }
        chunk = next;
    }
    this->statistics_.active_descriptor_count = OS_KERNEL_FILE_TABLE_EMPTY_VALUE;
    this->statistics_.allocated_chunk_count = OS_KERNEL_FILE_TABLE_EMPTY_VALUE;
    this->heap_ = nullptr;
    this->object_manager_ = nullptr;
    return release_succeeded ? FileTableStatus::Succeeded : FileTableStatus::ReleaseFailed;
}

FileTableStatus FileTable::Validate() noexcept {
    if (!this->initialized_) {
        return FileTableStatus::NotInitialized;
    }
    this->lock_.Lock();
    uint64_t observed_chunk_count = OS_KERNEL_FILE_TABLE_EMPTY_VALUE;
    uint64_t observed_descriptor_count = OS_KERNEL_FILE_TABLE_EMPTY_VALUE;
    uint64_t previous_base_descriptor = OS_KERNEL_FILE_TABLE_INVALID_DESCRIPTOR;
    for (FileTableChunk *chunk = this->chunk_head_; chunk != nullptr; chunk = chunk->next) {
        if (chunk->base_descriptor >= this->statistics_.hard_limit ||
            chunk->base_descriptor % OS_KERNEL_FILE_TABLE_CHUNK_DESCRIPTOR_COUNT !=
                OS_KERNEL_FILE_TABLE_EMPTY_VALUE ||
            (previous_base_descriptor != OS_KERNEL_FILE_TABLE_INVALID_DESCRIPTOR &&
             chunk->base_descriptor <= previous_base_descriptor)) {
            this->lock_.Unlock();
            return FileTableStatus::CorruptedState;
        }
        ++observed_chunk_count;
        previous_base_descriptor = chunk->base_descriptor;
        for (uint64_t entry_index = OS_KERNEL_FILE_TABLE_EMPTY_VALUE;
             entry_index < OS_KERNEL_FILE_TABLE_CHUNK_DESCRIPTOR_COUNT; ++entry_index) {
            const uint64_t descriptor = chunk->base_descriptor + entry_index;
            const FileTableEntry &entry = chunk->entries[entry_index];
            if (descriptor >= this->statistics_.hard_limit) {
                if (!entry.handle.IsEmpty()) {
                    this->lock_.Unlock();
                    return FileTableStatus::CorruptedState;
                }
                continue;
            }
            if (entry.handle.IsEmpty()) {
                if (entry.descriptor_flags != OS_KERNEL_FILE_TABLE_EMPTY_VALUE) {
                    this->lock_.Unlock();
                    return FileTableStatus::CorruptedState;
                }
                continue;
            }
            if (!this->AreDescriptorFlagsValid(entry.descriptor_flags) ||
                !this->object_manager_->ValidateHandle(entry.handle)) {
                this->lock_.Unlock();
                return FileTableStatus::CorruptedState;
            }
            ++observed_descriptor_count;
        }
    }
    const bool valid =
        observed_chunk_count == this->statistics_.allocated_chunk_count &&
        observed_descriptor_count == this->statistics_.active_descriptor_count &&
        this->statistics_.soft_limit != OS_KERNEL_FILE_TABLE_EMPTY_VALUE &&
        this->statistics_.soft_limit <= this->statistics_.hard_limit &&
        this->statistics_.hard_limit <= OS_KERNEL_FILE_TABLE_MAXIMUM_HARD_LIMIT &&
        this->statistics_.peak_active_descriptor_count >=
            this->statistics_.active_descriptor_count &&
        this->statistics_.peak_allocated_chunk_count >= this->statistics_.allocated_chunk_count;
    this->lock_.Unlock();
    return valid ? FileTableStatus::Succeeded : FileTableStatus::CorruptedState;
}

FileTableStatistics FileTable::Statistics() noexcept {
    if (!this->initialized_) {
        return this->statistics_;
    }
    this->lock_.Lock();
    const FileTableStatistics statistics = this->statistics_;
    this->lock_.Unlock();
    return statistics;
}

FileTableChunk *FileTable::FindChunk(const uint64_t chunk_base_descriptor) noexcept {
    for (FileTableChunk *chunk = this->chunk_head_; chunk != nullptr; chunk = chunk->next) {
        if (chunk->base_descriptor == chunk_base_descriptor) {
            return chunk;
        }
        if (chunk->base_descriptor > chunk_base_descriptor) {
            return nullptr;
        }
    }
    return nullptr;
}

FileTableStatus FileTable::EnsureChunk(const uint64_t chunk_base_descriptor) noexcept {
    this->lock_.Lock();
    if (!this->initialized_) {
        this->lock_.Unlock();
        return FileTableStatus::NotInitialized;
    }
    if (chunk_base_descriptor >= this->statistics_.hard_limit) {
        this->lock_.Unlock();
        return FileTableStatus::InvalidDescriptor;
    }
    if (this->FindChunk(chunk_base_descriptor) != nullptr) {
        this->lock_.Unlock();
        return FileTableStatus::Succeeded;
    }
    KernelHeap *const heap = this->heap_;
    this->lock_.Unlock();

    // 堆申请可能耗时，不能在持表锁时进行。申请完成后重新检查，若另一条路径
    // 已经提交相同分块，则释放本次准备对象，形成明确的两阶段回滚。
    void *allocation = nullptr;
    if (heap->TryAllocate(sizeof(FileTableChunk), OS_KERNEL_FILE_TABLE_CHUNK_ALIGNMENT_BYTES,
                          allocation) != KernelHeapStatus::Succeeded) {
        return FileTableStatus::AllocationFailed;
    }
    FileTableChunk *const prepared_chunk = static_cast<FileTableChunk *>(allocation);
    *prepared_chunk = FileTableChunk{
        .base_descriptor = chunk_base_descriptor,
        .next = nullptr,
        .entries = {},
    };

    this->lock_.Lock();
    if (!this->initialized_) {
        this->lock_.Unlock();
        static_cast<void>(heap->TryRelease(prepared_chunk));
        return FileTableStatus::NotInitialized;
    }
    if (this->FindChunk(chunk_base_descriptor) != nullptr) {
        ++this->statistics_.two_phase_rollback_count;
        this->lock_.Unlock();
        if (heap->TryRelease(prepared_chunk) != KernelHeapStatus::Succeeded) {
            return FileTableStatus::ReleaseFailed;
        }
        return FileTableStatus::Succeeded;
    }
    this->InsertChunk(*prepared_chunk);
    ++this->statistics_.allocated_chunk_count;
    ++this->statistics_.chunk_allocation_count;
    this->statistics_.peak_allocated_chunk_count = Maximum(
        this->statistics_.peak_allocated_chunk_count, this->statistics_.allocated_chunk_count);
    this->lock_.Unlock();
    return FileTableStatus::Succeeded;
}

bool FileTable::FindLowestAvailableDescriptor(const uint64_t minimum_descriptor,
                                              uint64_t &descriptor,
                                              uint64_t &missing_chunk_base_descriptor) noexcept {
    descriptor = OS_KERNEL_FILE_TABLE_INVALID_DESCRIPTOR;
    missing_chunk_base_descriptor = OS_KERNEL_FILE_TABLE_INVALID_DESCRIPTOR;
    uint64_t candidate = minimum_descriptor;
    while (candidate < this->statistics_.soft_limit) {
        const uint64_t chunk_base_descriptor = ChunkBaseForDescriptor(candidate);
        FileTableChunk *const chunk = this->FindChunk(chunk_base_descriptor);
        if (chunk == nullptr) {
            descriptor = candidate;
            missing_chunk_base_descriptor = chunk_base_descriptor;
            return true;
        }
        const uint64_t chunk_end_descriptor =
            chunk_base_descriptor > UINT64_MAX - OS_KERNEL_FILE_TABLE_CHUNK_DESCRIPTOR_COUNT
                ? UINT64_MAX
                : chunk_base_descriptor + OS_KERNEL_FILE_TABLE_CHUNK_DESCRIPTOR_COUNT;
        const uint64_t scan_end_descriptor = chunk_end_descriptor < this->statistics_.soft_limit
                                                 ? chunk_end_descriptor
                                                 : this->statistics_.soft_limit;
        while (candidate < scan_end_descriptor) {
            if (chunk->entries[ChunkEntryIndex(candidate)].handle.IsEmpty()) {
                descriptor = candidate;
                return true;
            }
            ++candidate;
        }
    }
    return false;
}

FileTableStatus FileTable::ValidateLimits(const uint64_t soft_limit,
                                          const uint64_t hard_limit) const noexcept {
    if (soft_limit == OS_KERNEL_FILE_TABLE_EMPTY_VALUE ||
        hard_limit == OS_KERNEL_FILE_TABLE_EMPTY_VALUE || soft_limit > hard_limit ||
        hard_limit > OS_KERNEL_FILE_TABLE_MAXIMUM_HARD_LIMIT) {
        return FileTableStatus::InvalidLimit;
    }
    return FileTableStatus::Succeeded;
}

bool FileTable::AreDescriptorFlagsValid(const uint64_t descriptor_flags) const noexcept {
    return (descriptor_flags & ~OS_KERNEL_FILE_DESCRIPTOR_VALID_FLAG_MASK) ==
           OS_KERNEL_FILE_TABLE_EMPTY_VALUE;
}

void FileTable::InsertChunk(FileTableChunk &chunk) noexcept {
    if (this->chunk_head_ == nullptr ||
        chunk.base_descriptor < this->chunk_head_->base_descriptor) {
        chunk.next = this->chunk_head_;
        this->chunk_head_ = &chunk;
        return;
    }
    FileTableChunk *previous = this->chunk_head_;
    while (previous->next != nullptr && previous->next->base_descriptor < chunk.base_descriptor) {
        previous = previous->next;
    }
    chunk.next = previous->next;
    previous->next = &chunk;
}

}

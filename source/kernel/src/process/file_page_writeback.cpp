#include <os/kernel/process/file_page_writeback.hpp>

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_FILE_PAGE_WRITEBACK_FIRST_GENERATION = 1ULL;
constexpr uint64_t OS_KERNEL_FILE_PAGE_WRITEBACK_COUNTER_INCREMENT = 1ULL;

[[nodiscard]] bool IdentitiesEqual(const FilePageIdentity &left,
                                   const FilePageIdentity &right) noexcept {
    return FileCacheIdentitiesEqual(left.file, right.file) && left.page_index == right.page_index;
}

}

FilePageWritebackStatus FilePageWritebackCoordinator::Initialize(
    FilePageWritebackSlot *const writeback_storage, const uint64_t writeback_capacity,
    FilePageWritebackWaiter *const waiter_storage, const uint64_t waiter_capacity) noexcept {
    if (this->initialized_) {
        return FilePageWritebackStatus::AlreadyInitialized;
    }
    if (writeback_storage == nullptr || waiter_storage == nullptr) {
        return FilePageWritebackStatus::InvalidStorage;
    }
    if (writeback_capacity == OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE ||
        waiter_capacity == OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE) {
        return FilePageWritebackStatus::InvalidCapacity;
    }
    for (uint64_t slot_index = OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE;
         slot_index < writeback_capacity; ++slot_index) {
        writeback_storage[slot_index] = FilePageWritebackSlot{};
    }
    for (uint64_t waiter_index = OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE;
         waiter_index < waiter_capacity; ++waiter_index) {
        waiter_storage[waiter_index] = FilePageWritebackWaiter{};
    }
    this->writeback_storage_ = writeback_storage;
    this->writeback_capacity_ = writeback_capacity;
    this->waiter_storage_ = waiter_storage;
    this->waiter_capacity_ = waiter_capacity;
    this->statistics_ = FilePageWritebackStatistics{};
    this->statistics_.writeback_capacity = writeback_capacity;
    this->statistics_.waiter_capacity = waiter_capacity;
    this->initialized_ = true;
    return FilePageWritebackStatus::Succeeded;
}

FilePageWritebackStatus FilePageWritebackCoordinator::Begin(
    const FilePageIdentity &identity, const uint64_t physical_address,
    const uint64_t writeback_generation, const uint64_t owner_thread_index,
    FilePageWritebackToken &token) noexcept {
    token = FilePageWritebackToken{};
    if (!this->initialized_ || this->writeback_storage_ == nullptr ||
        this->waiter_storage_ == nullptr) {
        return FilePageWritebackStatus::NotInitialized;
    }
    if (!FileCacheIdentityIsValid(identity.file) ||
        (physical_address &
         (OS_KERNEL_MEMORY_PAGE_SIZE_BYTES - OS_KERNEL_FILE_PAGE_WRITEBACK_COUNTER_INCREMENT)) !=
            OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE ||
        writeback_generation == OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE ||
        owner_thread_index >= this->waiter_capacity_) {
        return FilePageWritebackStatus::InvalidWriteback;
    }
    if (this->FindWritebackSlotIndex(identity, physical_address, writeback_generation) !=
            OS_KERNEL_FILE_PAGE_WRITEBACK_INVALID_SLOT_INDEX ||
        this->OwnerHasWritingWriteback(owner_thread_index)) {
        return FilePageWritebackStatus::WritebackAlreadyRegistered;
    }
    const uint64_t slot_index = this->FindFreeSlotIndex();
    if (slot_index == OS_KERNEL_FILE_PAGE_WRITEBACK_INVALID_SLOT_INDEX) {
        if (this->statistics_.capacity_rejection_count == UINT64_MAX) {
            return FilePageWritebackStatus::CounterOverflow;
        }
        ++this->statistics_.capacity_rejection_count;
        return FilePageWritebackStatus::CapacityExhausted;
    }
    FilePageWritebackSlot &slot = this->writeback_storage_[slot_index];
    if (slot.generation == UINT64_MAX || this->statistics_.active_writeback_count == UINT64_MAX ||
        this->statistics_.writing_writeback_count == UINT64_MAX ||
        this->statistics_.begin_count == UINT64_MAX) {
        return slot.generation == UINT64_MAX ? FilePageWritebackStatus::GenerationExhausted
                                             : FilePageWritebackStatus::CounterOverflow;
    }
    const uint64_t generation =
        slot.generation == OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE
            ? OS_KERNEL_FILE_PAGE_WRITEBACK_FIRST_GENERATION
            : slot.generation + OS_KERNEL_FILE_PAGE_WRITEBACK_COUNTER_INCREMENT;
    slot = FilePageWritebackSlot{
        .identity = identity,
        .physical_address = physical_address,
        .writeback_generation = writeback_generation,
        .owner_thread_index = owner_thread_index,
        .generation = generation,
        .registered_waiter_count = OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE,
        .waiting_waiter_count = OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE,
        .remaining_result_count = OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE,
        .result = FilePageCacheStatus::WritebackWaitUnavailable,
        .state = FilePageWritebackState::Writing,
    };
    token = FilePageWritebackToken{
        .slot_index = slot_index,
        .generation = generation,
    };
    ++this->statistics_.active_writeback_count;
    ++this->statistics_.writing_writeback_count;
    ++this->statistics_.begin_count;
    if (this->statistics_.active_writeback_count > this->statistics_.peak_active_writeback_count) {
        this->statistics_.peak_active_writeback_count = this->statistics_.active_writeback_count;
    }
    return FilePageWritebackStatus::Succeeded;
}

FilePageWritebackStatus FilePageWritebackCoordinator::RegisterWaiter(
    const FilePageIdentity &identity, const uint64_t physical_address,
    const uint64_t writeback_generation, const uint64_t waiter_thread_index,
    FilePageWritebackToken &token) noexcept {
    token = FilePageWritebackToken{};
    if (!this->initialized_ || this->writeback_storage_ == nullptr ||
        this->waiter_storage_ == nullptr) {
        return FilePageWritebackStatus::NotInitialized;
    }
    if (waiter_thread_index >= this->waiter_capacity_) {
        return FilePageWritebackStatus::InvalidThread;
    }
    FilePageWritebackWaiter &waiter = this->waiter_storage_[waiter_thread_index];
    if (waiter.state != FilePageWritebackWaiterState::Free) {
        return FilePageWritebackStatus::WaiterAlreadyRegistered;
    }
    const uint64_t slot_index =
        this->FindWritebackSlotIndex(identity, physical_address, writeback_generation);
    if (slot_index == OS_KERNEL_FILE_PAGE_WRITEBACK_INVALID_SLOT_INDEX) {
        return FilePageWritebackStatus::WritebackNotFound;
    }
    FilePageWritebackSlot &slot = this->writeback_storage_[slot_index];
    if (slot.state != FilePageWritebackState::Writing ||
        slot.owner_thread_index == waiter_thread_index) {
        return FilePageWritebackStatus::InvalidState;
    }
    if (slot.registered_waiter_count == UINT64_MAX ||
        this->statistics_.registered_waiter_count == UINT64_MAX ||
        this->statistics_.waiter_registration_count == UINT64_MAX) {
        return FilePageWritebackStatus::CounterOverflow;
    }
    waiter = FilePageWritebackWaiter{
        .slot_index = slot_index,
        .generation = slot.generation,
        .state = FilePageWritebackWaiterState::Registered,
    };
    token = FilePageWritebackToken{
        .slot_index = slot_index,
        .generation = slot.generation,
    };
    ++slot.registered_waiter_count;
    ++this->statistics_.registered_waiter_count;
    ++this->statistics_.waiter_registration_count;
    const uint64_t active_waiter_count = this->statistics_.registered_waiter_count +
                                         this->statistics_.waiting_waiter_count +
                                         this->statistics_.ready_waiter_count;
    if (active_waiter_count > this->statistics_.peak_waiter_count) {
        this->statistics_.peak_waiter_count = active_waiter_count;
    }
    return FilePageWritebackStatus::Succeeded;
}

FilePageWritebackStatus
FilePageWritebackCoordinator::PrepareWait(const FilePageWritebackToken token,
                                          const uint64_t waiter_thread_index,
                                          bool &wait_required) noexcept {
    wait_required = false;
    if (!this->TokenIsValid(token)) {
        return FilePageWritebackStatus::InvalidToken;
    }
    if (waiter_thread_index >= this->waiter_capacity_) {
        return FilePageWritebackStatus::InvalidThread;
    }
    FilePageWritebackWaiter &waiter = this->waiter_storage_[waiter_thread_index];
    FilePageWritebackSlot &slot = this->writeback_storage_[token.slot_index];
    if (waiter.slot_index != token.slot_index || waiter.generation != token.generation ||
        waiter.state != FilePageWritebackWaiterState::Registered) {
        return FilePageWritebackStatus::InvalidState;
    }
    if (slot.state == FilePageWritebackState::Completed) {
        if (this->statistics_.immediate_completion_count == UINT64_MAX ||
            this->statistics_.registered_waiter_count ==
                OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE ||
            this->statistics_.ready_waiter_count == UINT64_MAX) {
            return FilePageWritebackStatus::CounterOverflow;
        }
        waiter.state = FilePageWritebackWaiterState::Ready;
        --this->statistics_.registered_waiter_count;
        ++this->statistics_.ready_waiter_count;
        ++this->statistics_.immediate_completion_count;
        return FilePageWritebackStatus::Succeeded;
    }
    if (slot.state != FilePageWritebackState::Writing || slot.waiting_waiter_count == UINT64_MAX ||
        this->statistics_.waiting_waiter_count == UINT64_MAX ||
        this->statistics_.registered_waiter_count == OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE ||
        this->statistics_.wait_commit_count == UINT64_MAX) {
        return slot.state == FilePageWritebackState::Writing
                   ? FilePageWritebackStatus::CounterOverflow
                   : FilePageWritebackStatus::InvalidState;
    }
    waiter.state = FilePageWritebackWaiterState::Waiting;
    ++slot.waiting_waiter_count;
    --this->statistics_.registered_waiter_count;
    ++this->statistics_.waiting_waiter_count;
    ++this->statistics_.wait_commit_count;
    wait_required = true;
    return FilePageWritebackStatus::Succeeded;
}

FilePageWritebackStatus FilePageWritebackCoordinator::Complete(
    const FilePageWritebackToken token, const uint64_t owner_thread_index,
    const FilePageCacheStatus result, FilePageWritebackCompletionDecision &decision) noexcept {
    decision = FilePageWritebackCompletionDecision{};
    if (!this->TokenIsValid(token)) {
        return FilePageWritebackStatus::InvalidToken;
    }
    FilePageWritebackSlot &slot = this->writeback_storage_[token.slot_index];
    if (slot.state != FilePageWritebackState::Writing ||
        slot.owner_thread_index != owner_thread_index || !this->ResultIsTerminal(result)) {
        return FilePageWritebackStatus::InvalidState;
    }
    if (this->statistics_.writing_writeback_count == OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE ||
        this->statistics_.completion_count == UINT64_MAX ||
        this->statistics_.completed_writeback_count == UINT64_MAX ||
        this->statistics_.broadcast_wake_count > UINT64_MAX - slot.waiting_waiter_count ||
        (result != FilePageCacheStatus::Succeeded &&
         slot.registered_waiter_count != OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE &&
         this->statistics_.failure_broadcast_count == UINT64_MAX)) {
        return FilePageWritebackStatus::CounterOverflow;
    }
    decision.slot_index = token.slot_index;
    decision.wake_count = slot.waiting_waiter_count;
    slot.result = result;
    slot.remaining_result_count = slot.registered_waiter_count;
    slot.state = FilePageWritebackState::Completed;
    --this->statistics_.writing_writeback_count;
    ++this->statistics_.completion_count;
    ++this->statistics_.completed_writeback_count;
    this->statistics_.broadcast_wake_count += slot.waiting_waiter_count;
    if (slot.registered_waiter_count == OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE) {
        return this->ReleaseSlot(token.slot_index);
    }
    if (result != FilePageCacheStatus::Succeeded) {
        ++this->statistics_.failure_broadcast_count;
    }
    for (uint64_t waiter_index = OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE;
         waiter_index < this->waiter_capacity_; ++waiter_index) {
        FilePageWritebackWaiter &waiter = this->waiter_storage_[waiter_index];
        if (waiter.slot_index != token.slot_index || waiter.generation != token.generation) {
            continue;
        }
        if (waiter.state == FilePageWritebackWaiterState::Waiting) {
            if (this->statistics_.waiting_waiter_count ==
                    OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE ||
                this->statistics_.ready_waiter_count == UINT64_MAX) {
                return FilePageWritebackStatus::Corrupt;
            }
            waiter.state = FilePageWritebackWaiterState::Ready;
            --this->statistics_.waiting_waiter_count;
            ++this->statistics_.ready_waiter_count;
        } else if (waiter.state != FilePageWritebackWaiterState::Registered) {
            return FilePageWritebackStatus::Corrupt;
        }
    }
    slot.waiting_waiter_count = OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE;
    return FilePageWritebackStatus::Succeeded;
}

FilePageWritebackStatus
FilePageWritebackCoordinator::TakeResult(const FilePageWritebackToken token,
                                         const uint64_t waiter_thread_index,
                                         FilePageCacheStatus &result) noexcept {
    result = FilePageCacheStatus::WritebackWaitFailed;
    if (!this->TokenIsValid(token)) {
        return FilePageWritebackStatus::InvalidToken;
    }
    if (waiter_thread_index >= this->waiter_capacity_) {
        return FilePageWritebackStatus::InvalidThread;
    }
    FilePageWritebackWaiter &waiter = this->waiter_storage_[waiter_thread_index];
    FilePageWritebackSlot &slot = this->writeback_storage_[token.slot_index];
    if (slot.state != FilePageWritebackState::Completed || waiter.slot_index != token.slot_index ||
        waiter.generation != token.generation ||
        (waiter.state != FilePageWritebackWaiterState::Registered &&
         waiter.state != FilePageWritebackWaiterState::Ready) ||
        slot.registered_waiter_count == OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE ||
        slot.remaining_result_count == OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE ||
        this->statistics_.result_take_count == UINT64_MAX) {
        return FilePageWritebackStatus::InvalidState;
    }
    if (waiter.state == FilePageWritebackWaiterState::Registered) {
        if (this->statistics_.registered_waiter_count ==
            OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE) {
            return FilePageWritebackStatus::Corrupt;
        }
        --this->statistics_.registered_waiter_count;
    } else {
        if (this->statistics_.ready_waiter_count == OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE) {
            return FilePageWritebackStatus::Corrupt;
        }
        --this->statistics_.ready_waiter_count;
    }
    result = slot.result;
    waiter = FilePageWritebackWaiter{};
    --slot.registered_waiter_count;
    --slot.remaining_result_count;
    ++this->statistics_.result_take_count;
    return slot.remaining_result_count == OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE
               ? this->ReleaseSlot(token.slot_index)
               : FilePageWritebackStatus::Succeeded;
}

FilePageWritebackStatus FilePageWritebackCoordinator::Validate() const noexcept {
    if (!this->initialized_ || this->writeback_storage_ == nullptr ||
        this->waiter_storage_ == nullptr) {
        return FilePageWritebackStatus::NotInitialized;
    }
    uint64_t active_writeback_count = OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE;
    uint64_t writing_writeback_count = OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE;
    uint64_t completed_writeback_count = OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE;
    uint64_t registered_waiter_count = OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE;
    uint64_t waiting_waiter_count = OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE;
    uint64_t ready_waiter_count = OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE;
    for (uint64_t slot_index = OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE;
         slot_index < this->writeback_capacity_; ++slot_index) {
        const FilePageWritebackSlot &slot = this->writeback_storage_[slot_index];
        if (slot.state == FilePageWritebackState::Free) {
            if (FileCacheIdentityIsValid(slot.identity.file) ||
                slot.physical_address != OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE ||
                slot.writeback_generation != OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE ||
                slot.owner_thread_index != OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE ||
                slot.registered_waiter_count != OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE ||
                slot.waiting_waiter_count != OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE ||
                slot.remaining_result_count != OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE) {
                return FilePageWritebackStatus::Corrupt;
            }
            continue;
        }
        if (!FileCacheIdentityIsValid(slot.identity.file) ||
            slot.writeback_generation == OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE ||
            slot.owner_thread_index >= this->waiter_capacity_ ||
            slot.generation == OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE) {
            return FilePageWritebackStatus::Corrupt;
        }
        uint64_t slot_registered_waiter_count = OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE;
        uint64_t slot_waiting_waiter_count = OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE;
        uint64_t slot_ready_waiter_count = OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE;
        for (uint64_t waiter_index = OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE;
             waiter_index < this->waiter_capacity_; ++waiter_index) {
            const FilePageWritebackWaiter &waiter = this->waiter_storage_[waiter_index];
            if (waiter.slot_index != slot_index || waiter.generation != slot.generation) {
                continue;
            }
            if (waiter.state == FilePageWritebackWaiterState::Registered) {
                ++slot_registered_waiter_count;
            } else if (waiter.state == FilePageWritebackWaiterState::Waiting) {
                ++slot_waiting_waiter_count;
            } else if (waiter.state == FilePageWritebackWaiterState::Ready) {
                ++slot_ready_waiter_count;
            } else {
                return FilePageWritebackStatus::Corrupt;
            }
        }
        ++active_writeback_count;
        if (slot.state == FilePageWritebackState::Writing) {
            if (slot.remaining_result_count != OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE ||
                slot.registered_waiter_count !=
                    slot_registered_waiter_count + slot_waiting_waiter_count ||
                slot.waiting_waiter_count != slot_waiting_waiter_count ||
                slot_ready_waiter_count != OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE) {
                return FilePageWritebackStatus::Corrupt;
            }
            ++writing_writeback_count;
        } else if (slot.state == FilePageWritebackState::Completed) {
            if (!this->ResultIsTerminal(slot.result) ||
                slot.remaining_result_count == OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE ||
                slot.registered_waiter_count != slot.remaining_result_count ||
                slot.registered_waiter_count !=
                    slot_registered_waiter_count + slot_ready_waiter_count ||
                slot.waiting_waiter_count != OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE ||
                slot_waiting_waiter_count != OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE) {
                return FilePageWritebackStatus::Corrupt;
            }
            ++completed_writeback_count;
        } else {
            return FilePageWritebackStatus::Corrupt;
        }
    }
    for (uint64_t waiter_index = OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE;
         waiter_index < this->waiter_capacity_; ++waiter_index) {
        const FilePageWritebackWaiter &waiter = this->waiter_storage_[waiter_index];
        if (waiter.state == FilePageWritebackWaiterState::Free) {
            if (waiter.slot_index != OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE ||
                waiter.generation != OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE) {
                return FilePageWritebackStatus::Corrupt;
            }
            continue;
        }
        if (!this->TokenIsValid(FilePageWritebackToken{
                .slot_index = waiter.slot_index,
                .generation = waiter.generation,
            })) {
            return FilePageWritebackStatus::Corrupt;
        }
        if (waiter.state == FilePageWritebackWaiterState::Registered) {
            ++registered_waiter_count;
        } else if (waiter.state == FilePageWritebackWaiterState::Waiting) {
            ++waiting_waiter_count;
        } else if (waiter.state == FilePageWritebackWaiterState::Ready) {
            ++ready_waiter_count;
        } else {
            return FilePageWritebackStatus::Corrupt;
        }
    }
    const uint64_t active_waiter_count =
        registered_waiter_count + waiting_waiter_count + ready_waiter_count;
    return active_writeback_count == this->statistics_.active_writeback_count &&
                   writing_writeback_count == this->statistics_.writing_writeback_count &&
                   completed_writeback_count == this->statistics_.completed_writeback_count &&
                   registered_waiter_count == this->statistics_.registered_waiter_count &&
                   waiting_waiter_count == this->statistics_.waiting_waiter_count &&
                   ready_waiter_count == this->statistics_.ready_waiter_count &&
                   active_writeback_count == writing_writeback_count + completed_writeback_count &&
                   active_writeback_count <= this->writeback_capacity_ &&
                   active_waiter_count <= this->waiter_capacity_ &&
                   this->statistics_.peak_active_writeback_count >= active_writeback_count &&
                   this->statistics_.peak_waiter_count >= active_waiter_count &&
                   this->statistics_.begin_count ==
                       this->statistics_.completion_count + writing_writeback_count &&
                   this->statistics_.waiter_registration_count ==
                       this->statistics_.result_take_count + active_waiter_count &&
                   this->statistics_.wait_commit_count ==
                       this->statistics_.broadcast_wake_count + waiting_waiter_count
               ? FilePageWritebackStatus::Succeeded
               : FilePageWritebackStatus::Corrupt;
}

FilePageWritebackStatistics FilePageWritebackCoordinator::Statistics() const noexcept {
    return this->statistics_;
}

bool FilePageWritebackCoordinator::TokenIsValid(const FilePageWritebackToken token) const noexcept {
    return this->initialized_ && token.slot_index < this->writeback_capacity_ &&
           token.generation != OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE &&
           this->writeback_storage_[token.slot_index].state != FilePageWritebackState::Free &&
           this->writeback_storage_[token.slot_index].generation == token.generation;
}

bool FilePageWritebackCoordinator::ResultIsTerminal(
    const FilePageCacheStatus result) const noexcept {
    return result == FilePageCacheStatus::Succeeded ||
           result == FilePageCacheStatus::SourceWriteFailed ||
           result == FilePageCacheStatus::FrameAccessFailed ||
           result == FilePageCacheStatus::Corrupt;
}

uint64_t FilePageWritebackCoordinator::FindFreeSlotIndex() const noexcept {
    for (uint64_t slot_index = OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE;
         slot_index < this->writeback_capacity_; ++slot_index) {
        if (this->writeback_storage_[slot_index].state == FilePageWritebackState::Free) {
            return slot_index;
        }
    }
    return OS_KERNEL_FILE_PAGE_WRITEBACK_INVALID_SLOT_INDEX;
}

uint64_t FilePageWritebackCoordinator::FindWritebackSlotIndex(
    const FilePageIdentity &identity, const uint64_t physical_address,
    const uint64_t writeback_generation) const noexcept {
    for (uint64_t slot_index = OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE;
         slot_index < this->writeback_capacity_; ++slot_index) {
        const FilePageWritebackSlot &slot = this->writeback_storage_[slot_index];
        if (slot.state != FilePageWritebackState::Free &&
            IdentitiesEqual(slot.identity, identity) && slot.physical_address == physical_address &&
            slot.writeback_generation == writeback_generation) {
            return slot_index;
        }
    }
    return OS_KERNEL_FILE_PAGE_WRITEBACK_INVALID_SLOT_INDEX;
}

bool FilePageWritebackCoordinator::OwnerHasWritingWriteback(
    const uint64_t owner_thread_index) const noexcept {
    for (uint64_t slot_index = OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE;
         slot_index < this->writeback_capacity_; ++slot_index) {
        const FilePageWritebackSlot &slot = this->writeback_storage_[slot_index];
        if (slot.state == FilePageWritebackState::Writing &&
            slot.owner_thread_index == owner_thread_index) {
            return true;
        }
    }
    return false;
}

FilePageWritebackStatus
FilePageWritebackCoordinator::ReleaseSlot(const uint64_t slot_index) noexcept {
    if (slot_index >= this->writeback_capacity_ ||
        this->statistics_.active_writeback_count == OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE) {
        return FilePageWritebackStatus::Corrupt;
    }
    FilePageWritebackSlot &slot = this->writeback_storage_[slot_index];
    if (slot.state != FilePageWritebackState::Completed ||
        slot.remaining_result_count != OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE ||
        slot.registered_waiter_count != OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE ||
        this->statistics_.completed_writeback_count == OS_KERNEL_FILE_PAGE_WRITEBACK_EMPTY_VALUE) {
        return FilePageWritebackStatus::InvalidState;
    }
    const uint64_t generation = slot.generation;
    slot = FilePageWritebackSlot{};
    slot.generation = generation;
    --this->statistics_.active_writeback_count;
    --this->statistics_.completed_writeback_count;
    return FilePageWritebackStatus::Succeeded;
}

}

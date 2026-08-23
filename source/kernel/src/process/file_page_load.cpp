#include <os/kernel/process/file_page_load.hpp>

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_FILE_PAGE_LOAD_FIRST_GENERATION = 1ULL;
constexpr uint64_t OS_KERNEL_FILE_PAGE_LOAD_COUNTER_INCREMENT = 1ULL;

[[nodiscard]] bool IdentitiesEqual(const FilePageIdentity &left,
                                   const FilePageIdentity &right) noexcept {
    return FileCacheIdentitiesEqual(left.file, right.file) && left.page_index == right.page_index;
}

}

FilePageLoadStatus FilePageLoadCoordinator::Initialize(FilePageLoadSlot *const load_storage,
                                                       const uint64_t load_capacity,
                                                       FilePageLoadWaiter *const waiter_storage,
                                                       const uint64_t waiter_capacity) noexcept {
    if (this->initialized_) {
        return FilePageLoadStatus::AlreadyInitialized;
    }
    if (load_storage == nullptr || waiter_storage == nullptr) {
        return FilePageLoadStatus::InvalidStorage;
    }
    if (load_capacity == OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE ||
        waiter_capacity == OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE) {
        return FilePageLoadStatus::InvalidCapacity;
    }
    for (uint64_t slot_index = OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE; slot_index < load_capacity;
         ++slot_index) {
        load_storage[slot_index] = FilePageLoadSlot{};
    }
    for (uint64_t waiter_index = OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE;
         waiter_index < waiter_capacity; ++waiter_index) {
        waiter_storage[waiter_index] = FilePageLoadWaiter{};
    }
    this->load_storage_ = load_storage;
    this->load_capacity_ = load_capacity;
    this->waiter_storage_ = waiter_storage;
    this->waiter_capacity_ = waiter_capacity;
    this->statistics_ = FilePageLoadStatistics{};
    this->statistics_.load_capacity = load_capacity;
    this->statistics_.waiter_capacity = waiter_capacity;
    this->initialized_ = true;
    return FilePageLoadStatus::Succeeded;
}

FilePageLoadStatus FilePageLoadCoordinator::Begin(const FilePageIdentity &identity,
                                                  const uint64_t physical_address,
                                                  const uint64_t load_generation,
                                                  const uint64_t owner_thread_index,
                                                  FilePageLoadToken &token) noexcept {
    token = FilePageLoadToken{};
    if (!this->initialized_ || this->load_storage_ == nullptr || this->waiter_storage_ == nullptr) {
        return FilePageLoadStatus::NotInitialized;
    }
    if (!FileCacheIdentityIsValid(identity.file) ||
        (physical_address &
         (OS_KERNEL_MEMORY_PAGE_SIZE_BYTES - OS_KERNEL_FILE_PAGE_LOAD_COUNTER_INCREMENT)) !=
            OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE ||
        load_generation == OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE ||
        owner_thread_index >= this->waiter_capacity_) {
        return FilePageLoadStatus::InvalidLoad;
    }
    if (this->FindLoadSlotIndex(identity, physical_address, load_generation) !=
            OS_KERNEL_FILE_PAGE_LOAD_INVALID_SLOT_INDEX ||
        this->OwnerHasActiveLoad(owner_thread_index)) {
        return FilePageLoadStatus::LoadAlreadyRegistered;
    }
    const uint64_t slot_index = this->FindFreeSlotIndex();
    if (slot_index == OS_KERNEL_FILE_PAGE_LOAD_INVALID_SLOT_INDEX) {
        if (this->statistics_.capacity_rejection_count == UINT64_MAX) {
            return FilePageLoadStatus::CounterOverflow;
        }
        ++this->statistics_.capacity_rejection_count;
        return FilePageLoadStatus::CapacityExhausted;
    }
    FilePageLoadSlot &slot = this->load_storage_[slot_index];
    if (slot.generation == UINT64_MAX) {
        return FilePageLoadStatus::GenerationExhausted;
    }
    const uint64_t generation = slot.generation == OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE
                                    ? OS_KERNEL_FILE_PAGE_LOAD_FIRST_GENERATION
                                    : slot.generation + OS_KERNEL_FILE_PAGE_LOAD_COUNTER_INCREMENT;
    if (this->statistics_.active_load_count == UINT64_MAX ||
        this->statistics_.loading_load_count == UINT64_MAX ||
        this->statistics_.begin_count == UINT64_MAX) {
        return FilePageLoadStatus::CounterOverflow;
    }
    slot = FilePageLoadSlot{
        .identity = identity,
        .physical_address = physical_address,
        .load_generation = load_generation,
        .owner_thread_index = owner_thread_index,
        .generation = generation,
        .registered_waiter_count = OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE,
        .waiting_waiter_count = OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE,
        .remaining_result_count = OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE,
        .result = FilePageCacheStatus::LoadingWaitUnavailable,
        .state = FilePageLoadState::Loading,
    };
    token = FilePageLoadToken{
        .slot_index = slot_index,
        .generation = generation,
    };
    ++this->statistics_.active_load_count;
    ++this->statistics_.loading_load_count;
    ++this->statistics_.begin_count;
    if (this->statistics_.active_load_count > this->statistics_.peak_active_load_count) {
        this->statistics_.peak_active_load_count = this->statistics_.active_load_count;
    }
    return FilePageLoadStatus::Succeeded;
}

FilePageLoadStatus FilePageLoadCoordinator::RegisterWaiter(const FilePageIdentity &identity,
                                                           const uint64_t physical_address,
                                                           const uint64_t load_generation,
                                                           const uint64_t waiter_thread_index,
                                                           FilePageLoadToken &token) noexcept {
    token = FilePageLoadToken{};
    if (!this->initialized_ || this->load_storage_ == nullptr || this->waiter_storage_ == nullptr) {
        return FilePageLoadStatus::NotInitialized;
    }
    if (waiter_thread_index >= this->waiter_capacity_) {
        return FilePageLoadStatus::InvalidThread;
    }
    FilePageLoadWaiter &waiter = this->waiter_storage_[waiter_thread_index];
    if (waiter.state != FilePageLoadWaiterState::Free) {
        return FilePageLoadStatus::WaiterAlreadyRegistered;
    }
    const uint64_t slot_index =
        this->FindLoadSlotIndex(identity, physical_address, load_generation);
    if (slot_index == OS_KERNEL_FILE_PAGE_LOAD_INVALID_SLOT_INDEX) {
        return FilePageLoadStatus::LoadNotFound;
    }
    FilePageLoadSlot &slot = this->load_storage_[slot_index];
    if (slot.state != FilePageLoadState::Loading ||
        slot.owner_thread_index == waiter_thread_index ||
        slot.registered_waiter_count == UINT64_MAX ||
        this->statistics_.registered_waiter_count == UINT64_MAX ||
        this->statistics_.waiter_registration_count == UINT64_MAX) {
        return slot.state == FilePageLoadState::Loading ? FilePageLoadStatus::CounterOverflow
                                                        : FilePageLoadStatus::InvalidState;
    }
    waiter = FilePageLoadWaiter{
        .slot_index = slot_index,
        .generation = slot.generation,
        .state = FilePageLoadWaiterState::Registered,
    };
    token = FilePageLoadToken{
        .slot_index = slot_index,
        .generation = slot.generation,
    };
    ++slot.registered_waiter_count;
    ++this->statistics_.registered_waiter_count;
    ++this->statistics_.waiter_registration_count;
    if (this->statistics_.registered_waiter_count > this->statistics_.peak_waiter_count) {
        this->statistics_.peak_waiter_count = this->statistics_.registered_waiter_count;
    }
    return FilePageLoadStatus::Succeeded;
}

FilePageLoadStatus FilePageLoadCoordinator::PrepareWait(const FilePageLoadToken token,
                                                        const uint64_t waiter_thread_index,
                                                        bool &wait_required) noexcept {
    wait_required = false;
    if (!this->TokenIsValid(token)) {
        return FilePageLoadStatus::InvalidToken;
    }
    if (waiter_thread_index >= this->waiter_capacity_) {
        return FilePageLoadStatus::InvalidThread;
    }
    FilePageLoadWaiter &waiter = this->waiter_storage_[waiter_thread_index];
    FilePageLoadSlot &slot = this->load_storage_[token.slot_index];
    if (waiter.slot_index != token.slot_index || waiter.generation != token.generation ||
        waiter.state != FilePageLoadWaiterState::Registered) {
        return FilePageLoadStatus::InvalidState;
    }
    if (slot.state == FilePageLoadState::Completed) {
        if (this->statistics_.immediate_completion_count == UINT64_MAX ||
            this->statistics_.registered_waiter_count == OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE ||
            this->statistics_.ready_waiter_count == UINT64_MAX) {
            return FilePageLoadStatus::CounterOverflow;
        }
        waiter.state = FilePageLoadWaiterState::Ready;
        --this->statistics_.registered_waiter_count;
        ++this->statistics_.ready_waiter_count;
        ++this->statistics_.immediate_completion_count;
        return FilePageLoadStatus::Succeeded;
    }
    if (slot.state != FilePageLoadState::Loading || slot.waiting_waiter_count == UINT64_MAX ||
        this->statistics_.waiting_waiter_count == UINT64_MAX ||
        this->statistics_.registered_waiter_count == OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE ||
        this->statistics_.wait_commit_count == UINT64_MAX) {
        return slot.state == FilePageLoadState::Loading ? FilePageLoadStatus::CounterOverflow
                                                        : FilePageLoadStatus::InvalidState;
    }
    waiter.state = FilePageLoadWaiterState::Waiting;
    ++slot.waiting_waiter_count;
    --this->statistics_.registered_waiter_count;
    ++this->statistics_.waiting_waiter_count;
    ++this->statistics_.wait_commit_count;
    wait_required = true;
    return FilePageLoadStatus::Succeeded;
}

FilePageLoadStatus
FilePageLoadCoordinator::RegisteredWaiterCount(const FilePageLoadToken token,
                                               const uint64_t owner_thread_index,
                                               uint64_t &waiter_count) const noexcept {
    waiter_count = OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE;
    if (!this->TokenIsValid(token)) {
        return FilePageLoadStatus::InvalidToken;
    }
    const FilePageLoadSlot &slot = this->load_storage_[token.slot_index];
    if (slot.state != FilePageLoadState::Loading || slot.owner_thread_index != owner_thread_index) {
        return FilePageLoadStatus::InvalidState;
    }
    waiter_count = slot.registered_waiter_count;
    return FilePageLoadStatus::Succeeded;
}

FilePageLoadStatus
FilePageLoadCoordinator::Complete(const FilePageLoadToken token, const uint64_t owner_thread_index,
                                  const FilePageCacheStatus result,
                                  FilePageLoadCompletionDecision &decision) noexcept {
    decision = FilePageLoadCompletionDecision{};
    if (!this->TokenIsValid(token)) {
        return FilePageLoadStatus::InvalidToken;
    }
    FilePageLoadSlot &slot = this->load_storage_[token.slot_index];
    if (slot.state != FilePageLoadState::Loading || slot.owner_thread_index != owner_thread_index ||
        !this->ResultIsTerminal(result)) {
        return FilePageLoadStatus::InvalidState;
    }
    if (this->statistics_.loading_load_count == OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE ||
        this->statistics_.completion_count == UINT64_MAX ||
        this->statistics_.completed_load_count == UINT64_MAX ||
        this->statistics_.broadcast_wake_count > UINT64_MAX - slot.waiting_waiter_count ||
        (result != FilePageCacheStatus::Succeeded &&
         slot.registered_waiter_count != OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE &&
         this->statistics_.failure_broadcast_count == UINT64_MAX)) {
        return FilePageLoadStatus::CounterOverflow;
    }
    decision.slot_index = token.slot_index;
    decision.wake_count = slot.waiting_waiter_count;
    slot.result = result;
    slot.remaining_result_count = slot.registered_waiter_count;
    slot.state = FilePageLoadState::Completed;
    --this->statistics_.loading_load_count;
    ++this->statistics_.completion_count;
    ++this->statistics_.completed_load_count;
    this->statistics_.broadcast_wake_count += slot.waiting_waiter_count;
    if (slot.registered_waiter_count == OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE) {
        return this->ReleaseSlot(token.slot_index);
    }
    if (result != FilePageCacheStatus::Succeeded) {
        ++this->statistics_.failure_broadcast_count;
    }
    for (uint64_t waiter_index = OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE;
         waiter_index < this->waiter_capacity_; ++waiter_index) {
        FilePageLoadWaiter &waiter = this->waiter_storage_[waiter_index];
        if (waiter.slot_index != token.slot_index || waiter.generation != token.generation) {
            continue;
        }
        if (waiter.state == FilePageLoadWaiterState::Waiting) {
            if (this->statistics_.waiting_waiter_count == OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE ||
                this->statistics_.ready_waiter_count == UINT64_MAX) {
                return FilePageLoadStatus::Corrupt;
            }
            waiter.state = FilePageLoadWaiterState::Ready;
            --this->statistics_.waiting_waiter_count;
            ++this->statistics_.ready_waiter_count;
        } else if (waiter.state != FilePageLoadWaiterState::Registered) {
            return FilePageLoadStatus::Corrupt;
        }
    }
    slot.waiting_waiter_count = OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE;
    return FilePageLoadStatus::Succeeded;
}

FilePageLoadStatus FilePageLoadCoordinator::TakeResult(const FilePageLoadToken token,
                                                       const uint64_t waiter_thread_index,
                                                       FilePageCacheStatus &result) noexcept {
    result = FilePageCacheStatus::LoadingWaitFailed;
    if (!this->TokenIsValid(token)) {
        return FilePageLoadStatus::InvalidToken;
    }
    if (waiter_thread_index >= this->waiter_capacity_) {
        return FilePageLoadStatus::InvalidThread;
    }
    FilePageLoadWaiter &waiter = this->waiter_storage_[waiter_thread_index];
    FilePageLoadSlot &slot = this->load_storage_[token.slot_index];
    if (slot.state != FilePageLoadState::Completed || waiter.slot_index != token.slot_index ||
        waiter.generation != token.generation || waiter.state != FilePageLoadWaiterState::Ready ||
        slot.remaining_result_count == OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE ||
        this->statistics_.ready_waiter_count == OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE ||
        this->statistics_.result_take_count == UINT64_MAX) {
        return FilePageLoadStatus::InvalidState;
    }
    result = slot.result;
    waiter = FilePageLoadWaiter{};
    --slot.registered_waiter_count;
    --slot.remaining_result_count;
    --this->statistics_.ready_waiter_count;
    ++this->statistics_.result_take_count;
    return slot.remaining_result_count == OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE
               ? this->ReleaseSlot(token.slot_index)
               : FilePageLoadStatus::Succeeded;
}

FilePageLoadStatus FilePageLoadCoordinator::Validate() const noexcept {
    if (!this->initialized_ || this->load_storage_ == nullptr || this->waiter_storage_ == nullptr) {
        return FilePageLoadStatus::NotInitialized;
    }
    uint64_t active_load_count = OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE;
    uint64_t loading_load_count = OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE;
    uint64_t completed_load_count = OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE;
    uint64_t registered_waiter_count = OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE;
    uint64_t waiting_waiter_count = OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE;
    uint64_t ready_waiter_count = OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE;
    for (uint64_t slot_index = OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE;
         slot_index < this->load_capacity_; ++slot_index) {
        const FilePageLoadSlot &slot = this->load_storage_[slot_index];
        if (slot.state == FilePageLoadState::Free) {
            if (slot.physical_address != OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE ||
                slot.load_generation != OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE ||
                slot.owner_thread_index != OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE ||
                slot.registered_waiter_count != OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE ||
                slot.waiting_waiter_count != OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE ||
                slot.remaining_result_count != OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE) {
                return FilePageLoadStatus::Corrupt;
            }
            continue;
        }
        if (!FileCacheIdentityIsValid(slot.identity.file) ||
            slot.load_generation == OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE ||
            slot.generation == OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE ||
            slot.owner_thread_index >= this->waiter_capacity_) {
            return FilePageLoadStatus::Corrupt;
        }
        uint64_t slot_registered_waiter_count = OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE;
        uint64_t slot_waiting_waiter_count = OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE;
        uint64_t slot_ready_waiter_count = OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE;
        for (uint64_t waiter_index = OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE;
             waiter_index < this->waiter_capacity_; ++waiter_index) {
            const FilePageLoadWaiter &waiter = this->waiter_storage_[waiter_index];
            if (waiter.slot_index != slot_index || waiter.generation != slot.generation) {
                continue;
            }
            if (waiter.state == FilePageLoadWaiterState::Registered) {
                ++slot_registered_waiter_count;
            } else if (waiter.state == FilePageLoadWaiterState::Waiting) {
                ++slot_waiting_waiter_count;
            } else if (waiter.state == FilePageLoadWaiterState::Ready) {
                ++slot_ready_waiter_count;
            } else {
                return FilePageLoadStatus::Corrupt;
            }
        }
        ++active_load_count;
        if (slot.state == FilePageLoadState::Loading) {
            if (slot.remaining_result_count != OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE ||
                slot.registered_waiter_count !=
                    slot_registered_waiter_count + slot_waiting_waiter_count ||
                slot.waiting_waiter_count != slot_waiting_waiter_count ||
                slot_ready_waiter_count != OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE) {
                return FilePageLoadStatus::Corrupt;
            }
            ++loading_load_count;
        } else if (slot.state == FilePageLoadState::Completed) {
            if (!this->ResultIsTerminal(slot.result) ||
                slot.remaining_result_count == OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE ||
                slot.registered_waiter_count != slot.remaining_result_count ||
                slot.registered_waiter_count !=
                    slot_registered_waiter_count + slot_ready_waiter_count ||
                slot.waiting_waiter_count != OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE ||
                slot_waiting_waiter_count != OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE) {
                return FilePageLoadStatus::Corrupt;
            }
            ++completed_load_count;
        } else {
            return FilePageLoadStatus::Corrupt;
        }
    }
    for (uint64_t waiter_index = OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE;
         waiter_index < this->waiter_capacity_; ++waiter_index) {
        const FilePageLoadWaiter &waiter = this->waiter_storage_[waiter_index];
        if (waiter.state == FilePageLoadWaiterState::Free) {
            if (waiter.slot_index != OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE ||
                waiter.generation != OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE) {
                return FilePageLoadStatus::Corrupt;
            }
            continue;
        }
        const FilePageLoadToken token{
            .slot_index = waiter.slot_index,
            .generation = waiter.generation,
        };
        if (!this->TokenIsValid(token)) {
            return FilePageLoadStatus::Corrupt;
        }
        if (waiter.state == FilePageLoadWaiterState::Registered) {
            ++registered_waiter_count;
        } else if (waiter.state == FilePageLoadWaiterState::Waiting) {
            ++waiting_waiter_count;
        } else if (waiter.state == FilePageLoadWaiterState::Ready) {
            ++ready_waiter_count;
        } else {
            return FilePageLoadStatus::Corrupt;
        }
    }
    if (active_load_count != this->statistics_.active_load_count ||
        loading_load_count != this->statistics_.loading_load_count ||
        completed_load_count != this->statistics_.completed_load_count ||
        registered_waiter_count != this->statistics_.registered_waiter_count ||
        waiting_waiter_count != this->statistics_.waiting_waiter_count ||
        ready_waiter_count != this->statistics_.ready_waiter_count ||
        active_load_count > this->load_capacity_ ||
        registered_waiter_count + waiting_waiter_count + ready_waiter_count >
            this->waiter_capacity_) {
        return FilePageLoadStatus::Corrupt;
    }
    return FilePageLoadStatus::Succeeded;
}

FilePageLoadStatistics FilePageLoadCoordinator::Statistics() const noexcept {
    return this->statistics_;
}

bool FilePageLoadCoordinator::TokenIsValid(const FilePageLoadToken token) const noexcept {
    return this->initialized_ && token.slot_index < this->load_capacity_ &&
           token.generation != OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE &&
           this->load_storage_[token.slot_index].state != FilePageLoadState::Free &&
           this->load_storage_[token.slot_index].generation == token.generation;
}

bool FilePageLoadCoordinator::ResultIsTerminal(const FilePageCacheStatus result) const noexcept {
    return result == FilePageCacheStatus::Succeeded ||
           result == FilePageCacheStatus::FrameAccessFailed ||
           result == FilePageCacheStatus::SourceReadFailed ||
           result == FilePageCacheStatus::FrameReleaseFailed ||
           result == FilePageCacheStatus::MetadataReleaseFailed ||
           result == FilePageCacheStatus::Corrupt;
}

uint64_t FilePageLoadCoordinator::FindFreeSlotIndex() const noexcept {
    for (uint64_t slot_index = OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE;
         slot_index < this->load_capacity_; ++slot_index) {
        if (this->load_storage_[slot_index].state == FilePageLoadState::Free) {
            return slot_index;
        }
    }
    return OS_KERNEL_FILE_PAGE_LOAD_INVALID_SLOT_INDEX;
}

uint64_t FilePageLoadCoordinator::FindLoadSlotIndex(const FilePageIdentity &identity,
                                                    const uint64_t physical_address,
                                                    const uint64_t load_generation) const noexcept {
    for (uint64_t slot_index = OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE;
         slot_index < this->load_capacity_; ++slot_index) {
        const FilePageLoadSlot &slot = this->load_storage_[slot_index];
        if (slot.state != FilePageLoadState::Free && IdentitiesEqual(slot.identity, identity) &&
            slot.physical_address == physical_address && slot.load_generation == load_generation) {
            return slot_index;
        }
    }
    return OS_KERNEL_FILE_PAGE_LOAD_INVALID_SLOT_INDEX;
}

bool FilePageLoadCoordinator::OwnerHasActiveLoad(const uint64_t owner_thread_index) const noexcept {
    for (uint64_t slot_index = OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE;
         slot_index < this->load_capacity_; ++slot_index) {
        if (this->load_storage_[slot_index].state != FilePageLoadState::Free &&
            this->load_storage_[slot_index].owner_thread_index == owner_thread_index) {
            return true;
        }
    }
    return false;
}

FilePageLoadStatus FilePageLoadCoordinator::ReleaseSlot(const uint64_t slot_index) noexcept {
    if (slot_index >= this->load_capacity_ ||
        this->statistics_.active_load_count == OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE) {
        return FilePageLoadStatus::Corrupt;
    }
    FilePageLoadSlot &slot = this->load_storage_[slot_index];
    if (slot.state == FilePageLoadState::Completed) {
        if (slot.remaining_result_count != OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE ||
            slot.registered_waiter_count != OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE ||
            this->statistics_.completed_load_count == OS_KERNEL_FILE_PAGE_LOAD_EMPTY_VALUE) {
            return FilePageLoadStatus::Corrupt;
        }
        --this->statistics_.completed_load_count;
    } else {
        return FilePageLoadStatus::InvalidState;
    }
    const uint64_t generation = slot.generation;
    slot = FilePageLoadSlot{};
    slot.generation = generation;
    --this->statistics_.active_load_count;
    return FilePageLoadStatus::Succeeded;
}

}

#include <os/kernel/process/block_io.hpp>

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_BLOCK_IO_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_BLOCK_IO_COUNTER_INCREMENT = 1ULL;

}

BlockIoStatus BlockIoCoordinator::Initialize(BlockIoSlot *const storage,
                                             const uint64_t capacity) noexcept {
    if (this->initialized_) {
        return BlockIoStatus::AlreadyInitialized;
    }
    if (storage == nullptr) {
        return BlockIoStatus::InvalidStorage;
    }
    if (capacity == OS_KERNEL_BLOCK_IO_EMPTY_VALUE) {
        return BlockIoStatus::InvalidCapacity;
    }
    for (uint64_t slot_index = OS_KERNEL_BLOCK_IO_EMPTY_VALUE; slot_index < capacity;
         ++slot_index) {
        storage[slot_index] = BlockIoSlot{};
    }
    this->storage_ = storage;
    this->capacity_ = capacity;
    this->statistics_ = BlockIoStatistics{};
    this->statistics_.capacity = capacity;
    this->initialized_ = true;
    return BlockIoStatus::Succeeded;
}

BlockIoStatus BlockIoCoordinator::Register(const uint64_t request_identifier,
                                           const uint64_t owner_thread_index,
                                           BlockIoTicket &ticket) noexcept {
    ticket = BlockIoTicket{};
    if (!this->initialized_ || this->storage_ == nullptr) {
        return BlockIoStatus::NotInitialized;
    }
    if (request_identifier == OS_KERNEL_BLOCK_IO_EMPTY_VALUE ||
        owner_thread_index == OS_KERNEL_BLOCK_REQUEST_KERNEL_OWNER_THREAD_INDEX) {
        return BlockIoStatus::InvalidRequest;
    }
    if (this->FindRequestSlotIndex(owner_thread_index, request_identifier) !=
            OS_KERNEL_BLOCK_IO_INVALID_SLOT_INDEX ||
        this->OwnerHasActiveRequest(owner_thread_index)) {
        return BlockIoStatus::RequestAlreadyRegistered;
    }
    const uint64_t slot_index = this->FindFreeSlotIndex();
    if (slot_index == OS_KERNEL_BLOCK_IO_INVALID_SLOT_INDEX) {
        if (this->statistics_.capacity_rejection_count == UINT64_MAX) {
            return BlockIoStatus::CounterOverflow;
        }
        ++this->statistics_.capacity_rejection_count;
        return BlockIoStatus::CapacityExhausted;
    }
    BlockIoSlot &slot = this->storage_[slot_index];
    if (slot.generation == UINT64_MAX) {
        return BlockIoStatus::GenerationExhausted;
    }
    const uint64_t generation = slot.generation == OS_KERNEL_BLOCK_IO_EMPTY_VALUE
                                    ? OS_KERNEL_BLOCK_IO_FIRST_GENERATION
                                    : slot.generation + OS_KERNEL_BLOCK_IO_COUNTER_INCREMENT;
    if (this->statistics_.active_request_count == UINT64_MAX ||
        this->statistics_.registered_request_count == UINT64_MAX ||
        this->statistics_.registration_count == UINT64_MAX) {
        return BlockIoStatus::CounterOverflow;
    }
    slot = BlockIoSlot{
        .request_identifier = request_identifier,
        .owner_thread_index = owner_thread_index,
        .generation = generation,
        .result = BlockRequestResult::None,
        .state = BlockIoState::Registered,
    };
    ticket = BlockIoTicket{
        .slot_index = slot_index,
        .generation = generation,
    };
    ++this->statistics_.active_request_count;
    ++this->statistics_.registered_request_count;
    ++this->statistics_.registration_count;
    if (this->statistics_.active_request_count > this->statistics_.peak_active_request_count) {
        this->statistics_.peak_active_request_count = this->statistics_.active_request_count;
    }
    return BlockIoStatus::Succeeded;
}

BlockIoStatus BlockIoCoordinator::PrepareWait(const BlockIoTicket ticket,
                                              bool &wait_required) noexcept {
    wait_required = false;
    if (!this->initialized_ || this->storage_ == nullptr) {
        return BlockIoStatus::NotInitialized;
    }
    if (!this->TicketIsValid(ticket)) {
        return BlockIoStatus::InvalidTicket;
    }
    BlockIoSlot &slot = this->storage_[ticket.slot_index];
    if (slot.state == BlockIoState::Completed) {
        if (this->statistics_.immediate_completion_count == UINT64_MAX) {
            return BlockIoStatus::CounterOverflow;
        }
        ++this->statistics_.immediate_completion_count;
        return BlockIoStatus::Succeeded;
    }
    if (slot.state != BlockIoState::Registered ||
        this->statistics_.registered_request_count == OS_KERNEL_BLOCK_IO_EMPTY_VALUE ||
        this->statistics_.waiting_request_count == UINT64_MAX ||
        this->statistics_.wait_commit_count == UINT64_MAX) {
        return slot.state == BlockIoState::Registered ? BlockIoStatus::CounterOverflow
                                                      : BlockIoStatus::InvalidState;
    }
    slot.state = BlockIoState::Waiting;
    --this->statistics_.registered_request_count;
    ++this->statistics_.waiting_request_count;
    ++this->statistics_.wait_commit_count;
    wait_required = true;
    return BlockIoStatus::Succeeded;
}

BlockIoStatus BlockIoCoordinator::Complete(const uint64_t owner_thread_index,
                                           const uint64_t request_identifier,
                                           const BlockRequestResult result,
                                           BlockIoCompletionDecision &decision) noexcept {
    decision = BlockIoCompletionDecision{};
    if (!this->initialized_ || this->storage_ == nullptr) {
        return BlockIoStatus::NotInitialized;
    }
    if (!this->ResultIsTerminal(result)) {
        return BlockIoStatus::InvalidRequest;
    }
    const uint64_t slot_index = this->FindRequestSlotIndex(owner_thread_index, request_identifier);
    if (slot_index == OS_KERNEL_BLOCK_IO_INVALID_SLOT_INDEX) {
        return BlockIoStatus::RequestNotFound;
    }
    BlockIoSlot &slot = this->storage_[slot_index];
    if (slot.state == BlockIoState::Completed) {
        return BlockIoStatus::RequestAlreadyResolved;
    }
    if (slot.state == BlockIoState::Abandoned) {
        if (this->statistics_.late_completion_count == UINT64_MAX) {
            return BlockIoStatus::CounterOverflow;
        }
        decision.owner_thread_index = owner_thread_index;
        decision.abandoned = true;
        ++this->statistics_.late_completion_count;
        return this->ReleaseSlot(slot_index);
    }
    if (slot.state != BlockIoState::Registered && slot.state != BlockIoState::Waiting) {
        return BlockIoStatus::InvalidState;
    }
    if (this->statistics_.completion_count == UINT64_MAX ||
        this->statistics_.completed_request_count == UINT64_MAX ||
        (slot.state == BlockIoState::Waiting &&
         this->statistics_.wake_required_count == UINT64_MAX)) {
        return BlockIoStatus::CounterOverflow;
    }
    decision.owner_thread_index = owner_thread_index;
    decision.wake_required = slot.state == BlockIoState::Waiting;
    if (slot.state == BlockIoState::Waiting) {
        if (this->statistics_.waiting_request_count == OS_KERNEL_BLOCK_IO_EMPTY_VALUE) {
            return BlockIoStatus::Corrupt;
        }
        --this->statistics_.waiting_request_count;
        ++this->statistics_.wake_required_count;
    } else {
        if (this->statistics_.registered_request_count == OS_KERNEL_BLOCK_IO_EMPTY_VALUE) {
            return BlockIoStatus::Corrupt;
        }
        --this->statistics_.registered_request_count;
    }
    slot.result = result;
    slot.state = BlockIoState::Completed;
    ++this->statistics_.completed_request_count;
    ++this->statistics_.completion_count;
    return BlockIoStatus::Succeeded;
}

BlockIoStatus BlockIoCoordinator::TakeResult(const BlockIoTicket ticket,
                                             BlockRequestResult &result) noexcept {
    result = BlockRequestResult::None;
    if (!this->initialized_ || this->storage_ == nullptr) {
        return BlockIoStatus::NotInitialized;
    }
    if (!this->TicketIsValid(ticket)) {
        return BlockIoStatus::InvalidTicket;
    }
    const BlockIoSlot &slot = this->storage_[ticket.slot_index];
    if (slot.state != BlockIoState::Completed || !this->ResultIsTerminal(slot.result)) {
        return BlockIoStatus::InvalidState;
    }
    if (this->statistics_.result_take_count == UINT64_MAX) {
        return BlockIoStatus::CounterOverflow;
    }
    result = slot.result;
    ++this->statistics_.result_take_count;
    return this->ReleaseSlot(ticket.slot_index);
}

BlockIoStatus BlockIoCoordinator::Abandon(const BlockIoTicket ticket) noexcept {
    if (!this->initialized_ || this->storage_ == nullptr) {
        return BlockIoStatus::NotInitialized;
    }
    if (!this->TicketIsValid(ticket)) {
        return BlockIoStatus::InvalidTicket;
    }
    BlockIoSlot &slot = this->storage_[ticket.slot_index];
    if (slot.state == BlockIoState::Completed) {
        return BlockIoStatus::RequestAlreadyResolved;
    }
    if (slot.state != BlockIoState::Registered && slot.state != BlockIoState::Waiting) {
        return BlockIoStatus::InvalidState;
    }
    if (this->statistics_.abandonment_count == UINT64_MAX ||
        this->statistics_.abandoned_request_count == UINT64_MAX) {
        return BlockIoStatus::CounterOverflow;
    }
    if (slot.state == BlockIoState::Registered) {
        if (this->statistics_.registered_request_count == OS_KERNEL_BLOCK_IO_EMPTY_VALUE) {
            return BlockIoStatus::Corrupt;
        }
        --this->statistics_.registered_request_count;
    } else {
        if (this->statistics_.waiting_request_count == OS_KERNEL_BLOCK_IO_EMPTY_VALUE) {
            return BlockIoStatus::Corrupt;
        }
        --this->statistics_.waiting_request_count;
    }
    slot.state = BlockIoState::Abandoned;
    ++this->statistics_.abandoned_request_count;
    ++this->statistics_.abandonment_count;
    return BlockIoStatus::Succeeded;
}

BlockIoStatus BlockIoCoordinator::Validate() const noexcept {
    if (!this->initialized_ || this->storage_ == nullptr) {
        return BlockIoStatus::NotInitialized;
    }
    uint64_t active_request_count = OS_KERNEL_BLOCK_IO_EMPTY_VALUE;
    uint64_t registered_request_count = OS_KERNEL_BLOCK_IO_EMPTY_VALUE;
    uint64_t waiting_request_count = OS_KERNEL_BLOCK_IO_EMPTY_VALUE;
    uint64_t completed_request_count = OS_KERNEL_BLOCK_IO_EMPTY_VALUE;
    uint64_t abandoned_request_count = OS_KERNEL_BLOCK_IO_EMPTY_VALUE;
    for (uint64_t slot_index = OS_KERNEL_BLOCK_IO_EMPTY_VALUE; slot_index < this->capacity_;
         ++slot_index) {
        const BlockIoSlot &slot = this->storage_[slot_index];
        if (slot.state == BlockIoState::Free) {
            if (slot.request_identifier != OS_KERNEL_BLOCK_IO_EMPTY_VALUE ||
                slot.owner_thread_index != OS_KERNEL_BLOCK_IO_EMPTY_VALUE ||
                slot.result != BlockRequestResult::None) {
                return BlockIoStatus::Corrupt;
            }
            continue;
        }
        if (slot.request_identifier == OS_KERNEL_BLOCK_IO_EMPTY_VALUE ||
            slot.owner_thread_index == OS_KERNEL_BLOCK_REQUEST_KERNEL_OWNER_THREAD_INDEX ||
            slot.generation == OS_KERNEL_BLOCK_IO_EMPTY_VALUE) {
            return BlockIoStatus::Corrupt;
        }
        ++active_request_count;
        if (slot.state == BlockIoState::Registered) {
            ++registered_request_count;
        } else if (slot.state == BlockIoState::Waiting) {
            ++waiting_request_count;
        } else if (slot.state == BlockIoState::Completed) {
            if (!this->ResultIsTerminal(slot.result)) {
                return BlockIoStatus::Corrupt;
            }
            ++completed_request_count;
        } else if (slot.state == BlockIoState::Abandoned) {
            ++abandoned_request_count;
        } else {
            return BlockIoStatus::Corrupt;
        }
        for (uint64_t comparison_index = slot_index + OS_KERNEL_BLOCK_IO_COUNTER_INCREMENT;
             comparison_index < this->capacity_; ++comparison_index) {
            const BlockIoSlot &comparison = this->storage_[comparison_index];
            if (comparison.state != BlockIoState::Free &&
                comparison.owner_thread_index == slot.owner_thread_index) {
                return BlockIoStatus::Corrupt;
            }
        }
    }
    if (active_request_count != this->statistics_.active_request_count ||
        registered_request_count != this->statistics_.registered_request_count ||
        waiting_request_count != this->statistics_.waiting_request_count ||
        completed_request_count != this->statistics_.completed_request_count ||
        abandoned_request_count != this->statistics_.abandoned_request_count ||
        active_request_count > this->capacity_ ||
        this->statistics_.peak_active_request_count < active_request_count ||
        this->statistics_.peak_active_request_count > this->capacity_) {
        return BlockIoStatus::Corrupt;
    }
    return BlockIoStatus::Succeeded;
}

BlockIoStatistics BlockIoCoordinator::Statistics() const noexcept { return this->statistics_; }

bool BlockIoCoordinator::ResultIsTerminal(const BlockRequestResult result) const noexcept {
    return result == BlockRequestResult::Succeeded || result == BlockRequestResult::DeviceError ||
           result == BlockRequestResult::TimedOut || result == BlockRequestResult::Cancelled;
}

bool BlockIoCoordinator::TicketIsValid(const BlockIoTicket ticket) const noexcept {
    return ticket.slot_index < this->capacity_ &&
           ticket.slot_index != OS_KERNEL_BLOCK_IO_INVALID_SLOT_INDEX &&
           ticket.generation != OS_KERNEL_BLOCK_IO_EMPTY_VALUE &&
           this->storage_[ticket.slot_index].state != BlockIoState::Free &&
           this->storage_[ticket.slot_index].generation == ticket.generation;
}

uint64_t BlockIoCoordinator::FindFreeSlotIndex() const noexcept {
    for (uint64_t slot_index = OS_KERNEL_BLOCK_IO_EMPTY_VALUE; slot_index < this->capacity_;
         ++slot_index) {
        if (this->storage_[slot_index].state == BlockIoState::Free) {
            return slot_index;
        }
    }
    return OS_KERNEL_BLOCK_IO_INVALID_SLOT_INDEX;
}

uint64_t
BlockIoCoordinator::FindRequestSlotIndex(const uint64_t owner_thread_index,
                                         const uint64_t request_identifier) const noexcept {
    for (uint64_t slot_index = OS_KERNEL_BLOCK_IO_EMPTY_VALUE; slot_index < this->capacity_;
         ++slot_index) {
        const BlockIoSlot &slot = this->storage_[slot_index];
        if (slot.state != BlockIoState::Free && slot.owner_thread_index == owner_thread_index &&
            slot.request_identifier == request_identifier) {
            return slot_index;
        }
    }
    return OS_KERNEL_BLOCK_IO_INVALID_SLOT_INDEX;
}

bool BlockIoCoordinator::OwnerHasActiveRequest(const uint64_t owner_thread_index) const noexcept {
    for (uint64_t slot_index = OS_KERNEL_BLOCK_IO_EMPTY_VALUE; slot_index < this->capacity_;
         ++slot_index) {
        if (this->storage_[slot_index].state != BlockIoState::Free &&
            this->storage_[slot_index].owner_thread_index == owner_thread_index) {
            return true;
        }
    }
    return false;
}

BlockIoStatus BlockIoCoordinator::ReleaseSlot(const uint64_t slot_index) noexcept {
    if (slot_index >= this->capacity_ ||
        this->statistics_.active_request_count == OS_KERNEL_BLOCK_IO_EMPTY_VALUE) {
        return BlockIoStatus::Corrupt;
    }
    BlockIoSlot &slot = this->storage_[slot_index];
    if (slot.state == BlockIoState::Completed) {
        if (this->statistics_.completed_request_count == OS_KERNEL_BLOCK_IO_EMPTY_VALUE) {
            return BlockIoStatus::Corrupt;
        }
        --this->statistics_.completed_request_count;
    } else if (slot.state == BlockIoState::Abandoned) {
        if (this->statistics_.abandoned_request_count == OS_KERNEL_BLOCK_IO_EMPTY_VALUE) {
            return BlockIoStatus::Corrupt;
        }
        --this->statistics_.abandoned_request_count;
    } else {
        return BlockIoStatus::InvalidState;
    }
    const uint64_t generation = slot.generation;
    slot = BlockIoSlot{};
    slot.generation = generation;
    --this->statistics_.active_request_count;
    return BlockIoStatus::Succeeded;
}

}

#include <os/kernel/process/file_readahead_request.hpp>

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_FILE_READAHEAD_REQUEST_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_FILE_READAHEAD_REQUEST_FIRST_GENERATION = 1ULL;
constexpr uint64_t OS_KERNEL_FILE_READAHEAD_REQUEST_COUNTER_INCREMENT = 1ULL;

}

FileReadaheadRequestStatus
FileReadaheadRequestQueue::Initialize(FileReadaheadRequestSlot *const slot_storage,
                                      uint64_t *const ready_storage,
                                      const uint64_t capacity) noexcept {
    SpinLockGuard guard{this->lock_};
    if (this->initialized_) {
        return FileReadaheadRequestStatus::AlreadyInitialized;
    }
    if (slot_storage == nullptr || ready_storage == nullptr) {
        return FileReadaheadRequestStatus::InvalidStorage;
    }
    if (capacity == OS_KERNEL_FILE_READAHEAD_REQUEST_EMPTY_VALUE) {
        return FileReadaheadRequestStatus::InvalidCapacity;
    }
    for (uint64_t slot_index = OS_KERNEL_FILE_READAHEAD_REQUEST_EMPTY_VALUE; slot_index < capacity;
         ++slot_index) {
        slot_storage[slot_index] = FileReadaheadRequestSlot{};
        ready_storage[slot_index] = OS_KERNEL_FILE_READAHEAD_REQUEST_INVALID_SLOT_INDEX;
    }
    this->slots_ = slot_storage;
    this->ready_storage_ = ready_storage;
    this->capacity_ = capacity;
    this->ready_head_ = OS_KERNEL_FILE_READAHEAD_REQUEST_EMPTY_VALUE;
    this->ready_tail_ = OS_KERNEL_FILE_READAHEAD_REQUEST_EMPTY_VALUE;
    this->ready_count_ = OS_KERNEL_FILE_READAHEAD_REQUEST_EMPTY_VALUE;
    this->statistics_ = FileReadaheadRequestStatistics{};
    this->statistics_.capacity = capacity;
    this->initialized_ = true;
    return FileReadaheadRequestStatus::Succeeded;
}

FileReadaheadRequestStatus
FileReadaheadRequestQueue::Enqueue(const FileReadaheadRequest &request,
                                   FileReadaheadRequestToken &token) noexcept {
    SpinLockGuard guard{this->lock_};
    token = FileReadaheadRequestToken{};
    if (!this->initialized_ || this->slots_ == nullptr || this->ready_storage_ == nullptr) {
        return FileReadaheadRequestStatus::NotInitialized;
    }
    if (!this->RequestIsValid(request)) {
        return FileReadaheadRequestStatus::InvalidRequest;
    }
    const uint64_t slot_index = this->FindFreeSlotIndex();
    if (slot_index == OS_KERNEL_FILE_READAHEAD_REQUEST_INVALID_SLOT_INDEX) {
        if (this->statistics_.capacity_rejection_count == UINT64_MAX) {
            return FileReadaheadRequestStatus::CounterOverflow;
        }
        ++this->statistics_.capacity_rejection_count;
        return FileReadaheadRequestStatus::CapacityExhausted;
    }
    FileReadaheadRequestSlot &slot = this->slots_[slot_index];
    if (slot.generation == UINT64_MAX) {
        return FileReadaheadRequestStatus::GenerationExhausted;
    }
    if (this->statistics_.active_request_count == UINT64_MAX ||
        this->statistics_.queued_request_count == UINT64_MAX ||
        this->statistics_.enqueue_count == UINT64_MAX || this->ready_count_ >= this->capacity_) {
        return FileReadaheadRequestStatus::CounterOverflow;
    }
    const uint64_t generation =
        slot.generation == OS_KERNEL_FILE_READAHEAD_REQUEST_EMPTY_VALUE
            ? OS_KERNEL_FILE_READAHEAD_REQUEST_FIRST_GENERATION
            : slot.generation + OS_KERNEL_FILE_READAHEAD_REQUEST_COUNTER_INCREMENT;
    slot = FileReadaheadRequestSlot{
        .request = request,
        .generation = generation,
        .state = FileReadaheadRequestState::Queued,
    };
    this->ready_storage_[this->ready_tail_] = slot_index;
    this->ready_tail_ =
        (this->ready_tail_ + OS_KERNEL_FILE_READAHEAD_REQUEST_COUNTER_INCREMENT) % this->capacity_;
    ++this->ready_count_;
    ++this->statistics_.active_request_count;
    ++this->statistics_.queued_request_count;
    ++this->statistics_.enqueue_count;
    if (this->statistics_.active_request_count > this->statistics_.peak_active_request_count) {
        this->statistics_.peak_active_request_count = this->statistics_.active_request_count;
    }
    token = FileReadaheadRequestToken{
        .slot_index = slot_index,
        .generation = generation,
    };
    return FileReadaheadRequestStatus::Succeeded;
}

FileReadaheadRequestStatus
FileReadaheadRequestQueue::Acquire(FileReadaheadRequestToken &token,
                                   FileReadaheadRequest &request) noexcept {
    SpinLockGuard guard{this->lock_};
    token = FileReadaheadRequestToken{};
    request = FileReadaheadRequest{};
    if (!this->initialized_ || this->slots_ == nullptr || this->ready_storage_ == nullptr) {
        return FileReadaheadRequestStatus::NotInitialized;
    }
    if (this->ready_count_ == OS_KERNEL_FILE_READAHEAD_REQUEST_EMPTY_VALUE) {
        return FileReadaheadRequestStatus::NoQueuedRequest;
    }
    const uint64_t slot_index = this->ready_storage_[this->ready_head_];
    if (slot_index >= this->capacity_) {
        return FileReadaheadRequestStatus::Corrupt;
    }
    FileReadaheadRequestSlot &slot = this->slots_[slot_index];
    if (slot.state != FileReadaheadRequestState::Queued ||
        this->statistics_.queued_request_count == OS_KERNEL_FILE_READAHEAD_REQUEST_EMPTY_VALUE ||
        this->statistics_.running_request_count == UINT64_MAX ||
        this->statistics_.acquisition_count == UINT64_MAX) {
        return FileReadaheadRequestStatus::Corrupt;
    }
    this->ready_storage_[this->ready_head_] = OS_KERNEL_FILE_READAHEAD_REQUEST_INVALID_SLOT_INDEX;
    this->ready_head_ =
        (this->ready_head_ + OS_KERNEL_FILE_READAHEAD_REQUEST_COUNTER_INCREMENT) % this->capacity_;
    --this->ready_count_;
    --this->statistics_.queued_request_count;
    ++this->statistics_.running_request_count;
    ++this->statistics_.acquisition_count;
    slot.state = FileReadaheadRequestState::Running;
    token = FileReadaheadRequestToken{
        .slot_index = slot_index,
        .generation = slot.generation,
    };
    request = slot.request;
    return FileReadaheadRequestStatus::Succeeded;
}

FileReadaheadRequestStatus
FileReadaheadRequestQueue::Complete(const FileReadaheadRequestToken token) noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->TokenIsValid(token)) {
        return FileReadaheadRequestStatus::InvalidToken;
    }
    FileReadaheadRequestSlot &slot = this->slots_[token.slot_index];
    if (slot.state != FileReadaheadRequestState::Running ||
        this->statistics_.active_request_count == OS_KERNEL_FILE_READAHEAD_REQUEST_EMPTY_VALUE ||
        this->statistics_.running_request_count == OS_KERNEL_FILE_READAHEAD_REQUEST_EMPTY_VALUE ||
        this->statistics_.completion_count == UINT64_MAX) {
        return FileReadaheadRequestStatus::InvalidState;
    }
    const uint64_t generation = slot.generation;
    slot = FileReadaheadRequestSlot{};
    slot.generation = generation;
    --this->statistics_.active_request_count;
    --this->statistics_.running_request_count;
    ++this->statistics_.completion_count;
    return FileReadaheadRequestStatus::Succeeded;
}

FileReadaheadRequestStatistics FileReadaheadRequestQueue::Statistics() const noexcept {
    SpinLockGuard guard{this->lock_};
    return this->statistics_;
}

FileReadaheadRequestStatus FileReadaheadRequestQueue::Validate() const noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_ || this->slots_ == nullptr || this->ready_storage_ == nullptr ||
        this->capacity_ == OS_KERNEL_FILE_READAHEAD_REQUEST_EMPTY_VALUE ||
        this->ready_head_ >= this->capacity_ || this->ready_tail_ >= this->capacity_ ||
        this->ready_count_ > this->capacity_) {
        return FileReadaheadRequestStatus::NotInitialized;
    }
    uint64_t active_request_count = OS_KERNEL_FILE_READAHEAD_REQUEST_EMPTY_VALUE;
    uint64_t queued_request_count = OS_KERNEL_FILE_READAHEAD_REQUEST_EMPTY_VALUE;
    uint64_t running_request_count = OS_KERNEL_FILE_READAHEAD_REQUEST_EMPTY_VALUE;
    for (uint64_t slot_index = OS_KERNEL_FILE_READAHEAD_REQUEST_EMPTY_VALUE;
         slot_index < this->capacity_; ++slot_index) {
        const FileReadaheadRequestSlot &slot = this->slots_[slot_index];
        if (slot.state == FileReadaheadRequestState::Free) {
            if (slot.request.vfs != nullptr || slot.request.open_file.open ||
                slot.request.page_count != OS_KERNEL_FILE_READAHEAD_REQUEST_EMPTY_VALUE ||
                slot.request.policy_generation != OS_KERNEL_FILE_READAHEAD_REQUEST_EMPTY_VALUE) {
                return FileReadaheadRequestStatus::Corrupt;
            }
            continue;
        }
        if (!this->RequestIsValid(slot.request) ||
            slot.generation == OS_KERNEL_FILE_READAHEAD_REQUEST_EMPTY_VALUE) {
            return FileReadaheadRequestStatus::Corrupt;
        }
        ++active_request_count;
        if (slot.state == FileReadaheadRequestState::Queued) {
            ++queued_request_count;
        } else if (slot.state == FileReadaheadRequestState::Running) {
            ++running_request_count;
        } else {
            return FileReadaheadRequestStatus::Corrupt;
        }
    }
    if (queued_request_count != this->ready_count_) {
        return FileReadaheadRequestStatus::Corrupt;
    }
    for (uint64_t ready_offset = OS_KERNEL_FILE_READAHEAD_REQUEST_EMPTY_VALUE;
         ready_offset < this->ready_count_; ++ready_offset) {
        const uint64_t ready_index = (this->ready_head_ + ready_offset) % this->capacity_;
        const uint64_t slot_index = this->ready_storage_[ready_index];
        if (slot_index >= this->capacity_ ||
            this->slots_[slot_index].state != FileReadaheadRequestState::Queued) {
            return FileReadaheadRequestStatus::Corrupt;
        }
        for (uint64_t comparison_offset =
                 ready_offset + OS_KERNEL_FILE_READAHEAD_REQUEST_COUNTER_INCREMENT;
             comparison_offset < this->ready_count_; ++comparison_offset) {
            const uint64_t comparison_index =
                (this->ready_head_ + comparison_offset) % this->capacity_;
            if (this->ready_storage_[comparison_index] == slot_index) {
                return FileReadaheadRequestStatus::Corrupt;
            }
        }
    }
    return active_request_count == this->statistics_.active_request_count &&
                   queued_request_count == this->statistics_.queued_request_count &&
                   running_request_count == this->statistics_.running_request_count &&
                   active_request_count == queued_request_count + running_request_count &&
                   this->statistics_.peak_active_request_count >= active_request_count &&
                   this->statistics_.enqueue_count ==
                       this->statistics_.completion_count + active_request_count
               ? FileReadaheadRequestStatus::Succeeded
               : FileReadaheadRequestStatus::Corrupt;
}

bool FileReadaheadRequestQueue::RequestIsValid(const FileReadaheadRequest &request) const noexcept {
    return request.vfs != nullptr && request.open_file.open &&
           request.open_file.path.vnode.type == fs::NodeType::RegularFile &&
           request.open_file.readable &&
           request.page_count != OS_KERNEL_FILE_READAHEAD_REQUEST_EMPTY_VALUE &&
           request.policy_generation != OS_KERNEL_FILE_READAHEAD_REQUEST_EMPTY_VALUE &&
           request.start_page_index <= UINT64_MAX - request.page_count;
}

bool FileReadaheadRequestQueue::TokenIsValid(const FileReadaheadRequestToken token) const noexcept {
    return this->initialized_ && token.slot_index < this->capacity_ &&
           token.generation != OS_KERNEL_FILE_READAHEAD_REQUEST_EMPTY_VALUE &&
           this->slots_[token.slot_index].state != FileReadaheadRequestState::Free &&
           this->slots_[token.slot_index].generation == token.generation;
}

uint64_t FileReadaheadRequestQueue::FindFreeSlotIndex() const noexcept {
    for (uint64_t slot_index = OS_KERNEL_FILE_READAHEAD_REQUEST_EMPTY_VALUE;
         slot_index < this->capacity_; ++slot_index) {
        if (this->slots_[slot_index].state == FileReadaheadRequestState::Free) {
            return slot_index;
        }
    }
    return OS_KERNEL_FILE_READAHEAD_REQUEST_INVALID_SLOT_INDEX;
}

}

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
        .cancellation_requested = false,
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
    const bool cancellation_requested = slot.cancellation_requested;
    if (cancellation_requested && (this->statistics_.active_running_cancellation_count ==
                                       OS_KERNEL_FILE_READAHEAD_REQUEST_EMPTY_VALUE ||
                                   this->statistics_.cancelled_completion_count == UINT64_MAX)) {
        return FileReadaheadRequestStatus::InvalidState;
    }
    const uint64_t generation = slot.generation;
    slot = FileReadaheadRequestSlot{};
    slot.generation = generation;
    --this->statistics_.active_request_count;
    --this->statistics_.running_request_count;
    ++this->statistics_.completion_count;
    if (cancellation_requested) {
        --this->statistics_.active_running_cancellation_count;
        ++this->statistics_.cancelled_completion_count;
    }
    return FileReadaheadRequestStatus::Succeeded;
}

FileReadaheadRequestStatus FileReadaheadRequestQueue::CancelStream(
    const FileReadaheadStreamToken stream, const uint64_t maximum_policy_generation,
    FileReadaheadRequest *const cancelled_request_storage,
    const uint64_t cancelled_request_capacity, uint64_t &cancelled_request_count) noexcept {
    SpinLockGuard guard{this->lock_};
    cancelled_request_count = OS_KERNEL_FILE_READAHEAD_REQUEST_EMPTY_VALUE;
    if (!FileReadaheadStreamTokenIsValid(stream) ||
        maximum_policy_generation == OS_KERNEL_FILE_READAHEAD_REQUEST_EMPTY_VALUE) {
        return FileReadaheadRequestStatus::InvalidRequest;
    }
    return this->CancelMatching(&stream, nullptr, maximum_policy_generation,
                                cancelled_request_storage, cancelled_request_capacity,
                                cancelled_request_count);
}

FileReadaheadRequestStatus FileReadaheadRequestQueue::CancelFile(
    const FileCacheIdentity &identity, FileReadaheadRequest *const cancelled_request_storage,
    const uint64_t cancelled_request_capacity, uint64_t &cancelled_request_count) noexcept {
    SpinLockGuard guard{this->lock_};
    cancelled_request_count = OS_KERNEL_FILE_READAHEAD_REQUEST_EMPTY_VALUE;
    if (!FileCacheIdentityIsValid(identity)) {
        return FileReadaheadRequestStatus::InvalidRequest;
    }
    return this->CancelMatching(nullptr, &identity, UINT64_MAX, cancelled_request_storage,
                                cancelled_request_capacity, cancelled_request_count);
}

FileReadaheadRequestStatus
FileReadaheadRequestQueue::CancellationRequested(const FileReadaheadRequestToken token,
                                                 bool &cancellation_requested) noexcept {
    SpinLockGuard guard{this->lock_};
    cancellation_requested = false;
    if (!this->TokenIsValid(token)) {
        return FileReadaheadRequestStatus::InvalidToken;
    }
    const FileReadaheadRequestSlot &slot = this->slots_[token.slot_index];
    if (slot.state != FileReadaheadRequestState::Running) {
        return FileReadaheadRequestStatus::InvalidState;
    }
    cancellation_requested = slot.cancellation_requested;
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
        this->ready_count_ > this->capacity_ ||
        this->ready_tail_ != (this->ready_head_ + this->ready_count_) % this->capacity_) {
        return FileReadaheadRequestStatus::NotInitialized;
    }
    uint64_t active_request_count = OS_KERNEL_FILE_READAHEAD_REQUEST_EMPTY_VALUE;
    uint64_t queued_request_count = OS_KERNEL_FILE_READAHEAD_REQUEST_EMPTY_VALUE;
    uint64_t running_request_count = OS_KERNEL_FILE_READAHEAD_REQUEST_EMPTY_VALUE;
    uint64_t active_running_cancellation_count = OS_KERNEL_FILE_READAHEAD_REQUEST_EMPTY_VALUE;
    for (uint64_t slot_index = OS_KERNEL_FILE_READAHEAD_REQUEST_EMPTY_VALUE;
         slot_index < this->capacity_; ++slot_index) {
        const FileReadaheadRequestSlot &slot = this->slots_[slot_index];
        if (slot.state == FileReadaheadRequestState::Free) {
            if (slot.request.vfs != nullptr || slot.request.open_file.open ||
                slot.request.page_count != OS_KERNEL_FILE_READAHEAD_REQUEST_EMPTY_VALUE ||
                slot.request.policy_generation != OS_KERNEL_FILE_READAHEAD_REQUEST_EMPTY_VALUE ||
                slot.request.stream.slot_index != OS_KERNEL_FILE_READAHEAD_REQUEST_EMPTY_VALUE ||
                slot.request.stream.generation != OS_KERNEL_FILE_READAHEAD_REQUEST_EMPTY_VALUE ||
                slot.cancellation_requested) {
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
            if (slot.cancellation_requested) {
                return FileReadaheadRequestStatus::Corrupt;
            }
            ++queued_request_count;
        } else if (slot.state == FileReadaheadRequestState::Running) {
            ++running_request_count;
            if (slot.cancellation_requested) {
                ++active_running_cancellation_count;
            }
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
    if (this->statistics_.completion_count >
            UINT64_MAX - this->statistics_.queued_cancellation_count ||
        this->statistics_.completion_count + this->statistics_.queued_cancellation_count >
            UINT64_MAX - active_request_count ||
        this->statistics_.cancelled_completion_count >
            UINT64_MAX - active_running_cancellation_count) {
        return FileReadaheadRequestStatus::Corrupt;
    }
    return active_request_count == this->statistics_.active_request_count &&
                   queued_request_count == this->statistics_.queued_request_count &&
                   running_request_count == this->statistics_.running_request_count &&
                   active_request_count == queued_request_count + running_request_count &&
                   this->statistics_.peak_active_request_count >= active_request_count &&
                   this->statistics_.enqueue_count ==
                       this->statistics_.completion_count +
                           this->statistics_.queued_cancellation_count + active_request_count &&
                   active_running_cancellation_count ==
                       this->statistics_.active_running_cancellation_count &&
                   this->statistics_.running_cancellation_request_count ==
                       this->statistics_.cancelled_completion_count +
                           active_running_cancellation_count &&
                   this->statistics_.cancelled_completion_count <=
                       this->statistics_.completion_count
               ? FileReadaheadRequestStatus::Succeeded
               : FileReadaheadRequestStatus::Corrupt;
}

FileReadaheadRequestStatus FileReadaheadRequestQueue::CancelMatching(
    const FileReadaheadStreamToken *const stream, const FileCacheIdentity *const identity,
    const uint64_t maximum_policy_generation, FileReadaheadRequest *const cancelled_request_storage,
    const uint64_t cancelled_request_capacity, uint64_t &cancelled_request_count) noexcept {
    cancelled_request_count = OS_KERNEL_FILE_READAHEAD_REQUEST_EMPTY_VALUE;
    if (!this->initialized_ || this->slots_ == nullptr || this->ready_storage_ == nullptr) {
        return FileReadaheadRequestStatus::NotInitialized;
    }
    uint64_t queued_match_count = OS_KERNEL_FILE_READAHEAD_REQUEST_EMPTY_VALUE;
    uint64_t running_match_count = OS_KERNEL_FILE_READAHEAD_REQUEST_EMPTY_VALUE;
    for (uint64_t slot_index = OS_KERNEL_FILE_READAHEAD_REQUEST_EMPTY_VALUE;
         slot_index < this->capacity_; ++slot_index) {
        const FileReadaheadRequestSlot &slot = this->slots_[slot_index];
        if (!this->RequestMatches(slot.request, stream, identity, maximum_policy_generation)) {
            continue;
        }
        if (slot.state == FileReadaheadRequestState::Queued) {
            ++queued_match_count;
        } else if (slot.state == FileReadaheadRequestState::Running &&
                   !slot.cancellation_requested) {
            ++running_match_count;
        }
    }
    if (queued_match_count != OS_KERNEL_FILE_READAHEAD_REQUEST_EMPTY_VALUE &&
        (cancelled_request_storage == nullptr || cancelled_request_capacity < queued_match_count)) {
        return FileReadaheadRequestStatus::InvalidStorage;
    }
    if (this->statistics_.active_request_count < queued_match_count ||
        this->statistics_.queued_request_count < queued_match_count) {
        return FileReadaheadRequestStatus::Corrupt;
    }
    if (this->statistics_.queued_cancellation_count > UINT64_MAX - queued_match_count ||
        this->statistics_.running_cancellation_request_count > UINT64_MAX - running_match_count ||
        this->statistics_.active_running_cancellation_count > UINT64_MAX - running_match_count) {
        return FileReadaheadRequestStatus::CounterOverflow;
    }
    uint64_t retained_ready_count = OS_KERNEL_FILE_READAHEAD_REQUEST_EMPTY_VALUE;
    const uint64_t original_ready_count = this->ready_count_;
    for (uint64_t ready_offset = OS_KERNEL_FILE_READAHEAD_REQUEST_EMPTY_VALUE;
         ready_offset < original_ready_count; ++ready_offset) {
        const uint64_t read_index = (this->ready_head_ + ready_offset) % this->capacity_;
        const uint64_t slot_index = this->ready_storage_[read_index];
        if (slot_index >= this->capacity_) {
            return FileReadaheadRequestStatus::Corrupt;
        }
        FileReadaheadRequestSlot &slot = this->slots_[slot_index];
        if (slot.state != FileReadaheadRequestState::Queued) {
            return FileReadaheadRequestStatus::Corrupt;
        }
        if (this->RequestMatches(slot.request, stream, identity, maximum_policy_generation)) {
            cancelled_request_storage[cancelled_request_count] = slot.request;
            ++cancelled_request_count;
            const uint64_t generation = slot.generation;
            slot = FileReadaheadRequestSlot{};
            slot.generation = generation;
            continue;
        }
        const uint64_t write_index = (this->ready_head_ + retained_ready_count) % this->capacity_;
        this->ready_storage_[write_index] = slot_index;
        ++retained_ready_count;
    }
    for (uint64_t ready_offset = retained_ready_count; ready_offset < original_ready_count;
         ++ready_offset) {
        const uint64_t ready_index = (this->ready_head_ + ready_offset) % this->capacity_;
        this->ready_storage_[ready_index] = OS_KERNEL_FILE_READAHEAD_REQUEST_INVALID_SLOT_INDEX;
    }
    this->ready_count_ = retained_ready_count;
    this->ready_tail_ = (this->ready_head_ + retained_ready_count) % this->capacity_;
    this->statistics_.active_request_count -= cancelled_request_count;
    this->statistics_.queued_request_count -= cancelled_request_count;
    this->statistics_.queued_cancellation_count += cancelled_request_count;
    for (uint64_t slot_index = OS_KERNEL_FILE_READAHEAD_REQUEST_EMPTY_VALUE;
         slot_index < this->capacity_; ++slot_index) {
        FileReadaheadRequestSlot &slot = this->slots_[slot_index];
        if (slot.state == FileReadaheadRequestState::Running && !slot.cancellation_requested &&
            this->RequestMatches(slot.request, stream, identity, maximum_policy_generation)) {
            slot.cancellation_requested = true;
        }
    }
    this->statistics_.running_cancellation_request_count += running_match_count;
    this->statistics_.active_running_cancellation_count += running_match_count;
    return FileReadaheadRequestStatus::Succeeded;
}

bool FileReadaheadRequestQueue::RequestMatches(
    const FileReadaheadRequest &request, const FileReadaheadStreamToken *const stream,
    const FileCacheIdentity *const identity,
    const uint64_t maximum_policy_generation) const noexcept {
    if (request.vfs == nullptr || request.policy_generation > maximum_policy_generation) {
        return false;
    }
    if (stream != nullptr && !FileReadaheadStreamTokensEqual(request.stream, *stream)) {
        return false;
    }
    if (identity == nullptr) {
        return stream != nullptr;
    }
    const fs::Vnode &vnode = request.open_file.path.vnode;
    return vnode.superblock != nullptr &&
           FileCacheIdentitiesEqual(
               FileCacheIdentity{
                   .superblock_identifier = vnode.superblock->identifier,
                   .superblock_generation = vnode.superblock->generation,
                   .node_identifier = vnode.identifier,
                   .node_generation = vnode.generation,
               },
               *identity);
}

bool FileReadaheadRequestQueue::RequestIsValid(const FileReadaheadRequest &request) const noexcept {
    return request.vfs != nullptr && request.open_file.open &&
           request.open_file.path.vnode.type == fs::NodeType::RegularFile &&
           request.open_file.readable &&
           request.page_count != OS_KERNEL_FILE_READAHEAD_REQUEST_EMPTY_VALUE &&
           request.policy_generation != OS_KERNEL_FILE_READAHEAD_REQUEST_EMPTY_VALUE &&
           FileReadaheadStreamTokenIsValid(request.stream) &&
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

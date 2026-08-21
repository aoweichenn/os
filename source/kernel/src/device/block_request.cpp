#include <os/kernel/device/block_request.hpp>

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_BLOCK_REQUEST_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_BLOCK_REQUEST_SINGLE_UNIT = 1ULL;

}

BlockRequestQueueStatus
BlockRequestQueue::Initialize(BlockRequest *const storage, const uint64_t capacity,
                              const BlockDeviceGeometry &geometry) noexcept {
    if (this->initialized_) {
        return BlockRequestQueueStatus::AlreadyInitialized;
    }
    if (storage == nullptr) {
        return BlockRequestQueueStatus::InvalidStorage;
    }
    if (capacity == OS_KERNEL_BLOCK_REQUEST_EMPTY_VALUE) {
        return BlockRequestQueueStatus::InvalidCapacity;
    }
    if (!this->GeometryIsValid(geometry) ||
        geometry.maximum_outstanding_request_count > capacity) {
        return BlockRequestQueueStatus::InvalidGeometry;
    }
    for (uint64_t request_index = OS_KERNEL_BLOCK_REQUEST_EMPTY_VALUE;
         request_index < capacity; ++request_index) {
        storage[request_index] = BlockRequest{};
        storage[request_index].next_queue_index = OS_KERNEL_BLOCK_REQUEST_INVALID_INDEX;
    }
    this->storage_ = storage;
    this->capacity_ = capacity;
    this->geometry_ = geometry;
    this->queue_head_index_ = OS_KERNEL_BLOCK_REQUEST_INVALID_INDEX;
    this->queue_tail_index_ = OS_KERNEL_BLOCK_REQUEST_INVALID_INDEX;
    this->next_identifier_ = OS_KERNEL_BLOCK_REQUEST_FIRST_IDENTIFIER;
    this->statistics_ = BlockRequestQueueStatistics{};
    this->statistics_.capacity = capacity;
    this->statistics_.geometry = geometry;
    this->initialized_ = true;
    return BlockRequestQueueStatus::Succeeded;
}

BlockRequestQueueStatus BlockRequestQueue::Submit(
    const BlockOperation operation, const uint64_t logical_block_address, uint8_t *const buffer,
    const uint64_t buffer_size_bytes, const uint64_t owner_thread_index,
    const uint64_t deadline_nanoseconds, uint64_t &request_identifier) noexcept {
    request_identifier = OS_KERNEL_BLOCK_REQUEST_EMPTY_VALUE;
    if (!this->initialized_ || this->storage_ == nullptr) {
        return BlockRequestQueueStatus::NotInitialized;
    }
    uint64_t logical_block_count = OS_KERNEL_BLOCK_REQUEST_EMPTY_VALUE;
    if (!this->RequestIsValid(operation, logical_block_address, buffer, buffer_size_bytes,
                              deadline_nanoseconds, logical_block_count)) {
        return BlockRequestQueueStatus::InvalidRequest;
    }
    uint64_t request_index = OS_KERNEL_BLOCK_REQUEST_INVALID_INDEX;
    if (!this->FindFreeIndex(request_index)) {
        ++this->statistics_.capacity_rejection_count;
        return BlockRequestQueueStatus::CapacityExhausted;
    }
    if (this->next_identifier_ == OS_KERNEL_BLOCK_REQUEST_EMPTY_VALUE ||
        this->next_identifier_ == UINT64_MAX) {
        return BlockRequestQueueStatus::IdentifierExhausted;
    }

    request_identifier = this->next_identifier_;
    ++this->next_identifier_;
    this->storage_[request_index] = BlockRequest{
        .identifier = request_identifier,
        .operation = operation,
        .logical_block_address = logical_block_address,
        .logical_block_count = logical_block_count,
        .buffer = buffer,
        .buffer_size_bytes = buffer_size_bytes,
        .owner_thread_index = owner_thread_index,
        .deadline_nanoseconds = deadline_nanoseconds,
        .state = BlockRequestState::Queued,
        .result = BlockRequestResult::None,
        .next_queue_index = OS_KERNEL_BLOCK_REQUEST_INVALID_INDEX,
    };
    this->AppendQueuedIndex(request_index);
    ++this->statistics_.active_request_count;
    ++this->statistics_.queued_request_count;
    ++this->statistics_.submission_count;
    if (this->statistics_.active_request_count > this->statistics_.peak_active_request_count) {
        this->statistics_.peak_active_request_count = this->statistics_.active_request_count;
    }
    return BlockRequestQueueStatus::Succeeded;
}

BlockRequestQueueStatus
BlockRequestQueue::IssueNext(BlockRequest &request, bool &issued) noexcept {
    request = BlockRequest{};
    issued = false;
    if (!this->initialized_ || this->storage_ == nullptr) {
        return BlockRequestQueueStatus::NotInitialized;
    }
    if (this->statistics_.issued_request_count >=
            this->geometry_.maximum_outstanding_request_count ||
        this->queue_head_index_ == OS_KERNEL_BLOCK_REQUEST_INVALID_INDEX) {
        return this->Validate();
    }
    const uint64_t request_index = this->queue_head_index_;
    BlockRequest &candidate = this->storage_[request_index];
    if (candidate.state != BlockRequestState::Queued ||
        !this->RemoveQueuedIndex(request_index) ||
        this->statistics_.queued_request_count == OS_KERNEL_BLOCK_REQUEST_EMPTY_VALUE) {
        return BlockRequestQueueStatus::Corrupt;
    }
    candidate.state = BlockRequestState::Issued;
    --this->statistics_.queued_request_count;
    ++this->statistics_.issued_request_count;
    ++this->statistics_.issue_count;
    if (this->statistics_.issued_request_count >
        this->statistics_.peak_issued_request_count) {
        this->statistics_.peak_issued_request_count = this->statistics_.issued_request_count;
    }
    request = candidate;
    issued = true;
    return BlockRequestQueueStatus::Succeeded;
}

BlockRequestQueueStatus
BlockRequestQueue::Complete(const uint64_t request_identifier,
                            const BlockRequestResult result) noexcept {
    if (!this->initialized_ || this->storage_ == nullptr) {
        return BlockRequestQueueStatus::NotInitialized;
    }
    if (!this->ResultIsTerminal(result)) {
        return BlockRequestQueueStatus::InvalidRequest;
    }
    BlockRequest *const request = this->Find(request_identifier);
    if (request == nullptr) {
        return BlockRequestQueueStatus::RequestNotFound;
    }
    if (request->state == BlockRequestState::Completed) {
        ++this->statistics_.duplicate_resolution_count;
        return BlockRequestQueueStatus::RequestAlreadyResolved;
    }
    if (request->state != BlockRequestState::Issued ||
        this->statistics_.issued_request_count == OS_KERNEL_BLOCK_REQUEST_EMPTY_VALUE) {
        return BlockRequestQueueStatus::RequestNotIssued;
    }
    request->state = BlockRequestState::Completed;
    request->result = result;
    --this->statistics_.issued_request_count;
    ++this->statistics_.completed_request_count;
    this->RecordResolution(result);
    return BlockRequestQueueStatus::Succeeded;
}

BlockRequestQueueStatus BlockRequestQueue::ResolveTimeout(
    const uint64_t now_nanoseconds, BlockRequest &request, bool &resolved) noexcept {
    request = BlockRequest{};
    resolved = false;
    if (!this->initialized_ || this->storage_ == nullptr) {
        return BlockRequestQueueStatus::NotInitialized;
    }
    BlockRequest *candidate = nullptr;
    for (uint64_t request_index = OS_KERNEL_BLOCK_REQUEST_EMPTY_VALUE;
         request_index < this->capacity_; ++request_index) {
        BlockRequest &current = this->storage_[request_index];
        if (current.state != BlockRequestState::Issued ||
            now_nanoseconds < current.deadline_nanoseconds) {
            continue;
        }
        if (candidate == nullptr ||
            current.deadline_nanoseconds < candidate->deadline_nanoseconds ||
            (current.deadline_nanoseconds == candidate->deadline_nanoseconds &&
             current.identifier < candidate->identifier)) {
            candidate = &current;
        }
    }
    if (candidate == nullptr) {
        return BlockRequestQueueStatus::Succeeded;
    }
    request = *candidate;
    const BlockRequestQueueStatus status =
        this->Complete(candidate->identifier, BlockRequestResult::TimedOut);
    resolved = status == BlockRequestQueueStatus::Succeeded;
    if (resolved) {
        request = *candidate;
    }
    return status;
}

BlockRequestQueueStatus
BlockRequestQueue::CancelQueued(const uint64_t request_identifier) noexcept {
    if (!this->initialized_ || this->storage_ == nullptr) {
        return BlockRequestQueueStatus::NotInitialized;
    }
    BlockRequest *const request = this->Find(request_identifier);
    if (request == nullptr) {
        return BlockRequestQueueStatus::RequestNotFound;
    }
    if (request->state == BlockRequestState::Completed) {
        ++this->statistics_.duplicate_resolution_count;
        return BlockRequestQueueStatus::RequestAlreadyResolved;
    }
    if (request->state != BlockRequestState::Queued) {
        return BlockRequestQueueStatus::RequestNotQueued;
    }
    const uint64_t request_index = this->IndexOf(*request);
    if (!this->RemoveQueuedIndex(request_index) ||
        this->statistics_.queued_request_count == OS_KERNEL_BLOCK_REQUEST_EMPTY_VALUE) {
        return BlockRequestQueueStatus::Corrupt;
    }
    --this->statistics_.queued_request_count;
    ++this->statistics_.completed_request_count;
    request->state = BlockRequestState::Completed;
    request->result = BlockRequestResult::Cancelled;
    this->RecordResolution(BlockRequestResult::Cancelled);
    return BlockRequestQueueStatus::Succeeded;
}

BlockRequestQueueStatus
BlockRequestQueue::Read(const uint64_t request_identifier, BlockRequest &request) const noexcept {
    request = BlockRequest{};
    if (!this->initialized_ || this->storage_ == nullptr) {
        return BlockRequestQueueStatus::NotInitialized;
    }
    const BlockRequest *const found_request = this->Find(request_identifier);
    if (found_request == nullptr) {
        return BlockRequestQueueStatus::RequestNotFound;
    }
    request = *found_request;
    return BlockRequestQueueStatus::Succeeded;
}

BlockRequestQueueStatus
BlockRequestQueue::Reap(const uint64_t request_identifier) noexcept {
    if (!this->initialized_ || this->storage_ == nullptr) {
        return BlockRequestQueueStatus::NotInitialized;
    }
    BlockRequest *const request = this->Find(request_identifier);
    if (request == nullptr) {
        return BlockRequestQueueStatus::RequestNotFound;
    }
    if (request->state != BlockRequestState::Completed) {
        return BlockRequestQueueStatus::RequestNotCompleted;
    }
    if (this->statistics_.active_request_count == OS_KERNEL_BLOCK_REQUEST_EMPTY_VALUE ||
        this->statistics_.completed_request_count == OS_KERNEL_BLOCK_REQUEST_EMPTY_VALUE) {
        return BlockRequestQueueStatus::Corrupt;
    }
    *request = BlockRequest{};
    request->next_queue_index = OS_KERNEL_BLOCK_REQUEST_INVALID_INDEX;
    --this->statistics_.active_request_count;
    --this->statistics_.completed_request_count;
    ++this->statistics_.reap_count;
    return BlockRequestQueueStatus::Succeeded;
}

BlockRequestQueueStatus BlockRequestQueue::Validate() const noexcept {
    if (!this->initialized_ || this->storage_ == nullptr) {
        return BlockRequestQueueStatus::NotInitialized;
    }
    if (!this->GeometryIsValid(this->geometry_) ||
        this->geometry_.maximum_outstanding_request_count > this->capacity_ ||
        this->statistics_.capacity != this->capacity_ ||
        this->statistics_.geometry.logical_block_size_bytes !=
            this->geometry_.logical_block_size_bytes ||
        this->statistics_.geometry.logical_block_count != this->geometry_.logical_block_count ||
        this->statistics_.geometry.maximum_transfer_block_count !=
            this->geometry_.maximum_transfer_block_count ||
        this->statistics_.geometry.maximum_outstanding_request_count !=
            this->geometry_.maximum_outstanding_request_count ||
        this->statistics_.geometry.write_supported != this->geometry_.write_supported ||
        this->statistics_.geometry.flush_supported != this->geometry_.flush_supported) {
        return BlockRequestQueueStatus::Corrupt;
    }
    uint64_t active_request_count = OS_KERNEL_BLOCK_REQUEST_EMPTY_VALUE;
    uint64_t queued_request_count = OS_KERNEL_BLOCK_REQUEST_EMPTY_VALUE;
    uint64_t issued_request_count = OS_KERNEL_BLOCK_REQUEST_EMPTY_VALUE;
    uint64_t completed_request_count = OS_KERNEL_BLOCK_REQUEST_EMPTY_VALUE;
    for (uint64_t request_index = OS_KERNEL_BLOCK_REQUEST_EMPTY_VALUE;
         request_index < this->capacity_; ++request_index) {
        const BlockRequest &request = this->storage_[request_index];
        if (request.state == BlockRequestState::Unused) {
            if (request.identifier != OS_KERNEL_BLOCK_REQUEST_EMPTY_VALUE) {
                return BlockRequestQueueStatus::Corrupt;
            }
            continue;
        }
        uint64_t expected_logical_block_count = OS_KERNEL_BLOCK_REQUEST_EMPTY_VALUE;
        if (request.identifier == OS_KERNEL_BLOCK_REQUEST_EMPTY_VALUE ||
            !this->RequestIsValid(request.operation, request.logical_block_address, request.buffer,
                                  request.buffer_size_bytes, request.deadline_nanoseconds,
                                  expected_logical_block_count) ||
            request.logical_block_count != expected_logical_block_count) {
            return BlockRequestQueueStatus::Corrupt;
        }
        ++active_request_count;
        if (request.state == BlockRequestState::Queued) {
            ++queued_request_count;
        } else if (request.state == BlockRequestState::Issued) {
            ++issued_request_count;
        } else if (request.state == BlockRequestState::Completed) {
            ++completed_request_count;
            if (!this->ResultIsTerminal(request.result)) {
                return BlockRequestQueueStatus::Corrupt;
            }
        } else {
            return BlockRequestQueueStatus::Corrupt;
        }
        for (uint64_t comparison_index = request_index + OS_KERNEL_BLOCK_REQUEST_SINGLE_UNIT;
             comparison_index < this->capacity_; ++comparison_index) {
            if (this->storage_[comparison_index].state != BlockRequestState::Unused &&
                this->storage_[comparison_index].identifier == request.identifier) {
                return BlockRequestQueueStatus::Corrupt;
            }
        }
    }

    uint64_t observed_queue_count = OS_KERNEL_BLOCK_REQUEST_EMPTY_VALUE;
    uint64_t queue_index = this->queue_head_index_;
    uint64_t previous_index = OS_KERNEL_BLOCK_REQUEST_INVALID_INDEX;
    while (queue_index != OS_KERNEL_BLOCK_REQUEST_INVALID_INDEX) {
        if (queue_index >= this->capacity_ ||
            this->storage_[queue_index].state != BlockRequestState::Queued ||
            observed_queue_count >= this->capacity_) {
            return BlockRequestQueueStatus::Corrupt;
        }
        previous_index = queue_index;
        queue_index = this->storage_[queue_index].next_queue_index;
        ++observed_queue_count;
    }
    if ((observed_queue_count == OS_KERNEL_BLOCK_REQUEST_EMPTY_VALUE &&
         (this->queue_head_index_ != OS_KERNEL_BLOCK_REQUEST_INVALID_INDEX ||
          this->queue_tail_index_ != OS_KERNEL_BLOCK_REQUEST_INVALID_INDEX)) ||
        (observed_queue_count != OS_KERNEL_BLOCK_REQUEST_EMPTY_VALUE &&
         previous_index != this->queue_tail_index_) ||
        active_request_count != this->statistics_.active_request_count ||
        queued_request_count != this->statistics_.queued_request_count ||
        issued_request_count != this->statistics_.issued_request_count ||
        completed_request_count != this->statistics_.completed_request_count ||
        observed_queue_count != queued_request_count ||
        issued_request_count > this->geometry_.maximum_outstanding_request_count ||
        this->statistics_.peak_issued_request_count < issued_request_count ||
        this->statistics_.peak_issued_request_count >
            this->geometry_.maximum_outstanding_request_count ||
        active_request_count > this->capacity_) {
        return BlockRequestQueueStatus::Corrupt;
    }
    return BlockRequestQueueStatus::Succeeded;
}

BlockRequestQueueStatistics BlockRequestQueue::Statistics() const noexcept {
    return this->statistics_;
}

bool BlockRequestQueue::GeometryIsValid(const BlockDeviceGeometry &geometry) const noexcept {
    return geometry.logical_block_size_bytes != OS_KERNEL_BLOCK_REQUEST_EMPTY_VALUE &&
           geometry.logical_block_count != OS_KERNEL_BLOCK_REQUEST_EMPTY_VALUE &&
           geometry.maximum_transfer_block_count != OS_KERNEL_BLOCK_REQUEST_EMPTY_VALUE &&
           geometry.maximum_transfer_block_count <= geometry.logical_block_count &&
           geometry.maximum_outstanding_request_count != OS_KERNEL_BLOCK_REQUEST_EMPTY_VALUE &&
           geometry.logical_block_size_bytes <=
               UINT64_MAX / geometry.maximum_transfer_block_count;
}

bool BlockRequestQueue::ResultIsTerminal(const BlockRequestResult result) const noexcept {
    return result == BlockRequestResult::Succeeded || result == BlockRequestResult::DeviceError ||
           result == BlockRequestResult::TimedOut || result == BlockRequestResult::Cancelled;
}

bool BlockRequestQueue::RequestIsValid(const BlockOperation operation,
                                       const uint64_t logical_block_address,
                                       const uint8_t *const buffer,
                                       const uint64_t buffer_size_bytes,
                                       const uint64_t deadline_nanoseconds,
                                       uint64_t &logical_block_count) const noexcept {
    logical_block_count = OS_KERNEL_BLOCK_REQUEST_EMPTY_VALUE;
    if (deadline_nanoseconds == OS_KERNEL_BLOCK_REQUEST_EMPTY_VALUE) {
        return false;
    }
    if (operation == BlockOperation::Flush) {
        return this->geometry_.flush_supported &&
               logical_block_address == OS_KERNEL_BLOCK_REQUEST_EMPTY_VALUE && buffer == nullptr &&
               buffer_size_bytes == OS_KERNEL_BLOCK_REQUEST_EMPTY_VALUE;
    }
    if (operation != BlockOperation::Read && operation != BlockOperation::Write) {
        return false;
    }
    if (operation == BlockOperation::Write && !this->geometry_.write_supported) {
        return false;
    }
    if (buffer == nullptr || buffer_size_bytes == OS_KERNEL_BLOCK_REQUEST_EMPTY_VALUE ||
        buffer_size_bytes % this->geometry_.logical_block_size_bytes !=
            OS_KERNEL_BLOCK_REQUEST_EMPTY_VALUE) {
        return false;
    }
    logical_block_count = buffer_size_bytes / this->geometry_.logical_block_size_bytes;
    return logical_block_count <= this->geometry_.maximum_transfer_block_count &&
           logical_block_address < this->geometry_.logical_block_count &&
           logical_block_count <= this->geometry_.logical_block_count - logical_block_address;
}

BlockRequest *BlockRequestQueue::Find(const uint64_t request_identifier) noexcept {
    if (request_identifier == OS_KERNEL_BLOCK_REQUEST_EMPTY_VALUE) {
        return nullptr;
    }
    for (uint64_t request_index = OS_KERNEL_BLOCK_REQUEST_EMPTY_VALUE;
         request_index < this->capacity_; ++request_index) {
        if (this->storage_[request_index].state != BlockRequestState::Unused &&
            this->storage_[request_index].identifier == request_identifier) {
            return &this->storage_[request_index];
        }
    }
    return nullptr;
}

const BlockRequest *
BlockRequestQueue::Find(const uint64_t request_identifier) const noexcept {
    if (request_identifier == OS_KERNEL_BLOCK_REQUEST_EMPTY_VALUE) {
        return nullptr;
    }
    for (uint64_t request_index = OS_KERNEL_BLOCK_REQUEST_EMPTY_VALUE;
         request_index < this->capacity_; ++request_index) {
        if (this->storage_[request_index].state != BlockRequestState::Unused &&
            this->storage_[request_index].identifier == request_identifier) {
            return &this->storage_[request_index];
        }
    }
    return nullptr;
}

bool BlockRequestQueue::FindFreeIndex(uint64_t &request_index) const noexcept {
    request_index = OS_KERNEL_BLOCK_REQUEST_INVALID_INDEX;
    for (uint64_t candidate_index = OS_KERNEL_BLOCK_REQUEST_EMPTY_VALUE;
         candidate_index < this->capacity_; ++candidate_index) {
        if (this->storage_[candidate_index].state == BlockRequestState::Unused) {
            request_index = candidate_index;
            return true;
        }
    }
    return false;
}

uint64_t BlockRequestQueue::IndexOf(const BlockRequest &request) const noexcept {
    const uint64_t request_address = reinterpret_cast<uint64_t>(&request);
    const uint64_t storage_address = reinterpret_cast<uint64_t>(this->storage_);
    if (request_address < storage_address) {
        return OS_KERNEL_BLOCK_REQUEST_INVALID_INDEX;
    }
    const uint64_t byte_offset = request_address - storage_address;
    if (byte_offset % sizeof(BlockRequest) != OS_KERNEL_BLOCK_REQUEST_EMPTY_VALUE) {
        return OS_KERNEL_BLOCK_REQUEST_INVALID_INDEX;
    }
    const uint64_t request_index = byte_offset / sizeof(BlockRequest);
    return request_index < this->capacity_ ? request_index
                                           : OS_KERNEL_BLOCK_REQUEST_INVALID_INDEX;
}

void BlockRequestQueue::AppendQueuedIndex(const uint64_t request_index) noexcept {
    if (this->queue_tail_index_ == OS_KERNEL_BLOCK_REQUEST_INVALID_INDEX) {
        this->queue_head_index_ = request_index;
        this->queue_tail_index_ = request_index;
        return;
    }
    this->storage_[this->queue_tail_index_].next_queue_index = request_index;
    this->queue_tail_index_ = request_index;
}

bool BlockRequestQueue::RemoveQueuedIndex(const uint64_t request_index) noexcept {
    uint64_t previous_index = OS_KERNEL_BLOCK_REQUEST_INVALID_INDEX;
    uint64_t current_index = this->queue_head_index_;
    while (current_index != OS_KERNEL_BLOCK_REQUEST_INVALID_INDEX) {
        if (current_index >= this->capacity_) {
            return false;
        }
        if (current_index == request_index) {
            const uint64_t next_index = this->storage_[current_index].next_queue_index;
            if (previous_index == OS_KERNEL_BLOCK_REQUEST_INVALID_INDEX) {
                this->queue_head_index_ = next_index;
            } else {
                this->storage_[previous_index].next_queue_index = next_index;
            }
            if (this->queue_tail_index_ == current_index) {
                this->queue_tail_index_ = previous_index;
            }
            this->storage_[current_index].next_queue_index =
                OS_KERNEL_BLOCK_REQUEST_INVALID_INDEX;
            return true;
        }
        previous_index = current_index;
        current_index = this->storage_[current_index].next_queue_index;
    }
    return false;
}

void BlockRequestQueue::RecordResolution(const BlockRequestResult result) noexcept {
    if (result == BlockRequestResult::Succeeded) {
        ++this->statistics_.successful_completion_count;
    } else if (result == BlockRequestResult::DeviceError) {
        ++this->statistics_.device_error_completion_count;
    } else if (result == BlockRequestResult::TimedOut) {
        ++this->statistics_.timeout_completion_count;
    } else if (result == BlockRequestResult::Cancelled) {
        ++this->statistics_.cancellation_count;
    }
}

}

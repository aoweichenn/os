#include "os/kernel/ipc/pipe_manager.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_PIPE_MANAGER_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_PIPE_MANAGER_INVALID_INDEX = UINT64_MAX;
constexpr uint64_t OS_KERNEL_PIPE_MANAGER_COUNTER_INCREMENT = 1ULL;

[[nodiscard]] uint64_t Maximum(const uint64_t left, const uint64_t right) noexcept {
    return left > right ? left : right;
}

}

PipeManagerStatus PipeManager::Initialize(const PipePageAllocator &page_allocator,
                                          const uint64_t capacity) noexcept {
    if (this->initialized_) {
        return PipeManagerStatus::AlreadyInitialized;
    }
    if (page_allocator.allocate_page == nullptr || page_allocator.release_page == nullptr ||
        capacity == OS_KERNEL_PIPE_MANAGER_EMPTY_VALUE ||
        capacity > OS_KERNEL_PIPE_MANAGER_MAXIMUM_CAPACITY) {
        return PipeManagerStatus::InvalidConfiguration;
    }
    this->page_allocator_ = page_allocator;
    this->lock_ = SpinLock{};
    this->statistics_ = PipeManagerStatistics{
        .capacity = capacity,
        .active_pipe_count = OS_KERNEL_PIPE_MANAGER_EMPTY_VALUE,
        .peak_active_pipe_count = OS_KERNEL_PIPE_MANAGER_EMPTY_VALUE,
        .creation_count = OS_KERNEL_PIPE_MANAGER_EMPTY_VALUE,
        .release_count = OS_KERNEL_PIPE_MANAGER_EMPTY_VALUE,
        .capacity_rejection_count = OS_KERNEL_PIPE_MANAGER_EMPTY_VALUE,
    };
    for (uint64_t pipe_index = OS_KERNEL_PIPE_MANAGER_EMPTY_VALUE;
         pipe_index < OS_KERNEL_PIPE_MANAGER_MAXIMUM_CAPACITY; ++pipe_index) {
        this->active_[pipe_index] = false;
    }
    this->initialized_ = true;
    return PipeManagerStatus::Succeeded;
}

PipeManagerStatus PipeManager::Create(Pipe *&pipe) noexcept {
    pipe = nullptr;
    if (!this->initialized_) {
        return PipeManagerStatus::NotInitialized;
    }
    this->lock_.Lock();
    uint64_t available_index = OS_KERNEL_PIPE_MANAGER_INVALID_INDEX;
    for (uint64_t pipe_index = OS_KERNEL_PIPE_MANAGER_EMPTY_VALUE;
         pipe_index < this->statistics_.capacity; ++pipe_index) {
        if (!this->active_[pipe_index]) {
            available_index = pipe_index;
            break;
        }
    }
    if (available_index == OS_KERNEL_PIPE_MANAGER_INVALID_INDEX) {
        ++this->statistics_.capacity_rejection_count;
        this->lock_.Unlock();
        return PipeManagerStatus::CapacityExhausted;
    }
    this->active_[available_index] = true;
    ++this->statistics_.active_pipe_count;
    ++this->statistics_.creation_count;
    this->statistics_.peak_active_pipe_count =
        Maximum(this->statistics_.peak_active_pipe_count, this->statistics_.active_pipe_count);
    this->lock_.Unlock();

    Pipe &selected_pipe = this->pipes_[available_index];
    const PipeStatus initialize_status =
        selected_pipe.Initialize(this->page_allocator_, OS_KERNEL_PIPE_DYNAMIC_CAPACITY_BYTES);
    if (initialize_status != PipeStatus::Succeeded) {
        this->lock_.Lock();
        this->active_[available_index] = false;
        --this->statistics_.active_pipe_count;
        ++this->statistics_.release_count;
        this->lock_.Unlock();
        return PipeManagerStatus::EndpointFailure;
    }
    pipe = &selected_pipe;
    return PipeManagerStatus::Succeeded;
}

PipeManagerStatus PipeManager::CloseReader(Pipe &pipe) noexcept {
    if (!this->initialized_ ||
        this->FindPipeIndex(pipe) == OS_KERNEL_PIPE_MANAGER_INVALID_INDEX) {
        return PipeManagerStatus::InvalidPipe;
    }
    const PipeStatus close_status = pipe.CloseReader();
    if (close_status != PipeStatus::Succeeded) {
        return PipeManagerStatus::EndpointFailure;
    }
    return this->ReleaseIfClosed(pipe);
}

PipeManagerStatus PipeManager::CloseWriter(Pipe &pipe) noexcept {
    if (!this->initialized_ ||
        this->FindPipeIndex(pipe) == OS_KERNEL_PIPE_MANAGER_INVALID_INDEX) {
        return PipeManagerStatus::InvalidPipe;
    }
    const PipeStatus close_status = pipe.CloseWriter();
    if (close_status != PipeStatus::Succeeded) {
        return PipeManagerStatus::EndpointFailure;
    }
    return this->ReleaseIfClosed(pipe);
}

PipeManagerStatus PipeManager::Validate() noexcept {
    if (!this->initialized_) {
        return PipeManagerStatus::NotInitialized;
    }
    this->lock_.Lock();
    uint64_t observed_active_count = OS_KERNEL_PIPE_MANAGER_EMPTY_VALUE;
    for (uint64_t pipe_index = OS_KERNEL_PIPE_MANAGER_EMPTY_VALUE;
         pipe_index < OS_KERNEL_PIPE_MANAGER_MAXIMUM_CAPACITY; ++pipe_index) {
        if (pipe_index >= this->statistics_.capacity && this->active_[pipe_index]) {
            this->lock_.Unlock();
            return PipeManagerStatus::CorruptedState;
        }
        if (this->active_[pipe_index]) {
            ++observed_active_count;
        }
    }
    const bool valid =
        observed_active_count == this->statistics_.active_pipe_count &&
        this->statistics_.peak_active_pipe_count >= this->statistics_.active_pipe_count &&
        this->statistics_.creation_count >= this->statistics_.release_count &&
        this->statistics_.creation_count - this->statistics_.release_count ==
            this->statistics_.active_pipe_count;
    this->lock_.Unlock();
    return valid ? PipeManagerStatus::Succeeded : PipeManagerStatus::CorruptedState;
}

PipeManagerStatistics PipeManager::Statistics() noexcept {
    if (!this->initialized_) {
        return this->statistics_;
    }
    SpinLockGuard guard{this->lock_};
    return this->statistics_;
}

uint64_t PipeManager::FindPipeIndex(const Pipe &pipe) const noexcept {
    for (uint64_t pipe_index = OS_KERNEL_PIPE_MANAGER_EMPTY_VALUE;
         pipe_index < this->statistics_.capacity; ++pipe_index) {
        if (&this->pipes_[pipe_index] == &pipe) {
            return pipe_index;
        }
    }
    return OS_KERNEL_PIPE_MANAGER_INVALID_INDEX;
}

PipeManagerStatus PipeManager::ReleaseIfClosed(Pipe &pipe) noexcept {
    if (!pipe.IsFullyClosed()) {
        return PipeManagerStatus::Succeeded;
    }
    const uint64_t pipe_index = this->FindPipeIndex(pipe);
    if (pipe_index == OS_KERNEL_PIPE_MANAGER_INVALID_INDEX) {
        return PipeManagerStatus::InvalidPipe;
    }
    this->lock_.Lock();
    if (!this->active_[pipe_index] ||
        this->statistics_.active_pipe_count == OS_KERNEL_PIPE_MANAGER_EMPTY_VALUE) {
        this->lock_.Unlock();
        return PipeManagerStatus::CorruptedState;
    }
    this->active_[pipe_index] = false;
    --this->statistics_.active_pipe_count;
    this->statistics_.release_count += OS_KERNEL_PIPE_MANAGER_COUNTER_INCREMENT;
    this->lock_.Unlock();
    return PipeManagerStatus::Succeeded;
}

}

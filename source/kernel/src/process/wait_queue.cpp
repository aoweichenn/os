#include "os/kernel/process/wait_queue.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_WAIT_QUEUE_EMPTY_VALUE = 0ULL;

}

WaitQueueStatus WaitQueue::Initialize(const WaitQueueId queue_id) noexcept {
    if (this->initialized_) {
        return WaitQueueStatus::AlreadyInitialized;
    }
    if (queue_id.value == OS_KERNEL_WAIT_QUEUE_EMPTY_VALUE) {
        return WaitQueueStatus::InvalidIdentifier;
    }
    this->queue_id_ = queue_id;
    this->head_thread_index_ = OS_KERNEL_WAIT_QUEUE_INVALID_THREAD_INDEX;
    this->tail_thread_index_ = OS_KERNEL_WAIT_QUEUE_INVALID_THREAD_INDEX;
    this->waiting_thread_count_ = OS_KERNEL_WAIT_QUEUE_EMPTY_VALUE;
    this->enqueue_count_ = OS_KERNEL_WAIT_QUEUE_EMPTY_VALUE;
    this->wake_count_ = OS_KERNEL_WAIT_QUEUE_EMPTY_VALUE;
    this->close_count_ = OS_KERNEL_WAIT_QUEUE_EMPTY_VALUE;
    this->closed_ = false;
    this->initialized_ = true;
    return WaitQueueStatus::Succeeded;
}

WaitQueueStatus WaitQueue::Reset() noexcept {
    if (!this->initialized_) {
        return WaitQueueStatus::NotInitialized;
    }
    if (this->waiting_thread_count_ != OS_KERNEL_WAIT_QUEUE_EMPTY_VALUE) {
        return WaitQueueStatus::WaitersRemain;
    }
    this->queue_id_ = WaitQueueId{};
    this->head_thread_index_ = OS_KERNEL_WAIT_QUEUE_INVALID_THREAD_INDEX;
    this->tail_thread_index_ = OS_KERNEL_WAIT_QUEUE_INVALID_THREAD_INDEX;
    this->waiting_thread_count_ = OS_KERNEL_WAIT_QUEUE_EMPTY_VALUE;
    this->enqueue_count_ = OS_KERNEL_WAIT_QUEUE_EMPTY_VALUE;
    this->wake_count_ = OS_KERNEL_WAIT_QUEUE_EMPTY_VALUE;
    this->close_count_ = OS_KERNEL_WAIT_QUEUE_EMPTY_VALUE;
    this->closed_ = false;
    this->initialized_ = false;
    return WaitQueueStatus::Succeeded;
}

WaitQueueStatistics WaitQueue::Statistics() const noexcept {
    return WaitQueueStatistics{
        .queue_id = this->queue_id_,
        .waiting_thread_count = this->waiting_thread_count_,
        .enqueue_count = this->enqueue_count_,
        .wake_count = this->wake_count_,
        .close_count = this->close_count_,
        .closed = this->closed_,
    };
}

bool WaitQueue::IsInitialized() const noexcept { return this->initialized_; }

bool WaitQueue::IsClosed() const noexcept { return this->closed_; }

}

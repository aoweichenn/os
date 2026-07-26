#include "os/kernel/sync/mutex.hpp"

#include "os/kernel/sync/spin_lock.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_MUTEX_EMPTY_IDENTIFIER = 0ULL;
constexpr uint64_t OS_KERNEL_MUTEX_EMPTY_COUNT = 0ULL;
constexpr uint64_t OS_KERNEL_MUTEX_COUNTER_INCREMENT = 1ULL;

}

MutexStatus Mutex::Initialize(const WaitQueueId wait_queue_id) noexcept {
    if (this->initialized_) {
        return MutexStatus::AlreadyInitialized;
    }
    if (this->wait_queue_.Initialize(wait_queue_id) !=
        WaitQueueStatus::Succeeded) {
        return MutexStatus::WaitQueueFailure;
    }
    this->owner_thread_id_ = ThreadId{};
    this->acquisition_count_ = OS_KERNEL_MUTEX_EMPTY_COUNT;
    this->contention_count_ = OS_KERNEL_MUTEX_EMPTY_COUNT;
    this->handoff_count_ = OS_KERNEL_MUTEX_EMPTY_COUNT;
    this->unlock_count_ = OS_KERNEL_MUTEX_EMPTY_COUNT;
    this->owned_ = false;
    this->handoff_pending_ = false;
    this->initialized_ = true;
    return MutexStatus::Succeeded;
}

MutexStatus Mutex::TryLock(const ThreadId thread_id) noexcept {
    if (!this->initialized_) {
        return MutexStatus::NotInitialized;
    }
    if (thread_id.value == OS_KERNEL_MUTEX_EMPTY_IDENTIFIER) {
        return MutexStatus::InvalidOwner;
    }
    if (!this->owned_) {
        this->owner_thread_id_ = thread_id;
        this->owned_ = true;
        this->handoff_pending_ = false;
        this->acquisition_count_ += OS_KERNEL_MUTEX_COUNTER_INCREMENT;
        return MutexStatus::Succeeded;
    }
    if (this->owner_thread_id_.value == thread_id.value) {
        if (this->handoff_pending_) {
            this->handoff_pending_ = false;
            this->acquisition_count_ += OS_KERNEL_MUTEX_COUNTER_INCREMENT;
            return MutexStatus::Succeeded;
        }
        return MutexStatus::AlreadyOwned;
    }
    return MutexStatus::WouldBlock;
}

MutexStatus Mutex::Lock(ThreadScheduler &scheduler, const ThreadId thread_id,
                        ThreadSchedulingDecision &decision) noexcept {
    if (CurrentSpinLockDepth() != OS_KERNEL_MUTEX_EMPTY_COUNT) {
        return MutexStatus::SpinLockHeld;
    }
    const MutexStatus immediate_status = this->TryLock(thread_id);
    if (immediate_status != MutexStatus::WouldBlock) {
        return immediate_status;
    }
    if (scheduler.BlockCurrentThread(this->wait_queue_,
                                     WaitCondition::MutexAvailable,
                                     decision) !=
        ThreadSchedulerStatus::Succeeded) {
        return MutexStatus::SchedulerFailure;
    }
    this->contention_count_ += OS_KERNEL_MUTEX_COUNTER_INCREMENT;
    return MutexStatus::Blocked;
}

MutexStatus Mutex::Unlock(ThreadScheduler &scheduler, const ThreadId thread_id,
                          uint64_t &woken_thread_index) noexcept {
    woken_thread_index = OS_KERNEL_THREAD_INVALID_INDEX;
    if (!this->initialized_) {
        return MutexStatus::NotInitialized;
    }
    if (CurrentSpinLockDepth() != OS_KERNEL_MUTEX_EMPTY_COUNT) {
        return MutexStatus::SpinLockHeld;
    }
    if (!this->owned_ || this->owner_thread_id_.value != thread_id.value ||
        this->handoff_pending_) {
        return MutexStatus::NotOwner;
    }

    bool wake_won = false;
    const ThreadSchedulerStatus wake_status =
        scheduler.WakeOne(this->wait_queue_, WakeReason::ConditionSatisfied,
                          woken_thread_index, wake_won);
    if (wake_status != ThreadSchedulerStatus::Succeeded) {
        return MutexStatus::SchedulerFailure;
    }
    this->unlock_count_ += OS_KERNEL_MUTEX_COUNTER_INCREMENT;
    if (!wake_won) {
        this->owner_thread_id_ = ThreadId{};
        this->owned_ = false;
        this->handoff_pending_ = false;
        return MutexStatus::Succeeded;
    }

    ThreadEntry woken_thread{};
    if (scheduler.ReadThread(woken_thread_index, woken_thread) !=
            ThreadSchedulerStatus::Succeeded ||
        woken_thread.thread_id.value == OS_KERNEL_MUTEX_EMPTY_IDENTIFIER) {
        return MutexStatus::SchedulerFailure;
    }
    this->owner_thread_id_ = woken_thread.thread_id;
    this->owned_ = true;
    this->handoff_pending_ = true;
    this->handoff_count_ += OS_KERNEL_MUTEX_COUNTER_INCREMENT;
    return MutexStatus::Succeeded;
}

MutexStatus Mutex::Reset() noexcept {
    if (!this->initialized_) {
        return MutexStatus::NotInitialized;
    }
    if (this->owned_ ||
        this->wait_queue_.Statistics().waiting_thread_count !=
            OS_KERNEL_MUTEX_EMPTY_COUNT) {
        return MutexStatus::AlreadyOwned;
    }
    if (this->wait_queue_.Reset() != WaitQueueStatus::Succeeded) {
        return MutexStatus::WaitQueueFailure;
    }
    this->owner_thread_id_ = ThreadId{};
    this->acquisition_count_ = OS_KERNEL_MUTEX_EMPTY_COUNT;
    this->contention_count_ = OS_KERNEL_MUTEX_EMPTY_COUNT;
    this->handoff_count_ = OS_KERNEL_MUTEX_EMPTY_COUNT;
    this->unlock_count_ = OS_KERNEL_MUTEX_EMPTY_COUNT;
    this->owned_ = false;
    this->handoff_pending_ = false;
    this->initialized_ = false;
    return MutexStatus::Succeeded;
}

bool Mutex::IsOwnedBy(const ThreadId thread_id) const noexcept {
    return this->initialized_ && this->owned_ &&
           this->owner_thread_id_.value == thread_id.value;
}

MutexStatistics Mutex::Statistics() const noexcept {
    const WaitQueueStatistics wait_statistics = this->wait_queue_.Statistics();
    return MutexStatistics{
        .owner_thread_id = this->owner_thread_id_,
        .acquisition_count = this->acquisition_count_,
        .contention_count = this->contention_count_,
        .handoff_count = this->handoff_count_,
        .unlock_count = this->unlock_count_,
        .waiting_thread_count = wait_statistics.waiting_thread_count,
        .owned = this->owned_,
    };
}

const WaitQueue &Mutex::GetWaitQueue() const noexcept {
    return this->wait_queue_;
}

}

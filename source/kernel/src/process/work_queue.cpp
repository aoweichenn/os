#include <os/kernel/process/work_queue.hpp>

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_WORK_QUEUE_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_WORK_QUEUE_FIRST_GENERATION = 1ULL;
constexpr uint64_t OS_KERNEL_WORK_QUEUE_FIRST_SEQUENCE = 1ULL;
constexpr uint64_t OS_KERNEL_WORK_QUEUE_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_KERNEL_WORK_QUEUE_FIRST_INDEX = 0ULL;
constexpr uint64_t OS_KERNEL_WORK_QUEUE_HEAP_BRANCHING_FACTOR = 2ULL;

[[nodiscard]] WorkQueueEntry EmptyEntry(const uint64_t generation) noexcept {
    return WorkQueueEntry{
        .generation = generation,
        .state = WorkState::Free,
        .operation = nullptr,
        .context = nullptr,
        .deadline_nanoseconds = OS_KERNEL_WORK_QUEUE_EMPTY_VALUE,
        .enqueue_sequence = OS_KERNEL_WORK_QUEUE_EMPTY_VALUE,
        .delayed_heap_index = OS_KERNEL_WORK_QUEUE_INVALID_INDEX,
        .previous_ready_index = OS_KERNEL_WORK_QUEUE_INVALID_INDEX,
        .next_ready_index = OS_KERNEL_WORK_QUEUE_INVALID_INDEX,
    };
}

[[nodiscard]] WorkExecution EmptyExecution() noexcept {
    return WorkExecution{
        .handle =
            WorkHandle{
                .slot_index = OS_KERNEL_WORK_QUEUE_INVALID_INDEX,
                .generation = OS_KERNEL_WORK_QUEUE_EMPTY_VALUE,
            },
        .operation = nullptr,
        .context = nullptr,
    };
}

}

WorkQueueStatus WorkQueue::Initialize(WorkQueueEntry *const entry_storage,
                                      uint64_t *const delayed_heap_storage,
                                      const uint64_t capacity) noexcept {
    if (this->initialized_) {
        return WorkQueueStatus::AlreadyInitialized;
    }
    if (entry_storage == nullptr) {
        return WorkQueueStatus::NullEntryStorage;
    }
    if (delayed_heap_storage == nullptr) {
        return WorkQueueStatus::NullDelayedHeapStorage;
    }
    if (capacity == OS_KERNEL_WORK_QUEUE_EMPTY_VALUE ||
        capacity > OS_KERNEL_WORK_QUEUE_CAPACITY_LIMIT) {
        return WorkQueueStatus::InvalidCapacity;
    }
    for (uint64_t slot_index = OS_KERNEL_WORK_QUEUE_FIRST_INDEX; slot_index < capacity;
         ++slot_index) {
        entry_storage[slot_index] = EmptyEntry(OS_KERNEL_WORK_QUEUE_EMPTY_VALUE);
        delayed_heap_storage[slot_index] = OS_KERNEL_WORK_QUEUE_INVALID_INDEX;
    }
    this->entries_ = entry_storage;
    this->delayed_heap_ = delayed_heap_storage;
    this->capacity_ = capacity;
    this->delayed_count_ = OS_KERNEL_WORK_QUEUE_EMPTY_VALUE;
    this->ready_head_index_ = OS_KERNEL_WORK_QUEUE_INVALID_INDEX;
    this->ready_tail_index_ = OS_KERNEL_WORK_QUEUE_INVALID_INDEX;
    this->next_enqueue_sequence_ = OS_KERNEL_WORK_QUEUE_FIRST_SEQUENCE;
    this->cumulative_statistics_ = WorkQueueStatistics{};
    this->cumulative_statistics_.capacity = capacity;
    this->draining_ = false;
    this->initialized_ = true;
    return WorkQueueStatus::Succeeded;
}

WorkQueueStatus WorkQueue::Register(const WorkOperation operation, void *const context,
                                    WorkHandle &handle) noexcept {
    handle = WorkHandle{
        .slot_index = OS_KERNEL_WORK_QUEUE_INVALID_INDEX,
        .generation = OS_KERNEL_WORK_QUEUE_EMPTY_VALUE,
    };
    SpinLockGuard guard{this->lock_};
    if (!this->IsInitialized()) {
        return WorkQueueStatus::NotInitialized;
    }
    if (operation == nullptr) {
        return WorkQueueStatus::InvalidOperation;
    }
    if (this->draining_) {
        if (this->cumulative_statistics_.drain_rejection_count == UINT64_MAX) {
            return WorkQueueStatus::CounterOverflow;
        }
        ++this->cumulative_statistics_.drain_rejection_count;
        return WorkQueueStatus::DrainInProgress;
    }
    uint64_t free_slot_index = OS_KERNEL_WORK_QUEUE_INVALID_INDEX;
    bool generation_exhausted = false;
    for (uint64_t slot_index = OS_KERNEL_WORK_QUEUE_FIRST_INDEX; slot_index < this->capacity_;
         ++slot_index) {
        if (this->entries_[slot_index].state != WorkState::Free) {
            continue;
        }
        if (this->entries_[slot_index].generation == UINT64_MAX) {
            generation_exhausted = true;
        } else {
            free_slot_index = slot_index;
            break;
        }
    }
    if (free_slot_index == OS_KERNEL_WORK_QUEUE_INVALID_INDEX) {
        if (generation_exhausted) {
            return WorkQueueStatus::IdentifierExhausted;
        }
        if (this->cumulative_statistics_.capacity_rejection_count == UINT64_MAX) {
            return WorkQueueStatus::CounterOverflow;
        }
        ++this->cumulative_statistics_.capacity_rejection_count;
        return WorkQueueStatus::CapacityExhausted;
    }
    if (this->cumulative_statistics_.registration_count == UINT64_MAX) {
        return WorkQueueStatus::CounterOverflow;
    }
    WorkQueueEntry &entry = this->entries_[free_slot_index];
    const uint64_t generation = entry.generation == OS_KERNEL_WORK_QUEUE_EMPTY_VALUE
                                    ? OS_KERNEL_WORK_QUEUE_FIRST_GENERATION
                                    : entry.generation + OS_KERNEL_WORK_QUEUE_COUNTER_INCREMENT;
    entry = WorkQueueEntry{
        .generation = generation,
        .state = WorkState::Idle,
        .operation = operation,
        .context = context,
        .deadline_nanoseconds = OS_KERNEL_WORK_QUEUE_EMPTY_VALUE,
        .enqueue_sequence = OS_KERNEL_WORK_QUEUE_EMPTY_VALUE,
        .delayed_heap_index = OS_KERNEL_WORK_QUEUE_INVALID_INDEX,
        .previous_ready_index = OS_KERNEL_WORK_QUEUE_INVALID_INDEX,
        .next_ready_index = OS_KERNEL_WORK_QUEUE_INVALID_INDEX,
    };
    ++this->cumulative_statistics_.registration_count;
    const WorkQueueStatistics statistics = this->StatisticsLocked();
    if (statistics.registered_count > this->cumulative_statistics_.peak_registered_count) {
        this->cumulative_statistics_.peak_registered_count = statistics.registered_count;
    }
    handle = WorkHandle{
        .slot_index = free_slot_index,
        .generation = generation,
    };
    return WorkQueueStatus::Succeeded;
}

WorkQueueStatus WorkQueue::Release(const WorkHandle handle) noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->IsInitialized()) {
        return WorkQueueStatus::NotInitialized;
    }
    if (!this->HandleIsValid(handle)) {
        if (handle.slot_index < this->capacity_ &&
            this->cumulative_statistics_.stale_handle_rejection_count != UINT64_MAX) {
            ++this->cumulative_statistics_.stale_handle_rejection_count;
            return WorkQueueStatus::StaleHandle;
        }
        return WorkQueueStatus::InvalidHandle;
    }
    WorkQueueEntry &entry = this->entries_[handle.slot_index];
    if (entry.state != WorkState::Idle && entry.state != WorkState::Completed &&
        entry.state != WorkState::Cancelled) {
        return WorkQueueStatus::InvalidState;
    }
    if (this->cumulative_statistics_.release_count == UINT64_MAX) {
        return WorkQueueStatus::CounterOverflow;
    }
    const uint64_t generation = entry.generation;
    entry = EmptyEntry(generation);
    ++this->cumulative_statistics_.release_count;
    return WorkQueueStatus::Succeeded;
}

WorkQueueStatus WorkQueue::Queue(const WorkHandle handle) noexcept {
    SpinLockGuard guard{this->lock_};
    return this->QueueLocked(handle, false, OS_KERNEL_WORK_QUEUE_EMPTY_VALUE);
}

WorkQueueStatus WorkQueue::QueueDelayed(const WorkHandle handle,
                                        const uint64_t deadline_nanoseconds) noexcept {
    SpinLockGuard guard{this->lock_};
    return this->QueueLocked(handle, true, deadline_nanoseconds);
}

WorkQueueStatus WorkQueue::Cancel(const WorkHandle handle) noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->IsInitialized()) {
        return WorkQueueStatus::NotInitialized;
    }
    if (!this->HandleIsValid(handle)) {
        return WorkQueueStatus::StaleHandle;
    }
    WorkQueueEntry &entry = this->entries_[handle.slot_index];
    if (entry.state == WorkState::Running) {
        return WorkQueueStatus::AlreadyRunning;
    }
    if (entry.state != WorkState::Queued && entry.state != WorkState::Delayed) {
        return WorkQueueStatus::NotPending;
    }
    if (this->cumulative_statistics_.cancellation_count == UINT64_MAX) {
        return WorkQueueStatus::CounterOverflow;
    }
    if (entry.state == WorkState::Queued) {
        this->RemoveReady(handle.slot_index);
    } else {
        this->RemoveDelayed(entry.delayed_heap_index);
    }
    entry.state = WorkState::Cancelled;
    entry.deadline_nanoseconds = OS_KERNEL_WORK_QUEUE_EMPTY_VALUE;
    entry.enqueue_sequence = OS_KERNEL_WORK_QUEUE_EMPTY_VALUE;
    ++this->cumulative_statistics_.cancellation_count;
    return WorkQueueStatus::Succeeded;
}

WorkQueueStatus WorkQueue::Reset(const WorkHandle handle) noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->IsInitialized()) {
        return WorkQueueStatus::NotInitialized;
    }
    if (!this->HandleIsValid(handle)) {
        return WorkQueueStatus::StaleHandle;
    }
    WorkQueueEntry &entry = this->entries_[handle.slot_index];
    if (entry.state != WorkState::Completed && entry.state != WorkState::Cancelled) {
        return WorkQueueStatus::InvalidState;
    }
    if (this->cumulative_statistics_.reset_count == UINT64_MAX) {
        return WorkQueueStatus::CounterOverflow;
    }
    entry.state = WorkState::Idle;
    entry.deadline_nanoseconds = OS_KERNEL_WORK_QUEUE_EMPTY_VALUE;
    entry.enqueue_sequence = OS_KERNEL_WORK_QUEUE_EMPTY_VALUE;
    ++this->cumulative_statistics_.reset_count;
    return WorkQueueStatus::Succeeded;
}

WorkQueueStatus WorkQueue::AcquireNext(const uint64_t now_nanoseconds,
                                       WorkExecution &execution) noexcept {
    execution = EmptyExecution();
    SpinLockGuard guard{this->lock_};
    if (!this->IsInitialized()) {
        return WorkQueueStatus::NotInitialized;
    }
    const WorkQueueStatus promotion_status = this->PromoteDue(now_nanoseconds);
    if (promotion_status != WorkQueueStatus::Succeeded) {
        return promotion_status;
    }
    if (this->ready_head_index_ == OS_KERNEL_WORK_QUEUE_INVALID_INDEX) {
        return WorkQueueStatus::NoReadyWork;
    }
    if (this->cumulative_statistics_.acquisition_count == UINT64_MAX) {
        return WorkQueueStatus::CounterOverflow;
    }
    const uint64_t slot_index = this->ready_head_index_;
    WorkQueueEntry &entry = this->entries_[slot_index];
    if (entry.state != WorkState::Queued || entry.operation == nullptr) {
        return WorkQueueStatus::CorruptedState;
    }
    this->RemoveReady(slot_index);
    entry.state = WorkState::Running;
    entry.deadline_nanoseconds = OS_KERNEL_WORK_QUEUE_EMPTY_VALUE;
    entry.enqueue_sequence = OS_KERNEL_WORK_QUEUE_EMPTY_VALUE;
    ++this->cumulative_statistics_.acquisition_count;
    const WorkQueueStatistics statistics = this->StatisticsLocked();
    if (statistics.running_count > this->cumulative_statistics_.peak_running_count) {
        this->cumulative_statistics_.peak_running_count = statistics.running_count;
    }
    execution = WorkExecution{
        .handle =
            WorkHandle{
                .slot_index = slot_index,
                .generation = entry.generation,
            },
        .operation = entry.operation,
        .context = entry.context,
    };
    return WorkQueueStatus::Succeeded;
}

WorkQueueStatus WorkQueue::NextDeadline(uint64_t &deadline_nanoseconds,
                                        bool &deadline_available) const noexcept {
    deadline_nanoseconds = OS_KERNEL_WORK_QUEUE_EMPTY_VALUE;
    deadline_available = false;
    SpinLockGuard guard{this->lock_};
    if (!this->IsInitialized()) {
        return WorkQueueStatus::NotInitialized;
    }
    if (this->delayed_count_ == OS_KERNEL_WORK_QUEUE_EMPTY_VALUE) {
        return WorkQueueStatus::Succeeded;
    }
    const uint64_t slot_index = this->delayed_heap_[OS_KERNEL_WORK_QUEUE_FIRST_INDEX];
    if (slot_index >= this->capacity_ || this->entries_[slot_index].state != WorkState::Delayed ||
        this->entries_[slot_index].deadline_nanoseconds == OS_KERNEL_WORK_QUEUE_EMPTY_VALUE) {
        return WorkQueueStatus::CorruptedState;
    }
    deadline_nanoseconds = this->entries_[slot_index].deadline_nanoseconds;
    deadline_available = true;
    return WorkQueueStatus::Succeeded;
}

WorkQueueStatus WorkQueue::Complete(const WorkHandle handle,
                                    const WorkExecutionResult result) noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->IsInitialized()) {
        return WorkQueueStatus::NotInitialized;
    }
    if (!this->HandleIsValid(handle)) {
        return WorkQueueStatus::StaleHandle;
    }
    if (result != WorkExecutionResult::Succeeded && result != WorkExecutionResult::Failed) {
        return WorkQueueStatus::InvalidState;
    }
    WorkQueueEntry &entry = this->entries_[handle.slot_index];
    if (entry.state != WorkState::Running) {
        return WorkQueueStatus::InvalidState;
    }
    if (this->cumulative_statistics_.completion_count == UINT64_MAX ||
        (result == WorkExecutionResult::Failed &&
         this->cumulative_statistics_.failed_execution_count == UINT64_MAX)) {
        return WorkQueueStatus::CounterOverflow;
    }
    entry.state = WorkState::Completed;
    ++this->cumulative_statistics_.completion_count;
    if (result == WorkExecutionResult::Failed) {
        ++this->cumulative_statistics_.failed_execution_count;
    }
    return WorkQueueStatus::Succeeded;
}

WorkQueueStatus WorkQueue::BeginDrain() noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->IsInitialized()) {
        return WorkQueueStatus::NotInitialized;
    }
    if (this->draining_) {
        return WorkQueueStatus::AlreadyDraining;
    }
    if (this->cumulative_statistics_.drain_begin_count == UINT64_MAX) {
        return WorkQueueStatus::CounterOverflow;
    }
    this->draining_ = true;
    ++this->cumulative_statistics_.drain_begin_count;
    return WorkQueueStatus::Succeeded;
}

WorkQueueStatus WorkQueue::EndDrain() noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->IsInitialized()) {
        return WorkQueueStatus::NotInitialized;
    }
    if (!this->draining_) {
        return WorkQueueStatus::InvalidState;
    }
    uint64_t pending_count = OS_KERNEL_WORK_QUEUE_EMPTY_VALUE;
    if (!this->PendingCount(pending_count)) {
        return WorkQueueStatus::CorruptedState;
    }
    if (pending_count != OS_KERNEL_WORK_QUEUE_EMPTY_VALUE) {
        return WorkQueueStatus::DrainIncomplete;
    }
    if (this->cumulative_statistics_.drain_end_count == UINT64_MAX) {
        return WorkQueueStatus::CounterOverflow;
    }
    this->draining_ = false;
    ++this->cumulative_statistics_.drain_end_count;
    return WorkQueueStatus::Succeeded;
}

bool WorkQueue::DrainComplete() const noexcept {
    SpinLockGuard guard{this->lock_};
    uint64_t pending_count = OS_KERNEL_WORK_QUEUE_EMPTY_VALUE;
    return this->IsInitialized() && this->PendingCount(pending_count) &&
           pending_count == OS_KERNEL_WORK_QUEUE_EMPTY_VALUE;
}

WorkQueueStatus WorkQueue::Read(const WorkHandle handle, WorkQueueEntry &entry) const noexcept {
    entry = EmptyEntry(OS_KERNEL_WORK_QUEUE_EMPTY_VALUE);
    SpinLockGuard guard{this->lock_};
    if (!this->IsInitialized()) {
        return WorkQueueStatus::NotInitialized;
    }
    if (!this->HandleIsValid(handle)) {
        return WorkQueueStatus::StaleHandle;
    }
    entry = this->entries_[handle.slot_index];
    return WorkQueueStatus::Succeeded;
}

WorkQueueStatistics WorkQueue::Statistics() const noexcept {
    SpinLockGuard guard{this->lock_};
    return this->StatisticsLocked();
}

WorkQueueStatus WorkQueue::Validate() const noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->IsInitialized()) {
        return WorkQueueStatus::NotInitialized;
    }
    bool ready_seen[OS_KERNEL_WORK_QUEUE_CAPACITY_LIMIT]{};
    bool delayed_seen[OS_KERNEL_WORK_QUEUE_CAPACITY_LIMIT]{};
    uint64_t ready_count = OS_KERNEL_WORK_QUEUE_EMPTY_VALUE;
    uint64_t previous_ready_index = OS_KERNEL_WORK_QUEUE_INVALID_INDEX;
    uint64_t ready_index = this->ready_head_index_;
    while (ready_index != OS_KERNEL_WORK_QUEUE_INVALID_INDEX) {
        if (ready_index >= this->capacity_ || ready_seen[ready_index] ||
            ready_count >= this->capacity_) {
            return WorkQueueStatus::CorruptedState;
        }
        const WorkQueueEntry &entry = this->entries_[ready_index];
        if (entry.state != WorkState::Queued ||
            entry.previous_ready_index != previous_ready_index || entry.operation == nullptr) {
            return WorkQueueStatus::CorruptedState;
        }
        ready_seen[ready_index] = true;
        ++ready_count;
        previous_ready_index = ready_index;
        ready_index = entry.next_ready_index;
    }
    if ((ready_count == OS_KERNEL_WORK_QUEUE_EMPTY_VALUE &&
         (this->ready_head_index_ != OS_KERNEL_WORK_QUEUE_INVALID_INDEX ||
          this->ready_tail_index_ != OS_KERNEL_WORK_QUEUE_INVALID_INDEX)) ||
        (ready_count != OS_KERNEL_WORK_QUEUE_EMPTY_VALUE &&
         previous_ready_index != this->ready_tail_index_)) {
        return WorkQueueStatus::CorruptedState;
    }
    for (uint64_t heap_index = OS_KERNEL_WORK_QUEUE_FIRST_INDEX; heap_index < this->delayed_count_;
         ++heap_index) {
        const uint64_t slot_index = this->delayed_heap_[heap_index];
        if (slot_index >= this->capacity_ || delayed_seen[slot_index]) {
            return WorkQueueStatus::CorruptedState;
        }
        const WorkQueueEntry &entry = this->entries_[slot_index];
        if (entry.state != WorkState::Delayed || entry.delayed_heap_index != heap_index ||
            entry.deadline_nanoseconds == OS_KERNEL_WORK_QUEUE_EMPTY_VALUE ||
            entry.operation == nullptr) {
            return WorkQueueStatus::CorruptedState;
        }
        delayed_seen[slot_index] = true;
        if (heap_index != OS_KERNEL_WORK_QUEUE_FIRST_INDEX) {
            const uint64_t parent_heap_index =
                (heap_index - OS_KERNEL_WORK_QUEUE_COUNTER_INCREMENT) /
                OS_KERNEL_WORK_QUEUE_HEAP_BRANCHING_FACTOR;
            if (this->DelayedLess(slot_index, this->delayed_heap_[parent_heap_index])) {
                return WorkQueueStatus::CorruptedState;
            }
        }
    }
    for (uint64_t heap_index = this->delayed_count_; heap_index < this->capacity_; ++heap_index) {
        if (this->delayed_heap_[heap_index] != OS_KERNEL_WORK_QUEUE_INVALID_INDEX) {
            return WorkQueueStatus::CorruptedState;
        }
    }
    const WorkQueueStatistics statistics = this->StatisticsLocked();
    for (uint64_t slot_index = OS_KERNEL_WORK_QUEUE_FIRST_INDEX; slot_index < this->capacity_;
         ++slot_index) {
        const WorkQueueEntry &entry = this->entries_[slot_index];
        if (entry.state == WorkState::Free) {
            if (entry.operation != nullptr || entry.context != nullptr || ready_seen[slot_index] ||
                delayed_seen[slot_index]) {
                return WorkQueueStatus::CorruptedState;
            }
            continue;
        }
        if (entry.generation == OS_KERNEL_WORK_QUEUE_EMPTY_VALUE || entry.operation == nullptr ||
            (entry.state == WorkState::Queued) != ready_seen[slot_index] ||
            (entry.state == WorkState::Delayed) != delayed_seen[slot_index]) {
            return WorkQueueStatus::CorruptedState;
        }
        if (entry.state != WorkState::Queued &&
            (entry.previous_ready_index != OS_KERNEL_WORK_QUEUE_INVALID_INDEX ||
             entry.next_ready_index != OS_KERNEL_WORK_QUEUE_INVALID_INDEX)) {
            return WorkQueueStatus::CorruptedState;
        }
        if (entry.state != WorkState::Delayed &&
            entry.delayed_heap_index != OS_KERNEL_WORK_QUEUE_INVALID_INDEX) {
            return WorkQueueStatus::CorruptedState;
        }
        const bool scheduling_fields_present =
            entry.deadline_nanoseconds != OS_KERNEL_WORK_QUEUE_EMPTY_VALUE ||
            entry.enqueue_sequence != OS_KERNEL_WORK_QUEUE_EMPTY_VALUE;
        if ((entry.state == WorkState::Delayed &&
             (entry.deadline_nanoseconds == OS_KERNEL_WORK_QUEUE_EMPTY_VALUE ||
              entry.enqueue_sequence == OS_KERNEL_WORK_QUEUE_EMPTY_VALUE)) ||
            (entry.state == WorkState::Queued &&
             (entry.deadline_nanoseconds != OS_KERNEL_WORK_QUEUE_EMPTY_VALUE ||
              entry.enqueue_sequence == OS_KERNEL_WORK_QUEUE_EMPTY_VALUE)) ||
            ((entry.state == WorkState::Idle || entry.state == WorkState::Running ||
              entry.state == WorkState::Completed || entry.state == WorkState::Cancelled) &&
             scheduling_fields_present)) {
            return WorkQueueStatus::CorruptedState;
        }
    }
    uint64_t pending_count = OS_KERNEL_WORK_QUEUE_EMPTY_VALUE;
    if (!this->PendingCount(pending_count) || ready_count != statistics.queued_count ||
        this->delayed_count_ != statistics.delayed_count ||
        statistics.registered_count != statistics.idle_count + statistics.delayed_count +
                                           statistics.queued_count + statistics.running_count +
                                           statistics.completed_count +
                                           statistics.cancelled_count ||
        statistics.registration_count != statistics.registered_count + statistics.release_count ||
        statistics.peak_registered_count < statistics.registered_count ||
        statistics.peak_pending_count < pending_count ||
        statistics.peak_running_count < statistics.running_count ||
        this->next_enqueue_sequence_ == OS_KERNEL_WORK_QUEUE_EMPTY_VALUE) {
        return WorkQueueStatus::CorruptedState;
    }
    return WorkQueueStatus::Succeeded;
}

bool WorkQueue::IsInitialized() const noexcept { return this->initialized_; }

bool WorkQueue::HandleIsValid(const WorkHandle handle) const noexcept {
    return handle.slot_index < this->capacity_ &&
           this->entries_[handle.slot_index].state != WorkState::Free &&
           this->entries_[handle.slot_index].generation == handle.generation;
}

bool WorkQueue::PendingCount(uint64_t &pending_count) const noexcept {
    pending_count = OS_KERNEL_WORK_QUEUE_EMPTY_VALUE;
    for (uint64_t slot_index = OS_KERNEL_WORK_QUEUE_FIRST_INDEX; slot_index < this->capacity_;
         ++slot_index) {
        const WorkState state = this->entries_[slot_index].state;
        if (state == WorkState::Delayed || state == WorkState::Queued ||
            state == WorkState::Running) {
            if (pending_count == UINT64_MAX) {
                return false;
            }
            ++pending_count;
        }
    }
    return true;
}

bool WorkQueue::DelayedLess(const uint64_t left_slot_index,
                            const uint64_t right_slot_index) const noexcept {
    const WorkQueueEntry &left = this->entries_[left_slot_index];
    const WorkQueueEntry &right = this->entries_[right_slot_index];
    return left.deadline_nanoseconds < right.deadline_nanoseconds ||
           (left.deadline_nanoseconds == right.deadline_nanoseconds &&
            left.enqueue_sequence < right.enqueue_sequence);
}

void WorkQueue::AppendReady(const uint64_t slot_index) noexcept {
    WorkQueueEntry &entry = this->entries_[slot_index];
    entry.previous_ready_index = this->ready_tail_index_;
    entry.next_ready_index = OS_KERNEL_WORK_QUEUE_INVALID_INDEX;
    if (this->ready_tail_index_ == OS_KERNEL_WORK_QUEUE_INVALID_INDEX) {
        this->ready_head_index_ = slot_index;
    } else {
        this->entries_[this->ready_tail_index_].next_ready_index = slot_index;
    }
    this->ready_tail_index_ = slot_index;
}

void WorkQueue::RemoveReady(const uint64_t slot_index) noexcept {
    WorkQueueEntry &entry = this->entries_[slot_index];
    if (entry.previous_ready_index == OS_KERNEL_WORK_QUEUE_INVALID_INDEX) {
        this->ready_head_index_ = entry.next_ready_index;
    } else {
        this->entries_[entry.previous_ready_index].next_ready_index = entry.next_ready_index;
    }
    if (entry.next_ready_index == OS_KERNEL_WORK_QUEUE_INVALID_INDEX) {
        this->ready_tail_index_ = entry.previous_ready_index;
    } else {
        this->entries_[entry.next_ready_index].previous_ready_index = entry.previous_ready_index;
    }
    entry.previous_ready_index = OS_KERNEL_WORK_QUEUE_INVALID_INDEX;
    entry.next_ready_index = OS_KERNEL_WORK_QUEUE_INVALID_INDEX;
}

void WorkQueue::SwapDelayed(const uint64_t left_heap_index,
                            const uint64_t right_heap_index) noexcept {
    const uint64_t left_slot_index = this->delayed_heap_[left_heap_index];
    const uint64_t right_slot_index = this->delayed_heap_[right_heap_index];
    this->delayed_heap_[left_heap_index] = right_slot_index;
    this->delayed_heap_[right_heap_index] = left_slot_index;
    this->entries_[left_slot_index].delayed_heap_index = right_heap_index;
    this->entries_[right_slot_index].delayed_heap_index = left_heap_index;
}

void WorkQueue::BubbleDelayedUp(uint64_t heap_index) noexcept {
    while (heap_index != OS_KERNEL_WORK_QUEUE_FIRST_INDEX) {
        const uint64_t parent_heap_index = (heap_index - OS_KERNEL_WORK_QUEUE_COUNTER_INCREMENT) /
                                           OS_KERNEL_WORK_QUEUE_HEAP_BRANCHING_FACTOR;
        if (!this->DelayedLess(this->delayed_heap_[heap_index],
                               this->delayed_heap_[parent_heap_index])) {
            return;
        }
        this->SwapDelayed(heap_index, parent_heap_index);
        heap_index = parent_heap_index;
    }
}

void WorkQueue::BubbleDelayedDown(uint64_t heap_index) noexcept {
    while (true) {
        const uint64_t left_heap_index = heap_index * OS_KERNEL_WORK_QUEUE_HEAP_BRANCHING_FACTOR +
                                         OS_KERNEL_WORK_QUEUE_COUNTER_INCREMENT;
        if (left_heap_index >= this->delayed_count_) {
            return;
        }
        const uint64_t right_heap_index = left_heap_index + OS_KERNEL_WORK_QUEUE_COUNTER_INCREMENT;
        uint64_t candidate_heap_index = left_heap_index;
        if (right_heap_index < this->delayed_count_ &&
            this->DelayedLess(this->delayed_heap_[right_heap_index],
                              this->delayed_heap_[left_heap_index])) {
            candidate_heap_index = right_heap_index;
        }
        if (!this->DelayedLess(this->delayed_heap_[candidate_heap_index],
                               this->delayed_heap_[heap_index])) {
            return;
        }
        this->SwapDelayed(heap_index, candidate_heap_index);
        heap_index = candidate_heap_index;
    }
}

void WorkQueue::InsertDelayed(const uint64_t slot_index) noexcept {
    const uint64_t heap_index = this->delayed_count_;
    this->delayed_heap_[heap_index] = slot_index;
    this->entries_[slot_index].delayed_heap_index = heap_index;
    ++this->delayed_count_;
    this->BubbleDelayedUp(heap_index);
}

void WorkQueue::RemoveDelayed(const uint64_t heap_index) noexcept {
    const uint64_t removed_slot_index = this->delayed_heap_[heap_index];
    --this->delayed_count_;
    if (heap_index != this->delayed_count_) {
        const uint64_t replacement_slot_index = this->delayed_heap_[this->delayed_count_];
        this->delayed_heap_[heap_index] = replacement_slot_index;
        this->entries_[replacement_slot_index].delayed_heap_index = heap_index;
        if (heap_index != OS_KERNEL_WORK_QUEUE_FIRST_INDEX) {
            const uint64_t parent_heap_index =
                (heap_index - OS_KERNEL_WORK_QUEUE_COUNTER_INCREMENT) /
                OS_KERNEL_WORK_QUEUE_HEAP_BRANCHING_FACTOR;
            if (this->DelayedLess(replacement_slot_index, this->delayed_heap_[parent_heap_index])) {
                this->BubbleDelayedUp(heap_index);
            } else {
                this->BubbleDelayedDown(heap_index);
            }
        } else {
            this->BubbleDelayedDown(heap_index);
        }
    }
    this->delayed_heap_[this->delayed_count_] = OS_KERNEL_WORK_QUEUE_INVALID_INDEX;
    this->entries_[removed_slot_index].delayed_heap_index = OS_KERNEL_WORK_QUEUE_INVALID_INDEX;
}

WorkQueueStatus WorkQueue::PromoteDue(const uint64_t now_nanoseconds) noexcept {
    uint64_t due_count = OS_KERNEL_WORK_QUEUE_EMPTY_VALUE;
    for (uint64_t heap_index = OS_KERNEL_WORK_QUEUE_FIRST_INDEX; heap_index < this->delayed_count_;
         ++heap_index) {
        if (this->entries_[this->delayed_heap_[heap_index]].deadline_nanoseconds <=
            now_nanoseconds) {
            ++due_count;
        }
    }
    if (this->cumulative_statistics_.delayed_promotion_count > UINT64_MAX - due_count) {
        return WorkQueueStatus::CounterOverflow;
    }
    while (this->delayed_count_ != OS_KERNEL_WORK_QUEUE_EMPTY_VALUE) {
        const uint64_t slot_index = this->delayed_heap_[OS_KERNEL_WORK_QUEUE_FIRST_INDEX];
        WorkQueueEntry &entry = this->entries_[slot_index];
        if (entry.deadline_nanoseconds > now_nanoseconds) {
            break;
        }
        this->RemoveDelayed(OS_KERNEL_WORK_QUEUE_FIRST_INDEX);
        entry.state = WorkState::Queued;
        entry.deadline_nanoseconds = OS_KERNEL_WORK_QUEUE_EMPTY_VALUE;
        this->AppendReady(slot_index);
        ++this->cumulative_statistics_.delayed_promotion_count;
    }
    return WorkQueueStatus::Succeeded;
}

WorkQueueStatus WorkQueue::QueueLocked(const WorkHandle handle, const bool delayed,
                                       const uint64_t deadline_nanoseconds) noexcept {
    if (!this->IsInitialized()) {
        return WorkQueueStatus::NotInitialized;
    }
    if (!this->HandleIsValid(handle)) {
        if (handle.slot_index < this->capacity_ &&
            this->cumulative_statistics_.stale_handle_rejection_count != UINT64_MAX) {
            ++this->cumulative_statistics_.stale_handle_rejection_count;
        }
        return WorkQueueStatus::StaleHandle;
    }
    if (delayed && deadline_nanoseconds == OS_KERNEL_WORK_QUEUE_EMPTY_VALUE) {
        return WorkQueueStatus::InvalidDeadline;
    }
    WorkQueueEntry &entry = this->entries_[handle.slot_index];
    if (entry.state == WorkState::Delayed && !delayed) {
        if (this->cumulative_statistics_.immediate_queue_count == UINT64_MAX ||
            this->cumulative_statistics_.expedited_queue_count == UINT64_MAX) {
            return WorkQueueStatus::CounterOverflow;
        }
        // 即时请求提升已排队的延迟工作；单队列 Worker 不应被旧 deadline 拖住。
        this->RemoveDelayed(entry.delayed_heap_index);
        entry.state = WorkState::Queued;
        entry.deadline_nanoseconds = OS_KERNEL_WORK_QUEUE_EMPTY_VALUE;
        this->AppendReady(handle.slot_index);
        ++this->cumulative_statistics_.immediate_queue_count;
        ++this->cumulative_statistics_.expedited_queue_count;
        return WorkQueueStatus::Succeeded;
    }
    if (entry.state == WorkState::Delayed || entry.state == WorkState::Queued ||
        entry.state == WorkState::Running) {
        if (this->cumulative_statistics_.coalesced_queue_count == UINT64_MAX) {
            return WorkQueueStatus::CounterOverflow;
        }
        ++this->cumulative_statistics_.coalesced_queue_count;
        return WorkQueueStatus::AlreadyPending;
    }
    if (entry.state != WorkState::Idle) {
        return WorkQueueStatus::InvalidState;
    }
    if (this->draining_) {
        if (this->cumulative_statistics_.drain_rejection_count == UINT64_MAX) {
            return WorkQueueStatus::CounterOverflow;
        }
        ++this->cumulative_statistics_.drain_rejection_count;
        return WorkQueueStatus::DrainInProgress;
    }
    if (this->next_enqueue_sequence_ == UINT64_MAX) {
        return WorkQueueStatus::IdentifierExhausted;
    }
    if ((delayed && this->cumulative_statistics_.delayed_queue_count == UINT64_MAX) ||
        (!delayed && this->cumulative_statistics_.immediate_queue_count == UINT64_MAX)) {
        return WorkQueueStatus::CounterOverflow;
    }
    entry.state = delayed ? WorkState::Delayed : WorkState::Queued;
    entry.deadline_nanoseconds = delayed ? deadline_nanoseconds : OS_KERNEL_WORK_QUEUE_EMPTY_VALUE;
    entry.enqueue_sequence = this->next_enqueue_sequence_;
    ++this->next_enqueue_sequence_;
    if (delayed) {
        this->InsertDelayed(handle.slot_index);
        ++this->cumulative_statistics_.delayed_queue_count;
    } else {
        this->AppendReady(handle.slot_index);
        ++this->cumulative_statistics_.immediate_queue_count;
    }
    uint64_t pending_count = OS_KERNEL_WORK_QUEUE_EMPTY_VALUE;
    if (!this->PendingCount(pending_count)) {
        return WorkQueueStatus::CorruptedState;
    }
    if (pending_count > this->cumulative_statistics_.peak_pending_count) {
        this->cumulative_statistics_.peak_pending_count = pending_count;
    }
    return WorkQueueStatus::Succeeded;
}

WorkQueueStatistics WorkQueue::StatisticsLocked() const noexcept {
    WorkQueueStatistics statistics = this->cumulative_statistics_;
    statistics.draining = this->draining_;
    for (uint64_t slot_index = OS_KERNEL_WORK_QUEUE_FIRST_INDEX; slot_index < this->capacity_;
         ++slot_index) {
        const WorkState state = this->entries_[slot_index].state;
        if (state == WorkState::Free) {
            continue;
        }
        ++statistics.registered_count;
        if (state == WorkState::Idle) {
            ++statistics.idle_count;
        } else if (state == WorkState::Delayed) {
            ++statistics.delayed_count;
        } else if (state == WorkState::Queued) {
            ++statistics.queued_count;
        } else if (state == WorkState::Running) {
            ++statistics.running_count;
        } else if (state == WorkState::Completed) {
            ++statistics.completed_count;
        } else if (state == WorkState::Cancelled) {
            ++statistics.cancelled_count;
        }
    }
    return statistics;
}

}

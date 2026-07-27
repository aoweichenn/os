#include "os/kernel/process/thread_scheduler.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_THREAD_SCHEDULER_FIRST_IDENTIFIER = 1ULL;
constexpr uint64_t OS_KERNEL_THREAD_SCHEDULER_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_KERNEL_THREAD_SCHEDULER_FIRST_INDEX = 0ULL;

[[nodiscard]] ProcessEntry EmptyProcessEntry() noexcept {
    return ProcessEntry{
        .process_id = ProcessId{},
        .state = ProcessState::Unused,
        .address_space_root_physical_address = OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE,
        .first_thread_index = OS_KERNEL_THREAD_INVALID_INDEX,
        .thread_count = OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE,
        .live_thread_count = OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE,
        .exited_thread_count = OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE,
    };
}

[[nodiscard]] ThreadEntry EmptyThreadEntry() noexcept {
    return ThreadEntry{
        .thread_id = ThreadId{},
        .process_index = OS_KERNEL_PROCESS_INVALID_INDEX,
        .state = ThreadState::Unused,
        .kernel_stack_slot_index = OS_KERNEL_THREAD_INVALID_INDEX,
        .user_stack_pointer = OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE,
        .thread_local_storage_base = OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE,
        .signal_mask = OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE,
        .run_tick_count = OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE,
        .dispatch_count = OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE,
        .block_count = OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE,
        .wake_count = OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE,
        .wait_condition = WaitCondition::None,
        .wake_reason = WakeReason::None,
        .next_process_thread_index = OS_KERNEL_THREAD_INVALID_INDEX,
        .previous_run_thread_index = OS_KERNEL_THREAD_INVALID_INDEX,
        .next_run_thread_index = OS_KERNEL_THREAD_INVALID_INDEX,
        .next_wait_thread_index = OS_KERNEL_THREAD_INVALID_INDEX,
        .wait_queue = nullptr,
    };
}

}

ThreadSchedulerStatus ThreadScheduler::Initialize(ProcessEntry *const process_storage,
                                                  const uint64_t process_capacity,
                                                  ThreadEntry *const thread_storage,
                                                  const uint64_t thread_capacity,
                                                  const uint64_t maximum_threads_per_process,
                                                  const uint64_t quantum_ticks) noexcept {
    if (this->initialized_) {
        return ThreadSchedulerStatus::AlreadyInitialized;
    }
    if (process_storage == nullptr) {
        return ThreadSchedulerStatus::NullProcessStorage;
    }
    if (thread_storage == nullptr) {
        return ThreadSchedulerStatus::NullThreadStorage;
    }
    if (process_capacity == OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE ||
        process_capacity > OS_KERNEL_PROCESS_CAPACITY_LIMIT) {
        return ThreadSchedulerStatus::InvalidProcessCapacity;
    }
    if (thread_capacity == OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE ||
        thread_capacity > OS_KERNEL_THREAD_CAPACITY_LIMIT || thread_capacity < process_capacity) {
        return ThreadSchedulerStatus::InvalidThreadCapacity;
    }
    if (maximum_threads_per_process == OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE ||
        maximum_threads_per_process > OS_KERNEL_CAPACITY_THREADS_PER_PROCESS ||
        maximum_threads_per_process > thread_capacity) {
        return ThreadSchedulerStatus::InvalidThreadsPerProcess;
    }
    if (quantum_ticks == OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE) {
        return ThreadSchedulerStatus::InvalidQuantum;
    }
    if (this->deadline_queue_.Initialize(thread_capacity) != DeadlineQueueStatus::Succeeded) {
        return ThreadSchedulerStatus::DeadlineFailure;
    }

    for (uint64_t process_index = OS_KERNEL_THREAD_SCHEDULER_FIRST_INDEX;
         process_index < process_capacity; ++process_index) {
        process_storage[process_index] = EmptyProcessEntry();
    }
    for (uint64_t thread_index = OS_KERNEL_THREAD_SCHEDULER_FIRST_INDEX;
         thread_index < thread_capacity; ++thread_index) {
        thread_storage[thread_index] = EmptyThreadEntry();
    }

    this->processes_ = process_storage;
    this->threads_ = thread_storage;
    this->process_capacity_ = process_capacity;
    this->thread_capacity_ = thread_capacity;
    this->maximum_threads_per_process_ = maximum_threads_per_process;
    this->quantum_ticks_ = quantum_ticks;
    this->elapsed_quantum_ticks_ = OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE;
    this->next_process_id_ = OS_KERNEL_THREAD_SCHEDULER_FIRST_IDENTIFIER;
    this->next_thread_id_ = OS_KERNEL_THREAD_SCHEDULER_FIRST_IDENTIFIER;
    this->ready_head_thread_index_ = OS_KERNEL_THREAD_INVALID_INDEX;
    this->ready_tail_thread_index_ = OS_KERNEL_THREAD_INVALID_INDEX;
    this->current_thread_index_ = OS_KERNEL_THREAD_INVALID_INDEX;
    this->cumulative_statistics_ = ThreadSchedulerStatistics{};
    this->cumulative_statistics_.process_capacity = process_capacity;
    this->cumulative_statistics_.thread_capacity = thread_capacity;
    this->cumulative_statistics_.maximum_threads_per_process = maximum_threads_per_process;
    this->initialized_ = true;
    return ThreadSchedulerStatus::Succeeded;
}

ThreadSchedulerStatus
ThreadScheduler::CreateProcess(const uint64_t address_space_root_physical_address,
                               uint64_t &process_index, ProcessId &process_id) noexcept {
    if (!this->initialized_) {
        return ThreadSchedulerStatus::NotInitialized;
    }
    if (address_space_root_physical_address == OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE) {
        return ThreadSchedulerStatus::InvalidAddressSpace;
    }
    uint64_t free_process_index = OS_KERNEL_PROCESS_INVALID_INDEX;
    if (!this->FindFreeProcess(free_process_index)) {
        return ThreadSchedulerStatus::ProcessCapacityExhausted;
    }
    if (this->next_process_id_ == UINT64_MAX) {
        return ThreadSchedulerStatus::IdentifierExhausted;
    }

    const ProcessId assigned_process_id{.value = this->next_process_id_};
    this->next_process_id_ += OS_KERNEL_THREAD_SCHEDULER_COUNTER_INCREMENT;
    this->processes_[free_process_index] = ProcessEntry{
        .process_id = assigned_process_id,
        .state = ProcessState::Alive,
        .address_space_root_physical_address = address_space_root_physical_address,
        .first_thread_index = OS_KERNEL_THREAD_INVALID_INDEX,
        .thread_count = OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE,
        .live_thread_count = OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE,
        .exited_thread_count = OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE,
    };
    this->cumulative_statistics_.created_process_count +=
        OS_KERNEL_THREAD_SCHEDULER_COUNTER_INCREMENT;
    process_index = free_process_index;
    process_id = assigned_process_id;
    return ThreadSchedulerStatus::Succeeded;
}

ThreadSchedulerStatus ThreadScheduler::DiscardProcess(const uint64_t process_index) noexcept {
    if (!this->initialized_) {
        return ThreadSchedulerStatus::NotInitialized;
    }
    if (process_index >= this->process_capacity_) {
        return ThreadSchedulerStatus::InvalidProcessIndex;
    }
    const ProcessEntry &process = this->processes_[process_index];
    if (process.state != ProcessState::Alive) {
        return ThreadSchedulerStatus::InvalidProcessState;
    }
    if (process.thread_count != OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE) {
        return ThreadSchedulerStatus::ProcessThreadsRemain;
    }
    this->processes_[process_index] = EmptyProcessEntry();
    this->cumulative_statistics_.discarded_process_count +=
        OS_KERNEL_THREAD_SCHEDULER_COUNTER_INCREMENT;
    return ThreadSchedulerStatus::Succeeded;
}

ThreadSchedulerStatus
ThreadScheduler::CreateThread(const uint64_t process_index, const uint64_t kernel_stack_slot_index,
                              const uint64_t user_stack_pointer,
                              const uint64_t thread_local_storage_base, const uint64_t signal_mask,
                              uint64_t &thread_index, ThreadId &thread_id) noexcept {
    if (!this->initialized_) {
        return ThreadSchedulerStatus::NotInitialized;
    }
    if (process_index >= this->process_capacity_) {
        return ThreadSchedulerStatus::InvalidProcessIndex;
    }
    ProcessEntry &process = this->processes_[process_index];
    if (process.state != ProcessState::Alive) {
        return ThreadSchedulerStatus::InvalidProcessState;
    }
    if (kernel_stack_slot_index == OS_KERNEL_THREAD_INVALID_INDEX) {
        return ThreadSchedulerStatus::InvalidKernelStack;
    }
    if (process.thread_count >= this->maximum_threads_per_process_) {
        return ThreadSchedulerStatus::ProcessThreadLimitReached;
    }
    uint64_t free_thread_index = OS_KERNEL_THREAD_INVALID_INDEX;
    if (!this->FindFreeThread(free_thread_index)) {
        return ThreadSchedulerStatus::ThreadCapacityExhausted;
    }
    if (this->next_thread_id_ == UINT64_MAX) {
        return ThreadSchedulerStatus::IdentifierExhausted;
    }

    const ThreadId assigned_thread_id{.value = this->next_thread_id_};
    this->next_thread_id_ += OS_KERNEL_THREAD_SCHEDULER_COUNTER_INCREMENT;
    this->threads_[free_thread_index] = ThreadEntry{
        .thread_id = assigned_thread_id,
        .process_index = process_index,
        .state = ThreadState::Ready,
        .kernel_stack_slot_index = kernel_stack_slot_index,
        .user_stack_pointer = user_stack_pointer,
        .thread_local_storage_base = thread_local_storage_base,
        .signal_mask = signal_mask,
        .run_tick_count = OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE,
        .dispatch_count = OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE,
        .block_count = OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE,
        .wake_count = OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE,
        .wait_condition = WaitCondition::None,
        .wake_reason = WakeReason::None,
        .next_process_thread_index = OS_KERNEL_THREAD_INVALID_INDEX,
        .previous_run_thread_index = OS_KERNEL_THREAD_INVALID_INDEX,
        .next_run_thread_index = OS_KERNEL_THREAD_INVALID_INDEX,
        .next_wait_thread_index = OS_KERNEL_THREAD_INVALID_INDEX,
        .wait_queue = nullptr,
    };
    this->AppendProcessThread(process_index, free_thread_index);
    this->AppendReadyThread(free_thread_index);
    process.thread_count += OS_KERNEL_THREAD_SCHEDULER_COUNTER_INCREMENT;
    process.live_thread_count += OS_KERNEL_THREAD_SCHEDULER_COUNTER_INCREMENT;
    this->cumulative_statistics_.created_thread_count +=
        OS_KERNEL_THREAD_SCHEDULER_COUNTER_INCREMENT;
    thread_index = free_thread_index;
    thread_id = assigned_thread_id;
    return ThreadSchedulerStatus::Succeeded;
}

ThreadSchedulerStatus ThreadScheduler::DiscardReadyThread(const uint64_t thread_index) noexcept {
    if (!this->initialized_) {
        return ThreadSchedulerStatus::NotInitialized;
    }
    if (thread_index >= this->thread_capacity_) {
        return ThreadSchedulerStatus::InvalidThreadIndex;
    }
    ThreadEntry &thread = this->threads_[thread_index];
    if (thread.state != ThreadState::Ready || thread.process_index >= this->process_capacity_) {
        return ThreadSchedulerStatus::InvalidThreadState;
    }
    ProcessEntry &process = this->processes_[thread.process_index];
    if (process.state != ProcessState::Alive ||
        process.thread_count == OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE ||
        process.live_thread_count == OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE) {
        return ThreadSchedulerStatus::CorruptedState;
    }
    this->RemoveReadyThread(thread_index);
    this->RemoveProcessThread(thread.process_index, thread_index);
    --process.thread_count;
    --process.live_thread_count;
    thread = EmptyThreadEntry();
    this->cumulative_statistics_.discarded_thread_count +=
        OS_KERNEL_THREAD_SCHEDULER_COUNTER_INCREMENT;
    return ThreadSchedulerStatus::Succeeded;
}

ThreadSchedulerStatus ThreadScheduler::Start(ThreadSchedulingDecision &decision) noexcept {
    this->ResetDecision(decision);
    if (!this->initialized_) {
        return ThreadSchedulerStatus::NotInitialized;
    }
    if (this->current_thread_index_ != OS_KERNEL_THREAD_INVALID_INDEX) {
        return ThreadSchedulerStatus::AlreadyRunning;
    }
    uint64_t next_thread_index = OS_KERNEL_THREAD_INVALID_INDEX;
    if (!this->PopReadyThread(next_thread_index)) {
        decision.idle = this->HasBlockedThread();
        decision.completed = !this->HasLiveThread();
        return ThreadSchedulerStatus::NoReadyThread;
    }
    this->ActivateThread(next_thread_index, OS_KERNEL_THREAD_INVALID_INDEX, false, decision);
    return ThreadSchedulerStatus::Succeeded;
}

ThreadSchedulerStatus
ThreadScheduler::HandleTimerTick(ThreadSchedulingDecision &decision) noexcept {
    this->ResetDecision(decision);
    if (!this->initialized_) {
        return ThreadSchedulerStatus::NotInitialized;
    }
    if (this->current_thread_index_ == OS_KERNEL_THREAD_INVALID_INDEX ||
        this->threads_[this->current_thread_index_].state != ThreadState::Running) {
        return ThreadSchedulerStatus::InvalidCurrentThread;
    }

    ThreadEntry &current_thread = this->threads_[this->current_thread_index_];
    current_thread.run_tick_count += OS_KERNEL_THREAD_SCHEDULER_COUNTER_INCREMENT;
    this->cumulative_statistics_.timer_tick_count += OS_KERNEL_THREAD_SCHEDULER_COUNTER_INCREMENT;
    this->elapsed_quantum_ticks_ += OS_KERNEL_THREAD_SCHEDULER_COUNTER_INCREMENT;
    decision.current_thread_index = this->current_thread_index_;
    if (this->elapsed_quantum_ticks_ < this->quantum_ticks_ ||
        this->ready_head_thread_index_ == OS_KERNEL_THREAD_INVALID_INDEX) {
        return ThreadSchedulerStatus::Succeeded;
    }

    this->elapsed_quantum_ticks_ = OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE;
    const uint64_t previous_thread_index = this->current_thread_index_;
    current_thread.state = ThreadState::Ready;
    this->AppendReadyThread(previous_thread_index);
    uint64_t next_thread_index = OS_KERNEL_THREAD_INVALID_INDEX;
    if (!this->PopReadyThread(next_thread_index)) {
        return ThreadSchedulerStatus::CorruptedState;
    }
    this->cumulative_statistics_.preemption_count += OS_KERNEL_THREAD_SCHEDULER_COUNTER_INCREMENT;
    this->ActivateThread(next_thread_index, previous_thread_index, true, decision);
    return ThreadSchedulerStatus::Succeeded;
}

ThreadSchedulerStatus
ThreadScheduler::YieldCurrentThread(ThreadSchedulingDecision &decision) noexcept {
    this->ResetDecision(decision);
    if (!this->initialized_) {
        return ThreadSchedulerStatus::NotInitialized;
    }
    if (this->current_thread_index_ == OS_KERNEL_THREAD_INVALID_INDEX ||
        this->threads_[this->current_thread_index_].state != ThreadState::Running) {
        return ThreadSchedulerStatus::InvalidCurrentThread;
    }
    if (this->ready_head_thread_index_ == OS_KERNEL_THREAD_INVALID_INDEX) {
        return ThreadSchedulerStatus::Succeeded;
    }

    const uint64_t previous_thread_index = this->current_thread_index_;
    this->threads_[previous_thread_index].state = ThreadState::Ready;
    this->AppendReadyThread(previous_thread_index);
    uint64_t next_thread_index = OS_KERNEL_THREAD_INVALID_INDEX;
    if (!this->PopReadyThread(next_thread_index)) {
        return ThreadSchedulerStatus::CorruptedState;
    }
    this->elapsed_quantum_ticks_ = OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE;
    this->cumulative_statistics_.yield_count += OS_KERNEL_THREAD_SCHEDULER_COUNTER_INCREMENT;
    this->ActivateThread(next_thread_index, previous_thread_index, true, decision);
    return ThreadSchedulerStatus::Succeeded;
}

ThreadSchedulerStatus
ThreadScheduler::BlockCurrentThread(WaitQueue &wait_queue, const WaitCondition wait_condition,
                                    ThreadSchedulingDecision &decision) noexcept {
    this->ResetDecision(decision);
    if (!this->initialized_) {
        return ThreadSchedulerStatus::NotInitialized;
    }
    if (!wait_queue.initialized_) {
        return ThreadSchedulerStatus::WaitQueueNotInitialized;
    }
    if (wait_queue.closed_) {
        return ThreadSchedulerStatus::WaitQueueClosed;
    }
    if (wait_condition == WaitCondition::None) {
        return ThreadSchedulerStatus::InvalidWaitCondition;
    }
    if (this->current_thread_index_ == OS_KERNEL_THREAD_INVALID_INDEX ||
        this->threads_[this->current_thread_index_].state != ThreadState::Running) {
        return ThreadSchedulerStatus::InvalidCurrentThread;
    }

    const uint64_t blocked_thread_index = this->current_thread_index_;
    ThreadEntry &blocked_thread = this->threads_[blocked_thread_index];
    blocked_thread.state = ThreadState::Blocked;
    blocked_thread.wait_condition = wait_condition;
    blocked_thread.wake_reason = WakeReason::None;
    blocked_thread.wait_queue = &wait_queue;
    blocked_thread.block_count += OS_KERNEL_THREAD_SCHEDULER_COUNTER_INCREMENT;
    this->AppendWaitingThread(wait_queue, blocked_thread_index);
    this->cumulative_statistics_.block_count += OS_KERNEL_THREAD_SCHEDULER_COUNTER_INCREMENT;
    this->elapsed_quantum_ticks_ = OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE;
    this->current_thread_index_ = OS_KERNEL_THREAD_INVALID_INDEX;
    this->SelectAfterCurrentStops(blocked_thread_index, decision);
    return ThreadSchedulerStatus::Succeeded;
}

ThreadSchedulerStatus ThreadScheduler::BlockCurrentThreadUntil(
    WaitQueue &wait_queue, const WaitCondition wait_condition, const uint64_t now_nanoseconds,
    const uint64_t deadline_nanoseconds, ThreadSchedulingDecision &decision) noexcept {
    this->ResetDecision(decision);
    if (!this->initialized_) {
        return ThreadSchedulerStatus::NotInitialized;
    }
    if (!wait_queue.initialized_) {
        return ThreadSchedulerStatus::WaitQueueNotInitialized;
    }
    if (wait_queue.closed_) {
        return ThreadSchedulerStatus::WaitQueueClosed;
    }
    if (wait_condition == WaitCondition::None) {
        return ThreadSchedulerStatus::InvalidWaitCondition;
    }
    if (deadline_nanoseconds <= now_nanoseconds) {
        return ThreadSchedulerStatus::DeadlineAlreadyReached;
    }
    if (this->current_thread_index_ == OS_KERNEL_THREAD_INVALID_INDEX ||
        this->threads_[this->current_thread_index_].state != ThreadState::Running) {
        return ThreadSchedulerStatus::InvalidCurrentThread;
    }

    const uint64_t blocked_thread_index = this->current_thread_index_;
    if (this->deadline_queue_.Schedule(blocked_thread_index, deadline_nanoseconds) !=
        DeadlineQueueStatus::Succeeded) {
        return ThreadSchedulerStatus::DeadlineFailure;
    }
    const ThreadSchedulerStatus block_status =
        this->BlockCurrentThread(wait_queue, wait_condition, decision);
    if (block_status != ThreadSchedulerStatus::Succeeded) {
        if (this->deadline_queue_.Resolve(blocked_thread_index, DeadlineResolution::Cancelled) !=
            DeadlineQueueStatus::Succeeded) {
            return ThreadSchedulerStatus::CorruptedState;
        }
        return block_status;
    }
    return ThreadSchedulerStatus::Succeeded;
}

ThreadSchedulerStatus ThreadScheduler::ExpireNextDeadline(const uint64_t now_nanoseconds,
                                                          uint64_t &woken_thread_index,
                                                          WaitCondition &wait_condition,
                                                          WaitQueue *&wait_queue,
                                                          bool &expired) noexcept {
    woken_thread_index = OS_KERNEL_THREAD_INVALID_INDEX;
    wait_condition = WaitCondition::None;
    wait_queue = nullptr;
    expired = false;
    if (!this->initialized_) {
        return ThreadSchedulerStatus::NotInitialized;
    }

    uint64_t candidate_thread_index = OS_KERNEL_THREAD_INVALID_INDEX;
    bool deadline_expired = false;
    if (this->deadline_queue_.PeekExpired(now_nanoseconds, candidate_thread_index,
                                          deadline_expired) != DeadlineQueueStatus::Succeeded) {
        return ThreadSchedulerStatus::DeadlineFailure;
    }
    if (!deadline_expired) {
        return ThreadSchedulerStatus::Succeeded;
    }
    if (candidate_thread_index >= this->thread_capacity_) {
        return ThreadSchedulerStatus::CorruptedState;
    }
    ThreadEntry &thread = this->threads_[candidate_thread_index];
    if (thread.state != ThreadState::Blocked || thread.wait_queue == nullptr ||
        thread.wait_condition == WaitCondition::None) {
        return ThreadSchedulerStatus::CorruptedState;
    }
    WaitQueue *const candidate_wait_queue = thread.wait_queue;
    const WaitCondition candidate_wait_condition = thread.wait_condition;
    bool wake_won = false;
    const ThreadSchedulerStatus wake_status = this->WakeThread(
        *candidate_wait_queue, candidate_thread_index, WakeReason::Timeout, wake_won);
    if (wake_status != ThreadSchedulerStatus::Succeeded || !wake_won) {
        return wake_status == ThreadSchedulerStatus::Succeeded
                   ? ThreadSchedulerStatus::CorruptedState
                   : wake_status;
    }
    woken_thread_index = candidate_thread_index;
    wait_condition = candidate_wait_condition;
    wait_queue = candidate_wait_queue;
    expired = true;
    return ThreadSchedulerStatus::Succeeded;
}

ThreadSchedulerStatus ThreadScheduler::WakeOne(WaitQueue &wait_queue, const WakeReason wake_reason,
                                               uint64_t &woken_thread_index,
                                               bool &wake_won) noexcept {
    woken_thread_index = OS_KERNEL_THREAD_INVALID_INDEX;
    wake_won = false;
    if (!this->initialized_) {
        return ThreadSchedulerStatus::NotInitialized;
    }
    if (!wait_queue.initialized_) {
        return ThreadSchedulerStatus::WaitQueueNotInitialized;
    }
    if (wake_reason == WakeReason::None) {
        return ThreadSchedulerStatus::InvalidWakeReason;
    }
    if (wait_queue.head_thread_index_ == OS_KERNEL_THREAD_INVALID_INDEX) {
        return ThreadSchedulerStatus::Succeeded;
    }
    const uint64_t candidate_thread_index = wait_queue.head_thread_index_;
    const ThreadSchedulerStatus status =
        this->WakeThread(wait_queue, candidate_thread_index, wake_reason, wake_won);
    if (status == ThreadSchedulerStatus::Succeeded && wake_won) {
        woken_thread_index = candidate_thread_index;
    }
    return status;
}

ThreadSchedulerStatus ThreadScheduler::WakeThread(WaitQueue &wait_queue,
                                                  const uint64_t thread_index,
                                                  const WakeReason wake_reason,
                                                  bool &wake_won) noexcept {
    wake_won = false;
    if (!this->initialized_) {
        return ThreadSchedulerStatus::NotInitialized;
    }
    if (!wait_queue.initialized_) {
        return ThreadSchedulerStatus::WaitQueueNotInitialized;
    }
    if (wake_reason == WakeReason::None) {
        return ThreadSchedulerStatus::InvalidWakeReason;
    }
    if (thread_index >= this->thread_capacity_) {
        return ThreadSchedulerStatus::InvalidThreadIndex;
    }

    ThreadEntry &thread = this->threads_[thread_index];
    if (thread.state != ThreadState::Blocked || thread.wait_queue != &wait_queue ||
        thread.wake_reason != WakeReason::None) {
        return ThreadSchedulerStatus::WakeAlreadyResolved;
    }
    bool deadline_scheduled = false;
    if (this->deadline_queue_.Contains(thread_index, deadline_scheduled) !=
        DeadlineQueueStatus::Succeeded) {
        return ThreadSchedulerStatus::DeadlineFailure;
    }
    if (deadline_scheduled &&
        this->deadline_queue_.Resolve(thread_index, wake_reason == WakeReason::Timeout
                                                        ? DeadlineResolution::Expired
                                                        : DeadlineResolution::Cancelled) !=
            DeadlineQueueStatus::Succeeded) {
        return ThreadSchedulerStatus::DeadlineFailure;
    }
    this->RemoveWaitingThread(wait_queue, thread_index);
    thread.state = ThreadState::Ready;
    thread.wait_condition = WaitCondition::None;
    thread.wake_reason = wake_reason;
    thread.wait_queue = nullptr;
    thread.wake_count += OS_KERNEL_THREAD_SCHEDULER_COUNTER_INCREMENT;
    this->AppendReadyThread(thread_index);
    wait_queue.wake_count_ += OS_KERNEL_THREAD_SCHEDULER_COUNTER_INCREMENT;
    this->cumulative_statistics_.wake_count += OS_KERNEL_THREAD_SCHEDULER_COUNTER_INCREMENT;
    wake_won = true;
    return ThreadSchedulerStatus::Succeeded;
}

ThreadSchedulerStatus ThreadScheduler::WakeMany(WaitQueue &wait_queue, const WakeReason wake_reason,
                                                const uint64_t maximum_wake_count,
                                                uint64_t &woken_thread_count) noexcept {
    woken_thread_count = OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE;
    if (!this->initialized_) {
        return ThreadSchedulerStatus::NotInitialized;
    }
    if (!wait_queue.initialized_) {
        return ThreadSchedulerStatus::WaitQueueNotInitialized;
    }
    if (wake_reason == WakeReason::None) {
        return ThreadSchedulerStatus::InvalidWakeReason;
    }
    if (maximum_wake_count == OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE) {
        return ThreadSchedulerStatus::InvalidWakeCount;
    }

    while (woken_thread_count < maximum_wake_count &&
           wait_queue.head_thread_index_ != OS_KERNEL_THREAD_INVALID_INDEX) {
        uint64_t woken_thread_index = OS_KERNEL_THREAD_INVALID_INDEX;
        bool wake_won = false;
        const ThreadSchedulerStatus status =
            this->WakeOne(wait_queue, wake_reason, woken_thread_index, wake_won);
        if (status != ThreadSchedulerStatus::Succeeded || !wake_won) {
            return status == ThreadSchedulerStatus::Succeeded
                       ? ThreadSchedulerStatus::CorruptedState
                       : status;
        }
        ++woken_thread_count;
    }
    return ThreadSchedulerStatus::Succeeded;
}

ThreadSchedulerStatus ThreadScheduler::CloseWaitQueue(WaitQueue &wait_queue,
                                                      uint64_t &woken_thread_count) noexcept {
    woken_thread_count = OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE;
    if (!this->initialized_) {
        return ThreadSchedulerStatus::NotInitialized;
    }
    if (!wait_queue.initialized_) {
        return ThreadSchedulerStatus::WaitQueueNotInitialized;
    }
    if (wait_queue.closed_) {
        return ThreadSchedulerStatus::WaitQueueClosed;
    }
    wait_queue.closed_ = true;
    wait_queue.close_count_ += OS_KERNEL_THREAD_SCHEDULER_COUNTER_INCREMENT;
    if (wait_queue.waiting_thread_count_ == OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE) {
        return ThreadSchedulerStatus::Succeeded;
    }
    return this->WakeMany(wait_queue, WakeReason::ObjectClosed, wait_queue.waiting_thread_count_,
                          woken_thread_count);
}

ThreadSchedulerStatus
ThreadScheduler::TerminateCurrentThread(ThreadSchedulingDecision &decision) noexcept {
    this->ResetDecision(decision);
    if (!this->initialized_) {
        return ThreadSchedulerStatus::NotInitialized;
    }
    if (this->current_thread_index_ == OS_KERNEL_THREAD_INVALID_INDEX ||
        this->threads_[this->current_thread_index_].state != ThreadState::Running) {
        return ThreadSchedulerStatus::InvalidCurrentThread;
    }

    const uint64_t exited_thread_index = this->current_thread_index_;
    ThreadEntry &thread = this->threads_[exited_thread_index];
    if (thread.process_index >= this->process_capacity_) {
        return ThreadSchedulerStatus::CorruptedState;
    }
    ProcessEntry &process = this->processes_[thread.process_index];
    if (process.state != ProcessState::Alive ||
        process.live_thread_count == OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE) {
        return ThreadSchedulerStatus::CorruptedState;
    }

    thread.state = ThreadState::Exited;
    --process.live_thread_count;
    ++process.exited_thread_count;
    if (process.live_thread_count == OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE) {
        process.state = ProcessState::Zombie;
        this->cumulative_statistics_.zombie_transition_count +=
            OS_KERNEL_THREAD_SCHEDULER_COUNTER_INCREMENT;
    }
    this->current_thread_index_ = OS_KERNEL_THREAD_INVALID_INDEX;
    this->elapsed_quantum_ticks_ = OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE;
    this->SelectAfterCurrentStops(exited_thread_index, decision);
    return ThreadSchedulerStatus::Succeeded;
}

ThreadSchedulerStatus
ThreadScheduler::TerminateNonRunningThread(const uint64_t thread_index) noexcept {
    if (thread_index >= this->thread_capacity_) {
        return ThreadSchedulerStatus::InvalidThreadIndex;
    }
    ThreadEntry &thread = this->threads_[thread_index];
    if (thread.process_index >= this->process_capacity_) {
        return ThreadSchedulerStatus::CorruptedState;
    }
    ProcessEntry &process = this->processes_[thread.process_index];
    if (thread.state == ThreadState::Exited) {
        return ThreadSchedulerStatus::Succeeded;
    }
    if (thread.state == ThreadState::Ready) {
        this->RemoveReadyThread(thread_index);
    } else if (thread.state == ThreadState::Blocked) {
        if (thread.wait_queue == nullptr) {
            return ThreadSchedulerStatus::CorruptedState;
        }
        bool deadline_scheduled = false;
        if (this->deadline_queue_.Contains(thread_index, deadline_scheduled) !=
            DeadlineQueueStatus::Succeeded) {
            return ThreadSchedulerStatus::DeadlineFailure;
        }
        if (deadline_scheduled &&
            this->deadline_queue_.Resolve(thread_index, DeadlineResolution::Cancelled) !=
                DeadlineQueueStatus::Succeeded) {
            return ThreadSchedulerStatus::DeadlineFailure;
        }
        this->RemoveWaitingThread(*thread.wait_queue, thread_index);
        thread.wait_condition = WaitCondition::None;
        thread.wake_reason = WakeReason::Cancelled;
        ++thread.wake_count;
        ++thread.wait_queue->wake_count_;
        ++this->cumulative_statistics_.wake_count;
        thread.wait_queue = nullptr;
    } else {
        return ThreadSchedulerStatus::InvalidThreadState;
    }
    if (process.state != ProcessState::Alive ||
        process.live_thread_count == OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE) {
        return ThreadSchedulerStatus::CorruptedState;
    }
    thread.state = ThreadState::Exited;
    --process.live_thread_count;
    ++process.exited_thread_count;
    return ThreadSchedulerStatus::Succeeded;
}

ThreadSchedulerStatus
ThreadScheduler::TerminateProcessSiblings(const uint64_t process_index,
                                          const uint64_t current_thread_index,
                                          uint64_t &terminated_thread_count) noexcept {
    terminated_thread_count = OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE;
    if (!this->initialized_) {
        return ThreadSchedulerStatus::NotInitialized;
    }
    if (process_index >= this->process_capacity_) {
        return ThreadSchedulerStatus::InvalidProcessIndex;
    }
    if (current_thread_index >= this->thread_capacity_ ||
        this->current_thread_index_ != current_thread_index ||
        this->threads_[current_thread_index].state != ThreadState::Running ||
        this->threads_[current_thread_index].process_index != process_index) {
        return ThreadSchedulerStatus::InvalidCurrentThread;
    }
    ProcessEntry &process = this->processes_[process_index];
    if (process.state != ProcessState::Alive) {
        return ThreadSchedulerStatus::InvalidProcessState;
    }
    uint64_t thread_index = process.first_thread_index;
    while (thread_index != OS_KERNEL_THREAD_INVALID_INDEX) {
        const uint64_t next_thread_index = this->threads_[thread_index].next_process_thread_index;
        if (thread_index != current_thread_index &&
            this->threads_[thread_index].state != ThreadState::Exited) {
            const ThreadSchedulerStatus terminate_status =
                this->TerminateNonRunningThread(thread_index);
            if (terminate_status != ThreadSchedulerStatus::Succeeded) {
                return terminate_status;
            }
            ++terminated_thread_count;
        }
        thread_index = next_thread_index;
    }
    return process.live_thread_count == OS_KERNEL_THREAD_SCHEDULER_COUNTER_INCREMENT
               ? ThreadSchedulerStatus::Succeeded
               : ThreadSchedulerStatus::CorruptedState;
}

ThreadSchedulerStatus
ThreadScheduler::TerminateCurrentProcess(const uint64_t process_index,
                                         ThreadSchedulingDecision &decision,
                                         uint64_t &terminated_thread_count) noexcept {
    terminated_thread_count = OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE;
    if (!this->initialized_) {
        return ThreadSchedulerStatus::NotInitialized;
    }
    const uint64_t current_thread_index = this->current_thread_index_;
    uint64_t sibling_count = OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE;
    const ThreadSchedulerStatus sibling_status =
        this->TerminateProcessSiblings(process_index, current_thread_index, sibling_count);
    if (sibling_status != ThreadSchedulerStatus::Succeeded) {
        return sibling_status;
    }
    const ThreadSchedulerStatus current_status = this->TerminateCurrentThread(decision);
    if (current_status != ThreadSchedulerStatus::Succeeded) {
        return current_status;
    }
    terminated_thread_count = sibling_count + OS_KERNEL_THREAD_SCHEDULER_COUNTER_INCREMENT;
    return this->processes_[process_index].state == ProcessState::Zombie
               ? ThreadSchedulerStatus::Succeeded
               : ThreadSchedulerStatus::CorruptedState;
}

ThreadSchedulerStatus ThreadScheduler::ReapExitedThread(const uint64_t thread_index) noexcept {
    if (!this->initialized_) {
        return ThreadSchedulerStatus::NotInitialized;
    }
    if (thread_index >= this->thread_capacity_) {
        return ThreadSchedulerStatus::InvalidThreadIndex;
    }
    ThreadEntry &thread = this->threads_[thread_index];
    if (thread.state != ThreadState::Exited) {
        return ThreadSchedulerStatus::ThreadNotExited;
    }
    if (thread.process_index >= this->process_capacity_) {
        return ThreadSchedulerStatus::CorruptedState;
    }
    ProcessEntry &process = this->processes_[thread.process_index];
    if (process.thread_count == OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE ||
        process.exited_thread_count == OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE) {
        return ThreadSchedulerStatus::CorruptedState;
    }

    this->RemoveProcessThread(thread.process_index, thread_index);
    --process.thread_count;
    --process.exited_thread_count;
    thread = EmptyThreadEntry();
    this->cumulative_statistics_.reaped_thread_count +=
        OS_KERNEL_THREAD_SCHEDULER_COUNTER_INCREMENT;
    return ThreadSchedulerStatus::Succeeded;
}

ThreadSchedulerStatus ThreadScheduler::ReapZombieProcess(const uint64_t process_index) noexcept {
    if (!this->initialized_) {
        return ThreadSchedulerStatus::NotInitialized;
    }
    if (process_index >= this->process_capacity_) {
        return ThreadSchedulerStatus::InvalidProcessIndex;
    }
    const ProcessEntry &process = this->processes_[process_index];
    if (process.state != ProcessState::Zombie) {
        return ThreadSchedulerStatus::ProcessNotZombie;
    }
    if (process.thread_count != OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE ||
        process.live_thread_count != OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE ||
        process.exited_thread_count != OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE) {
        return ThreadSchedulerStatus::ProcessThreadsRemain;
    }
    this->processes_[process_index] = EmptyProcessEntry();
    this->cumulative_statistics_.reaped_process_count +=
        OS_KERNEL_THREAD_SCHEDULER_COUNTER_INCREMENT;
    return ThreadSchedulerStatus::Succeeded;
}

ThreadSchedulerStatus
ThreadScheduler::CommitProcessImage(const uint64_t process_index, const uint64_t thread_index,
                                    const uint64_t address_space_root_physical_address,
                                    const uint64_t user_stack_pointer) noexcept {
    if (!this->initialized_) {
        return ThreadSchedulerStatus::NotInitialized;
    }
    if (process_index >= this->process_capacity_) {
        return ThreadSchedulerStatus::InvalidProcessIndex;
    }
    if (thread_index >= this->thread_capacity_) {
        return ThreadSchedulerStatus::InvalidThreadIndex;
    }
    if (address_space_root_physical_address == OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE) {
        return ThreadSchedulerStatus::InvalidAddressSpace;
    }
    ProcessEntry &process = this->processes_[process_index];
    ThreadEntry &thread = this->threads_[thread_index];
    if (process.state != ProcessState::Alive ||
        process.thread_count != OS_KERNEL_THREAD_SCHEDULER_COUNTER_INCREMENT ||
        process.live_thread_count != OS_KERNEL_THREAD_SCHEDULER_COUNTER_INCREMENT ||
        process.exited_thread_count != OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE) {
        return ThreadSchedulerStatus::InvalidProcessState;
    }
    if (thread.process_index != process_index || thread.state != ThreadState::Running ||
        this->current_thread_index_ != thread_index) {
        return ThreadSchedulerStatus::InvalidThreadState;
    }
    process.address_space_root_physical_address = address_space_root_physical_address;
    thread.user_stack_pointer = user_stack_pointer;
    thread.thread_local_storage_base = OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE;
    return ThreadSchedulerStatus::Succeeded;
}

ThreadSchedulerStatus ThreadScheduler::ReadProcess(const uint64_t process_index,
                                                   ProcessEntry &entry) const noexcept {
    if (!this->initialized_) {
        return ThreadSchedulerStatus::NotInitialized;
    }
    if (process_index >= this->process_capacity_) {
        return ThreadSchedulerStatus::InvalidProcessIndex;
    }
    entry = this->processes_[process_index];
    return ThreadSchedulerStatus::Succeeded;
}

ThreadSchedulerStatus ThreadScheduler::ReadThread(const uint64_t thread_index,
                                                  ThreadEntry &entry) const noexcept {
    if (!this->initialized_) {
        return ThreadSchedulerStatus::NotInitialized;
    }
    if (thread_index >= this->thread_capacity_) {
        return ThreadSchedulerStatus::InvalidThreadIndex;
    }
    entry = this->threads_[thread_index];
    return ThreadSchedulerStatus::Succeeded;
}

ThreadSchedulerStatus ThreadScheduler::FindProcessThread(const uint64_t process_index,
                                                         const ThreadId thread_id,
                                                         uint64_t &thread_index,
                                                         ThreadEntry &entry) const noexcept {
    thread_index = OS_KERNEL_THREAD_INVALID_INDEX;
    entry = EmptyThreadEntry();
    if (!this->initialized_) {
        return ThreadSchedulerStatus::NotInitialized;
    }
    if (process_index >= this->process_capacity_) {
        return ThreadSchedulerStatus::InvalidProcessIndex;
    }
    if (thread_id.value == OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE) {
        return ThreadSchedulerStatus::ThreadNotFound;
    }
    uint64_t candidate_index = this->processes_[process_index].first_thread_index;
    while (candidate_index != OS_KERNEL_THREAD_INVALID_INDEX) {
        if (candidate_index >= this->thread_capacity_) {
            return ThreadSchedulerStatus::CorruptedState;
        }
        const ThreadEntry &candidate = this->threads_[candidate_index];
        if (candidate.thread_id.value == thread_id.value) {
            thread_index = candidate_index;
            entry = candidate;
            return ThreadSchedulerStatus::Succeeded;
        }
        candidate_index = candidate.next_process_thread_index;
    }
    return ThreadSchedulerStatus::ThreadNotFound;
}

ThreadSchedulerStatus ThreadScheduler::SetCurrentThreadLocalStorageBase(
    const uint64_t thread_local_storage_base) noexcept {
    if (!this->initialized_) {
        return ThreadSchedulerStatus::NotInitialized;
    }
    if (this->current_thread_index_ == OS_KERNEL_THREAD_INVALID_INDEX ||
        this->threads_[this->current_thread_index_].state != ThreadState::Running) {
        return ThreadSchedulerStatus::InvalidCurrentThread;
    }
    this->threads_[this->current_thread_index_].thread_local_storage_base =
        thread_local_storage_base;
    return ThreadSchedulerStatus::Succeeded;
}

ThreadSchedulerStatus
ThreadScheduler::SetCurrentThreadSignalMask(const uint64_t signal_mask) noexcept {
    if (!this->initialized_) {
        return ThreadSchedulerStatus::NotInitialized;
    }
    if (this->current_thread_index_ == OS_KERNEL_THREAD_INVALID_INDEX ||
        this->threads_[this->current_thread_index_].state != ThreadState::Running) {
        return ThreadSchedulerStatus::InvalidCurrentThread;
    }
    this->threads_[this->current_thread_index_].signal_mask = signal_mask;
    return ThreadSchedulerStatus::Succeeded;
}

ThreadSchedulerStatus
ThreadScheduler::ConsumeCurrentThreadWakeReason(WakeReason &wake_reason) noexcept {
    wake_reason = WakeReason::None;
    if (!this->initialized_) {
        return ThreadSchedulerStatus::NotInitialized;
    }
    if (this->current_thread_index_ == OS_KERNEL_THREAD_INVALID_INDEX ||
        this->threads_[this->current_thread_index_].state != ThreadState::Running) {
        return ThreadSchedulerStatus::InvalidCurrentThread;
    }
    wake_reason = this->threads_[this->current_thread_index_].wake_reason;
    this->threads_[this->current_thread_index_].wake_reason = WakeReason::None;
    return ThreadSchedulerStatus::Succeeded;
}

ThreadSchedulerStatus ThreadScheduler::Validate() const noexcept {
    if (!this->initialized_) {
        return ThreadSchedulerStatus::NotInitialized;
    }
    if (this->deadline_queue_.Validate() != DeadlineQueueStatus::Succeeded) {
        return ThreadSchedulerStatus::CorruptedState;
    }
    bool process_thread_seen[OS_KERNEL_THREAD_CAPACITY_LIMIT]{};
    bool ready_thread_seen[OS_KERNEL_THREAD_CAPACITY_LIMIT]{};
    uint64_t observed_process_count = OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE;
    uint64_t observed_thread_count = OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE;

    for (uint64_t process_index = OS_KERNEL_THREAD_SCHEDULER_FIRST_INDEX;
         process_index < this->process_capacity_; ++process_index) {
        const ProcessEntry &process = this->processes_[process_index];
        if (process.state == ProcessState::Unused) {
            if (process.process_id.value != OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE ||
                process.address_space_root_physical_address !=
                    OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE ||
                process.first_thread_index != OS_KERNEL_THREAD_INVALID_INDEX ||
                process.thread_count != OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE ||
                process.live_thread_count != OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE ||
                process.exited_thread_count != OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE) {
                return ThreadSchedulerStatus::CorruptedState;
            }
            continue;
        }
        ++observed_process_count;
        if (process.process_id.value == OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE ||
            process.address_space_root_physical_address == OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE ||
            process.thread_count > this->maximum_threads_per_process_ ||
            process.live_thread_count > process.thread_count ||
            process.exited_thread_count > process.thread_count ||
            process.live_thread_count + process.exited_thread_count != process.thread_count ||
            (process.thread_count == OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE &&
             process.first_thread_index != OS_KERNEL_THREAD_INVALID_INDEX) ||
            (process.thread_count != OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE &&
             process.first_thread_index == OS_KERNEL_THREAD_INVALID_INDEX)) {
            return ThreadSchedulerStatus::CorruptedState;
        }
        if ((process.state == ProcessState::Alive &&
             process.thread_count != OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE &&
             process.live_thread_count == OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE) ||
            (process.state == ProcessState::Zombie &&
             process.live_thread_count != OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE)) {
            return ThreadSchedulerStatus::CorruptedState;
        }

        uint64_t process_thread_count = OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE;
        uint64_t live_thread_count = OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE;
        uint64_t exited_thread_count = OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE;
        uint64_t thread_index = process.first_thread_index;
        while (thread_index != OS_KERNEL_THREAD_INVALID_INDEX) {
            if (thread_index >= this->thread_capacity_ || process_thread_seen[thread_index] ||
                process_thread_count >= this->thread_capacity_) {
                return ThreadSchedulerStatus::CorruptedState;
            }
            const ThreadEntry &thread = this->threads_[thread_index];
            if (thread.state == ThreadState::Unused || thread.process_index != process_index) {
                return ThreadSchedulerStatus::CorruptedState;
            }
            process_thread_seen[thread_index] = true;
            ++process_thread_count;
            if (thread.state == ThreadState::Exited) {
                ++exited_thread_count;
            } else {
                ++live_thread_count;
            }
            thread_index = thread.next_process_thread_index;
        }
        if (process_thread_count != process.thread_count ||
            live_thread_count != process.live_thread_count ||
            exited_thread_count != process.exited_thread_count) {
            return ThreadSchedulerStatus::CorruptedState;
        }
    }

    uint64_t ready_thread_count = OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE;
    uint64_t previous_thread_index = OS_KERNEL_THREAD_INVALID_INDEX;
    uint64_t ready_thread_index = this->ready_head_thread_index_;
    while (ready_thread_index != OS_KERNEL_THREAD_INVALID_INDEX) {
        if (ready_thread_index >= this->thread_capacity_ || ready_thread_seen[ready_thread_index] ||
            ready_thread_count >= this->thread_capacity_) {
            return ThreadSchedulerStatus::CorruptedState;
        }
        const ThreadEntry &thread = this->threads_[ready_thread_index];
        if (thread.state != ThreadState::Ready ||
            thread.previous_run_thread_index != previous_thread_index) {
            return ThreadSchedulerStatus::CorruptedState;
        }
        ready_thread_seen[ready_thread_index] = true;
        ++ready_thread_count;
        previous_thread_index = ready_thread_index;
        ready_thread_index = thread.next_run_thread_index;
    }
    if ((ready_thread_count == OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE &&
         (this->ready_head_thread_index_ != OS_KERNEL_THREAD_INVALID_INDEX ||
          this->ready_tail_thread_index_ != OS_KERNEL_THREAD_INVALID_INDEX)) ||
        (ready_thread_count != OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE &&
         previous_thread_index != this->ready_tail_thread_index_)) {
        return ThreadSchedulerStatus::CorruptedState;
    }

    uint64_t running_thread_count = OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE;
    for (uint64_t thread_index = OS_KERNEL_THREAD_SCHEDULER_FIRST_INDEX;
         thread_index < this->thread_capacity_; ++thread_index) {
        const ThreadEntry &thread = this->threads_[thread_index];
        bool deadline_scheduled = false;
        if (this->deadline_queue_.Contains(thread_index, deadline_scheduled) !=
                DeadlineQueueStatus::Succeeded ||
            (deadline_scheduled && thread.state != ThreadState::Blocked)) {
            return ThreadSchedulerStatus::CorruptedState;
        }
        if (thread.state == ThreadState::Unused) {
            if (process_thread_seen[thread_index] ||
                thread.thread_id.value != OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE ||
                thread.process_index != OS_KERNEL_PROCESS_INVALID_INDEX ||
                thread.kernel_stack_slot_index != OS_KERNEL_THREAD_INVALID_INDEX ||
                thread.wait_queue != nullptr) {
                return ThreadSchedulerStatus::CorruptedState;
            }
            continue;
        }
        ++observed_thread_count;
        if (!process_thread_seen[thread_index] ||
            thread.thread_id.value == OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE ||
            thread.process_index >= this->process_capacity_ ||
            thread.kernel_stack_slot_index == OS_KERNEL_THREAD_INVALID_INDEX) {
            return ThreadSchedulerStatus::CorruptedState;
        }
        if (thread.state == ThreadState::Ready) {
            if (!ready_thread_seen[thread_index] || thread.wait_queue != nullptr ||
                thread.wait_condition != WaitCondition::None) {
                return ThreadSchedulerStatus::CorruptedState;
            }
        } else if (thread.state == ThreadState::Running) {
            ++running_thread_count;
            if (thread_index != this->current_thread_index_ || ready_thread_seen[thread_index] ||
                thread.wait_queue != nullptr || thread.wait_condition != WaitCondition::None ||
                thread.previous_run_thread_index != OS_KERNEL_THREAD_INVALID_INDEX ||
                thread.next_run_thread_index != OS_KERNEL_THREAD_INVALID_INDEX) {
                return ThreadSchedulerStatus::CorruptedState;
            }
        } else if (thread.state == ThreadState::Blocked) {
            if (ready_thread_seen[thread_index] || thread.wait_queue == nullptr ||
                thread.wait_condition == WaitCondition::None ||
                thread.wake_reason != WakeReason::None ||
                thread.previous_run_thread_index != OS_KERNEL_THREAD_INVALID_INDEX ||
                thread.next_run_thread_index != OS_KERNEL_THREAD_INVALID_INDEX) {
                return ThreadSchedulerStatus::CorruptedState;
            }
        } else if (thread.state == ThreadState::Exited) {
            if (ready_thread_seen[thread_index] || thread.wait_queue != nullptr ||
                thread.wait_condition != WaitCondition::None ||
                thread.previous_run_thread_index != OS_KERNEL_THREAD_INVALID_INDEX ||
                thread.next_run_thread_index != OS_KERNEL_THREAD_INVALID_INDEX ||
                thread.next_wait_thread_index != OS_KERNEL_THREAD_INVALID_INDEX) {
                return ThreadSchedulerStatus::CorruptedState;
            }
        } else {
            return ThreadSchedulerStatus::CorruptedState;
        }
    }

    const ThreadSchedulerStatistics statistics = this->Statistics();
    if (observed_process_count != statistics.owned_process_count ||
        observed_thread_count != statistics.owned_thread_count ||
        ready_thread_count != statistics.ready_thread_count ||
        running_thread_count != statistics.running_thread_count ||
        running_thread_count > OS_KERNEL_THREAD_SCHEDULER_COUNTER_INCREMENT ||
        (this->current_thread_index_ == OS_KERNEL_THREAD_INVALID_INDEX) !=
            (running_thread_count == OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE) ||
        this->cumulative_statistics_.created_process_count !=
            statistics.owned_process_count + this->cumulative_statistics_.discarded_process_count +
                this->cumulative_statistics_.reaped_process_count ||
        this->cumulative_statistics_.created_thread_count !=
            statistics.owned_thread_count + this->cumulative_statistics_.discarded_thread_count +
                this->cumulative_statistics_.reaped_thread_count) {
        return ThreadSchedulerStatus::CorruptedState;
    }
    return ThreadSchedulerStatus::Succeeded;
}

ThreadSchedulerStatus
ThreadScheduler::ValidateWaitQueue(const WaitQueue &wait_queue) const noexcept {
    if (!this->initialized_) {
        return ThreadSchedulerStatus::NotInitialized;
    }
    if (!wait_queue.initialized_) {
        return ThreadSchedulerStatus::WaitQueueNotInitialized;
    }
    if (wait_queue.queue_id_.value == OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE ||
        wait_queue.wake_count_ > wait_queue.enqueue_count_ ||
        wait_queue.waiting_thread_count_ != wait_queue.enqueue_count_ - wait_queue.wake_count_) {
        return ThreadSchedulerStatus::CorruptedState;
    }

    bool waiting_thread_seen[OS_KERNEL_THREAD_CAPACITY_LIMIT]{};
    uint64_t observed_waiting_thread_count = OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE;
    uint64_t previous_thread_index = OS_KERNEL_THREAD_INVALID_INDEX;
    uint64_t thread_index = wait_queue.head_thread_index_;
    while (thread_index != OS_KERNEL_THREAD_INVALID_INDEX) {
        if (thread_index >= this->thread_capacity_ || waiting_thread_seen[thread_index] ||
            observed_waiting_thread_count >= this->thread_capacity_) {
            return ThreadSchedulerStatus::CorruptedState;
        }
        const ThreadEntry &thread = this->threads_[thread_index];
        if (thread.state != ThreadState::Blocked || thread.wait_queue != &wait_queue ||
            thread.wait_condition == WaitCondition::None ||
            thread.wake_reason != WakeReason::None) {
            return ThreadSchedulerStatus::CorruptedState;
        }
        waiting_thread_seen[thread_index] = true;
        ++observed_waiting_thread_count;
        previous_thread_index = thread_index;
        thread_index = thread.next_wait_thread_index;
    }
    if (observed_waiting_thread_count != wait_queue.waiting_thread_count_ ||
        (observed_waiting_thread_count == OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE &&
         (wait_queue.head_thread_index_ != OS_KERNEL_THREAD_INVALID_INDEX ||
          wait_queue.tail_thread_index_ != OS_KERNEL_THREAD_INVALID_INDEX)) ||
        (observed_waiting_thread_count != OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE &&
         previous_thread_index != wait_queue.tail_thread_index_) ||
        (wait_queue.closed_ &&
         observed_waiting_thread_count != OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE)) {
        return ThreadSchedulerStatus::CorruptedState;
    }
    return ThreadSchedulerStatus::Succeeded;
}

ThreadSchedulerStatistics ThreadScheduler::Statistics() const noexcept {
    ThreadSchedulerStatistics statistics = this->cumulative_statistics_;
    const DeadlineQueueStatistics deadline_statistics = this->deadline_queue_.Statistics();
    statistics.active_deadline_count = deadline_statistics.active_entry_count;
    statistics.peak_deadline_count = deadline_statistics.peak_active_entry_count;
    statistics.deadline_schedule_count = deadline_statistics.schedule_count;
    statistics.deadline_expiration_count = deadline_statistics.expiration_count;
    statistics.deadline_cancellation_count = deadline_statistics.cancellation_count;
    for (uint64_t process_index = OS_KERNEL_THREAD_SCHEDULER_FIRST_INDEX;
         process_index < this->process_capacity_; ++process_index) {
        const ProcessState state = this->processes_[process_index].state;
        if (state == ProcessState::Alive) {
            ++statistics.owned_process_count;
            ++statistics.alive_process_count;
        } else if (state == ProcessState::Zombie) {
            ++statistics.owned_process_count;
            ++statistics.zombie_process_count;
        }
    }
    for (uint64_t thread_index = OS_KERNEL_THREAD_SCHEDULER_FIRST_INDEX;
         thread_index < this->thread_capacity_; ++thread_index) {
        const ThreadState state = this->threads_[thread_index].state;
        if (state == ThreadState::Unused) {
            continue;
        }
        ++statistics.owned_thread_count;
        if (state == ThreadState::Ready) {
            ++statistics.ready_thread_count;
        } else if (state == ThreadState::Running) {
            ++statistics.running_thread_count;
        } else if (state == ThreadState::Blocked) {
            ++statistics.blocked_thread_count;
        } else if (state == ThreadState::Exited) {
            ++statistics.exited_thread_count;
        }
    }
    return statistics;
}

uint64_t ThreadScheduler::CurrentThreadIndex() const noexcept {
    return this->current_thread_index_;
}

ThreadSchedulerStatus ThreadScheduler::CurrentThreadId(ThreadId &thread_id) const noexcept {
    if (!this->initialized_) {
        return ThreadSchedulerStatus::NotInitialized;
    }
    if (this->current_thread_index_ == OS_KERNEL_THREAD_INVALID_INDEX ||
        this->threads_[this->current_thread_index_].state != ThreadState::Running) {
        return ThreadSchedulerStatus::InvalidCurrentThread;
    }
    thread_id = this->threads_[this->current_thread_index_].thread_id;
    return ThreadSchedulerStatus::Succeeded;
}

bool ThreadScheduler::IsActive() const noexcept {
    return this->initialized_ && this->current_thread_index_ != OS_KERNEL_THREAD_INVALID_INDEX;
}

bool ThreadScheduler::IsInitialized() const noexcept { return this->initialized_; }

bool ThreadScheduler::FindFreeProcess(uint64_t &process_index) const noexcept {
    for (uint64_t candidate_index = OS_KERNEL_THREAD_SCHEDULER_FIRST_INDEX;
         candidate_index < this->process_capacity_; ++candidate_index) {
        if (this->processes_[candidate_index].state == ProcessState::Unused) {
            process_index = candidate_index;
            return true;
        }
    }
    return false;
}

bool ThreadScheduler::FindFreeThread(uint64_t &thread_index) const noexcept {
    for (uint64_t candidate_index = OS_KERNEL_THREAD_SCHEDULER_FIRST_INDEX;
         candidate_index < this->thread_capacity_; ++candidate_index) {
        if (this->threads_[candidate_index].state == ThreadState::Unused) {
            thread_index = candidate_index;
            return true;
        }
    }
    return false;
}

void ThreadScheduler::AppendReadyThread(const uint64_t thread_index) noexcept {
    ThreadEntry &thread = this->threads_[thread_index];
    thread.previous_run_thread_index = this->ready_tail_thread_index_;
    thread.next_run_thread_index = OS_KERNEL_THREAD_INVALID_INDEX;
    if (this->ready_tail_thread_index_ == OS_KERNEL_THREAD_INVALID_INDEX) {
        this->ready_head_thread_index_ = thread_index;
    } else {
        this->threads_[this->ready_tail_thread_index_].next_run_thread_index = thread_index;
    }
    this->ready_tail_thread_index_ = thread_index;
}

bool ThreadScheduler::PopReadyThread(uint64_t &thread_index) noexcept {
    if (this->ready_head_thread_index_ == OS_KERNEL_THREAD_INVALID_INDEX) {
        return false;
    }
    thread_index = this->ready_head_thread_index_;
    ThreadEntry &thread = this->threads_[thread_index];
    this->ready_head_thread_index_ = thread.next_run_thread_index;
    if (this->ready_head_thread_index_ == OS_KERNEL_THREAD_INVALID_INDEX) {
        this->ready_tail_thread_index_ = OS_KERNEL_THREAD_INVALID_INDEX;
    } else {
        this->threads_[this->ready_head_thread_index_].previous_run_thread_index =
            OS_KERNEL_THREAD_INVALID_INDEX;
    }
    thread.previous_run_thread_index = OS_KERNEL_THREAD_INVALID_INDEX;
    thread.next_run_thread_index = OS_KERNEL_THREAD_INVALID_INDEX;
    return true;
}

void ThreadScheduler::RemoveReadyThread(const uint64_t thread_index) noexcept {
    ThreadEntry &thread = this->threads_[thread_index];
    if (thread.previous_run_thread_index == OS_KERNEL_THREAD_INVALID_INDEX) {
        this->ready_head_thread_index_ = thread.next_run_thread_index;
    } else {
        this->threads_[thread.previous_run_thread_index].next_run_thread_index =
            thread.next_run_thread_index;
    }
    if (thread.next_run_thread_index == OS_KERNEL_THREAD_INVALID_INDEX) {
        this->ready_tail_thread_index_ = thread.previous_run_thread_index;
    } else {
        this->threads_[thread.next_run_thread_index].previous_run_thread_index =
            thread.previous_run_thread_index;
    }
    thread.previous_run_thread_index = OS_KERNEL_THREAD_INVALID_INDEX;
    thread.next_run_thread_index = OS_KERNEL_THREAD_INVALID_INDEX;
}

void ThreadScheduler::AppendProcessThread(const uint64_t process_index,
                                          const uint64_t thread_index) noexcept {
    ProcessEntry &process = this->processes_[process_index];
    if (process.first_thread_index == OS_KERNEL_THREAD_INVALID_INDEX) {
        process.first_thread_index = thread_index;
        return;
    }
    uint64_t tail_thread_index = process.first_thread_index;
    while (this->threads_[tail_thread_index].next_process_thread_index !=
           OS_KERNEL_THREAD_INVALID_INDEX) {
        tail_thread_index = this->threads_[tail_thread_index].next_process_thread_index;
    }
    this->threads_[tail_thread_index].next_process_thread_index = thread_index;
}

void ThreadScheduler::RemoveProcessThread(const uint64_t process_index,
                                          const uint64_t thread_index) noexcept {
    ProcessEntry &process = this->processes_[process_index];
    uint64_t previous_thread_index = OS_KERNEL_THREAD_INVALID_INDEX;
    uint64_t current_thread_index = process.first_thread_index;
    while (current_thread_index != OS_KERNEL_THREAD_INVALID_INDEX &&
           current_thread_index != thread_index) {
        previous_thread_index = current_thread_index;
        current_thread_index = this->threads_[current_thread_index].next_process_thread_index;
    }
    if (current_thread_index == OS_KERNEL_THREAD_INVALID_INDEX) {
        return;
    }
    if (previous_thread_index == OS_KERNEL_THREAD_INVALID_INDEX) {
        process.first_thread_index = this->threads_[current_thread_index].next_process_thread_index;
    } else {
        this->threads_[previous_thread_index].next_process_thread_index =
            this->threads_[current_thread_index].next_process_thread_index;
    }
    this->threads_[current_thread_index].next_process_thread_index = OS_KERNEL_THREAD_INVALID_INDEX;
}

void ThreadScheduler::AppendWaitingThread(WaitQueue &wait_queue,
                                          const uint64_t thread_index) noexcept {
    ThreadEntry &thread = this->threads_[thread_index];
    thread.next_wait_thread_index = OS_KERNEL_THREAD_INVALID_INDEX;
    if (wait_queue.tail_thread_index_ == OS_KERNEL_THREAD_INVALID_INDEX) {
        wait_queue.head_thread_index_ = thread_index;
    } else {
        this->threads_[wait_queue.tail_thread_index_].next_wait_thread_index = thread_index;
    }
    wait_queue.tail_thread_index_ = thread_index;
    ++wait_queue.waiting_thread_count_;
    ++wait_queue.enqueue_count_;
}

void ThreadScheduler::RemoveWaitingThread(WaitQueue &wait_queue,
                                          const uint64_t thread_index) noexcept {
    uint64_t previous_thread_index = OS_KERNEL_THREAD_INVALID_INDEX;
    uint64_t current_thread_index = wait_queue.head_thread_index_;
    while (current_thread_index != OS_KERNEL_THREAD_INVALID_INDEX &&
           current_thread_index != thread_index) {
        previous_thread_index = current_thread_index;
        current_thread_index = this->threads_[current_thread_index].next_wait_thread_index;
    }
    if (current_thread_index == OS_KERNEL_THREAD_INVALID_INDEX) {
        return;
    }
    const uint64_t next_thread_index = this->threads_[current_thread_index].next_wait_thread_index;
    if (previous_thread_index == OS_KERNEL_THREAD_INVALID_INDEX) {
        wait_queue.head_thread_index_ = next_thread_index;
    } else {
        this->threads_[previous_thread_index].next_wait_thread_index = next_thread_index;
    }
    if (wait_queue.tail_thread_index_ == current_thread_index) {
        wait_queue.tail_thread_index_ = previous_thread_index;
    }
    this->threads_[current_thread_index].next_wait_thread_index = OS_KERNEL_THREAD_INVALID_INDEX;
    --wait_queue.waiting_thread_count_;
}

void ThreadScheduler::ActivateThread(const uint64_t thread_index,
                                     const uint64_t previous_thread_index, const bool switched,
                                     ThreadSchedulingDecision &decision) noexcept {
    ThreadEntry &thread = this->threads_[thread_index];
    thread.state = ThreadState::Running;
    thread.dispatch_count += OS_KERNEL_THREAD_SCHEDULER_COUNTER_INCREMENT;
    this->cumulative_statistics_.dispatch_count += OS_KERNEL_THREAD_SCHEDULER_COUNTER_INCREMENT;
    this->current_thread_index_ = thread_index;
    decision = ThreadSchedulingDecision{
        .previous_thread_index = previous_thread_index,
        .current_thread_index = thread_index,
        .switched = switched,
        .completed = false,
        .idle = false,
    };
}

void ThreadScheduler::SelectAfterCurrentStops(const uint64_t previous_thread_index,
                                              ThreadSchedulingDecision &decision) noexcept {
    uint64_t next_thread_index = OS_KERNEL_THREAD_INVALID_INDEX;
    if (this->PopReadyThread(next_thread_index)) {
        this->ActivateThread(next_thread_index, previous_thread_index, true, decision);
        return;
    }
    decision = ThreadSchedulingDecision{
        .previous_thread_index = previous_thread_index,
        .current_thread_index = OS_KERNEL_THREAD_INVALID_INDEX,
        .switched = false,
        .completed = !this->HasLiveThread(),
        .idle = this->HasBlockedThread(),
    };
}

void ThreadScheduler::ResetDecision(ThreadSchedulingDecision &decision) const noexcept {
    decision = ThreadSchedulingDecision{
        .previous_thread_index = OS_KERNEL_THREAD_INVALID_INDEX,
        .current_thread_index = this->current_thread_index_,
        .switched = false,
        .completed = false,
        .idle = false,
    };
}

bool ThreadScheduler::HasBlockedThread() const noexcept {
    for (uint64_t thread_index = OS_KERNEL_THREAD_SCHEDULER_FIRST_INDEX;
         thread_index < this->thread_capacity_; ++thread_index) {
        if (this->threads_[thread_index].state == ThreadState::Blocked) {
            return true;
        }
    }
    return false;
}

bool ThreadScheduler::HasLiveThread() const noexcept {
    for (uint64_t thread_index = OS_KERNEL_THREAD_SCHEDULER_FIRST_INDEX;
         thread_index < this->thread_capacity_; ++thread_index) {
        const ThreadState state = this->threads_[thread_index].state;
        if (state == ThreadState::Ready || state == ThreadState::Running ||
            state == ThreadState::Blocked) {
            return true;
        }
    }
    return false;
}

bool ThreadScheduler::ProcessContainsThread(const uint64_t process_index,
                                            const uint64_t thread_index) const noexcept {
    if (process_index >= this->process_capacity_ || thread_index >= this->thread_capacity_) {
        return false;
    }
    uint64_t candidate_thread_index = this->processes_[process_index].first_thread_index;
    uint64_t observed_thread_count = OS_KERNEL_THREAD_SCHEDULER_EMPTY_VALUE;
    while (candidate_thread_index != OS_KERNEL_THREAD_INVALID_INDEX &&
           observed_thread_count < this->thread_capacity_) {
        if (candidate_thread_index == thread_index) {
            return true;
        }
        candidate_thread_index = this->threads_[candidate_thread_index].next_process_thread_index;
        ++observed_thread_count;
    }
    return false;
}

}

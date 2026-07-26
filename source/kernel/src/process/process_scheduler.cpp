#include "os/kernel/process/process_scheduler.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_PROCESS_FIRST_IDENTIFIER = 1ULL;
constexpr uint64_t OS_KERNEL_PROCESS_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_KERNEL_PROCESS_FIRST_INDEX = 0ULL;
constexpr uint64_t OS_KERNEL_PROCESS_EMPTY_VALUE = 0ULL;

}

ProcessSchedulerStatus ProcessScheduler::Initialize(const uint64_t quantum_ticks) noexcept {
    for (uint64_t process_index = OS_KERNEL_PROCESS_FIRST_INDEX;
         process_index < OS_KERNEL_PROCESS_CAPACITY; ++process_index) {
        this->entries_[process_index] = ProcessSchedulerEntry{
            .process_id = OS_KERNEL_PROCESS_EMPTY_VALUE,
            .state = ProcessState::Unused,
            .run_tick_count = OS_KERNEL_PROCESS_EMPTY_VALUE,
            .dispatch_count = OS_KERNEL_PROCESS_EMPTY_VALUE,
            .block_count = OS_KERNEL_PROCESS_EMPTY_VALUE,
            .wakeup_count = OS_KERNEL_PROCESS_EMPTY_VALUE,
            .wait_reason = ProcessWaitReason::None,
        };
    }
    this->statistics_ = ProcessSchedulerStatistics{};
    this->quantum_ticks_ = quantum_ticks;
    this->elapsed_quantum_ticks_ = OS_KERNEL_PROCESS_EMPTY_VALUE;
    this->next_process_id_ = OS_KERNEL_PROCESS_FIRST_IDENTIFIER;
    this->current_process_index_ = OS_KERNEL_PROCESS_INVALID_INDEX;
    this->initialized_ = quantum_ticks != OS_KERNEL_PROCESS_EMPTY_VALUE;
    return this->initialized_ ? ProcessSchedulerStatus::Succeeded
                              : ProcessSchedulerStatus::InvalidQuantum;
}

ProcessSchedulerStatus ProcessScheduler::CreateProcess(uint64_t &process_index,
                                                       uint64_t &process_id) noexcept {
    if (!this->initialized_) {
        return ProcessSchedulerStatus::NotInitialized;
    }
    uint64_t free_process_index = OS_KERNEL_PROCESS_INVALID_INDEX;
    if (!this->FindFreeProcess(free_process_index)) {
        return ProcessSchedulerStatus::CapacityExhausted;
    }
    const uint64_t assigned_process_id = this->next_process_id_;
    this->next_process_id_ += OS_KERNEL_PROCESS_COUNTER_INCREMENT;
    this->entries_[free_process_index] = ProcessSchedulerEntry{
        .process_id = assigned_process_id,
        .state = ProcessState::Ready,
        .run_tick_count = OS_KERNEL_PROCESS_EMPTY_VALUE,
        .dispatch_count = OS_KERNEL_PROCESS_EMPTY_VALUE,
        .block_count = OS_KERNEL_PROCESS_EMPTY_VALUE,
        .wakeup_count = OS_KERNEL_PROCESS_EMPTY_VALUE,
        .wait_reason = ProcessWaitReason::None,
    };
    this->statistics_.created_process_count += OS_KERNEL_PROCESS_COUNTER_INCREMENT;
    process_index = free_process_index;
    process_id = assigned_process_id;
    return ProcessSchedulerStatus::Succeeded;
}

ProcessSchedulerStatus
ProcessScheduler::DiscardReadyProcess(const uint64_t process_index) noexcept {
    if (!this->initialized_) {
        return ProcessSchedulerStatus::NotInitialized;
    }
    if (process_index >= OS_KERNEL_PROCESS_CAPACITY ||
        this->entries_[process_index].state != ProcessState::Ready) {
        return ProcessSchedulerStatus::InvalidProcessIndex;
    }
    this->entries_[process_index] = ProcessSchedulerEntry{
        .process_id = OS_KERNEL_PROCESS_EMPTY_VALUE,
        .state = ProcessState::Unused,
        .run_tick_count = OS_KERNEL_PROCESS_EMPTY_VALUE,
        .dispatch_count = OS_KERNEL_PROCESS_EMPTY_VALUE,
        .block_count = OS_KERNEL_PROCESS_EMPTY_VALUE,
        .wakeup_count = OS_KERNEL_PROCESS_EMPTY_VALUE,
        .wait_reason = ProcessWaitReason::None,
    };
    --this->statistics_.created_process_count;
    return ProcessSchedulerStatus::Succeeded;
}

ProcessSchedulerStatus ProcessScheduler::Start(ProcessSchedulingDecision &decision) noexcept {
    this->ResetDecision(decision);
    if (!this->initialized_) {
        return ProcessSchedulerStatus::NotInitialized;
    }
    if (this->current_process_index_ != OS_KERNEL_PROCESS_INVALID_INDEX) {
        return ProcessSchedulerStatus::AlreadyRunning;
    }
    uint64_t next_process_index = OS_KERNEL_PROCESS_INVALID_INDEX;
    if (!this->FindNextReadyProcess(OS_KERNEL_PROCESS_FIRST_INDEX, next_process_index)) {
        return ProcessSchedulerStatus::NoReadyProcess;
    }
    this->ActivateProcess(next_process_index, OS_KERNEL_PROCESS_INVALID_INDEX, false, decision);
    return ProcessSchedulerStatus::Succeeded;
}

ProcessSchedulerStatus
ProcessScheduler::HandleTimerTick(ProcessSchedulingDecision &decision) noexcept {
    this->ResetDecision(decision);
    if (!this->initialized_) {
        return ProcessSchedulerStatus::NotInitialized;
    }
    if (this->current_process_index_ == OS_KERNEL_PROCESS_INVALID_INDEX ||
        this->entries_[this->current_process_index_].state != ProcessState::Running) {
        return ProcessSchedulerStatus::InvalidCurrentProcess;
    }

    ProcessSchedulerEntry &current_entry = this->entries_[this->current_process_index_];
    current_entry.run_tick_count += OS_KERNEL_PROCESS_COUNTER_INCREMENT;
    this->statistics_.timer_tick_count += OS_KERNEL_PROCESS_COUNTER_INCREMENT;
    this->elapsed_quantum_ticks_ += OS_KERNEL_PROCESS_COUNTER_INCREMENT;
    decision.current_process_index = this->current_process_index_;
    if (this->elapsed_quantum_ticks_ < this->quantum_ticks_) {
        return ProcessSchedulerStatus::Succeeded;
    }
    this->elapsed_quantum_ticks_ = OS_KERNEL_PROCESS_EMPTY_VALUE;

    uint64_t next_process_index = OS_KERNEL_PROCESS_INVALID_INDEX;
    const uint64_t first_candidate_index =
        (this->current_process_index_ + OS_KERNEL_PROCESS_COUNTER_INCREMENT) %
        OS_KERNEL_PROCESS_CAPACITY;
    if (!this->FindNextReadyProcess(first_candidate_index, next_process_index)) {
        return ProcessSchedulerStatus::Succeeded;
    }

    const uint64_t previous_process_index = this->current_process_index_;
    this->entries_[previous_process_index].state = ProcessState::Ready;
    this->statistics_.preemption_count += OS_KERNEL_PROCESS_COUNTER_INCREMENT;
    this->ActivateProcess(next_process_index, previous_process_index, true, decision);
    return ProcessSchedulerStatus::Succeeded;
}

ProcessSchedulerStatus
ProcessScheduler::TerminateCurrentProcess(ProcessSchedulingDecision &decision) noexcept {
    this->ResetDecision(decision);
    if (!this->initialized_) {
        return ProcessSchedulerStatus::NotInitialized;
    }
    if (this->current_process_index_ == OS_KERNEL_PROCESS_INVALID_INDEX ||
        this->entries_[this->current_process_index_].state != ProcessState::Running) {
        return ProcessSchedulerStatus::InvalidCurrentProcess;
    }

    const uint64_t terminated_process_index = this->current_process_index_;
    this->entries_[terminated_process_index].state = ProcessState::Terminated;
    this->statistics_.terminated_process_count += OS_KERNEL_PROCESS_COUNTER_INCREMENT;
    this->current_process_index_ = OS_KERNEL_PROCESS_INVALID_INDEX;
    this->elapsed_quantum_ticks_ = OS_KERNEL_PROCESS_EMPTY_VALUE;

    uint64_t next_process_index = OS_KERNEL_PROCESS_INVALID_INDEX;
    const uint64_t first_candidate_index =
        (terminated_process_index + OS_KERNEL_PROCESS_COUNTER_INCREMENT) %
        OS_KERNEL_PROCESS_CAPACITY;
    if (!this->FindNextReadyProcess(first_candidate_index, next_process_index)) {
        decision.previous_process_index = terminated_process_index;
        if (this->HasBlockedProcess()) {
            return ProcessSchedulerStatus::Succeeded;
        }
        decision.completed = true;
        return ProcessSchedulerStatus::Succeeded;
    }
    this->ActivateProcess(next_process_index, terminated_process_index, true, decision);
    return ProcessSchedulerStatus::Succeeded;
}

ProcessSchedulerStatus
ProcessScheduler::BlockCurrentProcess(const ProcessWaitReason wait_reason,
                                      ProcessSchedulingDecision &decision) noexcept {
    this->ResetDecision(decision);
    if (!this->initialized_) {
        return ProcessSchedulerStatus::NotInitialized;
    }
    if (wait_reason == ProcessWaitReason::None) {
        return ProcessSchedulerStatus::InvalidWaitReason;
    }
    if (this->current_process_index_ == OS_KERNEL_PROCESS_INVALID_INDEX ||
        this->entries_[this->current_process_index_].state != ProcessState::Running) {
        return ProcessSchedulerStatus::InvalidCurrentProcess;
    }

    const uint64_t blocked_process_index = this->current_process_index_;
    uint64_t next_process_index = OS_KERNEL_PROCESS_INVALID_INDEX;
    const uint64_t first_candidate_index =
        (blocked_process_index + OS_KERNEL_PROCESS_COUNTER_INCREMENT) % OS_KERNEL_PROCESS_CAPACITY;
    const bool ready_process_available =
        this->FindNextReadyProcess(first_candidate_index, next_process_index);

    ProcessSchedulerEntry &blocked_entry = this->entries_[blocked_process_index];
    blocked_entry.state = ProcessState::Blocked;
    blocked_entry.wait_reason = wait_reason;
    blocked_entry.block_count += OS_KERNEL_PROCESS_COUNTER_INCREMENT;
    this->statistics_.block_count += OS_KERNEL_PROCESS_COUNTER_INCREMENT;
    this->elapsed_quantum_ticks_ = OS_KERNEL_PROCESS_EMPTY_VALUE;
    if (!ready_process_available) {
        this->current_process_index_ = OS_KERNEL_PROCESS_INVALID_INDEX;
        decision.previous_process_index = blocked_process_index;
        decision.current_process_index = OS_KERNEL_PROCESS_INVALID_INDEX;
        return ProcessSchedulerStatus::Succeeded;
    }
    this->ActivateProcess(next_process_index, blocked_process_index, true, decision);
    return ProcessSchedulerStatus::Succeeded;
}

ProcessSchedulerStatus
ProcessScheduler::WakeBlockedProcesses(const ProcessWaitReason wait_reason,
                                       const uint64_t maximum_wake_count,
                                       uint64_t &woken_process_count) noexcept {
    woken_process_count = OS_KERNEL_PROCESS_EMPTY_VALUE;
    if (!this->initialized_) {
        return ProcessSchedulerStatus::NotInitialized;
    }
    if (wait_reason == ProcessWaitReason::None) {
        return ProcessSchedulerStatus::InvalidWaitReason;
    }
    if (maximum_wake_count == OS_KERNEL_PROCESS_EMPTY_VALUE) {
        return ProcessSchedulerStatus::InvalidWakeCount;
    }

    for (uint64_t process_index = OS_KERNEL_PROCESS_FIRST_INDEX;
         process_index < OS_KERNEL_PROCESS_CAPACITY && woken_process_count < maximum_wake_count;
         ++process_index) {
        ProcessSchedulerEntry &entry = this->entries_[process_index];
        if (entry.state != ProcessState::Blocked || entry.wait_reason != wait_reason) {
            continue;
        }
        entry.state = ProcessState::Ready;
        entry.wait_reason = ProcessWaitReason::None;
        entry.wakeup_count += OS_KERNEL_PROCESS_COUNTER_INCREMENT;
        this->statistics_.wakeup_count += OS_KERNEL_PROCESS_COUNTER_INCREMENT;
        woken_process_count += OS_KERNEL_PROCESS_COUNTER_INCREMENT;
    }
    return ProcessSchedulerStatus::Succeeded;
}

ProcessSchedulerStatus ProcessScheduler::ReadEntry(const uint64_t process_index,
                                                   ProcessSchedulerEntry &entry) const noexcept {
    if (process_index >= OS_KERNEL_PROCESS_CAPACITY) {
        return ProcessSchedulerStatus::InvalidProcessIndex;
    }
    entry = this->entries_[process_index];
    return ProcessSchedulerStatus::Succeeded;
}

ProcessSchedulerStatistics ProcessScheduler::Statistics() const noexcept {
    return this->statistics_;
}

uint64_t ProcessScheduler::CurrentProcessIndex() const noexcept {
    return this->current_process_index_;
}

bool ProcessScheduler::IsActive() const noexcept {
    return this->initialized_ && this->current_process_index_ != OS_KERNEL_PROCESS_INVALID_INDEX;
}

bool ProcessScheduler::FindFreeProcess(uint64_t &process_index) const noexcept {
    for (uint64_t candidate_index = OS_KERNEL_PROCESS_FIRST_INDEX;
         candidate_index < OS_KERNEL_PROCESS_CAPACITY; ++candidate_index) {
        if (this->entries_[candidate_index].state == ProcessState::Unused) {
            process_index = candidate_index;
            return true;
        }
    }
    return false;
}

bool ProcessScheduler::FindNextReadyProcess(const uint64_t first_process_index,
                                            uint64_t &process_index) const noexcept {
    for (uint64_t offset = OS_KERNEL_PROCESS_FIRST_INDEX; offset < OS_KERNEL_PROCESS_CAPACITY;
         ++offset) {
        const uint64_t candidate_index =
            (first_process_index + offset) % OS_KERNEL_PROCESS_CAPACITY;
        if (this->entries_[candidate_index].state == ProcessState::Ready) {
            process_index = candidate_index;
            return true;
        }
    }
    return false;
}

bool ProcessScheduler::HasBlockedProcess() const noexcept {
    for (uint64_t process_index = OS_KERNEL_PROCESS_FIRST_INDEX;
         process_index < OS_KERNEL_PROCESS_CAPACITY; ++process_index) {
        if (this->entries_[process_index].state == ProcessState::Blocked) {
            return true;
        }
    }
    return false;
}

void ProcessScheduler::ActivateProcess(const uint64_t process_index,
                                       const uint64_t previous_process_index, const bool switched,
                                       ProcessSchedulingDecision &decision) noexcept {
    this->entries_[process_index].state = ProcessState::Running;
    this->entries_[process_index].dispatch_count += OS_KERNEL_PROCESS_COUNTER_INCREMENT;
    this->statistics_.dispatch_count += OS_KERNEL_PROCESS_COUNTER_INCREMENT;
    this->current_process_index_ = process_index;
    decision = ProcessSchedulingDecision{
        .previous_process_index = previous_process_index,
        .current_process_index = process_index,
        .switched = switched,
        .completed = false,
    };
}

void ProcessScheduler::ResetDecision(ProcessSchedulingDecision &decision) const noexcept {
    decision = ProcessSchedulingDecision{
        .previous_process_index = OS_KERNEL_PROCESS_INVALID_INDEX,
        .current_process_index = this->current_process_index_,
        .switched = false,
        .completed = false,
    };
}

}

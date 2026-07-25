#include "os/kernel/process_scheduler.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_PROCESS_FIRST_IDENTIFIER = 1ULL;
constexpr uint64_t OS_KERNEL_PROCESS_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_KERNEL_PROCESS_FIRST_INDEX = 0ULL;
constexpr uint64_t OS_KERNEL_PROCESS_EMPTY_VALUE = 0ULL;

}

ProcessSchedulerStatus ProcessScheduler::Initialize(const uint64_t quantumTicks) noexcept {
    for (uint64_t processIndex = OS_KERNEL_PROCESS_FIRST_INDEX;
         processIndex < OS_KERNEL_PROCESS_CAPACITY;
         ++processIndex) {
        this->entries_[processIndex] = ProcessSchedulerEntry{
            .processId = OS_KERNEL_PROCESS_EMPTY_VALUE,
            .state = ProcessState::Unused,
            .runTickCount = OS_KERNEL_PROCESS_EMPTY_VALUE,
            .dispatchCount = OS_KERNEL_PROCESS_EMPTY_VALUE,
        };
    }
    this->statistics_ = ProcessSchedulerStatistics{};
    this->quantumTicks_ = quantumTicks;
    this->elapsedQuantumTicks_ = OS_KERNEL_PROCESS_EMPTY_VALUE;
    this->nextProcessId_ = OS_KERNEL_PROCESS_FIRST_IDENTIFIER;
    this->currentProcessIndex_ = OS_KERNEL_PROCESS_INVALID_INDEX;
    this->initialized_ = quantumTicks != OS_KERNEL_PROCESS_EMPTY_VALUE;
    return this->initialized_ ? ProcessSchedulerStatus::Succeeded
                              : ProcessSchedulerStatus::InvalidQuantum;
}

ProcessSchedulerStatus ProcessScheduler::CreateProcess(uint64_t &processIndex,
                                                       uint64_t &processId) noexcept {
    if (!this->initialized_) {
        return ProcessSchedulerStatus::NotInitialized;
    }
    uint64_t freeProcessIndex = OS_KERNEL_PROCESS_INVALID_INDEX;
    if (!this->FindFreeProcess(freeProcessIndex)) {
        return ProcessSchedulerStatus::CapacityExhausted;
    }
    const uint64_t assignedProcessId = this->nextProcessId_;
    this->nextProcessId_ += OS_KERNEL_PROCESS_COUNTER_INCREMENT;
    this->entries_[freeProcessIndex] = ProcessSchedulerEntry{
        .processId = assignedProcessId,
        .state = ProcessState::Ready,
        .runTickCount = OS_KERNEL_PROCESS_EMPTY_VALUE,
        .dispatchCount = OS_KERNEL_PROCESS_EMPTY_VALUE,
    };
    this->statistics_.createdProcessCount += OS_KERNEL_PROCESS_COUNTER_INCREMENT;
    processIndex = freeProcessIndex;
    processId = assignedProcessId;
    return ProcessSchedulerStatus::Succeeded;
}

ProcessSchedulerStatus
ProcessScheduler::DiscardReadyProcess(const uint64_t processIndex) noexcept {
    if (!this->initialized_) {
        return ProcessSchedulerStatus::NotInitialized;
    }
    if (processIndex >= OS_KERNEL_PROCESS_CAPACITY ||
        this->entries_[processIndex].state != ProcessState::Ready) {
        return ProcessSchedulerStatus::InvalidProcessIndex;
    }
    this->entries_[processIndex] = ProcessSchedulerEntry{
        .processId = OS_KERNEL_PROCESS_EMPTY_VALUE,
        .state = ProcessState::Unused,
        .runTickCount = OS_KERNEL_PROCESS_EMPTY_VALUE,
        .dispatchCount = OS_KERNEL_PROCESS_EMPTY_VALUE,
    };
    --this->statistics_.createdProcessCount;
    return ProcessSchedulerStatus::Succeeded;
}

ProcessSchedulerStatus
ProcessScheduler::Start(ProcessSchedulingDecision &decision) noexcept {
    this->ResetDecision(decision);
    if (!this->initialized_) {
        return ProcessSchedulerStatus::NotInitialized;
    }
    if (this->currentProcessIndex_ != OS_KERNEL_PROCESS_INVALID_INDEX) {
        return ProcessSchedulerStatus::AlreadyRunning;
    }
    uint64_t nextProcessIndex = OS_KERNEL_PROCESS_INVALID_INDEX;
    if (!this->FindNextReadyProcess(OS_KERNEL_PROCESS_FIRST_INDEX, nextProcessIndex)) {
        return ProcessSchedulerStatus::NoReadyProcess;
    }
    this->ActivateProcess(nextProcessIndex, OS_KERNEL_PROCESS_INVALID_INDEX, false, decision);
    return ProcessSchedulerStatus::Succeeded;
}

ProcessSchedulerStatus
ProcessScheduler::HandleTimerTick(ProcessSchedulingDecision &decision) noexcept {
    this->ResetDecision(decision);
    if (!this->initialized_) {
        return ProcessSchedulerStatus::NotInitialized;
    }
    if (this->currentProcessIndex_ == OS_KERNEL_PROCESS_INVALID_INDEX ||
        this->entries_[this->currentProcessIndex_].state != ProcessState::Running) {
        return ProcessSchedulerStatus::InvalidCurrentProcess;
    }

    ProcessSchedulerEntry &currentEntry = this->entries_[this->currentProcessIndex_];
    currentEntry.runTickCount += OS_KERNEL_PROCESS_COUNTER_INCREMENT;
    this->statistics_.timerTickCount += OS_KERNEL_PROCESS_COUNTER_INCREMENT;
    this->elapsedQuantumTicks_ += OS_KERNEL_PROCESS_COUNTER_INCREMENT;
    decision.currentProcessIndex = this->currentProcessIndex_;
    if (this->elapsedQuantumTicks_ < this->quantumTicks_) {
        return ProcessSchedulerStatus::Succeeded;
    }
    this->elapsedQuantumTicks_ = OS_KERNEL_PROCESS_EMPTY_VALUE;

    uint64_t nextProcessIndex = OS_KERNEL_PROCESS_INVALID_INDEX;
    const uint64_t firstCandidateIndex =
        (this->currentProcessIndex_ + OS_KERNEL_PROCESS_COUNTER_INCREMENT) %
        OS_KERNEL_PROCESS_CAPACITY;
    if (!this->FindNextReadyProcess(firstCandidateIndex, nextProcessIndex)) {
        return ProcessSchedulerStatus::Succeeded;
    }

    const uint64_t previousProcessIndex = this->currentProcessIndex_;
    this->entries_[previousProcessIndex].state = ProcessState::Ready;
    this->statistics_.preemptionCount += OS_KERNEL_PROCESS_COUNTER_INCREMENT;
    this->ActivateProcess(nextProcessIndex, previousProcessIndex, true, decision);
    return ProcessSchedulerStatus::Succeeded;
}

ProcessSchedulerStatus
ProcessScheduler::TerminateCurrentProcess(ProcessSchedulingDecision &decision) noexcept {
    this->ResetDecision(decision);
    if (!this->initialized_) {
        return ProcessSchedulerStatus::NotInitialized;
    }
    if (this->currentProcessIndex_ == OS_KERNEL_PROCESS_INVALID_INDEX ||
        this->entries_[this->currentProcessIndex_].state != ProcessState::Running) {
        return ProcessSchedulerStatus::InvalidCurrentProcess;
    }

    const uint64_t terminatedProcessIndex = this->currentProcessIndex_;
    this->entries_[terminatedProcessIndex].state = ProcessState::Terminated;
    this->statistics_.terminatedProcessCount += OS_KERNEL_PROCESS_COUNTER_INCREMENT;
    this->currentProcessIndex_ = OS_KERNEL_PROCESS_INVALID_INDEX;
    this->elapsedQuantumTicks_ = OS_KERNEL_PROCESS_EMPTY_VALUE;

    uint64_t nextProcessIndex = OS_KERNEL_PROCESS_INVALID_INDEX;
    const uint64_t firstCandidateIndex =
        (terminatedProcessIndex + OS_KERNEL_PROCESS_COUNTER_INCREMENT) %
        OS_KERNEL_PROCESS_CAPACITY;
    if (!this->FindNextReadyProcess(firstCandidateIndex, nextProcessIndex)) {
        decision.previousProcessIndex = terminatedProcessIndex;
        decision.completed = true;
        return ProcessSchedulerStatus::Succeeded;
    }
    this->ActivateProcess(nextProcessIndex, terminatedProcessIndex, true, decision);
    return ProcessSchedulerStatus::Succeeded;
}

ProcessSchedulerStatus
ProcessScheduler::ReadEntry(const uint64_t processIndex,
                            ProcessSchedulerEntry &entry) const noexcept {
    if (processIndex >= OS_KERNEL_PROCESS_CAPACITY) {
        return ProcessSchedulerStatus::InvalidProcessIndex;
    }
    entry = this->entries_[processIndex];
    return ProcessSchedulerStatus::Succeeded;
}

ProcessSchedulerStatistics ProcessScheduler::Statistics() const noexcept {
    return this->statistics_;
}

uint64_t ProcessScheduler::CurrentProcessIndex() const noexcept {
    return this->currentProcessIndex_;
}

bool ProcessScheduler::IsActive() const noexcept {
    return this->initialized_ &&
           this->currentProcessIndex_ != OS_KERNEL_PROCESS_INVALID_INDEX;
}

bool ProcessScheduler::FindFreeProcess(uint64_t &processIndex) const noexcept {
    for (uint64_t candidateIndex = OS_KERNEL_PROCESS_FIRST_INDEX;
         candidateIndex < OS_KERNEL_PROCESS_CAPACITY;
         ++candidateIndex) {
        if (this->entries_[candidateIndex].state == ProcessState::Unused) {
            processIndex = candidateIndex;
            return true;
        }
    }
    return false;
}

bool ProcessScheduler::FindNextReadyProcess(const uint64_t firstProcessIndex,
                                            uint64_t &processIndex) const noexcept {
    for (uint64_t offset = OS_KERNEL_PROCESS_FIRST_INDEX;
         offset < OS_KERNEL_PROCESS_CAPACITY; ++offset) {
        const uint64_t candidateIndex =
            (firstProcessIndex + offset) % OS_KERNEL_PROCESS_CAPACITY;
        if (this->entries_[candidateIndex].state == ProcessState::Ready) {
            processIndex = candidateIndex;
            return true;
        }
    }
    return false;
}

void ProcessScheduler::ActivateProcess(const uint64_t processIndex,
                                       const uint64_t previousProcessIndex,
                                       const bool switched,
                                       ProcessSchedulingDecision &decision) noexcept {
    this->entries_[processIndex].state = ProcessState::Running;
    this->entries_[processIndex].dispatchCount += OS_KERNEL_PROCESS_COUNTER_INCREMENT;
    this->statistics_.dispatchCount += OS_KERNEL_PROCESS_COUNTER_INCREMENT;
    this->currentProcessIndex_ = processIndex;
    decision = ProcessSchedulingDecision{
        .previousProcessIndex = previousProcessIndex,
        .currentProcessIndex = processIndex,
        .switched = switched,
        .completed = false,
    };
}

void ProcessScheduler::ResetDecision(ProcessSchedulingDecision &decision) const noexcept {
    decision = ProcessSchedulingDecision{
        .previousProcessIndex = OS_KERNEL_PROCESS_INVALID_INDEX,
        .currentProcessIndex = this->currentProcessIndex_,
        .switched = false,
        .completed = false,
    };
}

}

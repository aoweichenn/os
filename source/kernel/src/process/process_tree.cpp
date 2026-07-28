#include "os/kernel/process/process_tree.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_PROCESS_TREE_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_PROCESS_TREE_FIRST_PROCESS_ID = 1ULL;
constexpr uint64_t OS_KERNEL_PROCESS_TREE_COUNTER_INCREMENT = 1ULL;

[[nodiscard]] ProcessTreeEntry EmptyProcessTreeEntry() noexcept {
    return ProcessTreeEntry{
        .process_id = OS_KERNEL_PROCESS_TREE_EMPTY_VALUE,
        .parent_process_index = OS_KERNEL_PROCESS_TREE_INVALID_INDEX,
        .state = ProcessTreeState::Unused,
        .exit_status = {},
        .stop_signal_number = OS_KERNEL_PROCESS_TREE_EMPTY_VALUE,
        .stopped_event_pending = false,
        .continued_event_pending = false,
    };
}

}

ProcessTreeStatus ProcessTree::Initialize(ProcessTreeEntry *const entry_storage,
                                          const uint64_t capacity) noexcept {
    if (this->initialized_) {
        return ProcessTreeStatus::AlreadyInitialized;
    }
    if (entry_storage == nullptr) {
        return ProcessTreeStatus::InvalidStorage;
    }
    if (capacity == OS_KERNEL_PROCESS_TREE_EMPTY_VALUE) {
        return ProcessTreeStatus::InvalidCapacity;
    }
    for (uint64_t process_index = OS_KERNEL_PROCESS_TREE_EMPTY_VALUE; process_index < capacity;
         ++process_index) {
        entry_storage[process_index] = EmptyProcessTreeEntry();
    }
    this->entries_ = entry_storage;
    this->capacity_ = capacity;
    this->init_process_index_ = OS_KERNEL_PROCESS_TREE_INVALID_INDEX;
    this->statistics_ = ProcessTreeStatistics{
        .capacity = capacity,
        .active_process_count = OS_KERNEL_PROCESS_TREE_EMPTY_VALUE,
        .alive_process_count = OS_KERNEL_PROCESS_TREE_EMPTY_VALUE,
        .stopped_process_count = OS_KERNEL_PROCESS_TREE_EMPTY_VALUE,
        .zombie_process_count = OS_KERNEL_PROCESS_TREE_EMPTY_VALUE,
        .registered_process_count = OS_KERNEL_PROCESS_TREE_EMPTY_VALUE,
        .exited_process_count = OS_KERNEL_PROCESS_TREE_EMPTY_VALUE,
        .collected_process_count = OS_KERNEL_PROCESS_TREE_EMPTY_VALUE,
        .reparented_process_count = OS_KERNEL_PROCESS_TREE_EMPTY_VALUE,
        .wait_attempt_count = OS_KERNEL_PROCESS_TREE_EMPTY_VALUE,
        .wait_success_count = OS_KERNEL_PROCESS_TREE_EMPTY_VALUE,
        .wait_block_count = OS_KERNEL_PROCESS_TREE_EMPTY_VALUE,
        .wait_no_child_count = OS_KERNEL_PROCESS_TREE_EMPTY_VALUE,
        .stopped_event_count = OS_KERNEL_PROCESS_TREE_EMPTY_VALUE,
        .continued_event_count = OS_KERNEL_PROCESS_TREE_EMPTY_VALUE,
        .observed_stopped_event_count = OS_KERNEL_PROCESS_TREE_EMPTY_VALUE,
        .observed_continued_event_count = OS_KERNEL_PROCESS_TREE_EMPTY_VALUE,
    };
    this->initialized_ = true;
    return ProcessTreeStatus::Succeeded;
}

ProcessTreeStatus ProcessTree::RegisterInit(const uint64_t process_index,
                                            const uint64_t process_id) noexcept {
    if (!this->initialized_) {
        return ProcessTreeStatus::NotInitialized;
    }
    if (process_index >= this->capacity_) {
        return ProcessTreeStatus::InvalidProcessIndex;
    }
    if (process_id != OS_KERNEL_PROCESS_TREE_FIRST_PROCESS_ID) {
        return ProcessTreeStatus::InvalidProcessId;
    }
    if (this->init_process_index_ != OS_KERNEL_PROCESS_TREE_INVALID_INDEX) {
        return ProcessTreeStatus::InitAlreadyRegistered;
    }
    if (this->IsRegistered(process_index)) {
        return ProcessTreeStatus::ProcessAlreadyRegistered;
    }
    this->entries_[process_index] = ProcessTreeEntry{
        .process_id = process_id,
        .parent_process_index = OS_KERNEL_PROCESS_TREE_INVALID_INDEX,
        .state = ProcessTreeState::Alive,
        .exit_status = {},
        .stop_signal_number = OS_KERNEL_PROCESS_TREE_EMPTY_VALUE,
        .stopped_event_pending = false,
        .continued_event_pending = false,
    };
    this->init_process_index_ = process_index;
    this->statistics_.active_process_count += OS_KERNEL_PROCESS_TREE_COUNTER_INCREMENT;
    this->statistics_.alive_process_count += OS_KERNEL_PROCESS_TREE_COUNTER_INCREMENT;
    this->statistics_.registered_process_count += OS_KERNEL_PROCESS_TREE_COUNTER_INCREMENT;
    return ProcessTreeStatus::Succeeded;
}

ProcessTreeStatus ProcessTree::RegisterChild(const uint64_t process_index,
                                             const uint64_t process_id,
                                             const uint64_t parent_process_index) noexcept {
    if (!this->initialized_) {
        return ProcessTreeStatus::NotInitialized;
    }
    if (this->init_process_index_ == OS_KERNEL_PROCESS_TREE_INVALID_INDEX) {
        return ProcessTreeStatus::InitRequired;
    }
    if (process_index >= this->capacity_) {
        return ProcessTreeStatus::InvalidProcessIndex;
    }
    if (process_id == OS_KERNEL_PROCESS_TREE_EMPTY_VALUE ||
        process_id == OS_KERNEL_PROCESS_TREE_WAIT_ANY_PROCESS_ID) {
        return ProcessTreeStatus::InvalidProcessId;
    }
    if (parent_process_index >= this->capacity_ ||
        this->entries_[parent_process_index].state != ProcessTreeState::Alive) {
        return ProcessTreeStatus::InvalidParent;
    }
    if (this->IsRegistered(process_index)) {
        return ProcessTreeStatus::ProcessAlreadyRegistered;
    }
    for (uint64_t candidate_index = OS_KERNEL_PROCESS_TREE_EMPTY_VALUE;
         candidate_index < this->capacity_; ++candidate_index) {
        if (this->IsRegistered(candidate_index) &&
            this->entries_[candidate_index].process_id == process_id) {
            return ProcessTreeStatus::InvalidProcessId;
        }
    }
    this->entries_[process_index] = ProcessTreeEntry{
        .process_id = process_id,
        .parent_process_index = parent_process_index,
        .state = ProcessTreeState::Alive,
        .exit_status = {},
        .stop_signal_number = OS_KERNEL_PROCESS_TREE_EMPTY_VALUE,
        .stopped_event_pending = false,
        .continued_event_pending = false,
    };
    this->statistics_.active_process_count += OS_KERNEL_PROCESS_TREE_COUNTER_INCREMENT;
    this->statistics_.alive_process_count += OS_KERNEL_PROCESS_TREE_COUNTER_INCREMENT;
    this->statistics_.registered_process_count += OS_KERNEL_PROCESS_TREE_COUNTER_INCREMENT;
    return ProcessTreeStatus::Succeeded;
}

ProcessTreeStatus ProcessTree::MarkExited(const uint64_t process_index,
                                          const ProcessTreeExitStatus &exit_status,
                                          uint64_t &reparented_process_count) noexcept {
    reparented_process_count = OS_KERNEL_PROCESS_TREE_EMPTY_VALUE;
    if (!this->initialized_) {
        return ProcessTreeStatus::NotInitialized;
    }
    if (process_index >= this->capacity_) {
        return ProcessTreeStatus::InvalidProcessIndex;
    }
    ProcessTreeEntry &entry = this->entries_[process_index];
    if (entry.state == ProcessTreeState::Unused) {
        return ProcessTreeStatus::ProcessNotRegistered;
    }
    if ((entry.state != ProcessTreeState::Alive && entry.state != ProcessTreeState::Stopped) ||
        exit_status.termination_reason == ProcessTreeTerminationReason::None) {
        return ProcessTreeStatus::InvalidState;
    }
    if (process_index == this->init_process_index_ && this->HasChild(process_index)) {
        return ProcessTreeStatus::ProcessHasChildren;
    }

    const ProcessTreeState previous_state = entry.state;
    entry.state = ProcessTreeState::Zombie;
    entry.exit_status = exit_status;
    if (!entry.stopped_event_pending) {
        entry.stop_signal_number = OS_KERNEL_PROCESS_TREE_EMPTY_VALUE;
    }
    if (previous_state == ProcessTreeState::Stopped) {
        --this->statistics_.stopped_process_count;
    } else {
        --this->statistics_.alive_process_count;
    }
    this->statistics_.zombie_process_count += OS_KERNEL_PROCESS_TREE_COUNTER_INCREMENT;
    this->statistics_.exited_process_count += OS_KERNEL_PROCESS_TREE_COUNTER_INCREMENT;

    if (process_index != this->init_process_index_) {
        for (uint64_t child_index = OS_KERNEL_PROCESS_TREE_EMPTY_VALUE;
             child_index < this->capacity_; ++child_index) {
            ProcessTreeEntry &child = this->entries_[child_index];
            if (child.state == ProcessTreeState::Unused ||
                child.parent_process_index != process_index) {
                continue;
            }
            child.parent_process_index = this->init_process_index_;
            reparented_process_count += OS_KERNEL_PROCESS_TREE_COUNTER_INCREMENT;
        }
    }
    this->statistics_.reparented_process_count += reparented_process_count;
    return ProcessTreeStatus::Succeeded;
}

ProcessTreeStatus ProcessTree::MarkStopped(const uint64_t process_index,
                                           const uint64_t signal_number) noexcept {
    if (!this->initialized_) {
        return ProcessTreeStatus::NotInitialized;
    }
    if (process_index >= this->capacity_) {
        return ProcessTreeStatus::InvalidProcessIndex;
    }
    ProcessTreeEntry &entry = this->entries_[process_index];
    if (entry.state == ProcessTreeState::Unused) {
        return ProcessTreeStatus::ProcessNotRegistered;
    }
    if (entry.state != ProcessTreeState::Alive ||
        signal_number == OS_KERNEL_PROCESS_TREE_EMPTY_VALUE) {
        return ProcessTreeStatus::InvalidState;
    }
    entry.state = ProcessTreeState::Stopped;
    entry.stop_signal_number = signal_number;
    entry.stopped_event_pending = true;
    --this->statistics_.alive_process_count;
    this->statistics_.stopped_process_count += OS_KERNEL_PROCESS_TREE_COUNTER_INCREMENT;
    this->statistics_.stopped_event_count += OS_KERNEL_PROCESS_TREE_COUNTER_INCREMENT;
    return ProcessTreeStatus::Succeeded;
}

ProcessTreeStatus ProcessTree::MarkContinued(const uint64_t process_index) noexcept {
    if (!this->initialized_) {
        return ProcessTreeStatus::NotInitialized;
    }
    if (process_index >= this->capacity_) {
        return ProcessTreeStatus::InvalidProcessIndex;
    }
    ProcessTreeEntry &entry = this->entries_[process_index];
    if (entry.state == ProcessTreeState::Unused) {
        return ProcessTreeStatus::ProcessNotRegistered;
    }
    if (entry.state != ProcessTreeState::Stopped) {
        return ProcessTreeStatus::InvalidState;
    }
    entry.state = ProcessTreeState::Alive;
    entry.continued_event_pending = true;
    --this->statistics_.stopped_process_count;
    this->statistics_.alive_process_count += OS_KERNEL_PROCESS_TREE_COUNTER_INCREMENT;
    this->statistics_.continued_event_count += OS_KERNEL_PROCESS_TREE_COUNTER_INCREMENT;
    return ProcessTreeStatus::Succeeded;
}

ProcessTreeStatus ProcessTree::TryWait(const uint64_t parent_process_index,
                                       const uint64_t requested_process_id,
                                       ProcessTreeWaitResult &wait_result) noexcept {
    wait_result = ProcessTreeWaitResult{};
    if (!this->initialized_) {
        return ProcessTreeStatus::NotInitialized;
    }
    if (parent_process_index >= this->capacity_ ||
        this->entries_[parent_process_index].state != ProcessTreeState::Alive) {
        return ProcessTreeStatus::InvalidParent;
    }
    if (requested_process_id == OS_KERNEL_PROCESS_TREE_EMPTY_VALUE) {
        return ProcessTreeStatus::InvalidProcessId;
    }
    this->statistics_.wait_attempt_count += OS_KERNEL_PROCESS_TREE_COUNTER_INCREMENT;

    bool matching_child_found = false;
    for (uint64_t child_index = OS_KERNEL_PROCESS_TREE_EMPTY_VALUE; child_index < this->capacity_;
         ++child_index) {
        const ProcessTreeEntry &child = this->entries_[child_index];
        if (child.state == ProcessTreeState::Unused ||
            child.parent_process_index != parent_process_index ||
            !this->MatchesRequestedProcess(child, requested_process_id)) {
            continue;
        }
        matching_child_found = true;
        if (child.state == ProcessTreeState::Zombie) {
            return this->Collect(child_index, wait_result, true);
        }
    }
    if (matching_child_found) {
        this->statistics_.wait_block_count += OS_KERNEL_PROCESS_TREE_COUNTER_INCREMENT;
        return ProcessTreeStatus::ChildStillRunning;
    }
    this->statistics_.wait_no_child_count += OS_KERNEL_PROCESS_TREE_COUNTER_INCREMENT;
    return ProcessTreeStatus::NoMatchingChild;
}

ProcessTreeStatus ProcessTree::TryWaitEvent(const uint64_t parent_process_index,
                                            const uint64_t requested_process_id,
                                            const uint64_t wait_flags,
                                            ProcessTreeWaitEventResult &wait_result) noexcept {
    wait_result = ProcessTreeWaitEventResult{};
    if (!this->initialized_) {
        return ProcessTreeStatus::NotInitialized;
    }
    if (parent_process_index >= this->capacity_ ||
        this->entries_[parent_process_index].state != ProcessTreeState::Alive) {
        return ProcessTreeStatus::InvalidParent;
    }
    if (requested_process_id == OS_KERNEL_PROCESS_TREE_EMPTY_VALUE ||
        wait_flags == OS_KERNEL_PROCESS_TREE_EMPTY_VALUE ||
        (wait_flags & ~OS_KERNEL_PROCESS_TREE_WAIT_VALID_FLAG_MASK) !=
            OS_KERNEL_PROCESS_TREE_EMPTY_VALUE) {
        return ProcessTreeStatus::InvalidProcessId;
    }
    this->statistics_.wait_attempt_count += OS_KERNEL_PROCESS_TREE_COUNTER_INCREMENT;

    bool matching_child_found = false;
    for (uint64_t child_index = OS_KERNEL_PROCESS_TREE_EMPTY_VALUE; child_index < this->capacity_;
         ++child_index) {
        ProcessTreeEntry &child = this->entries_[child_index];
        if (child.state == ProcessTreeState::Unused ||
            child.parent_process_index != parent_process_index ||
            !this->MatchesRequestedProcess(child, requested_process_id)) {
            continue;
        }
        matching_child_found = true;
        uint64_t parent_process_id = OS_KERNEL_PROCESS_TREE_EMPTY_VALUE;
        if (child.parent_process_index < this->capacity_) {
            parent_process_id = this->entries_[child.parent_process_index].process_id;
        }
        if ((wait_flags & OS_KERNEL_PROCESS_TREE_WAIT_STOPPED_FLAG) !=
                OS_KERNEL_PROCESS_TREE_EMPTY_VALUE &&
            child.stopped_event_pending) {
            wait_result = ProcessTreeWaitEventResult{
                .process_id = child.process_id,
                .process_index = child_index,
                .parent_process_id = parent_process_id,
                .event_type = ProcessTreeEventType::Stopped,
                .exit_status = {},
                .signal_number = child.stop_signal_number,
            };
            child.stopped_event_pending = false;
            if (child.state != ProcessTreeState::Stopped) {
                child.stop_signal_number = OS_KERNEL_PROCESS_TREE_EMPTY_VALUE;
            }
            this->statistics_.wait_success_count += OS_KERNEL_PROCESS_TREE_COUNTER_INCREMENT;
            this->statistics_.observed_stopped_event_count +=
                OS_KERNEL_PROCESS_TREE_COUNTER_INCREMENT;
            return ProcessTreeStatus::Succeeded;
        }
        if ((wait_flags & OS_KERNEL_PROCESS_TREE_WAIT_CONTINUED_FLAG) !=
                OS_KERNEL_PROCESS_TREE_EMPTY_VALUE &&
            child.continued_event_pending) {
            wait_result = ProcessTreeWaitEventResult{
                .process_id = child.process_id,
                .process_index = child_index,
                .parent_process_id = parent_process_id,
                .event_type = ProcessTreeEventType::Continued,
                .exit_status = {},
                .signal_number = OS_KERNEL_PROCESS_TREE_EMPTY_VALUE,
            };
            child.continued_event_pending = false;
            this->statistics_.wait_success_count += OS_KERNEL_PROCESS_TREE_COUNTER_INCREMENT;
            this->statistics_.observed_continued_event_count +=
                OS_KERNEL_PROCESS_TREE_COUNTER_INCREMENT;
            return ProcessTreeStatus::Succeeded;
        }
        if ((wait_flags & OS_KERNEL_PROCESS_TREE_WAIT_EXITED_FLAG) !=
                OS_KERNEL_PROCESS_TREE_EMPTY_VALUE &&
            child.state == ProcessTreeState::Zombie) {
            ProcessTreeWaitResult exit_result{};
            const ProcessTreeStatus collect_status = this->Collect(child_index, exit_result, true);
            if (collect_status != ProcessTreeStatus::Succeeded) {
                return collect_status;
            }
            wait_result = ProcessTreeWaitEventResult{
                .process_id = exit_result.process_id,
                .process_index = exit_result.process_index,
                .parent_process_id = exit_result.parent_process_id,
                .event_type = ProcessTreeEventType::Exited,
                .exit_status = exit_result.exit_status,
                .signal_number = OS_KERNEL_PROCESS_TREE_EMPTY_VALUE,
            };
            return ProcessTreeStatus::Succeeded;
        }
    }
    if (matching_child_found) {
        this->statistics_.wait_block_count += OS_KERNEL_PROCESS_TREE_COUNTER_INCREMENT;
        return ProcessTreeStatus::ChildStillRunning;
    }
    this->statistics_.wait_no_child_count += OS_KERNEL_PROCESS_TREE_COUNTER_INCREMENT;
    return ProcessTreeStatus::NoMatchingChild;
}

ProcessTreeStatus ProcessTree::CollectInit(ProcessTreeWaitResult &wait_result) noexcept {
    wait_result = ProcessTreeWaitResult{};
    if (!this->initialized_) {
        return ProcessTreeStatus::NotInitialized;
    }
    if (this->init_process_index_ == OS_KERNEL_PROCESS_TREE_INVALID_INDEX) {
        return ProcessTreeStatus::InitRequired;
    }
    if (this->entries_[this->init_process_index_].state != ProcessTreeState::Zombie) {
        return ProcessTreeStatus::InvalidState;
    }
    if (this->HasChild(this->init_process_index_)) {
        return ProcessTreeStatus::ProcessHasChildren;
    }
    return this->Collect(this->init_process_index_, wait_result, false);
}

ProcessTreeStatus ProcessTree::Read(const uint64_t process_index,
                                    ProcessTreeEntry &entry) const noexcept {
    entry = ProcessTreeEntry{};
    if (!this->initialized_) {
        return ProcessTreeStatus::NotInitialized;
    }
    if (process_index >= this->capacity_) {
        return ProcessTreeStatus::InvalidProcessIndex;
    }
    entry = this->entries_[process_index];
    return entry.state == ProcessTreeState::Unused ? ProcessTreeStatus::ProcessNotRegistered
                                                   : ProcessTreeStatus::Succeeded;
}

ProcessTreeStatus ProcessTree::Validate() const noexcept {
    if (!this->initialized_) {
        return ProcessTreeStatus::NotInitialized;
    }
    uint64_t active_process_count = OS_KERNEL_PROCESS_TREE_EMPTY_VALUE;
    uint64_t alive_process_count = OS_KERNEL_PROCESS_TREE_EMPTY_VALUE;
    uint64_t stopped_process_count = OS_KERNEL_PROCESS_TREE_EMPTY_VALUE;
    uint64_t zombie_process_count = OS_KERNEL_PROCESS_TREE_EMPTY_VALUE;
    uint64_t init_count = OS_KERNEL_PROCESS_TREE_EMPTY_VALUE;
    for (uint64_t process_index = OS_KERNEL_PROCESS_TREE_EMPTY_VALUE;
         process_index < this->capacity_; ++process_index) {
        const ProcessTreeEntry &entry = this->entries_[process_index];
        if (entry.state == ProcessTreeState::Unused) {
            if (entry.process_id != OS_KERNEL_PROCESS_TREE_EMPTY_VALUE ||
                entry.parent_process_index != OS_KERNEL_PROCESS_TREE_INVALID_INDEX ||
                entry.exit_status.termination_reason != ProcessTreeTerminationReason::None ||
                entry.stop_signal_number != OS_KERNEL_PROCESS_TREE_EMPTY_VALUE ||
                entry.stopped_event_pending || entry.continued_event_pending) {
                return ProcessTreeStatus::CorruptedState;
            }
            continue;
        }
        ++active_process_count;
        if (entry.process_id == OS_KERNEL_PROCESS_TREE_EMPTY_VALUE ||
            entry.process_id == OS_KERNEL_PROCESS_TREE_WAIT_ANY_PROCESS_ID) {
            return ProcessTreeStatus::CorruptedState;
        }
        if (process_index == this->init_process_index_) {
            ++init_count;
            if (entry.process_id != OS_KERNEL_PROCESS_TREE_FIRST_PROCESS_ID ||
                entry.parent_process_index != OS_KERNEL_PROCESS_TREE_INVALID_INDEX) {
                return ProcessTreeStatus::CorruptedState;
            }
        } else if (entry.parent_process_index >= this->capacity_ ||
                   this->entries_[entry.parent_process_index].state == ProcessTreeState::Unused) {
            return ProcessTreeStatus::CorruptedState;
        }
        for (uint64_t previous_index = OS_KERNEL_PROCESS_TREE_EMPTY_VALUE;
             previous_index < process_index; ++previous_index) {
            if (this->entries_[previous_index].state != ProcessTreeState::Unused &&
                this->entries_[previous_index].process_id == entry.process_id) {
                return ProcessTreeStatus::CorruptedState;
            }
        }
        if (entry.state == ProcessTreeState::Alive) {
            ++alive_process_count;
            if (entry.exit_status.termination_reason != ProcessTreeTerminationReason::None ||
                (entry.stop_signal_number != OS_KERNEL_PROCESS_TREE_EMPTY_VALUE &&
                 !entry.stopped_event_pending)) {
                return ProcessTreeStatus::CorruptedState;
            }
        } else if (entry.state == ProcessTreeState::Stopped) {
            ++stopped_process_count;
            if (entry.exit_status.termination_reason != ProcessTreeTerminationReason::None ||
                entry.stop_signal_number == OS_KERNEL_PROCESS_TREE_EMPTY_VALUE) {
                return ProcessTreeStatus::CorruptedState;
            }
        } else if (entry.state == ProcessTreeState::Zombie) {
            ++zombie_process_count;
            if (entry.exit_status.termination_reason == ProcessTreeTerminationReason::None ||
                (entry.stop_signal_number != OS_KERNEL_PROCESS_TREE_EMPTY_VALUE &&
                 !entry.stopped_event_pending)) {
                return ProcessTreeStatus::CorruptedState;
            }
        } else {
            return ProcessTreeStatus::CorruptedState;
        }
    }
    if ((this->init_process_index_ == OS_KERNEL_PROCESS_TREE_INVALID_INDEX) !=
            (init_count == OS_KERNEL_PROCESS_TREE_EMPTY_VALUE) ||
        init_count > OS_KERNEL_PROCESS_TREE_COUNTER_INCREMENT ||
        active_process_count != this->statistics_.active_process_count ||
        alive_process_count != this->statistics_.alive_process_count ||
        stopped_process_count != this->statistics_.stopped_process_count ||
        zombie_process_count != this->statistics_.zombie_process_count ||
        this->statistics_.registered_process_count !=
            this->statistics_.active_process_count + this->statistics_.collected_process_count ||
        this->statistics_.exited_process_count !=
            this->statistics_.zombie_process_count + this->statistics_.collected_process_count ||
        this->statistics_.wait_success_count > this->statistics_.wait_attempt_count) {
        return ProcessTreeStatus::CorruptedState;
    }
    if (this->statistics_.observed_stopped_event_count >
            this->statistics_.stopped_event_count ||
        this->statistics_.observed_continued_event_count >
            this->statistics_.continued_event_count) {
        return ProcessTreeStatus::CorruptedState;
    }
    return ProcessTreeStatus::Succeeded;
}

ProcessTreeStatistics ProcessTree::Statistics() const noexcept { return this->statistics_; }

uint64_t ProcessTree::InitProcessIndex() const noexcept { return this->init_process_index_; }

bool ProcessTree::IsInitialized() const noexcept { return this->initialized_; }

bool ProcessTree::IsRegistered(const uint64_t process_index) const noexcept {
    return process_index < this->capacity_ &&
           this->entries_[process_index].state != ProcessTreeState::Unused;
}

bool ProcessTree::HasChild(const uint64_t parent_process_index) const noexcept {
    for (uint64_t process_index = OS_KERNEL_PROCESS_TREE_EMPTY_VALUE;
         process_index < this->capacity_; ++process_index) {
        if (this->entries_[process_index].state != ProcessTreeState::Unused &&
            this->entries_[process_index].parent_process_index == parent_process_index) {
            return true;
        }
    }
    return false;
}

bool ProcessTree::MatchesRequestedProcess(const ProcessTreeEntry &entry,
                                          const uint64_t requested_process_id) const noexcept {
    return requested_process_id == OS_KERNEL_PROCESS_TREE_WAIT_ANY_PROCESS_ID ||
           entry.process_id == requested_process_id;
}

ProcessTreeStatus ProcessTree::Collect(const uint64_t process_index,
                                       ProcessTreeWaitResult &wait_result,
                                       const bool record_wait_success) noexcept {
    if (process_index >= this->capacity_ ||
        this->entries_[process_index].state != ProcessTreeState::Zombie) {
        return ProcessTreeStatus::InvalidState;
    }
    const ProcessTreeEntry collected = this->entries_[process_index];
    uint64_t parent_process_id = OS_KERNEL_PROCESS_TREE_EMPTY_VALUE;
    if (collected.parent_process_index < this->capacity_ &&
        this->entries_[collected.parent_process_index].state != ProcessTreeState::Unused) {
        parent_process_id = this->entries_[collected.parent_process_index].process_id;
    }
    wait_result = ProcessTreeWaitResult{
        .process_id = collected.process_id,
        .process_index = process_index,
        .parent_process_id = parent_process_id,
        .exit_status = collected.exit_status,
    };
    this->entries_[process_index] = EmptyProcessTreeEntry();
    if (process_index == this->init_process_index_) {
        this->init_process_index_ = OS_KERNEL_PROCESS_TREE_INVALID_INDEX;
    }
    --this->statistics_.active_process_count;
    --this->statistics_.zombie_process_count;
    this->statistics_.collected_process_count += OS_KERNEL_PROCESS_TREE_COUNTER_INCREMENT;
    if (record_wait_success) {
        this->statistics_.wait_success_count += OS_KERNEL_PROCESS_TREE_COUNTER_INCREMENT;
    }
    return ProcessTreeStatus::Succeeded;
}

}

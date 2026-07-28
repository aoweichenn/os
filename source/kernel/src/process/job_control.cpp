#include "os/kernel/process/job_control.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_JOB_CONTROL_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_JOB_CONTROL_FIRST_PROCESS_ID = 1ULL;
constexpr uint64_t OS_KERNEL_JOB_CONTROL_COUNTER_INCREMENT = 1ULL;

}

JobControlStatus JobControlManager::Initialize(
    JobControlProcessState *const process_storage, const uint64_t capacity) noexcept {
    if (this->initialized_) {
        return JobControlStatus::AlreadyInitialized;
    }
    if (process_storage == nullptr) {
        return JobControlStatus::InvalidStorage;
    }
    if (capacity == OS_KERNEL_JOB_CONTROL_EMPTY_VALUE) {
        return JobControlStatus::InvalidCapacity;
    }
    this->processes_ = process_storage;
    this->capacity_ = capacity;
    this->statistics_ = JobControlStatistics{};
    this->statistics_.capacity = capacity;
    for (uint64_t process_index = OS_KERNEL_JOB_CONTROL_EMPTY_VALUE;
         process_index < capacity; ++process_index) {
        this->processes_[process_index] = JobControlProcessState{};
    }
    this->initialized_ = true;
    return JobControlStatus::Succeeded;
}

JobControlStatus JobControlManager::RegisterInit(const uint64_t process_index,
                                                 const uint64_t process_id) noexcept {
    if (!this->initialized_) {
        return JobControlStatus::NotInitialized;
    }
    if (process_index >= this->capacity_) {
        return JobControlStatus::InvalidProcessIndex;
    }
    if (process_id != OS_KERNEL_JOB_CONTROL_FIRST_PROCESS_ID) {
        return JobControlStatus::InvalidProcessId;
    }
    if (this->processes_[process_index].active) {
        return JobControlStatus::ProcessAlreadyRegistered;
    }
    this->processes_[process_index] = JobControlProcessState{
        .process_id = process_id,
        .process_group_id = process_id,
        .session_id = process_id,
        .active = true,
        .session_leader = true,
    };
    this->statistics_.active_process_count += OS_KERNEL_JOB_CONTROL_COUNTER_INCREMENT;
    this->statistics_.session_create_count += OS_KERNEL_JOB_CONTROL_COUNTER_INCREMENT;
    return JobControlStatus::Succeeded;
}

JobControlStatus JobControlManager::ForkProcess(const uint64_t parent_process_index,
                                                const uint64_t child_process_index,
                                                const uint64_t child_process_id) noexcept {
    if (!this->initialized_) {
        return JobControlStatus::NotInitialized;
    }
    if (parent_process_index >= this->capacity_ || child_process_index >= this->capacity_) {
        return JobControlStatus::InvalidProcessIndex;
    }
    if (!this->processes_[parent_process_index].active) {
        return JobControlStatus::ProcessNotFound;
    }
    if (child_process_id == OS_KERNEL_JOB_CONTROL_EMPTY_VALUE) {
        return JobControlStatus::InvalidProcessId;
    }
    if (this->processes_[child_process_index].active) {
        return JobControlStatus::ProcessAlreadyRegistered;
    }
    uint64_t duplicate_process_index = OS_KERNEL_JOB_CONTROL_INVALID_INDEX;
    if (this->FindProcess(child_process_id, duplicate_process_index) ==
        JobControlStatus::Succeeded) {
        return JobControlStatus::InvalidProcessId;
    }
    const JobControlProcessState &parent = this->processes_[parent_process_index];
    this->processes_[child_process_index] = JobControlProcessState{
        .process_id = child_process_id,
        .process_group_id = parent.process_group_id,
        .session_id = parent.session_id,
        .active = true,
        .session_leader = false,
    };
    this->statistics_.active_process_count += OS_KERNEL_JOB_CONTROL_COUNTER_INCREMENT;
    return JobControlStatus::Succeeded;
}

JobControlStatus JobControlManager::RemoveProcess(const uint64_t process_index) noexcept {
    if (!this->initialized_) {
        return JobControlStatus::NotInitialized;
    }
    if (process_index >= this->capacity_) {
        return JobControlStatus::InvalidProcessIndex;
    }
    if (!this->processes_[process_index].active) {
        return JobControlStatus::ProcessNotFound;
    }
    this->processes_[process_index] = JobControlProcessState{};
    --this->statistics_.active_process_count;
    return JobControlStatus::Succeeded;
}

JobControlStatus JobControlManager::CreateSession(const uint64_t process_index,
                                                  uint64_t &session_id) noexcept {
    session_id = OS_KERNEL_JOB_CONTROL_EMPTY_VALUE;
    if (!this->initialized_) {
        return JobControlStatus::NotInitialized;
    }
    if (process_index >= this->capacity_) {
        return JobControlStatus::InvalidProcessIndex;
    }
    JobControlProcessState &process = this->processes_[process_index];
    if (!process.active) {
        return JobControlStatus::ProcessNotFound;
    }
    if (process.session_leader) {
        return JobControlStatus::SessionLeader;
    }
    if (process.process_group_id == process.process_id) {
        return JobControlStatus::ProcessGroupLeader;
    }
    process.process_group_id = process.process_id;
    process.session_id = process.process_id;
    process.session_leader = true;
    session_id = process.session_id;
    this->statistics_.session_create_count += OS_KERNEL_JOB_CONTROL_COUNTER_INCREMENT;
    this->statistics_.process_group_change_count +=
        OS_KERNEL_JOB_CONTROL_COUNTER_INCREMENT;
    return JobControlStatus::Succeeded;
}

JobControlStatus JobControlManager::SetProcessGroup(
    const uint64_t caller_process_index, const uint64_t target_process_index,
    const uint64_t process_group_id) noexcept {
    if (!this->initialized_) {
        return JobControlStatus::NotInitialized;
    }
    if (caller_process_index >= this->capacity_ || target_process_index >= this->capacity_) {
        return JobControlStatus::InvalidProcessIndex;
    }
    const JobControlProcessState &caller = this->processes_[caller_process_index];
    JobControlProcessState &target = this->processes_[target_process_index];
    if (!caller.active || !target.active) {
        return JobControlStatus::ProcessNotFound;
    }
    if (caller.session_id != target.session_id) {
        return JobControlStatus::PermissionDenied;
    }
    if (target.session_leader) {
        return JobControlStatus::SessionLeader;
    }
    if (process_group_id == OS_KERNEL_JOB_CONTROL_EMPTY_VALUE ||
        (process_group_id != target.process_id &&
         !this->ProcessGroupExists(process_group_id, target.session_id))) {
        return JobControlStatus::InvalidProcessGroup;
    }
    target.process_group_id = process_group_id;
    this->statistics_.process_group_change_count +=
        OS_KERNEL_JOB_CONTROL_COUNTER_INCREMENT;
    return JobControlStatus::Succeeded;
}

JobControlStatus JobControlManager::ReadProcess(
    const uint64_t process_index, JobControlProcessState &state) const noexcept {
    state = JobControlProcessState{};
    if (!this->initialized_) {
        return JobControlStatus::NotInitialized;
    }
    if (process_index >= this->capacity_) {
        return JobControlStatus::InvalidProcessIndex;
    }
    if (!this->processes_[process_index].active) {
        return JobControlStatus::ProcessNotFound;
    }
    state = this->processes_[process_index];
    return JobControlStatus::Succeeded;
}

JobControlStatus JobControlManager::FindProcess(const uint64_t process_id,
                                               uint64_t &process_index) const noexcept {
    process_index = OS_KERNEL_JOB_CONTROL_INVALID_INDEX;
    if (!this->initialized_) {
        return JobControlStatus::NotInitialized;
    }
    if (process_id == OS_KERNEL_JOB_CONTROL_EMPTY_VALUE) {
        return JobControlStatus::InvalidProcessId;
    }
    for (uint64_t candidate_index = OS_KERNEL_JOB_CONTROL_EMPTY_VALUE;
         candidate_index < this->capacity_; ++candidate_index) {
        if (this->processes_[candidate_index].active &&
            this->processes_[candidate_index].process_id == process_id) {
            process_index = candidate_index;
            return JobControlStatus::Succeeded;
        }
    }
    return JobControlStatus::ProcessNotFound;
}

bool JobControlManager::GroupBelongsToSession(const uint64_t process_group_id,
                                              const uint64_t session_id) const noexcept {
    return this->initialized_ &&
           this->ProcessGroupExists(process_group_id, session_id);
}

JobControlStatus JobControlManager::Validate() const noexcept {
    if (!this->initialized_) {
        return JobControlStatus::NotInitialized;
    }
    uint64_t active_process_count = OS_KERNEL_JOB_CONTROL_EMPTY_VALUE;
    for (uint64_t process_index = OS_KERNEL_JOB_CONTROL_EMPTY_VALUE;
         process_index < this->capacity_; ++process_index) {
        const JobControlProcessState &process = this->processes_[process_index];
        if (!process.active) {
            if (process.process_id != OS_KERNEL_JOB_CONTROL_EMPTY_VALUE ||
                process.process_group_id != OS_KERNEL_JOB_CONTROL_EMPTY_VALUE ||
                process.session_id != OS_KERNEL_JOB_CONTROL_EMPTY_VALUE ||
                process.session_leader) {
                return JobControlStatus::CorruptedState;
            }
            continue;
        }
        ++active_process_count;
        if (process.process_id == OS_KERNEL_JOB_CONTROL_EMPTY_VALUE ||
            process.process_group_id == OS_KERNEL_JOB_CONTROL_EMPTY_VALUE ||
            process.session_id == OS_KERNEL_JOB_CONTROL_EMPTY_VALUE ||
            (process.session_leader &&
             (process.process_id != process.session_id ||
              process.process_group_id != process.process_id)) ||
            !this->ProcessGroupExists(process.process_group_id, process.session_id)) {
            return JobControlStatus::CorruptedState;
        }
        for (uint64_t previous_index = OS_KERNEL_JOB_CONTROL_EMPTY_VALUE;
             previous_index < process_index; ++previous_index) {
            if (this->processes_[previous_index].active &&
                this->processes_[previous_index].process_id == process.process_id) {
                return JobControlStatus::CorruptedState;
            }
        }
    }
    return active_process_count == this->statistics_.active_process_count
               ? JobControlStatus::Succeeded
               : JobControlStatus::CorruptedState;
}

JobControlStatistics JobControlManager::Statistics() const noexcept {
    JobControlStatistics statistics = this->statistics_;
    for (uint64_t process_index = OS_KERNEL_JOB_CONTROL_EMPTY_VALUE;
         process_index < this->capacity_; ++process_index) {
        const JobControlProcessState &process = this->processes_[process_index];
        if (!process.active) {
            continue;
        }
        if (process.session_leader) {
            ++statistics.active_session_count;
        }
        bool first_group_member = true;
        for (uint64_t previous_index = OS_KERNEL_JOB_CONTROL_EMPTY_VALUE;
             previous_index < process_index; ++previous_index) {
            if (this->processes_[previous_index].active &&
                this->processes_[previous_index].session_id == process.session_id &&
                this->processes_[previous_index].process_group_id == process.process_group_id) {
                first_group_member = false;
                break;
            }
        }
        if (first_group_member) {
            ++statistics.active_process_group_count;
        }
    }
    return statistics;
}

bool JobControlManager::ProcessGroupExists(const uint64_t process_group_id,
                                           const uint64_t session_id) const noexcept {
    for (uint64_t process_index = OS_KERNEL_JOB_CONTROL_EMPTY_VALUE;
         process_index < this->capacity_; ++process_index) {
        if (this->processes_[process_index].active &&
            this->processes_[process_index].process_group_id == process_group_id &&
            this->processes_[process_index].session_id == session_id) {
            return true;
        }
    }
    return false;
}

}

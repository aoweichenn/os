#include "os/kernel/process/signal_manager.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_SIGNAL_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_SIGNAL_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_KERNEL_SIGNAL_FIRST_INDEX = 0ULL;
constexpr uint64_t OS_KERNEL_SIGNAL_FIRST_FRAME_COOKIE = 1ULL;

[[nodiscard]] uint64_t LowestSignalNumber(const uint64_t signal_set) noexcept {
    for (uint64_t signal_number = os::abi::OS_ABI_SIGNAL_MINIMUM_NUMBER;
         signal_number <= os::abi::OS_ABI_SIGNAL_MAXIMUM_NUMBER; ++signal_number) {
        if ((signal_set & os::abi::SignalBit(signal_number)) != OS_KERNEL_SIGNAL_EMPTY_VALUE) {
            return signal_number;
        }
    }
    return OS_KERNEL_SIGNAL_EMPTY_VALUE;
}

[[nodiscard]] SignalThreadState EmptyThreadState() noexcept {
    return SignalThreadState{
        .thread_id = OS_KERNEL_SIGNAL_EMPTY_VALUE,
        .process_index = OS_KERNEL_SIGNAL_INVALID_INDEX,
        .signal_mask = OS_KERNEL_SIGNAL_EMPTY_VALUE,
        .pending_set = OS_KERNEL_SIGNAL_EMPTY_VALUE,
        .active_frame_address = OS_KERNEL_SIGNAL_EMPTY_VALUE,
        .active_frame_cookie = OS_KERNEL_SIGNAL_EMPTY_VALUE,
        .active_signal_number = OS_KERNEL_SIGNAL_EMPTY_VALUE,
        .active_restorer_address = OS_KERNEL_SIGNAL_EMPTY_VALUE,
        .active_previous_mask = OS_KERNEL_SIGNAL_EMPTY_VALUE,
        .active = false,
        .frame_active = false,
    };
}

}

SignalManagerStatus SignalManager::Initialize(SignalProcessState *const process_storage,
                                              const uint64_t process_capacity,
                                              SignalThreadState *const thread_storage,
                                              const uint64_t thread_capacity) noexcept {
    if (this->initialized_) {
        return SignalManagerStatus::AlreadyInitialized;
    }
    if (process_storage == nullptr || thread_storage == nullptr) {
        return SignalManagerStatus::InvalidStorage;
    }
    if (process_capacity == OS_KERNEL_SIGNAL_EMPTY_VALUE ||
        thread_capacity == OS_KERNEL_SIGNAL_EMPTY_VALUE) {
        return SignalManagerStatus::InvalidCapacity;
    }
    this->processes_ = process_storage;
    this->threads_ = thread_storage;
    this->process_capacity_ = process_capacity;
    this->thread_capacity_ = thread_capacity;
    this->next_frame_cookie_ = OS_KERNEL_SIGNAL_FIRST_FRAME_COOKIE;
    this->statistics_ = SignalManagerStatistics{};
    for (uint64_t process_index = OS_KERNEL_SIGNAL_FIRST_INDEX;
         process_index < this->process_capacity_; ++process_index) {
        this->processes_[process_index] = SignalProcessState{};
    }
    for (uint64_t thread_index = OS_KERNEL_SIGNAL_FIRST_INDEX;
         thread_index < this->thread_capacity_; ++thread_index) {
        this->threads_[thread_index] = EmptyThreadState();
    }
    this->initialized_ = true;
    return SignalManagerStatus::Succeeded;
}

SignalManagerStatus SignalManager::RegisterProcess(const uint64_t process_index,
                                                   const uint64_t process_id,
                                                   const uint64_t process_group_id) noexcept {
    if (!this->initialized_) {
        return SignalManagerStatus::NotInitialized;
    }
    if (process_index >= this->process_capacity_) {
        return SignalManagerStatus::InvalidProcessIndex;
    }
    if (process_id == OS_KERNEL_SIGNAL_EMPTY_VALUE) {
        return SignalManagerStatus::InvalidProcessId;
    }
    if (process_group_id == OS_KERNEL_SIGNAL_EMPTY_VALUE) {
        return SignalManagerStatus::InvalidProcessGroup;
    }
    if (this->processes_[process_index].active) {
        return SignalManagerStatus::ProcessAlreadyRegistered;
    }
    for (uint64_t candidate_index = OS_KERNEL_SIGNAL_FIRST_INDEX;
         candidate_index < this->process_capacity_; ++candidate_index) {
        if (this->processes_[candidate_index].active &&
            this->processes_[candidate_index].process_id == process_id) {
            return SignalManagerStatus::InvalidProcessId;
        }
    }
    this->processes_[process_index] = SignalProcessState{
        .process_id = process_id,
        .process_group_id = process_group_id,
        .pending_set = OS_KERNEL_SIGNAL_EMPTY_VALUE,
        .next_thread_index = OS_KERNEL_SIGNAL_FIRST_INDEX,
        .actions = {},
        .active = true,
    };
    this->statistics_.active_process_count += OS_KERNEL_SIGNAL_COUNTER_INCREMENT;
    return SignalManagerStatus::Succeeded;
}

SignalManagerStatus SignalManager::ForkProcess(const uint64_t parent_process_index,
                                               const uint64_t child_process_index,
                                               const uint64_t child_process_id) noexcept {
    if (!this->initialized_) {
        return SignalManagerStatus::NotInitialized;
    }
    if (parent_process_index >= this->process_capacity_ ||
        child_process_index >= this->process_capacity_) {
        return SignalManagerStatus::InvalidProcessIndex;
    }
    const SignalProcessState &parent = this->processes_[parent_process_index];
    if (!parent.active) {
        return SignalManagerStatus::ProcessNotFound;
    }
    const SignalManagerStatus register_status =
        this->RegisterProcess(child_process_index, child_process_id, parent.process_group_id);
    if (register_status != SignalManagerStatus::Succeeded) {
        return register_status;
    }
    for (uint64_t action_index = OS_KERNEL_SIGNAL_FIRST_INDEX;
         action_index < OS_KERNEL_SIGNAL_ACTION_CAPACITY; ++action_index) {
        this->processes_[child_process_index].actions[action_index] = parent.actions[action_index];
    }
    return SignalManagerStatus::Succeeded;
}

SignalManagerStatus SignalManager::ExecProcess(const uint64_t process_index,
                                               const uint64_t surviving_thread_index) noexcept {
    if (!this->initialized_) {
        return SignalManagerStatus::NotInitialized;
    }
    if (process_index >= this->process_capacity_) {
        return SignalManagerStatus::InvalidProcessIndex;
    }
    if (surviving_thread_index >= this->thread_capacity_) {
        return SignalManagerStatus::InvalidThreadIndex;
    }
    SignalProcessState &process = this->processes_[process_index];
    SignalThreadState &survivor = this->threads_[surviving_thread_index];
    if (!process.active || !survivor.active || survivor.process_index != process_index) {
        return SignalManagerStatus::ProcessNotFound;
    }
    for (uint64_t action_index = OS_KERNEL_SIGNAL_FIRST_INDEX;
         action_index < OS_KERNEL_SIGNAL_ACTION_CAPACITY; ++action_index) {
        os::abi::SignalAction &action = process.actions[action_index];
        if (action.disposition == os::abi::SignalDisposition::Handler) {
            action = os::abi::SignalAction{};
        }
    }
    // exec 只替换用户映像，不会撤销已经发生的异步事实。保留 Process pending
    // 与存活 Thread pending，避免控制终端在 fork/exec 窗口投递的信号丢失。
    survivor.active_frame_address = OS_KERNEL_SIGNAL_EMPTY_VALUE;
    survivor.active_frame_cookie = OS_KERNEL_SIGNAL_EMPTY_VALUE;
    survivor.active_signal_number = OS_KERNEL_SIGNAL_EMPTY_VALUE;
    survivor.active_restorer_address = OS_KERNEL_SIGNAL_EMPTY_VALUE;
    survivor.active_previous_mask = OS_KERNEL_SIGNAL_EMPTY_VALUE;
    survivor.frame_active = false;
    for (uint64_t thread_index = OS_KERNEL_SIGNAL_FIRST_INDEX;
         thread_index < this->thread_capacity_; ++thread_index) {
        if (thread_index != surviving_thread_index && this->threads_[thread_index].active &&
            this->threads_[thread_index].process_index == process_index) {
            this->threads_[thread_index] = EmptyThreadState();
            this->statistics_.active_thread_count -= OS_KERNEL_SIGNAL_COUNTER_INCREMENT;
        }
    }
    return SignalManagerStatus::Succeeded;
}

SignalManagerStatus SignalManager::RemoveProcess(const uint64_t process_index) noexcept {
    if (!this->initialized_) {
        return SignalManagerStatus::NotInitialized;
    }
    if (process_index >= this->process_capacity_) {
        return SignalManagerStatus::InvalidProcessIndex;
    }
    SignalProcessState &process = this->processes_[process_index];
    if (!process.active) {
        return SignalManagerStatus::ProcessNotFound;
    }
    for (uint64_t thread_index = OS_KERNEL_SIGNAL_FIRST_INDEX;
         thread_index < this->thread_capacity_; ++thread_index) {
        if (this->threads_[thread_index].active &&
            this->threads_[thread_index].process_index == process_index) {
            return SignalManagerStatus::ProcessThreadsRemain;
        }
    }
    process = SignalProcessState{};
    this->statistics_.active_process_count -= OS_KERNEL_SIGNAL_COUNTER_INCREMENT;
    return SignalManagerStatus::Succeeded;
}

SignalManagerStatus SignalManager::RegisterThread(const uint64_t thread_index,
                                                  const uint64_t process_index,
                                                  const uint64_t thread_id,
                                                  const uint64_t signal_mask) noexcept {
    if (!this->initialized_) {
        return SignalManagerStatus::NotInitialized;
    }
    if (thread_index >= this->thread_capacity_) {
        return SignalManagerStatus::InvalidThreadIndex;
    }
    if (process_index >= this->process_capacity_) {
        return SignalManagerStatus::InvalidProcessIndex;
    }
    if (thread_id == OS_KERNEL_SIGNAL_EMPTY_VALUE) {
        return SignalManagerStatus::InvalidThreadId;
    }
    if (!this->processes_[process_index].active) {
        return SignalManagerStatus::ProcessNotFound;
    }
    if (this->threads_[thread_index].active) {
        return SignalManagerStatus::ThreadAlreadyRegistered;
    }
    this->threads_[thread_index] = SignalThreadState{
        .thread_id = thread_id,
        .process_index = process_index,
        .signal_mask =
            signal_mask & os::abi::OS_ABI_SIGNAL_VALID_SET & ~os::abi::OS_ABI_SIGNAL_UNMASKABLE_SET,
        .pending_set = OS_KERNEL_SIGNAL_EMPTY_VALUE,
        .active_frame_address = OS_KERNEL_SIGNAL_EMPTY_VALUE,
        .active_frame_cookie = OS_KERNEL_SIGNAL_EMPTY_VALUE,
        .active_signal_number = OS_KERNEL_SIGNAL_EMPTY_VALUE,
        .active_restorer_address = OS_KERNEL_SIGNAL_EMPTY_VALUE,
        .active_previous_mask = OS_KERNEL_SIGNAL_EMPTY_VALUE,
        .active = true,
        .frame_active = false,
    };
    this->statistics_.active_thread_count += OS_KERNEL_SIGNAL_COUNTER_INCREMENT;
    this->AssignAllEligiblePending(process_index);
    return SignalManagerStatus::Succeeded;
}

SignalManagerStatus SignalManager::RemoveThread(const uint64_t thread_index) noexcept {
    if (!this->initialized_) {
        return SignalManagerStatus::NotInitialized;
    }
    if (thread_index >= this->thread_capacity_) {
        return SignalManagerStatus::InvalidThreadIndex;
    }
    SignalThreadState &thread = this->threads_[thread_index];
    if (!thread.active) {
        return SignalManagerStatus::ThreadNotFound;
    }
    const uint64_t process_index = thread.process_index;
    if (process_index >= this->process_capacity_ || !this->processes_[process_index].active) {
        return SignalManagerStatus::CorruptedState;
    }
    this->processes_[process_index].pending_set |= thread.pending_set;
    thread = EmptyThreadState();
    this->statistics_.active_thread_count -= OS_KERNEL_SIGNAL_COUNTER_INCREMENT;
    this->AssignAllEligiblePending(process_index);
    return SignalManagerStatus::Succeeded;
}

SignalManagerStatus SignalManager::SetAction(const uint64_t process_index,
                                             const uint64_t signal_number,
                                             const os::abi::SignalAction &action,
                                             os::abi::SignalAction &previous_action) noexcept {
    previous_action = os::abi::SignalAction{};
    if (!this->initialized_) {
        return SignalManagerStatus::NotInitialized;
    }
    if (process_index >= this->process_capacity_) {
        return SignalManagerStatus::InvalidProcessIndex;
    }
    if (!this->processes_[process_index].active) {
        return SignalManagerStatus::ProcessNotFound;
    }
    if (!this->SignalNumberIsValid(signal_number)) {
        return SignalManagerStatus::InvalidSignal;
    }
    if (!this->ActionIsValid(signal_number, action)) {
        return SignalManagerStatus::InvalidAction;
    }
    const uint64_t action_index = signal_number - os::abi::OS_ABI_SIGNAL_MINIMUM_NUMBER;
    previous_action = this->processes_[process_index].actions[action_index];
    this->processes_[process_index].actions[action_index] = action;
    if (action.disposition == os::abi::SignalDisposition::Ignore ||
        (action.disposition == os::abi::SignalDisposition::Default &&
         this->DefaultDispositionIgnores(signal_number))) {
        const uint64_t signal_bit = os::abi::SignalBit(signal_number);
        this->processes_[process_index].pending_set &= ~signal_bit;
        for (uint64_t thread_index = OS_KERNEL_SIGNAL_FIRST_INDEX;
             thread_index < this->thread_capacity_; ++thread_index) {
            if (this->threads_[thread_index].active &&
                this->threads_[thread_index].process_index == process_index) {
                this->threads_[thread_index].pending_set &= ~signal_bit;
            }
        }
    }
    return SignalManagerStatus::Succeeded;
}

SignalManagerStatus SignalManager::SetThreadMask(const uint64_t thread_index,
                                                 const uint64_t signal_mask,
                                                 uint64_t &previous_signal_mask) noexcept {
    previous_signal_mask = OS_KERNEL_SIGNAL_EMPTY_VALUE;
    if (!this->initialized_) {
        return SignalManagerStatus::NotInitialized;
    }
    if (thread_index >= this->thread_capacity_) {
        return SignalManagerStatus::InvalidThreadIndex;
    }
    SignalThreadState &thread = this->threads_[thread_index];
    if (!thread.active) {
        return SignalManagerStatus::ThreadNotFound;
    }
    previous_signal_mask = thread.signal_mask;
    thread.signal_mask =
        signal_mask & os::abi::OS_ABI_SIGNAL_VALID_SET & ~os::abi::OS_ABI_SIGNAL_UNMASKABLE_SET;
    this->AssignAllEligiblePending(thread.process_index);
    return SignalManagerStatus::Succeeded;
}

SignalManagerStatus SignalManager::SendToProcess(const uint64_t process_id,
                                                 const uint64_t signal_number,
                                                 uint64_t &selected_thread_index) noexcept {
    selected_thread_index = OS_KERNEL_SIGNAL_INVALID_INDEX;
    uint64_t process_index = OS_KERNEL_SIGNAL_INVALID_INDEX;
    const SignalManagerStatus find_status = this->FindProcess(process_id, process_index);
    if (find_status != SignalManagerStatus::Succeeded) {
        return find_status;
    }
    return this->QueueForProcess(process_index, signal_number, selected_thread_index);
}

SignalManagerStatus SignalManager::SendToProcessGroup(const uint64_t process_group_id,
                                                      const uint64_t signal_number,
                                                      uint64_t *const selected_thread_storage,
                                                      const uint64_t selected_thread_capacity,
                                                      uint64_t &selected_thread_count,
                                                      uint64_t &target_process_count) noexcept {
    selected_thread_count = OS_KERNEL_SIGNAL_EMPTY_VALUE;
    target_process_count = OS_KERNEL_SIGNAL_EMPTY_VALUE;
    if (!this->initialized_) {
        return SignalManagerStatus::NotInitialized;
    }
    if (process_group_id == OS_KERNEL_SIGNAL_EMPTY_VALUE) {
        return SignalManagerStatus::InvalidProcessGroup;
    }
    if (!this->SignalNumberIsValid(signal_number)) {
        return SignalManagerStatus::InvalidSignal;
    }
    if (selected_thread_storage == nullptr || selected_thread_capacity < this->process_capacity_) {
        return SignalManagerStatus::InvalidStorage;
    }
    for (uint64_t process_index = OS_KERNEL_SIGNAL_FIRST_INDEX;
         process_index < this->process_capacity_; ++process_index) {
        if (!this->processes_[process_index].active ||
            this->processes_[process_index].process_group_id != process_group_id) {
            continue;
        }
        ++target_process_count;
        uint64_t selected_thread_index = OS_KERNEL_SIGNAL_INVALID_INDEX;
        const SignalManagerStatus queue_status =
            this->QueueForProcess(process_index, signal_number, selected_thread_index);
        if (queue_status != SignalManagerStatus::Succeeded) {
            return queue_status;
        }
        if (selected_thread_index != OS_KERNEL_SIGNAL_INVALID_INDEX) {
            if (selected_thread_count >= selected_thread_capacity) {
                return SignalManagerStatus::CapacityExhausted;
            }
            selected_thread_storage[selected_thread_count++] = selected_thread_index;
        }
    }
    if (target_process_count == OS_KERNEL_SIGNAL_EMPTY_VALUE) {
        return SignalManagerStatus::ProcessNotFound;
    }
    this->statistics_.process_group_send_count += OS_KERNEL_SIGNAL_COUNTER_INCREMENT;
    return SignalManagerStatus::Succeeded;
}

SignalManagerStatus SignalManager::BeginThreadDelivery(const uint64_t thread_index,
                                                       SignalDelivery &delivery) noexcept {
    delivery = SignalDelivery{};
    if (!this->initialized_) {
        return SignalManagerStatus::NotInitialized;
    }
    if (thread_index >= this->thread_capacity_) {
        return SignalManagerStatus::InvalidThreadIndex;
    }
    SignalThreadState &thread = this->threads_[thread_index];
    if (!thread.active) {
        return SignalManagerStatus::ThreadNotFound;
    }
    if (thread.frame_active) {
        return SignalManagerStatus::Succeeded;
    }
    const uint64_t eligible_set = thread.pending_set & ~thread.signal_mask;
    const uint64_t signal_number = LowestSignalNumber(eligible_set);
    if (signal_number == OS_KERNEL_SIGNAL_EMPTY_VALUE) {
        return SignalManagerStatus::Succeeded;
    }
    const uint64_t signal_bit = os::abi::SignalBit(signal_number);
    const SignalProcessState &process = this->processes_[thread.process_index];
    const os::abi::SignalAction action =
        process.actions[signal_number - os::abi::OS_ABI_SIGNAL_MINIMUM_NUMBER];
    thread.pending_set &= ~signal_bit;
    if (action.disposition == os::abi::SignalDisposition::Ignore ||
        (action.disposition == os::abi::SignalDisposition::Default &&
         this->DefaultDispositionIgnores(signal_number))) {
        this->statistics_.ignored_signal_count += OS_KERNEL_SIGNAL_COUNTER_INCREMENT;
        return this->BeginThreadDelivery(thread_index, delivery);
    }
    delivery.signal_number = signal_number;
    delivery.action = action;
    if (action.disposition == os::abi::SignalDisposition::Default) {
        if (signal_number == os::abi::OS_ABI_SIGNAL_STOP_NUMBER ||
            signal_number == os::abi::OS_ABI_SIGNAL_TERMINAL_STOP_NUMBER) {
            delivery.kind = SignalDeliveryKind::DefaultStop;
            this->statistics_.default_stop_count += OS_KERNEL_SIGNAL_COUNTER_INCREMENT;
        } else if (signal_number == os::abi::OS_ABI_SIGNAL_CONTINUE_NUMBER) {
            delivery.kind = SignalDeliveryKind::DefaultContinue;
            this->statistics_.default_continue_count +=
                OS_KERNEL_SIGNAL_COUNTER_INCREMENT;
        } else {
            delivery.kind = SignalDeliveryKind::DefaultTerminate;
            this->statistics_.default_termination_count +=
                OS_KERNEL_SIGNAL_COUNTER_INCREMENT;
        }
        return SignalManagerStatus::Succeeded;
    }
    delivery.kind = SignalDeliveryKind::UserHandler;
    delivery.previous_mask = thread.signal_mask;
    thread.signal_mask |=
        (signal_bit | action.additional_mask) & ~os::abi::OS_ABI_SIGNAL_UNMASKABLE_SET;
    thread.active_signal_number = signal_number;
    thread.active_restorer_address = action.restorer_address;
    thread.active_previous_mask = delivery.previous_mask;
    thread.active_frame_cookie = this->next_frame_cookie_;
    ++this->next_frame_cookie_;
    if (this->next_frame_cookie_ == OS_KERNEL_SIGNAL_EMPTY_VALUE) {
        this->next_frame_cookie_ = OS_KERNEL_SIGNAL_FIRST_FRAME_COOKIE;
    }
    thread.frame_active = true;
    this->statistics_.handler_delivery_count += OS_KERNEL_SIGNAL_COUNTER_INCREMENT;
    return SignalManagerStatus::Succeeded;
}

SignalManagerStatus SignalManager::CommitHandlerFrame(const uint64_t thread_index,
                                                      const uint64_t frame_address,
                                                      const uint64_t frame_cookie) noexcept {
    if (!this->initialized_) {
        return SignalManagerStatus::NotInitialized;
    }
    if (thread_index >= this->thread_capacity_) {
        return SignalManagerStatus::InvalidThreadIndex;
    }
    SignalThreadState &thread = this->threads_[thread_index];
    if (!thread.active || !thread.frame_active) {
        return SignalManagerStatus::SignalFrameNotActive;
    }
    if (frame_address == OS_KERNEL_SIGNAL_EMPTY_VALUE ||
        frame_cookie != thread.active_frame_cookie) {
        return SignalManagerStatus::SignalFrameMismatch;
    }
    thread.active_frame_address = frame_address;
    return SignalManagerStatus::Succeeded;
}

SignalManagerStatus SignalManager::CompleteHandlerFrame(const uint64_t thread_index,
                                                        const uint64_t frame_address,
                                                        const uint64_t frame_cookie,
                                                        const uint64_t signal_number,
                                                        const uint64_t restorer_address,
                                                        const uint64_t restored_mask) noexcept {
    const SignalManagerStatus validate_status = this->ValidateHandlerFrame(
        thread_index, frame_address, frame_cookie, signal_number, restorer_address, restored_mask);
    if (validate_status != SignalManagerStatus::Succeeded) {
        return validate_status;
    }
    SignalThreadState &thread = this->threads_[thread_index];
    thread.signal_mask =
        restored_mask & os::abi::OS_ABI_SIGNAL_VALID_SET & ~os::abi::OS_ABI_SIGNAL_UNMASKABLE_SET;
    thread.active_frame_address = OS_KERNEL_SIGNAL_EMPTY_VALUE;
    thread.active_frame_cookie = OS_KERNEL_SIGNAL_EMPTY_VALUE;
    thread.active_signal_number = OS_KERNEL_SIGNAL_EMPTY_VALUE;
    thread.active_restorer_address = OS_KERNEL_SIGNAL_EMPTY_VALUE;
    thread.active_previous_mask = OS_KERNEL_SIGNAL_EMPTY_VALUE;
    thread.frame_active = false;
    this->AssignAllEligiblePending(thread.process_index);
    return SignalManagerStatus::Succeeded;
}

SignalManagerStatus
SignalManager::ValidateHandlerFrame(const uint64_t thread_index, const uint64_t frame_address,
                                    const uint64_t frame_cookie, const uint64_t signal_number,
                                    const uint64_t restorer_address,
                                    const uint64_t restored_mask) const noexcept {
    if (!this->initialized_) {
        return SignalManagerStatus::NotInitialized;
    }
    if (thread_index >= this->thread_capacity_) {
        return SignalManagerStatus::InvalidThreadIndex;
    }
    const SignalThreadState &thread = this->threads_[thread_index];
    if (!thread.active || !thread.frame_active) {
        return SignalManagerStatus::SignalFrameNotActive;
    }
    return thread.active_frame_address == frame_address &&
                   thread.active_frame_cookie == frame_cookie &&
                   thread.active_signal_number == signal_number &&
                   thread.active_restorer_address == restorer_address &&
                   thread.active_previous_mask == restored_mask
               ? SignalManagerStatus::Succeeded
               : SignalManagerStatus::SignalFrameMismatch;
}

SignalManagerStatus SignalManager::GetProcessGroup(const uint64_t process_index,
                                                   uint64_t &process_group_id) const noexcept {
    process_group_id = OS_KERNEL_SIGNAL_EMPTY_VALUE;
    if (!this->initialized_) {
        return SignalManagerStatus::NotInitialized;
    }
    if (process_index >= this->process_capacity_) {
        return SignalManagerStatus::InvalidProcessIndex;
    }
    if (!this->processes_[process_index].active) {
        return SignalManagerStatus::ProcessNotFound;
    }
    process_group_id = this->processes_[process_index].process_group_id;
    return SignalManagerStatus::Succeeded;
}

SignalManagerStatus SignalManager::SetProcessGroup(const uint64_t process_index,
                                                   const uint64_t process_group_id) noexcept {
    if (!this->initialized_) {
        return SignalManagerStatus::NotInitialized;
    }
    if (process_index >= this->process_capacity_) {
        return SignalManagerStatus::InvalidProcessIndex;
    }
    if (!this->processes_[process_index].active) {
        return SignalManagerStatus::ProcessNotFound;
    }
    if (process_group_id == OS_KERNEL_SIGNAL_EMPTY_VALUE) {
        return SignalManagerStatus::InvalidProcessGroup;
    }
    bool group_exists = false;
    for (uint64_t candidate_index = OS_KERNEL_SIGNAL_FIRST_INDEX;
         candidate_index < this->process_capacity_; ++candidate_index) {
        if (this->processes_[candidate_index].active &&
            (this->processes_[candidate_index].process_group_id == process_group_id ||
             this->processes_[candidate_index].process_id == process_group_id)) {
            group_exists = true;
            break;
        }
    }
    if (!group_exists) {
        return SignalManagerStatus::InvalidProcessGroup;
    }
    this->processes_[process_index].process_group_id = process_group_id;
    return SignalManagerStatus::Succeeded;
}

SignalManagerStatus SignalManager::FindProcess(const uint64_t process_id,
                                               uint64_t &process_index) const noexcept {
    process_index = OS_KERNEL_SIGNAL_INVALID_INDEX;
    if (!this->initialized_) {
        return SignalManagerStatus::NotInitialized;
    }
    if (process_id == OS_KERNEL_SIGNAL_EMPTY_VALUE) {
        return SignalManagerStatus::InvalidProcessId;
    }
    for (uint64_t candidate_index = OS_KERNEL_SIGNAL_FIRST_INDEX;
         candidate_index < this->process_capacity_; ++candidate_index) {
        if (this->processes_[candidate_index].active &&
            this->processes_[candidate_index].process_id == process_id) {
            process_index = candidate_index;
            return SignalManagerStatus::Succeeded;
        }
    }
    return SignalManagerStatus::ProcessNotFound;
}

SignalManagerStatus SignalManager::ReadProcess(const uint64_t process_index,
                                               SignalProcessState &state) const noexcept {
    state = SignalProcessState{};
    if (!this->initialized_) {
        return SignalManagerStatus::NotInitialized;
    }
    if (process_index >= this->process_capacity_) {
        return SignalManagerStatus::InvalidProcessIndex;
    }
    if (!this->processes_[process_index].active) {
        return SignalManagerStatus::ProcessNotFound;
    }
    state = this->processes_[process_index];
    return SignalManagerStatus::Succeeded;
}

SignalManagerStatus SignalManager::ReadThread(const uint64_t thread_index,
                                              SignalThreadState &state) const noexcept {
    state = SignalThreadState{};
    if (!this->initialized_) {
        return SignalManagerStatus::NotInitialized;
    }
    if (thread_index >= this->thread_capacity_) {
        return SignalManagerStatus::InvalidThreadIndex;
    }
    if (!this->threads_[thread_index].active) {
        return SignalManagerStatus::ThreadNotFound;
    }
    state = this->threads_[thread_index];
    return SignalManagerStatus::Succeeded;
}

void SignalManager::RecordInterruptedWait(const bool restarted) noexcept {
    if (restarted) {
        this->statistics_.restarted_wait_count += OS_KERNEL_SIGNAL_COUNTER_INCREMENT;
    } else {
        this->statistics_.interrupted_wait_count += OS_KERNEL_SIGNAL_COUNTER_INCREMENT;
    }
}

void SignalManager::RecordRejectedFrame() noexcept {
    this->statistics_.rejected_frame_count += OS_KERNEL_SIGNAL_COUNTER_INCREMENT;
}

SignalManagerStatus SignalManager::Validate() const noexcept {
    if (!this->initialized_) {
        return SignalManagerStatus::NotInitialized;
    }
    uint64_t active_process_count = OS_KERNEL_SIGNAL_EMPTY_VALUE;
    uint64_t active_thread_count = OS_KERNEL_SIGNAL_EMPTY_VALUE;
    for (uint64_t process_index = OS_KERNEL_SIGNAL_FIRST_INDEX;
         process_index < this->process_capacity_; ++process_index) {
        const SignalProcessState &process = this->processes_[process_index];
        if (!process.active) {
            continue;
        }
        ++active_process_count;
        if (process.process_id == OS_KERNEL_SIGNAL_EMPTY_VALUE ||
            process.process_group_id == OS_KERNEL_SIGNAL_EMPTY_VALUE ||
            process.next_thread_index >= this->thread_capacity_ ||
            (process.pending_set & ~os::abi::OS_ABI_SIGNAL_VALID_SET) !=
                OS_KERNEL_SIGNAL_EMPTY_VALUE) {
            return SignalManagerStatus::CorruptedState;
        }
        uint64_t owned_pending_set = process.pending_set;
        for (uint64_t action_index = OS_KERNEL_SIGNAL_FIRST_INDEX;
             action_index < OS_KERNEL_SIGNAL_ACTION_CAPACITY; ++action_index) {
            const uint64_t signal_number = action_index + os::abi::OS_ABI_SIGNAL_MINIMUM_NUMBER;
            if (!this->ActionIsValid(signal_number, process.actions[action_index])) {
                return SignalManagerStatus::CorruptedState;
            }
        }
        for (uint64_t thread_index = OS_KERNEL_SIGNAL_FIRST_INDEX;
             thread_index < this->thread_capacity_; ++thread_index) {
            const SignalThreadState &thread = this->threads_[thread_index];
            if (!thread.active || thread.process_index != process_index) {
                continue;
            }
            if ((owned_pending_set & thread.pending_set) != OS_KERNEL_SIGNAL_EMPTY_VALUE) {
                return SignalManagerStatus::CorruptedState;
            }
            owned_pending_set |= thread.pending_set;
        }
    }
    for (uint64_t thread_index = OS_KERNEL_SIGNAL_FIRST_INDEX;
         thread_index < this->thread_capacity_; ++thread_index) {
        const SignalThreadState &thread = this->threads_[thread_index];
        if (!thread.active) {
            continue;
        }
        ++active_thread_count;
        if (thread.thread_id == OS_KERNEL_SIGNAL_EMPTY_VALUE ||
            thread.process_index >= this->process_capacity_ ||
            !this->processes_[thread.process_index].active ||
            (thread.signal_mask & os::abi::OS_ABI_SIGNAL_UNMASKABLE_SET) !=
                OS_KERNEL_SIGNAL_EMPTY_VALUE ||
            (thread.signal_mask & ~os::abi::OS_ABI_SIGNAL_VALID_SET) !=
                OS_KERNEL_SIGNAL_EMPTY_VALUE ||
            (thread.pending_set & ~os::abi::OS_ABI_SIGNAL_VALID_SET) !=
                OS_KERNEL_SIGNAL_EMPTY_VALUE ||
            (thread.frame_active &&
             (thread.active_frame_cookie == OS_KERNEL_SIGNAL_EMPTY_VALUE ||
              !this->SignalNumberIsValid(thread.active_signal_number) ||
              thread.active_restorer_address == OS_KERNEL_SIGNAL_EMPTY_VALUE ||
              (thread.active_previous_mask & ~os::abi::OS_ABI_SIGNAL_VALID_SET) !=
                  OS_KERNEL_SIGNAL_EMPTY_VALUE)) ||
            (!thread.frame_active &&
             (thread.active_frame_address != OS_KERNEL_SIGNAL_EMPTY_VALUE ||
              thread.active_frame_cookie != OS_KERNEL_SIGNAL_EMPTY_VALUE ||
              thread.active_signal_number != OS_KERNEL_SIGNAL_EMPTY_VALUE ||
              thread.active_restorer_address != OS_KERNEL_SIGNAL_EMPTY_VALUE ||
              thread.active_previous_mask != OS_KERNEL_SIGNAL_EMPTY_VALUE))) {
            return SignalManagerStatus::CorruptedState;
        }
    }
    return active_process_count == this->statistics_.active_process_count &&
                   active_thread_count == this->statistics_.active_thread_count
               ? SignalManagerStatus::Succeeded
               : SignalManagerStatus::CorruptedState;
}

SignalManagerStatistics SignalManager::Statistics() const noexcept { return this->statistics_; }

bool SignalManager::SignalNumberIsValid(const uint64_t signal_number) const noexcept {
    return signal_number >= os::abi::OS_ABI_SIGNAL_MINIMUM_NUMBER &&
           signal_number <= os::abi::OS_ABI_SIGNAL_MAXIMUM_NUMBER;
}

bool SignalManager::ActionIsValid(const uint64_t signal_number,
                                  const os::abi::SignalAction &action) const noexcept {
    if ((action.flags & ~os::abi::OS_ABI_SIGNAL_ACTION_VALID_FLAG_MASK) !=
            OS_KERNEL_SIGNAL_EMPTY_VALUE ||
        (action.additional_mask & ~os::abi::OS_ABI_SIGNAL_VALID_SET) !=
            OS_KERNEL_SIGNAL_EMPTY_VALUE) {
        return false;
    }
    if ((signal_number == os::abi::OS_ABI_SIGNAL_KILL_NUMBER ||
         signal_number == os::abi::OS_ABI_SIGNAL_STOP_NUMBER) &&
        action.disposition != os::abi::SignalDisposition::Default) {
        return false;
    }
    if (action.disposition == os::abi::SignalDisposition::Handler) {
        return action.handler_address != OS_KERNEL_SIGNAL_EMPTY_VALUE &&
               action.restorer_address != OS_KERNEL_SIGNAL_EMPTY_VALUE;
    }
    if (action.disposition == os::abi::SignalDisposition::Default ||
        action.disposition == os::abi::SignalDisposition::Ignore) {
        return action.handler_address == OS_KERNEL_SIGNAL_EMPTY_VALUE &&
               action.restorer_address == OS_KERNEL_SIGNAL_EMPTY_VALUE &&
               action.additional_mask == OS_KERNEL_SIGNAL_EMPTY_VALUE &&
               action.flags == OS_KERNEL_SIGNAL_EMPTY_VALUE;
    }
    return false;
}

bool SignalManager::SignalAlreadyPending(const uint64_t process_index,
                                         const uint64_t signal_number) const noexcept {
    const uint64_t signal_bit = os::abi::SignalBit(signal_number);
    if ((this->processes_[process_index].pending_set & signal_bit) !=
        OS_KERNEL_SIGNAL_EMPTY_VALUE) {
        return true;
    }
    for (uint64_t thread_index = OS_KERNEL_SIGNAL_FIRST_INDEX;
         thread_index < this->thread_capacity_; ++thread_index) {
        const SignalThreadState &thread = this->threads_[thread_index];
        if (thread.active && thread.process_index == process_index &&
            (thread.pending_set & signal_bit) != OS_KERNEL_SIGNAL_EMPTY_VALUE) {
            return true;
        }
    }
    return false;
}

bool SignalManager::DefaultDispositionIgnores(const uint64_t signal_number) const noexcept {
    return signal_number == os::abi::OS_ABI_SIGNAL_CHILD_NUMBER;
}

SignalManagerStatus SignalManager::QueueForProcess(const uint64_t process_index,
                                                   const uint64_t signal_number,
                                                   uint64_t &selected_thread_index) noexcept {
    selected_thread_index = OS_KERNEL_SIGNAL_INVALID_INDEX;
    if (!this->SignalNumberIsValid(signal_number)) {
        return SignalManagerStatus::InvalidSignal;
    }
    SignalProcessState &process = this->processes_[process_index];
    const os::abi::SignalAction &action =
        process.actions[signal_number - os::abi::OS_ABI_SIGNAL_MINIMUM_NUMBER];
    if (action.disposition == os::abi::SignalDisposition::Ignore ||
        (action.disposition == os::abi::SignalDisposition::Default &&
         this->DefaultDispositionIgnores(signal_number))) {
        this->statistics_.ignored_signal_count += OS_KERNEL_SIGNAL_COUNTER_INCREMENT;
        return SignalManagerStatus::Succeeded;
    }
    if (this->SignalAlreadyPending(process_index, signal_number)) {
        this->statistics_.coalesced_signal_count += OS_KERNEL_SIGNAL_COUNTER_INCREMENT;
        return SignalManagerStatus::Succeeded;
    }
    process.pending_set |= os::abi::SignalBit(signal_number);
    this->statistics_.queued_signal_count += OS_KERNEL_SIGNAL_COUNTER_INCREMENT;
    return this->AssignPendingSignal(process_index, signal_number, selected_thread_index);
}

SignalManagerStatus SignalManager::AssignPendingSignal(const uint64_t process_index,
                                                       const uint64_t signal_number,
                                                       uint64_t &selected_thread_index) noexcept {
    selected_thread_index = OS_KERNEL_SIGNAL_INVALID_INDEX;
    SignalProcessState &process = this->processes_[process_index];
    const uint64_t signal_bit = os::abi::SignalBit(signal_number);
    if ((process.pending_set & signal_bit) == OS_KERNEL_SIGNAL_EMPTY_VALUE) {
        return SignalManagerStatus::Succeeded;
    }
    for (uint64_t offset = OS_KERNEL_SIGNAL_FIRST_INDEX; offset < this->thread_capacity_;
         ++offset) {
        const uint64_t thread_index = (process.next_thread_index + offset) % this->thread_capacity_;
        SignalThreadState &thread = this->threads_[thread_index];
        if (!thread.active || thread.process_index != process_index ||
            (thread.signal_mask & signal_bit) != OS_KERNEL_SIGNAL_EMPTY_VALUE) {
            continue;
        }
        thread.pending_set |= signal_bit;
        process.pending_set &= ~signal_bit;
        process.next_thread_index =
            (thread_index + OS_KERNEL_SIGNAL_COUNTER_INCREMENT) % this->thread_capacity_;
        selected_thread_index = thread_index;
        return SignalManagerStatus::Succeeded;
    }
    return SignalManagerStatus::Succeeded;
}

void SignalManager::AssignAllEligiblePending(const uint64_t process_index) noexcept {
    if (process_index >= this->process_capacity_ || !this->processes_[process_index].active) {
        return;
    }
    for (uint64_t signal_number = os::abi::OS_ABI_SIGNAL_MINIMUM_NUMBER;
         signal_number <= os::abi::OS_ABI_SIGNAL_MAXIMUM_NUMBER; ++signal_number) {
        uint64_t selected_thread_index = OS_KERNEL_SIGNAL_INVALID_INDEX;
        static_cast<void>(
            this->AssignPendingSignal(process_index, signal_number, selected_thread_index));
    }
}

}

#include "os/kernel/arch/cpu_local.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_CPU_LOCAL_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_CPU_LOCAL_BOOLEAN_TRUE = 1ULL;
constexpr uint64_t OS_KERNEL_CPU_LOCAL_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_KERNEL_CPU_LOCAL_STACK_ALIGNMENT_BYTES = 16ULL;
constexpr uint64_t OS_KERNEL_CPU_LOCAL_MAXIMUM_SYSTEM_CALL_DEPTH = 1ULL;
constexpr uint64_t OS_KERNEL_CPU_LOCAL_INVALID_ENTRY_METHOD =
    static_cast<uint64_t>(UserContextEntryMethod::Invalid);

alignas(OS_KERNEL_CPU_LOCAL_CACHE_LINE_SIZE_BYTES) constinit CpuLocal kernel_cpu_local{};

}

CpuLocalStatus CpuLocal::Initialize(const uint64_t bootstrap_stack_pointer) noexcept {
    if (this->initialized_ != OS_KERNEL_CPU_LOCAL_EMPTY_VALUE) {
        return CpuLocalStatus::AlreadyInitialized;
    }
    if (!this->StackPointerIsValid(bootstrap_stack_pointer)) {
        return CpuLocalStatus::InvalidStackPointer;
    }
    this->self_address_ = reinterpret_cast<uint64_t>(this);
    this->current_thread_index_ = OS_KERNEL_CPU_LOCAL_INVALID_THREAD_INDEX;
    this->kernel_entry_stack_pointer_ = bootstrap_stack_pointer;
    this->system_call_user_stack_pointer_ = OS_KERNEL_CPU_LOCAL_EMPTY_VALUE;
    this->interrupt_depth_ = OS_KERNEL_CPU_LOCAL_EMPTY_VALUE;
    this->maximum_interrupt_depth_ = OS_KERNEL_CPU_LOCAL_EMPTY_VALUE;
    this->preemption_disable_depth_ = OS_KERNEL_CPU_LOCAL_EMPTY_VALUE;
    this->maximum_preemption_disable_depth_ = OS_KERNEL_CPU_LOCAL_EMPTY_VALUE;
    this->need_reschedule_ = OS_KERNEL_CPU_LOCAL_EMPTY_VALUE;
    this->system_call_depth_ = OS_KERNEL_CPU_LOCAL_EMPTY_VALUE;
    this->legacy_system_call_count_ = OS_KERNEL_CPU_LOCAL_EMPTY_VALUE;
    this->native_system_call_count_ = OS_KERNEL_CPU_LOCAL_EMPTY_VALUE;
    this->interrupt_during_system_call_count_ = OS_KERNEL_CPU_LOCAL_EMPTY_VALUE;
    this->return_reschedule_count_ = OS_KERNEL_CPU_LOCAL_EMPTY_VALUE;
    this->system_return_count_ = OS_KERNEL_CPU_LOCAL_EMPTY_VALUE;
    this->interrupt_return_count_ = OS_KERNEL_CPU_LOCAL_EMPTY_VALUE;
    this->native_interrupt_return_count_ = OS_KERNEL_CPU_LOCAL_EMPTY_VALUE;
    this->rejected_return_count_ = OS_KERNEL_CPU_LOCAL_EMPTY_VALUE;
    this->trusted_stack_validation_count_ = OS_KERNEL_CPU_LOCAL_EMPTY_VALUE;
    this->initialized_ = OS_KERNEL_CPU_LOCAL_BOOLEAN_TRUE;
    this->system_call_entry_method_ = OS_KERNEL_CPU_LOCAL_INVALID_ENTRY_METHOD;
    return CpuLocalStatus::Succeeded;
}

CpuLocalStatus CpuLocal::SetCurrentThread(const uint64_t thread_index,
                                          const uint64_t kernel_entry_stack_pointer) noexcept {
    if (this->initialized_ == OS_KERNEL_CPU_LOCAL_EMPTY_VALUE) {
        return CpuLocalStatus::NotInitialized;
    }
    if (thread_index == OS_KERNEL_CPU_LOCAL_INVALID_THREAD_INDEX) {
        return CpuLocalStatus::InvalidThreadIndex;
    }
    if (!this->StackPointerIsValid(kernel_entry_stack_pointer)) {
        return CpuLocalStatus::InvalidStackPointer;
    }
    this->current_thread_index_ = thread_index;
    this->kernel_entry_stack_pointer_ = kernel_entry_stack_pointer;
    this->system_call_user_stack_pointer_ = OS_KERNEL_CPU_LOCAL_EMPTY_VALUE;
    this->need_reschedule_ = OS_KERNEL_CPU_LOCAL_EMPTY_VALUE;
    return CpuLocalStatus::Succeeded;
}

CpuLocalStatus CpuLocal::ClearCurrentThread(const uint64_t bootstrap_stack_pointer) noexcept {
    if (this->initialized_ == OS_KERNEL_CPU_LOCAL_EMPTY_VALUE) {
        return CpuLocalStatus::NotInitialized;
    }
    if (!this->StackPointerIsValid(bootstrap_stack_pointer)) {
        return CpuLocalStatus::InvalidStackPointer;
    }
    this->current_thread_index_ = OS_KERNEL_CPU_LOCAL_INVALID_THREAD_INDEX;
    this->kernel_entry_stack_pointer_ = bootstrap_stack_pointer;
    this->system_call_user_stack_pointer_ = OS_KERNEL_CPU_LOCAL_EMPTY_VALUE;
    this->need_reschedule_ = OS_KERNEL_CPU_LOCAL_EMPTY_VALUE;
    // 最后一个用户线程会直接恢复 ExecuteProcesses 的内核调用链，
    // 不再经过系统调用汇编尾部，因此清理线程所有权时必须收束入口状态。
    this->system_call_depth_ = OS_KERNEL_CPU_LOCAL_EMPTY_VALUE;
    this->system_call_entry_method_ = OS_KERNEL_CPU_LOCAL_INVALID_ENTRY_METHOD;
    return CpuLocalStatus::Succeeded;
}

CpuLocalStatus CpuLocal::EnterInterrupt() noexcept {
    if (this->initialized_ == OS_KERNEL_CPU_LOCAL_EMPTY_VALUE) {
        return CpuLocalStatus::NotInitialized;
    }
    const CpuLocalStatus depth_status = this->IncrementCounter(this->interrupt_depth_);
    if (depth_status != CpuLocalStatus::Succeeded) {
        return depth_status;
    }
    if (this->interrupt_depth_ > this->maximum_interrupt_depth_) {
        this->maximum_interrupt_depth_ = this->interrupt_depth_;
    }
    if (this->system_call_depth_ != OS_KERNEL_CPU_LOCAL_EMPTY_VALUE) {
        return this->IncrementCounter(this->interrupt_during_system_call_count_);
    }
    return CpuLocalStatus::Succeeded;
}

CpuLocalStatus CpuLocal::LeaveInterrupt() noexcept {
    if (this->initialized_ == OS_KERNEL_CPU_LOCAL_EMPTY_VALUE) {
        return CpuLocalStatus::NotInitialized;
    }
    if (this->interrupt_depth_ == OS_KERNEL_CPU_LOCAL_EMPTY_VALUE) {
        return CpuLocalStatus::InvalidState;
    }
    this->interrupt_depth_ -= OS_KERNEL_CPU_LOCAL_COUNTER_INCREMENT;
    return CpuLocalStatus::Succeeded;
}

CpuLocalStatus CpuLocal::DisablePreemption() noexcept {
    if (this->initialized_ == OS_KERNEL_CPU_LOCAL_EMPTY_VALUE) {
        return CpuLocalStatus::NotInitialized;
    }
    const CpuLocalStatus depth_status = this->IncrementCounter(this->preemption_disable_depth_);
    if (depth_status != CpuLocalStatus::Succeeded) {
        return depth_status;
    }
    if (this->preemption_disable_depth_ > this->maximum_preemption_disable_depth_) {
        this->maximum_preemption_disable_depth_ = this->preemption_disable_depth_;
    }
    return CpuLocalStatus::Succeeded;
}

CpuLocalStatus CpuLocal::EnablePreemption() noexcept {
    if (this->initialized_ == OS_KERNEL_CPU_LOCAL_EMPTY_VALUE) {
        return CpuLocalStatus::NotInitialized;
    }
    if (this->preemption_disable_depth_ == OS_KERNEL_CPU_LOCAL_EMPTY_VALUE) {
        return CpuLocalStatus::InvalidState;
    }
    this->preemption_disable_depth_ -= OS_KERNEL_CPU_LOCAL_COUNTER_INCREMENT;
    return CpuLocalStatus::Succeeded;
}

CpuLocalStatus CpuLocal::BeginSystemCall(const UserContextEntryMethod entry_method) noexcept {
    if (this->initialized_ == OS_KERNEL_CPU_LOCAL_EMPTY_VALUE) {
        return CpuLocalStatus::NotInitialized;
    }
    if (this->system_call_depth_ != OS_KERNEL_CPU_LOCAL_EMPTY_VALUE) {
        return CpuLocalStatus::InvalidState;
    }
    uint64_t *entry_counter = nullptr;
    if (entry_method == UserContextEntryMethod::LegacyInterrupt) {
        entry_counter = &this->legacy_system_call_count_;
    } else if (entry_method == UserContextEntryMethod::NativeSystemCall) {
        entry_counter = &this->native_system_call_count_;
    } else {
        return CpuLocalStatus::InvalidEntryMethod;
    }
    const CpuLocalStatus counter_status = this->IncrementCounter(*entry_counter);
    if (counter_status != CpuLocalStatus::Succeeded) {
        return counter_status;
    }
    this->system_call_depth_ = OS_KERNEL_CPU_LOCAL_MAXIMUM_SYSTEM_CALL_DEPTH;
    this->system_call_entry_method_ = static_cast<uint64_t>(entry_method);
    return CpuLocalStatus::Succeeded;
}

CpuLocalStatus CpuLocal::SuspendSystemCall(UserContextEntryMethod &entry_method) noexcept {
    entry_method = UserContextEntryMethod::Invalid;
    if (this->initialized_ == OS_KERNEL_CPU_LOCAL_EMPTY_VALUE) {
        return CpuLocalStatus::NotInitialized;
    }
    if (this->system_call_depth_ != OS_KERNEL_CPU_LOCAL_MAXIMUM_SYSTEM_CALL_DEPTH ||
        (this->system_call_entry_method_ !=
             static_cast<uint64_t>(UserContextEntryMethod::LegacyInterrupt) &&
         this->system_call_entry_method_ !=
             static_cast<uint64_t>(UserContextEntryMethod::NativeSystemCall))) {
        return CpuLocalStatus::InvalidState;
    }
    entry_method = static_cast<UserContextEntryMethod>(this->system_call_entry_method_);
    this->system_call_depth_ = OS_KERNEL_CPU_LOCAL_EMPTY_VALUE;
    this->system_call_user_stack_pointer_ = OS_KERNEL_CPU_LOCAL_EMPTY_VALUE;
    this->system_call_entry_method_ = OS_KERNEL_CPU_LOCAL_INVALID_ENTRY_METHOD;
    return CpuLocalStatus::Succeeded;
}

CpuLocalStatus CpuLocal::ResumeSystemCall(const UserContextEntryMethod entry_method) noexcept {
    if (this->initialized_ == OS_KERNEL_CPU_LOCAL_EMPTY_VALUE) {
        return CpuLocalStatus::NotInitialized;
    }
    if (this->system_call_depth_ != OS_KERNEL_CPU_LOCAL_EMPTY_VALUE ||
        (entry_method != UserContextEntryMethod::LegacyInterrupt &&
         entry_method != UserContextEntryMethod::NativeSystemCall)) {
        return CpuLocalStatus::InvalidState;
    }
    this->system_call_depth_ = OS_KERNEL_CPU_LOCAL_MAXIMUM_SYSTEM_CALL_DEPTH;
    this->system_call_entry_method_ = static_cast<uint64_t>(entry_method);
    return CpuLocalStatus::Succeeded;
}

CpuLocalStatus CpuLocal::EndSystemCall() noexcept {
    if (this->initialized_ == OS_KERNEL_CPU_LOCAL_EMPTY_VALUE) {
        return CpuLocalStatus::NotInitialized;
    }
    if (this->system_call_depth_ != OS_KERNEL_CPU_LOCAL_MAXIMUM_SYSTEM_CALL_DEPTH) {
        return CpuLocalStatus::InvalidState;
    }
    this->system_call_depth_ = OS_KERNEL_CPU_LOCAL_EMPTY_VALUE;
    this->system_call_user_stack_pointer_ = OS_KERNEL_CPU_LOCAL_EMPTY_VALUE;
    this->system_call_entry_method_ = OS_KERNEL_CPU_LOCAL_INVALID_ENTRY_METHOD;
    return CpuLocalStatus::Succeeded;
}

void CpuLocal::RequestReschedule() noexcept {
    this->need_reschedule_ = OS_KERNEL_CPU_LOCAL_BOOLEAN_TRUE;
}

bool CpuLocal::ConsumeRescheduleRequest() noexcept {
    const bool requested = this->need_reschedule_ != OS_KERNEL_CPU_LOCAL_EMPTY_VALUE;
    this->need_reschedule_ = OS_KERNEL_CPU_LOCAL_EMPTY_VALUE;
    return requested;
}

CpuLocalStatus CpuLocal::RecordReturnReschedule() noexcept {
    if (this->initialized_ == OS_KERNEL_CPU_LOCAL_EMPTY_VALUE) {
        return CpuLocalStatus::NotInitialized;
    }
    return this->IncrementCounter(this->return_reschedule_count_);
}

CpuLocalStatus CpuLocal::RecordUserReturn(const UserContextEntryMethod entry_method,
                                          const UserReturnMethod return_method) noexcept {
    if (this->initialized_ == OS_KERNEL_CPU_LOCAL_EMPTY_VALUE) {
        return CpuLocalStatus::NotInitialized;
    }
    if (return_method == UserReturnMethod::Rejected) {
        return this->IncrementCounter(this->rejected_return_count_);
    }
    if (entry_method == UserContextEntryMethod::Invalid) {
        return CpuLocalStatus::InvalidEntryMethod;
    }
    if (return_method == UserReturnMethod::SystemReturn) {
        return this->IncrementCounter(this->system_return_count_);
    }
    if (return_method == UserReturnMethod::InterruptReturn) {
        const CpuLocalStatus interrupt_status =
            this->IncrementCounter(this->interrupt_return_count_);
        if (interrupt_status != CpuLocalStatus::Succeeded ||
            entry_method != UserContextEntryMethod::NativeSystemCall) {
            return interrupt_status;
        }
        return this->IncrementCounter(this->native_interrupt_return_count_);
    }
    return CpuLocalStatus::InvalidReturnMethod;
}

CpuLocalStatus CpuLocal::RecordTrustedStackValidation() noexcept {
    if (this->initialized_ == OS_KERNEL_CPU_LOCAL_EMPTY_VALUE) {
        return CpuLocalStatus::NotInitialized;
    }
    return this->IncrementCounter(this->trusted_stack_validation_count_);
}

CpuLocalStatus CpuLocal::Validate() const noexcept {
    if (this->initialized_ == OS_KERNEL_CPU_LOCAL_EMPTY_VALUE) {
        return CpuLocalStatus::NotInitialized;
    }
    if (this->self_address_ != reinterpret_cast<uint64_t>(this) ||
        !this->StackPointerIsValid(this->kernel_entry_stack_pointer_) ||
        this->need_reschedule_ > OS_KERNEL_CPU_LOCAL_BOOLEAN_TRUE ||
        this->system_call_depth_ > OS_KERNEL_CPU_LOCAL_MAXIMUM_SYSTEM_CALL_DEPTH ||
        (this->system_call_depth_ == OS_KERNEL_CPU_LOCAL_EMPTY_VALUE &&
         this->system_call_entry_method_ != OS_KERNEL_CPU_LOCAL_INVALID_ENTRY_METHOD) ||
        (this->system_call_depth_ == OS_KERNEL_CPU_LOCAL_MAXIMUM_SYSTEM_CALL_DEPTH &&
         this->system_call_entry_method_ !=
             static_cast<uint64_t>(UserContextEntryMethod::LegacyInterrupt) &&
         this->system_call_entry_method_ !=
             static_cast<uint64_t>(UserContextEntryMethod::NativeSystemCall)) ||
        this->interrupt_depth_ > this->maximum_interrupt_depth_ ||
        this->preemption_disable_depth_ > this->maximum_preemption_disable_depth_) {
        return CpuLocalStatus::InvalidState;
    }
    return CpuLocalStatus::Succeeded;
}

CpuLocalStatistics CpuLocal::Statistics() const noexcept {
    return CpuLocalStatistics{
        .current_thread_index = this->current_thread_index_,
        .kernel_entry_stack_pointer = this->kernel_entry_stack_pointer_,
        .interrupt_depth = this->interrupt_depth_,
        .maximum_interrupt_depth = this->maximum_interrupt_depth_,
        .preemption_disable_depth = this->preemption_disable_depth_,
        .maximum_preemption_disable_depth = this->maximum_preemption_disable_depth_,
        .legacy_system_call_count = this->legacy_system_call_count_,
        .native_system_call_count = this->native_system_call_count_,
        .interrupt_during_system_call_count = this->interrupt_during_system_call_count_,
        .return_reschedule_count = this->return_reschedule_count_,
        .system_return_count = this->system_return_count_,
        .interrupt_return_count = this->interrupt_return_count_,
        .native_interrupt_return_count = this->native_interrupt_return_count_,
        .rejected_return_count = this->rejected_return_count_,
        .trusted_stack_validation_count = this->trusted_stack_validation_count_,
        .need_reschedule = this->need_reschedule_ != OS_KERNEL_CPU_LOCAL_EMPTY_VALUE,
        .system_call_active = this->system_call_depth_ != OS_KERNEL_CPU_LOCAL_EMPTY_VALUE,
        .initialized = this->initialized_ != OS_KERNEL_CPU_LOCAL_EMPTY_VALUE,
    };
}

uint64_t CpuLocal::Address() const noexcept { return this->self_address_; }

uint64_t CpuLocal::CurrentThreadIndex() const noexcept { return this->current_thread_index_; }

uint64_t CpuLocal::KernelEntryStackPointer() const noexcept {
    return this->kernel_entry_stack_pointer_;
}

uint64_t CpuLocal::SystemCallUserStackPointer() const noexcept {
    return this->system_call_user_stack_pointer_;
}

bool CpuLocal::NativeSystemCallActive() const noexcept {
    return this->initialized_ != OS_KERNEL_CPU_LOCAL_EMPTY_VALUE &&
           this->system_call_depth_ == OS_KERNEL_CPU_LOCAL_MAXIMUM_SYSTEM_CALL_DEPTH &&
           this->system_call_entry_method_ ==
               static_cast<uint64_t>(UserContextEntryMethod::NativeSystemCall);
}

CpuLocalStatus CpuLocal::IncrementCounter(uint64_t &counter) noexcept {
    if (counter == UINT64_MAX) {
        return CpuLocalStatus::CounterOverflow;
    }
    counter += OS_KERNEL_CPU_LOCAL_COUNTER_INCREMENT;
    return CpuLocalStatus::Succeeded;
}

bool CpuLocal::StackPointerIsValid(const uint64_t stack_pointer) const noexcept {
    return stack_pointer != OS_KERNEL_CPU_LOCAL_EMPTY_VALUE &&
           stack_pointer % OS_KERNEL_CPU_LOCAL_STACK_ALIGNMENT_BYTES ==
               OS_KERNEL_CPU_LOCAL_EMPTY_VALUE;
}

CpuPreemptionGuard::CpuPreemptionGuard() noexcept {
    this->acquired_ = GetCpuLocal().DisablePreemption() == CpuLocalStatus::Succeeded;
}

CpuPreemptionGuard::~CpuPreemptionGuard() noexcept {
    if (this->acquired_ && GetCpuLocal().EnablePreemption() != CpuLocalStatus::Succeeded) {
        __builtin_trap();
    }
}

bool CpuPreemptionGuard::Succeeded() const noexcept { return this->acquired_; }

CpuLocal &GetCpuLocal() noexcept { return kernel_cpu_local; }

}

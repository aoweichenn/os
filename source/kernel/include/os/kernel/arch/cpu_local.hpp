#pragma once

#include "os/kernel/arch/user_context.hpp"

#include <stddef.h>
#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_CPU_LOCAL_INVALID_THREAD_INDEX = UINT64_MAX;
inline constexpr uint64_t OS_KERNEL_CPU_LOCAL_CACHE_LINE_SIZE_BYTES = 64ULL;
inline constexpr uint64_t OS_KERNEL_CPU_LOCAL_SELF_ADDRESS_OFFSET = 0ULL;
inline constexpr uint64_t OS_KERNEL_CPU_LOCAL_CURRENT_THREAD_INDEX_OFFSET = 8ULL;
inline constexpr uint64_t OS_KERNEL_CPU_LOCAL_ENTRY_STACK_POINTER_OFFSET = 16ULL;
inline constexpr uint64_t OS_KERNEL_CPU_LOCAL_SYSTEM_CALL_USER_STACK_POINTER_OFFSET = 24ULL;

enum class CpuLocalStatus : uint64_t {
    Succeeded,
    NotInitialized,
    AlreadyInitialized,
    InvalidStackPointer,
    InvalidThreadIndex,
    InvalidEntryMethod,
    InvalidReturnMethod,
    InvalidState,
    CounterOverflow,
};

struct CpuLocalStatistics final {
    uint64_t current_thread_index;
    uint64_t kernel_entry_stack_pointer;
    uint64_t interrupt_depth;
    uint64_t maximum_interrupt_depth;
    uint64_t preemption_disable_depth;
    uint64_t maximum_preemption_disable_depth;
    uint64_t legacy_system_call_count;
    uint64_t native_system_call_count;
    uint64_t interrupt_during_system_call_count;
    uint64_t return_reschedule_count;
    uint64_t system_return_count;
    uint64_t interrupt_return_count;
    uint64_t native_interrupt_return_count;
    uint64_t rejected_return_count;
    uint64_t trusted_stack_validation_count;
    bool need_reschedule;
    bool system_call_active;
    bool initialized;
};

[[nodiscard]] consteval bool ValidateCpuLocalLayout() noexcept;

class alignas(OS_KERNEL_CPU_LOCAL_CACHE_LINE_SIZE_BYTES) CpuLocal final {
  public:
    [[nodiscard]] CpuLocalStatus Initialize(uint64_t bootstrap_stack_pointer) noexcept;
    [[nodiscard]] CpuLocalStatus SetCurrentThread(uint64_t thread_index,
                                                  uint64_t kernel_entry_stack_pointer) noexcept;
    [[nodiscard]] CpuLocalStatus ClearCurrentThread(uint64_t bootstrap_stack_pointer) noexcept;
    [[nodiscard]] CpuLocalStatus EnterInterrupt() noexcept;
    [[nodiscard]] CpuLocalStatus LeaveInterrupt() noexcept;
    [[nodiscard]] CpuLocalStatus DisablePreemption() noexcept;
    [[nodiscard]] CpuLocalStatus EnablePreemption() noexcept;
    [[nodiscard]] CpuLocalStatus BeginSystemCall(UserContextEntryMethod entry_method) noexcept;
    [[nodiscard]] CpuLocalStatus EndSystemCall() noexcept;
    void RequestReschedule() noexcept;
    [[nodiscard]] bool ConsumeRescheduleRequest() noexcept;
    [[nodiscard]] CpuLocalStatus RecordReturnReschedule() noexcept;
    [[nodiscard]] CpuLocalStatus RecordUserReturn(UserContextEntryMethod entry_method,
                                                  UserReturnMethod return_method) noexcept;
    [[nodiscard]] CpuLocalStatus RecordTrustedStackValidation() noexcept;
    [[nodiscard]] CpuLocalStatus Validate() const noexcept;
    [[nodiscard]] CpuLocalStatistics Statistics() const noexcept;
    [[nodiscard]] uint64_t Address() const noexcept;
    [[nodiscard]] uint64_t SystemCallUserStackPointer() const noexcept;
    [[nodiscard]] bool NativeSystemCallActive() const noexcept;

  private:
    friend consteval bool ValidateCpuLocalLayout() noexcept;

    // 前四项是汇编入口 ABI。只能通过成员函数修改；SYSCALL 入口仅写暂存 RSP。
    uint64_t self_address_{};
    uint64_t current_thread_index_{};
    uint64_t kernel_entry_stack_pointer_{};
    uint64_t system_call_user_stack_pointer_{};
    uint64_t interrupt_depth_{};
    uint64_t maximum_interrupt_depth_{};
    uint64_t preemption_disable_depth_{};
    uint64_t maximum_preemption_disable_depth_{};
    uint64_t need_reschedule_{};
    uint64_t system_call_depth_{};
    uint64_t legacy_system_call_count_{};
    uint64_t native_system_call_count_{};
    uint64_t interrupt_during_system_call_count_{};
    uint64_t return_reschedule_count_{};
    uint64_t system_return_count_{};
    uint64_t interrupt_return_count_{};
    uint64_t native_interrupt_return_count_{};
    uint64_t rejected_return_count_{};
    uint64_t trusted_stack_validation_count_{};
    uint64_t initialized_{};
    uint64_t system_call_entry_method_{};

    [[nodiscard]] CpuLocalStatus IncrementCounter(uint64_t &counter) noexcept;
    [[nodiscard]] bool StackPointerIsValid(uint64_t stack_pointer) const noexcept;
};

class CpuPreemptionGuard final {
  public:
    CpuPreemptionGuard() noexcept;
    ~CpuPreemptionGuard() noexcept;

    CpuPreemptionGuard(const CpuPreemptionGuard &) = delete;
    CpuPreemptionGuard &operator=(const CpuPreemptionGuard &) = delete;

    [[nodiscard]] bool Succeeded() const noexcept;

  private:
    bool acquired_{};
};

[[nodiscard]] CpuLocal &GetCpuLocal() noexcept;

consteval bool ValidateCpuLocalLayout() noexcept {
    return offsetof(CpuLocal, self_address_) == OS_KERNEL_CPU_LOCAL_SELF_ADDRESS_OFFSET &&
           offsetof(CpuLocal, current_thread_index_) ==
               OS_KERNEL_CPU_LOCAL_CURRENT_THREAD_INDEX_OFFSET &&
           offsetof(CpuLocal, kernel_entry_stack_pointer_) ==
               OS_KERNEL_CPU_LOCAL_ENTRY_STACK_POINTER_OFFSET &&
           offsetof(CpuLocal, system_call_user_stack_pointer_) ==
               OS_KERNEL_CPU_LOCAL_SYSTEM_CALL_USER_STACK_POINTER_OFFSET;
}

static_assert(ValidateCpuLocalLayout());
static_assert(alignof(CpuLocal) == OS_KERNEL_CPU_LOCAL_CACHE_LINE_SIZE_BYTES);

}

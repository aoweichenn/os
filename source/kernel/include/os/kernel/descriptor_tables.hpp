#pragma once

#include <stdint.h>

namespace os::kernel {

enum class DescriptorTableValidationStatus : uint64_t {
    Succeeded,
    InvalidGlobalDescriptorTable,
    InvalidInterruptDescriptorTable,
    InvalidCodeSegment,
    InvalidStackSegment,
    InvalidTaskRegister,
    InvalidTaskStateSegment,
};

extern const uint16_t OS_KERNEL_DESCRIPTOR_KERNEL_CODE_SELECTOR;
extern const uint16_t OS_KERNEL_DESCRIPTOR_KERNEL_DATA_SELECTOR;
extern const uint16_t OS_KERNEL_DESCRIPTOR_USER_DATA_SELECTOR;
extern const uint16_t OS_KERNEL_DESCRIPTOR_USER_CODE_SELECTOR;
extern const uint16_t OS_KERNEL_DESCRIPTOR_TASK_STATE_SELECTOR;
extern const uint64_t OS_KERNEL_DESCRIPTOR_INTERRUPT_STACK_GUARD_PAGE_COUNT;

void InitializeGlobalDescriptorTable() noexcept;
void InitializeInterruptDescriptorTable() noexcept;

[[nodiscard]] DescriptorTableValidationStatus ValidateDescriptorTables() noexcept;
[[nodiscard]] uint64_t InterruptStackGuardPageAddress(uint64_t guardPageIndex) noexcept;
[[nodiscard]] uint64_t DefaultPrivilegeStackPointer0() noexcept;
[[nodiscard]] uint64_t CurrentPrivilegeStackPointer0() noexcept;
[[nodiscard]] bool SetPrivilegeStackPointer0(uint64_t stackPointer) noexcept;

}

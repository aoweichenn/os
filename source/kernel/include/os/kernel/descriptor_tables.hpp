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
extern const uint16_t OS_KERNEL_DESCRIPTOR_TASK_STATE_SELECTOR;

void initializeGlobalDescriptorTable(uint64_t privilegeStackTop) noexcept;
void initializeInterruptDescriptorTable() noexcept;

[[nodiscard]] DescriptorTableValidationStatus
validateDescriptorTables(uint64_t expectedPrivilegeStackTop) noexcept;

}

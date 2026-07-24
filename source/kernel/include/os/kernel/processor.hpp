#pragma once

#include <stdint.h>

namespace os::kernel {

extern const uint64_t OS_KERNEL_PROCESSOR_UNMAPPED_TEST_ADDRESS;

[[noreturn]] void haltProcessor() noexcept;
[[nodiscard]] uint64_t readPageTableRoot() noexcept;
[[nodiscard]] uint64_t readPageFaultLinearAddress() noexcept;
void triggerBreakpoint() noexcept;
[[noreturn]] void triggerInvalidOpcode() noexcept;
[[noreturn]] void triggerPageFault() noexcept;

}

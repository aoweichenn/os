#pragma once

#include <stdint.h>

namespace os::kernel {

extern const uint64_t OS_KERNEL_PROCESSOR_UNMAPPED_TEST_ADDRESS;

[[noreturn]] void haltProcessor() noexcept;
[[nodiscard]] uint64_t readPageTableRoot() noexcept;
[[nodiscard]] uint64_t readPageFaultLinearAddress() noexcept;
[[nodiscard]] bool processorSupportsNoExecute() noexcept;
[[nodiscard]] bool enableKernelMemoryProtection() noexcept;
[[nodiscard]] bool kernelMemoryProtectionEnabled() noexcept;
void activatePageTable(uint64_t rootPhysicalAddress) noexcept;
void invalidatePage(uint64_t virtualAddress) noexcept;
void triggerBreakpoint() noexcept;
[[noreturn]] void triggerInvalidOpcode() noexcept;
[[noreturn]] void triggerPageFault() noexcept;
[[noreturn]] void triggerWriteProtectionFault(uint64_t protectedAddress) noexcept;

}

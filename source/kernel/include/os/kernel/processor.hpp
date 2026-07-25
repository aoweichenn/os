#pragma once

#include <stdint.h>

namespace os::kernel {

extern const uint64_t OS_KERNEL_PROCESSOR_UNMAPPED_TEST_ADDRESS;

[[noreturn]] void HaltProcessor() noexcept;
[[nodiscard]] bool DisableInterrupts() noexcept;
void RestoreInterrupts(bool interruptsWereEnabled) noexcept;
void EnableInterrupts() noexcept;
void WaitForInterrupt() noexcept;
void EnableInterruptsWaitAndDisable() noexcept;
[[nodiscard]] uint64_t ReadPageTableRoot() noexcept;
[[nodiscard]] uint64_t ReadPageFaultLinearAddress() noexcept;
[[nodiscard]] uint64_t ProcessorPhysicalAddressWidthBits() noexcept;
[[nodiscard]] uint64_t ProcessorVirtualAddressWidthBits() noexcept;
[[nodiscard]] uint64_t ProcessorMaximumPhysicalAddressExclusive() noexcept;
[[nodiscard]] bool ProcessorSupportsNoExecute() noexcept;
[[nodiscard]] bool ProcessorSupportsLocalApic() noexcept;
[[nodiscard]] bool ProcessorSupportsFiveLevelPaging() noexcept;
[[nodiscard]] uint64_t LocalApicPhysicalAddress() noexcept;
[[nodiscard]] bool EnableKernelMemoryProtection() noexcept;
[[nodiscard]] bool KernelMemoryProtectionEnabled() noexcept;
[[nodiscard]] bool ConfigureLegacyInterruptRouting() noexcept;
void ActivatePageTable(uint64_t rootPhysicalAddress) noexcept;
void InvalidatePage(uint64_t virtualAddress) noexcept;
void TriggerBreakpoint() noexcept;
void TriggerLegacyPicSpuriousInterrupt() noexcept;
[[noreturn]] void TriggerInvalidOpcode() noexcept;
[[noreturn]] void TriggerPageFault() noexcept;
[[noreturn]] void TriggerWriteProtectionFault(uint64_t protectedAddress) noexcept;

}

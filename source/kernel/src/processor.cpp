#include "os/kernel/processor.hpp"

namespace os::kernel {

const uint64_t OS_KERNEL_PROCESSOR_UNMAPPED_TEST_ADDRESS = 0x0000000004000000ULL;

namespace {

constexpr uint32_t OS_KERNEL_PROCESSOR_CPUID_EXTENDED_MAXIMUM_LEAF = 0x80000000U;
constexpr uint32_t OS_KERNEL_PROCESSOR_CPUID_EXTENDED_FEATURES_LEAF = 0x80000001U;
constexpr uint32_t OS_KERNEL_PROCESSOR_CPUID_STANDARD_FEATURES_LEAF = 0x00000001U;
constexpr uint32_t OS_KERNEL_PROCESSOR_CPUID_NO_EXECUTE_BIT = 0x00100000U;
constexpr uint32_t OS_KERNEL_PROCESSOR_CPUID_LOCAL_APIC_BIT = 0x00000200U;
constexpr uint32_t OS_KERNEL_PROCESSOR_IA32_EFER_MSR = 0xC0000080U;
constexpr uint32_t OS_KERNEL_PROCESSOR_IA32_APIC_BASE_MSR = 0x0000001BU;
constexpr uint64_t OS_KERNEL_PROCESSOR_IA32_EFER_NO_EXECUTE_ENABLE_BIT = 0x0000000000000800ULL;
constexpr uint64_t OS_KERNEL_PROCESSOR_IA32_APIC_GLOBAL_ENABLE_BIT = 0x0000000000000800ULL;
constexpr uint64_t OS_KERNEL_PROCESSOR_IA32_APIC_X2_ENABLE_BIT = 0x0000000000000400ULL;
constexpr uint64_t OS_KERNEL_PROCESSOR_IA32_APIC_BASE_ADDRESS_MASK = 0x0000000FFFFFF000ULL;
constexpr uint64_t OS_KERNEL_PROCESSOR_CR0_WRITE_PROTECT_BIT = 0x0000000000010000ULL;
constexpr uint64_t OS_KERNEL_PROCESSOR_REGISTER_HALF_WIDTH_BITS = 32ULL;
constexpr uint64_t OS_KERNEL_PROCESSOR_RFLAGS_INTERRUPT_ENABLE_BIT = 0x0000000000000200ULL;
constexpr uint64_t OS_KERNEL_PROCESSOR_LOCAL_APIC_SPURIOUS_REGISTER_OFFSET = 0x00000000000000F0ULL;
constexpr uint64_t OS_KERNEL_PROCESSOR_LOCAL_APIC_LINT0_REGISTER_OFFSET = 0x0000000000000350ULL;
constexpr uint32_t OS_KERNEL_PROCESSOR_LOCAL_APIC_SPURIOUS_VECTOR_MASK = 0x000000FFU;
constexpr uint32_t OS_KERNEL_PROCESSOR_LOCAL_APIC_SPURIOUS_VECTOR = 0x000000FFU;
constexpr uint32_t OS_KERNEL_PROCESSOR_LOCAL_APIC_SOFTWARE_ENABLE_BIT = 0x00000100U;
constexpr uint32_t OS_KERNEL_PROCESSOR_LOCAL_APIC_DELIVERY_MODE_MASK = 0x00000700U;
constexpr uint32_t OS_KERNEL_PROCESSOR_LOCAL_APIC_EXTINT_DELIVERY_MODE = 0x00000700U;
constexpr uint32_t OS_KERNEL_PROCESSOR_LOCAL_APIC_MASK_BIT = 0x00010000U;

struct CpuIdResult final {
    uint32_t accumulator;
    uint32_t base;
    uint32_t counter;
    uint32_t data;
};

[[nodiscard]] CpuIdResult ReadCpuId(const uint32_t leaf) noexcept {
    CpuIdResult result{};
    asm volatile("cpuid"
                 : "=a"(result.accumulator), "=b"(result.base), "=c"(result.counter),
                   "=d"(result.data)
                 : "a"(leaf), "c"(0U));
    return result;
}

[[nodiscard]] uint64_t ReadModelSpecificRegister(const uint32_t registerIndex) noexcept {
    uint32_t lowValue = 0U;
    uint32_t highValue = 0U;
    asm volatile("rdmsr" : "=a"(lowValue), "=d"(highValue) : "c"(registerIndex));
    return static_cast<uint64_t>(lowValue) |
           (static_cast<uint64_t>(highValue) << OS_KERNEL_PROCESSOR_REGISTER_HALF_WIDTH_BITS);
}

void WriteModelSpecificRegister(const uint32_t registerIndex, const uint64_t value) noexcept {
    const uint32_t lowValue = static_cast<uint32_t>(value);
    const uint32_t highValue =
        static_cast<uint32_t>(value >> OS_KERNEL_PROCESSOR_REGISTER_HALF_WIDTH_BITS);
    asm volatile("wrmsr" : : "c"(registerIndex), "a"(lowValue), "d"(highValue));
}

[[nodiscard]] uint64_t ReadControlRegister0() noexcept {
    uint64_t value = 0ULL;
    asm volatile("mov %0, cr0" : "=r"(value));
    return value;
}

void WriteControlRegister0(const uint64_t value) noexcept {
    asm volatile("mov cr0, %0" : : "r"(value) : "memory");
}

[[nodiscard]] volatile uint32_t *LocalApicRegister(const uint64_t registerOffset) noexcept {
    return reinterpret_cast<volatile uint32_t *>(LocalApicPhysicalAddress() + registerOffset);
}

}

[[noreturn]] void HaltProcessor() noexcept {
    asm volatile("cli");
    while (true) {
        asm volatile("hlt");
    }
}

bool DisableInterrupts() noexcept {
    uint64_t flags = 0ULL;
    asm volatile("pushfq; pop %0; cli" : "=r"(flags) : : "memory");
    return (flags & OS_KERNEL_PROCESSOR_RFLAGS_INTERRUPT_ENABLE_BIT) != 0ULL;
}

void RestoreInterrupts(const bool interruptsWereEnabled) noexcept {
    if (interruptsWereEnabled) {
        asm volatile("sti" : : : "memory");
    }
}

void EnableInterrupts() noexcept { asm volatile("sti" : : : "memory"); }

void WaitForInterrupt() noexcept { asm volatile("hlt" : : : "memory"); }

uint64_t ReadPageTableRoot() noexcept {
    uint64_t pageTableRoot = 0ULL;
    asm volatile("mov %0, cr3" : "=r"(pageTableRoot));
    return pageTableRoot;
}

uint64_t ReadPageFaultLinearAddress() noexcept {
    uint64_t pageFaultLinearAddress = 0ULL;
    asm volatile("mov %0, cr2" : "=r"(pageFaultLinearAddress));
    return pageFaultLinearAddress;
}

bool ProcessorSupportsNoExecute() noexcept {
    const CpuIdResult maximumLeaf = ReadCpuId(OS_KERNEL_PROCESSOR_CPUID_EXTENDED_MAXIMUM_LEAF);
    if (maximumLeaf.accumulator < OS_KERNEL_PROCESSOR_CPUID_EXTENDED_FEATURES_LEAF) {
        return false;
    }
    const CpuIdResult features = ReadCpuId(OS_KERNEL_PROCESSOR_CPUID_EXTENDED_FEATURES_LEAF);
    return (features.data & OS_KERNEL_PROCESSOR_CPUID_NO_EXECUTE_BIT) != 0U;
}

bool ProcessorSupportsLocalApic() noexcept {
    const CpuIdResult features = ReadCpuId(OS_KERNEL_PROCESSOR_CPUID_STANDARD_FEATURES_LEAF);
    return (features.data & OS_KERNEL_PROCESSOR_CPUID_LOCAL_APIC_BIT) != 0U;
}

uint64_t LocalApicPhysicalAddress() noexcept {
    return ReadModelSpecificRegister(OS_KERNEL_PROCESSOR_IA32_APIC_BASE_MSR) &
           OS_KERNEL_PROCESSOR_IA32_APIC_BASE_ADDRESS_MASK;
}

bool EnableKernelMemoryProtection() noexcept {
    if (!ProcessorSupportsNoExecute()) {
        return false;
    }
    const uint64_t extendedFeatureRegister =
        ReadModelSpecificRegister(OS_KERNEL_PROCESSOR_IA32_EFER_MSR) |
        OS_KERNEL_PROCESSOR_IA32_EFER_NO_EXECUTE_ENABLE_BIT;
    WriteModelSpecificRegister(OS_KERNEL_PROCESSOR_IA32_EFER_MSR, extendedFeatureRegister);
    WriteControlRegister0(ReadControlRegister0() | OS_KERNEL_PROCESSOR_CR0_WRITE_PROTECT_BIT);
    return KernelMemoryProtectionEnabled();
}

bool KernelMemoryProtectionEnabled() noexcept {
    return (ReadModelSpecificRegister(OS_KERNEL_PROCESSOR_IA32_EFER_MSR) &
            OS_KERNEL_PROCESSOR_IA32_EFER_NO_EXECUTE_ENABLE_BIT) != 0ULL &&
           (ReadControlRegister0() & OS_KERNEL_PROCESSOR_CR0_WRITE_PROTECT_BIT) != 0ULL;
}

bool ConfigureLegacyInterruptRouting() noexcept {
    if (!ProcessorSupportsLocalApic()) {
        return true;
    }
    const uint64_t localApicBase =
        ReadModelSpecificRegister(OS_KERNEL_PROCESSOR_IA32_APIC_BASE_MSR);
    if ((localApicBase & OS_KERNEL_PROCESSOR_IA32_APIC_GLOBAL_ENABLE_BIT) == 0ULL ||
        (localApicBase & OS_KERNEL_PROCESSOR_IA32_APIC_X2_ENABLE_BIT) != 0ULL) {
        return false;
    }

    volatile uint32_t *const spuriousRegister =
        LocalApicRegister(OS_KERNEL_PROCESSOR_LOCAL_APIC_SPURIOUS_REGISTER_OFFSET);
    const uint32_t spuriousValue =
        (*spuriousRegister & ~OS_KERNEL_PROCESSOR_LOCAL_APIC_SPURIOUS_VECTOR_MASK) |
        OS_KERNEL_PROCESSOR_LOCAL_APIC_SPURIOUS_VECTOR |
        OS_KERNEL_PROCESSOR_LOCAL_APIC_SOFTWARE_ENABLE_BIT;
    *spuriousRegister = spuriousValue;

    volatile uint32_t *const lint0Register =
        LocalApicRegister(OS_KERNEL_PROCESSOR_LOCAL_APIC_LINT0_REGISTER_OFFSET);
    const uint32_t lint0Value =
        (*lint0Register & ~(OS_KERNEL_PROCESSOR_LOCAL_APIC_DELIVERY_MODE_MASK |
                            OS_KERNEL_PROCESSOR_LOCAL_APIC_MASK_BIT)) |
        OS_KERNEL_PROCESSOR_LOCAL_APIC_EXTINT_DELIVERY_MODE;
    *lint0Register = lint0Value;
    asm volatile("" : : : "memory");

    return (*spuriousRegister & OS_KERNEL_PROCESSOR_LOCAL_APIC_SOFTWARE_ENABLE_BIT) != 0U &&
           (*spuriousRegister & OS_KERNEL_PROCESSOR_LOCAL_APIC_SPURIOUS_VECTOR_MASK) ==
               OS_KERNEL_PROCESSOR_LOCAL_APIC_SPURIOUS_VECTOR &&
           (*lint0Register & OS_KERNEL_PROCESSOR_LOCAL_APIC_DELIVERY_MODE_MASK) ==
               OS_KERNEL_PROCESSOR_LOCAL_APIC_EXTINT_DELIVERY_MODE &&
           (*lint0Register & OS_KERNEL_PROCESSOR_LOCAL_APIC_MASK_BIT) == 0U;
}

void ActivatePageTable(const uint64_t rootPhysicalAddress) noexcept {
    asm volatile("mov cr3, %0" : : "r"(rootPhysicalAddress) : "memory");
}

void InvalidatePage(const uint64_t virtualAddress) noexcept {
    asm volatile("invlpg [%0]" : : "r"(virtualAddress) : "memory");
}

void TriggerBreakpoint() noexcept { asm volatile("int3"); }

void TriggerLegacyPicSpuriousInterrupt() noexcept { asm volatile("int 0x27"); }

[[noreturn]] void TriggerInvalidOpcode() noexcept {
    asm volatile("ud2");
    HaltProcessor();
}

[[noreturn]] void TriggerPageFault() noexcept {
    const volatile uint64_t *const unmappedAddress =
        reinterpret_cast<const volatile uint64_t *>(OS_KERNEL_PROCESSOR_UNMAPPED_TEST_ADDRESS);
    static_cast<void>(*unmappedAddress);
    HaltProcessor();
}

[[noreturn]] void TriggerWriteProtectionFault(const uint64_t protectedAddress) noexcept {
    volatile uint64_t *const writeProtectedAddress =
        reinterpret_cast<volatile uint64_t *>(protectedAddress);
    *writeProtectedAddress = protectedAddress;
    HaltProcessor();
}

}

#include "os/kernel/processor.hpp"

namespace os::kernel {

const uint64_t OS_KERNEL_PROCESSOR_UNMAPPED_TEST_ADDRESS = 0x0000000004000000ULL;

namespace {

constexpr uint32_t OS_KERNEL_PROCESSOR_CPUID_EXTENDED_MAXIMUM_LEAF = 0x80000000U;
constexpr uint32_t OS_KERNEL_PROCESSOR_CPUID_EXTENDED_FEATURES_LEAF = 0x80000001U;
constexpr uint32_t OS_KERNEL_PROCESSOR_CPUID_NO_EXECUTE_BIT = 0x00100000U;
constexpr uint32_t OS_KERNEL_PROCESSOR_IA32_EFER_MSR = 0xC0000080U;
constexpr uint64_t OS_KERNEL_PROCESSOR_IA32_EFER_NO_EXECUTE_ENABLE_BIT = 0x0000000000000800ULL;
constexpr uint64_t OS_KERNEL_PROCESSOR_CR0_WRITE_PROTECT_BIT = 0x0000000000010000ULL;
constexpr uint64_t OS_KERNEL_PROCESSOR_REGISTER_HALF_WIDTH_BITS = 32ULL;

struct CpuIdResult final {
    uint32_t accumulator;
    uint32_t base;
    uint32_t counter;
    uint32_t data;
};

[[nodiscard]] CpuIdResult readCpuId(const uint32_t leaf) noexcept {
    CpuIdResult result{};
    asm volatile("cpuid"
                 : "=a"(result.accumulator), "=b"(result.base), "=c"(result.counter),
                   "=d"(result.data)
                 : "a"(leaf), "c"(0U));
    return result;
}

[[nodiscard]] uint64_t readModelSpecificRegister(const uint32_t registerIndex) noexcept {
    uint32_t lowValue = 0U;
    uint32_t highValue = 0U;
    asm volatile("rdmsr" : "=a"(lowValue), "=d"(highValue) : "c"(registerIndex));
    return static_cast<uint64_t>(lowValue) |
           (static_cast<uint64_t>(highValue) << OS_KERNEL_PROCESSOR_REGISTER_HALF_WIDTH_BITS);
}

void writeModelSpecificRegister(const uint32_t registerIndex, const uint64_t value) noexcept {
    const uint32_t lowValue = static_cast<uint32_t>(value);
    const uint32_t highValue =
        static_cast<uint32_t>(value >> OS_KERNEL_PROCESSOR_REGISTER_HALF_WIDTH_BITS);
    asm volatile("wrmsr" : : "c"(registerIndex), "a"(lowValue), "d"(highValue));
}

[[nodiscard]] uint64_t readControlRegister0() noexcept {
    uint64_t value = 0ULL;
    asm volatile("mov %0, cr0" : "=r"(value));
    return value;
}

void writeControlRegister0(const uint64_t value) noexcept {
    asm volatile("mov cr0, %0" : : "r"(value) : "memory");
}

}

[[noreturn]] void haltProcessor() noexcept {
    asm volatile("cli");
    while (true) {
        asm volatile("hlt");
    }
}

uint64_t readPageTableRoot() noexcept {
    uint64_t pageTableRoot = 0ULL;
    asm volatile("mov %0, cr3" : "=r"(pageTableRoot));
    return pageTableRoot;
}

uint64_t readPageFaultLinearAddress() noexcept {
    uint64_t pageFaultLinearAddress = 0ULL;
    asm volatile("mov %0, cr2" : "=r"(pageFaultLinearAddress));
    return pageFaultLinearAddress;
}

bool processorSupportsNoExecute() noexcept {
    const CpuIdResult maximumLeaf = readCpuId(OS_KERNEL_PROCESSOR_CPUID_EXTENDED_MAXIMUM_LEAF);
    if (maximumLeaf.accumulator < OS_KERNEL_PROCESSOR_CPUID_EXTENDED_FEATURES_LEAF) {
        return false;
    }
    const CpuIdResult features = readCpuId(OS_KERNEL_PROCESSOR_CPUID_EXTENDED_FEATURES_LEAF);
    return (features.data & OS_KERNEL_PROCESSOR_CPUID_NO_EXECUTE_BIT) != 0U;
}

bool enableKernelMemoryProtection() noexcept {
    if (!processorSupportsNoExecute()) {
        return false;
    }
    const uint64_t extendedFeatureRegister =
        readModelSpecificRegister(OS_KERNEL_PROCESSOR_IA32_EFER_MSR) |
        OS_KERNEL_PROCESSOR_IA32_EFER_NO_EXECUTE_ENABLE_BIT;
    writeModelSpecificRegister(OS_KERNEL_PROCESSOR_IA32_EFER_MSR, extendedFeatureRegister);
    writeControlRegister0(readControlRegister0() | OS_KERNEL_PROCESSOR_CR0_WRITE_PROTECT_BIT);
    return kernelMemoryProtectionEnabled();
}

bool kernelMemoryProtectionEnabled() noexcept {
    return (readModelSpecificRegister(OS_KERNEL_PROCESSOR_IA32_EFER_MSR) &
            OS_KERNEL_PROCESSOR_IA32_EFER_NO_EXECUTE_ENABLE_BIT) != 0ULL &&
           (readControlRegister0() & OS_KERNEL_PROCESSOR_CR0_WRITE_PROTECT_BIT) != 0ULL;
}

void activatePageTable(const uint64_t rootPhysicalAddress) noexcept {
    asm volatile("mov cr3, %0" : : "r"(rootPhysicalAddress) : "memory");
}

void invalidatePage(const uint64_t virtualAddress) noexcept {
    asm volatile("invlpg [%0]" : : "r"(virtualAddress) : "memory");
}

void triggerBreakpoint() noexcept { asm volatile("int3"); }

[[noreturn]] void triggerInvalidOpcode() noexcept {
    asm volatile("ud2");
    haltProcessor();
}

[[noreturn]] void triggerPageFault() noexcept {
    const volatile uint64_t *const unmappedAddress =
        reinterpret_cast<const volatile uint64_t *>(OS_KERNEL_PROCESSOR_UNMAPPED_TEST_ADDRESS);
    static_cast<void>(*unmappedAddress);
    haltProcessor();
}

[[noreturn]] void triggerWriteProtectionFault(const uint64_t protectedAddress) noexcept {
    volatile uint64_t *const writeProtectedAddress =
        reinterpret_cast<volatile uint64_t *>(protectedAddress);
    *writeProtectedAddress = protectedAddress;
    haltProcessor();
}

}

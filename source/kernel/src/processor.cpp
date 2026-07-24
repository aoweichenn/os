#include "os/kernel/processor.hpp"

namespace os::kernel {

const uint64_t OS_KERNEL_PROCESSOR_UNMAPPED_TEST_ADDRESS = 0x0000000004000000ULL;

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

}

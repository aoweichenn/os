#include "os/kernel/entry.hpp"

namespace os::kernel {

extern "C" [[noreturn, gnu::section(".text.os_kernel_entry")]]
void osKernelEntry() noexcept {
    // v0.4 首个内核产物只建立可信 ELF64 入口；串口与 BootInfo 在装载交接后接入。
    while (true) {
        asm volatile("hlt");
    }
}

}

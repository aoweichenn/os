#include "os/kernel/boot/entry.hpp"
#include "os/kernel/core/kernel_main.hpp"

namespace os::kernel {

extern "C" [[noreturn, gnu::section(".text.os_kernel_entry")]]
void OsKernelEntry(const BootInfo *boot_info) noexcept {
    RunKernel(boot_info, KernelFaultInjection::PageFault, UserProgramSelection::Smoke);
}
}

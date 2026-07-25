#include "os/kernel/entry.hpp"
#include "os/kernel/kernel_main.hpp"

namespace os::kernel {

extern "C" [[noreturn, gnu::section(".text.os_kernel_entry")]]
void OsKernelEntry(const BootInfo *boot_info) noexcept {
    RunKernel(boot_info, KernelFaultInjection::WriteProtection, UserProgramSelection::Smoke);
}
}

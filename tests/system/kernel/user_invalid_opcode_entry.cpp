#include "os/kernel/entry.hpp"
#include "os/kernel/kernel_main.hpp"

namespace os::kernel {

extern "C" [[noreturn, gnu::section(".text.os_kernel_entry")]]
void osKernelEntry(const BootInfo *bootInfo) noexcept {
    RunKernel(bootInfo, KernelFaultInjection::None, UserProgramSelection::InvalidOpcode);
}

}

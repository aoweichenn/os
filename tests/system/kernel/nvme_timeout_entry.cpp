#include <os/kernel/boot/entry.hpp>
#include <os/kernel/core/kernel_main.hpp>
#include <os/kernel/device/nvme.hpp>

namespace os::kernel {

extern "C" [[noreturn, gnu::section(".text.os_kernel_entry")]]
void OsKernelEntry(const BootInfo *const boot_info) noexcept {
    ArmNvmeCommandTimeoutInjection();
    RunKernel(boot_info, KernelFaultInjection::None, UserProgramSelection::Smoke);
}

}

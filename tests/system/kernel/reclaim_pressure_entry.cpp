#include <os/kernel/arch/processor.hpp>
#include <os/kernel/boot/entry.hpp>
#include <os/kernel/core/kernel_main.hpp>
#include <os/kernel/user/user_memory.hpp>

namespace os::kernel {

namespace {

constexpr uint64_t OS_TEST_RECLAIM_PRESSURE_RESIDENT_LIMIT_PAGE_COUNT = 9216ULL;

}

extern "C" [[noreturn, gnu::section(".text.os_kernel_entry")]]
void OsKernelEntry(const BootInfo *boot_info) noexcept {
    if (!ConfigureUserMemoryResidentLimit(
            OS_TEST_RECLAIM_PRESSURE_RESIDENT_LIMIT_PAGE_COUNT)) {
        HaltProcessor();
    }
    RunKernel(boot_info, KernelFaultInjection::None, UserProgramSelection::Smoke);
}

}

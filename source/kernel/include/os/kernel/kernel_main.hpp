#pragma once

#include "os/kernel/boot_info.hpp"
#include "os/kernel/user_program_images.hpp"

#include <stdint.h>

namespace os::kernel {

enum class KernelFaultInjection : uint64_t {
    None,
    InvalidOpcode,
    PageFault,
    WriteProtection,
};

[[noreturn]] void RunKernel(const BootInfo *boot_info, KernelFaultInjection fault_injection,
                            UserProgramSelection user_program_selection) noexcept;
}

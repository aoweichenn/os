#pragma once

#include "os/kernel/boot_info.hpp"

#include <stdint.h>

namespace os::kernel {

enum class KernelFaultInjection : uint64_t {
    None,
    InvalidOpcode,
    PageFault,
};

[[noreturn]] void runKernel(const BootInfo *bootInfo, KernelFaultInjection faultInjection) noexcept;

}

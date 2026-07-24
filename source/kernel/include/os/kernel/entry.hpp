#pragma once

#include "os/kernel/boot_info.hpp"

namespace os::kernel {

extern "C" [[noreturn]] void osKernelEntry(const BootInfo *bootInfo) noexcept;

}

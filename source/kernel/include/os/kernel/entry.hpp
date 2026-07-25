#pragma once

#include "os/kernel/boot_info.hpp"

namespace os::kernel {

extern "C" [[noreturn]] void OsKernelEntry(const BootInfo *boot_info) noexcept;
}

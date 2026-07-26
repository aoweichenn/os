#pragma once

#include "os/kernel/arch/exception_frame.hpp"

#include <stdint.h>

namespace os::kernel {

extern "C" [[nodiscard]] ExceptionFrame *OsKernelDispatchSystemCall(ExceptionFrame *frame) noexcept;
extern "C" [[nodiscard]] ExceptionFrame *
OsKernelPrepareUserReturn(ExceptionFrame *frame) noexcept;
extern "C" [[nodiscard]] uint64_t
OsKernelSelectUserReturn(const ExceptionFrame *frame) noexcept;

}

#pragma once

#include "os/kernel/arch/exception_frame.hpp"

namespace os::kernel {

extern "C" [[nodiscard]] ExceptionFrame *OsKernelDispatchSystemCall(ExceptionFrame *frame) noexcept;

}

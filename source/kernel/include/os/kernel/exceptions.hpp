#pragma once

#include "os/kernel/exception_frame.hpp"

namespace os::kernel {

extern "C" [[nodiscard]] ExceptionFrame *OsKernelDispatchException(ExceptionFrame *frame) noexcept;

}

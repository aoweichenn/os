#pragma once

#include "os/kernel/exception_frame.hpp"

namespace os::kernel {

extern "C" [[nodiscard]] ExceptionFrame *osKernelDispatchException(ExceptionFrame *frame) noexcept;

}

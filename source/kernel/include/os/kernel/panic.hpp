#pragma once

#include "os/kernel/exception_frame.hpp"

namespace os::kernel {

[[noreturn]] void PanicFromException(const ExceptionFrame &frame) noexcept;

}

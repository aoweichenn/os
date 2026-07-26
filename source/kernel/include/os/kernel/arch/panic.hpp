#pragma once

#include "os/kernel/arch/exception_frame.hpp"

namespace os::kernel {

[[noreturn]] void PanicFromException(const ExceptionFrame &frame) noexcept;

}

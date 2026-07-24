#pragma once

#include "os/kernel/exception_frame.hpp"

namespace os::kernel {

[[noreturn]] void panicFromException(const ExceptionFrame &frame) noexcept;

}

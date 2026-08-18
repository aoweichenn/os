#pragma once

#include "os/abi/time.hpp"

#include <stdint.h>

namespace os::kernel {

enum class RealtimeClockStatus : uint64_t {
    Succeeded,
    InvalidCalendar,
    OutOfRange,
};

[[nodiscard]] bool IsLeapYear(uint64_t year) noexcept;
[[nodiscard]] RealtimeClockStatus
CalendarToRealtimeInformation(uint64_t year, uint64_t month, uint64_t day, uint64_t hour,
                              uint64_t minute, uint64_t second,
                              os::abi::RealtimeInformation &information) noexcept;

}

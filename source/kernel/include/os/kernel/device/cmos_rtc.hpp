#pragma once

#include "os/abi/time.hpp"

#include <stdint.h>

namespace os::kernel {

enum class CmosRtcStatus : uint64_t {
    Succeeded,
    TimedOut,
    Unstable,
    InvalidCalendar,
};

[[nodiscard]] CmosRtcStatus ReadCmosRtc(os::abi::RealtimeInformation &information) noexcept;

}

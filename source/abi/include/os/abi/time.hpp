#pragma once

#include <stdint.h>

namespace os::abi {

inline constexpr uint64_t OS_ABI_TIME_NANOSECONDS_PER_MICROSECOND = 1'000ULL;
inline constexpr uint64_t OS_ABI_TIME_NANOSECONDS_PER_MILLISECOND = 1'000'000ULL;
inline constexpr uint64_t OS_ABI_TIME_NANOSECONDS_PER_SECOND = 1'000'000'000ULL;

inline constexpr uint64_t OS_ABI_REALTIME_INFORMATION_SIZE_BYTES = 64ULL;

struct RealtimeInformation final {
    uint64_t year;
    uint64_t month;
    uint64_t day;
    uint64_t hour;
    uint64_t minute;
    uint64_t second;
    uint64_t unix_seconds;
    uint64_t reserved;
};

static_assert(sizeof(RealtimeInformation) == OS_ABI_REALTIME_INFORMATION_SIZE_BYTES);

}

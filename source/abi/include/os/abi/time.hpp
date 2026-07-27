#pragma once

#include <stdint.h>

namespace os::abi {

inline constexpr uint64_t OS_ABI_TIME_NANOSECONDS_PER_MICROSECOND = 1'000ULL;
inline constexpr uint64_t OS_ABI_TIME_NANOSECONDS_PER_MILLISECOND = 1'000'000ULL;
inline constexpr uint64_t OS_ABI_TIME_NANOSECONDS_PER_SECOND = 1'000'000'000ULL;

}

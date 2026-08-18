#pragma once

#include <stdint.h>

namespace os::abi {

inline constexpr uint64_t OS_ABI_VERSION_MAJOR = 2ULL;
inline constexpr uint64_t OS_ABI_VERSION_MINOR = 3ULL;
inline constexpr uint64_t OS_ABI_VERSION_PATCH = 0ULL;
inline constexpr uint64_t OS_ABI_SYSTEM_CALL_COUNT = 84ULL;
inline constexpr uint64_t OS_ABI_SYSTEM_CALL_LAST_NUMBER = 84ULL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_FIRST_ERROR = -1LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_LAST_ERROR = -59LL;

}

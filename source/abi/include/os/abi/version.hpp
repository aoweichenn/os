#pragma once

#include <stdint.h>

namespace os::abi {

inline constexpr uint64_t OS_ABI_VERSION_MAJOR = 2ULL;
inline constexpr uint64_t OS_ABI_VERSION_MINOR = 4ULL;
inline constexpr uint64_t OS_ABI_VERSION_PATCH = 0ULL;
inline constexpr uint64_t OS_ABI_SYSTEM_CALL_COUNT = 87ULL;
inline constexpr uint64_t OS_ABI_SYSTEM_CALL_LAST_NUMBER = 87ULL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_FIRST_ERROR = -1LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_LAST_ERROR = -59LL;

}

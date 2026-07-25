#pragma once

#include <stdint.h>

namespace os::abi {

enum class SystemCallNumber : uint64_t {
    WriteLog = 1ULL,
    ExitProcess = 2ULL,
};

inline constexpr uint64_t OS_ABI_SYSTEM_CALL_VECTOR = 0x80ULL;
inline constexpr uint64_t OS_ABI_SYSTEM_CALL_MAXIMUM_WRITE_SIZE_BYTES = 160ULL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_INVALID_USER_MEMORY = -1LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_UNKNOWN_NUMBER = -2LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_WRITE_TOO_LARGE = -3LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_DEVICE_FAILURE = -4LL;

}

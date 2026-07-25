#pragma once

#include <stdint.h>

namespace os::abi {

enum class SystemCallNumber : uint64_t {
    WriteLog = 1ULL,
    ExitProcess = 2ULL,
    GetProcessId = 3ULL,
    TryReadPipe = 4ULL,
    TryWritePipe = 5ULL,
    WaitPipeReadable = 6ULL,
    WaitPipeWritable = 7ULL,
    ClosePipeReader = 8ULL,
    ClosePipeWriter = 9ULL,
};

inline constexpr uint64_t OS_ABI_SYSTEM_CALL_VECTOR = 0x80ULL;
inline constexpr uint64_t OS_ABI_SYSTEM_CALL_MAXIMUM_WRITE_SIZE_BYTES = 160ULL;
inline constexpr uint64_t OS_ABI_SYSTEM_CALL_MAXIMUM_PIPE_TRANSFER_SIZE_BYTES = 64ULL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_INVALID_USER_MEMORY = -1LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_UNKNOWN_NUMBER = -2LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_WRITE_TOO_LARGE = -3LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_DEVICE_FAILURE = -4LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_WOULD_BLOCK = -5LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_BROKEN_PIPE = -6LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_PIPE_PERMISSION_DENIED = -7LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_ENDPOINT_CLOSED = -8LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_NO_READY_PROCESS = -9LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_INVALID_ARGUMENT = -10LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_PIPE_TRANSFER_TOO_LARGE = -11LL;

}

#pragma once

#include <stdint.h>

namespace os::abi {

enum class TerminalInputMode : uint64_t {
    Canonical = 0ULL,
    ShellEditor = 1ULL,
};

enum class ProcessTerminationReason : uint64_t {
    None = 0ULL,
    Exited = 1ULL,
    Exception = 2ULL,
    Signal = 3ULL,
};

inline constexpr uint64_t OS_ABI_PROCESS_WAIT_EXITED_FLAG = 1ULL << 0ULL;
inline constexpr uint64_t OS_ABI_PROCESS_WAIT_STOPPED_FLAG = 1ULL << 1ULL;
inline constexpr uint64_t OS_ABI_PROCESS_WAIT_CONTINUED_FLAG = 1ULL << 2ULL;
inline constexpr uint64_t OS_ABI_PROCESS_WAIT_NO_HANG_FLAG = 1ULL << 3ULL;
inline constexpr uint64_t OS_ABI_PROCESS_WAIT_VALID_FLAG_MASK =
    OS_ABI_PROCESS_WAIT_EXITED_FLAG | OS_ABI_PROCESS_WAIT_STOPPED_FLAG |
    OS_ABI_PROCESS_WAIT_CONTINUED_FLAG | OS_ABI_PROCESS_WAIT_NO_HANG_FLAG;

enum class ProcessWaitEventType : uint64_t {
    Exited = 1ULL,
    Stopped = 2ULL,
    Continued = 3ULL,
};

inline constexpr uint64_t OS_ABI_PROCESS_WAIT_EVENT_RESULT_SIZE_BYTES = 56ULL;

struct ProcessWaitEventResult final {
    uint64_t process_id;
    uint64_t parent_process_id;
    ProcessWaitEventType event_type;
    ProcessTerminationReason termination_reason;
    int64_t exit_code;
    uint64_t exception_vector;
    uint64_t signal_number;
};

static_assert(sizeof(ProcessWaitEventResult) == OS_ABI_PROCESS_WAIT_EVENT_RESULT_SIZE_BYTES);

inline constexpr uint64_t OS_ABI_TERMINAL_INFORMATION_SIZE_BYTES = 24ULL;

struct TerminalInformation final {
    uint64_t terminal_id;
    uint64_t controlling_session_id;
    uint64_t foreground_process_group_id;
};

static_assert(sizeof(TerminalInformation) == OS_ABI_TERMINAL_INFORMATION_SIZE_BYTES);

}

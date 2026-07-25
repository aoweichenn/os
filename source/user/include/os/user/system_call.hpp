#pragma once

#include <stdint.h>

namespace os::user {

[[nodiscard]] int64_t InvokeSystemCall(uint64_t systemCallNumber, uint64_t argument0,
                                       uint64_t argument1) noexcept;
[[nodiscard]] int64_t WriteLog(const char *message, uint64_t messageSizeBytes) noexcept;
[[nodiscard]] uint64_t GetProcessId() noexcept;
[[noreturn]] void ExitProcess(int64_t exitCode) noexcept;

}

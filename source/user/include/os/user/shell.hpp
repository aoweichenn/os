#pragma once

#include <stdint.h>

namespace os::user {

[[nodiscard]] int64_t RunShell(uint64_t argument_count, const char *const *arguments,
                               const char *const *environment) noexcept;
[[nodiscard]] int64_t RunShellCommand(const char *command, uint64_t command_length_bytes) noexcept;

}

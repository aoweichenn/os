#pragma once

#include <stdint.h>

namespace os::user {

inline constexpr uint64_t OS_USER_SHELL_MAXIMUM_LINE_SIZE_BYTES = 128ULL;
inline constexpr uint64_t OS_USER_SHELL_MAXIMUM_ARGUMENT_COUNT = 8ULL;
inline constexpr uint64_t OS_USER_SHELL_STORAGE_TERMINATOR_SIZE_BYTES = 1ULL;
inline constexpr uint64_t OS_USER_SHELL_STORAGE_SIZE_BYTES =
    OS_USER_SHELL_MAXIMUM_LINE_SIZE_BYTES + OS_USER_SHELL_STORAGE_TERMINATOR_SIZE_BYTES;

enum class ShellParseStatus : uint64_t {
    Succeeded,
    Empty,
    LineTooLong,
    TooManyArguments,
    UnterminatedQuote,
    DanglingEscape,
    InvalidArgument,
};

enum class ShellCommand : uint64_t {
    Help,
    Echo,
    PrintWorkingDirectory,
    ListDirectory,
    CreateDirectory,
    WriteFile,
    ConcatenateFile,
    Synchronize,
    Exit,
    Unknown,
};

struct ShellArgument final {
    uint64_t offset_bytes;
    uint64_t length_bytes;
};

struct ShellCommandLine final {
    char storage[OS_USER_SHELL_STORAGE_SIZE_BYTES];
    ShellArgument arguments[OS_USER_SHELL_MAXIMUM_ARGUMENT_COUNT];
    uint64_t argument_count;
};

[[nodiscard]] ShellParseStatus ParseShellCommandLine(const char *line, uint64_t line_length_bytes,
                                                     ShellCommandLine &command_line) noexcept;
[[nodiscard]] ShellCommand ResolveShellCommand(const ShellCommandLine &command_line) noexcept;
[[nodiscard]] const char *ShellArgumentBytes(const ShellCommandLine &command_line,
                                             uint64_t argument_index) noexcept;
}

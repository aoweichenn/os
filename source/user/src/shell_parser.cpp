#include "os/user/shell_parser.hpp"

namespace os::user {

namespace {

constexpr uint64_t OS_USER_SHELL_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_USER_SHELL_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_USER_SHELL_COMMAND_ARGUMENT_INDEX = 0ULL;
constexpr char OS_USER_SHELL_STRING_TERMINATOR = '\0';
constexpr char OS_USER_SHELL_SPACE = ' ';
constexpr char OS_USER_SHELL_TAB = '\t';
constexpr char OS_USER_SHELL_SINGLE_QUOTE = '\'';
constexpr char OS_USER_SHELL_DOUBLE_QUOTE = '"';
constexpr char OS_USER_SHELL_ESCAPE = '\\';
constexpr char OS_USER_SHELL_HELP_COMMAND[] = "help";
constexpr char OS_USER_SHELL_ECHO_COMMAND[] = "echo";
constexpr char OS_USER_SHELL_PWD_COMMAND[] = "pwd";
constexpr char OS_USER_SHELL_CD_COMMAND[] = "cd";
constexpr char OS_USER_SHELL_LS_COMMAND[] = "ls";
constexpr char OS_USER_SHELL_MKDIR_COMMAND[] = "mkdir";
constexpr char OS_USER_SHELL_WRITE_COMMAND[] = "write";
constexpr char OS_USER_SHELL_CAT_COMMAND[] = "cat";
constexpr char OS_USER_SHELL_RM_COMMAND[] = "rm";
constexpr char OS_USER_SHELL_RMDIR_COMMAND[] = "rmdir";
constexpr char OS_USER_SHELL_MV_COMMAND[] = "mv";
constexpr char OS_USER_SHELL_TRUNCATE_COMMAND[] = "truncate";
constexpr char OS_USER_SHELL_STAT_COMMAND[] = "stat";
constexpr char OS_USER_SHELL_SYNC_COMMAND[] = "sync";
constexpr char OS_USER_SHELL_EXIT_COMMAND[] = "exit";

enum class ShellQuoteState : uint64_t {
    None,
    Single,
    Double,
};

[[nodiscard]] bool IsShellWhitespace(const char character) noexcept {
    return character == OS_USER_SHELL_SPACE || character == OS_USER_SHELL_TAB;
}

[[nodiscard]] bool ShellArgumentEquals(const ShellCommandLine &command_line,
                                       const uint64_t argument_index, const char *const expected,
                                       const uint64_t expected_length_bytes) noexcept {
    if (expected == nullptr || argument_index >= command_line.argument_count ||
        command_line.arguments[argument_index].length_bytes != expected_length_bytes) {
        return false;
    }
    const ShellArgument &argument = command_line.arguments[argument_index];
    for (uint64_t byte_index = OS_USER_SHELL_EMPTY_VALUE; byte_index < expected_length_bytes;
         ++byte_index) {
        if (command_line.storage[argument.offset_bytes + byte_index] != expected[byte_index]) {
            return false;
        }
    }
    return true;
}
}

ShellParseStatus ParseShellCommandLine(const char *const line, const uint64_t line_length_bytes,
                                       ShellCommandLine &command_line) noexcept {
    command_line = ShellCommandLine{};
    if (line == nullptr && line_length_bytes != OS_USER_SHELL_EMPTY_VALUE) {
        return ShellParseStatus::InvalidArgument;
    }
    if (line_length_bytes > OS_USER_SHELL_MAXIMUM_LINE_SIZE_BYTES) {
        return ShellParseStatus::LineTooLong;
    }

    ShellCommandLine parsed_command_line{};
    uint64_t read_index = OS_USER_SHELL_EMPTY_VALUE;
    uint64_t write_index = OS_USER_SHELL_EMPTY_VALUE;
    while (read_index < line_length_bytes) {
        while (read_index < line_length_bytes && IsShellWhitespace(line[read_index])) {
            ++read_index;
        }
        if (read_index >= line_length_bytes) {
            break;
        }
        if (parsed_command_line.argument_count >= OS_USER_SHELL_MAXIMUM_ARGUMENT_COUNT) {
            return ShellParseStatus::TooManyArguments;
        }

        ShellArgument &argument = parsed_command_line.arguments[parsed_command_line.argument_count];
        argument.offset_bytes = write_index;
        ShellQuoteState quote_state = ShellQuoteState::None;
        bool escaping = false;
        bool argument_started = false;
        while (read_index < line_length_bytes) {
            const char character = line[read_index];
            if (escaping) {
                if (write_index >= OS_USER_SHELL_STORAGE_SIZE_BYTES) {
                    return ShellParseStatus::LineTooLong;
                }
                parsed_command_line.storage[write_index] = character;
                ++write_index;
                ++argument.length_bytes;
                ++read_index;
                escaping = false;
                argument_started = true;
                continue;
            }
            if (quote_state != ShellQuoteState::Single && character == OS_USER_SHELL_ESCAPE) {
                escaping = true;
                ++read_index;
                argument_started = true;
                continue;
            }
            if (quote_state == ShellQuoteState::None && character == OS_USER_SHELL_SINGLE_QUOTE) {
                quote_state = ShellQuoteState::Single;
                ++read_index;
                argument_started = true;
                continue;
            }
            if (quote_state == ShellQuoteState::Single && character == OS_USER_SHELL_SINGLE_QUOTE) {
                quote_state = ShellQuoteState::None;
                ++read_index;
                continue;
            }
            if (quote_state == ShellQuoteState::None && character == OS_USER_SHELL_DOUBLE_QUOTE) {
                quote_state = ShellQuoteState::Double;
                ++read_index;
                argument_started = true;
                continue;
            }
            if (quote_state == ShellQuoteState::Double && character == OS_USER_SHELL_DOUBLE_QUOTE) {
                quote_state = ShellQuoteState::None;
                ++read_index;
                continue;
            }
            if (quote_state == ShellQuoteState::None && IsShellWhitespace(character)) {
                break;
            }
            if (write_index >= OS_USER_SHELL_STORAGE_SIZE_BYTES) {
                return ShellParseStatus::LineTooLong;
            }
            parsed_command_line.storage[write_index] = character;
            ++write_index;
            ++argument.length_bytes;
            ++read_index;
            argument_started = true;
        }
        if (escaping) {
            return ShellParseStatus::DanglingEscape;
        }
        if (quote_state != ShellQuoteState::None) {
            return ShellParseStatus::UnterminatedQuote;
        }
        if (!argument_started) {
            return ShellParseStatus::InvalidArgument;
        }
        if (write_index >= OS_USER_SHELL_STORAGE_SIZE_BYTES) {
            return ShellParseStatus::LineTooLong;
        }
        parsed_command_line.storage[write_index] = OS_USER_SHELL_STRING_TERMINATOR;
        ++write_index;
        ++parsed_command_line.argument_count;
    }
    if (parsed_command_line.argument_count == OS_USER_SHELL_EMPTY_VALUE) {
        return ShellParseStatus::Empty;
    }
    command_line = parsed_command_line;
    return ShellParseStatus::Succeeded;
}

ShellCommand ResolveShellCommand(const ShellCommandLine &command_line) noexcept {
    if (ShellArgumentEquals(command_line, OS_USER_SHELL_COMMAND_ARGUMENT_INDEX,
                            OS_USER_SHELL_HELP_COMMAND,
                            sizeof(OS_USER_SHELL_HELP_COMMAND) - OS_USER_SHELL_COUNTER_INCREMENT)) {
        return ShellCommand::Help;
    }
    if (ShellArgumentEquals(command_line, OS_USER_SHELL_COMMAND_ARGUMENT_INDEX,
                            OS_USER_SHELL_ECHO_COMMAND,
                            sizeof(OS_USER_SHELL_ECHO_COMMAND) - OS_USER_SHELL_COUNTER_INCREMENT)) {
        return ShellCommand::Echo;
    }
    if (ShellArgumentEquals(command_line, OS_USER_SHELL_COMMAND_ARGUMENT_INDEX,
                            OS_USER_SHELL_PWD_COMMAND,
                            sizeof(OS_USER_SHELL_PWD_COMMAND) - OS_USER_SHELL_COUNTER_INCREMENT)) {
        return ShellCommand::PrintWorkingDirectory;
    }
    if (ShellArgumentEquals(command_line, OS_USER_SHELL_COMMAND_ARGUMENT_INDEX,
                            OS_USER_SHELL_CD_COMMAND,
                            sizeof(OS_USER_SHELL_CD_COMMAND) - OS_USER_SHELL_COUNTER_INCREMENT)) {
        return ShellCommand::ChangeDirectory;
    }
    if (ShellArgumentEquals(command_line, OS_USER_SHELL_COMMAND_ARGUMENT_INDEX,
                            OS_USER_SHELL_LS_COMMAND,
                            sizeof(OS_USER_SHELL_LS_COMMAND) - OS_USER_SHELL_COUNTER_INCREMENT)) {
        return ShellCommand::ListDirectory;
    }
    if (ShellArgumentEquals(
            command_line, OS_USER_SHELL_COMMAND_ARGUMENT_INDEX, OS_USER_SHELL_MKDIR_COMMAND,
            sizeof(OS_USER_SHELL_MKDIR_COMMAND) - OS_USER_SHELL_COUNTER_INCREMENT)) {
        return ShellCommand::CreateDirectory;
    }
    if (ShellArgumentEquals(
            command_line, OS_USER_SHELL_COMMAND_ARGUMENT_INDEX, OS_USER_SHELL_WRITE_COMMAND,
            sizeof(OS_USER_SHELL_WRITE_COMMAND) - OS_USER_SHELL_COUNTER_INCREMENT)) {
        return ShellCommand::WriteFile;
    }
    if (ShellArgumentEquals(command_line, OS_USER_SHELL_COMMAND_ARGUMENT_INDEX,
                            OS_USER_SHELL_CAT_COMMAND,
                            sizeof(OS_USER_SHELL_CAT_COMMAND) - OS_USER_SHELL_COUNTER_INCREMENT)) {
        return ShellCommand::ConcatenateFile;
    }
    if (ShellArgumentEquals(command_line, OS_USER_SHELL_COMMAND_ARGUMENT_INDEX,
                            OS_USER_SHELL_RM_COMMAND,
                            sizeof(OS_USER_SHELL_RM_COMMAND) - OS_USER_SHELL_COUNTER_INCREMENT)) {
        return ShellCommand::RemoveFile;
    }
    if (ShellArgumentEquals(
            command_line, OS_USER_SHELL_COMMAND_ARGUMENT_INDEX, OS_USER_SHELL_RMDIR_COMMAND,
            sizeof(OS_USER_SHELL_RMDIR_COMMAND) - OS_USER_SHELL_COUNTER_INCREMENT)) {
        return ShellCommand::RemoveDirectory;
    }
    if (ShellArgumentEquals(command_line, OS_USER_SHELL_COMMAND_ARGUMENT_INDEX,
                            OS_USER_SHELL_MV_COMMAND,
                            sizeof(OS_USER_SHELL_MV_COMMAND) - OS_USER_SHELL_COUNTER_INCREMENT)) {
        return ShellCommand::MovePath;
    }
    if (ShellArgumentEquals(
            command_line, OS_USER_SHELL_COMMAND_ARGUMENT_INDEX, OS_USER_SHELL_TRUNCATE_COMMAND,
            sizeof(OS_USER_SHELL_TRUNCATE_COMMAND) - OS_USER_SHELL_COUNTER_INCREMENT)) {
        return ShellCommand::TruncateFile;
    }
    if (ShellArgumentEquals(command_line, OS_USER_SHELL_COMMAND_ARGUMENT_INDEX,
                            OS_USER_SHELL_STAT_COMMAND,
                            sizeof(OS_USER_SHELL_STAT_COMMAND) - OS_USER_SHELL_COUNTER_INCREMENT)) {
        return ShellCommand::StatPath;
    }
    if (ShellArgumentEquals(command_line, OS_USER_SHELL_COMMAND_ARGUMENT_INDEX,
                            OS_USER_SHELL_SYNC_COMMAND,
                            sizeof(OS_USER_SHELL_SYNC_COMMAND) - OS_USER_SHELL_COUNTER_INCREMENT)) {
        return ShellCommand::Synchronize;
    }
    if (ShellArgumentEquals(command_line, OS_USER_SHELL_COMMAND_ARGUMENT_INDEX,
                            OS_USER_SHELL_EXIT_COMMAND,
                            sizeof(OS_USER_SHELL_EXIT_COMMAND) - OS_USER_SHELL_COUNTER_INCREMENT)) {
        return ShellCommand::Exit;
    }
    return ShellCommand::Unknown;
}

const char *ShellArgumentBytes(const ShellCommandLine &command_line,
                               const uint64_t argument_index) noexcept {
    if (argument_index >= command_line.argument_count) {
        return nullptr;
    }
    return command_line.storage + command_line.arguments[argument_index].offset_bytes;
}
}

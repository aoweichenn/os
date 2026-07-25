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
constexpr char OS_USER_SHELL_LS_COMMAND[] = "ls";
constexpr char OS_USER_SHELL_MKDIR_COMMAND[] = "mkdir";
constexpr char OS_USER_SHELL_WRITE_COMMAND[] = "write";
constexpr char OS_USER_SHELL_CAT_COMMAND[] = "cat";
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

[[nodiscard]] bool ShellArgumentEquals(const ShellCommandLine &commandLine,
                                       const uint64_t argumentIndex,
                                       const char *const expected,
                                       const uint64_t expectedLengthBytes) noexcept {
    if (expected == nullptr || argumentIndex >= commandLine.argumentCount ||
        commandLine.arguments[argumentIndex].lengthBytes !=
            expectedLengthBytes) {
        return false;
    }
    const ShellArgument &argument = commandLine.arguments[argumentIndex];
    for (uint64_t byteIndex = OS_USER_SHELL_EMPTY_VALUE;
         byteIndex < expectedLengthBytes; ++byteIndex) {
        if (commandLine.storage[argument.offsetBytes + byteIndex] !=
            expected[byteIndex]) {
            return false;
        }
    }
    return true;
}

}

ShellParseStatus ParseShellCommandLine(
    const char *const line, const uint64_t lineLengthBytes,
    ShellCommandLine &commandLine) noexcept {
    commandLine = ShellCommandLine{};
    if (line == nullptr &&
        lineLengthBytes != OS_USER_SHELL_EMPTY_VALUE) {
        return ShellParseStatus::InvalidArgument;
    }
    if (lineLengthBytes > OS_USER_SHELL_MAXIMUM_LINE_SIZE_BYTES) {
        return ShellParseStatus::LineTooLong;
    }

    ShellCommandLine parsedCommandLine{};
    uint64_t readIndex = OS_USER_SHELL_EMPTY_VALUE;
    uint64_t writeIndex = OS_USER_SHELL_EMPTY_VALUE;
    while (readIndex < lineLengthBytes) {
        while (readIndex < lineLengthBytes &&
               IsShellWhitespace(line[readIndex])) {
            ++readIndex;
        }
        if (readIndex >= lineLengthBytes) {
            break;
        }
        if (parsedCommandLine.argumentCount >=
            OS_USER_SHELL_MAXIMUM_ARGUMENT_COUNT) {
            return ShellParseStatus::TooManyArguments;
        }

        ShellArgument &argument =
            parsedCommandLine.arguments[
                parsedCommandLine.argumentCount];
        argument.offsetBytes = writeIndex;
        ShellQuoteState quoteState = ShellQuoteState::None;
        bool escaping = false;
        bool argumentStarted = false;
        while (readIndex < lineLengthBytes) {
            const char character = line[readIndex];
            if (escaping) {
                if (writeIndex >= OS_USER_SHELL_STORAGE_SIZE_BYTES) {
                    return ShellParseStatus::LineTooLong;
                }
                parsedCommandLine.storage[writeIndex] = character;
                ++writeIndex;
                ++argument.lengthBytes;
                ++readIndex;
                escaping = false;
                argumentStarted = true;
                continue;
            }
            if (quoteState != ShellQuoteState::Single &&
                character == OS_USER_SHELL_ESCAPE) {
                escaping = true;
                ++readIndex;
                argumentStarted = true;
                continue;
            }
            if (quoteState == ShellQuoteState::None &&
                character == OS_USER_SHELL_SINGLE_QUOTE) {
                quoteState = ShellQuoteState::Single;
                ++readIndex;
                argumentStarted = true;
                continue;
            }
            if (quoteState == ShellQuoteState::Single &&
                character == OS_USER_SHELL_SINGLE_QUOTE) {
                quoteState = ShellQuoteState::None;
                ++readIndex;
                continue;
            }
            if (quoteState == ShellQuoteState::None &&
                character == OS_USER_SHELL_DOUBLE_QUOTE) {
                quoteState = ShellQuoteState::Double;
                ++readIndex;
                argumentStarted = true;
                continue;
            }
            if (quoteState == ShellQuoteState::Double &&
                character == OS_USER_SHELL_DOUBLE_QUOTE) {
                quoteState = ShellQuoteState::None;
                ++readIndex;
                continue;
            }
            if (quoteState == ShellQuoteState::None &&
                IsShellWhitespace(character)) {
                break;
            }
            if (writeIndex >= OS_USER_SHELL_STORAGE_SIZE_BYTES) {
                return ShellParseStatus::LineTooLong;
            }
            parsedCommandLine.storage[writeIndex] = character;
            ++writeIndex;
            ++argument.lengthBytes;
            ++readIndex;
            argumentStarted = true;
        }
        if (escaping) {
            return ShellParseStatus::DanglingEscape;
        }
        if (quoteState != ShellQuoteState::None) {
            return ShellParseStatus::UnterminatedQuote;
        }
        if (!argumentStarted) {
            return ShellParseStatus::InvalidArgument;
        }
        if (writeIndex >= OS_USER_SHELL_STORAGE_SIZE_BYTES) {
            return ShellParseStatus::LineTooLong;
        }
        parsedCommandLine.storage[writeIndex] =
            OS_USER_SHELL_STRING_TERMINATOR;
        ++writeIndex;
        ++parsedCommandLine.argumentCount;
    }
    if (parsedCommandLine.argumentCount ==
        OS_USER_SHELL_EMPTY_VALUE) {
        return ShellParseStatus::Empty;
    }
    commandLine = parsedCommandLine;
    return ShellParseStatus::Succeeded;
}

ShellCommand ResolveShellCommand(
    const ShellCommandLine &commandLine) noexcept {
    if (ShellArgumentEquals(
            commandLine, OS_USER_SHELL_COMMAND_ARGUMENT_INDEX,
            OS_USER_SHELL_HELP_COMMAND,
            sizeof(OS_USER_SHELL_HELP_COMMAND) -
                OS_USER_SHELL_COUNTER_INCREMENT)) {
        return ShellCommand::Help;
    }
    if (ShellArgumentEquals(
            commandLine, OS_USER_SHELL_COMMAND_ARGUMENT_INDEX,
            OS_USER_SHELL_ECHO_COMMAND,
            sizeof(OS_USER_SHELL_ECHO_COMMAND) -
                OS_USER_SHELL_COUNTER_INCREMENT)) {
        return ShellCommand::Echo;
    }
    if (ShellArgumentEquals(
            commandLine, OS_USER_SHELL_COMMAND_ARGUMENT_INDEX,
            OS_USER_SHELL_PWD_COMMAND,
            sizeof(OS_USER_SHELL_PWD_COMMAND) -
                OS_USER_SHELL_COUNTER_INCREMENT)) {
        return ShellCommand::PrintWorkingDirectory;
    }
    if (ShellArgumentEquals(
            commandLine, OS_USER_SHELL_COMMAND_ARGUMENT_INDEX,
            OS_USER_SHELL_LS_COMMAND,
            sizeof(OS_USER_SHELL_LS_COMMAND) -
                OS_USER_SHELL_COUNTER_INCREMENT)) {
        return ShellCommand::ListDirectory;
    }
    if (ShellArgumentEquals(
            commandLine, OS_USER_SHELL_COMMAND_ARGUMENT_INDEX,
            OS_USER_SHELL_MKDIR_COMMAND,
            sizeof(OS_USER_SHELL_MKDIR_COMMAND) -
                OS_USER_SHELL_COUNTER_INCREMENT)) {
        return ShellCommand::CreateDirectory;
    }
    if (ShellArgumentEquals(
            commandLine, OS_USER_SHELL_COMMAND_ARGUMENT_INDEX,
            OS_USER_SHELL_WRITE_COMMAND,
            sizeof(OS_USER_SHELL_WRITE_COMMAND) -
                OS_USER_SHELL_COUNTER_INCREMENT)) {
        return ShellCommand::WriteFile;
    }
    if (ShellArgumentEquals(
            commandLine, OS_USER_SHELL_COMMAND_ARGUMENT_INDEX,
            OS_USER_SHELL_CAT_COMMAND,
            sizeof(OS_USER_SHELL_CAT_COMMAND) -
                OS_USER_SHELL_COUNTER_INCREMENT)) {
        return ShellCommand::ConcatenateFile;
    }
    if (ShellArgumentEquals(
            commandLine, OS_USER_SHELL_COMMAND_ARGUMENT_INDEX,
            OS_USER_SHELL_SYNC_COMMAND,
            sizeof(OS_USER_SHELL_SYNC_COMMAND) -
                OS_USER_SHELL_COUNTER_INCREMENT)) {
        return ShellCommand::Synchronize;
    }
    if (ShellArgumentEquals(
            commandLine, OS_USER_SHELL_COMMAND_ARGUMENT_INDEX,
            OS_USER_SHELL_EXIT_COMMAND,
            sizeof(OS_USER_SHELL_EXIT_COMMAND) -
                OS_USER_SHELL_COUNTER_INCREMENT)) {
        return ShellCommand::Exit;
    }
    return ShellCommand::Unknown;
}

const char *ShellArgumentBytes(const ShellCommandLine &commandLine,
                               const uint64_t argumentIndex) noexcept {
    if (argumentIndex >= commandLine.argumentCount) {
        return nullptr;
    }
    return commandLine.storage +
           commandLine.arguments[argumentIndex].offsetBytes;
}

}

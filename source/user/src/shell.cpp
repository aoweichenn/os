#include "os/user/shell.hpp"

#include "os/abi/system_call.hpp"
#include "os/user/shell_parser.hpp"
#include "os/user/system_call.hpp"

namespace os::user {

namespace {

constexpr uint64_t OS_USER_SHELL_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_USER_SHELL_FIRST_PARAMETER_INDEX = 1ULL;
constexpr uint64_t OS_USER_SHELL_SECOND_PARAMETER_INDEX = 2ULL;
constexpr uint64_t OS_USER_SHELL_HELP_ARGUMENT_COUNT = 1ULL;
constexpr uint64_t OS_USER_SHELL_PWD_ARGUMENT_COUNT = 1ULL;
constexpr uint64_t OS_USER_SHELL_SYNC_ARGUMENT_COUNT = 1ULL;
constexpr uint64_t OS_USER_SHELL_EXIT_ARGUMENT_COUNT = 1ULL;
constexpr uint64_t OS_USER_SHELL_PATH_COMMAND_ARGUMENT_COUNT = 2ULL;
constexpr uint64_t OS_USER_SHELL_WRITE_MINIMUM_ARGUMENT_COUNT = 3ULL;
constexpr uint64_t OS_USER_SHELL_LIST_MAXIMUM_ARGUMENT_COUNT = 2ULL;
constexpr uint64_t OS_USER_SHELL_SINGLE_CHARACTER_SIZE_BYTES = 1ULL;
constexpr uint64_t OS_USER_SHELL_FILE_TRANSFER_SIZE_BYTES = 128ULL;
constexpr uint64_t OS_USER_SHELL_WRITE_BUFFER_SIZE_BYTES = 256ULL;
constexpr uint64_t OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES = 1ULL;
constexpr uint8_t OS_USER_SHELL_NEWLINE_CHARACTER = static_cast<uint8_t>('\n');
constexpr uint8_t OS_USER_SHELL_BACKSPACE_CHARACTER = static_cast<uint8_t>('\b');
constexpr uint8_t OS_USER_SHELL_TAB_CHARACTER = static_cast<uint8_t>('\t');
constexpr uint8_t OS_USER_SHELL_SPACE_CHARACTER = static_cast<uint8_t>(' ');
constexpr uint8_t OS_USER_SHELL_STRING_TERMINATOR_CHARACTER = static_cast<uint8_t>('\0');
constexpr uint8_t OS_USER_SHELL_FIRST_PRINTABLE_CHARACTER = 0x20U;
constexpr uint8_t OS_USER_SHELL_LAST_PRINTABLE_CHARACTER = 0x7EU;
constexpr int64_t OS_USER_SHELL_SUCCESS_EXIT_CODE = 0LL;
constexpr int64_t OS_USER_SHELL_FAILURE_EXIT_CODE = 1LL;
constexpr int64_t OS_USER_SHELL_SUCCESS_RESULT = 0LL;
constexpr int64_t OS_USER_SHELL_DIRECTORY_ENTRY_RESULT = 1LL;
constexpr uint64_t OS_USER_SHELL_WRITE_OPEN_FLAGS = os::abi::OS_ABI_FILE_OPEN_WRITE_FLAG |
                                                    os::abi::OS_ABI_FILE_OPEN_CREATE_FLAG |
                                                    os::abi::OS_ABI_FILE_OPEN_TRUNCATE_FLAG;
constexpr char OS_USER_SHELL_BANNER[] = "\r\nx86-64 OS Lab v1.3\r\n"
                                        "输入 help 查看可用命令。\r\n";
constexpr char OS_USER_SHELL_READY_MARKER[] = "[OS][USER][SHELL] READY\r\n";
constexpr char OS_USER_SHELL_PROMPT[] = "[os:/]$ ";
constexpr char OS_USER_SHELL_NEWLINE[] = "\r\n";
constexpr char OS_USER_SHELL_BACKSPACE_SEQUENCE[] = "\b \b";
constexpr char OS_USER_SHELL_SPACE[] = " ";
constexpr char OS_USER_SHELL_DIRECTORY_SUFFIX[] = "/";
constexpr char OS_USER_SHELL_ROOT_PATH[] = "/";
constexpr char OS_USER_SHELL_HELP_TEXT[] = "help                 显示命令帮助\r\n"
                                           "echo [text...]       输出文本\r\n"
                                           "pwd                  显示当前目录\r\n"
                                           "ls [path]            列出目录\r\n"
                                           "mkdir <path>         创建目录\r\n"
                                           "write <path> <text>  创建或覆盖文件\r\n"
                                           "cat <path>           输出文件\r\n"
                                           "sync                 持久化文件系统\r\n"
                                           "exit                 退出 Shell\r\n";
constexpr char OS_USER_SHELL_USAGE_ERROR[] = "error: 参数数量不正确\r\n";
constexpr char OS_USER_SHELL_PARSE_ERROR[] = "error: 无法解析命令行\r\n";
constexpr char OS_USER_SHELL_LINE_TOO_LONG_ERROR[] = "error: 命令行超过 128 字节\r\n";
constexpr char OS_USER_SHELL_OPERATION_ERROR[] = "error: 操作失败\r\n";
constexpr char OS_USER_SHELL_UNKNOWN_ERROR[] = "error: 未知命令\r\n";
constexpr char OS_USER_SHELL_DIRECTORY_EXISTS[] = "目录已经存在\r\n";
constexpr char OS_USER_SHELL_OPERATION_SUCCEEDED[] = "ok\r\n";
constexpr char OS_USER_SHELL_COMMAND_HELP_MARKER[] = "[OS][USER][SHELL] COMMAND=HELP\r\n";
constexpr char OS_USER_SHELL_COMMAND_ECHO_MARKER[] = "[OS][USER][SHELL] COMMAND=ECHO\r\n";
constexpr char OS_USER_SHELL_COMMAND_PWD_MARKER[] = "[OS][USER][SHELL] COMMAND=PWD\r\n";
constexpr char OS_USER_SHELL_COMMAND_LS_MARKER[] = "[OS][USER][SHELL] COMMAND=LS\r\n";
constexpr char OS_USER_SHELL_COMMAND_MKDIR_MARKER[] = "[OS][USER][SHELL] COMMAND=MKDIR\r\n";
constexpr char OS_USER_SHELL_COMMAND_WRITE_MARKER[] = "[OS][USER][SHELL] COMMAND=WRITE\r\n";
constexpr char OS_USER_SHELL_COMMAND_CAT_MARKER[] = "[OS][USER][SHELL] COMMAND=CAT\r\n";
constexpr char OS_USER_SHELL_COMMAND_SYNC_MARKER[] = "[OS][USER][SHELL] COMMAND=SYNC\r\n";
constexpr char OS_USER_SHELL_COMMAND_EXIT_MARKER[] = "[OS][USER][SHELL] COMMAND=EXIT\r\n";
constexpr char OS_USER_SHELL_UNKNOWN_COMMAND_MARKER[] =
    "[OS][USER][SHELL] UNKNOWN_COMMAND_REJECTED\r\n";
constexpr char OS_USER_SHELL_EXIT_MARKER[] = "[OS][USER][SHELL] EXIT\r\n";

enum class ShellReadLineStatus : uint64_t {
    Succeeded,
    TooLong,
    IoFailure,
};

enum class ShellExecutionAction : uint64_t {
    Continue,
    Exit,
    Fatal,
};

template <uint64_t MessageSizeBytes>
[[nodiscard]] bool WriteLiteral(const char (&message)[MessageSizeBytes]) noexcept {
    return WriteDescriptor(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR,
                           reinterpret_cast<const uint8_t *>(message),
                           MessageSizeBytes - OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES) >=
           OS_USER_SHELL_SUCCESS_RESULT;
}

[[nodiscard]] bool WriteBytes(const char *const bytes, const uint64_t length_bytes) noexcept {
    return WriteDescriptor(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR,
                           reinterpret_cast<const uint8_t *>(bytes),
                           length_bytes) == static_cast<int64_t>(length_bytes);
}

[[nodiscard]] bool WriteArgument(const ShellCommandLine &command_line,
                                 const uint64_t argument_index) noexcept {
    const char *const bytes = ShellArgumentBytes(command_line, argument_index);
    return bytes != nullptr &&
           WriteBytes(bytes, command_line.arguments[argument_index].length_bytes);
}

[[nodiscard]] bool WriteOperationResult(const bool succeeded) noexcept {
    return succeeded ? WriteLiteral(OS_USER_SHELL_OPERATION_SUCCEEDED)
                     : WriteLiteral(OS_USER_SHELL_OPERATION_ERROR);
}

[[nodiscard]] ShellReadLineStatus ReadShellLine(char *const line,
                                                uint64_t &line_length_bytes) noexcept {
    line_length_bytes = OS_USER_SHELL_EMPTY_VALUE;
    bool line_too_long = false;
    while (true) {
        uint8_t character = OS_USER_SHELL_STRING_TERMINATOR_CHARACTER;
        const int64_t read_result =
            ReadDescriptor(os::abi::OS_ABI_STANDARD_INPUT_DESCRIPTOR, &character,
                           OS_USER_SHELL_SINGLE_CHARACTER_SIZE_BYTES);
        if (read_result != static_cast<int64_t>(OS_USER_SHELL_SINGLE_CHARACTER_SIZE_BYTES)) {
            return ShellReadLineStatus::IoFailure;
        }
        if (character == OS_USER_SHELL_NEWLINE_CHARACTER) {
            if (!WriteLiteral(OS_USER_SHELL_NEWLINE)) {
                return ShellReadLineStatus::IoFailure;
            }
            line[line_length_bytes] = static_cast<char>(OS_USER_SHELL_STRING_TERMINATOR_CHARACTER);
            return line_too_long ? ShellReadLineStatus::TooLong : ShellReadLineStatus::Succeeded;
        }
        if (character == OS_USER_SHELL_BACKSPACE_CHARACTER) {
            if (!line_too_long && line_length_bytes != OS_USER_SHELL_EMPTY_VALUE) {
                --line_length_bytes;
                if (!WriteLiteral(OS_USER_SHELL_BACKSPACE_SEQUENCE)) {
                    return ShellReadLineStatus::IoFailure;
                }
            }
            continue;
        }
        const bool printable = (character >= OS_USER_SHELL_FIRST_PRINTABLE_CHARACTER &&
                                character <= OS_USER_SHELL_LAST_PRINTABLE_CHARACTER) ||
                               character == OS_USER_SHELL_TAB_CHARACTER;
        if (!printable) {
            continue;
        }
        if (line_length_bytes >= OS_USER_SHELL_MAXIMUM_LINE_SIZE_BYTES) {
            line_too_long = true;
            continue;
        }
        line[line_length_bytes] = static_cast<char>(character);
        ++line_length_bytes;
        if (!WriteBytes(reinterpret_cast<const char *>(&character),
                        OS_USER_SHELL_SINGLE_CHARACTER_SIZE_BYTES)) {
            return ShellReadLineStatus::IoFailure;
        }
    }
}

[[nodiscard]] bool WriteArguments(const ShellCommandLine &command_line,
                                  const uint64_t first_argument_index,
                                  const bool append_newline) noexcept {
    for (uint64_t argument_index = first_argument_index;
         argument_index < command_line.argument_count; ++argument_index) {
        if (argument_index != first_argument_index && !WriteLiteral(OS_USER_SHELL_SPACE)) {
            return false;
        }
        if (!WriteArgument(command_line, argument_index)) {
            return false;
        }
    }
    return !append_newline || WriteLiteral(OS_USER_SHELL_NEWLINE);
}

[[nodiscard]] ShellExecutionAction ExecuteHelp(const ShellCommandLine &command_line) noexcept {
    if (command_line.argument_count != OS_USER_SHELL_HELP_ARGUMENT_COUNT) {
        return WriteLiteral(OS_USER_SHELL_USAGE_ERROR) ? ShellExecutionAction::Continue
                                                       : ShellExecutionAction::Fatal;
    }
    return WriteLiteral(OS_USER_SHELL_COMMAND_HELP_MARKER) && WriteLiteral(OS_USER_SHELL_HELP_TEXT)
               ? ShellExecutionAction::Continue
               : ShellExecutionAction::Fatal;
}

[[nodiscard]] ShellExecutionAction ExecuteEcho(const ShellCommandLine &command_line) noexcept {
    return WriteLiteral(OS_USER_SHELL_COMMAND_ECHO_MARKER) &&
                   WriteArguments(command_line, OS_USER_SHELL_FIRST_PARAMETER_INDEX, true)
               ? ShellExecutionAction::Continue
               : ShellExecutionAction::Fatal;
}

[[nodiscard]] ShellExecutionAction ExecutePwd(const ShellCommandLine &command_line) noexcept {
    if (command_line.argument_count != OS_USER_SHELL_PWD_ARGUMENT_COUNT) {
        return WriteLiteral(OS_USER_SHELL_USAGE_ERROR) ? ShellExecutionAction::Continue
                                                       : ShellExecutionAction::Fatal;
    }
    return WriteLiteral(OS_USER_SHELL_COMMAND_PWD_MARKER) &&
                   WriteLiteral(OS_USER_SHELL_ROOT_PATH) && WriteLiteral(OS_USER_SHELL_NEWLINE)
               ? ShellExecutionAction::Continue
               : ShellExecutionAction::Fatal;
}

[[nodiscard]] ShellExecutionAction ExecuteMkdir(const ShellCommandLine &command_line) noexcept {
    if (command_line.argument_count != OS_USER_SHELL_PATH_COMMAND_ARGUMENT_COUNT) {
        return WriteLiteral(OS_USER_SHELL_USAGE_ERROR) ? ShellExecutionAction::Continue
                                                       : ShellExecutionAction::Fatal;
    }
    if (!WriteLiteral(OS_USER_SHELL_COMMAND_MKDIR_MARKER)) {
        return ShellExecutionAction::Fatal;
    }
    const int64_t result =
        CreateDirectory(ShellArgumentBytes(command_line, OS_USER_SHELL_FIRST_PARAMETER_INDEX),
                        command_line.arguments[OS_USER_SHELL_FIRST_PARAMETER_INDEX].length_bytes);
    if (result == os::abi::OS_ABI_SYSTEM_CALL_RESULT_FILE_ALREADY_EXISTS) {
        return WriteLiteral(OS_USER_SHELL_DIRECTORY_EXISTS) ? ShellExecutionAction::Continue
                                                            : ShellExecutionAction::Fatal;
    }
    return WriteOperationResult(result == OS_USER_SHELL_SUCCESS_RESULT)
               ? ShellExecutionAction::Continue
               : ShellExecutionAction::Fatal;
}

[[nodiscard]] bool BuildWritePayload(const ShellCommandLine &command_line, uint8_t *const payload,
                                     uint64_t &payload_length_bytes) noexcept {
    payload_length_bytes = OS_USER_SHELL_EMPTY_VALUE;
    for (uint64_t argument_index = OS_USER_SHELL_SECOND_PARAMETER_INDEX;
         argument_index < command_line.argument_count; ++argument_index) {
        if (argument_index != OS_USER_SHELL_SECOND_PARAMETER_INDEX) {
            if (payload_length_bytes >= OS_USER_SHELL_WRITE_BUFFER_SIZE_BYTES) {
                return false;
            }
            payload[payload_length_bytes] = OS_USER_SHELL_SPACE_CHARACTER;
            ++payload_length_bytes;
        }
        const ShellArgument &argument = command_line.arguments[argument_index];
        if (argument.length_bytes > OS_USER_SHELL_WRITE_BUFFER_SIZE_BYTES - payload_length_bytes) {
            return false;
        }
        const char *const argument_bytes = ShellArgumentBytes(command_line, argument_index);
        if (argument_bytes == nullptr) {
            return false;
        }
        for (uint64_t byte_index = OS_USER_SHELL_EMPTY_VALUE; byte_index < argument.length_bytes;
             ++byte_index) {
            payload[payload_length_bytes] = static_cast<uint8_t>(argument_bytes[byte_index]);
            ++payload_length_bytes;
        }
    }
    return true;
}

[[nodiscard]] ShellExecutionAction ExecuteWrite(const ShellCommandLine &command_line) noexcept {
    if (command_line.argument_count < OS_USER_SHELL_WRITE_MINIMUM_ARGUMENT_COUNT) {
        return WriteLiteral(OS_USER_SHELL_USAGE_ERROR) ? ShellExecutionAction::Continue
                                                       : ShellExecutionAction::Fatal;
    }
    if (!WriteLiteral(OS_USER_SHELL_COMMAND_WRITE_MARKER)) {
        return ShellExecutionAction::Fatal;
    }
    uint8_t payload[OS_USER_SHELL_WRITE_BUFFER_SIZE_BYTES]{};
    uint64_t payload_length_bytes = OS_USER_SHELL_EMPTY_VALUE;
    if (!BuildWritePayload(command_line, payload, payload_length_bytes)) {
        return WriteLiteral(OS_USER_SHELL_LINE_TOO_LONG_ERROR) ? ShellExecutionAction::Continue
                                                               : ShellExecutionAction::Fatal;
    }
    const int64_t descriptor =
        OpenFile(ShellArgumentBytes(command_line, OS_USER_SHELL_FIRST_PARAMETER_INDEX),
                 command_line.arguments[OS_USER_SHELL_FIRST_PARAMETER_INDEX].length_bytes,
                 OS_USER_SHELL_WRITE_OPEN_FLAGS);
    if (descriptor < OS_USER_SHELL_SUCCESS_RESULT) {
        return WriteLiteral(OS_USER_SHELL_OPERATION_ERROR) ? ShellExecutionAction::Continue
                                                           : ShellExecutionAction::Fatal;
    }
    const int64_t write_result =
        WriteDescriptor(static_cast<uint64_t>(descriptor), payload, payload_length_bytes);
    const int64_t close_result = CloseDescriptor(static_cast<uint64_t>(descriptor));
    return WriteOperationResult(write_result == static_cast<int64_t>(payload_length_bytes) &&
                                close_result == OS_USER_SHELL_SUCCESS_RESULT)
               ? ShellExecutionAction::Continue
               : ShellExecutionAction::Fatal;
}

[[nodiscard]] ShellExecutionAction ExecuteCat(const ShellCommandLine &command_line) noexcept {
    if (command_line.argument_count != OS_USER_SHELL_PATH_COMMAND_ARGUMENT_COUNT) {
        return WriteLiteral(OS_USER_SHELL_USAGE_ERROR) ? ShellExecutionAction::Continue
                                                       : ShellExecutionAction::Fatal;
    }
    if (!WriteLiteral(OS_USER_SHELL_COMMAND_CAT_MARKER)) {
        return ShellExecutionAction::Fatal;
    }
    const int64_t descriptor =
        OpenFile(ShellArgumentBytes(command_line, OS_USER_SHELL_FIRST_PARAMETER_INDEX),
                 command_line.arguments[OS_USER_SHELL_FIRST_PARAMETER_INDEX].length_bytes,
                 os::abi::OS_ABI_FILE_OPEN_READ_FLAG);
    if (descriptor < OS_USER_SHELL_SUCCESS_RESULT) {
        return WriteLiteral(OS_USER_SHELL_OPERATION_ERROR) ? ShellExecutionAction::Continue
                                                           : ShellExecutionAction::Fatal;
    }
    uint8_t file_bytes[OS_USER_SHELL_FILE_TRANSFER_SIZE_BYTES]{};
    bool succeeded = true;
    while (succeeded) {
        const int64_t read_result = ReadDescriptor(static_cast<uint64_t>(descriptor), file_bytes,
                                                   OS_USER_SHELL_FILE_TRANSFER_SIZE_BYTES);
        if (read_result < OS_USER_SHELL_SUCCESS_RESULT) {
            succeeded = false;
            break;
        }
        if (read_result == OS_USER_SHELL_SUCCESS_RESULT) {
            break;
        }
        succeeded = WriteBytes(reinterpret_cast<const char *>(file_bytes),
                               static_cast<uint64_t>(read_result));
    }
    succeeded =
        CloseDescriptor(static_cast<uint64_t>(descriptor)) == OS_USER_SHELL_SUCCESS_RESULT &&
        succeeded && WriteLiteral(OS_USER_SHELL_NEWLINE);
    if (!succeeded && !WriteLiteral(OS_USER_SHELL_OPERATION_ERROR)) {
        return ShellExecutionAction::Fatal;
    }
    return ShellExecutionAction::Continue;
}

[[nodiscard]] ShellExecutionAction ExecuteList(const ShellCommandLine &command_line) noexcept {
    if (command_line.argument_count > OS_USER_SHELL_LIST_MAXIMUM_ARGUMENT_COUNT) {
        return WriteLiteral(OS_USER_SHELL_USAGE_ERROR) ? ShellExecutionAction::Continue
                                                       : ShellExecutionAction::Fatal;
    }
    if (!WriteLiteral(OS_USER_SHELL_COMMAND_LS_MARKER)) {
        return ShellExecutionAction::Fatal;
    }
    const char *path = OS_USER_SHELL_ROOT_PATH;
    uint64_t path_length_bytes =
        sizeof(OS_USER_SHELL_ROOT_PATH) - OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES;
    if (command_line.argument_count == OS_USER_SHELL_LIST_MAXIMUM_ARGUMENT_COUNT) {
        path = ShellArgumentBytes(command_line, OS_USER_SHELL_FIRST_PARAMETER_INDEX);
        path_length_bytes =
            command_line.arguments[OS_USER_SHELL_FIRST_PARAMETER_INDEX].length_bytes;
    }
    const int64_t descriptor = OpenDirectory(path, path_length_bytes);
    if (descriptor < OS_USER_SHELL_SUCCESS_RESULT) {
        return WriteLiteral(OS_USER_SHELL_OPERATION_ERROR) ? ShellExecutionAction::Continue
                                                           : ShellExecutionAction::Fatal;
    }
    bool succeeded = true;
    while (succeeded) {
        os::abi::DirectoryEntry entry{};
        const int64_t read_result = ReadDirectory(static_cast<uint64_t>(descriptor), entry);
        if (read_result < OS_USER_SHELL_SUCCESS_RESULT) {
            succeeded = false;
            break;
        }
        if (read_result == OS_USER_SHELL_SUCCESS_RESULT) {
            break;
        }
        succeeded = read_result == OS_USER_SHELL_DIRECTORY_ENTRY_RESULT &&
                    WriteBytes(reinterpret_cast<const char *>(entry.name), entry.name_length_bytes);
        if (succeeded && entry.type == os::abi::DirectoryEntryType::Directory) {
            succeeded = WriteLiteral(OS_USER_SHELL_DIRECTORY_SUFFIX);
        }
        succeeded = succeeded && WriteLiteral(OS_USER_SHELL_NEWLINE);
    }
    succeeded =
        CloseDescriptor(static_cast<uint64_t>(descriptor)) == OS_USER_SHELL_SUCCESS_RESULT &&
        succeeded;
    if (!succeeded && !WriteLiteral(OS_USER_SHELL_OPERATION_ERROR)) {
        return ShellExecutionAction::Fatal;
    }
    return ShellExecutionAction::Continue;
}

[[nodiscard]] ShellExecutionAction ExecuteSync(const ShellCommandLine &command_line) noexcept {
    if (command_line.argument_count != OS_USER_SHELL_SYNC_ARGUMENT_COUNT) {
        return WriteLiteral(OS_USER_SHELL_USAGE_ERROR) ? ShellExecutionAction::Continue
                                                       : ShellExecutionAction::Fatal;
    }
    if (!WriteLiteral(OS_USER_SHELL_COMMAND_SYNC_MARKER)) {
        return ShellExecutionAction::Fatal;
    }
    return WriteOperationResult(SyncFileSystem() == OS_USER_SHELL_SUCCESS_RESULT)
               ? ShellExecutionAction::Continue
               : ShellExecutionAction::Fatal;
}

[[nodiscard]] ShellExecutionAction ExecuteCommand(const ShellCommandLine &command_line) noexcept {
    switch (ResolveShellCommand(command_line)) {
    case ShellCommand::Help:
        return ExecuteHelp(command_line);
    case ShellCommand::Echo:
        return ExecuteEcho(command_line);
    case ShellCommand::PrintWorkingDirectory:
        return ExecutePwd(command_line);
    case ShellCommand::ListDirectory:
        return ExecuteList(command_line);
    case ShellCommand::CreateDirectory:
        return ExecuteMkdir(command_line);
    case ShellCommand::WriteFile:
        return ExecuteWrite(command_line);
    case ShellCommand::ConcatenateFile:
        return ExecuteCat(command_line);
    case ShellCommand::Synchronize:
        return ExecuteSync(command_line);
    case ShellCommand::Exit:
        if (command_line.argument_count != OS_USER_SHELL_EXIT_ARGUMENT_COUNT) {
            return WriteLiteral(OS_USER_SHELL_USAGE_ERROR) ? ShellExecutionAction::Continue
                                                           : ShellExecutionAction::Fatal;
        }
        return WriteLiteral(OS_USER_SHELL_COMMAND_EXIT_MARKER) &&
                       WriteLiteral(OS_USER_SHELL_EXIT_MARKER)
                   ? ShellExecutionAction::Exit
                   : ShellExecutionAction::Fatal;
    case ShellCommand::Unknown:
        return WriteLiteral(OS_USER_SHELL_UNKNOWN_COMMAND_MARKER) &&
                       WriteLiteral(OS_USER_SHELL_UNKNOWN_ERROR)
                   ? ShellExecutionAction::Continue
                   : ShellExecutionAction::Fatal;
    }
    return ShellExecutionAction::Fatal;
}
}

int64_t RunShell() noexcept {
    if (!WriteLiteral(OS_USER_SHELL_BANNER) || !WriteLiteral(OS_USER_SHELL_READY_MARKER)) {
        return OS_USER_SHELL_FAILURE_EXIT_CODE;
    }
    while (true) {
        if (!WriteLiteral(OS_USER_SHELL_PROMPT)) {
            return OS_USER_SHELL_FAILURE_EXIT_CODE;
        }
        char line[OS_USER_SHELL_MAXIMUM_LINE_SIZE_BYTES +
                  OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES]{};
        uint64_t line_length_bytes = OS_USER_SHELL_EMPTY_VALUE;
        const ShellReadLineStatus read_status = ReadShellLine(line, line_length_bytes);
        if (read_status == ShellReadLineStatus::IoFailure) {
            return OS_USER_SHELL_FAILURE_EXIT_CODE;
        }
        if (read_status == ShellReadLineStatus::TooLong) {
            if (!WriteLiteral(OS_USER_SHELL_LINE_TOO_LONG_ERROR)) {
                return OS_USER_SHELL_FAILURE_EXIT_CODE;
            }
            continue;
        }
        ShellCommandLine command_line{};
        const ShellParseStatus parse_status =
            ParseShellCommandLine(line, line_length_bytes, command_line);
        if (parse_status == ShellParseStatus::Empty) {
            continue;
        }
        if (parse_status != ShellParseStatus::Succeeded) {
            if (!WriteLiteral(OS_USER_SHELL_PARSE_ERROR)) {
                return OS_USER_SHELL_FAILURE_EXIT_CODE;
            }
            continue;
        }
        const ShellExecutionAction action = ExecuteCommand(command_line);
        if (action == ShellExecutionAction::Exit) {
            return OS_USER_SHELL_SUCCESS_EXIT_CODE;
        }
        if (action == ShellExecutionAction::Fatal) {
            return OS_USER_SHELL_FAILURE_EXIT_CODE;
        }
    }
}

}

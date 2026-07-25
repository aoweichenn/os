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
constexpr uint8_t OS_USER_SHELL_STRING_TERMINATOR_CHARACTER =
    static_cast<uint8_t>('\0');
constexpr uint8_t OS_USER_SHELL_FIRST_PRINTABLE_CHARACTER = 0x20U;
constexpr uint8_t OS_USER_SHELL_LAST_PRINTABLE_CHARACTER = 0x7EU;
constexpr int64_t OS_USER_SHELL_SUCCESS_EXIT_CODE = 0LL;
constexpr int64_t OS_USER_SHELL_FAILURE_EXIT_CODE = 1LL;
constexpr int64_t OS_USER_SHELL_SUCCESS_RESULT = 0LL;
constexpr int64_t OS_USER_SHELL_DIRECTORY_ENTRY_RESULT = 1LL;
constexpr uint64_t OS_USER_SHELL_WRITE_OPEN_FLAGS =
    os::abi::OS_ABI_FILE_OPEN_WRITE_FLAG |
    os::abi::OS_ABI_FILE_OPEN_CREATE_FLAG |
    os::abi::OS_ABI_FILE_OPEN_TRUNCATE_FLAG;
constexpr char OS_USER_SHELL_BANNER[] =
    "\r\nx86-64 OS Lab v1.0\r\n"
    "输入 help 查看可用命令。\r\n";
constexpr char OS_USER_SHELL_READY_MARKER[] =
    "[OS][USER][SHELL] READY\r\n";
constexpr char OS_USER_SHELL_PROMPT[] = "[os:/]$ ";
constexpr char OS_USER_SHELL_NEWLINE[] = "\r\n";
constexpr char OS_USER_SHELL_BACKSPACE_SEQUENCE[] = "\b \b";
constexpr char OS_USER_SHELL_SPACE[] = " ";
constexpr char OS_USER_SHELL_DIRECTORY_SUFFIX[] = "/";
constexpr char OS_USER_SHELL_ROOT_PATH[] = "/";
constexpr char OS_USER_SHELL_HELP_TEXT[] =
    "help                 显示命令帮助\r\n"
    "echo [text...]       输出文本\r\n"
    "pwd                  显示当前目录\r\n"
    "ls [path]            列出目录\r\n"
    "mkdir <path>         创建目录\r\n"
    "write <path> <text>  创建或覆盖文件\r\n"
    "cat <path>           输出文件\r\n"
    "sync                 持久化文件系统\r\n"
    "exit                 退出 Shell\r\n";
constexpr char OS_USER_SHELL_USAGE_ERROR[] =
    "error: 参数数量不正确\r\n";
constexpr char OS_USER_SHELL_PARSE_ERROR[] =
    "error: 无法解析命令行\r\n";
constexpr char OS_USER_SHELL_LINE_TOO_LONG_ERROR[] =
    "error: 命令行超过 128 字节\r\n";
constexpr char OS_USER_SHELL_OPERATION_ERROR[] =
    "error: 操作失败\r\n";
constexpr char OS_USER_SHELL_UNKNOWN_ERROR[] =
    "error: 未知命令\r\n";
constexpr char OS_USER_SHELL_DIRECTORY_EXISTS[] =
    "目录已经存在\r\n";
constexpr char OS_USER_SHELL_OPERATION_SUCCEEDED[] = "ok\r\n";
constexpr char OS_USER_SHELL_COMMAND_HELP_MARKER[] =
    "[OS][USER][SHELL] COMMAND=HELP\r\n";
constexpr char OS_USER_SHELL_COMMAND_ECHO_MARKER[] =
    "[OS][USER][SHELL] COMMAND=ECHO\r\n";
constexpr char OS_USER_SHELL_COMMAND_PWD_MARKER[] =
    "[OS][USER][SHELL] COMMAND=PWD\r\n";
constexpr char OS_USER_SHELL_COMMAND_LS_MARKER[] =
    "[OS][USER][SHELL] COMMAND=LS\r\n";
constexpr char OS_USER_SHELL_COMMAND_MKDIR_MARKER[] =
    "[OS][USER][SHELL] COMMAND=MKDIR\r\n";
constexpr char OS_USER_SHELL_COMMAND_WRITE_MARKER[] =
    "[OS][USER][SHELL] COMMAND=WRITE\r\n";
constexpr char OS_USER_SHELL_COMMAND_CAT_MARKER[] =
    "[OS][USER][SHELL] COMMAND=CAT\r\n";
constexpr char OS_USER_SHELL_COMMAND_SYNC_MARKER[] =
    "[OS][USER][SHELL] COMMAND=SYNC\r\n";
constexpr char OS_USER_SHELL_COMMAND_EXIT_MARKER[] =
    "[OS][USER][SHELL] COMMAND=EXIT\r\n";
constexpr char OS_USER_SHELL_UNKNOWN_COMMAND_MARKER[] =
    "[OS][USER][SHELL] UNKNOWN_COMMAND_REJECTED\r\n";
constexpr char OS_USER_SHELL_EXIT_MARKER[] =
    "[OS][USER][SHELL] EXIT\r\n";

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
[[nodiscard]] bool WriteLiteral(
    const char (&message)[MessageSizeBytes]) noexcept {
    return WriteDescriptor(
               os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR,
               reinterpret_cast<const uint8_t *>(message),
               MessageSizeBytes -
                   OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES) >=
           OS_USER_SHELL_SUCCESS_RESULT;
}

[[nodiscard]] bool WriteBytes(const char *const bytes,
                              const uint64_t lengthBytes) noexcept {
    return WriteDescriptor(
               os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR,
               reinterpret_cast<const uint8_t *>(bytes), lengthBytes) ==
           static_cast<int64_t>(lengthBytes);
}

[[nodiscard]] bool WriteArgument(
    const ShellCommandLine &commandLine,
    const uint64_t argumentIndex) noexcept {
    const char *const bytes =
        ShellArgumentBytes(commandLine, argumentIndex);
    return bytes != nullptr &&
           WriteBytes(
               bytes,
               commandLine.arguments[argumentIndex].lengthBytes);
}

[[nodiscard]] bool WriteOperationResult(const bool succeeded) noexcept {
    return succeeded ? WriteLiteral(OS_USER_SHELL_OPERATION_SUCCEEDED)
                     : WriteLiteral(OS_USER_SHELL_OPERATION_ERROR);
}

[[nodiscard]] ShellReadLineStatus ReadShellLine(
    char *const line, uint64_t &lineLengthBytes) noexcept {
    lineLengthBytes = OS_USER_SHELL_EMPTY_VALUE;
    bool lineTooLong = false;
    while (true) {
        uint8_t character = OS_USER_SHELL_STRING_TERMINATOR_CHARACTER;
        const int64_t readResult = ReadDescriptor(
            os::abi::OS_ABI_STANDARD_INPUT_DESCRIPTOR, &character,
            OS_USER_SHELL_SINGLE_CHARACTER_SIZE_BYTES);
        if (readResult !=
            static_cast<int64_t>(
                OS_USER_SHELL_SINGLE_CHARACTER_SIZE_BYTES)) {
            return ShellReadLineStatus::IoFailure;
        }
        if (character == OS_USER_SHELL_NEWLINE_CHARACTER) {
            if (!WriteLiteral(OS_USER_SHELL_NEWLINE)) {
                return ShellReadLineStatus::IoFailure;
            }
            line[lineLengthBytes] =
                static_cast<char>(
                    OS_USER_SHELL_STRING_TERMINATOR_CHARACTER);
            return lineTooLong ? ShellReadLineStatus::TooLong
                               : ShellReadLineStatus::Succeeded;
        }
        if (character == OS_USER_SHELL_BACKSPACE_CHARACTER) {
            if (!lineTooLong &&
                lineLengthBytes != OS_USER_SHELL_EMPTY_VALUE) {
                --lineLengthBytes;
                if (!WriteLiteral(
                        OS_USER_SHELL_BACKSPACE_SEQUENCE)) {
                    return ShellReadLineStatus::IoFailure;
                }
            }
            continue;
        }
        const bool printable =
            (character >= OS_USER_SHELL_FIRST_PRINTABLE_CHARACTER &&
             character <= OS_USER_SHELL_LAST_PRINTABLE_CHARACTER) ||
            character == OS_USER_SHELL_TAB_CHARACTER;
        if (!printable) {
            continue;
        }
        if (lineLengthBytes >=
            OS_USER_SHELL_MAXIMUM_LINE_SIZE_BYTES) {
            lineTooLong = true;
            continue;
        }
        line[lineLengthBytes] = static_cast<char>(character);
        ++lineLengthBytes;
        if (!WriteBytes(reinterpret_cast<const char *>(&character),
                        OS_USER_SHELL_SINGLE_CHARACTER_SIZE_BYTES)) {
            return ShellReadLineStatus::IoFailure;
        }
    }
}

[[nodiscard]] bool WriteArguments(
    const ShellCommandLine &commandLine,
    const uint64_t firstArgumentIndex,
    const bool appendNewline) noexcept {
    for (uint64_t argumentIndex = firstArgumentIndex;
         argumentIndex < commandLine.argumentCount; ++argumentIndex) {
        if (argumentIndex != firstArgumentIndex &&
            !WriteLiteral(OS_USER_SHELL_SPACE)) {
            return false;
        }
        if (!WriteArgument(commandLine, argumentIndex)) {
            return false;
        }
    }
    return !appendNewline || WriteLiteral(OS_USER_SHELL_NEWLINE);
}

[[nodiscard]] ShellExecutionAction ExecuteHelp(
    const ShellCommandLine &commandLine) noexcept {
    if (commandLine.argumentCount != OS_USER_SHELL_HELP_ARGUMENT_COUNT) {
        return WriteLiteral(OS_USER_SHELL_USAGE_ERROR)
                   ? ShellExecutionAction::Continue
                   : ShellExecutionAction::Fatal;
    }
    return WriteLiteral(OS_USER_SHELL_COMMAND_HELP_MARKER) &&
                   WriteLiteral(OS_USER_SHELL_HELP_TEXT)
               ? ShellExecutionAction::Continue
               : ShellExecutionAction::Fatal;
}

[[nodiscard]] ShellExecutionAction ExecuteEcho(
    const ShellCommandLine &commandLine) noexcept {
    return WriteLiteral(OS_USER_SHELL_COMMAND_ECHO_MARKER) &&
                   WriteArguments(
                       commandLine,
                       OS_USER_SHELL_FIRST_PARAMETER_INDEX, true)
               ? ShellExecutionAction::Continue
               : ShellExecutionAction::Fatal;
}

[[nodiscard]] ShellExecutionAction ExecutePwd(
    const ShellCommandLine &commandLine) noexcept {
    if (commandLine.argumentCount != OS_USER_SHELL_PWD_ARGUMENT_COUNT) {
        return WriteLiteral(OS_USER_SHELL_USAGE_ERROR)
                   ? ShellExecutionAction::Continue
                   : ShellExecutionAction::Fatal;
    }
    return WriteLiteral(OS_USER_SHELL_COMMAND_PWD_MARKER) &&
                   WriteLiteral(OS_USER_SHELL_ROOT_PATH) &&
                   WriteLiteral(OS_USER_SHELL_NEWLINE)
               ? ShellExecutionAction::Continue
               : ShellExecutionAction::Fatal;
}

[[nodiscard]] ShellExecutionAction ExecuteMkdir(
    const ShellCommandLine &commandLine) noexcept {
    if (commandLine.argumentCount !=
        OS_USER_SHELL_PATH_COMMAND_ARGUMENT_COUNT) {
        return WriteLiteral(OS_USER_SHELL_USAGE_ERROR)
                   ? ShellExecutionAction::Continue
                   : ShellExecutionAction::Fatal;
    }
    if (!WriteLiteral(OS_USER_SHELL_COMMAND_MKDIR_MARKER)) {
        return ShellExecutionAction::Fatal;
    }
    const int64_t result = CreateDirectory(
        ShellArgumentBytes(
            commandLine, OS_USER_SHELL_FIRST_PARAMETER_INDEX),
        commandLine.arguments[OS_USER_SHELL_FIRST_PARAMETER_INDEX]
            .lengthBytes);
    if (result ==
        os::abi::OS_ABI_SYSTEM_CALL_RESULT_FILE_ALREADY_EXISTS) {
        return WriteLiteral(OS_USER_SHELL_DIRECTORY_EXISTS)
                   ? ShellExecutionAction::Continue
                   : ShellExecutionAction::Fatal;
    }
    return WriteOperationResult(
               result == OS_USER_SHELL_SUCCESS_RESULT)
               ? ShellExecutionAction::Continue
               : ShellExecutionAction::Fatal;
}

[[nodiscard]] bool BuildWritePayload(
    const ShellCommandLine &commandLine, uint8_t *const payload,
    uint64_t &payloadLengthBytes) noexcept {
    payloadLengthBytes = OS_USER_SHELL_EMPTY_VALUE;
    for (uint64_t argumentIndex =
             OS_USER_SHELL_SECOND_PARAMETER_INDEX;
         argumentIndex < commandLine.argumentCount; ++argumentIndex) {
        if (argumentIndex != OS_USER_SHELL_SECOND_PARAMETER_INDEX) {
            if (payloadLengthBytes >=
                OS_USER_SHELL_WRITE_BUFFER_SIZE_BYTES) {
                return false;
            }
            payload[payloadLengthBytes] =
                OS_USER_SHELL_SPACE_CHARACTER;
            ++payloadLengthBytes;
        }
        const ShellArgument &argument =
            commandLine.arguments[argumentIndex];
        if (argument.lengthBytes >
            OS_USER_SHELL_WRITE_BUFFER_SIZE_BYTES -
                payloadLengthBytes) {
            return false;
        }
        const char *const argumentBytes =
            ShellArgumentBytes(commandLine, argumentIndex);
        if (argumentBytes == nullptr) {
            return false;
        }
        for (uint64_t byteIndex = OS_USER_SHELL_EMPTY_VALUE;
             byteIndex < argument.lengthBytes; ++byteIndex) {
            payload[payloadLengthBytes] =
                static_cast<uint8_t>(argumentBytes[byteIndex]);
            ++payloadLengthBytes;
        }
    }
    return true;
}

[[nodiscard]] ShellExecutionAction ExecuteWrite(
    const ShellCommandLine &commandLine) noexcept {
    if (commandLine.argumentCount <
        OS_USER_SHELL_WRITE_MINIMUM_ARGUMENT_COUNT) {
        return WriteLiteral(OS_USER_SHELL_USAGE_ERROR)
                   ? ShellExecutionAction::Continue
                   : ShellExecutionAction::Fatal;
    }
    if (!WriteLiteral(OS_USER_SHELL_COMMAND_WRITE_MARKER)) {
        return ShellExecutionAction::Fatal;
    }
    uint8_t payload[OS_USER_SHELL_WRITE_BUFFER_SIZE_BYTES]{};
    uint64_t payloadLengthBytes = OS_USER_SHELL_EMPTY_VALUE;
    if (!BuildWritePayload(commandLine, payload,
                           payloadLengthBytes)) {
        return WriteLiteral(OS_USER_SHELL_LINE_TOO_LONG_ERROR)
                   ? ShellExecutionAction::Continue
                   : ShellExecutionAction::Fatal;
    }
    const int64_t descriptor = OpenFile(
        ShellArgumentBytes(
            commandLine, OS_USER_SHELL_FIRST_PARAMETER_INDEX),
        commandLine.arguments[OS_USER_SHELL_FIRST_PARAMETER_INDEX]
            .lengthBytes,
        OS_USER_SHELL_WRITE_OPEN_FLAGS);
    if (descriptor < OS_USER_SHELL_SUCCESS_RESULT) {
        return WriteLiteral(OS_USER_SHELL_OPERATION_ERROR)
                   ? ShellExecutionAction::Continue
                   : ShellExecutionAction::Fatal;
    }
    const int64_t writeResult = WriteDescriptor(
        static_cast<uint64_t>(descriptor), payload,
        payloadLengthBytes);
    const int64_t closeResult =
        CloseDescriptor(static_cast<uint64_t>(descriptor));
    return WriteOperationResult(
               writeResult ==
                       static_cast<int64_t>(payloadLengthBytes) &&
                   closeResult == OS_USER_SHELL_SUCCESS_RESULT)
               ? ShellExecutionAction::Continue
               : ShellExecutionAction::Fatal;
}

[[nodiscard]] ShellExecutionAction ExecuteCat(
    const ShellCommandLine &commandLine) noexcept {
    if (commandLine.argumentCount !=
        OS_USER_SHELL_PATH_COMMAND_ARGUMENT_COUNT) {
        return WriteLiteral(OS_USER_SHELL_USAGE_ERROR)
                   ? ShellExecutionAction::Continue
                   : ShellExecutionAction::Fatal;
    }
    if (!WriteLiteral(OS_USER_SHELL_COMMAND_CAT_MARKER)) {
        return ShellExecutionAction::Fatal;
    }
    const int64_t descriptor = OpenFile(
        ShellArgumentBytes(
            commandLine, OS_USER_SHELL_FIRST_PARAMETER_INDEX),
        commandLine.arguments[OS_USER_SHELL_FIRST_PARAMETER_INDEX]
            .lengthBytes,
        os::abi::OS_ABI_FILE_OPEN_READ_FLAG);
    if (descriptor < OS_USER_SHELL_SUCCESS_RESULT) {
        return WriteLiteral(OS_USER_SHELL_OPERATION_ERROR)
                   ? ShellExecutionAction::Continue
                   : ShellExecutionAction::Fatal;
    }
    uint8_t fileBytes[OS_USER_SHELL_FILE_TRANSFER_SIZE_BYTES]{};
    bool succeeded = true;
    while (succeeded) {
        const int64_t readResult = ReadDescriptor(
            static_cast<uint64_t>(descriptor), fileBytes,
            OS_USER_SHELL_FILE_TRANSFER_SIZE_BYTES);
        if (readResult < OS_USER_SHELL_SUCCESS_RESULT) {
            succeeded = false;
            break;
        }
        if (readResult == OS_USER_SHELL_SUCCESS_RESULT) {
            break;
        }
        succeeded = WriteBytes(
            reinterpret_cast<const char *>(fileBytes),
            static_cast<uint64_t>(readResult));
    }
    succeeded =
        CloseDescriptor(static_cast<uint64_t>(descriptor)) ==
            OS_USER_SHELL_SUCCESS_RESULT &&
        succeeded && WriteLiteral(OS_USER_SHELL_NEWLINE);
    if (!succeeded &&
        !WriteLiteral(OS_USER_SHELL_OPERATION_ERROR)) {
        return ShellExecutionAction::Fatal;
    }
    return ShellExecutionAction::Continue;
}

[[nodiscard]] ShellExecutionAction ExecuteList(
    const ShellCommandLine &commandLine) noexcept {
    if (commandLine.argumentCount >
        OS_USER_SHELL_LIST_MAXIMUM_ARGUMENT_COUNT) {
        return WriteLiteral(OS_USER_SHELL_USAGE_ERROR)
                   ? ShellExecutionAction::Continue
                   : ShellExecutionAction::Fatal;
    }
    if (!WriteLiteral(OS_USER_SHELL_COMMAND_LS_MARKER)) {
        return ShellExecutionAction::Fatal;
    }
    const char *path = OS_USER_SHELL_ROOT_PATH;
    uint64_t pathLengthBytes =
        sizeof(OS_USER_SHELL_ROOT_PATH) -
        OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES;
    if (commandLine.argumentCount ==
        OS_USER_SHELL_LIST_MAXIMUM_ARGUMENT_COUNT) {
        path = ShellArgumentBytes(
            commandLine, OS_USER_SHELL_FIRST_PARAMETER_INDEX);
        pathLengthBytes =
            commandLine.arguments[OS_USER_SHELL_FIRST_PARAMETER_INDEX]
                .lengthBytes;
    }
    const int64_t descriptor =
        OpenDirectory(path, pathLengthBytes);
    if (descriptor < OS_USER_SHELL_SUCCESS_RESULT) {
        return WriteLiteral(OS_USER_SHELL_OPERATION_ERROR)
                   ? ShellExecutionAction::Continue
                   : ShellExecutionAction::Fatal;
    }
    bool succeeded = true;
    while (succeeded) {
        os::abi::DirectoryEntry entry{};
        const int64_t readResult = ReadDirectory(
            static_cast<uint64_t>(descriptor), entry);
        if (readResult < OS_USER_SHELL_SUCCESS_RESULT) {
            succeeded = false;
            break;
        }
        if (readResult == OS_USER_SHELL_SUCCESS_RESULT) {
            break;
        }
        succeeded =
            readResult == OS_USER_SHELL_DIRECTORY_ENTRY_RESULT &&
            WriteBytes(reinterpret_cast<const char *>(entry.name),
                       entry.nameLengthBytes);
        if (succeeded &&
            entry.type == os::abi::DirectoryEntryType::Directory) {
            succeeded =
                WriteLiteral(OS_USER_SHELL_DIRECTORY_SUFFIX);
        }
        succeeded =
            succeeded && WriteLiteral(OS_USER_SHELL_NEWLINE);
    }
    succeeded =
        CloseDescriptor(static_cast<uint64_t>(descriptor)) ==
            OS_USER_SHELL_SUCCESS_RESULT &&
        succeeded;
    if (!succeeded &&
        !WriteLiteral(OS_USER_SHELL_OPERATION_ERROR)) {
        return ShellExecutionAction::Fatal;
    }
    return ShellExecutionAction::Continue;
}

[[nodiscard]] ShellExecutionAction ExecuteSync(
    const ShellCommandLine &commandLine) noexcept {
    if (commandLine.argumentCount != OS_USER_SHELL_SYNC_ARGUMENT_COUNT) {
        return WriteLiteral(OS_USER_SHELL_USAGE_ERROR)
                   ? ShellExecutionAction::Continue
                   : ShellExecutionAction::Fatal;
    }
    if (!WriteLiteral(OS_USER_SHELL_COMMAND_SYNC_MARKER)) {
        return ShellExecutionAction::Fatal;
    }
    return WriteOperationResult(
               SyncFileSystem() == OS_USER_SHELL_SUCCESS_RESULT)
               ? ShellExecutionAction::Continue
               : ShellExecutionAction::Fatal;
}

[[nodiscard]] ShellExecutionAction ExecuteCommand(
    const ShellCommandLine &commandLine) noexcept {
    switch (ResolveShellCommand(commandLine)) {
    case ShellCommand::Help:
        return ExecuteHelp(commandLine);
    case ShellCommand::Echo:
        return ExecuteEcho(commandLine);
    case ShellCommand::PrintWorkingDirectory:
        return ExecutePwd(commandLine);
    case ShellCommand::ListDirectory:
        return ExecuteList(commandLine);
    case ShellCommand::CreateDirectory:
        return ExecuteMkdir(commandLine);
    case ShellCommand::WriteFile:
        return ExecuteWrite(commandLine);
    case ShellCommand::ConcatenateFile:
        return ExecuteCat(commandLine);
    case ShellCommand::Synchronize:
        return ExecuteSync(commandLine);
    case ShellCommand::Exit:
        if (commandLine.argumentCount !=
            OS_USER_SHELL_EXIT_ARGUMENT_COUNT) {
            return WriteLiteral(OS_USER_SHELL_USAGE_ERROR)
                       ? ShellExecutionAction::Continue
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
    if (!WriteLiteral(OS_USER_SHELL_BANNER) ||
        !WriteLiteral(OS_USER_SHELL_READY_MARKER)) {
        return OS_USER_SHELL_FAILURE_EXIT_CODE;
    }
    while (true) {
        if (!WriteLiteral(OS_USER_SHELL_PROMPT)) {
            return OS_USER_SHELL_FAILURE_EXIT_CODE;
        }
        char line[OS_USER_SHELL_MAXIMUM_LINE_SIZE_BYTES +
                  OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES]{};
        uint64_t lineLengthBytes = OS_USER_SHELL_EMPTY_VALUE;
        const ShellReadLineStatus readStatus =
            ReadShellLine(line, lineLengthBytes);
        if (readStatus == ShellReadLineStatus::IoFailure) {
            return OS_USER_SHELL_FAILURE_EXIT_CODE;
        }
        if (readStatus == ShellReadLineStatus::TooLong) {
            if (!WriteLiteral(OS_USER_SHELL_LINE_TOO_LONG_ERROR)) {
                return OS_USER_SHELL_FAILURE_EXIT_CODE;
            }
            continue;
        }
        ShellCommandLine commandLine{};
        const ShellParseStatus parseStatus =
            ParseShellCommandLine(line, lineLengthBytes, commandLine);
        if (parseStatus == ShellParseStatus::Empty) {
            continue;
        }
        if (parseStatus != ShellParseStatus::Succeeded) {
            if (!WriteLiteral(OS_USER_SHELL_PARSE_ERROR)) {
                return OS_USER_SHELL_FAILURE_EXIT_CODE;
            }
            continue;
        }
        const ShellExecutionAction action =
            ExecuteCommand(commandLine);
        if (action == ShellExecutionAction::Exit) {
            return OS_USER_SHELL_SUCCESS_EXIT_CODE;
        }
        if (action == ShellExecutionAction::Fatal) {
            return OS_USER_SHELL_FAILURE_EXIT_CODE;
        }
    }
}

}

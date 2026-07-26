#include "os/user/shell_parser.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_SHELL_PARSER_SUITE_NAME = "user/shell_parser/unit";
constexpr std::string_view OS_TEST_SHELL_PARSER_SIMPLE = "简单命令必须切分命令名和参数";
constexpr std::string_view OS_TEST_SHELL_PARSER_QUOTES = "引号和转义必须生成单个去引号参数";
constexpr std::string_view OS_TEST_SHELL_PARSER_CHANGE_DIRECTORY =
    "cd 命令必须解析为独立的目录切换动作";
constexpr std::string_view OS_TEST_SHELL_PARSER_NAMESPACE_MUTATIONS =
    "rm、rmdir、mv、truncate 和 stat 必须解析为各自的命名空间动作";
constexpr std::string_view OS_TEST_SHELL_PARSER_EMPTY = "空白输入必须返回 Empty";
constexpr std::string_view OS_TEST_SHELL_PARSER_FAILURES =
    "未闭合引号、悬空转义和参数过多必须稳定拒绝且不留下半命令";
constexpr std::string_view OS_TEST_SHELL_PARSER_BOUNDARIES =
    "精确行长、精确参数容量、空参数和非法边界必须具有稳定结果";
constexpr char OS_TEST_SHELL_PARSER_SIMPLE_LINE[] = "write /demo/message hello";
constexpr char OS_TEST_SHELL_PARSER_QUOTED_LINE[] =
    "echo \"hello world\" 'from shell' escaped\\ value";
constexpr char OS_TEST_SHELL_PARSER_CHANGE_DIRECTORY_LINE[] = "cd ../tmp";
constexpr char OS_TEST_SHELL_PARSER_REMOVE_FILE_LINE[] = "rm /demo/file";
constexpr char OS_TEST_SHELL_PARSER_REMOVE_DIRECTORY_LINE[] = "rmdir /demo";
constexpr char OS_TEST_SHELL_PARSER_MOVE_PATH_LINE[] = "mv /old /new";
constexpr char OS_TEST_SHELL_PARSER_TRUNCATE_FILE_LINE[] = "truncate /demo/file 64";
constexpr char OS_TEST_SHELL_PARSER_STAT_PATH_LINE[] = "stat /demo/file";
constexpr char OS_TEST_SHELL_PARSER_EMPTY_LINE[] = " \t ";
constexpr char OS_TEST_SHELL_PARSER_UNTERMINATED_LINE[] = "echo \"unfinished";
constexpr char OS_TEST_SHELL_PARSER_DANGLING_ESCAPE_LINE[] = "echo unfinished\\";
constexpr char OS_TEST_SHELL_PARSER_TOO_MANY_ARGUMENTS_LINE[] = "echo 1 2 3 4 5 6 7 8";
constexpr char OS_TEST_SHELL_PARSER_MAXIMUM_ARGUMENTS_LINE[] = "a b c d e f g h";
constexpr char OS_TEST_SHELL_PARSER_EMPTY_ARGUMENTS_LINE[] = "echo \"\" ''";
constexpr char OS_TEST_SHELL_PARSER_EXPECTED_PATH[] = "/demo/message";
constexpr char OS_TEST_SHELL_PARSER_EXPECTED_TEXT[] = "hello";
constexpr char OS_TEST_SHELL_PARSER_EXPECTED_QUOTED_TEXT[] = "hello world";
constexpr char OS_TEST_SHELL_PARSER_EXPECTED_SINGLE_QUOTED_TEXT[] = "from shell";
constexpr char OS_TEST_SHELL_PARSER_EXPECTED_ESCAPED_TEXT[] = "escaped value";
constexpr uint64_t OS_TEST_SHELL_PARSER_COMMAND_INDEX = 0ULL;
constexpr uint64_t OS_TEST_SHELL_PARSER_FIRST_ARGUMENT_INDEX = 1ULL;
constexpr uint64_t OS_TEST_SHELL_PARSER_SECOND_ARGUMENT_INDEX = 2ULL;
constexpr uint64_t OS_TEST_SHELL_PARSER_THIRD_ARGUMENT_INDEX = 3ULL;
constexpr uint64_t OS_TEST_SHELL_PARSER_SIMPLE_ARGUMENT_COUNT = 3ULL;
constexpr uint64_t OS_TEST_SHELL_PARSER_QUOTED_ARGUMENT_COUNT = 4ULL;
constexpr uint64_t OS_TEST_SHELL_PARSER_SINGLE_ARGUMENT_COUNT = 1ULL;
constexpr uint64_t OS_TEST_SHELL_PARSER_MAXIMUM_ARGUMENT_COUNT = 8ULL;
constexpr uint64_t OS_TEST_SHELL_PARSER_EMPTY_ARGUMENT_COUNT = 3ULL;
constexpr uint64_t OS_TEST_SHELL_PARSER_EMPTY_ARGUMENT_LENGTH_BYTES = 0ULL;
constexpr uint64_t OS_TEST_SHELL_PARSER_STRING_TERMINATOR_SIZE_BYTES = 1ULL;
constexpr uint64_t OS_TEST_SHELL_PARSER_INVALID_NULL_INPUT_LENGTH_BYTES = 1ULL;
constexpr char OS_TEST_SHELL_PARSER_MAXIMUM_LINE_CHARACTER = 'a';
constexpr char OS_TEST_SHELL_PARSER_STRING_TERMINATOR = '\0';

[[nodiscard]] bool ArgumentEquals(const os::user::ShellCommandLine &command_line,
                                  const uint64_t argument_index, const char *const expected,
                                  const uint64_t expected_length_bytes) noexcept {
    if (argument_index >= command_line.argument_count ||
        command_line.arguments[argument_index].length_bytes != expected_length_bytes) {
        return false;
    }
    const char *const actual = os::user::ShellArgumentBytes(command_line, argument_index);
    if (actual == nullptr) {
        return false;
    }
    for (uint64_t byte_index = 0ULL; byte_index < expected_length_bytes; ++byte_index) {
        if (actual[byte_index] != expected[byte_index]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool ResolvesTo(const char *const line, const uint64_t line_length_bytes,
                              const os::user::ShellCommand expected) noexcept {
    os::user::ShellCommandLine command_line{};
    return os::user::ParseShellCommandLine(line, line_length_bytes, command_line) ==
               os::user::ShellParseStatus::Succeeded &&
           os::user::ResolveShellCommand(command_line) == expected;
}
}

int main() {
    os::test::TestContext test_context{OS_TEST_SHELL_PARSER_SUITE_NAME};
    os::user::ShellCommandLine command_line{};

    test_context.Expect(
        os::user::ParseShellCommandLine(OS_TEST_SHELL_PARSER_SIMPLE_LINE,
                                        sizeof(OS_TEST_SHELL_PARSER_SIMPLE_LINE) -
                                            OS_TEST_SHELL_PARSER_STRING_TERMINATOR_SIZE_BYTES,
                                        command_line) == os::user::ShellParseStatus::Succeeded &&
            command_line.argument_count == OS_TEST_SHELL_PARSER_SIMPLE_ARGUMENT_COUNT &&
            os::user::ResolveShellCommand(command_line) == os::user::ShellCommand::WriteFile &&
            ArgumentEquals(command_line, OS_TEST_SHELL_PARSER_FIRST_ARGUMENT_INDEX,
                           OS_TEST_SHELL_PARSER_EXPECTED_PATH,
                           sizeof(OS_TEST_SHELL_PARSER_EXPECTED_PATH) -
                               OS_TEST_SHELL_PARSER_STRING_TERMINATOR_SIZE_BYTES) &&
            ArgumentEquals(command_line, OS_TEST_SHELL_PARSER_SECOND_ARGUMENT_INDEX,
                           OS_TEST_SHELL_PARSER_EXPECTED_TEXT,
                           sizeof(OS_TEST_SHELL_PARSER_EXPECTED_TEXT) -
                               OS_TEST_SHELL_PARSER_STRING_TERMINATOR_SIZE_BYTES),
        OS_TEST_SHELL_PARSER_SIMPLE);

    test_context.Expect(
        os::user::ParseShellCommandLine(OS_TEST_SHELL_PARSER_QUOTED_LINE,
                                        sizeof(OS_TEST_SHELL_PARSER_QUOTED_LINE) -
                                            OS_TEST_SHELL_PARSER_STRING_TERMINATOR_SIZE_BYTES,
                                        command_line) == os::user::ShellParseStatus::Succeeded &&
            command_line.argument_count == OS_TEST_SHELL_PARSER_QUOTED_ARGUMENT_COUNT &&
            os::user::ResolveShellCommand(command_line) == os::user::ShellCommand::Echo &&
            ArgumentEquals(command_line, OS_TEST_SHELL_PARSER_FIRST_ARGUMENT_INDEX,
                           OS_TEST_SHELL_PARSER_EXPECTED_QUOTED_TEXT,
                           sizeof(OS_TEST_SHELL_PARSER_EXPECTED_QUOTED_TEXT) -
                               OS_TEST_SHELL_PARSER_STRING_TERMINATOR_SIZE_BYTES) &&
            ArgumentEquals(command_line, OS_TEST_SHELL_PARSER_SECOND_ARGUMENT_INDEX,
                           OS_TEST_SHELL_PARSER_EXPECTED_SINGLE_QUOTED_TEXT,
                           sizeof(OS_TEST_SHELL_PARSER_EXPECTED_SINGLE_QUOTED_TEXT) -
                               OS_TEST_SHELL_PARSER_STRING_TERMINATOR_SIZE_BYTES) &&
            ArgumentEquals(command_line, OS_TEST_SHELL_PARSER_THIRD_ARGUMENT_INDEX,
                           OS_TEST_SHELL_PARSER_EXPECTED_ESCAPED_TEXT,
                           sizeof(OS_TEST_SHELL_PARSER_EXPECTED_ESCAPED_TEXT) -
                               OS_TEST_SHELL_PARSER_STRING_TERMINATOR_SIZE_BYTES),
        OS_TEST_SHELL_PARSER_QUOTES);

    test_context.Expect(
        os::user::ParseShellCommandLine(OS_TEST_SHELL_PARSER_CHANGE_DIRECTORY_LINE,
                                        sizeof(OS_TEST_SHELL_PARSER_CHANGE_DIRECTORY_LINE) -
                                            OS_TEST_SHELL_PARSER_STRING_TERMINATOR_SIZE_BYTES,
                                        command_line) == os::user::ShellParseStatus::Succeeded &&
            command_line.argument_count == OS_TEST_SHELL_PARSER_SECOND_ARGUMENT_INDEX &&
            os::user::ResolveShellCommand(command_line) == os::user::ShellCommand::ChangeDirectory,
        OS_TEST_SHELL_PARSER_CHANGE_DIRECTORY);

    test_context.Expect(ResolvesTo(OS_TEST_SHELL_PARSER_REMOVE_FILE_LINE,
                                   sizeof(OS_TEST_SHELL_PARSER_REMOVE_FILE_LINE) -
                                       OS_TEST_SHELL_PARSER_STRING_TERMINATOR_SIZE_BYTES,
                                   os::user::ShellCommand::RemoveFile) &&
                            ResolvesTo(OS_TEST_SHELL_PARSER_REMOVE_DIRECTORY_LINE,
                                       sizeof(OS_TEST_SHELL_PARSER_REMOVE_DIRECTORY_LINE) -
                                           OS_TEST_SHELL_PARSER_STRING_TERMINATOR_SIZE_BYTES,
                                       os::user::ShellCommand::RemoveDirectory) &&
                            ResolvesTo(OS_TEST_SHELL_PARSER_MOVE_PATH_LINE,
                                       sizeof(OS_TEST_SHELL_PARSER_MOVE_PATH_LINE) -
                                           OS_TEST_SHELL_PARSER_STRING_TERMINATOR_SIZE_BYTES,
                                       os::user::ShellCommand::MovePath) &&
                            ResolvesTo(OS_TEST_SHELL_PARSER_TRUNCATE_FILE_LINE,
                                       sizeof(OS_TEST_SHELL_PARSER_TRUNCATE_FILE_LINE) -
                                           OS_TEST_SHELL_PARSER_STRING_TERMINATOR_SIZE_BYTES,
                                       os::user::ShellCommand::TruncateFile) &&
                            ResolvesTo(OS_TEST_SHELL_PARSER_STAT_PATH_LINE,
                                       sizeof(OS_TEST_SHELL_PARSER_STAT_PATH_LINE) -
                                           OS_TEST_SHELL_PARSER_STRING_TERMINATOR_SIZE_BYTES,
                                       os::user::ShellCommand::StatPath),
                        OS_TEST_SHELL_PARSER_NAMESPACE_MUTATIONS);

    test_context.Expect(
        os::user::ParseShellCommandLine(OS_TEST_SHELL_PARSER_EMPTY_LINE,
                                        sizeof(OS_TEST_SHELL_PARSER_EMPTY_LINE) -
                                            OS_TEST_SHELL_PARSER_STRING_TERMINATOR_SIZE_BYTES,
                                        command_line) == os::user::ShellParseStatus::Empty,
        OS_TEST_SHELL_PARSER_EMPTY);

    const bool unterminated_quote_rejected =
        os::user::ParseShellCommandLine(OS_TEST_SHELL_PARSER_UNTERMINATED_LINE,
                                        sizeof(OS_TEST_SHELL_PARSER_UNTERMINATED_LINE) -
                                            OS_TEST_SHELL_PARSER_STRING_TERMINATOR_SIZE_BYTES,
                                        command_line) ==
            os::user::ShellParseStatus::UnterminatedQuote &&
        command_line.argument_count == OS_TEST_SHELL_PARSER_COMMAND_INDEX;
    const bool dangling_escape_rejected =
        os::user::ParseShellCommandLine(OS_TEST_SHELL_PARSER_DANGLING_ESCAPE_LINE,
                                        sizeof(OS_TEST_SHELL_PARSER_DANGLING_ESCAPE_LINE) -
                                            OS_TEST_SHELL_PARSER_STRING_TERMINATOR_SIZE_BYTES,
                                        command_line) ==
            os::user::ShellParseStatus::DanglingEscape &&
        command_line.argument_count == OS_TEST_SHELL_PARSER_COMMAND_INDEX;
    const bool too_many_arguments_rejected =
        os::user::ParseShellCommandLine(OS_TEST_SHELL_PARSER_TOO_MANY_ARGUMENTS_LINE,
                                        sizeof(OS_TEST_SHELL_PARSER_TOO_MANY_ARGUMENTS_LINE) -
                                            OS_TEST_SHELL_PARSER_STRING_TERMINATOR_SIZE_BYTES,
                                        command_line) ==
            os::user::ShellParseStatus::TooManyArguments &&
        command_line.argument_count == OS_TEST_SHELL_PARSER_COMMAND_INDEX;
    test_context.Expect(unterminated_quote_rejected && dangling_escape_rejected &&
                            too_many_arguments_rejected,
                        OS_TEST_SHELL_PARSER_FAILURES);

    char maximum_line[os::user::OS_USER_SHELL_MAXIMUM_LINE_SIZE_BYTES]{};
    for (uint64_t byte_index = OS_TEST_SHELL_PARSER_COMMAND_INDEX;
         byte_index < os::user::OS_USER_SHELL_MAXIMUM_LINE_SIZE_BYTES; ++byte_index) {
        maximum_line[byte_index] = OS_TEST_SHELL_PARSER_MAXIMUM_LINE_CHARACTER;
    }
    const bool maximum_line_accepted =
        os::user::ParseShellCommandLine(maximum_line,
                                        os::user::OS_USER_SHELL_MAXIMUM_LINE_SIZE_BYTES,
                                        command_line) == os::user::ShellParseStatus::Succeeded &&
        command_line.argument_count == OS_TEST_SHELL_PARSER_SINGLE_ARGUMENT_COUNT &&
        command_line.arguments[OS_TEST_SHELL_PARSER_COMMAND_INDEX].length_bytes ==
            os::user::OS_USER_SHELL_MAXIMUM_LINE_SIZE_BYTES &&
        command_line.storage[os::user::OS_USER_SHELL_MAXIMUM_LINE_SIZE_BYTES] ==
            OS_TEST_SHELL_PARSER_STRING_TERMINATOR;
    const bool line_too_long_rejected =
        os::user::ParseShellCommandLine(maximum_line,
                                        os::user::OS_USER_SHELL_MAXIMUM_LINE_SIZE_BYTES +
                                            OS_TEST_SHELL_PARSER_STRING_TERMINATOR_SIZE_BYTES,
                                        command_line) == os::user::ShellParseStatus::LineTooLong &&
        command_line.argument_count == OS_TEST_SHELL_PARSER_COMMAND_INDEX;
    const bool maximum_arguments_accepted =
        os::user::ParseShellCommandLine(OS_TEST_SHELL_PARSER_MAXIMUM_ARGUMENTS_LINE,
                                        sizeof(OS_TEST_SHELL_PARSER_MAXIMUM_ARGUMENTS_LINE) -
                                            OS_TEST_SHELL_PARSER_STRING_TERMINATOR_SIZE_BYTES,
                                        command_line) == os::user::ShellParseStatus::Succeeded &&
        command_line.argument_count == OS_TEST_SHELL_PARSER_MAXIMUM_ARGUMENT_COUNT;
    const bool empty_arguments_accepted =
        os::user::ParseShellCommandLine(OS_TEST_SHELL_PARSER_EMPTY_ARGUMENTS_LINE,
                                        sizeof(OS_TEST_SHELL_PARSER_EMPTY_ARGUMENTS_LINE) -
                                            OS_TEST_SHELL_PARSER_STRING_TERMINATOR_SIZE_BYTES,
                                        command_line) == os::user::ShellParseStatus::Succeeded &&
        command_line.argument_count == OS_TEST_SHELL_PARSER_EMPTY_ARGUMENT_COUNT &&
        command_line.arguments[OS_TEST_SHELL_PARSER_FIRST_ARGUMENT_INDEX].length_bytes ==
            OS_TEST_SHELL_PARSER_EMPTY_ARGUMENT_LENGTH_BYTES &&
        command_line.arguments[OS_TEST_SHELL_PARSER_SECOND_ARGUMENT_INDEX].length_bytes ==
            OS_TEST_SHELL_PARSER_EMPTY_ARGUMENT_LENGTH_BYTES;
    const bool null_input_rejected =
        os::user::ParseShellCommandLine(
            nullptr, OS_TEST_SHELL_PARSER_INVALID_NULL_INPUT_LENGTH_BYTES, command_line) ==
            os::user::ShellParseStatus::InvalidArgument &&
        command_line.argument_count == OS_TEST_SHELL_PARSER_COMMAND_INDEX;
    test_context.Expect(maximum_line_accepted && line_too_long_rejected &&
                            maximum_arguments_accepted && empty_arguments_accepted &&
                            null_input_rejected,
                        OS_TEST_SHELL_PARSER_BOUNDARIES);

    return test_context.ExitCode();
}

#include "os/user/shell_parser.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_SHELL_PARSER_SUITE_NAME =
    "user/shell_parser/unit";
constexpr std::string_view OS_TEST_SHELL_PARSER_SIMPLE =
    "简单命令必须切分命令名和参数";
constexpr std::string_view OS_TEST_SHELL_PARSER_QUOTES =
    "引号和转义必须生成单个去引号参数";
constexpr std::string_view OS_TEST_SHELL_PARSER_EMPTY =
    "空白输入必须返回 Empty";
constexpr std::string_view OS_TEST_SHELL_PARSER_FAILURES =
    "未闭合引号、悬空转义和参数过多必须稳定拒绝且不留下半命令";
constexpr std::string_view OS_TEST_SHELL_PARSER_BOUNDARIES =
    "精确行长、精确参数容量、空参数和非法边界必须具有稳定结果";
constexpr char OS_TEST_SHELL_PARSER_SIMPLE_LINE[] =
    "write /demo/message hello";
constexpr char OS_TEST_SHELL_PARSER_QUOTED_LINE[] =
    "echo \"hello world\" 'from shell' escaped\\ value";
constexpr char OS_TEST_SHELL_PARSER_EMPTY_LINE[] = " \t ";
constexpr char OS_TEST_SHELL_PARSER_UNTERMINATED_LINE[] =
    "echo \"unfinished";
constexpr char OS_TEST_SHELL_PARSER_DANGLING_ESCAPE_LINE[] =
    "echo unfinished\\";
constexpr char OS_TEST_SHELL_PARSER_TOO_MANY_ARGUMENTS_LINE[] =
    "echo 1 2 3 4 5 6 7 8";
constexpr char OS_TEST_SHELL_PARSER_MAXIMUM_ARGUMENTS_LINE[] =
    "a b c d e f g h";
constexpr char OS_TEST_SHELL_PARSER_EMPTY_ARGUMENTS_LINE[] =
    "echo \"\" ''";
constexpr char OS_TEST_SHELL_PARSER_EXPECTED_PATH[] = "/demo/message";
constexpr char OS_TEST_SHELL_PARSER_EXPECTED_TEXT[] = "hello";
constexpr char OS_TEST_SHELL_PARSER_EXPECTED_QUOTED_TEXT[] =
    "hello world";
constexpr char OS_TEST_SHELL_PARSER_EXPECTED_SINGLE_QUOTED_TEXT[] =
    "from shell";
constexpr char OS_TEST_SHELL_PARSER_EXPECTED_ESCAPED_TEXT[] =
    "escaped value";
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

[[nodiscard]] bool ArgumentEquals(
    const os::user::ShellCommandLine &commandLine,
    const uint64_t argumentIndex, const char *const expected,
    const uint64_t expectedLengthBytes) noexcept {
    if (argumentIndex >= commandLine.argumentCount ||
        commandLine.arguments[argumentIndex].lengthBytes !=
            expectedLengthBytes) {
        return false;
    }
    const char *const actual =
        os::user::ShellArgumentBytes(commandLine, argumentIndex);
    if (actual == nullptr) {
        return false;
    }
    for (uint64_t byteIndex = 0ULL; byteIndex < expectedLengthBytes;
         ++byteIndex) {
        if (actual[byteIndex] != expected[byteIndex]) {
            return false;
        }
    }
    return true;
}

}

int main() {
    os::test::TestContext testContext{OS_TEST_SHELL_PARSER_SUITE_NAME};
    os::user::ShellCommandLine commandLine{};

    testContext.Expect(
        os::user::ParseShellCommandLine(
            OS_TEST_SHELL_PARSER_SIMPLE_LINE,
            sizeof(OS_TEST_SHELL_PARSER_SIMPLE_LINE) -
                OS_TEST_SHELL_PARSER_STRING_TERMINATOR_SIZE_BYTES,
            commandLine) == os::user::ShellParseStatus::Succeeded &&
            commandLine.argumentCount ==
                OS_TEST_SHELL_PARSER_SIMPLE_ARGUMENT_COUNT &&
            os::user::ResolveShellCommand(commandLine) ==
                os::user::ShellCommand::WriteFile &&
            ArgumentEquals(
                commandLine, OS_TEST_SHELL_PARSER_FIRST_ARGUMENT_INDEX,
                OS_TEST_SHELL_PARSER_EXPECTED_PATH,
                sizeof(OS_TEST_SHELL_PARSER_EXPECTED_PATH) -
                    OS_TEST_SHELL_PARSER_STRING_TERMINATOR_SIZE_BYTES) &&
            ArgumentEquals(
                commandLine, OS_TEST_SHELL_PARSER_SECOND_ARGUMENT_INDEX,
                OS_TEST_SHELL_PARSER_EXPECTED_TEXT,
                sizeof(OS_TEST_SHELL_PARSER_EXPECTED_TEXT) -
                    OS_TEST_SHELL_PARSER_STRING_TERMINATOR_SIZE_BYTES),
        OS_TEST_SHELL_PARSER_SIMPLE);

    testContext.Expect(
        os::user::ParseShellCommandLine(
            OS_TEST_SHELL_PARSER_QUOTED_LINE,
            sizeof(OS_TEST_SHELL_PARSER_QUOTED_LINE) -
                OS_TEST_SHELL_PARSER_STRING_TERMINATOR_SIZE_BYTES,
            commandLine) == os::user::ShellParseStatus::Succeeded &&
            commandLine.argumentCount ==
                OS_TEST_SHELL_PARSER_QUOTED_ARGUMENT_COUNT &&
            os::user::ResolveShellCommand(commandLine) ==
                os::user::ShellCommand::Echo &&
            ArgumentEquals(
                commandLine, OS_TEST_SHELL_PARSER_FIRST_ARGUMENT_INDEX,
                OS_TEST_SHELL_PARSER_EXPECTED_QUOTED_TEXT,
                sizeof(OS_TEST_SHELL_PARSER_EXPECTED_QUOTED_TEXT) -
                    OS_TEST_SHELL_PARSER_STRING_TERMINATOR_SIZE_BYTES) &&
            ArgumentEquals(
                commandLine, OS_TEST_SHELL_PARSER_SECOND_ARGUMENT_INDEX,
                OS_TEST_SHELL_PARSER_EXPECTED_SINGLE_QUOTED_TEXT,
                sizeof(OS_TEST_SHELL_PARSER_EXPECTED_SINGLE_QUOTED_TEXT) -
                    OS_TEST_SHELL_PARSER_STRING_TERMINATOR_SIZE_BYTES) &&
            ArgumentEquals(
                commandLine, OS_TEST_SHELL_PARSER_THIRD_ARGUMENT_INDEX,
                OS_TEST_SHELL_PARSER_EXPECTED_ESCAPED_TEXT,
                sizeof(OS_TEST_SHELL_PARSER_EXPECTED_ESCAPED_TEXT) -
                    OS_TEST_SHELL_PARSER_STRING_TERMINATOR_SIZE_BYTES),
        OS_TEST_SHELL_PARSER_QUOTES);

    testContext.Expect(
        os::user::ParseShellCommandLine(
            OS_TEST_SHELL_PARSER_EMPTY_LINE,
            sizeof(OS_TEST_SHELL_PARSER_EMPTY_LINE) -
                OS_TEST_SHELL_PARSER_STRING_TERMINATOR_SIZE_BYTES,
            commandLine) == os::user::ShellParseStatus::Empty,
        OS_TEST_SHELL_PARSER_EMPTY);

    const bool unterminatedQuoteRejected =
        os::user::ParseShellCommandLine(
            OS_TEST_SHELL_PARSER_UNTERMINATED_LINE,
            sizeof(OS_TEST_SHELL_PARSER_UNTERMINATED_LINE) -
                OS_TEST_SHELL_PARSER_STRING_TERMINATOR_SIZE_BYTES,
            commandLine) ==
            os::user::ShellParseStatus::UnterminatedQuote &&
        commandLine.argumentCount == OS_TEST_SHELL_PARSER_COMMAND_INDEX;
    const bool danglingEscapeRejected =
        os::user::ParseShellCommandLine(
            OS_TEST_SHELL_PARSER_DANGLING_ESCAPE_LINE,
            sizeof(OS_TEST_SHELL_PARSER_DANGLING_ESCAPE_LINE) -
                OS_TEST_SHELL_PARSER_STRING_TERMINATOR_SIZE_BYTES,
            commandLine) ==
            os::user::ShellParseStatus::DanglingEscape &&
        commandLine.argumentCount == OS_TEST_SHELL_PARSER_COMMAND_INDEX;
    const bool tooManyArgumentsRejected =
        os::user::ParseShellCommandLine(
            OS_TEST_SHELL_PARSER_TOO_MANY_ARGUMENTS_LINE,
            sizeof(OS_TEST_SHELL_PARSER_TOO_MANY_ARGUMENTS_LINE) -
                OS_TEST_SHELL_PARSER_STRING_TERMINATOR_SIZE_BYTES,
            commandLine) ==
            os::user::ShellParseStatus::TooManyArguments &&
        commandLine.argumentCount == OS_TEST_SHELL_PARSER_COMMAND_INDEX;
    testContext.Expect(
        unterminatedQuoteRejected && danglingEscapeRejected &&
            tooManyArgumentsRejected,
        OS_TEST_SHELL_PARSER_FAILURES);

    char maximumLine
        [os::user::OS_USER_SHELL_MAXIMUM_LINE_SIZE_BYTES]{};
    for (uint64_t byteIndex = OS_TEST_SHELL_PARSER_COMMAND_INDEX;
         byteIndex <
         os::user::OS_USER_SHELL_MAXIMUM_LINE_SIZE_BYTES;
         ++byteIndex) {
        maximumLine[byteIndex] =
            OS_TEST_SHELL_PARSER_MAXIMUM_LINE_CHARACTER;
    }
    const bool maximumLineAccepted =
        os::user::ParseShellCommandLine(
            maximumLine,
            os::user::OS_USER_SHELL_MAXIMUM_LINE_SIZE_BYTES,
            commandLine) == os::user::ShellParseStatus::Succeeded &&
        commandLine.argumentCount ==
            OS_TEST_SHELL_PARSER_SINGLE_ARGUMENT_COUNT &&
        commandLine.arguments[OS_TEST_SHELL_PARSER_COMMAND_INDEX]
                .lengthBytes ==
            os::user::OS_USER_SHELL_MAXIMUM_LINE_SIZE_BYTES &&
        commandLine.storage
                [os::user::OS_USER_SHELL_MAXIMUM_LINE_SIZE_BYTES] ==
            OS_TEST_SHELL_PARSER_STRING_TERMINATOR;
    const bool lineTooLongRejected =
        os::user::ParseShellCommandLine(
            maximumLine,
            os::user::OS_USER_SHELL_MAXIMUM_LINE_SIZE_BYTES +
                OS_TEST_SHELL_PARSER_STRING_TERMINATOR_SIZE_BYTES,
            commandLine) == os::user::ShellParseStatus::LineTooLong &&
        commandLine.argumentCount == OS_TEST_SHELL_PARSER_COMMAND_INDEX;
    const bool maximumArgumentsAccepted =
        os::user::ParseShellCommandLine(
            OS_TEST_SHELL_PARSER_MAXIMUM_ARGUMENTS_LINE,
            sizeof(OS_TEST_SHELL_PARSER_MAXIMUM_ARGUMENTS_LINE) -
                OS_TEST_SHELL_PARSER_STRING_TERMINATOR_SIZE_BYTES,
            commandLine) == os::user::ShellParseStatus::Succeeded &&
        commandLine.argumentCount ==
            OS_TEST_SHELL_PARSER_MAXIMUM_ARGUMENT_COUNT;
    const bool emptyArgumentsAccepted =
        os::user::ParseShellCommandLine(
            OS_TEST_SHELL_PARSER_EMPTY_ARGUMENTS_LINE,
            sizeof(OS_TEST_SHELL_PARSER_EMPTY_ARGUMENTS_LINE) -
                OS_TEST_SHELL_PARSER_STRING_TERMINATOR_SIZE_BYTES,
            commandLine) == os::user::ShellParseStatus::Succeeded &&
        commandLine.argumentCount ==
            OS_TEST_SHELL_PARSER_EMPTY_ARGUMENT_COUNT &&
        commandLine.arguments[OS_TEST_SHELL_PARSER_FIRST_ARGUMENT_INDEX]
                .lengthBytes ==
            OS_TEST_SHELL_PARSER_EMPTY_ARGUMENT_LENGTH_BYTES &&
        commandLine.arguments[OS_TEST_SHELL_PARSER_SECOND_ARGUMENT_INDEX]
                .lengthBytes ==
            OS_TEST_SHELL_PARSER_EMPTY_ARGUMENT_LENGTH_BYTES;
    const bool nullInputRejected =
        os::user::ParseShellCommandLine(
            nullptr,
            OS_TEST_SHELL_PARSER_INVALID_NULL_INPUT_LENGTH_BYTES,
            commandLine) ==
            os::user::ShellParseStatus::InvalidArgument &&
        commandLine.argumentCount == OS_TEST_SHELL_PARSER_COMMAND_INDEX;
    testContext.Expect(
        maximumLineAccepted && lineTooLongRejected &&
            maximumArgumentsAccepted && emptyArgumentsAccepted &&
            nullInputRejected,
        OS_TEST_SHELL_PARSER_BOUNDARIES);

    return testContext.ExitCode();
}

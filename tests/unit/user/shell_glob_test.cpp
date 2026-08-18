#include "os/user/shell_execution.hpp"
#include "os/user/shell_glob.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_SHELL_GLOB_SUITE_NAME = "user/shell_glob/unit";
constexpr std::string_view OS_TEST_SHELL_GLOB_STAR = "星号必须匹配零个或多个字节并支持确定性回退";
constexpr std::string_view OS_TEST_SHELL_GLOB_QUESTION = "问号必须精确匹配一个字节";
constexpr std::string_view OS_TEST_SHELL_GLOB_QUOTED =
    "没有 glob flag 的星号和问号必须作为普通字节";
constexpr std::string_view OS_TEST_SHELL_GLOB_ARGUMENT =
    "执行解析器必须只给未引用、未转义的通配符设置 flag";
constexpr char OS_TEST_SHELL_GLOB_PATTERN[] = "a*b*c";
constexpr char OS_TEST_SHELL_GLOB_MATCH[] = "axbyc";
constexpr char OS_TEST_SHELL_GLOB_ZERO_MATCH[] = "abc";
constexpr char OS_TEST_SHELL_GLOB_REJECT[] = "axbyd";
constexpr char OS_TEST_SHELL_GLOB_QUESTION_PATTERN[] = "file?.txt";
constexpr char OS_TEST_SHELL_GLOB_QUESTION_MATCH[] = "file1.txt";
constexpr char OS_TEST_SHELL_GLOB_QUESTION_REJECT[] = "file10.txt";
constexpr char OS_TEST_SHELL_GLOB_LITERAL_PATTERN[] = "literal*?";
constexpr char OS_TEST_SHELL_GLOB_LITERAL_MATCH[] = "literal*?";
constexpr char OS_TEST_SHELL_GLOB_PARSE_LINE[] = "echo *.cpp \"*.txt\" \\?";
constexpr uint64_t OS_TEST_SHELL_GLOB_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_SHELL_GLOB_FIRST_VALUE = 1ULL;

}

int main() {
    os::test::TestContext test_context{OS_TEST_SHELL_GLOB_SUITE_NAME};
    uint8_t star_flags[sizeof(OS_TEST_SHELL_GLOB_PATTERN) - OS_TEST_SHELL_GLOB_FIRST_VALUE]{};
    star_flags[1] = os::user::OS_USER_SHELL_STORAGE_GLOB_STAR_FLAG;
    star_flags[3] = os::user::OS_USER_SHELL_STORAGE_GLOB_STAR_FLAG;
    test_context.Expect(
        os::user::MatchShellGlobPattern(
            OS_TEST_SHELL_GLOB_PATTERN, star_flags,
            sizeof(OS_TEST_SHELL_GLOB_PATTERN) - OS_TEST_SHELL_GLOB_FIRST_VALUE,
            OS_TEST_SHELL_GLOB_MATCH,
            sizeof(OS_TEST_SHELL_GLOB_MATCH) - OS_TEST_SHELL_GLOB_FIRST_VALUE) &&
            os::user::MatchShellGlobPattern(
                OS_TEST_SHELL_GLOB_PATTERN, star_flags,
                sizeof(OS_TEST_SHELL_GLOB_PATTERN) - OS_TEST_SHELL_GLOB_FIRST_VALUE,
                OS_TEST_SHELL_GLOB_ZERO_MATCH,
                sizeof(OS_TEST_SHELL_GLOB_ZERO_MATCH) - OS_TEST_SHELL_GLOB_FIRST_VALUE) &&
            !os::user::MatchShellGlobPattern(
                OS_TEST_SHELL_GLOB_PATTERN, star_flags,
                sizeof(OS_TEST_SHELL_GLOB_PATTERN) - OS_TEST_SHELL_GLOB_FIRST_VALUE,
                OS_TEST_SHELL_GLOB_REJECT,
                sizeof(OS_TEST_SHELL_GLOB_REJECT) - OS_TEST_SHELL_GLOB_FIRST_VALUE),
        OS_TEST_SHELL_GLOB_STAR);

    uint8_t question_flags[sizeof(OS_TEST_SHELL_GLOB_QUESTION_PATTERN) -
                           OS_TEST_SHELL_GLOB_FIRST_VALUE]{};
    question_flags[4] = os::user::OS_USER_SHELL_STORAGE_GLOB_QUESTION_FLAG;
    test_context.Expect(
        os::user::MatchShellGlobPattern(
            OS_TEST_SHELL_GLOB_QUESTION_PATTERN, question_flags,
            sizeof(OS_TEST_SHELL_GLOB_QUESTION_PATTERN) - OS_TEST_SHELL_GLOB_FIRST_VALUE,
            OS_TEST_SHELL_GLOB_QUESTION_MATCH,
            sizeof(OS_TEST_SHELL_GLOB_QUESTION_MATCH) - OS_TEST_SHELL_GLOB_FIRST_VALUE) &&
            !os::user::MatchShellGlobPattern(
                OS_TEST_SHELL_GLOB_QUESTION_PATTERN, question_flags,
                sizeof(OS_TEST_SHELL_GLOB_QUESTION_PATTERN) - OS_TEST_SHELL_GLOB_FIRST_VALUE,
                OS_TEST_SHELL_GLOB_QUESTION_REJECT,
                sizeof(OS_TEST_SHELL_GLOB_QUESTION_REJECT) - OS_TEST_SHELL_GLOB_FIRST_VALUE),
        OS_TEST_SHELL_GLOB_QUESTION);

    uint8_t literal_flags[sizeof(OS_TEST_SHELL_GLOB_LITERAL_PATTERN) -
                          OS_TEST_SHELL_GLOB_FIRST_VALUE]{};
    test_context.Expect(
        os::user::MatchShellGlobPattern(
            OS_TEST_SHELL_GLOB_LITERAL_PATTERN, literal_flags,
            sizeof(OS_TEST_SHELL_GLOB_LITERAL_PATTERN) - OS_TEST_SHELL_GLOB_FIRST_VALUE,
            OS_TEST_SHELL_GLOB_LITERAL_MATCH,
            sizeof(OS_TEST_SHELL_GLOB_LITERAL_MATCH) - OS_TEST_SHELL_GLOB_FIRST_VALUE),
        OS_TEST_SHELL_GLOB_QUOTED);

    os::user::ShellExecutionPlan plan{};
    const bool argument_flags_valid =
        os::user::ParseShellExecutionPlan(OS_TEST_SHELL_GLOB_PARSE_LINE,
                                          sizeof(OS_TEST_SHELL_GLOB_PARSE_LINE) -
                                              OS_TEST_SHELL_GLOB_FIRST_VALUE,
                                          plan) == os::user::ShellExecutionParseStatus::Succeeded &&
        plan.argument_count == 4ULL && os::user::ShellExecutionArgumentHasGlob(plan, 1ULL) &&
        !os::user::ShellExecutionArgumentHasGlob(plan, 2ULL) &&
        !os::user::ShellExecutionArgumentHasGlob(plan, 3ULL) &&
        !os::user::ShellExecutionArgumentHasGlob(plan, OS_TEST_SHELL_GLOB_EMPTY_VALUE);
    test_context.Expect(argument_flags_valid, OS_TEST_SHELL_GLOB_ARGUMENT);

    return test_context.ExitCode();
}

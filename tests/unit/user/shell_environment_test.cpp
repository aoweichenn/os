#include "os/user/shell_environment.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_SHELL_ENVIRONMENT_SUITE_NAME = "user/shell_environment/unit";
constexpr std::string_view OS_TEST_SHELL_ENVIRONMENT_INITIALIZE =
    "初始 envp 必须去重并保留最后一次赋值";
constexpr std::string_view OS_TEST_SHELL_ENVIRONMENT_MUTATE =
    "set、unset 与空值必须保持计数和查找一致";
constexpr std::string_view OS_TEST_SHELL_ENVIRONMENT_REJECTION =
    "非法名称、缺少等号、超长和满表必须失败原子";
constexpr std::string_view OS_TEST_SHELL_ENVIRONMENT_ITERATE =
    "导出迭代必须只返回占用项和精确 NAME=value 长度";
constexpr uint64_t OS_TEST_SHELL_ENVIRONMENT_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_SHELL_ENVIRONMENT_FIRST_VALUE = 1ULL;
constexpr char OS_TEST_SHELL_ENVIRONMENT_FIRST[] = "PATH=/bin";
constexpr char OS_TEST_SHELL_ENVIRONMENT_DUPLICATE[] = "PATH=/usr/bin";
constexpr char OS_TEST_SHELL_ENVIRONMENT_SECOND[] = "HOME=/";
constexpr char OS_TEST_SHELL_ENVIRONMENT_NAME[] = "PATH";
constexpr char OS_TEST_SHELL_ENVIRONMENT_EXPECTED_VALUE[] = "/usr/bin";
constexpr char OS_TEST_SHELL_ENVIRONMENT_EMPTY_ASSIGNMENT[] = "EMPTY=";
constexpr char OS_TEST_SHELL_ENVIRONMENT_UNSET_NAME[] = "HOME";
constexpr char OS_TEST_SHELL_ENVIRONMENT_INVALID_NAME[] = "9BAD=value";
constexpr char OS_TEST_SHELL_ENVIRONMENT_MISSING_SEPARATOR[] = "MISSING";

[[nodiscard]] bool BytesEqual(const char *const actual, const uint64_t actual_length_bytes,
                              const char *const expected,
                              const uint64_t expected_length_bytes) noexcept {
    if (actual == nullptr || expected == nullptr || actual_length_bytes != expected_length_bytes) {
        return false;
    }
    for (uint64_t byte_index = OS_TEST_SHELL_ENVIRONMENT_EMPTY_VALUE;
         byte_index < expected_length_bytes; ++byte_index) {
        if (actual[byte_index] != expected[byte_index]) {
            return false;
        }
    }
    return true;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_SHELL_ENVIRONMENT_SUITE_NAME};
    const char *const initial_environment[]{
        OS_TEST_SHELL_ENVIRONMENT_FIRST,
        OS_TEST_SHELL_ENVIRONMENT_DUPLICATE,
        OS_TEST_SHELL_ENVIRONMENT_SECOND,
        nullptr,
    };
    os::user::ShellEnvironmentTable environment{};
    const char *value = nullptr;
    uint64_t value_length_bytes = OS_TEST_SHELL_ENVIRONMENT_EMPTY_VALUE;
    const bool initialized =
        environment.Initialize(initial_environment) ==
            os::user::ShellEnvironmentStatus::Succeeded &&
        environment.Count() == 2ULL && environment.Validate() &&
        environment.Find(
            OS_TEST_SHELL_ENVIRONMENT_NAME,
            sizeof(OS_TEST_SHELL_ENVIRONMENT_NAME) - OS_TEST_SHELL_ENVIRONMENT_FIRST_VALUE, value,
            value_length_bytes) == os::user::ShellEnvironmentStatus::Succeeded &&
        BytesEqual(value, value_length_bytes, OS_TEST_SHELL_ENVIRONMENT_EXPECTED_VALUE,
                   sizeof(OS_TEST_SHELL_ENVIRONMENT_EXPECTED_VALUE) -
                       OS_TEST_SHELL_ENVIRONMENT_FIRST_VALUE);
    test_context.Expect(initialized, OS_TEST_SHELL_ENVIRONMENT_INITIALIZE);

    const bool mutated =
        environment.SetAssignment(OS_TEST_SHELL_ENVIRONMENT_EMPTY_ASSIGNMENT,
                                  sizeof(OS_TEST_SHELL_ENVIRONMENT_EMPTY_ASSIGNMENT) -
                                      OS_TEST_SHELL_ENVIRONMENT_FIRST_VALUE) ==
            os::user::ShellEnvironmentStatus::Succeeded &&
        environment.Unset(OS_TEST_SHELL_ENVIRONMENT_UNSET_NAME,
                          sizeof(OS_TEST_SHELL_ENVIRONMENT_UNSET_NAME) -
                              OS_TEST_SHELL_ENVIRONMENT_FIRST_VALUE) ==
            os::user::ShellEnvironmentStatus::Succeeded &&
        environment.Count() == 2ULL && environment.Validate() &&
        environment.Find("EMPTY", 5ULL, value, value_length_bytes) ==
            os::user::ShellEnvironmentStatus::Succeeded &&
        value_length_bytes == OS_TEST_SHELL_ENVIRONMENT_EMPTY_VALUE;
    test_context.Expect(mutated, OS_TEST_SHELL_ENVIRONMENT_MUTATE);

    char long_assignment[os::user::OS_USER_SHELL_ENVIRONMENT_MAXIMUM_ENTRY_SIZE_BYTES]{};
    long_assignment[OS_TEST_SHELL_ENVIRONMENT_EMPTY_VALUE] = 'A';
    long_assignment[OS_TEST_SHELL_ENVIRONMENT_FIRST_VALUE] = '=';
    for (uint64_t byte_index = 2ULL; byte_index < sizeof(long_assignment); ++byte_index) {
        long_assignment[byte_index] = 'x';
    }
    const uint64_t count_before_rejection = environment.Count();
    const bool rejected =
        environment.SetAssignment(OS_TEST_SHELL_ENVIRONMENT_INVALID_NAME,
                                  sizeof(OS_TEST_SHELL_ENVIRONMENT_INVALID_NAME) -
                                      OS_TEST_SHELL_ENVIRONMENT_FIRST_VALUE) ==
            os::user::ShellEnvironmentStatus::InvalidName &&
        environment.SetAssignment(OS_TEST_SHELL_ENVIRONMENT_MISSING_SEPARATOR,
                                  sizeof(OS_TEST_SHELL_ENVIRONMENT_MISSING_SEPARATOR) -
                                      OS_TEST_SHELL_ENVIRONMENT_FIRST_VALUE) ==
            os::user::ShellEnvironmentStatus::InvalidAssignment &&
        environment.SetAssignment(long_assignment, sizeof(long_assignment)) ==
            os::user::ShellEnvironmentStatus::EntryTooLong &&
        environment.Count() == count_before_rejection && environment.Validate();
    test_context.Expect(rejected, OS_TEST_SHELL_ENVIRONMENT_REJECTION);

    bool iteration_valid = true;
    for (uint64_t logical_index = OS_TEST_SHELL_ENVIRONMENT_EMPTY_VALUE;
         logical_index < environment.Count(); ++logical_index) {
        const char *entry = nullptr;
        uint64_t entry_length_bytes = OS_TEST_SHELL_ENVIRONMENT_EMPTY_VALUE;
        iteration_valid = iteration_valid &&
                          environment.Read(logical_index, entry, entry_length_bytes) ==
                              os::user::ShellEnvironmentStatus::Succeeded &&
                          entry != nullptr &&
                          entry_length_bytes != OS_TEST_SHELL_ENVIRONMENT_EMPTY_VALUE;
    }
    const char *missing_entry = nullptr;
    uint64_t missing_length_bytes = OS_TEST_SHELL_ENVIRONMENT_FIRST_VALUE;
    iteration_valid = iteration_valid &&
                      environment.Read(environment.Count(), missing_entry, missing_length_bytes) ==
                          os::user::ShellEnvironmentStatus::NotFound &&
                      missing_entry == nullptr &&
                      missing_length_bytes == OS_TEST_SHELL_ENVIRONMENT_EMPTY_VALUE;
    test_context.Expect(iteration_valid, OS_TEST_SHELL_ENVIRONMENT_ITERATE);

    return test_context.ExitCode();
}

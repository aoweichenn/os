#include "os/user/shell_environment.hpp"
#include "os/user/shell_execution.hpp"
#include "os/user/shell_glob.hpp"
#include "test_context.hpp"

#include <random>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view OS_TEST_SHELL_ENV_GLOB_SUITE_NAME =
    "user/shell_environment_glob/randomized";
constexpr std::string_view OS_TEST_SHELL_ENV_GLOB_ENVIRONMENT =
    "10000 步环境 set/unset/find 必须逐步匹配独立模型";
constexpr std::string_view OS_TEST_SHELL_ENV_GLOB_PATTERN =
    "10000 组引用感知 glob 必须匹配独立动态规划 oracle";
constexpr os::test::RandomSeed OS_TEST_SHELL_ENV_GLOB_SEED = 0xE670B20260818ULL;
constexpr os::test::TestCount OS_TEST_SHELL_ENV_GLOB_ITERATIONS = 10000ULL;
constexpr uint64_t OS_TEST_SHELL_ENV_GLOB_MODEL_ENTRY_COUNT = 40ULL;
constexpr uint64_t OS_TEST_SHELL_ENV_GLOB_PATTERN_CAPACITY = 8ULL;
constexpr uint64_t OS_TEST_SHELL_ENV_GLOB_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_SHELL_ENV_GLOB_FIRST_VALUE = 1ULL;

[[nodiscard]] std::string NameForIndex(const uint64_t index) { return "v" + std::to_string(index); }

[[nodiscard]] bool OracleMatch(const char *const pattern, const uint8_t *const flags,
                               const uint64_t pattern_length, const char *const candidate,
                               const uint64_t candidate_length) noexcept {
    bool states[OS_TEST_SHELL_ENV_GLOB_PATTERN_CAPACITY + OS_TEST_SHELL_ENV_GLOB_FIRST_VALUE]
               [OS_TEST_SHELL_ENV_GLOB_PATTERN_CAPACITY + OS_TEST_SHELL_ENV_GLOB_FIRST_VALUE]{};
    states[0][0] = true;
    for (uint64_t pattern_index = OS_TEST_SHELL_ENV_GLOB_EMPTY_VALUE;
         pattern_index < pattern_length; ++pattern_index) {
        const bool star =
            pattern[pattern_index] == '*' &&
            (flags[pattern_index] & os::user::OS_USER_SHELL_STORAGE_GLOB_STAR_FLAG) != 0U;
        const bool question =
            pattern[pattern_index] == '?' &&
            (flags[pattern_index] & os::user::OS_USER_SHELL_STORAGE_GLOB_QUESTION_FLAG) != 0U;
        for (uint64_t candidate_index = OS_TEST_SHELL_ENV_GLOB_EMPTY_VALUE;
             candidate_index <= candidate_length; ++candidate_index) {
            if (star) {
                states[pattern_index + 1ULL][candidate_index] =
                    states[pattern_index][candidate_index] ||
                    (candidate_index > 0ULL &&
                     states[pattern_index + 1ULL][candidate_index - 1ULL]);
            } else if (candidate_index > 0ULL &&
                       (question || pattern[pattern_index] == candidate[candidate_index - 1ULL])) {
                states[pattern_index + 1ULL][candidate_index] =
                    states[pattern_index][candidate_index - 1ULL];
            }
        }
    }
    return states[pattern_length][candidate_length];
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_SHELL_ENV_GLOB_SUITE_NAME};
    std::mt19937_64 generator{OS_TEST_SHELL_ENV_GLOB_SEED};
    std::uniform_int_distribution<uint64_t> entry_distribution{
        OS_TEST_SHELL_ENV_GLOB_EMPTY_VALUE, OS_TEST_SHELL_ENV_GLOB_MODEL_ENTRY_COUNT - 1ULL};
    std::uniform_int_distribution<uint64_t> operation_distribution{0ULL, 2ULL};
    bool occupied[OS_TEST_SHELL_ENV_GLOB_MODEL_ENTRY_COUNT]{};
    std::string values[OS_TEST_SHELL_ENV_GLOB_MODEL_ENTRY_COUNT]{};
    uint64_t model_count = OS_TEST_SHELL_ENV_GLOB_EMPTY_VALUE;
    os::user::ShellEnvironmentTable environment{};
    const char *const empty_environment[]{nullptr};
    bool environment_valid =
        environment.Initialize(empty_environment) == os::user::ShellEnvironmentStatus::Succeeded;
    for (os::test::TestCount iteration = OS_TEST_SHELL_ENV_GLOB_EMPTY_VALUE;
         iteration < OS_TEST_SHELL_ENV_GLOB_ITERATIONS && environment_valid; ++iteration) {
        const uint64_t entry_index = entry_distribution(generator);
        const uint64_t operation = operation_distribution(generator);
        const std::string name = NameForIndex(entry_index);
        if (operation == 0ULL) {
            const std::string value = "value-" + std::to_string(iteration);
            const os::user::ShellEnvironmentStatus status =
                environment.Set(name.data(), name.size(), value.data(), value.size());
            const bool expected_success =
                occupied[entry_index] ||
                model_count < os::user::OS_USER_SHELL_ENVIRONMENT_MAXIMUM_ENTRY_COUNT;
            environment_valid =
                status == (expected_success ? os::user::ShellEnvironmentStatus::Succeeded
                                            : os::user::ShellEnvironmentStatus::CapacityExceeded);
            if (expected_success) {
                if (!occupied[entry_index]) {
                    occupied[entry_index] = true;
                    ++model_count;
                }
                values[entry_index] = value;
            }
        } else if (operation == 1ULL) {
            const os::user::ShellEnvironmentStatus status =
                environment.Unset(name.data(), name.size());
            environment_valid =
                status == (occupied[entry_index] ? os::user::ShellEnvironmentStatus::Succeeded
                                                 : os::user::ShellEnvironmentStatus::NotFound);
            if (occupied[entry_index]) {
                occupied[entry_index] = false;
                values[entry_index].clear();
                --model_count;
            }
        } else {
            const char *value = nullptr;
            uint64_t value_length = OS_TEST_SHELL_ENV_GLOB_EMPTY_VALUE;
            const os::user::ShellEnvironmentStatus status =
                environment.Find(name.data(), name.size(), value, value_length);
            environment_valid =
                status == (occupied[entry_index] ? os::user::ShellEnvironmentStatus::Succeeded
                                                 : os::user::ShellEnvironmentStatus::NotFound) &&
                (!occupied[entry_index] ||
                 (value_length == values[entry_index].size() &&
                  std::string_view{value, value_length} == values[entry_index]));
        }
        environment_valid =
            environment_valid && environment.Count() == model_count && environment.Validate();
    }
    test_context.Expect(environment_valid, OS_TEST_SHELL_ENV_GLOB_ENVIRONMENT);

    std::uniform_int_distribution<uint64_t> length_distribution{
        OS_TEST_SHELL_ENV_GLOB_EMPTY_VALUE, OS_TEST_SHELL_ENV_GLOB_PATTERN_CAPACITY};
    std::uniform_int_distribution<uint64_t> character_distribution{0ULL, 3ULL};
    std::uniform_int_distribution<uint64_t> flag_distribution{0ULL, 1ULL};
    constexpr char alphabet[]{'a', 'b', '*', '?'};
    bool glob_valid = true;
    for (os::test::TestCount iteration = OS_TEST_SHELL_ENV_GLOB_EMPTY_VALUE;
         iteration < OS_TEST_SHELL_ENV_GLOB_ITERATIONS && glob_valid; ++iteration) {
        char pattern[OS_TEST_SHELL_ENV_GLOB_PATTERN_CAPACITY]{};
        uint8_t flags[OS_TEST_SHELL_ENV_GLOB_PATTERN_CAPACITY]{};
        char candidate[OS_TEST_SHELL_ENV_GLOB_PATTERN_CAPACITY]{};
        const uint64_t pattern_length = length_distribution(generator);
        const uint64_t candidate_length = length_distribution(generator);
        for (uint64_t index = OS_TEST_SHELL_ENV_GLOB_EMPTY_VALUE; index < pattern_length; ++index) {
            pattern[index] = alphabet[character_distribution(generator)];
            if (flag_distribution(generator) != 0ULL) {
                flags[index] =
                    pattern[index] == '*'   ? os::user::OS_USER_SHELL_STORAGE_GLOB_STAR_FLAG
                    : pattern[index] == '?' ? os::user::OS_USER_SHELL_STORAGE_GLOB_QUESTION_FLAG
                                            : 0U;
            }
        }
        for (uint64_t index = OS_TEST_SHELL_ENV_GLOB_EMPTY_VALUE; index < candidate_length;
             ++index) {
            candidate[index] = alphabet[character_distribution(generator) % 2ULL];
        }
        glob_valid = os::user::MatchShellGlobPattern(pattern, flags, pattern_length, candidate,
                                                     candidate_length) ==
                     OracleMatch(pattern, flags, pattern_length, candidate, candidate_length);
    }
    test_context.Expect(glob_valid, OS_TEST_SHELL_ENV_GLOB_PATTERN);
    return test_context.ExitCode();
}

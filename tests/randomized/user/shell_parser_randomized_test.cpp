#include "os/user/shell_parser.hpp"
#include "test_context.hpp"

#include <cstdint>
#include <random>
#include <string_view>

namespace {

constexpr std::string_view OS_TEST_SHELL_RANDOM_SUITE_NAME = "user/shell_parser/randomized";
constexpr std::string_view OS_TEST_SHELL_RANDOM_ROUND_TRIP =
    "随机安全参数经转义拼接后必须逐字节往返";
constexpr std::string_view OS_TEST_SHELL_RANDOM_ARBITRARY_INPUT =
    "任意 7-bit 输入必须保持固定布局、失败原子性和可重复结果";
constexpr os::test::RandomSeed OS_TEST_SHELL_RANDOM_SEED = 0x5E11A55E20260010ULL;
constexpr os::test::TestCount OS_TEST_SHELL_RANDOM_ITERATION_COUNT = 4096ULL;
constexpr uint64_t OS_TEST_SHELL_RANDOM_MINIMUM_ARGUMENT_COUNT = 1ULL;
constexpr uint64_t OS_TEST_SHELL_RANDOM_MAXIMUM_ARGUMENT_COUNT = 6ULL;
constexpr uint64_t OS_TEST_SHELL_RANDOM_MINIMUM_ARGUMENT_LENGTH = 1ULL;
constexpr uint64_t OS_TEST_SHELL_RANDOM_MAXIMUM_ARGUMENT_LENGTH = 10ULL;
constexpr uint64_t OS_TEST_SHELL_RANDOM_FIRST_CHARACTER_INDEX = 0ULL;
constexpr uint64_t OS_TEST_SHELL_RANDOM_ALPHABET_SIZE = 36ULL;
constexpr uint64_t OS_TEST_SHELL_RANDOM_DIGIT_OFFSET = 26ULL;
constexpr uint64_t OS_TEST_SHELL_RANDOM_SPACE_SIZE_BYTES = 1ULL;
constexpr uint64_t OS_TEST_SHELL_RANDOM_STRING_TERMINATOR_SIZE_BYTES = 1ULL;
constexpr uint64_t OS_TEST_SHELL_RANDOM_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_SHELL_RANDOM_MINIMUM_ARBITRARY_LENGTH_BYTES = 0ULL;
constexpr uint64_t OS_TEST_SHELL_RANDOM_MAXIMUM_ARBITRARY_LENGTH_BYTES =
    os::user::OS_USER_SHELL_MAXIMUM_LINE_SIZE_BYTES;
constexpr uint64_t OS_TEST_SHELL_RANDOM_MINIMUM_ARBITRARY_BYTE = 0ULL;
constexpr uint64_t OS_TEST_SHELL_RANDOM_MAXIMUM_ARBITRARY_BYTE = 0x7FULL;
constexpr char OS_TEST_SHELL_RANDOM_STRING_TERMINATOR = '\0';
constexpr char OS_TEST_SHELL_RANDOM_SPACE = ' ';
constexpr char OS_TEST_SHELL_RANDOM_FIRST_LETTER = 'a';
constexpr char OS_TEST_SHELL_RANDOM_FIRST_DIGIT = '0';

[[nodiscard]] char CharacterForIndex(const uint64_t character_index) noexcept {
    if (character_index < OS_TEST_SHELL_RANDOM_DIGIT_OFFSET) {
        return static_cast<char>(OS_TEST_SHELL_RANDOM_FIRST_LETTER +
                                 static_cast<char>(character_index));
    }
    return static_cast<char>(
        OS_TEST_SHELL_RANDOM_FIRST_DIGIT +
        static_cast<char>(character_index - OS_TEST_SHELL_RANDOM_DIGIT_OFFSET));
}

[[nodiscard]] bool CommandLinesEqual(const os::user::ShellCommandLine &first,
                                     const os::user::ShellCommandLine &second) noexcept {
    if (first.argument_count != second.argument_count) {
        return false;
    }
    for (uint64_t byte_index = OS_TEST_SHELL_RANDOM_EMPTY_VALUE;
         byte_index < os::user::OS_USER_SHELL_STORAGE_SIZE_BYTES; ++byte_index) {
        if (first.storage[byte_index] != second.storage[byte_index]) {
            return false;
        }
    }
    for (uint64_t argument_index = OS_TEST_SHELL_RANDOM_EMPTY_VALUE;
         argument_index < os::user::OS_USER_SHELL_MAXIMUM_ARGUMENT_COUNT; ++argument_index) {
        if (first.arguments[argument_index].offset_bytes !=
                second.arguments[argument_index].offset_bytes ||
            first.arguments[argument_index].length_bytes !=
                second.arguments[argument_index].length_bytes) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool
CommandLineLayoutIsValid(const os::user::ShellCommandLine &command_line) noexcept {
    if (command_line.argument_count > os::user::OS_USER_SHELL_MAXIMUM_ARGUMENT_COUNT) {
        return false;
    }
    for (uint64_t argument_index = OS_TEST_SHELL_RANDOM_EMPTY_VALUE;
         argument_index < command_line.argument_count; ++argument_index) {
        const os::user::ShellArgument &argument = command_line.arguments[argument_index];
        if (argument.offset_bytes >= os::user::OS_USER_SHELL_STORAGE_SIZE_BYTES ||
            argument.length_bytes >=
                os::user::OS_USER_SHELL_STORAGE_SIZE_BYTES - argument.offset_bytes ||
            command_line.storage[argument.offset_bytes + argument.length_bytes] !=
                OS_TEST_SHELL_RANDOM_STRING_TERMINATOR) {
            return false;
        }
    }
    return true;
}
}

int main() {
    os::test::TestContext test_context{OS_TEST_SHELL_RANDOM_SUITE_NAME};
    std::mt19937_64 generator{OS_TEST_SHELL_RANDOM_SEED};
    std::uniform_int_distribution<uint64_t> argument_count_distribution{
        OS_TEST_SHELL_RANDOM_MINIMUM_ARGUMENT_COUNT, OS_TEST_SHELL_RANDOM_MAXIMUM_ARGUMENT_COUNT};
    std::uniform_int_distribution<uint64_t> argument_length_distribution{
        OS_TEST_SHELL_RANDOM_MINIMUM_ARGUMENT_LENGTH, OS_TEST_SHELL_RANDOM_MAXIMUM_ARGUMENT_LENGTH};
    std::uniform_int_distribution<uint64_t> character_distribution{
        OS_TEST_SHELL_RANDOM_FIRST_CHARACTER_INDEX,
        OS_TEST_SHELL_RANDOM_ALPHABET_SIZE - OS_TEST_SHELL_RANDOM_STRING_TERMINATOR_SIZE_BYTES};
    std::uniform_int_distribution<uint64_t> arbitrary_length_distribution{
        OS_TEST_SHELL_RANDOM_MINIMUM_ARBITRARY_LENGTH_BYTES,
        OS_TEST_SHELL_RANDOM_MAXIMUM_ARBITRARY_LENGTH_BYTES};
    std::uniform_int_distribution<uint64_t> arbitrary_byte_distribution{
        OS_TEST_SHELL_RANDOM_MINIMUM_ARBITRARY_BYTE, OS_TEST_SHELL_RANDOM_MAXIMUM_ARBITRARY_BYTE};

    for (os::test::TestCount iteration = 0ULL; iteration < OS_TEST_SHELL_RANDOM_ITERATION_COUNT;
         ++iteration) {
        char line[os::user::OS_USER_SHELL_MAXIMUM_LINE_SIZE_BYTES]{};
        char expected[os::user::OS_USER_SHELL_MAXIMUM_ARGUMENT_COUNT]
                     [OS_TEST_SHELL_RANDOM_MAXIMUM_ARGUMENT_LENGTH]{};
        uint64_t expected_lengths[os::user::OS_USER_SHELL_MAXIMUM_ARGUMENT_COUNT]{};
        uint64_t line_length_bytes = 0ULL;
        const uint64_t argument_count = argument_count_distribution(generator);
        for (uint64_t argument_index = 0ULL; argument_index < argument_count; ++argument_index) {
            if (argument_index != 0ULL) {
                line[line_length_bytes] = OS_TEST_SHELL_RANDOM_SPACE;
                line_length_bytes += OS_TEST_SHELL_RANDOM_SPACE_SIZE_BYTES;
            }
            expected_lengths[argument_index] = argument_length_distribution(generator);
            for (uint64_t byte_index = 0ULL; byte_index < expected_lengths[argument_index];
                 ++byte_index) {
                const char character = CharacterForIndex(character_distribution(generator));
                expected[argument_index][byte_index] = character;
                line[line_length_bytes] = character;
                ++line_length_bytes;
            }
        }

        os::user::ShellCommandLine command_line{};
        bool valid = os::user::ParseShellCommandLine(line, line_length_bytes, command_line) ==
                         os::user::ShellParseStatus::Succeeded &&
                     command_line.argument_count == argument_count;
        for (uint64_t argument_index = 0ULL; argument_index < argument_count && valid;
             ++argument_index) {
            valid = command_line.arguments[argument_index].length_bytes ==
                    expected_lengths[argument_index];
            const char *const actual = os::user::ShellArgumentBytes(command_line, argument_index);
            valid = valid && actual != nullptr;
            for (uint64_t byte_index = 0ULL; byte_index < expected_lengths[argument_index] && valid;
                 ++byte_index) {
                valid = actual[byte_index] == expected[argument_index][byte_index];
            }
        }
        test_context.ExpectRandom(valid, OS_TEST_SHELL_RANDOM_ROUND_TRIP, OS_TEST_SHELL_RANDOM_SEED,
                                  iteration);

        char arbitrary_line[os::user::OS_USER_SHELL_MAXIMUM_LINE_SIZE_BYTES]{};
        const uint64_t arbitrary_length_bytes = arbitrary_length_distribution(generator);
        for (uint64_t byte_index = OS_TEST_SHELL_RANDOM_EMPTY_VALUE;
             byte_index < arbitrary_length_bytes; ++byte_index) {
            arbitrary_line[byte_index] = static_cast<char>(arbitrary_byte_distribution(generator));
        }
        os::user::ShellCommandLine first_command_line{};
        os::user::ShellCommandLine second_command_line{};
        const os::user::ShellParseStatus first_status = os::user::ParseShellCommandLine(
            arbitrary_line, arbitrary_length_bytes, first_command_line);
        const os::user::ShellParseStatus second_status = os::user::ParseShellCommandLine(
            arbitrary_line, arbitrary_length_bytes, second_command_line);
        const bool successful_layout_is_valid =
            first_status != os::user::ShellParseStatus::Succeeded ||
            CommandLineLayoutIsValid(first_command_line);
        const bool non_success_is_atomic =
            first_status == os::user::ShellParseStatus::Succeeded ||
            first_command_line.argument_count == OS_TEST_SHELL_RANDOM_EMPTY_VALUE;
        test_context.ExpectRandom(first_status == second_status &&
                                      CommandLinesEqual(first_command_line, second_command_line) &&
                                      successful_layout_is_valid && non_success_is_atomic,
                                  OS_TEST_SHELL_RANDOM_ARBITRARY_INPUT, OS_TEST_SHELL_RANDOM_SEED,
                                  iteration);
    }

    return test_context.ExitCode();
}

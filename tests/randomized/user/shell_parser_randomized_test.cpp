#include "os/user/shell_parser.hpp"
#include "test_context.hpp"

#include <cstdint>
#include <random>
#include <string_view>

namespace {

constexpr std::string_view OS_TEST_SHELL_RANDOM_SUITE_NAME =
    "user/shell_parser/randomized";
constexpr std::string_view OS_TEST_SHELL_RANDOM_ROUND_TRIP =
    "随机安全参数经转义拼接后必须逐字节往返";
constexpr std::string_view OS_TEST_SHELL_RANDOM_ARBITRARY_INPUT =
    "任意 7-bit 输入必须保持固定布局、失败原子性和可重复结果";
constexpr os::test::RandomSeed OS_TEST_SHELL_RANDOM_SEED =
    0x5E11A55E20260010ULL;
constexpr os::test::TestCount OS_TEST_SHELL_RANDOM_ITERATION_COUNT =
    4096ULL;
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

[[nodiscard]] char CharacterForIndex(const uint64_t characterIndex) noexcept {
    if (characterIndex < OS_TEST_SHELL_RANDOM_DIGIT_OFFSET) {
        return static_cast<char>(
            OS_TEST_SHELL_RANDOM_FIRST_LETTER +
            static_cast<char>(characterIndex));
    }
    return static_cast<char>(
        OS_TEST_SHELL_RANDOM_FIRST_DIGIT +
        static_cast<char>(characterIndex -
                          OS_TEST_SHELL_RANDOM_DIGIT_OFFSET));
}

[[nodiscard]] bool CommandLinesEqual(
    const os::user::ShellCommandLine &first,
    const os::user::ShellCommandLine &second) noexcept {
    if (first.argumentCount != second.argumentCount) {
        return false;
    }
    for (uint64_t byteIndex = OS_TEST_SHELL_RANDOM_EMPTY_VALUE;
         byteIndex < os::user::OS_USER_SHELL_STORAGE_SIZE_BYTES;
         ++byteIndex) {
        if (first.storage[byteIndex] != second.storage[byteIndex]) {
            return false;
        }
    }
    for (uint64_t argumentIndex = OS_TEST_SHELL_RANDOM_EMPTY_VALUE;
         argumentIndex < os::user::OS_USER_SHELL_MAXIMUM_ARGUMENT_COUNT;
         ++argumentIndex) {
        if (first.arguments[argumentIndex].offsetBytes !=
                second.arguments[argumentIndex].offsetBytes ||
            first.arguments[argumentIndex].lengthBytes !=
                second.arguments[argumentIndex].lengthBytes) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool CommandLineLayoutIsValid(
    const os::user::ShellCommandLine &commandLine) noexcept {
    if (commandLine.argumentCount >
        os::user::OS_USER_SHELL_MAXIMUM_ARGUMENT_COUNT) {
        return false;
    }
    for (uint64_t argumentIndex = OS_TEST_SHELL_RANDOM_EMPTY_VALUE;
         argumentIndex < commandLine.argumentCount; ++argumentIndex) {
        const os::user::ShellArgument &argument =
            commandLine.arguments[argumentIndex];
        if (argument.offsetBytes >=
                os::user::OS_USER_SHELL_STORAGE_SIZE_BYTES ||
            argument.lengthBytes >=
                os::user::OS_USER_SHELL_STORAGE_SIZE_BYTES -
                    argument.offsetBytes ||
            commandLine.storage[argument.offsetBytes +
                                argument.lengthBytes] !=
                OS_TEST_SHELL_RANDOM_STRING_TERMINATOR) {
            return false;
        }
    }
    return true;
}

}

int main() {
    os::test::TestContext testContext{OS_TEST_SHELL_RANDOM_SUITE_NAME};
    std::mt19937_64 generator{OS_TEST_SHELL_RANDOM_SEED};
    std::uniform_int_distribution<uint64_t> argumentCountDistribution{
        OS_TEST_SHELL_RANDOM_MINIMUM_ARGUMENT_COUNT,
        OS_TEST_SHELL_RANDOM_MAXIMUM_ARGUMENT_COUNT};
    std::uniform_int_distribution<uint64_t> argumentLengthDistribution{
        OS_TEST_SHELL_RANDOM_MINIMUM_ARGUMENT_LENGTH,
        OS_TEST_SHELL_RANDOM_MAXIMUM_ARGUMENT_LENGTH};
    std::uniform_int_distribution<uint64_t> characterDistribution{
        OS_TEST_SHELL_RANDOM_FIRST_CHARACTER_INDEX,
        OS_TEST_SHELL_RANDOM_ALPHABET_SIZE -
            OS_TEST_SHELL_RANDOM_STRING_TERMINATOR_SIZE_BYTES};
    std::uniform_int_distribution<uint64_t> arbitraryLengthDistribution{
        OS_TEST_SHELL_RANDOM_MINIMUM_ARBITRARY_LENGTH_BYTES,
        OS_TEST_SHELL_RANDOM_MAXIMUM_ARBITRARY_LENGTH_BYTES};
    std::uniform_int_distribution<uint64_t> arbitraryByteDistribution{
        OS_TEST_SHELL_RANDOM_MINIMUM_ARBITRARY_BYTE,
        OS_TEST_SHELL_RANDOM_MAXIMUM_ARBITRARY_BYTE};

    for (os::test::TestCount iteration = 0ULL;
         iteration < OS_TEST_SHELL_RANDOM_ITERATION_COUNT; ++iteration) {
        char line[os::user::OS_USER_SHELL_MAXIMUM_LINE_SIZE_BYTES]{};
        char expected[os::user::OS_USER_SHELL_MAXIMUM_ARGUMENT_COUNT]
                     [OS_TEST_SHELL_RANDOM_MAXIMUM_ARGUMENT_LENGTH]{};
        uint64_t expectedLengths
            [os::user::OS_USER_SHELL_MAXIMUM_ARGUMENT_COUNT]{};
        uint64_t lineLengthBytes = 0ULL;
        const uint64_t argumentCount =
            argumentCountDistribution(generator);
        for (uint64_t argumentIndex = 0ULL;
             argumentIndex < argumentCount; ++argumentIndex) {
            if (argumentIndex != 0ULL) {
                line[lineLengthBytes] = OS_TEST_SHELL_RANDOM_SPACE;
                lineLengthBytes += OS_TEST_SHELL_RANDOM_SPACE_SIZE_BYTES;
            }
            expectedLengths[argumentIndex] =
                argumentLengthDistribution(generator);
            for (uint64_t byteIndex = 0ULL;
                 byteIndex < expectedLengths[argumentIndex]; ++byteIndex) {
                const char character =
                    CharacterForIndex(characterDistribution(generator));
                expected[argumentIndex][byteIndex] = character;
                line[lineLengthBytes] = character;
                ++lineLengthBytes;
            }
        }

        os::user::ShellCommandLine commandLine{};
        bool valid =
            os::user::ParseShellCommandLine(
                line, lineLengthBytes, commandLine) ==
                os::user::ShellParseStatus::Succeeded &&
            commandLine.argumentCount == argumentCount;
        for (uint64_t argumentIndex = 0ULL;
             argumentIndex < argumentCount && valid; ++argumentIndex) {
            valid =
                commandLine.arguments[argumentIndex].lengthBytes ==
                expectedLengths[argumentIndex];
            const char *const actual =
                os::user::ShellArgumentBytes(commandLine, argumentIndex);
            valid = valid && actual != nullptr;
            for (uint64_t byteIndex = 0ULL;
                 byteIndex < expectedLengths[argumentIndex] && valid;
                 ++byteIndex) {
                valid = actual[byteIndex] ==
                        expected[argumentIndex][byteIndex];
            }
        }
        testContext.ExpectRandom(
            valid, OS_TEST_SHELL_RANDOM_ROUND_TRIP,
            OS_TEST_SHELL_RANDOM_SEED, iteration);

        char arbitraryLine
            [os::user::OS_USER_SHELL_MAXIMUM_LINE_SIZE_BYTES]{};
        const uint64_t arbitraryLengthBytes =
            arbitraryLengthDistribution(generator);
        for (uint64_t byteIndex = OS_TEST_SHELL_RANDOM_EMPTY_VALUE;
             byteIndex < arbitraryLengthBytes; ++byteIndex) {
            arbitraryLine[byteIndex] = static_cast<char>(
                arbitraryByteDistribution(generator));
        }
        os::user::ShellCommandLine firstCommandLine{};
        os::user::ShellCommandLine secondCommandLine{};
        const os::user::ShellParseStatus firstStatus =
            os::user::ParseShellCommandLine(
                arbitraryLine, arbitraryLengthBytes,
                firstCommandLine);
        const os::user::ShellParseStatus secondStatus =
            os::user::ParseShellCommandLine(
                arbitraryLine, arbitraryLengthBytes,
                secondCommandLine);
        const bool successfulLayoutIsValid =
            firstStatus != os::user::ShellParseStatus::Succeeded ||
            CommandLineLayoutIsValid(firstCommandLine);
        const bool nonSuccessIsAtomic =
            firstStatus == os::user::ShellParseStatus::Succeeded ||
            firstCommandLine.argumentCount ==
                OS_TEST_SHELL_RANDOM_EMPTY_VALUE;
        testContext.ExpectRandom(
            firstStatus == secondStatus &&
                CommandLinesEqual(firstCommandLine,
                                  secondCommandLine) &&
                successfulLayoutIsValid && nonSuccessIsAtomic,
            OS_TEST_SHELL_RANDOM_ARBITRARY_INPUT,
            OS_TEST_SHELL_RANDOM_SEED, iteration);
    }

    return testContext.ExitCode();
}

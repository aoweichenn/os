#include "os/kernel/console_input.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_CONSOLE_INPUT_SUITE_NAME =
    "kernel/console_input/unit";
constexpr std::string_view OS_TEST_CONSOLE_INPUT_EMPTY =
    "空控制台必须返回 Empty 且保持零读取";
constexpr std::string_view OS_TEST_CONSOLE_INPUT_FIFO =
    "控制台输入必须按 FIFO 顺序支持部分读取和回绕";
constexpr std::string_view OS_TEST_CONSOLE_INPUT_FULL =
    "满缓冲必须拒绝覆盖旧输入并统计丢弃字节";
constexpr uint64_t OS_TEST_CONSOLE_INPUT_EMPTY_COUNT = 0ULL;
constexpr uint64_t OS_TEST_CONSOLE_INPUT_FIRST_INDEX = 0ULL;
constexpr uint64_t OS_TEST_CONSOLE_INPUT_PARTIAL_READ_SIZE_BYTES = 3ULL;
constexpr uint64_t OS_TEST_CONSOLE_INPUT_COUNTER_INCREMENT = 1ULL;
constexpr uint8_t OS_TEST_CONSOLE_INPUT_FIRST_CHARACTER = static_cast<uint8_t>('a');
constexpr uint8_t OS_TEST_CONSOLE_INPUT_SECOND_CHARACTER = static_cast<uint8_t>('b');
constexpr uint8_t OS_TEST_CONSOLE_INPUT_THIRD_CHARACTER = static_cast<uint8_t>('c');
constexpr uint8_t OS_TEST_CONSOLE_INPUT_FOURTH_CHARACTER = static_cast<uint8_t>('d');
constexpr uint8_t OS_TEST_CONSOLE_INPUT_OVERFLOW_CHARACTER = static_cast<uint8_t>('!');

}

int main() {
    os::test::TestContext testContext{OS_TEST_CONSOLE_INPUT_SUITE_NAME};
    os::kernel::ConsoleInput consoleInput{};
    consoleInput.Initialize();

    uint8_t readBuffer[OS_TEST_CONSOLE_INPUT_PARTIAL_READ_SIZE_BYTES]{};
    uint64_t readBytes = OS_TEST_CONSOLE_INPUT_PARTIAL_READ_SIZE_BYTES;
    testContext.Expect(
        consoleInput.TryRead(readBuffer, OS_TEST_CONSOLE_INPUT_PARTIAL_READ_SIZE_BYTES,
                             readBytes) == os::kernel::ConsoleInputStatus::Empty &&
            readBytes == OS_TEST_CONSOLE_INPUT_EMPTY_COUNT &&
            !consoleInput.ReadCanProgress(),
        OS_TEST_CONSOLE_INPUT_EMPTY);

    static_cast<void>(consoleInput.Submit(OS_TEST_CONSOLE_INPUT_FIRST_CHARACTER));
    static_cast<void>(consoleInput.Submit(OS_TEST_CONSOLE_INPUT_SECOND_CHARACTER));
    static_cast<void>(consoleInput.Submit(OS_TEST_CONSOLE_INPUT_THIRD_CHARACTER));
    static_cast<void>(consoleInput.Submit(OS_TEST_CONSOLE_INPUT_FOURTH_CHARACTER));
    const bool partialRead =
        consoleInput.TryRead(readBuffer, OS_TEST_CONSOLE_INPUT_PARTIAL_READ_SIZE_BYTES,
                             readBytes) == os::kernel::ConsoleInputStatus::Succeeded &&
        readBytes == OS_TEST_CONSOLE_INPUT_PARTIAL_READ_SIZE_BYTES &&
        readBuffer[0] == OS_TEST_CONSOLE_INPUT_FIRST_CHARACTER &&
        readBuffer[1] == OS_TEST_CONSOLE_INPUT_SECOND_CHARACTER &&
        readBuffer[2] == OS_TEST_CONSOLE_INPUT_THIRD_CHARACTER;
    uint8_t finalCharacter = 0U;
    const bool finalRead =
        consoleInput.TryRead(&finalCharacter, sizeof(finalCharacter), readBytes) ==
            os::kernel::ConsoleInputStatus::Succeeded &&
        readBytes == sizeof(finalCharacter) &&
        finalCharacter == OS_TEST_CONSOLE_INPUT_FOURTH_CHARACTER &&
        !consoleInput.ReadCanProgress();
    testContext.Expect(partialRead && finalRead, OS_TEST_CONSOLE_INPUT_FIFO);

    consoleInput.Initialize();
    bool filled = true;
    for (uint64_t byteIndex = OS_TEST_CONSOLE_INPUT_FIRST_INDEX;
         byteIndex < os::kernel::OS_KERNEL_CONSOLE_INPUT_CAPACITY_BYTES;
         ++byteIndex) {
        filled =
            filled &&
            consoleInput.Submit(static_cast<uint8_t>(byteIndex)) ==
                os::kernel::ConsoleInputStatus::Succeeded;
    }
    const os::kernel::ConsoleInputStatistics fullStatistics =
        consoleInput.Statistics();
    const bool overflowRejected =
        consoleInput.Submit(OS_TEST_CONSOLE_INPUT_OVERFLOW_CHARACTER) ==
            os::kernel::ConsoleInputStatus::Full;
    const os::kernel::ConsoleInputStatistics overflowStatistics =
        consoleInput.Statistics();
    testContext.Expect(
        filled && fullStatistics.bufferedByteCount ==
                      os::kernel::OS_KERNEL_CONSOLE_INPUT_CAPACITY_BYTES &&
            overflowRejected &&
            overflowStatistics.bufferedByteCount ==
                os::kernel::OS_KERNEL_CONSOLE_INPUT_CAPACITY_BYTES &&
            overflowStatistics.droppedByteCount ==
                OS_TEST_CONSOLE_INPUT_COUNTER_INCREMENT,
        OS_TEST_CONSOLE_INPUT_FULL);

    return testContext.ExitCode();
}

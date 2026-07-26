#include "os/kernel/io/console_input.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_CONSOLE_INPUT_SUITE_NAME = "kernel/console_input/unit";
constexpr std::string_view OS_TEST_CONSOLE_INPUT_EMPTY = "空控制台必须返回 Empty 且保持零读取";
constexpr std::string_view OS_TEST_CONSOLE_INPUT_FIFO =
    "控制台输入必须按 FIFO 顺序支持部分读取和回绕";
constexpr std::string_view OS_TEST_CONSOLE_INPUT_FULL = "满缓冲必须拒绝覆盖旧输入并统计丢弃字节";
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
    os::test::TestContext test_context{OS_TEST_CONSOLE_INPUT_SUITE_NAME};
    os::kernel::ConsoleInput console_input{};
    console_input.Initialize();

    uint8_t read_buffer[OS_TEST_CONSOLE_INPUT_PARTIAL_READ_SIZE_BYTES]{};
    uint64_t read_bytes = OS_TEST_CONSOLE_INPUT_PARTIAL_READ_SIZE_BYTES;
    test_context.Expect(
        console_input.TryRead(read_buffer, OS_TEST_CONSOLE_INPUT_PARTIAL_READ_SIZE_BYTES,
                              read_bytes) == os::kernel::ConsoleInputStatus::Empty &&
            read_bytes == OS_TEST_CONSOLE_INPUT_EMPTY_COUNT && !console_input.ReadCanProgress(),
        OS_TEST_CONSOLE_INPUT_EMPTY);

    static_cast<void>(console_input.Submit(OS_TEST_CONSOLE_INPUT_FIRST_CHARACTER));
    static_cast<void>(console_input.Submit(OS_TEST_CONSOLE_INPUT_SECOND_CHARACTER));
    static_cast<void>(console_input.Submit(OS_TEST_CONSOLE_INPUT_THIRD_CHARACTER));
    static_cast<void>(console_input.Submit(OS_TEST_CONSOLE_INPUT_FOURTH_CHARACTER));
    const bool partial_read =
        console_input.TryRead(read_buffer, OS_TEST_CONSOLE_INPUT_PARTIAL_READ_SIZE_BYTES,
                              read_bytes) == os::kernel::ConsoleInputStatus::Succeeded &&
        read_bytes == OS_TEST_CONSOLE_INPUT_PARTIAL_READ_SIZE_BYTES &&
        read_buffer[0] == OS_TEST_CONSOLE_INPUT_FIRST_CHARACTER &&
        read_buffer[1] == OS_TEST_CONSOLE_INPUT_SECOND_CHARACTER &&
        read_buffer[2] == OS_TEST_CONSOLE_INPUT_THIRD_CHARACTER;
    uint8_t final_character = 0U;
    const bool final_read =
        console_input.TryRead(&final_character, sizeof(final_character), read_bytes) ==
            os::kernel::ConsoleInputStatus::Succeeded &&
        read_bytes == sizeof(final_character) &&
        final_character == OS_TEST_CONSOLE_INPUT_FOURTH_CHARACTER &&
        !console_input.ReadCanProgress();
    test_context.Expect(partial_read && final_read, OS_TEST_CONSOLE_INPUT_FIFO);

    console_input.Initialize();
    bool filled = true;
    for (uint64_t byte_index = OS_TEST_CONSOLE_INPUT_FIRST_INDEX;
         byte_index < os::kernel::OS_KERNEL_CONSOLE_INPUT_CAPACITY_BYTES; ++byte_index) {
        filled = filled && console_input.Submit(static_cast<uint8_t>(byte_index)) ==
                               os::kernel::ConsoleInputStatus::Succeeded;
    }
    const os::kernel::ConsoleInputStatistics full_statistics = console_input.Statistics();
    const bool overflow_rejected = console_input.Submit(OS_TEST_CONSOLE_INPUT_OVERFLOW_CHARACTER) ==
                                   os::kernel::ConsoleInputStatus::Full;
    const os::kernel::ConsoleInputStatistics overflow_statistics = console_input.Statistics();
    test_context.Expect(filled &&
                            full_statistics.buffered_byte_count ==
                                os::kernel::OS_KERNEL_CONSOLE_INPUT_CAPACITY_BYTES &&
                            overflow_rejected &&
                            overflow_statistics.buffered_byte_count ==
                                os::kernel::OS_KERNEL_CONSOLE_INPUT_CAPACITY_BYTES &&
                            overflow_statistics.dropped_byte_count ==
                                OS_TEST_CONSOLE_INPUT_COUNTER_INCREMENT,
                        OS_TEST_CONSOLE_INPUT_FULL);

    return test_context.ExitCode();
}

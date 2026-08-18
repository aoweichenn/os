#include "os/kernel/io/terminal.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_TERMINAL_SUITE_NAME = "kernel/terminal/unit";
constexpr std::string_view OS_TEST_TERMINAL_CANONICAL =
    "规范模式必须在换行前隐藏编辑缓冲，并提交退格后的完整行";
constexpr std::string_view OS_TEST_TERMINAL_CONTROL =
    "Ctrl-C、Ctrl-Z 与 Ctrl-D 必须产生控制动作而不混入普通输入";
constexpr std::string_view OS_TEST_TERMINAL_OWNERSHIP =
    "控制终端必须只允许同一会话的前台进程组读取";
constexpr std::string_view OS_TEST_TERMINAL_EDITOR_MODE =
    "Shell 编辑模式必须只允许前台组切换并逐字节无回显提交";
constexpr std::string_view OS_TEST_TERMINAL_OUTPUT = "终端输出队列必须完整排空到设备并保持统计守恒";
constexpr uint64_t OS_TEST_TERMINAL_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_TERMINAL_SESSION_ID = 11ULL;
constexpr uint64_t OS_TEST_TERMINAL_FOREGROUND_GROUP_ID = 12ULL;
constexpr uint64_t OS_TEST_TERMINAL_BACKGROUND_GROUP_ID = 13ULL;
constexpr uint8_t OS_TEST_TERMINAL_FIRST_CHARACTER = static_cast<uint8_t>('a');
constexpr uint8_t OS_TEST_TERMINAL_SECOND_CHARACTER = static_cast<uint8_t>('b');
constexpr uint8_t OS_TEST_TERMINAL_THIRD_CHARACTER = static_cast<uint8_t>('c');
constexpr uint8_t OS_TEST_TERMINAL_REPLACEMENT_CHARACTER = static_cast<uint8_t>('d');
constexpr uint64_t OS_TEST_TERMINAL_EXPECTED_LINE_SIZE_BYTES = 4ULL;
constexpr uint64_t OS_TEST_TERMINAL_OUTPUT_SIZE_BYTES = 5ULL;
constexpr uint64_t OS_TEST_TERMINAL_EXPECTED_SUBMITTED_BYTE_COUNT = 10ULL;
constexpr uint64_t OS_TEST_TERMINAL_EXPECTED_CONSUMED_BYTE_COUNT = 5ULL;
constexpr uint64_t OS_TEST_TERMINAL_EXPECTED_READ_BYTE_COUNT = 5ULL;

struct OutputCapture final {
    uint8_t bytes[OS_TEST_TERMINAL_OUTPUT_SIZE_BYTES];
    uint64_t written_bytes;
};

[[nodiscard]] bool CaptureOutput(void *const context, const uint8_t *const source,
                                 const uint64_t length_bytes, uint64_t &written_bytes) noexcept {
    written_bytes = OS_TEST_TERMINAL_EMPTY_VALUE;
    if (context == nullptr || source == nullptr) {
        return false;
    }
    OutputCapture &capture = *static_cast<OutputCapture *>(context);
    if (length_bytes > OS_TEST_TERMINAL_OUTPUT_SIZE_BYTES - capture.written_bytes) {
        return false;
    }
    for (uint64_t byte_index = OS_TEST_TERMINAL_EMPTY_VALUE; byte_index < length_bytes;
         ++byte_index) {
        capture.bytes[capture.written_bytes + byte_index] = source[byte_index];
    }
    capture.written_bytes += length_bytes;
    written_bytes = length_bytes;
    return true;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_TERMINAL_SUITE_NAME};
    os::kernel::Terminal terminal{};
    terminal.Initialize(os::kernel::OS_KERNEL_TERMINAL_IDENTIFIER);

    os::kernel::TerminalInputAction action = os::kernel::TerminalInputAction::None;
    static_cast<void>(terminal.SubmitCharacter(OS_TEST_TERMINAL_FIRST_CHARACTER, action));
    static_cast<void>(terminal.SubmitCharacter(OS_TEST_TERMINAL_SECOND_CHARACTER, action));
    static_cast<void>(terminal.SubmitCharacter(OS_TEST_TERMINAL_THIRD_CHARACTER, action));
    static_cast<void>(
        terminal.SubmitCharacter(os::kernel::OS_KERNEL_TERMINAL_BACKSPACE_CHARACTER, action));
    static_cast<void>(terminal.SubmitCharacter(OS_TEST_TERMINAL_REPLACEMENT_CHARACTER, action));
    uint8_t line[OS_TEST_TERMINAL_EXPECTED_LINE_SIZE_BYTES]{};
    uint64_t read_bytes = OS_TEST_TERMINAL_EMPTY_VALUE;
    const bool hidden_before_newline =
        terminal.TryRead(line, sizeof(line), read_bytes) == os::kernel::TerminalStatus::Empty;
    const bool line_ready =
        terminal.SubmitCharacter(os::kernel::OS_KERNEL_TERMINAL_NEWLINE_CHARACTER, action) ==
            os::kernel::TerminalStatus::Succeeded &&
        action == os::kernel::TerminalInputAction::InputReady;
    const bool canonical_line =
        terminal.TryRead(line, sizeof(line), read_bytes) == os::kernel::TerminalStatus::Succeeded &&
        read_bytes == OS_TEST_TERMINAL_EXPECTED_LINE_SIZE_BYTES &&
        line[0] == OS_TEST_TERMINAL_FIRST_CHARACTER &&
        line[1] == OS_TEST_TERMINAL_SECOND_CHARACTER &&
        line[2] == OS_TEST_TERMINAL_REPLACEMENT_CHARACTER &&
        line[3] == os::kernel::OS_KERNEL_TERMINAL_NEWLINE_CHARACTER;
    test_context.Expect(hidden_before_newline && line_ready && canonical_line,
                        OS_TEST_TERMINAL_CANONICAL);

    const bool interrupt =
        terminal.SubmitCharacter(os::kernel::OS_KERNEL_TERMINAL_INTERRUPT_CHARACTER, action) ==
            os::kernel::TerminalStatus::Succeeded &&
        action == os::kernel::TerminalInputAction::InterruptForeground;
    const bool stop = terminal.SubmitCharacter(os::kernel::OS_KERNEL_TERMINAL_STOP_CHARACTER,
                                               action) == os::kernel::TerminalStatus::Succeeded &&
                      action == os::kernel::TerminalInputAction::StopForeground;
    const bool end_of_file =
        terminal.SubmitCharacter(os::kernel::OS_KERNEL_TERMINAL_END_OF_FILE_CHARACTER, action) ==
            os::kernel::TerminalStatus::Succeeded &&
        action == os::kernel::TerminalInputAction::EndOfFileReady &&
        terminal.TryRead(line, sizeof(line), read_bytes) == os::kernel::TerminalStatus::EndOfFile;
    test_context.Expect(interrupt && stop && end_of_file, OS_TEST_TERMINAL_CONTROL);

    const bool acquired =
        terminal.AcquireControllingSession(OS_TEST_TERMINAL_SESSION_ID, OS_TEST_TERMINAL_SESSION_ID,
                                           OS_TEST_TERMINAL_FOREGROUND_GROUP_ID) ==
        os::kernel::TerminalStatus::Succeeded;
    const bool foreground_allowed =
        terminal.CanRead(OS_TEST_TERMINAL_SESSION_ID, OS_TEST_TERMINAL_FOREGROUND_GROUP_ID);
    const bool background_rejected =
        !terminal.CanRead(OS_TEST_TERMINAL_SESSION_ID, OS_TEST_TERMINAL_BACKGROUND_GROUP_ID);
    const bool changed =
        terminal.SetForegroundProcessGroup(OS_TEST_TERMINAL_SESSION_ID,
                                           OS_TEST_TERMINAL_BACKGROUND_GROUP_ID) ==
            os::kernel::TerminalStatus::Succeeded &&
        terminal.CanRead(OS_TEST_TERMINAL_SESSION_ID, OS_TEST_TERMINAL_BACKGROUND_GROUP_ID);
    test_context.Expect(acquired && foreground_allowed && background_rejected && changed,
                        OS_TEST_TERMINAL_OWNERSHIP);

    const bool editor_mode =
        terminal.SetInputMode(OS_TEST_TERMINAL_SESSION_ID, OS_TEST_TERMINAL_FOREGROUND_GROUP_ID,
                              os::abi::TerminalInputMode::ShellEditor) ==
            os::kernel::TerminalStatus::PermissionDenied &&
        terminal.SetInputMode(OS_TEST_TERMINAL_SESSION_ID, OS_TEST_TERMINAL_BACKGROUND_GROUP_ID,
                              os::abi::TerminalInputMode::ShellEditor) ==
            os::kernel::TerminalStatus::Succeeded &&
        terminal.SubmitCharacter(os::kernel::OS_KERNEL_TERMINAL_BACKSPACE_CHARACTER, action) ==
            os::kernel::TerminalStatus::Succeeded &&
        action == os::kernel::TerminalInputAction::InputReadyNoEcho &&
        terminal.TryRead(line, sizeof(line), read_bytes) == os::kernel::TerminalStatus::Succeeded &&
        read_bytes == 1ULL && line[0] == os::kernel::OS_KERNEL_TERMINAL_BACKSPACE_CHARACTER &&
        terminal.SetInputMode(OS_TEST_TERMINAL_SESSION_ID, OS_TEST_TERMINAL_BACKGROUND_GROUP_ID,
                              os::abi::TerminalInputMode::Canonical) ==
            os::kernel::TerminalStatus::Succeeded;
    test_context.Expect(editor_mode, OS_TEST_TERMINAL_EDITOR_MODE);

    const uint8_t output[OS_TEST_TERMINAL_OUTPUT_SIZE_BYTES]{
        static_cast<uint8_t>('h'), static_cast<uint8_t>('e'), static_cast<uint8_t>('l'),
        static_cast<uint8_t>('l'), static_cast<uint8_t>('o')};
    OutputCapture capture{};
    uint64_t written_bytes = OS_TEST_TERMINAL_EMPTY_VALUE;
    const bool output_written =
        terminal.TryWrite(output, sizeof(output), CaptureOutput, &capture, written_bytes) ==
            os::kernel::TerminalStatus::Succeeded &&
        written_bytes == sizeof(output) && capture.written_bytes == sizeof(output);
    const os::kernel::TerminalStatistics statistics = terminal.Statistics();
    test_context.Expect(
        output_written &&
            statistics.submitted_byte_count == OS_TEST_TERMINAL_EXPECTED_SUBMITTED_BYTE_COUNT &&
            statistics.read_byte_count == OS_TEST_TERMINAL_EXPECTED_READ_BYTE_COUNT &&
            statistics.consumed_byte_count == OS_TEST_TERMINAL_EXPECTED_CONSUMED_BYTE_COUNT &&
            statistics.output_queued_byte_count == OS_TEST_TERMINAL_OUTPUT_SIZE_BYTES &&
            statistics.output_written_byte_count == OS_TEST_TERMINAL_OUTPUT_SIZE_BYTES &&
            statistics.output_pending_byte_count == OS_TEST_TERMINAL_EMPTY_VALUE &&
            terminal.Validate() == os::kernel::TerminalStatus::Succeeded,
        OS_TEST_TERMINAL_OUTPUT);
    return test_context.ExitCode();
}

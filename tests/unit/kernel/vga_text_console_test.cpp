#include <os/kernel/device/vga_text_console.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_VGA_CONSOLE_SUITE_NAME = "kernel/vga_text_console/unit";
constexpr std::string_view OS_TEST_VGA_CONSOLE_RENDERING =
    "启动诊断必须渲染字符并把相同字节追加到日志区";
constexpr std::string_view OS_TEST_VGA_CONSOLE_CURSOR =
    "硬件实例必须通过注入端口操作提交 CRTC 光标位置";
constexpr std::string_view OS_TEST_VGA_CONSOLE_SCROLLING = "最后一行换行必须上移文本并清空新的末行";
constexpr std::string_view OS_TEST_VGA_CONSOLE_TERMINAL_ACTIVATION =
    "激活终端必须清屏并让后续诊断只写日志、前台文本同时写屏幕";
constexpr std::string_view OS_TEST_VGA_CONSOLE_OVERFLOW =
    "日志区写满后必须拒绝伪造完整日志但仍允许 panic 写入屏幕";
constexpr std::string_view OS_TEST_VGA_CONSOLE_ANSI = "终端必须支持有界 CSI 光标、清行和清屏序列";
constexpr uint8_t OS_TEST_VGA_CONSOLE_FIRST_CHARACTER = static_cast<uint8_t>('A');
constexpr uint8_t OS_TEST_VGA_CONSOLE_SECOND_CHARACTER = static_cast<uint8_t>('B');
constexpr uint8_t OS_TEST_VGA_CONSOLE_THIRD_CHARACTER = static_cast<uint8_t>('C');
constexpr uint8_t OS_TEST_VGA_CONSOLE_SCROLL_CHARACTER = static_cast<uint8_t>('S');
constexpr uint8_t OS_TEST_VGA_CONSOLE_DIAGNOSTIC_CHARACTER = static_cast<uint8_t>('D');
constexpr uint8_t OS_TEST_VGA_CONSOLE_TERMINAL_CHARACTER = static_cast<uint8_t>('T');
constexpr uint8_t OS_TEST_VGA_CONSOLE_EMERGENCY_CHARACTER = static_cast<uint8_t>('X');
constexpr uint8_t OS_TEST_VGA_CONSOLE_SPACE_CHARACTER = static_cast<uint8_t>(' ');
constexpr uint64_t OS_TEST_VGA_CONSOLE_CELL_COUNT =
    static_cast<uint64_t>(os::kernel::OS_KERNEL_VGA_TEXT_COLUMN_COUNT) *
    os::kernel::OS_KERNEL_VGA_TEXT_ROW_COUNT;
constexpr uint64_t OS_TEST_VGA_CONSOLE_CURSOR_WRITE_COUNT = 4ULL;
constexpr uint16_t OS_TEST_VGA_CONSOLE_CRTC_INDEX_PORT = 0x03D4U;
constexpr uint16_t OS_TEST_VGA_CONSOLE_CRTC_DATA_PORT = 0x03D5U;
constexpr uint8_t OS_TEST_VGA_CONSOLE_CRTC_CURSOR_HIGH_INDEX = 0x0EU;
constexpr uint8_t OS_TEST_VGA_CONSOLE_CRTC_CURSOR_LOW_INDEX = 0x0FU;

uint16_t cursor_ports[OS_TEST_VGA_CONSOLE_CURSOR_WRITE_COUNT]{};
uint8_t cursor_values[OS_TEST_VGA_CONSOLE_CURSOR_WRITE_COUNT]{};
uint64_t cursor_write_count{};

struct VgaConsoleStorage final {
    os::kernel::VgaConsoleSharedState state;
    uint8_t trace_bytes[os::kernel::OS_KERNEL_VGA_CONSOLE_TRACE_CAPACITY_BYTES];
};

static_assert(__builtin_offsetof(VgaConsoleStorage, trace_bytes) ==
              os::kernel::OS_KERNEL_VGA_CONSOLE_TRACE_OFFSET_BYTES);

[[nodiscard]] uint8_t CharacterAt(const uint16_t cell) noexcept {
    return static_cast<uint8_t>(cell & 0x00FFU);
}

void ResetState(VgaConsoleStorage &storage) noexcept {
    storage.state = os::kernel::VgaConsoleSharedState{
        .magic = os::kernel::OS_KERNEL_VGA_CONSOLE_STATE_MAGIC,
        .version = os::kernel::OS_KERNEL_VGA_CONSOLE_STATE_VERSION,
        .trace_length_bytes = 0U,
        .trace_overflow = 0U,
        .cursor_row = 0U,
        .cursor_column = 0U,
        .attribute = os::kernel::OS_KERNEL_VGA_TEXT_DEFAULT_ATTRIBUTE,
        .output_mode = os::kernel::OS_KERNEL_VGA_CONSOLE_BOOT_OUTPUT_MODE,
        .reserved = {},
    };
}

void CaptureCursorPortWrite(const uint16_t port, const uint8_t value) noexcept {
    if (cursor_write_count < OS_TEST_VGA_CONSOLE_CURSOR_WRITE_COUNT) {
        cursor_ports[cursor_write_count] = port;
        cursor_values[cursor_write_count] = value;
    }
    ++cursor_write_count;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_VGA_CONSOLE_SUITE_NAME};
    static uint16_t text_buffer[OS_TEST_VGA_CONSOLE_CELL_COUNT]{};
    static VgaConsoleStorage storage{};
    ResetState(storage);
    const os::kernel::VgaTextConsole cursor_console{text_buffer, &storage.state,
                                                    CaptureCursorPortWrite};
    const bool cursor_initialized =
        cursor_console.Initialize() &&
        cursor_write_count == OS_TEST_VGA_CONSOLE_CURSOR_WRITE_COUNT &&
        cursor_ports[0] == OS_TEST_VGA_CONSOLE_CRTC_INDEX_PORT &&
        cursor_values[0] == OS_TEST_VGA_CONSOLE_CRTC_CURSOR_HIGH_INDEX &&
        cursor_ports[1] == OS_TEST_VGA_CONSOLE_CRTC_DATA_PORT && cursor_values[1] == 0U &&
        cursor_ports[2] == OS_TEST_VGA_CONSOLE_CRTC_INDEX_PORT &&
        cursor_values[2] == OS_TEST_VGA_CONSOLE_CRTC_CURSOR_LOW_INDEX &&
        cursor_ports[3] == OS_TEST_VGA_CONSOLE_CRTC_DATA_PORT && cursor_values[3] == 0U;
    test_context.Expect(cursor_initialized, OS_TEST_VGA_CONSOLE_CURSOR);

    const os::kernel::VgaTextConsole console{text_buffer, &storage.state, nullptr};

    const bool rendered = console.Initialize() && console.TryWriteDiagnosticString("AB\r\nC") &&
                          CharacterAt(text_buffer[0]) == OS_TEST_VGA_CONSOLE_FIRST_CHARACTER &&
                          CharacterAt(text_buffer[1]) == OS_TEST_VGA_CONSOLE_SECOND_CHARACTER &&
                          CharacterAt(text_buffer[os::kernel::OS_KERNEL_VGA_TEXT_COLUMN_COUNT]) ==
                              OS_TEST_VGA_CONSOLE_THIRD_CHARACTER &&
                          storage.state.cursor_row == 1U && storage.state.cursor_column == 1U &&
                          storage.state.trace_length_bytes == 5U &&
                          storage.trace_bytes[0] == OS_TEST_VGA_CONSOLE_FIRST_CHARACTER &&
                          storage.trace_bytes[4] == OS_TEST_VGA_CONSOLE_THIRD_CHARACTER;
    test_context.Expect(rendered, OS_TEST_VGA_CONSOLE_RENDERING);

    ResetState(storage);
    storage.state.cursor_row = static_cast<uint16_t>(os::kernel::OS_KERNEL_VGA_TEXT_ROW_COUNT - 1U);
    text_buffer[os::kernel::OS_KERNEL_VGA_TEXT_COLUMN_COUNT] =
        static_cast<uint16_t>(OS_TEST_VGA_CONSOLE_SCROLL_CHARACTER);
    const bool scrolled =
        console.TryWriteDiagnosticByte('\n') &&
        CharacterAt(text_buffer[0]) == OS_TEST_VGA_CONSOLE_SCROLL_CHARACTER &&
        CharacterAt(text_buffer[OS_TEST_VGA_CONSOLE_CELL_COUNT - 1ULL]) ==
            OS_TEST_VGA_CONSOLE_SPACE_CHARACTER &&
        storage.state.cursor_row == os::kernel::OS_KERNEL_VGA_TEXT_ROW_COUNT - 1U &&
        storage.state.cursor_column == 0U;
    test_context.Expect(scrolled, OS_TEST_VGA_CONSOLE_SCROLLING);

    ResetState(storage);
    text_buffer[0] = static_cast<uint16_t>(OS_TEST_VGA_CONSOLE_FIRST_CHARACTER);
    const bool terminal_activated =
        !console.TryWriteTerminalByte(static_cast<char>(OS_TEST_VGA_CONSOLE_TERMINAL_CHARACTER)) &&
        storage.state.trace_length_bytes == 0U && console.ActivateTerminal() &&
        storage.state.output_mode == os::kernel::OS_KERNEL_VGA_CONSOLE_TERMINAL_OUTPUT_MODE &&
        storage.state.cursor_row == 0U && storage.state.cursor_column == 0U &&
        CharacterAt(text_buffer[0]) == OS_TEST_VGA_CONSOLE_SPACE_CHARACTER &&
        console.TryWriteDiagnosticByte(
            static_cast<char>(OS_TEST_VGA_CONSOLE_DIAGNOSTIC_CHARACTER)) &&
        CharacterAt(text_buffer[0]) == OS_TEST_VGA_CONSOLE_SPACE_CHARACTER &&
        console.TryWriteTerminalByte(static_cast<char>(OS_TEST_VGA_CONSOLE_TERMINAL_CHARACTER)) &&
        CharacterAt(text_buffer[0]) == OS_TEST_VGA_CONSOLE_TERMINAL_CHARACTER &&
        storage.state.trace_length_bytes == 2U &&
        storage.trace_bytes[0] == OS_TEST_VGA_CONSOLE_DIAGNOSTIC_CHARACTER &&
        storage.trace_bytes[1] == OS_TEST_VGA_CONSOLE_TERMINAL_CHARACTER;
    test_context.Expect(terminal_activated, OS_TEST_VGA_CONSOLE_TERMINAL_ACTIVATION);

    ResetState(storage);
    storage.state.output_mode = os::kernel::OS_KERNEL_VGA_CONSOLE_TERMINAL_OUTPUT_MODE;
    const bool ansi_valid =
        console.Initialize() && console.TryWriteTerminalString("AB\x1b[DX") &&
        CharacterAt(text_buffer[0]) == OS_TEST_VGA_CONSOLE_FIRST_CHARACTER &&
        CharacterAt(text_buffer[1]) == static_cast<uint8_t>('X') &&
        storage.state.cursor_column == 2U && console.TryWriteTerminalString("\r\x1b[2K") &&
        CharacterAt(text_buffer[0]) == OS_TEST_VGA_CONSOLE_SPACE_CHARACTER &&
        CharacterAt(text_buffer[1]) == OS_TEST_VGA_CONSOLE_SPACE_CHARACTER &&
        storage.state.cursor_column == 0U && console.TryWriteTerminalString("T\x1b[2J\x1b[H") &&
        CharacterAt(text_buffer[0]) == OS_TEST_VGA_CONSOLE_SPACE_CHARACTER &&
        storage.state.cursor_row == 0U && storage.state.cursor_column == 0U;
    test_context.Expect(ansi_valid, OS_TEST_VGA_CONSOLE_ANSI);

    ResetState(storage);
    storage.state.output_mode = os::kernel::OS_KERNEL_VGA_CONSOLE_TERMINAL_OUTPUT_MODE;
    storage.state.trace_length_bytes =
        static_cast<uint32_t>(os::kernel::OS_KERNEL_VGA_CONSOLE_TRACE_CAPACITY_BYTES);
    const bool overflow_rejected =
        !console.TryWriteDiagnosticByte(
            static_cast<char>(OS_TEST_VGA_CONSOLE_EMERGENCY_CHARACTER)) &&
        storage.state.trace_overflow == 1U &&
        storage.state.trace_length_bytes ==
            os::kernel::OS_KERNEL_VGA_CONSOLE_TRACE_CAPACITY_BYTES &&
        console.TryWriteEmergencyByte(static_cast<char>(OS_TEST_VGA_CONSOLE_EMERGENCY_CHARACTER)) &&
        CharacterAt(text_buffer[0]) == OS_TEST_VGA_CONSOLE_EMERGENCY_CHARACTER;
    test_context.Expect(overflow_rejected, OS_TEST_VGA_CONSOLE_OVERFLOW);
    return test_context.ExitCode();
}

#include <os/kernel/device/vga_text_console.hpp>

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_VGA_TEXT_CELL_COUNT =
    static_cast<uint64_t>(OS_KERNEL_VGA_TEXT_COLUMN_COUNT) * OS_KERNEL_VGA_TEXT_ROW_COUNT;
constexpr uint8_t OS_KERNEL_VGA_TEXT_SPACE_CHARACTER = 0x20U;
constexpr uint8_t OS_KERNEL_VGA_TEXT_FIRST_PRINTABLE_CHARACTER = 0x20U;
constexpr uint8_t OS_KERNEL_VGA_TEXT_LAST_PRINTABLE_CHARACTER = 0x7EU;
constexpr uint8_t OS_KERNEL_VGA_TEXT_CARRIAGE_RETURN_CHARACTER = 0x0DU;
constexpr uint8_t OS_KERNEL_VGA_TEXT_LINE_FEED_CHARACTER = 0x0AU;
constexpr uint8_t OS_KERNEL_VGA_TEXT_BACKSPACE_CHARACTER = 0x08U;
constexpr uint8_t OS_KERNEL_VGA_TEXT_TAB_CHARACTER = 0x09U;
constexpr uint16_t OS_KERNEL_VGA_TEXT_TAB_WIDTH = 8U;
constexpr uint16_t OS_KERNEL_VGA_CRTC_INDEX_PORT = 0x03D4U;
constexpr uint16_t OS_KERNEL_VGA_CRTC_DATA_PORT = 0x03D5U;
constexpr uint8_t OS_KERNEL_VGA_CRTC_CURSOR_HIGH_INDEX = 0x0EU;
constexpr uint8_t OS_KERNEL_VGA_CRTC_CURSOR_LOW_INDEX = 0x0FU;
constexpr uint64_t OS_KERNEL_VGA_HEXADECIMAL_DIGIT_COUNT = 16ULL;
constexpr uint64_t OS_KERNEL_VGA_HEXADECIMAL_BITS_PER_DIGIT = 4ULL;
constexpr uint64_t OS_KERNEL_VGA_HEXADECIMAL_DIGIT_MASK = 0x0FULL;
constexpr char OS_KERNEL_VGA_HEXADECIMAL_DIGITS[] = "0123456789ABCDEF";
constexpr char OS_KERNEL_VGA_HEXADECIMAL_PREFIX[] = "0x";
constexpr char OS_KERNEL_VGA_LINE_ENDING[] = "\r\n";

[[nodiscard]] uint16_t Cell(const uint8_t character, const uint8_t attribute) noexcept {
    return static_cast<uint16_t>(static_cast<uint16_t>(attribute) << 8U) |
           static_cast<uint16_t>(character);
}

}

VgaTextConsole::VgaTextConsole(volatile uint16_t *const text_buffer,
                               volatile VgaConsoleSharedState *const shared_state,
                               const VgaTextPortWriteOperation port_write_operation) noexcept
    : text_buffer_{text_buffer}, shared_state_{shared_state},
      port_write_operation_{port_write_operation} {}

VgaTextConsole
VgaTextConsole::Hardware(const VgaTextPortWriteOperation port_write_operation) noexcept {
    return VgaTextConsole{
        reinterpret_cast<volatile uint16_t *>(OS_KERNEL_VGA_TEXT_BUFFER_PHYSICAL_ADDRESS),
        reinterpret_cast<volatile VgaConsoleSharedState *>(
            OS_KERNEL_VGA_CONSOLE_STATE_PHYSICAL_ADDRESS),
        port_write_operation,
    };
}

bool VgaTextConsole::Initialize() const noexcept {
    if (!this->IsStateValid()) {
        return false;
    }
    this->UpdateHardwareCursor();
    return true;
}

bool VgaTextConsole::ActivateTerminal() const noexcept {
    if (!this->IsStateValid()) {
        return false;
    }
    this->ClearScreen();
    this->shared_state_->cursor_row = 0U;
    this->shared_state_->cursor_column = 0U;
    this->shared_state_->output_mode = OS_KERNEL_VGA_CONSOLE_TERMINAL_OUTPUT_MODE;
    this->UpdateHardwareCursor();
    return true;
}

bool VgaTextConsole::TryWriteDiagnosticByte(const char byte) const noexcept {
    if (!this->IsStateValid()) {
        return false;
    }
    const uint8_t raw_byte = static_cast<uint8_t>(byte);
    if (!this->AppendTrace(raw_byte)) {
        return false;
    }
    if (this->shared_state_->output_mode == OS_KERNEL_VGA_CONSOLE_BOOT_OUTPUT_MODE) {
        this->RenderByte(raw_byte);
    }
    return true;
}

bool VgaTextConsole::TryWriteDiagnosticString(const char *const text) const noexcept {
    return this->TryWriteStringWith(text, &VgaTextConsole::TryWriteDiagnosticByte);
}

bool VgaTextConsole::TryWriteDiagnosticHexLine(const char *const prefix,
                                               const uint64_t value) const noexcept {
    return this->TryWriteHexLineWith(prefix, value, &VgaTextConsole::TryWriteDiagnosticByte);
}

bool VgaTextConsole::TryWriteTerminalByte(const char byte) const noexcept {
    if (!this->IsStateValid() ||
        this->shared_state_->output_mode != OS_KERNEL_VGA_CONSOLE_TERMINAL_OUTPUT_MODE) {
        return false;
    }
    const uint8_t raw_byte = static_cast<uint8_t>(byte);
    if (!this->AppendTrace(raw_byte)) {
        return false;
    }
    this->RenderByte(raw_byte);
    return true;
}

bool VgaTextConsole::TryWriteTerminalString(const char *const text) const noexcept {
    return this->TryWriteStringWith(text, &VgaTextConsole::TryWriteTerminalByte);
}

bool VgaTextConsole::TryWriteEmergencyByte(const char byte) const noexcept {
    const uint8_t raw_byte = static_cast<uint8_t>(byte);
    if (!this->IsRenderStateValid()) {
        return false;
    }
    static_cast<void>(this->AppendTrace(raw_byte));
    this->RenderByte(raw_byte);
    return true;
}

bool VgaTextConsole::TryWriteEmergencyString(const char *const text) const noexcept {
    return this->TryWriteStringWith(text, &VgaTextConsole::TryWriteEmergencyByte);
}

bool VgaTextConsole::TryWriteEmergencyHexLine(const char *const prefix,
                                              const uint64_t value) const noexcept {
    return this->TryWriteHexLineWith(prefix, value, &VgaTextConsole::TryWriteEmergencyByte);
}

bool VgaTextConsole::IsStateValid() const noexcept {
    return this->IsRenderStateValid() &&
           this->shared_state_->trace_length_bytes <= OS_KERNEL_VGA_CONSOLE_TRACE_CAPACITY_BYTES &&
           this->shared_state_->trace_overflow == 0U;
}

bool VgaTextConsole::IsRenderStateValid() const noexcept {
    return this->text_buffer_ != nullptr && this->shared_state_ != nullptr &&
           this->shared_state_->magic == OS_KERNEL_VGA_CONSOLE_STATE_MAGIC &&
           this->shared_state_->version == OS_KERNEL_VGA_CONSOLE_STATE_VERSION &&
           this->shared_state_->cursor_row < OS_KERNEL_VGA_TEXT_ROW_COUNT &&
           this->shared_state_->cursor_column < OS_KERNEL_VGA_TEXT_COLUMN_COUNT &&
           this->shared_state_->output_mode <= OS_KERNEL_VGA_CONSOLE_TERMINAL_OUTPUT_MODE;
}

bool VgaTextConsole::TryWriteStringWith(const char *text,
                                        const ByteWriteOperation write_operation) const noexcept {
    if (text == nullptr || write_operation == nullptr) {
        return false;
    }
    while (*text != '\0') {
        if (!(this->*write_operation)(*text)) {
            return false;
        }
        ++text;
    }
    return true;
}

bool VgaTextConsole::TryWriteHexLineWith(const char *const prefix, const uint64_t value,
                                         const ByteWriteOperation write_operation) const noexcept {
    if (!this->TryWriteStringWith(prefix, write_operation) ||
        !this->TryWriteStringWith(OS_KERNEL_VGA_HEXADECIMAL_PREFIX, write_operation)) {
        return false;
    }
    uint64_t remaining_digit_count = OS_KERNEL_VGA_HEXADECIMAL_DIGIT_COUNT;
    while (remaining_digit_count > 0ULL) {
        --remaining_digit_count;
        const uint64_t shift_bit_count =
            remaining_digit_count * OS_KERNEL_VGA_HEXADECIMAL_BITS_PER_DIGIT;
        const uint64_t digit_index =
            (value >> shift_bit_count) & OS_KERNEL_VGA_HEXADECIMAL_DIGIT_MASK;
        if (!(this->*write_operation)(OS_KERNEL_VGA_HEXADECIMAL_DIGITS[digit_index])) {
            return false;
        }
    }
    return this->TryWriteStringWith(OS_KERNEL_VGA_LINE_ENDING, write_operation);
}

bool VgaTextConsole::AppendTrace(const uint8_t byte) const noexcept {
    if (this->shared_state_ == nullptr ||
        this->shared_state_->magic != OS_KERNEL_VGA_CONSOLE_STATE_MAGIC ||
        this->shared_state_->version != OS_KERNEL_VGA_CONSOLE_STATE_VERSION) {
        return false;
    }
    const uint32_t trace_length_bytes = this->shared_state_->trace_length_bytes;
    if (trace_length_bytes >= OS_KERNEL_VGA_CONSOLE_TRACE_CAPACITY_BYTES) {
        this->shared_state_->trace_overflow = 1U;
        return false;
    }
    volatile uint8_t *const trace_bytes =
        reinterpret_cast<volatile uint8_t *>(this->shared_state_) +
        OS_KERNEL_VGA_CONSOLE_TRACE_OFFSET_BYTES;
    trace_bytes[trace_length_bytes] = byte;
    this->shared_state_->trace_length_bytes = trace_length_bytes + 1U;
    return true;
}

void VgaTextConsole::RenderByte(const uint8_t byte) const noexcept {
    if (byte == OS_KERNEL_VGA_TEXT_CARRIAGE_RETURN_CHARACTER) {
        this->shared_state_->cursor_column = 0U;
    } else if (byte == OS_KERNEL_VGA_TEXT_LINE_FEED_CHARACTER) {
        this->AdvanceLine();
    } else if (byte == OS_KERNEL_VGA_TEXT_BACKSPACE_CHARACTER) {
        const uint16_t cursor_column = this->shared_state_->cursor_column;
        if (cursor_column > 0U) {
            this->shared_state_->cursor_column = static_cast<uint16_t>(cursor_column - 1U);
            this->PutCharacter(OS_KERNEL_VGA_TEXT_SPACE_CHARACTER);
            this->shared_state_->cursor_column = static_cast<uint16_t>(cursor_column - 1U);
        }
    } else if (byte == OS_KERNEL_VGA_TEXT_TAB_CHARACTER) {
        do {
            this->PutCharacter(OS_KERNEL_VGA_TEXT_SPACE_CHARACTER);
        } while ((this->shared_state_->cursor_column % OS_KERNEL_VGA_TEXT_TAB_WIDTH) != 0U);
    } else if (byte >= OS_KERNEL_VGA_TEXT_FIRST_PRINTABLE_CHARACTER &&
               byte <= OS_KERNEL_VGA_TEXT_LAST_PRINTABLE_CHARACTER) {
        this->PutCharacter(byte);
    }
    this->UpdateHardwareCursor();
}

void VgaTextConsole::ClearScreen() const noexcept {
    const uint16_t blank_cell =
        Cell(OS_KERNEL_VGA_TEXT_SPACE_CHARACTER, this->shared_state_->attribute);
    for (uint64_t cell_index = 0ULL; cell_index < OS_KERNEL_VGA_TEXT_CELL_COUNT; ++cell_index) {
        this->text_buffer_[cell_index] = blank_cell;
    }
}

void VgaTextConsole::PutCharacter(const uint8_t character) const noexcept {
    const uint16_t cursor_row = this->shared_state_->cursor_row;
    const uint16_t cursor_column = this->shared_state_->cursor_column;
    const uint64_t cell_index =
        static_cast<uint64_t>(cursor_row) * OS_KERNEL_VGA_TEXT_COLUMN_COUNT + cursor_column;
    this->text_buffer_[cell_index] = Cell(character, this->shared_state_->attribute);
    const uint16_t next_column = static_cast<uint16_t>(cursor_column + 1U);
    if (next_column >= OS_KERNEL_VGA_TEXT_COLUMN_COUNT) {
        this->AdvanceLine();
    } else {
        this->shared_state_->cursor_column = next_column;
    }
}

void VgaTextConsole::AdvanceLine() const noexcept {
    this->shared_state_->cursor_column = 0U;
    const uint16_t next_row = static_cast<uint16_t>(this->shared_state_->cursor_row + 1U);
    if (next_row >= OS_KERNEL_VGA_TEXT_ROW_COUNT) {
        this->Scroll();
    } else {
        this->shared_state_->cursor_row = next_row;
    }
}

void VgaTextConsole::Scroll() const noexcept {
    for (uint64_t destination_cell = 0ULL;
         destination_cell < OS_KERNEL_VGA_TEXT_CELL_COUNT - OS_KERNEL_VGA_TEXT_COLUMN_COUNT;
         ++destination_cell) {
        this->text_buffer_[destination_cell] =
            this->text_buffer_[destination_cell + OS_KERNEL_VGA_TEXT_COLUMN_COUNT];
    }
    const uint16_t blank_cell =
        Cell(OS_KERNEL_VGA_TEXT_SPACE_CHARACTER, this->shared_state_->attribute);
    for (uint64_t cell_index = OS_KERNEL_VGA_TEXT_CELL_COUNT - OS_KERNEL_VGA_TEXT_COLUMN_COUNT;
         cell_index < OS_KERNEL_VGA_TEXT_CELL_COUNT; ++cell_index) {
        this->text_buffer_[cell_index] = blank_cell;
    }
    this->shared_state_->cursor_row = static_cast<uint16_t>(OS_KERNEL_VGA_TEXT_ROW_COUNT - 1U);
}

void VgaTextConsole::UpdateHardwareCursor() const noexcept {
    if (this->port_write_operation_ == nullptr) {
        return;
    }
    const uint16_t cursor_position =
        static_cast<uint16_t>(this->shared_state_->cursor_row * OS_KERNEL_VGA_TEXT_COLUMN_COUNT +
                              this->shared_state_->cursor_column);
    this->port_write_operation_(OS_KERNEL_VGA_CRTC_INDEX_PORT,
                                OS_KERNEL_VGA_CRTC_CURSOR_HIGH_INDEX);
    this->port_write_operation_(OS_KERNEL_VGA_CRTC_DATA_PORT,
                                static_cast<uint8_t>(cursor_position >> 8U));
    this->port_write_operation_(OS_KERNEL_VGA_CRTC_INDEX_PORT, OS_KERNEL_VGA_CRTC_CURSOR_LOW_INDEX);
    this->port_write_operation_(OS_KERNEL_VGA_CRTC_DATA_PORT,
                                static_cast<uint8_t>(cursor_position & 0x00FFU));
}

}

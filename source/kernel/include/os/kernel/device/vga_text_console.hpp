#pragma once

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_VGA_TEXT_BUFFER_PHYSICAL_ADDRESS = 0x00000000000B8000ULL;
inline constexpr uint64_t OS_KERNEL_VGA_CONSOLE_STATE_PHYSICAL_ADDRESS = 0x0000000000020000ULL;
inline constexpr uint32_t OS_KERNEL_VGA_CONSOLE_STATE_MAGIC = 0x4756534FU;
inline constexpr uint32_t OS_KERNEL_VGA_CONSOLE_STATE_VERSION = 3U;
inline constexpr uint64_t OS_KERNEL_VGA_CONSOLE_TRACE_OFFSET_BYTES = 32ULL;
inline constexpr uint64_t OS_KERNEL_VGA_CONSOLE_TRACE_CAPACITY_BYTES =
    0x00080000ULL - OS_KERNEL_VGA_CONSOLE_TRACE_OFFSET_BYTES;
inline constexpr uint16_t OS_KERNEL_VGA_TEXT_COLUMN_COUNT = 80U;
inline constexpr uint16_t OS_KERNEL_VGA_TEXT_ROW_COUNT = 25U;
inline constexpr uint8_t OS_KERNEL_VGA_TEXT_DEFAULT_ATTRIBUTE = 0x07U;
inline constexpr uint8_t OS_KERNEL_VGA_CONSOLE_BOOT_OUTPUT_MODE = 0U;
inline constexpr uint8_t OS_KERNEL_VGA_CONSOLE_TERMINAL_OUTPUT_MODE = 1U;

struct VgaConsoleSharedState final {
    uint32_t magic;
    uint32_t version;
    uint32_t trace_length_bytes;
    uint32_t trace_overflow;
    uint16_t cursor_row;
    uint16_t cursor_column;
    uint8_t attribute;
    uint8_t output_mode;
    uint8_t reserved[10];
};

using VgaTextPortWriteOperation = void (*)(uint16_t port, uint8_t value) noexcept;

static_assert(sizeof(VgaConsoleSharedState) == OS_KERNEL_VGA_CONSOLE_TRACE_OFFSET_BYTES);
static_assert(__builtin_offsetof(VgaConsoleSharedState, trace_length_bytes) == 8ULL);
static_assert(__builtin_offsetof(VgaConsoleSharedState, cursor_row) == 16ULL);
static_assert(__builtin_offsetof(VgaConsoleSharedState, output_mode) == 21ULL);

class VgaTextConsole final {
  public:
    VgaTextConsole(volatile uint16_t *text_buffer, volatile VgaConsoleSharedState *shared_state,
                   VgaTextPortWriteOperation port_write_operation) noexcept;

    [[nodiscard]] static VgaTextConsole
    Hardware(VgaTextPortWriteOperation port_write_operation) noexcept;
    [[nodiscard]] bool Initialize() const noexcept;
    [[nodiscard]] bool ActivateTerminal() const noexcept;
    [[nodiscard]] bool TryWriteDiagnosticByte(char byte) const noexcept;
    [[nodiscard]] bool TryWriteDiagnosticString(const char *text) const noexcept;
    [[nodiscard]] bool TryWriteDiagnosticHexLine(const char *prefix, uint64_t value) const noexcept;
    [[nodiscard]] bool TryWriteTerminalByte(char byte) const noexcept;
    [[nodiscard]] bool TryWriteTerminalString(const char *text) const noexcept;
    [[nodiscard]] bool TryWriteEmergencyByte(char byte) const noexcept;
    [[nodiscard]] bool TryWriteEmergencyString(const char *text) const noexcept;
    [[nodiscard]] bool TryWriteEmergencyHexLine(const char *prefix, uint64_t value) const noexcept;

  private:
    using ByteWriteOperation = bool (VgaTextConsole::*)(char byte) const noexcept;

    [[nodiscard]] bool IsStateValid() const noexcept;
    [[nodiscard]] bool IsRenderStateValid() const noexcept;
    [[nodiscard]] bool AppendTrace(uint8_t byte) const noexcept;
    [[nodiscard]] bool TryWriteStringWith(const char *text,
                                          ByteWriteOperation write_operation) const noexcept;
    [[nodiscard]] bool TryWriteHexLineWith(const char *prefix, uint64_t value,
                                           ByteWriteOperation write_operation) const noexcept;
    void RenderByte(uint8_t byte) const noexcept;
    void ClearScreen() const noexcept;
    void PutCharacter(uint8_t character) const noexcept;
    void AdvanceLine() const noexcept;
    void Scroll() const noexcept;
    void UpdateHardwareCursor() const noexcept;

    volatile uint16_t *text_buffer_;
    volatile VgaConsoleSharedState *shared_state_;
    VgaTextPortWriteOperation port_write_operation_;
};

}

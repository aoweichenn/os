#pragma once

#include "os/kernel/sync/spin_lock.hpp"

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_TERMINAL_IDENTIFIER = 1ULL;
inline constexpr uint64_t OS_KERNEL_TERMINAL_INPUT_CAPACITY_BYTES = 1024ULL;
inline constexpr uint64_t OS_KERNEL_TERMINAL_EDIT_CAPACITY_BYTES = 512ULL;
inline constexpr uint64_t OS_KERNEL_TERMINAL_OUTPUT_CAPACITY_BYTES = 2048ULL;
inline constexpr uint8_t OS_KERNEL_TERMINAL_INTERRUPT_CHARACTER = 0x03U;
inline constexpr uint8_t OS_KERNEL_TERMINAL_END_OF_FILE_CHARACTER = 0x04U;
inline constexpr uint8_t OS_KERNEL_TERMINAL_BACKSPACE_CHARACTER = 0x08U;
inline constexpr uint8_t OS_KERNEL_TERMINAL_NEWLINE_CHARACTER = 0x0AU;
inline constexpr uint8_t OS_KERNEL_TERMINAL_CARRIAGE_RETURN_CHARACTER = 0x0DU;
inline constexpr uint8_t OS_KERNEL_TERMINAL_STOP_CHARACTER = 0x1AU;
inline constexpr uint8_t OS_KERNEL_TERMINAL_DELETE_CHARACTER = 0x7FU;

using TerminalDeviceWriteOperation = bool (*)(void *context, const uint8_t *source,
                                              uint64_t length_bytes,
                                              uint64_t &written_bytes) noexcept;

[[nodiscard]] bool TerminalDisableInterruptsNoop() noexcept;
void TerminalRestoreInterruptsNoop(bool interrupts_were_enabled) noexcept;

enum class TerminalStatus : uint64_t {
    Succeeded,
    Empty,
    Full,
    EndOfFile,
    InvalidArgument,
    PermissionDenied,
    DeviceFailure,
};

enum class TerminalInputAction : uint64_t {
    None,
    Buffered,
    InputReady,
    EndOfFileReady,
    Erased,
    InterruptForeground,
    StopForeground,
};

struct TerminalStatistics final {
    uint64_t submitted_byte_count;
    uint64_t committed_line_count;
    uint64_t read_byte_count;
    uint64_t dropped_byte_count;
    uint64_t consumed_byte_count;
    uint64_t buffered_byte_count;
    uint64_t editing_byte_count;
    uint64_t erase_count;
    uint64_t end_of_file_count;
    uint64_t interrupt_count;
    uint64_t stop_count;
    uint64_t output_queued_byte_count;
    uint64_t output_written_byte_count;
    uint64_t output_pending_byte_count;
    uint64_t foreground_change_count;
    uint64_t rejected_background_read_count;
};

class Terminal final {
  public:
    Terminal() noexcept;

    void Initialize(
        uint64_t terminal_id,
        DisableInterruptsOperation disable_interrupts = TerminalDisableInterruptsNoop,
        RestoreInterruptsOperation restore_interrupts = TerminalRestoreInterruptsNoop) noexcept;
    [[nodiscard]] TerminalStatus SubmitCharacter(uint8_t character,
                                                 TerminalInputAction &action) noexcept;
    [[nodiscard]] TerminalStatus TryRead(uint8_t *destination, uint64_t capacity_bytes,
                                         uint64_t &read_bytes) noexcept;
    [[nodiscard]] TerminalStatus
    TryWrite(const uint8_t *source, uint64_t length_bytes,
             TerminalDeviceWriteOperation device_write_operation, void *device_write_context,
             uint64_t &written_bytes) noexcept;
    [[nodiscard]] TerminalStatus AcquireControllingSession(uint64_t session_id,
                                                           uint64_t session_leader_process_id,
                                                           uint64_t foreground_process_group_id)
        noexcept;
    [[nodiscard]] TerminalStatus SetForegroundProcessGroup(
        uint64_t caller_session_id, uint64_t foreground_process_group_id) noexcept;
    [[nodiscard]] bool CanRead(uint64_t session_id, uint64_t process_group_id) noexcept;
    [[nodiscard]] bool ReadCanProgress() const noexcept;
    [[nodiscard]] uint64_t Identifier() const noexcept;
    [[nodiscard]] uint64_t ControllingSessionId() const noexcept;
    [[nodiscard]] uint64_t ForegroundProcessGroupId() const noexcept;
    [[nodiscard]] TerminalStatistics Statistics() const noexcept;
    [[nodiscard]] TerminalStatus Validate() const noexcept;

  private:
    [[nodiscard]] bool CommitEditedBytes(bool append_newline) noexcept;
    [[nodiscard]] TerminalStatus DrainOutput(
        TerminalDeviceWriteOperation device_write_operation, void *device_write_context) noexcept;

    uint8_t input_bytes_[OS_KERNEL_TERMINAL_INPUT_CAPACITY_BYTES]{};
    uint8_t edit_bytes_[OS_KERNEL_TERMINAL_EDIT_CAPACITY_BYTES]{};
    uint8_t output_bytes_[OS_KERNEL_TERMINAL_OUTPUT_CAPACITY_BYTES]{};
    uint64_t input_read_index_{};
    uint64_t input_write_index_{};
    uint64_t edit_length_bytes_{};
    uint64_t output_read_index_{};
    uint64_t output_write_index_{};
    uint64_t terminal_id_{};
    uint64_t controlling_session_id_{};
    uint64_t session_leader_process_id_{};
    uint64_t foreground_process_group_id_{};
    TerminalStatistics statistics_{};
    mutable IrqSaveSpinLock lock_{TerminalDisableInterruptsNoop,
                                  TerminalRestoreInterruptsNoop};
    bool end_of_file_pending_{};
};

}

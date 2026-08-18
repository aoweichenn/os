#include "os/kernel/io/terminal.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_TERMINAL_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_TERMINAL_COUNTER_INCREMENT = 1ULL;

}

bool TerminalDisableInterruptsNoop() noexcept { return false; }

void TerminalRestoreInterruptsNoop(const bool interrupts_were_enabled) noexcept {
    static_cast<void>(interrupts_were_enabled);
}

Terminal::Terminal() noexcept = default;

void Terminal::Initialize(const uint64_t terminal_id,
                          const DisableInterruptsOperation disable_interrupts,
                          const RestoreInterruptsOperation restore_interrupts) noexcept {
    this->input_read_index_ = OS_KERNEL_TERMINAL_EMPTY_VALUE;
    this->input_write_index_ = OS_KERNEL_TERMINAL_EMPTY_VALUE;
    this->edit_length_bytes_ = OS_KERNEL_TERMINAL_EMPTY_VALUE;
    this->output_read_index_ = OS_KERNEL_TERMINAL_EMPTY_VALUE;
    this->output_write_index_ = OS_KERNEL_TERMINAL_EMPTY_VALUE;
    this->terminal_id_ = terminal_id;
    this->controlling_session_id_ = OS_KERNEL_TERMINAL_EMPTY_VALUE;
    this->session_leader_process_id_ = OS_KERNEL_TERMINAL_EMPTY_VALUE;
    this->foreground_process_group_id_ = OS_KERNEL_TERMINAL_EMPTY_VALUE;
    this->statistics_ = TerminalStatistics{};
    this->lock_ = IrqSaveSpinLock{
        disable_interrupts == nullptr ? TerminalDisableInterruptsNoop : disable_interrupts,
        restore_interrupts == nullptr ? TerminalRestoreInterruptsNoop : restore_interrupts,
    };
    this->end_of_file_pending_ = false;
    this->input_mode_ = os::abi::TerminalInputMode::Canonical;
}

TerminalStatus Terminal::SubmitCharacter(const uint8_t character,
                                         TerminalInputAction &action) noexcept {
    IrqSaveSpinLockGuard guard{this->lock_};
    action = TerminalInputAction::None;
    if (this->terminal_id_ == OS_KERNEL_TERMINAL_EMPTY_VALUE) {
        return TerminalStatus::InvalidArgument;
    }
    this->statistics_.submitted_byte_count += OS_KERNEL_TERMINAL_COUNTER_INCREMENT;
    if (character == OS_KERNEL_TERMINAL_INTERRUPT_CHARACTER) {
        this->statistics_.consumed_byte_count +=
            OS_KERNEL_TERMINAL_COUNTER_INCREMENT + this->edit_length_bytes_;
        this->edit_length_bytes_ = OS_KERNEL_TERMINAL_EMPTY_VALUE;
        this->statistics_.editing_byte_count = OS_KERNEL_TERMINAL_EMPTY_VALUE;
        this->statistics_.interrupt_count += OS_KERNEL_TERMINAL_COUNTER_INCREMENT;
        action = TerminalInputAction::InterruptForeground;
        return TerminalStatus::Succeeded;
    }
    if (character == OS_KERNEL_TERMINAL_STOP_CHARACTER) {
        this->statistics_.consumed_byte_count +=
            OS_KERNEL_TERMINAL_COUNTER_INCREMENT + this->edit_length_bytes_;
        this->edit_length_bytes_ = OS_KERNEL_TERMINAL_EMPTY_VALUE;
        this->statistics_.editing_byte_count = OS_KERNEL_TERMINAL_EMPTY_VALUE;
        this->statistics_.stop_count += OS_KERNEL_TERMINAL_COUNTER_INCREMENT;
        action = TerminalInputAction::StopForeground;
        return TerminalStatus::Succeeded;
    }
    if (this->input_mode_ == os::abi::TerminalInputMode::ShellEditor) {
        if (this->statistics_.buffered_byte_count >= OS_KERNEL_TERMINAL_INPUT_CAPACITY_BYTES) {
            this->statistics_.dropped_byte_count += OS_KERNEL_TERMINAL_COUNTER_INCREMENT;
            return TerminalStatus::Full;
        }
        this->input_bytes_[this->input_write_index_] = character;
        this->input_write_index_ =
            (this->input_write_index_ + OS_KERNEL_TERMINAL_COUNTER_INCREMENT) %
            OS_KERNEL_TERMINAL_INPUT_CAPACITY_BYTES;
        this->statistics_.buffered_byte_count += OS_KERNEL_TERMINAL_COUNTER_INCREMENT;
        action = TerminalInputAction::InputReadyNoEcho;
        return TerminalStatus::Succeeded;
    }
    if (character == OS_KERNEL_TERMINAL_BACKSPACE_CHARACTER ||
        character == OS_KERNEL_TERMINAL_DELETE_CHARACTER) {
        this->statistics_.consumed_byte_count += OS_KERNEL_TERMINAL_COUNTER_INCREMENT;
        if (this->edit_length_bytes_ != OS_KERNEL_TERMINAL_EMPTY_VALUE) {
            --this->edit_length_bytes_;
            this->statistics_.consumed_byte_count += OS_KERNEL_TERMINAL_COUNTER_INCREMENT;
            this->statistics_.editing_byte_count = this->edit_length_bytes_;
            this->statistics_.erase_count += OS_KERNEL_TERMINAL_COUNTER_INCREMENT;
            action = TerminalInputAction::Erased;
        }
        return TerminalStatus::Succeeded;
    }
    if (character == OS_KERNEL_TERMINAL_END_OF_FILE_CHARACTER) {
        this->statistics_.consumed_byte_count += OS_KERNEL_TERMINAL_COUNTER_INCREMENT;
        this->statistics_.end_of_file_count += OS_KERNEL_TERMINAL_COUNTER_INCREMENT;
        if (this->edit_length_bytes_ != OS_KERNEL_TERMINAL_EMPTY_VALUE) {
            if (!this->CommitEditedBytes(false)) {
                return TerminalStatus::Full;
            }
            action = TerminalInputAction::InputReady;
        } else {
            this->end_of_file_pending_ = true;
            action = TerminalInputAction::EndOfFileReady;
        }
        return TerminalStatus::Succeeded;
    }
    if (character == OS_KERNEL_TERMINAL_NEWLINE_CHARACTER ||
        character == OS_KERNEL_TERMINAL_CARRIAGE_RETURN_CHARACTER) {
        if (!this->CommitEditedBytes(true)) {
            this->statistics_.dropped_byte_count += OS_KERNEL_TERMINAL_COUNTER_INCREMENT;
            return TerminalStatus::Full;
        }
        action = TerminalInputAction::InputReady;
        return TerminalStatus::Succeeded;
    }
    if (this->edit_length_bytes_ >= OS_KERNEL_TERMINAL_EDIT_CAPACITY_BYTES) {
        this->statistics_.dropped_byte_count += OS_KERNEL_TERMINAL_COUNTER_INCREMENT;
        return TerminalStatus::Full;
    }
    this->edit_bytes_[this->edit_length_bytes_] = character;
    ++this->edit_length_bytes_;
    this->statistics_.editing_byte_count = this->edit_length_bytes_;
    action = TerminalInputAction::Buffered;
    return TerminalStatus::Succeeded;
}

TerminalStatus Terminal::TryRead(uint8_t *const destination, const uint64_t capacity_bytes,
                                 uint64_t &read_bytes) noexcept {
    IrqSaveSpinLockGuard guard{this->lock_};
    read_bytes = OS_KERNEL_TERMINAL_EMPTY_VALUE;
    if (destination == nullptr || capacity_bytes == OS_KERNEL_TERMINAL_EMPTY_VALUE) {
        return TerminalStatus::InvalidArgument;
    }
    if (this->statistics_.buffered_byte_count == OS_KERNEL_TERMINAL_EMPTY_VALUE) {
        if (this->end_of_file_pending_) {
            this->end_of_file_pending_ = false;
            return TerminalStatus::EndOfFile;
        }
        return TerminalStatus::Empty;
    }
    const uint64_t transferable_bytes = capacity_bytes < this->statistics_.buffered_byte_count
                                            ? capacity_bytes
                                            : this->statistics_.buffered_byte_count;
    while (read_bytes < transferable_bytes) {
        destination[read_bytes] = this->input_bytes_[this->input_read_index_];
        this->input_read_index_ = (this->input_read_index_ + OS_KERNEL_TERMINAL_COUNTER_INCREMENT) %
                                  OS_KERNEL_TERMINAL_INPUT_CAPACITY_BYTES;
        ++read_bytes;
    }
    this->statistics_.read_byte_count += read_bytes;
    this->statistics_.buffered_byte_count -= read_bytes;
    return TerminalStatus::Succeeded;
}

TerminalStatus Terminal::TryWrite(const uint8_t *const source, const uint64_t length_bytes,
                                  const TerminalDeviceWriteOperation device_write_operation,
                                  void *const device_write_context,
                                  uint64_t &written_bytes) noexcept {
    IrqSaveSpinLockGuard guard{this->lock_};
    written_bytes = OS_KERNEL_TERMINAL_EMPTY_VALUE;
    if ((source == nullptr && length_bytes != OS_KERNEL_TERMINAL_EMPTY_VALUE) ||
        device_write_operation == nullptr) {
        return TerminalStatus::InvalidArgument;
    }
    while (written_bytes < length_bytes) {
        if (this->statistics_.output_pending_byte_count >=
                OS_KERNEL_TERMINAL_OUTPUT_CAPACITY_BYTES &&
            this->DrainOutput(device_write_operation, device_write_context) !=
                TerminalStatus::Succeeded) {
            return TerminalStatus::DeviceFailure;
        }
        const uint64_t free_bytes =
            OS_KERNEL_TERMINAL_OUTPUT_CAPACITY_BYTES - this->statistics_.output_pending_byte_count;
        const uint64_t remaining_bytes = length_bytes - written_bytes;
        const uint64_t queue_bytes = remaining_bytes < free_bytes ? remaining_bytes : free_bytes;
        for (uint64_t byte_index = OS_KERNEL_TERMINAL_EMPTY_VALUE; byte_index < queue_bytes;
             ++byte_index) {
            this->output_bytes_[this->output_write_index_] = source[written_bytes + byte_index];
            this->output_write_index_ =
                (this->output_write_index_ + OS_KERNEL_TERMINAL_COUNTER_INCREMENT) %
                OS_KERNEL_TERMINAL_OUTPUT_CAPACITY_BYTES;
        }
        written_bytes += queue_bytes;
        this->statistics_.output_queued_byte_count += queue_bytes;
        this->statistics_.output_pending_byte_count += queue_bytes;
        if (this->DrainOutput(device_write_operation, device_write_context) !=
            TerminalStatus::Succeeded) {
            return TerminalStatus::DeviceFailure;
        }
    }
    return TerminalStatus::Succeeded;
}

TerminalStatus
Terminal::AcquireControllingSession(const uint64_t session_id,
                                    const uint64_t session_leader_process_id,
                                    const uint64_t foreground_process_group_id) noexcept {
    IrqSaveSpinLockGuard guard{this->lock_};
    if (session_id == OS_KERNEL_TERMINAL_EMPTY_VALUE || session_leader_process_id != session_id ||
        foreground_process_group_id == OS_KERNEL_TERMINAL_EMPTY_VALUE) {
        return TerminalStatus::InvalidArgument;
    }
    if (this->controlling_session_id_ != OS_KERNEL_TERMINAL_EMPTY_VALUE) {
        return TerminalStatus::PermissionDenied;
    }
    this->controlling_session_id_ = session_id;
    this->session_leader_process_id_ = session_leader_process_id;
    this->foreground_process_group_id_ = foreground_process_group_id;
    this->statistics_.foreground_change_count += OS_KERNEL_TERMINAL_COUNTER_INCREMENT;
    return TerminalStatus::Succeeded;
}

TerminalStatus
Terminal::SetForegroundProcessGroup(const uint64_t caller_session_id,
                                    const uint64_t foreground_process_group_id) noexcept {
    IrqSaveSpinLockGuard guard{this->lock_};
    if (caller_session_id == OS_KERNEL_TERMINAL_EMPTY_VALUE ||
        foreground_process_group_id == OS_KERNEL_TERMINAL_EMPTY_VALUE) {
        return TerminalStatus::InvalidArgument;
    }
    if (caller_session_id != this->controlling_session_id_) {
        return TerminalStatus::PermissionDenied;
    }
    this->foreground_process_group_id_ = foreground_process_group_id;
    this->statistics_.foreground_change_count += OS_KERNEL_TERMINAL_COUNTER_INCREMENT;
    return TerminalStatus::Succeeded;
}

TerminalStatus Terminal::SetInputMode(const uint64_t caller_session_id,
                                      const uint64_t caller_process_group_id,
                                      const os::abi::TerminalInputMode mode) noexcept {
    IrqSaveSpinLockGuard guard{this->lock_};
    if (mode != os::abi::TerminalInputMode::Canonical &&
        mode != os::abi::TerminalInputMode::ShellEditor) {
        return TerminalStatus::InvalidArgument;
    }
    if (caller_session_id == OS_KERNEL_TERMINAL_EMPTY_VALUE ||
        caller_session_id != this->controlling_session_id_ ||
        caller_process_group_id != this->foreground_process_group_id_) {
        return TerminalStatus::PermissionDenied;
    }
    if (this->statistics_.buffered_byte_count != OS_KERNEL_TERMINAL_EMPTY_VALUE ||
        this->edit_length_bytes_ != OS_KERNEL_TERMINAL_EMPTY_VALUE || this->end_of_file_pending_) {
        return TerminalStatus::InvalidArgument;
    }
    this->input_mode_ = mode;
    return TerminalStatus::Succeeded;
}

bool Terminal::CanRead(const uint64_t session_id, const uint64_t process_group_id) noexcept {
    IrqSaveSpinLockGuard guard{this->lock_};
    const bool can_read = session_id != OS_KERNEL_TERMINAL_EMPTY_VALUE &&
                          session_id == this->controlling_session_id_ &&
                          process_group_id == this->foreground_process_group_id_;
    if (!can_read) {
        this->statistics_.rejected_background_read_count += OS_KERNEL_TERMINAL_COUNTER_INCREMENT;
    }
    return can_read;
}

bool Terminal::ReadCanProgress() const noexcept {
    IrqSaveSpinLockGuard guard{this->lock_};
    return this->statistics_.buffered_byte_count != OS_KERNEL_TERMINAL_EMPTY_VALUE ||
           this->end_of_file_pending_;
}

uint64_t Terminal::Identifier() const noexcept {
    IrqSaveSpinLockGuard guard{this->lock_};
    return this->terminal_id_;
}

uint64_t Terminal::ControllingSessionId() const noexcept {
    IrqSaveSpinLockGuard guard{this->lock_};
    return this->controlling_session_id_;
}

uint64_t Terminal::ForegroundProcessGroupId() const noexcept {
    IrqSaveSpinLockGuard guard{this->lock_};
    return this->foreground_process_group_id_;
}

TerminalStatistics Terminal::Statistics() const noexcept {
    IrqSaveSpinLockGuard guard{this->lock_};
    return this->statistics_;
}

TerminalStatus Terminal::Validate() const noexcept {
    IrqSaveSpinLockGuard guard{this->lock_};
    if (this->terminal_id_ == OS_KERNEL_TERMINAL_EMPTY_VALUE ||
        this->input_read_index_ >= OS_KERNEL_TERMINAL_INPUT_CAPACITY_BYTES ||
        this->input_write_index_ >= OS_KERNEL_TERMINAL_INPUT_CAPACITY_BYTES ||
        this->edit_length_bytes_ > OS_KERNEL_TERMINAL_EDIT_CAPACITY_BYTES ||
        this->statistics_.editing_byte_count != this->edit_length_bytes_ ||
        this->statistics_.buffered_byte_count > OS_KERNEL_TERMINAL_INPUT_CAPACITY_BYTES ||
        this->output_read_index_ >= OS_KERNEL_TERMINAL_OUTPUT_CAPACITY_BYTES ||
        this->output_write_index_ >= OS_KERNEL_TERMINAL_OUTPUT_CAPACITY_BYTES ||
        this->statistics_.output_pending_byte_count > OS_KERNEL_TERMINAL_OUTPUT_CAPACITY_BYTES ||
        this->statistics_.read_byte_count + this->statistics_.buffered_byte_count +
                this->statistics_.editing_byte_count + this->statistics_.dropped_byte_count +
                this->statistics_.consumed_byte_count !=
            this->statistics_.submitted_byte_count ||
        this->statistics_.output_written_byte_count + this->statistics_.output_pending_byte_count !=
            this->statistics_.output_queued_byte_count ||
        ((this->controlling_session_id_ == OS_KERNEL_TERMINAL_EMPTY_VALUE) !=
         (this->session_leader_process_id_ == OS_KERNEL_TERMINAL_EMPTY_VALUE)) ||
        (this->controlling_session_id_ == OS_KERNEL_TERMINAL_EMPTY_VALUE &&
         this->foreground_process_group_id_ != OS_KERNEL_TERMINAL_EMPTY_VALUE) ||
        (this->input_mode_ != os::abi::TerminalInputMode::Canonical &&
         this->input_mode_ != os::abi::TerminalInputMode::ShellEditor)) {
        return TerminalStatus::InvalidArgument;
    }
    return TerminalStatus::Succeeded;
}

bool Terminal::CommitEditedBytes(const bool append_newline) noexcept {
    const uint64_t required_bytes =
        this->edit_length_bytes_ +
        (append_newline ? OS_KERNEL_TERMINAL_COUNTER_INCREMENT : OS_KERNEL_TERMINAL_EMPTY_VALUE);
    if (required_bytes >
        OS_KERNEL_TERMINAL_INPUT_CAPACITY_BYTES - this->statistics_.buffered_byte_count) {
        return false;
    }
    for (uint64_t byte_index = OS_KERNEL_TERMINAL_EMPTY_VALUE;
         byte_index < this->edit_length_bytes_; ++byte_index) {
        this->input_bytes_[this->input_write_index_] = this->edit_bytes_[byte_index];
        this->input_write_index_ =
            (this->input_write_index_ + OS_KERNEL_TERMINAL_COUNTER_INCREMENT) %
            OS_KERNEL_TERMINAL_INPUT_CAPACITY_BYTES;
    }
    if (append_newline) {
        this->input_bytes_[this->input_write_index_] = OS_KERNEL_TERMINAL_NEWLINE_CHARACTER;
        this->input_write_index_ =
            (this->input_write_index_ + OS_KERNEL_TERMINAL_COUNTER_INCREMENT) %
            OS_KERNEL_TERMINAL_INPUT_CAPACITY_BYTES;
        this->statistics_.committed_line_count += OS_KERNEL_TERMINAL_COUNTER_INCREMENT;
    }
    this->statistics_.buffered_byte_count += required_bytes;
    this->edit_length_bytes_ = OS_KERNEL_TERMINAL_EMPTY_VALUE;
    this->statistics_.editing_byte_count = OS_KERNEL_TERMINAL_EMPTY_VALUE;
    return true;
}

TerminalStatus Terminal::DrainOutput(const TerminalDeviceWriteOperation device_write_operation,
                                     void *const device_write_context) noexcept {
    while (this->statistics_.output_pending_byte_count != OS_KERNEL_TERMINAL_EMPTY_VALUE) {
        const uint64_t contiguous_bytes =
            this->output_read_index_ < this->output_write_index_
                ? this->output_write_index_ - this->output_read_index_
                : OS_KERNEL_TERMINAL_OUTPUT_CAPACITY_BYTES - this->output_read_index_;
        uint64_t device_written_bytes = OS_KERNEL_TERMINAL_EMPTY_VALUE;
        if (!device_write_operation(device_write_context,
                                    this->output_bytes_ + this->output_read_index_,
                                    contiguous_bytes, device_written_bytes) ||
            device_written_bytes == OS_KERNEL_TERMINAL_EMPTY_VALUE ||
            device_written_bytes > contiguous_bytes) {
            return TerminalStatus::DeviceFailure;
        }
        this->output_read_index_ = (this->output_read_index_ + device_written_bytes) %
                                   OS_KERNEL_TERMINAL_OUTPUT_CAPACITY_BYTES;
        this->statistics_.output_written_byte_count += device_written_bytes;
        this->statistics_.output_pending_byte_count -= device_written_bytes;
    }
    return TerminalStatus::Succeeded;
}

}

#include "os/kernel/io/console_input.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_CONSOLE_INPUT_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_CONSOLE_INPUT_COUNTER_INCREMENT = 1ULL;

}

void ConsoleInput::Initialize() noexcept {
    for (uint64_t byte_index = OS_KERNEL_CONSOLE_INPUT_EMPTY_VALUE;
         byte_index < OS_KERNEL_CONSOLE_INPUT_CAPACITY_BYTES; ++byte_index) {
        this->bytes_[byte_index] = static_cast<uint8_t>(OS_KERNEL_CONSOLE_INPUT_EMPTY_VALUE);
    }
    this->read_index_ = OS_KERNEL_CONSOLE_INPUT_EMPTY_VALUE;
    this->write_index_ = OS_KERNEL_CONSOLE_INPUT_EMPTY_VALUE;
    this->statistics_ = ConsoleInputStatistics{};
}

ConsoleInputStatus ConsoleInput::Submit(const uint8_t character) noexcept {
    if (this->statistics_.buffered_byte_count >= OS_KERNEL_CONSOLE_INPUT_CAPACITY_BYTES) {
        this->statistics_.dropped_byte_count += OS_KERNEL_CONSOLE_INPUT_COUNTER_INCREMENT;
        return ConsoleInputStatus::Full;
    }
    this->bytes_[this->write_index_] = character;
    this->write_index_ = (this->write_index_ + OS_KERNEL_CONSOLE_INPUT_COUNTER_INCREMENT) %
                         OS_KERNEL_CONSOLE_INPUT_CAPACITY_BYTES;
    this->statistics_.submitted_byte_count += OS_KERNEL_CONSOLE_INPUT_COUNTER_INCREMENT;
    this->statistics_.buffered_byte_count += OS_KERNEL_CONSOLE_INPUT_COUNTER_INCREMENT;
    return ConsoleInputStatus::Succeeded;
}

ConsoleInputStatus ConsoleInput::TryRead(uint8_t *const destination, const uint64_t capacity_bytes,
                                         uint64_t &read_bytes) noexcept {
    read_bytes = OS_KERNEL_CONSOLE_INPUT_EMPTY_VALUE;
    if (destination == nullptr || capacity_bytes == OS_KERNEL_CONSOLE_INPUT_EMPTY_VALUE) {
        return ConsoleInputStatus::InvalidArgument;
    }
    if (this->statistics_.buffered_byte_count == OS_KERNEL_CONSOLE_INPUT_EMPTY_VALUE) {
        return ConsoleInputStatus::Empty;
    }
    const uint64_t transferable_bytes = capacity_bytes < this->statistics_.buffered_byte_count
                                            ? capacity_bytes
                                            : this->statistics_.buffered_byte_count;
    while (read_bytes < transferable_bytes) {
        destination[read_bytes] = this->bytes_[this->read_index_];
        this->read_index_ = (this->read_index_ + OS_KERNEL_CONSOLE_INPUT_COUNTER_INCREMENT) %
                            OS_KERNEL_CONSOLE_INPUT_CAPACITY_BYTES;
        ++read_bytes;
    }
    this->statistics_.read_byte_count += read_bytes;
    this->statistics_.buffered_byte_count -= read_bytes;
    return ConsoleInputStatus::Succeeded;
}

bool ConsoleInput::ReadCanProgress() const noexcept {
    return this->statistics_.buffered_byte_count != OS_KERNEL_CONSOLE_INPUT_EMPTY_VALUE;
}

ConsoleInputStatistics ConsoleInput::Statistics() const noexcept { return this->statistics_; }
}

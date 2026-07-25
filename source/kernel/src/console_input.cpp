#include "os/kernel/console_input.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_CONSOLE_INPUT_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_CONSOLE_INPUT_COUNTER_INCREMENT = 1ULL;

}

void ConsoleInput::Initialize() noexcept {
    for (uint64_t byteIndex = OS_KERNEL_CONSOLE_INPUT_EMPTY_VALUE;
         byteIndex < OS_KERNEL_CONSOLE_INPUT_CAPACITY_BYTES; ++byteIndex) {
        this->bytes_[byteIndex] = static_cast<uint8_t>(OS_KERNEL_CONSOLE_INPUT_EMPTY_VALUE);
    }
    this->readIndex_ = OS_KERNEL_CONSOLE_INPUT_EMPTY_VALUE;
    this->writeIndex_ = OS_KERNEL_CONSOLE_INPUT_EMPTY_VALUE;
    this->statistics_ = ConsoleInputStatistics{};
}

ConsoleInputStatus ConsoleInput::Submit(const uint8_t character) noexcept {
    if (this->statistics_.bufferedByteCount >= OS_KERNEL_CONSOLE_INPUT_CAPACITY_BYTES) {
        this->statistics_.droppedByteCount += OS_KERNEL_CONSOLE_INPUT_COUNTER_INCREMENT;
        return ConsoleInputStatus::Full;
    }
    this->bytes_[this->writeIndex_] = character;
    this->writeIndex_ =
        (this->writeIndex_ + OS_KERNEL_CONSOLE_INPUT_COUNTER_INCREMENT) %
        OS_KERNEL_CONSOLE_INPUT_CAPACITY_BYTES;
    this->statistics_.submittedByteCount += OS_KERNEL_CONSOLE_INPUT_COUNTER_INCREMENT;
    this->statistics_.bufferedByteCount += OS_KERNEL_CONSOLE_INPUT_COUNTER_INCREMENT;
    return ConsoleInputStatus::Succeeded;
}

ConsoleInputStatus ConsoleInput::TryRead(uint8_t *const destination,
                                        const uint64_t capacityBytes,
                                        uint64_t &readBytes) noexcept {
    readBytes = OS_KERNEL_CONSOLE_INPUT_EMPTY_VALUE;
    if (destination == nullptr || capacityBytes == OS_KERNEL_CONSOLE_INPUT_EMPTY_VALUE) {
        return ConsoleInputStatus::InvalidArgument;
    }
    if (this->statistics_.bufferedByteCount == OS_KERNEL_CONSOLE_INPUT_EMPTY_VALUE) {
        return ConsoleInputStatus::Empty;
    }
    const uint64_t transferableBytes =
        capacityBytes < this->statistics_.bufferedByteCount
            ? capacityBytes
            : this->statistics_.bufferedByteCount;
    while (readBytes < transferableBytes) {
        destination[readBytes] = this->bytes_[this->readIndex_];
        this->readIndex_ =
            (this->readIndex_ + OS_KERNEL_CONSOLE_INPUT_COUNTER_INCREMENT) %
            OS_KERNEL_CONSOLE_INPUT_CAPACITY_BYTES;
        ++readBytes;
    }
    this->statistics_.readByteCount += readBytes;
    this->statistics_.bufferedByteCount -= readBytes;
    return ConsoleInputStatus::Succeeded;
}

bool ConsoleInput::ReadCanProgress() const noexcept {
    return this->statistics_.bufferedByteCount != OS_KERNEL_CONSOLE_INPUT_EMPTY_VALUE;
}

ConsoleInputStatistics ConsoleInput::Statistics() const noexcept {
    return this->statistics_;
}

}

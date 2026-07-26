#include "os/kernel/device/serial_port.hpp"

namespace os::kernel {

const uint16_t OS_KERNEL_SERIAL_COM1_BASE_PORT = 0x03F8U;

namespace {

constexpr uint16_t OS_KERNEL_SERIAL_INTERRUPT_ENABLE_OFFSET = 0x0001U;
constexpr uint16_t OS_KERNEL_SERIAL_FIFO_CONTROL_OFFSET = 0x0002U;
constexpr uint16_t OS_KERNEL_SERIAL_LINE_CONTROL_OFFSET = 0x0003U;
constexpr uint16_t OS_KERNEL_SERIAL_MODEM_CONTROL_OFFSET = 0x0004U;
constexpr uint16_t OS_KERNEL_SERIAL_LINE_STATUS_OFFSET = 0x0005U;
constexpr uint16_t OS_KERNEL_SERIAL_DIVISOR_LOW_OFFSET = 0x0000U;
constexpr uint16_t OS_KERNEL_SERIAL_DIVISOR_HIGH_OFFSET = 0x0001U;
constexpr uint8_t OS_KERNEL_SERIAL_DISABLE_INTERRUPTS = 0x00U;
constexpr uint8_t OS_KERNEL_SERIAL_ENABLE_DLAB = 0x80U;
constexpr uint8_t OS_KERNEL_SERIAL_BAUD_DIVISOR_LOW = 0x01U;
constexpr uint8_t OS_KERNEL_SERIAL_BAUD_DIVISOR_HIGH = 0x00U;
constexpr uint8_t OS_KERNEL_SERIAL_LINE_8N1 = 0x03U;
constexpr uint8_t OS_KERNEL_SERIAL_FIFO_CONFIGURATION = 0xC7U;
constexpr uint8_t OS_KERNEL_SERIAL_MODEM_CONFIGURATION = 0x0BU;
constexpr uint8_t OS_KERNEL_SERIAL_TRANSMITTER_EMPTY_BIT = 0x20U;
constexpr uint64_t OS_KERNEL_SERIAL_READY_POLL_LIMIT = 0x0000FFFFULL;
constexpr uint64_t OS_KERNEL_SERIAL_HEXADECIMAL_DIGIT_COUNT = 16ULL;
constexpr uint64_t OS_KERNEL_SERIAL_HEXADECIMAL_BITS_PER_DIGIT = 4ULL;
constexpr uint64_t OS_KERNEL_SERIAL_HEXADECIMAL_DIGIT_MASK = 0x0FULL;
constexpr char OS_KERNEL_SERIAL_HEXADECIMAL_DIGITS[] = "0123456789ABCDEF";
constexpr char OS_KERNEL_SERIAL_HEXADECIMAL_PREFIX[] = "0x";
constexpr char OS_KERNEL_SERIAL_LINE_ENDING[] = "\r\n";

}

SerialPort::SerialPort(const uint16_t base_port) noexcept : base_port_{base_port} {}

void SerialPort::Initialize() const noexcept {
    this->WriteRegister(OS_KERNEL_SERIAL_INTERRUPT_ENABLE_OFFSET,
                        OS_KERNEL_SERIAL_DISABLE_INTERRUPTS);
    this->WriteRegister(OS_KERNEL_SERIAL_LINE_CONTROL_OFFSET, OS_KERNEL_SERIAL_ENABLE_DLAB);
    this->WriteRegister(OS_KERNEL_SERIAL_DIVISOR_LOW_OFFSET, OS_KERNEL_SERIAL_BAUD_DIVISOR_LOW);
    this->WriteRegister(OS_KERNEL_SERIAL_DIVISOR_HIGH_OFFSET, OS_KERNEL_SERIAL_BAUD_DIVISOR_HIGH);
    this->WriteRegister(OS_KERNEL_SERIAL_LINE_CONTROL_OFFSET, OS_KERNEL_SERIAL_LINE_8N1);
    this->WriteRegister(OS_KERNEL_SERIAL_FIFO_CONTROL_OFFSET, OS_KERNEL_SERIAL_FIFO_CONFIGURATION);
    this->WriteRegister(OS_KERNEL_SERIAL_MODEM_CONTROL_OFFSET,
                        OS_KERNEL_SERIAL_MODEM_CONFIGURATION);
}

bool SerialPort::TryWriteByte(const char byte) const noexcept {
    if (!this->WaitForTransmitter()) {
        return false;
    }
    this->WriteRegister(OS_KERNEL_SERIAL_DIVISOR_LOW_OFFSET, static_cast<uint8_t>(byte));
    return true;
}

bool SerialPort::TryWriteString(const char *text) const noexcept {
    if (text == nullptr) {
        return false;
    }
    while (*text != '\0') {
        if (!this->TryWriteByte(*text)) {
            return false;
        }
        ++text;
    }
    return true;
}

bool SerialPort::TryWriteHexLine(const char *prefix, const uint64_t value) const noexcept {
    if (!this->TryWriteString(prefix) ||
        !this->TryWriteString(OS_KERNEL_SERIAL_HEXADECIMAL_PREFIX)) {
        return false;
    }

    uint64_t remaining_digit_count = OS_KERNEL_SERIAL_HEXADECIMAL_DIGIT_COUNT;
    while (remaining_digit_count > 0ULL) {
        --remaining_digit_count;
        const uint64_t shift_bit_count =
            remaining_digit_count * OS_KERNEL_SERIAL_HEXADECIMAL_BITS_PER_DIGIT;
        const uint64_t digit_index =
            (value >> shift_bit_count) & OS_KERNEL_SERIAL_HEXADECIMAL_DIGIT_MASK;
        if (!this->TryWriteByte(OS_KERNEL_SERIAL_HEXADECIMAL_DIGITS[digit_index])) {
            return false;
        }
    }
    return this->TryWriteString(OS_KERNEL_SERIAL_LINE_ENDING);
}

uint8_t SerialPort::ReadRegister(const uint16_t offset) const noexcept {
    const uint16_t port = static_cast<uint16_t>(this->base_port_ + offset);
    uint8_t value = 0U;
    asm volatile("in al, dx" : "=a"(value) : "d"(port));
    return value;
}

void SerialPort::WriteRegister(const uint16_t offset, const uint8_t value) const noexcept {
    const uint16_t port = static_cast<uint16_t>(this->base_port_ + offset);
    asm volatile("out dx, al" : : "a"(value), "d"(port));
}

bool SerialPort::WaitForTransmitter() const noexcept {
    uint64_t remaining_poll_count = OS_KERNEL_SERIAL_READY_POLL_LIMIT;
    while (remaining_poll_count > 0ULL) {
        const uint8_t line_status = this->ReadRegister(OS_KERNEL_SERIAL_LINE_STATUS_OFFSET);
        if ((line_status & OS_KERNEL_SERIAL_TRANSMITTER_EMPTY_BIT) != 0U) {
            return true;
        }
        --remaining_poll_count;
    }
    return false;
}

}

#include "os/kernel/ps2_keyboard.hpp"

#include "os/kernel/port_io.hpp"

namespace os::kernel {

namespace {

constexpr uint16_t OS_KERNEL_PS2_DATA_PORT = 0x0060U;
constexpr uint16_t OS_KERNEL_PS2_STATUS_COMMAND_PORT = 0x0064U;
constexpr uint8_t OS_KERNEL_PS2_STATUS_OUTPUT_BUFFER_FULL_BIT = 0x01U;
constexpr uint8_t OS_KERNEL_PS2_STATUS_INPUT_BUFFER_FULL_BIT = 0x02U;
constexpr uint8_t OS_KERNEL_PS2_DISABLE_FIRST_PORT_COMMAND = 0xADU;
constexpr uint8_t OS_KERNEL_PS2_DISABLE_SECOND_PORT_COMMAND = 0xA7U;
constexpr uint8_t OS_KERNEL_PS2_READ_CONFIGURATION_COMMAND = 0x20U;
constexpr uint8_t OS_KERNEL_PS2_WRITE_CONFIGURATION_COMMAND = 0x60U;
constexpr uint8_t OS_KERNEL_PS2_ENABLE_FIRST_PORT_COMMAND = 0xAEU;
constexpr uint8_t OS_KERNEL_PS2_CONFIGURATION_FIRST_PORT_INTERRUPT_BIT = 0x01U;
constexpr uint8_t OS_KERNEL_PS2_CONFIGURATION_SECOND_PORT_INTERRUPT_BIT = 0x02U;
constexpr uint8_t OS_KERNEL_PS2_CONFIGURATION_FIRST_PORT_CLOCK_DISABLED_BIT = 0x10U;
constexpr uint8_t OS_KERNEL_PS2_CONFIGURATION_TRANSLATION_BIT = 0x40U;
constexpr uint8_t OS_KERNEL_PS2_KEYBOARD_ENABLE_SCANNING_COMMAND = 0xF4U;
constexpr uint8_t OS_KERNEL_PS2_KEYBOARD_ACKNOWLEDGEMENT = 0xFAU;
constexpr uint64_t OS_KERNEL_PS2_CONTROLLER_POLL_LIMIT = 0x0000FFFFULL;
constexpr uint64_t OS_KERNEL_PS2_OUTPUT_FLUSH_LIMIT = 32ULL;

}

Ps2KeyboardStatus Ps2Keyboard::Initialize() const noexcept {
    if (!this->WaitForControllerInput()) {
        return Ps2KeyboardStatus::ControllerInputTimeout;
    }
    WritePort8(OS_KERNEL_PS2_STATUS_COMMAND_PORT, OS_KERNEL_PS2_DISABLE_FIRST_PORT_COMMAND);
    if (!this->WaitForControllerInput()) {
        return Ps2KeyboardStatus::ControllerInputTimeout;
    }
    WritePort8(OS_KERNEL_PS2_STATUS_COMMAND_PORT, OS_KERNEL_PS2_DISABLE_SECOND_PORT_COMMAND);
    this->FlushControllerOutput();

    if (!this->WaitForControllerInput()) {
        return Ps2KeyboardStatus::ControllerInputTimeout;
    }
    WritePort8(OS_KERNEL_PS2_STATUS_COMMAND_PORT, OS_KERNEL_PS2_READ_CONFIGURATION_COMMAND);
    if (!this->WaitForControllerOutput()) {
        return Ps2KeyboardStatus::ControllerOutputTimeout;
    }
    uint8_t configuration = ReadPort8(OS_KERNEL_PS2_DATA_PORT);
    configuration =
        static_cast<uint8_t>(configuration | OS_KERNEL_PS2_CONFIGURATION_FIRST_PORT_INTERRUPT_BIT |
                             OS_KERNEL_PS2_CONFIGURATION_TRANSLATION_BIT);
    configuration = static_cast<uint8_t>(
        configuration &
        static_cast<uint8_t>(~(OS_KERNEL_PS2_CONFIGURATION_SECOND_PORT_INTERRUPT_BIT |
                               OS_KERNEL_PS2_CONFIGURATION_FIRST_PORT_CLOCK_DISABLED_BIT)));

    if (!this->WaitForControllerInput()) {
        return Ps2KeyboardStatus::ControllerInputTimeout;
    }
    WritePort8(OS_KERNEL_PS2_STATUS_COMMAND_PORT, OS_KERNEL_PS2_WRITE_CONFIGURATION_COMMAND);
    if (!this->WaitForControllerInput()) {
        return Ps2KeyboardStatus::ControllerInputTimeout;
    }
    WritePort8(OS_KERNEL_PS2_DATA_PORT, configuration);
    if (!this->WaitForControllerInput()) {
        return Ps2KeyboardStatus::ControllerInputTimeout;
    }
    WritePort8(OS_KERNEL_PS2_STATUS_COMMAND_PORT, OS_KERNEL_PS2_ENABLE_FIRST_PORT_COMMAND);

    return this->WriteDeviceCommand(OS_KERNEL_PS2_KEYBOARD_ENABLE_SCANNING_COMMAND);
}

Ps2KeyboardStatus Ps2Keyboard::TryReadScanCode(uint8_t &scan_code) const noexcept {
    if ((ReadPort8(OS_KERNEL_PS2_STATUS_COMMAND_PORT) &
         OS_KERNEL_PS2_STATUS_OUTPUT_BUFFER_FULL_BIT) == 0U) {
        return Ps2KeyboardStatus::NoScanCodeAvailable;
    }
    scan_code = ReadPort8(OS_KERNEL_PS2_DATA_PORT);
    return Ps2KeyboardStatus::Succeeded;
}

bool Ps2Keyboard::WaitForControllerInput() const noexcept {
    uint64_t remaining_poll_count = OS_KERNEL_PS2_CONTROLLER_POLL_LIMIT;
    while (remaining_poll_count > 0ULL) {
        if ((ReadPort8(OS_KERNEL_PS2_STATUS_COMMAND_PORT) &
             OS_KERNEL_PS2_STATUS_INPUT_BUFFER_FULL_BIT) == 0U) {
            return true;
        }
        --remaining_poll_count;
    }
    return false;
}

bool Ps2Keyboard::WaitForControllerOutput() const noexcept {
    uint64_t remaining_poll_count = OS_KERNEL_PS2_CONTROLLER_POLL_LIMIT;
    while (remaining_poll_count > 0ULL) {
        if ((ReadPort8(OS_KERNEL_PS2_STATUS_COMMAND_PORT) &
             OS_KERNEL_PS2_STATUS_OUTPUT_BUFFER_FULL_BIT) != 0U) {
            return true;
        }
        --remaining_poll_count;
    }
    return false;
}

void Ps2Keyboard::FlushControllerOutput() const noexcept {
    uint64_t remaining_read_count = OS_KERNEL_PS2_OUTPUT_FLUSH_LIMIT;
    while (remaining_read_count > 0ULL && (ReadPort8(OS_KERNEL_PS2_STATUS_COMMAND_PORT) &
                                           OS_KERNEL_PS2_STATUS_OUTPUT_BUFFER_FULL_BIT) != 0U) {
        static_cast<void>(ReadPort8(OS_KERNEL_PS2_DATA_PORT));
        --remaining_read_count;
    }
}

Ps2KeyboardStatus Ps2Keyboard::WriteDeviceCommand(const uint8_t command) const noexcept {
    if (!this->WaitForControllerInput()) {
        return Ps2KeyboardStatus::ControllerInputTimeout;
    }
    WritePort8(OS_KERNEL_PS2_DATA_PORT, command);
    if (!this->WaitForControllerOutput()) {
        return Ps2KeyboardStatus::ControllerOutputTimeout;
    }
    if (ReadPort8(OS_KERNEL_PS2_DATA_PORT) != OS_KERNEL_PS2_KEYBOARD_ACKNOWLEDGEMENT) {
        return Ps2KeyboardStatus::DeviceRejectedCommand;
    }
    return Ps2KeyboardStatus::Succeeded;
}

}

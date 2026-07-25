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

Ps2KeyboardStatus Ps2Keyboard::initialize() const noexcept {
    if (!this->waitForControllerInput()) {
        return Ps2KeyboardStatus::ControllerInputTimeout;
    }
    writePort8(OS_KERNEL_PS2_STATUS_COMMAND_PORT, OS_KERNEL_PS2_DISABLE_FIRST_PORT_COMMAND);
    if (!this->waitForControllerInput()) {
        return Ps2KeyboardStatus::ControllerInputTimeout;
    }
    writePort8(OS_KERNEL_PS2_STATUS_COMMAND_PORT, OS_KERNEL_PS2_DISABLE_SECOND_PORT_COMMAND);
    this->flushControllerOutput();

    if (!this->waitForControllerInput()) {
        return Ps2KeyboardStatus::ControllerInputTimeout;
    }
    writePort8(OS_KERNEL_PS2_STATUS_COMMAND_PORT, OS_KERNEL_PS2_READ_CONFIGURATION_COMMAND);
    if (!this->waitForControllerOutput()) {
        return Ps2KeyboardStatus::ControllerOutputTimeout;
    }
    uint8_t configuration = readPort8(OS_KERNEL_PS2_DATA_PORT);
    configuration =
        static_cast<uint8_t>(configuration | OS_KERNEL_PS2_CONFIGURATION_FIRST_PORT_INTERRUPT_BIT |
                             OS_KERNEL_PS2_CONFIGURATION_TRANSLATION_BIT);
    configuration = static_cast<uint8_t>(
        configuration &
        static_cast<uint8_t>(~(OS_KERNEL_PS2_CONFIGURATION_SECOND_PORT_INTERRUPT_BIT |
                               OS_KERNEL_PS2_CONFIGURATION_FIRST_PORT_CLOCK_DISABLED_BIT)));

    if (!this->waitForControllerInput()) {
        return Ps2KeyboardStatus::ControllerInputTimeout;
    }
    writePort8(OS_KERNEL_PS2_STATUS_COMMAND_PORT, OS_KERNEL_PS2_WRITE_CONFIGURATION_COMMAND);
    if (!this->waitForControllerInput()) {
        return Ps2KeyboardStatus::ControllerInputTimeout;
    }
    writePort8(OS_KERNEL_PS2_DATA_PORT, configuration);
    if (!this->waitForControllerInput()) {
        return Ps2KeyboardStatus::ControllerInputTimeout;
    }
    writePort8(OS_KERNEL_PS2_STATUS_COMMAND_PORT, OS_KERNEL_PS2_ENABLE_FIRST_PORT_COMMAND);

    return this->writeDeviceCommand(OS_KERNEL_PS2_KEYBOARD_ENABLE_SCANNING_COMMAND);
}

Ps2KeyboardStatus Ps2Keyboard::tryReadScanCode(uint8_t &scanCode) const noexcept {
    if ((readPort8(OS_KERNEL_PS2_STATUS_COMMAND_PORT) &
         OS_KERNEL_PS2_STATUS_OUTPUT_BUFFER_FULL_BIT) == 0U) {
        return Ps2KeyboardStatus::NoScanCodeAvailable;
    }
    scanCode = readPort8(OS_KERNEL_PS2_DATA_PORT);
    return Ps2KeyboardStatus::Succeeded;
}

bool Ps2Keyboard::waitForControllerInput() const noexcept {
    uint64_t remainingPollCount = OS_KERNEL_PS2_CONTROLLER_POLL_LIMIT;
    while (remainingPollCount > 0ULL) {
        if ((readPort8(OS_KERNEL_PS2_STATUS_COMMAND_PORT) &
             OS_KERNEL_PS2_STATUS_INPUT_BUFFER_FULL_BIT) == 0U) {
            return true;
        }
        --remainingPollCount;
    }
    return false;
}

bool Ps2Keyboard::waitForControllerOutput() const noexcept {
    uint64_t remainingPollCount = OS_KERNEL_PS2_CONTROLLER_POLL_LIMIT;
    while (remainingPollCount > 0ULL) {
        if ((readPort8(OS_KERNEL_PS2_STATUS_COMMAND_PORT) &
             OS_KERNEL_PS2_STATUS_OUTPUT_BUFFER_FULL_BIT) != 0U) {
            return true;
        }
        --remainingPollCount;
    }
    return false;
}

void Ps2Keyboard::flushControllerOutput() const noexcept {
    uint64_t remainingReadCount = OS_KERNEL_PS2_OUTPUT_FLUSH_LIMIT;
    while (remainingReadCount > 0ULL && (readPort8(OS_KERNEL_PS2_STATUS_COMMAND_PORT) &
                                         OS_KERNEL_PS2_STATUS_OUTPUT_BUFFER_FULL_BIT) != 0U) {
        static_cast<void>(readPort8(OS_KERNEL_PS2_DATA_PORT));
        --remainingReadCount;
    }
}

Ps2KeyboardStatus Ps2Keyboard::writeDeviceCommand(const uint8_t command) const noexcept {
    if (!this->waitForControllerInput()) {
        return Ps2KeyboardStatus::ControllerInputTimeout;
    }
    writePort8(OS_KERNEL_PS2_DATA_PORT, command);
    if (!this->waitForControllerOutput()) {
        return Ps2KeyboardStatus::ControllerOutputTimeout;
    }
    if (readPort8(OS_KERNEL_PS2_DATA_PORT) != OS_KERNEL_PS2_KEYBOARD_ACKNOWLEDGEMENT) {
        return Ps2KeyboardStatus::DeviceRejectedCommand;
    }
    return Ps2KeyboardStatus::Succeeded;
}

}

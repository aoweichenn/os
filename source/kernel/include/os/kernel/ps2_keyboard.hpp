#pragma once

#include <stdint.h>

namespace os::kernel {

enum class Ps2KeyboardStatus : uint64_t {
    Succeeded,
    ControllerInputTimeout,
    ControllerOutputTimeout,
    DeviceRejectedCommand,
    NoScanCodeAvailable,
};

class Ps2Keyboard final {
  public:
    [[nodiscard]] Ps2KeyboardStatus initialize() const noexcept;
    [[nodiscard]] Ps2KeyboardStatus tryReadScanCode(uint8_t &scanCode) const noexcept;

  private:
    [[nodiscard]] bool waitForControllerInput() const noexcept;
    [[nodiscard]] bool waitForControllerOutput() const noexcept;
    void flushControllerOutput() const noexcept;
    [[nodiscard]] Ps2KeyboardStatus writeDeviceCommand(uint8_t command) const noexcept;
};

}

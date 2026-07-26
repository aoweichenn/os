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
    [[nodiscard]] Ps2KeyboardStatus Initialize() const noexcept;
    [[nodiscard]] Ps2KeyboardStatus TryReadScanCode(uint8_t &scan_code) const noexcept;

  private:
    [[nodiscard]] bool WaitForControllerInput() const noexcept;
    [[nodiscard]] bool WaitForControllerOutput() const noexcept;
    void FlushControllerOutput() const noexcept;
    [[nodiscard]] Ps2KeyboardStatus WriteDeviceCommand(uint8_t command) const noexcept;
};

}

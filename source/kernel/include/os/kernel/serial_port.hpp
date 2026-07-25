#pragma once

#include <stdint.h>

namespace os::kernel {

extern const uint16_t OS_KERNEL_SERIAL_COM1_BASE_PORT;

class SerialPort final {
  public:
    explicit SerialPort(uint16_t basePort) noexcept;

    void Initialize() const noexcept;
    [[nodiscard]] bool TryWriteByte(char byte) const noexcept;
    [[nodiscard]] bool TryWriteString(const char *text) const noexcept;
    [[nodiscard]] bool TryWriteHexLine(const char *prefix, uint64_t value) const noexcept;

  private:
    [[nodiscard]] uint8_t ReadRegister(uint16_t offset) const noexcept;
    void WriteRegister(uint16_t offset, uint8_t value) const noexcept;
    [[nodiscard]] bool WaitForTransmitter() const noexcept;

    uint16_t basePort_;
};

}

#pragma once

#include <stdint.h>

namespace os::kernel {

extern const uint16_t OS_KERNEL_SERIAL_COM1_BASE_PORT;

class SerialPort final {
  public:
    explicit SerialPort(uint16_t basePort) noexcept;

    void initialize() const noexcept;
    [[nodiscard]] bool tryWriteByte(char byte) const noexcept;
    [[nodiscard]] bool tryWriteString(const char *text) const noexcept;
    [[nodiscard]] bool tryWriteHexLine(const char *prefix, uint64_t value) const noexcept;

  private:
    [[nodiscard]] uint8_t readRegister(uint16_t offset) const noexcept;
    void writeRegister(uint16_t offset, uint8_t value) const noexcept;
    [[nodiscard]] bool waitForTransmitter() const noexcept;

    uint16_t basePort_;
};

}

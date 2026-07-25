#pragma once

#include <stdint.h>

namespace os::kernel {

[[nodiscard]] uint8_t readPort8(uint16_t port) noexcept;
[[nodiscard]] uint16_t readPort16(uint16_t port) noexcept;
void writePort8(uint16_t port, uint8_t value) noexcept;
void writePort16(uint16_t port, uint16_t value) noexcept;
void waitForPortIo() noexcept;

}

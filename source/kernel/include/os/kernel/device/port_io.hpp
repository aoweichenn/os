#pragma once

#include <stdint.h>

namespace os::kernel {

[[nodiscard]] uint8_t ReadPort8(uint16_t port) noexcept;
[[nodiscard]] uint16_t ReadPort16(uint16_t port) noexcept;
[[nodiscard]] uint32_t ReadPort32(uint16_t port) noexcept;
void WritePort8(uint16_t port, uint8_t value) noexcept;
void WritePort16(uint16_t port, uint16_t value) noexcept;
void WritePort32(uint16_t port, uint32_t value) noexcept;
void WaitForPortIo() noexcept;

}

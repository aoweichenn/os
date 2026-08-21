#include <os/kernel/device/port_io.hpp>

namespace os::kernel {

namespace {

constexpr uint16_t OS_KERNEL_PORT_IO_LEGACY_DELAY_PORT = 0x0080U;
constexpr uint8_t OS_KERNEL_PORT_IO_LEGACY_DELAY_VALUE = 0x00U;

}

uint8_t ReadPort8(const uint16_t port) noexcept {
    uint8_t value = 0U;
    asm volatile("in al, dx" : "=a"(value) : "d"(port));
    return value;
}

uint16_t ReadPort16(const uint16_t port) noexcept {
    uint16_t value = 0U;
    asm volatile("in ax, dx" : "=a"(value) : "d"(port));
    return value;
}

uint32_t ReadPort32(const uint16_t port) noexcept {
    uint32_t value = 0U;
    asm volatile("in eax, dx" : "=a"(value) : "d"(port));
    return value;
}

void WritePort8(const uint16_t port, const uint8_t value) noexcept {
    asm volatile("out dx, al" : : "a"(value), "d"(port));
}

void WritePort16(const uint16_t port, const uint16_t value) noexcept {
    asm volatile("out dx, ax" : : "a"(value), "d"(port));
}

void WritePort32(const uint16_t port, const uint32_t value) noexcept {
    asm volatile("out dx, eax" : : "a"(value), "d"(port));
}

void WaitForPortIo() noexcept {
    WritePort8(OS_KERNEL_PORT_IO_LEGACY_DELAY_PORT, OS_KERNEL_PORT_IO_LEGACY_DELAY_VALUE);
}

}

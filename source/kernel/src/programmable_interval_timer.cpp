#include "os/kernel/programmable_interval_timer.hpp"

#include "os/kernel/port_io.hpp"

namespace os::kernel {

namespace {

constexpr uint16_t OS_KERNEL_PIT_CHANNEL0_DATA_PORT = 0x0040U;
constexpr uint16_t OS_KERNEL_PIT_MODE_COMMAND_PORT = 0x0043U;
constexpr uint8_t OS_KERNEL_PIT_CHANNEL0_LOW_HIGH_MODE2_BINARY = 0x34U;
constexpr uint64_t OS_KERNEL_PIT_DIVISOR_HIGH_SHIFT_BITS = 8ULL;
constexpr uint16_t OS_KERNEL_PIT_DIVISOR_BYTE_MASK = 0x00FFU;

}

PitConfigurationStatus
ProgrammableIntervalTimer::Initialize(const uint64_t requested_frequency_hz,
                                      PitConfiguration &configuration) const noexcept {
    const PitConfigurationStatus status =
        CreatePitConfiguration(requested_frequency_hz, configuration);
    if (status != PitConfigurationStatus::Succeeded) {
        return status;
    }

    WritePort8(OS_KERNEL_PIT_MODE_COMMAND_PORT, OS_KERNEL_PIT_CHANNEL0_LOW_HIGH_MODE2_BINARY);
    // 访问模式规定同一数据端口先接收低字节、再接收高字节。
    WritePort8(OS_KERNEL_PIT_CHANNEL0_DATA_PORT,
               static_cast<uint8_t>(configuration.divisor & OS_KERNEL_PIT_DIVISOR_BYTE_MASK));
    WritePort8(
        OS_KERNEL_PIT_CHANNEL0_DATA_PORT,
        static_cast<uint8_t>((configuration.divisor >> OS_KERNEL_PIT_DIVISOR_HIGH_SHIFT_BITS) &
                             OS_KERNEL_PIT_DIVISOR_BYTE_MASK));
    return PitConfigurationStatus::Succeeded;
}
}

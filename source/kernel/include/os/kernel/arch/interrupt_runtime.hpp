#pragma once

#include "os/kernel/device/device_model.hpp"
#include "os/kernel/arch/exception_frame.hpp"

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_INTERRUPT_TIMER_REQUEST = 0ULL;
inline constexpr uint64_t OS_KERNEL_INTERRUPT_KEYBOARD_REQUEST = 1ULL;
inline constexpr uint64_t OS_KERNEL_INTERRUPT_SPURIOUS_TEST_REQUEST = 7ULL;
inline constexpr uint64_t OS_KERNEL_INTERRUPT_TARGET_TIMER_FREQUENCY_HZ = 1000ULL;

enum class InterruptRuntimeStatus : uint64_t {
    Succeeded,
    LegacyInterruptRoutingFailed,
    InvalidPitConfiguration,
    KeyboardInitializationFailed,
    AtaReadFailed,
    InvalidBootDescriptor,
    PicConfigurationFailed,
};

struct InterruptRuntimeStatistics final {
    uint64_t timer_tick_count;
    uint64_t monotonic_milliseconds;
    uint64_t keyboard_interrupt_count;
    uint64_t supported_keyboard_event_count;
    uint64_t spurious_interrupt_count;
    uint16_t pic_mask;
    uint16_t pit_divisor;
    uint64_t pit_actual_frequency_hz;
};

[[nodiscard]] InterruptRuntimeStatus InitializeInterruptRuntime() noexcept;
[[nodiscard]] InterruptRuntimeStatistics GetInterruptRuntimeStatistics() noexcept;
[[nodiscard]] bool TryTakeKeyboardEvent(KeyboardEvent &event) noexcept;

extern "C" [[nodiscard]] ExceptionFrame *
OsKernelDispatchHardwareInterrupt(ExceptionFrame *frame) noexcept;

}

#pragma once

#include "os/kernel/device_model.hpp"
#include "os/kernel/exception_frame.hpp"

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
    uint64_t timerTickCount;
    uint64_t monotonicMilliseconds;
    uint64_t keyboardInterruptCount;
    uint64_t supportedKeyboardEventCount;
    uint64_t spuriousInterruptCount;
    uint16_t picMask;
    uint16_t pitDivisor;
    uint64_t pitActualFrequencyHz;
};

[[nodiscard]] InterruptRuntimeStatus InitializeInterruptRuntime() noexcept;
[[nodiscard]] InterruptRuntimeStatistics GetInterruptRuntimeStatistics() noexcept;
[[nodiscard]] bool TryTakeKeyboardEvent(KeyboardEvent &event) noexcept;

extern "C" [[nodiscard]] ExceptionFrame *
osKernelDispatchHardwareInterrupt(ExceptionFrame *frame) noexcept;

}

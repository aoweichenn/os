#pragma once

#include <os/kernel/arch/exception_frame.hpp>
#include <os/kernel/device/ata_pio.hpp>
#include <os/kernel/device/device_model.hpp>

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_INTERRUPT_TIMER_REQUEST = 0ULL;
inline constexpr uint64_t OS_KERNEL_INTERRUPT_KEYBOARD_REQUEST = 1ULL;
inline constexpr uint64_t OS_KERNEL_INTERRUPT_SPURIOUS_TEST_REQUEST = 7ULL;
inline constexpr uint64_t OS_KERNEL_INTERRUPT_PRIMARY_ATA_REQUEST = 14ULL;
inline constexpr uint64_t OS_KERNEL_INTERRUPT_NVME_MSIX_VECTOR = 0x50ULL;
inline constexpr uint64_t OS_KERNEL_INTERRUPT_TARGET_TIMER_FREQUENCY_HZ = 1000ULL;

enum class InterruptRuntimeStatus : uint64_t {
    Succeeded,
    LegacyInterruptRoutingFailed,
    InvalidPitConfiguration,
    KeyboardInitializationFailed,
    AtaReadFailed,
    AtaRequestInitializationFailed,
    InvalidBootDescriptor,
    PicConfigurationFailed,
};

struct InterruptRuntimeStatistics final {
    uint64_t timer_tick_count;
    uint64_t monotonic_nanoseconds;
    uint64_t monotonic_milliseconds;
    uint64_t monotonic_fractional_numerator;
    uint64_t keyboard_interrupt_count;
    uint64_t supported_keyboard_event_count;
    uint64_t spurious_interrupt_count;
    uint64_t ata_interrupt_count;
    uint64_t ata_completion_count;
    uint64_t ata_timeout_count;
    uint64_t ata_request_capacity;
    uint16_t pic_mask;
    uint16_t pit_divisor;
    uint64_t pit_actual_frequency_hz;
    bool monotonic_saturated;
};

[[nodiscard]] InterruptRuntimeStatus InitializeInterruptRuntime() noexcept;
[[nodiscard]] InterruptRuntimeStatistics GetInterruptRuntimeStatistics() noexcept;
[[nodiscard]] uint64_t GetMonotonicNanoseconds() noexcept;
[[nodiscard]] bool TryTakeKeyboardEvent(KeyboardEvent &event) noexcept;
[[nodiscard]] AtaPioStatus
SubmitAsynchronousAtaFlush(uint64_t owner_thread_index,
                           uint64_t deadline_nanoseconds,
                           uint64_t &request_identifier,
                           BlockRequestResult &immediate_result) noexcept;

extern "C" [[nodiscard]] ExceptionFrame *
OsKernelDispatchHardwareInterrupt(ExceptionFrame *frame) noexcept;

}

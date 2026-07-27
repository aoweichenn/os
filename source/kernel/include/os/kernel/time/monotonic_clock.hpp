#pragma once

#include "os/abi/time.hpp"

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_MONOTONIC_NANOSECONDS_PER_SECOND =
    os::abi::OS_ABI_TIME_NANOSECONDS_PER_SECOND;

enum class MonotonicClockStatus : uint64_t {
    Succeeded,
    NotInitialized,
    AlreadyInitialized,
    InvalidInputFrequency,
    InvalidDivisor,
    InputFrequencyOutOfRange,
    Corrupt,
};

struct MonotonicClockSnapshot final {
    uint64_t input_frequency_hz;
    uint64_t divisor;
    uint64_t delivered_tick_count;
    uint64_t nanoseconds;
    uint64_t fractional_numerator;
    bool saturated;
};

class MonotonicClock final {
  public:
    [[nodiscard]] MonotonicClockStatus Initialize(uint64_t input_frequency_hz,
                                                  uint64_t divisor) noexcept;
    [[nodiscard]] MonotonicClockStatus Advance(uint64_t tick_count) noexcept;
    [[nodiscard]] MonotonicClockSnapshot Read() const noexcept;
    [[nodiscard]] MonotonicClockStatus Validate() const noexcept;

  private:
    void AdvanceChunk(uint64_t tick_count) noexcept;
    void Saturate() noexcept;
    void AddDeliveredTicks(uint64_t tick_count) noexcept;

    uint64_t input_frequency_hz_{};
    uint64_t divisor_{};
    uint64_t delivered_tick_count_{};
    uint64_t nanoseconds_{};
    uint64_t fractional_numerator_{};
    bool saturated_{};
    bool initialized_{};
};

}

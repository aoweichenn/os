#include "os/kernel/time/monotonic_clock.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_MONOTONIC_EMPTY_VALUE = 0ULL;

}

MonotonicClockStatus MonotonicClock::Initialize(const uint64_t input_frequency_hz,
                                                const uint64_t divisor) noexcept {
    if (this->initialized_) {
        return MonotonicClockStatus::AlreadyInitialized;
    }
    if (input_frequency_hz == OS_KERNEL_MONOTONIC_EMPTY_VALUE) {
        return MonotonicClockStatus::InvalidInputFrequency;
    }
    if (divisor == OS_KERNEL_MONOTONIC_EMPTY_VALUE) {
        return MonotonicClockStatus::InvalidDivisor;
    }
    if (input_frequency_hz >
        UINT64_MAX / OS_KERNEL_MONOTONIC_NANOSECONDS_PER_SECOND) {
        return MonotonicClockStatus::InputFrequencyOutOfRange;
    }

    this->input_frequency_hz_ = input_frequency_hz;
    this->divisor_ = divisor;
    this->delivered_tick_count_ = OS_KERNEL_MONOTONIC_EMPTY_VALUE;
    this->nanoseconds_ = OS_KERNEL_MONOTONIC_EMPTY_VALUE;
    this->fractional_numerator_ = OS_KERNEL_MONOTONIC_EMPTY_VALUE;
    this->saturated_ = false;
    this->initialized_ = true;
    return MonotonicClockStatus::Succeeded;
}

MonotonicClockStatus MonotonicClock::Advance(const uint64_t tick_count) noexcept {
    if (!this->initialized_) {
        return MonotonicClockStatus::NotInitialized;
    }
    if (tick_count == OS_KERNEL_MONOTONIC_EMPTY_VALUE) {
        return MonotonicClockStatus::Succeeded;
    }

    this->AddDeliveredTicks(tick_count);
    uint64_t remaining_tick_count = tick_count;
    const uint64_t maximum_chunk_tick_count = UINT64_MAX / this->divisor_;
    while (remaining_tick_count != OS_KERNEL_MONOTONIC_EMPTY_VALUE &&
           !this->saturated_) {
        const uint64_t chunk_tick_count =
            remaining_tick_count < maximum_chunk_tick_count
                ? remaining_tick_count
                : maximum_chunk_tick_count;
        this->AdvanceChunk(chunk_tick_count);
        remaining_tick_count -= chunk_tick_count;
    }
    return MonotonicClockStatus::Succeeded;
}

void MonotonicClock::AdvanceChunk(const uint64_t tick_count) noexcept {
    const uint64_t elapsed_cycle_count = tick_count * this->divisor_;
    const uint64_t whole_second_count = elapsed_cycle_count / this->input_frequency_hz_;
    const uint64_t remaining_cycle_count = elapsed_cycle_count % this->input_frequency_hz_;
    if (whole_second_count >
        (UINT64_MAX - this->nanoseconds_) /
            OS_KERNEL_MONOTONIC_NANOSECONDS_PER_SECOND) {
        this->Saturate();
        return;
    }
    this->nanoseconds_ +=
        whole_second_count * OS_KERNEL_MONOTONIC_NANOSECONDS_PER_SECOND;

    const uint64_t cycle_fraction_numerator =
        remaining_cycle_count *
        OS_KERNEL_MONOTONIC_NANOSECONDS_PER_SECOND;
    const uint64_t base_additional_nanoseconds =
        cycle_fraction_numerator / this->input_frequency_hz_;
    const uint64_t combined_fractional_numerator =
        cycle_fraction_numerator % this->input_frequency_hz_ +
        this->fractional_numerator_;
    const uint64_t fractional_carry_nanoseconds =
        combined_fractional_numerator / this->input_frequency_hz_;
    const uint64_t additional_nanoseconds =
        base_additional_nanoseconds + fractional_carry_nanoseconds;
    if (additional_nanoseconds > UINT64_MAX - this->nanoseconds_) {
        this->Saturate();
        return;
    }
    this->nanoseconds_ += additional_nanoseconds;
    this->fractional_numerator_ =
        combined_fractional_numerator % this->input_frequency_hz_;
}

MonotonicClockSnapshot MonotonicClock::Read() const noexcept {
    return MonotonicClockSnapshot{
        .input_frequency_hz = this->input_frequency_hz_,
        .divisor = this->divisor_,
        .delivered_tick_count = this->delivered_tick_count_,
        .nanoseconds = this->nanoseconds_,
        .fractional_numerator = this->fractional_numerator_,
        .saturated = this->saturated_,
    };
}

MonotonicClockStatus MonotonicClock::Validate() const noexcept {
    if (!this->initialized_) {
        return MonotonicClockStatus::NotInitialized;
    }
    if (this->input_frequency_hz_ == OS_KERNEL_MONOTONIC_EMPTY_VALUE ||
        this->divisor_ == OS_KERNEL_MONOTONIC_EMPTY_VALUE ||
        this->fractional_numerator_ >= this->input_frequency_hz_ ||
        (this->saturated_ &&
         (this->nanoseconds_ != UINT64_MAX ||
          this->fractional_numerator_ != OS_KERNEL_MONOTONIC_EMPTY_VALUE))) {
        return MonotonicClockStatus::Corrupt;
    }
    return MonotonicClockStatus::Succeeded;
}

void MonotonicClock::Saturate() noexcept {
    this->nanoseconds_ = UINT64_MAX;
    this->fractional_numerator_ = OS_KERNEL_MONOTONIC_EMPTY_VALUE;
    this->saturated_ = true;
}

void MonotonicClock::AddDeliveredTicks(const uint64_t tick_count) noexcept {
    if (tick_count > UINT64_MAX - this->delivered_tick_count_) {
        this->delivered_tick_count_ = UINT64_MAX;
        return;
    }
    this->delivered_tick_count_ += tick_count;
}

}

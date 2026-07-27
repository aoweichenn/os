#include "os/kernel/time/monotonic_clock.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_MONOTONIC_CLOCK_SUITE_NAME =
    "kernel/time/monotonic_clock/unit";
constexpr std::string_view OS_TEST_MONOTONIC_CLOCK_EXACT_ACCUMULATION =
    "PIT divisor 的分数余量必须跨批次保留且时间不得倒退";
constexpr std::string_view OS_TEST_MONOTONIC_CLOCK_LONG_TICKS =
    "长 tick 批量换算必须保持精确且不依赖易溢出的连乘";
constexpr std::string_view OS_TEST_MONOTONIC_CLOCK_PRODUCT_OVERFLOW =
    "周期乘积越过 64 位时必须分块换算而不能过早饱和";
constexpr std::string_view OS_TEST_MONOTONIC_CLOCK_FRACTIONAL_OVERFLOW =
    "分数余量相加越过中间乘积余量时必须精确携带";
constexpr std::string_view OS_TEST_MONOTONIC_CLOCK_SATURATION =
    "64 位边界必须永久饱和而不能回绕";
constexpr std::string_view OS_TEST_MONOTONIC_CLOCK_BOUNDARIES =
    "零频率、零 divisor 和过大频率必须明确拒绝";
constexpr uint64_t OS_TEST_MONOTONIC_CLOCK_PIT_INPUT_FREQUENCY_HZ = 1193182ULL;
constexpr uint64_t OS_TEST_MONOTONIC_CLOCK_PIT_DIVISOR = 11932ULL;
constexpr uint64_t OS_TEST_MONOTONIC_CLOCK_BATCH_TICK_COUNT = 1000ULL;
constexpr uint64_t OS_TEST_MONOTONIC_CLOCK_LONG_TICK_COUNT = 1000000000ULL;
constexpr uint64_t OS_TEST_MONOTONIC_CLOCK_SINGLE_TICK_COUNT = 1ULL;
constexpr uint64_t OS_TEST_MONOTONIC_CLOCK_PRODUCT_OVERFLOW_DIVISOR = 2ULL;
constexpr uint64_t OS_TEST_MONOTONIC_CLOCK_PRODUCT_OVERFLOW_EXTRA_TICKS = 2ULL;
constexpr uint64_t OS_TEST_MONOTONIC_CLOCK_FRACTIONAL_PRELOAD_TICKS = 2ULL;
constexpr uint64_t OS_TEST_MONOTONIC_CLOCK_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_MONOTONIC_CLOCK_MINIMUM_FREQUENCY_HZ = 1ULL;
constexpr uint64_t OS_TEST_MONOTONIC_CLOCK_MAXIMUM_FREQUENCY_HZ =
    UINT64_MAX / os::kernel::OS_KERNEL_MONOTONIC_NANOSECONDS_PER_SECOND;

[[nodiscard]] uint64_t ExpectedNanoseconds(const uint64_t tick_count) noexcept {
    const uint64_t cycle_count =
        tick_count * OS_TEST_MONOTONIC_CLOCK_PIT_DIVISOR;
    const uint64_t whole_seconds =
        cycle_count / OS_TEST_MONOTONIC_CLOCK_PIT_INPUT_FREQUENCY_HZ;
    const uint64_t remaining_cycles =
        cycle_count % OS_TEST_MONOTONIC_CLOCK_PIT_INPUT_FREQUENCY_HZ;
    return whole_seconds *
               os::kernel::OS_KERNEL_MONOTONIC_NANOSECONDS_PER_SECOND +
           remaining_cycles *
               os::kernel::OS_KERNEL_MONOTONIC_NANOSECONDS_PER_SECOND /
               OS_TEST_MONOTONIC_CLOCK_PIT_INPUT_FREQUENCY_HZ;
}

}

int main() {
    os::test::TestContext test_context{
        OS_TEST_MONOTONIC_CLOCK_SUITE_NAME};

    os::kernel::MonotonicClock batched_clock{};
    os::kernel::MonotonicClock incremental_clock{};
    bool accumulation_valid =
        batched_clock.Initialize(
            OS_TEST_MONOTONIC_CLOCK_PIT_INPUT_FREQUENCY_HZ,
            OS_TEST_MONOTONIC_CLOCK_PIT_DIVISOR) ==
            os::kernel::MonotonicClockStatus::Succeeded &&
        incremental_clock.Initialize(
            OS_TEST_MONOTONIC_CLOCK_PIT_INPUT_FREQUENCY_HZ,
            OS_TEST_MONOTONIC_CLOCK_PIT_DIVISOR) ==
            os::kernel::MonotonicClockStatus::Succeeded &&
        batched_clock.Advance(OS_TEST_MONOTONIC_CLOCK_BATCH_TICK_COUNT) ==
            os::kernel::MonotonicClockStatus::Succeeded;
    uint64_t previous_nanoseconds = OS_TEST_MONOTONIC_CLOCK_EMPTY_VALUE;
    for (uint64_t tick_index = OS_TEST_MONOTONIC_CLOCK_EMPTY_VALUE;
         accumulation_valid &&
         tick_index < OS_TEST_MONOTONIC_CLOCK_BATCH_TICK_COUNT;
         ++tick_index) {
        accumulation_valid =
            incremental_clock.Advance(
                OS_TEST_MONOTONIC_CLOCK_SINGLE_TICK_COUNT) ==
                os::kernel::MonotonicClockStatus::Succeeded;
        const uint64_t current_nanoseconds =
            incremental_clock.Read().nanoseconds;
        accumulation_valid =
            accumulation_valid &&
            current_nanoseconds >= previous_nanoseconds;
        previous_nanoseconds = current_nanoseconds;
    }
    const os::kernel::MonotonicClockSnapshot batched_snapshot =
        batched_clock.Read();
    const os::kernel::MonotonicClockSnapshot incremental_snapshot =
        incremental_clock.Read();
    test_context.Expect(
        accumulation_valid &&
            batched_snapshot.nanoseconds ==
                incremental_snapshot.nanoseconds &&
            batched_snapshot.fractional_numerator ==
                incremental_snapshot.fractional_numerator &&
            batched_snapshot.nanoseconds ==
                ExpectedNanoseconds(
                    OS_TEST_MONOTONIC_CLOCK_BATCH_TICK_COUNT) &&
            batched_clock.Validate() ==
                os::kernel::MonotonicClockStatus::Succeeded &&
            incremental_clock.Validate() ==
                os::kernel::MonotonicClockStatus::Succeeded,
        OS_TEST_MONOTONIC_CLOCK_EXACT_ACCUMULATION);

    os::kernel::MonotonicClock long_clock{};
    const bool long_ticks_valid =
        long_clock.Initialize(
            OS_TEST_MONOTONIC_CLOCK_PIT_INPUT_FREQUENCY_HZ,
            OS_TEST_MONOTONIC_CLOCK_PIT_DIVISOR) ==
            os::kernel::MonotonicClockStatus::Succeeded &&
        long_clock.Advance(OS_TEST_MONOTONIC_CLOCK_LONG_TICK_COUNT) ==
            os::kernel::MonotonicClockStatus::Succeeded &&
        long_clock.Read().nanoseconds ==
            ExpectedNanoseconds(OS_TEST_MONOTONIC_CLOCK_LONG_TICK_COUNT) &&
        !long_clock.Read().saturated;
    test_context.Expect(long_ticks_valid,
                        OS_TEST_MONOTONIC_CLOCK_LONG_TICKS);

    os::kernel::MonotonicClock product_overflow_clock{};
    os::kernel::MonotonicClock product_overflow_reference_clock{};
    const uint64_t maximum_single_chunk_tick_count =
        UINT64_MAX / OS_TEST_MONOTONIC_CLOCK_PRODUCT_OVERFLOW_DIVISOR;
    const uint64_t product_overflow_tick_count =
        maximum_single_chunk_tick_count +
        OS_TEST_MONOTONIC_CLOCK_PRODUCT_OVERFLOW_EXTRA_TICKS;
    bool product_overflow_valid =
        product_overflow_clock.Initialize(
            OS_TEST_MONOTONIC_CLOCK_MAXIMUM_FREQUENCY_HZ,
            OS_TEST_MONOTONIC_CLOCK_PRODUCT_OVERFLOW_DIVISOR) ==
            os::kernel::MonotonicClockStatus::Succeeded &&
        product_overflow_reference_clock.Initialize(
            OS_TEST_MONOTONIC_CLOCK_MAXIMUM_FREQUENCY_HZ,
            OS_TEST_MONOTONIC_CLOCK_PRODUCT_OVERFLOW_DIVISOR) ==
            os::kernel::MonotonicClockStatus::Succeeded &&
        product_overflow_clock.Advance(product_overflow_tick_count) ==
            os::kernel::MonotonicClockStatus::Succeeded &&
        product_overflow_reference_clock.Advance(
            maximum_single_chunk_tick_count) ==
            os::kernel::MonotonicClockStatus::Succeeded;
    for (uint64_t tick_index = OS_TEST_MONOTONIC_CLOCK_EMPTY_VALUE;
         product_overflow_valid &&
         tick_index < OS_TEST_MONOTONIC_CLOCK_PRODUCT_OVERFLOW_EXTRA_TICKS;
         ++tick_index) {
        product_overflow_valid =
            product_overflow_reference_clock.Advance(
                OS_TEST_MONOTONIC_CLOCK_SINGLE_TICK_COUNT) ==
            os::kernel::MonotonicClockStatus::Succeeded;
    }
    const os::kernel::MonotonicClockSnapshot product_overflow_snapshot =
        product_overflow_clock.Read();
    const os::kernel::MonotonicClockSnapshot
        product_overflow_reference_snapshot =
            product_overflow_reference_clock.Read();
    test_context.Expect(
        product_overflow_valid &&
            !product_overflow_snapshot.saturated &&
            product_overflow_snapshot.delivered_tick_count ==
                product_overflow_tick_count &&
            product_overflow_snapshot.nanoseconds ==
                product_overflow_reference_snapshot.nanoseconds &&
            product_overflow_snapshot.fractional_numerator ==
                product_overflow_reference_snapshot.fractional_numerator &&
            product_overflow_clock.Validate() ==
                os::kernel::MonotonicClockStatus::Succeeded,
        OS_TEST_MONOTONIC_CLOCK_PRODUCT_OVERFLOW);

    os::kernel::MonotonicClock fractional_overflow_clock{};
    os::kernel::MonotonicClock fractional_overflow_reference_clock{};
    const uint64_t fractional_large_tick_count =
        OS_TEST_MONOTONIC_CLOCK_MAXIMUM_FREQUENCY_HZ -
        OS_TEST_MONOTONIC_CLOCK_SINGLE_TICK_COUNT;
    const uint64_t fractional_first_split_tick_count =
        fractional_large_tick_count /
        OS_TEST_MONOTONIC_CLOCK_PRODUCT_OVERFLOW_DIVISOR;
    const uint64_t fractional_second_split_tick_count =
        fractional_large_tick_count -
        fractional_first_split_tick_count;
    const bool fractional_overflow_valid =
        fractional_overflow_clock.Initialize(
            OS_TEST_MONOTONIC_CLOCK_MAXIMUM_FREQUENCY_HZ,
            OS_TEST_MONOTONIC_CLOCK_SINGLE_TICK_COUNT) ==
            os::kernel::MonotonicClockStatus::Succeeded &&
        fractional_overflow_reference_clock.Initialize(
            OS_TEST_MONOTONIC_CLOCK_MAXIMUM_FREQUENCY_HZ,
            OS_TEST_MONOTONIC_CLOCK_SINGLE_TICK_COUNT) ==
            os::kernel::MonotonicClockStatus::Succeeded &&
        fractional_overflow_clock.Advance(
            OS_TEST_MONOTONIC_CLOCK_FRACTIONAL_PRELOAD_TICKS) ==
            os::kernel::MonotonicClockStatus::Succeeded &&
        fractional_overflow_reference_clock.Advance(
            OS_TEST_MONOTONIC_CLOCK_FRACTIONAL_PRELOAD_TICKS) ==
            os::kernel::MonotonicClockStatus::Succeeded &&
        fractional_overflow_clock.Advance(
            fractional_large_tick_count) ==
            os::kernel::MonotonicClockStatus::Succeeded &&
        fractional_overflow_reference_clock.Advance(
            fractional_first_split_tick_count) ==
            os::kernel::MonotonicClockStatus::Succeeded &&
        fractional_overflow_reference_clock.Advance(
            fractional_second_split_tick_count) ==
            os::kernel::MonotonicClockStatus::Succeeded;
    const os::kernel::MonotonicClockSnapshot fractional_overflow_snapshot =
        fractional_overflow_clock.Read();
    const os::kernel::MonotonicClockSnapshot
        fractional_overflow_reference_snapshot =
            fractional_overflow_reference_clock.Read();
    test_context.Expect(
        fractional_overflow_valid &&
            !fractional_overflow_snapshot.saturated &&
            fractional_overflow_snapshot.nanoseconds ==
                fractional_overflow_reference_snapshot.nanoseconds &&
            fractional_overflow_snapshot.fractional_numerator ==
                fractional_overflow_reference_snapshot.fractional_numerator &&
            fractional_overflow_clock.Validate() ==
                os::kernel::MonotonicClockStatus::Succeeded,
        OS_TEST_MONOTONIC_CLOCK_FRACTIONAL_OVERFLOW);

    os::kernel::MonotonicClock saturation_clock{};
    const bool saturation_valid =
        saturation_clock.Initialize(
            OS_TEST_MONOTONIC_CLOCK_MINIMUM_FREQUENCY_HZ, UINT64_MAX) ==
            os::kernel::MonotonicClockStatus::Succeeded &&
        saturation_clock.Advance(
            OS_TEST_MONOTONIC_CLOCK_SINGLE_TICK_COUNT) ==
            os::kernel::MonotonicClockStatus::Succeeded &&
        saturation_clock.Read().nanoseconds == UINT64_MAX &&
        saturation_clock.Read().saturated &&
        saturation_clock.Advance(UINT64_MAX) ==
            os::kernel::MonotonicClockStatus::Succeeded &&
        saturation_clock.Read().nanoseconds == UINT64_MAX &&
        saturation_clock.Validate() ==
            os::kernel::MonotonicClockStatus::Succeeded;
    test_context.Expect(saturation_valid,
                        OS_TEST_MONOTONIC_CLOCK_SATURATION);

    os::kernel::MonotonicClock invalid_frequency_clock{};
    os::kernel::MonotonicClock invalid_divisor_clock{};
    os::kernel::MonotonicClock excessive_frequency_clock{};
    test_context.Expect(
        invalid_frequency_clock.Initialize(
            OS_TEST_MONOTONIC_CLOCK_EMPTY_VALUE,
            OS_TEST_MONOTONIC_CLOCK_PIT_DIVISOR) ==
            os::kernel::MonotonicClockStatus::InvalidInputFrequency &&
            invalid_divisor_clock.Initialize(
                OS_TEST_MONOTONIC_CLOCK_PIT_INPUT_FREQUENCY_HZ,
                OS_TEST_MONOTONIC_CLOCK_EMPTY_VALUE) ==
                os::kernel::MonotonicClockStatus::InvalidDivisor &&
            excessive_frequency_clock.Initialize(
                UINT64_MAX,
                OS_TEST_MONOTONIC_CLOCK_PIT_DIVISOR) ==
                os::kernel::MonotonicClockStatus::
                    InputFrequencyOutOfRange,
        OS_TEST_MONOTONIC_CLOCK_BOUNDARIES);
    return test_context.ExitCode();
}

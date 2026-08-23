#include <os/kernel/memory/file_readahead.hpp>

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_FILE_READAHEAD_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_FILE_READAHEAD_SINGLE_VALUE = 1ULL;
constexpr uint64_t OS_KERNEL_FILE_READAHEAD_HIGH_PRESSURE_DIVISOR = 2ULL;
constexpr uint64_t OS_KERNEL_FILE_READAHEAD_LOW_PRESSURE_DIVISOR = 4ULL;
constexpr uint64_t OS_KERNEL_FILE_READAHEAD_SMALL_WINDOW_DIVISOR = 16ULL;
constexpr uint64_t OS_KERNEL_FILE_READAHEAD_MEDIUM_WINDOW_DIVISOR = 2ULL;
constexpr uint64_t OS_KERNEL_FILE_READAHEAD_SMALL_WINDOW_MULTIPLIER = 4ULL;
constexpr uint64_t OS_KERNEL_FILE_READAHEAD_MEDIUM_WINDOW_MULTIPLIER = 2ULL;
constexpr uint64_t OS_KERNEL_FILE_READAHEAD_INITIAL_SMALL_WINDOW_DIVISOR = 32ULL;
constexpr uint64_t OS_KERNEL_FILE_READAHEAD_INITIAL_MEDIUM_WINDOW_DIVISOR = 4ULL;

[[nodiscard]] uint64_t Minimum(const uint64_t left, const uint64_t right) noexcept {
    return left < right ? left : right;
}

[[nodiscard]] uint64_t Maximum(const uint64_t left, const uint64_t right) noexcept {
    return left > right ? left : right;
}

[[nodiscard]] bool TryIncrement(uint64_t &value) noexcept {
    if (value == UINT64_MAX) {
        return false;
    }
    ++value;
    return true;
}

[[nodiscard]] bool TryAdd(uint64_t &value, const uint64_t increment) noexcept {
    if (value > UINT64_MAX - increment) {
        return false;
    }
    value += increment;
    return true;
}

[[nodiscard]] bool ThreeCountsEqual(const uint64_t expected, const uint64_t first,
                                    const uint64_t second, const uint64_t third) noexcept {
    if (first > UINT64_MAX - second) {
        return false;
    }
    const uint64_t first_pair = first + second;
    return first_pair <= UINT64_MAX - third && expected == first_pair + third;
}

[[nodiscard]] bool SumDoesNotExceed(const uint64_t first, const uint64_t second,
                                    const uint64_t maximum) noexcept {
    return first <= UINT64_MAX - second && first + second <= maximum;
}

[[nodiscard]] bool TriggerIsValid(const FileReadaheadTrigger trigger) noexcept {
    return trigger == FileReadaheadTrigger::DemandHit ||
           trigger == FileReadaheadTrigger::DemandMiss ||
           trigger == FileReadaheadTrigger::PrefetchedHit;
}

[[nodiscard]] bool PressureLevelIsValid(const MemoryPressureLevel pressure_level) noexcept {
    return pressure_level == MemoryPressureLevel::Balanced ||
           pressure_level == MemoryPressureLevel::BelowHigh ||
           pressure_level == MemoryPressureLevel::BelowLow ||
           pressure_level == MemoryPressureLevel::BelowMinimum;
}

[[nodiscard]] uint64_t EffectiveMaximumWindow(const uint64_t configured_maximum_window_page_count,
                                              const uint64_t adaptive_maximum_window_page_count,
                                              const MemoryPressureLevel pressure_level) noexcept {
    if (pressure_level == MemoryPressureLevel::BelowMinimum) {
        return OS_KERNEL_FILE_READAHEAD_EMPTY_VALUE;
    }
    uint64_t pressure_maximum_window_page_count = configured_maximum_window_page_count;
    if (pressure_level == MemoryPressureLevel::BelowHigh) {
        pressure_maximum_window_page_count = Maximum(
            OS_KERNEL_FILE_READAHEAD_SINGLE_VALUE,
            configured_maximum_window_page_count / OS_KERNEL_FILE_READAHEAD_HIGH_PRESSURE_DIVISOR);
    } else if (pressure_level == MemoryPressureLevel::BelowLow) {
        pressure_maximum_window_page_count = Maximum(
            OS_KERNEL_FILE_READAHEAD_SINGLE_VALUE,
            configured_maximum_window_page_count / OS_KERNEL_FILE_READAHEAD_LOW_PRESSURE_DIVISOR);
    }
    return Minimum(adaptive_maximum_window_page_count, pressure_maximum_window_page_count);
}

void ClearWindow(FileReadaheadStatistics &statistics) noexcept {
    statistics.window_start_page_index = OS_KERNEL_FILE_READAHEAD_EMPTY_VALUE;
    statistics.window_page_count = OS_KERNEL_FILE_READAHEAD_EMPTY_VALUE;
    statistics.asynchronous_page_count = OS_KERNEL_FILE_READAHEAD_EMPTY_VALUE;
    statistics.trigger_page_index = OS_KERNEL_FILE_READAHEAD_EMPTY_VALUE;
    statistics.window_active = false;
}

void ClearStream(FileReadaheadStatistics &statistics) noexcept {
    ClearWindow(statistics);
    statistics.next_expected_page_index = OS_KERNEL_FILE_READAHEAD_EMPTY_VALUE;
    statistics.stream_active = false;
}

[[nodiscard]] uint64_t RoundUpPowerOfTwo(const uint64_t value, const uint64_t maximum) noexcept {
    uint64_t rounded_value = OS_KERNEL_FILE_READAHEAD_SINGLE_VALUE;
    while (rounded_value < value && rounded_value <= maximum / 2ULL) {
        rounded_value *= 2ULL;
    }
    return rounded_value < value ? maximum : rounded_value;
}

[[nodiscard]] uint64_t InitialWindowPageCount(const uint64_t requested_page_count,
                                              const uint64_t maximum_window_page_count) noexcept {
    if (requested_page_count >= maximum_window_page_count) {
        return requested_page_count;
    }
    uint64_t window_page_count = RoundUpPowerOfTwo(requested_page_count, maximum_window_page_count);
    if (window_page_count <=
        maximum_window_page_count / OS_KERNEL_FILE_READAHEAD_INITIAL_SMALL_WINDOW_DIVISOR) {
        window_page_count *= OS_KERNEL_FILE_READAHEAD_SMALL_WINDOW_MULTIPLIER;
    } else if (window_page_count <=
               maximum_window_page_count / OS_KERNEL_FILE_READAHEAD_INITIAL_MEDIUM_WINDOW_DIVISOR) {
        window_page_count *= OS_KERNEL_FILE_READAHEAD_MEDIUM_WINDOW_MULTIPLIER;
    } else {
        window_page_count = maximum_window_page_count;
    }
    return Minimum(window_page_count, maximum_window_page_count);
}

[[nodiscard]] uint64_t NextWindowPageCount(const uint64_t current_window_page_count,
                                           const uint64_t maximum_window_page_count) noexcept {
    if (current_window_page_count >= maximum_window_page_count) {
        return maximum_window_page_count;
    }
    if (current_window_page_count <
        maximum_window_page_count / OS_KERNEL_FILE_READAHEAD_SMALL_WINDOW_DIVISOR) {
        return Minimum(maximum_window_page_count,
                       current_window_page_count *
                           OS_KERNEL_FILE_READAHEAD_SMALL_WINDOW_MULTIPLIER);
    }
    if (current_window_page_count <=
        maximum_window_page_count / OS_KERNEL_FILE_READAHEAD_MEDIUM_WINDOW_DIVISOR) {
        return Minimum(maximum_window_page_count,
                       current_window_page_count *
                           OS_KERNEL_FILE_READAHEAD_MEDIUM_WINDOW_MULTIPLIER);
    }
    return maximum_window_page_count;
}

[[nodiscard]] bool AccessContainsPage(const FileReadaheadAccess &access,
                                      const uint64_t access_end_page_index,
                                      const uint64_t page_index) noexcept {
    return access.first_page_index <= page_index && page_index < access_end_page_index;
}

[[nodiscard]] bool RecordAccessKind(FileReadaheadStatistics &statistics,
                                    const FileReadaheadTrigger trigger) noexcept {
    if (trigger == FileReadaheadTrigger::DemandHit) {
        return TryIncrement(statistics.demand_hit_access_count);
    }
    if (trigger == FileReadaheadTrigger::DemandMiss) {
        return TryIncrement(statistics.demand_miss_access_count);
    }
    return TryIncrement(statistics.prefetched_hit_access_count);
}

[[nodiscard]] bool BuildInitialDecision(const FileReadaheadAccess &access,
                                        const uint64_t access_end_page_index,
                                        FileReadaheadStatistics &statistics,
                                        FileReadaheadDecision &decision) noexcept {
    const uint64_t initial_window_page_count = InitialWindowPageCount(
        access.requested_page_count, statistics.effective_maximum_window_page_count);
    const uint64_t available_page_count = access.file_page_count - access.first_page_index;
    const uint64_t window_page_count = Minimum(initial_window_page_count, available_page_count);
    if (window_page_count <= access.requested_page_count) {
        ClearWindow(statistics);
        return true;
    }
    const uint64_t prefetch_page_count = window_page_count - access.requested_page_count;
    if (statistics.generation == UINT64_MAX) {
        return false;
    }
    const uint64_t generation = statistics.generation + OS_KERNEL_FILE_READAHEAD_SINGLE_VALUE;
    decision = FileReadaheadDecision{
        .action = FileReadaheadAction::Submit,
        .generation = generation,
        .window_start_page_index = access.first_page_index,
        .window_page_count = window_page_count,
        .prefetch_start_page_index = access_end_page_index,
        .prefetch_page_count = prefetch_page_count,
        .trigger_page_index = access_end_page_index,
        .effective_maximum_window_page_count = statistics.effective_maximum_window_page_count,
        .sequential_access = decision.sequential_access,
        .stream_reset = decision.stream_reset,
    };
    statistics.window_start_page_index = access.first_page_index;
    statistics.window_page_count = window_page_count;
    statistics.asynchronous_page_count = prefetch_page_count;
    statistics.trigger_page_index = access_end_page_index;
    statistics.window_active = true;
    statistics.generation = generation;
    return true;
}

[[nodiscard]] bool BuildNextDecision(const FileReadaheadAccess &access,
                                     FileReadaheadStatistics &statistics,
                                     FileReadaheadDecision &decision) noexcept {
    const uint64_t current_window_end_page_index =
        statistics.window_start_page_index + statistics.window_page_count;
    if (current_window_end_page_index >= access.file_page_count) {
        ClearWindow(statistics);
        return true;
    }
    const uint64_t requested_window_page_count = NextWindowPageCount(
        statistics.window_page_count, statistics.effective_maximum_window_page_count);
    const uint64_t available_page_count = access.file_page_count - current_window_end_page_index;
    const uint64_t window_page_count = Minimum(requested_window_page_count, available_page_count);
    if (window_page_count == OS_KERNEL_FILE_READAHEAD_EMPTY_VALUE) {
        ClearWindow(statistics);
        return true;
    }
    if (statistics.generation == UINT64_MAX) {
        return false;
    }
    const uint64_t previous_window_page_count = statistics.window_page_count;
    const uint64_t generation = statistics.generation + OS_KERNEL_FILE_READAHEAD_SINGLE_VALUE;
    decision = FileReadaheadDecision{
        .action = FileReadaheadAction::Submit,
        .generation = generation,
        .window_start_page_index = current_window_end_page_index,
        .window_page_count = window_page_count,
        .prefetch_start_page_index = current_window_end_page_index,
        .prefetch_page_count = window_page_count,
        .trigger_page_index = current_window_end_page_index,
        .effective_maximum_window_page_count = statistics.effective_maximum_window_page_count,
        .sequential_access = decision.sequential_access,
        .stream_reset = decision.stream_reset,
    };
    statistics.window_start_page_index = current_window_end_page_index;
    statistics.window_page_count = window_page_count;
    statistics.asynchronous_page_count = window_page_count;
    statistics.trigger_page_index = current_window_end_page_index;
    statistics.window_active = true;
    statistics.generation = generation;
    if (window_page_count > previous_window_page_count) {
        return TryIncrement(statistics.window_growth_count);
    }
    return window_page_count < previous_window_page_count
               ? TryIncrement(statistics.window_shrink_count)
               : true;
}

[[nodiscard]] bool RecordDecision(FileReadaheadStatistics &statistics,
                                  const FileReadaheadDecision &decision) noexcept {
    return decision.action == FileReadaheadAction::None ||
           (TryIncrement(statistics.submission_decision_count) &&
            TryAdd(statistics.planned_window_page_count, decision.window_page_count) &&
            TryAdd(statistics.planned_prefetch_page_count, decision.prefetch_page_count));
}

}

FileReadaheadStatus
FileReadaheadPolicy::Initialize(const FileReadaheadConfiguration &configuration) noexcept {
    if (this->initialized_) {
        return FileReadaheadStatus::AlreadyInitialized;
    }
    if (configuration.maximum_window_page_count == OS_KERNEL_FILE_READAHEAD_EMPTY_VALUE) {
        return FileReadaheadStatus::InvalidConfiguration;
    }
    this->configuration_ = configuration;
    this->statistics_ = FileReadaheadStatistics{};
    this->statistics_.configured_maximum_window_page_count =
        configuration.maximum_window_page_count;
    this->statistics_.adaptive_maximum_window_page_count = configuration.maximum_window_page_count;
    this->statistics_.effective_maximum_window_page_count = configuration.maximum_window_page_count;
    this->statistics_.pressure_level = MemoryPressureLevel::Balanced;
    this->initialized_ = true;
    return FileReadaheadStatus::Succeeded;
}

FileReadaheadStatus FileReadaheadPolicy::ObserveAccess(const FileReadaheadAccess &access,
                                                       FileReadaheadDecision &decision) noexcept {
    decision = FileReadaheadDecision{};
    if (!this->initialized_) {
        return FileReadaheadStatus::NotInitialized;
    }
    if (!TriggerIsValid(access.trigger) || !PressureLevelIsValid(access.pressure_level) ||
        access.requested_page_count == OS_KERNEL_FILE_READAHEAD_EMPTY_VALUE ||
        access.file_page_count == OS_KERNEL_FILE_READAHEAD_EMPTY_VALUE ||
        access.first_page_index >= access.file_page_count ||
        access.requested_page_count > access.file_page_count - access.first_page_index) {
        return FileReadaheadStatus::InvalidAccess;
    }
    const uint64_t access_end_page_index = access.first_page_index + access.requested_page_count;
    const bool initial_access = !this->statistics_.stream_active;
    const bool sequential_access =
        this->statistics_.stream_active &&
        access.first_page_index == this->statistics_.next_expected_page_index;
    const bool random_access = this->statistics_.stream_active && !sequential_access;
    if (access.trigger == FileReadaheadTrigger::PrefetchedHit &&
        (!sequential_access || !this->statistics_.window_active ||
         !AccessContainsPage(access, access_end_page_index,
                             this->statistics_.trigger_page_index))) {
        return FileReadaheadStatus::InvalidAccess;
    }

    FileReadaheadStatistics candidate = this->statistics_;
    decision.sequential_access = sequential_access;
    decision.stream_reset = random_access;
    candidate.pressure_level = access.pressure_level;
    candidate.effective_maximum_window_page_count =
        EffectiveMaximumWindow(candidate.configured_maximum_window_page_count,
                               candidate.adaptive_maximum_window_page_count, access.pressure_level);
    if (!TryIncrement(candidate.access_count) ||
        (initial_access && !TryIncrement(candidate.initial_access_count)) ||
        (sequential_access && !TryIncrement(candidate.sequential_access_count)) ||
        (random_access && (!TryIncrement(candidate.random_access_count) ||
                           !TryIncrement(candidate.stream_reset_count))) ||
        !RecordAccessKind(candidate, access.trigger)) {
        return FileReadaheadStatus::CounterOverflow;
    }
    if (random_access) {
        ClearStream(candidate);
    }
    if (candidate.effective_maximum_window_page_count <
            candidate.adaptive_maximum_window_page_count &&
        access.pressure_level != MemoryPressureLevel::BelowMinimum &&
        !TryIncrement(candidate.pressure_limited_access_count)) {
        return FileReadaheadStatus::CounterOverflow;
    }
    if (access.pressure_level == MemoryPressureLevel::BelowMinimum) {
        if (!TryIncrement(candidate.pressure_disabled_access_count)) {
            return FileReadaheadStatus::CounterOverflow;
        }
        ClearWindow(candidate);
    } else if (access.trigger == FileReadaheadTrigger::DemandMiss &&
               (initial_access || sequential_access ||
                access.first_page_index == OS_KERNEL_FILE_READAHEAD_EMPTY_VALUE)) {
        if (!BuildInitialDecision(access, access_end_page_index, candidate, decision)) {
            return candidate.generation == UINT64_MAX ? FileReadaheadStatus::GenerationExhausted
                                                      : FileReadaheadStatus::CounterOverflow;
        }
    } else if (access.trigger == FileReadaheadTrigger::PrefetchedHit) {
        if (!BuildNextDecision(access, candidate, decision)) {
            return candidate.generation == UINT64_MAX ? FileReadaheadStatus::GenerationExhausted
                                                      : FileReadaheadStatus::CounterOverflow;
        }
    } else if (random_access) {
        ClearWindow(candidate);
    }
    if (!RecordDecision(candidate, decision)) {
        return FileReadaheadStatus::CounterOverflow;
    }
    candidate.stream_active = true;
    candidate.next_expected_page_index = access_end_page_index;
    decision.effective_maximum_window_page_count = candidate.effective_maximum_window_page_count;
    this->statistics_ = candidate;
    return FileReadaheadStatus::Succeeded;
}

FileReadaheadStatus FileReadaheadPolicy::RecordFeedback(const uint64_t useful_page_count,
                                                        const uint64_t wasted_page_count) noexcept {
    if (!this->initialized_) {
        return FileReadaheadStatus::NotInitialized;
    }
    if (useful_page_count == OS_KERNEL_FILE_READAHEAD_EMPTY_VALUE &&
        wasted_page_count == OS_KERNEL_FILE_READAHEAD_EMPTY_VALUE) {
        return FileReadaheadStatus::InvalidFeedback;
    }
    FileReadaheadStatistics candidate = this->statistics_;
    if (!TryIncrement(candidate.feedback_count) ||
        !TryAdd(candidate.useful_prefetched_page_count, useful_page_count) ||
        !TryAdd(candidate.wasted_prefetched_page_count, wasted_page_count)) {
        return FileReadaheadStatus::CounterOverflow;
    }
    if (wasted_page_count > useful_page_count &&
        candidate.adaptive_maximum_window_page_count > OS_KERNEL_FILE_READAHEAD_SINGLE_VALUE) {
        candidate.adaptive_maximum_window_page_count =
            Maximum(OS_KERNEL_FILE_READAHEAD_SINGLE_VALUE,
                    candidate.adaptive_maximum_window_page_count /
                            OS_KERNEL_FILE_READAHEAD_HIGH_PRESSURE_DIVISOR +
                        candidate.adaptive_maximum_window_page_count %
                            OS_KERNEL_FILE_READAHEAD_HIGH_PRESSURE_DIVISOR);
        if (!TryIncrement(candidate.feedback_shrink_count)) {
            return FileReadaheadStatus::CounterOverflow;
        }
    } else if (useful_page_count > wasted_page_count &&
               candidate.adaptive_maximum_window_page_count <
                   candidate.configured_maximum_window_page_count) {
        candidate.adaptive_maximum_window_page_count =
            candidate.adaptive_maximum_window_page_count >
                    candidate.configured_maximum_window_page_count /
                        OS_KERNEL_FILE_READAHEAD_HIGH_PRESSURE_DIVISOR
                ? candidate.configured_maximum_window_page_count
                : candidate.adaptive_maximum_window_page_count *
                      OS_KERNEL_FILE_READAHEAD_HIGH_PRESSURE_DIVISOR;
        if (!TryIncrement(candidate.feedback_recovery_count)) {
            return FileReadaheadStatus::CounterOverflow;
        }
    }
    candidate.effective_maximum_window_page_count = EffectiveMaximumWindow(
        candidate.configured_maximum_window_page_count,
        candidate.adaptive_maximum_window_page_count, candidate.pressure_level);
    this->statistics_ = candidate;
    return FileReadaheadStatus::Succeeded;
}

FileReadaheadStatus FileReadaheadPolicy::Reset() noexcept {
    if (!this->initialized_) {
        return FileReadaheadStatus::NotInitialized;
    }
    FileReadaheadStatistics candidate = this->statistics_;
    if (!TryIncrement(candidate.stream_reset_count)) {
        return FileReadaheadStatus::CounterOverflow;
    }
    ClearStream(candidate);
    this->statistics_ = candidate;
    return FileReadaheadStatus::Succeeded;
}

FileReadaheadStatistics FileReadaheadPolicy::Statistics() const noexcept {
    return this->statistics_;
}

FileReadaheadStatus FileReadaheadPolicy::Validate() const noexcept {
    if (!this->initialized_) {
        return FileReadaheadStatus::NotInitialized;
    }
    const FileReadaheadStatistics &statistics = this->statistics_;
    if (this->configuration_.maximum_window_page_count == OS_KERNEL_FILE_READAHEAD_EMPTY_VALUE ||
        statistics.configured_maximum_window_page_count !=
            this->configuration_.maximum_window_page_count ||
        statistics.adaptive_maximum_window_page_count == OS_KERNEL_FILE_READAHEAD_EMPTY_VALUE ||
        statistics.adaptive_maximum_window_page_count >
            statistics.configured_maximum_window_page_count ||
        !PressureLevelIsValid(statistics.pressure_level) ||
        statistics.effective_maximum_window_page_count !=
            EffectiveMaximumWindow(statistics.configured_maximum_window_page_count,
                                   statistics.adaptive_maximum_window_page_count,
                                   statistics.pressure_level) ||
        !ThreeCountsEqual(statistics.access_count, statistics.initial_access_count,
                          statistics.sequential_access_count, statistics.random_access_count) ||
        !ThreeCountsEqual(statistics.access_count, statistics.demand_hit_access_count,
                          statistics.demand_miss_access_count,
                          statistics.prefetched_hit_access_count) ||
        statistics.submission_decision_count != statistics.generation ||
        statistics.planned_prefetch_page_count > statistics.planned_window_page_count ||
        !SumDoesNotExceed(statistics.feedback_shrink_count, statistics.feedback_recovery_count,
                          statistics.feedback_count) ||
        !SumDoesNotExceed(statistics.window_growth_count, statistics.window_shrink_count,
                          statistics.submission_decision_count) ||
        statistics.stream_reset_count < statistics.random_access_count ||
        (!statistics.stream_active &&
         statistics.next_expected_page_index != OS_KERNEL_FILE_READAHEAD_EMPTY_VALUE) ||
        (statistics.stream_active &&
         statistics.next_expected_page_index == OS_KERNEL_FILE_READAHEAD_EMPTY_VALUE) ||
        (statistics.pressure_level == MemoryPressureLevel::BelowMinimum &&
         statistics.window_active)) {
        return FileReadaheadStatus::Corrupt;
    }
    if (!statistics.window_active) {
        return statistics.window_start_page_index == OS_KERNEL_FILE_READAHEAD_EMPTY_VALUE &&
                       statistics.window_page_count == OS_KERNEL_FILE_READAHEAD_EMPTY_VALUE &&
                       statistics.asynchronous_page_count == OS_KERNEL_FILE_READAHEAD_EMPTY_VALUE &&
                       statistics.trigger_page_index == OS_KERNEL_FILE_READAHEAD_EMPTY_VALUE
                   ? FileReadaheadStatus::Succeeded
                   : FileReadaheadStatus::Corrupt;
    }
    if (!statistics.stream_active ||
        statistics.window_page_count == OS_KERNEL_FILE_READAHEAD_EMPTY_VALUE ||
        statistics.asynchronous_page_count == OS_KERNEL_FILE_READAHEAD_EMPTY_VALUE ||
        statistics.asynchronous_page_count > statistics.window_page_count ||
        statistics.window_start_page_index > UINT64_MAX - statistics.window_page_count) {
        return FileReadaheadStatus::Corrupt;
    }
    const uint64_t expected_trigger_page_index = statistics.window_start_page_index +
                                                 statistics.window_page_count -
                                                 statistics.asynchronous_page_count;
    return statistics.trigger_page_index == expected_trigger_page_index
               ? FileReadaheadStatus::Succeeded
               : FileReadaheadStatus::Corrupt;
}

}

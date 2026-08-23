#include <os/kernel/memory/file_readahead.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_FILE_READAHEAD_RANDOMIZED_SUITE_NAME =
    "kernel/file_readahead/randomized";
constexpr std::string_view OS_TEST_FILE_READAHEAD_RANDOMIZED_MESSAGE =
    "固定种子十万步访问、反馈和重置必须与独立预读窗口模型一致";
constexpr uint64_t OS_TEST_FILE_READAHEAD_RANDOMIZED_SEED = 0x5245414441484541ULL;
constexpr uint64_t OS_TEST_FILE_READAHEAD_RANDOMIZED_ITERATION_COUNT = 100000ULL;
constexpr uint64_t OS_TEST_FILE_READAHEAD_RANDOMIZED_FILE_PAGE_COUNT = 4096ULL;
constexpr uint64_t OS_TEST_FILE_READAHEAD_RANDOMIZED_MAXIMUM_WINDOW_PAGE_COUNT =
    os::kernel::OS_KERNEL_FILE_READAHEAD_DEFAULT_MAXIMUM_WINDOW_PAGE_COUNT;
constexpr uint64_t OS_TEST_FILE_READAHEAD_RANDOMIZED_MAXIMUM_REQUEST_PAGE_COUNT = 8ULL;
constexpr uint64_t OS_TEST_FILE_READAHEAD_RANDOMIZED_MAXIMUM_FEEDBACK_PAGE_COUNT = 15ULL;
constexpr uint64_t OS_TEST_FILE_READAHEAD_RANDOMIZED_ACCESS_OPERATION_LIMIT = 75ULL;
constexpr uint64_t OS_TEST_FILE_READAHEAD_RANDOMIZED_FEEDBACK_OPERATION_LIMIT = 90ULL;
constexpr uint64_t OS_TEST_FILE_READAHEAD_RANDOMIZED_OPERATION_MODULUS = 100ULL;
constexpr uint64_t OS_TEST_FILE_READAHEAD_RANDOMIZED_SEQUENTIAL_PERCENT = 70ULL;
constexpr uint64_t OS_TEST_FILE_READAHEAD_RANDOMIZED_PREFETCH_TRIGGER_MODULUS = 4ULL;
constexpr uint64_t OS_TEST_FILE_READAHEAD_RANDOMIZED_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_FILE_READAHEAD_RANDOMIZED_SINGLE_VALUE = 1ULL;

struct ReferenceModel final {
    os::kernel::FileReadaheadStatistics statistics;
};

[[nodiscard]] uint64_t NextRandom(uint64_t &state) noexcept {
    state ^= state << 13ULL;
    state ^= state >> 7ULL;
    state ^= state << 17ULL;
    return state;
}

[[nodiscard]] uint64_t Minimum(const uint64_t left, const uint64_t right) noexcept {
    return left < right ? left : right;
}

[[nodiscard]] uint64_t Maximum(const uint64_t left, const uint64_t right) noexcept {
    return left > right ? left : right;
}

[[nodiscard]] uint64_t
EffectiveMaximum(const uint64_t adaptive_maximum_window_page_count,
                 const os::kernel::MemoryPressureLevel pressure_level) noexcept {
    if (pressure_level == os::kernel::MemoryPressureLevel::BelowMinimum) {
        return OS_TEST_FILE_READAHEAD_RANDOMIZED_EMPTY_VALUE;
    }
    uint64_t pressure_maximum_window_page_count =
        OS_TEST_FILE_READAHEAD_RANDOMIZED_MAXIMUM_WINDOW_PAGE_COUNT;
    if (pressure_level == os::kernel::MemoryPressureLevel::BelowHigh) {
        pressure_maximum_window_page_count =
            OS_TEST_FILE_READAHEAD_RANDOMIZED_MAXIMUM_WINDOW_PAGE_COUNT / 2ULL;
    } else if (pressure_level == os::kernel::MemoryPressureLevel::BelowLow) {
        pressure_maximum_window_page_count =
            OS_TEST_FILE_READAHEAD_RANDOMIZED_MAXIMUM_WINDOW_PAGE_COUNT / 4ULL;
    }
    return Minimum(adaptive_maximum_window_page_count, pressure_maximum_window_page_count);
}

void ClearWindow(os::kernel::FileReadaheadStatistics &statistics) noexcept {
    statistics.window_start_page_index = OS_TEST_FILE_READAHEAD_RANDOMIZED_EMPTY_VALUE;
    statistics.window_page_count = OS_TEST_FILE_READAHEAD_RANDOMIZED_EMPTY_VALUE;
    statistics.asynchronous_page_count = OS_TEST_FILE_READAHEAD_RANDOMIZED_EMPTY_VALUE;
    statistics.trigger_page_index = OS_TEST_FILE_READAHEAD_RANDOMIZED_EMPTY_VALUE;
    statistics.window_active = false;
}

void ClearStream(os::kernel::FileReadaheadStatistics &statistics) noexcept {
    ClearWindow(statistics);
    statistics.next_expected_page_index = OS_TEST_FILE_READAHEAD_RANDOMIZED_EMPTY_VALUE;
    statistics.stream_active = false;
}

[[nodiscard]] uint64_t RoundUpPowerOfTwo(const uint64_t value, const uint64_t maximum) noexcept {
    uint64_t rounded_value = OS_TEST_FILE_READAHEAD_RANDOMIZED_SINGLE_VALUE;
    while (rounded_value < value && rounded_value <= maximum / 2ULL) {
        rounded_value *= 2ULL;
    }
    return rounded_value < value ? maximum : rounded_value;
}

[[nodiscard]] uint64_t InitialWindow(const uint64_t requested_page_count,
                                     const uint64_t maximum_window_page_count) noexcept {
    if (requested_page_count >= maximum_window_page_count) {
        return requested_page_count;
    }
    uint64_t window_page_count = RoundUpPowerOfTwo(requested_page_count, maximum_window_page_count);
    if (window_page_count <= maximum_window_page_count / 32ULL) {
        window_page_count *= 4ULL;
    } else if (window_page_count <= maximum_window_page_count / 4ULL) {
        window_page_count *= 2ULL;
    } else {
        window_page_count = maximum_window_page_count;
    }
    return Minimum(window_page_count, maximum_window_page_count);
}

[[nodiscard]] uint64_t NextWindow(const uint64_t current_window_page_count,
                                  const uint64_t maximum_window_page_count) noexcept {
    if (current_window_page_count >= maximum_window_page_count) {
        return maximum_window_page_count;
    }
    if (current_window_page_count < maximum_window_page_count / 16ULL) {
        return Minimum(maximum_window_page_count, current_window_page_count * 4ULL);
    }
    if (current_window_page_count <= maximum_window_page_count / 2ULL) {
        return Minimum(maximum_window_page_count, current_window_page_count * 2ULL);
    }
    return maximum_window_page_count;
}

void RecordPlan(os::kernel::FileReadaheadStatistics &statistics,
                const os::kernel::FileReadaheadDecision &decision) noexcept {
    if (decision.action == os::kernel::FileReadaheadAction::Submit) {
        ++statistics.submission_decision_count;
        statistics.planned_window_page_count += decision.window_page_count;
        statistics.planned_prefetch_page_count += decision.prefetch_page_count;
    }
}

[[nodiscard]] os::kernel::FileReadaheadDecision
ObserveReference(ReferenceModel &model, const os::kernel::FileReadaheadAccess &access) noexcept {
    os::kernel::FileReadaheadStatistics &statistics = model.statistics;
    os::kernel::FileReadaheadDecision decision{};
    const uint64_t access_end_page_index = access.first_page_index + access.requested_page_count;
    const bool initial_access = !statistics.stream_active;
    const bool sequential_access =
        statistics.stream_active && access.first_page_index == statistics.next_expected_page_index;
    const bool random_access = statistics.stream_active && !sequential_access;
    decision.sequential_access = sequential_access;
    decision.stream_reset = random_access;
    statistics.pressure_level = access.pressure_level;
    statistics.effective_maximum_window_page_count =
        EffectiveMaximum(statistics.adaptive_maximum_window_page_count, access.pressure_level);
    ++statistics.access_count;
    if (initial_access) {
        ++statistics.initial_access_count;
    } else if (sequential_access) {
        ++statistics.sequential_access_count;
    } else {
        ++statistics.random_access_count;
        ++statistics.stream_reset_count;
        ClearStream(statistics);
    }
    if (access.trigger == os::kernel::FileReadaheadTrigger::DemandHit) {
        ++statistics.demand_hit_access_count;
    } else if (access.trigger == os::kernel::FileReadaheadTrigger::DemandMiss) {
        ++statistics.demand_miss_access_count;
    } else {
        ++statistics.prefetched_hit_access_count;
    }
    if (statistics.effective_maximum_window_page_count <
            statistics.adaptive_maximum_window_page_count &&
        access.pressure_level != os::kernel::MemoryPressureLevel::BelowMinimum) {
        ++statistics.pressure_limited_access_count;
    }
    if (access.pressure_level == os::kernel::MemoryPressureLevel::BelowMinimum) {
        ++statistics.pressure_disabled_access_count;
        ClearWindow(statistics);
    } else if (access.trigger == os::kernel::FileReadaheadTrigger::DemandMiss &&
               (initial_access || sequential_access || access.first_page_index == 0ULL)) {
        const uint64_t available_page_count = access.file_page_count - access.first_page_index;
        const uint64_t window_page_count =
            Minimum(InitialWindow(access.requested_page_count,
                                  statistics.effective_maximum_window_page_count),
                    available_page_count);
        if (window_page_count > access.requested_page_count) {
            ++statistics.generation;
            decision = os::kernel::FileReadaheadDecision{
                .action = os::kernel::FileReadaheadAction::Submit,
                .generation = statistics.generation,
                .window_start_page_index = access.first_page_index,
                .window_page_count = window_page_count,
                .prefetch_start_page_index = access_end_page_index,
                .prefetch_page_count = window_page_count - access.requested_page_count,
                .trigger_page_index = access_end_page_index,
                .effective_maximum_window_page_count =
                    statistics.effective_maximum_window_page_count,
                .sequential_access = sequential_access,
                .stream_reset = random_access,
            };
            statistics.window_start_page_index = access.first_page_index;
            statistics.window_page_count = window_page_count;
            statistics.asynchronous_page_count = decision.prefetch_page_count;
            statistics.trigger_page_index = access_end_page_index;
            statistics.window_active = true;
        } else {
            ClearWindow(statistics);
        }
    } else if (access.trigger == os::kernel::FileReadaheadTrigger::PrefetchedHit) {
        const uint64_t current_window_end_page_index =
            statistics.window_start_page_index + statistics.window_page_count;
        if (current_window_end_page_index < access.file_page_count) {
            const uint64_t previous_window_page_count = statistics.window_page_count;
            const uint64_t window_page_count =
                Minimum(NextWindow(previous_window_page_count,
                                   statistics.effective_maximum_window_page_count),
                        access.file_page_count - current_window_end_page_index);
            ++statistics.generation;
            decision = os::kernel::FileReadaheadDecision{
                .action = os::kernel::FileReadaheadAction::Submit,
                .generation = statistics.generation,
                .window_start_page_index = current_window_end_page_index,
                .window_page_count = window_page_count,
                .prefetch_start_page_index = current_window_end_page_index,
                .prefetch_page_count = window_page_count,
                .trigger_page_index = current_window_end_page_index,
                .effective_maximum_window_page_count =
                    statistics.effective_maximum_window_page_count,
                .sequential_access = true,
                .stream_reset = false,
            };
            statistics.window_start_page_index = current_window_end_page_index;
            statistics.window_page_count = window_page_count;
            statistics.asynchronous_page_count = window_page_count;
            statistics.trigger_page_index = current_window_end_page_index;
            statistics.window_active = true;
            if (window_page_count > previous_window_page_count) {
                ++statistics.window_growth_count;
            } else if (window_page_count < previous_window_page_count) {
                ++statistics.window_shrink_count;
            }
        } else {
            ClearWindow(statistics);
        }
    }
    RecordPlan(statistics, decision);
    statistics.stream_active = true;
    statistics.next_expected_page_index = access_end_page_index;
    decision.effective_maximum_window_page_count = statistics.effective_maximum_window_page_count;
    return decision;
}

void RecordReferenceFeedback(ReferenceModel &model, const uint64_t useful_page_count,
                             const uint64_t wasted_page_count) noexcept {
    os::kernel::FileReadaheadStatistics &statistics = model.statistics;
    ++statistics.feedback_count;
    statistics.useful_prefetched_page_count += useful_page_count;
    statistics.wasted_prefetched_page_count += wasted_page_count;
    if (wasted_page_count > useful_page_count &&
        statistics.adaptive_maximum_window_page_count > 1ULL) {
        statistics.adaptive_maximum_window_page_count =
            Maximum(1ULL, statistics.adaptive_maximum_window_page_count / 2ULL +
                              statistics.adaptive_maximum_window_page_count % 2ULL);
        ++statistics.feedback_shrink_count;
    } else if (useful_page_count > wasted_page_count &&
               statistics.adaptive_maximum_window_page_count <
                   OS_TEST_FILE_READAHEAD_RANDOMIZED_MAXIMUM_WINDOW_PAGE_COUNT) {
        statistics.adaptive_maximum_window_page_count =
            Minimum(OS_TEST_FILE_READAHEAD_RANDOMIZED_MAXIMUM_WINDOW_PAGE_COUNT,
                    statistics.adaptive_maximum_window_page_count * 2ULL);
        ++statistics.feedback_recovery_count;
    }
    statistics.effective_maximum_window_page_count =
        EffectiveMaximum(statistics.adaptive_maximum_window_page_count, statistics.pressure_level);
}

void ResetReference(ReferenceModel &model) noexcept {
    ++model.statistics.stream_reset_count;
    ClearStream(model.statistics);
}

[[nodiscard]] bool DecisionsEqual(const os::kernel::FileReadaheadDecision &left,
                                  const os::kernel::FileReadaheadDecision &right) noexcept {
    return left.action == right.action && left.generation == right.generation &&
           left.window_start_page_index == right.window_start_page_index &&
           left.window_page_count == right.window_page_count &&
           left.prefetch_start_page_index == right.prefetch_start_page_index &&
           left.prefetch_page_count == right.prefetch_page_count &&
           left.trigger_page_index == right.trigger_page_index &&
           left.effective_maximum_window_page_count == right.effective_maximum_window_page_count &&
           left.sequential_access == right.sequential_access &&
           left.stream_reset == right.stream_reset;
}

[[nodiscard]] bool StatisticsEqual(const os::kernel::FileReadaheadStatistics &left,
                                   const os::kernel::FileReadaheadStatistics &right) noexcept {
    return left.configured_maximum_window_page_count ==
               right.configured_maximum_window_page_count &&
           left.adaptive_maximum_window_page_count == right.adaptive_maximum_window_page_count &&
           left.effective_maximum_window_page_count == right.effective_maximum_window_page_count &&
           left.window_start_page_index == right.window_start_page_index &&
           left.window_page_count == right.window_page_count &&
           left.asynchronous_page_count == right.asynchronous_page_count &&
           left.trigger_page_index == right.trigger_page_index &&
           left.next_expected_page_index == right.next_expected_page_index &&
           left.generation == right.generation && left.access_count == right.access_count &&
           left.initial_access_count == right.initial_access_count &&
           left.sequential_access_count == right.sequential_access_count &&
           left.random_access_count == right.random_access_count &&
           left.demand_hit_access_count == right.demand_hit_access_count &&
           left.demand_miss_access_count == right.demand_miss_access_count &&
           left.prefetched_hit_access_count == right.prefetched_hit_access_count &&
           left.submission_decision_count == right.submission_decision_count &&
           left.planned_window_page_count == right.planned_window_page_count &&
           left.planned_prefetch_page_count == right.planned_prefetch_page_count &&
           left.window_growth_count == right.window_growth_count &&
           left.window_shrink_count == right.window_shrink_count &&
           left.stream_reset_count == right.stream_reset_count &&
           left.feedback_count == right.feedback_count &&
           left.useful_prefetched_page_count == right.useful_prefetched_page_count &&
           left.wasted_prefetched_page_count == right.wasted_prefetched_page_count &&
           left.feedback_shrink_count == right.feedback_shrink_count &&
           left.feedback_recovery_count == right.feedback_recovery_count &&
           left.pressure_limited_access_count == right.pressure_limited_access_count &&
           left.pressure_disabled_access_count == right.pressure_disabled_access_count &&
           left.pressure_level == right.pressure_level &&
           left.stream_active == right.stream_active && left.window_active == right.window_active;
}

[[nodiscard]] os::kernel::MemoryPressureLevel RandomPressure(uint64_t &random_state) noexcept {
    const uint64_t value = NextRandom(random_state) % 8ULL;
    return value < 5ULL    ? os::kernel::MemoryPressureLevel::Balanced
           : value == 5ULL ? os::kernel::MemoryPressureLevel::BelowHigh
           : value == 6ULL ? os::kernel::MemoryPressureLevel::BelowLow
                           : os::kernel::MemoryPressureLevel::BelowMinimum;
}

[[nodiscard]] os::kernel::FileReadaheadAccess RandomAccess(ReferenceModel &model,
                                                           uint64_t &random_state) noexcept {
    const os::kernel::FileReadaheadStatistics &statistics = model.statistics;
    uint64_t first_page_index =
        NextRandom(random_state) % OS_TEST_FILE_READAHEAD_RANDOMIZED_FILE_PAGE_COUNT;
    os::kernel::FileReadaheadTrigger trigger = (NextRandom(random_state) & 1ULL) == 0ULL
                                                   ? os::kernel::FileReadaheadTrigger::DemandHit
                                                   : os::kernel::FileReadaheadTrigger::DemandMiss;
    if (statistics.stream_active && statistics.window_active &&
        statistics.next_expected_page_index == statistics.trigger_page_index &&
        statistics.trigger_page_index < OS_TEST_FILE_READAHEAD_RANDOMIZED_FILE_PAGE_COUNT &&
        NextRandom(random_state) % OS_TEST_FILE_READAHEAD_RANDOMIZED_PREFETCH_TRIGGER_MODULUS ==
            OS_TEST_FILE_READAHEAD_RANDOMIZED_EMPTY_VALUE) {
        first_page_index = statistics.trigger_page_index;
        trigger = os::kernel::FileReadaheadTrigger::PrefetchedHit;
    } else if (statistics.stream_active &&
               statistics.next_expected_page_index <
                   OS_TEST_FILE_READAHEAD_RANDOMIZED_FILE_PAGE_COUNT &&
               NextRandom(random_state) % OS_TEST_FILE_READAHEAD_RANDOMIZED_OPERATION_MODULUS <
                   OS_TEST_FILE_READAHEAD_RANDOMIZED_SEQUENTIAL_PERCENT) {
        first_page_index = statistics.next_expected_page_index;
    } else if (statistics.stream_active &&
               first_page_index == statistics.next_expected_page_index) {
        first_page_index = (first_page_index + OS_TEST_FILE_READAHEAD_RANDOMIZED_SINGLE_VALUE) %
                           OS_TEST_FILE_READAHEAD_RANDOMIZED_FILE_PAGE_COUNT;
    }
    uint64_t requested_page_count =
        NextRandom(random_state) % OS_TEST_FILE_READAHEAD_RANDOMIZED_MAXIMUM_REQUEST_PAGE_COUNT +
        OS_TEST_FILE_READAHEAD_RANDOMIZED_SINGLE_VALUE;
    requested_page_count = Minimum(
        requested_page_count, OS_TEST_FILE_READAHEAD_RANDOMIZED_FILE_PAGE_COUNT - first_page_index);
    return os::kernel::FileReadaheadAccess{
        .first_page_index = first_page_index,
        .requested_page_count = requested_page_count,
        .file_page_count = OS_TEST_FILE_READAHEAD_RANDOMIZED_FILE_PAGE_COUNT,
        .trigger = trigger,
        .pressure_level = RandomPressure(random_state),
    };
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_FILE_READAHEAD_RANDOMIZED_SUITE_NAME};
    os::kernel::FileReadaheadPolicy policy{};
    ReferenceModel model{};
    model.statistics.configured_maximum_window_page_count =
        OS_TEST_FILE_READAHEAD_RANDOMIZED_MAXIMUM_WINDOW_PAGE_COUNT;
    model.statistics.adaptive_maximum_window_page_count =
        OS_TEST_FILE_READAHEAD_RANDOMIZED_MAXIMUM_WINDOW_PAGE_COUNT;
    model.statistics.effective_maximum_window_page_count =
        OS_TEST_FILE_READAHEAD_RANDOMIZED_MAXIMUM_WINDOW_PAGE_COUNT;
    model.statistics.pressure_level = os::kernel::MemoryPressureLevel::Balanced;
    bool consistent = policy.Initialize(os::kernel::FileReadaheadConfiguration{
                          .maximum_window_page_count =
                              OS_TEST_FILE_READAHEAD_RANDOMIZED_MAXIMUM_WINDOW_PAGE_COUNT,
                      }) == os::kernel::FileReadaheadStatus::Succeeded;
    uint64_t random_state = OS_TEST_FILE_READAHEAD_RANDOMIZED_SEED;
    for (uint64_t iteration = OS_TEST_FILE_READAHEAD_RANDOMIZED_EMPTY_VALUE;
         consistent && iteration < OS_TEST_FILE_READAHEAD_RANDOMIZED_ITERATION_COUNT; ++iteration) {
        const uint64_t operation =
            NextRandom(random_state) % OS_TEST_FILE_READAHEAD_RANDOMIZED_OPERATION_MODULUS;
        if (operation < OS_TEST_FILE_READAHEAD_RANDOMIZED_ACCESS_OPERATION_LIMIT) {
            const os::kernel::FileReadaheadAccess access = RandomAccess(model, random_state);
            const os::kernel::FileReadaheadDecision expected_decision =
                ObserveReference(model, access);
            os::kernel::FileReadaheadDecision observed_decision{};
            consistent = policy.ObserveAccess(access, observed_decision) ==
                             os::kernel::FileReadaheadStatus::Succeeded &&
                         DecisionsEqual(observed_decision, expected_decision);
        } else if (operation < OS_TEST_FILE_READAHEAD_RANDOMIZED_FEEDBACK_OPERATION_LIMIT) {
            uint64_t useful_page_count =
                NextRandom(random_state) %
                (OS_TEST_FILE_READAHEAD_RANDOMIZED_MAXIMUM_FEEDBACK_PAGE_COUNT + 1ULL);
            uint64_t wasted_page_count =
                NextRandom(random_state) %
                (OS_TEST_FILE_READAHEAD_RANDOMIZED_MAXIMUM_FEEDBACK_PAGE_COUNT + 1ULL);
            if (useful_page_count == OS_TEST_FILE_READAHEAD_RANDOMIZED_EMPTY_VALUE &&
                wasted_page_count == OS_TEST_FILE_READAHEAD_RANDOMIZED_EMPTY_VALUE) {
                useful_page_count = OS_TEST_FILE_READAHEAD_RANDOMIZED_SINGLE_VALUE;
            }
            RecordReferenceFeedback(model, useful_page_count, wasted_page_count);
            consistent = policy.RecordFeedback(useful_page_count, wasted_page_count) ==
                         os::kernel::FileReadaheadStatus::Succeeded;
        } else {
            ResetReference(model);
            consistent = policy.Reset() == os::kernel::FileReadaheadStatus::Succeeded;
        }
        consistent = consistent && StatisticsEqual(policy.Statistics(), model.statistics) &&
                     policy.Validate() == os::kernel::FileReadaheadStatus::Succeeded;
    }
    test_context.Expect(consistent, OS_TEST_FILE_READAHEAD_RANDOMIZED_MESSAGE);
    return test_context.ExitCode();
}

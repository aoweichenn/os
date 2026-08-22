#include <os/kernel/memory/background_reclaim.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_BACKGROUND_RECLAIM_RANDOM_SUITE_NAME =
    "kernel/background_reclaim/randomized";
constexpr std::string_view OS_TEST_BACKGROUND_RECLAIM_RANDOM_STATE_MACHINE =
    "十万步水位、批次、退避与恢复必须逐步匹配独立状态机";

constexpr uint64_t OS_TEST_BACKGROUND_RECLAIM_RANDOM_SEED = 0x563239424752434CULL;
constexpr uint64_t OS_TEST_BACKGROUND_RECLAIM_RANDOM_SHIFT_FIRST = 12ULL;
constexpr uint64_t OS_TEST_BACKGROUND_RECLAIM_RANDOM_SHIFT_SECOND = 25ULL;
constexpr uint64_t OS_TEST_BACKGROUND_RECLAIM_RANDOM_SHIFT_THIRD = 27ULL;
constexpr uint64_t OS_TEST_BACKGROUND_RECLAIM_RANDOM_MULTIPLIER = 0x2545F4914F6CDD1DULL;
constexpr uint64_t OS_TEST_BACKGROUND_RECLAIM_RANDOM_ITERATION_COUNT = 100000ULL;
constexpr uint64_t OS_TEST_BACKGROUND_RECLAIM_RANDOM_BATCH_PAGE_COUNT = 64ULL;
constexpr uint64_t OS_TEST_BACKGROUND_RECLAIM_RANDOM_BACKOFF_NANOSECONDS = 100ULL;
constexpr uint64_t OS_TEST_BACKGROUND_RECLAIM_RANDOM_RESIDENT_LIMIT_PAGE_COUNT = 4096ULL;
constexpr uint64_t OS_TEST_BACKGROUND_RECLAIM_RANDOM_MINIMUM_FREE_PAGE_COUNT = 256ULL;
constexpr uint64_t OS_TEST_BACKGROUND_RECLAIM_RANDOM_LOW_FREE_PAGE_COUNT = 320ULL;
constexpr uint64_t OS_TEST_BACKGROUND_RECLAIM_RANDOM_HIGH_FREE_PAGE_COUNT = 384ULL;

constexpr os::kernel::MemoryWatermarks OS_TEST_BACKGROUND_RECLAIM_RANDOM_WATERMARKS{
    .resident_limit_page_count = OS_TEST_BACKGROUND_RECLAIM_RANDOM_RESIDENT_LIMIT_PAGE_COUNT,
    .minimum_free_page_count = OS_TEST_BACKGROUND_RECLAIM_RANDOM_MINIMUM_FREE_PAGE_COUNT,
    .low_free_page_count = OS_TEST_BACKGROUND_RECLAIM_RANDOM_LOW_FREE_PAGE_COUNT,
    .high_free_page_count = OS_TEST_BACKGROUND_RECLAIM_RANDOM_HIGH_FREE_PAGE_COUNT,
};

[[nodiscard]] uint64_t NextRandom(uint64_t &state) noexcept {
    state ^= state >> OS_TEST_BACKGROUND_RECLAIM_RANDOM_SHIFT_FIRST;
    state ^= state << OS_TEST_BACKGROUND_RECLAIM_RANDOM_SHIFT_SECOND;
    state ^= state >> OS_TEST_BACKGROUND_RECLAIM_RANDOM_SHIFT_THIRD;
    state *= OS_TEST_BACKGROUND_RECLAIM_RANDOM_MULTIPLIER;
    return state;
}

[[nodiscard]] uint64_t Minimum(const uint64_t left, const uint64_t right) noexcept {
    return left < right ? left : right;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_BACKGROUND_RECLAIM_RANDOM_SUITE_NAME};
    os::kernel::BackgroundReclaimController controller{};
    bool valid = controller.Initialize(os::kernel::BackgroundReclaimConfiguration{
                     .batch_page_count = OS_TEST_BACKGROUND_RECLAIM_RANDOM_BATCH_PAGE_COUNT,
                     .no_progress_backoff_nanoseconds =
                         OS_TEST_BACKGROUND_RECLAIM_RANDOM_BACKOFF_NANOSECONDS,
                 }) == os::kernel::BackgroundReclaimStatus::Succeeded;
    os::kernel::BackgroundReclaimState model_state = os::kernel::BackgroundReclaimState::Sleeping;
    uint64_t model_deadline_nanoseconds = 0ULL;
    uint64_t now_nanoseconds = 1ULL;
    uint64_t random_state = OS_TEST_BACKGROUND_RECLAIM_RANDOM_SEED;
    for (uint64_t iteration = 0ULL;
         valid && iteration < OS_TEST_BACKGROUND_RECLAIM_RANDOM_ITERATION_COUNT; ++iteration) {
        now_nanoseconds += NextRandom(random_state) % 50ULL;
        const uint64_t free_page_count =
            NextRandom(random_state) % OS_TEST_BACKGROUND_RECLAIM_RANDOM_RESIDENT_LIMIT_PAGE_COUNT;
        const uint64_t resident_page_count =
            OS_TEST_BACKGROUND_RECLAIM_RANDOM_RESIDENT_LIMIT_PAGE_COUNT - free_page_count;
        if (model_state == os::kernel::BackgroundReclaimState::Sleeping &&
            free_page_count < OS_TEST_BACKGROUND_RECLAIM_RANDOM_LOW_FREE_PAGE_COUNT) {
            model_state = os::kernel::BackgroundReclaimState::Running;
        }
        if (model_state != os::kernel::BackgroundReclaimState::Sleeping &&
            free_page_count >= OS_TEST_BACKGROUND_RECLAIM_RANDOM_HIGH_FREE_PAGE_COUNT) {
            model_state = os::kernel::BackgroundReclaimState::Sleeping;
            model_deadline_nanoseconds = 0ULL;
        }
        os::kernel::BackgroundReclaimAction expected_action =
            os::kernel::BackgroundReclaimAction::Sleep;
        uint64_t expected_target_page_count = 0ULL;
        if (model_state == os::kernel::BackgroundReclaimState::BackingOff &&
            now_nanoseconds < model_deadline_nanoseconds) {
            expected_action = os::kernel::BackgroundReclaimAction::Wait;
        } else if (model_state != os::kernel::BackgroundReclaimState::Sleeping) {
            model_state = os::kernel::BackgroundReclaimState::Running;
            model_deadline_nanoseconds = 0ULL;
            expected_action = os::kernel::BackgroundReclaimAction::Reclaim;
            expected_target_page_count =
                Minimum(OS_TEST_BACKGROUND_RECLAIM_RANDOM_BATCH_PAGE_COUNT,
                        OS_TEST_BACKGROUND_RECLAIM_RANDOM_HIGH_FREE_PAGE_COUNT - free_page_count);
        }
        os::kernel::BackgroundReclaimDecision decision{};
        valid = controller.Evaluate(OS_TEST_BACKGROUND_RECLAIM_RANDOM_WATERMARKS,
                                    resident_page_count, now_nanoseconds,
                                    decision) == os::kernel::BackgroundReclaimStatus::Succeeded &&
                decision.state == model_state && decision.action == expected_action &&
                decision.free_page_count == free_page_count &&
                decision.target_page_count == expected_target_page_count &&
                decision.deadline_nanoseconds == model_deadline_nanoseconds;
        if (!valid || expected_action != os::kernel::BackgroundReclaimAction::Reclaim) {
            valid =
                valid && controller.Validate() == os::kernel::BackgroundReclaimStatus::Succeeded;
            continue;
        }
        const uint64_t outcome = NextRandom(random_state) % 4ULL;
        const uint64_t reclaimed_page_count =
            outcome == 0ULL ? NextRandom(random_state) % (decision.target_page_count + 1ULL) : 0ULL;
        const uint64_t written_page_count =
            outcome == 1ULL ? NextRandom(random_state) % (decision.target_page_count + 1ULL) : 0ULL;
        const bool failed = outcome == 2ULL;
        valid = controller.RecordBatch(
                    os::kernel::BackgroundReclaimBatchResult{
                        .requested_page_count = decision.target_page_count,
                        .clean_file_page_count = reclaimed_page_count,
                        .swapped_anonymous_page_count = 0ULL,
                        .reclaimed_page_count = reclaimed_page_count,
                        .written_page_count = written_page_count,
                        .failed = failed,
                    },
                    now_nanoseconds) == os::kernel::BackgroundReclaimStatus::Succeeded;
        if (failed || reclaimed_page_count == 0ULL) {
            model_state = os::kernel::BackgroundReclaimState::BackingOff;
            model_deadline_nanoseconds =
                now_nanoseconds + OS_TEST_BACKGROUND_RECLAIM_RANDOM_BACKOFF_NANOSECONDS;
        }
        valid = valid && controller.Statistics().state == model_state &&
                controller.Statistics().next_deadline_nanoseconds == model_deadline_nanoseconds &&
                controller.Validate() == os::kernel::BackgroundReclaimStatus::Succeeded;
    }
    test_context.ExpectRandom(valid, OS_TEST_BACKGROUND_RECLAIM_RANDOM_STATE_MACHINE,
                              OS_TEST_BACKGROUND_RECLAIM_RANDOM_SEED,
                              OS_TEST_BACKGROUND_RECLAIM_RANDOM_ITERATION_COUNT);
    return test_context.ExitCode();
}

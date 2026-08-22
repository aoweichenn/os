#include <os/kernel/memory/background_reclaim.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_BACKGROUND_RECLAIM_SUITE_NAME = "kernel/background_reclaim/unit";
constexpr std::string_view OS_TEST_BACKGROUND_RECLAIM_HYSTERESIS =
    "低水位必须唤醒、高水位必须休眠且批次受上限约束";
constexpr std::string_view OS_TEST_BACKGROUND_RECLAIM_BACKOFF =
    "无进展、仅写回和失败必须退避并在 deadline 后恢复";
constexpr std::string_view OS_TEST_BACKGROUND_RECLAIM_FAILURES =
    "初始化、压力样本、批次结果和状态失败不得破坏统计守恒";

constexpr uint64_t OS_TEST_BACKGROUND_RECLAIM_BATCH_PAGE_COUNT = 64ULL;
constexpr uint64_t OS_TEST_BACKGROUND_RECLAIM_BACKOFF_NANOSECONDS = 1000ULL;
constexpr uint64_t OS_TEST_BACKGROUND_RECLAIM_RESIDENT_LIMIT_PAGE_COUNT = 1000ULL;
constexpr uint64_t OS_TEST_BACKGROUND_RECLAIM_MINIMUM_FREE_PAGE_COUNT = 100ULL;
constexpr uint64_t OS_TEST_BACKGROUND_RECLAIM_LOW_FREE_PAGE_COUNT = 200ULL;
constexpr uint64_t OS_TEST_BACKGROUND_RECLAIM_HIGH_FREE_PAGE_COUNT = 300ULL;
constexpr uint64_t OS_TEST_BACKGROUND_RECLAIM_LOW_RESIDENT_PAGE_COUNT = 801ULL;
constexpr uint64_t OS_TEST_BACKGROUND_RECLAIM_BALANCED_RESIDENT_PAGE_COUNT = 700ULL;
constexpr uint64_t OS_TEST_BACKGROUND_RECLAIM_FIRST_TIME_NANOSECONDS = 5000ULL;

constexpr os::kernel::MemoryWatermarks OS_TEST_BACKGROUND_RECLAIM_WATERMARKS{
    .resident_limit_page_count = OS_TEST_BACKGROUND_RECLAIM_RESIDENT_LIMIT_PAGE_COUNT,
    .minimum_free_page_count = OS_TEST_BACKGROUND_RECLAIM_MINIMUM_FREE_PAGE_COUNT,
    .low_free_page_count = OS_TEST_BACKGROUND_RECLAIM_LOW_FREE_PAGE_COUNT,
    .high_free_page_count = OS_TEST_BACKGROUND_RECLAIM_HIGH_FREE_PAGE_COUNT,
};

[[nodiscard]] bool Initialize(os::kernel::BackgroundReclaimController &controller) noexcept {
    return controller.Initialize(os::kernel::BackgroundReclaimConfiguration{
               .batch_page_count = OS_TEST_BACKGROUND_RECLAIM_BATCH_PAGE_COUNT,
               .no_progress_backoff_nanoseconds = OS_TEST_BACKGROUND_RECLAIM_BACKOFF_NANOSECONDS,
           }) == os::kernel::BackgroundReclaimStatus::Succeeded;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_BACKGROUND_RECLAIM_SUITE_NAME};

    os::kernel::BackgroundReclaimController controller{};
    os::kernel::BackgroundReclaimDecision decision{};
    bool hysteresis_valid =
        Initialize(controller) &&
        controller.Evaluate(OS_TEST_BACKGROUND_RECLAIM_WATERMARKS,
                            OS_TEST_BACKGROUND_RECLAIM_BALANCED_RESIDENT_PAGE_COUNT,
                            OS_TEST_BACKGROUND_RECLAIM_FIRST_TIME_NANOSECONDS,
                            decision) == os::kernel::BackgroundReclaimStatus::Succeeded &&
        decision.action == os::kernel::BackgroundReclaimAction::Sleep &&
        decision.state == os::kernel::BackgroundReclaimState::Sleeping &&
        controller.Evaluate(OS_TEST_BACKGROUND_RECLAIM_WATERMARKS,
                            OS_TEST_BACKGROUND_RECLAIM_LOW_RESIDENT_PAGE_COUNT,
                            OS_TEST_BACKGROUND_RECLAIM_FIRST_TIME_NANOSECONDS,
                            decision) == os::kernel::BackgroundReclaimStatus::Succeeded &&
        decision.action == os::kernel::BackgroundReclaimAction::Reclaim &&
        decision.state == os::kernel::BackgroundReclaimState::Running &&
        decision.free_page_count == 199ULL &&
        decision.target_page_count == OS_TEST_BACKGROUND_RECLAIM_BATCH_PAGE_COUNT &&
        controller.RecordBatch(
            os::kernel::BackgroundReclaimBatchResult{
                .requested_page_count = decision.target_page_count,
                .clean_file_page_count = decision.target_page_count,
                .swapped_anonymous_page_count = 0ULL,
                .reclaimed_page_count = decision.target_page_count,
                .written_page_count = 0ULL,
                .failed = false,
            },
            OS_TEST_BACKGROUND_RECLAIM_FIRST_TIME_NANOSECONDS) ==
            os::kernel::BackgroundReclaimStatus::Succeeded &&
        controller.Evaluate(OS_TEST_BACKGROUND_RECLAIM_WATERMARKS,
                            OS_TEST_BACKGROUND_RECLAIM_BALANCED_RESIDENT_PAGE_COUNT,
                            OS_TEST_BACKGROUND_RECLAIM_FIRST_TIME_NANOSECONDS,
                            decision) == os::kernel::BackgroundReclaimStatus::Succeeded &&
        decision.action == os::kernel::BackgroundReclaimAction::Sleep &&
        controller.Statistics().wake_count == 1ULL && controller.Statistics().sleep_count == 1ULL &&
        controller.Statistics().batch_count == 1ULL &&
        controller.Validate() == os::kernel::BackgroundReclaimStatus::Succeeded;
    test_context.Expect(hysteresis_valid, OS_TEST_BACKGROUND_RECLAIM_HYSTERESIS);

    os::kernel::BackgroundReclaimController backoff_controller{};
    os::kernel::BackgroundReclaimDecision backoff_decision{};
    bool backoff_valid =
        Initialize(backoff_controller) &&
        backoff_controller.Evaluate(OS_TEST_BACKGROUND_RECLAIM_WATERMARKS,
                                    OS_TEST_BACKGROUND_RECLAIM_LOW_RESIDENT_PAGE_COUNT,
                                    OS_TEST_BACKGROUND_RECLAIM_FIRST_TIME_NANOSECONDS,
                                    backoff_decision) ==
            os::kernel::BackgroundReclaimStatus::Succeeded &&
        backoff_controller.RecordBatch(
            os::kernel::BackgroundReclaimBatchResult{
                .requested_page_count = backoff_decision.target_page_count,
                .clean_file_page_count = 0ULL,
                .swapped_anonymous_page_count = 0ULL,
                .reclaimed_page_count = 0ULL,
                .written_page_count = 0ULL,
                .failed = false,
            },
            OS_TEST_BACKGROUND_RECLAIM_FIRST_TIME_NANOSECONDS) ==
            os::kernel::BackgroundReclaimStatus::Succeeded &&
        backoff_controller.Evaluate(OS_TEST_BACKGROUND_RECLAIM_WATERMARKS,
                                    OS_TEST_BACKGROUND_RECLAIM_LOW_RESIDENT_PAGE_COUNT,
                                    OS_TEST_BACKGROUND_RECLAIM_FIRST_TIME_NANOSECONDS + 1ULL,
                                    backoff_decision) ==
            os::kernel::BackgroundReclaimStatus::Succeeded &&
        backoff_decision.action == os::kernel::BackgroundReclaimAction::Wait &&
        backoff_decision.deadline_nanoseconds ==
            OS_TEST_BACKGROUND_RECLAIM_FIRST_TIME_NANOSECONDS +
                OS_TEST_BACKGROUND_RECLAIM_BACKOFF_NANOSECONDS &&
        backoff_controller.Evaluate(OS_TEST_BACKGROUND_RECLAIM_WATERMARKS,
                                    OS_TEST_BACKGROUND_RECLAIM_LOW_RESIDENT_PAGE_COUNT,
                                    OS_TEST_BACKGROUND_RECLAIM_FIRST_TIME_NANOSECONDS +
                                        OS_TEST_BACKGROUND_RECLAIM_BACKOFF_NANOSECONDS,
                                    backoff_decision) ==
            os::kernel::BackgroundReclaimStatus::Succeeded &&
        backoff_decision.action == os::kernel::BackgroundReclaimAction::Reclaim &&
        backoff_controller.RecordBatch(
            os::kernel::BackgroundReclaimBatchResult{
                .requested_page_count = backoff_decision.target_page_count,
                .clean_file_page_count = 0ULL,
                .swapped_anonymous_page_count = 0ULL,
                .reclaimed_page_count = 0ULL,
                .written_page_count = 8ULL,
                .failed = false,
            },
            OS_TEST_BACKGROUND_RECLAIM_FIRST_TIME_NANOSECONDS +
                OS_TEST_BACKGROUND_RECLAIM_BACKOFF_NANOSECONDS) ==
            os::kernel::BackgroundReclaimStatus::Succeeded &&
        backoff_controller.Evaluate(OS_TEST_BACKGROUND_RECLAIM_WATERMARKS,
                                    OS_TEST_BACKGROUND_RECLAIM_LOW_RESIDENT_PAGE_COUNT,
                                    OS_TEST_BACKGROUND_RECLAIM_FIRST_TIME_NANOSECONDS +
                                        OS_TEST_BACKGROUND_RECLAIM_BACKOFF_NANOSECONDS * 2ULL,
                                    backoff_decision) ==
            os::kernel::BackgroundReclaimStatus::Succeeded &&
        backoff_controller.RecordBatch(
            os::kernel::BackgroundReclaimBatchResult{
                .requested_page_count = backoff_decision.target_page_count,
                .clean_file_page_count = 0ULL,
                .swapped_anonymous_page_count = 0ULL,
                .reclaimed_page_count = 0ULL,
                .written_page_count = 0ULL,
                .failed = true,
            },
            OS_TEST_BACKGROUND_RECLAIM_FIRST_TIME_NANOSECONDS +
                OS_TEST_BACKGROUND_RECLAIM_BACKOFF_NANOSECONDS * 2ULL) ==
            os::kernel::BackgroundReclaimStatus::Succeeded &&
        backoff_controller.Statistics().no_progress_count == 1ULL &&
        backoff_controller.Statistics().failure_count == 1ULL &&
        backoff_controller.Statistics().backoff_count == 3ULL &&
        backoff_controller.Statistics().resume_count == 2ULL &&
        backoff_controller.Reset() == os::kernel::BackgroundReclaimStatus::Succeeded &&
        backoff_controller.Statistics().state == os::kernel::BackgroundReclaimState::Sleeping &&
        backoff_controller.Validate() == os::kernel::BackgroundReclaimStatus::Succeeded;
    test_context.Expect(backoff_valid, OS_TEST_BACKGROUND_RECLAIM_BACKOFF);

    os::kernel::BackgroundReclaimController invalid_controller{};
    os::kernel::BackgroundReclaimController state_controller{};
    os::kernel::BackgroundReclaimDecision invalid_decision{};
    const bool failures_valid =
        invalid_controller.Initialize(os::kernel::BackgroundReclaimConfiguration{}) ==
            os::kernel::BackgroundReclaimStatus::InvalidConfiguration &&
        invalid_controller.Evaluate(OS_TEST_BACKGROUND_RECLAIM_WATERMARKS, 0ULL, 0ULL,
                                    invalid_decision) ==
            os::kernel::BackgroundReclaimStatus::NotInitialized &&
        Initialize(state_controller) &&
        state_controller.RecordBatch(os::kernel::BackgroundReclaimBatchResult{}, 0ULL) ==
            os::kernel::BackgroundReclaimStatus::InvalidState &&
        state_controller.Evaluate(os::kernel::MemoryWatermarks{}, 0ULL, 0ULL, invalid_decision) ==
            os::kernel::BackgroundReclaimStatus::InvalidPressureSample &&
        state_controller.Evaluate(OS_TEST_BACKGROUND_RECLAIM_WATERMARKS,
                                  OS_TEST_BACKGROUND_RECLAIM_LOW_RESIDENT_PAGE_COUNT, 0ULL,
                                  invalid_decision) ==
            os::kernel::BackgroundReclaimStatus::Succeeded &&
        state_controller.RecordBatch(
            os::kernel::BackgroundReclaimBatchResult{
                .requested_page_count = OS_TEST_BACKGROUND_RECLAIM_BATCH_PAGE_COUNT + 1ULL,
                .clean_file_page_count = 0ULL,
                .swapped_anonymous_page_count = 0ULL,
                .reclaimed_page_count = 0ULL,
                .written_page_count = 0ULL,
                .failed = false,
            },
            0ULL) == os::kernel::BackgroundReclaimStatus::InvalidBatchResult &&
        state_controller.Validate() == os::kernel::BackgroundReclaimStatus::Succeeded;
    test_context.Expect(failures_valid, OS_TEST_BACKGROUND_RECLAIM_FAILURES);

    return test_context.ExitCode();
}

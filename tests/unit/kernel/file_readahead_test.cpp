#include <os/kernel/memory/file_readahead.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_FILE_READAHEAD_SUITE_NAME = "kernel/file_readahead/unit";
constexpr std::string_view OS_TEST_FILE_READAHEAD_LINUX_GROWTH_MESSAGE =
    "顺序 miss 与异步触发必须按 Linux 分档规则建立并放大窗口";
constexpr std::string_view OS_TEST_FILE_READAHEAD_RANDOM_RESET_MESSAGE =
    "随机访问必须重置流，下一次连续 miss 才能重新建立窗口";
constexpr std::string_view OS_TEST_FILE_READAHEAD_PRESSURE_FEEDBACK_MESSAGE =
    "浪费反馈和四级内存压力必须收缩窗口，有用反馈必须有界恢复";
constexpr std::string_view OS_TEST_FILE_READAHEAD_END_OF_FILE_MESSAGE =
    "文件末尾必须裁剪窗口且不得提交零页预读";
constexpr std::string_view OS_TEST_FILE_READAHEAD_FAILURE_MESSAGE =
    "非法配置、访问、触发与空反馈必须无副作用失败";
constexpr uint64_t OS_TEST_FILE_READAHEAD_MAXIMUM_WINDOW_PAGE_COUNT =
    os::kernel::OS_KERNEL_FILE_READAHEAD_DEFAULT_MAXIMUM_WINDOW_PAGE_COUNT;
constexpr uint64_t OS_TEST_FILE_READAHEAD_FILE_PAGE_COUNT = 100ULL;
constexpr uint64_t OS_TEST_FILE_READAHEAD_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_FILE_READAHEAD_SINGLE_PAGE = 1ULL;
constexpr uint64_t OS_TEST_FILE_READAHEAD_MAXIMUM_FEEDBACK_EXPECTED_WINDOW = 1ULL << 63ULL;

[[nodiscard]] os::kernel::FileReadaheadAccess
MakeAccess(const uint64_t first_page_index, const uint64_t requested_page_count,
           const uint64_t file_page_count, const os::kernel::FileReadaheadTrigger trigger,
           const os::kernel::MemoryPressureLevel pressure_level =
               os::kernel::MemoryPressureLevel::Balanced) noexcept {
    return os::kernel::FileReadaheadAccess{
        .first_page_index = first_page_index,
        .requested_page_count = requested_page_count,
        .file_page_count = file_page_count,
        .trigger = trigger,
        .pressure_level = pressure_level,
    };
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_FILE_READAHEAD_SUITE_NAME};

    os::kernel::FileReadaheadPolicy policy{};
    os::kernel::FileReadaheadDecision decision{};
    bool linux_growth_valid =
        policy.Initialize(os::kernel::FileReadaheadConfiguration{
            .maximum_window_page_count = OS_TEST_FILE_READAHEAD_MAXIMUM_WINDOW_PAGE_COUNT,
        }) == os::kernel::FileReadaheadStatus::Succeeded &&
        policy.ObserveAccess(MakeAccess(OS_TEST_FILE_READAHEAD_EMPTY_VALUE,
                                        OS_TEST_FILE_READAHEAD_SINGLE_PAGE,
                                        OS_TEST_FILE_READAHEAD_FILE_PAGE_COUNT,
                                        os::kernel::FileReadaheadTrigger::DemandMiss),
                             decision) == os::kernel::FileReadaheadStatus::Succeeded &&
        decision.action == os::kernel::FileReadaheadAction::Submit && decision.generation == 1ULL &&
        decision.window_start_page_index == 0ULL && decision.window_page_count == 4ULL &&
        decision.prefetch_start_page_index == 1ULL && decision.prefetch_page_count == 3ULL &&
        decision.trigger_page_index == 1ULL && !decision.sequential_access &&
        !decision.stream_reset;
    linux_growth_valid =
        linux_growth_valid &&
        policy.ObserveAccess(MakeAccess(1ULL, 1ULL, OS_TEST_FILE_READAHEAD_FILE_PAGE_COUNT,
                                        os::kernel::FileReadaheadTrigger::PrefetchedHit),
                             decision) == os::kernel::FileReadaheadStatus::Succeeded &&
        decision.action == os::kernel::FileReadaheadAction::Submit && decision.generation == 2ULL &&
        decision.window_start_page_index == 4ULL && decision.window_page_count == 8ULL &&
        decision.prefetch_start_page_index == 4ULL && decision.prefetch_page_count == 8ULL &&
        decision.trigger_page_index == 4ULL && decision.sequential_access;
    linux_growth_valid =
        linux_growth_valid &&
        policy.ObserveAccess(MakeAccess(2ULL, 2ULL, OS_TEST_FILE_READAHEAD_FILE_PAGE_COUNT,
                                        os::kernel::FileReadaheadTrigger::DemandHit),
                             decision) == os::kernel::FileReadaheadStatus::Succeeded &&
        decision.action == os::kernel::FileReadaheadAction::None &&
        policy.ObserveAccess(MakeAccess(4ULL, 1ULL, OS_TEST_FILE_READAHEAD_FILE_PAGE_COUNT,
                                        os::kernel::FileReadaheadTrigger::PrefetchedHit),
                             decision) == os::kernel::FileReadaheadStatus::Succeeded &&
        decision.action == os::kernel::FileReadaheadAction::Submit && decision.generation == 3ULL &&
        decision.window_start_page_index == 12ULL && decision.window_page_count == 16ULL &&
        decision.prefetch_page_count == 16ULL && policy.Statistics().access_count == 4ULL &&
        policy.Statistics().initial_access_count == 1ULL &&
        policy.Statistics().sequential_access_count == 3ULL &&
        policy.Statistics().submission_decision_count == 3ULL &&
        policy.Statistics().planned_window_page_count == 28ULL &&
        policy.Statistics().planned_prefetch_page_count == 27ULL &&
        policy.Statistics().window_growth_count == 2ULL &&
        policy.Validate() == os::kernel::FileReadaheadStatus::Succeeded;
    test_context.Expect(linux_growth_valid, OS_TEST_FILE_READAHEAD_LINUX_GROWTH_MESSAGE);

    const bool random_reset_valid =
        policy.ObserveAccess(MakeAccess(70ULL, 1ULL, OS_TEST_FILE_READAHEAD_FILE_PAGE_COUNT,
                                        os::kernel::FileReadaheadTrigger::DemandHit),
                             decision) == os::kernel::FileReadaheadStatus::Succeeded &&
        decision.action == os::kernel::FileReadaheadAction::None && decision.stream_reset &&
        !decision.sequential_access && !policy.Statistics().window_active &&
        policy.ObserveAccess(MakeAccess(71ULL, 1ULL, OS_TEST_FILE_READAHEAD_FILE_PAGE_COUNT,
                                        os::kernel::FileReadaheadTrigger::DemandMiss),
                             decision) == os::kernel::FileReadaheadStatus::Succeeded &&
        decision.action == os::kernel::FileReadaheadAction::Submit && decision.sequential_access &&
        decision.window_start_page_index == 71ULL && decision.window_page_count == 4ULL &&
        decision.prefetch_start_page_index == 72ULL && decision.prefetch_page_count == 3ULL &&
        policy.Statistics().random_access_count == 1ULL &&
        policy.Statistics().stream_reset_count == 1ULL &&
        policy.Validate() == os::kernel::FileReadaheadStatus::Succeeded;
    test_context.Expect(random_reset_valid, OS_TEST_FILE_READAHEAD_RANDOM_RESET_MESSAGE);

    os::kernel::FileReadaheadPolicy pressure_policy{};
    const bool pressure_feedback_valid =
        pressure_policy.Initialize(os::kernel::FileReadaheadConfiguration{
            .maximum_window_page_count = OS_TEST_FILE_READAHEAD_MAXIMUM_WINDOW_PAGE_COUNT,
        }) == os::kernel::FileReadaheadStatus::Succeeded &&
        pressure_policy.RecordFeedback(0ULL, 8ULL) == os::kernel::FileReadaheadStatus::Succeeded &&
        pressure_policy.Statistics().adaptive_maximum_window_page_count == 16ULL &&
        pressure_policy.RecordFeedback(0ULL, 8ULL) == os::kernel::FileReadaheadStatus::Succeeded &&
        pressure_policy.Statistics().adaptive_maximum_window_page_count == 8ULL &&
        pressure_policy.RecordFeedback(8ULL, 0ULL) == os::kernel::FileReadaheadStatus::Succeeded &&
        pressure_policy.Statistics().adaptive_maximum_window_page_count == 16ULL &&
        pressure_policy.ObserveAccess(MakeAccess(0ULL, 1ULL, OS_TEST_FILE_READAHEAD_FILE_PAGE_COUNT,
                                                 os::kernel::FileReadaheadTrigger::DemandMiss,
                                                 os::kernel::MemoryPressureLevel::BelowLow),
                                      decision) == os::kernel::FileReadaheadStatus::Succeeded &&
        decision.action == os::kernel::FileReadaheadAction::Submit &&
        decision.effective_maximum_window_page_count == 8ULL &&
        decision.window_page_count == 2ULL && decision.prefetch_page_count == 1ULL &&
        pressure_policy.ObserveAccess(MakeAccess(1ULL, 1ULL, OS_TEST_FILE_READAHEAD_FILE_PAGE_COUNT,
                                                 os::kernel::FileReadaheadTrigger::DemandMiss,
                                                 os::kernel::MemoryPressureLevel::BelowMinimum),
                                      decision) == os::kernel::FileReadaheadStatus::Succeeded &&
        decision.action == os::kernel::FileReadaheadAction::None &&
        decision.effective_maximum_window_page_count == 0ULL &&
        !pressure_policy.Statistics().window_active &&
        pressure_policy.Statistics().feedback_shrink_count == 2ULL &&
        pressure_policy.Statistics().feedback_recovery_count == 1ULL &&
        pressure_policy.Statistics().pressure_limited_access_count == 1ULL &&
        pressure_policy.Statistics().pressure_disabled_access_count == 1ULL &&
        pressure_policy.Validate() == os::kernel::FileReadaheadStatus::Succeeded;
    test_context.Expect(pressure_feedback_valid, OS_TEST_FILE_READAHEAD_PRESSURE_FEEDBACK_MESSAGE);

    os::kernel::FileReadaheadPolicy end_policy{};
    const bool end_of_file_valid =
        end_policy.Initialize(os::kernel::FileReadaheadConfiguration{
            .maximum_window_page_count = OS_TEST_FILE_READAHEAD_MAXIMUM_WINDOW_PAGE_COUNT,
        }) == os::kernel::FileReadaheadStatus::Succeeded &&
        end_policy.ObserveAccess(MakeAccess(98ULL, 1ULL, OS_TEST_FILE_READAHEAD_FILE_PAGE_COUNT,
                                            os::kernel::FileReadaheadTrigger::DemandHit),
                                 decision) == os::kernel::FileReadaheadStatus::Succeeded &&
        end_policy.ObserveAccess(MakeAccess(99ULL, 1ULL, OS_TEST_FILE_READAHEAD_FILE_PAGE_COUNT,
                                            os::kernel::FileReadaheadTrigger::DemandMiss),
                                 decision) == os::kernel::FileReadaheadStatus::Succeeded &&
        decision.action == os::kernel::FileReadaheadAction::None &&
        !end_policy.Statistics().window_active &&
        end_policy.Statistics().submission_decision_count == 0ULL &&
        end_policy.Validate() == os::kernel::FileReadaheadStatus::Succeeded;
    test_context.Expect(end_of_file_valid, OS_TEST_FILE_READAHEAD_END_OF_FILE_MESSAGE);

    os::kernel::FileReadaheadPolicy invalid_policy{};
    os::kernel::FileReadaheadPolicy maximum_policy{};
    const os::kernel::FileReadaheadStatistics statistics_before_failure = policy.Statistics();
    const bool failures_valid =
        invalid_policy.Reset() == os::kernel::FileReadaheadStatus::NotInitialized &&
        invalid_policy.ObserveAccess(
            MakeAccess(0ULL, 1ULL, 1ULL, os::kernel::FileReadaheadTrigger::DemandMiss), decision) ==
            os::kernel::FileReadaheadStatus::NotInitialized &&
        invalid_policy.Initialize(os::kernel::FileReadaheadConfiguration{
            .maximum_window_page_count = 0ULL,
        }) == os::kernel::FileReadaheadStatus::InvalidConfiguration &&
        policy.ObserveAccess(MakeAccess(OS_TEST_FILE_READAHEAD_FILE_PAGE_COUNT, 1ULL,
                                        OS_TEST_FILE_READAHEAD_FILE_PAGE_COUNT,
                                        os::kernel::FileReadaheadTrigger::DemandMiss),
                             decision) == os::kernel::FileReadaheadStatus::InvalidAccess &&
        policy.ObserveAccess(MakeAccess(72ULL, 0ULL, OS_TEST_FILE_READAHEAD_FILE_PAGE_COUNT,
                                        os::kernel::FileReadaheadTrigger::DemandHit),
                             decision) == os::kernel::FileReadaheadStatus::InvalidAccess &&
        policy.ObserveAccess(MakeAccess(72ULL, 1ULL, OS_TEST_FILE_READAHEAD_FILE_PAGE_COUNT,
                                        static_cast<os::kernel::FileReadaheadTrigger>(UINT64_MAX)),
                             decision) == os::kernel::FileReadaheadStatus::InvalidAccess &&
        policy.ObserveAccess(MakeAccess(73ULL, 1ULL, OS_TEST_FILE_READAHEAD_FILE_PAGE_COUNT,
                                        os::kernel::FileReadaheadTrigger::PrefetchedHit),
                             decision) == os::kernel::FileReadaheadStatus::InvalidAccess &&
        policy.RecordFeedback(0ULL, 0ULL) == os::kernel::FileReadaheadStatus::InvalidFeedback &&
        maximum_policy.Initialize(os::kernel::FileReadaheadConfiguration{
            .maximum_window_page_count = UINT64_MAX,
        }) == os::kernel::FileReadaheadStatus::Succeeded &&
        maximum_policy.RecordFeedback(0ULL, 1ULL) == os::kernel::FileReadaheadStatus::Succeeded &&
        maximum_policy.Statistics().adaptive_maximum_window_page_count ==
            OS_TEST_FILE_READAHEAD_MAXIMUM_FEEDBACK_EXPECTED_WINDOW &&
        maximum_policy.Validate() == os::kernel::FileReadaheadStatus::Succeeded &&
        policy.Statistics().access_count == statistics_before_failure.access_count &&
        policy.Statistics().generation == statistics_before_failure.generation &&
        policy.Validate() == os::kernel::FileReadaheadStatus::Succeeded;
    test_context.Expect(failures_valid, OS_TEST_FILE_READAHEAD_FAILURE_MESSAGE);
    return test_context.ExitCode();
}

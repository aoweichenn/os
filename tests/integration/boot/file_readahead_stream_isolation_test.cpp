#include <os/kernel/memory/file_readahead.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_FILE_READAHEAD_STREAM_SUITE_NAME =
    "kernel/file_readahead_stream_isolation/integration";
constexpr std::string_view OS_TEST_FILE_READAHEAD_STREAM_MESSAGE =
    "两个打开文件流的窗口、反馈和压力状态必须完全隔离";
constexpr uint64_t OS_TEST_FILE_READAHEAD_STREAM_MAXIMUM_WINDOW_PAGE_COUNT =
    os::kernel::OS_KERNEL_FILE_READAHEAD_DEFAULT_MAXIMUM_WINDOW_PAGE_COUNT;
constexpr uint64_t OS_TEST_FILE_READAHEAD_STREAM_FILE_PAGE_COUNT = 512ULL;

[[nodiscard]] os::kernel::FileReadaheadAccess
MakeAccess(const uint64_t first_page_index, const os::kernel::FileReadaheadTrigger trigger,
           const os::kernel::MemoryPressureLevel pressure_level =
               os::kernel::MemoryPressureLevel::Balanced) noexcept {
    return os::kernel::FileReadaheadAccess{
        .first_page_index = first_page_index,
        .requested_page_count = 1ULL,
        .file_page_count = OS_TEST_FILE_READAHEAD_STREAM_FILE_PAGE_COUNT,
        .trigger = trigger,
        .pressure_level = pressure_level,
    };
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_FILE_READAHEAD_STREAM_SUITE_NAME};
    os::kernel::FileReadaheadPolicy first_stream{};
    os::kernel::FileReadaheadPolicy second_stream{};
    os::kernel::FileReadaheadDecision first_decision{};
    os::kernel::FileReadaheadDecision second_decision{};
    const os::kernel::FileReadaheadConfiguration configuration{
        .maximum_window_page_count = OS_TEST_FILE_READAHEAD_STREAM_MAXIMUM_WINDOW_PAGE_COUNT,
    };
    const bool isolated =
        first_stream.Initialize(configuration) == os::kernel::FileReadaheadStatus::Succeeded &&
        second_stream.Initialize(configuration) == os::kernel::FileReadaheadStatus::Succeeded &&
        first_stream.ObserveAccess(MakeAccess(0ULL, os::kernel::FileReadaheadTrigger::DemandMiss),
                                   first_decision) == os::kernel::FileReadaheadStatus::Succeeded &&
        first_stream.ObserveAccess(
            MakeAccess(1ULL, os::kernel::FileReadaheadTrigger::PrefetchedHit), first_decision) ==
            os::kernel::FileReadaheadStatus::Succeeded &&
        first_decision.window_start_page_index == 4ULL &&
        first_decision.window_page_count == 8ULL &&
        second_stream.ObserveAccess(MakeAccess(200ULL, os::kernel::FileReadaheadTrigger::DemandHit),
                                    second_decision) ==
            os::kernel::FileReadaheadStatus::Succeeded &&
        second_decision.action == os::kernel::FileReadaheadAction::None &&
        second_stream.ObserveAccess(
            MakeAccess(201ULL, os::kernel::FileReadaheadTrigger::DemandMiss), second_decision) ==
            os::kernel::FileReadaheadStatus::Succeeded &&
        second_decision.action == os::kernel::FileReadaheadAction::Submit &&
        second_decision.window_start_page_index == 201ULL &&
        second_stream.RecordFeedback(0ULL, 8ULL) == os::kernel::FileReadaheadStatus::Succeeded &&
        second_stream.Statistics().adaptive_maximum_window_page_count == 16ULL &&
        first_stream.Statistics().adaptive_maximum_window_page_count == 32ULL &&
        first_stream.ObserveAccess(MakeAccess(2ULL, os::kernel::FileReadaheadTrigger::DemandHit,
                                              os::kernel::MemoryPressureLevel::BelowMinimum),
                                   first_decision) == os::kernel::FileReadaheadStatus::Succeeded &&
        !first_stream.Statistics().window_active && second_stream.Statistics().window_active &&
        first_stream.Statistics().generation == 2ULL &&
        second_stream.Statistics().generation == 1ULL &&
        first_stream.Validate() == os::kernel::FileReadaheadStatus::Succeeded &&
        second_stream.Validate() == os::kernel::FileReadaheadStatus::Succeeded;
    test_context.Expect(isolated, OS_TEST_FILE_READAHEAD_STREAM_MESSAGE);
    return test_context.ExitCode();
}

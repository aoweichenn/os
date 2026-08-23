#include <os/kernel/memory/file_readahead_feedback.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_FILE_READAHEAD_FEEDBACK_SUITE_NAME =
    "kernel/file_readahead_feedback/unit";
constexpr std::string_view OS_TEST_FILE_READAHEAD_FEEDBACK_INITIALIZATION =
    "反馈账本必须拒绝空存储、零容量和重复初始化";
constexpr std::string_view OS_TEST_FILE_READAHEAD_FEEDBACK_LIFECYCLE =
    "active stream 必须精确累计、领取 useful/waste 并在 retire 后释放";
constexpr std::string_view OS_TEST_FILE_READAHEAD_FEEDBACK_RETIRING =
    "带活动任务的 stream 必须进入 retiring，任务释放后丢弃无人领取反馈";
constexpr std::string_view OS_TEST_FILE_READAHEAD_FEEDBACK_GENERATION =
    "槽位复用必须递增 generation 且旧 token 反馈只能进入 stale 统计";

constexpr uint64_t OS_TEST_FILE_READAHEAD_FEEDBACK_CAPACITY = 2ULL;
constexpr uint64_t OS_TEST_FILE_READAHEAD_FEEDBACK_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_FILE_READAHEAD_FEEDBACK_FIRST_VALUE = 1ULL;

[[nodiscard]] os::kernel::FileCacheIdentity MakeIdentity(const uint64_t node_identifier) noexcept {
    return os::kernel::FileCacheIdentity{
        .superblock_identifier = 3ULL,
        .superblock_generation = 5ULL,
        .node_identifier = node_identifier,
        .node_generation = 7ULL,
    };
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_FILE_READAHEAD_FEEDBACK_SUITE_NAME};
    os::kernel::FileReadaheadFeedbackLedger invalid_ledger{};
    os::kernel::FileReadaheadFeedbackSlot invalid_slots[OS_TEST_FILE_READAHEAD_FEEDBACK_CAPACITY]{};
    const bool initialization_valid =
        invalid_ledger.Initialize(nullptr, OS_TEST_FILE_READAHEAD_FEEDBACK_CAPACITY) ==
            os::kernel::FileReadaheadFeedbackStatus::InvalidStorage &&
        invalid_ledger.Initialize(invalid_slots, OS_TEST_FILE_READAHEAD_FEEDBACK_EMPTY_VALUE) ==
            os::kernel::FileReadaheadFeedbackStatus::InvalidCapacity &&
        invalid_ledger.Initialize(invalid_slots, OS_TEST_FILE_READAHEAD_FEEDBACK_CAPACITY) ==
            os::kernel::FileReadaheadFeedbackStatus::Succeeded &&
        invalid_ledger.Initialize(invalid_slots, OS_TEST_FILE_READAHEAD_FEEDBACK_CAPACITY) ==
            os::kernel::FileReadaheadFeedbackStatus::AlreadyInitialized;
    test_context.Expect(initialization_valid, OS_TEST_FILE_READAHEAD_FEEDBACK_INITIALIZATION);

    os::kernel::FileReadaheadFeedbackSlot slots[OS_TEST_FILE_READAHEAD_FEEDBACK_CAPACITY]{};
    os::kernel::FileReadaheadFeedbackLedger ledger{};
    os::kernel::FileReadaheadStreamToken first{};
    os::kernel::FileReadaheadStreamToken second{};
    os::kernel::FileReadaheadStreamToken overflow{};
    const bool registered = ledger.Initialize(slots, OS_TEST_FILE_READAHEAD_FEEDBACK_CAPACITY) ==
                                os::kernel::FileReadaheadFeedbackStatus::Succeeded &&
                            ledger.RegisterStream(MakeIdentity(11ULL), first) ==
                                os::kernel::FileReadaheadFeedbackStatus::Succeeded &&
                            ledger.RegisterStream(MakeIdentity(13ULL), second) ==
                                os::kernel::FileReadaheadFeedbackStatus::Succeeded &&
                            ledger.RegisterStream(MakeIdentity(17ULL), overflow) ==
                                os::kernel::FileReadaheadFeedbackStatus::CapacityExhausted;
    os::kernel::FileReadaheadFeedback feedback{};
    const bool active_lifecycle =
        registered &&
        ledger.Record(first,
                      os::kernel::FileReadaheadFeedback{
                          .useful_page_count = 3ULL,
                          .wasted_page_count = 2ULL,
                      }) == os::kernel::FileReadaheadFeedbackStatus::Succeeded &&
        ledger.Take(first, feedback) == os::kernel::FileReadaheadFeedbackStatus::Succeeded &&
        feedback.useful_page_count == 3ULL && feedback.wasted_page_count == 2ULL &&
        ledger.RetireStream(first) == os::kernel::FileReadaheadFeedbackStatus::Succeeded &&
        ledger.Validate() == os::kernel::FileReadaheadFeedbackStatus::Succeeded;
    test_context.Expect(active_lifecycle, OS_TEST_FILE_READAHEAD_FEEDBACK_LIFECYCLE);

    const bool retiring_lifecycle =
        ledger.RetainTask(second) == os::kernel::FileReadaheadFeedbackStatus::Succeeded &&
        ledger.RetireStream(second) == os::kernel::FileReadaheadFeedbackStatus::Succeeded &&
        ledger.Record(second,
                      os::kernel::FileReadaheadFeedback{
                          .useful_page_count = OS_TEST_FILE_READAHEAD_FEEDBACK_EMPTY_VALUE,
                          .wasted_page_count = 4ULL,
                      }) == os::kernel::FileReadaheadFeedbackStatus::Succeeded &&
        ledger.ReleaseTask(second) == os::kernel::FileReadaheadFeedbackStatus::Succeeded &&
        ledger.Statistics().active_stream_count == OS_TEST_FILE_READAHEAD_FEEDBACK_EMPTY_VALUE &&
        ledger.Statistics().retiring_stream_count == OS_TEST_FILE_READAHEAD_FEEDBACK_EMPTY_VALUE &&
        ledger.Statistics().stale_wasted_page_drop_count == 4ULL &&
        ledger.Validate() == os::kernel::FileReadaheadFeedbackStatus::Succeeded;
    test_context.Expect(retiring_lifecycle, OS_TEST_FILE_READAHEAD_FEEDBACK_RETIRING);

    os::kernel::FileReadaheadStreamToken reused{};
    const bool generation_safe =
        ledger.RegisterStream(MakeIdentity(19ULL), reused) ==
            os::kernel::FileReadaheadFeedbackStatus::Succeeded &&
        reused.slot_index == first.slot_index && reused.generation != first.generation &&
        ledger.Record(first,
                      os::kernel::FileReadaheadFeedback{
                          .useful_page_count = OS_TEST_FILE_READAHEAD_FEEDBACK_FIRST_VALUE,
                          .wasted_page_count = OS_TEST_FILE_READAHEAD_FEEDBACK_EMPTY_VALUE,
                      }) == os::kernel::FileReadaheadFeedbackStatus::Succeeded &&
        ledger.Statistics().stale_useful_page_drop_count ==
            OS_TEST_FILE_READAHEAD_FEEDBACK_FIRST_VALUE &&
        ledger.RetireStream(reused) == os::kernel::FileReadaheadFeedbackStatus::Succeeded &&
        ledger.Validate() == os::kernel::FileReadaheadFeedbackStatus::Succeeded;
    test_context.Expect(generation_safe, OS_TEST_FILE_READAHEAD_FEEDBACK_GENERATION);
    return test_context.ExitCode();
}

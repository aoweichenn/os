#include <os/kernel/process/file_page_writeback.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_FILE_PAGE_WRITEBACK_SUITE_NAME =
    "kernel/file_page_writeback/unit";
constexpr std::string_view OS_TEST_FILE_PAGE_WRITEBACK_HANDOFF_MESSAGE =
    "同页 Writeback 必须向阻塞和完成前登记的 waiter 交付唯一结果";
constexpr std::string_view OS_TEST_FILE_PAGE_WRITEBACK_FAILURE_MESSAGE =
    "写回失败必须广播同一错误并保持 generation 与统计守恒";
constexpr uint64_t OS_TEST_FILE_PAGE_WRITEBACK_CAPACITY = 2ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_WRITEBACK_WAITER_CAPACITY = 4ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_WRITEBACK_OWNER_THREAD_INDEX = 0ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_WRITEBACK_FIRST_WAITER_INDEX = 1ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_WRITEBACK_SECOND_WAITER_INDEX = 2ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_WRITEBACK_PHYSICAL_ADDRESS = 0x4000ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_WRITEBACK_GENERATION = 7ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_WRITEBACK_EMPTY_VALUE = 0ULL;

[[nodiscard]] os::kernel::FilePageIdentity TestIdentity() noexcept {
    return os::kernel::FilePageIdentity{
        .file =
            os::kernel::FileIdentity{
                .superblock_identifier = 5ULL,
                .superblock_generation = 2ULL,
                .node_identifier = 11ULL,
                .node_generation = 3ULL,
            },
        .page_index = 13ULL,
    };
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_FILE_PAGE_WRITEBACK_SUITE_NAME};
    os::kernel::FilePageWritebackSlot writeback_storage[OS_TEST_FILE_PAGE_WRITEBACK_CAPACITY]{};
    os::kernel::FilePageWritebackWaiter
        waiter_storage[OS_TEST_FILE_PAGE_WRITEBACK_WAITER_CAPACITY]{};
    os::kernel::FilePageWritebackCoordinator coordinator{};
    os::kernel::FilePageWritebackToken owner_token{};
    os::kernel::FilePageWritebackToken first_waiter_token{};
    os::kernel::FilePageWritebackToken second_waiter_token{};
    const os::kernel::FilePageIdentity identity = TestIdentity();
    bool first_wait_required = false;
    bool second_wait_required = true;
    os::kernel::FilePageWritebackCompletionDecision decision{};
    os::kernel::FilePageCacheStatus result = os::kernel::FilePageCacheStatus::Corrupt;
    const bool handoff_succeeded =
        coordinator.Initialize(writeback_storage, OS_TEST_FILE_PAGE_WRITEBACK_CAPACITY,
                               waiter_storage, OS_TEST_FILE_PAGE_WRITEBACK_WAITER_CAPACITY) ==
            os::kernel::FilePageWritebackStatus::Succeeded &&
        coordinator.Begin(identity, OS_TEST_FILE_PAGE_WRITEBACK_PHYSICAL_ADDRESS,
                          OS_TEST_FILE_PAGE_WRITEBACK_GENERATION,
                          OS_TEST_FILE_PAGE_WRITEBACK_OWNER_THREAD_INDEX,
                          owner_token) == os::kernel::FilePageWritebackStatus::Succeeded &&
        coordinator.Begin(identity, OS_TEST_FILE_PAGE_WRITEBACK_PHYSICAL_ADDRESS,
                          OS_TEST_FILE_PAGE_WRITEBACK_GENERATION + 1ULL,
                          OS_TEST_FILE_PAGE_WRITEBACK_OWNER_THREAD_INDEX, first_waiter_token) ==
            os::kernel::FilePageWritebackStatus::WritebackAlreadyRegistered &&
        coordinator.RegisterWaiter(
            identity, OS_TEST_FILE_PAGE_WRITEBACK_PHYSICAL_ADDRESS,
            OS_TEST_FILE_PAGE_WRITEBACK_GENERATION, OS_TEST_FILE_PAGE_WRITEBACK_FIRST_WAITER_INDEX,
            first_waiter_token) == os::kernel::FilePageWritebackStatus::Succeeded &&
        coordinator.PrepareWait(first_waiter_token, OS_TEST_FILE_PAGE_WRITEBACK_FIRST_WAITER_INDEX,
                                first_wait_required) ==
            os::kernel::FilePageWritebackStatus::Succeeded &&
        first_wait_required &&
        coordinator.RegisterWaiter(
            identity, OS_TEST_FILE_PAGE_WRITEBACK_PHYSICAL_ADDRESS,
            OS_TEST_FILE_PAGE_WRITEBACK_GENERATION, OS_TEST_FILE_PAGE_WRITEBACK_SECOND_WAITER_INDEX,
            second_waiter_token) == os::kernel::FilePageWritebackStatus::Succeeded &&
        coordinator.Complete(owner_token, OS_TEST_FILE_PAGE_WRITEBACK_OWNER_THREAD_INDEX,
                             os::kernel::FilePageCacheStatus::Succeeded,
                             decision) == os::kernel::FilePageWritebackStatus::Succeeded &&
        decision.wake_count == 1ULL;
    const os::kernel::FilePageWritebackToken stale_token = owner_token;
    os::kernel::FilePageWritebackToken next_owner_token{};
    bool completed_owner_can_continue =
        coordinator.Begin(identity, OS_TEST_FILE_PAGE_WRITEBACK_PHYSICAL_ADDRESS,
                          OS_TEST_FILE_PAGE_WRITEBACK_GENERATION + 1ULL,
                          OS_TEST_FILE_PAGE_WRITEBACK_OWNER_THREAD_INDEX,
                          next_owner_token) == os::kernel::FilePageWritebackStatus::Succeeded &&
        coordinator.Complete(next_owner_token, OS_TEST_FILE_PAGE_WRITEBACK_OWNER_THREAD_INDEX,
                             os::kernel::FilePageCacheStatus::Succeeded,
                             decision) == os::kernel::FilePageWritebackStatus::Succeeded &&
        coordinator.PrepareWait(
            second_waiter_token, OS_TEST_FILE_PAGE_WRITEBACK_SECOND_WAITER_INDEX,
            second_wait_required) == os::kernel::FilePageWritebackStatus::Succeeded &&
        !second_wait_required &&
        coordinator.TakeResult(first_waiter_token, OS_TEST_FILE_PAGE_WRITEBACK_FIRST_WAITER_INDEX,
                               result) == os::kernel::FilePageWritebackStatus::Succeeded &&
        result == os::kernel::FilePageCacheStatus::Succeeded &&
        coordinator.TakeResult(second_waiter_token, OS_TEST_FILE_PAGE_WRITEBACK_SECOND_WAITER_INDEX,
                               result) == os::kernel::FilePageWritebackStatus::Succeeded &&
        result == os::kernel::FilePageCacheStatus::Succeeded;
    test_context.Expect(handoff_succeeded && completed_owner_can_continue,
                        OS_TEST_FILE_PAGE_WRITEBACK_HANDOFF_MESSAGE);

    bool failure_wait_required = false;
    const bool failure_succeeded =
        coordinator.Begin(identity, OS_TEST_FILE_PAGE_WRITEBACK_PHYSICAL_ADDRESS,
                          OS_TEST_FILE_PAGE_WRITEBACK_GENERATION + 2ULL,
                          OS_TEST_FILE_PAGE_WRITEBACK_OWNER_THREAD_INDEX,
                          owner_token) == os::kernel::FilePageWritebackStatus::Succeeded &&
        coordinator.RegisterWaiter(identity, OS_TEST_FILE_PAGE_WRITEBACK_PHYSICAL_ADDRESS,
                                   OS_TEST_FILE_PAGE_WRITEBACK_GENERATION + 2ULL,
                                   OS_TEST_FILE_PAGE_WRITEBACK_FIRST_WAITER_INDEX,
                                   first_waiter_token) ==
            os::kernel::FilePageWritebackStatus::Succeeded &&
        coordinator.PrepareWait(first_waiter_token, OS_TEST_FILE_PAGE_WRITEBACK_FIRST_WAITER_INDEX,
                                failure_wait_required) ==
            os::kernel::FilePageWritebackStatus::Succeeded &&
        failure_wait_required &&
        coordinator.Complete(owner_token, OS_TEST_FILE_PAGE_WRITEBACK_OWNER_THREAD_INDEX,
                             os::kernel::FilePageCacheStatus::SourceWriteFailed,
                             decision) == os::kernel::FilePageWritebackStatus::Succeeded &&
        decision.wake_count == 1ULL &&
        coordinator.TakeResult(first_waiter_token, OS_TEST_FILE_PAGE_WRITEBACK_FIRST_WAITER_INDEX,
                               result) == os::kernel::FilePageWritebackStatus::Succeeded &&
        result == os::kernel::FilePageCacheStatus::SourceWriteFailed &&
        coordinator.PrepareWait(stale_token, OS_TEST_FILE_PAGE_WRITEBACK_FIRST_WAITER_INDEX,
                                failure_wait_required) ==
            os::kernel::FilePageWritebackStatus::InvalidToken &&
        coordinator.Validate() == os::kernel::FilePageWritebackStatus::Succeeded;
    const os::kernel::FilePageWritebackStatistics statistics = coordinator.Statistics();
    test_context.Expect(
        failure_succeeded &&
            statistics.active_writeback_count == OS_TEST_FILE_PAGE_WRITEBACK_EMPTY_VALUE &&
            statistics.begin_count == 3ULL && statistics.waiter_registration_count == 3ULL &&
            statistics.wait_commit_count == 2ULL && statistics.immediate_completion_count == 1ULL &&
            statistics.completion_count == 3ULL && statistics.broadcast_wake_count == 2ULL &&
            statistics.failure_broadcast_count == 1ULL && statistics.result_take_count == 3ULL,
        OS_TEST_FILE_PAGE_WRITEBACK_FAILURE_MESSAGE);
    return test_context.ExitCode();
}

#include <os/kernel/process/file_page_load.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_FILE_PAGE_LOAD_SUITE_NAME = "kernel/file_page_load/unit";
constexpr std::string_view OS_TEST_FILE_PAGE_LOAD_BROADCAST_MESSAGE =
    "同页 Loading 必须登记全部 waiter 并只广播一次完成";
constexpr std::string_view OS_TEST_FILE_PAGE_LOAD_FAILURE_MESSAGE =
    "来源读取失败必须向同期 waiter 广播同一结果并使旧 token 失效";
constexpr uint64_t OS_TEST_FILE_PAGE_LOAD_CAPACITY = 2ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_LOAD_WAITER_CAPACITY = 4ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_LOAD_OWNER_THREAD_INDEX = 0ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_LOAD_FIRST_WAITER_INDEX = 1ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_LOAD_SECOND_WAITER_INDEX = 2ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_LOAD_PHYSICAL_ADDRESS = 0ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_LOAD_GENERATION = 7ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_LOAD_EMPTY_VALUE = 0ULL;

[[nodiscard]] os::kernel::FilePageIdentity TestIdentity() noexcept {
    return os::kernel::FilePageIdentity{
        .file =
            os::kernel::FileIdentity{
                .superblock_identifier = 1ULL,
                .superblock_generation = 2ULL,
                .node_identifier = 3ULL,
                .node_generation = 4ULL,
            },
        .page_index = 5ULL,
    };
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_FILE_PAGE_LOAD_SUITE_NAME};
    os::kernel::FilePageLoadSlot load_storage[OS_TEST_FILE_PAGE_LOAD_CAPACITY]{};
    os::kernel::FilePageLoadWaiter waiter_storage[OS_TEST_FILE_PAGE_LOAD_WAITER_CAPACITY]{};
    os::kernel::FilePageLoadCoordinator coordinator{};
    os::kernel::FilePageLoadToken owner_token{};
    os::kernel::FilePageLoadToken first_waiter_token{};
    os::kernel::FilePageLoadToken second_waiter_token{};
    bool first_wait_required = false;
    bool second_wait_required = true;
    uint64_t registered_waiter_count = OS_TEST_FILE_PAGE_LOAD_EMPTY_VALUE;
    os::kernel::FilePageLoadCompletionDecision decision{};
    os::kernel::FilePageCacheStatus result = os::kernel::FilePageCacheStatus::Corrupt;
    const os::kernel::FilePageIdentity identity = TestIdentity();
    const bool broadcast_succeeded =
        coordinator.Initialize(load_storage, OS_TEST_FILE_PAGE_LOAD_CAPACITY, waiter_storage,
                               OS_TEST_FILE_PAGE_LOAD_WAITER_CAPACITY) ==
            os::kernel::FilePageLoadStatus::Succeeded &&
        coordinator.Begin(identity, OS_TEST_FILE_PAGE_LOAD_PHYSICAL_ADDRESS,
                          OS_TEST_FILE_PAGE_LOAD_GENERATION,
                          OS_TEST_FILE_PAGE_LOAD_OWNER_THREAD_INDEX,
                          owner_token) == os::kernel::FilePageLoadStatus::Succeeded &&
        coordinator.RegisterWaiter(identity, OS_TEST_FILE_PAGE_LOAD_PHYSICAL_ADDRESS,
                                   OS_TEST_FILE_PAGE_LOAD_GENERATION,
                                   OS_TEST_FILE_PAGE_LOAD_FIRST_WAITER_INDEX, first_waiter_token) ==
            os::kernel::FilePageLoadStatus::Succeeded &&
        coordinator.PrepareWait(first_waiter_token, OS_TEST_FILE_PAGE_LOAD_FIRST_WAITER_INDEX,
                                first_wait_required) == os::kernel::FilePageLoadStatus::Succeeded &&
        first_wait_required &&
        coordinator.RegisterWaiter(
            identity, OS_TEST_FILE_PAGE_LOAD_PHYSICAL_ADDRESS, OS_TEST_FILE_PAGE_LOAD_GENERATION,
            OS_TEST_FILE_PAGE_LOAD_SECOND_WAITER_INDEX,
            second_waiter_token) == os::kernel::FilePageLoadStatus::Succeeded &&
        coordinator.RegisteredWaiterCount(owner_token, OS_TEST_FILE_PAGE_LOAD_OWNER_THREAD_INDEX,
                                          registered_waiter_count) ==
            os::kernel::FilePageLoadStatus::Succeeded &&
        registered_waiter_count == 2ULL &&
        coordinator.Complete(owner_token, OS_TEST_FILE_PAGE_LOAD_OWNER_THREAD_INDEX,
                             os::kernel::FilePageCacheStatus::Succeeded,
                             decision) == os::kernel::FilePageLoadStatus::Succeeded &&
        decision.slot_index == owner_token.slot_index && decision.wake_count == 1ULL &&
        coordinator.PrepareWait(second_waiter_token, OS_TEST_FILE_PAGE_LOAD_SECOND_WAITER_INDEX,
                                second_wait_required) ==
            os::kernel::FilePageLoadStatus::Succeeded &&
        !second_wait_required &&
        coordinator.TakeResult(first_waiter_token, OS_TEST_FILE_PAGE_LOAD_FIRST_WAITER_INDEX,
                               result) == os::kernel::FilePageLoadStatus::Succeeded &&
        result == os::kernel::FilePageCacheStatus::Succeeded &&
        coordinator.TakeResult(second_waiter_token, OS_TEST_FILE_PAGE_LOAD_SECOND_WAITER_INDEX,
                               result) == os::kernel::FilePageLoadStatus::Succeeded &&
        result == os::kernel::FilePageCacheStatus::Succeeded &&
        coordinator.Validate() == os::kernel::FilePageLoadStatus::Succeeded;
    test_context.Expect(broadcast_succeeded, OS_TEST_FILE_PAGE_LOAD_BROADCAST_MESSAGE);

    const os::kernel::FilePageLoadToken stale_token = owner_token;
    bool failure_wait_required = false;
    const bool failure_succeeded =
        coordinator.Begin(identity, OS_TEST_FILE_PAGE_LOAD_PHYSICAL_ADDRESS,
                          OS_TEST_FILE_PAGE_LOAD_GENERATION + 1ULL,
                          OS_TEST_FILE_PAGE_LOAD_OWNER_THREAD_INDEX,
                          owner_token) == os::kernel::FilePageLoadStatus::Succeeded &&
        coordinator.RegisterWaiter(identity, OS_TEST_FILE_PAGE_LOAD_PHYSICAL_ADDRESS,
                                   OS_TEST_FILE_PAGE_LOAD_GENERATION + 1ULL,
                                   OS_TEST_FILE_PAGE_LOAD_FIRST_WAITER_INDEX, first_waiter_token) ==
            os::kernel::FilePageLoadStatus::Succeeded &&
        coordinator.PrepareWait(first_waiter_token, OS_TEST_FILE_PAGE_LOAD_FIRST_WAITER_INDEX,
                                failure_wait_required) ==
            os::kernel::FilePageLoadStatus::Succeeded &&
        failure_wait_required &&
        coordinator.RegisteredWaiterCount(owner_token, OS_TEST_FILE_PAGE_LOAD_FIRST_WAITER_INDEX,
                                          registered_waiter_count) ==
            os::kernel::FilePageLoadStatus::InvalidState &&
        coordinator.Complete(owner_token, OS_TEST_FILE_PAGE_LOAD_OWNER_THREAD_INDEX,
                             os::kernel::FilePageCacheStatus::SourceReadFailed,
                             decision) == os::kernel::FilePageLoadStatus::Succeeded &&
        decision.wake_count == 1ULL &&
        coordinator.TakeResult(first_waiter_token, OS_TEST_FILE_PAGE_LOAD_FIRST_WAITER_INDEX,
                               result) == os::kernel::FilePageLoadStatus::Succeeded &&
        result == os::kernel::FilePageCacheStatus::SourceReadFailed &&
        coordinator.PrepareWait(stale_token, OS_TEST_FILE_PAGE_LOAD_FIRST_WAITER_INDEX,
                                failure_wait_required) ==
            os::kernel::FilePageLoadStatus::InvalidToken &&
        coordinator.Validate() == os::kernel::FilePageLoadStatus::Succeeded &&
        coordinator.Statistics().active_load_count == OS_TEST_FILE_PAGE_LOAD_EMPTY_VALUE &&
        coordinator.Statistics().begin_count == 2ULL &&
        coordinator.Statistics().waiter_registration_count == 3ULL &&
        coordinator.Statistics().wait_commit_count == 2ULL &&
        coordinator.Statistics().immediate_completion_count == 1ULL &&
        coordinator.Statistics().completion_count == 2ULL &&
        coordinator.Statistics().broadcast_wake_count == 2ULL &&
        coordinator.Statistics().failure_broadcast_count == 1ULL &&
        coordinator.Statistics().result_take_count == 3ULL;
    test_context.Expect(failure_succeeded, OS_TEST_FILE_PAGE_LOAD_FAILURE_MESSAGE);
    return test_context.ExitCode();
}

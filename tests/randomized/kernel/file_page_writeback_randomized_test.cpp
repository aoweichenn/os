#include <os/kernel/process/file_page_writeback.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_FILE_PAGE_WRITEBACK_RANDOMIZED_SUITE_NAME =
    "kernel/file_page_writeback/randomized";
constexpr std::string_view OS_TEST_FILE_PAGE_WRITEBACK_RANDOMIZED_MESSAGE =
    "固定种子十万轮 Writeback 广播模型必须保持 generation、单赢家结果和 waiter 守恒";
constexpr uint64_t OS_TEST_FILE_PAGE_WRITEBACK_RANDOMIZED_SEED = 0x5752495445574149ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_WRITEBACK_RANDOMIZED_ITERATION_COUNT = 100000ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_WRITEBACK_RANDOMIZED_WRITEBACK_CAPACITY = 2ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_WRITEBACK_RANDOMIZED_WAITER_CAPACITY = 8ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_WRITEBACK_RANDOMIZED_MAXIMUM_WAITER_COUNT = 7ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_WRITEBACK_RANDOMIZED_OWNER_THREAD_INDEX = 0ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_WRITEBACK_RANDOMIZED_FIRST_WAITER_INDEX = 1ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_WRITEBACK_RANDOMIZED_PHYSICAL_ADDRESS = 0x8000ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_WRITEBACK_RANDOMIZED_EMPTY_VALUE = 0ULL;

[[nodiscard]] uint64_t NextRandom(uint64_t &state) noexcept {
    state ^= state << 13ULL;
    state ^= state >> 7ULL;
    state ^= state << 17ULL;
    return state;
}

[[nodiscard]] os::kernel::FilePageIdentity TestIdentity(const uint64_t iteration) noexcept {
    return os::kernel::FilePageIdentity{
        .file =
            os::kernel::FileIdentity{
                .superblock_identifier = 1ULL,
                .superblock_generation = 1ULL,
                .node_identifier = iteration + 1ULL,
                .node_generation = 1ULL,
            },
        .page_index = iteration,
    };
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_FILE_PAGE_WRITEBACK_RANDOMIZED_SUITE_NAME};
    os::kernel::FilePageWritebackSlot
        writeback_storage[OS_TEST_FILE_PAGE_WRITEBACK_RANDOMIZED_WRITEBACK_CAPACITY]{};
    os::kernel::FilePageWritebackWaiter
        waiter_storage[OS_TEST_FILE_PAGE_WRITEBACK_RANDOMIZED_WAITER_CAPACITY]{};
    os::kernel::FilePageWritebackCoordinator coordinator{};
    bool consistent =
        coordinator.Initialize(
            writeback_storage, OS_TEST_FILE_PAGE_WRITEBACK_RANDOMIZED_WRITEBACK_CAPACITY,
            waiter_storage, OS_TEST_FILE_PAGE_WRITEBACK_RANDOMIZED_WAITER_CAPACITY) ==
        os::kernel::FilePageWritebackStatus::Succeeded;
    uint64_t random_state = OS_TEST_FILE_PAGE_WRITEBACK_RANDOMIZED_SEED;
    uint64_t expected_waiter_registration_count =
        OS_TEST_FILE_PAGE_WRITEBACK_RANDOMIZED_EMPTY_VALUE;
    uint64_t expected_wait_commit_count = OS_TEST_FILE_PAGE_WRITEBACK_RANDOMIZED_EMPTY_VALUE;
    uint64_t expected_immediate_completion_count =
        OS_TEST_FILE_PAGE_WRITEBACK_RANDOMIZED_EMPTY_VALUE;
    uint64_t expected_broadcast_wake_count = OS_TEST_FILE_PAGE_WRITEBACK_RANDOMIZED_EMPTY_VALUE;
    uint64_t expected_failure_broadcast_count = OS_TEST_FILE_PAGE_WRITEBACK_RANDOMIZED_EMPTY_VALUE;
    uint64_t expected_result_take_count = OS_TEST_FILE_PAGE_WRITEBACK_RANDOMIZED_EMPTY_VALUE;
    for (uint64_t iteration = OS_TEST_FILE_PAGE_WRITEBACK_RANDOMIZED_EMPTY_VALUE;
         consistent && iteration < OS_TEST_FILE_PAGE_WRITEBACK_RANDOMIZED_ITERATION_COUNT;
         ++iteration) {
        const os::kernel::FilePageIdentity identity = TestIdentity(iteration);
        const uint64_t writeback_generation = iteration + 1ULL;
        os::kernel::FilePageWritebackToken owner_token{};
        consistent =
            coordinator.Begin(identity, OS_TEST_FILE_PAGE_WRITEBACK_RANDOMIZED_PHYSICAL_ADDRESS,
                              writeback_generation,
                              OS_TEST_FILE_PAGE_WRITEBACK_RANDOMIZED_OWNER_THREAD_INDEX,
                              owner_token) == os::kernel::FilePageWritebackStatus::Succeeded;
        const uint64_t waiter_count =
            NextRandom(random_state) %
            (OS_TEST_FILE_PAGE_WRITEBACK_RANDOMIZED_MAXIMUM_WAITER_COUNT + 1ULL);
        const uint64_t precompletion_waiter_count =
            waiter_count == OS_TEST_FILE_PAGE_WRITEBACK_RANDOMIZED_EMPTY_VALUE
                ? OS_TEST_FILE_PAGE_WRITEBACK_RANDOMIZED_EMPTY_VALUE
                : NextRandom(random_state) % (waiter_count + 1ULL);
        os::kernel::FilePageWritebackToken
            waiter_tokens[OS_TEST_FILE_PAGE_WRITEBACK_RANDOMIZED_MAXIMUM_WAITER_COUNT]{};
        for (uint64_t waiter_ordinal = OS_TEST_FILE_PAGE_WRITEBACK_RANDOMIZED_EMPTY_VALUE;
             consistent && waiter_ordinal < waiter_count; ++waiter_ordinal) {
            const uint64_t waiter_thread_index =
                OS_TEST_FILE_PAGE_WRITEBACK_RANDOMIZED_FIRST_WAITER_INDEX + waiter_ordinal;
            consistent =
                coordinator.RegisterWaiter(
                    identity, OS_TEST_FILE_PAGE_WRITEBACK_RANDOMIZED_PHYSICAL_ADDRESS,
                    writeback_generation, waiter_thread_index, waiter_tokens[waiter_ordinal]) ==
                os::kernel::FilePageWritebackStatus::Succeeded;
            ++expected_waiter_registration_count;
            if (waiter_ordinal < precompletion_waiter_count) {
                bool wait_required = false;
                consistent = coordinator.PrepareWait(waiter_tokens[waiter_ordinal],
                                                     waiter_thread_index, wait_required) ==
                                 os::kernel::FilePageWritebackStatus::Succeeded &&
                             wait_required;
                ++expected_wait_commit_count;
            }
        }
        const bool writeback_succeeded = (NextRandom(random_state) & 1ULL) == 0ULL;
        const os::kernel::FilePageCacheStatus expected_result =
            writeback_succeeded ? os::kernel::FilePageCacheStatus::Succeeded
                                : os::kernel::FilePageCacheStatus::SourceWriteFailed;
        os::kernel::FilePageWritebackCompletionDecision decision{};
        consistent = consistent &&
                     coordinator.Complete(owner_token,
                                          OS_TEST_FILE_PAGE_WRITEBACK_RANDOMIZED_OWNER_THREAD_INDEX,
                                          expected_result, decision) ==
                         os::kernel::FilePageWritebackStatus::Succeeded &&
                     decision.wake_count == precompletion_waiter_count;
        expected_broadcast_wake_count += precompletion_waiter_count;
        if (!writeback_succeeded &&
            waiter_count != OS_TEST_FILE_PAGE_WRITEBACK_RANDOMIZED_EMPTY_VALUE) {
            ++expected_failure_broadcast_count;
        }
        for (uint64_t waiter_ordinal = precompletion_waiter_count;
             consistent && waiter_ordinal < waiter_count; ++waiter_ordinal) {
            const uint64_t waiter_thread_index =
                OS_TEST_FILE_PAGE_WRITEBACK_RANDOMIZED_FIRST_WAITER_INDEX + waiter_ordinal;
            bool wait_required = true;
            consistent = coordinator.PrepareWait(waiter_tokens[waiter_ordinal], waiter_thread_index,
                                                 wait_required) ==
                             os::kernel::FilePageWritebackStatus::Succeeded &&
                         !wait_required;
            ++expected_immediate_completion_count;
        }
        for (uint64_t consumed_waiter_count = OS_TEST_FILE_PAGE_WRITEBACK_RANDOMIZED_EMPTY_VALUE;
             consistent && consumed_waiter_count < waiter_count; ++consumed_waiter_count) {
            const uint64_t remaining_waiter_count = waiter_count - consumed_waiter_count;
            const uint64_t selected_offset = NextRandom(random_state) % remaining_waiter_count;
            uint64_t observed_offset = OS_TEST_FILE_PAGE_WRITEBACK_RANDOMIZED_EMPTY_VALUE;
            uint64_t selected_ordinal = OS_TEST_FILE_PAGE_WRITEBACK_RANDOMIZED_EMPTY_VALUE;
            for (uint64_t waiter_ordinal = OS_TEST_FILE_PAGE_WRITEBACK_RANDOMIZED_EMPTY_VALUE;
                 waiter_ordinal < waiter_count; ++waiter_ordinal) {
                if (waiter_tokens[waiter_ordinal].generation ==
                    OS_TEST_FILE_PAGE_WRITEBACK_RANDOMIZED_EMPTY_VALUE) {
                    continue;
                }
                if (observed_offset == selected_offset) {
                    selected_ordinal = waiter_ordinal;
                    break;
                }
                ++observed_offset;
            }
            const uint64_t waiter_thread_index =
                OS_TEST_FILE_PAGE_WRITEBACK_RANDOMIZED_FIRST_WAITER_INDEX + selected_ordinal;
            os::kernel::FilePageCacheStatus result =
                os::kernel::FilePageCacheStatus::WritebackWaitFailed;
            consistent =
                coordinator.TakeResult(waiter_tokens[selected_ordinal], waiter_thread_index,
                                       result) == os::kernel::FilePageWritebackStatus::Succeeded &&
                result == expected_result;
            waiter_tokens[selected_ordinal] = os::kernel::FilePageWritebackToken{};
            ++expected_result_take_count;
        }
        bool stale_wait_required = false;
        consistent =
            consistent &&
            coordinator.PrepareWait(
                owner_token, OS_TEST_FILE_PAGE_WRITEBACK_RANDOMIZED_FIRST_WAITER_INDEX,
                stale_wait_required) == os::kernel::FilePageWritebackStatus::InvalidToken &&
            coordinator.Validate() == os::kernel::FilePageWritebackStatus::Succeeded;
    }
    const os::kernel::FilePageWritebackStatistics statistics = coordinator.Statistics();
    consistent =
        consistent &&
        statistics.active_writeback_count == OS_TEST_FILE_PAGE_WRITEBACK_RANDOMIZED_EMPTY_VALUE &&
        statistics.begin_count == OS_TEST_FILE_PAGE_WRITEBACK_RANDOMIZED_ITERATION_COUNT &&
        statistics.waiter_registration_count == expected_waiter_registration_count &&
        statistics.wait_commit_count == expected_wait_commit_count &&
        statistics.immediate_completion_count == expected_immediate_completion_count &&
        statistics.completion_count == OS_TEST_FILE_PAGE_WRITEBACK_RANDOMIZED_ITERATION_COUNT &&
        statistics.broadcast_wake_count == expected_broadcast_wake_count &&
        statistics.failure_broadcast_count == expected_failure_broadcast_count &&
        statistics.result_take_count == expected_result_take_count;
    test_context.Expect(consistent, OS_TEST_FILE_PAGE_WRITEBACK_RANDOMIZED_MESSAGE);
    return test_context.ExitCode();
}

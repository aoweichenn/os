#include <os/kernel/process/file_page_writeback.hpp>
#include <os/kernel/process/thread_scheduler.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_FILE_PAGE_WRITEBACK_WAIT_SUITE_NAME =
    "kernel/file_page_writeback_wait/integration";
constexpr std::string_view OS_TEST_FILE_PAGE_WRITEBACK_WAIT_HANDOFF_MESSAGE =
    "Writeback completion 必须通过独立 WaitQueue 广播并让所有 waiter 消费同一错误";
constexpr uint64_t OS_TEST_FILE_PAGE_WRITEBACK_WAIT_CAPACITY = 3ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_WRITEBACK_WAIT_THREADS_PER_PROCESS = 1ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_WRITEBACK_WAIT_QUANTUM_TICKS = 4ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_WRITEBACK_WAIT_ADDRESS_SPACE_BASE = 0x100000ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_WRITEBACK_WAIT_ADDRESS_SPACE_STRIDE = 0x1000ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_WRITEBACK_WAIT_USER_STACK_BASE = 0x70000000ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_WRITEBACK_WAIT_USER_STACK_STRIDE = 0x10000ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_WRITEBACK_WAIT_QUEUE_IDENTIFIER = 61ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_WRITEBACK_WAIT_PHYSICAL_ADDRESS = 0x5000ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_WRITEBACK_WAIT_GENERATION = 17ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_WRITEBACK_WAIT_EMPTY_VALUE = 0ULL;

[[nodiscard]] os::kernel::FilePageIdentity TestIdentity() noexcept {
    return os::kernel::FilePageIdentity{
        .file =
            os::kernel::FileIdentity{
                .superblock_identifier = 1ULL,
                .superblock_generation = 1ULL,
                .node_identifier = 9ULL,
                .node_generation = 4ULL,
            },
        .page_index = 6ULL,
    };
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_FILE_PAGE_WRITEBACK_WAIT_SUITE_NAME};
    os::kernel::ProcessEntry processes[OS_TEST_FILE_PAGE_WRITEBACK_WAIT_CAPACITY]{};
    os::kernel::ThreadEntry threads[OS_TEST_FILE_PAGE_WRITEBACK_WAIT_CAPACITY]{};
    os::kernel::ThreadScheduler scheduler{};
    os::kernel::WaitQueue wait_queue{};
    os::kernel::FilePageWritebackSlot
        writeback_storage[OS_TEST_FILE_PAGE_WRITEBACK_WAIT_CAPACITY]{};
    os::kernel::FilePageWritebackWaiter waiter_storage[OS_TEST_FILE_PAGE_WRITEBACK_WAIT_CAPACITY]{};
    os::kernel::FilePageWritebackCoordinator coordinator{};
    bool consistent =
        scheduler.Initialize(processes, OS_TEST_FILE_PAGE_WRITEBACK_WAIT_CAPACITY, threads,
                             OS_TEST_FILE_PAGE_WRITEBACK_WAIT_CAPACITY,
                             OS_TEST_FILE_PAGE_WRITEBACK_WAIT_THREADS_PER_PROCESS,
                             OS_TEST_FILE_PAGE_WRITEBACK_WAIT_QUANTUM_TICKS) ==
            os::kernel::ThreadSchedulerStatus::Succeeded &&
        wait_queue.Initialize(os::kernel::WaitQueueId{
            .value = OS_TEST_FILE_PAGE_WRITEBACK_WAIT_QUEUE_IDENTIFIER,
        }) == os::kernel::WaitQueueStatus::Succeeded &&
        coordinator.Initialize(writeback_storage, OS_TEST_FILE_PAGE_WRITEBACK_WAIT_CAPACITY,
                               waiter_storage, OS_TEST_FILE_PAGE_WRITEBACK_WAIT_CAPACITY) ==
            os::kernel::FilePageWritebackStatus::Succeeded;
    for (uint64_t process_ordinal = OS_TEST_FILE_PAGE_WRITEBACK_WAIT_EMPTY_VALUE;
         consistent && process_ordinal < OS_TEST_FILE_PAGE_WRITEBACK_WAIT_CAPACITY;
         ++process_ordinal) {
        uint64_t process_index = os::kernel::OS_KERNEL_PROCESS_INVALID_INDEX;
        os::kernel::ProcessId process_id{};
        uint64_t thread_index = os::kernel::OS_KERNEL_THREAD_INVALID_INDEX;
        os::kernel::ThreadId thread_id{};
        consistent =
            scheduler.CreateProcess(
                OS_TEST_FILE_PAGE_WRITEBACK_WAIT_ADDRESS_SPACE_BASE +
                    process_ordinal * OS_TEST_FILE_PAGE_WRITEBACK_WAIT_ADDRESS_SPACE_STRIDE,
                process_index, process_id) == os::kernel::ThreadSchedulerStatus::Succeeded &&
            scheduler.CreateThread(process_index, process_ordinal,
                                   OS_TEST_FILE_PAGE_WRITEBACK_WAIT_USER_STACK_BASE +
                                       process_ordinal *
                                           OS_TEST_FILE_PAGE_WRITEBACK_WAIT_USER_STACK_STRIDE,
                                   OS_TEST_FILE_PAGE_WRITEBACK_WAIT_EMPTY_VALUE,
                                   OS_TEST_FILE_PAGE_WRITEBACK_WAIT_EMPTY_VALUE, thread_index,
                                   thread_id) == os::kernel::ThreadSchedulerStatus::Succeeded;
    }
    os::kernel::ThreadSchedulingDecision decision{};
    consistent =
        consistent && scheduler.Start(decision) == os::kernel::ThreadSchedulerStatus::Succeeded;
    const uint64_t owner_thread_index = scheduler.CurrentThreadIndex();
    const os::kernel::FilePageIdentity identity = TestIdentity();
    os::kernel::FilePageWritebackToken owner_token{};
    consistent =
        consistent &&
        coordinator.Begin(identity, OS_TEST_FILE_PAGE_WRITEBACK_WAIT_PHYSICAL_ADDRESS,
                          OS_TEST_FILE_PAGE_WRITEBACK_WAIT_GENERATION, owner_thread_index,
                          owner_token) == os::kernel::FilePageWritebackStatus::Succeeded &&
        scheduler.YieldCurrentThread(decision) == os::kernel::ThreadSchedulerStatus::Succeeded;

    const uint64_t first_waiter_index = scheduler.CurrentThreadIndex();
    os::kernel::FilePageWritebackToken first_waiter_token{};
    bool wait_required = false;
    consistent =
        consistent &&
        coordinator.RegisterWaiter(identity, OS_TEST_FILE_PAGE_WRITEBACK_WAIT_PHYSICAL_ADDRESS,
                                   OS_TEST_FILE_PAGE_WRITEBACK_WAIT_GENERATION, first_waiter_index,
                                   first_waiter_token) ==
            os::kernel::FilePageWritebackStatus::Succeeded &&
        coordinator.PrepareWait(first_waiter_token, first_waiter_index, wait_required) ==
            os::kernel::FilePageWritebackStatus::Succeeded &&
        wait_required &&
        scheduler.BlockCurrentThread(wait_queue, os::kernel::WaitCondition::FilePageWriteback,
                                     decision) == os::kernel::ThreadSchedulerStatus::Succeeded;
    const uint64_t second_waiter_index = scheduler.CurrentThreadIndex();
    os::kernel::FilePageWritebackToken second_waiter_token{};
    consistent =
        consistent &&
        coordinator.RegisterWaiter(identity, OS_TEST_FILE_PAGE_WRITEBACK_WAIT_PHYSICAL_ADDRESS,
                                   OS_TEST_FILE_PAGE_WRITEBACK_WAIT_GENERATION, second_waiter_index,
                                   second_waiter_token) ==
            os::kernel::FilePageWritebackStatus::Succeeded &&
        coordinator.PrepareWait(second_waiter_token, second_waiter_index, wait_required) ==
            os::kernel::FilePageWritebackStatus::Succeeded &&
        wait_required &&
        scheduler.BlockCurrentThread(wait_queue, os::kernel::WaitCondition::FilePageWriteback,
                                     decision) == os::kernel::ThreadSchedulerStatus::Succeeded &&
        scheduler.CurrentThreadIndex() == owner_thread_index;

    os::kernel::FilePageWritebackCompletionDecision completion_decision{};
    uint64_t woken_thread_count = OS_TEST_FILE_PAGE_WRITEBACK_WAIT_EMPTY_VALUE;
    consistent = consistent &&
                 coordinator.Complete(owner_token, owner_thread_index,
                                      os::kernel::FilePageCacheStatus::SourceWriteFailed,
                                      completion_decision) ==
                     os::kernel::FilePageWritebackStatus::Succeeded &&
                 completion_decision.wake_count == 2ULL &&
                 scheduler.WakeMany(wait_queue, os::kernel::WakeReason::ConditionSatisfied,
                                    completion_decision.wake_count, woken_thread_count) ==
                     os::kernel::ThreadSchedulerStatus::Succeeded &&
                 woken_thread_count == completion_decision.wake_count;
    os::kernel::FilePageCacheStatus first_result = os::kernel::FilePageCacheStatus::Corrupt;
    os::kernel::FilePageCacheStatus second_result = os::kernel::FilePageCacheStatus::Corrupt;
    consistent =
        consistent &&
        coordinator.TakeResult(first_waiter_token, first_waiter_index, first_result) ==
            os::kernel::FilePageWritebackStatus::Succeeded &&
        coordinator.TakeResult(second_waiter_token, second_waiter_index, second_result) ==
            os::kernel::FilePageWritebackStatus::Succeeded &&
        first_result == os::kernel::FilePageCacheStatus::SourceWriteFailed &&
        second_result == os::kernel::FilePageCacheStatus::SourceWriteFailed &&
        coordinator.Validate() == os::kernel::FilePageWritebackStatus::Succeeded &&
        scheduler.ValidateWaitQueue(wait_queue) == os::kernel::ThreadSchedulerStatus::Succeeded &&
        wait_queue.Statistics().waiting_thread_count ==
            OS_TEST_FILE_PAGE_WRITEBACK_WAIT_EMPTY_VALUE;
    test_context.Expect(consistent, OS_TEST_FILE_PAGE_WRITEBACK_WAIT_HANDOFF_MESSAGE);
    return test_context.ExitCode();
}

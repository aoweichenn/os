#include <os/kernel/process/block_io.hpp>
#include <os/kernel/process/thread_scheduler.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_BLOCK_IO_WAIT_SUITE_NAME =
    "kernel/block_io_wait_queue/integration";
constexpr std::string_view OS_TEST_BLOCK_IO_WAIT_HANDOFF =
    "BlockIo wait commit、完成决策和 WaitQueue 精确唤醒必须形成一次原子交接";
constexpr uint64_t OS_TEST_BLOCK_IO_WAIT_PROCESS_CAPACITY = 2ULL;
constexpr uint64_t OS_TEST_BLOCK_IO_WAIT_THREAD_CAPACITY = 2ULL;
constexpr uint64_t OS_TEST_BLOCK_IO_WAIT_THREADS_PER_PROCESS = 1ULL;
constexpr uint64_t OS_TEST_BLOCK_IO_WAIT_QUANTUM_TICKS = 4ULL;
constexpr uint64_t OS_TEST_BLOCK_IO_WAIT_ADDRESS_SPACE_BASE = 0x00100000ULL;
constexpr uint64_t OS_TEST_BLOCK_IO_WAIT_ADDRESS_SPACE_STRIDE = 0x00001000ULL;
constexpr uint64_t OS_TEST_BLOCK_IO_WAIT_USER_STACK_BASE = 0x70000000ULL;
constexpr uint64_t OS_TEST_BLOCK_IO_WAIT_USER_STACK_STRIDE = 0x00010000ULL;
constexpr uint64_t OS_TEST_BLOCK_IO_WAIT_QUEUE_IDENTIFIER = 41ULL;
constexpr uint64_t OS_TEST_BLOCK_IO_WAIT_REQUEST_IDENTIFIER = 77ULL;
constexpr uint64_t OS_TEST_BLOCK_IO_WAIT_EMPTY_VALUE = 0ULL;

}

int main() {
    os::test::TestContext test_context{OS_TEST_BLOCK_IO_WAIT_SUITE_NAME};
    os::kernel::ProcessEntry processes[OS_TEST_BLOCK_IO_WAIT_PROCESS_CAPACITY]{};
    os::kernel::ThreadEntry threads[OS_TEST_BLOCK_IO_WAIT_THREAD_CAPACITY]{};
    os::kernel::ThreadScheduler scheduler{};
    os::kernel::WaitQueue wait_queue{};
    os::kernel::BlockIoSlot slots[OS_TEST_BLOCK_IO_WAIT_THREAD_CAPACITY]{};
    os::kernel::BlockIoCoordinator coordinator{};
    bool consistent = scheduler.Initialize(processes, OS_TEST_BLOCK_IO_WAIT_PROCESS_CAPACITY,
                                           threads, OS_TEST_BLOCK_IO_WAIT_THREAD_CAPACITY,
                                           OS_TEST_BLOCK_IO_WAIT_THREADS_PER_PROCESS,
                                           OS_TEST_BLOCK_IO_WAIT_QUANTUM_TICKS) ==
                          os::kernel::ThreadSchedulerStatus::Succeeded &&
                      wait_queue.Initialize(os::kernel::WaitQueueId{
                          .value = OS_TEST_BLOCK_IO_WAIT_QUEUE_IDENTIFIER,
                      }) == os::kernel::WaitQueueStatus::Succeeded &&
                      coordinator.Initialize(slots, OS_TEST_BLOCK_IO_WAIT_THREAD_CAPACITY) ==
                          os::kernel::BlockIoStatus::Succeeded;
    for (uint64_t process_ordinal = 0ULL;
         consistent && process_ordinal < OS_TEST_BLOCK_IO_WAIT_PROCESS_CAPACITY;
         ++process_ordinal) {
        uint64_t process_index = os::kernel::OS_KERNEL_PROCESS_INVALID_INDEX;
        os::kernel::ProcessId process_id{};
        uint64_t thread_index = os::kernel::OS_KERNEL_THREAD_INVALID_INDEX;
        os::kernel::ThreadId thread_id{};
        consistent =
            scheduler.CreateProcess(
                OS_TEST_BLOCK_IO_WAIT_ADDRESS_SPACE_BASE +
                    process_ordinal * OS_TEST_BLOCK_IO_WAIT_ADDRESS_SPACE_STRIDE,
                process_index, process_id) == os::kernel::ThreadSchedulerStatus::Succeeded &&
            scheduler.CreateThread(process_index, process_ordinal,
                                   OS_TEST_BLOCK_IO_WAIT_USER_STACK_BASE +
                                       process_ordinal * OS_TEST_BLOCK_IO_WAIT_USER_STACK_STRIDE,
                                   OS_TEST_BLOCK_IO_WAIT_EMPTY_VALUE,
                                   OS_TEST_BLOCK_IO_WAIT_EMPTY_VALUE, thread_index,
                                   thread_id) == os::kernel::ThreadSchedulerStatus::Succeeded;
    }
    os::kernel::ThreadSchedulingDecision scheduling_decision{};
    consistent = consistent && scheduler.Start(scheduling_decision) ==
                                   os::kernel::ThreadSchedulerStatus::Succeeded;
    const uint64_t owner_thread_index = scheduler.CurrentThreadIndex();
    os::kernel::BlockIoTicket ticket{};
    bool wait_required = false;
    consistent =
        consistent && owner_thread_index < OS_TEST_BLOCK_IO_WAIT_THREAD_CAPACITY &&
        coordinator.Register(OS_TEST_BLOCK_IO_WAIT_REQUEST_IDENTIFIER, owner_thread_index,
                             ticket) == os::kernel::BlockIoStatus::Succeeded &&
        coordinator.PrepareWait(ticket, wait_required) == os::kernel::BlockIoStatus::Succeeded &&
        wait_required &&
        scheduler.BlockCurrentThread(wait_queue, os::kernel::WaitCondition::BlockIo,
                                     scheduling_decision) ==
            os::kernel::ThreadSchedulerStatus::Succeeded &&
        scheduling_decision.switched;

    os::kernel::BlockIoCompletionDecision completion_decision{};
    bool wake_won = false;
    consistent =
        consistent &&
        coordinator.Complete(owner_thread_index, OS_TEST_BLOCK_IO_WAIT_REQUEST_IDENTIFIER,
                             os::kernel::BlockRequestResult::Succeeded,
                             completion_decision) == os::kernel::BlockIoStatus::Succeeded &&
        completion_decision.wake_required &&
        completion_decision.owner_thread_index == owner_thread_index &&
        scheduler.WakeThread(wait_queue, completion_decision.owner_thread_index,
                             os::kernel::WakeReason::ConditionSatisfied,
                             wake_won) == os::kernel::ThreadSchedulerStatus::Succeeded &&
        wake_won;
    os::kernel::BlockRequestResult result = os::kernel::BlockRequestResult::None;
    consistent =
        consistent &&
        coordinator.TakeResult(ticket, result) == os::kernel::BlockIoStatus::Succeeded &&
        result == os::kernel::BlockRequestResult::Succeeded &&
        coordinator.Validate() == os::kernel::BlockIoStatus::Succeeded &&
        scheduler.ValidateWaitQueue(wait_queue) == os::kernel::ThreadSchedulerStatus::Succeeded &&
        wait_queue.Statistics().waiting_thread_count == OS_TEST_BLOCK_IO_WAIT_EMPTY_VALUE;
    test_context.Expect(consistent, OS_TEST_BLOCK_IO_WAIT_HANDOFF);
    return test_context.ExitCode();
}

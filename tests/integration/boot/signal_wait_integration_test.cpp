#include "os/kernel/process/signal_manager.hpp"
#include "os/kernel/process/thread_scheduler.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_SIGNAL_WAIT_SUITE_NAME = "kernel/signal_wait/integration";
constexpr std::string_view OS_TEST_SIGNAL_WAIT_WINNER_MESSAGE =
    "signal、condition 与 timeout 竞争必须只提交一个 WakeReason";
constexpr uint64_t OS_TEST_SIGNAL_WAIT_PROCESS_CAPACITY = 1ULL;
constexpr uint64_t OS_TEST_SIGNAL_WAIT_THREAD_CAPACITY = 2ULL;
constexpr uint64_t OS_TEST_SIGNAL_WAIT_THREAD_LIMIT = 2ULL;
constexpr uint64_t OS_TEST_SIGNAL_WAIT_QUANTUM = 4ULL;
constexpr uint64_t OS_TEST_SIGNAL_WAIT_ROOT = 0x1000ULL;
constexpr uint64_t OS_TEST_SIGNAL_WAIT_STACK = 0x70000000ULL;
constexpr uint64_t OS_TEST_SIGNAL_WAIT_QUEUE_ID = 17ULL;
constexpr uint64_t OS_TEST_SIGNAL_WAIT_DEADLINE = 1000ULL;
constexpr uint64_t OS_TEST_SIGNAL_WAIT_PROCESS_ID = 1ULL;
constexpr uint64_t OS_TEST_SIGNAL_WAIT_THREAD_ID = 1ULL;
constexpr uint64_t OS_TEST_SIGNAL_WAIT_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_SIGNAL_WAIT_EXPECTED_CANCELLATION_COUNT = 1ULL;

}

int main() {
    os::test::TestContext test_context{OS_TEST_SIGNAL_WAIT_SUITE_NAME};
    os::kernel::ProcessEntry scheduler_processes[OS_TEST_SIGNAL_WAIT_PROCESS_CAPACITY]{};
    os::kernel::ThreadEntry scheduler_threads[OS_TEST_SIGNAL_WAIT_THREAD_CAPACITY]{};
    os::kernel::SignalProcessState signal_processes[OS_TEST_SIGNAL_WAIT_PROCESS_CAPACITY]{};
    os::kernel::SignalThreadState signal_threads[OS_TEST_SIGNAL_WAIT_THREAD_CAPACITY]{};
    os::kernel::ThreadScheduler scheduler{};
    os::kernel::SignalManager signals{};
    os::kernel::WaitQueue wait_queue{};
    uint64_t process_index = os::kernel::OS_KERNEL_PROCESS_INVALID_INDEX;
    os::kernel::ProcessId process_id{};
    uint64_t thread_index = os::kernel::OS_KERNEL_THREAD_INVALID_INDEX;
    os::kernel::ThreadId thread_id{};
    bool valid =
        scheduler.Initialize(scheduler_processes, OS_TEST_SIGNAL_WAIT_PROCESS_CAPACITY,
                             scheduler_threads, OS_TEST_SIGNAL_WAIT_THREAD_CAPACITY,
                             OS_TEST_SIGNAL_WAIT_THREAD_LIMIT, OS_TEST_SIGNAL_WAIT_QUANTUM) ==
            os::kernel::ThreadSchedulerStatus::Succeeded &&
        signals.Initialize(signal_processes, OS_TEST_SIGNAL_WAIT_PROCESS_CAPACITY, signal_threads,
                           OS_TEST_SIGNAL_WAIT_THREAD_CAPACITY) ==
            os::kernel::SignalManagerStatus::Succeeded &&
        wait_queue.Initialize(os::kernel::WaitQueueId{.value = OS_TEST_SIGNAL_WAIT_QUEUE_ID}) ==
            os::kernel::WaitQueueStatus::Succeeded &&
        scheduler.CreateProcess(OS_TEST_SIGNAL_WAIT_ROOT, process_index, process_id) ==
            os::kernel::ThreadSchedulerStatus::Succeeded &&
        scheduler.CreateThread(process_index, OS_TEST_SIGNAL_WAIT_EMPTY_VALUE,
                               OS_TEST_SIGNAL_WAIT_STACK, OS_TEST_SIGNAL_WAIT_EMPTY_VALUE,
                               OS_TEST_SIGNAL_WAIT_EMPTY_VALUE, thread_index,
                               thread_id) == os::kernel::ThreadSchedulerStatus::Succeeded &&
        signals.RegisterProcess(process_index, OS_TEST_SIGNAL_WAIT_PROCESS_ID,
                                OS_TEST_SIGNAL_WAIT_PROCESS_ID) ==
            os::kernel::SignalManagerStatus::Succeeded &&
        signals.RegisterThread(thread_index, process_index, OS_TEST_SIGNAL_WAIT_THREAD_ID,
                               OS_TEST_SIGNAL_WAIT_EMPTY_VALUE) ==
            os::kernel::SignalManagerStatus::Succeeded;
    os::kernel::ThreadSchedulingDecision decision{};
    valid = valid && scheduler.Start(decision) == os::kernel::ThreadSchedulerStatus::Succeeded &&
            scheduler.BlockCurrentThreadUntil(wait_queue, os::kernel::WaitCondition::TestCondition,
                                              OS_TEST_SIGNAL_WAIT_EMPTY_VALUE,
                                              OS_TEST_SIGNAL_WAIT_DEADLINE, decision) ==
                os::kernel::ThreadSchedulerStatus::Succeeded;
    uint64_t selected_thread_index = os::kernel::OS_KERNEL_SIGNAL_INVALID_INDEX;
    valid = valid &&
            signals.SendToProcess(OS_TEST_SIGNAL_WAIT_PROCESS_ID,
                                  os::abi::OS_ABI_SIGNAL_USER1_NUMBER, selected_thread_index) ==
                os::kernel::SignalManagerStatus::Succeeded &&
            selected_thread_index == thread_index;
    bool signal_won = false;
    valid = valid &&
            scheduler.WakeThread(wait_queue, thread_index, os::kernel::WakeReason::Signal,
                                 signal_won) == os::kernel::ThreadSchedulerStatus::Succeeded &&
            signal_won;
    bool condition_won = true;
    const os::kernel::ThreadSchedulerStatus condition_status = scheduler.WakeThread(
        wait_queue, thread_index, os::kernel::WakeReason::ConditionSatisfied, condition_won);
    uint64_t expired_thread_index = os::kernel::OS_KERNEL_THREAD_INVALID_INDEX;
    os::kernel::WaitCondition expired_condition = os::kernel::WaitCondition::None;
    os::kernel::WaitQueue *expired_queue = nullptr;
    bool expired = true;
    valid = valid && condition_status == os::kernel::ThreadSchedulerStatus::WakeAlreadyResolved &&
            !condition_won &&
            scheduler.ExpireNextDeadline(OS_TEST_SIGNAL_WAIT_DEADLINE, expired_thread_index,
                                         expired_condition, expired_queue,
                                         expired) == os::kernel::ThreadSchedulerStatus::Succeeded &&
            !expired;
    os::kernel::ThreadEntry observed_thread{};
    const os::kernel::ThreadSchedulerStatistics statistics = scheduler.Statistics();
    test_context.Expect(valid &&
                            scheduler.ReadThread(thread_index, observed_thread) ==
                                os::kernel::ThreadSchedulerStatus::Succeeded &&
                            observed_thread.wake_reason == os::kernel::WakeReason::Signal &&
                            statistics.deadline_cancellation_count ==
                                OS_TEST_SIGNAL_WAIT_EXPECTED_CANCELLATION_COUNT &&
                            statistics.deadline_expiration_count ==
                                OS_TEST_SIGNAL_WAIT_EMPTY_VALUE &&
                            scheduler.Validate() == os::kernel::ThreadSchedulerStatus::Succeeded &&
                            signals.Validate() == os::kernel::SignalManagerStatus::Succeeded,
                        OS_TEST_SIGNAL_WAIT_WINNER_MESSAGE);
    return test_context.ExitCode();
}

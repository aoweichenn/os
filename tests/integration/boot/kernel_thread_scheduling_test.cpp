#include "os/kernel/process/thread_scheduler.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_THREAD_INTEGRATION_SUITE_NAME =
    "kernel/thread_scheduling/integration";
constexpr std::string_view OS_TEST_THREAD_INTEGRATION_FAIRNESS =
    "三个 Process 的六个 Ready Thread 必须按相同时间片公平轮转";
constexpr std::string_view OS_TEST_THREAD_INTEGRATION_WAIT_HANDOFF =
    "阻塞、条件变化和唤醒必须跨 run queue 与 WaitQueue 完整交接";
constexpr std::string_view OS_TEST_THREAD_INTEGRATION_LIFECYCLE =
    "六个 Thread 退出后必须形成三个 Zombie Process 并按两级顺序回收";
constexpr std::string_view OS_TEST_THREAD_INTEGRATION_STATISTICS =
    "调度、阻塞、唤醒、退出和回收统计必须与状态迁移逐项守恒";

constexpr uint64_t OS_TEST_THREAD_INTEGRATION_PROCESS_COUNT = 3ULL;
constexpr uint64_t OS_TEST_THREAD_INTEGRATION_THREADS_PER_PROCESS = 2ULL;
constexpr uint64_t OS_TEST_THREAD_INTEGRATION_THREAD_COUNT =
    OS_TEST_THREAD_INTEGRATION_PROCESS_COUNT *
    OS_TEST_THREAD_INTEGRATION_THREADS_PER_PROCESS;
constexpr uint64_t OS_TEST_THREAD_INTEGRATION_QUANTUM_TICKS = 4ULL;
constexpr uint64_t OS_TEST_THREAD_INTEGRATION_TOTAL_TICKS = 48ULL;
constexpr uint64_t OS_TEST_THREAD_INTEGRATION_EXPECTED_TICKS_PER_THREAD =
    OS_TEST_THREAD_INTEGRATION_TOTAL_TICKS /
    OS_TEST_THREAD_INTEGRATION_THREAD_COUNT;
constexpr uint64_t OS_TEST_THREAD_INTEGRATION_EXPECTED_PREEMPTIONS =
    OS_TEST_THREAD_INTEGRATION_TOTAL_TICKS /
    OS_TEST_THREAD_INTEGRATION_QUANTUM_TICKS;
constexpr uint64_t OS_TEST_THREAD_INTEGRATION_INITIAL_DISPATCH_COUNT = 1ULL;
constexpr uint64_t OS_TEST_THREAD_INTEGRATION_BLOCK_HANDOFF_COUNT = 1ULL;
constexpr uint64_t OS_TEST_THREAD_INTEGRATION_TERMINATION_HANDOFF_COUNT =
    OS_TEST_THREAD_INTEGRATION_THREAD_COUNT - 1ULL;
constexpr uint64_t OS_TEST_THREAD_INTEGRATION_EXPECTED_DISPATCHES =
    OS_TEST_THREAD_INTEGRATION_INITIAL_DISPATCH_COUNT +
    OS_TEST_THREAD_INTEGRATION_EXPECTED_PREEMPTIONS +
    OS_TEST_THREAD_INTEGRATION_BLOCK_HANDOFF_COUNT +
    OS_TEST_THREAD_INTEGRATION_TERMINATION_HANDOFF_COUNT;
constexpr uint64_t OS_TEST_THREAD_INTEGRATION_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_THREAD_INTEGRATION_FIRST_INDEX = 0ULL;
constexpr uint64_t OS_TEST_THREAD_INTEGRATION_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_TEST_THREAD_INTEGRATION_ADDRESS_SPACE_BASE =
    0x00100000ULL;
constexpr uint64_t OS_TEST_THREAD_INTEGRATION_ADDRESS_SPACE_STRIDE =
    0x00001000ULL;
constexpr uint64_t OS_TEST_THREAD_INTEGRATION_USER_STACK_BASE = 0x70000000ULL;
constexpr uint64_t OS_TEST_THREAD_INTEGRATION_USER_STACK_STRIDE =
    0x00010000ULL;
constexpr uint64_t OS_TEST_THREAD_INTEGRATION_WAIT_QUEUE_ID = 1ULL;

}

int main() {
    os::test::TestContext test_context{
        OS_TEST_THREAD_INTEGRATION_SUITE_NAME};
    os::kernel::ProcessEntry
        processes[OS_TEST_THREAD_INTEGRATION_PROCESS_COUNT]{};
    os::kernel::ThreadEntry
        threads[OS_TEST_THREAD_INTEGRATION_THREAD_COUNT]{};
    os::kernel::ThreadScheduler scheduler{};
    os::kernel::WaitQueue wait_queue{};

    bool setup_succeeded =
        scheduler.Initialize(
            processes, OS_TEST_THREAD_INTEGRATION_PROCESS_COUNT, threads,
            OS_TEST_THREAD_INTEGRATION_THREAD_COUNT,
            OS_TEST_THREAD_INTEGRATION_THREADS_PER_PROCESS,
            OS_TEST_THREAD_INTEGRATION_QUANTUM_TICKS) ==
            os::kernel::ThreadSchedulerStatus::Succeeded &&
        wait_queue.Initialize(os::kernel::WaitQueueId{
            .value = OS_TEST_THREAD_INTEGRATION_WAIT_QUEUE_ID}) ==
            os::kernel::WaitQueueStatus::Succeeded;
    uint64_t thread_ordinal = OS_TEST_THREAD_INTEGRATION_FIRST_INDEX;
    for (uint64_t process_ordinal = OS_TEST_THREAD_INTEGRATION_FIRST_INDEX;
         process_ordinal < OS_TEST_THREAD_INTEGRATION_PROCESS_COUNT;
         ++process_ordinal) {
        uint64_t process_index = os::kernel::OS_KERNEL_PROCESS_INVALID_INDEX;
        os::kernel::ProcessId process_id{};
        setup_succeeded =
            setup_succeeded &&
            scheduler.CreateProcess(
                OS_TEST_THREAD_INTEGRATION_ADDRESS_SPACE_BASE +
                    process_ordinal *
                        OS_TEST_THREAD_INTEGRATION_ADDRESS_SPACE_STRIDE,
                process_index, process_id) ==
                os::kernel::ThreadSchedulerStatus::Succeeded &&
            process_index == process_ordinal &&
            process_id.value ==
                process_ordinal +
                    OS_TEST_THREAD_INTEGRATION_COUNTER_INCREMENT;
        for (uint64_t process_thread_ordinal =
                 OS_TEST_THREAD_INTEGRATION_FIRST_INDEX;
             process_thread_ordinal <
             OS_TEST_THREAD_INTEGRATION_THREADS_PER_PROCESS;
             ++process_thread_ordinal) {
            uint64_t thread_index =
                os::kernel::OS_KERNEL_THREAD_INVALID_INDEX;
            os::kernel::ThreadId thread_id{};
            setup_succeeded =
                setup_succeeded &&
                scheduler.CreateThread(
                    process_index, thread_ordinal,
                    OS_TEST_THREAD_INTEGRATION_USER_STACK_BASE +
                        thread_ordinal *
                            OS_TEST_THREAD_INTEGRATION_USER_STACK_STRIDE,
                    OS_TEST_THREAD_INTEGRATION_EMPTY_VALUE,
                    OS_TEST_THREAD_INTEGRATION_EMPTY_VALUE, thread_index,
                    thread_id) ==
                    os::kernel::ThreadSchedulerStatus::Succeeded &&
                thread_index == thread_ordinal &&
                thread_id.value ==
                    thread_ordinal +
                        OS_TEST_THREAD_INTEGRATION_COUNTER_INCREMENT;
            ++thread_ordinal;
        }
    }

    os::kernel::ThreadSchedulingDecision decision{};
    setup_succeeded =
        setup_succeeded &&
        scheduler.Start(decision) ==
            os::kernel::ThreadSchedulerStatus::Succeeded;
    for (uint64_t tick_index = OS_TEST_THREAD_INTEGRATION_FIRST_INDEX;
         tick_index < OS_TEST_THREAD_INTEGRATION_TOTAL_TICKS; ++tick_index) {
        setup_succeeded =
            setup_succeeded &&
            scheduler.HandleTimerTick(decision) ==
                os::kernel::ThreadSchedulerStatus::Succeeded &&
            scheduler.Validate() ==
                os::kernel::ThreadSchedulerStatus::Succeeded;
    }

    bool fair = setup_succeeded;
    for (uint64_t thread_index = OS_TEST_THREAD_INTEGRATION_FIRST_INDEX;
         thread_index < OS_TEST_THREAD_INTEGRATION_THREAD_COUNT;
         ++thread_index) {
        os::kernel::ThreadEntry thread{};
        fair =
            fair &&
            scheduler.ReadThread(thread_index, thread) ==
                os::kernel::ThreadSchedulerStatus::Succeeded &&
            thread.run_tick_count ==
                OS_TEST_THREAD_INTEGRATION_EXPECTED_TICKS_PER_THREAD;
    }
    const os::kernel::ThreadSchedulerStatistics running_statistics =
        scheduler.Statistics();
    test_context.Expect(
        fair &&
            running_statistics.preemption_count ==
                OS_TEST_THREAD_INTEGRATION_EXPECTED_PREEMPTIONS &&
            running_statistics.running_thread_count ==
                OS_TEST_THREAD_INTEGRATION_COUNTER_INCREMENT &&
            running_statistics.ready_thread_count ==
                OS_TEST_THREAD_INTEGRATION_THREAD_COUNT -
                    OS_TEST_THREAD_INTEGRATION_COUNTER_INCREMENT,
        OS_TEST_THREAD_INTEGRATION_FAIRNESS);

    const uint64_t blocked_thread_index =
        scheduler.CurrentThreadIndex();
    bool wait_handoff_succeeded =
        scheduler.BlockCurrentThread(
            wait_queue, os::kernel::WaitCondition::PipeReadable,
            decision) == os::kernel::ThreadSchedulerStatus::Succeeded &&
        decision.switched &&
        decision.previous_thread_index == blocked_thread_index;
    uint64_t woken_thread_index =
        os::kernel::OS_KERNEL_THREAD_INVALID_INDEX;
    bool wake_won = false;
    wait_handoff_succeeded =
        wait_handoff_succeeded &&
        scheduler.WakeOne(
            wait_queue, os::kernel::WakeReason::ConditionSatisfied,
            woken_thread_index, wake_won) ==
            os::kernel::ThreadSchedulerStatus::Succeeded &&
        wake_won && woken_thread_index == blocked_thread_index;
    os::kernel::ThreadEntry woken_thread{};
    wait_handoff_succeeded =
        wait_handoff_succeeded &&
        scheduler.ReadThread(blocked_thread_index, woken_thread) ==
            os::kernel::ThreadSchedulerStatus::Succeeded &&
        woken_thread.state == os::kernel::ThreadState::Ready &&
        woken_thread.wake_reason ==
            os::kernel::WakeReason::ConditionSatisfied &&
        scheduler.ValidateWaitQueue(wait_queue) ==
            os::kernel::ThreadSchedulerStatus::Succeeded &&
        scheduler.Validate() ==
            os::kernel::ThreadSchedulerStatus::Succeeded;
    test_context.Expect(wait_handoff_succeeded,
                        OS_TEST_THREAD_INTEGRATION_WAIT_HANDOFF);

    bool termination_succeeded = true;
    for (uint64_t exit_ordinal = OS_TEST_THREAD_INTEGRATION_FIRST_INDEX;
         exit_ordinal < OS_TEST_THREAD_INTEGRATION_THREAD_COUNT;
         ++exit_ordinal) {
        termination_succeeded =
            termination_succeeded &&
            scheduler.TerminateCurrentThread(decision) ==
                os::kernel::ThreadSchedulerStatus::Succeeded &&
            scheduler.Validate() ==
                os::kernel::ThreadSchedulerStatus::Succeeded;
    }
    bool all_processes_zombie =
        termination_succeeded && decision.completed &&
        !scheduler.IsActive();
    for (uint64_t process_index = OS_TEST_THREAD_INTEGRATION_FIRST_INDEX;
         process_index < OS_TEST_THREAD_INTEGRATION_PROCESS_COUNT;
         ++process_index) {
        os::kernel::ProcessEntry process{};
        all_processes_zombie =
            all_processes_zombie &&
            scheduler.ReadProcess(process_index, process) ==
                os::kernel::ThreadSchedulerStatus::Succeeded &&
            process.state == os::kernel::ProcessState::Zombie &&
            process.live_thread_count ==
                OS_TEST_THREAD_INTEGRATION_EMPTY_VALUE &&
            process.exited_thread_count ==
                OS_TEST_THREAD_INTEGRATION_THREADS_PER_PROCESS;
    }
    bool reaped = all_processes_zombie;
    for (uint64_t thread_index = OS_TEST_THREAD_INTEGRATION_FIRST_INDEX;
         thread_index < OS_TEST_THREAD_INTEGRATION_THREAD_COUNT;
         ++thread_index) {
        reaped =
            reaped &&
            scheduler.ReapExitedThread(thread_index) ==
                os::kernel::ThreadSchedulerStatus::Succeeded;
    }
    for (uint64_t process_index = OS_TEST_THREAD_INTEGRATION_FIRST_INDEX;
         process_index < OS_TEST_THREAD_INTEGRATION_PROCESS_COUNT;
         ++process_index) {
        reaped =
            reaped &&
            scheduler.ReapZombieProcess(process_index) ==
                os::kernel::ThreadSchedulerStatus::Succeeded;
    }
    test_context.Expect(
        reaped &&
            scheduler.Validate() ==
                os::kernel::ThreadSchedulerStatus::Succeeded,
        OS_TEST_THREAD_INTEGRATION_LIFECYCLE);

    const os::kernel::ThreadSchedulerStatistics final_statistics =
        scheduler.Statistics();
    const os::kernel::WaitQueueStatistics wait_statistics =
        wait_queue.Statistics();
    test_context.Expect(
        final_statistics.owned_process_count ==
                OS_TEST_THREAD_INTEGRATION_EMPTY_VALUE &&
            final_statistics.owned_thread_count ==
                OS_TEST_THREAD_INTEGRATION_EMPTY_VALUE &&
            final_statistics.created_process_count ==
                OS_TEST_THREAD_INTEGRATION_PROCESS_COUNT &&
            final_statistics.reaped_process_count ==
                OS_TEST_THREAD_INTEGRATION_PROCESS_COUNT &&
            final_statistics.created_thread_count ==
                OS_TEST_THREAD_INTEGRATION_THREAD_COUNT &&
            final_statistics.reaped_thread_count ==
                OS_TEST_THREAD_INTEGRATION_THREAD_COUNT &&
            final_statistics.zombie_transition_count ==
                OS_TEST_THREAD_INTEGRATION_PROCESS_COUNT &&
            final_statistics.timer_tick_count ==
                OS_TEST_THREAD_INTEGRATION_TOTAL_TICKS &&
            final_statistics.preemption_count ==
                OS_TEST_THREAD_INTEGRATION_EXPECTED_PREEMPTIONS &&
            final_statistics.dispatch_count ==
                OS_TEST_THREAD_INTEGRATION_EXPECTED_DISPATCHES &&
            final_statistics.block_count ==
                OS_TEST_THREAD_INTEGRATION_COUNTER_INCREMENT &&
            final_statistics.wake_count ==
                OS_TEST_THREAD_INTEGRATION_COUNTER_INCREMENT &&
            wait_statistics.enqueue_count ==
                OS_TEST_THREAD_INTEGRATION_COUNTER_INCREMENT &&
            wait_statistics.wake_count ==
                OS_TEST_THREAD_INTEGRATION_COUNTER_INCREMENT &&
            wait_statistics.waiting_thread_count ==
                OS_TEST_THREAD_INTEGRATION_EMPTY_VALUE,
        OS_TEST_THREAD_INTEGRATION_STATISTICS);
    return test_context.ExitCode();
}

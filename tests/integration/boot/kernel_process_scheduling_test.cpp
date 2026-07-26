#include "os/kernel/process/process_scheduler.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_PROCESS_INTEGRATION_SUITE_NAME =
    "kernel/process_scheduling/integration";
constexpr std::string_view OS_TEST_PROCESS_INTEGRATION_FAIRNESS =
    "三个 Ready 进程必须按固定时间片获得相同运行 tick";
constexpr std::string_view OS_TEST_PROCESS_INTEGRATION_LIFECYCLE =
    "终止交接必须让全部进程到达 Terminated";
constexpr std::string_view OS_TEST_PROCESS_INTEGRATION_WAIT_HANDOFF =
    "管道等待必须先阻塞再由条件变化唤醒且不丢失交接";
constexpr uint64_t OS_TEST_PROCESS_INTEGRATION_PROCESS_COUNT = 3ULL;
constexpr uint64_t OS_TEST_PROCESS_INTEGRATION_QUANTUM_TICKS = 4ULL;
constexpr uint64_t OS_TEST_PROCESS_INTEGRATION_TOTAL_TICKS = 24ULL;
constexpr uint64_t OS_TEST_PROCESS_INTEGRATION_EXPECTED_TICKS_PER_PROCESS =
    OS_TEST_PROCESS_INTEGRATION_TOTAL_TICKS / OS_TEST_PROCESS_INTEGRATION_PROCESS_COUNT;
constexpr uint64_t OS_TEST_PROCESS_INTEGRATION_EXPECTED_PREEMPTIONS =
    OS_TEST_PROCESS_INTEGRATION_TOTAL_TICKS / OS_TEST_PROCESS_INTEGRATION_QUANTUM_TICKS;
constexpr uint64_t OS_TEST_PROCESS_INTEGRATION_INITIAL_DISPATCH_COUNT = 1ULL;
constexpr uint64_t OS_TEST_PROCESS_INTEGRATION_WAIT_HANDOFF_DISPATCH_COUNT = 1ULL;
constexpr uint64_t OS_TEST_PROCESS_INTEGRATION_EXPECTED_BLOCK_COUNT = 1ULL;
constexpr uint64_t OS_TEST_PROCESS_INTEGRATION_EXPECTED_WAKEUP_COUNT = 1ULL;
constexpr uint64_t OS_TEST_PROCESS_INTEGRATION_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_PROCESS_INTEGRATION_TERMINATION_HANDOFF_COUNT =
    OS_TEST_PROCESS_INTEGRATION_PROCESS_COUNT - 1ULL;
constexpr uint64_t OS_TEST_PROCESS_INTEGRATION_EXPECTED_DISPATCHES =
    OS_TEST_PROCESS_INTEGRATION_INITIAL_DISPATCH_COUNT +
    OS_TEST_PROCESS_INTEGRATION_EXPECTED_PREEMPTIONS +
    OS_TEST_PROCESS_INTEGRATION_TERMINATION_HANDOFF_COUNT;

}

int main() {
    os::test::TestContext test_context{OS_TEST_PROCESS_INTEGRATION_SUITE_NAME};
    os::kernel::ProcessScheduler scheduler{};
    bool setup_succeeded = scheduler.Initialize(OS_TEST_PROCESS_INTEGRATION_QUANTUM_TICKS) ==
                           os::kernel::ProcessSchedulerStatus::Succeeded;
    for (uint64_t process_index = OS_TEST_PROCESS_INTEGRATION_EMPTY_VALUE;
         process_index < OS_TEST_PROCESS_INTEGRATION_PROCESS_COUNT; ++process_index) {
        uint64_t created_process_index = os::kernel::OS_KERNEL_PROCESS_INVALID_INDEX;
        uint64_t process_id = OS_TEST_PROCESS_INTEGRATION_EMPTY_VALUE;
        setup_succeeded =
            setup_succeeded && scheduler.CreateProcess(created_process_index, process_id) ==
                                   os::kernel::ProcessSchedulerStatus::Succeeded;
    }

    os::kernel::ProcessSchedulingDecision decision{};
    setup_succeeded = setup_succeeded &&
                      scheduler.Start(decision) == os::kernel::ProcessSchedulerStatus::Succeeded;
    for (uint64_t tick_index = OS_TEST_PROCESS_INTEGRATION_EMPTY_VALUE;
         tick_index < OS_TEST_PROCESS_INTEGRATION_TOTAL_TICKS; ++tick_index) {
        setup_succeeded = setup_succeeded && scheduler.HandleTimerTick(decision) ==
                                                 os::kernel::ProcessSchedulerStatus::Succeeded;
    }

    bool fair = setup_succeeded;
    for (uint64_t process_index = OS_TEST_PROCESS_INTEGRATION_EMPTY_VALUE;
         process_index < OS_TEST_PROCESS_INTEGRATION_PROCESS_COUNT; ++process_index) {
        os::kernel::ProcessSchedulerEntry entry{};
        fair = fair &&
               scheduler.ReadEntry(process_index, entry) ==
                   os::kernel::ProcessSchedulerStatus::Succeeded &&
               entry.run_tick_count == OS_TEST_PROCESS_INTEGRATION_EXPECTED_TICKS_PER_PROCESS;
    }
    const os::kernel::ProcessSchedulerStatistics running_statistics = scheduler.Statistics();
    test_context.Expect(fair && running_statistics.preemption_count ==
                                    OS_TEST_PROCESS_INTEGRATION_EXPECTED_PREEMPTIONS,
                        OS_TEST_PROCESS_INTEGRATION_FAIRNESS);

    const uint64_t waiting_process_index = scheduler.CurrentProcessIndex();
    bool wait_handoff_succeeded =
        scheduler.BlockCurrentProcess(os::kernel::ProcessWaitReason::PipeWritable, decision) ==
            os::kernel::ProcessSchedulerStatus::Succeeded &&
        decision.switched && decision.previous_process_index == waiting_process_index;
    uint64_t woken_process_count = OS_TEST_PROCESS_INTEGRATION_EMPTY_VALUE;
    wait_handoff_succeeded =
        wait_handoff_succeeded &&
        scheduler.WakeBlockedProcesses(os::kernel::ProcessWaitReason::PipeWritable,
                                       OS_TEST_PROCESS_INTEGRATION_EXPECTED_WAKEUP_COUNT,
                                       woken_process_count) ==
            os::kernel::ProcessSchedulerStatus::Succeeded &&
        woken_process_count == OS_TEST_PROCESS_INTEGRATION_EXPECTED_WAKEUP_COUNT;
    os::kernel::ProcessSchedulerEntry waiting_entry{};
    wait_handoff_succeeded = wait_handoff_succeeded &&
                             scheduler.ReadEntry(waiting_process_index, waiting_entry) ==
                                 os::kernel::ProcessSchedulerStatus::Succeeded &&
                             waiting_entry.state == os::kernel::ProcessState::Ready &&
                             waiting_entry.wait_reason == os::kernel::ProcessWaitReason::None;
    test_context.Expect(wait_handoff_succeeded, OS_TEST_PROCESS_INTEGRATION_WAIT_HANDOFF);

    bool termination_succeeded = true;
    for (uint64_t process_index = OS_TEST_PROCESS_INTEGRATION_EMPTY_VALUE;
         process_index < OS_TEST_PROCESS_INTEGRATION_PROCESS_COUNT; ++process_index) {
        termination_succeeded =
            termination_succeeded && scheduler.TerminateCurrentProcess(decision) ==
                                         os::kernel::ProcessSchedulerStatus::Succeeded;
    }
    bool all_terminated = termination_succeeded && decision.completed;
    for (uint64_t process_index = OS_TEST_PROCESS_INTEGRATION_EMPTY_VALUE;
         process_index < OS_TEST_PROCESS_INTEGRATION_PROCESS_COUNT; ++process_index) {
        os::kernel::ProcessSchedulerEntry entry{};
        all_terminated = all_terminated &&
                         scheduler.ReadEntry(process_index, entry) ==
                             os::kernel::ProcessSchedulerStatus::Succeeded &&
                         entry.state == os::kernel::ProcessState::Terminated;
    }
    const os::kernel::ProcessSchedulerStatistics final_statistics = scheduler.Statistics();
    test_context.Expect(
        all_terminated &&
            final_statistics.terminated_process_count ==
                OS_TEST_PROCESS_INTEGRATION_PROCESS_COUNT &&
            final_statistics.dispatch_count ==
                OS_TEST_PROCESS_INTEGRATION_EXPECTED_DISPATCHES +
                    OS_TEST_PROCESS_INTEGRATION_WAIT_HANDOFF_DISPATCH_COUNT &&
            final_statistics.block_count == OS_TEST_PROCESS_INTEGRATION_EXPECTED_BLOCK_COUNT &&
            final_statistics.wakeup_count == OS_TEST_PROCESS_INTEGRATION_EXPECTED_WAKEUP_COUNT,
        OS_TEST_PROCESS_INTEGRATION_LIFECYCLE);
    return test_context.ExitCode();
}

#include "os/kernel/process_scheduler.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_PROCESS_INTEGRATION_SUITE_NAME =
    "kernel/process_scheduling/integration";
constexpr std::string_view OS_TEST_PROCESS_INTEGRATION_FAIRNESS =
    "三个 Ready 进程必须按固定时间片获得相同运行 tick";
constexpr std::string_view OS_TEST_PROCESS_INTEGRATION_LIFECYCLE =
    "终止交接必须让全部进程到达 Terminated";
constexpr uint64_t OS_TEST_PROCESS_INTEGRATION_PROCESS_COUNT = 3ULL;
constexpr uint64_t OS_TEST_PROCESS_INTEGRATION_QUANTUM_TICKS = 4ULL;
constexpr uint64_t OS_TEST_PROCESS_INTEGRATION_TOTAL_TICKS = 24ULL;
constexpr uint64_t OS_TEST_PROCESS_INTEGRATION_EXPECTED_TICKS_PER_PROCESS =
    OS_TEST_PROCESS_INTEGRATION_TOTAL_TICKS / OS_TEST_PROCESS_INTEGRATION_PROCESS_COUNT;
constexpr uint64_t OS_TEST_PROCESS_INTEGRATION_EXPECTED_PREEMPTIONS =
    OS_TEST_PROCESS_INTEGRATION_TOTAL_TICKS / OS_TEST_PROCESS_INTEGRATION_QUANTUM_TICKS;
constexpr uint64_t OS_TEST_PROCESS_INTEGRATION_INITIAL_DISPATCH_COUNT = 1ULL;
constexpr uint64_t OS_TEST_PROCESS_INTEGRATION_TERMINATION_HANDOFF_COUNT =
    OS_TEST_PROCESS_INTEGRATION_PROCESS_COUNT - 1ULL;
constexpr uint64_t OS_TEST_PROCESS_INTEGRATION_EXPECTED_DISPATCHES =
    OS_TEST_PROCESS_INTEGRATION_INITIAL_DISPATCH_COUNT +
    OS_TEST_PROCESS_INTEGRATION_EXPECTED_PREEMPTIONS +
    OS_TEST_PROCESS_INTEGRATION_TERMINATION_HANDOFF_COUNT;

}

int main() {
    os::test::TestContext testContext{OS_TEST_PROCESS_INTEGRATION_SUITE_NAME};
    os::kernel::ProcessScheduler scheduler{};
    bool setupSucceeded =
        scheduler.Initialize(OS_TEST_PROCESS_INTEGRATION_QUANTUM_TICKS) ==
        os::kernel::ProcessSchedulerStatus::Succeeded;
    for (uint64_t processIndex = 0ULL;
         processIndex < OS_TEST_PROCESS_INTEGRATION_PROCESS_COUNT; ++processIndex) {
        uint64_t createdProcessIndex = os::kernel::OS_KERNEL_PROCESS_INVALID_INDEX;
        uint64_t processId = 0ULL;
        setupSucceeded =
            setupSucceeded &&
            scheduler.CreateProcess(createdProcessIndex, processId) ==
                os::kernel::ProcessSchedulerStatus::Succeeded;
    }

    os::kernel::ProcessSchedulingDecision decision{};
    setupSucceeded =
        setupSucceeded &&
        scheduler.Start(decision) == os::kernel::ProcessSchedulerStatus::Succeeded;
    for (uint64_t tickIndex = 0ULL; tickIndex < OS_TEST_PROCESS_INTEGRATION_TOTAL_TICKS;
         ++tickIndex) {
        setupSucceeded =
            setupSucceeded &&
            scheduler.HandleTimerTick(decision) ==
                os::kernel::ProcessSchedulerStatus::Succeeded;
    }

    bool fair = setupSucceeded;
    for (uint64_t processIndex = 0ULL;
         processIndex < OS_TEST_PROCESS_INTEGRATION_PROCESS_COUNT; ++processIndex) {
        os::kernel::ProcessSchedulerEntry entry{};
        fair = fair &&
               scheduler.ReadEntry(processIndex, entry) ==
                   os::kernel::ProcessSchedulerStatus::Succeeded &&
               entry.runTickCount == OS_TEST_PROCESS_INTEGRATION_EXPECTED_TICKS_PER_PROCESS;
    }
    const os::kernel::ProcessSchedulerStatistics runningStatistics = scheduler.Statistics();
    testContext.Expect(
        fair &&
            runningStatistics.preemptionCount ==
                OS_TEST_PROCESS_INTEGRATION_EXPECTED_PREEMPTIONS,
        OS_TEST_PROCESS_INTEGRATION_FAIRNESS);

    bool terminationSucceeded = true;
    for (uint64_t processIndex = 0ULL;
         processIndex < OS_TEST_PROCESS_INTEGRATION_PROCESS_COUNT; ++processIndex) {
        terminationSucceeded =
            terminationSucceeded &&
            scheduler.TerminateCurrentProcess(decision) ==
                os::kernel::ProcessSchedulerStatus::Succeeded;
    }
    bool allTerminated = terminationSucceeded && decision.completed;
    for (uint64_t processIndex = 0ULL;
         processIndex < OS_TEST_PROCESS_INTEGRATION_PROCESS_COUNT; ++processIndex) {
        os::kernel::ProcessSchedulerEntry entry{};
        allTerminated =
            allTerminated &&
            scheduler.ReadEntry(processIndex, entry) ==
                os::kernel::ProcessSchedulerStatus::Succeeded &&
            entry.state == os::kernel::ProcessState::Terminated;
    }
    const os::kernel::ProcessSchedulerStatistics finalStatistics = scheduler.Statistics();
    testContext.Expect(
        allTerminated &&
            finalStatistics.terminatedProcessCount ==
                OS_TEST_PROCESS_INTEGRATION_PROCESS_COUNT &&
            finalStatistics.dispatchCount == OS_TEST_PROCESS_INTEGRATION_EXPECTED_DISPATCHES,
        OS_TEST_PROCESS_INTEGRATION_LIFECYCLE);
    return testContext.ExitCode();
}

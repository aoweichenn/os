#include "os/kernel/process_scheduler.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_PROCESS_RANDOMIZED_SUITE_NAME =
    "kernel/process_scheduler/randomized";
constexpr std::string_view OS_TEST_PROCESS_RANDOMIZED_INVARIANTS =
    "固定种子随机调度必须始终保持单一 Running 进程和统计守恒";
constexpr uint64_t OS_TEST_PROCESS_RANDOMIZED_SEED = 0xD1CE5CED5A17E123ULL;
constexpr uint64_t OS_TEST_PROCESS_RANDOMIZED_SCENARIO_COUNT = 4096ULL;
constexpr uint64_t OS_TEST_PROCESS_RANDOMIZED_MULTIPLIER = 6364136223846793005ULL;
constexpr uint64_t OS_TEST_PROCESS_RANDOMIZED_INCREMENT = 1442695040888963407ULL;
constexpr uint64_t OS_TEST_PROCESS_RANDOMIZED_MAXIMUM_QUANTUM_TICKS = 8ULL;
constexpr uint64_t OS_TEST_PROCESS_RANDOMIZED_MAXIMUM_TIMER_TICKS = 64ULL;
constexpr uint64_t OS_TEST_PROCESS_RANDOMIZED_COUNTER_INCREMENT = 1ULL;

[[nodiscard]] uint64_t NextRandom(uint64_t &state) noexcept {
    state = state * OS_TEST_PROCESS_RANDOMIZED_MULTIPLIER +
            OS_TEST_PROCESS_RANDOMIZED_INCREMENT;
    return state;
}

}

int main() {
    os::test::TestContext testContext{OS_TEST_PROCESS_RANDOMIZED_SUITE_NAME};
    uint64_t randomState = OS_TEST_PROCESS_RANDOMIZED_SEED;
    bool allInvariantsHeld = true;

    for (uint64_t scenarioIndex = 0ULL;
         scenarioIndex < OS_TEST_PROCESS_RANDOMIZED_SCENARIO_COUNT; ++scenarioIndex) {
        const uint64_t quantumTicks =
            NextRandom(randomState) % OS_TEST_PROCESS_RANDOMIZED_MAXIMUM_QUANTUM_TICKS +
            OS_TEST_PROCESS_RANDOMIZED_COUNTER_INCREMENT;
        const uint64_t processCount =
            NextRandom(randomState) % os::kernel::OS_KERNEL_PROCESS_CAPACITY +
            OS_TEST_PROCESS_RANDOMIZED_COUNTER_INCREMENT;
        const uint64_t timerTickCount =
            NextRandom(randomState) % OS_TEST_PROCESS_RANDOMIZED_MAXIMUM_TIMER_TICKS +
            OS_TEST_PROCESS_RANDOMIZED_COUNTER_INCREMENT;

        os::kernel::ProcessScheduler scheduler{};
        allInvariantsHeld =
            allInvariantsHeld &&
            scheduler.Initialize(quantumTicks) ==
                os::kernel::ProcessSchedulerStatus::Succeeded;
        for (uint64_t processIndex = 0ULL; processIndex < processCount; ++processIndex) {
            uint64_t createdProcessIndex = os::kernel::OS_KERNEL_PROCESS_INVALID_INDEX;
            uint64_t processId = 0ULL;
            allInvariantsHeld =
                allInvariantsHeld &&
                scheduler.CreateProcess(createdProcessIndex, processId) ==
                    os::kernel::ProcessSchedulerStatus::Succeeded;
        }

        os::kernel::ProcessSchedulingDecision decision{};
        allInvariantsHeld =
            allInvariantsHeld &&
            scheduler.Start(decision) == os::kernel::ProcessSchedulerStatus::Succeeded;
        for (uint64_t tickIndex = 0ULL; tickIndex < timerTickCount; ++tickIndex) {
            allInvariantsHeld =
                allInvariantsHeld &&
                scheduler.HandleTimerTick(decision) ==
                    os::kernel::ProcessSchedulerStatus::Succeeded;
            uint64_t runningProcessCount = 0ULL;
            uint64_t accumulatedRunTickCount = 0ULL;
            for (uint64_t processIndex = 0ULL; processIndex < processCount; ++processIndex) {
                os::kernel::ProcessSchedulerEntry entry{};
                allInvariantsHeld =
                    allInvariantsHeld &&
                    scheduler.ReadEntry(processIndex, entry) ==
                        os::kernel::ProcessSchedulerStatus::Succeeded;
                if (entry.state == os::kernel::ProcessState::Running) {
                    runningProcessCount += OS_TEST_PROCESS_RANDOMIZED_COUNTER_INCREMENT;
                }
                accumulatedRunTickCount += entry.runTickCount;
            }
            allInvariantsHeld =
                allInvariantsHeld &&
                runningProcessCount == OS_TEST_PROCESS_RANDOMIZED_COUNTER_INCREMENT &&
                accumulatedRunTickCount ==
                    tickIndex + OS_TEST_PROCESS_RANDOMIZED_COUNTER_INCREMENT;
        }

        for (uint64_t processIndex = 0ULL; processIndex < processCount; ++processIndex) {
            allInvariantsHeld =
                allInvariantsHeld &&
                scheduler.TerminateCurrentProcess(decision) ==
                    os::kernel::ProcessSchedulerStatus::Succeeded;
        }
        const os::kernel::ProcessSchedulerStatistics statistics = scheduler.Statistics();
        allInvariantsHeld =
            allInvariantsHeld && decision.completed && !scheduler.IsActive() &&
            statistics.createdProcessCount == processCount &&
            statistics.terminatedProcessCount == processCount &&
            statistics.timerTickCount == timerTickCount;
    }

    testContext.Expect(allInvariantsHeld, OS_TEST_PROCESS_RANDOMIZED_INVARIANTS);
    return testContext.ExitCode();
}

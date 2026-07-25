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
constexpr uint64_t OS_TEST_PROCESS_RANDOMIZED_EMPTY_VALUE = 0ULL;

[[nodiscard]] uint64_t NextRandom(uint64_t &state) noexcept {
    state = state * OS_TEST_PROCESS_RANDOMIZED_MULTIPLIER + OS_TEST_PROCESS_RANDOMIZED_INCREMENT;
    return state;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_PROCESS_RANDOMIZED_SUITE_NAME};
    uint64_t random_state = OS_TEST_PROCESS_RANDOMIZED_SEED;
    bool all_invariants_held = true;

    for (uint64_t scenario_index = OS_TEST_PROCESS_RANDOMIZED_EMPTY_VALUE;
         scenario_index < OS_TEST_PROCESS_RANDOMIZED_SCENARIO_COUNT; ++scenario_index) {
        const uint64_t quantum_ticks =
            NextRandom(random_state) % OS_TEST_PROCESS_RANDOMIZED_MAXIMUM_QUANTUM_TICKS +
            OS_TEST_PROCESS_RANDOMIZED_COUNTER_INCREMENT;
        const uint64_t process_count =
            NextRandom(random_state) % os::kernel::OS_KERNEL_PROCESS_CAPACITY +
            OS_TEST_PROCESS_RANDOMIZED_COUNTER_INCREMENT;
        const uint64_t timer_tick_count =
            NextRandom(random_state) % OS_TEST_PROCESS_RANDOMIZED_MAXIMUM_TIMER_TICKS +
            OS_TEST_PROCESS_RANDOMIZED_COUNTER_INCREMENT;

        os::kernel::ProcessScheduler scheduler{};
        all_invariants_held =
            all_invariants_held &&
            scheduler.Initialize(quantum_ticks) == os::kernel::ProcessSchedulerStatus::Succeeded;
        for (uint64_t process_index = OS_TEST_PROCESS_RANDOMIZED_EMPTY_VALUE;
             process_index < process_count; ++process_index) {
            uint64_t created_process_index = os::kernel::OS_KERNEL_PROCESS_INVALID_INDEX;
            uint64_t process_id = OS_TEST_PROCESS_RANDOMIZED_EMPTY_VALUE;
            all_invariants_held =
                all_invariants_held && scheduler.CreateProcess(created_process_index, process_id) ==
                                           os::kernel::ProcessSchedulerStatus::Succeeded;
        }

        os::kernel::ProcessSchedulingDecision decision{};
        all_invariants_held =
            all_invariants_held &&
            scheduler.Start(decision) == os::kernel::ProcessSchedulerStatus::Succeeded;
        for (uint64_t tick_index = OS_TEST_PROCESS_RANDOMIZED_EMPTY_VALUE;
             tick_index < timer_tick_count; ++tick_index) {
            all_invariants_held =
                all_invariants_held && scheduler.HandleTimerTick(decision) ==
                                           os::kernel::ProcessSchedulerStatus::Succeeded;
            uint64_t running_process_count = OS_TEST_PROCESS_RANDOMIZED_EMPTY_VALUE;
            uint64_t accumulated_run_tick_count = OS_TEST_PROCESS_RANDOMIZED_EMPTY_VALUE;
            for (uint64_t process_index = OS_TEST_PROCESS_RANDOMIZED_EMPTY_VALUE;
                 process_index < process_count; ++process_index) {
                os::kernel::ProcessSchedulerEntry entry{};
                all_invariants_held =
                    all_invariants_held && scheduler.ReadEntry(process_index, entry) ==
                                               os::kernel::ProcessSchedulerStatus::Succeeded;
                if (entry.state == os::kernel::ProcessState::Running) {
                    running_process_count += OS_TEST_PROCESS_RANDOMIZED_COUNTER_INCREMENT;
                }
                accumulated_run_tick_count += entry.run_tick_count;
            }
            all_invariants_held =
                all_invariants_held &&
                running_process_count == OS_TEST_PROCESS_RANDOMIZED_COUNTER_INCREMENT &&
                accumulated_run_tick_count ==
                    tick_index + OS_TEST_PROCESS_RANDOMIZED_COUNTER_INCREMENT;
        }

        if (process_count > OS_TEST_PROCESS_RANDOMIZED_COUNTER_INCREMENT) {
            const os::kernel::ProcessWaitReason wait_reason =
                (NextRandom(random_state) & OS_TEST_PROCESS_RANDOMIZED_COUNTER_INCREMENT) ==
                        OS_TEST_PROCESS_RANDOMIZED_EMPTY_VALUE
                    ? os::kernel::ProcessWaitReason::PipeReadable
                    : os::kernel::ProcessWaitReason::PipeWritable;
            const uint64_t blocked_process_index = scheduler.CurrentProcessIndex();
            all_invariants_held =
                all_invariants_held && scheduler.BlockCurrentProcess(wait_reason, decision) ==
                                           os::kernel::ProcessSchedulerStatus::Succeeded;
            os::kernel::ProcessSchedulerEntry blocked_entry{};
            all_invariants_held = all_invariants_held &&
                                  scheduler.ReadEntry(blocked_process_index, blocked_entry) ==
                                      os::kernel::ProcessSchedulerStatus::Succeeded &&
                                  blocked_entry.state == os::kernel::ProcessState::Blocked &&
                                  blocked_entry.wait_reason == wait_reason;
            uint64_t woken_process_count = OS_TEST_PROCESS_RANDOMIZED_EMPTY_VALUE;
            all_invariants_held =
                all_invariants_held &&
                scheduler.WakeBlockedProcesses(
                    wait_reason, OS_TEST_PROCESS_RANDOMIZED_COUNTER_INCREMENT,
                    woken_process_count) == os::kernel::ProcessSchedulerStatus::Succeeded &&
                woken_process_count == OS_TEST_PROCESS_RANDOMIZED_COUNTER_INCREMENT;
        }

        for (uint64_t process_index = OS_TEST_PROCESS_RANDOMIZED_EMPTY_VALUE;
             process_index < process_count; ++process_index) {
            all_invariants_held =
                all_invariants_held && scheduler.TerminateCurrentProcess(decision) ==
                                           os::kernel::ProcessSchedulerStatus::Succeeded;
        }
        const os::kernel::ProcessSchedulerStatistics statistics = scheduler.Statistics();
        all_invariants_held =
            all_invariants_held && decision.completed && !scheduler.IsActive() &&
            statistics.created_process_count == process_count &&
            statistics.terminated_process_count == process_count &&
            statistics.timer_tick_count == timer_tick_count &&
            statistics.block_count == (process_count > OS_TEST_PROCESS_RANDOMIZED_COUNTER_INCREMENT
                                           ? OS_TEST_PROCESS_RANDOMIZED_COUNTER_INCREMENT
                                           : OS_TEST_PROCESS_RANDOMIZED_EMPTY_VALUE) &&
            statistics.wakeup_count == statistics.block_count;
    }

    test_context.Expect(all_invariants_held, OS_TEST_PROCESS_RANDOMIZED_INVARIANTS);
    return test_context.ExitCode();
}

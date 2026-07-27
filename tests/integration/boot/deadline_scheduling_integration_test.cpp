#include "os/kernel/process/thread_scheduler.hpp"
#include "os/kernel/time/monotonic_clock.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_DEADLINE_SCHEDULING_SUITE_NAME =
    "kernel/deadline_scheduling/integration";
constexpr std::string_view OS_TEST_DEADLINE_SCHEDULING_TIMEOUT_MESSAGE =
    "单调时钟推进必须按截止期顺序唤醒阻塞线程";
constexpr std::string_view OS_TEST_DEADLINE_SCHEDULING_SINGLE_WINNER_MESSAGE =
    "条件与截止期竞争必须只有一个赢家且取消残留截止期";
constexpr std::string_view OS_TEST_DEADLINE_SCHEDULING_FAILURE_ATOMICITY_MESSAGE =
    "非法等待请求必须无 deadline 统计和调度状态副作用";
constexpr uint64_t OS_TEST_DEADLINE_SCHEDULING_PROCESS_CAPACITY = 2ULL;
constexpr uint64_t OS_TEST_DEADLINE_SCHEDULING_THREAD_CAPACITY = 4ULL;
constexpr uint64_t OS_TEST_DEADLINE_SCHEDULING_THREADS_PER_PROCESS = 4ULL;
constexpr uint64_t OS_TEST_DEADLINE_SCHEDULING_QUANTUM_TICKS = 4ULL;
constexpr uint64_t OS_TEST_DEADLINE_SCHEDULING_ADDRESS_SPACE_ROOT = 0x00100000ULL;
constexpr uint64_t OS_TEST_DEADLINE_SCHEDULING_STACK_BASE = 0x70000000ULL;
constexpr uint64_t OS_TEST_DEADLINE_SCHEDULING_STACK_STRIDE = 0x00010000ULL;
constexpr uint64_t OS_TEST_DEADLINE_SCHEDULING_QUEUE_ID = 1ULL;
constexpr uint64_t OS_TEST_DEADLINE_SCHEDULING_CLOCK_FREQUENCY_HZ = 1'000ULL;
constexpr uint64_t OS_TEST_DEADLINE_SCHEDULING_CLOCK_DIVISOR = 1ULL;
constexpr uint64_t OS_TEST_DEADLINE_SCHEDULING_FIRST_DEADLINE_NS = 3'000'000ULL;
constexpr uint64_t OS_TEST_DEADLINE_SCHEDULING_SECOND_DEADLINE_NS = 5'000'000ULL;
constexpr uint64_t OS_TEST_DEADLINE_SCHEDULING_FIRST_TICK_COUNT = 2ULL;
constexpr uint64_t OS_TEST_DEADLINE_SCHEDULING_ONE_TICK = 1ULL;
constexpr uint64_t OS_TEST_DEADLINE_SCHEDULING_FIRST_INDEX = 0ULL;
constexpr uint64_t OS_TEST_DEADLINE_SCHEDULING_SECOND_INDEX = 1ULL;
constexpr uint64_t OS_TEST_DEADLINE_SCHEDULING_THIRD_INDEX = 2ULL;
constexpr uint64_t OS_TEST_DEADLINE_SCHEDULING_EXPECTED_ONE = 1ULL;
constexpr uint64_t OS_TEST_DEADLINE_SCHEDULING_EXPECTED_TWO = 2ULL;
constexpr uint64_t OS_TEST_DEADLINE_SCHEDULING_EXPECTED_ZERO = 0ULL;

}

int main() {
    os::test::TestContext test_context{
        OS_TEST_DEADLINE_SCHEDULING_SUITE_NAME};
    os::kernel::ProcessEntry
        processes[OS_TEST_DEADLINE_SCHEDULING_PROCESS_CAPACITY]{};
    os::kernel::ThreadEntry
        threads[OS_TEST_DEADLINE_SCHEDULING_THREAD_CAPACITY]{};
    os::kernel::ThreadScheduler scheduler{};
    os::kernel::WaitQueue wait_queue{};
    os::kernel::WaitQueue uninitialized_wait_queue{};
    os::kernel::MonotonicClock clock{};
    uint64_t process_index = os::kernel::OS_KERNEL_PROCESS_INVALID_INDEX;
    os::kernel::ProcessId process_id{};
    bool setup_succeeded =
        scheduler.Initialize(
            processes, OS_TEST_DEADLINE_SCHEDULING_PROCESS_CAPACITY,
            threads, OS_TEST_DEADLINE_SCHEDULING_THREAD_CAPACITY,
            OS_TEST_DEADLINE_SCHEDULING_THREADS_PER_PROCESS,
            OS_TEST_DEADLINE_SCHEDULING_QUANTUM_TICKS) ==
            os::kernel::ThreadSchedulerStatus::Succeeded &&
        wait_queue.Initialize(os::kernel::WaitQueueId{
            .value = OS_TEST_DEADLINE_SCHEDULING_QUEUE_ID}) ==
            os::kernel::WaitQueueStatus::Succeeded &&
        clock.Initialize(
            OS_TEST_DEADLINE_SCHEDULING_CLOCK_FREQUENCY_HZ,
            OS_TEST_DEADLINE_SCHEDULING_CLOCK_DIVISOR) ==
            os::kernel::MonotonicClockStatus::Succeeded &&
        scheduler.CreateProcess(
            OS_TEST_DEADLINE_SCHEDULING_ADDRESS_SPACE_ROOT,
            process_index, process_id) ==
            os::kernel::ThreadSchedulerStatus::Succeeded;
    for (uint64_t thread_ordinal =
             OS_TEST_DEADLINE_SCHEDULING_FIRST_INDEX;
         thread_ordinal < OS_TEST_DEADLINE_SCHEDULING_THIRD_INDEX +
                              OS_TEST_DEADLINE_SCHEDULING_EXPECTED_ONE;
         ++thread_ordinal) {
        uint64_t thread_index = os::kernel::OS_KERNEL_THREAD_INVALID_INDEX;
        os::kernel::ThreadId thread_id{};
        setup_succeeded =
            setup_succeeded &&
            scheduler.CreateThread(
                process_index, thread_ordinal,
                OS_TEST_DEADLINE_SCHEDULING_STACK_BASE +
                    thread_ordinal *
                        OS_TEST_DEADLINE_SCHEDULING_STACK_STRIDE,
                OS_TEST_DEADLINE_SCHEDULING_EXPECTED_ZERO,
                OS_TEST_DEADLINE_SCHEDULING_EXPECTED_ZERO,
                thread_index, thread_id) ==
                os::kernel::ThreadSchedulerStatus::Succeeded &&
            thread_index == thread_ordinal;
    }

    os::kernel::ThreadSchedulingDecision decision{};
    setup_succeeded =
        setup_succeeded &&
        scheduler.Start(decision) ==
            os::kernel::ThreadSchedulerStatus::Succeeded;
    const bool failure_atomicity_valid =
        setup_succeeded &&
        scheduler.BlockCurrentThreadUntil(
            uninitialized_wait_queue,
            os::kernel::WaitCondition::TestCondition,
            clock.Read().nanoseconds,
            OS_TEST_DEADLINE_SCHEDULING_FIRST_DEADLINE_NS, decision) ==
            os::kernel::ThreadSchedulerStatus::WaitQueueNotInitialized &&
        scheduler.BlockCurrentThreadUntil(
            wait_queue, os::kernel::WaitCondition::None,
            clock.Read().nanoseconds,
            OS_TEST_DEADLINE_SCHEDULING_FIRST_DEADLINE_NS, decision) ==
            os::kernel::ThreadSchedulerStatus::InvalidWaitCondition &&
        scheduler.BlockCurrentThreadUntil(
            wait_queue, os::kernel::WaitCondition::TestCondition,
            clock.Read().nanoseconds, clock.Read().nanoseconds, decision) ==
            os::kernel::ThreadSchedulerStatus::DeadlineAlreadyReached &&
        scheduler.Statistics().deadline_schedule_count ==
            OS_TEST_DEADLINE_SCHEDULING_EXPECTED_ZERO &&
        scheduler.CurrentThreadIndex() ==
            OS_TEST_DEADLINE_SCHEDULING_FIRST_INDEX;
    test_context.Expect(
        failure_atomicity_valid,
        OS_TEST_DEADLINE_SCHEDULING_FAILURE_ATOMICITY_MESSAGE);

    setup_succeeded =
        failure_atomicity_valid &&
        scheduler.BlockCurrentThreadUntil(
            wait_queue, os::kernel::WaitCondition::TestCondition,
            clock.Read().nanoseconds,
            OS_TEST_DEADLINE_SCHEDULING_FIRST_DEADLINE_NS, decision) ==
            os::kernel::ThreadSchedulerStatus::Succeeded &&
        scheduler.BlockCurrentThreadUntil(
            wait_queue, os::kernel::WaitCondition::TestCondition,
            clock.Read().nanoseconds,
            OS_TEST_DEADLINE_SCHEDULING_SECOND_DEADLINE_NS, decision) ==
            os::kernel::ThreadSchedulerStatus::Succeeded &&
        scheduler.CurrentThreadIndex() ==
            OS_TEST_DEADLINE_SCHEDULING_THIRD_INDEX;

    setup_succeeded =
        setup_succeeded &&
        clock.Advance(OS_TEST_DEADLINE_SCHEDULING_FIRST_TICK_COUNT) ==
            os::kernel::MonotonicClockStatus::Succeeded;
    uint64_t woken_thread_index =
        os::kernel::OS_KERNEL_THREAD_INVALID_INDEX;
    os::kernel::WaitCondition wait_condition =
        os::kernel::WaitCondition::None;
    os::kernel::WaitQueue *expired_wait_queue = nullptr;
    bool expired = true;
    setup_succeeded =
        setup_succeeded &&
        scheduler.ExpireNextDeadline(
            clock.Read().nanoseconds, woken_thread_index,
            wait_condition, expired_wait_queue, expired) ==
            os::kernel::ThreadSchedulerStatus::Succeeded &&
        !expired &&
        clock.Advance(OS_TEST_DEADLINE_SCHEDULING_ONE_TICK) ==
            os::kernel::MonotonicClockStatus::Succeeded &&
        scheduler.ExpireNextDeadline(
            clock.Read().nanoseconds, woken_thread_index,
            wait_condition, expired_wait_queue, expired) ==
            os::kernel::ThreadSchedulerStatus::Succeeded &&
        expired &&
        woken_thread_index ==
            OS_TEST_DEADLINE_SCHEDULING_FIRST_INDEX &&
        wait_condition == os::kernel::WaitCondition::TestCondition &&
        expired_wait_queue == &wait_queue;
    os::kernel::ThreadEntry first_thread{};
    test_context.Expect(
        setup_succeeded &&
            scheduler.ReadThread(
                OS_TEST_DEADLINE_SCHEDULING_FIRST_INDEX,
                first_thread) ==
                os::kernel::ThreadSchedulerStatus::Succeeded &&
            first_thread.state == os::kernel::ThreadState::Ready &&
            first_thread.wake_reason == os::kernel::WakeReason::Timeout,
        OS_TEST_DEADLINE_SCHEDULING_TIMEOUT_MESSAGE);

    bool condition_wake_won = false;
    setup_succeeded =
        setup_succeeded &&
        scheduler.WakeThread(
            wait_queue, OS_TEST_DEADLINE_SCHEDULING_SECOND_INDEX,
            os::kernel::WakeReason::ConditionSatisfied,
            condition_wake_won) ==
            os::kernel::ThreadSchedulerStatus::Succeeded &&
        condition_wake_won &&
        clock.Advance(OS_TEST_DEADLINE_SCHEDULING_FIRST_TICK_COUNT) ==
            os::kernel::MonotonicClockStatus::Succeeded &&
        scheduler.ExpireNextDeadline(
            clock.Read().nanoseconds, woken_thread_index,
            wait_condition, expired_wait_queue, expired) ==
            os::kernel::ThreadSchedulerStatus::Succeeded &&
        !expired &&
        scheduler.Validate() ==
            os::kernel::ThreadSchedulerStatus::Succeeded;
    const os::kernel::ThreadSchedulerStatistics statistics =
        scheduler.Statistics();
    test_context.Expect(
        setup_succeeded &&
            statistics.deadline_schedule_count ==
                OS_TEST_DEADLINE_SCHEDULING_EXPECTED_TWO &&
            statistics.deadline_expiration_count ==
                OS_TEST_DEADLINE_SCHEDULING_EXPECTED_ONE &&
            statistics.deadline_cancellation_count ==
                OS_TEST_DEADLINE_SCHEDULING_EXPECTED_ONE &&
            statistics.active_deadline_count ==
                OS_TEST_DEADLINE_SCHEDULING_EXPECTED_ZERO &&
            wait_queue.Statistics().waiting_thread_count ==
                OS_TEST_DEADLINE_SCHEDULING_EXPECTED_ZERO,
        OS_TEST_DEADLINE_SCHEDULING_SINGLE_WINNER_MESSAGE);
    return test_context.ExitCode();
}

#include <os/kernel/process/work_queue.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_WORK_QUEUE_SUITE_NAME = "kernel/work_queue/unit";
constexpr std::string_view OS_TEST_WORK_QUEUE_INITIALIZATION =
    "初始化必须拒绝空存储、零容量和重复初始化";
constexpr std::string_view OS_TEST_WORK_QUEUE_FIFO_DRAIN =
    "即时任务必须 FIFO、重复提交合并、drain 封闭新任务且失败不阻塞后继";
constexpr std::string_view OS_TEST_WORK_QUEUE_DELAY_CANCEL =
    "延迟任务必须公开最近 deadline、支持即时提升、取消、reset、release 与 stale 拒绝";

constexpr uint64_t OS_TEST_WORK_QUEUE_CAPACITY = 8ULL;
constexpr uint64_t OS_TEST_WORK_QUEUE_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_WORK_QUEUE_FIRST_INDEX = 0ULL;
constexpr uint64_t OS_TEST_WORK_QUEUE_SECOND_INDEX = 1ULL;
constexpr uint64_t OS_TEST_WORK_QUEUE_THIRD_INDEX = 2ULL;
constexpr uint64_t OS_TEST_WORK_QUEUE_FOURTH_INDEX = 3ULL;
constexpr uint64_t OS_TEST_WORK_QUEUE_EXPECTED_SINGLE_COUNT = 1ULL;
constexpr uint64_t OS_TEST_WORK_QUEUE_FIRST_DEADLINE = 100ULL;
constexpr uint64_t OS_TEST_WORK_QUEUE_SECOND_DEADLINE = 200ULL;
constexpr uint64_t OS_TEST_WORK_QUEUE_EXPECTED_REGISTERED_COUNT = 4ULL;
constexpr uint64_t OS_TEST_WORK_QUEUE_EXPECTED_EXECUTED_COUNT = 3ULL;

[[nodiscard]] os::kernel::WorkExecutionResult SucceedWork(void *const context) noexcept {
    return context != nullptr &&
                   os::kernel::CurrentSpinLockDepth() == OS_TEST_WORK_QUEUE_EMPTY_VALUE
               ? os::kernel::WorkExecutionResult::Succeeded
               : os::kernel::WorkExecutionResult::Failed;
}

[[nodiscard]] os::kernel::WorkExecutionResult FailWork(void *const context) noexcept {
    static_cast<void>(context);
    return os::kernel::WorkExecutionResult::Failed;
}

[[nodiscard]] bool HandlesEqual(const os::kernel::WorkHandle left,
                                const os::kernel::WorkHandle right) noexcept {
    return left.slot_index == right.slot_index && left.generation == right.generation;
}

[[nodiscard]] bool ValidateInitialization() noexcept {
    os::kernel::WorkQueue queue{};
    os::kernel::WorkQueueEntry entries[OS_TEST_WORK_QUEUE_CAPACITY]{};
    uint64_t delayed_heap[OS_TEST_WORK_QUEUE_CAPACITY]{};
    return queue.Initialize(nullptr, delayed_heap, OS_TEST_WORK_QUEUE_CAPACITY) ==
               os::kernel::WorkQueueStatus::NullEntryStorage &&
           queue.Initialize(entries, nullptr, OS_TEST_WORK_QUEUE_CAPACITY) ==
               os::kernel::WorkQueueStatus::NullDelayedHeapStorage &&
           queue.Initialize(entries, delayed_heap, OS_TEST_WORK_QUEUE_EMPTY_VALUE) ==
               os::kernel::WorkQueueStatus::InvalidCapacity &&
           queue.Initialize(entries, delayed_heap, OS_TEST_WORK_QUEUE_CAPACITY) ==
               os::kernel::WorkQueueStatus::Succeeded &&
           queue.Initialize(entries, delayed_heap, OS_TEST_WORK_QUEUE_CAPACITY) ==
               os::kernel::WorkQueueStatus::AlreadyInitialized &&
           queue.Validate() == os::kernel::WorkQueueStatus::Succeeded;
}

[[nodiscard]] bool ValidateFifoAndDrain() noexcept {
    os::kernel::WorkQueue queue{};
    os::kernel::WorkQueueEntry entries[OS_TEST_WORK_QUEUE_CAPACITY]{};
    uint64_t delayed_heap[OS_TEST_WORK_QUEUE_CAPACITY]{};
    uint64_t contexts[OS_TEST_WORK_QUEUE_CAPACITY]{};
    if (queue.Initialize(entries, delayed_heap, OS_TEST_WORK_QUEUE_CAPACITY) !=
        os::kernel::WorkQueueStatus::Succeeded) {
        return false;
    }
    os::kernel::WorkHandle handles[OS_TEST_WORK_QUEUE_EXPECTED_REGISTERED_COUNT]{};
    for (uint64_t work_index = OS_TEST_WORK_QUEUE_FIRST_INDEX;
         work_index < OS_TEST_WORK_QUEUE_EXPECTED_REGISTERED_COUNT; ++work_index) {
        const os::kernel::WorkOperation operation =
            work_index == OS_TEST_WORK_QUEUE_SECOND_INDEX ? FailWork : SucceedWork;
        if (queue.Register(operation, &contexts[work_index], handles[work_index]) !=
            os::kernel::WorkQueueStatus::Succeeded) {
            return false;
        }
    }
    if (queue.Queue(handles[OS_TEST_WORK_QUEUE_FIRST_INDEX]) !=
            os::kernel::WorkQueueStatus::Succeeded ||
        queue.Queue(handles[OS_TEST_WORK_QUEUE_SECOND_INDEX]) !=
            os::kernel::WorkQueueStatus::Succeeded ||
        queue.Queue(handles[OS_TEST_WORK_QUEUE_THIRD_INDEX]) !=
            os::kernel::WorkQueueStatus::Succeeded ||
        queue.Queue(handles[OS_TEST_WORK_QUEUE_SECOND_INDEX]) !=
            os::kernel::WorkQueueStatus::AlreadyPending ||
        queue.BeginDrain() != os::kernel::WorkQueueStatus::Succeeded ||
        queue.Queue(handles[OS_TEST_WORK_QUEUE_FOURTH_INDEX]) !=
            os::kernel::WorkQueueStatus::DrainInProgress ||
        queue.EndDrain() != os::kernel::WorkQueueStatus::DrainIncomplete) {
        return false;
    }
    for (uint64_t work_index = OS_TEST_WORK_QUEUE_FIRST_INDEX;
         work_index < OS_TEST_WORK_QUEUE_EXPECTED_EXECUTED_COUNT; ++work_index) {
        os::kernel::WorkExecution execution{};
        if (queue.AcquireNext(OS_TEST_WORK_QUEUE_EMPTY_VALUE, execution) !=
                os::kernel::WorkQueueStatus::Succeeded ||
            !HandlesEqual(execution.handle, handles[work_index]) ||
            execution.operation == nullptr || execution.context != &contexts[work_index]) {
            return false;
        }
        const os::kernel::WorkExecutionResult result = execution.operation(execution.context);
        if (queue.Complete(execution.handle, result) != os::kernel::WorkQueueStatus::Succeeded) {
            return false;
        }
    }
    os::kernel::WorkExecution no_ready_execution{};
    const bool drained = queue.DrainComplete() &&
                         queue.EndDrain() == os::kernel::WorkQueueStatus::Succeeded &&
                         queue.AcquireNext(OS_TEST_WORK_QUEUE_EMPTY_VALUE, no_ready_execution) ==
                             os::kernel::WorkQueueStatus::NoReadyWork;
    const os::kernel::WorkQueueStatistics statistics = queue.Statistics();
    return drained && statistics.registered_count == OS_TEST_WORK_QUEUE_EXPECTED_REGISTERED_COUNT &&
           statistics.completed_count == OS_TEST_WORK_QUEUE_EXPECTED_EXECUTED_COUNT &&
           statistics.idle_count == OS_TEST_WORK_QUEUE_EXPECTED_SINGLE_COUNT &&
           statistics.coalesced_queue_count == OS_TEST_WORK_QUEUE_EXPECTED_SINGLE_COUNT &&
           statistics.failed_execution_count == OS_TEST_WORK_QUEUE_EXPECTED_SINGLE_COUNT &&
           statistics.drain_rejection_count == OS_TEST_WORK_QUEUE_EXPECTED_SINGLE_COUNT &&
           queue.Validate() == os::kernel::WorkQueueStatus::Succeeded;
}

[[nodiscard]] bool ValidateDelayedCancellationAndReuse() noexcept {
    os::kernel::WorkQueue queue{};
    os::kernel::WorkQueueEntry entries[OS_TEST_WORK_QUEUE_CAPACITY]{};
    uint64_t delayed_heap[OS_TEST_WORK_QUEUE_CAPACITY]{};
    uint64_t contexts[OS_TEST_WORK_QUEUE_CAPACITY]{};
    if (queue.Initialize(entries, delayed_heap, OS_TEST_WORK_QUEUE_CAPACITY) !=
        os::kernel::WorkQueueStatus::Succeeded) {
        return false;
    }
    os::kernel::WorkHandle first{};
    os::kernel::WorkHandle second{};
    os::kernel::WorkHandle third{};
    if (queue.Register(SucceedWork, &contexts[0], first) !=
            os::kernel::WorkQueueStatus::Succeeded ||
        queue.Register(SucceedWork, &contexts[1], second) !=
            os::kernel::WorkQueueStatus::Succeeded ||
        queue.Register(SucceedWork, &contexts[2], third) !=
            os::kernel::WorkQueueStatus::Succeeded ||
        queue.QueueDelayed(first, OS_TEST_WORK_QUEUE_SECOND_DEADLINE) !=
            os::kernel::WorkQueueStatus::Succeeded ||
        queue.QueueDelayed(second, OS_TEST_WORK_QUEUE_FIRST_DEADLINE) !=
            os::kernel::WorkQueueStatus::Succeeded ||
        queue.QueueDelayed(third, OS_TEST_WORK_QUEUE_FIRST_DEADLINE) !=
            os::kernel::WorkQueueStatus::Succeeded) {
        return false;
    }
    uint64_t deadline_nanoseconds = OS_TEST_WORK_QUEUE_EMPTY_VALUE;
    bool deadline_available = false;
    if (queue.NextDeadline(deadline_nanoseconds, deadline_available) !=
            os::kernel::WorkQueueStatus::Succeeded ||
        !deadline_available || deadline_nanoseconds != OS_TEST_WORK_QUEUE_FIRST_DEADLINE ||
        queue.Queue(first) != os::kernel::WorkQueueStatus::Succeeded) {
        return false;
    }
    os::kernel::WorkExecution execution{};
    if (queue.AcquireNext(OS_TEST_WORK_QUEUE_FIRST_DEADLINE - 1ULL, execution) !=
            os::kernel::WorkQueueStatus::Succeeded ||
        !HandlesEqual(execution.handle, first) ||
        queue.Complete(first, os::kernel::WorkExecutionResult::Succeeded) !=
            os::kernel::WorkQueueStatus::Succeeded ||
        queue.NextDeadline(deadline_nanoseconds, deadline_available) !=
            os::kernel::WorkQueueStatus::Succeeded ||
        !deadline_available || deadline_nanoseconds != OS_TEST_WORK_QUEUE_FIRST_DEADLINE ||
        queue.AcquireNext(OS_TEST_WORK_QUEUE_FIRST_DEADLINE, execution) !=
            os::kernel::WorkQueueStatus::Succeeded ||
        !HandlesEqual(execution.handle, second) ||
        queue.Cancel(second) != os::kernel::WorkQueueStatus::AlreadyRunning ||
        queue.Complete(second, os::kernel::WorkExecutionResult::Succeeded) !=
            os::kernel::WorkQueueStatus::Succeeded ||
        queue.AcquireNext(OS_TEST_WORK_QUEUE_FIRST_DEADLINE, execution) !=
            os::kernel::WorkQueueStatus::Succeeded ||
        !HandlesEqual(execution.handle, third) ||
        queue.Complete(third, os::kernel::WorkExecutionResult::Succeeded) !=
            os::kernel::WorkQueueStatus::Succeeded ||
        queue.Reset(first) != os::kernel::WorkQueueStatus::Succeeded ||
        queue.Release(first) != os::kernel::WorkQueueStatus::Succeeded ||
        queue.Queue(first) != os::kernel::WorkQueueStatus::StaleHandle) {
        return false;
    }
    os::kernel::WorkHandle reused{};
    const os::kernel::WorkQueueStatistics statistics = queue.Statistics();
    return queue.Register(SucceedWork, &contexts[3], reused) ==
               os::kernel::WorkQueueStatus::Succeeded &&
           reused.slot_index == first.slot_index && reused.generation != first.generation &&
           statistics.expedited_queue_count == OS_TEST_WORK_QUEUE_EXPECTED_SINGLE_COUNT &&
           queue.Validate() == os::kernel::WorkQueueStatus::Succeeded;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_WORK_QUEUE_SUITE_NAME};
    test_context.Expect(ValidateInitialization(), OS_TEST_WORK_QUEUE_INITIALIZATION);
    test_context.Expect(ValidateFifoAndDrain(), OS_TEST_WORK_QUEUE_FIFO_DRAIN);
    test_context.Expect(ValidateDelayedCancellationAndReuse(), OS_TEST_WORK_QUEUE_DELAY_CANCEL);
    return test_context.ExitCode();
}

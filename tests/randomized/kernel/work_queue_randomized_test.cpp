#include <os/kernel/process/work_queue.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_WORK_QUEUE_RANDOMIZED_SUITE_NAME =
    "kernel/work_queue/randomized";
constexpr std::string_view OS_TEST_WORK_QUEUE_RANDOMIZED_INVARIANT =
    "十万步注册、即时提升、deadline、合并、取消、执行、drain、reset 和复用必须匹配参考模型";
constexpr uint64_t OS_TEST_WORK_QUEUE_RANDOMIZED_SEED = 0x573239574F524B51ULL;
constexpr uint64_t OS_TEST_WORK_QUEUE_RANDOMIZED_STEP_COUNT = 100000ULL;
constexpr uint64_t OS_TEST_WORK_QUEUE_RANDOMIZED_MULTIPLIER = 6364136223846793005ULL;
constexpr uint64_t OS_TEST_WORK_QUEUE_RANDOMIZED_INCREMENT = 1442695040888963407ULL;
constexpr uint64_t OS_TEST_WORK_QUEUE_RANDOMIZED_CAPACITY = 32ULL;
constexpr uint64_t OS_TEST_WORK_QUEUE_RANDOMIZED_OPERATION_COUNT = 9ULL;
constexpr uint64_t OS_TEST_WORK_QUEUE_RANDOMIZED_REGISTER_OPERATION = 0ULL;
constexpr uint64_t OS_TEST_WORK_QUEUE_RANDOMIZED_QUEUE_OPERATION = 1ULL;
constexpr uint64_t OS_TEST_WORK_QUEUE_RANDOMIZED_DELAY_OPERATION = 2ULL;
constexpr uint64_t OS_TEST_WORK_QUEUE_RANDOMIZED_CANCEL_OPERATION = 3ULL;
constexpr uint64_t OS_TEST_WORK_QUEUE_RANDOMIZED_EXECUTE_OPERATION = 4ULL;
constexpr uint64_t OS_TEST_WORK_QUEUE_RANDOMIZED_RESET_OPERATION = 5ULL;
constexpr uint64_t OS_TEST_WORK_QUEUE_RANDOMIZED_RELEASE_OPERATION = 6ULL;
constexpr uint64_t OS_TEST_WORK_QUEUE_RANDOMIZED_BEGIN_DRAIN_OPERATION = 7ULL;
constexpr uint64_t OS_TEST_WORK_QUEUE_RANDOMIZED_END_DRAIN_OPERATION = 8ULL;
constexpr uint64_t OS_TEST_WORK_QUEUE_RANDOMIZED_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_WORK_QUEUE_RANDOMIZED_FIRST_VALUE = 1ULL;
constexpr uint64_t OS_TEST_WORK_QUEUE_RANDOMIZED_TIME_INCREMENT_LIMIT = 17ULL;
constexpr uint64_t OS_TEST_WORK_QUEUE_RANDOMIZED_DELAY_LIMIT = 97ULL;

struct ReferenceEntry final {
    os::kernel::WorkState state;
    os::kernel::WorkHandle handle;
    uint64_t deadline_nanoseconds;
    uint64_t enqueue_sequence;
    uint64_t ready_order;
};

struct ReferenceCounters final {
    uint64_t registration_count;
    uint64_t release_count;
    uint64_t immediate_queue_count;
    uint64_t delayed_queue_count;
    uint64_t expedited_queue_count;
    uint64_t coalesced_queue_count;
    uint64_t delayed_promotion_count;
    uint64_t acquisition_count;
    uint64_t completion_count;
    uint64_t failed_execution_count;
    uint64_t cancellation_count;
    uint64_t reset_count;
    uint64_t drain_begin_count;
    uint64_t drain_end_count;
    uint64_t drain_rejection_count;
    uint64_t capacity_rejection_count;
    uint64_t peak_registered_count;
    uint64_t peak_pending_count;
    uint64_t peak_running_count;
};

struct RandomizedModel final {
    os::kernel::WorkQueueEntry entries[OS_TEST_WORK_QUEUE_RANDOMIZED_CAPACITY];
    uint64_t delayed_heap[OS_TEST_WORK_QUEUE_RANDOMIZED_CAPACITY];
    ReferenceEntry reference[OS_TEST_WORK_QUEUE_RANDOMIZED_CAPACITY];
    os::kernel::WorkQueue queue;
    ReferenceCounters counters;
    uint64_t now_nanoseconds;
    uint64_t next_enqueue_sequence;
    uint64_t next_ready_order;
    bool draining;
};

[[nodiscard]] os::kernel::WorkExecutionResult RandomizedWork(void *const context) noexcept {
    return context == nullptr ? os::kernel::WorkExecutionResult::Failed
                              : os::kernel::WorkExecutionResult::Succeeded;
}

[[nodiscard]] uint64_t NextRandom(uint64_t &state) noexcept {
    state =
        state * OS_TEST_WORK_QUEUE_RANDOMIZED_MULTIPLIER + OS_TEST_WORK_QUEUE_RANDOMIZED_INCREMENT;
    return state;
}

[[nodiscard]] bool HandlesEqual(const os::kernel::WorkHandle left,
                                const os::kernel::WorkHandle right) noexcept {
    return left.slot_index == right.slot_index && left.generation == right.generation;
}

[[nodiscard]] bool StateIsPending(const os::kernel::WorkState state) noexcept {
    return state == os::kernel::WorkState::Delayed || state == os::kernel::WorkState::Queued ||
           state == os::kernel::WorkState::Running;
}

void UpdatePeaks(RandomizedModel &model) noexcept {
    uint64_t registered_count = OS_TEST_WORK_QUEUE_RANDOMIZED_EMPTY_VALUE;
    uint64_t pending_count = OS_TEST_WORK_QUEUE_RANDOMIZED_EMPTY_VALUE;
    uint64_t running_count = OS_TEST_WORK_QUEUE_RANDOMIZED_EMPTY_VALUE;
    for (const ReferenceEntry &entry : model.reference) {
        if (entry.state != os::kernel::WorkState::Free) {
            ++registered_count;
        }
        if (StateIsPending(entry.state)) {
            ++pending_count;
        }
        if (entry.state == os::kernel::WorkState::Running) {
            ++running_count;
        }
    }
    if (registered_count > model.counters.peak_registered_count) {
        model.counters.peak_registered_count = registered_count;
    }
    if (pending_count > model.counters.peak_pending_count) {
        model.counters.peak_pending_count = pending_count;
    }
    if (running_count > model.counters.peak_running_count) {
        model.counters.peak_running_count = running_count;
    }
}

[[nodiscard]] bool InitializeModel(RandomizedModel &model) noexcept {
    if (model.queue.Initialize(model.entries, model.delayed_heap,
                               OS_TEST_WORK_QUEUE_RANDOMIZED_CAPACITY) !=
        os::kernel::WorkQueueStatus::Succeeded) {
        return false;
    }
    for (uint64_t slot_index = OS_TEST_WORK_QUEUE_RANDOMIZED_EMPTY_VALUE;
         slot_index < OS_TEST_WORK_QUEUE_RANDOMIZED_CAPACITY; ++slot_index) {
        model.reference[slot_index] = ReferenceEntry{
            .state = os::kernel::WorkState::Free,
            .handle =
                os::kernel::WorkHandle{
                    .slot_index = slot_index,
                    .generation = OS_TEST_WORK_QUEUE_RANDOMIZED_EMPTY_VALUE,
                },
            .deadline_nanoseconds = OS_TEST_WORK_QUEUE_RANDOMIZED_EMPTY_VALUE,
            .enqueue_sequence = OS_TEST_WORK_QUEUE_RANDOMIZED_EMPTY_VALUE,
            .ready_order = OS_TEST_WORK_QUEUE_RANDOMIZED_EMPTY_VALUE,
        };
    }
    model.counters = ReferenceCounters{};
    model.now_nanoseconds = OS_TEST_WORK_QUEUE_RANDOMIZED_FIRST_VALUE;
    model.next_enqueue_sequence = OS_TEST_WORK_QUEUE_RANDOMIZED_FIRST_VALUE;
    model.next_ready_order = OS_TEST_WORK_QUEUE_RANDOMIZED_FIRST_VALUE;
    model.draining = false;
    return true;
}

[[nodiscard]] uint64_t FindFirstFreeSlot(const RandomizedModel &model) noexcept {
    for (uint64_t slot_index = OS_TEST_WORK_QUEUE_RANDOMIZED_EMPTY_VALUE;
         slot_index < OS_TEST_WORK_QUEUE_RANDOMIZED_CAPACITY; ++slot_index) {
        if (model.reference[slot_index].state == os::kernel::WorkState::Free &&
            model.reference[slot_index].handle.generation != UINT64_MAX) {
            return slot_index;
        }
    }
    return os::kernel::OS_KERNEL_WORK_QUEUE_INVALID_INDEX;
}

[[nodiscard]] bool RegisterWork(RandomizedModel &model) noexcept {
    os::kernel::WorkHandle handle{};
    const os::kernel::WorkQueueStatus status = model.queue.Register(RandomizedWork, &model, handle);
    if (model.draining) {
        ++model.counters.drain_rejection_count;
        return status == os::kernel::WorkQueueStatus::DrainInProgress;
    }
    const uint64_t slot_index = FindFirstFreeSlot(model);
    if (slot_index == os::kernel::OS_KERNEL_WORK_QUEUE_INVALID_INDEX) {
        ++model.counters.capacity_rejection_count;
        return status == os::kernel::WorkQueueStatus::CapacityExhausted;
    }
    ReferenceEntry &entry = model.reference[slot_index];
    const uint64_t generation =
        entry.handle.generation == OS_TEST_WORK_QUEUE_RANDOMIZED_EMPTY_VALUE
            ? OS_TEST_WORK_QUEUE_RANDOMIZED_FIRST_VALUE
            : entry.handle.generation + OS_TEST_WORK_QUEUE_RANDOMIZED_FIRST_VALUE;
    if (status != os::kernel::WorkQueueStatus::Succeeded || handle.slot_index != slot_index ||
        handle.generation != generation) {
        return false;
    }
    entry = ReferenceEntry{
        .state = os::kernel::WorkState::Idle,
        .handle = handle,
        .deadline_nanoseconds = OS_TEST_WORK_QUEUE_RANDOMIZED_EMPTY_VALUE,
        .enqueue_sequence = OS_TEST_WORK_QUEUE_RANDOMIZED_EMPTY_VALUE,
        .ready_order = OS_TEST_WORK_QUEUE_RANDOMIZED_EMPTY_VALUE,
    };
    ++model.counters.registration_count;
    UpdatePeaks(model);
    return true;
}

[[nodiscard]] bool QueueWork(RandomizedModel &model, const uint64_t slot_index, const bool delayed,
                             const uint64_t deadline_nanoseconds) noexcept {
    ReferenceEntry &entry = model.reference[slot_index];
    const os::kernel::WorkQueueStatus status =
        delayed ? model.queue.QueueDelayed(entry.handle, deadline_nanoseconds)
                : model.queue.Queue(entry.handle);
    if (entry.state == os::kernel::WorkState::Free) {
        return status == os::kernel::WorkQueueStatus::StaleHandle;
    }
    if (entry.state == os::kernel::WorkState::Delayed && !delayed) {
        if (status != os::kernel::WorkQueueStatus::Succeeded) {
            return false;
        }
        entry.state = os::kernel::WorkState::Queued;
        entry.deadline_nanoseconds = OS_TEST_WORK_QUEUE_RANDOMIZED_EMPTY_VALUE;
        entry.ready_order = model.next_ready_order++;
        ++model.counters.immediate_queue_count;
        ++model.counters.expedited_queue_count;
        return true;
    }
    if (StateIsPending(entry.state)) {
        ++model.counters.coalesced_queue_count;
        return status == os::kernel::WorkQueueStatus::AlreadyPending;
    }
    if (entry.state != os::kernel::WorkState::Idle) {
        return status == os::kernel::WorkQueueStatus::InvalidState;
    }
    if (model.draining) {
        ++model.counters.drain_rejection_count;
        return status == os::kernel::WorkQueueStatus::DrainInProgress;
    }
    if (status != os::kernel::WorkQueueStatus::Succeeded) {
        return false;
    }
    entry.state = delayed ? os::kernel::WorkState::Delayed : os::kernel::WorkState::Queued;
    entry.deadline_nanoseconds =
        delayed ? deadline_nanoseconds : OS_TEST_WORK_QUEUE_RANDOMIZED_EMPTY_VALUE;
    entry.enqueue_sequence = model.next_enqueue_sequence++;
    if (delayed) {
        ++model.counters.delayed_queue_count;
    } else {
        entry.ready_order = model.next_ready_order++;
        ++model.counters.immediate_queue_count;
    }
    UpdatePeaks(model);
    return true;
}

[[nodiscard]] bool CancelWork(RandomizedModel &model, const uint64_t slot_index) noexcept {
    ReferenceEntry &entry = model.reference[slot_index];
    const os::kernel::WorkQueueStatus status = model.queue.Cancel(entry.handle);
    if (entry.state == os::kernel::WorkState::Free) {
        return status == os::kernel::WorkQueueStatus::StaleHandle;
    }
    if (entry.state == os::kernel::WorkState::Running) {
        return status == os::kernel::WorkQueueStatus::AlreadyRunning;
    }
    if (entry.state != os::kernel::WorkState::Queued &&
        entry.state != os::kernel::WorkState::Delayed) {
        return status == os::kernel::WorkQueueStatus::NotPending;
    }
    if (status != os::kernel::WorkQueueStatus::Succeeded) {
        return false;
    }
    entry.state = os::kernel::WorkState::Cancelled;
    entry.deadline_nanoseconds = OS_TEST_WORK_QUEUE_RANDOMIZED_EMPTY_VALUE;
    entry.enqueue_sequence = OS_TEST_WORK_QUEUE_RANDOMIZED_EMPTY_VALUE;
    entry.ready_order = OS_TEST_WORK_QUEUE_RANDOMIZED_EMPTY_VALUE;
    ++model.counters.cancellation_count;
    return true;
}

void PromoteReferenceDue(RandomizedModel &model) noexcept {
    while (true) {
        uint64_t candidate_index = os::kernel::OS_KERNEL_WORK_QUEUE_INVALID_INDEX;
        for (uint64_t slot_index = OS_TEST_WORK_QUEUE_RANDOMIZED_EMPTY_VALUE;
             slot_index < OS_TEST_WORK_QUEUE_RANDOMIZED_CAPACITY; ++slot_index) {
            const ReferenceEntry &entry = model.reference[slot_index];
            if (entry.state != os::kernel::WorkState::Delayed ||
                entry.deadline_nanoseconds > model.now_nanoseconds) {
                continue;
            }
            if (candidate_index == os::kernel::OS_KERNEL_WORK_QUEUE_INVALID_INDEX ||
                entry.deadline_nanoseconds <
                    model.reference[candidate_index].deadline_nanoseconds ||
                (entry.deadline_nanoseconds ==
                     model.reference[candidate_index].deadline_nanoseconds &&
                 entry.enqueue_sequence < model.reference[candidate_index].enqueue_sequence)) {
                candidate_index = slot_index;
            }
        }
        if (candidate_index == os::kernel::OS_KERNEL_WORK_QUEUE_INVALID_INDEX) {
            return;
        }
        ReferenceEntry &entry = model.reference[candidate_index];
        entry.state = os::kernel::WorkState::Queued;
        entry.deadline_nanoseconds = OS_TEST_WORK_QUEUE_RANDOMIZED_EMPTY_VALUE;
        entry.ready_order = model.next_ready_order++;
        ++model.counters.delayed_promotion_count;
    }
}

[[nodiscard]] uint64_t FindNextReady(const RandomizedModel &model) noexcept {
    uint64_t candidate_index = os::kernel::OS_KERNEL_WORK_QUEUE_INVALID_INDEX;
    for (uint64_t slot_index = OS_TEST_WORK_QUEUE_RANDOMIZED_EMPTY_VALUE;
         slot_index < OS_TEST_WORK_QUEUE_RANDOMIZED_CAPACITY; ++slot_index) {
        const ReferenceEntry &entry = model.reference[slot_index];
        if (entry.state == os::kernel::WorkState::Queued &&
            (candidate_index == os::kernel::OS_KERNEL_WORK_QUEUE_INVALID_INDEX ||
             entry.ready_order < model.reference[candidate_index].ready_order)) {
            candidate_index = slot_index;
        }
    }
    return candidate_index;
}

[[nodiscard]] bool ExecuteWork(RandomizedModel &model, const uint64_t random_value) noexcept {
    PromoteReferenceDue(model);
    const uint64_t candidate_index = FindNextReady(model);
    os::kernel::WorkExecution execution{};
    const os::kernel::WorkQueueStatus acquire_status =
        model.queue.AcquireNext(model.now_nanoseconds, execution);
    if (candidate_index == os::kernel::OS_KERNEL_WORK_QUEUE_INVALID_INDEX) {
        return acquire_status == os::kernel::WorkQueueStatus::NoReadyWork;
    }
    ReferenceEntry &entry = model.reference[candidate_index];
    if (acquire_status != os::kernel::WorkQueueStatus::Succeeded ||
        !HandlesEqual(execution.handle, entry.handle) || execution.operation != RandomizedWork ||
        execution.context != &model) {
        return false;
    }
    entry.state = os::kernel::WorkState::Running;
    entry.enqueue_sequence = OS_TEST_WORK_QUEUE_RANDOMIZED_EMPTY_VALUE;
    entry.ready_order = OS_TEST_WORK_QUEUE_RANDOMIZED_EMPTY_VALUE;
    ++model.counters.acquisition_count;
    UpdatePeaks(model);
    const os::kernel::WorkExecutionResult result =
        (random_value & OS_TEST_WORK_QUEUE_RANDOMIZED_FIRST_VALUE) ==
                OS_TEST_WORK_QUEUE_RANDOMIZED_EMPTY_VALUE
            ? os::kernel::WorkExecutionResult::Succeeded
            : os::kernel::WorkExecutionResult::Failed;
    if (model.queue.Complete(entry.handle, result) != os::kernel::WorkQueueStatus::Succeeded) {
        return false;
    }
    entry.state = os::kernel::WorkState::Completed;
    ++model.counters.completion_count;
    if (result == os::kernel::WorkExecutionResult::Failed) {
        ++model.counters.failed_execution_count;
    }
    return true;
}

[[nodiscard]] bool ResetWork(RandomizedModel &model, const uint64_t slot_index) noexcept {
    ReferenceEntry &entry = model.reference[slot_index];
    const os::kernel::WorkQueueStatus status = model.queue.Reset(entry.handle);
    if (entry.state == os::kernel::WorkState::Free) {
        return status == os::kernel::WorkQueueStatus::StaleHandle;
    }
    if (entry.state != os::kernel::WorkState::Completed &&
        entry.state != os::kernel::WorkState::Cancelled) {
        return status == os::kernel::WorkQueueStatus::InvalidState;
    }
    if (status != os::kernel::WorkQueueStatus::Succeeded) {
        return false;
    }
    entry.state = os::kernel::WorkState::Idle;
    ++model.counters.reset_count;
    return true;
}

[[nodiscard]] bool ReleaseWork(RandomizedModel &model, const uint64_t slot_index) noexcept {
    ReferenceEntry &entry = model.reference[slot_index];
    const os::kernel::WorkQueueStatus status = model.queue.Release(entry.handle);
    if (entry.state == os::kernel::WorkState::Free) {
        return status == os::kernel::WorkQueueStatus::StaleHandle;
    }
    if (entry.state != os::kernel::WorkState::Idle &&
        entry.state != os::kernel::WorkState::Completed &&
        entry.state != os::kernel::WorkState::Cancelled) {
        return status == os::kernel::WorkQueueStatus::InvalidState;
    }
    if (status != os::kernel::WorkQueueStatus::Succeeded) {
        return false;
    }
    entry.state = os::kernel::WorkState::Free;
    ++model.counters.release_count;
    return true;
}

[[nodiscard]] bool BeginDrain(RandomizedModel &model) noexcept {
    const os::kernel::WorkQueueStatus status = model.queue.BeginDrain();
    if (model.draining) {
        return status == os::kernel::WorkQueueStatus::AlreadyDraining;
    }
    if (status != os::kernel::WorkQueueStatus::Succeeded) {
        return false;
    }
    model.draining = true;
    ++model.counters.drain_begin_count;
    return true;
}

[[nodiscard]] bool EndDrain(RandomizedModel &model) noexcept {
    const os::kernel::WorkQueueStatus status = model.queue.EndDrain();
    if (!model.draining) {
        return status == os::kernel::WorkQueueStatus::InvalidState;
    }
    for (const ReferenceEntry &entry : model.reference) {
        if (StateIsPending(entry.state)) {
            return status == os::kernel::WorkQueueStatus::DrainIncomplete;
        }
    }
    if (status != os::kernel::WorkQueueStatus::Succeeded) {
        return false;
    }
    model.draining = false;
    ++model.counters.drain_end_count;
    return true;
}

[[nodiscard]] bool ValidateModel(const RandomizedModel &model) noexcept {
    if (model.queue.Validate() != os::kernel::WorkQueueStatus::Succeeded) {
        return false;
    }
    uint64_t state_counts[7]{};
    uint64_t expected_deadline_nanoseconds = OS_TEST_WORK_QUEUE_RANDOMIZED_EMPTY_VALUE;
    bool expected_deadline_available = false;
    for (uint64_t slot_index = OS_TEST_WORK_QUEUE_RANDOMIZED_EMPTY_VALUE;
         slot_index < OS_TEST_WORK_QUEUE_RANDOMIZED_CAPACITY; ++slot_index) {
        os::kernel::WorkQueueEntry actual{};
        const ReferenceEntry &expected = model.reference[slot_index];
        if (expected.state == os::kernel::WorkState::Free) {
            continue;
        }
        if (model.queue.Read(expected.handle, actual) != os::kernel::WorkQueueStatus::Succeeded ||
            actual.state != expected.state || actual.generation != expected.handle.generation ||
            actual.deadline_nanoseconds != expected.deadline_nanoseconds ||
            actual.enqueue_sequence != expected.enqueue_sequence) {
            return false;
        }
        ++state_counts[static_cast<uint64_t>(expected.state)];
        if (expected.state == os::kernel::WorkState::Delayed &&
            (!expected_deadline_available ||
             expected.deadline_nanoseconds < expected_deadline_nanoseconds)) {
            expected_deadline_nanoseconds = expected.deadline_nanoseconds;
            expected_deadline_available = true;
        }
    }
    uint64_t actual_deadline_nanoseconds = OS_TEST_WORK_QUEUE_RANDOMIZED_EMPTY_VALUE;
    bool actual_deadline_available = false;
    if (model.queue.NextDeadline(actual_deadline_nanoseconds, actual_deadline_available) !=
            os::kernel::WorkQueueStatus::Succeeded ||
        actual_deadline_available != expected_deadline_available ||
        actual_deadline_nanoseconds != expected_deadline_nanoseconds) {
        return false;
    }
    const os::kernel::WorkQueueStatistics statistics = model.queue.Statistics();
    return statistics.registered_count ==
               model.counters.registration_count - model.counters.release_count &&
           statistics.idle_count ==
               state_counts[static_cast<uint64_t>(os::kernel::WorkState::Idle)] &&
           statistics.delayed_count ==
               state_counts[static_cast<uint64_t>(os::kernel::WorkState::Delayed)] &&
           statistics.queued_count ==
               state_counts[static_cast<uint64_t>(os::kernel::WorkState::Queued)] &&
           statistics.running_count ==
               state_counts[static_cast<uint64_t>(os::kernel::WorkState::Running)] &&
           statistics.completed_count ==
               state_counts[static_cast<uint64_t>(os::kernel::WorkState::Completed)] &&
           statistics.cancelled_count ==
               state_counts[static_cast<uint64_t>(os::kernel::WorkState::Cancelled)] &&
           statistics.registration_count == model.counters.registration_count &&
           statistics.release_count == model.counters.release_count &&
           statistics.immediate_queue_count == model.counters.immediate_queue_count &&
           statistics.delayed_queue_count == model.counters.delayed_queue_count &&
           statistics.expedited_queue_count == model.counters.expedited_queue_count &&
           statistics.coalesced_queue_count == model.counters.coalesced_queue_count &&
           statistics.delayed_promotion_count == model.counters.delayed_promotion_count &&
           statistics.acquisition_count == model.counters.acquisition_count &&
           statistics.completion_count == model.counters.completion_count &&
           statistics.failed_execution_count == model.counters.failed_execution_count &&
           statistics.cancellation_count == model.counters.cancellation_count &&
           statistics.reset_count == model.counters.reset_count &&
           statistics.drain_begin_count == model.counters.drain_begin_count &&
           statistics.drain_end_count == model.counters.drain_end_count &&
           statistics.drain_rejection_count == model.counters.drain_rejection_count &&
           statistics.capacity_rejection_count == model.counters.capacity_rejection_count &&
           statistics.peak_registered_count == model.counters.peak_registered_count &&
           statistics.peak_pending_count == model.counters.peak_pending_count &&
           statistics.peak_running_count == model.counters.peak_running_count &&
           statistics.draining == model.draining;
}

[[nodiscard]] bool ExecuteOperation(RandomizedModel &model, const uint64_t random_value) noexcept {
    const uint64_t operation = random_value % OS_TEST_WORK_QUEUE_RANDOMIZED_OPERATION_COUNT;
    const uint64_t slot_index = (random_value / OS_TEST_WORK_QUEUE_RANDOMIZED_OPERATION_COUNT) %
                                OS_TEST_WORK_QUEUE_RANDOMIZED_CAPACITY;
    model.now_nanoseconds += (random_value % OS_TEST_WORK_QUEUE_RANDOMIZED_TIME_INCREMENT_LIMIT) +
                             OS_TEST_WORK_QUEUE_RANDOMIZED_FIRST_VALUE;
    if (operation == OS_TEST_WORK_QUEUE_RANDOMIZED_REGISTER_OPERATION) {
        return RegisterWork(model);
    }
    if (operation == OS_TEST_WORK_QUEUE_RANDOMIZED_QUEUE_OPERATION) {
        return QueueWork(model, slot_index, false, OS_TEST_WORK_QUEUE_RANDOMIZED_EMPTY_VALUE);
    }
    if (operation == OS_TEST_WORK_QUEUE_RANDOMIZED_DELAY_OPERATION) {
        const uint64_t deadline_nanoseconds =
            model.now_nanoseconds + (random_value % OS_TEST_WORK_QUEUE_RANDOMIZED_DELAY_LIMIT) +
            OS_TEST_WORK_QUEUE_RANDOMIZED_FIRST_VALUE;
        return QueueWork(model, slot_index, true, deadline_nanoseconds);
    }
    if (operation == OS_TEST_WORK_QUEUE_RANDOMIZED_CANCEL_OPERATION) {
        return CancelWork(model, slot_index);
    }
    if (operation == OS_TEST_WORK_QUEUE_RANDOMIZED_EXECUTE_OPERATION) {
        return ExecuteWork(model, random_value);
    }
    if (operation == OS_TEST_WORK_QUEUE_RANDOMIZED_RESET_OPERATION) {
        return ResetWork(model, slot_index);
    }
    if (operation == OS_TEST_WORK_QUEUE_RANDOMIZED_RELEASE_OPERATION) {
        return ReleaseWork(model, slot_index);
    }
    if (operation == OS_TEST_WORK_QUEUE_RANDOMIZED_BEGIN_DRAIN_OPERATION) {
        return BeginDrain(model);
    }
    return operation == OS_TEST_WORK_QUEUE_RANDOMIZED_END_DRAIN_OPERATION && EndDrain(model);
}

[[nodiscard]] bool CleanupModel(RandomizedModel &model) noexcept {
    model.now_nanoseconds = UINT64_MAX;
    while (true) {
        PromoteReferenceDue(model);
        if (FindNextReady(model) == os::kernel::OS_KERNEL_WORK_QUEUE_INVALID_INDEX) {
            break;
        }
        if (!ExecuteWork(model, OS_TEST_WORK_QUEUE_RANDOMIZED_EMPTY_VALUE)) {
            return false;
        }
    }
    if (model.draining && !EndDrain(model)) {
        return false;
    }
    for (uint64_t slot_index = OS_TEST_WORK_QUEUE_RANDOMIZED_EMPTY_VALUE;
         slot_index < OS_TEST_WORK_QUEUE_RANDOMIZED_CAPACITY; ++slot_index) {
        ReferenceEntry &entry = model.reference[slot_index];
        if ((entry.state == os::kernel::WorkState::Completed ||
             entry.state == os::kernel::WorkState::Cancelled) &&
            !ResetWork(model, slot_index)) {
            return false;
        }
        if (entry.state == os::kernel::WorkState::Idle && !ReleaseWork(model, slot_index)) {
            return false;
        }
    }
    return ValidateModel(model) &&
           model.queue.Statistics().registered_count == OS_TEST_WORK_QUEUE_RANDOMIZED_EMPTY_VALUE;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_WORK_QUEUE_RANDOMIZED_SUITE_NAME};
    RandomizedModel model{};
    uint64_t random_state = OS_TEST_WORK_QUEUE_RANDOMIZED_SEED;
    uint64_t failure_step = OS_TEST_WORK_QUEUE_RANDOMIZED_STEP_COUNT;
    bool invariant_held = InitializeModel(model);
    for (uint64_t step = OS_TEST_WORK_QUEUE_RANDOMIZED_EMPTY_VALUE;
         invariant_held && step < OS_TEST_WORK_QUEUE_RANDOMIZED_STEP_COUNT; ++step) {
        invariant_held = ExecuteOperation(model, NextRandom(random_state)) && ValidateModel(model);
        if (!invariant_held) {
            failure_step = step;
        }
    }
    if (invariant_held) {
        invariant_held = CleanupModel(model);
    }
    test_context.ExpectRandom(invariant_held, OS_TEST_WORK_QUEUE_RANDOMIZED_INVARIANT,
                              OS_TEST_WORK_QUEUE_RANDOMIZED_SEED, failure_step);
    return test_context.ExitCode();
}

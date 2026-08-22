#include <os/kernel/process/thread_scheduler.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_THREAD_RANDOMIZED_SUITE_NAME =
    "kernel/thread_scheduler/randomized";
constexpr std::string_view OS_TEST_THREAD_RANDOMIZED_INVARIANTS =
    "十万步 User/Kernel Thread 创建、调度、阻塞、单赢家唤醒、退出和回收必须守恒";
constexpr uint64_t OS_TEST_THREAD_RANDOMIZED_SEED = 0x5448524541445632ULL;
constexpr uint64_t OS_TEST_THREAD_RANDOMIZED_STEP_COUNT = 100000ULL;
constexpr uint64_t OS_TEST_THREAD_RANDOMIZED_MULTIPLIER = 6364136223846793005ULL;
constexpr uint64_t OS_TEST_THREAD_RANDOMIZED_INCREMENT =
    1442695040888963407ULL;
constexpr uint64_t OS_TEST_THREAD_RANDOMIZED_PROCESS_CAPACITY = 16ULL;
constexpr uint64_t OS_TEST_THREAD_RANDOMIZED_THREAD_CAPACITY = 64ULL;
constexpr uint64_t OS_TEST_THREAD_RANDOMIZED_THREADS_PER_PROCESS = 8ULL;
constexpr uint64_t OS_TEST_THREAD_RANDOMIZED_QUANTUM_TICKS = 4ULL;
constexpr uint64_t OS_TEST_THREAD_RANDOMIZED_WAIT_QUEUE_COUNT = 3ULL;
constexpr uint64_t OS_TEST_THREAD_RANDOMIZED_INITIAL_PROCESS_COUNT = 8ULL;
constexpr uint64_t OS_TEST_THREAD_RANDOMIZED_INITIAL_THREADS_PER_PROCESS =
    4ULL;
constexpr uint64_t OS_TEST_THREAD_RANDOMIZED_INITIAL_THREAD_COUNT =
    OS_TEST_THREAD_RANDOMIZED_INITIAL_PROCESS_COUNT *
    OS_TEST_THREAD_RANDOMIZED_INITIAL_THREADS_PER_PROCESS;
constexpr uint64_t OS_TEST_THREAD_RANDOMIZED_INITIAL_KERNEL_THREAD_COUNT = 4ULL;
constexpr uint64_t OS_TEST_THREAD_RANDOMIZED_OPERATION_COUNT = 11ULL;
constexpr uint64_t OS_TEST_THREAD_RANDOMIZED_TICK_OPERATION = 0ULL;
constexpr uint64_t OS_TEST_THREAD_RANDOMIZED_YIELD_OPERATION = 1ULL;
constexpr uint64_t OS_TEST_THREAD_RANDOMIZED_BLOCK_OPERATION = 2ULL;
constexpr uint64_t OS_TEST_THREAD_RANDOMIZED_WAKE_OPERATION = 3ULL;
constexpr uint64_t OS_TEST_THREAD_RANDOMIZED_EXIT_OPERATION = 4ULL;
constexpr uint64_t OS_TEST_THREAD_RANDOMIZED_REAP_THREAD_OPERATION = 5ULL;
constexpr uint64_t OS_TEST_THREAD_RANDOMIZED_REAP_PROCESS_OPERATION = 6ULL;
constexpr uint64_t OS_TEST_THREAD_RANDOMIZED_CREATE_PROCESS_OPERATION = 7ULL;
constexpr uint64_t OS_TEST_THREAD_RANDOMIZED_CREATE_THREAD_OPERATION = 8ULL;
constexpr uint64_t OS_TEST_THREAD_RANDOMIZED_DISCARD_PROCESS_OPERATION = 9ULL;
constexpr uint64_t OS_TEST_THREAD_RANDOMIZED_CREATE_KERNEL_THREAD_OPERATION = 10ULL;
constexpr uint64_t OS_TEST_THREAD_RANDOMIZED_WAKE_REASON_COUNT = 5ULL;
constexpr uint64_t OS_TEST_THREAD_RANDOMIZED_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_THREAD_RANDOMIZED_FIRST_INDEX = 0ULL;
constexpr uint64_t OS_TEST_THREAD_RANDOMIZED_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_TEST_THREAD_RANDOMIZED_ADDRESS_SPACE_BASE =
    0x00100000ULL;
constexpr uint64_t OS_TEST_THREAD_RANDOMIZED_ADDRESS_SPACE_STRIDE =
    0x00001000ULL;
constexpr uint64_t OS_TEST_THREAD_RANDOMIZED_USER_STACK_BASE =
    0x70000000ULL;
constexpr uint64_t OS_TEST_THREAD_RANDOMIZED_USER_STACK_STRIDE =
    0x00010000ULL;
constexpr uint64_t OS_TEST_THREAD_RANDOMIZED_TLS_BASE = 0x60000000ULL;
constexpr uint64_t OS_TEST_THREAD_RANDOMIZED_CLEANUP_PASS_COUNT = 4ULL;
constexpr uint64_t OS_TEST_THREAD_RANDOMIZED_CLEANUP_LIMIT =
    OS_TEST_THREAD_RANDOMIZED_THREAD_CAPACITY *
    OS_TEST_THREAD_RANDOMIZED_COUNTER_INCREMENT *
    OS_TEST_THREAD_RANDOMIZED_CLEANUP_PASS_COUNT;

constexpr os::kernel::WaitCondition
    OS_TEST_THREAD_RANDOMIZED_WAIT_CONDITIONS
        [OS_TEST_THREAD_RANDOMIZED_WAIT_QUEUE_COUNT] = {
            os::kernel::WaitCondition::PipeReadable,
            os::kernel::WaitCondition::PipeWritable,
            os::kernel::WaitCondition::TestCondition,
        };
constexpr os::kernel::WakeReason OS_TEST_THREAD_RANDOMIZED_WAKE_REASONS
    [OS_TEST_THREAD_RANDOMIZED_WAKE_REASON_COUNT] = {
        os::kernel::WakeReason::ConditionSatisfied,
        os::kernel::WakeReason::Timeout,
        os::kernel::WakeReason::Signal,
        os::kernel::WakeReason::ObjectClosed,
        os::kernel::WakeReason::Cancelled,
    };

struct RandomizedModel final {
    os::kernel::ProcessEntry
        processes[OS_TEST_THREAD_RANDOMIZED_PROCESS_CAPACITY];
    os::kernel::ThreadEntry
        threads[OS_TEST_THREAD_RANDOMIZED_THREAD_CAPACITY];
    os::kernel::WaitQueue
        wait_queues[OS_TEST_THREAD_RANDOMIZED_WAIT_QUEUE_COUNT];
    os::kernel::ThreadScheduler scheduler;
    uint64_t last_process_id;
    uint64_t last_thread_id;
    uint64_t last_kernel_thread_id;
    uint64_t address_space_ordinal;
    uint64_t stack_ordinal;
};

[[nodiscard]] uint64_t NextRandom(uint64_t &state) noexcept {
    state = state * OS_TEST_THREAD_RANDOMIZED_MULTIPLIER +
            OS_TEST_THREAD_RANDOMIZED_INCREMENT;
    return state;
}

[[nodiscard]] bool InitializeModel(RandomizedModel &model) noexcept {
    if (model.scheduler.Initialize(
            model.processes, OS_TEST_THREAD_RANDOMIZED_PROCESS_CAPACITY,
            model.threads, OS_TEST_THREAD_RANDOMIZED_THREAD_CAPACITY,
            OS_TEST_THREAD_RANDOMIZED_THREADS_PER_PROCESS,
            OS_TEST_THREAD_RANDOMIZED_QUANTUM_TICKS) !=
        os::kernel::ThreadSchedulerStatus::Succeeded) {
        return false;
    }
    for (uint64_t queue_index = OS_TEST_THREAD_RANDOMIZED_FIRST_INDEX;
         queue_index < OS_TEST_THREAD_RANDOMIZED_WAIT_QUEUE_COUNT;
         ++queue_index) {
        if (model.wait_queues[queue_index].Initialize(
                os::kernel::WaitQueueId{
                    .value =
                        queue_index +
                        OS_TEST_THREAD_RANDOMIZED_COUNTER_INCREMENT}) !=
            os::kernel::WaitQueueStatus::Succeeded) {
            return false;
        }
    }
    model.last_process_id = OS_TEST_THREAD_RANDOMIZED_EMPTY_VALUE;
    model.last_thread_id = OS_TEST_THREAD_RANDOMIZED_EMPTY_VALUE;
    model.last_kernel_thread_id = os::kernel::OS_KERNEL_THREAD_FIRST_KERNEL_IDENTIFIER -
                                  OS_TEST_THREAD_RANDOMIZED_COUNTER_INCREMENT;
    model.address_space_ordinal = OS_TEST_THREAD_RANDOMIZED_COUNTER_INCREMENT;
    model.stack_ordinal = OS_TEST_THREAD_RANDOMIZED_EMPTY_VALUE;
    return true;
}

[[nodiscard]] bool CreateProcess(RandomizedModel &model) noexcept {
    uint64_t process_index = os::kernel::OS_KERNEL_PROCESS_INVALID_INDEX;
    os::kernel::ProcessId process_id{};
    const uint64_t address_space_root =
        OS_TEST_THREAD_RANDOMIZED_ADDRESS_SPACE_BASE +
        model.address_space_ordinal *
            OS_TEST_THREAD_RANDOMIZED_ADDRESS_SPACE_STRIDE;
    const os::kernel::ThreadSchedulerStatus status =
        model.scheduler.CreateProcess(address_space_root, process_index,
                                      process_id);
    const os::kernel::ThreadSchedulerStatistics statistics =
        model.scheduler.Statistics();
    if (statistics.owned_process_count >
        OS_TEST_THREAD_RANDOMIZED_PROCESS_CAPACITY) {
        return false;
    }
    if (status ==
        os::kernel::ThreadSchedulerStatus::ProcessCapacityExhausted) {
        return statistics.owned_process_count ==
               OS_TEST_THREAD_RANDOMIZED_PROCESS_CAPACITY;
    }
    if (status != os::kernel::ThreadSchedulerStatus::Succeeded ||
        process_id.value !=
            model.last_process_id +
                OS_TEST_THREAD_RANDOMIZED_COUNTER_INCREMENT) {
        return false;
    }
    model.last_process_id = process_id.value;
    ++model.address_space_ordinal;
    return true;
}

[[nodiscard]] bool FindAliveProcessWithThreadRoom(
    RandomizedModel &model, const uint64_t search_start,
    uint64_t &process_index) noexcept {
    for (uint64_t search_offset = OS_TEST_THREAD_RANDOMIZED_FIRST_INDEX;
         search_offset < OS_TEST_THREAD_RANDOMIZED_PROCESS_CAPACITY;
         ++search_offset) {
        const uint64_t candidate_index =
            (search_start + search_offset) %
            OS_TEST_THREAD_RANDOMIZED_PROCESS_CAPACITY;
        os::kernel::ProcessEntry process{};
        if (model.scheduler.ReadProcess(candidate_index, process) !=
            os::kernel::ThreadSchedulerStatus::Succeeded) {
            return false;
        }
        if (process.state == os::kernel::ProcessState::Alive &&
            process.thread_count <
                OS_TEST_THREAD_RANDOMIZED_THREADS_PER_PROCESS) {
            process_index = candidate_index;
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool CreateThread(RandomizedModel &model,
                                const uint64_t search_start) noexcept {
    if (model.scheduler.Statistics().owned_thread_count >=
        OS_TEST_THREAD_RANDOMIZED_THREAD_CAPACITY) {
        return true;
    }
    uint64_t process_index = os::kernel::OS_KERNEL_PROCESS_INVALID_INDEX;
    if (!FindAliveProcessWithThreadRoom(model, search_start,
                                        process_index)) {
        return true;
    }
    uint64_t thread_index = os::kernel::OS_KERNEL_THREAD_INVALID_INDEX;
    os::kernel::ThreadId thread_id{};
    const os::kernel::ThreadSchedulerStatus status =
        model.scheduler.CreateThread(
            process_index, model.stack_ordinal,
            OS_TEST_THREAD_RANDOMIZED_USER_STACK_BASE +
                model.stack_ordinal *
                    OS_TEST_THREAD_RANDOMIZED_USER_STACK_STRIDE,
            OS_TEST_THREAD_RANDOMIZED_TLS_BASE + model.stack_ordinal,
            OS_TEST_THREAD_RANDOMIZED_EMPTY_VALUE, thread_index, thread_id);
    if (status != os::kernel::ThreadSchedulerStatus::Succeeded ||
        thread_id.value !=
            model.last_thread_id +
                OS_TEST_THREAD_RANDOMIZED_COUNTER_INCREMENT) {
        return false;
    }
    model.last_thread_id = thread_id.value;
    ++model.stack_ordinal;
    return true;
}

[[nodiscard]] bool CreateKernelThread(RandomizedModel &model) noexcept {
    if (model.scheduler.Statistics().owned_thread_count >=
        OS_TEST_THREAD_RANDOMIZED_THREAD_CAPACITY) {
        return true;
    }
    uint64_t thread_index = os::kernel::OS_KERNEL_THREAD_INVALID_INDEX;
    os::kernel::ThreadId thread_id{};
    const os::kernel::ThreadSchedulerStatus status =
        model.scheduler.CreateKernelThread(model.stack_ordinal, thread_index, thread_id);
    if (status != os::kernel::ThreadSchedulerStatus::Succeeded ||
        thread_id.value !=
            model.last_kernel_thread_id + OS_TEST_THREAD_RANDOMIZED_COUNTER_INCREMENT) {
        return false;
    }
    model.last_kernel_thread_id = thread_id.value;
    ++model.stack_ordinal;
    return true;
}

[[nodiscard]] bool FindThreadInState(RandomizedModel &model, const os::kernel::ThreadState state,
                                     const uint64_t search_start, uint64_t &thread_index) noexcept {
    for (uint64_t search_offset = OS_TEST_THREAD_RANDOMIZED_FIRST_INDEX;
         search_offset < OS_TEST_THREAD_RANDOMIZED_THREAD_CAPACITY; ++search_offset) {
        const uint64_t candidate_index =
            (search_start + search_offset) % OS_TEST_THREAD_RANDOMIZED_THREAD_CAPACITY;
        os::kernel::ThreadEntry thread{};
        if (model.scheduler.ReadThread(candidate_index, thread) !=
            os::kernel::ThreadSchedulerStatus::Succeeded) {
            return false;
        }
        if (thread.state == state) {
            thread_index = candidate_index;
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool FindProcessInStateWithoutThreads(
    RandomizedModel &model, const os::kernel::ProcessState state,
    const uint64_t search_start, uint64_t &process_index) noexcept {
    for (uint64_t search_offset = OS_TEST_THREAD_RANDOMIZED_FIRST_INDEX;
         search_offset < OS_TEST_THREAD_RANDOMIZED_PROCESS_CAPACITY;
         ++search_offset) {
        const uint64_t candidate_index =
            (search_start + search_offset) %
            OS_TEST_THREAD_RANDOMIZED_PROCESS_CAPACITY;
        os::kernel::ProcessEntry process{};
        if (model.scheduler.ReadProcess(candidate_index, process) !=
            os::kernel::ThreadSchedulerStatus::Succeeded) {
            return false;
        }
        if (process.state == state &&
            process.thread_count ==
                OS_TEST_THREAD_RANDOMIZED_EMPTY_VALUE) {
            process_index = candidate_index;
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool StartReadyThreadIfNeeded(
    RandomizedModel &model) noexcept {
    const os::kernel::ThreadSchedulerStatistics statistics =
        model.scheduler.Statistics();
    if (model.scheduler.IsActive() ||
        statistics.ready_thread_count ==
            OS_TEST_THREAD_RANDOMIZED_EMPTY_VALUE) {
        return true;
    }
    os::kernel::ThreadSchedulingDecision decision{};
    return model.scheduler.Start(decision) ==
           os::kernel::ThreadSchedulerStatus::Succeeded;
}

[[nodiscard]] bool ValidateModel(const RandomizedModel &model) noexcept {
    if (model.scheduler.Validate() !=
        os::kernel::ThreadSchedulerStatus::Succeeded) {
        return false;
    }
    for (uint64_t queue_index = OS_TEST_THREAD_RANDOMIZED_FIRST_INDEX;
         queue_index < OS_TEST_THREAD_RANDOMIZED_WAIT_QUEUE_COUNT;
         ++queue_index) {
        if (model.scheduler.ValidateWaitQueue(
                model.wait_queues[queue_index]) !=
            os::kernel::ThreadSchedulerStatus::Succeeded) {
            return false;
        }
    }
    const os::kernel::ThreadSchedulerStatistics statistics =
        model.scheduler.Statistics();
    if (statistics.owned_process_count !=
            statistics.alive_process_count +
                statistics.zombie_process_count ||
        statistics.owned_thread_count !=
            statistics.ready_thread_count +
                statistics.running_thread_count +
                statistics.blocked_thread_count +
                statistics.exited_thread_count ||
        statistics.running_thread_count >
            OS_TEST_THREAD_RANDOMIZED_COUNTER_INCREMENT ||
        (model.scheduler.CurrentThreadIndex() ==
         os::kernel::OS_KERNEL_THREAD_INVALID_INDEX) !=
            (statistics.running_thread_count ==
             OS_TEST_THREAD_RANDOMIZED_EMPTY_VALUE)) {
        return false;
    }

    uint64_t observed_process_count = OS_TEST_THREAD_RANDOMIZED_EMPTY_VALUE;
    uint64_t observed_thread_count = OS_TEST_THREAD_RANDOMIZED_EMPTY_VALUE;
    uint64_t observed_user_thread_count = OS_TEST_THREAD_RANDOMIZED_EMPTY_VALUE;
    uint64_t observed_kernel_thread_count = OS_TEST_THREAD_RANDOMIZED_EMPTY_VALUE;
    for (uint64_t process_index = OS_TEST_THREAD_RANDOMIZED_FIRST_INDEX;
         process_index < OS_TEST_THREAD_RANDOMIZED_PROCESS_CAPACITY; ++process_index) {
        os::kernel::ProcessEntry process{};
        if (model.scheduler.ReadProcess(process_index, process) !=
            os::kernel::ThreadSchedulerStatus::Succeeded) {
            return false;
        }
        if (process.state == os::kernel::ProcessState::Unused) {
            continue;
        }
        ++observed_process_count;
        if (process.live_thread_count + process.exited_thread_count !=
                process.thread_count ||
            (process.state == os::kernel::ProcessState::Zombie &&
             process.live_thread_count !=
                 OS_TEST_THREAD_RANDOMIZED_EMPTY_VALUE)) {
            return false;
        }
    }
    for (uint64_t thread_index = OS_TEST_THREAD_RANDOMIZED_FIRST_INDEX;
         thread_index < OS_TEST_THREAD_RANDOMIZED_THREAD_CAPACITY;
         ++thread_index) {
        os::kernel::ThreadEntry thread{};
        if (model.scheduler.ReadThread(thread_index, thread) !=
            os::kernel::ThreadSchedulerStatus::Succeeded) {
            return false;
        }
        if (thread.state == os::kernel::ThreadState::Unused) {
            if (thread.kind != os::kernel::ThreadKind::None) {
                return false;
            }
            continue;
        }
        ++observed_thread_count;
        if (thread.kind == os::kernel::ThreadKind::User) {
            if (thread.process_index >= OS_TEST_THREAD_RANDOMIZED_PROCESS_CAPACITY) {
                return false;
            }
            ++observed_user_thread_count;
        } else if (thread.kind == os::kernel::ThreadKind::Kernel) {
            if (thread.process_index != os::kernel::OS_KERNEL_PROCESS_INVALID_INDEX ||
                thread.user_stack_pointer != OS_TEST_THREAD_RANDOMIZED_EMPTY_VALUE ||
                thread.thread_local_storage_base != OS_TEST_THREAD_RANDOMIZED_EMPTY_VALUE ||
                thread.signal_mask != OS_TEST_THREAD_RANDOMIZED_EMPTY_VALUE) {
                return false;
            }
            ++observed_kernel_thread_count;
        } else {
            return false;
        }
        if ((thread.state == os::kernel::ThreadState::Blocked) != (thread.wait_queue != nullptr) ||
            (thread.state == os::kernel::ThreadState::Blocked) !=
                (thread.wait_condition != os::kernel::WaitCondition::None) ||
            (thread.state == os::kernel::ThreadState::Blocked &&
             thread.wake_reason != os::kernel::WakeReason::None)) {
            return false;
        }
    }
    return observed_process_count == statistics.owned_process_count &&
           observed_thread_count == statistics.owned_thread_count &&
           observed_user_thread_count == statistics.owned_user_thread_count &&
           observed_kernel_thread_count == statistics.owned_kernel_thread_count &&
           statistics.created_process_count == statistics.owned_process_count +
                                                   statistics.discarded_process_count +
                                                   statistics.reaped_process_count &&
           statistics.created_thread_count == statistics.owned_thread_count +
                                                  statistics.discarded_thread_count +
                                                  statistics.reaped_thread_count;
}

[[nodiscard]] bool SeedInitialWorkload(RandomizedModel &model) noexcept {
    for (uint64_t process_ordinal =
             OS_TEST_THREAD_RANDOMIZED_FIRST_INDEX;
         process_ordinal <
         OS_TEST_THREAD_RANDOMIZED_INITIAL_PROCESS_COUNT;
         ++process_ordinal) {
        if (!CreateProcess(model)) {
            return false;
        }
    }
    for (uint64_t thread_ordinal =
             OS_TEST_THREAD_RANDOMIZED_FIRST_INDEX;
         thread_ordinal <
         OS_TEST_THREAD_RANDOMIZED_INITIAL_THREAD_COUNT;
         ++thread_ordinal) {
        const uint64_t process_index =
            thread_ordinal %
            OS_TEST_THREAD_RANDOMIZED_INITIAL_PROCESS_COUNT;
        uint64_t thread_index =
            os::kernel::OS_KERNEL_THREAD_INVALID_INDEX;
        os::kernel::ThreadId thread_id{};
        if (model.scheduler.CreateThread(
                process_index, model.stack_ordinal,
                OS_TEST_THREAD_RANDOMIZED_USER_STACK_BASE +
                    model.stack_ordinal *
                        OS_TEST_THREAD_RANDOMIZED_USER_STACK_STRIDE,
                OS_TEST_THREAD_RANDOMIZED_TLS_BASE + model.stack_ordinal,
                OS_TEST_THREAD_RANDOMIZED_EMPTY_VALUE, thread_index,
                thread_id) !=
                os::kernel::ThreadSchedulerStatus::Succeeded ||
            thread_id.value !=
                model.last_thread_id +
                    OS_TEST_THREAD_RANDOMIZED_COUNTER_INCREMENT) {
            return false;
        }
        model.last_thread_id = thread_id.value;
        ++model.stack_ordinal;
    }
    for (uint64_t thread_ordinal = OS_TEST_THREAD_RANDOMIZED_FIRST_INDEX;
         thread_ordinal < OS_TEST_THREAD_RANDOMIZED_INITIAL_KERNEL_THREAD_COUNT; ++thread_ordinal) {
        if (!CreateKernelThread(model)) {
            return false;
        }
    }
    return StartReadyThreadIfNeeded(model) && ValidateModel(model);
}

[[nodiscard]] bool ExecuteRandomOperation(
    RandomizedModel &model, const uint64_t random_value) noexcept {
    const uint64_t operation =
        random_value % OS_TEST_THREAD_RANDOMIZED_OPERATION_COUNT;
    const uint64_t search_start =
        random_value % OS_TEST_THREAD_RANDOMIZED_THREAD_CAPACITY;
    os::kernel::ThreadSchedulingDecision decision{};

    if (operation == OS_TEST_THREAD_RANDOMIZED_TICK_OPERATION) {
        return !model.scheduler.IsActive() ||
               model.scheduler.HandleTimerTick(decision) ==
                   os::kernel::ThreadSchedulerStatus::Succeeded;
    }
    if (operation == OS_TEST_THREAD_RANDOMIZED_YIELD_OPERATION) {
        return !model.scheduler.IsActive() ||
               model.scheduler.YieldCurrentThread(decision) ==
                   os::kernel::ThreadSchedulerStatus::Succeeded;
    }
    if (operation == OS_TEST_THREAD_RANDOMIZED_BLOCK_OPERATION) {
        if (!model.scheduler.IsActive()) {
            return true;
        }
        const uint64_t queue_index =
            random_value % OS_TEST_THREAD_RANDOMIZED_WAIT_QUEUE_COUNT;
        return model.scheduler.BlockCurrentThread(
                   model.wait_queues[queue_index],
                   OS_TEST_THREAD_RANDOMIZED_WAIT_CONDITIONS[queue_index],
                   decision) ==
               os::kernel::ThreadSchedulerStatus::Succeeded;
    }
    if (operation == OS_TEST_THREAD_RANDOMIZED_WAKE_OPERATION) {
        const uint64_t queue_index =
            random_value % OS_TEST_THREAD_RANDOMIZED_WAIT_QUEUE_COUNT;
        const os::kernel::WakeReason wake_reason =
            OS_TEST_THREAD_RANDOMIZED_WAKE_REASONS[
                (random_value /
                 OS_TEST_THREAD_RANDOMIZED_WAIT_QUEUE_COUNT) %
                OS_TEST_THREAD_RANDOMIZED_WAKE_REASON_COUNT];
        uint64_t woken_thread_index =
            os::kernel::OS_KERNEL_THREAD_INVALID_INDEX;
        bool wake_won = false;
        if (model.scheduler.WakeOne(
                model.wait_queues[queue_index], wake_reason,
                woken_thread_index, wake_won) !=
            os::kernel::ThreadSchedulerStatus::Succeeded) {
            return false;
        }
        if (!wake_won) {
            return woken_thread_index ==
                   os::kernel::OS_KERNEL_THREAD_INVALID_INDEX;
        }
        bool duplicate_wake_won = true;
        return model.scheduler.WakeThread(
                   model.wait_queues[queue_index], woken_thread_index,
                   wake_reason, duplicate_wake_won) ==
                   os::kernel::ThreadSchedulerStatus::WakeAlreadyResolved &&
               !duplicate_wake_won;
    }
    if (operation == OS_TEST_THREAD_RANDOMIZED_EXIT_OPERATION) {
        return !model.scheduler.IsActive() ||
               model.scheduler.TerminateCurrentThread(decision) ==
                   os::kernel::ThreadSchedulerStatus::Succeeded;
    }
    if (operation ==
        OS_TEST_THREAD_RANDOMIZED_REAP_THREAD_OPERATION) {
        uint64_t thread_index =
            os::kernel::OS_KERNEL_THREAD_INVALID_INDEX;
        return !FindThreadInState(
                   model, os::kernel::ThreadState::Exited, search_start,
                   thread_index) ||
               model.scheduler.ReapExitedThread(thread_index) ==
                   os::kernel::ThreadSchedulerStatus::Succeeded;
    }
    if (operation ==
        OS_TEST_THREAD_RANDOMIZED_REAP_PROCESS_OPERATION) {
        uint64_t process_index =
            os::kernel::OS_KERNEL_PROCESS_INVALID_INDEX;
        return !FindProcessInStateWithoutThreads(
                   model, os::kernel::ProcessState::Zombie,
                   search_start %
                       OS_TEST_THREAD_RANDOMIZED_PROCESS_CAPACITY,
                   process_index) ||
               model.scheduler.ReapZombieProcess(process_index) ==
                   os::kernel::ThreadSchedulerStatus::Succeeded;
    }
    if (operation ==
        OS_TEST_THREAD_RANDOMIZED_CREATE_PROCESS_OPERATION) {
        return CreateProcess(model);
    }
    if (operation ==
        OS_TEST_THREAD_RANDOMIZED_CREATE_THREAD_OPERATION) {
        return CreateThread(
            model, search_start %
                       OS_TEST_THREAD_RANDOMIZED_PROCESS_CAPACITY);
    }
    if (operation == OS_TEST_THREAD_RANDOMIZED_DISCARD_PROCESS_OPERATION) {
        uint64_t process_index = os::kernel::OS_KERNEL_PROCESS_INVALID_INDEX;
        return !FindProcessInStateWithoutThreads(
                   model, os::kernel::ProcessState::Alive,
                   search_start % OS_TEST_THREAD_RANDOMIZED_PROCESS_CAPACITY, process_index) ||
               model.scheduler.DiscardProcess(process_index) ==
                   os::kernel::ThreadSchedulerStatus::Succeeded;
    }
    if (operation == OS_TEST_THREAD_RANDOMIZED_CREATE_KERNEL_THREAD_OPERATION) {
        return CreateKernelThread(model);
    }
    return false;
}

[[nodiscard]] bool CleanupModel(RandomizedModel &model) noexcept {
    for (uint64_t queue_index = OS_TEST_THREAD_RANDOMIZED_FIRST_INDEX;
         queue_index < OS_TEST_THREAD_RANDOMIZED_WAIT_QUEUE_COUNT;
         ++queue_index) {
        uint64_t woken_thread_count =
            OS_TEST_THREAD_RANDOMIZED_EMPTY_VALUE;
        if (model.scheduler.WakeMany(
                model.wait_queues[queue_index],
                os::kernel::WakeReason::Cancelled,
                OS_TEST_THREAD_RANDOMIZED_THREAD_CAPACITY,
                woken_thread_count) !=
            os::kernel::ThreadSchedulerStatus::Succeeded) {
            return false;
        }
    }
    if (!StartReadyThreadIfNeeded(model)) {
        return false;
    }
    for (uint64_t cleanup_step = OS_TEST_THREAD_RANDOMIZED_FIRST_INDEX;
         cleanup_step < OS_TEST_THREAD_RANDOMIZED_CLEANUP_LIMIT;
         ++cleanup_step) {
        const os::kernel::ThreadSchedulerStatistics statistics =
            model.scheduler.Statistics();
        if (statistics.ready_thread_count ==
                OS_TEST_THREAD_RANDOMIZED_EMPTY_VALUE &&
            statistics.running_thread_count ==
                OS_TEST_THREAD_RANDOMIZED_EMPTY_VALUE &&
            statistics.blocked_thread_count ==
                OS_TEST_THREAD_RANDOMIZED_EMPTY_VALUE) {
            break;
        }
        if (!model.scheduler.IsActive()) {
            if (!StartReadyThreadIfNeeded(model)) {
                return false;
            }
            continue;
        }
        os::kernel::ThreadSchedulingDecision decision{};
        if (model.scheduler.TerminateCurrentThread(decision) !=
            os::kernel::ThreadSchedulerStatus::Succeeded) {
            return false;
        }
    }
    for (uint64_t thread_index = OS_TEST_THREAD_RANDOMIZED_FIRST_INDEX;
         thread_index < OS_TEST_THREAD_RANDOMIZED_THREAD_CAPACITY;
         ++thread_index) {
        os::kernel::ThreadEntry thread{};
        if (model.scheduler.ReadThread(thread_index, thread) !=
            os::kernel::ThreadSchedulerStatus::Succeeded) {
            return false;
        }
        if (thread.state == os::kernel::ThreadState::Exited &&
            model.scheduler.ReapExitedThread(thread_index) !=
                os::kernel::ThreadSchedulerStatus::Succeeded) {
            return false;
        }
    }
    for (uint64_t process_index =
             OS_TEST_THREAD_RANDOMIZED_FIRST_INDEX;
         process_index < OS_TEST_THREAD_RANDOMIZED_PROCESS_CAPACITY;
         ++process_index) {
        os::kernel::ProcessEntry process{};
        if (model.scheduler.ReadProcess(process_index, process) !=
            os::kernel::ThreadSchedulerStatus::Succeeded) {
            return false;
        }
        if (process.state == os::kernel::ProcessState::Zombie) {
            if (model.scheduler.ReapZombieProcess(process_index) !=
                os::kernel::ThreadSchedulerStatus::Succeeded) {
                return false;
            }
        } else if (process.state == os::kernel::ProcessState::Alive) {
            if (process.thread_count !=
                    OS_TEST_THREAD_RANDOMIZED_EMPTY_VALUE ||
                model.scheduler.DiscardProcess(process_index) !=
                    os::kernel::ThreadSchedulerStatus::Succeeded) {
                return false;
            }
        }
    }
    const os::kernel::ThreadSchedulerStatistics statistics =
        model.scheduler.Statistics();
    return ValidateModel(model) &&
           statistics.owned_process_count ==
               OS_TEST_THREAD_RANDOMIZED_EMPTY_VALUE &&
           statistics.owned_thread_count ==
               OS_TEST_THREAD_RANDOMIZED_EMPTY_VALUE &&
           statistics.created_process_count ==
               statistics.discarded_process_count +
                   statistics.reaped_process_count &&
           statistics.created_thread_count ==
               statistics.discarded_thread_count +
                   statistics.reaped_thread_count;
}

}

int main() {
    os::test::TestContext test_context{
        OS_TEST_THREAD_RANDOMIZED_SUITE_NAME};
    RandomizedModel model{};
    uint64_t random_state = OS_TEST_THREAD_RANDOMIZED_SEED;
    uint64_t failure_step = OS_TEST_THREAD_RANDOMIZED_STEP_COUNT;
    bool invariants_held =
        InitializeModel(model) && SeedInitialWorkload(model);
    for (uint64_t step = OS_TEST_THREAD_RANDOMIZED_FIRST_INDEX;
         invariants_held && step < OS_TEST_THREAD_RANDOMIZED_STEP_COUNT;
         ++step) {
        const uint64_t random_value = NextRandom(random_state);
        invariants_held =
            ExecuteRandomOperation(model, random_value) &&
            StartReadyThreadIfNeeded(model) && ValidateModel(model);
        if (!invariants_held) {
            failure_step = step;
        }
    }
    if (invariants_held) {
        invariants_held = CleanupModel(model);
    }
    test_context.ExpectRandom(
        invariants_held, OS_TEST_THREAD_RANDOMIZED_INVARIANTS,
        OS_TEST_THREAD_RANDOMIZED_SEED, failure_step);
    return test_context.ExitCode();
}

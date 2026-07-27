#include "os/kernel/process/thread_scheduler.hpp"
#include "os/kernel/sync/mutex.hpp"
#include "os/kernel/sync/spin_lock.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_THREAD_SCHEDULER_SUITE_NAME = "kernel/thread_scheduler/unit";
constexpr std::string_view OS_TEST_THREAD_SCHEDULER_INITIALIZATION_BOUNDARIES =
    "初始化必须拒绝空存储、非法容量、非法单进程线程数和零时间片";
constexpr std::string_view OS_TEST_THREAD_SCHEDULER_INVALID_KERNEL_STACK =
    "Thread 创建必须拒绝无效内核栈槽且保持调度器状态与输出不变";
constexpr std::string_view OS_TEST_THREAD_SCHEDULER_CAPACITY_AND_IDENTIFIERS =
    "容量模型必须建立 256 Process/512 Thread 且 PID/TID 永不绑定槽位复用";
constexpr std::string_view OS_TEST_THREAD_SCHEDULER_PROCESS_THREAD_LIMIT =
    "单 Process 必须在第 65 个 Thread 前保持状态不变并返回明确错误";
constexpr std::string_view OS_TEST_THREAD_SCHEDULER_STATE_LIFECYCLE =
    "Thread 退出回收与 Process Zombie 回收必须保持集合和计数守恒";
constexpr std::string_view OS_TEST_THREAD_SCHEDULER_IMAGE_COMMIT =
    "exec 镜像提交必须只允许当前单线程 Process 原子更新 CR3 与用户栈";
constexpr std::string_view OS_TEST_THREAD_SCHEDULER_WAIT_QUEUE_FIFO =
    "WaitQueue 必须按 FIFO 交付全部 WakeReason 且重复唤醒只能有一个赢家";
constexpr std::string_view OS_TEST_THREAD_SCHEDULER_WAIT_QUEUE_CLOSE =
    "关闭 WaitQueue 必须以 ObjectClosed 唤醒全部等待者并拒绝后续阻塞";
constexpr std::string_view OS_TEST_THREAD_SCHEDULER_MUTEX_HANDOFF =
    "Mutex 竞争必须睡眠并把所有权直接交给 FIFO 队首 Thread";
constexpr std::string_view OS_TEST_THREAD_SCHEDULER_SPIN_LOCK_PROTOCOL =
    "spinlock 必须记录持锁深度，irq-save 锁必须恢复进入前中断状态";
constexpr std::string_view OS_TEST_THREAD_SCHEDULER_SPIN_LOCK_BLOCK_REJECTED =
    "持有 spinlock 时 Mutex 不得调用调度器阻塞";

constexpr uint64_t OS_TEST_THREAD_SCHEDULER_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_THREAD_SCHEDULER_FIRST_IDENTIFIER = 1ULL;
constexpr uint64_t OS_TEST_THREAD_SCHEDULER_SECOND_IDENTIFIER = 2ULL;
constexpr uint64_t OS_TEST_THREAD_SCHEDULER_FIRST_INDEX = 0ULL;
constexpr uint64_t OS_TEST_THREAD_SCHEDULER_SECOND_INDEX = 1ULL;
constexpr uint64_t OS_TEST_THREAD_SCHEDULER_TEST_PROCESS_CAPACITY = 8ULL;
constexpr uint64_t OS_TEST_THREAD_SCHEDULER_TEST_THREAD_CAPACITY = 16ULL;
constexpr uint64_t OS_TEST_THREAD_SCHEDULER_TEST_THREADS_PER_PROCESS = 8ULL;
constexpr uint64_t OS_TEST_THREAD_SCHEDULER_QUANTUM_TICKS = 4ULL;
constexpr uint64_t OS_TEST_THREAD_SCHEDULER_ADDRESS_SPACE_BASE = 0x00100000ULL;
constexpr uint64_t OS_TEST_THREAD_SCHEDULER_ADDRESS_SPACE_STRIDE = 0x00001000ULL;
constexpr uint64_t OS_TEST_THREAD_SCHEDULER_USER_STACK_BASE = 0x70000000ULL;
constexpr uint64_t OS_TEST_THREAD_SCHEDULER_USER_STACK_STRIDE = 0x00010000ULL;
constexpr uint64_t OS_TEST_THREAD_SCHEDULER_TLS_BASE = 0x60000000ULL;
constexpr uint64_t OS_TEST_THREAD_SCHEDULER_SIGNAL_MASK = 0xA5A5ULL;
constexpr uint64_t OS_TEST_THREAD_SCHEDULER_COMMITTED_ADDRESS_SPACE_ROOT = 0x00200000ULL;
constexpr uint64_t OS_TEST_THREAD_SCHEDULER_COMMITTED_USER_STACK = 0x7FFF0000ULL;
constexpr uint64_t OS_TEST_THREAD_SCHEDULER_WAIT_QUEUE_ID = 1ULL;
constexpr uint64_t OS_TEST_THREAD_SCHEDULER_CLOSE_QUEUE_ID = 2ULL;
constexpr uint64_t OS_TEST_THREAD_SCHEDULER_MUTEX_QUEUE_ID = 3ULL;
constexpr uint64_t OS_TEST_THREAD_SCHEDULER_WAKE_REASON_COUNT = 5ULL;
constexpr uint64_t OS_TEST_THREAD_SCHEDULER_CLOSE_WAITER_COUNT = 3ULL;
constexpr uint64_t OS_TEST_THREAD_SCHEDULER_EXPECTED_SINGLE_COUNT = 1ULL;
constexpr uint64_t OS_TEST_THREAD_SCHEDULER_EXPECTED_ZERO_COUNT = 0ULL;

bool test_interrupts_enabled = true;
uint64_t test_disable_interrupt_count;
uint64_t test_restore_interrupt_count;

[[nodiscard]] bool DisableTestInterrupts() noexcept {
    const bool interrupts_were_enabled = test_interrupts_enabled;
    test_interrupts_enabled = false;
    ++test_disable_interrupt_count;
    return interrupts_were_enabled;
}

void RestoreTestInterrupts(const bool interrupts_were_enabled) noexcept {
    test_interrupts_enabled = interrupts_were_enabled;
    ++test_restore_interrupt_count;
}

[[nodiscard]] uint64_t TestAddressSpaceRoot(const uint64_t process_ordinal) noexcept {
    return OS_TEST_THREAD_SCHEDULER_ADDRESS_SPACE_BASE +
           process_ordinal * OS_TEST_THREAD_SCHEDULER_ADDRESS_SPACE_STRIDE;
}

[[nodiscard]] bool CreateTestProcess(os::kernel::ThreadScheduler &scheduler,
                                     const uint64_t process_ordinal, uint64_t &process_index,
                                     os::kernel::ProcessId &process_id) noexcept {
    return scheduler.CreateProcess(TestAddressSpaceRoot(process_ordinal), process_index,
                                   process_id) == os::kernel::ThreadSchedulerStatus::Succeeded;
}

[[nodiscard]] bool CreateTestThread(os::kernel::ThreadScheduler &scheduler,
                                    const uint64_t process_index, const uint64_t thread_ordinal,
                                    uint64_t &thread_index,
                                    os::kernel::ThreadId &thread_id) noexcept {
    return scheduler.CreateThread(process_index, thread_ordinal,
                                  OS_TEST_THREAD_SCHEDULER_USER_STACK_BASE +
                                      thread_ordinal * OS_TEST_THREAD_SCHEDULER_USER_STACK_STRIDE,
                                  OS_TEST_THREAD_SCHEDULER_TLS_BASE + thread_ordinal,
                                  OS_TEST_THREAD_SCHEDULER_SIGNAL_MASK, thread_index,
                                  thread_id) == os::kernel::ThreadSchedulerStatus::Succeeded;
}

[[nodiscard]] bool InitializeTestScheduler(
    os::kernel::ThreadScheduler &scheduler,
    os::kernel::ProcessEntry (&processes)[OS_TEST_THREAD_SCHEDULER_TEST_PROCESS_CAPACITY],
    os::kernel::ThreadEntry (&threads)[OS_TEST_THREAD_SCHEDULER_TEST_THREAD_CAPACITY]) noexcept {
    return scheduler.Initialize(processes, OS_TEST_THREAD_SCHEDULER_TEST_PROCESS_CAPACITY, threads,
                                OS_TEST_THREAD_SCHEDULER_TEST_THREAD_CAPACITY,
                                OS_TEST_THREAD_SCHEDULER_TEST_THREADS_PER_PROCESS,
                                OS_TEST_THREAD_SCHEDULER_QUANTUM_TICKS) ==
           os::kernel::ThreadSchedulerStatus::Succeeded;
}

[[nodiscard]] bool ValidateInitializationBoundaries() noexcept {
    os::kernel::ThreadScheduler scheduler{};
    os::kernel::ProcessEntry processes[OS_TEST_THREAD_SCHEDULER_TEST_PROCESS_CAPACITY]{};
    os::kernel::ThreadEntry threads[OS_TEST_THREAD_SCHEDULER_TEST_THREAD_CAPACITY]{};
    os::kernel::ThreadSchedulingDecision decision{};
    return scheduler.Start(decision) == os::kernel::ThreadSchedulerStatus::NotInitialized &&
           scheduler.Initialize(nullptr, OS_TEST_THREAD_SCHEDULER_TEST_PROCESS_CAPACITY, threads,
                                OS_TEST_THREAD_SCHEDULER_TEST_THREAD_CAPACITY,
                                OS_TEST_THREAD_SCHEDULER_TEST_THREADS_PER_PROCESS,
                                OS_TEST_THREAD_SCHEDULER_QUANTUM_TICKS) ==
               os::kernel::ThreadSchedulerStatus::NullProcessStorage &&
           scheduler.Initialize(processes, OS_TEST_THREAD_SCHEDULER_TEST_PROCESS_CAPACITY, nullptr,
                                OS_TEST_THREAD_SCHEDULER_TEST_THREAD_CAPACITY,
                                OS_TEST_THREAD_SCHEDULER_TEST_THREADS_PER_PROCESS,
                                OS_TEST_THREAD_SCHEDULER_QUANTUM_TICKS) ==
               os::kernel::ThreadSchedulerStatus::NullThreadStorage &&
           scheduler.Initialize(processes, OS_TEST_THREAD_SCHEDULER_EMPTY_VALUE, threads,
                                OS_TEST_THREAD_SCHEDULER_TEST_THREAD_CAPACITY,
                                OS_TEST_THREAD_SCHEDULER_TEST_THREADS_PER_PROCESS,
                                OS_TEST_THREAD_SCHEDULER_QUANTUM_TICKS) ==
               os::kernel::ThreadSchedulerStatus::InvalidProcessCapacity &&
           scheduler.Initialize(processes, OS_TEST_THREAD_SCHEDULER_TEST_PROCESS_CAPACITY, threads,
                                OS_TEST_THREAD_SCHEDULER_TEST_PROCESS_CAPACITY -
                                    OS_TEST_THREAD_SCHEDULER_EXPECTED_SINGLE_COUNT,
                                OS_TEST_THREAD_SCHEDULER_TEST_THREADS_PER_PROCESS,
                                OS_TEST_THREAD_SCHEDULER_QUANTUM_TICKS) ==
               os::kernel::ThreadSchedulerStatus::InvalidThreadCapacity &&
           scheduler.Initialize(processes, OS_TEST_THREAD_SCHEDULER_TEST_PROCESS_CAPACITY, threads,
                                OS_TEST_THREAD_SCHEDULER_TEST_THREAD_CAPACITY,
                                OS_TEST_THREAD_SCHEDULER_EMPTY_VALUE,
                                OS_TEST_THREAD_SCHEDULER_QUANTUM_TICKS) ==
               os::kernel::ThreadSchedulerStatus::InvalidThreadsPerProcess &&
           scheduler.Initialize(processes, OS_TEST_THREAD_SCHEDULER_TEST_PROCESS_CAPACITY, threads,
                                OS_TEST_THREAD_SCHEDULER_TEST_THREAD_CAPACITY,
                                OS_TEST_THREAD_SCHEDULER_TEST_THREADS_PER_PROCESS,
                                OS_TEST_THREAD_SCHEDULER_EMPTY_VALUE) ==
               os::kernel::ThreadSchedulerStatus::InvalidQuantum &&
           InitializeTestScheduler(scheduler, processes, threads) &&
           scheduler.Initialize(processes, OS_TEST_THREAD_SCHEDULER_TEST_PROCESS_CAPACITY, threads,
                                OS_TEST_THREAD_SCHEDULER_TEST_THREAD_CAPACITY,
                                OS_TEST_THREAD_SCHEDULER_TEST_THREADS_PER_PROCESS,
                                OS_TEST_THREAD_SCHEDULER_QUANTUM_TICKS) ==
               os::kernel::ThreadSchedulerStatus::AlreadyInitialized;
}

[[nodiscard]] bool ValidateCapacityAndIdentifiers() noexcept {
    os::kernel::ProcessEntry processes[os::kernel::OS_KERNEL_PROCESS_CAPACITY_LIMIT]{};
    os::kernel::ThreadEntry threads[os::kernel::OS_KERNEL_THREAD_CAPACITY_LIMIT]{};
    os::kernel::ThreadScheduler scheduler{};
    if (scheduler.Initialize(processes, os::kernel::OS_KERNEL_PROCESS_CAPACITY_LIMIT, threads,
                             os::kernel::OS_KERNEL_THREAD_CAPACITY_LIMIT,
                             os::kernel::OS_KERNEL_CAPACITY_THREADS_PER_PROCESS,
                             OS_TEST_THREAD_SCHEDULER_QUANTUM_TICKS) !=
        os::kernel::ThreadSchedulerStatus::Succeeded) {
        return false;
    }

    os::kernel::ProcessId last_process_id{};
    for (uint64_t process_ordinal = OS_TEST_THREAD_SCHEDULER_FIRST_INDEX;
         process_ordinal < os::kernel::OS_KERNEL_PROCESS_CAPACITY_LIMIT; ++process_ordinal) {
        uint64_t process_index = os::kernel::OS_KERNEL_PROCESS_INVALID_INDEX;
        if (!CreateTestProcess(scheduler, process_ordinal, process_index, last_process_id) ||
            process_index != process_ordinal ||
            last_process_id.value != process_ordinal + OS_TEST_THREAD_SCHEDULER_FIRST_IDENTIFIER) {
            return false;
        }
    }
    uint64_t overflow_process_index = os::kernel::OS_KERNEL_PROCESS_INVALID_INDEX;
    os::kernel::ProcessId overflow_process_id{};
    if (scheduler.CreateProcess(TestAddressSpaceRoot(os::kernel::OS_KERNEL_PROCESS_CAPACITY_LIMIT),
                                overflow_process_index, overflow_process_id) !=
        os::kernel::ThreadSchedulerStatus::ProcessCapacityExhausted) {
        return false;
    }

    os::kernel::ThreadId last_thread_id{};
    for (uint64_t thread_ordinal = OS_TEST_THREAD_SCHEDULER_FIRST_INDEX;
         thread_ordinal < os::kernel::OS_KERNEL_THREAD_CAPACITY_LIMIT; ++thread_ordinal) {
        const uint64_t process_index =
            thread_ordinal % os::kernel::OS_KERNEL_PROCESS_CAPACITY_LIMIT;
        uint64_t thread_index = os::kernel::OS_KERNEL_THREAD_INVALID_INDEX;
        if (!CreateTestThread(scheduler, process_index, thread_ordinal, thread_index,
                              last_thread_id) ||
            thread_index != thread_ordinal ||
            last_thread_id.value != thread_ordinal + OS_TEST_THREAD_SCHEDULER_FIRST_IDENTIFIER) {
            return false;
        }
    }
    uint64_t overflow_thread_index = os::kernel::OS_KERNEL_THREAD_INVALID_INDEX;
    os::kernel::ThreadId overflow_thread_id{};
    if (scheduler.CreateThread(
            OS_TEST_THREAD_SCHEDULER_FIRST_INDEX, os::kernel::OS_KERNEL_THREAD_CAPACITY_LIMIT,
            OS_TEST_THREAD_SCHEDULER_USER_STACK_BASE, OS_TEST_THREAD_SCHEDULER_TLS_BASE,
            OS_TEST_THREAD_SCHEDULER_SIGNAL_MASK, overflow_thread_index,
            overflow_thread_id) != os::kernel::ThreadSchedulerStatus::ThreadCapacityExhausted) {
        return false;
    }

    const os::kernel::ThreadSchedulerStatistics statistics = scheduler.Statistics();
    return scheduler.Validate() == os::kernel::ThreadSchedulerStatus::Succeeded &&
           statistics.owned_process_count == os::kernel::OS_KERNEL_PROCESS_CAPACITY_LIMIT &&
           statistics.owned_thread_count == os::kernel::OS_KERNEL_THREAD_CAPACITY_LIMIT &&
           statistics.ready_thread_count == os::kernel::OS_KERNEL_THREAD_CAPACITY_LIMIT &&
           statistics.created_process_count == os::kernel::OS_KERNEL_PROCESS_CAPACITY_LIMIT &&
           statistics.created_thread_count == os::kernel::OS_KERNEL_THREAD_CAPACITY_LIMIT;
}

[[nodiscard]] bool ValidateInvalidKernelStackRejected() noexcept {
    os::kernel::ProcessEntry processes[OS_TEST_THREAD_SCHEDULER_TEST_PROCESS_CAPACITY]{};
    os::kernel::ThreadEntry threads[OS_TEST_THREAD_SCHEDULER_TEST_THREAD_CAPACITY]{};
    os::kernel::ThreadScheduler scheduler{};
    uint64_t process_index = os::kernel::OS_KERNEL_PROCESS_INVALID_INDEX;
    os::kernel::ProcessId process_id{};
    if (!InitializeTestScheduler(scheduler, processes, threads) ||
        !CreateTestProcess(scheduler, OS_TEST_THREAD_SCHEDULER_FIRST_INDEX, process_index,
                           process_id)) {
        return false;
    }

    uint64_t thread_index = OS_TEST_THREAD_SCHEDULER_SECOND_INDEX;
    os::kernel::ThreadId thread_id{.value = OS_TEST_THREAD_SCHEDULER_SECOND_IDENTIFIER};
    const os::kernel::ThreadSchedulerStatus status = scheduler.CreateThread(
        process_index, os::kernel::OS_KERNEL_THREAD_INVALID_INDEX,
        OS_TEST_THREAD_SCHEDULER_USER_STACK_BASE, OS_TEST_THREAD_SCHEDULER_TLS_BASE,
        OS_TEST_THREAD_SCHEDULER_SIGNAL_MASK, thread_index, thread_id);
    const os::kernel::ThreadSchedulerStatistics statistics = scheduler.Statistics();
    return status == os::kernel::ThreadSchedulerStatus::InvalidKernelStack &&
           thread_index == OS_TEST_THREAD_SCHEDULER_SECOND_INDEX &&
           thread_id.value == OS_TEST_THREAD_SCHEDULER_SECOND_IDENTIFIER &&
           statistics.owned_thread_count == OS_TEST_THREAD_SCHEDULER_EXPECTED_ZERO_COUNT &&
           statistics.created_thread_count == OS_TEST_THREAD_SCHEDULER_EXPECTED_ZERO_COUNT &&
           scheduler.Validate() == os::kernel::ThreadSchedulerStatus::Succeeded;
}

[[nodiscard]] bool ValidateProcessThreadLimit() noexcept {
    os::kernel::ProcessEntry process{};
    os::kernel::ThreadEntry threads[os::kernel::OS_KERNEL_CAPACITY_THREADS_PER_PROCESS]{};
    os::kernel::ThreadScheduler scheduler{};
    if (scheduler.Initialize(&process, OS_TEST_THREAD_SCHEDULER_EXPECTED_SINGLE_COUNT, threads,
                             os::kernel::OS_KERNEL_CAPACITY_THREADS_PER_PROCESS,
                             os::kernel::OS_KERNEL_CAPACITY_THREADS_PER_PROCESS,
                             OS_TEST_THREAD_SCHEDULER_QUANTUM_TICKS) !=
        os::kernel::ThreadSchedulerStatus::Succeeded) {
        return false;
    }
    uint64_t process_index = os::kernel::OS_KERNEL_PROCESS_INVALID_INDEX;
    os::kernel::ProcessId process_id{};
    if (!CreateTestProcess(scheduler, OS_TEST_THREAD_SCHEDULER_FIRST_INDEX, process_index,
                           process_id)) {
        return false;
    }
    for (uint64_t thread_ordinal = OS_TEST_THREAD_SCHEDULER_FIRST_INDEX;
         thread_ordinal < os::kernel::OS_KERNEL_CAPACITY_THREADS_PER_PROCESS; ++thread_ordinal) {
        uint64_t thread_index = os::kernel::OS_KERNEL_THREAD_INVALID_INDEX;
        os::kernel::ThreadId thread_id{};
        if (!CreateTestThread(scheduler, process_index, thread_ordinal, thread_index, thread_id)) {
            return false;
        }
    }
    const os::kernel::ThreadSchedulerStatistics before_statistics = scheduler.Statistics();
    uint64_t overflow_thread_index = os::kernel::OS_KERNEL_THREAD_INVALID_INDEX;
    os::kernel::ThreadId overflow_thread_id{};
    const os::kernel::ThreadSchedulerStatus overflow_status = scheduler.CreateThread(
        process_index, os::kernel::OS_KERNEL_CAPACITY_THREADS_PER_PROCESS,
        OS_TEST_THREAD_SCHEDULER_USER_STACK_BASE, OS_TEST_THREAD_SCHEDULER_TLS_BASE,
        OS_TEST_THREAD_SCHEDULER_SIGNAL_MASK, overflow_thread_index, overflow_thread_id);
    const os::kernel::ThreadSchedulerStatistics after_statistics = scheduler.Statistics();
    return overflow_status == os::kernel::ThreadSchedulerStatus::ProcessThreadLimitReached &&
           before_statistics.owned_thread_count == after_statistics.owned_thread_count &&
           before_statistics.created_thread_count == after_statistics.created_thread_count &&
           scheduler.Validate() == os::kernel::ThreadSchedulerStatus::Succeeded;
}

[[nodiscard]] bool ValidateStateLifecycle() noexcept {
    os::kernel::ProcessEntry processes[OS_TEST_THREAD_SCHEDULER_TEST_PROCESS_CAPACITY]{};
    os::kernel::ThreadEntry threads[OS_TEST_THREAD_SCHEDULER_TEST_THREAD_CAPACITY]{};
    os::kernel::ThreadScheduler scheduler{};
    uint64_t process_index = os::kernel::OS_KERNEL_PROCESS_INVALID_INDEX;
    os::kernel::ProcessId first_process_id{};
    uint64_t first_thread_index = os::kernel::OS_KERNEL_THREAD_INVALID_INDEX;
    uint64_t second_thread_index = os::kernel::OS_KERNEL_THREAD_INVALID_INDEX;
    os::kernel::ThreadId first_thread_id{};
    os::kernel::ThreadId second_thread_id{};
    if (!InitializeTestScheduler(scheduler, processes, threads) ||
        !CreateTestProcess(scheduler, OS_TEST_THREAD_SCHEDULER_FIRST_INDEX, process_index,
                           first_process_id) ||
        !CreateTestThread(scheduler, process_index, OS_TEST_THREAD_SCHEDULER_FIRST_INDEX,
                          first_thread_index, first_thread_id) ||
        !CreateTestThread(scheduler, process_index, OS_TEST_THREAD_SCHEDULER_SECOND_INDEX,
                          second_thread_index, second_thread_id)) {
        return false;
    }

    os::kernel::ThreadSchedulingDecision decision{};
    if (scheduler.Start(decision) != os::kernel::ThreadSchedulerStatus::Succeeded ||
        scheduler.TerminateCurrentThread(decision) !=
            os::kernel::ThreadSchedulerStatus::Succeeded ||
        !decision.switched ||
        scheduler.TerminateCurrentThread(decision) !=
            os::kernel::ThreadSchedulerStatus::Succeeded ||
        !decision.completed) {
        return false;
    }
    os::kernel::ProcessEntry zombie_process{};
    if (scheduler.ReadProcess(process_index, zombie_process) !=
            os::kernel::ThreadSchedulerStatus::Succeeded ||
        zombie_process.state != os::kernel::ProcessState::Zombie ||
        zombie_process.live_thread_count != OS_TEST_THREAD_SCHEDULER_EXPECTED_ZERO_COUNT ||
        zombie_process.exited_thread_count != OS_TEST_THREAD_SCHEDULER_SECOND_IDENTIFIER ||
        scheduler.ReapZombieProcess(process_index) !=
            os::kernel::ThreadSchedulerStatus::ProcessThreadsRemain ||
        scheduler.ReapExitedThread(first_thread_index) !=
            os::kernel::ThreadSchedulerStatus::Succeeded ||
        scheduler.ReapExitedThread(second_thread_index) !=
            os::kernel::ThreadSchedulerStatus::Succeeded ||
        scheduler.ReapZombieProcess(process_index) !=
            os::kernel::ThreadSchedulerStatus::Succeeded) {
        return false;
    }

    uint64_t replacement_process_index = os::kernel::OS_KERNEL_PROCESS_INVALID_INDEX;
    os::kernel::ProcessId replacement_process_id{};
    const os::kernel::ThreadSchedulerStatistics statistics = scheduler.Statistics();
    return CreateTestProcess(scheduler, OS_TEST_THREAD_SCHEDULER_SECOND_INDEX,
                             replacement_process_index, replacement_process_id) &&
           replacement_process_index == process_index &&
           first_process_id.value == OS_TEST_THREAD_SCHEDULER_FIRST_IDENTIFIER &&
           replacement_process_id.value == OS_TEST_THREAD_SCHEDULER_SECOND_IDENTIFIER &&
           first_thread_id.value == OS_TEST_THREAD_SCHEDULER_FIRST_IDENTIFIER &&
           second_thread_id.value == OS_TEST_THREAD_SCHEDULER_SECOND_IDENTIFIER &&
           statistics.reaped_thread_count == OS_TEST_THREAD_SCHEDULER_SECOND_IDENTIFIER &&
           statistics.reaped_process_count == OS_TEST_THREAD_SCHEDULER_EXPECTED_SINGLE_COUNT &&
           statistics.zombie_transition_count == OS_TEST_THREAD_SCHEDULER_EXPECTED_SINGLE_COUNT &&
           scheduler.Validate() == os::kernel::ThreadSchedulerStatus::Succeeded;
}

[[nodiscard]] bool ValidateImageCommit() noexcept {
    os::kernel::ProcessEntry processes[OS_TEST_THREAD_SCHEDULER_TEST_PROCESS_CAPACITY]{};
    os::kernel::ThreadEntry threads[OS_TEST_THREAD_SCHEDULER_TEST_THREAD_CAPACITY]{};
    os::kernel::ThreadScheduler scheduler{};
    uint64_t process_index = os::kernel::OS_KERNEL_PROCESS_INVALID_INDEX;
    os::kernel::ProcessId process_id{};
    uint64_t thread_index = os::kernel::OS_KERNEL_THREAD_INVALID_INDEX;
    os::kernel::ThreadId thread_id{};
    if (!InitializeTestScheduler(scheduler, processes, threads) ||
        !CreateTestProcess(scheduler, OS_TEST_THREAD_SCHEDULER_FIRST_INDEX, process_index,
                           process_id) ||
        !CreateTestThread(scheduler, process_index, OS_TEST_THREAD_SCHEDULER_FIRST_INDEX,
                          thread_index, thread_id) ||
        scheduler.CommitProcessImage(process_index, thread_index,
                                     OS_TEST_THREAD_SCHEDULER_COMMITTED_ADDRESS_SPACE_ROOT,
                                     OS_TEST_THREAD_SCHEDULER_COMMITTED_USER_STACK) !=
            os::kernel::ThreadSchedulerStatus::InvalidThreadState) {
        return false;
    }

    os::kernel::ThreadSchedulingDecision decision{};
    if (scheduler.Start(decision) != os::kernel::ThreadSchedulerStatus::Succeeded ||
        scheduler.CommitProcessImage(process_index, thread_index,
                                     OS_TEST_THREAD_SCHEDULER_EMPTY_VALUE,
                                     OS_TEST_THREAD_SCHEDULER_COMMITTED_USER_STACK) !=
            os::kernel::ThreadSchedulerStatus::InvalidAddressSpace ||
        scheduler.CommitProcessImage(process_index, thread_index,
                                     OS_TEST_THREAD_SCHEDULER_COMMITTED_ADDRESS_SPACE_ROOT,
                                     OS_TEST_THREAD_SCHEDULER_COMMITTED_USER_STACK) !=
            os::kernel::ThreadSchedulerStatus::Succeeded) {
        return false;
    }

    os::kernel::ProcessEntry committed_process{};
    os::kernel::ThreadEntry committed_thread{};
    return scheduler.ReadProcess(process_index, committed_process) ==
               os::kernel::ThreadSchedulerStatus::Succeeded &&
           scheduler.ReadThread(thread_index, committed_thread) ==
               os::kernel::ThreadSchedulerStatus::Succeeded &&
           committed_process.process_id.value == process_id.value &&
           committed_process.address_space_root_physical_address ==
               OS_TEST_THREAD_SCHEDULER_COMMITTED_ADDRESS_SPACE_ROOT &&
           committed_thread.thread_id.value == thread_id.value &&
           committed_thread.user_stack_pointer == OS_TEST_THREAD_SCHEDULER_COMMITTED_USER_STACK &&
           scheduler.Validate() == os::kernel::ThreadSchedulerStatus::Succeeded;
}

[[nodiscard]] bool ValidateWaitQueueFifo() noexcept {
    os::kernel::ProcessEntry processes[OS_TEST_THREAD_SCHEDULER_TEST_PROCESS_CAPACITY]{};
    os::kernel::ThreadEntry threads[OS_TEST_THREAD_SCHEDULER_TEST_THREAD_CAPACITY]{};
    os::kernel::ThreadScheduler scheduler{};
    os::kernel::WaitQueue wait_queue{};
    uint64_t process_index = os::kernel::OS_KERNEL_PROCESS_INVALID_INDEX;
    os::kernel::ProcessId process_id{};
    if (!InitializeTestScheduler(scheduler, processes, threads) ||
        wait_queue.Initialize(
            os::kernel::WaitQueueId{.value = OS_TEST_THREAD_SCHEDULER_WAIT_QUEUE_ID}) !=
            os::kernel::WaitQueueStatus::Succeeded ||
        !CreateTestProcess(scheduler, OS_TEST_THREAD_SCHEDULER_FIRST_INDEX, process_index,
                           process_id)) {
        return false;
    }

    uint64_t thread_indices[OS_TEST_THREAD_SCHEDULER_WAKE_REASON_COUNT]{};
    for (uint64_t thread_ordinal = OS_TEST_THREAD_SCHEDULER_FIRST_INDEX;
         thread_ordinal < OS_TEST_THREAD_SCHEDULER_WAKE_REASON_COUNT; ++thread_ordinal) {
        os::kernel::ThreadId thread_id{};
        if (!CreateTestThread(scheduler, process_index, thread_ordinal,
                              thread_indices[thread_ordinal], thread_id)) {
            return false;
        }
    }
    os::kernel::ThreadSchedulingDecision decision{};
    if (scheduler.Start(decision) != os::kernel::ThreadSchedulerStatus::Succeeded) {
        return false;
    }
    for (uint64_t waiter_index = OS_TEST_THREAD_SCHEDULER_FIRST_INDEX;
         waiter_index < OS_TEST_THREAD_SCHEDULER_WAKE_REASON_COUNT; ++waiter_index) {
        if (scheduler.BlockCurrentThread(wait_queue, os::kernel::WaitCondition::TestCondition,
                                         decision) !=
                os::kernel::ThreadSchedulerStatus::Succeeded ||
            decision.previous_thread_index != thread_indices[waiter_index]) {
            return false;
        }
    }
    constexpr os::kernel::WakeReason
        OS_TEST_THREAD_SCHEDULER_WAKE_REASONS[OS_TEST_THREAD_SCHEDULER_WAKE_REASON_COUNT] = {
            os::kernel::WakeReason::ConditionSatisfied,
            os::kernel::WakeReason::Timeout,
            os::kernel::WakeReason::Signal,
            os::kernel::WakeReason::Cancelled,
            os::kernel::WakeReason::ObjectClosed,
        };
    for (uint64_t wake_index = OS_TEST_THREAD_SCHEDULER_FIRST_INDEX;
         wake_index < OS_TEST_THREAD_SCHEDULER_WAKE_REASON_COUNT; ++wake_index) {
        uint64_t woken_thread_index = os::kernel::OS_KERNEL_THREAD_INVALID_INDEX;
        bool wake_won = false;
        if (scheduler.WakeOne(wait_queue, OS_TEST_THREAD_SCHEDULER_WAKE_REASONS[wake_index],
                              woken_thread_index,
                              wake_won) != os::kernel::ThreadSchedulerStatus::Succeeded ||
            !wake_won || woken_thread_index != thread_indices[wake_index]) {
            return false;
        }
        bool duplicate_wake_won = true;
        if (scheduler.WakeThread(
                wait_queue, woken_thread_index, OS_TEST_THREAD_SCHEDULER_WAKE_REASONS[wake_index],
                duplicate_wake_won) != os::kernel::ThreadSchedulerStatus::WakeAlreadyResolved ||
            duplicate_wake_won) {
            return false;
        }
        os::kernel::ThreadEntry woken_thread{};
        if (scheduler.ReadThread(woken_thread_index, woken_thread) !=
                os::kernel::ThreadSchedulerStatus::Succeeded ||
            woken_thread.state != os::kernel::ThreadState::Ready ||
            woken_thread.wake_reason != OS_TEST_THREAD_SCHEDULER_WAKE_REASONS[wake_index] ||
            woken_thread.wake_count != OS_TEST_THREAD_SCHEDULER_EXPECTED_SINGLE_COUNT) {
            return false;
        }
    }
    const os::kernel::WaitQueueStatistics wait_statistics = wait_queue.Statistics();
    return wait_statistics.enqueue_count == OS_TEST_THREAD_SCHEDULER_WAKE_REASON_COUNT &&
           wait_statistics.wake_count == OS_TEST_THREAD_SCHEDULER_WAKE_REASON_COUNT &&
           wait_statistics.waiting_thread_count == OS_TEST_THREAD_SCHEDULER_EXPECTED_ZERO_COUNT &&
           scheduler.ValidateWaitQueue(wait_queue) ==
               os::kernel::ThreadSchedulerStatus::Succeeded &&
           scheduler.Validate() == os::kernel::ThreadSchedulerStatus::Succeeded;
}

[[nodiscard]] bool ValidateWaitQueueClose() noexcept {
    os::kernel::ProcessEntry processes[OS_TEST_THREAD_SCHEDULER_TEST_PROCESS_CAPACITY]{};
    os::kernel::ThreadEntry threads[OS_TEST_THREAD_SCHEDULER_TEST_THREAD_CAPACITY]{};
    os::kernel::ThreadScheduler scheduler{};
    os::kernel::WaitQueue wait_queue{};
    uint64_t process_index = os::kernel::OS_KERNEL_PROCESS_INVALID_INDEX;
    os::kernel::ProcessId process_id{};
    if (!InitializeTestScheduler(scheduler, processes, threads) ||
        wait_queue.Initialize(
            os::kernel::WaitQueueId{.value = OS_TEST_THREAD_SCHEDULER_CLOSE_QUEUE_ID}) !=
            os::kernel::WaitQueueStatus::Succeeded ||
        !CreateTestProcess(scheduler, OS_TEST_THREAD_SCHEDULER_FIRST_INDEX, process_index,
                           process_id)) {
        return false;
    }
    for (uint64_t thread_ordinal = OS_TEST_THREAD_SCHEDULER_FIRST_INDEX;
         thread_ordinal < OS_TEST_THREAD_SCHEDULER_CLOSE_WAITER_COUNT; ++thread_ordinal) {
        uint64_t thread_index = os::kernel::OS_KERNEL_THREAD_INVALID_INDEX;
        os::kernel::ThreadId thread_id{};
        if (!CreateTestThread(scheduler, process_index, thread_ordinal, thread_index, thread_id)) {
            return false;
        }
    }
    os::kernel::ThreadSchedulingDecision decision{};
    if (scheduler.Start(decision) != os::kernel::ThreadSchedulerStatus::Succeeded) {
        return false;
    }
    for (uint64_t waiter_index = OS_TEST_THREAD_SCHEDULER_FIRST_INDEX;
         waiter_index < OS_TEST_THREAD_SCHEDULER_CLOSE_WAITER_COUNT; ++waiter_index) {
        if (scheduler.BlockCurrentThread(wait_queue, os::kernel::WaitCondition::TestCondition,
                                         decision) !=
            os::kernel::ThreadSchedulerStatus::Succeeded) {
            return false;
        }
    }
    uint64_t woken_thread_count = OS_TEST_THREAD_SCHEDULER_EXPECTED_ZERO_COUNT;
    if (scheduler.CloseWaitQueue(wait_queue, woken_thread_count) !=
            os::kernel::ThreadSchedulerStatus::Succeeded ||
        woken_thread_count != OS_TEST_THREAD_SCHEDULER_CLOSE_WAITER_COUNT ||
        scheduler.CloseWaitQueue(wait_queue, woken_thread_count) !=
            os::kernel::ThreadSchedulerStatus::WaitQueueClosed ||
        scheduler.Start(decision) != os::kernel::ThreadSchedulerStatus::Succeeded ||
        scheduler.BlockCurrentThread(wait_queue, os::kernel::WaitCondition::TestCondition,
                                     decision) !=
            os::kernel::ThreadSchedulerStatus::WaitQueueClosed) {
        return false;
    }
    for (uint64_t thread_index = OS_TEST_THREAD_SCHEDULER_FIRST_INDEX;
         thread_index < OS_TEST_THREAD_SCHEDULER_CLOSE_WAITER_COUNT; ++thread_index) {
        os::kernel::ThreadEntry thread{};
        if (scheduler.ReadThread(thread_index, thread) !=
                os::kernel::ThreadSchedulerStatus::Succeeded ||
            thread.wake_reason != os::kernel::WakeReason::ObjectClosed) {
            return false;
        }
    }
    const os::kernel::WaitQueueStatistics statistics = wait_queue.Statistics();
    return statistics.closed &&
           statistics.close_count == OS_TEST_THREAD_SCHEDULER_EXPECTED_SINGLE_COUNT &&
           statistics.waiting_thread_count == OS_TEST_THREAD_SCHEDULER_EXPECTED_ZERO_COUNT &&
           scheduler.ValidateWaitQueue(wait_queue) ==
               os::kernel::ThreadSchedulerStatus::Succeeded &&
           scheduler.Validate() == os::kernel::ThreadSchedulerStatus::Succeeded;
}

[[nodiscard]] bool ValidateMutexHandoff() noexcept {
    os::kernel::ProcessEntry processes[OS_TEST_THREAD_SCHEDULER_TEST_PROCESS_CAPACITY]{};
    os::kernel::ThreadEntry threads[OS_TEST_THREAD_SCHEDULER_TEST_THREAD_CAPACITY]{};
    os::kernel::ThreadScheduler scheduler{};
    os::kernel::Mutex mutex{};
    uint64_t process_index = os::kernel::OS_KERNEL_PROCESS_INVALID_INDEX;
    os::kernel::ProcessId process_id{};
    uint64_t first_thread_index = os::kernel::OS_KERNEL_THREAD_INVALID_INDEX;
    uint64_t second_thread_index = os::kernel::OS_KERNEL_THREAD_INVALID_INDEX;
    os::kernel::ThreadId first_thread_id{};
    os::kernel::ThreadId second_thread_id{};
    if (!InitializeTestScheduler(scheduler, processes, threads) ||
        mutex.Initialize(
            os::kernel::WaitQueueId{.value = OS_TEST_THREAD_SCHEDULER_MUTEX_QUEUE_ID}) !=
            os::kernel::MutexStatus::Succeeded ||
        !CreateTestProcess(scheduler, OS_TEST_THREAD_SCHEDULER_FIRST_INDEX, process_index,
                           process_id) ||
        !CreateTestThread(scheduler, process_index, OS_TEST_THREAD_SCHEDULER_FIRST_INDEX,
                          first_thread_index, first_thread_id) ||
        !CreateTestThread(scheduler, process_index, OS_TEST_THREAD_SCHEDULER_SECOND_INDEX,
                          second_thread_index, second_thread_id)) {
        return false;
    }
    os::kernel::ThreadSchedulingDecision decision{};
    uint64_t woken_thread_index = os::kernel::OS_KERNEL_THREAD_INVALID_INDEX;
    if (scheduler.Start(decision) != os::kernel::ThreadSchedulerStatus::Succeeded ||
        decision.current_thread_index != first_thread_index ||
        mutex.Lock(scheduler, first_thread_id, decision) != os::kernel::MutexStatus::Succeeded ||
        scheduler.YieldCurrentThread(decision) != os::kernel::ThreadSchedulerStatus::Succeeded ||
        decision.current_thread_index != second_thread_index ||
        mutex.Lock(scheduler, second_thread_id, decision) != os::kernel::MutexStatus::Blocked ||
        decision.current_thread_index != first_thread_index ||
        mutex.Unlock(scheduler, first_thread_id, woken_thread_index) !=
            os::kernel::MutexStatus::Succeeded ||
        woken_thread_index != second_thread_index || !mutex.IsOwnedBy(second_thread_id) ||
        scheduler.YieldCurrentThread(decision) != os::kernel::ThreadSchedulerStatus::Succeeded ||
        decision.current_thread_index != second_thread_index ||
        mutex.TryLock(second_thread_id) != os::kernel::MutexStatus::Succeeded ||
        mutex.Unlock(scheduler, second_thread_id, woken_thread_index) !=
            os::kernel::MutexStatus::Succeeded) {
        return false;
    }
    const os::kernel::MutexStatistics statistics = mutex.Statistics();
    return statistics.acquisition_count == OS_TEST_THREAD_SCHEDULER_SECOND_IDENTIFIER &&
           statistics.contention_count == OS_TEST_THREAD_SCHEDULER_EXPECTED_SINGLE_COUNT &&
           statistics.handoff_count == OS_TEST_THREAD_SCHEDULER_EXPECTED_SINGLE_COUNT &&
           statistics.unlock_count == OS_TEST_THREAD_SCHEDULER_SECOND_IDENTIFIER &&
           statistics.waiting_thread_count == OS_TEST_THREAD_SCHEDULER_EXPECTED_ZERO_COUNT &&
           !statistics.owned && mutex.Reset() == os::kernel::MutexStatus::Succeeded &&
           scheduler.Validate() == os::kernel::ThreadSchedulerStatus::Succeeded;
}

[[nodiscard]] bool ValidateSpinLockProtocol() noexcept {
    test_interrupts_enabled = true;
    test_disable_interrupt_count = OS_TEST_THREAD_SCHEDULER_EXPECTED_ZERO_COUNT;
    test_restore_interrupt_count = OS_TEST_THREAD_SCHEDULER_EXPECTED_ZERO_COUNT;
    os::kernel::SpinLock spin_lock{};
    if (os::kernel::CurrentSpinLockDepth() != OS_TEST_THREAD_SCHEDULER_EXPECTED_ZERO_COUNT ||
        !spin_lock.TryLock() || !spin_lock.IsLocked() ||
        os::kernel::CurrentSpinLockDepth() != OS_TEST_THREAD_SCHEDULER_EXPECTED_SINGLE_COUNT) {
        return false;
    }
    spin_lock.Unlock();
    os::kernel::IrqSaveSpinLock irq_lock{DisableTestInterrupts, RestoreTestInterrupts};
    bool interrupts_were_enabled = false;
    if (!irq_lock.TryLock(interrupts_were_enabled) || !interrupts_were_enabled ||
        test_interrupts_enabled || !irq_lock.IsLocked()) {
        return false;
    }
    irq_lock.Unlock(interrupts_were_enabled);
    return test_interrupts_enabled &&
           test_disable_interrupt_count == OS_TEST_THREAD_SCHEDULER_EXPECTED_SINGLE_COUNT &&
           test_restore_interrupt_count == OS_TEST_THREAD_SCHEDULER_EXPECTED_SINGLE_COUNT &&
           os::kernel::CurrentSpinLockDepth() == OS_TEST_THREAD_SCHEDULER_EXPECTED_ZERO_COUNT;
}

[[nodiscard]] bool ValidateSpinLockBlockRejected() noexcept {
    os::kernel::ProcessEntry processes[OS_TEST_THREAD_SCHEDULER_TEST_PROCESS_CAPACITY]{};
    os::kernel::ThreadEntry threads[OS_TEST_THREAD_SCHEDULER_TEST_THREAD_CAPACITY]{};
    os::kernel::ThreadScheduler scheduler{};
    os::kernel::Mutex mutex{};
    os::kernel::SpinLock spin_lock{};
    uint64_t process_index = os::kernel::OS_KERNEL_PROCESS_INVALID_INDEX;
    os::kernel::ProcessId process_id{};
    uint64_t first_thread_index = os::kernel::OS_KERNEL_THREAD_INVALID_INDEX;
    uint64_t second_thread_index = os::kernel::OS_KERNEL_THREAD_INVALID_INDEX;
    os::kernel::ThreadId first_thread_id{};
    os::kernel::ThreadId second_thread_id{};
    os::kernel::ThreadSchedulingDecision decision{};
    if (!InitializeTestScheduler(scheduler, processes, threads) ||
        mutex.Initialize(
            os::kernel::WaitQueueId{.value = OS_TEST_THREAD_SCHEDULER_MUTEX_QUEUE_ID}) !=
            os::kernel::MutexStatus::Succeeded ||
        !CreateTestProcess(scheduler, OS_TEST_THREAD_SCHEDULER_FIRST_INDEX, process_index,
                           process_id) ||
        !CreateTestThread(scheduler, process_index, OS_TEST_THREAD_SCHEDULER_FIRST_INDEX,
                          first_thread_index, first_thread_id) ||
        !CreateTestThread(scheduler, process_index, OS_TEST_THREAD_SCHEDULER_SECOND_INDEX,
                          second_thread_index, second_thread_id) ||
        scheduler.Start(decision) != os::kernel::ThreadSchedulerStatus::Succeeded ||
        mutex.TryLock(first_thread_id) != os::kernel::MutexStatus::Succeeded ||
        scheduler.YieldCurrentThread(decision) != os::kernel::ThreadSchedulerStatus::Succeeded) {
        return false;
    }
    spin_lock.Lock();
    const os::kernel::MutexStatus lock_status = mutex.Lock(scheduler, second_thread_id, decision);
    spin_lock.Unlock();
    const os::kernel::ThreadSchedulerStatistics statistics = scheduler.Statistics();
    return lock_status == os::kernel::MutexStatus::SpinLockHeld &&
           statistics.blocked_thread_count == OS_TEST_THREAD_SCHEDULER_EXPECTED_ZERO_COUNT &&
           statistics.running_thread_count == OS_TEST_THREAD_SCHEDULER_EXPECTED_SINGLE_COUNT &&
           scheduler.CurrentThreadIndex() == second_thread_index &&
           scheduler.Validate() == os::kernel::ThreadSchedulerStatus::Succeeded;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_THREAD_SCHEDULER_SUITE_NAME};
    test_context.Expect(ValidateInitializationBoundaries(),
                        OS_TEST_THREAD_SCHEDULER_INITIALIZATION_BOUNDARIES);
    test_context.Expect(ValidateInvalidKernelStackRejected(),
                        OS_TEST_THREAD_SCHEDULER_INVALID_KERNEL_STACK);
    test_context.Expect(ValidateCapacityAndIdentifiers(),
                        OS_TEST_THREAD_SCHEDULER_CAPACITY_AND_IDENTIFIERS);
    test_context.Expect(ValidateProcessThreadLimit(),
                        OS_TEST_THREAD_SCHEDULER_PROCESS_THREAD_LIMIT);
    test_context.Expect(ValidateStateLifecycle(), OS_TEST_THREAD_SCHEDULER_STATE_LIFECYCLE);
    test_context.Expect(ValidateImageCommit(), OS_TEST_THREAD_SCHEDULER_IMAGE_COMMIT);
    test_context.Expect(ValidateWaitQueueFifo(), OS_TEST_THREAD_SCHEDULER_WAIT_QUEUE_FIFO);
    test_context.Expect(ValidateWaitQueueClose(), OS_TEST_THREAD_SCHEDULER_WAIT_QUEUE_CLOSE);
    test_context.Expect(ValidateMutexHandoff(), OS_TEST_THREAD_SCHEDULER_MUTEX_HANDOFF);
    test_context.Expect(ValidateSpinLockProtocol(), OS_TEST_THREAD_SCHEDULER_SPIN_LOCK_PROTOCOL);
    test_context.Expect(ValidateSpinLockBlockRejected(),
                        OS_TEST_THREAD_SCHEDULER_SPIN_LOCK_BLOCK_REJECTED);
    return test_context.ExitCode();
}

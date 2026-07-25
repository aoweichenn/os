#include "os/kernel/process_memory_layout.hpp"
#include "os/kernel/process_scheduler.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_PROCESS_SCHEDULER_SUITE_NAME = "kernel/process_scheduler/unit";
constexpr std::string_view OS_TEST_PROCESS_SCHEDULER_REJECTS_ZERO_QUANTUM =
    "零 tick 时间片必须被拒绝";
constexpr std::string_view OS_TEST_PROCESS_SCHEDULER_REQUIRES_INITIALIZATION =
    "创建进程前必须成功初始化调度器";
constexpr std::string_view OS_TEST_PROCESS_SCHEDULER_CREATES_STABLE_IDENTIFIERS =
    "进程创建必须分配稳定 PID 并拒绝超出容量";
constexpr std::string_view OS_TEST_PROCESS_SCHEDULER_DISCARDS_READY_PROCESS =
    "创建失败回滚必须释放 Ready 槽位且保持 PID 单调";
constexpr std::string_view OS_TEST_PROCESS_SCHEDULER_PREEMPTS_AT_QUANTUM =
    "时间片到期必须轮转到下一个 Ready 进程";
constexpr std::string_view OS_TEST_PROCESS_SCHEDULER_TERMINATES_TO_COMPLETION =
    "终止必须依次交接并在无 Ready 进程时完成";
constexpr std::string_view OS_TEST_PROCESS_SCHEDULER_BLOCKS_AND_WAKES =
    "阻塞和唤醒必须原子迁移状态并保持等待原因统计";
constexpr std::string_view OS_TEST_PROCESS_SCHEDULER_ENTERS_IDLE =
    "最后一个运行进程阻塞后必须进入无当前进程的可唤醒 idle 状态";
constexpr std::string_view OS_TEST_PROCESS_SCHEDULER_REJECTS_INVALID_INDEX =
    "读取越界 PCB 索引必须返回明确错误";
constexpr std::string_view OS_TEST_PROCESS_SCHEDULER_KERNEL_STACK_LAYOUT =
    "每个进程必须拥有对齐、互不重叠且带保护页的 Ring 0 栈";
constexpr uint64_t OS_TEST_PROCESS_SCHEDULER_QUANTUM_TICKS = 4ULL;
constexpr uint64_t OS_TEST_PROCESS_SCHEDULER_FIRST_PROCESS_ID = 1ULL;
constexpr uint64_t OS_TEST_PROCESS_SCHEDULER_SECOND_PROCESS_ID = 2ULL;
constexpr uint64_t OS_TEST_PROCESS_SCHEDULER_LAST_PROCESS_ID =
    OS_TEST_PROCESS_SCHEDULER_FIRST_PROCESS_ID + os::kernel::OS_KERNEL_PROCESS_CAPACITY - 1ULL;
constexpr uint64_t OS_TEST_PROCESS_SCHEDULER_PREEMPTION_TICK_COUNT =
    OS_TEST_PROCESS_SCHEDULER_QUANTUM_TICKS;
constexpr uint64_t OS_TEST_PROCESS_SCHEDULER_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_PROCESS_SCHEDULER_FIRST_INDEX = 0ULL;
constexpr uint64_t OS_TEST_PROCESS_SCHEDULER_ADDRESS_PROBE_SIZE_BYTES = 1ULL;
constexpr uint64_t OS_TEST_PROCESS_SCHEDULER_EXPECTED_PREEMPTION_COUNT = 1ULL;
constexpr uint64_t OS_TEST_PROCESS_SCHEDULER_BLOCKING_PROCESS_COUNT = 3ULL;
constexpr uint64_t OS_TEST_PROCESS_SCHEDULER_SINGLE_WAKE_COUNT = 1ULL;

}

int main() {
    os::test::TestContext test_context{OS_TEST_PROCESS_SCHEDULER_SUITE_NAME};

    os::kernel::ProcessScheduler invalid_scheduler{};
    uint64_t invalid_process_index = os::kernel::OS_KERNEL_PROCESS_INVALID_INDEX;
    uint64_t invalid_process_id = OS_TEST_PROCESS_SCHEDULER_EMPTY_VALUE;
    test_context.Expect(
        invalid_scheduler.CreateProcess(invalid_process_index, invalid_process_id) ==
            os::kernel::ProcessSchedulerStatus::NotInitialized,
        OS_TEST_PROCESS_SCHEDULER_REQUIRES_INITIALIZATION);
    test_context.Expect(invalid_scheduler.Initialize(OS_TEST_PROCESS_SCHEDULER_EMPTY_VALUE) ==
                            os::kernel::ProcessSchedulerStatus::InvalidQuantum,
                        OS_TEST_PROCESS_SCHEDULER_REJECTS_ZERO_QUANTUM);

    os::kernel::ProcessScheduler rollback_scheduler{};
    uint64_t discarded_process_index = os::kernel::OS_KERNEL_PROCESS_INVALID_INDEX;
    uint64_t discarded_process_id = OS_TEST_PROCESS_SCHEDULER_EMPTY_VALUE;
    uint64_t replacement_process_index = os::kernel::OS_KERNEL_PROCESS_INVALID_INDEX;
    uint64_t replacement_process_id = OS_TEST_PROCESS_SCHEDULER_EMPTY_VALUE;
    const bool rollback_succeeded =
        rollback_scheduler.Initialize(OS_TEST_PROCESS_SCHEDULER_QUANTUM_TICKS) ==
            os::kernel::ProcessSchedulerStatus::Succeeded &&
        rollback_scheduler.CreateProcess(discarded_process_index, discarded_process_id) ==
            os::kernel::ProcessSchedulerStatus::Succeeded &&
        rollback_scheduler.DiscardReadyProcess(discarded_process_index) ==
            os::kernel::ProcessSchedulerStatus::Succeeded &&
        rollback_scheduler.CreateProcess(replacement_process_index, replacement_process_id) ==
            os::kernel::ProcessSchedulerStatus::Succeeded;
    test_context.Expect(rollback_succeeded &&
                            discarded_process_index == replacement_process_index &&
                            discarded_process_id == OS_TEST_PROCESS_SCHEDULER_FIRST_PROCESS_ID &&
                            replacement_process_id == OS_TEST_PROCESS_SCHEDULER_SECOND_PROCESS_ID &&
                            rollback_scheduler.Statistics().created_process_count ==
                                OS_TEST_PROCESS_SCHEDULER_FIRST_PROCESS_ID,
                        OS_TEST_PROCESS_SCHEDULER_DISCARDS_READY_PROCESS);

    os::kernel::ProcessScheduler scheduler{};
    const bool initialized = scheduler.Initialize(OS_TEST_PROCESS_SCHEDULER_QUANTUM_TICKS) ==
                             os::kernel::ProcessSchedulerStatus::Succeeded;
    uint64_t first_process_index = os::kernel::OS_KERNEL_PROCESS_INVALID_INDEX;
    uint64_t first_process_id = OS_TEST_PROCESS_SCHEDULER_EMPTY_VALUE;
    bool creation_succeeded = scheduler.CreateProcess(first_process_index, first_process_id) ==
                              os::kernel::ProcessSchedulerStatus::Succeeded;
    uint64_t last_process_id = first_process_id;
    for (uint64_t process_count = OS_TEST_PROCESS_SCHEDULER_FIRST_PROCESS_ID;
         process_count < os::kernel::OS_KERNEL_PROCESS_CAPACITY; ++process_count) {
        uint64_t process_index = os::kernel::OS_KERNEL_PROCESS_INVALID_INDEX;
        creation_succeeded =
            creation_succeeded && scheduler.CreateProcess(process_index, last_process_id) ==
                                      os::kernel::ProcessSchedulerStatus::Succeeded;
    }
    uint64_t overflow_process_index = os::kernel::OS_KERNEL_PROCESS_INVALID_INDEX;
    uint64_t overflow_process_id = OS_TEST_PROCESS_SCHEDULER_EMPTY_VALUE;
    test_context.Expect(initialized && creation_succeeded &&
                            first_process_id == OS_TEST_PROCESS_SCHEDULER_FIRST_PROCESS_ID &&
                            last_process_id == OS_TEST_PROCESS_SCHEDULER_LAST_PROCESS_ID &&
                            scheduler.CreateProcess(overflow_process_index, overflow_process_id) ==
                                os::kernel::ProcessSchedulerStatus::CapacityExhausted,
                        OS_TEST_PROCESS_SCHEDULER_CREATES_STABLE_IDENTIFIERS);

    os::kernel::ProcessSchedulingDecision decision{};
    bool scheduling_succeeded =
        scheduler.Start(decision) == os::kernel::ProcessSchedulerStatus::Succeeded &&
        decision.current_process_index == first_process_index && !decision.switched;
    for (uint64_t tick_index = OS_TEST_PROCESS_SCHEDULER_FIRST_INDEX;
         tick_index < OS_TEST_PROCESS_SCHEDULER_PREEMPTION_TICK_COUNT; ++tick_index) {
        scheduling_succeeded =
            scheduling_succeeded &&
            scheduler.HandleTimerTick(decision) == os::kernel::ProcessSchedulerStatus::Succeeded;
    }
    os::kernel::ProcessSchedulerEntry first_entry{};
    test_context.Expect(scheduling_succeeded && decision.switched &&
                            decision.previous_process_index == first_process_index &&
                            decision.current_process_index != first_process_index &&
                            scheduler.ReadEntry(first_process_index, first_entry) ==
                                os::kernel::ProcessSchedulerStatus::Succeeded &&
                            first_entry.state == os::kernel::ProcessState::Ready &&
                            first_entry.run_tick_count ==
                                OS_TEST_PROCESS_SCHEDULER_PREEMPTION_TICK_COUNT,
                        OS_TEST_PROCESS_SCHEDULER_PREEMPTS_AT_QUANTUM);

    bool termination_succeeded = true;
    for (uint64_t termination_index = 0ULL;
         termination_index < os::kernel::OS_KERNEL_PROCESS_CAPACITY; ++termination_index) {
        termination_succeeded =
            termination_succeeded && scheduler.TerminateCurrentProcess(decision) ==
                                         os::kernel::ProcessSchedulerStatus::Succeeded;
    }
    const os::kernel::ProcessSchedulerStatistics statistics = scheduler.Statistics();
    test_context.Expect(
        termination_succeeded && decision.completed && !scheduler.IsActive() &&
            statistics.created_process_count == os::kernel::OS_KERNEL_PROCESS_CAPACITY &&
            statistics.terminated_process_count == os::kernel::OS_KERNEL_PROCESS_CAPACITY &&
            statistics.preemption_count == OS_TEST_PROCESS_SCHEDULER_EXPECTED_PREEMPTION_COUNT,
        OS_TEST_PROCESS_SCHEDULER_TERMINATES_TO_COMPLETION);

    os::kernel::ProcessScheduler blocking_scheduler{};
    bool blocking_setup_succeeded =
        blocking_scheduler.Initialize(OS_TEST_PROCESS_SCHEDULER_QUANTUM_TICKS) ==
        os::kernel::ProcessSchedulerStatus::Succeeded;
    for (uint64_t process_index = OS_TEST_PROCESS_SCHEDULER_FIRST_INDEX;
         process_index < OS_TEST_PROCESS_SCHEDULER_BLOCKING_PROCESS_COUNT; ++process_index) {
        uint64_t created_process_index = os::kernel::OS_KERNEL_PROCESS_INVALID_INDEX;
        uint64_t created_process_id = OS_TEST_PROCESS_SCHEDULER_EMPTY_VALUE;
        blocking_setup_succeeded =
            blocking_setup_succeeded &&
            blocking_scheduler.CreateProcess(created_process_index, created_process_id) ==
                os::kernel::ProcessSchedulerStatus::Succeeded;
    }
    os::kernel::ProcessSchedulingDecision blocking_decision{};
    blocking_setup_succeeded =
        blocking_setup_succeeded && blocking_scheduler.Start(blocking_decision) ==
                                        os::kernel::ProcessSchedulerStatus::Succeeded;
    const uint64_t blocked_reader_index = blocking_decision.current_process_index;
    bool block_wake_succeeded =
        blocking_setup_succeeded &&
        blocking_scheduler.BlockCurrentProcess(os::kernel::ProcessWaitReason::PipeReadable,
                                               blocking_decision) ==
            os::kernel::ProcessSchedulerStatus::Succeeded &&
        blocking_decision.switched &&
        blocking_decision.previous_process_index == blocked_reader_index;
    os::kernel::ProcessSchedulerEntry blocked_reader_entry{};
    block_wake_succeeded =
        block_wake_succeeded &&
        blocking_scheduler.ReadEntry(blocked_reader_index, blocked_reader_entry) ==
            os::kernel::ProcessSchedulerStatus::Succeeded &&
        blocked_reader_entry.state == os::kernel::ProcessState::Blocked &&
        blocked_reader_entry.wait_reason == os::kernel::ProcessWaitReason::PipeReadable;
    uint64_t woken_process_count = OS_TEST_PROCESS_SCHEDULER_EMPTY_VALUE;
    block_wake_succeeded =
        block_wake_succeeded &&
        blocking_scheduler.WakeBlockedProcesses(os::kernel::ProcessWaitReason::PipeReadable,
                                                OS_TEST_PROCESS_SCHEDULER_SINGLE_WAKE_COUNT,
                                                woken_process_count) ==
            os::kernel::ProcessSchedulerStatus::Succeeded &&
        woken_process_count == OS_TEST_PROCESS_SCHEDULER_SINGLE_WAKE_COUNT &&
        blocking_scheduler.ReadEntry(blocked_reader_index, blocked_reader_entry) ==
            os::kernel::ProcessSchedulerStatus::Succeeded &&
        blocked_reader_entry.state == os::kernel::ProcessState::Ready &&
        blocked_reader_entry.wait_reason == os::kernel::ProcessWaitReason::None &&
        blocked_reader_entry.block_count == OS_TEST_PROCESS_SCHEDULER_SINGLE_WAKE_COUNT &&
        blocked_reader_entry.wakeup_count == OS_TEST_PROCESS_SCHEDULER_SINGLE_WAKE_COUNT;
    const os::kernel::ProcessSchedulerStatistics blocking_statistics =
        blocking_scheduler.Statistics();
    test_context.Expect(
        block_wake_succeeded &&
            blocking_statistics.block_count == OS_TEST_PROCESS_SCHEDULER_SINGLE_WAKE_COUNT &&
            blocking_statistics.wakeup_count == OS_TEST_PROCESS_SCHEDULER_SINGLE_WAKE_COUNT,
        OS_TEST_PROCESS_SCHEDULER_BLOCKS_AND_WAKES);

    os::kernel::ProcessScheduler single_process_scheduler{};
    uint64_t single_process_index = os::kernel::OS_KERNEL_PROCESS_INVALID_INDEX;
    uint64_t single_process_id = OS_TEST_PROCESS_SCHEDULER_EMPTY_VALUE;
    os::kernel::ProcessSchedulingDecision single_process_decision{};
    os::kernel::ProcessSchedulerEntry single_process_entry{};
    uint64_t single_woken_process_count = OS_TEST_PROCESS_SCHEDULER_EMPTY_VALUE;
    const bool idle_block_resumes =
        single_process_scheduler.Initialize(OS_TEST_PROCESS_SCHEDULER_QUANTUM_TICKS) ==
            os::kernel::ProcessSchedulerStatus::Succeeded &&
        single_process_scheduler.CreateProcess(single_process_index, single_process_id) ==
            os::kernel::ProcessSchedulerStatus::Succeeded &&
        single_process_scheduler.Start(single_process_decision) ==
            os::kernel::ProcessSchedulerStatus::Succeeded &&
        single_process_scheduler.BlockCurrentProcess(os::kernel::ProcessWaitReason::PipeReadable,
                                                     single_process_decision) ==
            os::kernel::ProcessSchedulerStatus::Succeeded &&
        !single_process_decision.switched &&
        single_process_scheduler.CurrentProcessIndex() ==
            os::kernel::OS_KERNEL_PROCESS_INVALID_INDEX &&
        !single_process_scheduler.IsActive() &&
        single_process_scheduler.ReadEntry(single_process_index, single_process_entry) ==
            os::kernel::ProcessSchedulerStatus::Succeeded &&
        single_process_entry.state == os::kernel::ProcessState::Blocked &&
        single_process_scheduler.WakeBlockedProcesses(os::kernel::ProcessWaitReason::PipeReadable,
                                                      OS_TEST_PROCESS_SCHEDULER_SINGLE_WAKE_COUNT,
                                                      single_woken_process_count) ==
            os::kernel::ProcessSchedulerStatus::Succeeded &&
        single_woken_process_count == OS_TEST_PROCESS_SCHEDULER_SINGLE_WAKE_COUNT &&
        single_process_scheduler.Start(single_process_decision) ==
            os::kernel::ProcessSchedulerStatus::Succeeded &&
        single_process_decision.current_process_index == single_process_index &&
        single_process_scheduler.IsActive();
    test_context.Expect(idle_block_resumes, OS_TEST_PROCESS_SCHEDULER_ENTERS_IDLE);

    os::kernel::ProcessSchedulerEntry invalid_entry{};
    test_context.Expect(
        scheduler.ReadEntry(os::kernel::OS_KERNEL_PROCESS_CAPACITY, invalid_entry) ==
            os::kernel::ProcessSchedulerStatus::InvalidProcessIndex,
        OS_TEST_PROCESS_SCHEDULER_REJECTS_INVALID_INDEX);

    bool kernel_stack_layout_valid = true;
    uint64_t previous_stack_top_address = OS_TEST_PROCESS_SCHEDULER_EMPTY_VALUE;
    for (uint64_t process_index = OS_TEST_PROCESS_SCHEDULER_FIRST_INDEX;
         process_index < os::kernel::OS_KERNEL_PROCESS_CAPACITY; ++process_index) {
        const uint64_t guard_page_address =
            os::kernel::ProcessKernelStackGuardPageAddress(process_index);
        const uint64_t stack_top_address = os::kernel::ProcessKernelStackTopAddress(process_index);
        kernel_stack_layout_valid =
            kernel_stack_layout_valid &&
            guard_page_address % os::kernel::OS_KERNEL_PROCESS_KERNEL_STACK_GUARD_SIZE_BYTES ==
                OS_TEST_PROCESS_SCHEDULER_EMPTY_VALUE &&
            stack_top_address - guard_page_address ==
                os::kernel::OS_KERNEL_PROCESS_KERNEL_STACK_STORAGE_SIZE_BYTES &&
            !os::kernel::ProcessKernelStackContains(
                process_index, guard_page_address,
                OS_TEST_PROCESS_SCHEDULER_ADDRESS_PROBE_SIZE_BYTES) &&
            os::kernel::ProcessKernelStackContains(
                process_index,
                guard_page_address + os::kernel::OS_KERNEL_PROCESS_KERNEL_STACK_GUARD_SIZE_BYTES,
                os::kernel::OS_KERNEL_PROCESS_KERNEL_STACK_SIZE_BYTES) &&
            !os::kernel::ProcessKernelStackContains(
                process_index, stack_top_address,
                OS_TEST_PROCESS_SCHEDULER_ADDRESS_PROBE_SIZE_BYTES) &&
            (process_index == OS_TEST_PROCESS_SCHEDULER_FIRST_INDEX ||
             guard_page_address == previous_stack_top_address);
        previous_stack_top_address = stack_top_address;
    }
    test_context.Expect(kernel_stack_layout_valid, OS_TEST_PROCESS_SCHEDULER_KERNEL_STACK_LAYOUT);
    return test_context.ExitCode();
}

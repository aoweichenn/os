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
    os::test::TestContext testContext{OS_TEST_PROCESS_SCHEDULER_SUITE_NAME};

    os::kernel::ProcessScheduler invalidScheduler{};
    uint64_t invalidProcessIndex = os::kernel::OS_KERNEL_PROCESS_INVALID_INDEX;
    uint64_t invalidProcessId = OS_TEST_PROCESS_SCHEDULER_EMPTY_VALUE;
    testContext.Expect(invalidScheduler.CreateProcess(invalidProcessIndex, invalidProcessId) ==
                           os::kernel::ProcessSchedulerStatus::NotInitialized,
                       OS_TEST_PROCESS_SCHEDULER_REQUIRES_INITIALIZATION);
    testContext.Expect(invalidScheduler.Initialize(OS_TEST_PROCESS_SCHEDULER_EMPTY_VALUE) ==
                           os::kernel::ProcessSchedulerStatus::InvalidQuantum,
                       OS_TEST_PROCESS_SCHEDULER_REJECTS_ZERO_QUANTUM);

    os::kernel::ProcessScheduler rollbackScheduler{};
    uint64_t discardedProcessIndex = os::kernel::OS_KERNEL_PROCESS_INVALID_INDEX;
    uint64_t discardedProcessId = OS_TEST_PROCESS_SCHEDULER_EMPTY_VALUE;
    uint64_t replacementProcessIndex = os::kernel::OS_KERNEL_PROCESS_INVALID_INDEX;
    uint64_t replacementProcessId = OS_TEST_PROCESS_SCHEDULER_EMPTY_VALUE;
    const bool rollbackSucceeded =
        rollbackScheduler.Initialize(OS_TEST_PROCESS_SCHEDULER_QUANTUM_TICKS) ==
            os::kernel::ProcessSchedulerStatus::Succeeded &&
        rollbackScheduler.CreateProcess(discardedProcessIndex, discardedProcessId) ==
            os::kernel::ProcessSchedulerStatus::Succeeded &&
        rollbackScheduler.DiscardReadyProcess(discardedProcessIndex) ==
            os::kernel::ProcessSchedulerStatus::Succeeded &&
        rollbackScheduler.CreateProcess(replacementProcessIndex, replacementProcessId) ==
            os::kernel::ProcessSchedulerStatus::Succeeded;
    testContext.Expect(rollbackSucceeded && discardedProcessIndex == replacementProcessIndex &&
                           discardedProcessId == OS_TEST_PROCESS_SCHEDULER_FIRST_PROCESS_ID &&
                           replacementProcessId == OS_TEST_PROCESS_SCHEDULER_SECOND_PROCESS_ID &&
                           rollbackScheduler.Statistics().createdProcessCount ==
                               OS_TEST_PROCESS_SCHEDULER_FIRST_PROCESS_ID,
                       OS_TEST_PROCESS_SCHEDULER_DISCARDS_READY_PROCESS);

    os::kernel::ProcessScheduler scheduler{};
    const bool initialized = scheduler.Initialize(OS_TEST_PROCESS_SCHEDULER_QUANTUM_TICKS) ==
                             os::kernel::ProcessSchedulerStatus::Succeeded;
    uint64_t firstProcessIndex = os::kernel::OS_KERNEL_PROCESS_INVALID_INDEX;
    uint64_t firstProcessId = OS_TEST_PROCESS_SCHEDULER_EMPTY_VALUE;
    bool creationSucceeded = scheduler.CreateProcess(firstProcessIndex, firstProcessId) ==
                             os::kernel::ProcessSchedulerStatus::Succeeded;
    uint64_t lastProcessId = firstProcessId;
    for (uint64_t processCount = OS_TEST_PROCESS_SCHEDULER_FIRST_PROCESS_ID;
         processCount < os::kernel::OS_KERNEL_PROCESS_CAPACITY; ++processCount) {
        uint64_t processIndex = os::kernel::OS_KERNEL_PROCESS_INVALID_INDEX;
        creationSucceeded =
            creationSucceeded && scheduler.CreateProcess(processIndex, lastProcessId) ==
                                     os::kernel::ProcessSchedulerStatus::Succeeded;
    }
    uint64_t overflowProcessIndex = os::kernel::OS_KERNEL_PROCESS_INVALID_INDEX;
    uint64_t overflowProcessId = OS_TEST_PROCESS_SCHEDULER_EMPTY_VALUE;
    testContext.Expect(initialized && creationSucceeded &&
                           firstProcessId == OS_TEST_PROCESS_SCHEDULER_FIRST_PROCESS_ID &&
                           lastProcessId == OS_TEST_PROCESS_SCHEDULER_LAST_PROCESS_ID &&
                           scheduler.CreateProcess(overflowProcessIndex, overflowProcessId) ==
                               os::kernel::ProcessSchedulerStatus::CapacityExhausted,
                       OS_TEST_PROCESS_SCHEDULER_CREATES_STABLE_IDENTIFIERS);

    os::kernel::ProcessSchedulingDecision decision{};
    bool schedulingSucceeded =
        scheduler.Start(decision) == os::kernel::ProcessSchedulerStatus::Succeeded &&
        decision.currentProcessIndex == firstProcessIndex && !decision.switched;
    for (uint64_t tickIndex = OS_TEST_PROCESS_SCHEDULER_FIRST_INDEX;
         tickIndex < OS_TEST_PROCESS_SCHEDULER_PREEMPTION_TICK_COUNT; ++tickIndex) {
        schedulingSucceeded =
            schedulingSucceeded &&
            scheduler.HandleTimerTick(decision) == os::kernel::ProcessSchedulerStatus::Succeeded;
    }
    os::kernel::ProcessSchedulerEntry firstEntry{};
    testContext.Expect(schedulingSucceeded && decision.switched &&
                           decision.previousProcessIndex == firstProcessIndex &&
                           decision.currentProcessIndex != firstProcessIndex &&
                           scheduler.ReadEntry(firstProcessIndex, firstEntry) ==
                               os::kernel::ProcessSchedulerStatus::Succeeded &&
                           firstEntry.state == os::kernel::ProcessState::Ready &&
                           firstEntry.runTickCount ==
                               OS_TEST_PROCESS_SCHEDULER_PREEMPTION_TICK_COUNT,
                       OS_TEST_PROCESS_SCHEDULER_PREEMPTS_AT_QUANTUM);

    bool terminationSucceeded = true;
    for (uint64_t terminationIndex = 0ULL;
         terminationIndex < os::kernel::OS_KERNEL_PROCESS_CAPACITY; ++terminationIndex) {
        terminationSucceeded =
            terminationSucceeded && scheduler.TerminateCurrentProcess(decision) ==
                                        os::kernel::ProcessSchedulerStatus::Succeeded;
    }
    const os::kernel::ProcessSchedulerStatistics statistics = scheduler.Statistics();
    testContext.Expect(
        terminationSucceeded && decision.completed && !scheduler.IsActive() &&
            statistics.createdProcessCount == os::kernel::OS_KERNEL_PROCESS_CAPACITY &&
            statistics.terminatedProcessCount == os::kernel::OS_KERNEL_PROCESS_CAPACITY &&
            statistics.preemptionCount == OS_TEST_PROCESS_SCHEDULER_EXPECTED_PREEMPTION_COUNT,
        OS_TEST_PROCESS_SCHEDULER_TERMINATES_TO_COMPLETION);

    os::kernel::ProcessScheduler blockingScheduler{};
    bool blockingSetupSucceeded =
        blockingScheduler.Initialize(OS_TEST_PROCESS_SCHEDULER_QUANTUM_TICKS) ==
        os::kernel::ProcessSchedulerStatus::Succeeded;
    for (uint64_t processIndex = OS_TEST_PROCESS_SCHEDULER_FIRST_INDEX;
         processIndex < OS_TEST_PROCESS_SCHEDULER_BLOCKING_PROCESS_COUNT; ++processIndex) {
        uint64_t createdProcessIndex = os::kernel::OS_KERNEL_PROCESS_INVALID_INDEX;
        uint64_t createdProcessId = OS_TEST_PROCESS_SCHEDULER_EMPTY_VALUE;
        blockingSetupSucceeded =
            blockingSetupSucceeded &&
            blockingScheduler.CreateProcess(createdProcessIndex, createdProcessId) ==
                os::kernel::ProcessSchedulerStatus::Succeeded;
    }
    os::kernel::ProcessSchedulingDecision blockingDecision{};
    blockingSetupSucceeded =
        blockingSetupSucceeded &&
        blockingScheduler.Start(blockingDecision) == os::kernel::ProcessSchedulerStatus::Succeeded;
    const uint64_t blockedReaderIndex = blockingDecision.currentProcessIndex;
    bool blockWakeSucceeded =
        blockingSetupSucceeded &&
        blockingScheduler.BlockCurrentProcess(os::kernel::ProcessWaitReason::PipeReadable,
                                              blockingDecision) ==
            os::kernel::ProcessSchedulerStatus::Succeeded &&
        blockingDecision.switched && blockingDecision.previousProcessIndex == blockedReaderIndex;
    os::kernel::ProcessSchedulerEntry blockedReaderEntry{};
    blockWakeSucceeded =
        blockWakeSucceeded &&
        blockingScheduler.ReadEntry(blockedReaderIndex, blockedReaderEntry) ==
            os::kernel::ProcessSchedulerStatus::Succeeded &&
        blockedReaderEntry.state == os::kernel::ProcessState::Blocked &&
        blockedReaderEntry.waitReason == os::kernel::ProcessWaitReason::PipeReadable;
    uint64_t wokenProcessCount = OS_TEST_PROCESS_SCHEDULER_EMPTY_VALUE;
    blockWakeSucceeded =
        blockWakeSucceeded &&
        blockingScheduler.WakeBlockedProcesses(os::kernel::ProcessWaitReason::PipeReadable,
                                               OS_TEST_PROCESS_SCHEDULER_SINGLE_WAKE_COUNT,
                                               wokenProcessCount) ==
            os::kernel::ProcessSchedulerStatus::Succeeded &&
        wokenProcessCount == OS_TEST_PROCESS_SCHEDULER_SINGLE_WAKE_COUNT &&
        blockingScheduler.ReadEntry(blockedReaderIndex, blockedReaderEntry) ==
            os::kernel::ProcessSchedulerStatus::Succeeded &&
        blockedReaderEntry.state == os::kernel::ProcessState::Ready &&
        blockedReaderEntry.waitReason == os::kernel::ProcessWaitReason::None &&
        blockedReaderEntry.blockCount == OS_TEST_PROCESS_SCHEDULER_SINGLE_WAKE_COUNT &&
        blockedReaderEntry.wakeupCount == OS_TEST_PROCESS_SCHEDULER_SINGLE_WAKE_COUNT;
    const os::kernel::ProcessSchedulerStatistics blockingStatistics =
        blockingScheduler.Statistics();
    testContext.Expect(
        blockWakeSucceeded &&
            blockingStatistics.blockCount == OS_TEST_PROCESS_SCHEDULER_SINGLE_WAKE_COUNT &&
            blockingStatistics.wakeupCount == OS_TEST_PROCESS_SCHEDULER_SINGLE_WAKE_COUNT,
        OS_TEST_PROCESS_SCHEDULER_BLOCKS_AND_WAKES);

    os::kernel::ProcessScheduler singleProcessScheduler{};
    uint64_t singleProcessIndex = os::kernel::OS_KERNEL_PROCESS_INVALID_INDEX;
    uint64_t singleProcessId = OS_TEST_PROCESS_SCHEDULER_EMPTY_VALUE;
    os::kernel::ProcessSchedulingDecision singleProcessDecision{};
    os::kernel::ProcessSchedulerEntry singleProcessEntry{};
    uint64_t singleWokenProcessCount =
        OS_TEST_PROCESS_SCHEDULER_EMPTY_VALUE;
    const bool idleBlockResumes =
        singleProcessScheduler.Initialize(OS_TEST_PROCESS_SCHEDULER_QUANTUM_TICKS) ==
            os::kernel::ProcessSchedulerStatus::Succeeded &&
        singleProcessScheduler.CreateProcess(singleProcessIndex, singleProcessId) ==
            os::kernel::ProcessSchedulerStatus::Succeeded &&
        singleProcessScheduler.Start(singleProcessDecision) ==
            os::kernel::ProcessSchedulerStatus::Succeeded &&
        singleProcessScheduler.BlockCurrentProcess(os::kernel::ProcessWaitReason::PipeReadable,
                                                   singleProcessDecision) ==
            os::kernel::ProcessSchedulerStatus::Succeeded &&
        !singleProcessDecision.switched &&
        singleProcessScheduler.CurrentProcessIndex() ==
            os::kernel::OS_KERNEL_PROCESS_INVALID_INDEX &&
        !singleProcessScheduler.IsActive() &&
        singleProcessScheduler.ReadEntry(singleProcessIndex,
                                         singleProcessEntry) ==
            os::kernel::ProcessSchedulerStatus::Succeeded &&
        singleProcessEntry.state == os::kernel::ProcessState::Blocked &&
        singleProcessScheduler.WakeBlockedProcesses(
            os::kernel::ProcessWaitReason::PipeReadable,
            OS_TEST_PROCESS_SCHEDULER_SINGLE_WAKE_COUNT,
            singleWokenProcessCount) ==
            os::kernel::ProcessSchedulerStatus::Succeeded &&
        singleWokenProcessCount ==
            OS_TEST_PROCESS_SCHEDULER_SINGLE_WAKE_COUNT &&
        singleProcessScheduler.Start(singleProcessDecision) ==
            os::kernel::ProcessSchedulerStatus::Succeeded &&
        singleProcessDecision.currentProcessIndex == singleProcessIndex &&
        singleProcessScheduler.IsActive();
    testContext.Expect(idleBlockResumes,
                       OS_TEST_PROCESS_SCHEDULER_ENTERS_IDLE);

    os::kernel::ProcessSchedulerEntry invalidEntry{};
    testContext.Expect(scheduler.ReadEntry(os::kernel::OS_KERNEL_PROCESS_CAPACITY, invalidEntry) ==
                           os::kernel::ProcessSchedulerStatus::InvalidProcessIndex,
                       OS_TEST_PROCESS_SCHEDULER_REJECTS_INVALID_INDEX);

    bool kernelStackLayoutValid = true;
    uint64_t previousStackTopAddress = OS_TEST_PROCESS_SCHEDULER_EMPTY_VALUE;
    for (uint64_t processIndex = OS_TEST_PROCESS_SCHEDULER_FIRST_INDEX;
         processIndex < os::kernel::OS_KERNEL_PROCESS_CAPACITY; ++processIndex) {
        const uint64_t guardPageAddress =
            os::kernel::ProcessKernelStackGuardPageAddress(processIndex);
        const uint64_t stackTopAddress = os::kernel::ProcessKernelStackTopAddress(processIndex);
        kernelStackLayoutValid =
            kernelStackLayoutValid &&
            guardPageAddress % os::kernel::OS_KERNEL_PROCESS_KERNEL_STACK_GUARD_SIZE_BYTES ==
                OS_TEST_PROCESS_SCHEDULER_EMPTY_VALUE &&
            stackTopAddress - guardPageAddress ==
                os::kernel::OS_KERNEL_PROCESS_KERNEL_STACK_STORAGE_SIZE_BYTES &&
            !os::kernel::ProcessKernelStackContains(
                processIndex, guardPageAddress,
                OS_TEST_PROCESS_SCHEDULER_ADDRESS_PROBE_SIZE_BYTES) &&
            os::kernel::ProcessKernelStackContains(
                processIndex,
                guardPageAddress + os::kernel::OS_KERNEL_PROCESS_KERNEL_STACK_GUARD_SIZE_BYTES,
                os::kernel::OS_KERNEL_PROCESS_KERNEL_STACK_SIZE_BYTES) &&
            !os::kernel::ProcessKernelStackContains(
                processIndex, stackTopAddress,
                OS_TEST_PROCESS_SCHEDULER_ADDRESS_PROBE_SIZE_BYTES) &&
            (processIndex == OS_TEST_PROCESS_SCHEDULER_FIRST_INDEX ||
             guardPageAddress == previousStackTopAddress);
        previousStackTopAddress = stackTopAddress;
    }
    testContext.Expect(kernelStackLayoutValid, OS_TEST_PROCESS_SCHEDULER_KERNEL_STACK_LAYOUT);
    return testContext.ExitCode();
}

#include "os/abi/signal.hpp"
#include "os/kernel/io/terminal.hpp"
#include "os/kernel/process/job_control.hpp"
#include "os/kernel/process/process_tree.hpp"
#include "os/kernel/process/thread_scheduler.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_TERMINAL_JOB_SUITE_NAME =
    "kernel/terminal_job_control/integration";
constexpr std::string_view OS_TEST_TERMINAL_JOB_SCHEDULING =
    "前台子进程停止后调度器必须回到 Shell，继续后原线程必须重新可运行";
constexpr std::string_view OS_TEST_TERMINAL_JOB_EVENTS =
    "停止、继续、退出三个父进程事件必须按顺序可观察且最终可回收";
constexpr std::string_view OS_TEST_TERMINAL_JOB_OWNERSHIP =
    "终端前台切换后必须拒绝 Shell 组并允许前台作业组读取";
constexpr uint64_t OS_TEST_TERMINAL_JOB_PROCESS_CAPACITY = 4ULL;
constexpr uint64_t OS_TEST_TERMINAL_JOB_THREAD_CAPACITY = 4ULL;
constexpr uint64_t OS_TEST_TERMINAL_JOB_THREADS_PER_PROCESS = 1ULL;
constexpr uint64_t OS_TEST_TERMINAL_JOB_QUANTUM_TICKS = 2ULL;
constexpr uint64_t OS_TEST_TERMINAL_JOB_INIT_ADDRESS_SPACE = 0x1000ULL;
constexpr uint64_t OS_TEST_TERMINAL_JOB_CHILD_ADDRESS_SPACE = 0x2000ULL;
constexpr uint64_t OS_TEST_TERMINAL_JOB_INIT_STACK_SLOT = 1ULL;
constexpr uint64_t OS_TEST_TERMINAL_JOB_CHILD_STACK_SLOT = 2ULL;
constexpr uint64_t OS_TEST_TERMINAL_JOB_USER_STACK = 0x70000000ULL;
constexpr uint64_t OS_TEST_TERMINAL_JOB_STOP_SIGNAL =
    os::abi::OS_ABI_SIGNAL_TERMINAL_STOP_NUMBER;

}

int main() {
    os::test::TestContext test_context{OS_TEST_TERMINAL_JOB_SUITE_NAME};
    os::kernel::ProcessEntry processes[OS_TEST_TERMINAL_JOB_PROCESS_CAPACITY]{};
    os::kernel::ThreadEntry threads[OS_TEST_TERMINAL_JOB_THREAD_CAPACITY]{};
    os::kernel::ThreadScheduler scheduler{};
    uint64_t init_process_index = UINT64_MAX;
    uint64_t child_process_index = UINT64_MAX;
    os::kernel::ProcessId init_process_id{};
    os::kernel::ProcessId child_process_id{};
    uint64_t init_thread_index = UINT64_MAX;
    uint64_t child_thread_index = UINT64_MAX;
    os::kernel::ThreadId init_thread_id{};
    os::kernel::ThreadId child_thread_id{};
    bool configured =
        scheduler.Initialize(processes, OS_TEST_TERMINAL_JOB_PROCESS_CAPACITY,
                             threads, OS_TEST_TERMINAL_JOB_THREAD_CAPACITY,
                             OS_TEST_TERMINAL_JOB_THREADS_PER_PROCESS,
                             OS_TEST_TERMINAL_JOB_QUANTUM_TICKS) ==
            os::kernel::ThreadSchedulerStatus::Succeeded &&
        scheduler.CreateProcess(OS_TEST_TERMINAL_JOB_INIT_ADDRESS_SPACE,
                                init_process_index, init_process_id) ==
            os::kernel::ThreadSchedulerStatus::Succeeded &&
        scheduler.CreateThread(init_process_index,
                               OS_TEST_TERMINAL_JOB_INIT_STACK_SLOT,
                               OS_TEST_TERMINAL_JOB_USER_STACK, 0ULL, 0ULL,
                               init_thread_index, init_thread_id) ==
            os::kernel::ThreadSchedulerStatus::Succeeded &&
        scheduler.CreateProcess(OS_TEST_TERMINAL_JOB_CHILD_ADDRESS_SPACE,
                                child_process_index, child_process_id) ==
            os::kernel::ThreadSchedulerStatus::Succeeded &&
        scheduler.CreateThread(child_process_index,
                               OS_TEST_TERMINAL_JOB_CHILD_STACK_SLOT,
                               OS_TEST_TERMINAL_JOB_USER_STACK, 0ULL, 0ULL,
                               child_thread_index, child_thread_id) ==
            os::kernel::ThreadSchedulerStatus::Succeeded;

    os::kernel::ProcessTreeEntry tree_storage[OS_TEST_TERMINAL_JOB_PROCESS_CAPACITY]{};
    os::kernel::ProcessTree tree{};
    os::kernel::JobControlProcessState
        job_storage[OS_TEST_TERMINAL_JOB_PROCESS_CAPACITY]{};
    os::kernel::JobControlManager jobs{};
    os::kernel::Terminal terminal{};
    terminal.Initialize(os::kernel::OS_KERNEL_TERMINAL_IDENTIFIER);
    configured =
        configured &&
        tree.Initialize(tree_storage, OS_TEST_TERMINAL_JOB_PROCESS_CAPACITY) ==
            os::kernel::ProcessTreeStatus::Succeeded &&
        tree.RegisterInit(init_process_index, init_process_id.value) ==
            os::kernel::ProcessTreeStatus::Succeeded &&
        tree.RegisterChild(child_process_index, child_process_id.value,
                           init_process_index) ==
            os::kernel::ProcessTreeStatus::Succeeded &&
        jobs.Initialize(job_storage, OS_TEST_TERMINAL_JOB_PROCESS_CAPACITY) ==
            os::kernel::JobControlStatus::Succeeded &&
        jobs.RegisterInit(init_process_index, init_process_id.value) ==
            os::kernel::JobControlStatus::Succeeded &&
        jobs.ForkProcess(init_process_index, child_process_index,
                         child_process_id.value) ==
            os::kernel::JobControlStatus::Succeeded &&
        jobs.SetProcessGroup(init_process_index, child_process_index,
                             child_process_id.value) ==
            os::kernel::JobControlStatus::Succeeded &&
        terminal.AcquireControllingSession(init_process_id.value,
                                           init_process_id.value,
                                           init_process_id.value) ==
            os::kernel::TerminalStatus::Succeeded &&
        terminal.SetForegroundProcessGroup(init_process_id.value,
                                           child_process_id.value) ==
            os::kernel::TerminalStatus::Succeeded;

    os::kernel::ThreadSchedulingDecision decision{};
    configured =
        configured &&
        scheduler.Start(decision) == os::kernel::ThreadSchedulerStatus::Succeeded &&
        scheduler.YieldCurrentThread(decision) ==
            os::kernel::ThreadSchedulerStatus::Succeeded &&
        decision.current_thread_index == child_thread_index &&
        tree.MarkStopped(child_process_index,
                         OS_TEST_TERMINAL_JOB_STOP_SIGNAL) ==
            os::kernel::ProcessTreeStatus::Succeeded &&
        scheduler.StopCurrentProcess(child_process_index, decision) ==
            os::kernel::ThreadSchedulerStatus::Succeeded &&
        decision.current_thread_index == init_thread_index;
    test_context.Expect(
        configured &&
            scheduler.ContinueProcess(child_process_index) ==
                os::kernel::ThreadSchedulerStatus::Succeeded &&
            tree.MarkContinued(child_process_index) ==
                os::kernel::ProcessTreeStatus::Succeeded &&
            scheduler.Validate() == os::kernel::ThreadSchedulerStatus::Succeeded,
        OS_TEST_TERMINAL_JOB_SCHEDULING);

    test_context.Expect(
        !terminal.CanRead(init_process_id.value, init_process_id.value) &&
            terminal.CanRead(init_process_id.value, child_process_id.value),
        OS_TEST_TERMINAL_JOB_OWNERSHIP);

    uint64_t reparented_process_count = 0ULL;
    uint64_t terminated_thread_count = 0ULL;
    const bool lifecycle_completed =
        scheduler.YieldCurrentThread(decision) ==
            os::kernel::ThreadSchedulerStatus::Succeeded &&
        decision.current_thread_index == child_thread_index &&
        tree.MarkExited(
            child_process_index,
            os::kernel::ProcessTreeExitStatus{
                .termination_reason =
                    os::kernel::ProcessTreeTerminationReason::Exited,
                .exit_code = 0LL,
                .exception_vector = 0ULL,
            },
            reparented_process_count) ==
            os::kernel::ProcessTreeStatus::Succeeded &&
        scheduler.TerminateCurrentProcess(child_process_index, decision,
                                          terminated_thread_count) ==
            os::kernel::ThreadSchedulerStatus::Succeeded;
    os::kernel::ProcessTreeWaitEventResult event{};
    const bool stopped_observed =
        tree.TryWaitEvent(init_process_index, child_process_id.value,
                          os::kernel::OS_KERNEL_PROCESS_TREE_WAIT_VALID_FLAG_MASK,
                          event) == os::kernel::ProcessTreeStatus::Succeeded &&
        event.event_type == os::kernel::ProcessTreeEventType::Stopped;
    const bool continued_observed =
        tree.TryWaitEvent(init_process_index, child_process_id.value,
                          os::kernel::OS_KERNEL_PROCESS_TREE_WAIT_VALID_FLAG_MASK,
                          event) == os::kernel::ProcessTreeStatus::Succeeded &&
        event.event_type == os::kernel::ProcessTreeEventType::Continued;
    const bool exited_observed =
        tree.TryWaitEvent(init_process_index, child_process_id.value,
                          os::kernel::OS_KERNEL_PROCESS_TREE_WAIT_VALID_FLAG_MASK,
                          event) == os::kernel::ProcessTreeStatus::Succeeded &&
        event.event_type == os::kernel::ProcessTreeEventType::Exited;
    test_context.Expect(lifecycle_completed && stopped_observed &&
                            continued_observed && exited_observed &&
                            tree.Validate() ==
                                os::kernel::ProcessTreeStatus::Succeeded,
                        OS_TEST_TERMINAL_JOB_EVENTS);
    return test_context.ExitCode();
}

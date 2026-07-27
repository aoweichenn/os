#include "os/kernel/process/process_tree.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_PROCESS_TREE_SUITE_NAME = "kernel/process_tree/unit";
constexpr std::string_view OS_TEST_PROCESS_TREE_INITIALIZATION_MESSAGE =
    "进程树必须拒绝非法初始化参数并只允许 PID 1 注册为 init";
constexpr std::string_view OS_TEST_PROCESS_TREE_WAIT_MESSAGE =
    "wait 必须区分运行中子进程、僵尸子进程和不存在的子进程";
constexpr std::string_view OS_TEST_PROCESS_TREE_REPARENT_MESSAGE =
    "父进程退出时必须把仍存续的后代重新托管给 PID 1";
constexpr std::string_view OS_TEST_PROCESS_TREE_INIT_MESSAGE =
    "PID 1 必须在全部子进程回收后才能退出并由内核最终回收";
constexpr std::string_view OS_TEST_PROCESS_TREE_STATISTICS_MESSAGE =
    "进程树生命周期统计必须与最终空树状态严格守恒";

constexpr uint64_t OS_TEST_PROCESS_TREE_CAPACITY = 4ULL;
constexpr uint64_t OS_TEST_PROCESS_TREE_INIT_INDEX = 0ULL;
constexpr uint64_t OS_TEST_PROCESS_TREE_PARENT_INDEX = 1ULL;
constexpr uint64_t OS_TEST_PROCESS_TREE_CHILD_INDEX = 2ULL;
constexpr uint64_t OS_TEST_PROCESS_TREE_INIT_PROCESS_ID = 1ULL;
constexpr uint64_t OS_TEST_PROCESS_TREE_PARENT_PROCESS_ID = 2ULL;
constexpr uint64_t OS_TEST_PROCESS_TREE_CHILD_PROCESS_ID = 3ULL;
constexpr uint64_t OS_TEST_PROCESS_TREE_INVALID_INIT_PROCESS_ID = 7ULL;
constexpr uint64_t OS_TEST_PROCESS_TREE_EXPECTED_REPARENT_COUNT = 1ULL;
constexpr uint64_t OS_TEST_PROCESS_TREE_EXPECTED_REGISTERED_COUNT = 3ULL;
constexpr uint64_t OS_TEST_PROCESS_TREE_EXPECTED_WAIT_ATTEMPT_COUNT = 4ULL;
constexpr uint64_t OS_TEST_PROCESS_TREE_EXPECTED_WAIT_SUCCESS_COUNT = 2ULL;
constexpr uint64_t OS_TEST_PROCESS_TREE_EXPECTED_WAIT_BLOCK_COUNT = 1ULL;
constexpr uint64_t OS_TEST_PROCESS_TREE_EXPECTED_WAIT_NO_CHILD_COUNT = 1ULL;
constexpr uint64_t OS_TEST_PROCESS_TREE_EMPTY_VALUE = 0ULL;
constexpr int64_t OS_TEST_PROCESS_TREE_PARENT_EXIT_CODE = 23LL;
constexpr int64_t OS_TEST_PROCESS_TREE_CHILD_EXIT_CODE = 42LL;
constexpr uint64_t OS_TEST_PROCESS_TREE_CHILD_EXCEPTION_VECTOR = 14ULL;

[[nodiscard]] os::kernel::ProcessTreeExitStatus ParentExitStatus() noexcept {
    return os::kernel::ProcessTreeExitStatus{
        .termination_reason = os::kernel::ProcessTreeTerminationReason::Exited,
        .exit_code = OS_TEST_PROCESS_TREE_PARENT_EXIT_CODE,
        .exception_vector = OS_TEST_PROCESS_TREE_EMPTY_VALUE,
    };
}

[[nodiscard]] os::kernel::ProcessTreeExitStatus ChildExitStatus() noexcept {
    return os::kernel::ProcessTreeExitStatus{
        .termination_reason = os::kernel::ProcessTreeTerminationReason::Exception,
        .exit_code = OS_TEST_PROCESS_TREE_CHILD_EXIT_CODE,
        .exception_vector = OS_TEST_PROCESS_TREE_CHILD_EXCEPTION_VECTOR,
    };
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_PROCESS_TREE_SUITE_NAME};
    os::kernel::ProcessTreeEntry entries[OS_TEST_PROCESS_TREE_CAPACITY]{};
    os::kernel::ProcessTree tree{};
    test_context.Expect(tree.Initialize(nullptr, OS_TEST_PROCESS_TREE_CAPACITY) ==
                                os::kernel::ProcessTreeStatus::InvalidStorage &&
                            tree.Initialize(entries, OS_TEST_PROCESS_TREE_EMPTY_VALUE) ==
                                os::kernel::ProcessTreeStatus::InvalidCapacity &&
                            tree.Initialize(entries, OS_TEST_PROCESS_TREE_CAPACITY) ==
                                os::kernel::ProcessTreeStatus::Succeeded &&
                            tree.RegisterInit(OS_TEST_PROCESS_TREE_INIT_INDEX,
                                              OS_TEST_PROCESS_TREE_INVALID_INIT_PROCESS_ID) ==
                                os::kernel::ProcessTreeStatus::InvalidProcessId &&
                            tree.RegisterInit(OS_TEST_PROCESS_TREE_INIT_INDEX,
                                              OS_TEST_PROCESS_TREE_INIT_PROCESS_ID) ==
                                os::kernel::ProcessTreeStatus::Succeeded,
                        OS_TEST_PROCESS_TREE_INITIALIZATION_MESSAGE);

    test_context.Expect(tree.RegisterChild(OS_TEST_PROCESS_TREE_PARENT_INDEX,
                                           OS_TEST_PROCESS_TREE_PARENT_PROCESS_ID,
                                           OS_TEST_PROCESS_TREE_INIT_INDEX) ==
                                os::kernel::ProcessTreeStatus::Succeeded &&
                            tree.RegisterChild(OS_TEST_PROCESS_TREE_CHILD_INDEX,
                                               OS_TEST_PROCESS_TREE_CHILD_PROCESS_ID,
                                               OS_TEST_PROCESS_TREE_PARENT_INDEX) ==
                                os::kernel::ProcessTreeStatus::Succeeded,
                        OS_TEST_PROCESS_TREE_REPARENT_MESSAGE);

    os::kernel::ProcessTreeWaitResult wait_result{};
    uint64_t reparented_process_count = OS_TEST_PROCESS_TREE_EMPTY_VALUE;
    test_context.Expect(
        tree.TryWait(OS_TEST_PROCESS_TREE_INIT_INDEX, OS_TEST_PROCESS_TREE_PARENT_PROCESS_ID,
                     wait_result) == os::kernel::ProcessTreeStatus::ChildStillRunning &&
            tree.MarkExited(OS_TEST_PROCESS_TREE_INIT_INDEX, ParentExitStatus(),
                            reparented_process_count) ==
                os::kernel::ProcessTreeStatus::ProcessHasChildren &&
            tree.MarkExited(OS_TEST_PROCESS_TREE_PARENT_INDEX, ParentExitStatus(),
                            reparented_process_count) == os::kernel::ProcessTreeStatus::Succeeded &&
            reparented_process_count == OS_TEST_PROCESS_TREE_EXPECTED_REPARENT_COUNT,
        OS_TEST_PROCESS_TREE_REPARENT_MESSAGE);

    os::kernel::ProcessTreeEntry child_entry{};
    test_context.Expect(
        tree.Read(OS_TEST_PROCESS_TREE_CHILD_INDEX, child_entry) ==
                os::kernel::ProcessTreeStatus::Succeeded &&
            child_entry.parent_process_index == OS_TEST_PROCESS_TREE_INIT_INDEX &&
            tree.TryWait(OS_TEST_PROCESS_TREE_INIT_INDEX, OS_TEST_PROCESS_TREE_PARENT_PROCESS_ID,
                         wait_result) == os::kernel::ProcessTreeStatus::Succeeded &&
            wait_result.process_id == OS_TEST_PROCESS_TREE_PARENT_PROCESS_ID &&
            wait_result.parent_process_id == OS_TEST_PROCESS_TREE_INIT_PROCESS_ID &&
            wait_result.exit_status.exit_code == OS_TEST_PROCESS_TREE_PARENT_EXIT_CODE,
        OS_TEST_PROCESS_TREE_WAIT_MESSAGE);

    test_context.Expect(
        tree.MarkExited(OS_TEST_PROCESS_TREE_CHILD_INDEX, ChildExitStatus(),
                        reparented_process_count) == os::kernel::ProcessTreeStatus::Succeeded &&
            tree.TryWait(OS_TEST_PROCESS_TREE_INIT_INDEX,
                         os::kernel::OS_KERNEL_PROCESS_TREE_WAIT_ANY_PROCESS_ID,
                         wait_result) == os::kernel::ProcessTreeStatus::Succeeded &&
            wait_result.process_id == OS_TEST_PROCESS_TREE_CHILD_PROCESS_ID &&
            wait_result.parent_process_id == OS_TEST_PROCESS_TREE_INIT_PROCESS_ID &&
            wait_result.exit_status.termination_reason ==
                os::kernel::ProcessTreeTerminationReason::Exception &&
            wait_result.exit_status.exception_vector ==
                OS_TEST_PROCESS_TREE_CHILD_EXCEPTION_VECTOR &&
            tree.TryWait(OS_TEST_PROCESS_TREE_INIT_INDEX,
                         os::kernel::OS_KERNEL_PROCESS_TREE_WAIT_ANY_PROCESS_ID,
                         wait_result) == os::kernel::ProcessTreeStatus::NoMatchingChild,
        OS_TEST_PROCESS_TREE_WAIT_MESSAGE);

    test_context.Expect(
        tree.MarkExited(OS_TEST_PROCESS_TREE_INIT_INDEX, ParentExitStatus(),
                        reparented_process_count) == os::kernel::ProcessTreeStatus::Succeeded &&
            tree.CollectInit(wait_result) == os::kernel::ProcessTreeStatus::Succeeded &&
            wait_result.process_id == OS_TEST_PROCESS_TREE_INIT_PROCESS_ID,
        OS_TEST_PROCESS_TREE_INIT_MESSAGE);

    const os::kernel::ProcessTreeStatistics statistics = tree.Statistics();
    test_context.Expect(
        tree.Validate() == os::kernel::ProcessTreeStatus::Succeeded &&
            tree.InitProcessIndex() == os::kernel::OS_KERNEL_PROCESS_TREE_INVALID_INDEX &&
            statistics.active_process_count == OS_TEST_PROCESS_TREE_EMPTY_VALUE &&
            statistics.alive_process_count == OS_TEST_PROCESS_TREE_EMPTY_VALUE &&
            statistics.zombie_process_count == OS_TEST_PROCESS_TREE_EMPTY_VALUE &&
            statistics.registered_process_count == OS_TEST_PROCESS_TREE_EXPECTED_REGISTERED_COUNT &&
            statistics.exited_process_count == OS_TEST_PROCESS_TREE_EXPECTED_REGISTERED_COUNT &&
            statistics.collected_process_count == OS_TEST_PROCESS_TREE_EXPECTED_REGISTERED_COUNT &&
            statistics.reparented_process_count == OS_TEST_PROCESS_TREE_EXPECTED_REPARENT_COUNT &&
            statistics.wait_attempt_count == OS_TEST_PROCESS_TREE_EXPECTED_WAIT_ATTEMPT_COUNT &&
            statistics.wait_success_count == OS_TEST_PROCESS_TREE_EXPECTED_WAIT_SUCCESS_COUNT &&
            statistics.wait_block_count == OS_TEST_PROCESS_TREE_EXPECTED_WAIT_BLOCK_COUNT &&
            statistics.wait_no_child_count == OS_TEST_PROCESS_TREE_EXPECTED_WAIT_NO_CHILD_COUNT,
        OS_TEST_PROCESS_TREE_STATISTICS_MESSAGE);
    return test_context.ExitCode();
}

#include "os/kernel/process/process_tree.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_PROCESS_LIFECYCLE_SUITE_NAME =
    "kernel/process_lifecycle/integration";
constexpr std::string_view OS_TEST_PROCESS_LIFECYCLE_CYCLE_MESSAGE =
    "4096 轮父子退出、重设父进程与 wait 回收必须始终恢复同一资源基线";
constexpr std::string_view OS_TEST_PROCESS_LIFECYCLE_FINAL_MESSAGE =
    "压力结束后 init、僵尸与活动进程计数必须全部归零";

constexpr uint64_t OS_TEST_PROCESS_LIFECYCLE_CAPACITY = 3ULL;
constexpr uint64_t OS_TEST_PROCESS_LIFECYCLE_INIT_INDEX = 0ULL;
constexpr uint64_t OS_TEST_PROCESS_LIFECYCLE_PARENT_INDEX = 1ULL;
constexpr uint64_t OS_TEST_PROCESS_LIFECYCLE_CHILD_INDEX = 2ULL;
constexpr uint64_t OS_TEST_PROCESS_LIFECYCLE_INIT_PROCESS_ID = 1ULL;
constexpr uint64_t OS_TEST_PROCESS_LIFECYCLE_FIRST_DYNAMIC_PROCESS_ID = 2ULL;
constexpr uint64_t OS_TEST_PROCESS_LIFECYCLE_PROCESS_ID_STRIDE = 2ULL;
constexpr uint64_t OS_TEST_PROCESS_LIFECYCLE_CYCLE_COUNT = 4096ULL;
constexpr uint64_t OS_TEST_PROCESS_LIFECYCLE_EXPECTED_DYNAMIC_PROCESS_COUNT =
    OS_TEST_PROCESS_LIFECYCLE_CYCLE_COUNT * OS_TEST_PROCESS_LIFECYCLE_PROCESS_ID_STRIDE;
constexpr uint64_t OS_TEST_PROCESS_LIFECYCLE_EXPECTED_TOTAL_PROCESS_COUNT =
    OS_TEST_PROCESS_LIFECYCLE_EXPECTED_DYNAMIC_PROCESS_COUNT +
    OS_TEST_PROCESS_LIFECYCLE_INIT_PROCESS_ID;
constexpr uint64_t OS_TEST_PROCESS_LIFECYCLE_EXPECTED_REPARENT_PER_CYCLE = 1ULL;
constexpr uint64_t OS_TEST_PROCESS_LIFECYCLE_EMPTY_COUNT = 0ULL;
constexpr int64_t OS_TEST_PROCESS_LIFECYCLE_SUCCESS_EXIT_CODE = 0LL;

[[nodiscard]] os::kernel::ProcessTreeExitStatus SuccessExitStatus() noexcept {
    return os::kernel::ProcessTreeExitStatus{
        .termination_reason = os::kernel::ProcessTreeTerminationReason::Exited,
        .exit_code = OS_TEST_PROCESS_LIFECYCLE_SUCCESS_EXIT_CODE,
        .exception_vector = OS_TEST_PROCESS_LIFECYCLE_EMPTY_COUNT,
    };
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_PROCESS_LIFECYCLE_SUITE_NAME};
    os::kernel::ProcessTreeEntry entries[OS_TEST_PROCESS_LIFECYCLE_CAPACITY]{};
    os::kernel::ProcessTree tree{};
    bool lifecycle_valid = tree.Initialize(entries, OS_TEST_PROCESS_LIFECYCLE_CAPACITY) ==
                               os::kernel::ProcessTreeStatus::Succeeded &&
                           tree.RegisterInit(OS_TEST_PROCESS_LIFECYCLE_INIT_INDEX,
                                             OS_TEST_PROCESS_LIFECYCLE_INIT_PROCESS_ID) ==
                               os::kernel::ProcessTreeStatus::Succeeded;

    for (uint64_t cycle = OS_TEST_PROCESS_LIFECYCLE_EMPTY_COUNT;
         cycle < OS_TEST_PROCESS_LIFECYCLE_CYCLE_COUNT; ++cycle) {
        const uint64_t parent_process_id = OS_TEST_PROCESS_LIFECYCLE_FIRST_DYNAMIC_PROCESS_ID +
                                           cycle * OS_TEST_PROCESS_LIFECYCLE_PROCESS_ID_STRIDE;
        const uint64_t child_process_id =
            parent_process_id + OS_TEST_PROCESS_LIFECYCLE_EXPECTED_REPARENT_PER_CYCLE;
        uint64_t reparented_process_count = OS_TEST_PROCESS_LIFECYCLE_EMPTY_COUNT;
        os::kernel::ProcessTreeWaitResult wait_result{};
        lifecycle_valid =
            tree.RegisterChild(OS_TEST_PROCESS_LIFECYCLE_PARENT_INDEX, parent_process_id,
                               OS_TEST_PROCESS_LIFECYCLE_INIT_INDEX) ==
                os::kernel::ProcessTreeStatus::Succeeded &&
            tree.RegisterChild(OS_TEST_PROCESS_LIFECYCLE_CHILD_INDEX, child_process_id,
                               OS_TEST_PROCESS_LIFECYCLE_PARENT_INDEX) ==
                os::kernel::ProcessTreeStatus::Succeeded &&
            tree.MarkExited(OS_TEST_PROCESS_LIFECYCLE_PARENT_INDEX, SuccessExitStatus(),
                            reparented_process_count) == os::kernel::ProcessTreeStatus::Succeeded &&
            reparented_process_count == OS_TEST_PROCESS_LIFECYCLE_EXPECTED_REPARENT_PER_CYCLE &&
            tree.TryWait(OS_TEST_PROCESS_LIFECYCLE_INIT_INDEX, parent_process_id, wait_result) ==
                os::kernel::ProcessTreeStatus::Succeeded &&
            wait_result.process_id == parent_process_id &&
            tree.MarkExited(OS_TEST_PROCESS_LIFECYCLE_CHILD_INDEX, SuccessExitStatus(),
                            reparented_process_count) == os::kernel::ProcessTreeStatus::Succeeded &&
            tree.TryWait(OS_TEST_PROCESS_LIFECYCLE_INIT_INDEX,
                         os::kernel::OS_KERNEL_PROCESS_TREE_WAIT_ANY_PROCESS_ID,
                         wait_result) == os::kernel::ProcessTreeStatus::Succeeded &&
            wait_result.process_id == child_process_id &&
            tree.Validate() == os::kernel::ProcessTreeStatus::Succeeded && lifecycle_valid;
    }
    test_context.Expect(lifecycle_valid, OS_TEST_PROCESS_LIFECYCLE_CYCLE_MESSAGE);

    uint64_t reparented_process_count = OS_TEST_PROCESS_LIFECYCLE_EMPTY_COUNT;
    os::kernel::ProcessTreeWaitResult init_result{};
    const bool init_collected =
        tree.MarkExited(OS_TEST_PROCESS_LIFECYCLE_INIT_INDEX, SuccessExitStatus(),
                        reparented_process_count) == os::kernel::ProcessTreeStatus::Succeeded &&
        tree.CollectInit(init_result) == os::kernel::ProcessTreeStatus::Succeeded;
    const os::kernel::ProcessTreeStatistics statistics = tree.Statistics();
    test_context.Expect(
        init_collected && tree.Validate() == os::kernel::ProcessTreeStatus::Succeeded &&
            statistics.active_process_count == OS_TEST_PROCESS_LIFECYCLE_EMPTY_COUNT &&
            statistics.alive_process_count == OS_TEST_PROCESS_LIFECYCLE_EMPTY_COUNT &&
            statistics.zombie_process_count == OS_TEST_PROCESS_LIFECYCLE_EMPTY_COUNT &&
            statistics.registered_process_count ==
                OS_TEST_PROCESS_LIFECYCLE_EXPECTED_TOTAL_PROCESS_COUNT &&
            statistics.exited_process_count ==
                OS_TEST_PROCESS_LIFECYCLE_EXPECTED_TOTAL_PROCESS_COUNT &&
            statistics.collected_process_count ==
                OS_TEST_PROCESS_LIFECYCLE_EXPECTED_TOTAL_PROCESS_COUNT &&
            statistics.reparented_process_count == OS_TEST_PROCESS_LIFECYCLE_CYCLE_COUNT &&
            statistics.wait_success_count ==
                OS_TEST_PROCESS_LIFECYCLE_EXPECTED_DYNAMIC_PROCESS_COUNT,
        OS_TEST_PROCESS_LIFECYCLE_FINAL_MESSAGE);
    return test_context.ExitCode();
}

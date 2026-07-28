#include "os/kernel/process/job_control.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_JOB_CONTROL_SUITE_NAME = "kernel/job_control/unit";
constexpr std::string_view OS_TEST_JOB_CONTROL_SESSION =
    "PID 1 必须建立 SID=PGID=PID 的首会话，fork 子进程必须继承";
constexpr std::string_view OS_TEST_JOB_CONTROL_GROUP =
    "父进程必须能建立子进程组，同会话成员可加入且会话领导者不可移动";
constexpr std::string_view OS_TEST_JOB_CONTROL_LIFECYCLE =
    "移除全部进程后统计与结构不变量必须同时归零";
constexpr uint64_t OS_TEST_JOB_CONTROL_CAPACITY = 8ULL;
constexpr uint64_t OS_TEST_JOB_CONTROL_INIT_INDEX = 0ULL;
constexpr uint64_t OS_TEST_JOB_CONTROL_INIT_PROCESS_ID = 1ULL;
constexpr uint64_t OS_TEST_JOB_CONTROL_FIRST_CHILD_INDEX = 1ULL;
constexpr uint64_t OS_TEST_JOB_CONTROL_FIRST_CHILD_PROCESS_ID = 2ULL;
constexpr uint64_t OS_TEST_JOB_CONTROL_SECOND_CHILD_INDEX = 2ULL;
constexpr uint64_t OS_TEST_JOB_CONTROL_SECOND_CHILD_PROCESS_ID = 3ULL;

}

int main() {
    os::test::TestContext test_context{OS_TEST_JOB_CONTROL_SUITE_NAME};
    os::kernel::JobControlProcessState storage[OS_TEST_JOB_CONTROL_CAPACITY]{};
    os::kernel::JobControlManager manager{};
    const bool initialized =
        manager.Initialize(storage, OS_TEST_JOB_CONTROL_CAPACITY) ==
            os::kernel::JobControlStatus::Succeeded &&
        manager.RegisterInit(OS_TEST_JOB_CONTROL_INIT_INDEX,
                             OS_TEST_JOB_CONTROL_INIT_PROCESS_ID) ==
            os::kernel::JobControlStatus::Succeeded &&
        manager.ForkProcess(OS_TEST_JOB_CONTROL_INIT_INDEX,
                            OS_TEST_JOB_CONTROL_FIRST_CHILD_INDEX,
                            OS_TEST_JOB_CONTROL_FIRST_CHILD_PROCESS_ID) ==
            os::kernel::JobControlStatus::Succeeded &&
        manager.ForkProcess(OS_TEST_JOB_CONTROL_INIT_INDEX,
                            OS_TEST_JOB_CONTROL_SECOND_CHILD_INDEX,
                            OS_TEST_JOB_CONTROL_SECOND_CHILD_PROCESS_ID) ==
            os::kernel::JobControlStatus::Succeeded;
    os::kernel::JobControlProcessState first_child{};
    const bool inherited =
        manager.ReadProcess(OS_TEST_JOB_CONTROL_FIRST_CHILD_INDEX, first_child) ==
            os::kernel::JobControlStatus::Succeeded &&
        first_child.session_id == OS_TEST_JOB_CONTROL_INIT_PROCESS_ID &&
        first_child.process_group_id == OS_TEST_JOB_CONTROL_INIT_PROCESS_ID &&
        !first_child.session_leader;
    test_context.Expect(initialized && inherited, OS_TEST_JOB_CONTROL_SESSION);

    const bool group_created =
        manager.SetProcessGroup(OS_TEST_JOB_CONTROL_INIT_INDEX,
                                OS_TEST_JOB_CONTROL_FIRST_CHILD_INDEX,
                                OS_TEST_JOB_CONTROL_FIRST_CHILD_PROCESS_ID) ==
            os::kernel::JobControlStatus::Succeeded &&
        manager.SetProcessGroup(OS_TEST_JOB_CONTROL_INIT_INDEX,
                                OS_TEST_JOB_CONTROL_SECOND_CHILD_INDEX,
                                OS_TEST_JOB_CONTROL_FIRST_CHILD_PROCESS_ID) ==
            os::kernel::JobControlStatus::Succeeded &&
        manager.GroupBelongsToSession(OS_TEST_JOB_CONTROL_FIRST_CHILD_PROCESS_ID,
                                      OS_TEST_JOB_CONTROL_INIT_PROCESS_ID);
    const bool leader_protected =
        manager.SetProcessGroup(OS_TEST_JOB_CONTROL_INIT_INDEX,
                                OS_TEST_JOB_CONTROL_INIT_INDEX,
                                OS_TEST_JOB_CONTROL_FIRST_CHILD_PROCESS_ID) ==
        os::kernel::JobControlStatus::SessionLeader;
    test_context.Expect(group_created && leader_protected &&
                            manager.Validate() ==
                                os::kernel::JobControlStatus::Succeeded,
                        OS_TEST_JOB_CONTROL_GROUP);

    const bool removed =
        manager.RemoveProcess(OS_TEST_JOB_CONTROL_SECOND_CHILD_INDEX) ==
            os::kernel::JobControlStatus::Succeeded &&
        manager.RemoveProcess(OS_TEST_JOB_CONTROL_FIRST_CHILD_INDEX) ==
            os::kernel::JobControlStatus::Succeeded &&
        manager.RemoveProcess(OS_TEST_JOB_CONTROL_INIT_INDEX) ==
            os::kernel::JobControlStatus::Succeeded;
    const os::kernel::JobControlStatistics statistics = manager.Statistics();
    test_context.Expect(
        removed && statistics.active_process_count == 0ULL &&
            statistics.active_process_group_count == 0ULL &&
            manager.Validate() == os::kernel::JobControlStatus::Succeeded,
        OS_TEST_JOB_CONTROL_LIFECYCLE);
    return test_context.ExitCode();
}

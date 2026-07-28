#include "os/kernel/process/job_control.hpp"
#include "test_context.hpp"

#include <random>
#include <string_view>

namespace {

constexpr std::string_view OS_TEST_JOB_CONTROL_RANDOM_SUITE_NAME =
    "kernel/job_control/randomized";
constexpr std::string_view OS_TEST_JOB_CONTROL_RANDOM_INVARIANT =
    "十万次同会话进程组迁移必须保持 SID、PGID、唯一 PID 与统计守恒";
constexpr os::test::RandomSeed OS_TEST_JOB_CONTROL_RANDOM_SEED =
    0x4A4F424354524C15ULL;
constexpr os::test::TestCount OS_TEST_JOB_CONTROL_RANDOM_ITERATION_COUNT = 100000ULL;
constexpr uint64_t OS_TEST_JOB_CONTROL_RANDOM_CAPACITY = 64ULL;
constexpr uint64_t OS_TEST_JOB_CONTROL_RANDOM_INIT_INDEX = 0ULL;
constexpr uint64_t OS_TEST_JOB_CONTROL_RANDOM_INIT_PROCESS_ID = 1ULL;
constexpr uint64_t OS_TEST_JOB_CONTROL_RANDOM_FIRST_CHILD_INDEX = 1ULL;
constexpr uint64_t OS_TEST_JOB_CONTROL_RANDOM_PROCESS_ID_OFFSET = 1ULL;

}

int main() {
    os::test::TestContext test_context{OS_TEST_JOB_CONTROL_RANDOM_SUITE_NAME};
    os::kernel::JobControlProcessState storage[OS_TEST_JOB_CONTROL_RANDOM_CAPACITY]{};
    os::kernel::JobControlManager manager{};
    bool valid =
        manager.Initialize(storage, OS_TEST_JOB_CONTROL_RANDOM_CAPACITY) ==
            os::kernel::JobControlStatus::Succeeded &&
        manager.RegisterInit(OS_TEST_JOB_CONTROL_RANDOM_INIT_INDEX,
                             OS_TEST_JOB_CONTROL_RANDOM_INIT_PROCESS_ID) ==
            os::kernel::JobControlStatus::Succeeded;
    for (uint64_t process_index = OS_TEST_JOB_CONTROL_RANDOM_FIRST_CHILD_INDEX;
         process_index < OS_TEST_JOB_CONTROL_RANDOM_CAPACITY; ++process_index) {
        valid =
            valid &&
            manager.ForkProcess(
                OS_TEST_JOB_CONTROL_RANDOM_INIT_INDEX, process_index,
                process_index + OS_TEST_JOB_CONTROL_RANDOM_PROCESS_ID_OFFSET) ==
                os::kernel::JobControlStatus::Succeeded;
    }

    std::mt19937_64 generator{OS_TEST_JOB_CONTROL_RANDOM_SEED};
    std::uniform_int_distribution<uint64_t> child_distribution{
        OS_TEST_JOB_CONTROL_RANDOM_FIRST_CHILD_INDEX,
        OS_TEST_JOB_CONTROL_RANDOM_CAPACITY -
            OS_TEST_JOB_CONTROL_RANDOM_PROCESS_ID_OFFSET};
    for (os::test::TestCount iteration = 0ULL;
         iteration < OS_TEST_JOB_CONTROL_RANDOM_ITERATION_COUNT; ++iteration) {
        const uint64_t target_index = child_distribution(generator);
        const uint64_t group_source_index = child_distribution(generator);
        os::kernel::JobControlProcessState group_source{};
        const bool state_read =
            manager.ReadProcess(group_source_index, group_source) ==
            os::kernel::JobControlStatus::Succeeded;
        const uint64_t target_group_id =
            (generator() & 1ULL) == 0ULL
                ? target_index + OS_TEST_JOB_CONTROL_RANDOM_PROCESS_ID_OFFSET
                : group_source.process_group_id;
        const bool changed =
            state_read &&
            manager.SetProcessGroup(OS_TEST_JOB_CONTROL_RANDOM_INIT_INDEX,
                                    target_index, target_group_id) ==
                os::kernel::JobControlStatus::Succeeded;
        const bool iteration_valid =
            valid && changed &&
            manager.Validate() == os::kernel::JobControlStatus::Succeeded;
        test_context.ExpectRandom(iteration_valid,
                                  OS_TEST_JOB_CONTROL_RANDOM_INVARIANT,
                                  OS_TEST_JOB_CONTROL_RANDOM_SEED, iteration);
    }
    return test_context.ExitCode();
}

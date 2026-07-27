#include "os/kernel/process/program_arguments.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_PROGRAM_ARGUMENTS_SUITE_NAME = "kernel/program_arguments/unit";
constexpr std::string_view OS_TEST_PROGRAM_ARGUMENTS_LAYOUT_MESSAGE =
    "argc、argv、envp 和字符串区域必须形成对齐且连续的初始用户栈";
constexpr std::string_view OS_TEST_PROGRAM_ARGUMENTS_LIMIT_MESSAGE =
    "参数字符串总量必须精确支持 128 KiB 并原子拒绝第一个越界字节";
constexpr std::string_view OS_TEST_PROGRAM_ARGUMENTS_COUNT_MESSAGE =
    "参数和环境项数量必须分别受 256 项硬上限约束";
constexpr std::string_view OS_TEST_PROGRAM_ARGUMENTS_CAPACITY_MESSAGE =
    "元数据与字符串放不下时必须拒绝且 Reset 后可安全复用";

constexpr uint64_t OS_TEST_PROGRAM_ARGUMENTS_STACK_BOTTOM = 0x000000007FFC0000ULL;
constexpr uint64_t OS_TEST_PROGRAM_ARGUMENTS_STACK_TOP = 0x0000000080000000ULL;
constexpr uint64_t OS_TEST_PROGRAM_ARGUMENTS_FIRST_INDEX = 0ULL;
constexpr uint64_t OS_TEST_PROGRAM_ARGUMENTS_SECOND_INDEX = 1ULL;
constexpr uint64_t OS_TEST_PROGRAM_ARGUMENTS_PATH_LENGTH_BYTES = 10ULL;
constexpr uint64_t OS_TEST_PROGRAM_ARGUMENTS_OPTION_LENGTH_BYTES = 5ULL;
constexpr uint64_t OS_TEST_PROGRAM_ARGUMENTS_ENVIRONMENT_LENGTH_BYTES = 13ULL;
constexpr uint64_t OS_TEST_PROGRAM_ARGUMENTS_TERMINATOR_SIZE_BYTES = 1ULL;
constexpr uint64_t OS_TEST_PROGRAM_ARGUMENTS_STACK_ALIGNMENT_BYTES = 16ULL;
constexpr uint64_t OS_TEST_PROGRAM_ARGUMENTS_POINTER_SIZE_BYTES = sizeof(uint64_t);
constexpr uint64_t OS_TEST_PROGRAM_ARGUMENTS_TINY_STACK_SIZE_BYTES = 32ULL;
constexpr uint64_t OS_TEST_PROGRAM_ARGUMENTS_EMPTY_VALUE = 0ULL;

}

int main() {
    os::test::TestContext test_context{OS_TEST_PROGRAM_ARGUMENTS_SUITE_NAME};
    os::kernel::ProgramArgumentPlan plan{};
    plan.Reset();
    test_context.Expect(
        plan.AddArgument(OS_TEST_PROGRAM_ARGUMENTS_PATH_LENGTH_BYTES) ==
                os::kernel::ProgramArgumentStatus::Succeeded &&
            plan.AddArgument(OS_TEST_PROGRAM_ARGUMENTS_OPTION_LENGTH_BYTES) ==
                os::kernel::ProgramArgumentStatus::Succeeded &&
            plan.AddEnvironment(OS_TEST_PROGRAM_ARGUMENTS_ENVIRONMENT_LENGTH_BYTES) ==
                os::kernel::ProgramArgumentStatus::Succeeded &&
            plan.Finalize(OS_TEST_PROGRAM_ARGUMENTS_STACK_BOTTOM,
                          OS_TEST_PROGRAM_ARGUMENTS_STACK_TOP) ==
                os::kernel::ProgramArgumentStatus::Succeeded &&
            plan.Validate() == os::kernel::ProgramArgumentStatus::Succeeded,
        OS_TEST_PROGRAM_ARGUMENTS_LAYOUT_MESSAGE);

    uint64_t first_length_bytes = OS_TEST_PROGRAM_ARGUMENTS_EMPTY_VALUE;
    uint64_t first_address = OS_TEST_PROGRAM_ARGUMENTS_EMPTY_VALUE;
    uint64_t second_length_bytes = OS_TEST_PROGRAM_ARGUMENTS_EMPTY_VALUE;
    uint64_t second_address = OS_TEST_PROGRAM_ARGUMENTS_EMPTY_VALUE;
    uint64_t environment_length_bytes = OS_TEST_PROGRAM_ARGUMENTS_EMPTY_VALUE;
    uint64_t environment_address = OS_TEST_PROGRAM_ARGUMENTS_EMPTY_VALUE;
    const os::kernel::ProgramArgumentLayout layout = plan.Layout();
    test_context.Expect(
        plan.ReadArgument(OS_TEST_PROGRAM_ARGUMENTS_FIRST_INDEX, first_length_bytes,
                          first_address) == os::kernel::ProgramArgumentStatus::Succeeded &&
            plan.ReadArgument(OS_TEST_PROGRAM_ARGUMENTS_SECOND_INDEX, second_length_bytes,
                              second_address) == os::kernel::ProgramArgumentStatus::Succeeded &&
            plan.ReadEnvironment(OS_TEST_PROGRAM_ARGUMENTS_FIRST_INDEX, environment_length_bytes,
                                 environment_address) ==
                os::kernel::ProgramArgumentStatus::Succeeded &&
            first_length_bytes == OS_TEST_PROGRAM_ARGUMENTS_PATH_LENGTH_BYTES &&
            second_length_bytes == OS_TEST_PROGRAM_ARGUMENTS_OPTION_LENGTH_BYTES &&
            environment_length_bytes == OS_TEST_PROGRAM_ARGUMENTS_ENVIRONMENT_LENGTH_BYTES &&
            second_address == first_address + first_length_bytes +
                                  OS_TEST_PROGRAM_ARGUMENTS_TERMINATOR_SIZE_BYTES &&
            environment_address == second_address + second_length_bytes +
                                       OS_TEST_PROGRAM_ARGUMENTS_TERMINATOR_SIZE_BYTES &&
            layout.stack_pointer % OS_TEST_PROGRAM_ARGUMENTS_STACK_ALIGNMENT_BYTES ==
                OS_TEST_PROGRAM_ARGUMENTS_EMPTY_VALUE &&
            layout.argument_vector_address ==
                layout.stack_pointer + OS_TEST_PROGRAM_ARGUMENTS_POINTER_SIZE_BYTES &&
            layout.environment_vector_address ==
                layout.argument_vector_address +
                    (plan.ArgumentCount() + OS_TEST_PROGRAM_ARGUMENTS_TERMINATOR_SIZE_BYTES) *
                        OS_TEST_PROGRAM_ARGUMENTS_POINTER_SIZE_BYTES,
        OS_TEST_PROGRAM_ARGUMENTS_LAYOUT_MESSAGE);

    plan.Reset();
    test_context.Expect(
        plan.AddArgument(os::kernel::OS_KERNEL_PROGRAM_ARGUMENT_MAXIMUM_STRING_BYTES -
                         OS_TEST_PROGRAM_ARGUMENTS_TERMINATOR_SIZE_BYTES) ==
                os::kernel::ProgramArgumentStatus::Succeeded &&
            plan.AddEnvironment(OS_TEST_PROGRAM_ARGUMENTS_EMPTY_VALUE) ==
                os::kernel::ProgramArgumentStatus::TotalSizeTooLarge &&
            plan.ArgumentCount() == OS_TEST_PROGRAM_ARGUMENTS_TERMINATOR_SIZE_BYTES &&
            plan.EnvironmentCount() == OS_TEST_PROGRAM_ARGUMENTS_EMPTY_VALUE &&
            plan.Finalize(OS_TEST_PROGRAM_ARGUMENTS_STACK_BOTTOM,
                          OS_TEST_PROGRAM_ARGUMENTS_STACK_TOP) ==
                os::kernel::ProgramArgumentStatus::Succeeded,
        OS_TEST_PROGRAM_ARGUMENTS_LIMIT_MESSAGE);

    plan.Reset();
    bool count_limit_valid = true;
    for (uint64_t argument_index = OS_TEST_PROGRAM_ARGUMENTS_EMPTY_VALUE;
         argument_index < os::kernel::OS_KERNEL_PROGRAM_ARGUMENT_MAXIMUM_ARGUMENT_COUNT;
         ++argument_index) {
        count_limit_valid = plan.AddArgument(OS_TEST_PROGRAM_ARGUMENTS_EMPTY_VALUE) ==
                                os::kernel::ProgramArgumentStatus::Succeeded &&
                            count_limit_valid;
    }
    count_limit_valid = plan.AddArgument(OS_TEST_PROGRAM_ARGUMENTS_EMPTY_VALUE) ==
                            os::kernel::ProgramArgumentStatus::TooManyArguments &&
                        count_limit_valid;
    test_context.Expect(count_limit_valid, OS_TEST_PROGRAM_ARGUMENTS_COUNT_MESSAGE);

    plan.Reset();
    test_context.Expect(plan.AddArgument(OS_TEST_PROGRAM_ARGUMENTS_PATH_LENGTH_BYTES) ==
                                os::kernel::ProgramArgumentStatus::Succeeded &&
                            plan.Finalize(OS_TEST_PROGRAM_ARGUMENTS_STACK_BOTTOM,
                                          OS_TEST_PROGRAM_ARGUMENTS_STACK_BOTTOM +
                                              OS_TEST_PROGRAM_ARGUMENTS_TINY_STACK_SIZE_BYTES) ==
                                os::kernel::ProgramArgumentStatus::StackCapacityExceeded &&
                            !plan.IsFinalized(),
                        OS_TEST_PROGRAM_ARGUMENTS_CAPACITY_MESSAGE);
    plan.Reset();
    test_context.Expect(plan.AddArgument(OS_TEST_PROGRAM_ARGUMENTS_EMPTY_VALUE) ==
                                os::kernel::ProgramArgumentStatus::Succeeded &&
                            plan.Finalize(OS_TEST_PROGRAM_ARGUMENTS_STACK_BOTTOM,
                                          OS_TEST_PROGRAM_ARGUMENTS_STACK_TOP) ==
                                os::kernel::ProgramArgumentStatus::Succeeded &&
                            plan.AddArgument(OS_TEST_PROGRAM_ARGUMENTS_EMPTY_VALUE) ==
                                os::kernel::ProgramArgumentStatus::AlreadyFinalized,
                        OS_TEST_PROGRAM_ARGUMENTS_CAPACITY_MESSAGE);
    return test_context.ExitCode();
}

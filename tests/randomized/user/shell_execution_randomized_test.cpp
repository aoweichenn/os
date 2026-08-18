#include "os/user/shell_execution.hpp"
#include "test_context.hpp"

#include <random>
#include <string_view>

namespace {

constexpr std::string_view OS_TEST_SHELL_EXECUTION_RANDOM_SUITE_NAME =
    "user/shell_execution/randomized";
constexpr std::string_view OS_TEST_SHELL_EXECUTION_RANDOM_DETERMINISM =
    "4096 条任意 7-bit 命令行必须解析确定、成功布局有界、失败不泄露半计划";
constexpr os::test::RandomSeed OS_TEST_SHELL_EXECUTION_RANDOM_SEED = 0x5E11E7EC20260111ULL;
constexpr os::test::TestCount OS_TEST_SHELL_EXECUTION_RANDOM_ITERATION_COUNT = 4096ULL;
constexpr uint64_t OS_TEST_SHELL_EXECUTION_RANDOM_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_SHELL_EXECUTION_RANDOM_MAXIMUM_BYTE = 0x7FULL;
constexpr char OS_TEST_SHELL_EXECUTION_RANDOM_STRING_TERMINATOR = '\0';

[[nodiscard]] bool PlansEqual(const os::user::ShellExecutionPlan &first,
                              const os::user::ShellExecutionPlan &second) noexcept {
    if (first.argument_count != second.argument_count || first.stage_count != second.stage_count ||
        first.background != second.background) {
        return false;
    }
    for (uint64_t byte_index = OS_TEST_SHELL_EXECUTION_RANDOM_EMPTY_VALUE;
         byte_index < os::user::OS_USER_SHELL_EXECUTION_STORAGE_SIZE_BYTES; ++byte_index) {
        if (first.storage[byte_index] != second.storage[byte_index] ||
            first.storage_flags[byte_index] != second.storage_flags[byte_index]) {
            return false;
        }
    }
    for (uint64_t argument_index = OS_TEST_SHELL_EXECUTION_RANDOM_EMPTY_VALUE;
         argument_index < os::user::OS_USER_SHELL_EXECUTION_MAXIMUM_ARGUMENT_COUNT;
         ++argument_index) {
        if (first.arguments[argument_index].offset_bytes !=
                second.arguments[argument_index].offset_bytes ||
            first.arguments[argument_index].length_bytes !=
                second.arguments[argument_index].length_bytes) {
            return false;
        }
    }
    for (uint64_t stage_index = OS_TEST_SHELL_EXECUTION_RANDOM_EMPTY_VALUE;
         stage_index < os::user::OS_USER_SHELL_EXECUTION_MAXIMUM_STAGE_COUNT; ++stage_index) {
        const os::user::ShellExecutionStage &first_stage = first.stages[stage_index];
        const os::user::ShellExecutionStage &second_stage = second.stages[stage_index];
        if (first_stage.first_argument_index != second_stage.first_argument_index ||
            first_stage.argument_count != second_stage.argument_count ||
            first_stage.has_input_redirection != second_stage.has_input_redirection ||
            first_stage.input_path.offset_bytes != second_stage.input_path.offset_bytes ||
            first_stage.input_path.length_bytes != second_stage.input_path.length_bytes ||
            first_stage.output_redirection != second_stage.output_redirection ||
            first_stage.output_path.offset_bytes != second_stage.output_path.offset_bytes ||
            first_stage.output_path.length_bytes != second_stage.output_path.length_bytes ||
            first_stage.error_redirection != second_stage.error_redirection ||
            first_stage.error_path.offset_bytes != second_stage.error_path.offset_bytes ||
            first_stage.error_path.length_bytes != second_stage.error_path.length_bytes) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool StoredArgumentIsValid(const os::user::ShellExecutionPlan &plan,
                                         const os::user::ShellArgument &argument) noexcept {
    return argument.offset_bytes < os::user::OS_USER_SHELL_EXECUTION_STORAGE_SIZE_BYTES &&
           argument.length_bytes <
               os::user::OS_USER_SHELL_EXECUTION_STORAGE_SIZE_BYTES - argument.offset_bytes &&
           plan.storage[argument.offset_bytes + argument.length_bytes] ==
               OS_TEST_SHELL_EXECUTION_RANDOM_STRING_TERMINATOR;
}

[[nodiscard]] bool SuccessfulLayoutIsValid(const os::user::ShellExecutionPlan &plan) noexcept {
    if (plan.stage_count == OS_TEST_SHELL_EXECUTION_RANDOM_EMPTY_VALUE ||
        plan.stage_count > os::user::OS_USER_SHELL_EXECUTION_MAXIMUM_STAGE_COUNT ||
        plan.argument_count > os::user::OS_USER_SHELL_EXECUTION_MAXIMUM_ARGUMENT_COUNT) {
        return false;
    }
    uint64_t observed_argument_count = OS_TEST_SHELL_EXECUTION_RANDOM_EMPTY_VALUE;
    for (uint64_t stage_index = OS_TEST_SHELL_EXECUTION_RANDOM_EMPTY_VALUE;
         stage_index < plan.stage_count; ++stage_index) {
        const os::user::ShellExecutionStage &stage = plan.stages[stage_index];
        if (stage.argument_count == OS_TEST_SHELL_EXECUTION_RANDOM_EMPTY_VALUE ||
            stage.argument_count > os::user::OS_USER_SHELL_EXECUTION_MAXIMUM_ARGUMENTS_PER_STAGE ||
            stage.first_argument_index != observed_argument_count) {
            return false;
        }
        if ((stage.has_input_redirection && !StoredArgumentIsValid(plan, stage.input_path)) ||
            (stage.output_redirection != os::user::ShellRedirectionMode::None &&
             !StoredArgumentIsValid(plan, stage.output_path)) ||
            (stage.error_redirection != os::user::ShellRedirectionMode::None &&
             !StoredArgumentIsValid(plan, stage.error_path))) {
            return false;
        }
        observed_argument_count += stage.argument_count;
    }
    for (uint64_t argument_index = OS_TEST_SHELL_EXECUTION_RANDOM_EMPTY_VALUE;
         argument_index < plan.argument_count; ++argument_index) {
        const os::user::ShellArgument &argument = plan.arguments[argument_index];
        if (!StoredArgumentIsValid(plan, argument)) {
            return false;
        }
    }
    return observed_argument_count == plan.argument_count;
}

[[nodiscard]] bool SequencesEqual(const os::user::ShellExecutionSequence &first,
                                  const os::user::ShellExecutionSequence &second) noexcept {
    if (first.command_count != second.command_count) {
        return false;
    }
    for (uint64_t command_index = OS_TEST_SHELL_EXECUTION_RANDOM_EMPTY_VALUE;
         command_index < os::user::OS_USER_SHELL_EXECUTION_MAXIMUM_COMMAND_COUNT; ++command_index) {
        if (first.commands[command_index].offset_bytes !=
                second.commands[command_index].offset_bytes ||
            first.commands[command_index].length_bytes !=
                second.commands[command_index].length_bytes ||
            first.commands[command_index].condition != second.commands[command_index].condition) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool SuccessfulSequenceIsValid(const os::user::ShellExecutionSequence &sequence,
                                             const uint64_t line_length_bytes) noexcept {
    if (sequence.command_count == OS_TEST_SHELL_EXECUTION_RANDOM_EMPTY_VALUE ||
        sequence.command_count > os::user::OS_USER_SHELL_EXECUTION_MAXIMUM_COMMAND_COUNT ||
        sequence.commands[OS_TEST_SHELL_EXECUTION_RANDOM_EMPTY_VALUE].condition !=
            os::user::ShellExecutionCondition::Always) {
        return false;
    }
    uint64_t previous_end_index = OS_TEST_SHELL_EXECUTION_RANDOM_EMPTY_VALUE;
    for (uint64_t command_index = OS_TEST_SHELL_EXECUTION_RANDOM_EMPTY_VALUE;
         command_index < sequence.command_count; ++command_index) {
        const os::user::ShellExecutionCommand &command = sequence.commands[command_index];
        if (command.length_bytes == OS_TEST_SHELL_EXECUTION_RANDOM_EMPTY_VALUE ||
            command.offset_bytes < previous_end_index || command.offset_bytes > line_length_bytes ||
            command.length_bytes > line_length_bytes - command.offset_bytes) {
            return false;
        }
        previous_end_index = command.offset_bytes + command.length_bytes;
    }
    return true;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_SHELL_EXECUTION_RANDOM_SUITE_NAME};
    std::mt19937_64 generator{OS_TEST_SHELL_EXECUTION_RANDOM_SEED};
    std::uniform_int_distribution<uint64_t> length_distribution{
        OS_TEST_SHELL_EXECUTION_RANDOM_EMPTY_VALUE,
        os::user::OS_USER_SHELL_EXECUTION_MAXIMUM_LINE_SIZE_BYTES};
    std::uniform_int_distribution<uint64_t> byte_distribution{
        OS_TEST_SHELL_EXECUTION_RANDOM_EMPTY_VALUE, OS_TEST_SHELL_EXECUTION_RANDOM_MAXIMUM_BYTE};

    for (os::test::TestCount iteration = OS_TEST_SHELL_EXECUTION_RANDOM_EMPTY_VALUE;
         iteration < OS_TEST_SHELL_EXECUTION_RANDOM_ITERATION_COUNT; ++iteration) {
        char line[os::user::OS_USER_SHELL_EXECUTION_MAXIMUM_LINE_SIZE_BYTES]{};
        const uint64_t line_length_bytes = length_distribution(generator);
        for (uint64_t byte_index = OS_TEST_SHELL_EXECUTION_RANDOM_EMPTY_VALUE;
             byte_index < line_length_bytes; ++byte_index) {
            line[byte_index] = static_cast<char>(byte_distribution(generator));
        }
        os::user::ShellExecutionPlan first_plan{};
        os::user::ShellExecutionPlan second_plan{};
        os::user::ShellExecutionSequence first_sequence{};
        os::user::ShellExecutionSequence second_sequence{};
        const os::user::ShellExecutionParseStatus first_status =
            os::user::ParseShellExecutionPlan(line, line_length_bytes, first_plan);
        const os::user::ShellExecutionParseStatus second_status =
            os::user::ParseShellExecutionPlan(line, line_length_bytes, second_plan);
        const os::user::ShellExecutionParseStatus first_sequence_status =
            os::user::ParseShellExecutionSequence(line, line_length_bytes, first_sequence);
        const os::user::ShellExecutionParseStatus second_sequence_status =
            os::user::ParseShellExecutionSequence(line, line_length_bytes, second_sequence);
        const bool atomic_failure =
            first_status == os::user::ShellExecutionParseStatus::Succeeded ||
            (first_plan.argument_count == OS_TEST_SHELL_EXECUTION_RANDOM_EMPTY_VALUE &&
             first_plan.stage_count == OS_TEST_SHELL_EXECUTION_RANDOM_EMPTY_VALUE);
        const bool valid =
            first_status == second_status && PlansEqual(first_plan, second_plan) &&
            atomic_failure &&
            (first_status != os::user::ShellExecutionParseStatus::Succeeded ||
             SuccessfulLayoutIsValid(first_plan)) &&
            first_sequence_status == second_sequence_status &&
            SequencesEqual(first_sequence, second_sequence) &&
            (first_sequence_status == os::user::ShellExecutionParseStatus::Succeeded ||
             first_sequence.command_count == OS_TEST_SHELL_EXECUTION_RANDOM_EMPTY_VALUE) &&
            (first_sequence_status != os::user::ShellExecutionParseStatus::Succeeded ||
             SuccessfulSequenceIsValid(first_sequence, line_length_bytes));
        test_context.ExpectRandom(valid, OS_TEST_SHELL_EXECUTION_RANDOM_DETERMINISM,
                                  OS_TEST_SHELL_EXECUTION_RANDOM_SEED, iteration);
    }
    return test_context.ExitCode();
}

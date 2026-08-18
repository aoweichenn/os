#include "os/user/shell_execution.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_SHELL_EXECUTION_SUITE_NAME = "user/shell_execution/unit";
constexpr std::string_view OS_TEST_SHELL_EXECUTION_PIPELINE =
    "16 级管线必须精确划分阶段并保留每一级参数";
constexpr std::string_view OS_TEST_SHELL_EXECUTION_REDIRECTION =
    "无空格重定向、引号和转义必须生成独立路径且不进入 argv";
constexpr std::string_view OS_TEST_SHELL_EXECUTION_FAILURES =
    "空阶段、重复重定向、缺失路径和第 17 级必须原子拒绝";
constexpr std::string_view OS_TEST_SHELL_EXECUTION_BACKGROUND =
    "未引用的末尾 & 必须标记后台管线，非末尾 & 必须原子拒绝";
constexpr std::string_view OS_TEST_SHELL_EXECUTION_SEQUENCE =
    "分号、成功短路和失败短路必须在引号与转义之外划分有界命令序列";
constexpr std::string_view OS_TEST_SHELL_EXECUTION_SEQUENCE_FAILURES =
    "空命令、悬空短路和第九条命令必须原子拒绝";
constexpr std::string_view OS_TEST_SHELL_EXECUTION_EXPANSION =
    "变量、上一退出码和 glob 标志必须服从引用与转义边界";
constexpr std::string_view OS_TEST_SHELL_EXECUTION_EXPANSION_FAILURES =
    "未闭合或非法 braced 变量必须失败原子";
constexpr char OS_TEST_SHELL_EXECUTION_PIPELINE_LINE[] =
    "echo payload|cat|cat|cat|cat|cat|cat|cat|cat|cat|cat|cat|cat|cat|cat|wc";
constexpr char OS_TEST_SHELL_EXECUTION_REDIRECTION_LINE[] =
    "cat<'input file' 2>error|tee \"copy file\">>escaped\\ output";
constexpr char OS_TEST_SHELL_EXECUTION_EMPTY_STAGE_LINE[] = "echo value||cat";
constexpr char OS_TEST_SHELL_EXECUTION_DUPLICATE_REDIRECTION_LINE[] = "cat < first < second";
constexpr char OS_TEST_SHELL_EXECUTION_DUPLICATE_ERROR_REDIRECTION_LINE[] = "cat 2>first 2>>second";
constexpr char OS_TEST_SHELL_EXECUTION_MISSING_REDIRECTION_LINE[] = "cat >";
constexpr char OS_TEST_SHELL_EXECUTION_TOO_MANY_STAGES_LINE[] = "a|b|c|d|e|f|g|h|i|j|k|l|m|n|o|p|q";
constexpr char OS_TEST_SHELL_EXECUTION_BACKGROUND_LINE[] = "cat | wc &";
constexpr char OS_TEST_SHELL_EXECUTION_BACKGROUND_NOT_LAST_LINE[] = "cat & echo";
constexpr char OS_TEST_SHELL_EXECUTION_EXPECTED_INPUT[] = "input file";
constexpr char OS_TEST_SHELL_EXECUTION_EXPECTED_OUTPUT[] = "escaped output";
constexpr char OS_TEST_SHELL_EXECUTION_EXPECTED_TEE_PATH[] = "copy file";
constexpr char OS_TEST_SHELL_EXECUTION_EXPECTED_ERROR_PATH[] = "error";
constexpr char OS_TEST_SHELL_EXECUTION_SEQUENCE_LINE[] = "false && echo no || echo yes; true;";
constexpr char OS_TEST_SHELL_EXECUTION_QUOTED_SEQUENCE_LINE[] = "echo 'a;b&&c||d'; echo ok";
constexpr char OS_TEST_SHELL_EXECUTION_ESCAPED_SEQUENCE_LINE[] = "echo a\\;b && echo c";
constexpr char OS_TEST_SHELL_EXECUTION_EMPTY_COMMAND_LINE[] = "; echo invalid";
constexpr char OS_TEST_SHELL_EXECUTION_DANGLING_CONTROL_LINE[] = "echo value &&";
constexpr char OS_TEST_SHELL_EXECUTION_TOO_MANY_COMMANDS_LINE[] = "a;b;c;d;e;f;g;h;i";
constexpr char OS_TEST_SHELL_EXECUTION_EXPECTED_FALSE_COMMAND[] = "false";
constexpr char OS_TEST_SHELL_EXECUTION_EXPECTED_SECOND_COMMAND[] = "echo no";
constexpr char OS_TEST_SHELL_EXECUTION_EXPECTED_THIRD_COMMAND[] = "echo yes";
constexpr char OS_TEST_SHELL_EXECUTION_EXPECTED_TRUE_COMMAND[] = "true";
constexpr char OS_TEST_SHELL_EXECUTION_EXPECTED_QUOTED_COMMAND[] = "echo 'a;b&&c||d'";
constexpr char OS_TEST_SHELL_EXECUTION_EXPANSION_LINE[] =
    "echo $NAME \"$PATTERN\" '$NAME' ${EMPTY} $? *.cpp \"?\"";
constexpr char OS_TEST_SHELL_EXECUTION_UNQUOTED_PATTERN_LINE[] = "echo $PATTERN";
constexpr char OS_TEST_SHELL_EXECUTION_INVALID_VARIABLE_LINE[] = "echo ${BAD-NAME}";
constexpr char OS_TEST_SHELL_EXECUTION_UNCLOSED_VARIABLE_LINE[] = "echo ${NAME";
constexpr char OS_TEST_SHELL_EXECUTION_NAME[] = "NAME";
constexpr char OS_TEST_SHELL_EXECUTION_NAME_VALUE[] = "expanded";
constexpr char OS_TEST_SHELL_EXECUTION_PATTERN[] = "PATTERN";
constexpr char OS_TEST_SHELL_EXECUTION_PATTERN_VALUE[] = "*.txt";
constexpr char OS_TEST_SHELL_EXECUTION_EMPTY_NAME[] = "EMPTY";
constexpr char OS_TEST_SHELL_EXECUTION_EXPECTED_LITERAL_NAME[] = "$NAME";
constexpr char OS_TEST_SHELL_EXECUTION_EXPECTED_STATUS[] = "37";
constexpr char OS_TEST_SHELL_EXECUTION_EXPECTED_CPP_PATTERN[] = "*.cpp";
constexpr char OS_TEST_SHELL_EXECUTION_EXPECTED_QUESTION[] = "?";
constexpr uint64_t OS_TEST_SHELL_EXECUTION_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_SHELL_EXECUTION_FIRST_VALUE = 1ULL;
constexpr uint64_t OS_TEST_SHELL_EXECUTION_SECOND_STAGE_INDEX = 1ULL;
constexpr uint64_t OS_TEST_SHELL_EXECUTION_THIRD_COMMAND_INDEX = 2ULL;
constexpr uint64_t OS_TEST_SHELL_EXECUTION_FOURTH_COMMAND_INDEX = 3ULL;
constexpr uint64_t OS_TEST_SHELL_EXECUTION_EXPECTED_COMMAND_COUNT = 4ULL;
constexpr uint64_t OS_TEST_SHELL_EXECUTION_SECOND_STAGE_ARGUMENT_COUNT = 2ULL;
constexpr uint64_t OS_TEST_SHELL_EXECUTION_REDIRECTION_ARGUMENT_COUNT = 3ULL;
constexpr uint64_t OS_TEST_SHELL_EXECUTION_STRING_TERMINATOR_SIZE_BYTES = 1ULL;

[[nodiscard]] bool ArgumentEquals(const os::user::ShellExecutionPlan &plan,
                                  const os::user::ShellArgument &argument,
                                  const char *const expected,
                                  const uint64_t expected_length_bytes) noexcept {
    if (expected == nullptr || argument.length_bytes != expected_length_bytes) {
        return false;
    }
    const char *const actual = plan.storage + argument.offset_bytes;
    for (uint64_t byte_index = OS_TEST_SHELL_EXECUTION_EMPTY_VALUE;
         byte_index < expected_length_bytes; ++byte_index) {
        if (actual[byte_index] != expected[byte_index]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool SequenceCommandEquals(const char *const line,
                                         const os::user::ShellExecutionCommand &command,
                                         const char *const expected,
                                         const uint64_t expected_length_bytes) noexcept {
    if (line == nullptr || expected == nullptr || command.length_bytes != expected_length_bytes) {
        return false;
    }
    for (uint64_t byte_index = OS_TEST_SHELL_EXECUTION_EMPTY_VALUE;
         byte_index < expected_length_bytes; ++byte_index) {
        if (line[command.offset_bytes + byte_index] != expected[byte_index]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool ExpansionLookup(void *const context, const char *const name,
                                   const uint64_t name_length_bytes, const char *&value,
                                   uint64_t &value_length_bytes) noexcept {
    static_cast<void>(context);
    value = nullptr;
    value_length_bytes = OS_TEST_SHELL_EXECUTION_EMPTY_VALUE;
    const auto name_matches = [name, name_length_bytes](const char *const expected,
                                                        const uint64_t expected_length_bytes) {
        if (name == nullptr || expected == nullptr || name_length_bytes != expected_length_bytes) {
            return false;
        }
        for (uint64_t byte_index = OS_TEST_SHELL_EXECUTION_EMPTY_VALUE;
             byte_index < expected_length_bytes; ++byte_index) {
            if (name[byte_index] != expected[byte_index]) {
                return false;
            }
        }
        return true;
    };
    if (name_matches(OS_TEST_SHELL_EXECUTION_NAME,
                     sizeof(OS_TEST_SHELL_EXECUTION_NAME) -
                         OS_TEST_SHELL_EXECUTION_STRING_TERMINATOR_SIZE_BYTES)) {
        value = OS_TEST_SHELL_EXECUTION_NAME_VALUE;
        value_length_bytes = sizeof(OS_TEST_SHELL_EXECUTION_NAME_VALUE) -
                             OS_TEST_SHELL_EXECUTION_STRING_TERMINATOR_SIZE_BYTES;
        return true;
    }
    if (name_matches(OS_TEST_SHELL_EXECUTION_PATTERN,
                     sizeof(OS_TEST_SHELL_EXECUTION_PATTERN) -
                         OS_TEST_SHELL_EXECUTION_STRING_TERMINATOR_SIZE_BYTES)) {
        value = OS_TEST_SHELL_EXECUTION_PATTERN_VALUE;
        value_length_bytes = sizeof(OS_TEST_SHELL_EXECUTION_PATTERN_VALUE) -
                             OS_TEST_SHELL_EXECUTION_STRING_TERMINATOR_SIZE_BYTES;
        return true;
    }
    return name_matches(OS_TEST_SHELL_EXECUTION_EMPTY_NAME,
                        sizeof(OS_TEST_SHELL_EXECUTION_EMPTY_NAME) -
                            OS_TEST_SHELL_EXECUTION_STRING_TERMINATOR_SIZE_BYTES);
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_SHELL_EXECUTION_SUITE_NAME};
    os::user::ShellExecutionPlan plan{};
    bool pipeline_valid =
        os::user::ParseShellExecutionPlan(OS_TEST_SHELL_EXECUTION_PIPELINE_LINE,
                                          sizeof(OS_TEST_SHELL_EXECUTION_PIPELINE_LINE) -
                                              OS_TEST_SHELL_EXECUTION_STRING_TERMINATOR_SIZE_BYTES,
                                          plan) == os::user::ShellExecutionParseStatus::Succeeded &&
        plan.stage_count == os::user::OS_USER_SHELL_EXECUTION_MAXIMUM_STAGE_COUNT &&
        plan.stages[OS_TEST_SHELL_EXECUTION_EMPTY_VALUE].argument_count ==
            OS_TEST_SHELL_EXECUTION_SECOND_STAGE_ARGUMENT_COUNT;
    for (uint64_t stage_index = OS_TEST_SHELL_EXECUTION_FIRST_VALUE; stage_index < plan.stage_count;
         ++stage_index) {
        pipeline_valid =
            pipeline_valid &&
            plan.stages[stage_index].argument_count == OS_TEST_SHELL_EXECUTION_FIRST_VALUE &&
            plan.stages[stage_index].first_argument_index ==
                stage_index + OS_TEST_SHELL_EXECUTION_FIRST_VALUE;
    }
    test_context.Expect(pipeline_valid, OS_TEST_SHELL_EXECUTION_PIPELINE);

    const bool redirection_valid =
        os::user::ParseShellExecutionPlan(OS_TEST_SHELL_EXECUTION_REDIRECTION_LINE,
                                          sizeof(OS_TEST_SHELL_EXECUTION_REDIRECTION_LINE) -
                                              OS_TEST_SHELL_EXECUTION_STRING_TERMINATOR_SIZE_BYTES,
                                          plan) == os::user::ShellExecutionParseStatus::Succeeded &&
        plan.stage_count == OS_TEST_SHELL_EXECUTION_SECOND_STAGE_ARGUMENT_COUNT &&
        plan.argument_count == OS_TEST_SHELL_EXECUTION_REDIRECTION_ARGUMENT_COUNT &&
        plan.stages[OS_TEST_SHELL_EXECUTION_EMPTY_VALUE].has_input_redirection &&
        plan.stages[OS_TEST_SHELL_EXECUTION_EMPTY_VALUE].output_redirection ==
            os::user::ShellRedirectionMode::None &&
        plan.stages[OS_TEST_SHELL_EXECUTION_EMPTY_VALUE].error_redirection ==
            os::user::ShellRedirectionMode::Truncate &&
        plan.stages[OS_TEST_SHELL_EXECUTION_SECOND_STAGE_INDEX].output_redirection ==
            os::user::ShellRedirectionMode::Append &&
        ArgumentEquals(plan, plan.stages[OS_TEST_SHELL_EXECUTION_EMPTY_VALUE].input_path,
                       OS_TEST_SHELL_EXECUTION_EXPECTED_INPUT,
                       sizeof(OS_TEST_SHELL_EXECUTION_EXPECTED_INPUT) -
                           OS_TEST_SHELL_EXECUTION_STRING_TERMINATOR_SIZE_BYTES) &&
        ArgumentEquals(plan, plan.stages[OS_TEST_SHELL_EXECUTION_SECOND_STAGE_INDEX].output_path,
                       OS_TEST_SHELL_EXECUTION_EXPECTED_OUTPUT,
                       sizeof(OS_TEST_SHELL_EXECUTION_EXPECTED_OUTPUT) -
                           OS_TEST_SHELL_EXECUTION_STRING_TERMINATOR_SIZE_BYTES) &&
        ArgumentEquals(plan, plan.stages[OS_TEST_SHELL_EXECUTION_EMPTY_VALUE].error_path,
                       OS_TEST_SHELL_EXECUTION_EXPECTED_ERROR_PATH,
                       sizeof(OS_TEST_SHELL_EXECUTION_EXPECTED_ERROR_PATH) -
                           OS_TEST_SHELL_EXECUTION_STRING_TERMINATOR_SIZE_BYTES) &&
        ArgumentEquals(plan,
                       plan.arguments[plan.stages[OS_TEST_SHELL_EXECUTION_SECOND_STAGE_INDEX]
                                          .first_argument_index +
                                      OS_TEST_SHELL_EXECUTION_FIRST_VALUE],
                       OS_TEST_SHELL_EXECUTION_EXPECTED_TEE_PATH,
                       sizeof(OS_TEST_SHELL_EXECUTION_EXPECTED_TEE_PATH) -
                           OS_TEST_SHELL_EXECUTION_STRING_TERMINATOR_SIZE_BYTES);
    test_context.Expect(redirection_valid, OS_TEST_SHELL_EXECUTION_REDIRECTION);

    const bool background_valid =
        os::user::ParseShellExecutionPlan(OS_TEST_SHELL_EXECUTION_BACKGROUND_LINE,
                                          sizeof(OS_TEST_SHELL_EXECUTION_BACKGROUND_LINE) -
                                              OS_TEST_SHELL_EXECUTION_STRING_TERMINATOR_SIZE_BYTES,
                                          plan) == os::user::ShellExecutionParseStatus::Succeeded &&
        plan.background &&
        os::user::ParseShellExecutionPlan(OS_TEST_SHELL_EXECUTION_BACKGROUND_NOT_LAST_LINE,
                                          sizeof(OS_TEST_SHELL_EXECUTION_BACKGROUND_NOT_LAST_LINE) -
                                              OS_TEST_SHELL_EXECUTION_STRING_TERMINATOR_SIZE_BYTES,
                                          plan) ==
            os::user::ShellExecutionParseStatus::BackgroundOperatorNotLast &&
        plan.stage_count == OS_TEST_SHELL_EXECUTION_EMPTY_VALUE;
    test_context.Expect(background_valid, OS_TEST_SHELL_EXECUTION_BACKGROUND);

    os::user::ShellExecutionSequence sequence{};
    const bool sequence_valid =
        os::user::ParseShellExecutionSequence(
            OS_TEST_SHELL_EXECUTION_SEQUENCE_LINE,
            sizeof(OS_TEST_SHELL_EXECUTION_SEQUENCE_LINE) -
                OS_TEST_SHELL_EXECUTION_STRING_TERMINATOR_SIZE_BYTES,
            sequence) == os::user::ShellExecutionParseStatus::Succeeded &&
        sequence.command_count == OS_TEST_SHELL_EXECUTION_EXPECTED_COMMAND_COUNT &&
        sequence.commands[0].condition == os::user::ShellExecutionCondition::Always &&
        sequence.commands[1].condition == os::user::ShellExecutionCondition::OnSuccess &&
        sequence.commands[OS_TEST_SHELL_EXECUTION_THIRD_COMMAND_INDEX].condition ==
            os::user::ShellExecutionCondition::OnFailure &&
        sequence.commands[OS_TEST_SHELL_EXECUTION_FOURTH_COMMAND_INDEX].condition ==
            os::user::ShellExecutionCondition::Always &&
        SequenceCommandEquals(OS_TEST_SHELL_EXECUTION_SEQUENCE_LINE, sequence.commands[0],
                              OS_TEST_SHELL_EXECUTION_EXPECTED_FALSE_COMMAND,
                              sizeof(OS_TEST_SHELL_EXECUTION_EXPECTED_FALSE_COMMAND) -
                                  OS_TEST_SHELL_EXECUTION_STRING_TERMINATOR_SIZE_BYTES) &&
        SequenceCommandEquals(OS_TEST_SHELL_EXECUTION_SEQUENCE_LINE, sequence.commands[1],
                              OS_TEST_SHELL_EXECUTION_EXPECTED_SECOND_COMMAND,
                              sizeof(OS_TEST_SHELL_EXECUTION_EXPECTED_SECOND_COMMAND) -
                                  OS_TEST_SHELL_EXECUTION_STRING_TERMINATOR_SIZE_BYTES) &&
        SequenceCommandEquals(OS_TEST_SHELL_EXECUTION_SEQUENCE_LINE,
                              sequence.commands[OS_TEST_SHELL_EXECUTION_THIRD_COMMAND_INDEX],
                              OS_TEST_SHELL_EXECUTION_EXPECTED_THIRD_COMMAND,
                              sizeof(OS_TEST_SHELL_EXECUTION_EXPECTED_THIRD_COMMAND) -
                                  OS_TEST_SHELL_EXECUTION_STRING_TERMINATOR_SIZE_BYTES) &&
        SequenceCommandEquals(OS_TEST_SHELL_EXECUTION_SEQUENCE_LINE,
                              sequence.commands[OS_TEST_SHELL_EXECUTION_FOURTH_COMMAND_INDEX],
                              OS_TEST_SHELL_EXECUTION_EXPECTED_TRUE_COMMAND,
                              sizeof(OS_TEST_SHELL_EXECUTION_EXPECTED_TRUE_COMMAND) -
                                  OS_TEST_SHELL_EXECUTION_STRING_TERMINATOR_SIZE_BYTES) &&
        os::user::ParseShellExecutionSequence(
            OS_TEST_SHELL_EXECUTION_QUOTED_SEQUENCE_LINE,
            sizeof(OS_TEST_SHELL_EXECUTION_QUOTED_SEQUENCE_LINE) -
                OS_TEST_SHELL_EXECUTION_STRING_TERMINATOR_SIZE_BYTES,
            sequence) == os::user::ShellExecutionParseStatus::Succeeded &&
        sequence.command_count == OS_TEST_SHELL_EXECUTION_SECOND_STAGE_ARGUMENT_COUNT &&
        SequenceCommandEquals(OS_TEST_SHELL_EXECUTION_QUOTED_SEQUENCE_LINE, sequence.commands[0],
                              OS_TEST_SHELL_EXECUTION_EXPECTED_QUOTED_COMMAND,
                              sizeof(OS_TEST_SHELL_EXECUTION_EXPECTED_QUOTED_COMMAND) -
                                  OS_TEST_SHELL_EXECUTION_STRING_TERMINATOR_SIZE_BYTES) &&
        os::user::ParseShellExecutionSequence(
            OS_TEST_SHELL_EXECUTION_ESCAPED_SEQUENCE_LINE,
            sizeof(OS_TEST_SHELL_EXECUTION_ESCAPED_SEQUENCE_LINE) -
                OS_TEST_SHELL_EXECUTION_STRING_TERMINATOR_SIZE_BYTES,
            sequence) == os::user::ShellExecutionParseStatus::Succeeded &&
        sequence.command_count == OS_TEST_SHELL_EXECUTION_SECOND_STAGE_ARGUMENT_COUNT;
    test_context.Expect(sequence_valid, OS_TEST_SHELL_EXECUTION_SEQUENCE);

    const bool sequence_failures_atomic =
        os::user::ParseShellExecutionSequence(
            OS_TEST_SHELL_EXECUTION_EMPTY_COMMAND_LINE,
            sizeof(OS_TEST_SHELL_EXECUTION_EMPTY_COMMAND_LINE) -
                OS_TEST_SHELL_EXECUTION_STRING_TERMINATOR_SIZE_BYTES,
            sequence) == os::user::ShellExecutionParseStatus::EmptyCommand &&
        sequence.command_count == OS_TEST_SHELL_EXECUTION_EMPTY_VALUE &&
        os::user::ParseShellExecutionSequence(
            OS_TEST_SHELL_EXECUTION_DANGLING_CONTROL_LINE,
            sizeof(OS_TEST_SHELL_EXECUTION_DANGLING_CONTROL_LINE) -
                OS_TEST_SHELL_EXECUTION_STRING_TERMINATOR_SIZE_BYTES,
            sequence) == os::user::ShellExecutionParseStatus::DanglingControlOperator &&
        sequence.command_count == OS_TEST_SHELL_EXECUTION_EMPTY_VALUE &&
        os::user::ParseShellExecutionSequence(
            OS_TEST_SHELL_EXECUTION_TOO_MANY_COMMANDS_LINE,
            sizeof(OS_TEST_SHELL_EXECUTION_TOO_MANY_COMMANDS_LINE) -
                OS_TEST_SHELL_EXECUTION_STRING_TERMINATOR_SIZE_BYTES,
            sequence) == os::user::ShellExecutionParseStatus::TooManyCommands &&
        sequence.command_count == OS_TEST_SHELL_EXECUTION_EMPTY_VALUE;
    test_context.Expect(sequence_failures_atomic, OS_TEST_SHELL_EXECUTION_SEQUENCE_FAILURES);

    const os::user::ShellExpansionContext expansion_context{
        .context = nullptr,
        .lookup_operation = ExpansionLookup,
        .previous_exit_code = 37LL,
    };
    const bool expansion_valid =
        os::user::ParseShellExecutionPlanExpanded(
            OS_TEST_SHELL_EXECUTION_EXPANSION_LINE,
            sizeof(OS_TEST_SHELL_EXECUTION_EXPANSION_LINE) -
                OS_TEST_SHELL_EXECUTION_STRING_TERMINATOR_SIZE_BYTES,
            expansion_context, plan) == os::user::ShellExecutionParseStatus::Succeeded &&
        plan.argument_count == 8ULL &&
        ArgumentEquals(plan, plan.arguments[1], OS_TEST_SHELL_EXECUTION_NAME_VALUE,
                       sizeof(OS_TEST_SHELL_EXECUTION_NAME_VALUE) -
                           OS_TEST_SHELL_EXECUTION_STRING_TERMINATOR_SIZE_BYTES) &&
        ArgumentEquals(plan, plan.arguments[2], OS_TEST_SHELL_EXECUTION_PATTERN_VALUE,
                       sizeof(OS_TEST_SHELL_EXECUTION_PATTERN_VALUE) -
                           OS_TEST_SHELL_EXECUTION_STRING_TERMINATOR_SIZE_BYTES) &&
        !os::user::ShellExecutionArgumentHasGlob(plan, 2ULL) &&
        ArgumentEquals(plan, plan.arguments[3], OS_TEST_SHELL_EXECUTION_EXPECTED_LITERAL_NAME,
                       sizeof(OS_TEST_SHELL_EXECUTION_EXPECTED_LITERAL_NAME) -
                           OS_TEST_SHELL_EXECUTION_STRING_TERMINATOR_SIZE_BYTES) &&
        plan.arguments[4].length_bytes == OS_TEST_SHELL_EXECUTION_EMPTY_VALUE &&
        ArgumentEquals(plan, plan.arguments[5], OS_TEST_SHELL_EXECUTION_EXPECTED_STATUS,
                       sizeof(OS_TEST_SHELL_EXECUTION_EXPECTED_STATUS) -
                           OS_TEST_SHELL_EXECUTION_STRING_TERMINATOR_SIZE_BYTES) &&
        ArgumentEquals(plan, plan.arguments[6], OS_TEST_SHELL_EXECUTION_EXPECTED_CPP_PATTERN,
                       sizeof(OS_TEST_SHELL_EXECUTION_EXPECTED_CPP_PATTERN) -
                           OS_TEST_SHELL_EXECUTION_STRING_TERMINATOR_SIZE_BYTES) &&
        os::user::ShellExecutionArgumentHasGlob(plan, 6ULL) &&
        ArgumentEquals(plan, plan.arguments[7], OS_TEST_SHELL_EXECUTION_EXPECTED_QUESTION,
                       sizeof(OS_TEST_SHELL_EXECUTION_EXPECTED_QUESTION) -
                           OS_TEST_SHELL_EXECUTION_STRING_TERMINATOR_SIZE_BYTES) &&
        !os::user::ShellExecutionArgumentHasGlob(plan, 7ULL) &&
        os::user::ParseShellExecutionPlanExpanded(
            OS_TEST_SHELL_EXECUTION_UNQUOTED_PATTERN_LINE,
            sizeof(OS_TEST_SHELL_EXECUTION_UNQUOTED_PATTERN_LINE) -
                OS_TEST_SHELL_EXECUTION_STRING_TERMINATOR_SIZE_BYTES,
            expansion_context, plan) == os::user::ShellExecutionParseStatus::Succeeded &&
        os::user::ShellExecutionArgumentHasGlob(plan, 1ULL);
    test_context.Expect(expansion_valid, OS_TEST_SHELL_EXECUTION_EXPANSION);

    const bool expansion_failures_atomic =
        os::user::ParseShellExecutionPlanExpanded(
            OS_TEST_SHELL_EXECUTION_INVALID_VARIABLE_LINE,
            sizeof(OS_TEST_SHELL_EXECUTION_INVALID_VARIABLE_LINE) -
                OS_TEST_SHELL_EXECUTION_STRING_TERMINATOR_SIZE_BYTES,
            expansion_context,
            plan) == os::user::ShellExecutionParseStatus::InvalidVariableExpansion &&
        plan.argument_count == OS_TEST_SHELL_EXECUTION_EMPTY_VALUE &&
        os::user::ParseShellExecutionPlanExpanded(
            OS_TEST_SHELL_EXECUTION_UNCLOSED_VARIABLE_LINE,
            sizeof(OS_TEST_SHELL_EXECUTION_UNCLOSED_VARIABLE_LINE) -
                OS_TEST_SHELL_EXECUTION_STRING_TERMINATOR_SIZE_BYTES,
            expansion_context,
            plan) == os::user::ShellExecutionParseStatus::InvalidVariableExpansion &&
        plan.argument_count == OS_TEST_SHELL_EXECUTION_EMPTY_VALUE;
    test_context.Expect(expansion_failures_atomic, OS_TEST_SHELL_EXECUTION_EXPANSION_FAILURES);

    const bool failures_atomic =
        os::user::ParseShellExecutionPlan(OS_TEST_SHELL_EXECUTION_EMPTY_STAGE_LINE,
                                          sizeof(OS_TEST_SHELL_EXECUTION_EMPTY_STAGE_LINE) -
                                              OS_TEST_SHELL_EXECUTION_STRING_TERMINATOR_SIZE_BYTES,
                                          plan) ==
            os::user::ShellExecutionParseStatus::EmptyPipelineStage &&
        plan.stage_count == OS_TEST_SHELL_EXECUTION_EMPTY_VALUE &&
        os::user::ParseShellExecutionPlan(
            OS_TEST_SHELL_EXECUTION_DUPLICATE_REDIRECTION_LINE,
            sizeof(OS_TEST_SHELL_EXECUTION_DUPLICATE_REDIRECTION_LINE) -
                OS_TEST_SHELL_EXECUTION_STRING_TERMINATOR_SIZE_BYTES,
            plan) == os::user::ShellExecutionParseStatus::DuplicateRedirection &&
        plan.stage_count == OS_TEST_SHELL_EXECUTION_EMPTY_VALUE &&
        os::user::ParseShellExecutionPlan(
            OS_TEST_SHELL_EXECUTION_DUPLICATE_ERROR_REDIRECTION_LINE,
            sizeof(OS_TEST_SHELL_EXECUTION_DUPLICATE_ERROR_REDIRECTION_LINE) -
                OS_TEST_SHELL_EXECUTION_STRING_TERMINATOR_SIZE_BYTES,
            plan) == os::user::ShellExecutionParseStatus::DuplicateRedirection &&
        plan.stage_count == OS_TEST_SHELL_EXECUTION_EMPTY_VALUE &&
        os::user::ParseShellExecutionPlan(OS_TEST_SHELL_EXECUTION_MISSING_REDIRECTION_LINE,
                                          sizeof(OS_TEST_SHELL_EXECUTION_MISSING_REDIRECTION_LINE) -
                                              OS_TEST_SHELL_EXECUTION_STRING_TERMINATOR_SIZE_BYTES,
                                          plan) ==
            os::user::ShellExecutionParseStatus::MissingRedirectionPath &&
        plan.stage_count == OS_TEST_SHELL_EXECUTION_EMPTY_VALUE &&
        os::user::ParseShellExecutionPlan(OS_TEST_SHELL_EXECUTION_TOO_MANY_STAGES_LINE,
                                          sizeof(OS_TEST_SHELL_EXECUTION_TOO_MANY_STAGES_LINE) -
                                              OS_TEST_SHELL_EXECUTION_STRING_TERMINATOR_SIZE_BYTES,
                                          plan) ==
            os::user::ShellExecutionParseStatus::TooManyStages &&
        plan.stage_count == OS_TEST_SHELL_EXECUTION_EMPTY_VALUE;
    test_context.Expect(failures_atomic, OS_TEST_SHELL_EXECUTION_FAILURES);
    return test_context.ExitCode();
}

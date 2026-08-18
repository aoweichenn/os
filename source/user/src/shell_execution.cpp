#include "os/user/shell_execution.hpp"

namespace os::user {

namespace {

constexpr uint64_t OS_USER_SHELL_EXECUTION_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_USER_SHELL_EXECUTION_FIRST_STAGE_COUNT = 1ULL;
constexpr uint64_t OS_USER_SHELL_EXECUTION_LAST_STAGE_OFFSET = 1ULL;
constexpr uint64_t OS_USER_SHELL_EXECUTION_LAST_BYTE_OFFSET = 1ULL;
constexpr uint64_t OS_USER_SHELL_EXECUTION_SINGLE_OPERATOR_SIZE_BYTES = 1ULL;
constexpr uint64_t OS_USER_SHELL_EXECUTION_DOUBLE_OPERATOR_SIZE_BYTES = 2ULL;
constexpr char OS_USER_SHELL_EXECUTION_STRING_TERMINATOR = '\0';
constexpr char OS_USER_SHELL_EXECUTION_SPACE = ' ';
constexpr char OS_USER_SHELL_EXECUTION_TAB = '\t';
constexpr char OS_USER_SHELL_EXECUTION_SINGLE_QUOTE = '\'';
constexpr char OS_USER_SHELL_EXECUTION_DOUBLE_QUOTE = '"';
constexpr char OS_USER_SHELL_EXECUTION_ESCAPE = '\\';
constexpr char OS_USER_SHELL_EXECUTION_PIPE = '|';
constexpr char OS_USER_SHELL_EXECUTION_INPUT = '<';
constexpr char OS_USER_SHELL_EXECUTION_OUTPUT = '>';
constexpr char OS_USER_SHELL_EXECUTION_BACKGROUND = '&';
constexpr char OS_USER_SHELL_EXECUTION_SEQUENCE = ';';
constexpr char OS_USER_SHELL_EXECUTION_ERROR_DESCRIPTOR = '2';
constexpr char OS_USER_SHELL_EXECUTION_VARIABLE_PREFIX = '$';
constexpr char OS_USER_SHELL_EXECUTION_VARIABLE_OPEN = '{';
constexpr char OS_USER_SHELL_EXECUTION_VARIABLE_CLOSE = '}';
constexpr char OS_USER_SHELL_EXECUTION_PREVIOUS_STATUS = '?';
constexpr char OS_USER_SHELL_EXECUTION_GLOB_STAR = '*';
constexpr char OS_USER_SHELL_EXECUTION_GLOB_QUESTION = '?';
constexpr uint64_t OS_USER_SHELL_EXECUTION_DECIMAL_RADIX = 10ULL;
constexpr uint64_t OS_USER_SHELL_EXECUTION_DECIMAL_CAPACITY_BYTES = 20ULL;

enum class ShellExecutionQuoteState : uint64_t {
    None,
    Single,
    Double,
};

enum class ShellExecutionPendingRedirection : uint64_t {
    None,
    Input,
    OutputTruncate,
    OutputAppend,
    ErrorTruncate,
    ErrorAppend,
};

[[nodiscard]] bool IsWhitespace(const char character) noexcept {
    return character == OS_USER_SHELL_EXECUTION_SPACE || character == OS_USER_SHELL_EXECUTION_TAB;
}

[[nodiscard]] bool IsOperator(const char character) noexcept {
    return character == OS_USER_SHELL_EXECUTION_PIPE ||
           character == OS_USER_SHELL_EXECUTION_INPUT ||
           character == OS_USER_SHELL_EXECUTION_OUTPUT ||
           character == OS_USER_SHELL_EXECUTION_BACKGROUND;
}

[[nodiscard]] bool IsVariableNameFirstCharacter(const char character) noexcept {
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
           character == '_';
}

[[nodiscard]] bool IsVariableNameCharacter(const char character) noexcept {
    return IsVariableNameFirstCharacter(character) || (character >= '0' && character <= '9');
}

[[nodiscard]] uint8_t GlobFlagForCharacter(const char character, const bool glob_enabled) noexcept {
    if (!glob_enabled) {
        return 0U;
    }
    if (character == OS_USER_SHELL_EXECUTION_GLOB_STAR) {
        return OS_USER_SHELL_STORAGE_GLOB_STAR_FLAG;
    }
    if (character == OS_USER_SHELL_EXECUTION_GLOB_QUESTION) {
        return OS_USER_SHELL_STORAGE_GLOB_QUESTION_FLAG;
    }
    return 0U;
}

[[nodiscard]] ShellExecutionParseStatus AppendWordByte(ShellExecutionPlan &parsed_plan,
                                                       ShellArgument &argument,
                                                       uint64_t &write_index, const char character,
                                                       const bool glob_enabled) noexcept {
    if (write_index >= OS_USER_SHELL_EXECUTION_STORAGE_SIZE_BYTES) {
        return ShellExecutionParseStatus::LineTooLong;
    }
    parsed_plan.storage[write_index] = character;
    parsed_plan.storage_flags[write_index] = GlobFlagForCharacter(character, glob_enabled);
    ++write_index;
    ++argument.length_bytes;
    return ShellExecutionParseStatus::Succeeded;
}

[[nodiscard]] ShellExecutionParseStatus
AppendWordBytes(ShellExecutionPlan &parsed_plan, ShellArgument &argument, uint64_t &write_index,
                const char *const bytes, const uint64_t length_bytes,
                const bool glob_enabled) noexcept {
    if (bytes == nullptr && length_bytes != OS_USER_SHELL_EXECUTION_EMPTY_VALUE) {
        return ShellExecutionParseStatus::InvalidArgument;
    }
    for (uint64_t byte_index = OS_USER_SHELL_EXECUTION_EMPTY_VALUE; byte_index < length_bytes;
         ++byte_index) {
        const ShellExecutionParseStatus status =
            AppendWordByte(parsed_plan, argument, write_index, bytes[byte_index], glob_enabled);
        if (status != ShellExecutionParseStatus::Succeeded) {
            return status;
        }
    }
    return ShellExecutionParseStatus::Succeeded;
}

[[nodiscard]] ShellExecutionParseStatus AppendPreviousExitCode(ShellExecutionPlan &parsed_plan,
                                                               ShellArgument &argument,
                                                               uint64_t &write_index,
                                                               const int64_t previous_exit_code,
                                                               const bool glob_enabled) noexcept {
    char reversed[OS_USER_SHELL_EXECUTION_DECIMAL_CAPACITY_BYTES]{};
    char output[OS_USER_SHELL_EXECUTION_DECIMAL_CAPACITY_BYTES]{};
    uint64_t value = previous_exit_code < 0LL ? 1ULL : static_cast<uint64_t>(previous_exit_code);
    uint64_t digit_count = OS_USER_SHELL_EXECUTION_EMPTY_VALUE;
    do {
        reversed[digit_count] =
            static_cast<char>('0' + value % OS_USER_SHELL_EXECUTION_DECIMAL_RADIX);
        value /= OS_USER_SHELL_EXECUTION_DECIMAL_RADIX;
        ++digit_count;
    } while (value != OS_USER_SHELL_EXECUTION_EMPTY_VALUE &&
             digit_count < OS_USER_SHELL_EXECUTION_DECIMAL_CAPACITY_BYTES);
    for (uint64_t digit_index = OS_USER_SHELL_EXECUTION_EMPTY_VALUE; digit_index < digit_count;
         ++digit_index) {
        output[digit_index] = reversed[digit_count - digit_index - 1ULL];
    }
    return AppendWordBytes(parsed_plan, argument, write_index, output, digit_count, glob_enabled);
}

[[nodiscard]] ShellExecutionParseStatus
AppendVariable(const char *const line, const uint64_t line_length_bytes, uint64_t &read_index,
               const ShellExpansionContext *const expansion_context,
               ShellExecutionPlan &parsed_plan, ShellArgument &argument, uint64_t &write_index,
               const bool glob_enabled) noexcept {
    ++read_index;
    if (read_index >= line_length_bytes) {
        return AppendWordByte(parsed_plan, argument, write_index,
                              OS_USER_SHELL_EXECUTION_VARIABLE_PREFIX, glob_enabled);
    }
    if (line[read_index] == OS_USER_SHELL_EXECUTION_PREVIOUS_STATUS) {
        ++read_index;
        const int64_t previous_exit_code =
            expansion_context == nullptr ? 0LL : expansion_context->previous_exit_code;
        return AppendPreviousExitCode(parsed_plan, argument, write_index, previous_exit_code,
                                      glob_enabled);
    }

    uint64_t name_begin_index = read_index;
    uint64_t name_end_index = read_index;
    if (line[read_index] == OS_USER_SHELL_EXECUTION_VARIABLE_OPEN) {
        ++name_begin_index;
        name_end_index = name_begin_index;
        while (name_end_index < line_length_bytes &&
               line[name_end_index] != OS_USER_SHELL_EXECUTION_VARIABLE_CLOSE) {
            ++name_end_index;
        }
        if (name_end_index >= line_length_bytes || name_begin_index == name_end_index) {
            return ShellExecutionParseStatus::InvalidVariableExpansion;
        }
        read_index = name_end_index + 1ULL;
    } else {
        if (!IsVariableNameFirstCharacter(line[read_index])) {
            return AppendWordByte(parsed_plan, argument, write_index,
                                  OS_USER_SHELL_EXECUTION_VARIABLE_PREFIX, glob_enabled);
        }
        while (name_end_index < line_length_bytes &&
               IsVariableNameCharacter(line[name_end_index])) {
            ++name_end_index;
        }
        read_index = name_end_index;
    }
    if (!IsVariableNameFirstCharacter(line[name_begin_index])) {
        return ShellExecutionParseStatus::InvalidVariableExpansion;
    }
    for (uint64_t byte_index = name_begin_index + 1ULL; byte_index < name_end_index; ++byte_index) {
        if (!IsVariableNameCharacter(line[byte_index])) {
            return ShellExecutionParseStatus::InvalidVariableExpansion;
        }
    }

    const char *value = nullptr;
    uint64_t value_length_bytes = OS_USER_SHELL_EXECUTION_EMPTY_VALUE;
    if (expansion_context == nullptr || expansion_context->lookup_operation == nullptr ||
        !expansion_context->lookup_operation(expansion_context->context, line + name_begin_index,
                                             name_end_index - name_begin_index, value,
                                             value_length_bytes)) {
        return ShellExecutionParseStatus::Succeeded;
    }
    return AppendWordBytes(parsed_plan, argument, write_index, value, value_length_bytes,
                           glob_enabled);
}

void TrimCommandSpan(const char *const line, uint64_t &begin_index, uint64_t &end_index) noexcept {
    while (begin_index < end_index && IsWhitespace(line[begin_index])) {
        ++begin_index;
    }
    while (end_index > begin_index &&
           IsWhitespace(line[end_index - OS_USER_SHELL_EXECUTION_LAST_BYTE_OFFSET])) {
        --end_index;
    }
}

[[nodiscard]] ShellExecutionParseStatus
AppendSequenceCommand(const char *const line, uint64_t begin_index, uint64_t end_index,
                      const ShellExecutionCondition condition,
                      ShellExecutionSequence &parsed_sequence) noexcept {
    TrimCommandSpan(line, begin_index, end_index);
    if (begin_index == end_index) {
        return ShellExecutionParseStatus::EmptyCommand;
    }
    if (parsed_sequence.command_count >= OS_USER_SHELL_EXECUTION_MAXIMUM_COMMAND_COUNT) {
        return ShellExecutionParseStatus::TooManyCommands;
    }
    parsed_sequence.commands[parsed_sequence.command_count] = ShellExecutionCommand{
        .offset_bytes = static_cast<uint16_t>(begin_index),
        .length_bytes = static_cast<uint16_t>(end_index - begin_index),
        .condition = condition,
    };
    ++parsed_sequence.command_count;
    return ShellExecutionParseStatus::Succeeded;
}

[[nodiscard]] ShellExecutionParseStatus
ParseWord(const char *const line, const uint64_t line_length_bytes, uint64_t &read_index,
          const ShellExpansionContext *const expansion_context, ShellExecutionPlan &parsed_plan,
          ShellArgument &argument, uint64_t &write_index) noexcept {
    argument = ShellArgument{
        .offset_bytes = static_cast<uint16_t>(write_index),
        .length_bytes = OS_USER_SHELL_EXECUTION_EMPTY_VALUE,
    };
    ShellExecutionQuoteState quote_state = ShellExecutionQuoteState::None;
    bool escaping = false;
    bool word_started = false;

    while (read_index < line_length_bytes) {
        const char character = line[read_index];
        if (escaping) {
            const ShellExecutionParseStatus append_status =
                AppendWordByte(parsed_plan, argument, write_index, character, false);
            if (append_status != ShellExecutionParseStatus::Succeeded) {
                return append_status;
            }
            ++read_index;
            escaping = false;
            word_started = true;
            continue;
        }
        if (quote_state != ShellExecutionQuoteState::Single &&
            character == OS_USER_SHELL_EXECUTION_ESCAPE) {
            escaping = true;
            ++read_index;
            word_started = true;
            continue;
        }
        if (quote_state == ShellExecutionQuoteState::None &&
            character == OS_USER_SHELL_EXECUTION_SINGLE_QUOTE) {
            quote_state = ShellExecutionQuoteState::Single;
            ++read_index;
            word_started = true;
            continue;
        }
        if (quote_state == ShellExecutionQuoteState::Single &&
            character == OS_USER_SHELL_EXECUTION_SINGLE_QUOTE) {
            quote_state = ShellExecutionQuoteState::None;
            ++read_index;
            continue;
        }
        if (quote_state == ShellExecutionQuoteState::None &&
            character == OS_USER_SHELL_EXECUTION_DOUBLE_QUOTE) {
            quote_state = ShellExecutionQuoteState::Double;
            ++read_index;
            word_started = true;
            continue;
        }
        if (quote_state == ShellExecutionQuoteState::Double &&
            character == OS_USER_SHELL_EXECUTION_DOUBLE_QUOTE) {
            quote_state = ShellExecutionQuoteState::None;
            ++read_index;
            continue;
        }
        if (quote_state == ShellExecutionQuoteState::None &&
            (IsWhitespace(character) || IsOperator(character))) {
            break;
        }
        if (quote_state != ShellExecutionQuoteState::Single &&
            character == OS_USER_SHELL_EXECUTION_VARIABLE_PREFIX) {
            const ShellExecutionParseStatus expansion_status = AppendVariable(
                line, line_length_bytes, read_index, expansion_context, parsed_plan, argument,
                write_index, quote_state == ShellExecutionQuoteState::None);
            if (expansion_status != ShellExecutionParseStatus::Succeeded) {
                return expansion_status;
            }
            word_started = true;
            continue;
        }
        const ShellExecutionParseStatus append_status =
            AppendWordByte(parsed_plan, argument, write_index, character,
                           quote_state == ShellExecutionQuoteState::None);
        if (append_status != ShellExecutionParseStatus::Succeeded) {
            return append_status;
        }
        ++read_index;
        word_started = true;
    }

    if (escaping) {
        return ShellExecutionParseStatus::DanglingEscape;
    }
    if (quote_state != ShellExecutionQuoteState::None) {
        return ShellExecutionParseStatus::UnterminatedQuote;
    }
    if (!word_started || write_index >= OS_USER_SHELL_EXECUTION_STORAGE_SIZE_BYTES) {
        return ShellExecutionParseStatus::InvalidArgument;
    }
    parsed_plan.storage[write_index] = OS_USER_SHELL_EXECUTION_STRING_TERMINATOR;
    ++write_index;
    return ShellExecutionParseStatus::Succeeded;
}

}

ShellExecutionParseStatus
ParseShellExecutionSequence(const char *const line, const uint64_t line_length_bytes,
                            ShellExecutionSequence &execution_sequence) noexcept {
    execution_sequence = ShellExecutionSequence{};
    if (line == nullptr && line_length_bytes != OS_USER_SHELL_EXECUTION_EMPTY_VALUE) {
        return ShellExecutionParseStatus::InvalidArgument;
    }
    if (line_length_bytes > OS_USER_SHELL_EXECUTION_MAXIMUM_LINE_SIZE_BYTES) {
        return ShellExecutionParseStatus::LineTooLong;
    }

    ShellExecutionSequence parsed_sequence{};
    ShellExecutionQuoteState quote_state = ShellExecutionQuoteState::None;
    ShellExecutionCondition next_condition = ShellExecutionCondition::Always;
    uint64_t command_begin_index = OS_USER_SHELL_EXECUTION_EMPTY_VALUE;
    uint64_t read_index = OS_USER_SHELL_EXECUTION_EMPTY_VALUE;
    bool escaping = false;
    bool trailing_sequence_operator = false;
    while (read_index < line_length_bytes) {
        const char character = line[read_index];
        if (escaping) {
            escaping = false;
            ++read_index;
            continue;
        }
        if (quote_state != ShellExecutionQuoteState::Single &&
            character == OS_USER_SHELL_EXECUTION_ESCAPE) {
            escaping = true;
            ++read_index;
            continue;
        }
        if (quote_state == ShellExecutionQuoteState::None &&
            character == OS_USER_SHELL_EXECUTION_SINGLE_QUOTE) {
            quote_state = ShellExecutionQuoteState::Single;
            ++read_index;
            continue;
        }
        if (quote_state == ShellExecutionQuoteState::Single &&
            character == OS_USER_SHELL_EXECUTION_SINGLE_QUOTE) {
            quote_state = ShellExecutionQuoteState::None;
            ++read_index;
            continue;
        }
        if (quote_state == ShellExecutionQuoteState::None &&
            character == OS_USER_SHELL_EXECUTION_DOUBLE_QUOTE) {
            quote_state = ShellExecutionQuoteState::Double;
            ++read_index;
            continue;
        }
        if (quote_state == ShellExecutionQuoteState::Double &&
            character == OS_USER_SHELL_EXECUTION_DOUBLE_QUOTE) {
            quote_state = ShellExecutionQuoteState::None;
            ++read_index;
            continue;
        }
        if (quote_state != ShellExecutionQuoteState::None) {
            ++read_index;
            continue;
        }

        uint64_t operator_length_bytes = OS_USER_SHELL_EXECUTION_EMPTY_VALUE;
        ShellExecutionCondition following_condition = ShellExecutionCondition::Always;
        if (character == OS_USER_SHELL_EXECUTION_SEQUENCE) {
            operator_length_bytes = OS_USER_SHELL_EXECUTION_SINGLE_OPERATOR_SIZE_BYTES;
        } else if (read_index + OS_USER_SHELL_EXECUTION_LAST_BYTE_OFFSET < line_length_bytes &&
                   character == OS_USER_SHELL_EXECUTION_BACKGROUND &&
                   line[read_index + OS_USER_SHELL_EXECUTION_LAST_BYTE_OFFSET] ==
                       OS_USER_SHELL_EXECUTION_BACKGROUND) {
            operator_length_bytes = OS_USER_SHELL_EXECUTION_DOUBLE_OPERATOR_SIZE_BYTES;
            following_condition = ShellExecutionCondition::OnSuccess;
        } else if (read_index + OS_USER_SHELL_EXECUTION_LAST_BYTE_OFFSET < line_length_bytes &&
                   character == OS_USER_SHELL_EXECUTION_PIPE &&
                   line[read_index + OS_USER_SHELL_EXECUTION_LAST_BYTE_OFFSET] ==
                       OS_USER_SHELL_EXECUTION_PIPE) {
            operator_length_bytes = OS_USER_SHELL_EXECUTION_DOUBLE_OPERATOR_SIZE_BYTES;
            following_condition = ShellExecutionCondition::OnFailure;
        }
        if (operator_length_bytes == OS_USER_SHELL_EXECUTION_EMPTY_VALUE) {
            ++read_index;
            continue;
        }

        const ShellExecutionParseStatus append_status = AppendSequenceCommand(
            line, command_begin_index, read_index, next_condition, parsed_sequence);
        if (append_status != ShellExecutionParseStatus::Succeeded) {
            return append_status;
        }
        trailing_sequence_operator = character == OS_USER_SHELL_EXECUTION_SEQUENCE;
        next_condition = following_condition;
        read_index += operator_length_bytes;
        command_begin_index = read_index;
    }

    if (escaping) {
        return ShellExecutionParseStatus::DanglingEscape;
    }
    if (quote_state != ShellExecutionQuoteState::None) {
        return ShellExecutionParseStatus::UnterminatedQuote;
    }

    uint64_t final_begin_index = command_begin_index;
    uint64_t final_end_index = line_length_bytes;
    TrimCommandSpan(line, final_begin_index, final_end_index);
    if (final_begin_index == final_end_index) {
        if (parsed_sequence.command_count == OS_USER_SHELL_EXECUTION_EMPTY_VALUE) {
            return ShellExecutionParseStatus::Empty;
        }
        if (!trailing_sequence_operator) {
            return ShellExecutionParseStatus::DanglingControlOperator;
        }
        execution_sequence = parsed_sequence;
        return ShellExecutionParseStatus::Succeeded;
    }

    const ShellExecutionParseStatus append_status = AppendSequenceCommand(
        line, final_begin_index, final_end_index, next_condition, parsed_sequence);
    if (append_status != ShellExecutionParseStatus::Succeeded) {
        return append_status;
    }
    execution_sequence = parsed_sequence;
    return ShellExecutionParseStatus::Succeeded;
}

namespace {

[[nodiscard]] ShellExecutionParseStatus
ParseShellExecutionPlanInternal(const char *const line, const uint64_t line_length_bytes,
                                const ShellExpansionContext *const expansion_context,
                                ShellExecutionPlan &execution_plan) noexcept {
    execution_plan = ShellExecutionPlan{};
    if (line == nullptr && line_length_bytes != OS_USER_SHELL_EXECUTION_EMPTY_VALUE) {
        return ShellExecutionParseStatus::InvalidArgument;
    }
    if (line_length_bytes > OS_USER_SHELL_EXECUTION_MAXIMUM_LINE_SIZE_BYTES) {
        return ShellExecutionParseStatus::LineTooLong;
    }

    ShellExecutionPlan parsed_plan{};
    parsed_plan.stage_count = OS_USER_SHELL_EXECUTION_FIRST_STAGE_COUNT;
    ShellExecutionPendingRedirection pending_redirection = ShellExecutionPendingRedirection::None;
    uint64_t read_index = OS_USER_SHELL_EXECUTION_EMPTY_VALUE;
    uint64_t write_index = OS_USER_SHELL_EXECUTION_EMPTY_VALUE;

    while (read_index < line_length_bytes) {
        while (read_index < line_length_bytes && IsWhitespace(line[read_index])) {
            ++read_index;
        }
        if (read_index >= line_length_bytes) {
            break;
        }

        ShellExecutionStage &stage =
            parsed_plan.stages[parsed_plan.stage_count - OS_USER_SHELL_EXECUTION_LAST_STAGE_OFFSET];
        const char character = line[read_index];
        if (character == OS_USER_SHELL_EXECUTION_BACKGROUND) {
            if (pending_redirection != ShellExecutionPendingRedirection::None ||
                stage.argument_count == OS_USER_SHELL_EXECUTION_EMPTY_VALUE) {
                return ShellExecutionParseStatus::InvalidArgument;
            }
            ++read_index;
            while (read_index < line_length_bytes && IsWhitespace(line[read_index])) {
                ++read_index;
            }
            if (read_index != line_length_bytes) {
                return ShellExecutionParseStatus::BackgroundOperatorNotLast;
            }
            parsed_plan.background = true;
            break;
        }
        if (character == OS_USER_SHELL_EXECUTION_PIPE) {
            if (pending_redirection != ShellExecutionPendingRedirection::None) {
                return ShellExecutionParseStatus::MissingRedirectionPath;
            }
            if (stage.argument_count == OS_USER_SHELL_EXECUTION_EMPTY_VALUE) {
                return ShellExecutionParseStatus::EmptyPipelineStage;
            }
            if (parsed_plan.stage_count >= OS_USER_SHELL_EXECUTION_MAXIMUM_STAGE_COUNT) {
                return ShellExecutionParseStatus::TooManyStages;
            }
            ++parsed_plan.stage_count;
            parsed_plan.stages[parsed_plan.stage_count - OS_USER_SHELL_EXECUTION_LAST_STAGE_OFFSET]
                .first_argument_index = parsed_plan.argument_count;
            ++read_index;
            continue;
        }
        const bool error_redirection =
            character == OS_USER_SHELL_EXECUTION_ERROR_DESCRIPTOR &&
            read_index + OS_USER_SHELL_EXECUTION_LAST_BYTE_OFFSET < line_length_bytes &&
            line[read_index + OS_USER_SHELL_EXECUTION_LAST_BYTE_OFFSET] ==
                OS_USER_SHELL_EXECUTION_OUTPUT;
        if (character == OS_USER_SHELL_EXECUTION_INPUT ||
            character == OS_USER_SHELL_EXECUTION_OUTPUT || error_redirection) {
            if (pending_redirection != ShellExecutionPendingRedirection::None) {
                return ShellExecutionParseStatus::MissingRedirectionPath;
            }
            const bool input = character == OS_USER_SHELL_EXECUTION_INPUT;
            const uint64_t output_operator_index =
                read_index + (error_redirection ? OS_USER_SHELL_EXECUTION_LAST_BYTE_OFFSET
                                                : OS_USER_SHELL_EXECUTION_EMPTY_VALUE);
            const bool append =
                !input &&
                output_operator_index + OS_USER_SHELL_EXECUTION_LAST_BYTE_OFFSET <
                    line_length_bytes &&
                line[output_operator_index + OS_USER_SHELL_EXECUTION_LAST_BYTE_OFFSET] ==
                    OS_USER_SHELL_EXECUTION_OUTPUT;
            if ((input && stage.has_input_redirection) ||
                (!input && !error_redirection &&
                 stage.output_redirection != ShellRedirectionMode::None) ||
                (error_redirection && stage.error_redirection != ShellRedirectionMode::None)) {
                return ShellExecutionParseStatus::DuplicateRedirection;
            }
            pending_redirection = input ? ShellExecutionPendingRedirection::Input
                                  : error_redirection
                                      ? (append ? ShellExecutionPendingRedirection::ErrorAppend
                                                : ShellExecutionPendingRedirection::ErrorTruncate)
                                  : append ? ShellExecutionPendingRedirection::OutputAppend
                                           : ShellExecutionPendingRedirection::OutputTruncate;
            read_index = output_operator_index +
                         (append ? OS_USER_SHELL_EXECUTION_DOUBLE_OPERATOR_SIZE_BYTES
                                 : OS_USER_SHELL_EXECUTION_SINGLE_OPERATOR_SIZE_BYTES);
            continue;
        }

        ShellArgument parsed_argument{};
        const ShellExecutionParseStatus word_status =
            ParseWord(line, line_length_bytes, read_index, expansion_context, parsed_plan,
                      parsed_argument, write_index);
        if (word_status != ShellExecutionParseStatus::Succeeded) {
            return word_status;
        }
        if (pending_redirection == ShellExecutionPendingRedirection::Input) {
            stage.has_input_redirection = true;
            stage.input_path = parsed_argument;
            pending_redirection = ShellExecutionPendingRedirection::None;
            continue;
        }
        if (pending_redirection == ShellExecutionPendingRedirection::OutputTruncate ||
            pending_redirection == ShellExecutionPendingRedirection::OutputAppend) {
            stage.output_redirection =
                pending_redirection == ShellExecutionPendingRedirection::OutputAppend
                    ? ShellRedirectionMode::Append
                    : ShellRedirectionMode::Truncate;
            stage.output_path = parsed_argument;
            pending_redirection = ShellExecutionPendingRedirection::None;
            continue;
        }
        if (pending_redirection == ShellExecutionPendingRedirection::ErrorTruncate ||
            pending_redirection == ShellExecutionPendingRedirection::ErrorAppend) {
            stage.error_redirection =
                pending_redirection == ShellExecutionPendingRedirection::ErrorAppend
                    ? ShellRedirectionMode::Append
                    : ShellRedirectionMode::Truncate;
            stage.error_path = parsed_argument;
            pending_redirection = ShellExecutionPendingRedirection::None;
            continue;
        }
        if (stage.argument_count >= OS_USER_SHELL_EXECUTION_MAXIMUM_ARGUMENTS_PER_STAGE ||
            parsed_plan.argument_count >= OS_USER_SHELL_EXECUTION_MAXIMUM_ARGUMENT_COUNT) {
            return ShellExecutionParseStatus::TooManyArguments;
        }
        parsed_plan.arguments[parsed_plan.argument_count] = parsed_argument;
        ++parsed_plan.argument_count;
        ++stage.argument_count;
    }

    if (pending_redirection != ShellExecutionPendingRedirection::None) {
        return ShellExecutionParseStatus::MissingRedirectionPath;
    }
    if (parsed_plan.argument_count == OS_USER_SHELL_EXECUTION_EMPTY_VALUE) {
        return ShellExecutionParseStatus::Empty;
    }
    if (parsed_plan.stages[parsed_plan.stage_count - OS_USER_SHELL_EXECUTION_LAST_STAGE_OFFSET]
            .argument_count == OS_USER_SHELL_EXECUTION_EMPTY_VALUE) {
        return ShellExecutionParseStatus::EmptyPipelineStage;
    }
    execution_plan = parsed_plan;
    return ShellExecutionParseStatus::Succeeded;
}

}

ShellExecutionParseStatus ParseShellExecutionPlan(const char *const line,
                                                  const uint64_t line_length_bytes,
                                                  ShellExecutionPlan &execution_plan) noexcept {
    return ParseShellExecutionPlanInternal(line, line_length_bytes, nullptr, execution_plan);
}

ShellExecutionParseStatus
ParseShellExecutionPlanExpanded(const char *const line, const uint64_t line_length_bytes,
                                const ShellExpansionContext &expansion_context,
                                ShellExecutionPlan &execution_plan) noexcept {
    return ParseShellExecutionPlanInternal(line, line_length_bytes, &expansion_context,
                                           execution_plan);
}

const char *ShellExecutionArgumentBytes(const ShellExecutionPlan &execution_plan,
                                        const uint64_t argument_index) noexcept {
    if (argument_index >= execution_plan.argument_count) {
        return nullptr;
    }
    return execution_plan.storage + execution_plan.arguments[argument_index].offset_bytes;
}

bool ShellExecutionArgumentHasGlob(const ShellExecutionPlan &execution_plan,
                                   const uint64_t argument_index) noexcept {
    if (argument_index >= execution_plan.argument_count) {
        return false;
    }
    const ShellArgument &argument = execution_plan.arguments[argument_index];
    for (uint64_t byte_index = OS_USER_SHELL_EXECUTION_EMPTY_VALUE;
         byte_index < argument.length_bytes; ++byte_index) {
        if (execution_plan.storage_flags[argument.offset_bytes + byte_index] != 0U) {
            return true;
        }
    }
    return false;
}

}

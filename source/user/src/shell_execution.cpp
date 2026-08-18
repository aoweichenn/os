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
          ShellExecutionPlan &parsed_plan, ShellArgument &argument,
          uint64_t &write_index) noexcept {
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
            if (write_index >= OS_USER_SHELL_EXECUTION_STORAGE_SIZE_BYTES) {
                return ShellExecutionParseStatus::LineTooLong;
            }
            parsed_plan.storage[write_index] = character;
            ++write_index;
            ++argument.length_bytes;
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
        if (write_index >= OS_USER_SHELL_EXECUTION_STORAGE_SIZE_BYTES) {
            return ShellExecutionParseStatus::LineTooLong;
        }
        parsed_plan.storage[write_index] = character;
        ++write_index;
        ++argument.length_bytes;
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

ShellExecutionParseStatus ParseShellExecutionPlan(const char *const line,
                                                  const uint64_t line_length_bytes,
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
        const ShellExecutionParseStatus word_status = ParseWord(
            line, line_length_bytes, read_index, parsed_plan, parsed_argument, write_index);
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

const char *ShellExecutionArgumentBytes(const ShellExecutionPlan &execution_plan,
                                        const uint64_t argument_index) noexcept {
    if (argument_index >= execution_plan.argument_count) {
        return nullptr;
    }
    return execution_plan.storage + execution_plan.arguments[argument_index].offset_bytes;
}

}

#include "os/user/shell_execution.hpp"

namespace os::user {

namespace {

constexpr uint64_t OS_USER_SHELL_EXECUTION_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_USER_SHELL_EXECUTION_FIRST_STAGE_COUNT = 1ULL;
constexpr uint64_t OS_USER_SHELL_EXECUTION_LAST_STAGE_OFFSET = 1ULL;
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

enum class ShellExecutionQuoteState : uint64_t {
    None,
    Single,
    Double,
};

enum class ShellExecutionPendingRedirection : uint64_t {
    None,
    Input,
    Output,
};

[[nodiscard]] bool IsWhitespace(const char character) noexcept {
    return character == OS_USER_SHELL_EXECUTION_SPACE ||
           character == OS_USER_SHELL_EXECUTION_TAB;
}

[[nodiscard]] bool IsOperator(const char character) noexcept {
    return character == OS_USER_SHELL_EXECUTION_PIPE ||
           character == OS_USER_SHELL_EXECUTION_INPUT ||
           character == OS_USER_SHELL_EXECUTION_OUTPUT ||
           character == OS_USER_SHELL_EXECUTION_BACKGROUND;
}

[[nodiscard]] ShellExecutionParseStatus ParseWord(
    const char *const line, const uint64_t line_length_bytes, uint64_t &read_index,
    ShellExecutionPlan &parsed_plan, ShellArgument &argument,
    uint64_t &write_index) noexcept {
    argument = ShellArgument{
        .offset_bytes = write_index,
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

ShellExecutionParseStatus ParseShellExecutionPlan(
    const char *const line, const uint64_t line_length_bytes,
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
    ShellExecutionPendingRedirection pending_redirection =
        ShellExecutionPendingRedirection::None;
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
            parsed_plan.stages[parsed_plan.stage_count -
                               OS_USER_SHELL_EXECUTION_LAST_STAGE_OFFSET];
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
            parsed_plan
                .stages[parsed_plan.stage_count -
                        OS_USER_SHELL_EXECUTION_LAST_STAGE_OFFSET]
                .first_argument_index = parsed_plan.argument_count;
            ++read_index;
            continue;
        }
        if (character == OS_USER_SHELL_EXECUTION_INPUT ||
            character == OS_USER_SHELL_EXECUTION_OUTPUT) {
            if (pending_redirection != ShellExecutionPendingRedirection::None) {
                return ShellExecutionParseStatus::MissingRedirectionPath;
            }
            const bool input = character == OS_USER_SHELL_EXECUTION_INPUT;
            if ((input && stage.has_input_redirection) ||
                (!input && stage.has_output_redirection)) {
                return ShellExecutionParseStatus::DuplicateRedirection;
            }
            pending_redirection = input ? ShellExecutionPendingRedirection::Input
                                        : ShellExecutionPendingRedirection::Output;
            ++read_index;
            continue;
        }

        ShellArgument parsed_argument{};
        const ShellExecutionParseStatus word_status =
            ParseWord(line, line_length_bytes, read_index, parsed_plan, parsed_argument,
                      write_index);
        if (word_status != ShellExecutionParseStatus::Succeeded) {
            return word_status;
        }
        if (pending_redirection == ShellExecutionPendingRedirection::Input) {
            stage.has_input_redirection = true;
            stage.input_path = parsed_argument;
            pending_redirection = ShellExecutionPendingRedirection::None;
            continue;
        }
        if (pending_redirection == ShellExecutionPendingRedirection::Output) {
            stage.has_output_redirection = true;
            stage.output_path = parsed_argument;
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
    if (parsed_plan
            .stages[parsed_plan.stage_count -
                    OS_USER_SHELL_EXECUTION_LAST_STAGE_OFFSET]
            .argument_count ==
        OS_USER_SHELL_EXECUTION_EMPTY_VALUE) {
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

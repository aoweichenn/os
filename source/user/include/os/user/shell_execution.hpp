#pragma once

#include "os/user/shell_parser.hpp"

#include <stdint.h>

namespace os::user {

inline constexpr uint64_t OS_USER_SHELL_EXECUTION_MAXIMUM_LINE_SIZE_BYTES = 512ULL;
inline constexpr uint64_t OS_USER_SHELL_EXECUTION_MAXIMUM_STAGE_COUNT = 16ULL;
inline constexpr uint64_t OS_USER_SHELL_EXECUTION_MAXIMUM_COMMAND_COUNT = 8ULL;
inline constexpr uint64_t OS_USER_SHELL_EXECUTION_MAXIMUM_ARGUMENTS_PER_STAGE = 8ULL;
inline constexpr uint64_t OS_USER_SHELL_EXECUTION_MAXIMUM_ARGUMENT_COUNT =
    OS_USER_SHELL_EXECUTION_MAXIMUM_STAGE_COUNT *
    OS_USER_SHELL_EXECUTION_MAXIMUM_ARGUMENTS_PER_STAGE;
inline constexpr uint64_t OS_USER_SHELL_EXECUTION_STORAGE_TERMINATOR_SIZE_BYTES = 1ULL;
inline constexpr uint64_t OS_USER_SHELL_EXECUTION_STORAGE_SIZE_BYTES =
    OS_USER_SHELL_EXECUTION_MAXIMUM_LINE_SIZE_BYTES +
    OS_USER_SHELL_EXECUTION_STORAGE_TERMINATOR_SIZE_BYTES;
inline constexpr uint64_t OS_USER_SHELL_EXECUTION_STACK_FRAME_LIMIT_BYTES = 4096ULL;

enum class ShellExecutionParseStatus : uint64_t {
    Succeeded,
    Empty,
    LineTooLong,
    TooManyStages,
    TooManyArguments,
    EmptyPipelineStage,
    MissingRedirectionPath,
    DuplicateRedirection,
    UnterminatedQuote,
    DanglingEscape,
    BackgroundOperatorNotLast,
    TooManyCommands,
    EmptyCommand,
    DanglingControlOperator,
    InvalidVariableExpansion,
    InvalidArgument,
};

using ShellVariableLookupOperation = bool (*)(void *context, const char *name,
                                              uint64_t name_length_bytes, const char *&value,
                                              uint64_t &value_length_bytes) noexcept;

struct ShellExpansionContext final {
    void *context;
    ShellVariableLookupOperation lookup_operation;
    int64_t previous_exit_code;
};

inline constexpr uint8_t OS_USER_SHELL_STORAGE_GLOB_STAR_FLAG = 1U << 0U;
inline constexpr uint8_t OS_USER_SHELL_STORAGE_GLOB_QUESTION_FLAG = 1U << 1U;

enum class ShellExecutionCondition : uint8_t {
    Always,
    OnSuccess,
    OnFailure,
};

struct ShellExecutionCommand final {
    uint16_t offset_bytes;
    uint16_t length_bytes;
    ShellExecutionCondition condition;
};

struct ShellExecutionSequence final {
    ShellExecutionCommand commands[OS_USER_SHELL_EXECUTION_MAXIMUM_COMMAND_COUNT];
    uint64_t command_count;
};

enum class ShellRedirectionMode : uint8_t {
    None,
    Truncate,
    Append,
};

struct ShellExecutionStage final {
    uint64_t first_argument_index;
    uint64_t argument_count;
    bool has_input_redirection;
    ShellArgument input_path;
    ShellRedirectionMode output_redirection;
    ShellArgument output_path;
    ShellRedirectionMode error_redirection;
    ShellArgument error_path;
};

struct ShellExecutionPlan final {
    char storage[OS_USER_SHELL_EXECUTION_STORAGE_SIZE_BYTES];
    uint8_t storage_flags[OS_USER_SHELL_EXECUTION_STORAGE_SIZE_BYTES];
    ShellArgument arguments[OS_USER_SHELL_EXECUTION_MAXIMUM_ARGUMENT_COUNT];
    ShellExecutionStage stages[OS_USER_SHELL_EXECUTION_MAXIMUM_STAGE_COUNT];
    uint64_t argument_count;
    uint64_t stage_count;
    bool background;
};

// 解析器会在用户栈上构造临时计划；单个栈帧必须小于一页，避免跨越按需增长边界。
static_assert(OS_USER_SHELL_EXECUTION_STORAGE_SIZE_BYTES <= UINT16_MAX);
static_assert(sizeof(ShellExecutionPlan) <= OS_USER_SHELL_EXECUTION_STACK_FRAME_LIMIT_BYTES);

[[nodiscard]] ShellExecutionParseStatus
ParseShellExecutionPlan(const char *line, uint64_t line_length_bytes,
                        ShellExecutionPlan &execution_plan) noexcept;
[[nodiscard]] ShellExecutionParseStatus
ParseShellExecutionPlanExpanded(const char *line, uint64_t line_length_bytes,
                                const ShellExpansionContext &expansion_context,
                                ShellExecutionPlan &execution_plan) noexcept;
[[nodiscard]] ShellExecutionParseStatus
ParseShellExecutionSequence(const char *line, uint64_t line_length_bytes,
                            ShellExecutionSequence &execution_sequence) noexcept;
[[nodiscard]] const char *ShellExecutionArgumentBytes(const ShellExecutionPlan &execution_plan,
                                                      uint64_t argument_index) noexcept;
[[nodiscard]] bool ShellExecutionArgumentHasGlob(const ShellExecutionPlan &execution_plan,
                                                 uint64_t argument_index) noexcept;

}

#include "os/user/shell.hpp"

#include "os/abi/system_call.hpp"
#include "os/user/shell_execution.hpp"
#include "os/user/system_call.hpp"

namespace os::user {

namespace {

constexpr uint64_t OS_USER_SHELL_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_USER_SHELL_FIRST_VALUE = 1ULL;
constexpr uint64_t OS_USER_SHELL_BUILTIN_PARAMETER_INDEX = 1ULL;
constexpr uint64_t OS_USER_SHELL_CD_ARGUMENT_COUNT = 2ULL;
constexpr uint64_t OS_USER_SHELL_EXIT_ARGUMENT_COUNT = 1ULL;
constexpr uint64_t OS_USER_SHELL_PIPE_COUNT_OFFSET = 1ULL;
constexpr uint64_t OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES = 1ULL;
constexpr uint64_t OS_USER_SHELL_PATH_CAPACITY_BYTES =
    os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_PATH_SIZE_BYTES;
constexpr uint64_t OS_USER_SHELL_NONINTERACTIVE_ARGUMENT_COUNT = 3ULL;
constexpr uint64_t OS_USER_SHELL_NONINTERACTIVE_OPTION_INDEX = 1ULL;
constexpr uint64_t OS_USER_SHELL_NONINTERACTIVE_COMMAND_INDEX = 2ULL;
constexpr int64_t OS_USER_SHELL_SUCCESS_RESULT = 0LL;
constexpr int64_t OS_USER_SHELL_FAILURE_EXIT_CODE = 1LL;
constexpr int64_t OS_USER_SHELL_COMMAND_NOT_FOUND_RESULT = 127LL;
constexpr int64_t OS_USER_SHELL_CHILD_SETUP_FAILURE_EXIT_CODE = 126LL;
constexpr uint8_t OS_USER_SHELL_NEWLINE_CHARACTER = static_cast<uint8_t>('\n');
constexpr uint8_t OS_USER_SHELL_CARRIAGE_RETURN_CHARACTER = static_cast<uint8_t>('\r');
constexpr uint8_t OS_USER_SHELL_BACKSPACE_CHARACTER = static_cast<uint8_t>('\b');
constexpr uint8_t OS_USER_SHELL_DELETE_CHARACTER = 0x7FU;
constexpr uint8_t OS_USER_SHELL_FIRST_PRINTABLE_CHARACTER = 0x20U;
constexpr uint8_t OS_USER_SHELL_LAST_PRINTABLE_CHARACTER = 0x7EU;
constexpr char OS_USER_SHELL_STRING_TERMINATOR = '\0';
constexpr char OS_USER_SHELL_PATH_SEPARATOR = '/';
constexpr char OS_USER_SHELL_BINARY_PREFIX[] = "/bin/";
constexpr char OS_USER_SHELL_HELP_COMMAND[] = "help";
constexpr char OS_USER_SHELL_ECHO_COMMAND[] = "echo";
constexpr char OS_USER_SHELL_PWD_COMMAND[] = "pwd";
constexpr char OS_USER_SHELL_CD_COMMAND[] = "cd";
constexpr char OS_USER_SHELL_LIST_COMMAND[] = "ls";
constexpr char OS_USER_SHELL_MKDIR_COMMAND[] = "mkdir";
constexpr char OS_USER_SHELL_WRITE_COMMAND[] = "write";
constexpr char OS_USER_SHELL_CAT_COMMAND[] = "cat";
constexpr char OS_USER_SHELL_REMOVE_COMMAND[] = "rm";
constexpr char OS_USER_SHELL_REMOVE_DIRECTORY_COMMAND[] = "rmdir";
constexpr char OS_USER_SHELL_MOVE_COMMAND[] = "mv";
constexpr char OS_USER_SHELL_TRUNCATE_COMMAND[] = "truncate";
constexpr char OS_USER_SHELL_STAT_COMMAND[] = "stat";
constexpr char OS_USER_SHELL_SYNC_COMMAND[] = "sync";
constexpr char OS_USER_SHELL_EXIT_COMMAND[] = "exit";
constexpr char OS_USER_SHELL_NONINTERACTIVE_OPTION[] = "-c";
constexpr char OS_USER_SHELL_BANNER[] =
    "\r\nx86-64 OS Lab v1.13\r\n"
    "外置工具、重定向与最多 16 级管线已经启用；输入 help 查看帮助。\r\n";
constexpr char OS_USER_SHELL_READY_MARKER[] = "[OS][USER][SHELL] READY\r\n";
constexpr char OS_USER_SHELL_PROMPT_PREFIX[] = "[os:";
constexpr char OS_USER_SHELL_PROMPT_SUFFIX[] = "]$ ";
constexpr char OS_USER_SHELL_NEWLINE[] = "\r\n";
constexpr char OS_USER_SHELL_BACKSPACE_SEQUENCE[] = "\b \b";
constexpr char OS_USER_SHELL_PARSE_ERROR[] = "error: 命令行语法错误\r\n";
constexpr char OS_USER_SHELL_LINE_TOO_LONG_ERROR[] = "error: 命令行超过 512 字节\r\n";
constexpr char OS_USER_SHELL_OPERATION_ERROR[] = "error: 命令执行失败\r\n";
constexpr char OS_USER_SHELL_USAGE_ERROR[] = "error: 参数数量不正确\r\n";
constexpr char OS_USER_SHELL_UNKNOWN_COMMAND_MARKER[] =
    "[OS][USER][SHELL] UNKNOWN_COMMAND_REJECTED\r\n";
constexpr char OS_USER_SHELL_EXIT_MARKER[] = "[OS][USER][SHELL] EXIT\r\n";
constexpr char OS_USER_SHELL_PIPELINE_VERIFIED_MARKER[] =
    "[OS][USER][SHELL] PIPELINE_16_VERIFIED\r\n";
constexpr char OS_USER_SHELL_REDIRECTION_VERIFIED_MARKER[] =
    "[OS][USER][SHELL] REDIRECTION_VERIFIED\r\n";
constexpr char OS_USER_SHELL_COMMAND_HELP_MARKER[] = "[OS][USER][SHELL] COMMAND=HELP\r\n";
constexpr char OS_USER_SHELL_COMMAND_ECHO_MARKER[] = "[OS][USER][SHELL] COMMAND=ECHO\r\n";
constexpr char OS_USER_SHELL_COMMAND_PWD_MARKER[] = "[OS][USER][SHELL] COMMAND=PWD\r\n";
constexpr char OS_USER_SHELL_COMMAND_CD_MARKER[] = "[OS][USER][SHELL] COMMAND=CD\r\n";
constexpr char OS_USER_SHELL_COMMAND_LS_MARKER[] = "[OS][USER][SHELL] COMMAND=LS\r\n";
constexpr char OS_USER_SHELL_COMMAND_MKDIR_MARKER[] = "[OS][USER][SHELL] COMMAND=MKDIR\r\n";
constexpr char OS_USER_SHELL_COMMAND_WRITE_MARKER[] = "[OS][USER][SHELL] COMMAND=WRITE\r\n";
constexpr char OS_USER_SHELL_COMMAND_CAT_MARKER[] = "[OS][USER][SHELL] COMMAND=CAT\r\n";
constexpr char OS_USER_SHELL_COMMAND_RM_MARKER[] = "[OS][USER][SHELL] COMMAND=RM\r\n";
constexpr char OS_USER_SHELL_COMMAND_RMDIR_MARKER[] = "[OS][USER][SHELL] COMMAND=RMDIR\r\n";
constexpr char OS_USER_SHELL_COMMAND_MV_MARKER[] = "[OS][USER][SHELL] COMMAND=MV\r\n";
constexpr char OS_USER_SHELL_COMMAND_TRUNCATE_MARKER[] = "[OS][USER][SHELL] COMMAND=TRUNCATE\r\n";
constexpr char OS_USER_SHELL_COMMAND_STAT_MARKER[] = "[OS][USER][SHELL] COMMAND=STAT\r\n";
constexpr char OS_USER_SHELL_COMMAND_SYNC_MARKER[] = "[OS][USER][SHELL] COMMAND=SYNC\r\n";
constexpr char OS_USER_SHELL_COMMAND_EXIT_MARKER[] = "[OS][USER][SHELL] COMMAND=EXIT\r\n";

struct ShellCommandMarker final {
    const char *command;
    uint64_t command_length_bytes;
    const char *marker;
    uint64_t marker_length_bytes;
};

constexpr ShellCommandMarker OS_USER_SHELL_COMMAND_MARKERS[]{
    {OS_USER_SHELL_HELP_COMMAND,
     sizeof(OS_USER_SHELL_HELP_COMMAND) - OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES,
     OS_USER_SHELL_COMMAND_HELP_MARKER,
     sizeof(OS_USER_SHELL_COMMAND_HELP_MARKER) - OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES},
    {OS_USER_SHELL_ECHO_COMMAND,
     sizeof(OS_USER_SHELL_ECHO_COMMAND) - OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES,
     OS_USER_SHELL_COMMAND_ECHO_MARKER,
     sizeof(OS_USER_SHELL_COMMAND_ECHO_MARKER) - OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES},
    {OS_USER_SHELL_PWD_COMMAND,
     sizeof(OS_USER_SHELL_PWD_COMMAND) - OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES,
     OS_USER_SHELL_COMMAND_PWD_MARKER,
     sizeof(OS_USER_SHELL_COMMAND_PWD_MARKER) - OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES},
    {OS_USER_SHELL_CD_COMMAND,
     sizeof(OS_USER_SHELL_CD_COMMAND) - OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES,
     OS_USER_SHELL_COMMAND_CD_MARKER,
     sizeof(OS_USER_SHELL_COMMAND_CD_MARKER) - OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES},
    {OS_USER_SHELL_LIST_COMMAND,
     sizeof(OS_USER_SHELL_LIST_COMMAND) - OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES,
     OS_USER_SHELL_COMMAND_LS_MARKER,
     sizeof(OS_USER_SHELL_COMMAND_LS_MARKER) - OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES},
    {OS_USER_SHELL_MKDIR_COMMAND,
     sizeof(OS_USER_SHELL_MKDIR_COMMAND) - OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES,
     OS_USER_SHELL_COMMAND_MKDIR_MARKER,
     sizeof(OS_USER_SHELL_COMMAND_MKDIR_MARKER) - OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES},
    {OS_USER_SHELL_WRITE_COMMAND,
     sizeof(OS_USER_SHELL_WRITE_COMMAND) - OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES,
     OS_USER_SHELL_COMMAND_WRITE_MARKER,
     sizeof(OS_USER_SHELL_COMMAND_WRITE_MARKER) - OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES},
    {OS_USER_SHELL_CAT_COMMAND,
     sizeof(OS_USER_SHELL_CAT_COMMAND) - OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES,
     OS_USER_SHELL_COMMAND_CAT_MARKER,
     sizeof(OS_USER_SHELL_COMMAND_CAT_MARKER) - OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES},
    {OS_USER_SHELL_REMOVE_COMMAND,
     sizeof(OS_USER_SHELL_REMOVE_COMMAND) - OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES,
     OS_USER_SHELL_COMMAND_RM_MARKER,
     sizeof(OS_USER_SHELL_COMMAND_RM_MARKER) - OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES},
    {OS_USER_SHELL_REMOVE_DIRECTORY_COMMAND,
     sizeof(OS_USER_SHELL_REMOVE_DIRECTORY_COMMAND) - OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES,
     OS_USER_SHELL_COMMAND_RMDIR_MARKER,
     sizeof(OS_USER_SHELL_COMMAND_RMDIR_MARKER) - OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES},
    {OS_USER_SHELL_MOVE_COMMAND,
     sizeof(OS_USER_SHELL_MOVE_COMMAND) - OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES,
     OS_USER_SHELL_COMMAND_MV_MARKER,
     sizeof(OS_USER_SHELL_COMMAND_MV_MARKER) - OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES},
    {OS_USER_SHELL_TRUNCATE_COMMAND,
     sizeof(OS_USER_SHELL_TRUNCATE_COMMAND) - OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES,
     OS_USER_SHELL_COMMAND_TRUNCATE_MARKER,
     sizeof(OS_USER_SHELL_COMMAND_TRUNCATE_MARKER) - OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES},
    {OS_USER_SHELL_STAT_COMMAND,
     sizeof(OS_USER_SHELL_STAT_COMMAND) - OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES,
     OS_USER_SHELL_COMMAND_STAT_MARKER,
     sizeof(OS_USER_SHELL_COMMAND_STAT_MARKER) - OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES},
    {OS_USER_SHELL_SYNC_COMMAND,
     sizeof(OS_USER_SHELL_SYNC_COMMAND) - OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES,
     OS_USER_SHELL_COMMAND_SYNC_MARKER,
     sizeof(OS_USER_SHELL_COMMAND_SYNC_MARKER) - OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES},
    {OS_USER_SHELL_EXIT_COMMAND,
     sizeof(OS_USER_SHELL_EXIT_COMMAND) - OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES,
     OS_USER_SHELL_COMMAND_EXIT_MARKER,
     sizeof(OS_USER_SHELL_COMMAND_EXIT_MARKER) - OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES},
};

[[nodiscard]] uint64_t StringLength(const char *const text,
                                    const uint64_t capacity_bytes) noexcept {
    if (text == nullptr) {
        return OS_USER_SHELL_EMPTY_VALUE;
    }
    uint64_t length_bytes = OS_USER_SHELL_EMPTY_VALUE;
    while (length_bytes < capacity_bytes && text[length_bytes] != OS_USER_SHELL_STRING_TERMINATOR) {
        ++length_bytes;
    }
    return length_bytes;
}

[[nodiscard]] bool BytesEqual(const char *const first, const uint64_t first_length_bytes,
                              const char *const second,
                              const uint64_t second_length_bytes) noexcept {
    if (first == nullptr || second == nullptr || first_length_bytes != second_length_bytes) {
        return false;
    }
    for (uint64_t byte_index = OS_USER_SHELL_EMPTY_VALUE; byte_index < first_length_bytes;
         ++byte_index) {
        if (first[byte_index] != second[byte_index]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool WriteBytes(const char *const bytes, const uint64_t length_bytes) noexcept {
    return bytes != nullptr && WriteDescriptor(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR,
                                               reinterpret_cast<const uint8_t *>(bytes),
                                               length_bytes) == static_cast<int64_t>(length_bytes);
}

template <uint64_t SizeBytes>
[[nodiscard]] bool WriteLiteral(const char (&literal)[SizeBytes]) noexcept {
    return WriteBytes(literal, SizeBytes - OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES);
}

[[nodiscard]] bool WriteCommandMarker(const ShellExecutionPlan &execution_plan) noexcept {
    const ShellExecutionStage &first_stage = execution_plan.stages[OS_USER_SHELL_EMPTY_VALUE];
    const uint64_t command_index = first_stage.first_argument_index;
    const char *const command = ShellExecutionArgumentBytes(execution_plan, command_index);
    const uint64_t command_length_bytes = execution_plan.arguments[command_index].length_bytes;
    for (const ShellCommandMarker &entry : OS_USER_SHELL_COMMAND_MARKERS) {
        if (BytesEqual(command, command_length_bytes, entry.command, entry.command_length_bytes)) {
            return WriteBytes(entry.marker, entry.marker_length_bytes);
        }
    }
    return true;
}

[[nodiscard]] bool BuildExecutablePath(const char *const command,
                                       const uint64_t command_length_bytes, char *const path,
                                       uint64_t &path_length_bytes) noexcept {
    if (command == nullptr || path == nullptr ||
        command_length_bytes == OS_USER_SHELL_EMPTY_VALUE) {
        return false;
    }
    bool contains_separator = false;
    for (uint64_t byte_index = OS_USER_SHELL_EMPTY_VALUE; byte_index < command_length_bytes;
         ++byte_index) {
        contains_separator =
            contains_separator || command[byte_index] == OS_USER_SHELL_PATH_SEPARATOR;
    }
    const uint64_t prefix_length_bytes =
        sizeof(OS_USER_SHELL_BINARY_PREFIX) - OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES;
    const uint64_t required_length_bytes =
        command_length_bytes +
        (contains_separator ? OS_USER_SHELL_EMPTY_VALUE : prefix_length_bytes);
    if (required_length_bytes + OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES >
        OS_USER_SHELL_PATH_CAPACITY_BYTES) {
        return false;
    }
    path_length_bytes = OS_USER_SHELL_EMPTY_VALUE;
    if (!contains_separator) {
        for (uint64_t byte_index = OS_USER_SHELL_EMPTY_VALUE; byte_index < prefix_length_bytes;
             ++byte_index) {
            path[path_length_bytes] = OS_USER_SHELL_BINARY_PREFIX[byte_index];
            ++path_length_bytes;
        }
    }
    for (uint64_t byte_index = OS_USER_SHELL_EMPTY_VALUE; byte_index < command_length_bytes;
         ++byte_index) {
        path[path_length_bytes] = command[byte_index];
        ++path_length_bytes;
    }
    path[path_length_bytes] = OS_USER_SHELL_STRING_TERMINATOR;
    return true;
}

void ClosePipelineDescriptors(os::abi::PipeDescriptorPair *const pipes,
                              const uint64_t pipe_count) noexcept {
    if (pipes == nullptr) {
        return;
    }
    for (uint64_t pipe_index = OS_USER_SHELL_EMPTY_VALUE; pipe_index < pipe_count; ++pipe_index) {
        if (pipes[pipe_index].reader_descriptor != UINT64_MAX) {
            static_cast<void>(CloseDescriptor(pipes[pipe_index].reader_descriptor));
            pipes[pipe_index].reader_descriptor = UINT64_MAX;
        }
        if (pipes[pipe_index].writer_descriptor != UINT64_MAX) {
            static_cast<void>(CloseDescriptor(pipes[pipe_index].writer_descriptor));
            pipes[pipe_index].writer_descriptor = UINT64_MAX;
        }
    }
}

[[nodiscard]] bool DuplicateForChild(const uint64_t source_descriptor,
                                     const uint64_t destination_descriptor) noexcept {
    return DuplicateDescriptorTo(source_descriptor, destination_descriptor,
                                 OS_USER_SHELL_EMPTY_VALUE) ==
           static_cast<int64_t>(destination_descriptor);
}

[[noreturn]] void ExecuteChildStage(const ShellExecutionPlan &execution_plan,
                                    const uint64_t stage_index,
                                    os::abi::PipeDescriptorPair *const pipes,
                                    const uint64_t pipe_count) noexcept {
    const ShellExecutionStage &stage = execution_plan.stages[stage_index];
    if (stage_index != OS_USER_SHELL_EMPTY_VALUE &&
        !DuplicateForChild(pipes[stage_index - OS_USER_SHELL_PIPE_COUNT_OFFSET].reader_descriptor,
                           os::abi::OS_ABI_STANDARD_INPUT_DESCRIPTOR)) {
        ExitProcess(OS_USER_SHELL_CHILD_SETUP_FAILURE_EXIT_CODE);
    }
    if (stage_index + OS_USER_SHELL_FIRST_VALUE < execution_plan.stage_count &&
        !DuplicateForChild(pipes[stage_index].writer_descriptor,
                           os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR)) {
        ExitProcess(OS_USER_SHELL_CHILD_SETUP_FAILURE_EXIT_CODE);
    }

    if (stage.has_input_redirection) {
        const int64_t descriptor =
            OpenFile(execution_plan.storage + stage.input_path.offset_bytes,
                     stage.input_path.length_bytes, os::abi::OS_ABI_FILE_OPEN_READ_FLAG);
        if (descriptor < OS_USER_SHELL_SUCCESS_RESULT ||
            !DuplicateForChild(static_cast<uint64_t>(descriptor),
                               os::abi::OS_ABI_STANDARD_INPUT_DESCRIPTOR)) {
            ExitProcess(OS_USER_SHELL_CHILD_SETUP_FAILURE_EXIT_CODE);
        }
        static_cast<void>(CloseDescriptor(static_cast<uint64_t>(descriptor)));
    }
    if (stage.has_output_redirection) {
        constexpr uint64_t output_flags = os::abi::OS_ABI_FILE_OPEN_WRITE_FLAG |
                                          os::abi::OS_ABI_FILE_OPEN_CREATE_FLAG |
                                          os::abi::OS_ABI_FILE_OPEN_TRUNCATE_FLAG;
        const int64_t descriptor = OpenFile(execution_plan.storage + stage.output_path.offset_bytes,
                                            stage.output_path.length_bytes, output_flags);
        if (descriptor < OS_USER_SHELL_SUCCESS_RESULT ||
            !DuplicateForChild(static_cast<uint64_t>(descriptor),
                               os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR)) {
            ExitProcess(OS_USER_SHELL_CHILD_SETUP_FAILURE_EXIT_CODE);
        }
        static_cast<void>(CloseDescriptor(static_cast<uint64_t>(descriptor)));
    }
    ClosePipelineDescriptors(pipes, pipe_count);

    char executable_path[OS_USER_SHELL_PATH_CAPACITY_BYTES]{};
    uint64_t executable_path_length_bytes = OS_USER_SHELL_EMPTY_VALUE;
    const uint64_t command_index = stage.first_argument_index;
    if (!BuildExecutablePath(ShellExecutionArgumentBytes(execution_plan, command_index),
                             execution_plan.arguments[command_index].length_bytes, executable_path,
                             executable_path_length_bytes)) {
        ExitProcess(OS_USER_SHELL_COMMAND_NOT_FOUND_RESULT);
    }

    os::abi::ProcessString arguments[OS_USER_SHELL_EXECUTION_MAXIMUM_ARGUMENTS_PER_STAGE]{};
    for (uint64_t argument_index = OS_USER_SHELL_EMPTY_VALUE; argument_index < stage.argument_count;
         ++argument_index) {
        const uint64_t plan_argument_index = stage.first_argument_index + argument_index;
        arguments[argument_index] = os::abi::ProcessString{
            .address = reinterpret_cast<uint64_t>(
                ShellExecutionArgumentBytes(execution_plan, plan_argument_index)),
            .length_bytes = execution_plan.arguments[plan_argument_index].length_bytes,
        };
    }
    const os::abi::ProcessLaunchRequest request{
        .path_address = reinterpret_cast<uint64_t>(executable_path),
        .path_length_bytes = executable_path_length_bytes,
        .argument_vector_address = reinterpret_cast<uint64_t>(arguments),
        .argument_count = stage.argument_count,
        .environment_vector_address = OS_USER_SHELL_EMPTY_VALUE,
        .environment_count = OS_USER_SHELL_EMPTY_VALUE,
    };
    static_cast<void>(ExecProcess(request));
    ExitProcess(OS_USER_SHELL_COMMAND_NOT_FOUND_RESULT);
}

[[nodiscard]] int64_t WaitForPipeline(const uint64_t *const process_ids,
                                      const uint64_t process_count,
                                      int64_t &last_exit_code) noexcept {
    bool wait_succeeded = true;
    last_exit_code = OS_USER_SHELL_FAILURE_EXIT_CODE;
    for (uint64_t process_index = OS_USER_SHELL_EMPTY_VALUE; process_index < process_count;
         ++process_index) {
        os::abi::ProcessWaitResult wait_result{};
        const int64_t result = WaitProcess(process_ids[process_index], wait_result);
        wait_succeeded = wait_succeeded &&
                         result == static_cast<int64_t>(process_ids[process_index]) &&
                         wait_result.process_id == process_ids[process_index];
        if (process_index + OS_USER_SHELL_FIRST_VALUE == process_count &&
            result == static_cast<int64_t>(process_ids[process_index]) &&
            wait_result.termination_reason == os::abi::ProcessTerminationReason::Exited) {
            last_exit_code = wait_result.exit_code;
        }
    }
    return wait_succeeded ? OS_USER_SHELL_SUCCESS_RESULT : OS_USER_SHELL_FAILURE_EXIT_CODE;
}

[[nodiscard]] int64_t ExecuteExternalPipeline(const ShellExecutionPlan &execution_plan) noexcept {
    const uint64_t pipe_count = execution_plan.stage_count - OS_USER_SHELL_PIPE_COUNT_OFFSET;
    os::abi::PipeDescriptorPair
        pipes[OS_USER_SHELL_EXECUTION_MAXIMUM_STAGE_COUNT - OS_USER_SHELL_PIPE_COUNT_OFFSET]{};
    for (uint64_t pipe_index = OS_USER_SHELL_EMPTY_VALUE; pipe_index < pipe_count; ++pipe_index) {
        pipes[pipe_index].reader_descriptor = UINT64_MAX;
        pipes[pipe_index].writer_descriptor = UINT64_MAX;
        if (CreatePipe(pipes[pipe_index]) != OS_USER_SHELL_SUCCESS_RESULT) {
            ClosePipelineDescriptors(pipes, pipe_count);
            return OS_USER_SHELL_FAILURE_EXIT_CODE;
        }
    }

    uint64_t process_ids[OS_USER_SHELL_EXECUTION_MAXIMUM_STAGE_COUNT]{};
    uint64_t process_count = OS_USER_SHELL_EMPTY_VALUE;
    for (uint64_t stage_index = OS_USER_SHELL_EMPTY_VALUE; stage_index < execution_plan.stage_count;
         ++stage_index) {
        const int64_t fork_result = ForkProcess();
        if (fork_result == OS_USER_SHELL_SUCCESS_RESULT) {
            ExecuteChildStage(execution_plan, stage_index, pipes, pipe_count);
        }
        if (fork_result < OS_USER_SHELL_SUCCESS_RESULT) {
            ClosePipelineDescriptors(pipes, pipe_count);
            int64_t ignored_exit_code = OS_USER_SHELL_FAILURE_EXIT_CODE;
            static_cast<void>(WaitForPipeline(process_ids, process_count, ignored_exit_code));
            return OS_USER_SHELL_FAILURE_EXIT_CODE;
        }
        process_ids[process_count] = static_cast<uint64_t>(fork_result);
        ++process_count;
    }
    ClosePipelineDescriptors(pipes, pipe_count);

    int64_t last_exit_code = OS_USER_SHELL_FAILURE_EXIT_CODE;
    if (WaitForPipeline(process_ids, process_count, last_exit_code) !=
        OS_USER_SHELL_SUCCESS_RESULT) {
        return OS_USER_SHELL_FAILURE_EXIT_CODE;
    }
    bool has_redirection = false;
    for (uint64_t stage_index = OS_USER_SHELL_EMPTY_VALUE; stage_index < execution_plan.stage_count;
         ++stage_index) {
        has_redirection = has_redirection ||
                          execution_plan.stages[stage_index].has_input_redirection ||
                          execution_plan.stages[stage_index].has_output_redirection;
    }
    if (has_redirection && last_exit_code == OS_USER_SHELL_SUCCESS_RESULT) {
        static_cast<void>(WriteLiteral(OS_USER_SHELL_REDIRECTION_VERIFIED_MARKER));
    }
    if (execution_plan.stage_count == OS_USER_SHELL_EXECUTION_MAXIMUM_STAGE_COUNT &&
        last_exit_code == OS_USER_SHELL_SUCCESS_RESULT) {
        static_cast<void>(WriteLiteral(OS_USER_SHELL_PIPELINE_VERIFIED_MARKER));
    }
    return last_exit_code;
}

[[nodiscard]] bool IsSingleStageBuiltin(const ShellExecutionPlan &execution_plan,
                                        const char *const command,
                                        const uint64_t command_length_bytes) noexcept {
    if (execution_plan.stage_count != OS_USER_SHELL_FIRST_VALUE) {
        return false;
    }
    const ShellExecutionStage &stage = execution_plan.stages[OS_USER_SHELL_EMPTY_VALUE];
    const uint64_t command_index = stage.first_argument_index;
    return !stage.has_input_redirection && !stage.has_output_redirection &&
           BytesEqual(ShellExecutionArgumentBytes(execution_plan, command_index),
                      execution_plan.arguments[command_index].length_bytes, command,
                      command_length_bytes);
}

[[nodiscard]] int64_t ExecutePlan(const ShellExecutionPlan &execution_plan,
                                  bool &exit_requested) noexcept {
    exit_requested = false;
    if (!WriteCommandMarker(execution_plan)) {
        return OS_USER_SHELL_FAILURE_EXIT_CODE;
    }

    if (IsSingleStageBuiltin(execution_plan, OS_USER_SHELL_CD_COMMAND,
                             sizeof(OS_USER_SHELL_CD_COMMAND) -
                                 OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES)) {
        const ShellExecutionStage &stage = execution_plan.stages[OS_USER_SHELL_EMPTY_VALUE];
        if (stage.argument_count != OS_USER_SHELL_CD_ARGUMENT_COUNT) {
            static_cast<void>(WriteLiteral(OS_USER_SHELL_USAGE_ERROR));
            return OS_USER_SHELL_FAILURE_EXIT_CODE;
        }
        const uint64_t path_index =
            stage.first_argument_index + OS_USER_SHELL_BUILTIN_PARAMETER_INDEX;
        return ChangeDirectory(ShellExecutionArgumentBytes(execution_plan, path_index),
                               execution_plan.arguments[path_index].length_bytes);
    }
    if (IsSingleStageBuiltin(execution_plan, OS_USER_SHELL_EXIT_COMMAND,
                             sizeof(OS_USER_SHELL_EXIT_COMMAND) -
                                 OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES)) {
        const ShellExecutionStage &stage = execution_plan.stages[OS_USER_SHELL_EMPTY_VALUE];
        if (stage.argument_count != OS_USER_SHELL_EXIT_ARGUMENT_COUNT) {
            static_cast<void>(WriteLiteral(OS_USER_SHELL_USAGE_ERROR));
            return OS_USER_SHELL_FAILURE_EXIT_CODE;
        }
        exit_requested = true;
        return OS_USER_SHELL_SUCCESS_RESULT;
    }

    const int64_t result = ExecuteExternalPipeline(execution_plan);
    if (result == OS_USER_SHELL_COMMAND_NOT_FOUND_RESULT) {
        static_cast<void>(WriteLiteral(OS_USER_SHELL_UNKNOWN_COMMAND_MARKER));
    } else if (result != OS_USER_SHELL_SUCCESS_RESULT) {
        static_cast<void>(WriteLiteral(OS_USER_SHELL_OPERATION_ERROR));
    }
    return result;
}

[[nodiscard]] ShellExecutionParseStatus ParseAndExecute(const char *const line,
                                                        const uint64_t line_length_bytes,
                                                        bool &exit_requested,
                                                        int64_t &command_result) noexcept {
    ShellExecutionPlan execution_plan{};
    const ShellExecutionParseStatus parse_status =
        ParseShellExecutionPlan(line, line_length_bytes, execution_plan);
    if (parse_status != ShellExecutionParseStatus::Succeeded) {
        return parse_status;
    }
    command_result = ExecutePlan(execution_plan, exit_requested);
    return ShellExecutionParseStatus::Succeeded;
}

[[nodiscard]] bool WritePrompt() noexcept {
    char path[OS_USER_SHELL_PATH_CAPACITY_BYTES]{};
    const int64_t path_length_bytes = GetWorkingDirectory(path, OS_USER_SHELL_PATH_CAPACITY_BYTES);
    return path_length_bytes > OS_USER_SHELL_SUCCESS_RESULT &&
           WriteLiteral(OS_USER_SHELL_PROMPT_PREFIX) &&
           WriteBytes(path, static_cast<uint64_t>(path_length_bytes)) &&
           WriteLiteral(OS_USER_SHELL_PROMPT_SUFFIX);
}

}

int64_t RunShellCommand(const char *const command, const uint64_t command_length_bytes) noexcept {
    bool exit_requested = false;
    int64_t command_result = OS_USER_SHELL_FAILURE_EXIT_CODE;
    const ShellExecutionParseStatus parse_status =
        ParseAndExecute(command, command_length_bytes, exit_requested, command_result);
    if (parse_status != ShellExecutionParseStatus::Succeeded) {
        const char *const message = parse_status == ShellExecutionParseStatus::LineTooLong
                                        ? OS_USER_SHELL_LINE_TOO_LONG_ERROR
                                        : OS_USER_SHELL_PARSE_ERROR;
        const uint64_t message_length_bytes =
            parse_status == ShellExecutionParseStatus::LineTooLong
                ? sizeof(OS_USER_SHELL_LINE_TOO_LONG_ERROR) -
                      OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES
                : sizeof(OS_USER_SHELL_PARSE_ERROR) - OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES;
        static_cast<void>(WriteBytes(message, message_length_bytes));
        return OS_USER_SHELL_FAILURE_EXIT_CODE;
    }
    return command_result;
}

int64_t RunShell(const uint64_t argument_count, const char *const *const arguments) noexcept {
    if (argument_count == OS_USER_SHELL_NONINTERACTIVE_ARGUMENT_COUNT && arguments != nullptr &&
        BytesEqual(arguments[OS_USER_SHELL_NONINTERACTIVE_OPTION_INDEX],
                   StringLength(arguments[OS_USER_SHELL_NONINTERACTIVE_OPTION_INDEX],
                                OS_USER_SHELL_EXECUTION_MAXIMUM_LINE_SIZE_BYTES),
                   OS_USER_SHELL_NONINTERACTIVE_OPTION,
                   sizeof(OS_USER_SHELL_NONINTERACTIVE_OPTION) -
                       OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES)) {
        const char *const command = arguments[OS_USER_SHELL_NONINTERACTIVE_COMMAND_INDEX];
        return RunShellCommand(
            command, StringLength(command, OS_USER_SHELL_EXECUTION_MAXIMUM_LINE_SIZE_BYTES +
                                               OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES));
    }

    if (!WriteLiteral(OS_USER_SHELL_BANNER) || !WriteLiteral(OS_USER_SHELL_READY_MARKER)) {
        return OS_USER_SHELL_FAILURE_EXIT_CODE;
    }
    char line[OS_USER_SHELL_EXECUTION_MAXIMUM_LINE_SIZE_BYTES]{};
    uint64_t line_length_bytes = OS_USER_SHELL_EMPTY_VALUE;
    while (true) {
        if (!WritePrompt()) {
            return OS_USER_SHELL_FAILURE_EXIT_CODE;
        }
        line_length_bytes = OS_USER_SHELL_EMPTY_VALUE;
        while (true) {
            uint8_t character = OS_USER_SHELL_EMPTY_VALUE;
            const int64_t read_result = ReadDescriptor(os::abi::OS_ABI_STANDARD_INPUT_DESCRIPTOR,
                                                       &character, OS_USER_SHELL_FIRST_VALUE);
            if (read_result != static_cast<int64_t>(OS_USER_SHELL_FIRST_VALUE)) {
                return OS_USER_SHELL_FAILURE_EXIT_CODE;
            }
            if (character == OS_USER_SHELL_NEWLINE_CHARACTER ||
                character == OS_USER_SHELL_CARRIAGE_RETURN_CHARACTER) {
                if (!WriteLiteral(OS_USER_SHELL_NEWLINE)) {
                    return OS_USER_SHELL_FAILURE_EXIT_CODE;
                }
                break;
            }
            if (character == OS_USER_SHELL_BACKSPACE_CHARACTER ||
                character == OS_USER_SHELL_DELETE_CHARACTER) {
                if (line_length_bytes != OS_USER_SHELL_EMPTY_VALUE) {
                    --line_length_bytes;
                    if (!WriteLiteral(OS_USER_SHELL_BACKSPACE_SEQUENCE)) {
                        return OS_USER_SHELL_FAILURE_EXIT_CODE;
                    }
                }
                continue;
            }
            if (character < OS_USER_SHELL_FIRST_PRINTABLE_CHARACTER ||
                character > OS_USER_SHELL_LAST_PRINTABLE_CHARACTER) {
                continue;
            }
            if (line_length_bytes >= OS_USER_SHELL_EXECUTION_MAXIMUM_LINE_SIZE_BYTES) {
                continue;
            }
            line[line_length_bytes] = static_cast<char>(character);
            ++line_length_bytes;
            if (!WriteBytes(reinterpret_cast<const char *>(&character),
                            OS_USER_SHELL_FIRST_VALUE)) {
                return OS_USER_SHELL_FAILURE_EXIT_CODE;
            }
        }

        bool exit_requested = false;
        int64_t command_result = OS_USER_SHELL_SUCCESS_RESULT;
        const ShellExecutionParseStatus parse_status =
            ParseAndExecute(line, line_length_bytes, exit_requested, command_result);
        if (parse_status == ShellExecutionParseStatus::Empty) {
            continue;
        }
        if (parse_status != ShellExecutionParseStatus::Succeeded) {
            const char *const message = parse_status == ShellExecutionParseStatus::LineTooLong
                                            ? OS_USER_SHELL_LINE_TOO_LONG_ERROR
                                            : OS_USER_SHELL_PARSE_ERROR;
            const uint64_t message_length_bytes =
                parse_status == ShellExecutionParseStatus::LineTooLong
                    ? sizeof(OS_USER_SHELL_LINE_TOO_LONG_ERROR) -
                          OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES
                    : sizeof(OS_USER_SHELL_PARSE_ERROR) -
                          OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES;
            if (!WriteBytes(message, message_length_bytes)) {
                return OS_USER_SHELL_FAILURE_EXIT_CODE;
            }
            continue;
        }
        if (exit_requested) {
            return WriteLiteral(OS_USER_SHELL_EXIT_MARKER) ? OS_USER_SHELL_SUCCESS_RESULT
                                                           : OS_USER_SHELL_FAILURE_EXIT_CODE;
        }
        static_cast<void>(command_result);
    }
}

}

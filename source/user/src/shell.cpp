#include "os/user/shell.hpp"

#include "os/abi/system_call.hpp"
#include "os/user/shell_environment.hpp"
#include "os/user/shell_execution.hpp"
#include "os/user/shell_glob.hpp"
#include "os/user/shell_line_editor.hpp"
#include "os/user/system_call.hpp"

namespace os::user {

namespace {

constexpr uint64_t OS_USER_SHELL_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_USER_SHELL_FIRST_VALUE = 1ULL;
constexpr uint64_t OS_USER_SHELL_BUILTIN_PARAMETER_INDEX = 1ULL;
constexpr uint64_t OS_USER_SHELL_CD_ARGUMENT_COUNT = 2ULL;
constexpr uint64_t OS_USER_SHELL_EXIT_ARGUMENT_COUNT = 1ULL;
constexpr uint64_t OS_USER_SHELL_ENVIRONMENT_BUILTIN_ARGUMENT_COUNT = 2ULL;
constexpr uint64_t OS_USER_SHELL_JOBS_ARGUMENT_COUNT = 1ULL;
constexpr uint64_t OS_USER_SHELL_JOB_BUILTIN_MINIMUM_ARGUMENT_COUNT = 1ULL;
constexpr uint64_t OS_USER_SHELL_JOB_BUILTIN_MAXIMUM_ARGUMENT_COUNT = 2ULL;
constexpr uint64_t OS_USER_SHELL_JOB_CAPACITY = 16ULL;
constexpr uint64_t OS_USER_SHELL_JOB_MEMBER_CAPACITY = OS_USER_SHELL_EXECUTION_MAXIMUM_STAGE_COUNT;
constexpr uint64_t OS_USER_SHELL_DECIMAL_BASE = 10ULL;
constexpr uint64_t OS_USER_SHELL_DECIMAL_BUFFER_CAPACITY_BYTES = 20ULL;
constexpr uint64_t OS_USER_SHELL_PIPE_COUNT_OFFSET = 1ULL;
constexpr uint64_t OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES = 1ULL;
constexpr uint64_t OS_USER_SHELL_BINARY_PREFIX_SIZE_BYTES = 5ULL;
constexpr uint64_t OS_USER_SHELL_PATH_CAPACITY_BYTES =
    os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_PATH_SIZE_BYTES;
constexpr uint64_t OS_USER_SHELL_EXECUTABLE_PATH_CAPACITY_BYTES =
    OS_USER_SHELL_BINARY_PREFIX_SIZE_BYTES + OS_USER_SHELL_EXECUTION_MAXIMUM_LINE_SIZE_BYTES +
    OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES;
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
constexpr uint8_t OS_USER_SHELL_ESCAPE_CHARACTER = 0x1BU;
constexpr uint8_t OS_USER_SHELL_TAB_CHARACTER = static_cast<uint8_t>('\t');
constexpr uint8_t OS_USER_SHELL_FIRST_PRINTABLE_CHARACTER = 0x20U;
constexpr uint8_t OS_USER_SHELL_LAST_PRINTABLE_CHARACTER = 0x7EU;
constexpr char OS_USER_SHELL_STRING_TERMINATOR = '\0';
constexpr char OS_USER_SHELL_PATH_SEPARATOR = '/';
constexpr char OS_USER_SHELL_BINARY_PREFIX[] = "/bin/";
constexpr char OS_USER_SHELL_CURSOR_LEFT_SEQUENCE[] = "\x1b[D";
constexpr char OS_USER_SHELL_CURSOR_RIGHT_SEQUENCE[] = "\x1b[C";
constexpr char OS_USER_SHELL_EDITOR_REDRAW_SEQUENCE[] = "\r\x1b[2K";
constexpr char OS_USER_SHELL_EDITOR_BACKSPACE_SEQUENCE[] = "\b \b";
constexpr char OS_USER_SHELL_EDITOR_NEWLINE_SEQUENCE[] = "\r\n";
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
constexpr char OS_USER_SHELL_JOBS_COMMAND[] = "jobs";
constexpr char OS_USER_SHELL_FOREGROUND_COMMAND[] = "fg";
constexpr char OS_USER_SHELL_BACKGROUND_COMMAND[] = "bg";
constexpr char OS_USER_SHELL_EXPORT_COMMAND[] = "export";
constexpr char OS_USER_SHELL_UNSET_COMMAND[] = "unset";
constexpr char OS_USER_SHELL_NONINTERACTIVE_OPTION[] = "-c";
constexpr char OS_USER_SHELL_BANNER[] =
    "\r\nx86-64 OS Lab v2.2\r\n"
    "行编辑、环境变量、通配符、完整重定向与 43 个用户工具已经启用。\r\n";
constexpr char OS_USER_SHELL_READY_MARKER[] = "[OS][USER][SHELL] READY\r\n";
constexpr char OS_USER_SHELL_PROMPT_PREFIX[] = "[os:";
constexpr char OS_USER_SHELL_PROMPT_SUFFIX[] = "]$ ";
constexpr char OS_USER_SHELL_PARSE_ERROR[] = "error: 命令行语法错误\r\n";
constexpr char OS_USER_SHELL_LINE_TOO_LONG_ERROR[] = "error: 命令行超过 512 字节\r\n";
constexpr char OS_USER_SHELL_OPERATION_ERROR[] = "error: 命令执行失败\r\n";
constexpr char OS_USER_SHELL_ENVIRONMENT_ERROR[] = "error: 环境变量无效或容量已满\r\n";
constexpr char OS_USER_SHELL_GLOB_ERROR[] = "error: 通配符展开超过参数上限\r\n";
constexpr char OS_USER_SHELL_JOB_NOT_FOUND_ERROR[] = "error: 作业不存在\r\n";
constexpr char OS_USER_SHELL_JOB_TABLE_FULL_ERROR[] = "error: 作业表已满\r\n";
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
constexpr char OS_USER_SHELL_COMMAND_JOBS_MARKER[] = "[OS][USER][SHELL] COMMAND=JOBS\r\n";
constexpr char OS_USER_SHELL_COMMAND_FOREGROUND_MARKER[] = "[OS][USER][SHELL] COMMAND=FG\r\n";
constexpr char OS_USER_SHELL_COMMAND_BACKGROUND_MARKER[] = "[OS][USER][SHELL] COMMAND=BG\r\n";
constexpr char OS_USER_SHELL_COMMAND_EXIT_MARKER[] = "[OS][USER][SHELL] COMMAND=EXIT\r\n";
constexpr char OS_USER_SHELL_COMMAND_COMPLETE_MARKER[] = "[OS][USER][SHELL] COMMAND_COMPLETE\r\n";
constexpr char OS_USER_SHELL_JOB_PREFIX[] = "[";
constexpr char OS_USER_SHELL_JOB_RUNNING_TEXT[] = "] Running PGID=";
constexpr char OS_USER_SHELL_JOB_STOPPED_TEXT[] = "] Stopped PGID=";
constexpr char OS_USER_SHELL_JOB_DONE_TEXT[] = "] Done PGID=";
constexpr char OS_USER_SHELL_JOB_SEPARATOR[] = "\r\n";
constexpr char OS_USER_SHELL_JOB_STARTED_MARKER[] = "[OS][USER][SHELL] BACKGROUND_JOB_STARTED\r\n";
constexpr char OS_USER_SHELL_JOB_STOPPED_MARKER[] = "[OS][USER][SHELL] FOREGROUND_JOB_STOPPED\r\n";
constexpr char OS_USER_SHELL_FOREGROUND_JOB_WAITING_MARKER[] =
    "[OS][USER][SHELL] FOREGROUND_JOB_WAITING\r\n";
constexpr char OS_USER_SHELL_JOB_CONTROL_READY_MARKER[] = "[OS][USER][SHELL] JOB_CONTROL_READY\r\n";
constexpr uint64_t OS_USER_SHELL_JOB_CONTROL_SIGNAL_SET =
    os::abi::SignalBit(os::abi::OS_ABI_SIGNAL_INTERRUPT_NUMBER) |
    os::abi::SignalBit(os::abi::OS_ABI_SIGNAL_TERMINAL_STOP_NUMBER);

static_assert(OS_USER_SHELL_EXECUTABLE_PATH_CAPACITY_BYTES <= OS_USER_SHELL_PATH_CAPACITY_BYTES);
static_assert(OS_USER_SHELL_EDITOR_LINE_CAPACITY_BYTES ==
              OS_USER_SHELL_EXECUTION_MAXIMUM_LINE_SIZE_BYTES);
static_assert(sizeof(OS_USER_SHELL_BINARY_PREFIX) - OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES ==
              OS_USER_SHELL_BINARY_PREFIX_SIZE_BYTES);

void ShellInteractiveSignalHandler(const uint64_t signal_number,
                                   os::abi::SignalFrame *const signal_frame) noexcept {
    // Shell 只需要让阻塞读取返回 EINTR；信号号与现场由内核完成校验和恢复。
    static_cast<void>(signal_number);
    static_cast<void>(signal_frame);
}

[[nodiscard]] bool InstallShellSignalPolicy() noexcept {
    return InstallSignalHandler(os::abi::OS_ABI_SIGNAL_INTERRUPT_NUMBER,
                                &ShellInteractiveSignalHandler, OS_USER_SHELL_EMPTY_VALUE,
                                OS_USER_SHELL_EMPTY_VALUE,
                                nullptr) == OS_USER_SHELL_SUCCESS_RESULT &&
           InstallSignalHandler(os::abi::OS_ABI_SIGNAL_TERMINAL_STOP_NUMBER,
                                &ShellInteractiveSignalHandler, OS_USER_SHELL_EMPTY_VALUE,
                                OS_USER_SHELL_EMPTY_VALUE, nullptr) == OS_USER_SHELL_SUCCESS_RESULT;
}

[[nodiscard]] bool RestoreChildSignalPolicy() noexcept {
    const os::abi::SignalAction default_action{
        .disposition = os::abi::SignalDisposition::Default,
        .handler_address = OS_USER_SHELL_EMPTY_VALUE,
        .restorer_address = OS_USER_SHELL_EMPTY_VALUE,
        .additional_mask = OS_USER_SHELL_EMPTY_VALUE,
        .flags = OS_USER_SHELL_EMPTY_VALUE,
    };
    return SetSignalAction(os::abi::OS_ABI_SIGNAL_INTERRUPT_NUMBER, default_action, nullptr) ==
               OS_USER_SHELL_SUCCESS_RESULT &&
           SetSignalAction(os::abi::OS_ABI_SIGNAL_TERMINAL_STOP_NUMBER, default_action, nullptr) ==
               OS_USER_SHELL_SUCCESS_RESULT;
}

enum class ShellJobState : uint64_t {
    Free,
    Running,
    Stopped,
    Done,
};

enum class ShellJobMemberState : uint64_t {
    Running,
    Stopped,
    Exited,
};

struct ShellJob final {
    uint64_t job_id;
    uint64_t process_group_id;
    uint64_t process_ids[OS_USER_SHELL_JOB_MEMBER_CAPACITY];
    ShellJobMemberState member_states[OS_USER_SHELL_JOB_MEMBER_CAPACITY];
    uint64_t process_count;
    int64_t last_exit_code;
    ShellJobState state;
};

ShellJob shell_jobs[OS_USER_SHELL_JOB_CAPACITY];
uint64_t next_shell_job_id = OS_USER_SHELL_FIRST_VALUE;
uint64_t shell_process_group_id = OS_USER_SHELL_EMPTY_VALUE;
int64_t shell_last_exit_code = OS_USER_SHELL_SUCCESS_RESULT;

struct ShellPreparedArgument final {
    char bytes[OS_USER_SHELL_EXECUTION_STORAGE_SIZE_BYTES];
    uint16_t length_bytes;
};

ShellEnvironmentTable shell_environment{};
ShellPreparedArgument
    shell_prepared_arguments[OS_USER_SHELL_EXECUTION_MAXIMUM_ARGUMENTS_PER_STAGE]{};
os::abi::ProcessString
    shell_prepared_process_arguments[OS_USER_SHELL_EXECUTION_MAXIMUM_ARGUMENTS_PER_STAGE]{};
os::abi::ProcessString shell_prepared_environment[OS_USER_SHELL_ENVIRONMENT_MAXIMUM_ENTRY_COUNT]{};
char shell_glob_directory_path[OS_USER_SHELL_EXECUTION_STORAGE_SIZE_BYTES]{};
os::abi::DirectoryEntry shell_glob_directory_entry{};
ShellLineEditor shell_line_editor{};

constexpr ShellCompletionCandidate OS_USER_SHELL_COMPLETION_CANDIDATES[]{
    {"help", 4U},     {"echo", 4U},    {"err", 3U},     {"cat", 3U},      {"wc", 2U},
    {"head", 4U},     {"tee", 3U},     {"true", 4U},    {"false", 5U},    {"pwd", 3U},
    {"ls", 2U},       {"stat", 4U},    {"mkdir", 5U},   {"write", 5U},    {"touch", 5U},
    {"rm", 2U},       {"rmdir", 5U},   {"mv", 2U},      {"truncate", 8U}, {"sync", 4U},
    {"basename", 8U}, {"dirname", 7U}, {"cp", 2U},      {"seq", 3U},      {"uptime", 6U},
    {"ps", 2U},       {"free", 4U},    {"uname", 5U},   {"mounts", 6U},   {"resources", 9U},
    {"sleep", 5U},    {"kill", 4U},    {"id", 2U},      {"env", 3U},      {"cd", 2U},
    {"exit", 4U},     {"jobs", 4U},    {"fg", 2U},      {"bg", 2U},       {"export", 6U},
    {"unset", 5U},    {"grep", 4U},    {"find", 4U},    {"sort", 4U},     {"tail", 4U},
    {"df", 2U},       {"du", 2U},      {"hexdump", 7U}, {"clear", 5U},    {"date", 4U},
};

enum class ShellEditorEscapeState : uint8_t {
    Ground,
    Escape,
    ControlSequence,
};

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
    {OS_USER_SHELL_JOBS_COMMAND,
     sizeof(OS_USER_SHELL_JOBS_COMMAND) - OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES,
     OS_USER_SHELL_COMMAND_JOBS_MARKER,
     sizeof(OS_USER_SHELL_COMMAND_JOBS_MARKER) - OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES},
    {OS_USER_SHELL_FOREGROUND_COMMAND,
     sizeof(OS_USER_SHELL_FOREGROUND_COMMAND) - OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES,
     OS_USER_SHELL_COMMAND_FOREGROUND_MARKER,
     sizeof(OS_USER_SHELL_COMMAND_FOREGROUND_MARKER) - OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES},
    {OS_USER_SHELL_BACKGROUND_COMMAND,
     sizeof(OS_USER_SHELL_BACKGROUND_COMMAND) - OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES,
     OS_USER_SHELL_COMMAND_BACKGROUND_MARKER,
     sizeof(OS_USER_SHELL_COMMAND_BACKGROUND_MARKER) - OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES},
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

[[nodiscard]] bool LookupShellVariable(void *const context, const char *const name,
                                       const uint64_t name_length_bytes, const char *&value,
                                       uint64_t &value_length_bytes) noexcept {
    if (context == nullptr) {
        value = nullptr;
        value_length_bytes = OS_USER_SHELL_EMPTY_VALUE;
        return false;
    }
    ShellEnvironmentTable &environment = *static_cast<ShellEnvironmentTable *>(context);
    return environment.Find(name, name_length_bytes, value, value_length_bytes) ==
           ShellEnvironmentStatus::Succeeded;
}

enum class ShellGlobPreparationStatus : uint64_t {
    Succeeded,
    CapacityExceeded,
    IoFailure,
};

[[nodiscard]] bool CopyPreparedArgument(const char *const bytes, const uint64_t length_bytes,
                                        uint64_t &prepared_count) noexcept {
    if ((bytes == nullptr && length_bytes != OS_USER_SHELL_EMPTY_VALUE) ||
        length_bytes >= OS_USER_SHELL_EXECUTION_STORAGE_SIZE_BYTES ||
        prepared_count >= OS_USER_SHELL_EXECUTION_MAXIMUM_ARGUMENTS_PER_STAGE) {
        return false;
    }
    ShellPreparedArgument &destination = shell_prepared_arguments[prepared_count];
    destination = ShellPreparedArgument{};
    for (uint64_t byte_index = OS_USER_SHELL_EMPTY_VALUE; byte_index < length_bytes; ++byte_index) {
        destination.bytes[byte_index] = bytes[byte_index];
    }
    destination.bytes[length_bytes] = OS_USER_SHELL_STRING_TERMINATOR;
    destination.length_bytes = static_cast<uint16_t>(length_bytes);
    ++prepared_count;
    return true;
}

[[nodiscard]] int64_t ComparePreparedArguments(const ShellPreparedArgument &first,
                                               const ShellPreparedArgument &second) noexcept {
    const uint64_t common_length_bytes =
        first.length_bytes < second.length_bytes ? first.length_bytes : second.length_bytes;
    for (uint64_t byte_index = OS_USER_SHELL_EMPTY_VALUE; byte_index < common_length_bytes;
         ++byte_index) {
        if (first.bytes[byte_index] < second.bytes[byte_index]) {
            return -1LL;
        }
        if (first.bytes[byte_index] > second.bytes[byte_index]) {
            return 1LL;
        }
    }
    if (first.length_bytes < second.length_bytes) {
        return -1LL;
    }
    return first.length_bytes == second.length_bytes ? 0LL : 1LL;
}

void SortPreparedArguments(const uint64_t begin_index, const uint64_t end_index) noexcept {
    for (uint64_t insertion_index = begin_index + OS_USER_SHELL_FIRST_VALUE;
         insertion_index < end_index; ++insertion_index) {
        ShellPreparedArgument value = shell_prepared_arguments[insertion_index];
        uint64_t destination_index = insertion_index;
        while (destination_index > begin_index &&
               ComparePreparedArguments(
                   shell_prepared_arguments[destination_index - OS_USER_SHELL_FIRST_VALUE], value) >
                   0LL) {
            shell_prepared_arguments[destination_index] =
                shell_prepared_arguments[destination_index - OS_USER_SHELL_FIRST_VALUE];
            --destination_index;
        }
        shell_prepared_arguments[destination_index] = value;
    }
}

[[nodiscard]] bool IsDotDirectoryEntry(const os::abi::DirectoryEntry &entry) noexcept {
    return (entry.name_length_bytes == OS_USER_SHELL_FIRST_VALUE && entry.name[0] == '.') ||
           (entry.name_length_bytes == 2ULL && entry.name[0] == '.' && entry.name[1] == '.');
}

[[nodiscard]] ShellGlobPreparationStatus
PrepareGlobArgument(const ShellExecutionPlan &execution_plan, const uint64_t argument_index,
                    uint64_t &prepared_count) noexcept {
    const ShellArgument &argument = execution_plan.arguments[argument_index];
    const char *const pattern = ShellExecutionArgumentBytes(execution_plan, argument_index);
    if (!ShellExecutionArgumentHasGlob(execution_plan, argument_index)) {
        return CopyPreparedArgument(pattern, argument.length_bytes, prepared_count)
                   ? ShellGlobPreparationStatus::Succeeded
                   : ShellGlobPreparationStatus::CapacityExceeded;
    }

    uint64_t basename_begin_index = OS_USER_SHELL_EMPTY_VALUE;
    for (uint64_t byte_index = OS_USER_SHELL_EMPTY_VALUE; byte_index < argument.length_bytes;
         ++byte_index) {
        if (pattern[byte_index] == OS_USER_SHELL_PATH_SEPARATOR) {
            basename_begin_index = byte_index + OS_USER_SHELL_FIRST_VALUE;
        }
    }
    for (uint64_t byte_index = OS_USER_SHELL_EMPTY_VALUE; byte_index < basename_begin_index;
         ++byte_index) {
        if (execution_plan.storage_flags[argument.offset_bytes + byte_index] != 0U) {
            return CopyPreparedArgument(pattern, argument.length_bytes, prepared_count)
                       ? ShellGlobPreparationStatus::Succeeded
                       : ShellGlobPreparationStatus::CapacityExceeded;
        }
    }

    const uint64_t directory_length_bytes =
        basename_begin_index == OS_USER_SHELL_EMPTY_VALUE
            ? OS_USER_SHELL_FIRST_VALUE
            : (basename_begin_index == OS_USER_SHELL_FIRST_VALUE
                   ? OS_USER_SHELL_FIRST_VALUE
                   : basename_begin_index - OS_USER_SHELL_FIRST_VALUE);
    if (basename_begin_index == OS_USER_SHELL_EMPTY_VALUE) {
        shell_glob_directory_path[OS_USER_SHELL_EMPTY_VALUE] = '.';
    } else {
        for (uint64_t byte_index = OS_USER_SHELL_EMPTY_VALUE; byte_index < directory_length_bytes;
             ++byte_index) {
            shell_glob_directory_path[byte_index] = pattern[byte_index];
        }
    }
    const int64_t descriptor = OpenDirectory(shell_glob_directory_path, directory_length_bytes);
    if (descriptor < OS_USER_SHELL_SUCCESS_RESULT) {
        return CopyPreparedArgument(pattern, argument.length_bytes, prepared_count)
                   ? ShellGlobPreparationStatus::Succeeded
                   : ShellGlobPreparationStatus::CapacityExceeded;
    }

    const uint64_t match_begin_index = prepared_count;
    ShellGlobPreparationStatus status = ShellGlobPreparationStatus::Succeeded;
    while (true) {
        shell_glob_directory_entry = os::abi::DirectoryEntry{};
        const int64_t read_result =
            ReadDirectory(static_cast<uint64_t>(descriptor), shell_glob_directory_entry);
        if (read_result == OS_USER_SHELL_SUCCESS_RESULT) {
            break;
        }
        if (read_result < OS_USER_SHELL_SUCCESS_RESULT) {
            status = ShellGlobPreparationStatus::IoFailure;
            break;
        }
        if (IsDotDirectoryEntry(shell_glob_directory_entry)) {
            continue;
        }
        const uint64_t pattern_component_length_bytes =
            argument.length_bytes - basename_begin_index;
        const bool pattern_starts_with_literal_dot =
            pattern_component_length_bytes != OS_USER_SHELL_EMPTY_VALUE &&
            pattern[basename_begin_index] == '.' &&
            execution_plan.storage_flags[argument.offset_bytes + basename_begin_index] == 0U;
        if (shell_glob_directory_entry.name_length_bytes != OS_USER_SHELL_EMPTY_VALUE &&
            shell_glob_directory_entry.name[0] == '.' && !pattern_starts_with_literal_dot) {
            continue;
        }
        if (!MatchShellGlobPattern(pattern + basename_begin_index,
                                   execution_plan.storage_flags + argument.offset_bytes +
                                       basename_begin_index,
                                   pattern_component_length_bytes,
                                   reinterpret_cast<const char *>(shell_glob_directory_entry.name),
                                   shell_glob_directory_entry.name_length_bytes)) {
            continue;
        }
        const uint64_t candidate_length_bytes =
            basename_begin_index + shell_glob_directory_entry.name_length_bytes;
        if (candidate_length_bytes >= OS_USER_SHELL_EXECUTION_STORAGE_SIZE_BYTES ||
            prepared_count >= OS_USER_SHELL_EXECUTION_MAXIMUM_ARGUMENTS_PER_STAGE) {
            status = ShellGlobPreparationStatus::CapacityExceeded;
            break;
        }
        ShellPreparedArgument &destination = shell_prepared_arguments[prepared_count];
        destination = ShellPreparedArgument{};
        for (uint64_t byte_index = OS_USER_SHELL_EMPTY_VALUE; byte_index < basename_begin_index;
             ++byte_index) {
            destination.bytes[byte_index] = pattern[byte_index];
        }
        for (uint64_t byte_index = OS_USER_SHELL_EMPTY_VALUE;
             byte_index < shell_glob_directory_entry.name_length_bytes; ++byte_index) {
            destination.bytes[basename_begin_index + byte_index] =
                static_cast<char>(shell_glob_directory_entry.name[byte_index]);
        }
        destination.bytes[candidate_length_bytes] = OS_USER_SHELL_STRING_TERMINATOR;
        destination.length_bytes = static_cast<uint16_t>(candidate_length_bytes);
        ++prepared_count;
    }
    if (CloseDescriptor(static_cast<uint64_t>(descriptor)) != OS_USER_SHELL_SUCCESS_RESULT &&
        status == ShellGlobPreparationStatus::Succeeded) {
        status = ShellGlobPreparationStatus::IoFailure;
    }
    if (status != ShellGlobPreparationStatus::Succeeded) {
        return status;
    }
    if (prepared_count == match_begin_index) {
        return CopyPreparedArgument(pattern, argument.length_bytes, prepared_count)
                   ? ShellGlobPreparationStatus::Succeeded
                   : ShellGlobPreparationStatus::CapacityExceeded;
    }
    SortPreparedArguments(match_begin_index, prepared_count);
    return ShellGlobPreparationStatus::Succeeded;
}

[[nodiscard]] ShellGlobPreparationStatus
PrepareStageArguments(const ShellExecutionPlan &execution_plan, const ShellExecutionStage &stage,
                      uint64_t &prepared_count) noexcept {
    prepared_count = OS_USER_SHELL_EMPTY_VALUE;
    for (uint64_t argument_index = OS_USER_SHELL_EMPTY_VALUE;
         argument_index < OS_USER_SHELL_EXECUTION_MAXIMUM_ARGUMENTS_PER_STAGE; ++argument_index) {
        shell_prepared_arguments[argument_index] = ShellPreparedArgument{};
        shell_prepared_process_arguments[argument_index] = os::abi::ProcessString{};
    }
    for (uint64_t argument_index = OS_USER_SHELL_EMPTY_VALUE; argument_index < stage.argument_count;
         ++argument_index) {
        const ShellGlobPreparationStatus status = PrepareGlobArgument(
            execution_plan, stage.first_argument_index + argument_index, prepared_count);
        if (status != ShellGlobPreparationStatus::Succeeded) {
            return status;
        }
    }
    for (uint64_t argument_index = OS_USER_SHELL_EMPTY_VALUE; argument_index < prepared_count;
         ++argument_index) {
        shell_prepared_process_arguments[argument_index] = os::abi::ProcessString{
            .address = reinterpret_cast<uint64_t>(shell_prepared_arguments[argument_index].bytes),
            .length_bytes = shell_prepared_arguments[argument_index].length_bytes,
        };
    }
    return ShellGlobPreparationStatus::Succeeded;
}

[[nodiscard]] bool PrepareChildEnvironment(uint64_t &environment_count) noexcept {
    environment_count = shell_environment.Count();
    for (uint64_t environment_index = OS_USER_SHELL_EMPTY_VALUE;
         environment_index < OS_USER_SHELL_ENVIRONMENT_MAXIMUM_ENTRY_COUNT; ++environment_index) {
        shell_prepared_environment[environment_index] = os::abi::ProcessString{};
    }
    for (uint64_t environment_index = OS_USER_SHELL_EMPTY_VALUE;
         environment_index < environment_count; ++environment_index) {
        const char *entry = nullptr;
        uint64_t entry_length_bytes = OS_USER_SHELL_EMPTY_VALUE;
        if (shell_environment.Read(environment_index, entry, entry_length_bytes) !=
            ShellEnvironmentStatus::Succeeded) {
            return false;
        }
        shell_prepared_environment[environment_index] = os::abi::ProcessString{
            .address = reinterpret_cast<uint64_t>(entry),
            .length_bytes = entry_length_bytes,
        };
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

[[nodiscard]] bool WriteUnsigned(const uint64_t value) noexcept {
    char reversed[OS_USER_SHELL_DECIMAL_BUFFER_CAPACITY_BYTES]{};
    char output[OS_USER_SHELL_DECIMAL_BUFFER_CAPACITY_BYTES]{};
    uint64_t remaining = value;
    uint64_t digit_count = OS_USER_SHELL_EMPTY_VALUE;
    do {
        reversed[digit_count] = static_cast<char>('0' + remaining % OS_USER_SHELL_DECIMAL_BASE);
        remaining /= OS_USER_SHELL_DECIMAL_BASE;
        ++digit_count;
    } while (remaining != OS_USER_SHELL_EMPTY_VALUE &&
             digit_count < OS_USER_SHELL_DECIMAL_BUFFER_CAPACITY_BYTES);
    for (uint64_t digit_index = OS_USER_SHELL_EMPTY_VALUE; digit_index < digit_count;
         ++digit_index) {
        output[digit_index] = reversed[digit_count - digit_index - OS_USER_SHELL_FIRST_VALUE];
    }
    return WriteBytes(output, digit_count);
}

void InitializeJobTable() noexcept {
    for (uint64_t job_index = OS_USER_SHELL_EMPTY_VALUE; job_index < OS_USER_SHELL_JOB_CAPACITY;
         ++job_index) {
        shell_jobs[job_index] = ShellJob{};
    }
    next_shell_job_id = OS_USER_SHELL_FIRST_VALUE;
}

[[nodiscard]] ShellJob *FindFreeJob() noexcept {
    for (uint64_t job_index = OS_USER_SHELL_EMPTY_VALUE; job_index < OS_USER_SHELL_JOB_CAPACITY;
         ++job_index) {
        if (shell_jobs[job_index].state == ShellJobState::Free) {
            return shell_jobs + job_index;
        }
    }
    return nullptr;
}

[[nodiscard]] ShellJob *FindJob(const uint64_t job_id) noexcept {
    for (uint64_t job_index = OS_USER_SHELL_EMPTY_VALUE; job_index < OS_USER_SHELL_JOB_CAPACITY;
         ++job_index) {
        if (shell_jobs[job_index].state != ShellJobState::Free &&
            shell_jobs[job_index].job_id == job_id) {
            return shell_jobs + job_index;
        }
    }
    return nullptr;
}

[[nodiscard]] ShellJob *FindLatestJob() noexcept {
    ShellJob *latest_job = nullptr;
    for (uint64_t job_index = OS_USER_SHELL_EMPTY_VALUE; job_index < OS_USER_SHELL_JOB_CAPACITY;
         ++job_index) {
        if (shell_jobs[job_index].state != ShellJobState::Free &&
            (latest_job == nullptr || shell_jobs[job_index].job_id > latest_job->job_id)) {
            latest_job = shell_jobs + job_index;
        }
    }
    return latest_job;
}

void ReleaseJob(ShellJob &job) noexcept { job = ShellJob{}; }

[[nodiscard]] bool WriteJob(const ShellJob &job) noexcept {
    const char *state_text = OS_USER_SHELL_JOB_RUNNING_TEXT;
    uint64_t state_text_length =
        sizeof(OS_USER_SHELL_JOB_RUNNING_TEXT) - OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES;
    if (job.state == ShellJobState::Stopped) {
        state_text = OS_USER_SHELL_JOB_STOPPED_TEXT;
        state_text_length =
            sizeof(OS_USER_SHELL_JOB_STOPPED_TEXT) - OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES;
    } else if (job.state == ShellJobState::Done) {
        state_text = OS_USER_SHELL_JOB_DONE_TEXT;
        state_text_length =
            sizeof(OS_USER_SHELL_JOB_DONE_TEXT) - OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES;
    }
    return WriteLiteral(OS_USER_SHELL_JOB_PREFIX) && WriteUnsigned(job.job_id) &&
           WriteBytes(state_text, state_text_length) && WriteUnsigned(job.process_group_id) &&
           WriteLiteral(OS_USER_SHELL_JOB_SEPARATOR);
}

void RefreshJobState(ShellJob &job) noexcept {
    uint64_t live_member_count = OS_USER_SHELL_EMPTY_VALUE;
    uint64_t stopped_member_count = OS_USER_SHELL_EMPTY_VALUE;
    for (uint64_t member_index = OS_USER_SHELL_EMPTY_VALUE; member_index < job.process_count;
         ++member_index) {
        if (job.member_states[member_index] != ShellJobMemberState::Exited) {
            ++live_member_count;
        }
        if (job.member_states[member_index] == ShellJobMemberState::Stopped) {
            ++stopped_member_count;
        }
    }
    if (live_member_count == OS_USER_SHELL_EMPTY_VALUE) {
        job.state = ShellJobState::Done;
    } else if (stopped_member_count == live_member_count) {
        job.state = ShellJobState::Stopped;
    } else {
        job.state = ShellJobState::Running;
    }
}

[[nodiscard]] bool PumpJobEvents(ShellJob &job, const bool no_hang) noexcept {
    const uint64_t wait_flags =
        os::abi::OS_ABI_PROCESS_WAIT_EXITED_FLAG | os::abi::OS_ABI_PROCESS_WAIT_STOPPED_FLAG |
        os::abi::OS_ABI_PROCESS_WAIT_CONTINUED_FLAG |
        (no_hang ? os::abi::OS_ABI_PROCESS_WAIT_NO_HANG_FLAG : OS_USER_SHELL_EMPTY_VALUE);
    for (uint64_t member_index = OS_USER_SHELL_EMPTY_VALUE; member_index < job.process_count;
         ++member_index) {
        if (job.member_states[member_index] == ShellJobMemberState::Exited ||
            (!no_hang && job.member_states[member_index] == ShellJobMemberState::Stopped)) {
            continue;
        }
        while (true) {
            os::abi::ProcessWaitEventResult wait_result{};
            const int64_t result =
                WaitProcessEvent(job.process_ids[member_index], wait_flags, wait_result);
            if (no_hang && result == os::abi::OS_ABI_SYSTEM_CALL_RESULT_WOULD_BLOCK) {
                break;
            }
            if (result != static_cast<int64_t>(job.process_ids[member_index]) ||
                wait_result.process_id != job.process_ids[member_index]) {
                return false;
            }
            if (wait_result.event_type == os::abi::ProcessWaitEventType::Exited) {
                job.member_states[member_index] = ShellJobMemberState::Exited;
                if (member_index + OS_USER_SHELL_FIRST_VALUE == job.process_count &&
                    wait_result.termination_reason == os::abi::ProcessTerminationReason::Exited) {
                    job.last_exit_code = wait_result.exit_code;
                }
                break;
            }
            if (wait_result.event_type == os::abi::ProcessWaitEventType::Stopped) {
                job.member_states[member_index] = ShellJobMemberState::Stopped;
                break;
            }
            job.member_states[member_index] = ShellJobMemberState::Running;
            if (no_hang) {
                continue;
            }
        }
    }
    RefreshJobState(job);
    return true;
}

[[nodiscard]] bool ContinueJob(ShellJob &job) noexcept {
    if (SendProcessGroupSignal(job.process_group_id, os::abi::OS_ABI_SIGNAL_CONTINUE_NUMBER) <
        OS_USER_SHELL_SUCCESS_RESULT) {
        return false;
    }
    for (uint64_t member_index = OS_USER_SHELL_EMPTY_VALUE; member_index < job.process_count;
         ++member_index) {
        if (job.member_states[member_index] == ShellJobMemberState::Stopped) {
            job.member_states[member_index] = ShellJobMemberState::Running;
        }
    }
    job.state = ShellJobState::Running;
    return true;
}

void ReapBackgroundJobs() noexcept {
    for (uint64_t job_index = OS_USER_SHELL_EMPTY_VALUE; job_index < OS_USER_SHELL_JOB_CAPACITY;
         ++job_index) {
        ShellJob &job = shell_jobs[job_index];
        if (job.state == ShellJobState::Free || !PumpJobEvents(job, true)) {
            continue;
        }
        if (job.state == ShellJobState::Done) {
            static_cast<void>(WriteJob(job));
            ReleaseJob(job);
        }
    }
}

[[nodiscard]] bool ParseJobId(const ShellExecutionPlan &execution_plan, uint64_t &job_id) noexcept {
    job_id = OS_USER_SHELL_EMPTY_VALUE;
    const ShellExecutionStage &stage = execution_plan.stages[OS_USER_SHELL_EMPTY_VALUE];
    if (stage.argument_count == OS_USER_SHELL_JOB_BUILTIN_MINIMUM_ARGUMENT_COUNT) {
        ShellJob *const latest_job = FindLatestJob();
        if (latest_job == nullptr) {
            return false;
        }
        job_id = latest_job->job_id;
        return true;
    }
    if (stage.argument_count != OS_USER_SHELL_JOB_BUILTIN_MAXIMUM_ARGUMENT_COUNT) {
        return false;
    }
    const uint64_t argument_index =
        stage.first_argument_index + OS_USER_SHELL_BUILTIN_PARAMETER_INDEX;
    const char *const bytes = ShellExecutionArgumentBytes(execution_plan, argument_index);
    const uint64_t length_bytes = execution_plan.arguments[argument_index].length_bytes;
    if (bytes == nullptr || length_bytes == OS_USER_SHELL_EMPTY_VALUE) {
        return false;
    }
    for (uint64_t byte_index = OS_USER_SHELL_EMPTY_VALUE; byte_index < length_bytes; ++byte_index) {
        if (bytes[byte_index] < '0' || bytes[byte_index] > '9' ||
            job_id > (UINT64_MAX - static_cast<uint64_t>(bytes[byte_index] - '0')) /
                         OS_USER_SHELL_DECIMAL_BASE) {
            return false;
        }
        job_id =
            job_id * OS_USER_SHELL_DECIMAL_BASE + static_cast<uint64_t>(bytes[byte_index] - '0');
    }
    return job_id != OS_USER_SHELL_EMPTY_VALUE;
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
    const uint64_t prefix_length_bytes = OS_USER_SHELL_BINARY_PREFIX_SIZE_BYTES;
    const uint64_t required_length_bytes =
        command_length_bytes +
        (contains_separator ? OS_USER_SHELL_EMPTY_VALUE : prefix_length_bytes);
    if (required_length_bytes + OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES >
        OS_USER_SHELL_EXECUTABLE_PATH_CAPACITY_BYTES) {
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

[[nodiscard]] bool RedirectChildDescriptor(const ShellExecutionPlan &execution_plan,
                                           const ShellRedirectionMode mode,
                                           const ShellArgument &path,
                                           const uint64_t destination_descriptor) noexcept {
    if (mode == ShellRedirectionMode::None) {
        return true;
    }
    const uint64_t open_flags =
        os::abi::OS_ABI_FILE_OPEN_WRITE_FLAG | os::abi::OS_ABI_FILE_OPEN_CREATE_FLAG |
        (mode == ShellRedirectionMode::Append ? os::abi::OS_ABI_FILE_OPEN_APPEND_FLAG
                                              : os::abi::OS_ABI_FILE_OPEN_TRUNCATE_FLAG);
    const int64_t descriptor =
        OpenFile(execution_plan.storage + path.offset_bytes, path.length_bytes, open_flags);
    if (descriptor < OS_USER_SHELL_SUCCESS_RESULT ||
        !DuplicateForChild(static_cast<uint64_t>(descriptor), destination_descriptor)) {
        if (descriptor >= OS_USER_SHELL_SUCCESS_RESULT) {
            static_cast<void>(CloseDescriptor(static_cast<uint64_t>(descriptor)));
        }
        return false;
    }
    return CloseDescriptor(static_cast<uint64_t>(descriptor)) == OS_USER_SHELL_SUCCESS_RESULT;
}

[[noreturn]] void ExecuteChildStage(const ShellExecutionPlan &execution_plan,
                                    const uint64_t stage_index,
                                    os::abi::PipeDescriptorPair *const pipes,
                                    const uint64_t pipe_count, const uint64_t process_group_id,
                                    const uint64_t inherited_signal_mask) noexcept {
    // 子进程继承 Shell handler，但在父进程发布前台组前同时继承了阻塞 mask。
    // 必须先恢复默认处置，再加入作业组并解除 mask，避免 Ctrl-C/Z 落入
    // fork 与 exec 之间时调用 Shell handler。
    if (!RestoreChildSignalPolicy() ||
        SetProcessGroup(process_group_id) != OS_USER_SHELL_SUCCESS_RESULT ||
        SetSignalMask(inherited_signal_mask, nullptr) != OS_USER_SHELL_SUCCESS_RESULT) {
        ExitProcess(OS_USER_SHELL_CHILD_SETUP_FAILURE_EXIT_CODE);
    }
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
    if (!RedirectChildDescriptor(execution_plan, stage.output_redirection, stage.output_path,
                                 os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR) ||
        !RedirectChildDescriptor(execution_plan, stage.error_redirection, stage.error_path,
                                 os::abi::OS_ABI_STANDARD_ERROR_DESCRIPTOR)) {
        ExitProcess(OS_USER_SHELL_CHILD_SETUP_FAILURE_EXIT_CODE);
    }
    ClosePipelineDescriptors(pipes, pipe_count);

    uint64_t prepared_argument_count = OS_USER_SHELL_EMPTY_VALUE;
    const ShellGlobPreparationStatus glob_status =
        PrepareStageArguments(execution_plan, stage, prepared_argument_count);
    if (glob_status != ShellGlobPreparationStatus::Succeeded) {
        if (glob_status == ShellGlobPreparationStatus::CapacityExceeded) {
            static_cast<void>(WriteLiteral(OS_USER_SHELL_GLOB_ERROR));
        }
        ExitProcess(OS_USER_SHELL_CHILD_SETUP_FAILURE_EXIT_CODE);
    }
    uint64_t environment_count = OS_USER_SHELL_EMPTY_VALUE;
    if (!PrepareChildEnvironment(environment_count)) {
        ExitProcess(OS_USER_SHELL_CHILD_SETUP_FAILURE_EXIT_CODE);
    }

    char executable_path[OS_USER_SHELL_EXECUTABLE_PATH_CAPACITY_BYTES]{};
    uint64_t executable_path_length_bytes = OS_USER_SHELL_EMPTY_VALUE;
    if (prepared_argument_count == OS_USER_SHELL_EMPTY_VALUE ||
        !BuildExecutablePath(shell_prepared_arguments[OS_USER_SHELL_EMPTY_VALUE].bytes,
                             shell_prepared_arguments[OS_USER_SHELL_EMPTY_VALUE].length_bytes,
                             executable_path, executable_path_length_bytes)) {
        ExitProcess(OS_USER_SHELL_COMMAND_NOT_FOUND_RESULT);
    }

    const os::abi::ProcessLaunchRequest request{
        .path_address = reinterpret_cast<uint64_t>(executable_path),
        .path_length_bytes = executable_path_length_bytes,
        .argument_vector_address = reinterpret_cast<uint64_t>(shell_prepared_process_arguments),
        .argument_count = prepared_argument_count,
        .environment_vector_address = reinterpret_cast<uint64_t>(shell_prepared_environment),
        .environment_count = environment_count,
    };
    static_cast<void>(ExecProcess(request));
    ExitProcess(OS_USER_SHELL_COMMAND_NOT_FOUND_RESULT);
}

[[nodiscard]] int64_t ExecuteExternalPipeline(const ShellExecutionPlan &execution_plan) noexcept {
    ShellJob *const job = FindFreeJob();
    if (job == nullptr) {
        static_cast<void>(WriteLiteral(OS_USER_SHELL_JOB_TABLE_FULL_ERROR));
        return OS_USER_SHELL_FAILURE_EXIT_CODE;
    }
    *job = ShellJob{
        .job_id = next_shell_job_id,
        .process_group_id = OS_USER_SHELL_EMPTY_VALUE,
        .process_ids = {},
        .member_states = {},
        .process_count = OS_USER_SHELL_EMPTY_VALUE,
        .last_exit_code = OS_USER_SHELL_FAILURE_EXIT_CODE,
        .state = ShellJobState::Running,
    };
    next_shell_job_id = next_shell_job_id == UINT64_MAX
                            ? OS_USER_SHELL_FIRST_VALUE
                            : next_shell_job_id + OS_USER_SHELL_FIRST_VALUE;

    const uint64_t pipe_count = execution_plan.stage_count - OS_USER_SHELL_PIPE_COUNT_OFFSET;
    os::abi::PipeDescriptorPair
        pipes[OS_USER_SHELL_EXECUTION_MAXIMUM_STAGE_COUNT - OS_USER_SHELL_PIPE_COUNT_OFFSET]{};
    for (uint64_t pipe_index = OS_USER_SHELL_EMPTY_VALUE; pipe_index < pipe_count; ++pipe_index) {
        pipes[pipe_index].reader_descriptor = UINT64_MAX;
        pipes[pipe_index].writer_descriptor = UINT64_MAX;
        if (CreatePipe(pipes[pipe_index]) != OS_USER_SHELL_SUCCESS_RESULT) {
            ClosePipelineDescriptors(pipes, pipe_count);
            ReleaseJob(*job);
            return OS_USER_SHELL_FAILURE_EXIT_CODE;
        }
    }

    uint64_t inherited_signal_mask = OS_USER_SHELL_EMPTY_VALUE;
    if (SetSignalMask(OS_USER_SHELL_JOB_CONTROL_SIGNAL_SET, &inherited_signal_mask) !=
        OS_USER_SHELL_SUCCESS_RESULT) {
        ClosePipelineDescriptors(pipes, pipe_count);
        ReleaseJob(*job);
        return OS_USER_SHELL_FAILURE_EXIT_CODE;
    }

    uint64_t process_group_id = OS_USER_SHELL_EMPTY_VALUE;
    for (uint64_t stage_index = OS_USER_SHELL_EMPTY_VALUE; stage_index < execution_plan.stage_count;
         ++stage_index) {
        const int64_t fork_result = ForkProcess();
        if (fork_result == OS_USER_SHELL_SUCCESS_RESULT) {
            ExecuteChildStage(execution_plan, stage_index, pipes, pipe_count, process_group_id,
                              inherited_signal_mask);
        }
        if (fork_result < OS_USER_SHELL_SUCCESS_RESULT) {
            ClosePipelineDescriptors(pipes, pipe_count);
            if (process_group_id != OS_USER_SHELL_EMPTY_VALUE) {
                static_cast<void>(
                    SendProcessGroupSignal(process_group_id, os::abi::OS_ABI_SIGNAL_KILL_NUMBER));
                static_cast<void>(PumpJobEvents(*job, false));
            }
            static_cast<void>(SetSignalMask(inherited_signal_mask, nullptr));
            ReleaseJob(*job);
            return OS_USER_SHELL_FAILURE_EXIT_CODE;
        }
        const uint64_t child_process_id = static_cast<uint64_t>(fork_result);
        if (process_group_id == OS_USER_SHELL_EMPTY_VALUE) {
            process_group_id = child_process_id;
            job->process_group_id = process_group_id;
        }
        job->process_ids[job->process_count] = child_process_id;
        job->member_states[job->process_count] = ShellJobMemberState::Running;
        ++job->process_count;
        if (SetProcessGroupFor(child_process_id, process_group_id) !=
            OS_USER_SHELL_SUCCESS_RESULT) {
            ClosePipelineDescriptors(pipes, pipe_count);
            static_cast<void>(
                SendProcessGroupSignal(process_group_id, os::abi::OS_ABI_SIGNAL_KILL_NUMBER));
            static_cast<void>(PumpJobEvents(*job, false));
            static_cast<void>(SetSignalMask(inherited_signal_mask, nullptr));
            ReleaseJob(*job);
            return OS_USER_SHELL_FAILURE_EXIT_CODE;
        }
    }
    ClosePipelineDescriptors(pipes, pipe_count);
    if (SetSignalMask(inherited_signal_mask, nullptr) != OS_USER_SHELL_SUCCESS_RESULT) {
        static_cast<void>(
            SendProcessGroupSignal(process_group_id, os::abi::OS_ABI_SIGNAL_KILL_NUMBER));
        static_cast<void>(PumpJobEvents(*job, false));
        ReleaseJob(*job);
        return OS_USER_SHELL_FAILURE_EXIT_CODE;
    }

    if (execution_plan.background) {
        static_cast<void>(WriteJob(*job));
        static_cast<void>(WriteLiteral(OS_USER_SHELL_JOB_STARTED_MARKER));
        return OS_USER_SHELL_SUCCESS_RESULT;
    }
    if (SetTerminalForegroundGroup(process_group_id) != OS_USER_SHELL_SUCCESS_RESULT) {
        static_cast<void>(
            SendProcessGroupSignal(process_group_id, os::abi::OS_ABI_SIGNAL_KILL_NUMBER));
        static_cast<void>(PumpJobEvents(*job, false));
        ReleaseJob(*job);
        return OS_USER_SHELL_FAILURE_EXIT_CODE;
    }
    if (!WriteLiteral(OS_USER_SHELL_FOREGROUND_JOB_WAITING_MARKER)) {
        static_cast<void>(
            SendProcessGroupSignal(process_group_id, os::abi::OS_ABI_SIGNAL_KILL_NUMBER));
        static_cast<void>(PumpJobEvents(*job, false));
        static_cast<void>(SetTerminalForegroundGroup(shell_process_group_id));
        ReleaseJob(*job);
        return OS_USER_SHELL_FAILURE_EXIT_CODE;
    }
    const bool wait_succeeded = PumpJobEvents(*job, false);
    const bool terminal_restored =
        SetTerminalForegroundGroup(shell_process_group_id) == OS_USER_SHELL_SUCCESS_RESULT;
    if (!wait_succeeded || !terminal_restored) {
        return OS_USER_SHELL_FAILURE_EXIT_CODE;
    }
    const int64_t last_exit_code = job->last_exit_code;
    if (job->state == ShellJobState::Stopped) {
        static_cast<void>(WriteJob(*job));
        static_cast<void>(WriteLiteral(OS_USER_SHELL_JOB_STOPPED_MARKER));
        return OS_USER_SHELL_SUCCESS_RESULT;
    }
    ReleaseJob(*job);

    bool has_redirection = false;
    for (uint64_t stage_index = OS_USER_SHELL_EMPTY_VALUE; stage_index < execution_plan.stage_count;
         ++stage_index) {
        has_redirection =
            has_redirection || execution_plan.stages[stage_index].has_input_redirection ||
            execution_plan.stages[stage_index].output_redirection != ShellRedirectionMode::None ||
            execution_plan.stages[stage_index].error_redirection != ShellRedirectionMode::None;
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
    return !execution_plan.background && !stage.has_input_redirection &&
           stage.output_redirection == ShellRedirectionMode::None &&
           stage.error_redirection == ShellRedirectionMode::None &&
           BytesEqual(ShellExecutionArgumentBytes(execution_plan, command_index),
                      execution_plan.arguments[command_index].length_bytes, command,
                      command_length_bytes);
}

[[nodiscard]] bool IsShellAssignment(const ShellExecutionPlan &execution_plan) noexcept {
    if (execution_plan.stage_count != OS_USER_SHELL_FIRST_VALUE || execution_plan.background) {
        return false;
    }
    const ShellExecutionStage &stage = execution_plan.stages[OS_USER_SHELL_EMPTY_VALUE];
    if (stage.argument_count != OS_USER_SHELL_FIRST_VALUE || stage.has_input_redirection ||
        stage.output_redirection != ShellRedirectionMode::None ||
        stage.error_redirection != ShellRedirectionMode::None) {
        return false;
    }
    const ShellArgument &argument = execution_plan.arguments[stage.first_argument_index];
    const char *const bytes =
        ShellExecutionArgumentBytes(execution_plan, stage.first_argument_index);
    uint64_t separator_index = argument.length_bytes;
    for (uint64_t byte_index = OS_USER_SHELL_EMPTY_VALUE; byte_index < argument.length_bytes;
         ++byte_index) {
        if (bytes[byte_index] == '=') {
            separator_index = byte_index;
            break;
        }
    }
    return separator_index < argument.length_bytes &&
           IsShellEnvironmentNameValid(bytes, separator_index);
}

[[nodiscard]] int64_t ExecutePlan(const ShellExecutionPlan &execution_plan,
                                  bool &exit_requested) noexcept {
    exit_requested = false;
    if (!WriteCommandMarker(execution_plan)) {
        return OS_USER_SHELL_FAILURE_EXIT_CODE;
    }

    if (IsShellAssignment(execution_plan)) {
        const ShellExecutionStage &stage = execution_plan.stages[OS_USER_SHELL_EMPTY_VALUE];
        const ShellArgument &argument = execution_plan.arguments[stage.first_argument_index];
        const ShellEnvironmentStatus status = shell_environment.SetAssignment(
            ShellExecutionArgumentBytes(execution_plan, stage.first_argument_index),
            argument.length_bytes);
        if (status != ShellEnvironmentStatus::Succeeded) {
            static_cast<void>(WriteLiteral(OS_USER_SHELL_ENVIRONMENT_ERROR));
            return OS_USER_SHELL_FAILURE_EXIT_CODE;
        }
        return OS_USER_SHELL_SUCCESS_RESULT;
    }
    if (IsSingleStageBuiltin(execution_plan, OS_USER_SHELL_EXPORT_COMMAND,
                             sizeof(OS_USER_SHELL_EXPORT_COMMAND) -
                                 OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES)) {
        const ShellExecutionStage &stage = execution_plan.stages[OS_USER_SHELL_EMPTY_VALUE];
        if (stage.argument_count != OS_USER_SHELL_ENVIRONMENT_BUILTIN_ARGUMENT_COUNT) {
            static_cast<void>(WriteLiteral(OS_USER_SHELL_USAGE_ERROR));
            return OS_USER_SHELL_FAILURE_EXIT_CODE;
        }
        const uint64_t assignment_index =
            stage.first_argument_index + OS_USER_SHELL_BUILTIN_PARAMETER_INDEX;
        const ShellEnvironmentStatus status = shell_environment.SetAssignment(
            ShellExecutionArgumentBytes(execution_plan, assignment_index),
            execution_plan.arguments[assignment_index].length_bytes);
        if (status != ShellEnvironmentStatus::Succeeded) {
            static_cast<void>(WriteLiteral(OS_USER_SHELL_ENVIRONMENT_ERROR));
            return OS_USER_SHELL_FAILURE_EXIT_CODE;
        }
        return OS_USER_SHELL_SUCCESS_RESULT;
    }
    if (IsSingleStageBuiltin(execution_plan, OS_USER_SHELL_UNSET_COMMAND,
                             sizeof(OS_USER_SHELL_UNSET_COMMAND) -
                                 OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES)) {
        const ShellExecutionStage &stage = execution_plan.stages[OS_USER_SHELL_EMPTY_VALUE];
        if (stage.argument_count != OS_USER_SHELL_ENVIRONMENT_BUILTIN_ARGUMENT_COUNT) {
            static_cast<void>(WriteLiteral(OS_USER_SHELL_USAGE_ERROR));
            return OS_USER_SHELL_FAILURE_EXIT_CODE;
        }
        const uint64_t name_index =
            stage.first_argument_index + OS_USER_SHELL_BUILTIN_PARAMETER_INDEX;
        const ShellEnvironmentStatus status =
            shell_environment.Unset(ShellExecutionArgumentBytes(execution_plan, name_index),
                                    execution_plan.arguments[name_index].length_bytes);
        if (status != ShellEnvironmentStatus::Succeeded &&
            status != ShellEnvironmentStatus::NotFound) {
            static_cast<void>(WriteLiteral(OS_USER_SHELL_ENVIRONMENT_ERROR));
            return OS_USER_SHELL_FAILURE_EXIT_CODE;
        }
        return OS_USER_SHELL_SUCCESS_RESULT;
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
    if (IsSingleStageBuiltin(execution_plan, OS_USER_SHELL_JOBS_COMMAND,
                             sizeof(OS_USER_SHELL_JOBS_COMMAND) -
                                 OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES)) {
        const ShellExecutionStage &stage = execution_plan.stages[OS_USER_SHELL_EMPTY_VALUE];
        if (stage.argument_count != OS_USER_SHELL_JOBS_ARGUMENT_COUNT) {
            static_cast<void>(WriteLiteral(OS_USER_SHELL_USAGE_ERROR));
            return OS_USER_SHELL_FAILURE_EXIT_CODE;
        }
        ReapBackgroundJobs();
        for (uint64_t job_index = OS_USER_SHELL_EMPTY_VALUE; job_index < OS_USER_SHELL_JOB_CAPACITY;
             ++job_index) {
            if (shell_jobs[job_index].state != ShellJobState::Free &&
                !WriteJob(shell_jobs[job_index])) {
                return OS_USER_SHELL_FAILURE_EXIT_CODE;
            }
        }
        return OS_USER_SHELL_SUCCESS_RESULT;
    }
    const bool foreground_builtin = IsSingleStageBuiltin(
        execution_plan, OS_USER_SHELL_FOREGROUND_COMMAND,
        sizeof(OS_USER_SHELL_FOREGROUND_COMMAND) - OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES);
    const bool background_builtin = IsSingleStageBuiltin(
        execution_plan, OS_USER_SHELL_BACKGROUND_COMMAND,
        sizeof(OS_USER_SHELL_BACKGROUND_COMMAND) - OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES);
    if (foreground_builtin || background_builtin) {
        uint64_t job_id = OS_USER_SHELL_EMPTY_VALUE;
        if (!ParseJobId(execution_plan, job_id)) {
            static_cast<void>(WriteLiteral(OS_USER_SHELL_USAGE_ERROR));
            return OS_USER_SHELL_FAILURE_EXIT_CODE;
        }
        ShellJob *const job = FindJob(job_id);
        if (job == nullptr) {
            static_cast<void>(WriteLiteral(OS_USER_SHELL_JOB_NOT_FOUND_ERROR));
            return OS_USER_SHELL_FAILURE_EXIT_CODE;
        }
        if (background_builtin) {
            if ((job->state == ShellJobState::Stopped && !ContinueJob(*job)) || !WriteJob(*job)) {
                return OS_USER_SHELL_FAILURE_EXIT_CODE;
            }
            return OS_USER_SHELL_SUCCESS_RESULT;
        }
        if (SetTerminalInputMode(os::abi::TerminalInputMode::Canonical) !=
            OS_USER_SHELL_SUCCESS_RESULT) {
            return OS_USER_SHELL_FAILURE_EXIT_CODE;
        }
        if (SetTerminalForegroundGroup(job->process_group_id) != OS_USER_SHELL_SUCCESS_RESULT) {
            static_cast<void>(SetTerminalInputMode(os::abi::TerminalInputMode::ShellEditor));
            return OS_USER_SHELL_FAILURE_EXIT_CODE;
        }
        if (!WriteLiteral(OS_USER_SHELL_FOREGROUND_JOB_WAITING_MARKER)) {
            static_cast<void>(SetTerminalForegroundGroup(shell_process_group_id));
            static_cast<void>(SetTerminalInputMode(os::abi::TerminalInputMode::ShellEditor));
            return OS_USER_SHELL_FAILURE_EXIT_CODE;
        }
        const bool continued = job->state != ShellJobState::Stopped || ContinueJob(*job);
        const bool waited = continued && PumpJobEvents(*job, false);
        const bool restored =
            SetTerminalForegroundGroup(shell_process_group_id) == OS_USER_SHELL_SUCCESS_RESULT;
        const bool editor_mode_restored =
            restored && SetTerminalInputMode(os::abi::TerminalInputMode::ShellEditor) ==
                            OS_USER_SHELL_SUCCESS_RESULT;
        if (!waited || !editor_mode_restored) {
            return OS_USER_SHELL_FAILURE_EXIT_CODE;
        }
        if (job->state == ShellJobState::Stopped) {
            static_cast<void>(WriteJob(*job));
            static_cast<void>(WriteLiteral(OS_USER_SHELL_JOB_STOPPED_MARKER));
            return OS_USER_SHELL_SUCCESS_RESULT;
        }
        const int64_t last_exit_code = job->last_exit_code;
        ReleaseJob(*job);
        return last_exit_code;
    }

    if (SetTerminalInputMode(os::abi::TerminalInputMode::Canonical) !=
        OS_USER_SHELL_SUCCESS_RESULT) {
        return OS_USER_SHELL_FAILURE_EXIT_CODE;
    }
    const int64_t result = ExecuteExternalPipeline(execution_plan);
    const bool editor_mode_restored =
        SetTerminalInputMode(os::abi::TerminalInputMode::ShellEditor) ==
        OS_USER_SHELL_SUCCESS_RESULT;
    if (!editor_mode_restored) {
        return OS_USER_SHELL_FAILURE_EXIT_CODE;
    }
    if (result == OS_USER_SHELL_COMMAND_NOT_FOUND_RESULT) {
        static_cast<void>(WriteLiteral(OS_USER_SHELL_UNKNOWN_COMMAND_MARKER));
    } else if (result != OS_USER_SHELL_SUCCESS_RESULT) {
        static_cast<void>(WriteLiteral(OS_USER_SHELL_OPERATION_ERROR));
    }
    return result;
}

[[nodiscard]] ShellExecutionParseStatus
ValidateShellSequenceCommand(const char *const line, const ShellExecutionCommand &command,
                             const bool final_command, const int64_t previous_exit_code) noexcept {
    ShellExecutionPlan validation_plan{};
    const ShellExpansionContext expansion_context{
        .context = &shell_environment,
        .lookup_operation = LookupShellVariable,
        .previous_exit_code = previous_exit_code,
    };
    const ShellExecutionParseStatus parse_status = ParseShellExecutionPlanExpanded(
        line + command.offset_bytes, command.length_bytes, expansion_context, validation_plan);
    if (parse_status != ShellExecutionParseStatus::Succeeded) {
        return parse_status;
    }
    return validation_plan.background && !final_command
               ? ShellExecutionParseStatus::BackgroundOperatorNotLast
               : ShellExecutionParseStatus::Succeeded;
}

[[nodiscard]] ShellExecutionParseStatus
ExecuteShellSequenceCommand(const char *const line, const ShellExecutionCommand &command,
                            const int64_t previous_exit_code, bool &exit_requested,
                            int64_t &command_result) noexcept {
    ShellExecutionPlan execution_plan{};
    const ShellExpansionContext expansion_context{
        .context = &shell_environment,
        .lookup_operation = LookupShellVariable,
        .previous_exit_code = previous_exit_code,
    };
    const ShellExecutionParseStatus parse_status = ParseShellExecutionPlanExpanded(
        line + command.offset_bytes, command.length_bytes, expansion_context, execution_plan);
    if (parse_status != ShellExecutionParseStatus::Succeeded) {
        return parse_status;
    }
    command_result = ExecutePlan(execution_plan, exit_requested);
    return ShellExecutionParseStatus::Succeeded;
}

[[nodiscard]] ShellExecutionParseStatus ParseAndExecute(const char *const line,
                                                        const uint64_t line_length_bytes,
                                                        bool &exit_requested,
                                                        int64_t &command_result) noexcept {
    exit_requested = false;
    ShellExecutionSequence sequence{};
    const ShellExecutionParseStatus sequence_status =
        ParseShellExecutionSequence(line, line_length_bytes, sequence);
    if (sequence_status != ShellExecutionParseStatus::Succeeded) {
        return sequence_status;
    }

    // 整行必须先完成语法验证，避免后段错误时前段已经产生文件或进程副作用。
    // 展开结果不会重新解释为控制操作符，因此预检可使用进入本行时的退出码；
    // 实际执行仍会逐条用最新退出码重新展开 `$?`。
    for (uint64_t command_index = OS_USER_SHELL_EMPTY_VALUE; command_index < sequence.command_count;
         ++command_index) {
        const ShellExecutionCommand &command = sequence.commands[command_index];
        const ShellExecutionParseStatus parse_status = ValidateShellSequenceCommand(
            line, command, command_index + OS_USER_SHELL_FIRST_VALUE == sequence.command_count,
            shell_last_exit_code);
        if (parse_status != ShellExecutionParseStatus::Succeeded) {
            return parse_status;
        }
    }

    command_result = shell_last_exit_code;
    for (uint64_t command_index = OS_USER_SHELL_EMPTY_VALUE; command_index < sequence.command_count;
         ++command_index) {
        const ShellExecutionCommand &command = sequence.commands[command_index];
        const bool should_execute = command.condition == ShellExecutionCondition::Always ||
                                    (command.condition == ShellExecutionCondition::OnSuccess &&
                                     command_result == OS_USER_SHELL_SUCCESS_RESULT) ||
                                    (command.condition == ShellExecutionCondition::OnFailure &&
                                     command_result != OS_USER_SHELL_SUCCESS_RESULT);
        if (!should_execute) {
            continue;
        }
        bool command_exit_requested = false;
        const ShellExecutionParseStatus parse_status = ExecuteShellSequenceCommand(
            line, command, command_result, command_exit_requested, command_result);
        if (parse_status != ShellExecutionParseStatus::Succeeded) {
            return parse_status;
        }
        if (command_exit_requested) {
            exit_requested = true;
            break;
        }
        shell_last_exit_code = command_result;
    }
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

[[nodiscard]] bool RedrawEditorLine() noexcept {
    if (!WriteLiteral(OS_USER_SHELL_EDITOR_REDRAW_SEQUENCE) || !WritePrompt() ||
        !WriteBytes(shell_line_editor.Bytes(), shell_line_editor.Length())) {
        return false;
    }
    for (uint64_t byte_index = shell_line_editor.Cursor(); byte_index < shell_line_editor.Length();
         ++byte_index) {
        if (!WriteLiteral(OS_USER_SHELL_CURSOR_LEFT_SEQUENCE)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool HandleEditorControlSequence(const uint8_t final_byte) noexcept {
    if (final_byte == static_cast<uint8_t>('A')) {
        return !shell_line_editor.SelectPreviousHistory() || RedrawEditorLine();
    }
    if (final_byte == static_cast<uint8_t>('B')) {
        return !shell_line_editor.SelectNextHistory() || RedrawEditorLine();
    }
    if (final_byte == static_cast<uint8_t>('C')) {
        return !shell_line_editor.MoveRight() || WriteLiteral(OS_USER_SHELL_CURSOR_RIGHT_SEQUENCE);
    }
    if (final_byte == static_cast<uint8_t>('D')) {
        return !shell_line_editor.MoveLeft() || WriteLiteral(OS_USER_SHELL_CURSOR_LEFT_SEQUENCE);
    }
    return true;
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

int64_t RunShell(const uint64_t argument_count, const char *const *const arguments,
                 const char *const *const environment) noexcept {
    InitializeJobTable();
    shell_last_exit_code = OS_USER_SHELL_SUCCESS_RESULT;
    if (shell_environment.Initialize(environment) != ShellEnvironmentStatus::Succeeded) {
        return OS_USER_SHELL_FAILURE_EXIT_CODE;
    }
    const uint64_t process_id = GetProcessId();
    if (process_id == OS_USER_SHELL_EMPTY_VALUE ||
        SetProcessGroup(process_id) != OS_USER_SHELL_SUCCESS_RESULT) {
        return OS_USER_SHELL_FAILURE_EXIT_CODE;
    }
    shell_process_group_id = process_id;
    os::abi::TerminalInformation terminal_information{};
    if (GetTerminalInformation(terminal_information) != OS_USER_SHELL_SUCCESS_RESULT ||
        SetTerminalForegroundGroup(shell_process_group_id) != OS_USER_SHELL_SUCCESS_RESULT ||
        !InstallShellSignalPolicy()) {
        return OS_USER_SHELL_FAILURE_EXIT_CODE;
    }
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

    if (SetTerminalInputMode(os::abi::TerminalInputMode::ShellEditor) !=
            OS_USER_SHELL_SUCCESS_RESULT ||
        !WriteLiteral(OS_USER_SHELL_BANNER) ||
        !WriteLiteral(OS_USER_SHELL_JOB_CONTROL_READY_MARKER) ||
        !WriteLiteral(OS_USER_SHELL_READY_MARKER)) {
        return OS_USER_SHELL_FAILURE_EXIT_CODE;
    }
    while (true) {
        ReapBackgroundJobs();
        if (!WritePrompt()) {
            return OS_USER_SHELL_FAILURE_EXIT_CODE;
        }
        shell_line_editor.Clear();
        bool input_interrupted = false;
        ShellEditorEscapeState escape_state = ShellEditorEscapeState::Ground;
        while (true) {
            uint8_t character = OS_USER_SHELL_EMPTY_VALUE;
            const int64_t read_result = ReadDescriptor(os::abi::OS_ABI_STANDARD_INPUT_DESCRIPTOR,
                                                       &character, OS_USER_SHELL_FIRST_VALUE);
            if (read_result == os::abi::OS_ABI_SYSTEM_CALL_RESULT_INTERRUPTED) {
                input_interrupted = true;
                break;
            }
            if (read_result != static_cast<int64_t>(OS_USER_SHELL_FIRST_VALUE)) {
                return OS_USER_SHELL_FAILURE_EXIT_CODE;
            }
            if (escape_state == ShellEditorEscapeState::Escape) {
                escape_state = character == static_cast<uint8_t>('[')
                                   ? ShellEditorEscapeState::ControlSequence
                                   : ShellEditorEscapeState::Ground;
                continue;
            }
            if (escape_state == ShellEditorEscapeState::ControlSequence) {
                escape_state = ShellEditorEscapeState::Ground;
                if (!HandleEditorControlSequence(character)) {
                    return OS_USER_SHELL_FAILURE_EXIT_CODE;
                }
                continue;
            }
            if (character == OS_USER_SHELL_ESCAPE_CHARACTER) {
                escape_state = ShellEditorEscapeState::Escape;
                continue;
            }
            if (character == OS_USER_SHELL_NEWLINE_CHARACTER ||
                character == OS_USER_SHELL_CARRIAGE_RETURN_CHARACTER) {
                if (!WriteLiteral(OS_USER_SHELL_EDITOR_NEWLINE_SEQUENCE)) {
                    return OS_USER_SHELL_FAILURE_EXIT_CODE;
                }
                break;
            }
            if (character == OS_USER_SHELL_BACKSPACE_CHARACTER ||
                character == OS_USER_SHELL_DELETE_CHARACTER) {
                const bool was_at_end = shell_line_editor.Cursor() == shell_line_editor.Length();
                if (shell_line_editor.Backspace() &&
                    !((was_at_end && WriteLiteral(OS_USER_SHELL_EDITOR_BACKSPACE_SEQUENCE)) ||
                      (!was_at_end && RedrawEditorLine()))) {
                    return OS_USER_SHELL_FAILURE_EXIT_CODE;
                }
                continue;
            }
            if (character == OS_USER_SHELL_TAB_CHARACTER) {
                if (shell_line_editor.CompleteCommand(
                        OS_USER_SHELL_COMPLETION_CANDIDATES,
                        sizeof(OS_USER_SHELL_COMPLETION_CANDIDATES) /
                            sizeof(OS_USER_SHELL_COMPLETION_CANDIDATES[0])) &&
                    !RedrawEditorLine()) {
                    return OS_USER_SHELL_FAILURE_EXIT_CODE;
                }
                continue;
            }
            if (character < OS_USER_SHELL_FIRST_PRINTABLE_CHARACTER ||
                character > OS_USER_SHELL_LAST_PRINTABLE_CHARACTER) {
                continue;
            }
            const bool append_at_end = shell_line_editor.Cursor() == shell_line_editor.Length();
            if (shell_line_editor.Insert(static_cast<char>(character)) &&
                !((append_at_end && WriteBytes(reinterpret_cast<const char *>(&character), 1ULL)) ||
                  (!append_at_end && RedrawEditorLine()))) {
                return OS_USER_SHELL_FAILURE_EXIT_CODE;
            }
        }
        if (input_interrupted) {
            shell_line_editor.Clear();
            continue;
        }

        shell_line_editor.CommitHistory();
        bool exit_requested = false;
        int64_t command_result = OS_USER_SHELL_SUCCESS_RESULT;
        const ShellExecutionParseStatus parse_status = ParseAndExecute(
            shell_line_editor.Bytes(), shell_line_editor.Length(), exit_requested, command_result);
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
            if (!WriteLiteral(OS_USER_SHELL_COMMAND_COMPLETE_MARKER)) {
                return OS_USER_SHELL_FAILURE_EXIT_CODE;
            }
            continue;
        }
        if (!WriteLiteral(OS_USER_SHELL_COMMAND_COMPLETE_MARKER)) {
            return OS_USER_SHELL_FAILURE_EXIT_CODE;
        }
        if (exit_requested) {
            return WriteLiteral(OS_USER_SHELL_EXIT_MARKER) ? OS_USER_SHELL_SUCCESS_RESULT
                                                           : OS_USER_SHELL_FAILURE_EXIT_CODE;
        }
        static_cast<void>(command_result);
    }
}

}

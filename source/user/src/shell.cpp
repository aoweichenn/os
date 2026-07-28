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
constexpr uint64_t OS_USER_SHELL_JOBS_ARGUMENT_COUNT = 1ULL;
constexpr uint64_t OS_USER_SHELL_JOB_BUILTIN_MINIMUM_ARGUMENT_COUNT = 1ULL;
constexpr uint64_t OS_USER_SHELL_JOB_BUILTIN_MAXIMUM_ARGUMENT_COUNT = 2ULL;
constexpr uint64_t OS_USER_SHELL_JOB_CAPACITY = 16ULL;
constexpr uint64_t OS_USER_SHELL_JOB_MEMBER_CAPACITY =
    OS_USER_SHELL_EXECUTION_MAXIMUM_STAGE_COUNT;
constexpr uint64_t OS_USER_SHELL_DECIMAL_BASE = 10ULL;
constexpr uint64_t OS_USER_SHELL_DECIMAL_BUFFER_CAPACITY_BYTES = 20ULL;
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
constexpr char OS_USER_SHELL_JOBS_COMMAND[] = "jobs";
constexpr char OS_USER_SHELL_FOREGROUND_COMMAND[] = "fg";
constexpr char OS_USER_SHELL_BACKGROUND_COMMAND[] = "bg";
constexpr char OS_USER_SHELL_NONINTERACTIVE_OPTION[] = "-c";
constexpr char OS_USER_SHELL_BANNER[] =
    "\r\nx86-64 OS Lab v1.18\r\n"
    "ABI v2、devfs、procfs、journal 与 32 个用户工具已经启用；输入 help 查看帮助。\r\n";
constexpr char OS_USER_SHELL_READY_MARKER[] = "[OS][USER][SHELL] READY\r\n";
constexpr char OS_USER_SHELL_PROMPT_PREFIX[] = "[os:";
constexpr char OS_USER_SHELL_PROMPT_SUFFIX[] = "]$ ";
constexpr char OS_USER_SHELL_PARSE_ERROR[] = "error: 命令行语法错误\r\n";
constexpr char OS_USER_SHELL_LINE_TOO_LONG_ERROR[] = "error: 命令行超过 512 字节\r\n";
constexpr char OS_USER_SHELL_OPERATION_ERROR[] = "error: 命令执行失败\r\n";
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
constexpr char OS_USER_SHELL_COMMAND_FOREGROUND_MARKER[] =
    "[OS][USER][SHELL] COMMAND=FG\r\n";
constexpr char OS_USER_SHELL_COMMAND_BACKGROUND_MARKER[] =
    "[OS][USER][SHELL] COMMAND=BG\r\n";
constexpr char OS_USER_SHELL_COMMAND_EXIT_MARKER[] = "[OS][USER][SHELL] COMMAND=EXIT\r\n";
constexpr char OS_USER_SHELL_COMMAND_COMPLETE_MARKER[] =
    "[OS][USER][SHELL] COMMAND_COMPLETE\r\n";
constexpr char OS_USER_SHELL_JOB_PREFIX[] = "[";
constexpr char OS_USER_SHELL_JOB_RUNNING_TEXT[] = "] Running PGID=";
constexpr char OS_USER_SHELL_JOB_STOPPED_TEXT[] = "] Stopped PGID=";
constexpr char OS_USER_SHELL_JOB_DONE_TEXT[] = "] Done PGID=";
constexpr char OS_USER_SHELL_JOB_SEPARATOR[] = "\r\n";
constexpr char OS_USER_SHELL_JOB_STARTED_MARKER[] =
    "[OS][USER][SHELL] BACKGROUND_JOB_STARTED\r\n";
constexpr char OS_USER_SHELL_JOB_STOPPED_MARKER[] =
    "[OS][USER][SHELL] FOREGROUND_JOB_STOPPED\r\n";
constexpr char OS_USER_SHELL_FOREGROUND_JOB_WAITING_MARKER[] =
    "[OS][USER][SHELL] FOREGROUND_JOB_WAITING\r\n";
constexpr char OS_USER_SHELL_JOB_CONTROL_READY_MARKER[] =
    "[OS][USER][SHELL] JOB_CONTROL_READY\r\n";
constexpr uint64_t OS_USER_SHELL_JOB_CONTROL_SIGNAL_SET =
    os::abi::SignalBit(os::abi::OS_ABI_SIGNAL_INTERRUPT_NUMBER) |
    os::abi::SignalBit(os::abi::OS_ABI_SIGNAL_TERMINAL_STOP_NUMBER);

void ShellInteractiveSignalHandler(const uint64_t signal_number,
                                   os::abi::SignalFrame *const signal_frame) noexcept {
    // Shell 只需要让阻塞读取返回 EINTR；信号号与现场由内核完成校验和恢复。
    static_cast<void>(signal_number);
    static_cast<void>(signal_frame);
}

[[nodiscard]] bool InstallShellSignalPolicy() noexcept {
    return InstallSignalHandler(os::abi::OS_ABI_SIGNAL_INTERRUPT_NUMBER,
                                &ShellInteractiveSignalHandler, OS_USER_SHELL_EMPTY_VALUE,
                                OS_USER_SHELL_EMPTY_VALUE, nullptr) ==
               OS_USER_SHELL_SUCCESS_RESULT &&
           InstallSignalHandler(os::abi::OS_ABI_SIGNAL_TERMINAL_STOP_NUMBER,
                                &ShellInteractiveSignalHandler, OS_USER_SHELL_EMPTY_VALUE,
                                OS_USER_SHELL_EMPTY_VALUE, nullptr) ==
               OS_USER_SHELL_SUCCESS_RESULT;
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
     sizeof(OS_USER_SHELL_COMMAND_JOBS_MARKER) -
         OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES},
    {OS_USER_SHELL_FOREGROUND_COMMAND,
     sizeof(OS_USER_SHELL_FOREGROUND_COMMAND) - OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES,
     OS_USER_SHELL_COMMAND_FOREGROUND_MARKER,
     sizeof(OS_USER_SHELL_COMMAND_FOREGROUND_MARKER) -
         OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES},
    {OS_USER_SHELL_BACKGROUND_COMMAND,
     sizeof(OS_USER_SHELL_BACKGROUND_COMMAND) - OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES,
     OS_USER_SHELL_COMMAND_BACKGROUND_MARKER,
     sizeof(OS_USER_SHELL_COMMAND_BACKGROUND_MARKER) -
         OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES},
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

[[nodiscard]] bool WriteUnsigned(const uint64_t value) noexcept {
    char reversed[OS_USER_SHELL_DECIMAL_BUFFER_CAPACITY_BYTES]{};
    char output[OS_USER_SHELL_DECIMAL_BUFFER_CAPACITY_BYTES]{};
    uint64_t remaining = value;
    uint64_t digit_count = OS_USER_SHELL_EMPTY_VALUE;
    do {
        reversed[digit_count] =
            static_cast<char>('0' + remaining % OS_USER_SHELL_DECIMAL_BASE);
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
    for (uint64_t job_index = OS_USER_SHELL_EMPTY_VALUE;
         job_index < OS_USER_SHELL_JOB_CAPACITY; ++job_index) {
        shell_jobs[job_index] = ShellJob{};
    }
    next_shell_job_id = OS_USER_SHELL_FIRST_VALUE;
}

[[nodiscard]] ShellJob *FindFreeJob() noexcept {
    for (uint64_t job_index = OS_USER_SHELL_EMPTY_VALUE;
         job_index < OS_USER_SHELL_JOB_CAPACITY; ++job_index) {
        if (shell_jobs[job_index].state == ShellJobState::Free) {
            return shell_jobs + job_index;
        }
    }
    return nullptr;
}

[[nodiscard]] ShellJob *FindJob(const uint64_t job_id) noexcept {
    for (uint64_t job_index = OS_USER_SHELL_EMPTY_VALUE;
         job_index < OS_USER_SHELL_JOB_CAPACITY; ++job_index) {
        if (shell_jobs[job_index].state != ShellJobState::Free &&
            shell_jobs[job_index].job_id == job_id) {
            return shell_jobs + job_index;
        }
    }
    return nullptr;
}

[[nodiscard]] ShellJob *FindLatestJob() noexcept {
    ShellJob *latest_job = nullptr;
    for (uint64_t job_index = OS_USER_SHELL_EMPTY_VALUE;
         job_index < OS_USER_SHELL_JOB_CAPACITY; ++job_index) {
        if (shell_jobs[job_index].state != ShellJobState::Free &&
            (latest_job == nullptr ||
             shell_jobs[job_index].job_id > latest_job->job_id)) {
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
           WriteBytes(state_text, state_text_length) &&
           WriteUnsigned(job.process_group_id) &&
           WriteLiteral(OS_USER_SHELL_JOB_SEPARATOR);
}

void RefreshJobState(ShellJob &job) noexcept {
    uint64_t live_member_count = OS_USER_SHELL_EMPTY_VALUE;
    uint64_t stopped_member_count = OS_USER_SHELL_EMPTY_VALUE;
    for (uint64_t member_index = OS_USER_SHELL_EMPTY_VALUE;
         member_index < job.process_count; ++member_index) {
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
        os::abi::OS_ABI_PROCESS_WAIT_EXITED_FLAG |
        os::abi::OS_ABI_PROCESS_WAIT_STOPPED_FLAG |
        os::abi::OS_ABI_PROCESS_WAIT_CONTINUED_FLAG |
        (no_hang ? os::abi::OS_ABI_PROCESS_WAIT_NO_HANG_FLAG
                 : OS_USER_SHELL_EMPTY_VALUE);
    for (uint64_t member_index = OS_USER_SHELL_EMPTY_VALUE;
         member_index < job.process_count; ++member_index) {
        if (job.member_states[member_index] == ShellJobMemberState::Exited ||
            (!no_hang &&
             job.member_states[member_index] == ShellJobMemberState::Stopped)) {
            continue;
        }
        while (true) {
            os::abi::ProcessWaitEventResult wait_result{};
            const int64_t result =
                WaitProcessEvent(job.process_ids[member_index], wait_flags, wait_result);
            if (no_hang &&
                result == os::abi::OS_ABI_SYSTEM_CALL_RESULT_WOULD_BLOCK) {
                break;
            }
            if (result != static_cast<int64_t>(job.process_ids[member_index]) ||
                wait_result.process_id != job.process_ids[member_index]) {
                return false;
            }
            if (wait_result.event_type == os::abi::ProcessWaitEventType::Exited) {
                job.member_states[member_index] = ShellJobMemberState::Exited;
                if (member_index + OS_USER_SHELL_FIRST_VALUE == job.process_count &&
                    wait_result.termination_reason ==
                        os::abi::ProcessTerminationReason::Exited) {
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
    if (SendProcessGroupSignal(job.process_group_id,
                               os::abi::OS_ABI_SIGNAL_CONTINUE_NUMBER) <
        OS_USER_SHELL_SUCCESS_RESULT) {
        return false;
    }
    for (uint64_t member_index = OS_USER_SHELL_EMPTY_VALUE;
         member_index < job.process_count; ++member_index) {
        if (job.member_states[member_index] == ShellJobMemberState::Stopped) {
            job.member_states[member_index] = ShellJobMemberState::Running;
        }
    }
    job.state = ShellJobState::Running;
    return true;
}

void ReapBackgroundJobs() noexcept {
    for (uint64_t job_index = OS_USER_SHELL_EMPTY_VALUE;
         job_index < OS_USER_SHELL_JOB_CAPACITY; ++job_index) {
        ShellJob &job = shell_jobs[job_index];
        if (job.state == ShellJobState::Free ||
            !PumpJobEvents(job, true)) {
            continue;
        }
        if (job.state == ShellJobState::Done) {
            static_cast<void>(WriteJob(job));
            ReleaseJob(job);
        }
    }
}

[[nodiscard]] bool ParseJobId(const ShellExecutionPlan &execution_plan,
                              uint64_t &job_id) noexcept {
    job_id = OS_USER_SHELL_EMPTY_VALUE;
    const ShellExecutionStage &stage =
        execution_plan.stages[OS_USER_SHELL_EMPTY_VALUE];
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
    const char *const bytes =
        ShellExecutionArgumentBytes(execution_plan, argument_index);
    const uint64_t length_bytes =
        execution_plan.arguments[argument_index].length_bytes;
    if (bytes == nullptr || length_bytes == OS_USER_SHELL_EMPTY_VALUE) {
        return false;
    }
    for (uint64_t byte_index = OS_USER_SHELL_EMPTY_VALUE; byte_index < length_bytes;
         ++byte_index) {
        if (bytes[byte_index] < '0' || bytes[byte_index] > '9' ||
            job_id > (UINT64_MAX - static_cast<uint64_t>(bytes[byte_index] - '0')) /
                         OS_USER_SHELL_DECIMAL_BASE) {
            return false;
        }
        job_id = job_id * OS_USER_SHELL_DECIMAL_BASE +
                 static_cast<uint64_t>(bytes[byte_index] - '0');
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
                                    const uint64_t pipe_count,
                                    const uint64_t process_group_id,
                                    const uint64_t inherited_signal_mask) noexcept {
    // 子进程继承 Shell handler，但在父进程发布前台组前同时继承了阻塞 mask。
    // 必须先恢复默认处置，再加入作业组并解除 mask，避免 Ctrl-C/Z 落入
    // fork 与 exec 之间时调用 Shell handler。
    if (!RestoreChildSignalPolicy() ||
        SetProcessGroup(process_group_id) != OS_USER_SHELL_SUCCESS_RESULT ||
        SetSignalMask(inherited_signal_mask, nullptr) !=
            OS_USER_SHELL_SUCCESS_RESULT) {
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
    if (SetSignalMask(OS_USER_SHELL_JOB_CONTROL_SIGNAL_SET,
                      &inherited_signal_mask) !=
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
            ExecuteChildStage(execution_plan, stage_index, pipes, pipe_count,
                              process_group_id, inherited_signal_mask);
        }
        if (fork_result < OS_USER_SHELL_SUCCESS_RESULT) {
            ClosePipelineDescriptors(pipes, pipe_count);
            if (process_group_id != OS_USER_SHELL_EMPTY_VALUE) {
                static_cast<void>(SendProcessGroupSignal(
                    process_group_id, os::abi::OS_ABI_SIGNAL_KILL_NUMBER));
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
            static_cast<void>(SendProcessGroupSignal(
                process_group_id, os::abi::OS_ABI_SIGNAL_KILL_NUMBER));
            static_cast<void>(PumpJobEvents(*job, false));
            static_cast<void>(SetSignalMask(inherited_signal_mask, nullptr));
            ReleaseJob(*job);
            return OS_USER_SHELL_FAILURE_EXIT_CODE;
        }
    }
    ClosePipelineDescriptors(pipes, pipe_count);
    if (SetSignalMask(inherited_signal_mask, nullptr) !=
        OS_USER_SHELL_SUCCESS_RESULT) {
        static_cast<void>(SendProcessGroupSignal(
            process_group_id, os::abi::OS_ABI_SIGNAL_KILL_NUMBER));
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
        static_cast<void>(SendProcessGroupSignal(
            process_group_id, os::abi::OS_ABI_SIGNAL_KILL_NUMBER));
        static_cast<void>(PumpJobEvents(*job, false));
        ReleaseJob(*job);
        return OS_USER_SHELL_FAILURE_EXIT_CODE;
    }
    if (!WriteLiteral(OS_USER_SHELL_FOREGROUND_JOB_WAITING_MARKER)) {
        static_cast<void>(SendProcessGroupSignal(
            process_group_id, os::abi::OS_ABI_SIGNAL_KILL_NUMBER));
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
    return !execution_plan.background && !stage.has_input_redirection &&
           !stage.has_output_redirection &&
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
    if (IsSingleStageBuiltin(execution_plan, OS_USER_SHELL_JOBS_COMMAND,
                             sizeof(OS_USER_SHELL_JOBS_COMMAND) -
                                 OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES)) {
        const ShellExecutionStage &stage =
            execution_plan.stages[OS_USER_SHELL_EMPTY_VALUE];
        if (stage.argument_count != OS_USER_SHELL_JOBS_ARGUMENT_COUNT) {
            static_cast<void>(WriteLiteral(OS_USER_SHELL_USAGE_ERROR));
            return OS_USER_SHELL_FAILURE_EXIT_CODE;
        }
        ReapBackgroundJobs();
        for (uint64_t job_index = OS_USER_SHELL_EMPTY_VALUE;
             job_index < OS_USER_SHELL_JOB_CAPACITY; ++job_index) {
            if (shell_jobs[job_index].state != ShellJobState::Free &&
                !WriteJob(shell_jobs[job_index])) {
                return OS_USER_SHELL_FAILURE_EXIT_CODE;
            }
        }
        return OS_USER_SHELL_SUCCESS_RESULT;
    }
    const bool foreground_builtin =
        IsSingleStageBuiltin(execution_plan, OS_USER_SHELL_FOREGROUND_COMMAND,
                             sizeof(OS_USER_SHELL_FOREGROUND_COMMAND) -
                                 OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES);
    const bool background_builtin =
        IsSingleStageBuiltin(execution_plan, OS_USER_SHELL_BACKGROUND_COMMAND,
                             sizeof(OS_USER_SHELL_BACKGROUND_COMMAND) -
                                 OS_USER_SHELL_STRING_TERMINATOR_SIZE_BYTES);
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
            if ((job->state == ShellJobState::Stopped && !ContinueJob(*job)) ||
                !WriteJob(*job)) {
                return OS_USER_SHELL_FAILURE_EXIT_CODE;
            }
            return OS_USER_SHELL_SUCCESS_RESULT;
        }
        if (SetTerminalForegroundGroup(job->process_group_id) !=
            OS_USER_SHELL_SUCCESS_RESULT) {
            return OS_USER_SHELL_FAILURE_EXIT_CODE;
        }
        if (!WriteLiteral(OS_USER_SHELL_FOREGROUND_JOB_WAITING_MARKER)) {
            static_cast<void>(SetTerminalForegroundGroup(shell_process_group_id));
            return OS_USER_SHELL_FAILURE_EXIT_CODE;
        }
        const bool continued =
            job->state != ShellJobState::Stopped || ContinueJob(*job);
        const bool waited = continued && PumpJobEvents(*job, false);
        const bool restored =
            SetTerminalForegroundGroup(shell_process_group_id) ==
            OS_USER_SHELL_SUCCESS_RESULT;
        if (!waited || !restored) {
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
    InitializeJobTable();
    const uint64_t process_id = GetProcessId();
    if (process_id == OS_USER_SHELL_EMPTY_VALUE ||
        SetProcessGroup(process_id) != OS_USER_SHELL_SUCCESS_RESULT) {
        return OS_USER_SHELL_FAILURE_EXIT_CODE;
    }
    shell_process_group_id = process_id;
    os::abi::TerminalInformation terminal_information{};
    if (GetTerminalInformation(terminal_information) != OS_USER_SHELL_SUCCESS_RESULT ||
        SetTerminalForegroundGroup(shell_process_group_id) !=
            OS_USER_SHELL_SUCCESS_RESULT ||
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

    if (!WriteLiteral(OS_USER_SHELL_BANNER) ||
        !WriteLiteral(OS_USER_SHELL_JOB_CONTROL_READY_MARKER) ||
        !WriteLiteral(OS_USER_SHELL_READY_MARKER)) {
        return OS_USER_SHELL_FAILURE_EXIT_CODE;
    }
    char line[OS_USER_SHELL_EXECUTION_MAXIMUM_LINE_SIZE_BYTES]{};
    uint64_t line_length_bytes = OS_USER_SHELL_EMPTY_VALUE;
    while (true) {
        ReapBackgroundJobs();
        if (!WritePrompt()) {
            return OS_USER_SHELL_FAILURE_EXIT_CODE;
        }
        line_length_bytes = OS_USER_SHELL_EMPTY_VALUE;
        bool input_interrupted = false;
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
            if (character == OS_USER_SHELL_NEWLINE_CHARACTER ||
                character == OS_USER_SHELL_CARRIAGE_RETURN_CHARACTER) {
                break;
            }
            if (character == OS_USER_SHELL_BACKSPACE_CHARACTER ||
                character == OS_USER_SHELL_DELETE_CHARACTER) {
                if (line_length_bytes != OS_USER_SHELL_EMPTY_VALUE) {
                    --line_length_bytes;
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
        }
        if (input_interrupted) {
            continue;
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

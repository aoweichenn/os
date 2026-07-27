#include "os/user/system_call.hpp"

#include "os/abi/system_call.hpp"

#include <stdint.h>

namespace {

constexpr char OS_USER_INIT_STARTED_MESSAGE[] = "[OS][USER][INIT] STARTED\r\n";
constexpr char OS_USER_INIT_ARGUMENTS_VALID_MESSAGE[] = "[OS][USER][INIT] ARGUMENTS_VALID\r\n";
constexpr char OS_USER_INIT_CHILDREN_STARTED_MESSAGE[] = "[OS][USER][INIT] CHILDREN_STARTED\r\n";
constexpr char OS_USER_INIT_ORPHAN_REAPED_MESSAGE[] = "[OS][USER][INIT] ORPHAN_REAPED\r\n";
constexpr char OS_USER_INIT_ALL_CHILDREN_REAPED_MESSAGE[] =
    "[OS][USER][INIT] ALL_CHILDREN_REAPED\r\n";
constexpr char OS_USER_INIT_NO_ZOMBIES_MESSAGE[] = "[OS][USER][INIT] NO_ZOMBIES\r\n";
constexpr char OS_USER_INIT_MEMORY_PROBE_REAPED_MESSAGE[] =
    "[OS][USER][INIT] MEMORY_PROBE_REAPED\r\n";
constexpr char OS_USER_INIT_VM_FAULT_POLICIES_MESSAGE[] =
    "[OS][USER][INIT] VM_FAULT_POLICIES_VERIFIED\r\n";
constexpr char OS_USER_INIT_FORK_PROBE_REAPED_MESSAGE[] =
    "[OS][USER][INIT] FORK_PROBE_REAPED\r\n";
constexpr char OS_USER_INIT_PATH[] = "/sbin/init";
constexpr char OS_USER_INIT_ENVIRONMENT[] = "OS_STAGE=v1.11";
constexpr char OS_USER_INIT_ORPHAN_PARENT_PATH[] = "/bin/orphan_parent";
constexpr char OS_USER_INIT_ARGUMENT_PROBE_PATH[] = "/bin/argument_probe";
constexpr char OS_USER_INIT_EXEC_PROBE_PATH[] = "/bin/exec_probe";
constexpr char OS_USER_INIT_FILE_SYSTEM_PROBE_PATH[] = "/bin/fs_probe";
constexpr char OS_USER_INIT_SMOKE_PATH[] = "/bin/smoke";
constexpr char OS_USER_INIT_SHELL_PATH[] = "/bin/sh";
constexpr char OS_USER_INIT_MEMORY_PROBE_PATH[] = "/bin/memory_probe";
constexpr char OS_USER_INIT_FORK_PROBE_PATH[] = "/bin/fork_probe";
constexpr char OS_USER_INIT_MEMORY_GUARD_PROBE_PATH[] = "/bin/memory_guard_probe";
constexpr char OS_USER_INIT_MEMORY_PROTECTION_PROBE_PATH[] = "/bin/memory_protection_probe";
constexpr uint64_t OS_USER_INIT_STRING_TERMINATOR_SIZE_BYTES = 1ULL;
constexpr uint64_t OS_USER_INIT_EXPECTED_PROCESS_ID = 1ULL;
constexpr uint64_t OS_USER_INIT_INITIAL_CHILD_COUNT = 6ULL;
constexpr uint64_t OS_USER_INIT_ADOPTED_CHILD_COUNT = 1ULL;
constexpr uint64_t OS_USER_INIT_EXPECTED_WAIT_COUNT =
    OS_USER_INIT_INITIAL_CHILD_COUNT + OS_USER_INIT_ADOPTED_CHILD_COUNT;
constexpr uint64_t OS_USER_INIT_FIRST_INDEX = 0ULL;
constexpr uint64_t OS_USER_INIT_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_USER_INIT_ARGUMENT_PROBE_INDEX = 1ULL;
constexpr uint64_t OS_USER_INIT_EXEC_PROBE_INDEX = 2ULL;
constexpr uint64_t OS_USER_INIT_FILE_SYSTEM_PROBE_INDEX = 3ULL;
constexpr uint64_t OS_USER_INIT_SMOKE_INDEX = 4ULL;
constexpr uint64_t OS_USER_INIT_SHELL_INDEX = 5ULL;
constexpr uint64_t OS_USER_INIT_LARGE_ARGUMENT_SIZE_BYTES =
    os::abi::OS_ABI_PROCESS_MAXIMUM_ARGUMENT_ENVIRONMENT_BYTES -
    (sizeof(OS_USER_INIT_ARGUMENT_PROBE_PATH) + sizeof(OS_USER_INIT_ENVIRONMENT) +
     OS_USER_INIT_STRING_TERMINATOR_SIZE_BYTES);
constexpr uint64_t OS_USER_INIT_PATTERN_MODULUS = 23ULL;
constexpr uint8_t OS_USER_INIT_PATTERN_BASE = 0x41U;
constexpr char OS_USER_INIT_STRING_TERMINATOR = '\0';
constexpr int64_t OS_USER_INIT_SUCCESS_EXIT_CODE = 0LL;
constexpr int64_t OS_USER_INIT_FAILURE_EXIT_CODE = 1LL;
constexpr int64_t OS_USER_INIT_ORPHAN_PARENT_EXIT_CODE = 23LL;
constexpr int64_t OS_USER_INIT_ORPHAN_CHILD_EXIT_CODE = 42LL;
constexpr int64_t OS_USER_INIT_FIRST_ERROR_RESULT = -1LL;
constexpr uint64_t OS_USER_INIT_PAGE_FAULT_VECTOR = 14ULL;

uint8_t large_argument[OS_USER_INIT_LARGE_ARGUMENT_SIZE_BYTES];

template <uint64_t MessageSizeBytes>
[[nodiscard]] bool WriteMessage(const char (&message)[MessageSizeBytes]) noexcept {
    return os::user::WriteLog(message,
                              MessageSizeBytes - OS_USER_INIT_STRING_TERMINATOR_SIZE_BYTES) >
           OS_USER_INIT_FIRST_ERROR_RESULT;
}

[[nodiscard]] bool StringsEqual(const char *const left, const char *const right,
                                const uint64_t length_bytes) noexcept {
    if (left == nullptr || right == nullptr) {
        return false;
    }
    for (uint64_t byte_index = OS_USER_INIT_FIRST_INDEX; byte_index < length_bytes; ++byte_index) {
        if (left[byte_index] != right[byte_index]) {
            return false;
        }
    }
    return left[length_bytes] == OS_USER_INIT_STRING_TERMINATOR;
}

[[nodiscard]] int64_t SpawnSimpleProcess(const char *const path,
                                         const uint64_t path_length_bytes) noexcept {
    const os::abi::ProcessString arguments[]{
        os::abi::ProcessString{
            .address = reinterpret_cast<uint64_t>(path),
            .length_bytes = path_length_bytes,
        },
    };
    const os::abi::ProcessString environment[]{
        os::abi::ProcessString{
            .address = reinterpret_cast<uint64_t>(OS_USER_INIT_ENVIRONMENT),
            .length_bytes =
                sizeof(OS_USER_INIT_ENVIRONMENT) - OS_USER_INIT_STRING_TERMINATOR_SIZE_BYTES,
        },
    };
    const os::abi::ProcessLaunchRequest request{
        .path_address = reinterpret_cast<uint64_t>(path),
        .path_length_bytes = path_length_bytes,
        .argument_vector_address = reinterpret_cast<uint64_t>(arguments),
        .argument_count = sizeof(arguments) / sizeof(arguments[OS_USER_INIT_FIRST_INDEX]),
        .environment_vector_address = reinterpret_cast<uint64_t>(environment),
        .environment_count = sizeof(environment) / sizeof(environment[OS_USER_INIT_FIRST_INDEX]),
    };
    return os::user::SpawnProcess(request);
}

[[nodiscard]] int64_t SpawnArgumentProbe() noexcept {
    for (uint64_t byte_index = OS_USER_INIT_FIRST_INDEX;
         byte_index < OS_USER_INIT_LARGE_ARGUMENT_SIZE_BYTES; ++byte_index) {
        large_argument[byte_index] = static_cast<uint8_t>(
            OS_USER_INIT_PATTERN_BASE + byte_index % OS_USER_INIT_PATTERN_MODULUS);
    }
    const os::abi::ProcessString arguments[]{
        os::abi::ProcessString{
            .address = reinterpret_cast<uint64_t>(OS_USER_INIT_ARGUMENT_PROBE_PATH),
            .length_bytes = sizeof(OS_USER_INIT_ARGUMENT_PROBE_PATH) -
                            OS_USER_INIT_STRING_TERMINATOR_SIZE_BYTES,
        },
        os::abi::ProcessString{
            .address = reinterpret_cast<uint64_t>(large_argument),
            .length_bytes = sizeof(large_argument),
        },
    };
    const os::abi::ProcessString environment[]{
        os::abi::ProcessString{
            .address = reinterpret_cast<uint64_t>(OS_USER_INIT_ENVIRONMENT),
            .length_bytes =
                sizeof(OS_USER_INIT_ENVIRONMENT) - OS_USER_INIT_STRING_TERMINATOR_SIZE_BYTES,
        },
    };
    const os::abi::ProcessLaunchRequest request{
        .path_address = reinterpret_cast<uint64_t>(OS_USER_INIT_ARGUMENT_PROBE_PATH),
        .path_length_bytes =
            sizeof(OS_USER_INIT_ARGUMENT_PROBE_PATH) - OS_USER_INIT_STRING_TERMINATOR_SIZE_BYTES,
        .argument_vector_address = reinterpret_cast<uint64_t>(arguments),
        .argument_count = sizeof(arguments) / sizeof(arguments[OS_USER_INIT_FIRST_INDEX]),
        .environment_vector_address = reinterpret_cast<uint64_t>(environment),
        .environment_count = sizeof(environment) / sizeof(environment[OS_USER_INIT_FIRST_INDEX]),
    };
    return os::user::SpawnProcess(request);
}

[[nodiscard]] bool RunExpectedProcess(const char *const path, const uint64_t path_length_bytes,
                                      const os::abi::ProcessTerminationReason termination_reason,
                                      const int64_t exit_code,
                                      const uint64_t exception_vector) noexcept {
    const int64_t process_id = SpawnSimpleProcess(path, path_length_bytes);
    if (process_id <= OS_USER_INIT_FIRST_ERROR_RESULT) {
        return false;
    }
    os::abi::ProcessWaitResult wait_result{};
    return os::user::WaitProcess(static_cast<uint64_t>(process_id), wait_result) == process_id &&
           wait_result.process_id == static_cast<uint64_t>(process_id) &&
           wait_result.termination_reason == termination_reason &&
           wait_result.exit_code == exit_code && wait_result.exception_vector == exception_vector;
}

}

extern "C" [[noreturn, gnu::section(".text.os_user_entry")]]
void OsUserEntry(const uint64_t argument_count, const char *const *const arguments,
                 const char *const *const environment) noexcept {
    if (os::user::GetProcessId() != OS_USER_INIT_EXPECTED_PROCESS_ID ||
        argument_count != OS_USER_INIT_COUNTER_INCREMENT || arguments == nullptr ||
        environment == nullptr ||
        !StringsEqual(arguments[OS_USER_INIT_FIRST_INDEX], OS_USER_INIT_PATH,
                      sizeof(OS_USER_INIT_PATH) - OS_USER_INIT_STRING_TERMINATOR_SIZE_BYTES) ||
        !StringsEqual(environment[OS_USER_INIT_FIRST_INDEX], OS_USER_INIT_ENVIRONMENT,
                      sizeof(OS_USER_INIT_ENVIRONMENT) -
                          OS_USER_INIT_STRING_TERMINATOR_SIZE_BYTES) ||
        environment[OS_USER_INIT_COUNTER_INCREMENT] != nullptr ||
        !WriteMessage(OS_USER_INIT_STARTED_MESSAGE) ||
        !WriteMessage(OS_USER_INIT_ARGUMENTS_VALID_MESSAGE)) {
        os::user::ExitProcess(OS_USER_INIT_FAILURE_EXIT_CODE);
    }

    int64_t child_process_ids[OS_USER_INIT_INITIAL_CHILD_COUNT]{};
    child_process_ids[OS_USER_INIT_FIRST_INDEX] = SpawnSimpleProcess(
        OS_USER_INIT_ORPHAN_PARENT_PATH,
        sizeof(OS_USER_INIT_ORPHAN_PARENT_PATH) - OS_USER_INIT_STRING_TERMINATOR_SIZE_BYTES);
    child_process_ids[OS_USER_INIT_ARGUMENT_PROBE_INDEX] = SpawnArgumentProbe();
    child_process_ids[OS_USER_INIT_EXEC_PROBE_INDEX] = SpawnSimpleProcess(
        OS_USER_INIT_EXEC_PROBE_PATH,
        sizeof(OS_USER_INIT_EXEC_PROBE_PATH) - OS_USER_INIT_STRING_TERMINATOR_SIZE_BYTES);
    child_process_ids[OS_USER_INIT_FILE_SYSTEM_PROBE_INDEX] = SpawnSimpleProcess(
        OS_USER_INIT_FILE_SYSTEM_PROBE_PATH,
        sizeof(OS_USER_INIT_FILE_SYSTEM_PROBE_PATH) - OS_USER_INIT_STRING_TERMINATOR_SIZE_BYTES);
    child_process_ids[OS_USER_INIT_SMOKE_INDEX] =
        SpawnSimpleProcess(OS_USER_INIT_SMOKE_PATH, sizeof(OS_USER_INIT_SMOKE_PATH) -
                                                        OS_USER_INIT_STRING_TERMINATOR_SIZE_BYTES);
    child_process_ids[OS_USER_INIT_SHELL_INDEX] =
        SpawnSimpleProcess(OS_USER_INIT_SHELL_PATH, sizeof(OS_USER_INIT_SHELL_PATH) -
                                                        OS_USER_INIT_STRING_TERMINATOR_SIZE_BYTES);
    for (uint64_t child_index = OS_USER_INIT_FIRST_INDEX;
         child_index < OS_USER_INIT_INITIAL_CHILD_COUNT; ++child_index) {
        if (child_process_ids[child_index] <= OS_USER_INIT_FIRST_ERROR_RESULT) {
            os::user::ExitProcess(OS_USER_INIT_FAILURE_EXIT_CODE);
        }
    }
    if (!WriteMessage(OS_USER_INIT_CHILDREN_STARTED_MESSAGE)) {
        os::user::ExitProcess(OS_USER_INIT_FAILURE_EXIT_CODE);
    }

    bool adopted_child_reaped = false;
    for (uint64_t wait_index = OS_USER_INIT_FIRST_INDEX;
         wait_index < OS_USER_INIT_EXPECTED_WAIT_COUNT; ++wait_index) {
        os::abi::ProcessWaitResult wait_result{};
        const int64_t wait_status =
            os::user::WaitProcess(os::abi::OS_ABI_PROCESS_WAIT_ANY_PROCESS_ID, wait_result);
        if (wait_status <= OS_USER_INIT_FIRST_ERROR_RESULT ||
            wait_result.termination_reason != os::abi::ProcessTerminationReason::Exited) {
            os::user::ExitProcess(OS_USER_INIT_FAILURE_EXIT_CODE);
        }
        bool initial_child = false;
        for (uint64_t child_index = OS_USER_INIT_FIRST_INDEX;
             child_index < OS_USER_INIT_INITIAL_CHILD_COUNT; ++child_index) {
            if (static_cast<uint64_t>(child_process_ids[child_index]) == wait_result.process_id) {
                initial_child = true;
                if (child_index == OS_USER_INIT_FIRST_INDEX &&
                    wait_result.exit_code != OS_USER_INIT_ORPHAN_PARENT_EXIT_CODE) {
                    os::user::ExitProcess(OS_USER_INIT_FAILURE_EXIT_CODE);
                }
                break;
            }
        }
        if (!initial_child) {
            if (adopted_child_reaped ||
                wait_result.exit_code != OS_USER_INIT_ORPHAN_CHILD_EXIT_CODE) {
                os::user::ExitProcess(OS_USER_INIT_FAILURE_EXIT_CODE);
            }
            adopted_child_reaped = true;
            if (!WriteMessage(OS_USER_INIT_ORPHAN_REAPED_MESSAGE)) {
                os::user::ExitProcess(OS_USER_INIT_FAILURE_EXIT_CODE);
            }
        } else if (wait_result.exit_code != OS_USER_INIT_SUCCESS_EXIT_CODE &&
                   wait_result.exit_code != OS_USER_INIT_ORPHAN_PARENT_EXIT_CODE) {
            os::user::ExitProcess(OS_USER_INIT_FAILURE_EXIT_CODE);
        }
    }

    if (!RunExpectedProcess(OS_USER_INIT_MEMORY_PROBE_PATH,
                            sizeof(OS_USER_INIT_MEMORY_PROBE_PATH) -
                                OS_USER_INIT_STRING_TERMINATOR_SIZE_BYTES,
                            os::abi::ProcessTerminationReason::Exited,
                            OS_USER_INIT_SUCCESS_EXIT_CODE, OS_USER_INIT_FIRST_INDEX) ||
        !WriteMessage(OS_USER_INIT_MEMORY_PROBE_REAPED_MESSAGE)) {
        os::user::ExitProcess(OS_USER_INIT_FAILURE_EXIT_CODE);
    }
    if (!RunExpectedProcess(OS_USER_INIT_FORK_PROBE_PATH,
                            sizeof(OS_USER_INIT_FORK_PROBE_PATH) -
                                OS_USER_INIT_STRING_TERMINATOR_SIZE_BYTES,
                            os::abi::ProcessTerminationReason::Exited,
                            OS_USER_INIT_SUCCESS_EXIT_CODE,
                            OS_USER_INIT_FIRST_INDEX) ||
        !WriteMessage(OS_USER_INIT_FORK_PROBE_REAPED_MESSAGE)) {
        os::user::ExitProcess(OS_USER_INIT_FAILURE_EXIT_CODE);
    }
    if (!RunExpectedProcess(OS_USER_INIT_MEMORY_GUARD_PROBE_PATH,
                            sizeof(OS_USER_INIT_MEMORY_GUARD_PROBE_PATH) -
                                OS_USER_INIT_STRING_TERMINATOR_SIZE_BYTES,
                            os::abi::ProcessTerminationReason::Exception,
                            OS_USER_INIT_SUCCESS_EXIT_CODE, OS_USER_INIT_PAGE_FAULT_VECTOR) ||
        !RunExpectedProcess(OS_USER_INIT_MEMORY_PROTECTION_PROBE_PATH,
                            sizeof(OS_USER_INIT_MEMORY_PROTECTION_PROBE_PATH) -
                                OS_USER_INIT_STRING_TERMINATOR_SIZE_BYTES,
                            os::abi::ProcessTerminationReason::Exception,
                            OS_USER_INIT_SUCCESS_EXIT_CODE, OS_USER_INIT_PAGE_FAULT_VECTOR) ||
        !WriteMessage(OS_USER_INIT_VM_FAULT_POLICIES_MESSAGE)) {
        os::user::ExitProcess(OS_USER_INIT_FAILURE_EXIT_CODE);
    }

    os::abi::ProcessWaitResult no_child_result{};
    if (!adopted_child_reaped ||
        os::user::WaitProcess(os::abi::OS_ABI_PROCESS_WAIT_ANY_PROCESS_ID, no_child_result) !=
            os::abi::OS_ABI_SYSTEM_CALL_RESULT_NO_CHILD_PROCESS ||
        !WriteMessage(OS_USER_INIT_ALL_CHILDREN_REAPED_MESSAGE) ||
        !WriteMessage(OS_USER_INIT_NO_ZOMBIES_MESSAGE)) {
        os::user::ExitProcess(OS_USER_INIT_FAILURE_EXIT_CODE);
    }
    os::user::ExitProcess(OS_USER_INIT_SUCCESS_EXIT_CODE);
}

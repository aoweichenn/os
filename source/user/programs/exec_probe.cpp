#include "os/user/system_call.hpp"

#include "os/abi/system_call.hpp"

#include <stdint.h>

namespace {

constexpr char OS_USER_EXEC_PROBE_TRUNCATED_PATH[] = "/bin/truncated.elf";
constexpr char OS_USER_EXEC_PROBE_TARGET_PATH[] = "/bin/exec_target";
constexpr char OS_USER_EXEC_PROBE_TARGET_ARGUMENT[] = "committed";
constexpr char OS_USER_EXEC_PROBE_TARGET_ENVIRONMENT[] = "OS_EXEC=atomic";
constexpr char OS_USER_EXEC_PROBE_OLD_IMAGE_SURVIVED_MESSAGE[] =
    "[OS][USER][PROC] EXEC_FAILURE_PRESERVED_IMAGE\r\n";
constexpr char OS_USER_EXEC_PROBE_LARGE_ARGUMENT_REJECTED_MESSAGE[] =
    "[OS][USER][PROC] EXEC_E2BIG_PRESERVED_IMAGE\r\n";
constexpr uint64_t OS_USER_EXEC_PROBE_STRING_TERMINATOR_SIZE_BYTES = 1ULL;
constexpr uint64_t OS_USER_EXEC_PROBE_FIRST_INDEX = 0ULL;
constexpr uint64_t OS_USER_EXEC_PROBE_EMPTY_VALUE = 0ULL;
constexpr uint8_t OS_USER_EXEC_PROBE_DUMMY_BYTE = 0x41U;
constexpr int64_t OS_USER_EXEC_PROBE_FAILURE_EXIT_CODE = 1LL;
constexpr int64_t OS_USER_EXEC_PROBE_FIRST_ERROR_RESULT = -1LL;

template <uint64_t MessageSizeBytes>
[[nodiscard]] bool WriteMessage(const char (&message)[MessageSizeBytes]) noexcept {
    return os::user::WriteLog(message,
                              MessageSizeBytes - OS_USER_EXEC_PROBE_STRING_TERMINATOR_SIZE_BYTES) >
           OS_USER_EXEC_PROBE_FIRST_ERROR_RESULT;
}

}

extern "C" [[noreturn, gnu::section(".text.os_user_entry")]]
void OsUserEntry() noexcept {
    const os::abi::ProcessLaunchRequest invalid_request{
        .path_address = reinterpret_cast<uint64_t>(OS_USER_EXEC_PROBE_TRUNCATED_PATH),
        .path_length_bytes = sizeof(OS_USER_EXEC_PROBE_TRUNCATED_PATH) -
                             OS_USER_EXEC_PROBE_STRING_TERMINATOR_SIZE_BYTES,
        .argument_vector_address = OS_USER_EXEC_PROBE_EMPTY_VALUE,
        .argument_count = OS_USER_EXEC_PROBE_EMPTY_VALUE,
        .environment_vector_address = OS_USER_EXEC_PROBE_EMPTY_VALUE,
        .environment_count = OS_USER_EXEC_PROBE_EMPTY_VALUE,
    };
    if (os::user::ExecProcess(invalid_request) !=
            os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_EXECUTABLE ||
        !WriteMessage(OS_USER_EXEC_PROBE_OLD_IMAGE_SURVIVED_MESSAGE)) {
        os::user::ExitProcess(OS_USER_EXEC_PROBE_FAILURE_EXIT_CODE);
    }

    const os::abi::ProcessString oversized_arguments[]{
        os::abi::ProcessString{
            .address = reinterpret_cast<uint64_t>(&OS_USER_EXEC_PROBE_DUMMY_BYTE),
            .length_bytes = os::abi::OS_ABI_PROCESS_MAXIMUM_ARGUMENT_ENVIRONMENT_BYTES,
        },
    };
    const os::abi::ProcessLaunchRequest oversized_request{
        .path_address = reinterpret_cast<uint64_t>(OS_USER_EXEC_PROBE_TARGET_PATH),
        .path_length_bytes = sizeof(OS_USER_EXEC_PROBE_TARGET_PATH) -
                             OS_USER_EXEC_PROBE_STRING_TERMINATOR_SIZE_BYTES,
        .argument_vector_address = reinterpret_cast<uint64_t>(oversized_arguments),
        .argument_count = sizeof(oversized_arguments) /
                          sizeof(oversized_arguments[OS_USER_EXEC_PROBE_FIRST_INDEX]),
        .environment_vector_address = OS_USER_EXEC_PROBE_EMPTY_VALUE,
        .environment_count = OS_USER_EXEC_PROBE_EMPTY_VALUE,
    };
    if (os::user::ExecProcess(oversized_request) !=
            os::abi::OS_ABI_SYSTEM_CALL_RESULT_ARGUMENT_LIST_TOO_LARGE ||
        !WriteMessage(OS_USER_EXEC_PROBE_LARGE_ARGUMENT_REJECTED_MESSAGE)) {
        os::user::ExitProcess(OS_USER_EXEC_PROBE_FAILURE_EXIT_CODE);
    }

    const os::abi::ProcessString arguments[]{
        os::abi::ProcessString{
            .address = reinterpret_cast<uint64_t>(OS_USER_EXEC_PROBE_TARGET_PATH),
            .length_bytes = sizeof(OS_USER_EXEC_PROBE_TARGET_PATH) -
                            OS_USER_EXEC_PROBE_STRING_TERMINATOR_SIZE_BYTES,
        },
        os::abi::ProcessString{
            .address = reinterpret_cast<uint64_t>(OS_USER_EXEC_PROBE_TARGET_ARGUMENT),
            .length_bytes = sizeof(OS_USER_EXEC_PROBE_TARGET_ARGUMENT) -
                            OS_USER_EXEC_PROBE_STRING_TERMINATOR_SIZE_BYTES,
        },
    };
    const os::abi::ProcessString environment[]{
        os::abi::ProcessString{
            .address = reinterpret_cast<uint64_t>(OS_USER_EXEC_PROBE_TARGET_ENVIRONMENT),
            .length_bytes = sizeof(OS_USER_EXEC_PROBE_TARGET_ENVIRONMENT) -
                            OS_USER_EXEC_PROBE_STRING_TERMINATOR_SIZE_BYTES,
        },
    };
    const os::abi::ProcessLaunchRequest request{
        .path_address = reinterpret_cast<uint64_t>(OS_USER_EXEC_PROBE_TARGET_PATH),
        .path_length_bytes = sizeof(OS_USER_EXEC_PROBE_TARGET_PATH) -
                             OS_USER_EXEC_PROBE_STRING_TERMINATOR_SIZE_BYTES,
        .argument_vector_address = reinterpret_cast<uint64_t>(arguments),
        .argument_count = sizeof(arguments) / sizeof(arguments[OS_USER_EXEC_PROBE_FIRST_INDEX]),
        .environment_vector_address = reinterpret_cast<uint64_t>(environment),
        .environment_count =
            sizeof(environment) / sizeof(environment[OS_USER_EXEC_PROBE_FIRST_INDEX]),
    };
    static_cast<void>(os::user::ExecProcess(request));
    os::user::ExitProcess(OS_USER_EXEC_PROBE_FAILURE_EXIT_CODE);
}

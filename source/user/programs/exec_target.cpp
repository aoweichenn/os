#include "os/user/system_call.hpp"

#include <stdint.h>

namespace {

constexpr char OS_USER_EXEC_TARGET_PATH[] = "/bin/exec_target";
constexpr char OS_USER_EXEC_TARGET_ARGUMENT[] = "committed";
constexpr char OS_USER_EXEC_TARGET_ENVIRONMENT[] = "OS_EXEC=atomic";
constexpr char OS_USER_EXEC_TARGET_COMMITTED_MESSAGE[] = "[OS][USER][PROC] EXEC_COMMITTED\r\n";
constexpr uint64_t OS_USER_EXEC_TARGET_STRING_TERMINATOR_SIZE_BYTES = 1ULL;
constexpr uint64_t OS_USER_EXEC_TARGET_FIRST_INDEX = 0ULL;
constexpr uint64_t OS_USER_EXEC_TARGET_ARGUMENT_INDEX = 1ULL;
constexpr uint64_t OS_USER_EXEC_TARGET_ENVIRONMENT_TERMINATOR_INDEX = 1ULL;
constexpr uint64_t OS_USER_EXEC_TARGET_EXPECTED_ARGUMENT_COUNT = 2ULL;
constexpr char OS_USER_EXEC_TARGET_STRING_TERMINATOR = '\0';
constexpr int64_t OS_USER_EXEC_TARGET_SUCCESS_EXIT_CODE = 0LL;
constexpr int64_t OS_USER_EXEC_TARGET_FAILURE_EXIT_CODE = 1LL;
constexpr int64_t OS_USER_EXEC_TARGET_FIRST_ERROR_RESULT = -1LL;

[[nodiscard]] bool VerifyString(const char *const actual, const char *const expected,
                                const uint64_t length_bytes) noexcept {
    if (actual == nullptr || expected == nullptr) {
        return false;
    }
    for (uint64_t byte_index = OS_USER_EXEC_TARGET_FIRST_INDEX; byte_index < length_bytes;
         ++byte_index) {
        if (actual[byte_index] != expected[byte_index]) {
            return false;
        }
    }
    return actual[length_bytes] == OS_USER_EXEC_TARGET_STRING_TERMINATOR;
}

}

extern "C" [[noreturn, gnu::section(".text.os_user_entry")]]
void OsUserEntry(const uint64_t argument_count, const char *const *const arguments,
                 const char *const *const environment) noexcept {
    if (argument_count != OS_USER_EXEC_TARGET_EXPECTED_ARGUMENT_COUNT || arguments == nullptr ||
        environment == nullptr ||
        !VerifyString(arguments[OS_USER_EXEC_TARGET_FIRST_INDEX], OS_USER_EXEC_TARGET_PATH,
                      sizeof(OS_USER_EXEC_TARGET_PATH) -
                          OS_USER_EXEC_TARGET_STRING_TERMINATOR_SIZE_BYTES) ||
        !VerifyString(arguments[OS_USER_EXEC_TARGET_ARGUMENT_INDEX], OS_USER_EXEC_TARGET_ARGUMENT,
                      sizeof(OS_USER_EXEC_TARGET_ARGUMENT) -
                          OS_USER_EXEC_TARGET_STRING_TERMINATOR_SIZE_BYTES) ||
        !VerifyString(environment[OS_USER_EXEC_TARGET_FIRST_INDEX], OS_USER_EXEC_TARGET_ENVIRONMENT,
                      sizeof(OS_USER_EXEC_TARGET_ENVIRONMENT) -
                          OS_USER_EXEC_TARGET_STRING_TERMINATOR_SIZE_BYTES) ||
        environment[OS_USER_EXEC_TARGET_ENVIRONMENT_TERMINATOR_INDEX] != nullptr ||
        os::user::WriteLog(OS_USER_EXEC_TARGET_COMMITTED_MESSAGE,
                           sizeof(OS_USER_EXEC_TARGET_COMMITTED_MESSAGE) -
                               OS_USER_EXEC_TARGET_STRING_TERMINATOR_SIZE_BYTES) <=
            OS_USER_EXEC_TARGET_FIRST_ERROR_RESULT) {
        os::user::ExitProcess(OS_USER_EXEC_TARGET_FAILURE_EXIT_CODE);
    }
    os::user::ExitProcess(OS_USER_EXEC_TARGET_SUCCESS_EXIT_CODE);
}

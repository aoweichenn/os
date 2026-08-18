#include "os/user/system_call.hpp"

#include "os/abi/system_call.hpp"

#include <stdint.h>

namespace {

constexpr char OS_USER_ARGUMENT_PROBE_PATH[] = "/bin/argument_probe";
constexpr char OS_USER_ARGUMENT_PROBE_ENVIRONMENT[] = "OS_STAGE=v2.4";
constexpr char OS_USER_ARGUMENT_PROBE_VERIFIED_MESSAGE[] =
    "[OS][USER][PROC] ARG_ENV_128K_VERIFIED\r\n";
constexpr uint64_t OS_USER_ARGUMENT_PROBE_STRING_TERMINATOR_SIZE_BYTES = 1ULL;
constexpr uint64_t OS_USER_ARGUMENT_PROBE_FIRST_INDEX = 0ULL;
constexpr uint64_t OS_USER_ARGUMENT_PROBE_LARGE_ARGUMENT_INDEX = 1ULL;
constexpr uint64_t OS_USER_ARGUMENT_PROBE_ENVIRONMENT_TERMINATOR_INDEX = 1ULL;
constexpr uint64_t OS_USER_ARGUMENT_PROBE_EXPECTED_ARGUMENT_COUNT = 2ULL;
constexpr uint64_t OS_USER_ARGUMENT_PROBE_LARGE_ARGUMENT_SIZE_BYTES =
    os::abi::OS_ABI_PROCESS_MAXIMUM_ARGUMENT_ENVIRONMENT_BYTES -
    (sizeof(OS_USER_ARGUMENT_PROBE_PATH) + sizeof(OS_USER_ARGUMENT_PROBE_ENVIRONMENT) +
     OS_USER_ARGUMENT_PROBE_STRING_TERMINATOR_SIZE_BYTES);
constexpr uint64_t OS_USER_ARGUMENT_PROBE_PATTERN_MODULUS = 23ULL;
constexpr uint8_t OS_USER_ARGUMENT_PROBE_PATTERN_BASE = 0x41U;
constexpr char OS_USER_ARGUMENT_PROBE_STRING_TERMINATOR = '\0';
constexpr int64_t OS_USER_ARGUMENT_PROBE_SUCCESS_EXIT_CODE = 0LL;
constexpr int64_t OS_USER_ARGUMENT_PROBE_FAILURE_EXIT_CODE = 1LL;
constexpr int64_t OS_USER_ARGUMENT_PROBE_FIRST_ERROR_RESULT = -1LL;

[[nodiscard]] bool VerifyString(const char *const actual, const char *const expected,
                                const uint64_t length_bytes) noexcept {
    if (actual == nullptr || expected == nullptr) {
        return false;
    }
    for (uint64_t byte_index = OS_USER_ARGUMENT_PROBE_FIRST_INDEX; byte_index < length_bytes;
         ++byte_index) {
        if (actual[byte_index] != expected[byte_index]) {
            return false;
        }
    }
    return actual[length_bytes] == OS_USER_ARGUMENT_PROBE_STRING_TERMINATOR;
}

}

extern "C" [[noreturn, gnu::section(".text.os_user_entry")]]
void OsUserEntry(const uint64_t argument_count, const char *const *const arguments,
                 const char *const *const environment) noexcept {
    if (argument_count != OS_USER_ARGUMENT_PROBE_EXPECTED_ARGUMENT_COUNT || arguments == nullptr ||
        environment == nullptr ||
        !VerifyString(arguments[OS_USER_ARGUMENT_PROBE_FIRST_INDEX], OS_USER_ARGUMENT_PROBE_PATH,
                      sizeof(OS_USER_ARGUMENT_PROBE_PATH) -
                          OS_USER_ARGUMENT_PROBE_STRING_TERMINATOR_SIZE_BYTES) ||
        !VerifyString(environment[OS_USER_ARGUMENT_PROBE_FIRST_INDEX],
                      OS_USER_ARGUMENT_PROBE_ENVIRONMENT,
                      sizeof(OS_USER_ARGUMENT_PROBE_ENVIRONMENT) -
                          OS_USER_ARGUMENT_PROBE_STRING_TERMINATOR_SIZE_BYTES) ||
        environment[OS_USER_ARGUMENT_PROBE_ENVIRONMENT_TERMINATOR_INDEX] != nullptr ||
        arguments[OS_USER_ARGUMENT_PROBE_LARGE_ARGUMENT_INDEX] == nullptr) {
        os::user::ExitProcess(OS_USER_ARGUMENT_PROBE_FAILURE_EXIT_CODE);
    }
    const char *const large_argument = arguments[OS_USER_ARGUMENT_PROBE_LARGE_ARGUMENT_INDEX];
    for (uint64_t byte_index = OS_USER_ARGUMENT_PROBE_FIRST_INDEX;
         byte_index < OS_USER_ARGUMENT_PROBE_LARGE_ARGUMENT_SIZE_BYTES; ++byte_index) {
        const char expected =
            static_cast<char>(OS_USER_ARGUMENT_PROBE_PATTERN_BASE +
                              byte_index % OS_USER_ARGUMENT_PROBE_PATTERN_MODULUS);
        if (large_argument[byte_index] != expected) {
            os::user::ExitProcess(OS_USER_ARGUMENT_PROBE_FAILURE_EXIT_CODE);
        }
    }
    if (large_argument[OS_USER_ARGUMENT_PROBE_LARGE_ARGUMENT_SIZE_BYTES] !=
            OS_USER_ARGUMENT_PROBE_STRING_TERMINATOR ||
        os::user::WriteLog(OS_USER_ARGUMENT_PROBE_VERIFIED_MESSAGE,
                           sizeof(OS_USER_ARGUMENT_PROBE_VERIFIED_MESSAGE) -
                               OS_USER_ARGUMENT_PROBE_STRING_TERMINATOR_SIZE_BYTES) <=
            OS_USER_ARGUMENT_PROBE_FIRST_ERROR_RESULT) {
        os::user::ExitProcess(OS_USER_ARGUMENT_PROBE_FAILURE_EXIT_CODE);
    }
    os::user::ExitProcess(OS_USER_ARGUMENT_PROBE_SUCCESS_EXIT_CODE);
}

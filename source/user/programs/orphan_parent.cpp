#include "os/user/system_call.hpp"

#include "os/abi/system_call.hpp"

#include <stdint.h>

namespace {

constexpr char OS_USER_ORPHAN_CHILD_PATH[] = "/bin/orphan_child";
constexpr char OS_USER_ORPHAN_PARENT_SPAWNED_MESSAGE[] =
    "[OS][USER][PROC] ORPHAN_CHILD_SPAWNED\r\n";
constexpr uint64_t OS_USER_ORPHAN_PARENT_STRING_TERMINATOR_SIZE_BYTES = 1ULL;
constexpr uint64_t OS_USER_ORPHAN_PARENT_FIRST_INDEX = 0ULL;
constexpr uint64_t OS_USER_ORPHAN_PARENT_EMPTY_VALUE = 0ULL;
constexpr int64_t OS_USER_ORPHAN_PARENT_EXIT_CODE = 23LL;
constexpr int64_t OS_USER_ORPHAN_PARENT_FAILURE_EXIT_CODE = 1LL;
constexpr int64_t OS_USER_ORPHAN_PARENT_FIRST_ERROR_RESULT = -1LL;

}

extern "C" [[noreturn, gnu::section(".text.os_user_entry")]]
void OsUserEntry() noexcept {
    const os::abi::ProcessString arguments[]{
        os::abi::ProcessString{
            .address = reinterpret_cast<uint64_t>(OS_USER_ORPHAN_CHILD_PATH),
            .length_bytes = sizeof(OS_USER_ORPHAN_CHILD_PATH) -
                            OS_USER_ORPHAN_PARENT_STRING_TERMINATOR_SIZE_BYTES,
        },
    };
    const os::abi::ProcessLaunchRequest request{
        .path_address = reinterpret_cast<uint64_t>(OS_USER_ORPHAN_CHILD_PATH),
        .path_length_bytes =
            sizeof(OS_USER_ORPHAN_CHILD_PATH) - OS_USER_ORPHAN_PARENT_STRING_TERMINATOR_SIZE_BYTES,
        .argument_vector_address = reinterpret_cast<uint64_t>(arguments),
        .argument_count = sizeof(arguments) / sizeof(arguments[OS_USER_ORPHAN_PARENT_FIRST_INDEX]),
        .environment_vector_address = OS_USER_ORPHAN_PARENT_EMPTY_VALUE,
        .environment_count = OS_USER_ORPHAN_PARENT_EMPTY_VALUE,
    };
    if (os::user::SpawnProcess(request) <= OS_USER_ORPHAN_PARENT_FIRST_ERROR_RESULT ||
        os::user::WriteLog(OS_USER_ORPHAN_PARENT_SPAWNED_MESSAGE,
                           sizeof(OS_USER_ORPHAN_PARENT_SPAWNED_MESSAGE) -
                               OS_USER_ORPHAN_PARENT_STRING_TERMINATOR_SIZE_BYTES) <=
            OS_USER_ORPHAN_PARENT_FIRST_ERROR_RESULT) {
        os::user::ExitProcess(OS_USER_ORPHAN_PARENT_FAILURE_EXIT_CODE);
    }
    os::user::ExitProcess(OS_USER_ORPHAN_PARENT_EXIT_CODE);
}

#include "os/user/system_call.hpp"

#include <stdint.h>

namespace {

constexpr char OS_USER_ORPHAN_CHILD_REPARENTED_MESSAGE[] =
    "[OS][USER][PROC] ORPHAN_CHILD_RUNNING\r\n";
constexpr uint64_t OS_USER_ORPHAN_CHILD_STRING_TERMINATOR_SIZE_BYTES = 1ULL;
constexpr int64_t OS_USER_ORPHAN_CHILD_EXIT_CODE = 42LL;
constexpr int64_t OS_USER_ORPHAN_CHILD_FAILURE_EXIT_CODE = 1LL;
constexpr int64_t OS_USER_ORPHAN_CHILD_FIRST_ERROR_RESULT = -1LL;

}

extern "C" [[noreturn, gnu::section(".text.os_user_entry")]]
void OsUserEntry() noexcept {
    if (os::user::WriteLog(OS_USER_ORPHAN_CHILD_REPARENTED_MESSAGE,
                           sizeof(OS_USER_ORPHAN_CHILD_REPARENTED_MESSAGE) -
                               OS_USER_ORPHAN_CHILD_STRING_TERMINATOR_SIZE_BYTES) <=
        OS_USER_ORPHAN_CHILD_FIRST_ERROR_RESULT) {
        os::user::ExitProcess(OS_USER_ORPHAN_CHILD_FAILURE_EXIT_CODE);
    }
    os::user::ExitProcess(OS_USER_ORPHAN_CHILD_EXIT_CODE);
}

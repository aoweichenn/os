#include "os/user/extended_state.hpp"
#include "os/user/shell.hpp"
#include "os/user/system_call.hpp"

namespace {

constexpr uint64_t OS_USER_SHELL_INVALID_PROCESS_ID = 0ULL;
constexpr int64_t OS_USER_SHELL_FAILURE_EXIT_CODE = 1LL;

}

extern "C" [[noreturn, gnu::section(".text.os_user_entry")]]
void OsUserEntry(const uint64_t argument_count, const char *const *const arguments,
                 const char *const *const environment) noexcept {
    const uint64_t process_id = os::user::GetProcessId();
    if (process_id == OS_USER_SHELL_INVALID_PROCESS_ID ||
        !os::user::InitializeExtendedStateIsolationTest(process_id)) {
        os::user::ExitProcess(OS_USER_SHELL_FAILURE_EXIT_CODE);
    }
    const int64_t shell_result = os::user::RunShell(argument_count, arguments, environment);
    if (!os::user::CompleteExtendedStateIsolationTest(process_id)) {
        os::user::ExitProcess(OS_USER_SHELL_FAILURE_EXIT_CODE);
    }
    os::user::ExitProcess(shell_result);
}

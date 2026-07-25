#include "os/user/shell.hpp"
#include "os/user/system_call.hpp"

extern "C" [[noreturn, gnu::section(".text.os_user_entry")]] void OsUserEntry() noexcept {
    os::user::ExitProcess(os::user::RunShell());
}

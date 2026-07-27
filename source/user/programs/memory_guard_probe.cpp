#include "os/user/system_call.hpp"

#include "os/abi/virtual_memory.hpp"

#include <stdint.h>

namespace {

constexpr char OS_USER_MEMORY_GUARD_PROBE_STARTED_MESSAGE[] =
    "[OS][USER][VM] GUARD_FAULT_ARMED\r\n";
constexpr uint64_t OS_USER_MEMORY_GUARD_PROBE_STRING_TERMINATOR_SIZE_BYTES = 1ULL;
constexpr uint8_t OS_USER_MEMORY_GUARD_PROBE_PATTERN = 0x47U;
constexpr int64_t OS_USER_MEMORY_GUARD_PROBE_FAILURE_EXIT_CODE = 1LL;
constexpr int64_t OS_USER_MEMORY_GUARD_PROBE_FIRST_ERROR_RESULT = -1LL;

}

extern "C" [[noreturn, gnu::section(".text.os_user_entry")]]
void OsUserEntry() noexcept {
    if (os::user::WriteLog(OS_USER_MEMORY_GUARD_PROBE_STARTED_MESSAGE,
                           sizeof(OS_USER_MEMORY_GUARD_PROBE_STARTED_MESSAGE) -
                               OS_USER_MEMORY_GUARD_PROBE_STRING_TERMINATOR_SIZE_BYTES) <=
        OS_USER_MEMORY_GUARD_PROBE_FIRST_ERROR_RESULT) {
        os::user::ExitProcess(OS_USER_MEMORY_GUARD_PROBE_FAILURE_EXIT_CODE);
    }
    volatile uint8_t *const guard_page =
        reinterpret_cast<volatile uint8_t *>(os::abi::OS_ABI_USER_STACK_GUARD_ADDRESS);
    *guard_page = OS_USER_MEMORY_GUARD_PROBE_PATTERN;
    os::user::ExitProcess(OS_USER_MEMORY_GUARD_PROBE_FAILURE_EXIT_CODE);
}

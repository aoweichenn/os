#include "os/user/system_call.hpp"

#include "os/abi/virtual_memory.hpp"

#include <stdint.h>

namespace {

constexpr char OS_USER_MEMORY_PROTECTION_PROBE_STARTED_MESSAGE[] =
    "[OS][USER][VM] PROTECTION_FAULT_ARMED\r\n";
constexpr uint64_t OS_USER_MEMORY_PROTECTION_PROBE_STRING_TERMINATOR_SIZE_BYTES = 1ULL;
constexpr uint8_t OS_USER_MEMORY_PROTECTION_PROBE_ZERO_VALUE = 0U;
constexpr uint8_t OS_USER_MEMORY_PROTECTION_PROBE_PATTERN = 0x57U;
constexpr int64_t OS_USER_MEMORY_PROTECTION_PROBE_FAILURE_EXIT_CODE = 1LL;
constexpr int64_t OS_USER_MEMORY_PROTECTION_PROBE_FIRST_ERROR_RESULT = -1LL;

}

extern "C" [[noreturn, gnu::section(".text.os_user_entry")]]
void OsUserEntry() noexcept {
    const int64_t mapping_result = os::user::MapAnonymousMemory(
        os::abi::OS_ABI_MEMORY_MAP_AUTOMATIC_ADDRESS, os::abi::OS_ABI_MEMORY_PAGE_SIZE_BYTES,
        os::abi::OS_ABI_MEMORY_PROTECTION_READ, os::abi::OS_ABI_MEMORY_MAP_NO_FLAGS);
    if (mapping_result <= OS_USER_MEMORY_PROTECTION_PROBE_FIRST_ERROR_RESULT ||
        os::user::WriteLog(OS_USER_MEMORY_PROTECTION_PROBE_STARTED_MESSAGE,
                           sizeof(OS_USER_MEMORY_PROTECTION_PROBE_STARTED_MESSAGE) -
                               OS_USER_MEMORY_PROTECTION_PROBE_STRING_TERMINATOR_SIZE_BYTES) <=
            OS_USER_MEMORY_PROTECTION_PROBE_FIRST_ERROR_RESULT) {
        os::user::ExitProcess(OS_USER_MEMORY_PROTECTION_PROBE_FAILURE_EXIT_CODE);
    }
    volatile uint8_t *const read_only_page =
        reinterpret_cast<volatile uint8_t *>(static_cast<uint64_t>(mapping_result));
    if (*read_only_page != OS_USER_MEMORY_PROTECTION_PROBE_ZERO_VALUE) {
        os::user::ExitProcess(OS_USER_MEMORY_PROTECTION_PROBE_FAILURE_EXIT_CODE);
    }
    *read_only_page = OS_USER_MEMORY_PROTECTION_PROBE_PATTERN;
    os::user::ExitProcess(OS_USER_MEMORY_PROTECTION_PROBE_FAILURE_EXIT_CODE);
}

#include "os/user/system_call.hpp"

#include "os/abi/system_call.hpp"

#include <stdint.h>

namespace {

constexpr char OS_USER_SMOKE_INVALID_POINTER_REJECTED_MESSAGE[] =
    "[OS][USER] INVALID_POINTER_REJECTED\r\n";
constexpr char OS_USER_SMOKE_UNKNOWN_SYSTEM_CALL_REJECTED_MESSAGE[] =
    "[OS][USER] UNKNOWN_SYSCALL_REJECTED\r\n";
constexpr char OS_USER_SMOKE_RING3_MESSAGE[] = "[OS][USER] HELLO_FROM_RING3\r\n";
constexpr char OS_USER_SMOKE_DUAL_ENTRY_EQUIVALENT_MESSAGE[] =
    "[OS][USER] DUAL_SYSCALL_ENTRY_EQUIVALENT\r\n";
constexpr char OS_USER_SMOKE_SYSTEM_RETURN_MESSAGE[] = "[OS][USER] SYSRET_RETURNED\r\n";
constexpr char OS_USER_SMOKE_INTERRUPT_FALLBACK_MESSAGE[] =
    "[OS][USER] SYSCALL_IRET_FALLBACK_RETURNED\r\n";
constexpr uint64_t OS_USER_SMOKE_UNMAPPED_POINTER = 0x000000003FFFFFFFULL;
constexpr uint64_t OS_USER_SMOKE_UNKNOWN_SYSTEM_CALL_NUMBER = 0xFFFFFFFFFFFFFFFFULL;
constexpr uint64_t OS_USER_SMOKE_POINTER_PROBE_SIZE_BYTES = 1ULL;
constexpr uint64_t OS_USER_SMOKE_STRING_TERMINATOR_SIZE_BYTES = 1ULL;
constexpr uint64_t OS_USER_SMOKE_UNUSED_SYSTEM_CALL_ARGUMENT = 0ULL;
constexpr int64_t OS_USER_SMOKE_SUCCESS_EXIT_CODE = 0LL;
constexpr int64_t OS_USER_SMOKE_FAILURE_EXIT_CODE = 1LL;
constexpr int64_t OS_USER_SMOKE_FIRST_ERROR_RESULT = -1LL;

template <uint64_t MessageSizeBytes>
[[nodiscard]] int64_t WriteMessage(const char (&message)[MessageSizeBytes]) noexcept {
    return os::user::WriteLog(message,
                              MessageSizeBytes - OS_USER_SMOKE_STRING_TERMINATOR_SIZE_BYTES);
}

[[nodiscard]] bool ValidateSystemCallEntries() noexcept {
    const uint64_t process_id = os::user::GetProcessId();
    const uint64_t get_process_id_number =
        static_cast<uint64_t>(os::abi::SystemCallNumber::GetProcessId);
    const int64_t legacy_process_id = os::user::InvokeLegacySystemCall(
        get_process_id_number, OS_USER_SMOKE_UNUSED_SYSTEM_CALL_ARGUMENT,
        OS_USER_SMOKE_UNUSED_SYSTEM_CALL_ARGUMENT, OS_USER_SMOKE_UNUSED_SYSTEM_CALL_ARGUMENT);
    const int64_t native_unknown_result = os::user::InvokeSystemCall(
        OS_USER_SMOKE_UNKNOWN_SYSTEM_CALL_NUMBER, OS_USER_SMOKE_UNUSED_SYSTEM_CALL_ARGUMENT,
        OS_USER_SMOKE_UNUSED_SYSTEM_CALL_ARGUMENT, OS_USER_SMOKE_UNUSED_SYSTEM_CALL_ARGUMENT);
    const int64_t legacy_unknown_result = os::user::InvokeLegacySystemCall(
        OS_USER_SMOKE_UNKNOWN_SYSTEM_CALL_NUMBER, OS_USER_SMOKE_UNUSED_SYSTEM_CALL_ARGUMENT,
        OS_USER_SMOKE_UNUSED_SYSTEM_CALL_ARGUMENT, OS_USER_SMOKE_UNUSED_SYSTEM_CALL_ARGUMENT);
    const int64_t fallback_process_id = os::user::InvokeSystemCallWithDirectionFlag(
        get_process_id_number, OS_USER_SMOKE_UNUSED_SYSTEM_CALL_ARGUMENT,
        OS_USER_SMOKE_UNUSED_SYSTEM_CALL_ARGUMENT, OS_USER_SMOKE_UNUSED_SYSTEM_CALL_ARGUMENT);
    return legacy_process_id == static_cast<int64_t>(process_id) &&
           fallback_process_id == static_cast<int64_t>(process_id) &&
           native_unknown_result == os::abi::OS_ABI_SYSTEM_CALL_RESULT_UNKNOWN_NUMBER &&
           legacy_unknown_result == native_unknown_result &&
           WriteMessage(OS_USER_SMOKE_DUAL_ENTRY_EQUIVALENT_MESSAGE) >
               OS_USER_SMOKE_FIRST_ERROR_RESULT &&
           WriteMessage(OS_USER_SMOKE_SYSTEM_RETURN_MESSAGE) > OS_USER_SMOKE_FIRST_ERROR_RESULT &&
           WriteMessage(OS_USER_SMOKE_INTERRUPT_FALLBACK_MESSAGE) >
               OS_USER_SMOKE_FIRST_ERROR_RESULT;
}

}

extern "C" [[noreturn, gnu::section(".text.os_user_entry")]] void OsUserEntry() noexcept {
    const int64_t invalid_pointer_result =
        os::user::WriteLog(reinterpret_cast<const char *>(OS_USER_SMOKE_UNMAPPED_POINTER),
                           OS_USER_SMOKE_POINTER_PROBE_SIZE_BYTES);
    if (invalid_pointer_result != os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_USER_MEMORY ||
        WriteMessage(OS_USER_SMOKE_INVALID_POINTER_REJECTED_MESSAGE) <=
            OS_USER_SMOKE_FIRST_ERROR_RESULT) {
        os::user::ExitProcess(OS_USER_SMOKE_FAILURE_EXIT_CODE);
    }

    if (!ValidateSystemCallEntries() ||
        WriteMessage(OS_USER_SMOKE_UNKNOWN_SYSTEM_CALL_REJECTED_MESSAGE) <=
            OS_USER_SMOKE_FIRST_ERROR_RESULT ||
        WriteMessage(OS_USER_SMOKE_RING3_MESSAGE) <= OS_USER_SMOKE_FIRST_ERROR_RESULT) {
        os::user::ExitProcess(OS_USER_SMOKE_FAILURE_EXIT_CODE);
    }
    os::user::ExitProcess(OS_USER_SMOKE_SUCCESS_EXIT_CODE);
}

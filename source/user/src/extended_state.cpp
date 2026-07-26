#include "os/user/extended_state.hpp"

#include "os/user/system_call.hpp"

namespace os::user {

namespace {

constexpr char OS_USER_EXTENDED_STATE_ISOLATED_MESSAGE[] =
    "[OS][USER] EXTENDED_STATE_ISOLATED\r\n";
constexpr uint64_t OS_USER_EXTENDED_STATE_STRING_TERMINATOR_SIZE_BYTES = 1ULL;
constexpr uint64_t OS_USER_EXTENDED_STATE_ASSEMBLY_SUCCESS = 1ULL;
constexpr int64_t OS_USER_EXTENDED_STATE_FIRST_ERROR_RESULT = -1LL;

extern "C" uint64_t
OsUserInstallExtendedStatePattern(uint64_t process_id) noexcept;
extern "C" uint64_t
OsUserValidateExtendedStatePattern(uint64_t process_id) noexcept;

}

bool InitializeExtendedStateIsolationTest(const uint64_t process_id) noexcept {
    return OsUserInstallExtendedStatePattern(process_id) ==
               OS_USER_EXTENDED_STATE_ASSEMBLY_SUCCESS &&
           OsUserValidateExtendedStatePattern(process_id) ==
               OS_USER_EXTENDED_STATE_ASSEMBLY_SUCCESS;
}

bool ValidateExtendedStateIsolationTest(const uint64_t process_id) noexcept {
    return OsUserValidateExtendedStatePattern(process_id) ==
           OS_USER_EXTENDED_STATE_ASSEMBLY_SUCCESS;
}

bool CompleteExtendedStateIsolationTest(const uint64_t process_id) noexcept {
    if (!ValidateExtendedStateIsolationTest(process_id)) {
        return false;
    }
    const int64_t write_result = WriteLog(
        OS_USER_EXTENDED_STATE_ISOLATED_MESSAGE,
        sizeof(OS_USER_EXTENDED_STATE_ISOLATED_MESSAGE) -
            OS_USER_EXTENDED_STATE_STRING_TERMINATOR_SIZE_BYTES);
    return write_result > OS_USER_EXTENDED_STATE_FIRST_ERROR_RESULT &&
           ValidateExtendedStateIsolationTest(process_id);
}

}

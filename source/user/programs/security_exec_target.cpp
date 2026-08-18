#include "os/user/system_call.hpp"

#include "os/abi/system_call.hpp"

#include <stdint.h>

namespace {

constexpr char OS_USER_SECURITY_EXEC_VERIFIED[] =
    "[OS][USER][SECURITY] FORK_EXEC_SETID_VERIFIED\r\n";
constexpr uint64_t OS_USER_SECURITY_EXEC_STRING_TERMINATOR_SIZE_BYTES = 1ULL;
constexpr uint64_t OS_USER_SECURITY_EXEC_EXPECTED_ARGUMENT_COUNT = 1ULL;
constexpr uint64_t OS_USER_SECURITY_EXEC_OPEN_FILE_LIMIT = 6ULL;
constexpr uint64_t OS_USER_SECURITY_EXEC_FILE_SIZE_LIMIT = 4ULL;
constexpr os::abi::UserIdentifier OS_USER_SECURITY_EXEC_REAL_USER_IDENTIFIER = 1000U;
constexpr os::abi::UserIdentifier OS_USER_SECURITY_EXEC_EFFECTIVE_USER_IDENTIFIER = 2000U;
constexpr os::abi::GroupIdentifier OS_USER_SECURITY_EXEC_REAL_GROUP_IDENTIFIER = 100U;
constexpr os::abi::GroupIdentifier OS_USER_SECURITY_EXEC_EFFECTIVE_GROUP_IDENTIFIER = 200U;
constexpr os::abi::GroupIdentifier OS_USER_SECURITY_EXEC_SUPPLEMENTARY_GROUP_IDENTIFIER = 300U;
constexpr os::abi::FileMode OS_USER_SECURITY_EXEC_CREATION_MASK = 0000027U;
constexpr int64_t OS_USER_SECURITY_EXEC_SUCCESS_RESULT = 0LL;
constexpr int64_t OS_USER_SECURITY_EXEC_SUCCESS_EXIT_CODE = 0LL;
constexpr int64_t OS_USER_SECURITY_EXEC_FAILURE_EXIT_CODE = 1LL;

}

extern "C" [[noreturn, gnu::section(".text.os_user_entry")]]
void OsUserEntry(const uint64_t argument_count, const char *const *const arguments) noexcept {
    static_cast<void>(arguments);
    os::abi::CredentialInformation information{};
    os::abi::GroupIdentifier groups[1]{};
    os::abi::ResourceLimit open_file_limit{};
    os::abi::ResourceLimit file_size_limit{};
    const bool valid =
        argument_count == OS_USER_SECURITY_EXEC_EXPECTED_ARGUMENT_COUNT &&
        os::user::GetCredentials(information) == OS_USER_SECURITY_EXEC_SUCCESS_RESULT &&
        information.real_user_identifier == OS_USER_SECURITY_EXEC_REAL_USER_IDENTIFIER &&
        information.effective_user_identifier == OS_USER_SECURITY_EXEC_EFFECTIVE_USER_IDENTIFIER &&
        information.saved_user_identifier == OS_USER_SECURITY_EXEC_EFFECTIVE_USER_IDENTIFIER &&
        information.real_group_identifier == OS_USER_SECURITY_EXEC_REAL_GROUP_IDENTIFIER &&
        information.effective_group_identifier ==
            OS_USER_SECURITY_EXEC_EFFECTIVE_GROUP_IDENTIFIER &&
        information.saved_group_identifier == OS_USER_SECURITY_EXEC_EFFECTIVE_GROUP_IDENTIFIER &&
        information.creation_mask == OS_USER_SECURITY_EXEC_CREATION_MASK &&
        os::user::GetSupplementaryGroups(groups, 1ULL) == 1LL &&
        groups[0ULL] == OS_USER_SECURITY_EXEC_SUPPLEMENTARY_GROUP_IDENTIFIER &&
        os::user::GetResourceLimit(os::abi::ResourceLimitKind::OpenFileCount, open_file_limit) ==
            OS_USER_SECURITY_EXEC_SUCCESS_RESULT &&
        open_file_limit.current == OS_USER_SECURITY_EXEC_OPEN_FILE_LIMIT &&
        os::user::GetResourceLimit(os::abi::ResourceLimitKind::FileSize, file_size_limit) ==
            OS_USER_SECURITY_EXEC_SUCCESS_RESULT &&
        file_size_limit.current == OS_USER_SECURITY_EXEC_FILE_SIZE_LIMIT &&
        os::user::WriteLog(OS_USER_SECURITY_EXEC_VERIFIED,
                           sizeof(OS_USER_SECURITY_EXEC_VERIFIED) -
                               OS_USER_SECURITY_EXEC_STRING_TERMINATOR_SIZE_BYTES) >=
            OS_USER_SECURITY_EXEC_SUCCESS_RESULT;
    os::user::ExitProcess(valid ? OS_USER_SECURITY_EXEC_SUCCESS_EXIT_CODE
                                : OS_USER_SECURITY_EXEC_FAILURE_EXIT_CODE);
}

#include "os/user/system_call.hpp"

#include "os/abi/system_call.hpp"

#include <stdint.h>

namespace {

constexpr char OS_USER_SECURITY_PROBE_DIRECTORY[] = "/security-v2";
constexpr char OS_USER_SECURITY_PROBE_FILE[] = "/security-v2/file";
constexpr char OS_USER_SECURITY_PROBE_HARD_LINK[] = "/security-v2/hard";
constexpr char OS_USER_SECURITY_PROBE_SYMBOLIC_LINK[] = "/security-v2/symbolic";
constexpr char OS_USER_SECURITY_PROBE_EXECUTABLE[] = "/bin/security_exec_target";
constexpr char OS_USER_SECURITY_PROBE_VERIFIED[] =
    "[OS][USER][SECURITY] CREDENTIALS_PERMISSIONS_RLIMIT_VERIFIED\r\n";
constexpr uint64_t OS_USER_SECURITY_PROBE_STRING_TERMINATOR_SIZE_BYTES = 1ULL;
constexpr uint64_t OS_USER_SECURITY_PROBE_EXPECTED_ARGUMENT_COUNT = 1ULL;
constexpr uint64_t OS_USER_SECURITY_PROBE_FIRST_INDEX = 0ULL;
constexpr uint64_t OS_USER_SECURITY_PROBE_OPEN_FILE_LIMIT = 6ULL;
constexpr uint64_t OS_USER_SECURITY_PROBE_FILE_SIZE_LIMIT = 4ULL;
constexpr os::abi::UserIdentifier OS_USER_SECURITY_PROBE_USER_IDENTIFIER = 1000U;
constexpr os::abi::UserIdentifier OS_USER_SECURITY_PROBE_EXEC_USER_IDENTIFIER = 2000U;
constexpr os::abi::GroupIdentifier OS_USER_SECURITY_PROBE_GROUP_IDENTIFIER = 100U;
constexpr os::abi::GroupIdentifier OS_USER_SECURITY_PROBE_EXEC_GROUP_IDENTIFIER = 200U;
constexpr os::abi::GroupIdentifier OS_USER_SECURITY_PROBE_SUPPLEMENTARY_GROUP_IDENTIFIER = 300U;
constexpr os::abi::FileMode OS_USER_SECURITY_PROBE_CREATION_MASK = 0000027U;
constexpr os::abi::FileMode OS_USER_SECURITY_PROBE_EXECUTABLE_MODE = 0006755U;
constexpr uint8_t OS_USER_SECURITY_PROBE_PAYLOAD[]{'1', '2', '3', '4', '5', '6', '7', '8'};
constexpr int64_t OS_USER_SECURITY_PROBE_SUCCESS_RESULT = 0LL;
constexpr int64_t OS_USER_SECURITY_PROBE_SUCCESS_EXIT_CODE = 0LL;
constexpr int64_t OS_USER_SECURITY_PROBE_FAILURE_EXIT_CODE = 1LL;

template <uint64_t SizeBytes>
[[nodiscard]] constexpr uint64_t TextLength(const char (&)[SizeBytes]) noexcept {
    return SizeBytes - OS_USER_SECURITY_PROBE_STRING_TERMINATOR_SIZE_BYTES;
}

[[nodiscard]] bool
CredentialsMatch(const os::abi::UserIdentifier real_user_identifier,
                 const os::abi::UserIdentifier effective_user_identifier,
                 const os::abi::GroupIdentifier real_group_identifier,
                 const os::abi::GroupIdentifier effective_group_identifier) noexcept {
    os::abi::CredentialInformation information{};
    os::abi::GroupIdentifier groups[1]{};
    return os::user::GetCredentials(information) == OS_USER_SECURITY_PROBE_SUCCESS_RESULT &&
           information.real_user_identifier == real_user_identifier &&
           information.effective_user_identifier == effective_user_identifier &&
           information.real_group_identifier == real_group_identifier &&
           information.effective_group_identifier == effective_group_identifier &&
           information.creation_mask == OS_USER_SECURITY_PROBE_CREATION_MASK &&
           os::user::GetSupplementaryGroups(groups, 1ULL) == 1LL &&
           groups[OS_USER_SECURITY_PROBE_FIRST_INDEX] ==
               OS_USER_SECURITY_PROBE_SUPPLEMENTARY_GROUP_IDENTIFIER;
}

[[nodiscard]] bool LimitsMatch() noexcept {
    os::abi::ResourceLimit open_file_limit{};
    os::abi::ResourceLimit file_size_limit{};
    return os::user::GetResourceLimit(os::abi::ResourceLimitKind::OpenFileCount, open_file_limit) ==
               OS_USER_SECURITY_PROBE_SUCCESS_RESULT &&
           open_file_limit.current == OS_USER_SECURITY_PROBE_OPEN_FILE_LIMIT &&
           os::user::GetResourceLimit(os::abi::ResourceLimitKind::FileSize, file_size_limit) ==
               OS_USER_SECURITY_PROBE_SUCCESS_RESULT &&
           file_size_limit.current == OS_USER_SECURITY_PROBE_FILE_SIZE_LIMIT;
}

[[nodiscard]] bool CloseIfOpen(const int64_t descriptor) noexcept {
    return descriptor >= OS_USER_SECURITY_PROBE_SUCCESS_RESULT &&
           os::user::CloseDescriptor(static_cast<uint64_t>(descriptor)) ==
               OS_USER_SECURITY_PROBE_SUCCESS_RESULT;
}

[[noreturn]] void Fail() noexcept {
    os::user::ExitProcess(OS_USER_SECURITY_PROBE_FAILURE_EXIT_CODE);
}

template <uint64_t SizeBytes>
[[nodiscard]] bool WriteMessage(const char (&message)[SizeBytes]) noexcept {
    return os::user::WriteLog(message, TextLength(message)) >=
           OS_USER_SECURITY_PROBE_SUCCESS_RESULT;
}

}

extern "C" [[noreturn, gnu::section(".text.os_user_entry")]]
void OsUserEntry(const uint64_t argument_count, const char *const *const arguments) noexcept {
    static_cast<void>(arguments);
    os::abi::CredentialInformation initial_credentials{};
    os::abi::ResourceLimit open_file_limit{};
    os::abi::ResourceLimit file_size_limit{};
    if (argument_count != OS_USER_SECURITY_PROBE_EXPECTED_ARGUMENT_COUNT ||
        os::user::GetCredentials(initial_credentials) != OS_USER_SECURITY_PROBE_SUCCESS_RESULT ||
        initial_credentials.effective_user_identifier != os::abi::OS_ABI_ROOT_USER_IDENTIFIER ||
        initial_credentials.effective_group_identifier != os::abi::OS_ABI_ROOT_GROUP_IDENTIFIER ||
        initial_credentials.creation_mask != os::abi::OS_ABI_DEFAULT_CREATION_MASK ||
        os::user::SetCreationMask(OS_USER_SECURITY_PROBE_CREATION_MASK) !=
            static_cast<int64_t>(os::abi::OS_ABI_DEFAULT_CREATION_MASK) ||
        os::user::CreateDirectory(OS_USER_SECURITY_PROBE_DIRECTORY,
                                  TextLength(OS_USER_SECURITY_PROBE_DIRECTORY)) !=
            OS_USER_SECURITY_PROBE_SUCCESS_RESULT) {
        Fail();
    }

    const uint64_t create_flags = os::abi::OS_ABI_FILE_OPEN_WRITE_FLAG |
                                  os::abi::OS_ABI_FILE_OPEN_CREATE_FLAG |
                                  os::abi::OS_ABI_FILE_OPEN_TRUNCATE_FLAG;
    const int64_t file_descriptor = os::user::OpenFile(
        OS_USER_SECURITY_PROBE_FILE, TextLength(OS_USER_SECURITY_PROBE_FILE), create_flags);
    if (!CloseIfOpen(file_descriptor) ||
        os::user::LinkFile(OS_USER_SECURITY_PROBE_FILE, TextLength(OS_USER_SECURITY_PROBE_FILE),
                           OS_USER_SECURITY_PROBE_HARD_LINK,
                           TextLength(OS_USER_SECURITY_PROBE_HARD_LINK)) !=
            OS_USER_SECURITY_PROBE_SUCCESS_RESULT ||
        os::user::CreateSymbolicLink(OS_USER_SECURITY_PROBE_FILE,
                                     TextLength(OS_USER_SECURITY_PROBE_FILE),
                                     OS_USER_SECURITY_PROBE_SYMBOLIC_LINK,
                                     TextLength(OS_USER_SECURITY_PROBE_SYMBOLIC_LINK)) !=
            OS_USER_SECURITY_PROBE_SUCCESS_RESULT) {
        Fail();
    }
    char link_target[sizeof(OS_USER_SECURITY_PROBE_FILE)]{};
    const int64_t link_length = os::user::ReadSymbolicLink(
        OS_USER_SECURITY_PROBE_SYMBOLIC_LINK, TextLength(OS_USER_SECURITY_PROBE_SYMBOLIC_LINK),
        link_target, sizeof(link_target));
    bool link_matches =
        link_length == static_cast<int64_t>(TextLength(OS_USER_SECURITY_PROBE_FILE));
    for (uint64_t byte_index = OS_USER_SECURITY_PROBE_FIRST_INDEX;
         link_matches && byte_index < TextLength(OS_USER_SECURITY_PROBE_FILE); ++byte_index) {
        link_matches = link_target[byte_index] == OS_USER_SECURITY_PROBE_FILE[byte_index];
    }
    os::abi::FileInformation information{};
    if (!link_matches ||
        os::user::StatFile(OS_USER_SECURITY_PROBE_FILE, TextLength(OS_USER_SECURITY_PROBE_FILE),
                           information) != OS_USER_SECURITY_PROBE_SUCCESS_RESULT ||
        information.link_count != 2ULL ||
        information.mode != (os::abi::OS_ABI_FILE_MODE_REGULAR | 0000640U) ||
        os::user::ChangeOwner(
            OS_USER_SECURITY_PROBE_DIRECTORY, TextLength(OS_USER_SECURITY_PROBE_DIRECTORY),
            OS_USER_SECURITY_PROBE_USER_IDENTIFIER,
            OS_USER_SECURITY_PROBE_GROUP_IDENTIFIER) != OS_USER_SECURITY_PROBE_SUCCESS_RESULT ||
        os::user::ChangeOwner(OS_USER_SECURITY_PROBE_FILE, TextLength(OS_USER_SECURITY_PROBE_FILE),
                              OS_USER_SECURITY_PROBE_USER_IDENTIFIER,
                              OS_USER_SECURITY_PROBE_GROUP_IDENTIFIER) !=
            OS_USER_SECURITY_PROBE_SUCCESS_RESULT ||
        os::user::ChangeOwner(OS_USER_SECURITY_PROBE_EXECUTABLE,
                              TextLength(OS_USER_SECURITY_PROBE_EXECUTABLE),
                              OS_USER_SECURITY_PROBE_EXEC_USER_IDENTIFIER,
                              OS_USER_SECURITY_PROBE_EXEC_GROUP_IDENTIFIER) !=
            OS_USER_SECURITY_PROBE_SUCCESS_RESULT ||
        os::user::ChangeMode(
            OS_USER_SECURITY_PROBE_EXECUTABLE, TextLength(OS_USER_SECURITY_PROBE_EXECUTABLE),
            OS_USER_SECURITY_PROBE_EXECUTABLE_MODE) != OS_USER_SECURITY_PROBE_SUCCESS_RESULT) {
        Fail();
    }

    const os::abi::GroupIdentifier supplementary_groups[]{
        OS_USER_SECURITY_PROBE_SUPPLEMENTARY_GROUP_IDENTIFIER,
    };
    const os::abi::IdentifierChangeRequest group_request{
        .real_identifier = OS_USER_SECURITY_PROBE_GROUP_IDENTIFIER,
        .effective_identifier = OS_USER_SECURITY_PROBE_GROUP_IDENTIFIER,
        .saved_identifier = OS_USER_SECURITY_PROBE_GROUP_IDENTIFIER,
        .reserved = 0U,
    };
    const os::abi::IdentifierChangeRequest user_request{
        .real_identifier = OS_USER_SECURITY_PROBE_USER_IDENTIFIER,
        .effective_identifier = OS_USER_SECURITY_PROBE_USER_IDENTIFIER,
        .saved_identifier = OS_USER_SECURITY_PROBE_USER_IDENTIFIER,
        .reserved = 0U,
    };
    if (os::user::GetResourceLimit(os::abi::ResourceLimitKind::OpenFileCount, open_file_limit) !=
            OS_USER_SECURITY_PROBE_SUCCESS_RESULT ||
        os::user::GetResourceLimit(os::abi::ResourceLimitKind::FileSize, file_size_limit) !=
            OS_USER_SECURITY_PROBE_SUCCESS_RESULT) {
        Fail();
    }
    open_file_limit.current = OS_USER_SECURITY_PROBE_OPEN_FILE_LIMIT;
    file_size_limit.current = OS_USER_SECURITY_PROBE_FILE_SIZE_LIMIT;
    if (os::user::SetResourceLimit(os::abi::ResourceLimitKind::OpenFileCount, open_file_limit) !=
            OS_USER_SECURITY_PROBE_SUCCESS_RESULT ||
        os::user::SetResourceLimit(os::abi::ResourceLimitKind::FileSize, file_size_limit) !=
            OS_USER_SECURITY_PROBE_SUCCESS_RESULT ||
        os::user::SetSupplementaryGroups(supplementary_groups, 1ULL) !=
            OS_USER_SECURITY_PROBE_SUCCESS_RESULT ||
        !LimitsMatch()) {
        Fail();
    }

    const int64_t fork_result = os::user::ForkProcess();
    if (fork_result == OS_USER_SECURITY_PROBE_SUCCESS_RESULT) {
        if (os::user::SetGroupIdentifiers(group_request) != OS_USER_SECURITY_PROBE_SUCCESS_RESULT ||
            os::user::SetUserIdentifiers(user_request) != OS_USER_SECURITY_PROBE_SUCCESS_RESULT ||
            !CredentialsMatch(
                OS_USER_SECURITY_PROBE_USER_IDENTIFIER, OS_USER_SECURITY_PROBE_USER_IDENTIFIER,
                OS_USER_SECURITY_PROBE_GROUP_IDENTIFIER, OS_USER_SECURITY_PROBE_GROUP_IDENTIFIER) ||
            !LimitsMatch()) {
            Fail();
        }
        const int64_t write_descriptor = os::user::OpenFile(
            OS_USER_SECURITY_PROBE_FILE, TextLength(OS_USER_SECURITY_PROBE_FILE), create_flags);
        if (write_descriptor < OS_USER_SECURITY_PROBE_SUCCESS_RESULT ||
            os::user::TryWriteDescriptor(static_cast<uint64_t>(write_descriptor),
                                         OS_USER_SECURITY_PROBE_PAYLOAD,
                                         sizeof(OS_USER_SECURITY_PROBE_PAYLOAD)) !=
                static_cast<int64_t>(OS_USER_SECURITY_PROBE_FILE_SIZE_LIMIT) ||
            os::user::TryWriteDescriptor(static_cast<uint64_t>(write_descriptor),
                                         OS_USER_SECURITY_PROBE_PAYLOAD, 1ULL) !=
                os::abi::OS_ABI_SYSTEM_CALL_RESULT_FILE_TOO_LARGE ||
            !CloseIfOpen(write_descriptor) ||
            os::user::ChangeOwner(OS_USER_SECURITY_PROBE_FILE,
                                  TextLength(OS_USER_SECURITY_PROBE_FILE), 1001U,
                                  os::abi::OS_ABI_GROUP_IDENTIFIER_UNCHANGED) !=
                os::abi::OS_ABI_SYSTEM_CALL_RESULT_FILE_PERMISSION_DENIED) {
            Fail();
        }
        os::user::ExitProcess(OS_USER_SECURITY_PROBE_SUCCESS_EXIT_CODE);
    }
    os::abi::ProcessWaitResult wait_result{};
    if (fork_result < OS_USER_SECURITY_PROBE_SUCCESS_RESULT ||
        os::user::WaitProcess(static_cast<uint64_t>(fork_result), wait_result) != fork_result ||
        wait_result.exit_code != OS_USER_SECURITY_PROBE_SUCCESS_EXIT_CODE) {
        Fail();
    }
    if (os::user::UnlinkFile(OS_USER_SECURITY_PROBE_HARD_LINK,
                             TextLength(OS_USER_SECURITY_PROBE_HARD_LINK)) !=
            OS_USER_SECURITY_PROBE_SUCCESS_RESULT ||
        os::user::UnlinkFile(OS_USER_SECURITY_PROBE_SYMBOLIC_LINK,
                             TextLength(OS_USER_SECURITY_PROBE_SYMBOLIC_LINK)) !=
            OS_USER_SECURITY_PROBE_SUCCESS_RESULT ||
        os::user::UnlinkFile(OS_USER_SECURITY_PROBE_FILE,
                             TextLength(OS_USER_SECURITY_PROBE_FILE)) !=
            OS_USER_SECURITY_PROBE_SUCCESS_RESULT ||
        os::user::RemoveDirectory(OS_USER_SECURITY_PROBE_DIRECTORY,
                                  TextLength(OS_USER_SECURITY_PROBE_DIRECTORY)) !=
            OS_USER_SECURITY_PROBE_SUCCESS_RESULT) {
        Fail();
    }
    if (!WriteMessage(OS_USER_SECURITY_PROBE_VERIFIED)) {
        Fail();
    }
    if (os::user::SetGroupIdentifiers(group_request) != OS_USER_SECURITY_PROBE_SUCCESS_RESULT ||
        os::user::SetUserIdentifiers(user_request) != OS_USER_SECURITY_PROBE_SUCCESS_RESULT ||
        !CredentialsMatch(
            OS_USER_SECURITY_PROBE_USER_IDENTIFIER, OS_USER_SECURITY_PROBE_USER_IDENTIFIER,
            OS_USER_SECURITY_PROBE_GROUP_IDENTIFIER, OS_USER_SECURITY_PROBE_GROUP_IDENTIFIER)) {
        Fail();
    }

    const os::abi::ProcessString exec_arguments[]{
        os::abi::ProcessString{
            .address = reinterpret_cast<uint64_t>(OS_USER_SECURITY_PROBE_EXECUTABLE),
            .length_bytes = TextLength(OS_USER_SECURITY_PROBE_EXECUTABLE),
        },
    };
    const os::abi::ProcessLaunchRequest request{
        .path_address = reinterpret_cast<uint64_t>(OS_USER_SECURITY_PROBE_EXECUTABLE),
        .path_length_bytes = TextLength(OS_USER_SECURITY_PROBE_EXECUTABLE),
        .argument_vector_address = reinterpret_cast<uint64_t>(exec_arguments),
        .argument_count = 1ULL,
        .environment_vector_address = 0ULL,
        .environment_count = 0ULL,
    };
    static_cast<void>(os::user::ExecProcess(request));
    Fail();
}

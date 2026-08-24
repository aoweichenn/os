#include <os/user/system_call.hpp>

#include <os/abi/system_call.hpp>

#include <stdint.h>

namespace {

constexpr char OS_USER_FS_PROBE_FILE_WRITTEN_MESSAGE[] = "[OS][USER][FS] FILE_WRITTEN\r\n";
constexpr char OS_USER_FS_PROBE_FILE_VERIFIED_MESSAGE[] = "[OS][USER][FS] FILE_VERIFIED\r\n";
constexpr char OS_USER_FS_PROBE_DIRECTORY_FAILURE_MESSAGE[] =
    "[OS][USER][FS][FAIL] DIRECTORY_AT\r\n";
constexpr char OS_USER_FS_PROBE_OPEN_FAILURE_MESSAGE[] = "[OS][USER][FS][FAIL] OPEN_AT\r\n";
constexpr char OS_USER_FS_PROBE_WRITE_FAILURE_MESSAGE[] = "[OS][USER][FS][FAIL] WRITE_AT\r\n";
constexpr char OS_USER_FS_PROBE_OPERATION_FAILURE_MESSAGE[] = "[OS][USER][FS][FAIL] PATH_AT\r\n";
constexpr char OS_USER_FS_PROBE_READ_FAILURE_MESSAGE[] = "[OS][USER][FS][FAIL] READ_AT\r\n";
constexpr char OS_USER_FS_PROBE_DIRECTORY_PATH[] = "/shared";
constexpr char OS_USER_FS_PROBE_FILE_NAME[] = "payload.bin";
constexpr char OS_USER_FS_PROBE_RENAMED_NAME[] = "renamed.bin";
constexpr char OS_USER_FS_PROBE_ALIAS_NAME[] = "alias.bin";
constexpr char OS_USER_FS_PROBE_SYMBOLIC_NAME[] = "payload.link";
constexpr char OS_USER_FS_PROBE_SUBDIRECTORY_NAME[] = "at-subdirectory";
constexpr char OS_USER_FS_PROBE_CURRENT_DIRECTORY_PATH[] = ".";
constexpr uint64_t OS_USER_FS_PROBE_PAYLOAD_SIZE_BYTES = 256ULL;
constexpr uint64_t OS_USER_FS_PROBE_STRING_TERMINATOR_SIZE_BYTES = 1ULL;
constexpr uint64_t OS_USER_FS_PROBE_BYTE_MULTIPLIER = 37ULL;
constexpr uint64_t OS_USER_FS_PROBE_BYTE_INCREMENT = 11ULL;
constexpr uint64_t OS_USER_FS_PROBE_BYTE_MASK = 0xFFULL;
constexpr uint64_t OS_USER_FS_PROBE_FIRST_INDEX = 0ULL;
constexpr uint64_t OS_USER_FS_PROBE_EMPTY_VALUE = 0ULL;
constexpr uint8_t OS_USER_FS_PROBE_EMPTY_BYTE = 0U;
constexpr uint64_t OS_USER_FS_PROBE_WRITE_FLAGS = os::abi::OS_ABI_FILE_OPEN_WRITE_FLAG |
                                                  os::abi::OS_ABI_FILE_OPEN_CREATE_FLAG |
                                                  os::abi::OS_ABI_FILE_OPEN_TRUNCATE_FLAG;
constexpr int64_t OS_USER_FS_PROBE_SUCCESS_RESULT = 0LL;
constexpr int64_t OS_USER_FS_PROBE_FAILURE_EXIT_CODE = 1LL;
constexpr int64_t OS_USER_FS_PROBE_FIRST_ERROR_RESULT = -1LL;

uint8_t payload[OS_USER_FS_PROBE_PAYLOAD_SIZE_BYTES];
uint8_t read_buffer[OS_USER_FS_PROBE_PAYLOAD_SIZE_BYTES];

[[nodiscard]] uint8_t ExpectedByte(const uint64_t byte_index) noexcept {
    return static_cast<uint8_t>(
        (byte_index * OS_USER_FS_PROBE_BYTE_MULTIPLIER + OS_USER_FS_PROBE_BYTE_INCREMENT) &
        OS_USER_FS_PROBE_BYTE_MASK);
}

[[nodiscard]] bool BytesEqual(const char *const left, const char *const right,
                              const uint64_t length_bytes) noexcept {
    if (left == nullptr || right == nullptr) {
        return false;
    }
    for (uint64_t byte_index = OS_USER_FS_PROBE_FIRST_INDEX; byte_index < length_bytes;
         ++byte_index) {
        if (left[byte_index] != right[byte_index]) {
            return false;
        }
    }
    return true;
}

template <uint64_t MessageSizeBytes>
[[nodiscard]] bool WriteMessage(const char (&message)[MessageSizeBytes]) noexcept {
    return os::user::WriteLog(message,
                              MessageSizeBytes - OS_USER_FS_PROBE_STRING_TERMINATOR_SIZE_BYTES) >
           OS_USER_FS_PROBE_FIRST_ERROR_RESULT;
}

template <uint64_t MessageSizeBytes>
[[noreturn]] void ExitFailure(const char (&message)[MessageSizeBytes]) noexcept {
    static_cast<void>(WriteMessage(message));
    os::user::ExitProcess(OS_USER_FS_PROBE_FAILURE_EXIT_CODE);
}

}

extern "C" [[noreturn, gnu::section(".text.os_user_entry")]]
void OsUserEntry() noexcept {
    for (uint64_t byte_index = OS_USER_FS_PROBE_FIRST_INDEX;
         byte_index < OS_USER_FS_PROBE_PAYLOAD_SIZE_BYTES; ++byte_index) {
        payload[byte_index] = ExpectedByte(byte_index);
    }
    const int64_t create_status = os::user::CreateDirectory(
        OS_USER_FS_PROBE_DIRECTORY_PATH,
        sizeof(OS_USER_FS_PROBE_DIRECTORY_PATH) - OS_USER_FS_PROBE_STRING_TERMINATOR_SIZE_BYTES);
    if (create_status != OS_USER_FS_PROBE_SUCCESS_RESULT &&
        create_status != os::abi::OS_ABI_SYSTEM_CALL_RESULT_FILE_ALREADY_EXISTS) {
        os::user::ExitProcess(OS_USER_FS_PROBE_FAILURE_EXIT_CODE);
    }
    const int64_t directory_descriptor = os::user::OpenDirectory(
        OS_USER_FS_PROBE_DIRECTORY_PATH,
        sizeof(OS_USER_FS_PROBE_DIRECTORY_PATH) - OS_USER_FS_PROBE_STRING_TERMINATOR_SIZE_BYTES);
    if (directory_descriptor < OS_USER_FS_PROBE_SUCCESS_RESULT) {
        ExitFailure(OS_USER_FS_PROBE_DIRECTORY_FAILURE_MESSAGE);
    }
    const int64_t nested_directory_descriptor = os::user::OpenDirectoryAt(
        static_cast<uint64_t>(directory_descriptor), OS_USER_FS_PROBE_CURRENT_DIRECTORY_PATH,
        sizeof(OS_USER_FS_PROBE_CURRENT_DIRECTORY_PATH) -
            OS_USER_FS_PROBE_STRING_TERMINATOR_SIZE_BYTES);
    if (nested_directory_descriptor < OS_USER_FS_PROBE_SUCCESS_RESULT ||
        os::user::CloseDescriptor(static_cast<uint64_t>(nested_directory_descriptor)) !=
            OS_USER_FS_PROBE_SUCCESS_RESULT) {
        ExitFailure(OS_USER_FS_PROBE_DIRECTORY_FAILURE_MESSAGE);
    }
    const int64_t write_descriptor = os::user::OpenFileAt(
        static_cast<uint64_t>(directory_descriptor), OS_USER_FS_PROBE_FILE_NAME,
        sizeof(OS_USER_FS_PROBE_FILE_NAME) - OS_USER_FS_PROBE_STRING_TERMINATOR_SIZE_BYTES,
        OS_USER_FS_PROBE_WRITE_FLAGS);
    if (write_descriptor < OS_USER_FS_PROBE_SUCCESS_RESULT ||
        os::user::WriteDescriptor(static_cast<uint64_t>(write_descriptor), payload,
                                  sizeof(payload)) != static_cast<int64_t>(sizeof(payload))) {
        ExitFailure(OS_USER_FS_PROBE_OPEN_FAILURE_MESSAGE);
    }
    if (os::user::CloseDescriptor(static_cast<uint64_t>(write_descriptor)) !=
            OS_USER_FS_PROBE_SUCCESS_RESULT ||
        os::user::SyncFileSystem() != OS_USER_FS_PROBE_SUCCESS_RESULT ||
        !WriteMessage(OS_USER_FS_PROBE_FILE_WRITTEN_MESSAGE)) {
        ExitFailure(OS_USER_FS_PROBE_WRITE_FAILURE_MESSAGE);
    }

    os::abi::FileInformation at_information{};
    char symbolic_target[sizeof(OS_USER_FS_PROBE_FILE_NAME)]{};
    const bool at_operations_succeeded =
        os::user::StatAt(
            static_cast<uint64_t>(directory_descriptor), OS_USER_FS_PROBE_FILE_NAME,
            sizeof(OS_USER_FS_PROBE_FILE_NAME) - OS_USER_FS_PROBE_STRING_TERMINATOR_SIZE_BYTES,
            OS_USER_FS_PROBE_EMPTY_BYTE, at_information) == OS_USER_FS_PROBE_SUCCESS_RESULT &&
        at_information.type == os::abi::DirectoryEntryType::RegularFile &&
        os::user::LinkAt(
            static_cast<uint64_t>(directory_descriptor), OS_USER_FS_PROBE_FILE_NAME,
            sizeof(OS_USER_FS_PROBE_FILE_NAME) - OS_USER_FS_PROBE_STRING_TERMINATOR_SIZE_BYTES,
            static_cast<uint64_t>(directory_descriptor), OS_USER_FS_PROBE_ALIAS_NAME,
            sizeof(OS_USER_FS_PROBE_ALIAS_NAME) - OS_USER_FS_PROBE_STRING_TERMINATOR_SIZE_BYTES) ==
            OS_USER_FS_PROBE_SUCCESS_RESULT &&
        os::user::CreateSymbolicLinkAt(
            OS_USER_FS_PROBE_FILE_NAME,
            sizeof(OS_USER_FS_PROBE_FILE_NAME) - OS_USER_FS_PROBE_STRING_TERMINATOR_SIZE_BYTES,
            static_cast<uint64_t>(directory_descriptor), OS_USER_FS_PROBE_SYMBOLIC_NAME,
            sizeof(OS_USER_FS_PROBE_SYMBOLIC_NAME) -
                OS_USER_FS_PROBE_STRING_TERMINATOR_SIZE_BYTES) == OS_USER_FS_PROBE_SUCCESS_RESULT &&
        os::user::ReadSymbolicLinkAt(
            static_cast<uint64_t>(directory_descriptor), OS_USER_FS_PROBE_SYMBOLIC_NAME,
            sizeof(OS_USER_FS_PROBE_SYMBOLIC_NAME) - OS_USER_FS_PROBE_STRING_TERMINATOR_SIZE_BYTES,
            symbolic_target, sizeof(symbolic_target)) ==
            static_cast<int64_t>(sizeof(OS_USER_FS_PROBE_FILE_NAME) -
                                 OS_USER_FS_PROBE_STRING_TERMINATOR_SIZE_BYTES) &&
        os::user::StatAt(
            static_cast<uint64_t>(directory_descriptor), OS_USER_FS_PROBE_SYMBOLIC_NAME,
            sizeof(OS_USER_FS_PROBE_SYMBOLIC_NAME) - OS_USER_FS_PROBE_STRING_TERMINATOR_SIZE_BYTES,
            os::abi::OS_ABI_AT_STAT_NO_FOLLOW_FLAG,
            at_information) == OS_USER_FS_PROBE_SUCCESS_RESULT &&
        at_information.type == os::abi::DirectoryEntryType::SymbolicLink &&
        os::user::RenameAt(
            static_cast<uint64_t>(directory_descriptor), OS_USER_FS_PROBE_FILE_NAME,
            sizeof(OS_USER_FS_PROBE_FILE_NAME) - OS_USER_FS_PROBE_STRING_TERMINATOR_SIZE_BYTES,
            static_cast<uint64_t>(directory_descriptor), OS_USER_FS_PROBE_RENAMED_NAME,
            sizeof(OS_USER_FS_PROBE_RENAMED_NAME) -
                OS_USER_FS_PROBE_STRING_TERMINATOR_SIZE_BYTES) == OS_USER_FS_PROBE_SUCCESS_RESULT &&
        os::user::RenameAt(
            static_cast<uint64_t>(directory_descriptor), OS_USER_FS_PROBE_RENAMED_NAME,
            sizeof(OS_USER_FS_PROBE_RENAMED_NAME) - OS_USER_FS_PROBE_STRING_TERMINATOR_SIZE_BYTES,
            static_cast<uint64_t>(directory_descriptor), OS_USER_FS_PROBE_FILE_NAME,
            sizeof(OS_USER_FS_PROBE_FILE_NAME) - OS_USER_FS_PROBE_STRING_TERMINATOR_SIZE_BYTES) ==
            OS_USER_FS_PROBE_SUCCESS_RESULT &&
        os::user::CreateDirectoryAt(
            static_cast<uint64_t>(directory_descriptor), OS_USER_FS_PROBE_SUBDIRECTORY_NAME,
            sizeof(OS_USER_FS_PROBE_SUBDIRECTORY_NAME) -
                OS_USER_FS_PROBE_STRING_TERMINATOR_SIZE_BYTES) == OS_USER_FS_PROBE_SUCCESS_RESULT &&
        os::user::RemoveAt(
            static_cast<uint64_t>(directory_descriptor), OS_USER_FS_PROBE_SUBDIRECTORY_NAME,
            sizeof(OS_USER_FS_PROBE_SUBDIRECTORY_NAME) -
                OS_USER_FS_PROBE_STRING_TERMINATOR_SIZE_BYTES,
            os::abi::OS_ABI_AT_REMOVE_DIRECTORY_FLAG) == OS_USER_FS_PROBE_SUCCESS_RESULT &&
        os::user::RemoveAt(static_cast<uint64_t>(directory_descriptor), OS_USER_FS_PROBE_ALIAS_NAME,
                           sizeof(OS_USER_FS_PROBE_ALIAS_NAME) -
                               OS_USER_FS_PROBE_STRING_TERMINATOR_SIZE_BYTES,
                           OS_USER_FS_PROBE_EMPTY_VALUE) == OS_USER_FS_PROBE_SUCCESS_RESULT &&
        os::user::RemoveAt(
            static_cast<uint64_t>(directory_descriptor), OS_USER_FS_PROBE_SYMBOLIC_NAME,
            sizeof(OS_USER_FS_PROBE_SYMBOLIC_NAME) - OS_USER_FS_PROBE_STRING_TERMINATOR_SIZE_BYTES,
            OS_USER_FS_PROBE_EMPTY_VALUE) == OS_USER_FS_PROBE_SUCCESS_RESULT &&
        BytesEqual(symbolic_target, OS_USER_FS_PROBE_FILE_NAME,
                   sizeof(OS_USER_FS_PROBE_FILE_NAME) -
                       OS_USER_FS_PROBE_STRING_TERMINATOR_SIZE_BYTES);
    if (!at_operations_succeeded || os::user::SyncFileSystem() != OS_USER_FS_PROBE_SUCCESS_RESULT) {
        ExitFailure(OS_USER_FS_PROBE_OPERATION_FAILURE_MESSAGE);
    }

    const int64_t read_descriptor = os::user::OpenFileAt(
        static_cast<uint64_t>(directory_descriptor), OS_USER_FS_PROBE_FILE_NAME,
        sizeof(OS_USER_FS_PROBE_FILE_NAME) - OS_USER_FS_PROBE_STRING_TERMINATOR_SIZE_BYTES,
        os::abi::OS_ABI_FILE_OPEN_READ_FLAG);
    if (read_descriptor < OS_USER_FS_PROBE_SUCCESS_RESULT ||
        os::user::ReadDescriptor(static_cast<uint64_t>(read_descriptor), read_buffer,
                                 sizeof(read_buffer)) !=
            static_cast<int64_t>(sizeof(read_buffer))) {
        ExitFailure(OS_USER_FS_PROBE_READ_FAILURE_MESSAGE);
    }
    for (uint64_t byte_index = OS_USER_FS_PROBE_FIRST_INDEX;
         byte_index < OS_USER_FS_PROBE_PAYLOAD_SIZE_BYTES; ++byte_index) {
        if (read_buffer[byte_index] != ExpectedByte(byte_index)) {
            ExitFailure(OS_USER_FS_PROBE_READ_FAILURE_MESSAGE);
        }
    }
    uint8_t end_probe = OS_USER_FS_PROBE_EMPTY_BYTE;
    if (os::user::ReadDescriptor(static_cast<uint64_t>(read_descriptor), &end_probe,
                                 sizeof(end_probe)) != OS_USER_FS_PROBE_SUCCESS_RESULT ||
        os::user::CloseDescriptor(static_cast<uint64_t>(read_descriptor)) !=
            OS_USER_FS_PROBE_SUCCESS_RESULT ||
        os::user::CloseDescriptor(static_cast<uint64_t>(directory_descriptor)) !=
            OS_USER_FS_PROBE_SUCCESS_RESULT ||
        !WriteMessage(OS_USER_FS_PROBE_FILE_VERIFIED_MESSAGE)) {
        ExitFailure(OS_USER_FS_PROBE_READ_FAILURE_MESSAGE);
    }
    os::user::ExitProcess(OS_USER_FS_PROBE_SUCCESS_RESULT);
}

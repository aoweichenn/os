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
constexpr char OS_USER_FS_PROBE_DESCRIPTION_FAILURE_MESSAGE[] =
    "[OS][USER][FS][FAIL] DESCRIPTION_IO\r\n";
constexpr char OS_USER_FS_PROBE_POSITIONED_FAILURE_MESSAGE[] =
    "[OS][USER][FS][FAIL] DESCRIPTION_POSITIONED\r\n";
constexpr char OS_USER_FS_PROBE_TRUNCATE_GROW_FAILURE_MESSAGE[] =
    "[OS][USER][FS][FAIL] DESCRIPTION_TRUNCATE_GROW\r\n";
constexpr char OS_USER_FS_PROBE_TRUNCATE_STAT_FAILURE_MESSAGE[] =
    "[OS][USER][FS][FAIL] DESCRIPTION_TRUNCATE_STAT\r\n";
constexpr char OS_USER_FS_PROBE_TRUNCATE_SHRINK_FAILURE_MESSAGE[] =
    "[OS][USER][FS][FAIL] DESCRIPTION_TRUNCATE_SHRINK\r\n";
constexpr char OS_USER_FS_PROBE_METADATA_FAILURE_MESSAGE[] =
    "[OS][USER][FS][FAIL] DESCRIPTION_METADATA\r\n";
constexpr char OS_USER_FS_PROBE_STATUS_FAILURE_MESSAGE[] =
    "[OS][USER][FS][FAIL] DESCRIPTION_STATUS\r\n";
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
constexpr uint64_t OS_USER_FS_PROBE_SEEK_READ_OFFSET_BYTES = 8ULL;
constexpr uint64_t OS_USER_FS_PROBE_SEEK_AFTER_READ_OFFSET_BYTES = 11ULL;
constexpr uint64_t OS_USER_FS_PROBE_POSITIONED_WRITE_OFFSET_BYTES = 20ULL;
constexpr uint64_t OS_USER_FS_PROBE_POSITIONED_READ_OFFSET_BYTES = 32ULL;
constexpr uint64_t OS_USER_FS_PROBE_EXTENDED_SIZE_BYTES = 260ULL;
constexpr uint8_t OS_USER_FS_PROBE_EMPTY_BYTE = 0U;
constexpr uint64_t OS_USER_FS_PROBE_WRITE_FLAGS = os::abi::OS_ABI_FILE_OPEN_WRITE_FLAG |
                                                  os::abi::OS_ABI_FILE_OPEN_CREATE_FLAG |
                                                  os::abi::OS_ABI_FILE_OPEN_TRUNCATE_FLAG;
constexpr uint64_t OS_USER_FS_PROBE_READ_WRITE_FLAGS =
    os::abi::OS_ABI_FILE_OPEN_READ_FLAG | os::abi::OS_ABI_FILE_OPEN_WRITE_FLAG;
constexpr uint64_t OS_USER_FS_PROBE_READ_WRITE_STATUS_FLAGS =
    os::abi::OS_ABI_FILE_STATUS_READABLE_FLAG | os::abi::OS_ABI_FILE_STATUS_WRITABLE_FLAG;
constexpr uint64_t OS_USER_FS_PROBE_APPEND_STATUS_FLAGS =
    OS_USER_FS_PROBE_READ_WRITE_STATUS_FLAGS | os::abi::OS_ABI_FILE_STATUS_APPEND_FLAG;
constexpr os::abi::FileMode OS_USER_FS_PROBE_PRIVATE_MODE = 0000600U;
constexpr os::abi::FileMode OS_USER_FS_PROBE_DEFAULT_MODE = 0000644U;
constexpr uint8_t OS_USER_FS_PROBE_POSITIONED_PATCH[] = {
    static_cast<uint8_t>('P'),
    static_cast<uint8_t>('O'),
    static_cast<uint8_t>('S'),
};
constexpr uint8_t OS_USER_FS_PROBE_APPEND_PATCH[] = {static_cast<uint8_t>('A')};
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

    const int64_t positioned_descriptor = os::user::OpenFileAt(
        static_cast<uint64_t>(directory_descriptor), OS_USER_FS_PROBE_FILE_NAME,
        sizeof(OS_USER_FS_PROBE_FILE_NAME) - OS_USER_FS_PROBE_STRING_TERMINATOR_SIZE_BYTES,
        OS_USER_FS_PROBE_READ_WRITE_FLAGS);
    uint8_t sequential_probe[sizeof(OS_USER_FS_PROBE_POSITIONED_PATCH)]{};
    uint8_t positioned_probe[sizeof(OS_USER_FS_PROBE_POSITIONED_PATCH)]{};
    os::abi::FileInformation descriptor_information{};
    const bool positioned_operations_succeeded =
        positioned_descriptor >= OS_USER_FS_PROBE_SUCCESS_RESULT &&
        os::user::SeekDescriptor(static_cast<uint64_t>(directory_descriptor),
                                 OS_USER_FS_PROBE_EMPTY_VALUE,
                                 os::abi::OS_ABI_SEEK_FROM_BEGINNING) ==
            os::abi::OS_ABI_SYSTEM_CALL_RESULT_NOT_SEEKABLE &&
        os::user::SeekDescriptor(static_cast<uint64_t>(positioned_descriptor),
                                 static_cast<int64_t>(OS_USER_FS_PROBE_SEEK_READ_OFFSET_BYTES),
                                 os::abi::OS_ABI_SEEK_FROM_BEGINNING) ==
            static_cast<int64_t>(OS_USER_FS_PROBE_SEEK_READ_OFFSET_BYTES) &&
        os::user::ReadDescriptor(static_cast<uint64_t>(positioned_descriptor), sequential_probe,
                                 sizeof(sequential_probe)) ==
            static_cast<int64_t>(sizeof(sequential_probe)) &&
        BytesEqual(
            reinterpret_cast<const char *>(sequential_probe),
            reinterpret_cast<const char *>(payload + OS_USER_FS_PROBE_SEEK_READ_OFFSET_BYTES),
            sizeof(sequential_probe)) &&
        os::user::ReadDescriptorAt(static_cast<uint64_t>(positioned_descriptor), positioned_probe,
                                   sizeof(positioned_probe),
                                   OS_USER_FS_PROBE_POSITIONED_READ_OFFSET_BYTES) ==
            static_cast<int64_t>(sizeof(positioned_probe)) &&
        BytesEqual(
            reinterpret_cast<const char *>(positioned_probe),
            reinterpret_cast<const char *>(payload + OS_USER_FS_PROBE_POSITIONED_READ_OFFSET_BYTES),
            sizeof(positioned_probe)) &&
        os::user::SeekDescriptor(static_cast<uint64_t>(positioned_descriptor),
                                 OS_USER_FS_PROBE_EMPTY_VALUE, os::abi::OS_ABI_SEEK_FROM_CURRENT) ==
            static_cast<int64_t>(OS_USER_FS_PROBE_SEEK_AFTER_READ_OFFSET_BYTES) &&
        os::user::WriteDescriptorAt(static_cast<uint64_t>(positioned_descriptor),
                                    OS_USER_FS_PROBE_POSITIONED_PATCH,
                                    sizeof(OS_USER_FS_PROBE_POSITIONED_PATCH),
                                    OS_USER_FS_PROBE_POSITIONED_WRITE_OFFSET_BYTES) ==
            static_cast<int64_t>(sizeof(OS_USER_FS_PROBE_POSITIONED_PATCH)) &&
        os::user::SeekDescriptor(static_cast<uint64_t>(positioned_descriptor),
                                 OS_USER_FS_PROBE_EMPTY_VALUE, os::abi::OS_ABI_SEEK_FROM_CURRENT) ==
            static_cast<int64_t>(OS_USER_FS_PROBE_SEEK_AFTER_READ_OFFSET_BYTES) &&
        os::user::StatDescriptor(static_cast<uint64_t>(positioned_descriptor),
                                 descriptor_information) == OS_USER_FS_PROBE_SUCCESS_RESULT &&
        descriptor_information.size_bytes == OS_USER_FS_PROBE_PAYLOAD_SIZE_BYTES;
    if (!positioned_operations_succeeded) {
        ExitFailure(OS_USER_FS_PROBE_POSITIONED_FAILURE_MESSAGE);
    }

    if (os::user::TruncateDescriptor(static_cast<uint64_t>(positioned_descriptor),
                                     OS_USER_FS_PROBE_EXTENDED_SIZE_BYTES) !=
        OS_USER_FS_PROBE_SUCCESS_RESULT) {
        ExitFailure(OS_USER_FS_PROBE_TRUNCATE_GROW_FAILURE_MESSAGE);
    }
    if (os::user::StatDescriptor(static_cast<uint64_t>(positioned_descriptor),
                                 descriptor_information) != OS_USER_FS_PROBE_SUCCESS_RESULT ||
        descriptor_information.size_bytes != OS_USER_FS_PROBE_EXTENDED_SIZE_BYTES) {
        ExitFailure(OS_USER_FS_PROBE_TRUNCATE_STAT_FAILURE_MESSAGE);
    }
    if (os::user::TruncateDescriptor(static_cast<uint64_t>(positioned_descriptor),
                                     OS_USER_FS_PROBE_PAYLOAD_SIZE_BYTES) !=
        OS_USER_FS_PROBE_SUCCESS_RESULT) {
        ExitFailure(OS_USER_FS_PROBE_TRUNCATE_SHRINK_FAILURE_MESSAGE);
    }

    const bool metadata_operations_succeeded =
        os::user::ChangeDescriptorMode(static_cast<uint64_t>(positioned_descriptor),
                                       OS_USER_FS_PROBE_PRIVATE_MODE) ==
            OS_USER_FS_PROBE_SUCCESS_RESULT &&
        os::user::StatDescriptor(static_cast<uint64_t>(positioned_descriptor),
                                 descriptor_information) == OS_USER_FS_PROBE_SUCCESS_RESULT &&
        (descriptor_information.mode & os::abi::OS_ABI_FILE_MODE_CHANGEABLE_MASK) ==
            OS_USER_FS_PROBE_PRIVATE_MODE &&
        os::user::ChangeDescriptorOwner(
            static_cast<uint64_t>(positioned_descriptor), os::abi::OS_ABI_IDENTIFIER_UNCHANGED,
            os::abi::OS_ABI_GROUP_IDENTIFIER_UNCHANGED) == OS_USER_FS_PROBE_SUCCESS_RESULT;
    if (!metadata_operations_succeeded) {
        ExitFailure(OS_USER_FS_PROBE_METADATA_FAILURE_MESSAGE);
    }

    const bool status_operations_succeeded =
        os::user::GetFileStatusFlags(static_cast<uint64_t>(positioned_descriptor)) ==
            static_cast<int64_t>(OS_USER_FS_PROBE_READ_WRITE_STATUS_FLAGS) &&
        os::user::SetFileStatusFlags(static_cast<uint64_t>(positioned_descriptor),
                                     OS_USER_FS_PROBE_APPEND_STATUS_FLAGS) ==
            OS_USER_FS_PROBE_SUCCESS_RESULT &&
        os::user::WriteDescriptorAt(
            static_cast<uint64_t>(positioned_descriptor), OS_USER_FS_PROBE_APPEND_PATCH,
            sizeof(OS_USER_FS_PROBE_APPEND_PATCH), OS_USER_FS_PROBE_EMPTY_VALUE) ==
            static_cast<int64_t>(sizeof(OS_USER_FS_PROBE_APPEND_PATCH)) &&
        os::user::SeekDescriptor(static_cast<uint64_t>(positioned_descriptor),
                                 OS_USER_FS_PROBE_EMPTY_VALUE, os::abi::OS_ABI_SEEK_FROM_CURRENT) ==
            static_cast<int64_t>(OS_USER_FS_PROBE_SEEK_AFTER_READ_OFFSET_BYTES) &&
        os::user::StatDescriptor(static_cast<uint64_t>(positioned_descriptor),
                                 descriptor_information) == OS_USER_FS_PROBE_SUCCESS_RESULT &&
        descriptor_information.size_bytes ==
            OS_USER_FS_PROBE_PAYLOAD_SIZE_BYTES + sizeof(OS_USER_FS_PROBE_APPEND_PATCH) &&
        os::user::SetFileStatusFlags(static_cast<uint64_t>(positioned_descriptor),
                                     OS_USER_FS_PROBE_READ_WRITE_STATUS_FLAGS) ==
            OS_USER_FS_PROBE_SUCCESS_RESULT &&
        os::user::TruncateDescriptor(static_cast<uint64_t>(positioned_descriptor),
                                     OS_USER_FS_PROBE_PAYLOAD_SIZE_BYTES) ==
            OS_USER_FS_PROBE_SUCCESS_RESULT &&
        os::user::ChangeDescriptorMode(static_cast<uint64_t>(positioned_descriptor),
                                       OS_USER_FS_PROBE_DEFAULT_MODE) ==
            OS_USER_FS_PROBE_SUCCESS_RESULT &&
        os::user::CloseDescriptor(static_cast<uint64_t>(positioned_descriptor)) ==
            OS_USER_FS_PROBE_SUCCESS_RESULT;
    if (!status_operations_succeeded) {
        ExitFailure(OS_USER_FS_PROBE_STATUS_FAILURE_MESSAGE);
    }
    if (os::user::SyncFileSystem() != OS_USER_FS_PROBE_SUCCESS_RESULT) {
        ExitFailure(OS_USER_FS_PROBE_DESCRIPTION_FAILURE_MESSAGE);
    }
    for (uint64_t patch_index = OS_USER_FS_PROBE_FIRST_INDEX;
         patch_index < sizeof(OS_USER_FS_PROBE_POSITIONED_PATCH); ++patch_index) {
        payload[OS_USER_FS_PROBE_POSITIONED_WRITE_OFFSET_BYTES + patch_index] =
            OS_USER_FS_PROBE_POSITIONED_PATCH[patch_index];
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
        if (read_buffer[byte_index] != payload[byte_index]) {
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

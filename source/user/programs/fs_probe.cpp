#include <os/user/system_call.hpp>

#include <os/abi/system_call.hpp>

#include <stdint.h>

namespace {

constexpr char OS_USER_FS_PROBE_FILE_WRITTEN_MESSAGE[] = "[OS][USER][FS] FILE_WRITTEN\r\n";
constexpr char OS_USER_FS_PROBE_FILE_VERIFIED_MESSAGE[] = "[OS][USER][FS] FILE_VERIFIED\r\n";
constexpr char OS_USER_FS_PROBE_DIRECTORY_PATH[] = "/shared";
constexpr char OS_USER_FS_PROBE_FILE_PATH[] = "/shared/payload.bin";
constexpr uint64_t OS_USER_FS_PROBE_PAYLOAD_SIZE_BYTES = 256ULL;
constexpr uint64_t OS_USER_FS_PROBE_STRING_TERMINATOR_SIZE_BYTES = 1ULL;
constexpr uint64_t OS_USER_FS_PROBE_BYTE_MULTIPLIER = 37ULL;
constexpr uint64_t OS_USER_FS_PROBE_BYTE_INCREMENT = 11ULL;
constexpr uint64_t OS_USER_FS_PROBE_BYTE_MASK = 0xFFULL;
constexpr uint64_t OS_USER_FS_PROBE_FIRST_INDEX = 0ULL;
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

template <uint64_t MessageSizeBytes>
[[nodiscard]] bool WriteMessage(const char (&message)[MessageSizeBytes]) noexcept {
    return os::user::WriteLog(message,
                              MessageSizeBytes - OS_USER_FS_PROBE_STRING_TERMINATOR_SIZE_BYTES) >
           OS_USER_FS_PROBE_FIRST_ERROR_RESULT;
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
    const int64_t write_descriptor = os::user::OpenFile(
        OS_USER_FS_PROBE_FILE_PATH,
        sizeof(OS_USER_FS_PROBE_FILE_PATH) - OS_USER_FS_PROBE_STRING_TERMINATOR_SIZE_BYTES,
        OS_USER_FS_PROBE_WRITE_FLAGS);
    if (write_descriptor < OS_USER_FS_PROBE_SUCCESS_RESULT ||
        os::user::WriteDescriptor(static_cast<uint64_t>(write_descriptor), payload,
                                  sizeof(payload)) != static_cast<int64_t>(sizeof(payload)) ||
        os::user::CloseDescriptor(static_cast<uint64_t>(write_descriptor)) !=
            OS_USER_FS_PROBE_SUCCESS_RESULT ||
        os::user::SyncFileSystem() != OS_USER_FS_PROBE_SUCCESS_RESULT ||
        !WriteMessage(OS_USER_FS_PROBE_FILE_WRITTEN_MESSAGE)) {
        os::user::ExitProcess(OS_USER_FS_PROBE_FAILURE_EXIT_CODE);
    }

    const int64_t read_descriptor = os::user::OpenFile(
        OS_USER_FS_PROBE_FILE_PATH,
        sizeof(OS_USER_FS_PROBE_FILE_PATH) - OS_USER_FS_PROBE_STRING_TERMINATOR_SIZE_BYTES,
        os::abi::OS_ABI_FILE_OPEN_READ_FLAG);
    if (read_descriptor < OS_USER_FS_PROBE_SUCCESS_RESULT ||
        os::user::ReadDescriptor(static_cast<uint64_t>(read_descriptor), read_buffer,
                                 sizeof(read_buffer)) !=
            static_cast<int64_t>(sizeof(read_buffer))) {
        os::user::ExitProcess(OS_USER_FS_PROBE_FAILURE_EXIT_CODE);
    }
    for (uint64_t byte_index = OS_USER_FS_PROBE_FIRST_INDEX;
         byte_index < OS_USER_FS_PROBE_PAYLOAD_SIZE_BYTES; ++byte_index) {
        if (read_buffer[byte_index] != ExpectedByte(byte_index)) {
            os::user::ExitProcess(OS_USER_FS_PROBE_FAILURE_EXIT_CODE);
        }
    }
    uint8_t end_probe = OS_USER_FS_PROBE_EMPTY_BYTE;
    if (os::user::ReadDescriptor(static_cast<uint64_t>(read_descriptor), &end_probe,
                                 sizeof(end_probe)) != OS_USER_FS_PROBE_SUCCESS_RESULT ||
        os::user::CloseDescriptor(static_cast<uint64_t>(read_descriptor)) !=
            OS_USER_FS_PROBE_SUCCESS_RESULT ||
        !WriteMessage(OS_USER_FS_PROBE_FILE_VERIFIED_MESSAGE)) {
        os::user::ExitProcess(OS_USER_FS_PROBE_FAILURE_EXIT_CODE);
    }
    os::user::ExitProcess(OS_USER_FS_PROBE_SUCCESS_RESULT);
}

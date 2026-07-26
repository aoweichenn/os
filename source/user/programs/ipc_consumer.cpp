#include "os/user/extended_state.hpp"
#include "os/user/system_call.hpp"

#include "os/abi/system_call.hpp"

#include <stdint.h>

namespace {

constexpr char OS_USER_IPC_CONSUMER_STARTED_MESSAGE[] = "[OS][USER][PIPE] CONSUMER_STARTED\r\n";
constexpr char OS_USER_IPC_CONSUMER_PAYLOAD_VERIFIED_MESSAGE[] =
    "[OS][USER][PIPE] PAYLOAD_VERIFIED\r\n";
constexpr char OS_USER_IPC_CONSUMER_END_OF_FILE_MESSAGE[] = "[OS][USER][PIPE] EOF_OBSERVED\r\n";
constexpr char OS_USER_IPC_CONSUMER_FILE_VERIFIED_MESSAGE[] = "[OS][USER][FS] FILE_VERIFIED\r\n";
constexpr char OS_USER_IPC_CONSUMER_FILE_PATH[] = "/shared/payload.bin";
constexpr uint64_t OS_USER_IPC_CONSUMER_PROCESS_ID = 3ULL;
constexpr uint64_t OS_USER_IPC_CONSUMER_PIPE_DESCRIPTOR = os::abi::OS_ABI_FIRST_DYNAMIC_DESCRIPTOR;
constexpr uint64_t OS_USER_IPC_CONSUMER_PAYLOAD_SIZE_BYTES = 256ULL;
constexpr uint64_t OS_USER_IPC_CONSUMER_READ_BUFFER_SIZE_BYTES = 31ULL;
constexpr uint64_t OS_USER_IPC_CONSUMER_STRING_TERMINATOR_SIZE_BYTES = 1ULL;
constexpr uint64_t OS_USER_IPC_CONSUMER_POINTER_PROBE_SIZE_BYTES = 1ULL;
constexpr uint64_t OS_USER_IPC_CONSUMER_UNMAPPED_POINTER = 0x000000003FFFFFFFULL;
constexpr uint64_t OS_USER_IPC_CONSUMER_BYTE_MULTIPLIER = 37ULL;
constexpr uint64_t OS_USER_IPC_CONSUMER_BYTE_INCREMENT = 11ULL;
constexpr uint64_t OS_USER_IPC_CONSUMER_BYTE_MASK = 0xFFULL;
constexpr uint64_t OS_USER_IPC_CONSUMER_EMPTY_BYTE_COUNT = 0ULL;
constexpr uint64_t OS_USER_IPC_CONSUMER_FIRST_BYTE_INDEX = 0ULL;
constexpr uint8_t OS_USER_IPC_CONSUMER_ZERO_BYTE = 0U;
constexpr int64_t OS_USER_IPC_CONSUMER_SUCCESS_EXIT_CODE = 0LL;
constexpr int64_t OS_USER_IPC_CONSUMER_FAILURE_EXIT_CODE = 1LL;
constexpr int64_t OS_USER_IPC_CONSUMER_FIRST_ERROR_RESULT = -1LL;
constexpr int64_t OS_USER_IPC_CONSUMER_END_OF_FILE_RESULT = 0LL;
constexpr int64_t OS_USER_IPC_CONSUMER_SUCCESS_RESULT = 0LL;

uint8_t consumer_buffer[OS_USER_IPC_CONSUMER_READ_BUFFER_SIZE_BYTES];
uint8_t file_buffer[OS_USER_IPC_CONSUMER_PAYLOAD_SIZE_BYTES];

template <uint64_t MessageSizeBytes>
[[nodiscard]] bool WriteMessage(const char (&message)[MessageSizeBytes]) noexcept {
    return os::user::WriteLog(message, MessageSizeBytes -
                                           OS_USER_IPC_CONSUMER_STRING_TERMINATOR_SIZE_BYTES) >
           OS_USER_IPC_CONSUMER_FIRST_ERROR_RESULT;
}

[[nodiscard]] uint8_t ExpectedPayloadByte(const uint64_t byte_index) noexcept {
    return static_cast<uint8_t>(
        (byte_index * OS_USER_IPC_CONSUMER_BYTE_MULTIPLIER + OS_USER_IPC_CONSUMER_BYTE_INCREMENT) &
        OS_USER_IPC_CONSUMER_BYTE_MASK);
}
}

extern "C" [[noreturn, gnu::section(".text.os_user_entry")]] void OsUserEntry() noexcept {
    const uint64_t process_id = os::user::GetProcessId();
    uint8_t permission_probe = OS_USER_IPC_CONSUMER_ZERO_BYTE;
    if (process_id != OS_USER_IPC_CONSUMER_PROCESS_ID ||
        !os::user::InitializeExtendedStateIsolationTest(process_id) ||
        os::user::TryWriteDescriptor(OS_USER_IPC_CONSUMER_PIPE_DESCRIPTOR, &permission_probe,
                                     OS_USER_IPC_CONSUMER_POINTER_PROBE_SIZE_BYTES) !=
            os::abi::OS_ABI_SYSTEM_CALL_RESULT_DESCRIPTOR_PERMISSION_DENIED ||
        os::user::TryReadDescriptor(
            OS_USER_IPC_CONSUMER_PIPE_DESCRIPTOR,
            reinterpret_cast<uint8_t *>(OS_USER_IPC_CONSUMER_UNMAPPED_POINTER),
            OS_USER_IPC_CONSUMER_POINTER_PROBE_SIZE_BYTES) !=
            os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_USER_MEMORY ||
        !WriteMessage(OS_USER_IPC_CONSUMER_STARTED_MESSAGE)) {
        os::user::ExitProcess(OS_USER_IPC_CONSUMER_FAILURE_EXIT_CODE);
    }

    uint64_t total_read_bytes = OS_USER_IPC_CONSUMER_EMPTY_BYTE_COUNT;
    while (true) {
        const int64_t read_result =
            os::user::ReadDescriptor(OS_USER_IPC_CONSUMER_PIPE_DESCRIPTOR, consumer_buffer,
                                     OS_USER_IPC_CONSUMER_READ_BUFFER_SIZE_BYTES);
        if (read_result < OS_USER_IPC_CONSUMER_END_OF_FILE_RESULT) {
            os::user::ExitProcess(OS_USER_IPC_CONSUMER_FAILURE_EXIT_CODE);
        }
        if (read_result == OS_USER_IPC_CONSUMER_END_OF_FILE_RESULT) {
            break;
        }
        const uint64_t read_bytes = static_cast<uint64_t>(read_result);
        if (total_read_bytes + read_bytes > OS_USER_IPC_CONSUMER_PAYLOAD_SIZE_BYTES) {
            os::user::ExitProcess(OS_USER_IPC_CONSUMER_FAILURE_EXIT_CODE);
        }
        for (uint64_t byte_index = OS_USER_IPC_CONSUMER_FIRST_BYTE_INDEX; byte_index < read_bytes;
             ++byte_index) {
            if (consumer_buffer[byte_index] != ExpectedPayloadByte(total_read_bytes + byte_index)) {
                os::user::ExitProcess(OS_USER_IPC_CONSUMER_FAILURE_EXIT_CODE);
            }
        }
        total_read_bytes += read_bytes;
    }

    if (total_read_bytes != OS_USER_IPC_CONSUMER_PAYLOAD_SIZE_BYTES ||
        !WriteMessage(OS_USER_IPC_CONSUMER_PAYLOAD_VERIFIED_MESSAGE) ||
        !WriteMessage(OS_USER_IPC_CONSUMER_END_OF_FILE_MESSAGE) ||
        os::user::CloseDescriptor(OS_USER_IPC_CONSUMER_PIPE_DESCRIPTOR) !=
            OS_USER_IPC_CONSUMER_SUCCESS_RESULT ||
        os::user::CloseDescriptor(OS_USER_IPC_CONSUMER_PIPE_DESCRIPTOR) !=
            os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_FILE_DESCRIPTOR) {
        os::user::ExitProcess(OS_USER_IPC_CONSUMER_FAILURE_EXIT_CODE);
    }

    const int64_t file_descriptor = os::user::OpenFile(
        OS_USER_IPC_CONSUMER_FILE_PATH,
        sizeof(OS_USER_IPC_CONSUMER_FILE_PATH) - OS_USER_IPC_CONSUMER_STRING_TERMINATOR_SIZE_BYTES,
        os::abi::OS_ABI_FILE_OPEN_READ_FLAG);
    if (file_descriptor < OS_USER_IPC_CONSUMER_SUCCESS_RESULT ||
        os::user::ReadDescriptor(static_cast<uint64_t>(file_descriptor), file_buffer,
                                 OS_USER_IPC_CONSUMER_PAYLOAD_SIZE_BYTES) !=
            static_cast<int64_t>(OS_USER_IPC_CONSUMER_PAYLOAD_SIZE_BYTES)) {
        os::user::ExitProcess(OS_USER_IPC_CONSUMER_FAILURE_EXIT_CODE);
    }
    for (uint64_t byte_index = OS_USER_IPC_CONSUMER_FIRST_BYTE_INDEX;
         byte_index < OS_USER_IPC_CONSUMER_PAYLOAD_SIZE_BYTES; ++byte_index) {
        if (file_buffer[byte_index] != ExpectedPayloadByte(byte_index)) {
            os::user::ExitProcess(OS_USER_IPC_CONSUMER_FAILURE_EXIT_CODE);
        }
    }
    if (os::user::ReadDescriptor(static_cast<uint64_t>(file_descriptor), file_buffer,
                                 OS_USER_IPC_CONSUMER_POINTER_PROBE_SIZE_BYTES) !=
            OS_USER_IPC_CONSUMER_END_OF_FILE_RESULT ||
        os::user::CloseDescriptor(static_cast<uint64_t>(file_descriptor)) !=
            OS_USER_IPC_CONSUMER_SUCCESS_RESULT ||
        !WriteMessage(OS_USER_IPC_CONSUMER_FILE_VERIFIED_MESSAGE) ||
        !os::user::CompleteExtendedStateIsolationTest(process_id)) {
        os::user::ExitProcess(OS_USER_IPC_CONSUMER_FAILURE_EXIT_CODE);
    }
    os::user::ExitProcess(OS_USER_IPC_CONSUMER_SUCCESS_EXIT_CODE);
}

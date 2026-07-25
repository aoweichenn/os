#include "os/user/system_call.hpp"

#include "os/abi/system_call.hpp"

#include <stdint.h>

namespace {

constexpr char OS_USER_IPC_PRODUCER_STARTED_MESSAGE[] = "[OS][USER][PIPE] PRODUCER_STARTED\r\n";
constexpr char OS_USER_IPC_PRODUCER_COMPLETED_MESSAGE[] = "[OS][USER][PIPE] PRODUCER_COMPLETED\r\n";
constexpr char OS_USER_IPC_PRODUCER_INVALID_POINTER_REJECTED_MESSAGE[] =
    "[OS][USER] INVALID_POINTER_REJECTED\r\n";
constexpr char OS_USER_IPC_PRODUCER_UNKNOWN_SYSTEM_CALL_REJECTED_MESSAGE[] =
    "[OS][USER] UNKNOWN_SYSCALL_REJECTED\r\n";
constexpr char OS_USER_IPC_PRODUCER_RING3_MESSAGE[] = "[OS][USER] HELLO_FROM_RING3\r\n";
constexpr char OS_USER_IPC_PRODUCER_FILE_WRITTEN_MESSAGE[] = "[OS][USER][FS] FILE_WRITTEN\r\n";
constexpr char OS_USER_IPC_PRODUCER_DIRECTORY_PATH[] = "/shared";
constexpr char OS_USER_IPC_PRODUCER_FILE_PATH[] = "/shared/payload.bin";
constexpr uint64_t OS_USER_IPC_PRODUCER_PROCESS_ID = 2ULL;
constexpr uint64_t OS_USER_IPC_PRODUCER_PIPE_DESCRIPTOR = os::abi::OS_ABI_FIRST_DYNAMIC_DESCRIPTOR;
constexpr uint64_t OS_USER_IPC_PRODUCER_PAYLOAD_SIZE_BYTES = 256ULL;
constexpr uint64_t OS_USER_IPC_PRODUCER_STRING_TERMINATOR_SIZE_BYTES = 1ULL;
constexpr uint64_t OS_USER_IPC_PRODUCER_POINTER_PROBE_SIZE_BYTES = 1ULL;
constexpr uint64_t OS_USER_IPC_PRODUCER_UNMAPPED_POINTER = 0x000000003FFFFFFFULL;
constexpr uint64_t OS_USER_IPC_PRODUCER_UNKNOWN_SYSTEM_CALL_NUMBER = 0xFFFFFFFFFFFFFFFFULL;
constexpr uint64_t OS_USER_IPC_PRODUCER_UNUSED_SYSTEM_CALL_ARGUMENT = 0ULL;
constexpr uint64_t OS_USER_IPC_PRODUCER_BYTE_MULTIPLIER = 37ULL;
constexpr uint64_t OS_USER_IPC_PRODUCER_BYTE_INCREMENT = 11ULL;
constexpr uint64_t OS_USER_IPC_PRODUCER_BYTE_MASK = 0xFFULL;
constexpr uint64_t OS_USER_IPC_PRODUCER_FIRST_BYTE_INDEX = 0ULL;
constexpr uint8_t OS_USER_IPC_PRODUCER_ZERO_BYTE = 0U;
constexpr int64_t OS_USER_IPC_PRODUCER_SUCCESS_EXIT_CODE = 0LL;
constexpr int64_t OS_USER_IPC_PRODUCER_FAILURE_EXIT_CODE = 1LL;
constexpr int64_t OS_USER_IPC_PRODUCER_FIRST_ERROR_RESULT = -1LL;
constexpr int64_t OS_USER_IPC_PRODUCER_SUCCESS_RESULT = 0LL;
constexpr uint64_t OS_USER_IPC_PRODUCER_FILE_OPEN_FLAGS = os::abi::OS_ABI_FILE_OPEN_WRITE_FLAG |
                                                          os::abi::OS_ABI_FILE_OPEN_CREATE_FLAG |
                                                          os::abi::OS_ABI_FILE_OPEN_TRUNCATE_FLAG;

uint8_t producer_payload[OS_USER_IPC_PRODUCER_PAYLOAD_SIZE_BYTES];

template <uint64_t MessageSizeBytes>
[[nodiscard]] bool WriteMessage(const char (&message)[MessageSizeBytes]) noexcept {
    return os::user::WriteLog(message, MessageSizeBytes -
                                           OS_USER_IPC_PRODUCER_STRING_TERMINATOR_SIZE_BYTES) >
           OS_USER_IPC_PRODUCER_FIRST_ERROR_RESULT;
}

[[nodiscard]] uint8_t ExpectedPayloadByte(const uint64_t byte_index) noexcept {
    return static_cast<uint8_t>(
        (byte_index * OS_USER_IPC_PRODUCER_BYTE_MULTIPLIER + OS_USER_IPC_PRODUCER_BYTE_INCREMENT) &
        OS_USER_IPC_PRODUCER_BYTE_MASK);
}

void FillPayload() noexcept {
    for (uint64_t byte_index = OS_USER_IPC_PRODUCER_FIRST_BYTE_INDEX;
         byte_index < OS_USER_IPC_PRODUCER_PAYLOAD_SIZE_BYTES; ++byte_index) {
        producer_payload[byte_index] = ExpectedPayloadByte(byte_index);
    }
}

}

extern "C" [[noreturn, gnu::section(".text.os_user_entry")]] void OsUserEntry() noexcept {
    uint8_t permission_probe = OS_USER_IPC_PRODUCER_ZERO_BYTE;
    if (os::user::GetProcessId() != OS_USER_IPC_PRODUCER_PROCESS_ID ||
        os::user::TryReadDescriptor(OS_USER_IPC_PRODUCER_PIPE_DESCRIPTOR, &permission_probe,
                                    OS_USER_IPC_PRODUCER_POINTER_PROBE_SIZE_BYTES) !=
            os::abi::OS_ABI_SYSTEM_CALL_RESULT_DESCRIPTOR_PERMISSION_DENIED ||
        os::user::TryWriteDescriptor(
            OS_USER_IPC_PRODUCER_PIPE_DESCRIPTOR,
            reinterpret_cast<const uint8_t *>(OS_USER_IPC_PRODUCER_UNMAPPED_POINTER),
            OS_USER_IPC_PRODUCER_POINTER_PROBE_SIZE_BYTES) !=
            os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_USER_MEMORY) {
        os::user::ExitProcess(OS_USER_IPC_PRODUCER_FAILURE_EXIT_CODE);
    }
    const int64_t unknown_system_call_result =
        os::user::InvokeSystemCall(OS_USER_IPC_PRODUCER_UNKNOWN_SYSTEM_CALL_NUMBER,
                                   OS_USER_IPC_PRODUCER_UNUSED_SYSTEM_CALL_ARGUMENT,
                                   OS_USER_IPC_PRODUCER_UNUSED_SYSTEM_CALL_ARGUMENT,
                                   OS_USER_IPC_PRODUCER_UNUSED_SYSTEM_CALL_ARGUMENT);
    if (unknown_system_call_result != os::abi::OS_ABI_SYSTEM_CALL_RESULT_UNKNOWN_NUMBER ||
        !WriteMessage(OS_USER_IPC_PRODUCER_INVALID_POINTER_REJECTED_MESSAGE) ||
        !WriteMessage(OS_USER_IPC_PRODUCER_UNKNOWN_SYSTEM_CALL_REJECTED_MESSAGE) ||
        !WriteMessage(OS_USER_IPC_PRODUCER_RING3_MESSAGE) ||
        !WriteMessage(OS_USER_IPC_PRODUCER_STARTED_MESSAGE)) {
        os::user::ExitProcess(OS_USER_IPC_PRODUCER_FAILURE_EXIT_CODE);
    }

    FillPayload();
    const int64_t create_directory_result = os::user::CreateDirectory(
        OS_USER_IPC_PRODUCER_DIRECTORY_PATH, sizeof(OS_USER_IPC_PRODUCER_DIRECTORY_PATH) -
                                                 OS_USER_IPC_PRODUCER_STRING_TERMINATOR_SIZE_BYTES);
    if (create_directory_result != OS_USER_IPC_PRODUCER_SUCCESS_RESULT &&
        create_directory_result != os::abi::OS_ABI_SYSTEM_CALL_RESULT_FILE_ALREADY_EXISTS) {
        os::user::ExitProcess(OS_USER_IPC_PRODUCER_FAILURE_EXIT_CODE);
    }
    const int64_t file_descriptor = os::user::OpenFile(
        OS_USER_IPC_PRODUCER_FILE_PATH,
        sizeof(OS_USER_IPC_PRODUCER_FILE_PATH) - OS_USER_IPC_PRODUCER_STRING_TERMINATOR_SIZE_BYTES,
        OS_USER_IPC_PRODUCER_FILE_OPEN_FLAGS);
    if (file_descriptor < OS_USER_IPC_PRODUCER_SUCCESS_RESULT ||
        os::user::WriteDescriptor(static_cast<uint64_t>(file_descriptor), producer_payload,
                                  OS_USER_IPC_PRODUCER_PAYLOAD_SIZE_BYTES) !=
            static_cast<int64_t>(OS_USER_IPC_PRODUCER_PAYLOAD_SIZE_BYTES) ||
        os::user::CloseDescriptor(static_cast<uint64_t>(file_descriptor)) !=
            OS_USER_IPC_PRODUCER_SUCCESS_RESULT ||
        os::user::SyncFileSystem() != OS_USER_IPC_PRODUCER_SUCCESS_RESULT ||
        !WriteMessage(OS_USER_IPC_PRODUCER_FILE_WRITTEN_MESSAGE) ||
        os::user::WriteDescriptor(OS_USER_IPC_PRODUCER_PIPE_DESCRIPTOR, producer_payload,
                                  OS_USER_IPC_PRODUCER_PAYLOAD_SIZE_BYTES) !=
            static_cast<int64_t>(OS_USER_IPC_PRODUCER_PAYLOAD_SIZE_BYTES) ||
        os::user::CloseDescriptor(OS_USER_IPC_PRODUCER_PIPE_DESCRIPTOR) !=
            OS_USER_IPC_PRODUCER_SUCCESS_RESULT ||
        os::user::CloseDescriptor(OS_USER_IPC_PRODUCER_PIPE_DESCRIPTOR) !=
            os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_FILE_DESCRIPTOR ||
        !WriteMessage(OS_USER_IPC_PRODUCER_COMPLETED_MESSAGE)) {
        os::user::ExitProcess(OS_USER_IPC_PRODUCER_FAILURE_EXIT_CODE);
    }
    os::user::ExitProcess(OS_USER_IPC_PRODUCER_SUCCESS_EXIT_CODE);
}

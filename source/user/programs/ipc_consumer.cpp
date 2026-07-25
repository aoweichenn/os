#include "os/user/system_call.hpp"

#include "os/abi/system_call.hpp"

#include <stdint.h>

namespace {

constexpr char OS_USER_IPC_CONSUMER_STARTED_MESSAGE[] = "[OS][USER][PIPE] CONSUMER_STARTED\r\n";
constexpr char OS_USER_IPC_CONSUMER_PAYLOAD_VERIFIED_MESSAGE[] =
    "[OS][USER][PIPE] PAYLOAD_VERIFIED\r\n";
constexpr char OS_USER_IPC_CONSUMER_END_OF_FILE_MESSAGE[] = "[OS][USER][PIPE] EOF_OBSERVED\r\n";
constexpr uint64_t OS_USER_IPC_CONSUMER_PROCESS_ID = 2ULL;
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

uint8_t consumerBuffer[OS_USER_IPC_CONSUMER_READ_BUFFER_SIZE_BYTES];

template <uint64_t MessageSizeBytes>
[[nodiscard]] bool WriteMessage(const char (&message)[MessageSizeBytes]) noexcept {
    return os::user::WriteLog(message, MessageSizeBytes -
                                           OS_USER_IPC_CONSUMER_STRING_TERMINATOR_SIZE_BYTES) >
           OS_USER_IPC_CONSUMER_FIRST_ERROR_RESULT;
}

[[nodiscard]] uint8_t ExpectedPayloadByte(const uint64_t byteIndex) noexcept {
    return static_cast<uint8_t>(
        (byteIndex * OS_USER_IPC_CONSUMER_BYTE_MULTIPLIER + OS_USER_IPC_CONSUMER_BYTE_INCREMENT) &
        OS_USER_IPC_CONSUMER_BYTE_MASK);
}

}

extern "C" [[noreturn, gnu::section(".text.os_user_entry")]] void osUserEntry() noexcept {
    uint8_t permissionProbe = OS_USER_IPC_CONSUMER_ZERO_BYTE;
    if (os::user::GetProcessId() != OS_USER_IPC_CONSUMER_PROCESS_ID ||
        os::user::TryWritePipe(&permissionProbe, OS_USER_IPC_CONSUMER_POINTER_PROBE_SIZE_BYTES) !=
            os::abi::OS_ABI_SYSTEM_CALL_RESULT_PIPE_PERMISSION_DENIED ||
        os::user::TryReadPipe(reinterpret_cast<uint8_t *>(OS_USER_IPC_CONSUMER_UNMAPPED_POINTER),
                              OS_USER_IPC_CONSUMER_POINTER_PROBE_SIZE_BYTES) !=
            os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_USER_MEMORY ||
        !WriteMessage(OS_USER_IPC_CONSUMER_STARTED_MESSAGE)) {
        os::user::ExitProcess(OS_USER_IPC_CONSUMER_FAILURE_EXIT_CODE);
    }

    uint64_t totalReadBytes = OS_USER_IPC_CONSUMER_EMPTY_BYTE_COUNT;
    while (true) {
        const int64_t readResult =
            os::user::ReadPipe(consumerBuffer, OS_USER_IPC_CONSUMER_READ_BUFFER_SIZE_BYTES);
        if (readResult < OS_USER_IPC_CONSUMER_END_OF_FILE_RESULT) {
            os::user::ExitProcess(OS_USER_IPC_CONSUMER_FAILURE_EXIT_CODE);
        }
        if (readResult == OS_USER_IPC_CONSUMER_END_OF_FILE_RESULT) {
            break;
        }
        const uint64_t readBytes = static_cast<uint64_t>(readResult);
        if (totalReadBytes + readBytes > OS_USER_IPC_CONSUMER_PAYLOAD_SIZE_BYTES) {
            os::user::ExitProcess(OS_USER_IPC_CONSUMER_FAILURE_EXIT_CODE);
        }
        for (uint64_t byteIndex = OS_USER_IPC_CONSUMER_FIRST_BYTE_INDEX;
             byteIndex < readBytes; ++byteIndex) {
            if (consumerBuffer[byteIndex] != ExpectedPayloadByte(totalReadBytes + byteIndex)) {
                os::user::ExitProcess(OS_USER_IPC_CONSUMER_FAILURE_EXIT_CODE);
            }
        }
        totalReadBytes += readBytes;
    }

    if (totalReadBytes != OS_USER_IPC_CONSUMER_PAYLOAD_SIZE_BYTES ||
        !WriteMessage(OS_USER_IPC_CONSUMER_PAYLOAD_VERIFIED_MESSAGE) ||
        !WriteMessage(OS_USER_IPC_CONSUMER_END_OF_FILE_MESSAGE) ||
        os::user::ClosePipeReader() != OS_USER_IPC_CONSUMER_SUCCESS_RESULT ||
        os::user::ClosePipeReader() != os::abi::OS_ABI_SYSTEM_CALL_RESULT_ENDPOINT_CLOSED) {
        os::user::ExitProcess(OS_USER_IPC_CONSUMER_FAILURE_EXIT_CODE);
    }
    os::user::ExitProcess(OS_USER_IPC_CONSUMER_SUCCESS_EXIT_CODE);
}

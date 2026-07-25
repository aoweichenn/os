#pragma once

#include <stdint.h>

namespace os::abi {

enum class SystemCallNumber : uint64_t {
    WriteLog = 1ULL,
    ExitProcess = 2ULL,
    GetProcessId = 3ULL,
    TryReadPipe = 4ULL,
    TryWritePipe = 5ULL,
    WaitPipeReadable = 6ULL,
    WaitPipeWritable = 7ULL,
    ClosePipeReader = 8ULL,
    ClosePipeWriter = 9ULL,
    OpenFile = 10ULL,
    ReadFile = 11ULL,
    WriteFile = 12ULL,
    CloseFile = 13ULL,
    CreateDirectory = 14ULL,
    SyncFileSystem = 15ULL,
    TryReadDescriptor = 16ULL,
    TryWriteDescriptor = 17ULL,
    WaitDescriptorReadable = 18ULL,
    WaitDescriptorWritable = 19ULL,
    CloseDescriptor = 20ULL,
    OpenDirectory = 21ULL,
    ReadDirectory = 22ULL,
};

inline constexpr uint64_t OS_ABI_SYSTEM_CALL_VECTOR = 0x80ULL;
inline constexpr uint64_t OS_ABI_SYSTEM_CALL_MAXIMUM_WRITE_SIZE_BYTES = 160ULL;
inline constexpr uint64_t OS_ABI_SYSTEM_CALL_MAXIMUM_PIPE_TRANSFER_SIZE_BYTES = 64ULL;
inline constexpr uint64_t OS_ABI_SYSTEM_CALL_MAXIMUM_FILE_TRANSFER_SIZE_BYTES = 256ULL;
inline constexpr uint64_t OS_ABI_SYSTEM_CALL_MAXIMUM_PATH_SIZE_BYTES = 128ULL;
inline constexpr uint64_t OS_ABI_SYSTEM_CALL_MAXIMUM_DESCRIPTOR_TRANSFER_SIZE_BYTES =
    256ULL;
inline constexpr uint64_t OS_ABI_STANDARD_INPUT_DESCRIPTOR = 0ULL;
inline constexpr uint64_t OS_ABI_STANDARD_OUTPUT_DESCRIPTOR = 1ULL;
inline constexpr uint64_t OS_ABI_STANDARD_ERROR_DESCRIPTOR = 2ULL;
inline constexpr uint64_t OS_ABI_FIRST_DYNAMIC_DESCRIPTOR = 3ULL;
inline constexpr uint64_t OS_ABI_DIRECTORY_ENTRY_NAME_CAPACITY_BYTES = 40ULL;
inline constexpr uint64_t OS_ABI_DIRECTORY_ENTRY_SIZE_BYTES = 64ULL;
inline constexpr uint64_t OS_ABI_FILE_OPEN_READ_FLAG = 0x01ULL;
inline constexpr uint64_t OS_ABI_FILE_OPEN_WRITE_FLAG = 0x02ULL;
inline constexpr uint64_t OS_ABI_FILE_OPEN_CREATE_FLAG = 0x04ULL;
inline constexpr uint64_t OS_ABI_FILE_OPEN_TRUNCATE_FLAG = 0x08ULL;
inline constexpr uint64_t OS_ABI_FILE_OPEN_VALID_FLAG_MASK =
    OS_ABI_FILE_OPEN_READ_FLAG | OS_ABI_FILE_OPEN_WRITE_FLAG |
    OS_ABI_FILE_OPEN_CREATE_FLAG | OS_ABI_FILE_OPEN_TRUNCATE_FLAG;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_INVALID_USER_MEMORY = -1LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_UNKNOWN_NUMBER = -2LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_WRITE_TOO_LARGE = -3LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_DEVICE_FAILURE = -4LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_WOULD_BLOCK = -5LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_BROKEN_PIPE = -6LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_PIPE_PERMISSION_DENIED = -7LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_ENDPOINT_CLOSED = -8LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_NO_READY_PROCESS = -9LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_INVALID_ARGUMENT = -10LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_PIPE_TRANSFER_TOO_LARGE = -11LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_INVALID_FILE_DESCRIPTOR = -12LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_FILE_NOT_FOUND = -13LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_FILE_ALREADY_EXISTS = -14LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_NOT_DIRECTORY = -15LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_IS_DIRECTORY = -16LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_FILE_SYSTEM_CAPACITY_EXHAUSTED = -17LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_FILE_TOO_LARGE = -18LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_FILE_SYSTEM_CORRUPT = -19LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_FILE_SYSTEM_NOT_INITIALIZED = -20LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_FILE_PERMISSION_DENIED = -21LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_DESCRIPTOR_PERMISSION_DENIED = -22LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_DESCRIPTOR_TRANSFER_TOO_LARGE = -23LL;

enum class DirectoryEntryType : uint64_t {
    RegularFile = 1ULL,
    Directory = 2ULL,
};

struct DirectoryEntry final {
    uint64_t inodeNumber;
    DirectoryEntryType type;
    uint64_t nameLengthBytes;
    uint8_t name[OS_ABI_DIRECTORY_ENTRY_NAME_CAPACITY_BYTES];
};

static_assert(sizeof(DirectoryEntry) == OS_ABI_DIRECTORY_ENTRY_SIZE_BYTES);

}

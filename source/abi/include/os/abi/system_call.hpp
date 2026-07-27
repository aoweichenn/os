#pragma once

#include "os/abi/virtual_memory.hpp"

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
    DuplicateDescriptor = 23ULL,
    GetDescriptorFlags = 24ULL,
    SetDescriptorFlags = 25ULL,
    SetDescriptorSoftLimit = 26ULL,
    GetDescriptorSoftLimit = 27ULL,
    GetDescriptorHardLimit = 28ULL,
    ChangeDirectory = 29ULL,
    GetWorkingDirectory = 30ULL,
    UnlinkFile = 31ULL,
    RemoveDirectory = 32ULL,
    Rename = 33ULL,
    TruncateFile = 34ULL,
    StatFile = 35ULL,
    SpawnProcess = 36ULL,
    ExecProcess = 37ULL,
    WaitProcess = 38ULL,
    MapAnonymousMemory = 39ULL,
    UnmapMemory = 40ULL,
    SetProgramBreak = 41ULL,
    GetVirtualMemoryStatistics = 42ULL,
    MapFileMemory = 43ULL,
    ForkProcess = 44ULL,
};

inline constexpr uint64_t OS_ABI_SYSTEM_CALL_VECTOR = 0x80ULL;
inline constexpr uint64_t OS_ABI_SYSTEM_CALL_MAXIMUM_WRITE_SIZE_BYTES = 160ULL;
inline constexpr uint64_t OS_ABI_SYSTEM_CALL_MAXIMUM_PIPE_TRANSFER_SIZE_BYTES = 64ULL;
inline constexpr uint64_t OS_ABI_SYSTEM_CALL_MAXIMUM_FILE_TRANSFER_SIZE_BYTES = 256ULL;
inline constexpr uint64_t OS_ABI_SYSTEM_CALL_MAXIMUM_PATH_SIZE_BYTES = 4096ULL;
inline constexpr uint64_t OS_ABI_SYSTEM_CALL_MAXIMUM_DESCRIPTOR_TRANSFER_SIZE_BYTES = 256ULL;
inline constexpr uint64_t OS_ABI_STANDARD_INPUT_DESCRIPTOR = 0ULL;
inline constexpr uint64_t OS_ABI_STANDARD_OUTPUT_DESCRIPTOR = 1ULL;
inline constexpr uint64_t OS_ABI_STANDARD_ERROR_DESCRIPTOR = 2ULL;
inline constexpr uint64_t OS_ABI_FIRST_DYNAMIC_DESCRIPTOR = 3ULL;
inline constexpr uint64_t OS_ABI_FILE_DESCRIPTOR_CLOSE_ON_EXEC_FLAG = 1ULL << 0ULL;
inline constexpr uint64_t OS_ABI_FILE_DESCRIPTOR_VALID_FLAG_MASK =
    OS_ABI_FILE_DESCRIPTOR_CLOSE_ON_EXEC_FLAG;
inline constexpr uint64_t OS_ABI_DIRECTORY_ENTRY_NAME_CAPACITY_BYTES = 255ULL;
inline constexpr uint64_t OS_ABI_DIRECTORY_ENTRY_SIZE_BYTES = 280ULL;
inline constexpr uint64_t OS_ABI_PROCESS_MAXIMUM_ARGUMENT_COUNT = 256ULL;
inline constexpr uint64_t OS_ABI_PROCESS_MAXIMUM_ENVIRONMENT_COUNT = 256ULL;
inline constexpr uint64_t OS_ABI_PROCESS_MAXIMUM_ARGUMENT_ENVIRONMENT_BYTES = 128ULL * 1024ULL;
inline constexpr uint64_t OS_ABI_PROCESS_WAIT_ANY_PROCESS_ID = UINT64_MAX;
inline constexpr uint64_t OS_ABI_FILE_OPEN_READ_FLAG = 0x01ULL;
inline constexpr uint64_t OS_ABI_FILE_OPEN_WRITE_FLAG = 0x02ULL;
inline constexpr uint64_t OS_ABI_FILE_OPEN_CREATE_FLAG = 0x04ULL;
inline constexpr uint64_t OS_ABI_FILE_OPEN_TRUNCATE_FLAG = 0x08ULL;
inline constexpr uint64_t OS_ABI_FILE_OPEN_VALID_FLAG_MASK =
    OS_ABI_FILE_OPEN_READ_FLAG | OS_ABI_FILE_OPEN_WRITE_FLAG | OS_ABI_FILE_OPEN_CREATE_FLAG |
    OS_ABI_FILE_OPEN_TRUNCATE_FLAG;
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
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_DESCRIPTOR_LIMIT_EXCEEDED = -24LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_KERNEL_OBJECT_FAILURE = -25LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_PATH_TOO_LONG = -26LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_NAME_TOO_LONG = -27LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_PATH_LOOP = -28LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_READ_ONLY_FILE_SYSTEM = -29LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_DIRECTORY_NOT_EMPTY = -30LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_CROSS_DEVICE = -31LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_RESOURCE_BUSY = -32LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_OPERATION_UNSUPPORTED = -33LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_PROCESS_LIMIT_EXCEEDED = -34LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_INVALID_EXECUTABLE = -35LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_ARGUMENT_LIST_TOO_LARGE = -36LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_NO_CHILD_PROCESS = -37LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_PROCESS_IMAGE_FAILURE = -38LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_OUT_OF_MEMORY = -39LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_ADDRESS_IN_USE = -40LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_INVALID_MEMORY_RANGE = -41LL;
inline constexpr int64_t OS_ABI_SYSTEM_CALL_RESULT_MEMORY_METADATA_EXHAUSTED = -42LL;

enum class DirectoryEntryType : uint64_t {
    RegularFile = 1ULL,
    Directory = 2ULL,
};

struct DirectoryEntry final {
    uint64_t inode_number;
    DirectoryEntryType type;
    uint64_t name_length_bytes;
    uint8_t name[OS_ABI_DIRECTORY_ENTRY_NAME_CAPACITY_BYTES];
    uint8_t reserved;
};

static_assert(sizeof(DirectoryEntry) == OS_ABI_DIRECTORY_ENTRY_SIZE_BYTES);

inline constexpr uint64_t OS_ABI_FILE_INFORMATION_SIZE_BYTES = 64ULL;

struct FileInformation final {
    uint64_t mount_identifier;
    uint64_t superblock_identifier;
    uint64_t inode_number;
    uint64_t generation;
    DirectoryEntryType type;
    uint64_t size_bytes;
    uint64_t allocated_size_bytes;
    uint64_t link_count;
};

static_assert(sizeof(FileInformation) == OS_ABI_FILE_INFORMATION_SIZE_BYTES);

inline constexpr uint64_t OS_ABI_PROCESS_STRING_SIZE_BYTES = 16ULL;

struct ProcessString final {
    uint64_t address;
    uint64_t length_bytes;
};

static_assert(sizeof(ProcessString) == OS_ABI_PROCESS_STRING_SIZE_BYTES);

inline constexpr uint64_t OS_ABI_PROCESS_LAUNCH_REQUEST_SIZE_BYTES = 48ULL;

struct ProcessLaunchRequest final {
    uint64_t path_address;
    uint64_t path_length_bytes;
    uint64_t argument_vector_address;
    uint64_t argument_count;
    uint64_t environment_vector_address;
    uint64_t environment_count;
};

static_assert(sizeof(ProcessLaunchRequest) == OS_ABI_PROCESS_LAUNCH_REQUEST_SIZE_BYTES);

enum class ProcessTerminationReason : uint64_t {
    Exited = 1ULL,
    Exception = 2ULL,
};

inline constexpr uint64_t OS_ABI_PROCESS_WAIT_RESULT_SIZE_BYTES = 40ULL;

struct ProcessWaitResult final {
    uint64_t process_id;
    uint64_t parent_process_id;
    ProcessTerminationReason termination_reason;
    int64_t exit_code;
    uint64_t exception_vector;
};

static_assert(sizeof(ProcessWaitResult) == OS_ABI_PROCESS_WAIT_RESULT_SIZE_BYTES);

}

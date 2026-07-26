#include "os/kernel/user/system_calls.hpp"

#include "os/abi/system_call.hpp"
#include "os/kernel/arch/descriptor_tables.hpp"
#include "os/kernel/memory/memory_manager.hpp"
#include "os/kernel/process/process_runtime.hpp"
#include "os/kernel/arch/processor.hpp"
#include "os/kernel/device/serial_port.hpp"
#include "os/kernel/user/user_elf.hpp"
#include "os/kernel/user/user_memory.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_SYSTEM_CALL_EMPTY_WRITE_SIZE_BYTES = 0ULL;
constexpr uint64_t OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES = 0ULL;
constexpr uint64_t OS_KERNEL_SYSTEM_CALL_EMPTY_WAKE_COUNT = 0ULL;
constexpr uint64_t OS_KERNEL_SYSTEM_CALL_FIRST_BYTE_INDEX = 0ULL;
constexpr uint64_t OS_KERNEL_SYSTEM_CALL_ADDRESS_PROBE_SIZE_BYTES = 1ULL;
constexpr uint64_t OS_KERNEL_SYSTEM_CALL_WAKE_ALL_THREAD_COUNT =
    OS_KERNEL_THREAD_CAPACITY_LIMIT;
constexpr int64_t OS_KERNEL_SYSTEM_CALL_EMPTY_WRITE_RESULT = 0LL;
constexpr int64_t OS_KERNEL_SYSTEM_CALL_PIPE_SUCCESS_RESULT = 0LL;
constexpr int64_t OS_KERNEL_SYSTEM_CALL_FILE_SYSTEM_SUCCESS_RESULT = 0LL;
constexpr int64_t OS_KERNEL_SYSTEM_CALL_DESCRIPTOR_SUCCESS_RESULT = 0LL;
constexpr int64_t OS_KERNEL_SYSTEM_CALL_DIRECTORY_ENTRY_RESULT = 1LL;

[[nodiscard]] int64_t MapPipeStatus(const PipeStatus status,
                                    const uint64_t transferred_bytes) noexcept {
    if (status == PipeStatus::Succeeded) {
        return static_cast<int64_t>(transferred_bytes);
    }
    if (status == PipeStatus::WouldBlock) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_WOULD_BLOCK;
    }
    if (status == PipeStatus::EndOfFile) {
        return OS_KERNEL_SYSTEM_CALL_PIPE_SUCCESS_RESULT;
    }
    if (status == PipeStatus::BrokenPipe) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_BROKEN_PIPE;
    }
    if (status == PipeStatus::AlreadyClosed) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_ENDPOINT_CLOSED;
    }
    return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_ARGUMENT;
}

[[nodiscard]] int64_t MapFileSystemStatus(const FileSystemStatus status,
                                          const uint64_t success_value) noexcept {
    if (status == FileSystemStatus::Succeeded) {
        return static_cast<int64_t>(success_value);
    }
    if (status == FileSystemStatus::InvalidHandle) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_FILE_DESCRIPTOR;
    }
    if (status == FileSystemStatus::NotFound) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_FILE_NOT_FOUND;
    }
    if (status == FileSystemStatus::AlreadyExists) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_FILE_ALREADY_EXISTS;
    }
    if (status == FileSystemStatus::NotDirectory) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_NOT_DIRECTORY;
    }
    if (status == FileSystemStatus::IsDirectory) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_IS_DIRECTORY;
    }
    if (status == FileSystemStatus::InodeCapacityExhausted ||
        status == FileSystemStatus::DataCapacityExhausted ||
        status == FileSystemStatus::DirectoryCapacityExhausted) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_FILE_SYSTEM_CAPACITY_EXHAUSTED;
    }
    if (status == FileSystemStatus::FileTooLarge) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_FILE_TOO_LARGE;
    }
    if (status == FileSystemStatus::Corrupt || status == FileSystemStatus::IncompleteTransaction) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_FILE_SYSTEM_CORRUPT;
    }
    if (status == FileSystemStatus::NotInitialized) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_FILE_SYSTEM_NOT_INITIALIZED;
    }
    if (status == FileSystemStatus::PermissionDenied) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_FILE_PERMISSION_DENIED;
    }
    if (status == FileSystemStatus::DeviceFailure) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_DEVICE_FAILURE;
    }
    return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_ARGUMENT;
}

[[nodiscard]] int64_t MapProcessIoStatus(const ProcessIoStatus status,
                                         const uint64_t transferred_bytes,
                                         const FileSystemStatus file_system_status) noexcept {
    if (status == ProcessIoStatus::Succeeded) {
        return static_cast<int64_t>(transferred_bytes);
    }
    if (status == ProcessIoStatus::WouldBlock) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_WOULD_BLOCK;
    }
    if (status == ProcessIoStatus::EndOfFile) {
        return OS_KERNEL_SYSTEM_CALL_DESCRIPTOR_SUCCESS_RESULT;
    }
    if (status == ProcessIoStatus::BrokenPipe) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_BROKEN_PIPE;
    }
    if (status == ProcessIoStatus::InvalidDescriptor) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_FILE_DESCRIPTOR;
    }
    if (status == ProcessIoStatus::PermissionDenied) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_DESCRIPTOR_PERMISSION_DENIED;
    }
    if (status == ProcessIoStatus::DeviceFailure) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_DEVICE_FAILURE;
    }
    if (status == ProcessIoStatus::FileSystemFailure) {
        return MapFileSystemStatus(file_system_status, transferred_bytes);
    }
    return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_ARGUMENT;
}

[[nodiscard]] bool ValidateUserSystemCallFrame(const ExceptionFrame &frame) noexcept {
    if (!IsProcessSchedulingActive() || !FrameOriginatedFromUser(frame) ||
        frame.vector != os::abi::OS_ABI_SYSTEM_CALL_VECTOR ||
        frame.code_segment != static_cast<uint64_t>(OS_KERNEL_DESCRIPTOR_USER_CODE_SELECTOR)) {
        return false;
    }
    const UserPrivilegeFrame &user_frame = AsUserPrivilegeFrame(frame);
    if (user_frame.user_stack_segment !=
            static_cast<uint64_t>(OS_KERNEL_DESCRIPTOR_USER_DATA_SELECTOR) ||
        !IsUserProgramVirtualAddressRange(frame.instruction_pointer,
                                          OS_KERNEL_SYSTEM_CALL_ADDRESS_PROBE_SIZE_BYTES) ||
        user_frame.user_stack_pointer < OS_KERNEL_USER_STACK_BOTTOM_VIRTUAL_ADDRESS ||
        user_frame.user_stack_pointer >= OS_KERNEL_USER_STACK_TOP_VIRTUAL_ADDRESS) {
        return false;
    }
    PageMapping instruction_mapping{};
    PageMapping stack_mapping{};
    return QueryActivePage(frame.instruction_pointer, instruction_mapping) ==
               PageTableStatus::Succeeded &&
           instruction_mapping.permissions.user_accessible &&
           instruction_mapping.permissions.executable &&
           !instruction_mapping.permissions.writable &&
           QueryActivePage(user_frame.user_stack_pointer, stack_mapping) ==
               PageTableStatus::Succeeded &&
           stack_mapping.permissions.user_accessible && stack_mapping.permissions.writable &&
           !stack_mapping.permissions.executable;
}

[[nodiscard]] int64_t DispatchWriteLog(const uint64_t user_address,
                                       const uint64_t length_bytes) noexcept {
    if (length_bytes == OS_KERNEL_SYSTEM_CALL_EMPTY_WRITE_SIZE_BYTES) {
        return OS_KERNEL_SYSTEM_CALL_EMPTY_WRITE_RESULT;
    }
    if (length_bytes > os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_WRITE_SIZE_BYTES) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_WRITE_TOO_LARGE;
    }
    uint8_t message[os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_WRITE_SIZE_BYTES]{};
    if (CopyFromUser(user_address, length_bytes, message,
                     os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_WRITE_SIZE_BYTES) !=
        UserMemoryCopyStatus::Succeeded) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_USER_MEMORY;
    }
    const SerialPort serial_port{OS_KERNEL_SERIAL_COM1_BASE_PORT};
    for (uint64_t byte_index = OS_KERNEL_SYSTEM_CALL_FIRST_BYTE_INDEX; byte_index < length_bytes;
         ++byte_index) {
        if (!serial_port.TryWriteByte(static_cast<char>(message[byte_index]))) {
            return os::abi::OS_ABI_SYSTEM_CALL_RESULT_DEVICE_FAILURE;
        }
    }
    return static_cast<int64_t>(length_bytes);
}

void WakePipeWaiters(const WaitCondition wait_condition) noexcept {
    uint64_t woken_thread_count = OS_KERNEL_SYSTEM_CALL_EMPTY_WAKE_COUNT;
    if (WakeThreads(wait_condition, WakeReason::ConditionSatisfied,
                    OS_KERNEL_SYSTEM_CALL_WAKE_ALL_THREAD_COUNT,
                    woken_thread_count) != ProcessRuntimeStatus::Succeeded) {
        HaltProcessor();
    }
}

[[nodiscard]] int64_t DispatchTryReadPipe(const uint64_t user_address,
                                          const uint64_t capacity_bytes) noexcept {
    if (!CurrentProcessCanReadPipe()) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_PIPE_PERMISSION_DENIED;
    }
    if (capacity_bytes == OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_ARGUMENT;
    }
    if (capacity_bytes > os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_PIPE_TRANSFER_SIZE_BYTES) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_PIPE_TRANSFER_TOO_LARGE;
    }
    if (ValidateUserWritableMemory(user_address, capacity_bytes) !=
        UserMemoryCopyStatus::Succeeded) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_USER_MEMORY;
    }

    uint8_t buffer[os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_PIPE_TRANSFER_SIZE_BYTES]{};
    uint64_t read_bytes = OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES;
    const PipeStatus status = TryReadCurrentProcessPipe(buffer, capacity_bytes, read_bytes);
    if (status == PipeStatus::Succeeded) {
        if (CopyToUser(user_address, read_bytes, buffer, capacity_bytes) !=
            UserMemoryCopyStatus::Succeeded) {
            HaltProcessor();
        }
        WakePipeWaiters(WaitCondition::PipeWritable);
    }
    return MapPipeStatus(status, read_bytes);
}

[[nodiscard]] int64_t DispatchTryWritePipe(const uint64_t user_address,
                                           const uint64_t length_bytes) noexcept {
    if (!CurrentProcessCanWritePipe()) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_PIPE_PERMISSION_DENIED;
    }
    if (length_bytes == OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_ARGUMENT;
    }
    if (length_bytes > os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_PIPE_TRANSFER_SIZE_BYTES) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_PIPE_TRANSFER_TOO_LARGE;
    }

    uint8_t buffer[os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_PIPE_TRANSFER_SIZE_BYTES]{};
    if (CopyFromUser(user_address, length_bytes, buffer,
                     os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_PIPE_TRANSFER_SIZE_BYTES) !=
        UserMemoryCopyStatus::Succeeded) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_USER_MEMORY;
    }
    uint64_t written_bytes = OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES;
    const PipeStatus status = TryWriteCurrentProcessPipe(buffer, length_bytes, written_bytes);
    if (status == PipeStatus::Succeeded) {
        WakePipeWaiters(WaitCondition::PipeReadable);
    }
    return MapPipeStatus(status, written_bytes);
}

[[nodiscard]] ExceptionFrame *DispatchWaitPipe(ExceptionFrame &frame,
                                               const WaitCondition wait_condition) noexcept {
    const bool can_progress = wait_condition == WaitCondition::PipeReadable
                                  ? ProcessPipeReadCanProgress()
                                  : ProcessPipeWriteCanProgress();
    if (can_progress) {
        frame.register_rax = static_cast<uint64_t>(OS_KERNEL_SYSTEM_CALL_PIPE_SUCCESS_RESULT);
        return &frame;
    }

    frame.register_rax = static_cast<uint64_t>(OS_KERNEL_SYSTEM_CALL_PIPE_SUCCESS_RESULT);
    ExceptionFrame *resume_frame = &frame;
    const ProcessRuntimeStatus status =
        BlockCurrentThread(frame, wait_condition, resume_frame);
    if (status == ProcessRuntimeStatus::NoReadyThread) {
        frame.register_rax =
            static_cast<uint64_t>(os::abi::OS_ABI_SYSTEM_CALL_RESULT_NO_READY_PROCESS);
        return &frame;
    }
    if (status != ProcessRuntimeStatus::Succeeded) {
        HaltProcessor();
    }
    return resume_frame;
}

[[nodiscard]] int64_t DispatchClosePipeReader() noexcept {
    if (!CurrentProcessCanReadPipe()) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_PIPE_PERMISSION_DENIED;
    }
    const PipeStatus status = CloseCurrentProcessPipeReader();
    return MapPipeStatus(status, OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES);
}

[[nodiscard]] int64_t DispatchClosePipeWriter() noexcept {
    if (!CurrentProcessCanWritePipe()) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_PIPE_PERMISSION_DENIED;
    }
    const PipeStatus status = CloseCurrentProcessPipeWriter();
    return MapPipeStatus(status, OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES);
}

[[nodiscard]] int64_t DispatchOpenFile(const uint64_t user_path_address,
                                       const uint64_t path_length_bytes,
                                       const uint64_t open_flags) noexcept {
    if (path_length_bytes == OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES ||
        path_length_bytes > os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_PATH_SIZE_BYTES ||
        (open_flags & ~os::abi::OS_ABI_FILE_OPEN_VALID_FLAG_MASK) !=
            OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_ARGUMENT;
    }
    uint8_t path[os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_PATH_SIZE_BYTES]{};
    if (CopyFromUser(user_path_address, path_length_bytes, path,
                     os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_PATH_SIZE_BYTES) !=
        UserMemoryCopyStatus::Succeeded) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_USER_MEMORY;
    }
    const FileSystemOpenOptions options{
        .readable = (open_flags & os::abi::OS_ABI_FILE_OPEN_READ_FLAG) !=
                    OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES,
        .writable = (open_flags & os::abi::OS_ABI_FILE_OPEN_WRITE_FLAG) !=
                    OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES,
        .create = (open_flags & os::abi::OS_ABI_FILE_OPEN_CREATE_FLAG) !=
                  OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES,
        .truncate = (open_flags & os::abi::OS_ABI_FILE_OPEN_TRUNCATE_FLAG) !=
                    OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES,
    };
    uint64_t file_descriptor = OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES;
    const FileSystemStatus status =
        OpenCurrentProcessFile(path, path_length_bytes, options, file_descriptor);
    return MapFileSystemStatus(status, file_descriptor);
}

[[nodiscard]] int64_t DispatchReadFile(const uint64_t file_descriptor, const uint64_t user_address,
                                       const uint64_t capacity_bytes) noexcept {
    if (capacity_bytes > os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_FILE_TRANSFER_SIZE_BYTES) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_FILE_TOO_LARGE;
    }
    if (capacity_bytes != OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES &&
        ValidateUserWritableMemory(user_address, capacity_bytes) !=
            UserMemoryCopyStatus::Succeeded) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_USER_MEMORY;
    }
    uint8_t buffer[os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_FILE_TRANSFER_SIZE_BYTES]{};
    uint64_t read_bytes = OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES;
    const FileSystemStatus status =
        ReadCurrentProcessFile(file_descriptor, buffer, capacity_bytes, read_bytes);
    if (status == FileSystemStatus::Succeeded &&
        read_bytes != OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES &&
        CopyToUser(user_address, read_bytes, buffer, capacity_bytes) !=
            UserMemoryCopyStatus::Succeeded) {
        HaltProcessor();
    }
    return MapFileSystemStatus(status, read_bytes);
}

[[nodiscard]] int64_t DispatchWriteFile(const uint64_t file_descriptor, const uint64_t user_address,
                                        const uint64_t length_bytes) noexcept {
    if (length_bytes > os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_FILE_TRANSFER_SIZE_BYTES) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_FILE_TOO_LARGE;
    }
    uint8_t buffer[os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_FILE_TRANSFER_SIZE_BYTES]{};
    if (length_bytes != OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES &&
        CopyFromUser(user_address, length_bytes, buffer,
                     os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_FILE_TRANSFER_SIZE_BYTES) !=
            UserMemoryCopyStatus::Succeeded) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_USER_MEMORY;
    }
    uint64_t written_bytes = OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES;
    const FileSystemStatus status =
        WriteCurrentProcessFile(file_descriptor, buffer, length_bytes, written_bytes);
    return MapFileSystemStatus(status, written_bytes);
}

[[nodiscard]] int64_t DispatchCloseFile(const uint64_t file_descriptor) noexcept {
    return MapFileSystemStatus(
        CloseCurrentProcessFile(file_descriptor),
        static_cast<uint64_t>(OS_KERNEL_SYSTEM_CALL_FILE_SYSTEM_SUCCESS_RESULT));
}

[[nodiscard]] int64_t DispatchCreateDirectory(const uint64_t user_path_address,
                                              const uint64_t path_length_bytes) noexcept {
    if (path_length_bytes == OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES ||
        path_length_bytes > os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_PATH_SIZE_BYTES) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_ARGUMENT;
    }
    uint8_t path[os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_PATH_SIZE_BYTES]{};
    if (CopyFromUser(user_path_address, path_length_bytes, path,
                     os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_PATH_SIZE_BYTES) !=
        UserMemoryCopyStatus::Succeeded) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_USER_MEMORY;
    }
    return MapFileSystemStatus(
        CreateCurrentProcessDirectory(path, path_length_bytes),
        static_cast<uint64_t>(OS_KERNEL_SYSTEM_CALL_FILE_SYSTEM_SUCCESS_RESULT));
}

[[nodiscard]] int64_t DispatchSyncFileSystem() noexcept {
    return MapFileSystemStatus(
        SyncCurrentProcessFileSystem(),
        static_cast<uint64_t>(OS_KERNEL_SYSTEM_CALL_FILE_SYSTEM_SUCCESS_RESULT));
}

[[nodiscard]] int64_t DispatchTryReadDescriptor(const uint64_t descriptor,
                                                const uint64_t user_address,
                                                const uint64_t capacity_bytes) noexcept {
    if (capacity_bytes == OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES) {
        return OS_KERNEL_SYSTEM_CALL_DESCRIPTOR_SUCCESS_RESULT;
    }
    if (capacity_bytes > os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_DESCRIPTOR_TRANSFER_SIZE_BYTES) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_DESCRIPTOR_TRANSFER_TOO_LARGE;
    }
    if (ValidateUserWritableMemory(user_address, capacity_bytes) !=
        UserMemoryCopyStatus::Succeeded) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_USER_MEMORY;
    }
    uint8_t buffer[os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_DESCRIPTOR_TRANSFER_SIZE_BYTES]{};
    uint64_t read_bytes = OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES;
    FileSystemStatus file_system_status = FileSystemStatus::Succeeded;
    const ProcessIoStatus status = TryReadCurrentProcessDescriptor(
        descriptor, buffer, capacity_bytes, read_bytes, file_system_status);
    if (status == ProcessIoStatus::Succeeded &&
        read_bytes != OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES &&
        CopyToUser(user_address, read_bytes, buffer, capacity_bytes) !=
            UserMemoryCopyStatus::Succeeded) {
        HaltProcessor();
    }
    return MapProcessIoStatus(status, read_bytes, file_system_status);
}

[[nodiscard]] int64_t DispatchTryWriteDescriptor(const uint64_t descriptor,
                                                 const uint64_t user_address,
                                                 const uint64_t length_bytes) noexcept {
    if (length_bytes == OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES) {
        return OS_KERNEL_SYSTEM_CALL_DESCRIPTOR_SUCCESS_RESULT;
    }
    if (length_bytes > os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_DESCRIPTOR_TRANSFER_SIZE_BYTES) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_DESCRIPTOR_TRANSFER_TOO_LARGE;
    }
    uint8_t buffer[os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_DESCRIPTOR_TRANSFER_SIZE_BYTES]{};
    if (CopyFromUser(user_address, length_bytes, buffer,
                     os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_DESCRIPTOR_TRANSFER_SIZE_BYTES) !=
        UserMemoryCopyStatus::Succeeded) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_USER_MEMORY;
    }
    uint64_t written_bytes = OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES;
    FileSystemStatus file_system_status = FileSystemStatus::Succeeded;
    const ProcessIoStatus status = TryWriteCurrentProcessDescriptor(
        descriptor, buffer, length_bytes, written_bytes, file_system_status);
    return MapProcessIoStatus(status, written_bytes, file_system_status);
}

[[nodiscard]] ExceptionFrame *DispatchWaitDescriptor(ExceptionFrame &frame,
                                                     const uint64_t descriptor,
                                                     const bool wait_for_read) noexcept {
    bool can_progress = false;
    const ProcessIoStatus io_status =
        wait_for_read ? CurrentProcessDescriptorReadCanProgress(descriptor, can_progress)
                      : CurrentProcessDescriptorWriteCanProgress(descriptor, can_progress);
    if (io_status != ProcessIoStatus::Succeeded) {
        frame.register_rax = static_cast<uint64_t>(
            MapProcessIoStatus(io_status, OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES,
                               FileSystemStatus::Succeeded));
        return &frame;
    }
    frame.register_rax = static_cast<uint64_t>(OS_KERNEL_SYSTEM_CALL_DESCRIPTOR_SUCCESS_RESULT);
    if (can_progress) {
        return &frame;
    }
    ExceptionFrame *resume_frame = &frame;
    const ProcessRuntimeStatus block_status =
        BlockCurrentThread(frame,
                           wait_for_read ? WaitCondition::DescriptorReadable
                                         : WaitCondition::DescriptorWritable,
                           resume_frame);
    if (block_status != ProcessRuntimeStatus::Succeeded) {
        frame.register_rax =
            static_cast<uint64_t>(block_status == ProcessRuntimeStatus::NoReadyThread
                                      ? os::abi::OS_ABI_SYSTEM_CALL_RESULT_NO_READY_PROCESS
                                      : os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_ARGUMENT);
        return &frame;
    }
    return resume_frame;
}

[[nodiscard]] int64_t DispatchCloseDescriptor(const uint64_t descriptor) noexcept {
    FileSystemStatus file_system_status = FileSystemStatus::Succeeded;
    const ProcessIoStatus status = CloseCurrentProcessDescriptor(descriptor, file_system_status);
    return MapProcessIoStatus(
        status, static_cast<uint64_t>(OS_KERNEL_SYSTEM_CALL_DESCRIPTOR_SUCCESS_RESULT),
        file_system_status);
}

[[nodiscard]] int64_t DispatchOpenDirectory(const uint64_t user_path_address,
                                            const uint64_t path_length_bytes) noexcept {
    if (path_length_bytes == OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES ||
        path_length_bytes > os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_PATH_SIZE_BYTES) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_ARGUMENT;
    }
    uint8_t path[os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_PATH_SIZE_BYTES]{};
    if (CopyFromUser(user_path_address, path_length_bytes, path,
                     os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_PATH_SIZE_BYTES) !=
        UserMemoryCopyStatus::Succeeded) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_USER_MEMORY;
    }
    uint64_t descriptor = OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES;
    return MapFileSystemStatus(OpenCurrentProcessDirectory(path, path_length_bytes, descriptor),
                               descriptor);
}

[[nodiscard]] int64_t DispatchReadDirectory(const uint64_t descriptor,
                                            const uint64_t user_entry_address,
                                            const uint64_t entry_size_bytes) noexcept {
    if (entry_size_bytes != sizeof(os::abi::DirectoryEntry)) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_ARGUMENT;
    }
    if (ValidateUserWritableMemory(user_entry_address, entry_size_bytes) !=
        UserMemoryCopyStatus::Succeeded) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_USER_MEMORY;
    }
    FileSystemDirectoryEntry file_system_entry{};
    bool end_of_directory = false;
    const FileSystemStatus status =
        ReadCurrentProcessDirectory(descriptor, file_system_entry, end_of_directory);
    if (status != FileSystemStatus::Succeeded) {
        return MapFileSystemStatus(
            status, static_cast<uint64_t>(OS_KERNEL_SYSTEM_CALL_DIRECTORY_ENTRY_RESULT));
    }
    if (end_of_directory) {
        return OS_KERNEL_SYSTEM_CALL_DESCRIPTOR_SUCCESS_RESULT;
    }
    os::abi::DirectoryEntry entry{
        .inode_number = file_system_entry.inode_number,
        .type = file_system_entry.type == FileSystemNodeType::Directory
                    ? os::abi::DirectoryEntryType::Directory
                    : os::abi::DirectoryEntryType::RegularFile,
        .name_length_bytes = file_system_entry.name_length_bytes,
        .name = {},
    };
    for (uint64_t byte_index = OS_KERNEL_SYSTEM_CALL_FIRST_BYTE_INDEX;
         byte_index < file_system_entry.name_length_bytes; ++byte_index) {
        entry.name[byte_index] = file_system_entry.name[byte_index];
    }
    if (CopyToUser(user_entry_address, sizeof(entry), reinterpret_cast<const uint8_t *>(&entry),
                   entry_size_bytes) != UserMemoryCopyStatus::Succeeded) {
        HaltProcessor();
    }
    return OS_KERNEL_SYSTEM_CALL_DIRECTORY_ENTRY_RESULT;
}
}

extern "C" ExceptionFrame *OsKernelDispatchSystemCall(ExceptionFrame *frame) noexcept {
    if (frame == nullptr || !ValidateUserSystemCallFrame(*frame)) {
        HaltProcessor();
    }
    RecordCurrentProcessSystemCall();

    const uint64_t system_call_number = frame->register_rax;
    if (system_call_number == static_cast<uint64_t>(os::abi::SystemCallNumber::WriteLog)) {
        frame->register_rax =
            static_cast<uint64_t>(DispatchWriteLog(frame->register_rdi, frame->register_rsi));
        return frame;
    }
    if (system_call_number == static_cast<uint64_t>(os::abi::SystemCallNumber::ExitProcess)) {
        return TerminateCurrentProcessFromExit(*frame, static_cast<int64_t>(frame->register_rdi));
    }
    if (system_call_number == static_cast<uint64_t>(os::abi::SystemCallNumber::GetProcessId)) {
        frame->register_rax = CurrentProcessId();
        return frame;
    }
    if (system_call_number == static_cast<uint64_t>(os::abi::SystemCallNumber::TryReadPipe)) {
        frame->register_rax =
            static_cast<uint64_t>(DispatchTryReadPipe(frame->register_rdi, frame->register_rsi));
        return frame;
    }
    if (system_call_number == static_cast<uint64_t>(os::abi::SystemCallNumber::TryWritePipe)) {
        frame->register_rax =
            static_cast<uint64_t>(DispatchTryWritePipe(frame->register_rdi, frame->register_rsi));
        return frame;
    }
    if (system_call_number == static_cast<uint64_t>(os::abi::SystemCallNumber::WaitPipeReadable)) {
        if (!CurrentProcessCanReadPipe()) {
            frame->register_rax =
                static_cast<uint64_t>(os::abi::OS_ABI_SYSTEM_CALL_RESULT_PIPE_PERMISSION_DENIED);
            return frame;
        }
        return DispatchWaitPipe(*frame, WaitCondition::PipeReadable);
    }
    if (system_call_number == static_cast<uint64_t>(os::abi::SystemCallNumber::WaitPipeWritable)) {
        if (!CurrentProcessCanWritePipe()) {
            frame->register_rax =
                static_cast<uint64_t>(os::abi::OS_ABI_SYSTEM_CALL_RESULT_PIPE_PERMISSION_DENIED);
            return frame;
        }
        return DispatchWaitPipe(*frame, WaitCondition::PipeWritable);
    }
    if (system_call_number == static_cast<uint64_t>(os::abi::SystemCallNumber::ClosePipeReader)) {
        frame->register_rax = static_cast<uint64_t>(DispatchClosePipeReader());
        return frame;
    }
    if (system_call_number == static_cast<uint64_t>(os::abi::SystemCallNumber::ClosePipeWriter)) {
        frame->register_rax = static_cast<uint64_t>(DispatchClosePipeWriter());
        return frame;
    }
    if (system_call_number == static_cast<uint64_t>(os::abi::SystemCallNumber::OpenFile)) {
        frame->register_rax = static_cast<uint64_t>(
            DispatchOpenFile(frame->register_rdi, frame->register_rsi, frame->register_rdx));
        return frame;
    }
    if (system_call_number == static_cast<uint64_t>(os::abi::SystemCallNumber::ReadFile)) {
        frame->register_rax = static_cast<uint64_t>(
            DispatchReadFile(frame->register_rdi, frame->register_rsi, frame->register_rdx));
        return frame;
    }
    if (system_call_number == static_cast<uint64_t>(os::abi::SystemCallNumber::WriteFile)) {
        frame->register_rax = static_cast<uint64_t>(
            DispatchWriteFile(frame->register_rdi, frame->register_rsi, frame->register_rdx));
        return frame;
    }
    if (system_call_number == static_cast<uint64_t>(os::abi::SystemCallNumber::CloseFile)) {
        frame->register_rax = static_cast<uint64_t>(DispatchCloseFile(frame->register_rdi));
        return frame;
    }
    if (system_call_number == static_cast<uint64_t>(os::abi::SystemCallNumber::CreateDirectory)) {
        frame->register_rax = static_cast<uint64_t>(
            DispatchCreateDirectory(frame->register_rdi, frame->register_rsi));
        return frame;
    }
    if (system_call_number == static_cast<uint64_t>(os::abi::SystemCallNumber::SyncFileSystem)) {
        frame->register_rax = static_cast<uint64_t>(DispatchSyncFileSystem());
        return frame;
    }
    if (system_call_number == static_cast<uint64_t>(os::abi::SystemCallNumber::TryReadDescriptor)) {
        frame->register_rax = static_cast<uint64_t>(DispatchTryReadDescriptor(
            frame->register_rdi, frame->register_rsi, frame->register_rdx));
        return frame;
    }
    if (system_call_number ==
        static_cast<uint64_t>(os::abi::SystemCallNumber::TryWriteDescriptor)) {
        frame->register_rax = static_cast<uint64_t>(DispatchTryWriteDescriptor(
            frame->register_rdi, frame->register_rsi, frame->register_rdx));
        return frame;
    }
    if (system_call_number ==
        static_cast<uint64_t>(os::abi::SystemCallNumber::WaitDescriptorReadable)) {
        return DispatchWaitDescriptor(*frame, frame->register_rdi, true);
    }
    if (system_call_number ==
        static_cast<uint64_t>(os::abi::SystemCallNumber::WaitDescriptorWritable)) {
        return DispatchWaitDescriptor(*frame, frame->register_rdi, false);
    }
    if (system_call_number == static_cast<uint64_t>(os::abi::SystemCallNumber::CloseDescriptor)) {
        frame->register_rax = static_cast<uint64_t>(DispatchCloseDescriptor(frame->register_rdi));
        return frame;
    }
    if (system_call_number == static_cast<uint64_t>(os::abi::SystemCallNumber::OpenDirectory)) {
        frame->register_rax =
            static_cast<uint64_t>(DispatchOpenDirectory(frame->register_rdi, frame->register_rsi));
        return frame;
    }
    if (system_call_number == static_cast<uint64_t>(os::abi::SystemCallNumber::ReadDirectory)) {
        frame->register_rax = static_cast<uint64_t>(
            DispatchReadDirectory(frame->register_rdi, frame->register_rsi, frame->register_rdx));
        return frame;
    }
    frame->register_rax = static_cast<uint64_t>(os::abi::OS_ABI_SYSTEM_CALL_RESULT_UNKNOWN_NUMBER);
    return frame;
}

}

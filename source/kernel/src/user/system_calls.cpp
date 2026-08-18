#include "os/kernel/user/system_calls.hpp"

#include "os/abi/system_call.hpp"
#include "os/abi/time.hpp"
#include "os/kernel/arch/cpu_local.hpp"
#include "os/kernel/arch/descriptor_tables.hpp"
#include "os/kernel/arch/interrupt_runtime.hpp"
#include "os/kernel/arch/native_system_call.hpp"
#include "os/kernel/arch/processor.hpp"
#include "os/kernel/arch/user_context.hpp"
#include "os/kernel/memory/memory_manager.hpp"
#include "os/kernel/process/process_runtime.hpp"
#include "os/kernel/user/user_elf.hpp"
#include "os/kernel/user/user_memory.hpp"
#include <os/kernel/device/port_io.hpp>
#include <os/kernel/device/vga_text_console.hpp>

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_SYSTEM_CALL_EMPTY_WRITE_SIZE_BYTES = 0ULL;
constexpr uint64_t OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES = 0ULL;
constexpr uint64_t OS_KERNEL_SYSTEM_CALL_EMPTY_WAKE_COUNT = 0ULL;
constexpr uint64_t OS_KERNEL_SYSTEM_CALL_FIRST_BYTE_INDEX = 0ULL;
constexpr uint64_t OS_KERNEL_SYSTEM_CALL_ADDRESS_PROBE_SIZE_BYTES = 1ULL;
constexpr uint64_t OS_KERNEL_SYSTEM_CALL_STACK_PROBE_SIZE_BYTES = 1ULL;
constexpr uint64_t OS_KERNEL_SYSTEM_CALL_WAKE_ALL_THREAD_COUNT = OS_KERNEL_THREAD_CAPACITY_LIMIT;
constexpr uint64_t OS_KERNEL_SYSTEM_CALL_ATA_FLUSH_TIMEOUT_MILLISECONDS = 500ULL;
constexpr uint64_t OS_KERNEL_SYSTEM_CALL_ATA_FLUSH_TIMEOUT_NANOSECONDS =
    OS_KERNEL_SYSTEM_CALL_ATA_FLUSH_TIMEOUT_MILLISECONDS *
    os::abi::OS_ABI_TIME_NANOSECONDS_PER_MILLISECOND;
constexpr uint64_t OS_KERNEL_SYSTEM_CALL_EMPTY_REQUEST_IDENTIFIER = 0ULL;
constexpr uint8_t OS_KERNEL_SYSTEM_CALL_DIRECTORY_ENTRY_RESERVED_VALUE = 0U;
constexpr int64_t OS_KERNEL_SYSTEM_CALL_EMPTY_WRITE_RESULT = 0LL;
constexpr int64_t OS_KERNEL_SYSTEM_CALL_PIPE_SUCCESS_RESULT = 0LL;
constexpr int64_t OS_KERNEL_SYSTEM_CALL_FILE_SYSTEM_SUCCESS_RESULT = 0LL;
constexpr int64_t OS_KERNEL_SYSTEM_CALL_DESCRIPTOR_SUCCESS_RESULT = 0LL;
constexpr int64_t OS_KERNEL_SYSTEM_CALL_DIRECTORY_ENTRY_RESULT = 1LL;
constexpr char OS_KERNEL_SYSTEM_CALL_REJECTED_RETURN_MESSAGE[] =
    "[OS][KERNEL] USER_RETURN_REJECTED\r\n";
constexpr char OS_KERNEL_SYSTEM_CALL_ATA_FLUSH_SUBMIT_REQUEST_PREFIX[] =
    "[OS][KERNEL][BLOCK] FLUSH_SUBMIT_REQUEST=";
constexpr char OS_KERNEL_SYSTEM_CALL_ATA_FLUSH_SUBMIT_TIME_PREFIX[] =
    "[OS][KERNEL][BLOCK] FLUSH_SUBMIT_TIME_NS=";
constexpr char OS_KERNEL_SYSTEM_CALL_ATA_FLUSH_SUBMIT_STATUS_PREFIX[] =
    "[OS][KERNEL][BLOCK] FLUSH_SUBMIT_STATUS=";
constexpr char OS_KERNEL_SYSTEM_CALL_ATA_FLUSH_IMMEDIATE_RESULT_PREFIX[] =
    "[OS][KERNEL][BLOCK] FLUSH_IMMEDIATE_RESULT=";
constexpr char OS_KERNEL_SYSTEM_CALL_REJECTED_RETURN_OWNERSHIP_PREFIX[] =
    "[OS][KERNEL] USER_RETURN_OWNED=";
constexpr char OS_KERNEL_SYSTEM_CALL_REJECTED_RETURN_STATUS_PREFIX[] =
    "[OS][KERNEL] USER_RETURN_STATUS=";
constexpr char OS_KERNEL_SYSTEM_CALL_REJECTED_RETURN_MEMORY_PREFIX[] =
    "[OS][KERNEL] USER_RETURN_MEMORY_VALID=";
constexpr char OS_KERNEL_SYSTEM_CALL_REJECTED_RETURN_ENTRY_PREFIX[] =
    "[OS][KERNEL] USER_RETURN_ENTRY_METHOD=";
constexpr char OS_KERNEL_SYSTEM_CALL_REJECTED_RETURN_VECTOR_PREFIX[] =
    "[OS][KERNEL] USER_RETURN_VECTOR=";
constexpr char OS_KERNEL_SYSTEM_CALL_REJECTED_RETURN_RIP_PREFIX[] = "[OS][KERNEL] USER_RETURN_RIP=";
constexpr char OS_KERNEL_SYSTEM_CALL_REJECTED_RETURN_RSP_PREFIX[] = "[OS][KERNEL] USER_RETURN_RSP=";
constexpr char OS_KERNEL_SYSTEM_CALL_REJECTED_RETURN_FLAGS_PREFIX[] =
    "[OS][KERNEL] USER_RETURN_RFLAGS=";
constexpr char OS_KERNEL_SYSTEM_CALL_REJECTED_RETURN_INSTRUCTION_PAGE_STATUS_PREFIX[] =
    "[OS][KERNEL] USER_RETURN_INSTRUCTION_PAGE_STATUS=";
constexpr char OS_KERNEL_SYSTEM_CALL_REJECTED_RETURN_INSTRUCTION_PAGE_FLAGS_PREFIX[] =
    "[OS][KERNEL] USER_RETURN_INSTRUCTION_PAGE_FLAGS=";
constexpr char OS_KERNEL_SYSTEM_CALL_REJECTED_RETURN_STACK_PAGE_STATUS_PREFIX[] =
    "[OS][KERNEL] USER_RETURN_STACK_PAGE_STATUS=";
constexpr char OS_KERNEL_SYSTEM_CALL_REJECTED_RETURN_STACK_PAGE_FLAGS_PREFIX[] =
    "[OS][KERNEL] USER_RETURN_STACK_PAGE_FLAGS=";
constexpr uint64_t OS_KERNEL_SYSTEM_CALL_BOOLEAN_TRUE = 1ULL;
constexpr uint64_t OS_KERNEL_SYSTEM_CALL_PAGE_WRITABLE_FLAG = 1ULL << 0ULL;
constexpr uint64_t OS_KERNEL_SYSTEM_CALL_PAGE_EXECUTABLE_FLAG = 1ULL << 1ULL;
constexpr uint64_t OS_KERNEL_SYSTEM_CALL_PAGE_USER_FLAG = 1ULL << 2ULL;
constexpr uint64_t OS_KERNEL_SYSTEM_CALL_PAGE_COPY_ON_WRITE_FLAG = 1ULL << 3ULL;

bool system_call_interrupt_self_test_completed;

void WriteRequiredSystemCallMessage(const VgaTextConsole &vga_console,
                                    const char *message) noexcept {
    if (!vga_console.TryWriteDiagnosticString(message)) {
        HaltProcessor();
    }
}

void WriteRequiredSystemCallValue(const VgaTextConsole &vga_console, const char *prefix,
                                  const uint64_t value) noexcept {
    if (!vga_console.TryWriteDiagnosticHexLine(prefix, value)) {
        HaltProcessor();
    }
}

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
    if (status == FileSystemStatus::PathTooLong) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_PATH_TOO_LONG;
    }
    if (status == FileSystemStatus::NameTooLong) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_NAME_TOO_LONG;
    }
    if (status == FileSystemStatus::LoopDetected) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_PATH_LOOP;
    }
    if (status == FileSystemStatus::ReadOnly) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_READ_ONLY_FILE_SYSTEM;
    }
    if (status == FileSystemStatus::MountCapacityExhausted) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_FILE_SYSTEM_CAPACITY_EXHAUSTED;
    }
    if (status == FileSystemStatus::DirectoryNotEmpty) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_DIRECTORY_NOT_EMPTY;
    }
    if (status == FileSystemStatus::CrossDevice) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_CROSS_DEVICE;
    }
    if (status == FileSystemStatus::Busy) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_RESOURCE_BUSY;
    }
    if (status == FileSystemStatus::Unsupported) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_OPERATION_UNSUPPORTED;
    }
    if (status == FileSystemStatus::WouldBlock) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_WOULD_BLOCK;
    }
    if (status == FileSystemStatus::BackgroundTerminalRead) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_BACKGROUND_TERMINAL_READ;
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
    if (status == ProcessIoStatus::BackgroundTerminalRead) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_BACKGROUND_TERMINAL_READ;
    }
    if (status == ProcessIoStatus::DeviceFailure) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_DEVICE_FAILURE;
    }
    if (status == ProcessIoStatus::FileSystemFailure) {
        return MapFileSystemStatus(file_system_status, transferred_bytes);
    }
    if (status == ProcessIoStatus::DescriptorLimitExceeded) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_DESCRIPTOR_LIMIT_EXCEEDED;
    }
    if (status == ProcessIoStatus::PipeLimitExceeded) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_PIPE_LIMIT_EXCEEDED;
    }
    if (status == ProcessIoStatus::ObjectFailure) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_KERNEL_OBJECT_FAILURE;
    }
    return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_ARGUMENT;
}

[[nodiscard]] int64_t MapProcessRuntimeStatus(const ProcessRuntimeStatus status) noexcept {
    if (status == ProcessRuntimeStatus::Succeeded) {
        return OS_KERNEL_SYSTEM_CALL_DESCRIPTOR_SUCCESS_RESULT;
    }
    if (status == ProcessRuntimeStatus::InvalidArguments) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_ARGUMENT;
    }
    if (status == ProcessRuntimeStatus::ArgumentListTooLarge) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_ARGUMENT_LIST_TOO_LARGE;
    }
    if (status == ProcessRuntimeStatus::InvalidElf) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_EXECUTABLE;
    }
    if (status == ProcessRuntimeStatus::ExecutableReadFailure) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_FILE_NOT_FOUND;
    }
    if (status == ProcessRuntimeStatus::ProcessLimitExceeded) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_PROCESS_LIMIT_EXCEEDED;
    }
    if (status == ProcessRuntimeStatus::ForkFailure) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_OUT_OF_MEMORY;
    }
    return os::abi::OS_ABI_SYSTEM_CALL_RESULT_PROCESS_IMAGE_FAILURE;
}

[[nodiscard]] int64_t MapUserThreadStatus(const UserThreadStatus status) noexcept {
    if (status == UserThreadStatus::Succeeded) {
        return OS_KERNEL_SYSTEM_CALL_DESCRIPTOR_SUCCESS_RESULT;
    }
    if (status == UserThreadStatus::WouldBlock) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_WOULD_BLOCK;
    }
    if (status == UserThreadStatus::InvalidMemory) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_USER_MEMORY;
    }
    if (status == UserThreadStatus::ThreadLimitExceeded) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_THREAD_LIMIT_EXCEEDED;
    }
    if (status == UserThreadStatus::ThreadNotFound) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_THREAD_NOT_FOUND;
    }
    if (status == UserThreadStatus::AlreadyJoined) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_THREAD_ALREADY_JOINED;
    }
    if (status == UserThreadStatus::Deadlock) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_DEADLOCK;
    }
    if (status == UserThreadStatus::InvalidArgument) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_ARGUMENT;
    }
    return os::abi::OS_ABI_SYSTEM_CALL_RESULT_PROCESS_IMAGE_FAILURE;
}

[[nodiscard]] int64_t MapPrivateFutexWaitStatus(const PrivateFutexWaitStatus status) noexcept {
    if (status == PrivateFutexWaitStatus::Succeeded) {
        return OS_KERNEL_SYSTEM_CALL_DESCRIPTOR_SUCCESS_RESULT;
    }
    if (status == PrivateFutexWaitStatus::ValueChanged) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_FUTEX_VALUE_CHANGED;
    }
    if (status == PrivateFutexWaitStatus::TimedOut) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_TIMED_OUT;
    }
    if (status == PrivateFutexWaitStatus::InvalidMemory) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_USER_MEMORY;
    }
    if (status == PrivateFutexWaitStatus::CapacityExhausted) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_FUTEX_LIMIT_EXCEEDED;
    }
    if (status == PrivateFutexWaitStatus::InvalidArgument) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_ARGUMENT;
    }
    return os::abi::OS_ABI_SYSTEM_CALL_RESULT_PROCESS_IMAGE_FAILURE;
}

[[nodiscard]] int64_t MapUserSignalStatus(const UserSignalStatus status) noexcept {
    if (status == UserSignalStatus::Succeeded) {
        return OS_KERNEL_SYSTEM_CALL_DESCRIPTOR_SUCCESS_RESULT;
    }
    if (status == UserSignalStatus::InvalidMemory) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_USER_MEMORY;
    }
    if (status == UserSignalStatus::ProcessNotFound) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_PROCESS_NOT_FOUND;
    }
    if (status == UserSignalStatus::InvalidState) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_SIGNAL_STATE_INVALID;
    }
    if (status == UserSignalStatus::PermissionDenied) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_SESSION_PERMISSION_DENIED;
    }
    if (status == UserSignalStatus::InvalidArgument) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_ARGUMENT;
    }
    return os::abi::OS_ABI_SYSTEM_CALL_RESULT_PROCESS_IMAGE_FAILURE;
}

[[nodiscard]] int64_t MapVirtualMemoryStatus(const UserVirtualMemoryStatus status) noexcept {
    if (status == UserVirtualMemoryStatus::Succeeded) {
        return OS_KERNEL_SYSTEM_CALL_DESCRIPTOR_SUCCESS_RESULT;
    }
    if (status == UserVirtualMemoryStatus::InvalidRange ||
        status == UserVirtualMemoryStatus::InvalidProtection) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_MEMORY_RANGE;
    }
    if (status == UserVirtualMemoryStatus::InvalidFile) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_FILE_DESCRIPTOR;
    }
    if (status == UserVirtualMemoryStatus::UnsupportedMapping) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_OPERATION_UNSUPPORTED;
    }
    if (status == UserVirtualMemoryStatus::AddressInUse) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_ADDRESS_IN_USE;
    }
    if (status == UserVirtualMemoryStatus::AddressSpaceExhausted ||
        status == UserVirtualMemoryStatus::PageAllocationFailed ||
        status == UserVirtualMemoryStatus::PageCacheExhausted) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_OUT_OF_MEMORY;
    }
    if (status == UserVirtualMemoryStatus::MetadataExhausted) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_MEMORY_METADATA_EXHAUSTED;
    }
    if (status == UserVirtualMemoryStatus::ThreadMemoryInUse) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_RESOURCE_BUSY;
    }
    return os::abi::OS_ABI_SYSTEM_CALL_RESULT_PROCESS_IMAGE_FAILURE;
}

[[nodiscard]] UserContextRequirements CurrentUserContextRequirements() noexcept {
    return UserContextRequirements{
        .virtual_address_width_bits = GetNativeSystemCallConfiguration().virtual_address_width_bits,
        .user_code_segment = static_cast<uint64_t>(OS_KERNEL_DESCRIPTOR_USER_CODE_SELECTOR),
        .user_stack_segment = static_cast<uint64_t>(OS_KERNEL_DESCRIPTOR_USER_DATA_SELECTOR),
    };
}

[[nodiscard]] bool ValidateUserContextMemory(const UserContext &context) noexcept {
    const uint64_t stack_probe_address =
        context.stack_pointer == OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES
            ? OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES
            : context.stack_pointer - OS_KERNEL_SYSTEM_CALL_STACK_PROBE_SIZE_BYTES;
    if (!IsUserProgramVirtualAddressRange(context.common.instruction_pointer,
                                          OS_KERNEL_SYSTEM_CALL_ADDRESS_PROBE_SIZE_BYTES) ||
        !IsUserVirtualAddressRange(stack_probe_address,
                                   OS_KERNEL_SYSTEM_CALL_STACK_PROBE_SIZE_BYTES)) {
        return false;
    }
    PageMapping instruction_mapping{};
    PageMapping stack_mapping{};
    return QueryActivePage(context.common.instruction_pointer, instruction_mapping) ==
               PageTableStatus::Succeeded &&
           instruction_mapping.permissions.user_accessible &&
           instruction_mapping.permissions.executable &&
           !instruction_mapping.permissions.writable &&
           QueryActivePage(stack_probe_address, stack_mapping) == PageTableStatus::Succeeded &&
           stack_mapping.permissions.user_accessible && stack_mapping.permissions.writable &&
           !stack_mapping.permissions.executable;
}

[[nodiscard]] uint64_t EncodePagePermissionFlags(const PageMapping &mapping) noexcept {
    uint64_t flags = OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES;
    if (mapping.permissions.writable) {
        flags |= OS_KERNEL_SYSTEM_CALL_PAGE_WRITABLE_FLAG;
    }
    if (mapping.permissions.executable) {
        flags |= OS_KERNEL_SYSTEM_CALL_PAGE_EXECUTABLE_FLAG;
    }
    if (mapping.permissions.user_accessible) {
        flags |= OS_KERNEL_SYSTEM_CALL_PAGE_USER_FLAG;
    }
    if (mapping.permissions.copy_on_write) {
        flags |= OS_KERNEL_SYSTEM_CALL_PAGE_COPY_ON_WRITE_FLAG;
    }
    return flags;
}

[[nodiscard]] bool ValidateUserSystemCallFrame(const ExceptionFrame &frame) noexcept {
    if (!IsProcessSchedulingActive() || !CurrentThreadOwnsUserContext(frame)) {
        return false;
    }
    const UserContext &context = AsUserContext(frame);
    const UserContextEntryMethod entry_method = DecodeUserContextEntryMethod(context);
    if ((entry_method != UserContextEntryMethod::LegacyInterrupt &&
         entry_method != UserContextEntryMethod::NativeSystemCall) ||
        ValidateUserContext(context, CurrentUserContextRequirements()) !=
            UserContextStatus::Succeeded ||
        !ValidateUserContextMemory(context)) {
        return false;
    }
    if (entry_method == UserContextEntryMethod::NativeSystemCall &&
        GetCpuLocal().SystemCallUserStackPointer() != context.stack_pointer) {
        return false;
    }
    return GetCpuLocal().RecordTrustedStackValidation() == CpuLocalStatus::Succeeded;
}

[[nodiscard]] bool ValidateUserReturnFrame(const ExceptionFrame &frame) noexcept {
    if (!IsProcessSchedulingActive() || !CurrentThreadOwnsUserContext(frame)) {
        return false;
    }
    const UserContext &context = AsUserContext(frame);
    return ValidateUserContext(context, CurrentUserContextRequirements()) ==
               UserContextStatus::Succeeded &&
           ValidateUserContextMemory(context);
}

[[nodiscard]] bool PrepareUserReturnFrameMemory(const ExceptionFrame &frame) noexcept {
    if (!IsProcessSchedulingActive() || !CurrentThreadOwnsUserContext(frame)) {
        return false;
    }
    const UserContext &context = AsUserContext(frame);
    if (ValidateUserContext(context, CurrentUserContextRequirements()) !=
        UserContextStatus::Succeeded) {
        return false;
    }
    return ResolveCurrentProcessUserReturnMemory(context.common.instruction_pointer,
                                                 context.stack_pointer) ==
           UserVirtualMemoryStatus::Succeeded;
}

void LogRejectedUserReturn(const ExceptionFrame &frame) noexcept {
    const VgaTextConsole vga_console{VgaTextConsole::Hardware(WritePort8)};
    const UserContext &context = AsUserContext(frame);
    const bool owned = CurrentThreadOwnsUserContext(frame);
    const UserContextStatus context_status =
        ValidateUserContext(context, CurrentUserContextRequirements());
    const bool memory_valid = ValidateUserContextMemory(context);
    const uint64_t stack_probe_address =
        context.stack_pointer == OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES
            ? OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES
            : context.stack_pointer - OS_KERNEL_SYSTEM_CALL_STACK_PROBE_SIZE_BYTES;
    PageMapping instruction_mapping{};
    PageMapping stack_mapping{};
    const PageTableStatus instruction_status =
        QueryActivePage(context.common.instruction_pointer, instruction_mapping);
    const PageTableStatus stack_status = QueryActivePage(stack_probe_address, stack_mapping);
    WriteRequiredSystemCallMessage(vga_console, OS_KERNEL_SYSTEM_CALL_REJECTED_RETURN_MESSAGE);
    WriteRequiredSystemCallValue(vga_console,
                                 OS_KERNEL_SYSTEM_CALL_REJECTED_RETURN_OWNERSHIP_PREFIX,
                                 owned ? OS_KERNEL_SYSTEM_CALL_BOOLEAN_TRUE
                                       : OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES);
    WriteRequiredSystemCallValue(vga_console, OS_KERNEL_SYSTEM_CALL_REJECTED_RETURN_STATUS_PREFIX,
                                 static_cast<uint64_t>(context_status));
    WriteRequiredSystemCallValue(vga_console, OS_KERNEL_SYSTEM_CALL_REJECTED_RETURN_MEMORY_PREFIX,
                                 memory_valid ? OS_KERNEL_SYSTEM_CALL_BOOLEAN_TRUE
                                              : OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES);
    WriteRequiredSystemCallValue(vga_console, OS_KERNEL_SYSTEM_CALL_REJECTED_RETURN_ENTRY_PREFIX,
                                 static_cast<uint64_t>(DecodeUserContextEntryMethod(context)));
    WriteRequiredSystemCallValue(vga_console, OS_KERNEL_SYSTEM_CALL_REJECTED_RETURN_VECTOR_PREFIX,
                                 context.common.vector);
    WriteRequiredSystemCallValue(vga_console, OS_KERNEL_SYSTEM_CALL_REJECTED_RETURN_RIP_PREFIX,
                                 context.common.instruction_pointer);
    WriteRequiredSystemCallValue(vga_console, OS_KERNEL_SYSTEM_CALL_REJECTED_RETURN_RSP_PREFIX,
                                 context.stack_pointer);
    WriteRequiredSystemCallValue(vga_console, OS_KERNEL_SYSTEM_CALL_REJECTED_RETURN_FLAGS_PREFIX,
                                 context.common.flags);
    WriteRequiredSystemCallValue(
        vga_console, OS_KERNEL_SYSTEM_CALL_REJECTED_RETURN_INSTRUCTION_PAGE_STATUS_PREFIX,
        static_cast<uint64_t>(instruction_status));
    WriteRequiredSystemCallValue(
        vga_console, OS_KERNEL_SYSTEM_CALL_REJECTED_RETURN_INSTRUCTION_PAGE_FLAGS_PREFIX,
        EncodePagePermissionFlags(instruction_mapping));
    WriteRequiredSystemCallValue(vga_console,
                                 OS_KERNEL_SYSTEM_CALL_REJECTED_RETURN_STACK_PAGE_STATUS_PREFIX,
                                 static_cast<uint64_t>(stack_status));
    WriteRequiredSystemCallValue(vga_console,
                                 OS_KERNEL_SYSTEM_CALL_REJECTED_RETURN_STACK_PAGE_FLAGS_PREFIX,
                                 EncodePagePermissionFlags(stack_mapping));
}

void CompleteSystemCallInterruptSelfTest() noexcept {
    if (system_call_interrupt_self_test_completed) {
        return;
    }
    const uint64_t initial_timer_tick_count = GetInterruptRuntimeStatistics().timer_tick_count;
    while (GetInterruptRuntimeStatistics().timer_tick_count == initial_timer_tick_count) {
        WaitForInterrupt();
    }
    system_call_interrupt_self_test_completed = true;
}

[[nodiscard]] int64_t DispatchWriteLog(const uint64_t user_address,
                                       const uint64_t length_bytes) noexcept {
    CompleteSystemCallInterruptSelfTest();
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
    const VgaTextConsole vga_console{VgaTextConsole::Hardware(WritePort8)};
    for (uint64_t byte_index = OS_KERNEL_SYSTEM_CALL_FIRST_BYTE_INDEX; byte_index < length_bytes;
         ++byte_index) {
        if (!vga_console.TryWriteDiagnosticByte(static_cast<char>(message[byte_index]))) {
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
    const uint64_t blocked_system_call_number =
        static_cast<uint64_t>(wait_condition == WaitCondition::PipeReadable
                                  ? os::abi::SystemCallNumber::WaitPipeReadable
                                  : os::abi::SystemCallNumber::WaitPipeWritable);
    const ProcessRuntimeStatus status =
        BlockCurrentThread(frame, wait_condition, blocked_system_call_number, true, resume_frame);
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
    if (user_path_address == OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES ||
        path_length_bytes == OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES ||
        (open_flags & ~os::abi::OS_ABI_FILE_OPEN_VALID_FLAG_MASK) !=
            OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_ARGUMENT;
    }
    if (path_length_bytes > os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_PATH_SIZE_BYTES) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_PATH_TOO_LONG;
    }
    uint8_t path[os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_PATH_SIZE_BYTES]{};
    if (CopyFromUser(user_path_address, path_length_bytes, path,
                     os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_PATH_SIZE_BYTES) !=
        UserMemoryCopyStatus::Succeeded) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_USER_MEMORY;
    }
    const fs::OpenOptions options{
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
        user_path_address == OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_ARGUMENT;
    }
    if (path_length_bytes > os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_PATH_SIZE_BYTES) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_PATH_TOO_LONG;
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

[[nodiscard]] ExceptionFrame *DispatchSyncFileSystem(ExceptionFrame &frame) noexcept {
    const int64_t sync_result = MapFileSystemStatus(
        SyncCurrentProcessFileSystem(),
        static_cast<uint64_t>(OS_KERNEL_SYSTEM_CALL_FILE_SYSTEM_SUCCESS_RESULT));
    if (sync_result != OS_KERNEL_SYSTEM_CALL_FILE_SYSTEM_SUCCESS_RESULT) {
        frame.register_rax = static_cast<uint64_t>(sync_result);
        return &frame;
    }
    const uint64_t owner_thread_index = CurrentThreadIndexForBlockIo();
    const uint64_t now_nanoseconds = GetMonotonicNanoseconds();
    const uint64_t deadline_nanoseconds =
        now_nanoseconds > UINT64_MAX - OS_KERNEL_SYSTEM_CALL_ATA_FLUSH_TIMEOUT_NANOSECONDS
            ? UINT64_MAX
            : now_nanoseconds + OS_KERNEL_SYSTEM_CALL_ATA_FLUSH_TIMEOUT_NANOSECONDS;
    const bool interrupts_were_enabled = DisableInterrupts();
    uint64_t request_identifier = OS_KERNEL_SYSTEM_CALL_EMPTY_REQUEST_IDENTIFIER;
    BlockRequestResult immediate_result = BlockRequestResult::None;
    const AtaPioStatus submit_status = SubmitAsynchronousAtaFlush(
        owner_thread_index, deadline_nanoseconds, request_identifier, immediate_result);
    const VgaTextConsole vga_console{VgaTextConsole::Hardware(WritePort8)};
    if (!vga_console.TryWriteDiagnosticHexLine(
            OS_KERNEL_SYSTEM_CALL_ATA_FLUSH_SUBMIT_REQUEST_PREFIX, request_identifier) ||
        !vga_console.TryWriteDiagnosticHexLine(OS_KERNEL_SYSTEM_CALL_ATA_FLUSH_SUBMIT_TIME_PREFIX,
                                               now_nanoseconds)) {
        HaltProcessor();
    }
    if (submit_status != AtaPioStatus::Succeeded || immediate_result != BlockRequestResult::None) {
        if (!vga_console.TryWriteDiagnosticHexLine(
                OS_KERNEL_SYSTEM_CALL_ATA_FLUSH_SUBMIT_STATUS_PREFIX,
                static_cast<uint64_t>(submit_status)) ||
            !vga_console.TryWriteDiagnosticHexLine(
                OS_KERNEL_SYSTEM_CALL_ATA_FLUSH_IMMEDIATE_RESULT_PREFIX,
                static_cast<uint64_t>(immediate_result))) {
            HaltProcessor();
        }
        RestoreInterrupts(interrupts_were_enabled);
        frame.register_rax =
            static_cast<uint64_t>(immediate_result == BlockRequestResult::TimedOut
                                      ? os::abi::OS_ABI_SYSTEM_CALL_RESULT_TIMED_OUT
                                      : os::abi::OS_ABI_SYSTEM_CALL_RESULT_DEVICE_FAILURE);
        return &frame;
    }
    if (RegisterCurrentBlockIoRequest(request_identifier) != ProcessRuntimeStatus::Succeeded) {
        HaltProcessor();
    }
    ExceptionFrame *resume_frame = &frame;
    if (BlockCurrentThread(frame, WaitCondition::BlockIo,
                           static_cast<uint64_t>(os::abi::SystemCallNumber::SyncFileSystem), false,
                           resume_frame) != ProcessRuntimeStatus::Succeeded) {
        HaltProcessor();
    }
    return resume_frame;
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
    const ProcessRuntimeStatus block_status = BlockCurrentThread(
        frame,
        wait_for_read ? WaitCondition::DescriptorReadable : WaitCondition::DescriptorWritable,
        static_cast<uint64_t>(wait_for_read ? os::abi::SystemCallNumber::WaitDescriptorReadable
                                            : os::abi::SystemCallNumber::WaitDescriptorWritable),
        true, resume_frame);
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

[[nodiscard]] int64_t DispatchDuplicateDescriptor(const uint64_t source_descriptor,
                                                  const uint64_t minimum_descriptor,
                                                  const uint64_t descriptor_flags) noexcept {
    if ((descriptor_flags & ~os::abi::OS_ABI_FILE_DESCRIPTOR_VALID_FLAG_MASK) !=
        OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_ARGUMENT;
    }
    uint64_t destination_descriptor = OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES;
    const ProcessIoStatus status = DuplicateCurrentProcessDescriptor(
        source_descriptor, minimum_descriptor, descriptor_flags, destination_descriptor);
    return MapProcessIoStatus(status, destination_descriptor, FileSystemStatus::Succeeded);
}

[[nodiscard]] int64_t DispatchDuplicateDescriptorTo(const uint64_t source_descriptor,
                                                    const uint64_t destination_descriptor,
                                                    const uint64_t descriptor_flags) noexcept {
    if ((descriptor_flags & ~os::abi::OS_ABI_FILE_DESCRIPTOR_VALID_FLAG_MASK) !=
        OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_ARGUMENT;
    }
    const ProcessIoStatus status = DuplicateCurrentProcessDescriptorTo(
        source_descriptor, destination_descriptor, descriptor_flags);
    return MapProcessIoStatus(status, destination_descriptor, FileSystemStatus::Succeeded);
}

[[nodiscard]] int64_t DispatchCreatePipe(const uint64_t user_pair_address,
                                         const uint64_t pair_size_bytes) noexcept {
    if (pair_size_bytes != os::abi::OS_ABI_PIPE_DESCRIPTOR_PAIR_SIZE_BYTES) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_ARGUMENT;
    }
    if (ValidateUserWritableMemory(user_pair_address, pair_size_bytes) !=
        UserMemoryCopyStatus::Succeeded) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_USER_MEMORY;
    }

    os::abi::PipeDescriptorPair descriptor_pair{};
    const ProcessIoStatus status = CreateCurrentProcessPipe(descriptor_pair.reader_descriptor,
                                                            descriptor_pair.writer_descriptor);
    if (status != ProcessIoStatus::Succeeded) {
        return MapProcessIoStatus(status, OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES,
                                  FileSystemStatus::Succeeded);
    }
    if (CopyToUser(
            user_pair_address, pair_size_bytes, reinterpret_cast<const uint8_t *>(&descriptor_pair),
            os::abi::OS_ABI_PIPE_DESCRIPTOR_PAIR_SIZE_BYTES) != UserMemoryCopyStatus::Succeeded) {
        HaltProcessor();
    }
    return OS_KERNEL_SYSTEM_CALL_DESCRIPTOR_SUCCESS_RESULT;
}

[[nodiscard]] int64_t DispatchGetDescriptorFlags(const uint64_t descriptor) noexcept {
    uint64_t descriptor_flags = OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES;
    const ProcessIoStatus status = GetCurrentProcessDescriptorFlags(descriptor, descriptor_flags);
    return MapProcessIoStatus(status, descriptor_flags, FileSystemStatus::Succeeded);
}

[[nodiscard]] int64_t DispatchSetDescriptorFlags(const uint64_t descriptor,
                                                 const uint64_t descriptor_flags) noexcept {
    if ((descriptor_flags & ~os::abi::OS_ABI_FILE_DESCRIPTOR_VALID_FLAG_MASK) !=
        OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_ARGUMENT;
    }
    return MapProcessIoStatus(SetCurrentProcessDescriptorFlags(descriptor, descriptor_flags),
                              OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES,
                              FileSystemStatus::Succeeded);
}

[[nodiscard]] int64_t DispatchSetDescriptorSoftLimit(const uint64_t soft_limit) noexcept {
    return MapProcessIoStatus(SetCurrentProcessDescriptorSoftLimit(soft_limit),
                              OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES,
                              FileSystemStatus::Succeeded);
}

[[nodiscard]] int64_t DispatchGetDescriptorLimit(const bool read_soft_limit) noexcept {
    uint64_t soft_limit = OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES;
    uint64_t hard_limit = OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES;
    const ProcessIoStatus status = GetCurrentProcessDescriptorLimits(soft_limit, hard_limit);
    return MapProcessIoStatus(status, read_soft_limit ? soft_limit : hard_limit,
                              FileSystemStatus::Succeeded);
}

[[nodiscard]] int64_t DispatchOpenDirectory(const uint64_t user_path_address,
                                            const uint64_t path_length_bytes) noexcept {
    if (path_length_bytes == OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES ||
        user_path_address == OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_ARGUMENT;
    }
    if (path_length_bytes > os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_PATH_SIZE_BYTES) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_PATH_TOO_LONG;
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
    fs::DirectoryEntry file_system_entry{};
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
        .inode_number = file_system_entry.node_identifier,
        .type = file_system_entry.type == fs::NodeType::Directory
                    ? os::abi::DirectoryEntryType::Directory
                : file_system_entry.type == fs::NodeType::CharacterDevice
                    ? os::abi::DirectoryEntryType::CharacterDevice
                    : os::abi::DirectoryEntryType::RegularFile,
        .name_length_bytes = file_system_entry.name_length_bytes,
        .name = {},
        .reserved = OS_KERNEL_SYSTEM_CALL_DIRECTORY_ENTRY_RESERVED_VALUE,
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

[[nodiscard]] int64_t DispatchChangeDirectory(const uint64_t user_path_address,
                                              const uint64_t path_length_bytes) noexcept {
    if (user_path_address == OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES ||
        path_length_bytes == OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_ARGUMENT;
    }
    if (path_length_bytes > os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_PATH_SIZE_BYTES) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_PATH_TOO_LONG;
    }
    uint8_t path[os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_PATH_SIZE_BYTES]{};
    if (CopyFromUser(user_path_address, path_length_bytes, path,
                     os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_PATH_SIZE_BYTES) !=
        UserMemoryCopyStatus::Succeeded) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_USER_MEMORY;
    }
    return MapFileSystemStatus(
        ChangeCurrentProcessDirectory(path, path_length_bytes),
        static_cast<uint64_t>(OS_KERNEL_SYSTEM_CALL_FILE_SYSTEM_SUCCESS_RESULT));
}

[[nodiscard]] int64_t DispatchGetWorkingDirectory(const uint64_t user_destination_address,
                                                  const uint64_t capacity_bytes) noexcept {
    if (user_destination_address == OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES ||
        capacity_bytes == OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_ARGUMENT;
    }
    const uint64_t effective_capacity_bytes =
        capacity_bytes < os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_PATH_SIZE_BYTES
            ? capacity_bytes
            : os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_PATH_SIZE_BYTES;
    if (ValidateUserWritableMemory(user_destination_address, effective_capacity_bytes) !=
        UserMemoryCopyStatus::Succeeded) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_USER_MEMORY;
    }
    uint8_t path[os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_PATH_SIZE_BYTES]{};
    uint64_t path_length_bytes = OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES;
    const FileSystemStatus status =
        GetCurrentProcessWorkingDirectory(path, effective_capacity_bytes, path_length_bytes);
    if (status == FileSystemStatus::Succeeded &&
        CopyToUser(user_destination_address, path_length_bytes, path, effective_capacity_bytes) !=
            UserMemoryCopyStatus::Succeeded) {
        HaltProcessor();
    }
    return MapFileSystemStatus(status, path_length_bytes);
}

[[nodiscard]] int64_t DispatchRemovePath(const uint64_t user_path_address,
                                         const uint64_t path_length_bytes,
                                         const bool directory) noexcept {
    if (user_path_address == OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES ||
        path_length_bytes == OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_ARGUMENT;
    }
    if (path_length_bytes > os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_PATH_SIZE_BYTES) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_PATH_TOO_LONG;
    }
    uint8_t path[os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_PATH_SIZE_BYTES]{};
    if (CopyFromUser(user_path_address, path_length_bytes, path, sizeof(path)) !=
        UserMemoryCopyStatus::Succeeded) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_USER_MEMORY;
    }
    const FileSystemStatus status = directory
                                        ? RemoveCurrentProcessDirectory(path, path_length_bytes)
                                        : RemoveCurrentProcessFile(path, path_length_bytes);
    return MapFileSystemStatus(
        status, static_cast<uint64_t>(OS_KERNEL_SYSTEM_CALL_FILE_SYSTEM_SUCCESS_RESULT));
}

[[nodiscard]] int64_t DispatchRename(const uint64_t user_source_address,
                                     const uint64_t source_length_bytes,
                                     const uint64_t user_destination_address,
                                     const uint64_t destination_length_bytes) noexcept {
    if (user_source_address == OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES ||
        user_destination_address == OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES ||
        source_length_bytes == OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES ||
        destination_length_bytes == OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_ARGUMENT;
    }
    if (source_length_bytes > os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_PATH_SIZE_BYTES ||
        destination_length_bytes > os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_PATH_SIZE_BYTES) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_PATH_TOO_LONG;
    }
    uint8_t source_path[os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_PATH_SIZE_BYTES]{};
    uint8_t destination_path[os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_PATH_SIZE_BYTES]{};
    if (CopyFromUser(user_source_address, source_length_bytes, source_path, sizeof(source_path)) !=
            UserMemoryCopyStatus::Succeeded ||
        CopyFromUser(user_destination_address, destination_length_bytes, destination_path,
                     sizeof(destination_path)) != UserMemoryCopyStatus::Succeeded) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_USER_MEMORY;
    }
    return MapFileSystemStatus(
        RenameCurrentProcessPath(source_path, source_length_bytes, destination_path,
                                 destination_length_bytes),
        static_cast<uint64_t>(OS_KERNEL_SYSTEM_CALL_FILE_SYSTEM_SUCCESS_RESULT));
}

[[nodiscard]] int64_t DispatchTruncateFile(const uint64_t user_path_address,
                                           const uint64_t path_length_bytes,
                                           const uint64_t size_bytes) noexcept {
    if (user_path_address == OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES ||
        path_length_bytes == OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_ARGUMENT;
    }
    if (path_length_bytes > os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_PATH_SIZE_BYTES) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_PATH_TOO_LONG;
    }
    uint8_t path[os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_PATH_SIZE_BYTES]{};
    if (CopyFromUser(user_path_address, path_length_bytes, path, sizeof(path)) !=
        UserMemoryCopyStatus::Succeeded) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_USER_MEMORY;
    }
    return MapFileSystemStatus(
        TruncateCurrentProcessFile(path, path_length_bytes, size_bytes),
        static_cast<uint64_t>(OS_KERNEL_SYSTEM_CALL_FILE_SYSTEM_SUCCESS_RESULT));
}

[[nodiscard]] int64_t DispatchStatFile(const uint64_t user_path_address,
                                       const uint64_t path_length_bytes,
                                       const uint64_t user_information_address,
                                       const uint64_t information_size_bytes) noexcept {
    if (user_path_address == OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES ||
        path_length_bytes == OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES ||
        information_size_bytes != sizeof(os::abi::FileInformation)) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_ARGUMENT;
    }
    if (path_length_bytes > os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_PATH_SIZE_BYTES) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_PATH_TOO_LONG;
    }
    if (ValidateUserWritableMemory(user_information_address, information_size_bytes) !=
        UserMemoryCopyStatus::Succeeded) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_USER_MEMORY;
    }
    uint8_t path[os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_PATH_SIZE_BYTES]{};
    if (CopyFromUser(user_path_address, path_length_bytes, path, sizeof(path)) !=
        UserMemoryCopyStatus::Succeeded) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_USER_MEMORY;
    }
    fs::NodeInformation node_information{};
    const FileSystemStatus status =
        StatCurrentProcessPath(path, path_length_bytes, node_information);
    if (status != FileSystemStatus::Succeeded) {
        return MapFileSystemStatus(
            status, static_cast<uint64_t>(OS_KERNEL_SYSTEM_CALL_FILE_SYSTEM_SUCCESS_RESULT));
    }
    os::abi::FileInformation information{
        .mount_identifier = node_information.mount_identifier,
        .superblock_identifier = node_information.superblock_identifier,
        .inode_number = node_information.node_identifier,
        .generation = node_information.generation,
        .type = node_information.type == fs::NodeType::Directory
                    ? os::abi::DirectoryEntryType::Directory
                : node_information.type == fs::NodeType::CharacterDevice
                    ? os::abi::DirectoryEntryType::CharacterDevice
                    : os::abi::DirectoryEntryType::RegularFile,
        .size_bytes = node_information.size_bytes,
        .allocated_size_bytes = node_information.allocated_size_bytes,
        .link_count = node_information.link_count,
    };
    if (CopyToUser(user_information_address, sizeof(information),
                   reinterpret_cast<const uint8_t *>(&information),
                   information_size_bytes) != UserMemoryCopyStatus::Succeeded) {
        HaltProcessor();
    }
    return OS_KERNEL_SYSTEM_CALL_FILE_SYSTEM_SUCCESS_RESULT;
}

[[nodiscard]] bool CopyProcessLaunchRequest(const uint64_t user_request_address,
                                            os::abi::ProcessLaunchRequest &request) noexcept {
    request = os::abi::ProcessLaunchRequest{};
    return CopyFromUser(user_request_address, sizeof(request),
                        reinterpret_cast<uint8_t *>(&request),
                        sizeof(request)) == UserMemoryCopyStatus::Succeeded;
}

[[nodiscard]] int64_t DispatchSpawnProcess(const uint64_t user_request_address,
                                           const uint64_t request_size_bytes) noexcept {
    if (request_size_bytes != sizeof(os::abi::ProcessLaunchRequest)) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_ARGUMENT;
    }
    os::abi::ProcessLaunchRequest request{};
    if (!CopyProcessLaunchRequest(user_request_address, request)) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_USER_MEMORY;
    }
    uint64_t process_id = OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES;
    const ProcessRuntimeStatus status = SpawnCurrentProcess(request, process_id);
    return status == ProcessRuntimeStatus::Succeeded ? static_cast<int64_t>(process_id)
                                                     : MapProcessRuntimeStatus(status);
}

[[nodiscard]] ExceptionFrame *DispatchForkProcess(ExceptionFrame &frame) noexcept {
    uint64_t process_id = OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES;
    const ProcessRuntimeStatus status = ForkCurrentProcess(frame, process_id);
    frame.register_rax = static_cast<uint64_t>(status == ProcessRuntimeStatus::Succeeded
                                                   ? static_cast<int64_t>(process_id)
                                                   : MapProcessRuntimeStatus(status));
    return &frame;
}

[[nodiscard]] ExceptionFrame *DispatchExecProcess(ExceptionFrame &frame,
                                                  const uint64_t user_request_address,
                                                  const uint64_t request_size_bytes) noexcept {
    if (request_size_bytes != sizeof(os::abi::ProcessLaunchRequest)) {
        frame.register_rax =
            static_cast<uint64_t>(os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_ARGUMENT);
        return &frame;
    }
    os::abi::ProcessLaunchRequest request{};
    if (!CopyProcessLaunchRequest(user_request_address, request)) {
        frame.register_rax =
            static_cast<uint64_t>(os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_USER_MEMORY);
        return &frame;
    }
    const ProcessRuntimeStatus status = ExecCurrentProcess(frame, request);
    if (status != ProcessRuntimeStatus::Succeeded) {
        frame.register_rax = static_cast<uint64_t>(MapProcessRuntimeStatus(status));
    }
    return &frame;
}

[[nodiscard]] ExceptionFrame *DispatchWaitProcess(ExceptionFrame &frame,
                                                  const uint64_t requested_process_id,
                                                  const uint64_t user_result_address,
                                                  const uint64_t result_size_bytes) noexcept {
    if (result_size_bytes != sizeof(os::abi::ProcessWaitResult)) {
        frame.register_rax =
            static_cast<uint64_t>(os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_ARGUMENT);
        return &frame;
    }
    if (ValidateUserWritableMemory(user_result_address, result_size_bytes) !=
        UserMemoryCopyStatus::Succeeded) {
        frame.register_rax =
            static_cast<uint64_t>(os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_USER_MEMORY);
        return &frame;
    }
    os::abi::ProcessWaitResult wait_result{};
    const ProcessWaitStatus wait_status = TryWaitCurrentProcess(requested_process_id, wait_result);
    if (wait_status == ProcessWaitStatus::Succeeded) {
        if (CopyToUser(user_result_address, sizeof(wait_result),
                       reinterpret_cast<const uint8_t *>(&wait_result),
                       sizeof(wait_result)) != UserMemoryCopyStatus::Succeeded) {
            HaltProcessor();
        }
        frame.register_rax = wait_result.process_id;
        return &frame;
    }
    if (wait_status == ProcessWaitStatus::NoChild) {
        frame.register_rax =
            static_cast<uint64_t>(os::abi::OS_ABI_SYSTEM_CALL_RESULT_NO_CHILD_PROCESS);
        return &frame;
    }
    if (wait_status == ProcessWaitStatus::InvalidArgument) {
        frame.register_rax =
            static_cast<uint64_t>(os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_ARGUMENT);
        return &frame;
    }
    if (wait_status != ProcessWaitStatus::WouldBlock) {
        frame.register_rax =
            static_cast<uint64_t>(os::abi::OS_ABI_SYSTEM_CALL_RESULT_PROCESS_IMAGE_FAILURE);
        return &frame;
    }

    frame.register_rax = static_cast<uint64_t>(os::abi::OS_ABI_SYSTEM_CALL_RESULT_WOULD_BLOCK);
    ExceptionFrame *resume_frame = &frame;
    const ProcessRuntimeStatus block_status = BlockCurrentThread(
        frame, WaitCondition::ChildProcess,
        static_cast<uint64_t>(os::abi::SystemCallNumber::WaitProcess), true, resume_frame);
    if (block_status != ProcessRuntimeStatus::Succeeded) {
        HaltProcessor();
    }
    return resume_frame;
}

[[nodiscard]] ExceptionFrame *DispatchWaitProcessEvent(ExceptionFrame &frame,
                                                       const uint64_t requested_process_id,
                                                       const uint64_t wait_flags,
                                                       const uint64_t user_result_address,
                                                       const uint64_t result_size_bytes) noexcept {
    if (result_size_bytes != sizeof(os::abi::ProcessWaitEventResult) ||
        (wait_flags & ~os::abi::OS_ABI_PROCESS_WAIT_VALID_FLAG_MASK) !=
            OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES) {
        frame.register_rax =
            static_cast<uint64_t>(os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_ARGUMENT);
        return &frame;
    }
    if (ValidateUserWritableMemory(user_result_address, result_size_bytes) !=
        UserMemoryCopyStatus::Succeeded) {
        frame.register_rax =
            static_cast<uint64_t>(os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_USER_MEMORY);
        return &frame;
    }
    os::abi::ProcessWaitEventResult wait_result{};
    const ProcessWaitStatus wait_status =
        TryWaitCurrentProcessEvent(requested_process_id, wait_flags, wait_result);
    if (wait_status == ProcessWaitStatus::Succeeded) {
        if (CopyToUser(user_result_address, sizeof(wait_result),
                       reinterpret_cast<const uint8_t *>(&wait_result),
                       sizeof(wait_result)) != UserMemoryCopyStatus::Succeeded) {
            HaltProcessor();
        }
        frame.register_rax = wait_result.process_id;
        return &frame;
    }
    if (wait_status == ProcessWaitStatus::NoChild) {
        frame.register_rax =
            static_cast<uint64_t>(os::abi::OS_ABI_SYSTEM_CALL_RESULT_NO_CHILD_PROCESS);
        return &frame;
    }
    if (wait_status == ProcessWaitStatus::InvalidArgument) {
        frame.register_rax =
            static_cast<uint64_t>(os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_ARGUMENT);
        return &frame;
    }
    if (wait_status != ProcessWaitStatus::WouldBlock) {
        frame.register_rax =
            static_cast<uint64_t>(os::abi::OS_ABI_SYSTEM_CALL_RESULT_PROCESS_IMAGE_FAILURE);
        return &frame;
    }
    frame.register_rax = static_cast<uint64_t>(os::abi::OS_ABI_SYSTEM_CALL_RESULT_WOULD_BLOCK);
    if ((wait_flags & os::abi::OS_ABI_PROCESS_WAIT_NO_HANG_FLAG) !=
        OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES) {
        return &frame;
    }
    ExceptionFrame *resume_frame = &frame;
    const ProcessRuntimeStatus block_status = BlockCurrentThread(
        frame, WaitCondition::ChildProcess,
        static_cast<uint64_t>(os::abi::SystemCallNumber::WaitProcessEvent), true, resume_frame);
    if (block_status != ProcessRuntimeStatus::Succeeded) {
        HaltProcessor();
    }
    return resume_frame;
}

[[nodiscard]] int64_t
DispatchGetTerminalInformation(const uint64_t user_information_address,
                               const uint64_t information_size_bytes) noexcept {
    if (information_size_bytes != sizeof(os::abi::TerminalInformation)) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_ARGUMENT;
    }
    if (ValidateUserWritableMemory(user_information_address, information_size_bytes) !=
        UserMemoryCopyStatus::Succeeded) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_USER_MEMORY;
    }
    os::abi::TerminalInformation information{};
    const UserSignalStatus status = GetCurrentTerminalInformation(information);
    if (status != UserSignalStatus::Succeeded) {
        return MapUserSignalStatus(status);
    }
    if (CopyToUser(user_information_address, sizeof(information),
                   reinterpret_cast<const uint8_t *>(&information),
                   sizeof(information)) != UserMemoryCopyStatus::Succeeded) {
        HaltProcessor();
    }
    return OS_KERNEL_SYSTEM_CALL_DESCRIPTOR_SUCCESS_RESULT;
}

[[nodiscard]] int64_t DispatchMapAnonymousMemory(const uint64_t requested_address,
                                                 const uint64_t length_bytes,
                                                 const uint64_t protection_flags,
                                                 const uint64_t map_flags) noexcept {
    uint64_t mapped_address = OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES;
    const UserVirtualMemoryStatus status = MapCurrentProcessAnonymousMemory(
        requested_address, length_bytes, protection_flags, map_flags, mapped_address);
    return status == UserVirtualMemoryStatus::Succeeded ? static_cast<int64_t>(mapped_address)
                                                        : MapVirtualMemoryStatus(status);
}

[[nodiscard]] int64_t DispatchUnmapMemory(const uint64_t address,
                                          const uint64_t length_bytes) noexcept {
    return MapVirtualMemoryStatus(UnmapCurrentProcessMemory(address, length_bytes));
}

[[nodiscard]] int64_t DispatchMapFileMemory(const uint64_t user_request_address,
                                            const uint64_t request_size_bytes) noexcept {
    if (request_size_bytes != sizeof(os::abi::FileMemoryMapRequest)) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_ARGUMENT;
    }
    os::abi::FileMemoryMapRequest request{};
    if (CopyFromUser(user_request_address, sizeof(request), reinterpret_cast<uint8_t *>(&request),
                     sizeof(request)) != UserMemoryCopyStatus::Succeeded) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_USER_MEMORY;
    }
    uint64_t mapped_address = OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES;
    const UserVirtualMemoryStatus status = MapCurrentProcessFileMemory(request, mapped_address);
    return status == UserVirtualMemoryStatus::Succeeded ? static_cast<int64_t>(mapped_address)
                                                        : MapVirtualMemoryStatus(status);
}

[[nodiscard]] int64_t DispatchSetProgramBreak(const uint64_t requested_address) noexcept {
    uint64_t program_break_address = OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES;
    const UserVirtualMemoryStatus status =
        SetCurrentProcessProgramBreak(requested_address, program_break_address);
    return status == UserVirtualMemoryStatus::Succeeded
               ? static_cast<int64_t>(program_break_address)
               : MapVirtualMemoryStatus(status);
}

[[nodiscard]] int64_t
DispatchGetVirtualMemoryStatistics(const uint64_t user_statistics_address,
                                   const uint64_t statistics_size_bytes) noexcept {
    if (statistics_size_bytes != sizeof(os::abi::VirtualMemoryStatistics)) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_ARGUMENT;
    }
    if (ValidateUserWritableMemory(user_statistics_address, statistics_size_bytes) !=
        UserMemoryCopyStatus::Succeeded) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_USER_MEMORY;
    }
    const os::abi::VirtualMemoryStatistics statistics = GetCurrentProcessVirtualMemoryStatistics();
    if (CopyToUser(user_statistics_address, sizeof(statistics),
                   reinterpret_cast<const uint8_t *>(&statistics),
                   sizeof(statistics)) != UserMemoryCopyStatus::Succeeded) {
        HaltProcessor();
    }
    return OS_KERNEL_SYSTEM_CALL_DESCRIPTOR_SUCCESS_RESULT;
}

[[nodiscard]] int64_t DispatchCreateThread(ExceptionFrame &frame,
                                           const uint64_t user_request_address,
                                           const uint64_t request_size_bytes) noexcept {
    if (request_size_bytes != sizeof(os::abi::ThreadCreateRequest)) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_ARGUMENT;
    }
    os::abi::ThreadCreateRequest request{};
    if (CopyFromUser(user_request_address, sizeof(request), reinterpret_cast<uint8_t *>(&request),
                     sizeof(request)) != UserMemoryCopyStatus::Succeeded) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_USER_MEMORY;
    }
    uint64_t thread_id = OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES;
    const UserThreadStatus status = CreateCurrentProcessThread(frame, request, thread_id);
    return status == UserThreadStatus::Succeeded ? static_cast<int64_t>(thread_id)
                                                 : MapUserThreadStatus(status);
}

[[nodiscard]] ExceptionFrame *DispatchJoinThread(ExceptionFrame &frame,
                                                 const uint64_t requested_thread_id,
                                                 const uint64_t user_result_address,
                                                 const uint64_t result_size_bytes) noexcept {
    if (result_size_bytes != sizeof(os::abi::ThreadJoinResult)) {
        frame.register_rax =
            static_cast<uint64_t>(os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_ARGUMENT);
        return &frame;
    }
    if (ValidateUserWritableMemory(user_result_address, result_size_bytes) !=
        UserMemoryCopyStatus::Succeeded) {
        frame.register_rax =
            static_cast<uint64_t>(os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_USER_MEMORY);
        return &frame;
    }
    os::abi::ThreadJoinResult join_result{};
    const UserThreadStatus status = TryJoinCurrentProcessThread(requested_thread_id, join_result);
    if (status == UserThreadStatus::Succeeded) {
        if (CopyToUser(user_result_address, sizeof(join_result),
                       reinterpret_cast<const uint8_t *>(&join_result),
                       sizeof(join_result)) != UserMemoryCopyStatus::Succeeded) {
            HaltProcessor();
        }
        frame.register_rax = requested_thread_id;
        return &frame;
    }
    if (status != UserThreadStatus::WouldBlock) {
        frame.register_rax = static_cast<uint64_t>(MapUserThreadStatus(status));
        return &frame;
    }
    frame.register_rax = static_cast<uint64_t>(os::abi::OS_ABI_SYSTEM_CALL_RESULT_WOULD_BLOCK);
    ExceptionFrame *resume_frame = &frame;
    if (BlockCurrentThread(frame, WaitCondition::ThreadJoin,
                           static_cast<uint64_t>(os::abi::SystemCallNumber::JoinThread), true,
                           resume_frame) != ProcessRuntimeStatus::Succeeded) {
        HaltProcessor();
    }
    return resume_frame;
}

[[nodiscard]] ExceptionFrame *
DispatchWaitPrivateFutex(ExceptionFrame &frame, const uint64_t user_address,
                         const uint32_t expected_value, const bool deadline_enabled,
                         const uint64_t deadline_nanoseconds) noexcept {
    frame.register_rax = OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES;
    ExceptionFrame *resume_frame = &frame;
    const PrivateFutexWaitStatus status = WaitCurrentProcessPrivateFutex(
        frame, user_address, expected_value, deadline_enabled, deadline_nanoseconds, resume_frame);
    if (status != PrivateFutexWaitStatus::Succeeded) {
        frame.register_rax = static_cast<uint64_t>(MapPrivateFutexWaitStatus(status));
        return &frame;
    }
    return resume_frame;
}

[[nodiscard]] ExceptionFrame *DispatchSleepUntil(ExceptionFrame &frame,
                                                 const uint64_t deadline_nanoseconds) noexcept {
    frame.register_rax = OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES;
    ExceptionFrame *resume_frame = &frame;
    const TimedWaitStatus status =
        SleepCurrentThreadUntil(frame, deadline_nanoseconds, resume_frame);
    if (status == TimedWaitStatus::Succeeded) {
        return resume_frame;
    }
    if (status == TimedWaitStatus::DeadlineReached) {
        return &frame;
    }
    frame.register_rax =
        static_cast<uint64_t>(status == TimedWaitStatus::InvalidArgument
                                  ? os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_ARGUMENT
                                  : os::abi::OS_ABI_SYSTEM_CALL_RESULT_PROCESS_IMAGE_FAILURE);
    return &frame;
}

[[nodiscard]] int64_t DispatchSetSignalAction(const uint64_t signal_number,
                                              const uint64_t user_action_address,
                                              const uint64_t user_previous_action_address,
                                              const uint64_t action_size_bytes) noexcept {
    if (action_size_bytes != sizeof(os::abi::SignalAction) ||
        user_action_address == OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_ARGUMENT;
    }
    os::abi::SignalAction action{};
    if (CopyFromUser(user_action_address, sizeof(action), reinterpret_cast<uint8_t *>(&action),
                     sizeof(action)) != UserMemoryCopyStatus::Succeeded ||
        (user_previous_action_address != OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES &&
         ValidateUserWritableMemory(user_previous_action_address, sizeof(action)) !=
             UserMemoryCopyStatus::Succeeded)) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_USER_MEMORY;
    }
    os::abi::SignalAction previous_action{};
    const UserSignalStatus status =
        SetCurrentProcessSignalAction(signal_number, action, previous_action);
    if (status != UserSignalStatus::Succeeded) {
        return MapUserSignalStatus(status);
    }
    if (user_previous_action_address != OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES &&
        CopyToUser(user_previous_action_address, sizeof(previous_action),
                   reinterpret_cast<const uint8_t *>(&previous_action),
                   sizeof(previous_action)) != UserMemoryCopyStatus::Succeeded) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_USER_MEMORY;
    }
    return OS_KERNEL_SYSTEM_CALL_DESCRIPTOR_SUCCESS_RESULT;
}

[[nodiscard]] int64_t DispatchSetSignalMask(const uint64_t signal_mask,
                                            const uint64_t user_previous_mask_address) noexcept {
    if (user_previous_mask_address != OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES &&
        ValidateUserWritableMemory(user_previous_mask_address, sizeof(uint64_t)) !=
            UserMemoryCopyStatus::Succeeded) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_USER_MEMORY;
    }
    uint64_t previous_mask = OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES;
    const UserSignalStatus status = SetCurrentThreadSignalMask(signal_mask, previous_mask);
    if (status != UserSignalStatus::Succeeded) {
        return MapUserSignalStatus(status);
    }
    if (user_previous_mask_address != OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES &&
        CopyToUser(user_previous_mask_address, sizeof(previous_mask),
                   reinterpret_cast<const uint8_t *>(&previous_mask),
                   sizeof(previous_mask)) != UserMemoryCopyStatus::Succeeded) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_USER_MEMORY;
    }
    return OS_KERNEL_SYSTEM_CALL_DESCRIPTOR_SUCCESS_RESULT;
}
}

[[nodiscard]] ExceptionFrame *DispatchValidatedSystemCall(ExceptionFrame *frame) noexcept {
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
        return DispatchSyncFileSystem(*frame);
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
    if (system_call_number ==
        static_cast<uint64_t>(os::abi::SystemCallNumber::DuplicateDescriptor)) {
        frame->register_rax = static_cast<uint64_t>(DispatchDuplicateDescriptor(
            frame->register_rdi, frame->register_rsi, frame->register_rdx));
        return frame;
    }
    if (system_call_number ==
        static_cast<uint64_t>(os::abi::SystemCallNumber::GetDescriptorFlags)) {
        frame->register_rax =
            static_cast<uint64_t>(DispatchGetDescriptorFlags(frame->register_rdi));
        return frame;
    }
    if (system_call_number ==
        static_cast<uint64_t>(os::abi::SystemCallNumber::SetDescriptorFlags)) {
        frame->register_rax = static_cast<uint64_t>(
            DispatchSetDescriptorFlags(frame->register_rdi, frame->register_rsi));
        return frame;
    }
    if (system_call_number ==
        static_cast<uint64_t>(os::abi::SystemCallNumber::SetDescriptorSoftLimit)) {
        frame->register_rax =
            static_cast<uint64_t>(DispatchSetDescriptorSoftLimit(frame->register_rdi));
        return frame;
    }
    if (system_call_number ==
        static_cast<uint64_t>(os::abi::SystemCallNumber::GetDescriptorSoftLimit)) {
        frame->register_rax = static_cast<uint64_t>(DispatchGetDescriptorLimit(true));
        return frame;
    }
    if (system_call_number ==
        static_cast<uint64_t>(os::abi::SystemCallNumber::GetDescriptorHardLimit)) {
        frame->register_rax = static_cast<uint64_t>(DispatchGetDescriptorLimit(false));
        return frame;
    }
    if (system_call_number == static_cast<uint64_t>(os::abi::SystemCallNumber::ChangeDirectory)) {
        frame->register_rax = static_cast<uint64_t>(
            DispatchChangeDirectory(frame->register_rdi, frame->register_rsi));
        return frame;
    }
    if (system_call_number ==
        static_cast<uint64_t>(os::abi::SystemCallNumber::GetWorkingDirectory)) {
        frame->register_rax = static_cast<uint64_t>(
            DispatchGetWorkingDirectory(frame->register_rdi, frame->register_rsi));
        return frame;
    }
    if (system_call_number == static_cast<uint64_t>(os::abi::SystemCallNumber::UnlinkFile)) {
        frame->register_rax = static_cast<uint64_t>(
            DispatchRemovePath(frame->register_rdi, frame->register_rsi, false));
        return frame;
    }
    if (system_call_number == static_cast<uint64_t>(os::abi::SystemCallNumber::RemoveDirectory)) {
        frame->register_rax = static_cast<uint64_t>(
            DispatchRemovePath(frame->register_rdi, frame->register_rsi, true));
        return frame;
    }
    if (system_call_number == static_cast<uint64_t>(os::abi::SystemCallNumber::Rename)) {
        frame->register_rax = static_cast<uint64_t>(DispatchRename(
            frame->register_rdi, frame->register_rsi, frame->register_rdx, frame->register_r10));
        return frame;
    }
    if (system_call_number == static_cast<uint64_t>(os::abi::SystemCallNumber::TruncateFile)) {
        frame->register_rax = static_cast<uint64_t>(
            DispatchTruncateFile(frame->register_rdi, frame->register_rsi, frame->register_rdx));
        return frame;
    }
    if (system_call_number == static_cast<uint64_t>(os::abi::SystemCallNumber::StatFile)) {
        frame->register_rax = static_cast<uint64_t>(DispatchStatFile(
            frame->register_rdi, frame->register_rsi, frame->register_rdx, frame->register_r10));
        return frame;
    }
    if (system_call_number == static_cast<uint64_t>(os::abi::SystemCallNumber::SpawnProcess)) {
        frame->register_rax =
            static_cast<uint64_t>(DispatchSpawnProcess(frame->register_rdi, frame->register_rsi));
        return frame;
    }
    if (system_call_number == static_cast<uint64_t>(os::abi::SystemCallNumber::ExecProcess)) {
        return DispatchExecProcess(*frame, frame->register_rdi, frame->register_rsi);
    }
    if (system_call_number == static_cast<uint64_t>(os::abi::SystemCallNumber::WaitProcess)) {
        return DispatchWaitProcess(*frame, frame->register_rdi, frame->register_rsi,
                                   frame->register_rdx);
    }
    if (system_call_number == static_cast<uint64_t>(os::abi::SystemCallNumber::WaitProcessEvent)) {
        return DispatchWaitProcessEvent(*frame, frame->register_rdi, frame->register_rsi,
                                        frame->register_rdx, frame->register_r10);
    }
    if (system_call_number ==
        static_cast<uint64_t>(os::abi::SystemCallNumber::MapAnonymousMemory)) {
        frame->register_rax = static_cast<uint64_t>(DispatchMapAnonymousMemory(
            frame->register_rdi, frame->register_rsi, frame->register_rdx, frame->register_r10));
        return frame;
    }
    if (system_call_number == static_cast<uint64_t>(os::abi::SystemCallNumber::UnmapMemory)) {
        frame->register_rax =
            static_cast<uint64_t>(DispatchUnmapMemory(frame->register_rdi, frame->register_rsi));
        return frame;
    }
    if (system_call_number == static_cast<uint64_t>(os::abi::SystemCallNumber::SetProgramBreak)) {
        frame->register_rax = static_cast<uint64_t>(DispatchSetProgramBreak(frame->register_rdi));
        return frame;
    }
    if (system_call_number ==
        static_cast<uint64_t>(os::abi::SystemCallNumber::GetVirtualMemoryStatistics)) {
        frame->register_rax = static_cast<uint64_t>(
            DispatchGetVirtualMemoryStatistics(frame->register_rdi, frame->register_rsi));
        return frame;
    }
    if (system_call_number == static_cast<uint64_t>(os::abi::SystemCallNumber::MapFileMemory)) {
        frame->register_rax =
            static_cast<uint64_t>(DispatchMapFileMemory(frame->register_rdi, frame->register_rsi));
        return frame;
    }
    if (system_call_number == static_cast<uint64_t>(os::abi::SystemCallNumber::ForkProcess)) {
        return DispatchForkProcess(*frame);
    }
    if (system_call_number == static_cast<uint64_t>(os::abi::SystemCallNumber::CreatePipe)) {
        frame->register_rax =
            static_cast<uint64_t>(DispatchCreatePipe(frame->register_rdi, frame->register_rsi));
        return frame;
    }
    if (system_call_number ==
        static_cast<uint64_t>(os::abi::SystemCallNumber::DuplicateDescriptorTo)) {
        frame->register_rax = static_cast<uint64_t>(DispatchDuplicateDescriptorTo(
            frame->register_rdi, frame->register_rsi, frame->register_rdx));
        return frame;
    }
    if (system_call_number == static_cast<uint64_t>(os::abi::SystemCallNumber::CreateThread)) {
        frame->register_rax = static_cast<uint64_t>(
            DispatchCreateThread(*frame, frame->register_rdi, frame->register_rsi));
        return frame;
    }
    if (system_call_number == static_cast<uint64_t>(os::abi::SystemCallNumber::ExitThread)) {
        return ExitCurrentUserThread(*frame, frame->register_rdi);
    }
    if (system_call_number == static_cast<uint64_t>(os::abi::SystemCallNumber::JoinThread)) {
        return DispatchJoinThread(*frame, frame->register_rdi, frame->register_rsi,
                                  frame->register_rdx);
    }
    if (system_call_number ==
        static_cast<uint64_t>(os::abi::SystemCallNumber::SetThreadLocalStorage)) {
        frame->register_rax = static_cast<uint64_t>(
            MapUserThreadStatus(SetCurrentThreadLocalStorage(frame->register_rdi)));
        return frame;
    }
    if (system_call_number == static_cast<uint64_t>(os::abi::SystemCallNumber::GetThreadId)) {
        frame->register_rax = CurrentThreadId();
        return frame;
    }
    if (system_call_number == static_cast<uint64_t>(os::abi::SystemCallNumber::WaitPrivateFutex)) {
        return DispatchWaitPrivateFutex(*frame, frame->register_rdi,
                                        static_cast<uint32_t>(frame->register_rsi), false,
                                        OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES);
    }
    if (system_call_number == static_cast<uint64_t>(os::abi::SystemCallNumber::WakePrivateFutex)) {
        uint64_t woken_thread_count = OS_KERNEL_SYSTEM_CALL_EMPTY_WAKE_COUNT;
        const PrivateFutexWaitStatus status = WakeCurrentProcessPrivateFutex(
            frame->register_rdi, frame->register_rsi, woken_thread_count);
        frame->register_rax = static_cast<uint64_t>(status == PrivateFutexWaitStatus::Succeeded
                                                        ? static_cast<int64_t>(woken_thread_count)
                                                        : MapPrivateFutexWaitStatus(status));
        return frame;
    }
    if (system_call_number == static_cast<uint64_t>(os::abi::SystemCallNumber::GetMonotonicTime)) {
        frame->register_rax = GetMonotonicNanoseconds();
        return frame;
    }
    if (system_call_number == static_cast<uint64_t>(os::abi::SystemCallNumber::SleepUntil)) {
        return DispatchSleepUntil(*frame, frame->register_rdi);
    }
    if (system_call_number ==
        static_cast<uint64_t>(os::abi::SystemCallNumber::WaitPrivateFutexUntil)) {
        return DispatchWaitPrivateFutex(*frame, frame->register_rdi,
                                        static_cast<uint32_t>(frame->register_rsi), true,
                                        frame->register_rdx);
    }
    if (system_call_number == static_cast<uint64_t>(os::abi::SystemCallNumber::SetSignalAction)) {
        frame->register_rax = static_cast<uint64_t>(DispatchSetSignalAction(
            frame->register_rdi, frame->register_rsi, frame->register_rdx, frame->register_r10));
        return frame;
    }
    if (system_call_number == static_cast<uint64_t>(os::abi::SystemCallNumber::SetSignalMask)) {
        frame->register_rax =
            static_cast<uint64_t>(DispatchSetSignalMask(frame->register_rdi, frame->register_rsi));
        return frame;
    }
    if (system_call_number == static_cast<uint64_t>(os::abi::SystemCallNumber::SendProcessSignal)) {
        frame->register_rax = static_cast<uint64_t>(
            MapUserSignalStatus(SendSignalToProcess(frame->register_rdi, frame->register_rsi)));
        return frame;
    }
    if (system_call_number ==
        static_cast<uint64_t>(os::abi::SystemCallNumber::SendProcessGroupSignal)) {
        uint64_t target_process_count = OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES;
        const UserSignalStatus status = SendSignalToProcessGroup(
            frame->register_rdi, frame->register_rsi, target_process_count);
        frame->register_rax = static_cast<uint64_t>(status == UserSignalStatus::Succeeded
                                                        ? static_cast<int64_t>(target_process_count)
                                                        : MapUserSignalStatus(status));
        return frame;
    }
    if (system_call_number == static_cast<uint64_t>(os::abi::SystemCallNumber::SignalReturn)) {
        return ReturnFromCurrentThreadSignal(*frame, frame->register_rdi);
    }
    if (system_call_number == static_cast<uint64_t>(os::abi::SystemCallNumber::GetProcessGroup)) {
        uint64_t process_group_id = OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES;
        const UserSignalStatus status = GetCurrentProcessGroup(process_group_id);
        frame->register_rax = static_cast<uint64_t>(status == UserSignalStatus::Succeeded
                                                        ? static_cast<int64_t>(process_group_id)
                                                        : MapUserSignalStatus(status));
        return frame;
    }
    if (system_call_number == static_cast<uint64_t>(os::abi::SystemCallNumber::SetProcessGroup)) {
        frame->register_rax =
            static_cast<uint64_t>(MapUserSignalStatus(SetCurrentProcessGroup(frame->register_rdi)));
        return frame;
    }
    if (system_call_number == static_cast<uint64_t>(os::abi::SystemCallNumber::CreateSession)) {
        uint64_t session_id = OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES;
        const UserSignalStatus status = CreateCurrentSession(session_id);
        frame->register_rax = static_cast<uint64_t>(status == UserSignalStatus::Succeeded
                                                        ? static_cast<int64_t>(session_id)
                                                        : MapUserSignalStatus(status));
        return frame;
    }
    if (system_call_number == static_cast<uint64_t>(os::abi::SystemCallNumber::GetSession)) {
        uint64_t session_id = OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES;
        const UserSignalStatus status = GetCurrentSession(session_id);
        frame->register_rax = static_cast<uint64_t>(status == UserSignalStatus::Succeeded
                                                        ? static_cast<int64_t>(session_id)
                                                        : MapUserSignalStatus(status));
        return frame;
    }
    if (system_call_number ==
        static_cast<uint64_t>(os::abi::SystemCallNumber::SetProcessGroupFor)) {
        frame->register_rax = static_cast<uint64_t>(MapUserSignalStatus(
            SetCurrentProcessGroupFor(frame->register_rdi, frame->register_rsi)));
        return frame;
    }
    if (system_call_number ==
        static_cast<uint64_t>(os::abi::SystemCallNumber::GetTerminalInformation)) {
        frame->register_rax = static_cast<uint64_t>(
            DispatchGetTerminalInformation(frame->register_rdi, frame->register_rsi));
        return frame;
    }
    if (system_call_number ==
        static_cast<uint64_t>(os::abi::SystemCallNumber::SetTerminalForegroundGroup)) {
        frame->register_rax = static_cast<uint64_t>(
            MapUserSignalStatus(SetCurrentTerminalForegroundGroup(frame->register_rdi)));
        return frame;
    }
    frame->register_rax = static_cast<uint64_t>(os::abi::OS_ABI_SYSTEM_CALL_RESULT_UNKNOWN_NUMBER);
    return frame;
}

extern "C" ExceptionFrame *OsKernelDispatchSystemCall(ExceptionFrame *frame) noexcept {
    if (frame == nullptr || !ValidateUserSystemCallFrame(*frame)) {
        if (frame != nullptr) {
            LogRejectedUserReturn(*frame);
        }
        HaltProcessor();
    }
    const UserContextEntryMethod entry_method = DecodeUserContextEntryMethod(AsUserContext(*frame));
    if (GetCpuLocal().BeginSystemCall(entry_method) != CpuLocalStatus::Succeeded) {
        HaltProcessor();
    }
    return DispatchValidatedSystemCall(frame);
}

extern "C" ExceptionFrame *OsKernelPrepareUserReturn(ExceptionFrame *frame) noexcept {
    if (frame == nullptr) {
        HaltProcessor();
    }
    ExceptionFrame *resume_frame = frame;
    if (GetCpuLocal().ConsumeRescheduleRequest()) {
        if (GetCpuLocal().RecordReturnReschedule() != CpuLocalStatus::Succeeded) {
            HaltProcessor();
        }
        resume_frame = RescheduleBeforeUserReturn(*resume_frame);
    }
    resume_frame = PrepareCurrentThreadSignalDelivery(*resume_frame);
    while (!PrepareUserReturnFrameMemory(*resume_frame) ||
           !ValidateUserReturnFrame(*resume_frame)) {
        LogRejectedUserReturn(*resume_frame);
        const UserContextEntryMethod entry_method =
            DecodeUserContextEntryMethod(AsUserContext(*resume_frame));
        if (GetCpuLocal().RecordUserReturn(entry_method, UserReturnMethod::Rejected) !=
            CpuLocalStatus::Succeeded) {
            HaltProcessor();
        }
        resume_frame = TerminateCurrentProcessFromInvalidReturn(*resume_frame);
    }
    return resume_frame;
}

extern "C" uint64_t OsKernelSelectUserReturn(const ExceptionFrame *frame) noexcept {
    if (frame == nullptr || !ValidateUserReturnFrame(*frame)) {
        HaltProcessor();
    }
    const UserContext &context = AsUserContext(*frame);
    const UserContextEntryMethod entry_method = DecodeUserContextEntryMethod(context);
    const UserReturnMethod return_method =
        SelectUserReturnMethod(context, CurrentUserContextRequirements());
    if (return_method == UserReturnMethod::Rejected ||
        GetCpuLocal().RecordUserReturn(entry_method, return_method) != CpuLocalStatus::Succeeded) {
        HaltProcessor();
    }
    if (GetCpuLocal().EndSystemCall() != CpuLocalStatus::Succeeded) {
        HaltProcessor();
    }
    return static_cast<uint64_t>(return_method);
}

}

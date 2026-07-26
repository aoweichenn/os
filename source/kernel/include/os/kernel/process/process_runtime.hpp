#pragma once

#include "os/kernel/io/console_input.hpp"
#include "os/kernel/arch/exception_frame.hpp"
#include "os/kernel/fs/file_system.hpp"
#include "os/kernel/io/io_descriptor.hpp"
#include "os/kernel/memory/kernel_stack_manager.hpp"
#include "os/kernel/memory/physical_frame_allocator.hpp"
#include "os/kernel/ipc/pipe.hpp"
#include "os/kernel/process/process_scheduler.hpp"
#include "os/kernel/memory/resource_snapshot.hpp"
#include "os/kernel/user/user_elf.hpp"
#include "os/kernel/user/user_memory.hpp"
#include "os/kernel/user/user_program_images.hpp"

#include <stdint.h>

namespace os::kernel {

enum class ProcessTerminationReason : uint64_t {
    None,
    Exited,
    Exception,
};

enum class ProcessRuntimeStatus : uint64_t {
    Succeeded,
    NotInitialized,
    AlreadyActive,
    InvalidElf,
    AddressSpaceFailure,
    SchedulerFailure,
    KernelStackFailure,
    ContextFrameFailure,
    PageTableActivationFailure,
    NoReadyProcess,
    ResourceLeakDetected,
};

enum class ProcessIoStatus : uint64_t {
    Succeeded,
    WouldBlock,
    EndOfFile,
    BrokenPipe,
    InvalidDescriptor,
    PermissionDenied,
    DeviceFailure,
    FileSystemFailure,
    InvalidArgument,
};

struct ProcessCreationResult final {
    uint64_t process_id;
    uint64_t process_index;
    uint64_t root_physical_address;
    uint64_t entry_virtual_address;
    uint64_t mapped_page_count;
    uint64_t kernel_stack_lower_guard_address;
    uint64_t kernel_stack_top_address;
    uint64_t kernel_stack_upper_guard_address;
};

struct ProcessExecutionResult final {
    uint64_t process_id;
    UserProgramSelection selection;
    ProcessTerminationReason termination_reason;
    int64_t exit_code;
    uint64_t exception_vector;
    uint64_t exception_error_code;
    uint64_t exception_instruction_pointer;
    uint64_t page_fault_address;
    uint64_t system_call_count;
    uint64_t root_physical_address;
    uint64_t mapped_page_count;
    uint64_t run_tick_count;
    uint64_t dispatch_count;
    uint64_t pipe_bytes_read;
    uint64_t pipe_bytes_written;
    uint64_t file_system_bytes_read;
    uint64_t file_system_bytes_written;
    uint64_t console_bytes_read;
    uint64_t console_bytes_written;
};

struct ProcessIpcStatistics final {
    PipeStatistics pipe;
    uint64_t reader_block_count;
    uint64_t writer_block_count;
    uint64_t end_of_file_observation_count;
    uint64_t broken_pipe_observation_count;
};

struct ProcessRuntimeStatistics final {
    ProcessSchedulerStatistics scheduler;
    PhysicalFrameAllocatorStatistics frames_before_processes;
    PhysicalFrameAllocatorStatistics frames_after_processes;
    KernelVirtualAddressAllocatorStatistics virtual_addresses_before_processes;
    KernelVirtualAddressAllocatorStatistics virtual_addresses_after_processes;
    KernelStackManagerStatistics kernel_stacks_before_processes;
    KernelStackManagerStatistics kernel_stacks_after_processes;
    ResourceSnapshot resource_snapshot_before_processes;
    ResourceSnapshot resource_snapshot_after_processes;
    ResourceSnapshotDifference resource_snapshot_difference;
    ProcessIpcStatistics ipc;
    ConsoleInputStatistics console_input;
    ProcessExecutionResult processes[OS_KERNEL_PROCESS_CAPACITY];
};

[[nodiscard]] ProcessRuntimeStatus InitializeProcessRuntime() noexcept;
[[nodiscard]] ProcessRuntimeStatus AttachProcessFileSystem(FileSystem &file_system) noexcept;
[[nodiscard]] ProcessRuntimeStatus
CreateProcess(UserProgramSelection selection, ProcessCreationResult &creation_result,
              UserElfValidationStatus &elf_validation_status,
              UserAddressSpaceStatus &address_space_status) noexcept;
[[nodiscard]] ProcessRuntimeStatus ExecuteProcesses() noexcept;
[[nodiscard]] ProcessRuntimeStatistics GetProcessRuntimeStatistics() noexcept;
[[nodiscard]] bool IsProcessSchedulingActive() noexcept;
[[nodiscard]] uint64_t CurrentProcessId() noexcept;
[[nodiscard]] UserProgramSelection CurrentProcessSelection() noexcept;
void RecordCurrentProcessSystemCall() noexcept;
[[nodiscard]] bool CurrentProcessCanReadPipe() noexcept;
[[nodiscard]] bool CurrentProcessCanWritePipe() noexcept;
[[nodiscard]] PipeStatus TryReadCurrentProcessPipe(uint8_t *destination, uint64_t capacity_bytes,
                                                   uint64_t &read_bytes) noexcept;
[[nodiscard]] PipeStatus TryWriteCurrentProcessPipe(const uint8_t *source, uint64_t length_bytes,
                                                    uint64_t &written_bytes) noexcept;
[[nodiscard]] PipeStatus CloseCurrentProcessPipeReader() noexcept;
[[nodiscard]] PipeStatus CloseCurrentProcessPipeWriter() noexcept;
[[nodiscard]] FileSystemStatus OpenCurrentProcessFile(const uint8_t *path,
                                                      uint64_t path_length_bytes,
                                                      const FileSystemOpenOptions &options,
                                                      uint64_t &file_descriptor) noexcept;
[[nodiscard]] FileSystemStatus ReadCurrentProcessFile(uint64_t file_descriptor,
                                                      uint8_t *destination, uint64_t capacity_bytes,
                                                      uint64_t &read_bytes) noexcept;
[[nodiscard]] FileSystemStatus WriteCurrentProcessFile(uint64_t file_descriptor,
                                                       const uint8_t *source, uint64_t length_bytes,
                                                       uint64_t &written_bytes) noexcept;
[[nodiscard]] FileSystemStatus CloseCurrentProcessFile(uint64_t file_descriptor) noexcept;
[[nodiscard]] FileSystemStatus CreateCurrentProcessDirectory(const uint8_t *path,
                                                             uint64_t path_length_bytes) noexcept;
[[nodiscard]] FileSystemStatus SyncCurrentProcessFileSystem() noexcept;
[[nodiscard]] ProcessIoStatus
TryReadCurrentProcessDescriptor(uint64_t descriptor, uint8_t *destination, uint64_t capacity_bytes,
                                uint64_t &read_bytes,
                                FileSystemStatus &file_system_status) noexcept;
[[nodiscard]] ProcessIoStatus
TryWriteCurrentProcessDescriptor(uint64_t descriptor, const uint8_t *source, uint64_t length_bytes,
                                 uint64_t &written_bytes,
                                 FileSystemStatus &file_system_status) noexcept;
[[nodiscard]] ProcessIoStatus
CloseCurrentProcessDescriptor(uint64_t descriptor, FileSystemStatus &file_system_status) noexcept;
[[nodiscard]] FileSystemStatus OpenCurrentProcessDirectory(const uint8_t *path,
                                                           uint64_t path_length_bytes,
                                                           uint64_t &file_descriptor) noexcept;
[[nodiscard]] FileSystemStatus ReadCurrentProcessDirectory(uint64_t file_descriptor,
                                                           FileSystemDirectoryEntry &entry,
                                                           bool &end_of_directory) noexcept;
[[nodiscard]] ProcessIoStatus CurrentProcessDescriptorReadCanProgress(uint64_t descriptor,
                                                                      bool &can_progress) noexcept;
[[nodiscard]] ProcessIoStatus CurrentProcessDescriptorWriteCanProgress(uint64_t descriptor,
                                                                       bool &can_progress) noexcept;
void SubmitConsoleCharacter(uint8_t character) noexcept;
[[nodiscard]] bool ProcessPipeReadCanProgress() noexcept;
[[nodiscard]] bool ProcessPipeWriteCanProgress() noexcept;
[[nodiscard]] ProcessRuntimeStatus BlockCurrentProcess(ExceptionFrame &frame,
                                                       ProcessWaitReason wait_reason,
                                                       ExceptionFrame *&resume_frame) noexcept;
[[nodiscard]] ProcessRuntimeStatus WakeProcesses(ProcessWaitReason wait_reason,
                                                 uint64_t maximum_wake_count,
                                                 uint64_t &woken_process_count) noexcept;
[[nodiscard]] ExceptionFrame *HandleProcessTimerInterrupt(ExceptionFrame &frame) noexcept;
[[nodiscard]] ExceptionFrame *TerminateCurrentProcessFromExit(ExceptionFrame &frame,
                                                              int64_t exit_code) noexcept;
[[nodiscard]] ExceptionFrame *
TerminateCurrentProcessFromException(ExceptionFrame &frame, uint64_t page_fault_address) noexcept;
}

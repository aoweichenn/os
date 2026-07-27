#pragma once

#include "os/abi/system_call.hpp"
#include "os/kernel/arch/exception_frame.hpp"
#include "os/kernel/arch/extended_state.hpp"
#include "os/kernel/arch/user_context.hpp"
#include "os/kernel/fs/file_system.hpp"
#include "os/kernel/fs/vfs.hpp"
#include "os/kernel/io/console_input.hpp"
#include "os/kernel/io/file_description.hpp"
#include "os/kernel/io/file_table.hpp"
#include "os/kernel/ipc/pipe.hpp"
#include "os/kernel/ipc/pipe_manager.hpp"
#include "os/kernel/memory/kernel_stack_manager.hpp"
#include "os/kernel/memory/physical_frame_allocator.hpp"
#include "os/kernel/memory/resource_snapshot.hpp"
#include "os/kernel/process/process_tree.hpp"
#include "os/kernel/process/thread_scheduler.hpp"
#include "os/kernel/sync/private_futex.hpp"
#include "os/kernel/user/user_elf.hpp"
#include "os/kernel/user/user_memory.hpp"
#include "os/kernel/user/user_program_images.hpp"

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_RESULT_CAPACITY = 16ULL;

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
    NoReadyThread,
    ResourceLeakDetected,
    ExtendedStateFailure,
    CapacitySelfTestFailure,
    LockFailure,
    CpuLocalFailure,
    NativeSystemCallFailure,
    DescriptorTableFailure,
    FileSystemFailure,
    ProcessTreeFailure,
    InvalidArguments,
    ArgumentListTooLarge,
    ExecutableReadFailure,
    ProcessLimitExceeded,
    ForkFailure,
    PipeFailure,
    ThreadFailure,
    FutexFailure,
};

enum class ProcessWaitStatus : uint64_t {
    Succeeded,
    WouldBlock,
    NoChild,
    InvalidArgument,
    RuntimeFailure,
};

enum class UserThreadStatus : uint64_t {
    Succeeded,
    WouldBlock,
    InvalidArgument,
    InvalidMemory,
    ThreadLimitExceeded,
    ThreadNotFound,
    AlreadyJoined,
    Deadlock,
    RuntimeFailure,
};

enum class PrivateFutexWaitStatus : uint64_t {
    Succeeded,
    TimedOut,
    ValueChanged,
    InvalidArgument,
    InvalidMemory,
    CapacityExhausted,
    RuntimeFailure,
};

enum class TimedWaitStatus : uint64_t {
    Succeeded,
    DeadlineReached,
    InvalidArgument,
    RuntimeFailure,
};

struct UserThreadRuntimeStatistics final {
    uint64_t create_count;
    uint64_t exit_count;
    uint64_t join_count;
    uint64_t process_exit_cancelled_thread_count;
    uint64_t exec_cancelled_thread_count;
    uint64_t thread_local_storage_update_count;
};

struct KernelProgramString final {
    const uint8_t *data;
    uint64_t length_bytes;
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
    DescriptorLimitExceeded,
    PipeLimitExceeded,
    ObjectFailure,
    InvalidArgument,
};

struct ProcessCreationResult final {
    uint64_t process_id;
    uint64_t process_index;
    uint64_t thread_id;
    uint64_t thread_index;
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
    PipeManagerStatistics dynamic_pipes;
    uint64_t reader_block_count;
    uint64_t writer_block_count;
    uint64_t end_of_file_observation_count;
    uint64_t broken_pipe_observation_count;
};

struct ProcessRuntimeStatistics final {
    ThreadSchedulerStatistics scheduler;
    ExtendedStateConfiguration extended_state;
    uint64_t configured_process_capacity;
    uint64_t configured_thread_capacity;
    uint64_t configured_threads_per_process;
    uint64_t capacity_self_test_process_count;
    uint64_t capacity_self_test_thread_count;
    uint64_t capacity_self_test_threads_per_process;
    PhysicalFrameAllocatorStatistics frames_before_processes;
    PhysicalFrameAllocatorStatistics frames_after_processes;
    KernelVirtualAddressAllocatorStatistics virtual_addresses_before_processes;
    KernelVirtualAddressAllocatorStatistics virtual_addresses_after_processes;
    KernelStackManagerStatistics kernel_stacks_before_processes;
    KernelStackManagerStatistics kernel_stacks_after_processes;
    VirtualMemoryAreaPoolStatistics virtual_memory_areas_before_processes;
    VirtualMemoryAreaPoolStatistics virtual_memory_areas_after_processes;
    UserPageReferenceStatistics user_page_references_before_processes;
    UserPageReferenceStatistics user_page_references_after_processes;
    ResourceSnapshot resource_snapshot_before_processes;
    ResourceSnapshot resource_snapshot_after_processes;
    ResourceSnapshotDifference resource_snapshot_difference;
    ProcessIpcStatistics ipc;
    UserThreadRuntimeStatistics user_threads;
    PrivateFutexStatistics private_futexes;
    ProcessTreeStatistics process_tree;
    ConsoleInputStatistics console_input;
    KernelObjectManagerStatistics object_manager;
    FileDescriptionManagerStatistics file_descriptions;
    FileTableStatistics file_tables[OS_KERNEL_PROCESS_RUNTIME_RESULT_CAPACITY];
    ProcessExecutionResult processes[OS_KERNEL_PROCESS_RUNTIME_RESULT_CAPACITY];
};

[[nodiscard]] ProcessRuntimeStatus InitializeProcessRuntime() noexcept;
[[nodiscard]] ProcessRuntimeStatus AttachProcessVfs(fs::Vfs &vfs) noexcept;
[[nodiscard]] ProcessRuntimeStatus
CreateProcess(UserProgramSelection selection, ProcessCreationResult &creation_result,
              UserElfValidationStatus &elf_validation_status,
              UserAddressSpaceStatus &address_space_status) noexcept;
[[nodiscard]] ProcessRuntimeStatus CreateInitialProcessFromPath(
    const uint8_t *path, uint64_t path_length_bytes, const KernelProgramString *arguments,
    uint64_t argument_count, const KernelProgramString *environment, uint64_t environment_count,
    ProcessCreationResult &creation_result, UserElfValidationStatus &elf_validation_status,
    UserAddressSpaceStatus &address_space_status) noexcept;
[[nodiscard]] ProcessRuntimeStatus SpawnCurrentProcess(const os::abi::ProcessLaunchRequest &request,
                                                       uint64_t &process_id) noexcept;
[[nodiscard]] ProcessRuntimeStatus ForkCurrentProcess(ExceptionFrame &frame,
                                                      uint64_t &process_id) noexcept;
[[nodiscard]] ProcessRuntimeStatus
ExecCurrentProcess(ExceptionFrame &frame, const os::abi::ProcessLaunchRequest &request) noexcept;
[[nodiscard]] ProcessWaitStatus
TryWaitCurrentProcess(uint64_t requested_process_id,
                      os::abi::ProcessWaitResult &wait_result) noexcept;
[[nodiscard]] UserThreadStatus
CreateCurrentProcessThread(ExceptionFrame &frame, const os::abi::ThreadCreateRequest &request,
                           uint64_t &thread_id) noexcept;
[[nodiscard]] ExceptionFrame *ExitCurrentUserThread(ExceptionFrame &frame,
                                                    uint64_t exit_value) noexcept;
[[nodiscard]] UserThreadStatus
TryJoinCurrentProcessThread(uint64_t requested_thread_id,
                            os::abi::ThreadJoinResult &join_result) noexcept;
[[nodiscard]] UserThreadStatus
SetCurrentThreadLocalStorage(uint64_t thread_local_storage_base) noexcept;
[[nodiscard]] PrivateFutexWaitStatus
WaitCurrentProcessPrivateFutex(ExceptionFrame &frame, uint64_t user_address,
                               uint32_t expected_value, bool deadline_enabled,
                               uint64_t deadline_nanoseconds,
                               ExceptionFrame *&resume_frame) noexcept;
[[nodiscard]] TimedWaitStatus
SleepCurrentThreadUntil(ExceptionFrame &frame, uint64_t deadline_nanoseconds,
                        ExceptionFrame *&resume_frame) noexcept;
[[nodiscard]] PrivateFutexWaitStatus
WakeCurrentProcessPrivateFutex(uint64_t user_address, uint64_t maximum_wake_count,
                               uint64_t &woken_thread_count) noexcept;
[[nodiscard]] ProcessRuntimeStatus ExecuteProcesses() noexcept;
[[nodiscard]] ProcessRuntimeStatistics GetProcessRuntimeStatistics() noexcept;
[[nodiscard]] bool IsProcessSchedulingActive() noexcept;
[[nodiscard]] uint64_t CurrentProcessId() noexcept;
[[nodiscard]] uint64_t CurrentThreadId() noexcept;
[[nodiscard]] UserProgramSelection CurrentProcessSelection() noexcept;
[[nodiscard]] UserVirtualMemoryStatus
MapCurrentProcessAnonymousMemory(uint64_t requested_address, uint64_t length_bytes,
                                 uint64_t protection_flags, uint64_t map_flags,
                                 uint64_t &mapped_address) noexcept;
[[nodiscard]] UserVirtualMemoryStatus
MapCurrentProcessFileMemory(const os::abi::FileMemoryMapRequest &request,
                            uint64_t &mapped_address) noexcept;
[[nodiscard]] UserVirtualMemoryStatus UnmapCurrentProcessMemory(uint64_t address,
                                                                uint64_t length_bytes) noexcept;
[[nodiscard]] UserVirtualMemoryStatus
SetCurrentProcessProgramBreak(uint64_t requested_address, uint64_t &program_break_address) noexcept;
[[nodiscard]] os::abi::VirtualMemoryStatistics GetCurrentProcessVirtualMemoryStatistics() noexcept;
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
                                                      const fs::OpenOptions &options,
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
[[nodiscard]] FileSystemStatus RemoveCurrentProcessFile(const uint8_t *path,
                                                        uint64_t path_length_bytes) noexcept;
[[nodiscard]] FileSystemStatus RemoveCurrentProcessDirectory(const uint8_t *path,
                                                             uint64_t path_length_bytes) noexcept;
[[nodiscard]] FileSystemStatus
RenameCurrentProcessPath(const uint8_t *source_path, uint64_t source_path_length_bytes,
                         const uint8_t *destination_path,
                         uint64_t destination_path_length_bytes) noexcept;
[[nodiscard]] FileSystemStatus TruncateCurrentProcessFile(const uint8_t *path,
                                                          uint64_t path_length_bytes,
                                                          uint64_t size_bytes) noexcept;
[[nodiscard]] FileSystemStatus StatCurrentProcessPath(const uint8_t *path,
                                                      uint64_t path_length_bytes,
                                                      fs::NodeInformation &information) noexcept;
[[nodiscard]] FileSystemStatus SyncCurrentProcessFileSystem() noexcept;
[[nodiscard]] FileSystemStatus ChangeCurrentProcessDirectory(const uint8_t *path,
                                                             uint64_t path_length_bytes) noexcept;
[[nodiscard]] FileSystemStatus
GetCurrentProcessWorkingDirectory(uint8_t *destination, uint64_t capacity_bytes,
                                  uint64_t &path_length_bytes) noexcept;
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
[[nodiscard]] ProcessIoStatus
DuplicateCurrentProcessDescriptor(uint64_t source_descriptor, uint64_t minimum_descriptor,
                                  uint64_t descriptor_flags,
                                  uint64_t &destination_descriptor) noexcept;
[[nodiscard]] ProcessIoStatus
DuplicateCurrentProcessDescriptorTo(uint64_t source_descriptor, uint64_t destination_descriptor,
                                    uint64_t descriptor_flags) noexcept;
[[nodiscard]] ProcessIoStatus CreateCurrentProcessPipe(uint64_t &reader_descriptor,
                                                       uint64_t &writer_descriptor) noexcept;
[[nodiscard]] ProcessIoStatus GetCurrentProcessDescriptorFlags(uint64_t descriptor,
                                                               uint64_t &descriptor_flags) noexcept;
[[nodiscard]] ProcessIoStatus SetCurrentProcessDescriptorFlags(uint64_t descriptor,
                                                               uint64_t descriptor_flags) noexcept;
[[nodiscard]] ProcessIoStatus SetCurrentProcessDescriptorSoftLimit(uint64_t soft_limit) noexcept;
[[nodiscard]] ProcessIoStatus GetCurrentProcessDescriptorLimits(uint64_t &soft_limit,
                                                                uint64_t &hard_limit) noexcept;
[[nodiscard]] FileSystemStatus OpenCurrentProcessDirectory(const uint8_t *path,
                                                           uint64_t path_length_bytes,
                                                           uint64_t &file_descriptor) noexcept;
[[nodiscard]] FileSystemStatus ReadCurrentProcessDirectory(uint64_t file_descriptor,
                                                           fs::DirectoryEntry &entry,
                                                           bool &end_of_directory) noexcept;
[[nodiscard]] ProcessIoStatus CurrentProcessDescriptorReadCanProgress(uint64_t descriptor,
                                                                      bool &can_progress) noexcept;
[[nodiscard]] ProcessIoStatus CurrentProcessDescriptorWriteCanProgress(uint64_t descriptor,
                                                                       bool &can_progress) noexcept;
void SubmitConsoleCharacter(uint8_t character) noexcept;
[[nodiscard]] bool ProcessPipeReadCanProgress() noexcept;
[[nodiscard]] bool ProcessPipeWriteCanProgress() noexcept;
[[nodiscard]] ProcessRuntimeStatus BlockCurrentThread(ExceptionFrame &frame,
                                                      WaitCondition wait_condition,
                                                      ExceptionFrame *&resume_frame) noexcept;
[[nodiscard]] ProcessRuntimeStatus WakeThreads(WaitCondition wait_condition, WakeReason wake_reason,
                                               uint64_t maximum_wake_count,
                                               uint64_t &woken_thread_count) noexcept;
[[nodiscard]] ExceptionFrame *HandleProcessTimerInterrupt(ExceptionFrame &frame) noexcept;
[[nodiscard]] uint64_t
HandleProcessDeadlineInterrupt(uint64_t now_nanoseconds) noexcept;
[[nodiscard]] ExceptionFrame *RescheduleBeforeUserReturn(ExceptionFrame &frame) noexcept;
[[nodiscard]] bool CurrentThreadOwnsUserContext(const ExceptionFrame &frame) noexcept;
[[nodiscard]] UserVirtualMemoryStatus
ResolveCurrentProcessUserReturnMemory(uint64_t instruction_pointer,
                                      uint64_t stack_pointer) noexcept;
[[nodiscard]] bool HandleCurrentProcessPageFault(ExceptionFrame &frame,
                                                 uint64_t fault_address) noexcept;
[[nodiscard]] ExceptionFrame *TerminateCurrentProcessFromExit(ExceptionFrame &frame,
                                                              int64_t exit_code) noexcept;
[[nodiscard]] ExceptionFrame *
TerminateCurrentProcessFromException(ExceptionFrame &frame, uint64_t page_fault_address) noexcept;
[[nodiscard]] ExceptionFrame *
TerminateCurrentProcessFromInvalidReturn(ExceptionFrame &frame) noexcept;
}

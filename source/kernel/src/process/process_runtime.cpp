#include "os/kernel/process/process_runtime.hpp"

#include "os/kernel/arch/cpu_local.hpp"
#include "os/kernel/arch/descriptor_tables.hpp"
#include "os/kernel/arch/native_system_call.hpp"
#include "os/kernel/arch/processor.hpp"
#include "os/kernel/arch/user_context.hpp"
#include "os/kernel/device/serial_port.hpp"
#include "os/kernel/fs/legacy_file_system.hpp"
#include "os/kernel/memory/memory_manager.hpp"
#include "os/kernel/process/program_arguments.hpp"
#include "os/kernel/sync/spin_lock.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_BOOLEAN_TRUE = 1ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_INVALID_RETURN_VECTOR = 13ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_NORMALIZED_ERROR_CODE = 0ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_INITIAL_USER_FLAGS = 0x202ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX = 0ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_WAKE_ALL_COUNT = OS_KERNEL_THREAD_CAPACITY_LIMIT;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_STACK_POINTER_PROBE_SIZE_BYTES = sizeof(uint64_t);
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_FUNCTIONAL_MEMORY_BYTES = 256ULL * 1024ULL * 1024ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_CAPACITY_MEMORY_BYTES =
    64ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_CAPACITY_TEST_USER_STACK_BASE = 0x00007FFFFFF00000ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_CAPACITY_TEST_USER_STACK_STRIDE_BYTES =
    0x0000000000001000ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_PIPE_READABLE_QUEUE_ID = 1ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_PIPE_WRITABLE_QUEUE_ID = 2ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_DESCRIPTOR_READABLE_QUEUE_ID = 3ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_DESCRIPTOR_WRITABLE_QUEUE_ID = 4ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_CHILD_EXIT_QUEUE_ID = 5ULL;
constexpr int64_t OS_KERNEL_PROCESS_RUNTIME_SUCCESS_EXIT_CODE = 0LL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_BOOTSTRAP_FILE_DESCRIPTOR_LIMIT = 64ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_INVALID_FILE_DESCRIPTOR = UINT64_MAX;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_ARGUMENT_COPY_CHUNK_SIZE_BYTES = 256ULL;
constexpr uint8_t OS_KERNEL_PROCESS_RUNTIME_STRING_TERMINATOR = 0U;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_INVALID_PARENT_INDEX = UINT64_MAX;
constexpr char OS_KERNEL_PROCESS_RUNTIME_SPAWN_PREFIX[] = "[OS][KERNEL][PROC] SPAWN_PID=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_EXEC_PREFIX[] = "[OS][KERNEL][PROC] EXEC_PID=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_EXIT_PREFIX[] = "[OS][KERNEL][PROC] EXIT_PID=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_REPARENT_PREFIX[] =
    "[OS][KERNEL][PROC] REPARENTED_CHILDREN=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_WAIT_PREFIX[] = "[OS][KERNEL][PROC] WAIT_REAPED_PID=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_VM_DEMAND_FAULT_PREFIX[] =
    "[OS][KERNEL][VM] DEMAND_FAULT_COUNT=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_VM_STACK_GROWTH_PREFIX[] =
    "[OS][KERNEL][VM] STACK_GROWTH_COUNT=";

[[nodiscard]] ProcessIoStatus
MapFileDescriptionStatus(const FileDescriptionStatus status) noexcept {
    if (status == FileDescriptionStatus::Succeeded) {
        return ProcessIoStatus::Succeeded;
    }
    if (status == FileDescriptionStatus::WouldBlock) {
        return ProcessIoStatus::WouldBlock;
    }
    if (status == FileDescriptionStatus::EndOfFile) {
        return ProcessIoStatus::EndOfFile;
    }
    if (status == FileDescriptionStatus::BrokenPipe) {
        return ProcessIoStatus::BrokenPipe;
    }
    if (status == FileDescriptionStatus::InvalidReference) {
        return ProcessIoStatus::InvalidDescriptor;
    }
    if (status == FileDescriptionStatus::PermissionDenied) {
        return ProcessIoStatus::PermissionDenied;
    }
    if (status == FileDescriptionStatus::DeviceFailure) {
        return ProcessIoStatus::DeviceFailure;
    }
    if (status == FileDescriptionStatus::FileSystemFailure) {
        return ProcessIoStatus::FileSystemFailure;
    }
    if (status == FileDescriptionStatus::ObjectFailure) {
        return ProcessIoStatus::ObjectFailure;
    }
    return ProcessIoStatus::InvalidArgument;
}

[[nodiscard]] ProcessIoStatus MapFileTableStatus(const FileTableStatus status) noexcept {
    if (status == FileTableStatus::Succeeded) {
        return ProcessIoStatus::Succeeded;
    }
    if (status == FileTableStatus::InvalidDescriptor) {
        return ProcessIoStatus::InvalidDescriptor;
    }
    if (status == FileTableStatus::SoftLimitExceeded ||
        status == FileTableStatus::AllocationFailed) {
        return ProcessIoStatus::DescriptorLimitExceeded;
    }
    if (status == FileTableStatus::InvalidFlags || status == FileTableStatus::InvalidLimit) {
        return ProcessIoStatus::InvalidArgument;
    }
    return ProcessIoStatus::ObjectFailure;
}

[[nodiscard]] PipeStatus MapProcessIoToPipeStatus(const ProcessIoStatus status) noexcept {
    if (status == ProcessIoStatus::Succeeded) {
        return PipeStatus::Succeeded;
    }
    if (status == ProcessIoStatus::WouldBlock) {
        return PipeStatus::WouldBlock;
    }
    if (status == ProcessIoStatus::EndOfFile) {
        return PipeStatus::EndOfFile;
    }
    if (status == ProcessIoStatus::BrokenPipe) {
        return PipeStatus::BrokenPipe;
    }
    if (status == ProcessIoStatus::InvalidDescriptor) {
        return PipeStatus::AlreadyClosed;
    }
    return PipeStatus::InvalidArgument;
}

[[nodiscard]] FileSystemStatus
MapProcessIoToFileSystemStatus(const ProcessIoStatus status,
                               const FileSystemStatus file_system_status) noexcept {
    if (status == ProcessIoStatus::Succeeded) {
        return FileSystemStatus::Succeeded;
    }
    if (status == ProcessIoStatus::InvalidDescriptor) {
        return FileSystemStatus::InvalidHandle;
    }
    if (status == ProcessIoStatus::PermissionDenied) {
        return FileSystemStatus::PermissionDenied;
    }
    if (status == ProcessIoStatus::FileSystemFailure) {
        return file_system_status;
    }
    if (status == ProcessIoStatus::DeviceFailure) {
        return FileSystemStatus::DeviceFailure;
    }
    if (status == ProcessIoStatus::DescriptorLimitExceeded ||
        status == ProcessIoStatus::ObjectFailure) {
        return FileSystemStatus::DataCapacityExhausted;
    }
    return FileSystemStatus::InvalidArgument;
}

struct ProcessRuntimeLimits final {
    uint64_t process_capacity;
    uint64_t thread_capacity;
    uint64_t maximum_threads_per_process;
    uint64_t file_descriptor_hard_limit;
};

struct ProcessRuntimeProcess final {
    UserAddressSpace address_space;
    ProcessExecutionResult result;
    FileTable file_table;
    fs::FsContext file_system_context;
    bool active;
};

struct VfsUserElfReaderContext final {
    fs::Vfs *vfs;
    fs::OpenFile *open_file;
};

struct alignas(OS_KERNEL_EXTENDED_STATE_AREA_ALIGNMENT_BYTES) ProcessRuntimeThread final {
    ExceptionFrame *saved_frame;
    FxSaveArea extended_state;
    bool active;
};

ThreadScheduler thread_scheduler;
ProcessEntry process_entries[OS_KERNEL_PROCESS_CAPACITY_LIMIT];
ThreadEntry thread_entries[OS_KERNEL_THREAD_CAPACITY_LIMIT];
Pipe process_pipe;
ConsoleInput process_console_input;
KernelObjectManager kernel_object_manager;
FileDescriptionManager file_description_manager;
ProcessRuntimeProcess runtime_processes[OS_KERNEL_PROCESS_CAPACITY_LIMIT];
ProcessRuntimeThread runtime_threads[OS_KERNEL_THREAD_CAPACITY_LIMIT];
WaitQueue pipe_readable_wait_queue;
WaitQueue pipe_writable_wait_queue;
WaitQueue descriptor_readable_wait_queue;
WaitQueue descriptor_writable_wait_queue;
WaitQueue child_exit_wait_queue;
ProcessTree process_tree;
ProcessTreeEntry process_tree_entries[OS_KERNEL_PROCESS_CAPACITY_LIMIT];
ProgramArgumentPlan program_argument_plan;
uint8_t launch_path_buffer[fs::OS_KERNEL_VFS_MAXIMUM_PATH_LENGTH_BYTES];
constinit IrqSaveSpinLock scheduler_lock{DisableInterrupts, RestoreInterrupts};
fs::Vfs *process_vfs;
ProcessRuntimeLimits process_runtime_limits;
PhysicalFrameAllocatorStatistics frames_before_processes;
PhysicalFrameAllocatorStatistics frames_after_processes;
KernelVirtualAddressAllocatorStatistics virtual_addresses_before_processes;
KernelVirtualAddressAllocatorStatistics virtual_addresses_after_processes;
KernelStackManagerStatistics kernel_stacks_before_processes;
KernelStackManagerStatistics kernel_stacks_after_processes;
VirtualMemoryAreaPoolStatistics virtual_memory_areas_before_processes;
VirtualMemoryAreaPoolStatistics virtual_memory_areas_after_processes;
ResourceSnapshot resource_snapshot_before_processes;
ResourceSnapshot resource_snapshot_after_processes;
ResourceSnapshotDifference resource_snapshot_difference;
uint64_t pipe_reader_block_count;
uint64_t pipe_writer_block_count;
uint64_t pipe_end_of_file_observation_count;
uint64_t pipe_broken_observation_count;
uint64_t capacity_self_test_process_count;
uint64_t capacity_self_test_thread_count;
uint64_t capacity_self_test_threads_per_process;
bool process_runtime_initialized;
bool process_scheduling_active;

ProcessEntry capacity_process_entries[OS_KERNEL_PROCESS_CAPACITY_LIMIT];
ThreadEntry capacity_thread_entries[OS_KERNEL_THREAD_CAPACITY_LIMIT];
FxSaveArea capacity_thread_extended_states[OS_KERNEL_THREAD_CAPACITY_LIMIT];
uint64_t capacity_process_roots[OS_KERNEL_PROCESS_CAPACITY_LIMIT];
bool capacity_stack_active[OS_KERNEL_THREAD_CAPACITY_LIMIT];

extern "C" void OsKernelEnterScheduledProcess(ExceptionFrame *frame) noexcept;
extern "C" [[noreturn]] void OsKernelReturnFromUserMode(uint64_t swap_gs_required) noexcept;

void WriteProcessRuntimeValue(const char *const prefix, const uint64_t value) noexcept {
    const SerialPort serial_port{OS_KERNEL_SERIAL_COM1_BASE_PORT};
    if (!serial_port.TryWriteHexLine(prefix, value)) {
        HaltProcessor();
    }
}

[[nodiscard]] bool ReadVfsUserElf(void *const context, const uint64_t offset_bytes,
                                  uint8_t *const destination,
                                  const uint64_t length_bytes) noexcept {
    if (context == nullptr || destination == nullptr) {
        return false;
    }
    VfsUserElfReaderContext &reader_context = *static_cast<VfsUserElfReaderContext *>(context);
    if (reader_context.vfs == nullptr || reader_context.open_file == nullptr ||
        !reader_context.open_file->open) {
        return false;
    }
    reader_context.open_file->offset_bytes = offset_bytes;
    uint64_t copied_bytes = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    while (copied_bytes < length_bytes) {
        uint64_t read_bytes = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        const fs::Status status =
            reader_context.vfs->Read(*reader_context.open_file, destination + copied_bytes,
                                     length_bytes - copied_bytes, read_bytes);
        if (status != fs::Status::Succeeded ||
            read_bytes == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
            return false;
        }
        copied_bytes += read_bytes;
    }
    return true;
}

[[nodiscard]] ProcessRuntimeLimits
SelectProcessRuntimeLimits(const uint64_t managed_memory_bytes) noexcept {
    if (managed_memory_bytes >= OS_KERNEL_PROCESS_RUNTIME_CAPACITY_MEMORY_BYTES) {
        return ProcessRuntimeLimits{
            .process_capacity = OS_KERNEL_PROCESS_CAPACITY_LIMIT,
            .thread_capacity = OS_KERNEL_THREAD_CAPACITY_LIMIT,
            .maximum_threads_per_process = OS_KERNEL_CAPACITY_THREADS_PER_PROCESS,
            .file_descriptor_hard_limit = OS_KERNEL_FILE_TABLE_MAXIMUM_HARD_LIMIT,
        };
    }
    if (managed_memory_bytes >= OS_KERNEL_PROCESS_RUNTIME_FUNCTIONAL_MEMORY_BYTES) {
        return ProcessRuntimeLimits{
            .process_capacity = OS_KERNEL_PROCESS_FUNCTIONAL_CAPACITY,
            .thread_capacity = OS_KERNEL_THREAD_FUNCTIONAL_CAPACITY,
            .maximum_threads_per_process = OS_KERNEL_FUNCTIONAL_THREADS_PER_PROCESS,
            .file_descriptor_hard_limit = OS_KERNEL_FILE_TABLE_FUNCTIONAL_HARD_LIMIT,
        };
    }
    return ProcessRuntimeLimits{
        .process_capacity = OS_KERNEL_PROCESS_BOOTSTRAP_CAPACITY,
        .thread_capacity = OS_KERNEL_THREAD_BOOTSTRAP_CAPACITY,
        .maximum_threads_per_process = OS_KERNEL_BOOTSTRAP_THREADS_PER_PROCESS,
        .file_descriptor_hard_limit = OS_KERNEL_PROCESS_RUNTIME_BOOTSTRAP_FILE_DESCRIPTOR_LIMIT,
    };
}

[[nodiscard]] bool DiscountPersistentVfsResources(ResourceSnapshot &snapshot,
                                                  const fs::ResourceUsage &usage) noexcept {
    if (snapshot.heap_consumed_bytes < usage.heap_consumed_bytes ||
        snapshot.heap_active_requested_bytes < usage.heap_active_requested_bytes ||
        snapshot.heap_allocation_count < usage.heap_allocation_count ||
        snapshot.vnode_count < usage.vnode_count) {
        return false;
    }
    snapshot.heap_consumed_bytes -= usage.heap_consumed_bytes;
    snapshot.heap_active_requested_bytes -= usage.heap_active_requested_bytes;
    snapshot.heap_allocation_count -= usage.heap_allocation_count;
    snapshot.vnode_count -= usage.vnode_count;
    return ValidateResourceSnapshot(snapshot) == ResourceSnapshotStatus::Succeeded;
}

void ResetRuntimeStorage() noexcept {
    for (uint64_t process_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         process_index < OS_KERNEL_PROCESS_CAPACITY_LIMIT; ++process_index) {
        runtime_processes[process_index].address_space = UserAddressSpace{};
        runtime_processes[process_index].result = ProcessExecutionResult{};
        runtime_processes[process_index].file_system_context = fs::FsContext{};
        runtime_processes[process_index].active = false;
    }
    for (uint64_t thread_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         thread_index < OS_KERNEL_THREAD_CAPACITY_LIMIT; ++thread_index) {
        runtime_threads[thread_index] = ProcessRuntimeThread{};
    }
}

[[nodiscard]] bool ReadThreadKernelStack(const uint64_t thread_index, KernelStack &stack) noexcept {
    ThreadEntry thread{};
    return thread_scheduler.ReadThread(thread_index, thread) == ThreadSchedulerStatus::Succeeded &&
           thread.kernel_stack_slot_index < OS_KERNEL_THREAD_CAPACITY_LIMIT &&
           GetKernelStackManager().Read(thread.kernel_stack_slot_index, stack) ==
               KernelStackManagerStatus::Succeeded;
}

[[nodiscard]] bool BuildInitialContextFrame(const uint64_t kernel_stack_slot_index,
                                            const UserAddressSpace &address_space,
                                            const ProgramArgumentLayout &argument_layout,
                                            ExceptionFrame *&saved_frame) noexcept {
    KernelStack stack{};
    if (GetKernelStackManager().Read(kernel_stack_slot_index, stack) !=
        KernelStackManagerStatus::Succeeded) {
        return false;
    }
    const uint64_t stack_top_address = KernelStackTopAddress(stack);
    if (stack_top_address < OS_KERNEL_USER_CONTEXT_SIZE_BYTES) {
        return false;
    }
    const uint64_t frame_address = stack_top_address - OS_KERNEL_USER_CONTEXT_SIZE_BYTES;
    if (!GetKernelStackManager().Contains(kernel_stack_slot_index, frame_address,
                                          OS_KERNEL_USER_CONTEXT_SIZE_BYTES)) {
        return false;
    }

    UserContext *const frame = reinterpret_cast<UserContext *>(frame_address);
    *frame = UserContext{};
    frame->common.register_rdi = argument_layout.argument_count;
    frame->common.register_rsi = argument_layout.argument_vector_address;
    frame->common.register_rdx = argument_layout.environment_vector_address;
    frame->common.vector = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    frame->common.error_code = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    frame->common.instruction_pointer = address_space.entry_virtual_address;
    frame->common.code_segment = static_cast<uint64_t>(OS_KERNEL_DESCRIPTOR_USER_CODE_SELECTOR);
    frame->common.flags = OS_KERNEL_PROCESS_RUNTIME_INITIAL_USER_FLAGS;
    frame->stack_pointer = argument_layout.stack_pointer;
    frame->stack_segment = static_cast<uint64_t>(OS_KERNEL_DESCRIPTOR_USER_DATA_SELECTOR);
    saved_frame = &frame->common;
    return true;
}

[[nodiscard]] bool CurrentFrameIsValid(const uint64_t thread_index,
                                       const ExceptionFrame &frame) noexcept {
    ThreadEntry thread{};
    if (thread_index >= process_runtime_limits.thread_capacity ||
        thread_scheduler.ReadThread(thread_index, thread) != ThreadSchedulerStatus::Succeeded ||
        thread.process_index >= process_runtime_limits.process_capacity ||
        !FrameOriginatedFromUser(frame) ||
        !GetKernelStackManager().Contains(thread.kernel_stack_slot_index,
                                          reinterpret_cast<uint64_t>(&frame),
                                          OS_KERNEL_USER_CONTEXT_SIZE_BYTES)) {
        return false;
    }
    const ProcessRuntimeProcess &process = runtime_processes[thread.process_index];
    return GetCpuLocal().Statistics().current_thread_index == thread_index &&
           process.address_space.root_physical_address != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE &&
           ReadPageTableRoot() == process.address_space.root_physical_address;
}

[[nodiscard]] bool ActivateThread(const uint64_t thread_index) noexcept {
    ThreadEntry thread{};
    if (thread_index >= process_runtime_limits.thread_capacity ||
        thread_scheduler.ReadThread(thread_index, thread) != ThreadSchedulerStatus::Succeeded ||
        thread.process_index >= process_runtime_limits.process_capacity) {
        return false;
    }
    CpuPreemptionGuard preemption_guard{};
    if (!preemption_guard.Succeeded()) {
        return false;
    }
    ProcessRuntimeProcess &process = runtime_processes[thread.process_index];
    ProcessRuntimeThread &runtime_thread = runtime_threads[thread_index];
    KernelStack stack{};
    if (!process.active || !runtime_thread.active || runtime_thread.saved_frame == nullptr ||
        !ReadThreadKernelStack(thread_index, stack) ||
        !GetKernelStackManager().Contains(thread.kernel_stack_slot_index,
                                          reinterpret_cast<uint64_t>(runtime_thread.saved_frame),
                                          OS_KERNEL_USER_CONTEXT_SIZE_BYTES) ||
        !ActivateUserPageTable(process.address_space.root_physical_address)) {
        return false;
    }
    const uint64_t kernel_stack_top = KernelStackTopAddress(stack);
    if (!SetPrivilegeStackPointer0(kernel_stack_top)) {
        SetActiveUserAddressSpace(nullptr);
        ActivateKernelPageTable();
        return false;
    }
    if (GetCpuLocal().SetCurrentThread(thread_index, kernel_stack_top) !=
        CpuLocalStatus::Succeeded) {
        SetActiveUserAddressSpace(nullptr);
        ActivateKernelPageTable();
        return false;
    }
    if (RestoreFxState(runtime_thread.extended_state) != ExtendedStateStatus::Succeeded) {
        SetActiveUserAddressSpace(nullptr);
        ActivateKernelPageTable();
        return false;
    }
    SetActiveUserAddressSpace(&process.address_space);
    return true;
}

[[nodiscard]] WaitQueue *SelectWaitQueue(const WaitCondition wait_condition) noexcept {
    if (wait_condition == WaitCondition::PipeReadable) {
        return &pipe_readable_wait_queue;
    }
    if (wait_condition == WaitCondition::PipeWritable) {
        return &pipe_writable_wait_queue;
    }
    if (wait_condition == WaitCondition::DescriptorReadable) {
        return &descriptor_readable_wait_queue;
    }
    if (wait_condition == WaitCondition::DescriptorWritable) {
        return &descriptor_writable_wait_queue;
    }
    if (wait_condition == WaitCondition::ChildProcess) {
        return &child_exit_wait_queue;
    }
    return nullptr;
}

void WakeRequiredThreads(const WaitCondition wait_condition,
                         const WakeReason wake_reason) noexcept {
    uint64_t woken_thread_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if (WakeThreads(wait_condition, wake_reason, OS_KERNEL_PROCESS_RUNTIME_WAKE_ALL_COUNT,
                    woken_thread_count) != ProcessRuntimeStatus::Succeeded) {
        HaltProcessor();
    }
}

[[nodiscard]] bool ReadCurrentThreadAndProcess(ThreadEntry &thread,
                                               ProcessEntry &process) noexcept {
    const uint64_t thread_index = thread_scheduler.CurrentThreadIndex();
    return thread_index < process_runtime_limits.thread_capacity &&
           thread_scheduler.ReadThread(thread_index, thread) == ThreadSchedulerStatus::Succeeded &&
           thread.process_index < process_runtime_limits.process_capacity &&
           thread_scheduler.ReadProcess(thread.process_index, process) ==
               ThreadSchedulerStatus::Succeeded;
}

[[nodiscard]] ProcessRuntimeProcess &CurrentRuntimeProcess() noexcept {
    ThreadEntry thread{};
    ProcessEntry process{};
    if (!ReadCurrentThreadAndProcess(thread, process)) {
        HaltProcessor();
    }
    return runtime_processes[thread.process_index];
}

[[nodiscard]] bool WriteConsoleDevice(void *const context, const uint8_t *const source,
                                      const uint64_t length_bytes,
                                      uint64_t &written_bytes) noexcept {
    static_cast<void>(context);
    written_bytes = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if (source == nullptr && length_bytes != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        return false;
    }
    const SerialPort serial_port{OS_KERNEL_SERIAL_COM1_BASE_PORT};
    while (written_bytes < length_bytes) {
        if (!serial_port.TryWriteByte(static_cast<char>(source[written_bytes]))) {
            return false;
        }
        ++written_bytes;
    }
    return true;
}

[[nodiscard]] bool CreateAndInstallInitialDescription(ProcessRuntimeProcess &process,
                                                      const FileDescriptionKind kind,
                                                      const uint64_t file_status_flags,
                                                      const uint64_t descriptor) noexcept {
    const bool console_output =
        kind == FileDescriptionKind::ConsoleOutput || kind == FileDescriptionKind::ConsoleError;
    const bool pipe_endpoint =
        kind == FileDescriptionKind::PipeReader || kind == FileDescriptionKind::PipeWriter;
    const FileDescriptionCreateRequest request{
        .kind = kind,
        .file_status_flags = file_status_flags,
        .console_input =
            kind == FileDescriptionKind::ConsoleInput ? &process_console_input : nullptr,
        .device_write_operation = console_output ? WriteConsoleDevice : nullptr,
        .device_write_context = nullptr,
        .pipe = pipe_endpoint ? &process_pipe : nullptr,
        .vfs = nullptr,
        .open_file = {},
    };
    KernelObjectReference reference{};
    return file_description_manager.Create(request, reference) ==
               FileDescriptionStatus::Succeeded &&
           process.file_table.InstallExact(reference, descriptor,
                                           OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) ==
               FileTableStatus::Succeeded;
}

[[nodiscard]] bool InitializeProcessFileTable(ProcessRuntimeProcess &process,
                                              const UserProgramSelection selection) noexcept {
    if (process.file_table.Initialize(GetKernelHeap(), kernel_object_manager,
                                      process_runtime_limits.file_descriptor_hard_limit,
                                      process_runtime_limits.file_descriptor_hard_limit) !=
        FileTableStatus::Succeeded) {
        return false;
    }
    const bool standard_descriptions_installed =
        CreateAndInstallInitialDescription(process, FileDescriptionKind::ConsoleInput,
                                           OS_KERNEL_FILE_DESCRIPTION_READABLE_STATUS_FLAG,
                                           OS_KERNEL_FILE_TABLE_STANDARD_INPUT_DESCRIPTOR) &&
        CreateAndInstallInitialDescription(process, FileDescriptionKind::ConsoleOutput,
                                           OS_KERNEL_FILE_DESCRIPTION_WRITABLE_STATUS_FLAG,
                                           OS_KERNEL_FILE_TABLE_STANDARD_OUTPUT_DESCRIPTOR) &&
        CreateAndInstallInitialDescription(process, FileDescriptionKind::ConsoleError,
                                           OS_KERNEL_FILE_DESCRIPTION_WRITABLE_STATUS_FLAG,
                                           OS_KERNEL_FILE_TABLE_STANDARD_ERROR_DESCRIPTOR);
    bool pipe_description_installed = true;
    if (standard_descriptions_installed && selection == UserProgramSelection::IpcConsumer) {
        pipe_description_installed =
            CreateAndInstallInitialDescription(process, FileDescriptionKind::PipeReader,
                                               OS_KERNEL_FILE_DESCRIPTION_READABLE_STATUS_FLAG,
                                               OS_KERNEL_FILE_TABLE_FIRST_DYNAMIC_DESCRIPTOR);
    } else if (standard_descriptions_installed && selection == UserProgramSelection::IpcProducer) {
        pipe_description_installed =
            CreateAndInstallInitialDescription(process, FileDescriptionKind::PipeWriter,
                                               OS_KERNEL_FILE_DESCRIPTION_WRITABLE_STATUS_FLAG,
                                               OS_KERNEL_FILE_TABLE_FIRST_DYNAMIC_DESCRIPTOR);
    }
    if (standard_descriptions_installed && pipe_description_installed) {
        return true;
    }
    static_cast<void>(process.file_table.Destroy());
    return false;
}

[[nodiscard]] bool ReadUserProcessString(const uint64_t vector_address, const uint64_t string_index,
                                         os::abi::ProcessString &string) noexcept {
    if (vector_address == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        string_index > (UINT64_MAX - vector_address) / sizeof(os::abi::ProcessString)) {
        return false;
    }
    const uint64_t descriptor_address =
        vector_address + string_index * sizeof(os::abi::ProcessString);
    return CopyFromUser(descriptor_address, sizeof(string), reinterpret_cast<uint8_t *>(&string),
                        sizeof(string)) == UserMemoryCopyStatus::Succeeded;
}

[[nodiscard]] ProcessRuntimeStatus
PlanUserProgramArguments(const os::abi::ProcessLaunchRequest &request) noexcept {
    if (request.argument_count > os::abi::OS_ABI_PROCESS_MAXIMUM_ARGUMENT_COUNT ||
        request.environment_count > os::abi::OS_ABI_PROCESS_MAXIMUM_ENVIRONMENT_COUNT) {
        return ProcessRuntimeStatus::ArgumentListTooLarge;
    }
    if ((request.argument_count != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE &&
         request.argument_vector_address == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) ||
        (request.environment_count != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE &&
         request.environment_vector_address == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE)) {
        return ProcessRuntimeStatus::InvalidArguments;
    }
    program_argument_plan.Reset();
    for (uint64_t argument_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         argument_index < request.argument_count; ++argument_index) {
        os::abi::ProcessString string{};
        if (!ReadUserProcessString(request.argument_vector_address, argument_index, string) ||
            (string.length_bytes != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE &&
             !IsUserVirtualAddressRange(string.address, string.length_bytes))) {
            return ProcessRuntimeStatus::InvalidArguments;
        }
        const ProgramArgumentStatus plan_status =
            program_argument_plan.AddArgument(string.length_bytes);
        if (plan_status != ProgramArgumentStatus::Succeeded) {
            return plan_status == ProgramArgumentStatus::TooManyArguments ||
                           plan_status == ProgramArgumentStatus::StringTooLarge ||
                           plan_status == ProgramArgumentStatus::TotalSizeTooLarge
                       ? ProcessRuntimeStatus::ArgumentListTooLarge
                       : ProcessRuntimeStatus::InvalidArguments;
        }
    }
    for (uint64_t environment_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         environment_index < request.environment_count; ++environment_index) {
        os::abi::ProcessString string{};
        if (!ReadUserProcessString(request.environment_vector_address, environment_index, string) ||
            (string.length_bytes != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE &&
             !IsUserVirtualAddressRange(string.address, string.length_bytes))) {
            return ProcessRuntimeStatus::InvalidArguments;
        }
        const ProgramArgumentStatus plan_status =
            program_argument_plan.AddEnvironment(string.length_bytes);
        if (plan_status != ProgramArgumentStatus::Succeeded) {
            return plan_status == ProgramArgumentStatus::TooManyEnvironmentEntries ||
                           plan_status == ProgramArgumentStatus::StringTooLarge ||
                           plan_status == ProgramArgumentStatus::TotalSizeTooLarge
                       ? ProcessRuntimeStatus::ArgumentListTooLarge
                       : ProcessRuntimeStatus::InvalidArguments;
        }
    }
    return ProcessRuntimeStatus::Succeeded;
}

[[nodiscard]] ProcessRuntimeStatus PlanKernelProgramArguments(
    const KernelProgramString *const arguments, const uint64_t argument_count,
    const KernelProgramString *const environment, const uint64_t environment_count) noexcept {
    if (argument_count > OS_KERNEL_PROGRAM_ARGUMENT_MAXIMUM_ARGUMENT_COUNT ||
        environment_count > OS_KERNEL_PROGRAM_ARGUMENT_MAXIMUM_ENVIRONMENT_COUNT) {
        return ProcessRuntimeStatus::ArgumentListTooLarge;
    }
    if ((argument_count != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE && arguments == nullptr) ||
        (environment_count != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE && environment == nullptr)) {
        return ProcessRuntimeStatus::InvalidArguments;
    }
    program_argument_plan.Reset();
    for (uint64_t argument_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         argument_index < argument_count; ++argument_index) {
        if (arguments[argument_index].length_bytes != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE &&
            arguments[argument_index].data == nullptr) {
            return ProcessRuntimeStatus::InvalidArguments;
        }
        if (program_argument_plan.AddArgument(arguments[argument_index].length_bytes) !=
            ProgramArgumentStatus::Succeeded) {
            return ProcessRuntimeStatus::ArgumentListTooLarge;
        }
    }
    for (uint64_t environment_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         environment_index < environment_count; ++environment_index) {
        if (environment[environment_index].length_bytes != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE &&
            environment[environment_index].data == nullptr) {
            return ProcessRuntimeStatus::InvalidArguments;
        }
        if (program_argument_plan.AddEnvironment(environment[environment_index].length_bytes) !=
            ProgramArgumentStatus::Succeeded) {
            return ProcessRuntimeStatus::ArgumentListTooLarge;
        }
    }
    return ProcessRuntimeStatus::Succeeded;
}

[[nodiscard]] bool WriteCandidateBytes(UserAddressSpace &address_space,
                                       const uint64_t destination_address,
                                       const uint8_t *const source,
                                       const uint64_t length_bytes) noexcept {
    return CopyToUserAddressSpace(address_space, destination_address, length_bytes, source,
                                  length_bytes) == UserMemoryCopyStatus::Succeeded;
}

[[nodiscard]] bool WriteCandidateValue(UserAddressSpace &address_space,
                                       const uint64_t destination_address,
                                       const uint64_t value) noexcept {
    return WriteCandidateBytes(address_space, destination_address,
                               reinterpret_cast<const uint8_t *>(&value), sizeof(value));
}

[[nodiscard]] bool WriteProgramArgumentMetadata(UserAddressSpace &address_space) noexcept {
    if (!program_argument_plan.IsFinalized() ||
        program_argument_plan.Validate() != ProgramArgumentStatus::Succeeded) {
        return false;
    }
    const ProgramArgumentLayout &layout = program_argument_plan.Layout();
    if (!WriteCandidateValue(address_space, layout.stack_pointer, layout.argument_count)) {
        return false;
    }
    uint64_t metadata_address = layout.argument_vector_address;
    for (uint64_t argument_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         argument_index < program_argument_plan.ArgumentCount(); ++argument_index) {
        uint64_t string_length_bytes = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        uint64_t string_address = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        if (program_argument_plan.ReadArgument(argument_index, string_length_bytes,
                                               string_address) !=
                ProgramArgumentStatus::Succeeded ||
            !WriteCandidateValue(address_space, metadata_address, string_address)) {
            return false;
        }
        metadata_address += sizeof(uint64_t);
    }
    if (!WriteCandidateValue(address_space, metadata_address,
                             OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE)) {
        return false;
    }
    metadata_address = layout.environment_vector_address;
    for (uint64_t environment_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         environment_index < program_argument_plan.EnvironmentCount(); ++environment_index) {
        uint64_t string_length_bytes = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        uint64_t string_address = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        if (program_argument_plan.ReadEnvironment(environment_index, string_length_bytes,
                                                  string_address) !=
                ProgramArgumentStatus::Succeeded ||
            !WriteCandidateValue(address_space, metadata_address, string_address)) {
            return false;
        }
        metadata_address += sizeof(uint64_t);
    }
    return WriteCandidateValue(address_space, metadata_address,
                               OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE);
}

[[nodiscard]] bool CopyUserStringToCandidate(UserAddressSpace &address_space,
                                             const os::abi::ProcessString &string,
                                             const uint64_t destination_address) noexcept {
    uint8_t transfer_buffer[OS_KERNEL_PROCESS_RUNTIME_ARGUMENT_COPY_CHUNK_SIZE_BYTES]{};
    uint64_t copied_bytes = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    while (copied_bytes < string.length_bytes) {
        const uint64_t remaining_bytes = string.length_bytes - copied_bytes;
        const uint64_t chunk_bytes =
            remaining_bytes < OS_KERNEL_PROCESS_RUNTIME_ARGUMENT_COPY_CHUNK_SIZE_BYTES
                ? remaining_bytes
                : OS_KERNEL_PROCESS_RUNTIME_ARGUMENT_COPY_CHUNK_SIZE_BYTES;
        if (CopyFromUser(string.address + copied_bytes, chunk_bytes, transfer_buffer,
                         sizeof(transfer_buffer)) != UserMemoryCopyStatus::Succeeded ||
            !WriteCandidateBytes(address_space, destination_address + copied_bytes, transfer_buffer,
                                 chunk_bytes)) {
            return false;
        }
        copied_bytes += chunk_bytes;
    }
    return WriteCandidateBytes(address_space, destination_address + string.length_bytes,
                               &OS_KERNEL_PROCESS_RUNTIME_STRING_TERMINATOR,
                               sizeof(OS_KERNEL_PROCESS_RUNTIME_STRING_TERMINATOR));
}

[[nodiscard]] bool PopulateUserProgramArguments(const os::abi::ProcessLaunchRequest &request,
                                                UserAddressSpace &address_space) noexcept {
    if (program_argument_plan.Finalize(OS_KERNEL_USER_STACK_BOTTOM_VIRTUAL_ADDRESS,
                                       OS_KERNEL_USER_STACK_TOP_VIRTUAL_ADDRESS) !=
            ProgramArgumentStatus::Succeeded ||
        PrepareUserStack(address_space, program_argument_plan.Layout().stack_pointer) !=
            UserAddressSpaceStatus::Succeeded) {
        return false;
    }
    for (uint64_t argument_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         argument_index < request.argument_count; ++argument_index) {
        os::abi::ProcessString string{};
        uint64_t planned_length_bytes = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        uint64_t destination_address = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        if (!ReadUserProcessString(request.argument_vector_address, argument_index, string) ||
            program_argument_plan.ReadArgument(argument_index, planned_length_bytes,
                                               destination_address) !=
                ProgramArgumentStatus::Succeeded ||
            planned_length_bytes != string.length_bytes ||
            !CopyUserStringToCandidate(address_space, string, destination_address)) {
            return false;
        }
    }
    for (uint64_t environment_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         environment_index < request.environment_count; ++environment_index) {
        os::abi::ProcessString string{};
        uint64_t planned_length_bytes = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        uint64_t destination_address = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        if (!ReadUserProcessString(request.environment_vector_address, environment_index, string) ||
            program_argument_plan.ReadEnvironment(environment_index, planned_length_bytes,
                                                  destination_address) !=
                ProgramArgumentStatus::Succeeded ||
            planned_length_bytes != string.length_bytes ||
            !CopyUserStringToCandidate(address_space, string, destination_address)) {
            return false;
        }
    }
    return WriteProgramArgumentMetadata(address_space);
}

[[nodiscard]] bool PopulateKernelProgramArguments(const KernelProgramString *const arguments,
                                                  const uint64_t argument_count,
                                                  const KernelProgramString *const environment,
                                                  const uint64_t environment_count,
                                                  UserAddressSpace &address_space) noexcept {
    if (program_argument_plan.Finalize(OS_KERNEL_USER_STACK_BOTTOM_VIRTUAL_ADDRESS,
                                       OS_KERNEL_USER_STACK_TOP_VIRTUAL_ADDRESS) !=
            ProgramArgumentStatus::Succeeded ||
        PrepareUserStack(address_space, program_argument_plan.Layout().stack_pointer) !=
            UserAddressSpaceStatus::Succeeded) {
        return false;
    }
    for (uint64_t argument_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         argument_index < argument_count; ++argument_index) {
        uint64_t planned_length_bytes = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        uint64_t destination_address = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        if (program_argument_plan.ReadArgument(argument_index, planned_length_bytes,
                                               destination_address) !=
                ProgramArgumentStatus::Succeeded ||
            planned_length_bytes != arguments[argument_index].length_bytes ||
            (planned_length_bytes != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE &&
             !WriteCandidateBytes(address_space, destination_address,
                                  arguments[argument_index].data, planned_length_bytes)) ||
            !WriteCandidateBytes(address_space, destination_address + planned_length_bytes,
                                 &OS_KERNEL_PROCESS_RUNTIME_STRING_TERMINATOR,
                                 sizeof(OS_KERNEL_PROCESS_RUNTIME_STRING_TERMINATOR))) {
            return false;
        }
    }
    for (uint64_t environment_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         environment_index < environment_count; ++environment_index) {
        uint64_t planned_length_bytes = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        uint64_t destination_address = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        if (program_argument_plan.ReadEnvironment(environment_index, planned_length_bytes,
                                                  destination_address) !=
                ProgramArgumentStatus::Succeeded ||
            planned_length_bytes != environment[environment_index].length_bytes ||
            (planned_length_bytes != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE &&
             !WriteCandidateBytes(address_space, destination_address,
                                  environment[environment_index].data, planned_length_bytes)) ||
            !WriteCandidateBytes(address_space, destination_address + planned_length_bytes,
                                 &OS_KERNEL_PROCESS_RUNTIME_STRING_TERMINATOR,
                                 sizeof(OS_KERNEL_PROCESS_RUNTIME_STRING_TERMINATOR))) {
            return false;
        }
    }
    return WriteProgramArgumentMetadata(address_space);
}

[[nodiscard]] ProcessRuntimeStatus
LoadExecutableFromPath(fs::FsContext &file_system_context, const uint8_t *const path,
                       const uint64_t path_length_bytes, UserAddressSpace &address_space,
                       UserElfValidationStatus &elf_validation_status,
                       UserAddressSpaceStatus &address_space_status) noexcept {
    address_space = UserAddressSpace{};
    elf_validation_status = UserElfValidationStatus::Succeeded;
    address_space_status = UserAddressSpaceStatus::Succeeded;
    if (process_vfs == nullptr || path == nullptr ||
        path_length_bytes == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        path_length_bytes > fs::OS_KERNEL_VFS_MAXIMUM_PATH_LENGTH_BYTES) {
        return ProcessRuntimeStatus::InvalidArguments;
    }
    fs::NodeInformation information{};
    const fs::Status stat_status =
        process_vfs->Stat(file_system_context, path, path_length_bytes, information);
    if (stat_status != fs::Status::Succeeded || information.type != fs::NodeType::RegularFile) {
        return ProcessRuntimeStatus::ExecutableReadFailure;
    }
    fs::OpenFile open_file{};
    const fs::Status open_status = process_vfs->Open(file_system_context, path, path_length_bytes,
                                                     fs::OpenOptions{
                                                         .readable = true,
                                                         .writable = false,
                                                         .create = false,
                                                         .truncate = false,
                                                     },
                                                     open_file);
    if (open_status != fs::Status::Succeeded) {
        return ProcessRuntimeStatus::ExecutableReadFailure;
    }
    VfsUserElfReaderContext reader_context{
        .vfs = process_vfs,
        .open_file = &open_file,
    };
    address_space_status = LoadUserAddressSpace(
        UserElfReader{
            .context = &reader_context,
            .image_size_bytes = information.size_bytes,
            .read = ReadVfsUserElf,
        },
        address_space, elf_validation_status);
    const fs::Status close_status = process_vfs->Close(open_file);
    if (close_status != fs::Status::Succeeded) {
        if (address_space.root_physical_address != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE &&
            DestroyUserAddressSpace(address_space) != UserAddressSpaceStatus::Succeeded) {
            HaltProcessor();
        }
        return ProcessRuntimeStatus::ExecutableReadFailure;
    }
    if (address_space_status != UserAddressSpaceStatus::Succeeded) {
        if (address_space_status == UserAddressSpaceStatus::InvalidElf) {
            return elf_validation_status == UserElfValidationStatus::ReadFailed
                       ? ProcessRuntimeStatus::ExecutableReadFailure
                       : ProcessRuntimeStatus::InvalidElf;
        }
        return address_space_status == UserAddressSpaceStatus::ImageReadFailed
                   ? ProcessRuntimeStatus::ExecutableReadFailure
                   : ProcessRuntimeStatus::AddressSpaceFailure;
    }
    return ProcessRuntimeStatus::Succeeded;
}

[[nodiscard]] ProcessRuntimeStatus
RegisterRuntimeProcess(UserAddressSpace &address_space, const UserProgramSelection selection,
                       const uint64_t parent_process_index,
                       ProcessCreationResult &creation_result) noexcept {
    uint64_t process_index = OS_KERNEL_PROCESS_INVALID_INDEX;
    ProcessId process_id{};
    bool interrupts_were_enabled = scheduler_lock.Lock();
    const ThreadSchedulerStatus create_process_status = thread_scheduler.CreateProcess(
        address_space.root_physical_address, process_index, process_id);
    scheduler_lock.Unlock(interrupts_were_enabled);
    if (create_process_status != ThreadSchedulerStatus::Succeeded) {
        return create_process_status == ThreadSchedulerStatus::ProcessCapacityExhausted
                   ? ProcessRuntimeStatus::ProcessLimitExceeded
                   : ProcessRuntimeStatus::SchedulerFailure;
    }

    uint64_t kernel_stack_slot_index = OS_KERNEL_THREAD_INVALID_INDEX;
    for (uint64_t candidate_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         candidate_index < process_runtime_limits.thread_capacity; ++candidate_index) {
        KernelStack candidate_stack{};
        if (!runtime_threads[candidate_index].active &&
            GetKernelStackManager().Read(candidate_index, candidate_stack) ==
                KernelStackManagerStatus::SlotNotActive) {
            kernel_stack_slot_index = candidate_index;
            break;
        }
    }
    if (kernel_stack_slot_index == OS_KERNEL_THREAD_INVALID_INDEX ||
        GetKernelStackManager().TryCreate(kernel_stack_slot_index) !=
            KernelStackManagerStatus::Succeeded) {
        interrupts_were_enabled = scheduler_lock.Lock();
        const ThreadSchedulerStatus discard_status = thread_scheduler.DiscardProcess(process_index);
        scheduler_lock.Unlock(interrupts_were_enabled);
        if (discard_status != ThreadSchedulerStatus::Succeeded) {
            HaltProcessor();
        }
        return ProcessRuntimeStatus::KernelStackFailure;
    }

    uint64_t thread_index = OS_KERNEL_THREAD_INVALID_INDEX;
    ThreadId thread_id{};
    interrupts_were_enabled = scheduler_lock.Lock();
    const ThreadSchedulerStatus create_thread_status = thread_scheduler.CreateThread(
        process_index, kernel_stack_slot_index, program_argument_plan.Layout().stack_pointer,
        OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE, OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE, thread_index,
        thread_id);
    scheduler_lock.Unlock(interrupts_were_enabled);
    if (create_thread_status != ThreadSchedulerStatus::Succeeded) {
        interrupts_were_enabled = scheduler_lock.Lock();
        const ThreadSchedulerStatus discard_process_status =
            thread_scheduler.DiscardProcess(process_index);
        scheduler_lock.Unlock(interrupts_were_enabled);
        if (GetKernelStackManager().TryDestroy(kernel_stack_slot_index) !=
                KernelStackManagerStatus::Succeeded ||
            discard_process_status != ThreadSchedulerStatus::Succeeded) {
            HaltProcessor();
        }
        return ProcessRuntimeStatus::SchedulerFailure;
    }

    ExceptionFrame *saved_frame = nullptr;
    if (!BuildInitialContextFrame(kernel_stack_slot_index, address_space,
                                  program_argument_plan.Layout(), saved_frame) ||
        InitializeFxSaveArea(runtime_threads[thread_index].extended_state) !=
            ExtendedStateStatus::Succeeded) {
        interrupts_were_enabled = scheduler_lock.Lock();
        const ThreadSchedulerStatus discard_thread_status =
            thread_scheduler.DiscardReadyThread(thread_index);
        const ThreadSchedulerStatus discard_process_status =
            thread_scheduler.DiscardProcess(process_index);
        scheduler_lock.Unlock(interrupts_were_enabled);
        if (GetKernelStackManager().TryDestroy(kernel_stack_slot_index) !=
                KernelStackManagerStatus::Succeeded ||
            discard_thread_status != ThreadSchedulerStatus::Succeeded ||
            discard_process_status != ThreadSchedulerStatus::Succeeded) {
            HaltProcessor();
        }
        return ProcessRuntimeStatus::ContextFrameFailure;
    }

    ProcessRuntimeProcess &runtime_process = runtime_processes[process_index];
    runtime_process.address_space = address_space;
    runtime_process.result = ProcessExecutionResult{
        .process_id = process_id.value,
        .selection = selection,
        .termination_reason = ProcessTerminationReason::None,
        .exit_code = OS_KERNEL_PROCESS_RUNTIME_SUCCESS_EXIT_CODE,
        .exception_vector = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
        .exception_error_code = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
        .exception_instruction_pointer = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
        .page_fault_address = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
        .system_call_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
        .root_physical_address = address_space.root_physical_address,
        .mapped_page_count = address_space.mapped_page_count,
        .run_tick_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
        .dispatch_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
        .pipe_bytes_read = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
        .pipe_bytes_written = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
        .file_system_bytes_read = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
        .file_system_bytes_written = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
        .console_bytes_read = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
        .console_bytes_written = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
    };
    runtime_process.file_system_context = fs::FsContext{};
    const bool file_system_context_initialized =
        process_vfs == nullptr || process_vfs->InitializeContext(
                                      runtime_process.file_system_context) == fs::Status::Succeeded;
    const bool file_table_initialized =
        file_system_context_initialized && InitializeProcessFileTable(runtime_process, selection);
    ProcessTreeStatus tree_status = ProcessTreeStatus::InvalidState;
    if (file_table_initialized) {
        tree_status =
            parent_process_index == OS_KERNEL_PROCESS_RUNTIME_INVALID_PARENT_INDEX
                ? process_tree.RegisterInit(process_index, process_id.value)
                : process_tree.RegisterChild(process_index, process_id.value, parent_process_index);
    }
    if (!file_table_initialized || tree_status != ProcessTreeStatus::Succeeded) {
        if (file_table_initialized &&
            runtime_process.file_table.Destroy() != FileTableStatus::Succeeded) {
            HaltProcessor();
        }
        if (file_system_context_initialized && process_vfs != nullptr &&
            process_vfs->ReleaseContext(runtime_process.file_system_context) !=
                fs::Status::Succeeded) {
            HaltProcessor();
        }
        interrupts_were_enabled = scheduler_lock.Lock();
        const ThreadSchedulerStatus discard_thread_status =
            thread_scheduler.DiscardReadyThread(thread_index);
        const ThreadSchedulerStatus discard_process_status =
            thread_scheduler.DiscardProcess(process_index);
        scheduler_lock.Unlock(interrupts_were_enabled);
        if (GetKernelStackManager().TryDestroy(kernel_stack_slot_index) !=
                KernelStackManagerStatus::Succeeded ||
            discard_thread_status != ThreadSchedulerStatus::Succeeded ||
            discard_process_status != ThreadSchedulerStatus::Succeeded) {
            HaltProcessor();
        }
        runtime_process.address_space = UserAddressSpace{};
        runtime_process.result = ProcessExecutionResult{};
        runtime_process.file_system_context = fs::FsContext{};
        return tree_status != ProcessTreeStatus::Succeeded && file_table_initialized
                   ? ProcessRuntimeStatus::ProcessTreeFailure
               : file_system_context_initialized ? ProcessRuntimeStatus::DescriptorTableFailure
                                                 : ProcessRuntimeStatus::FileSystemFailure;
    }

    runtime_process.active = true;
    runtime_threads[thread_index].saved_frame = saved_frame;
    runtime_threads[thread_index].active = true;
    KernelStack kernel_stack{};
    if (!ReadThreadKernelStack(thread_index, kernel_stack)) {
        HaltProcessor();
    }
    creation_result = ProcessCreationResult{
        .process_id = process_id.value,
        .process_index = process_index,
        .thread_id = thread_id.value,
        .thread_index = thread_index,
        .root_physical_address = address_space.root_physical_address,
        .entry_virtual_address = address_space.entry_virtual_address,
        .mapped_page_count = address_space.mapped_page_count,
        .kernel_stack_lower_guard_address = KernelStackLowerGuardAddress(kernel_stack),
        .kernel_stack_top_address = KernelStackTopAddress(kernel_stack),
        .kernel_stack_upper_guard_address = KernelStackUpperGuardAddress(kernel_stack),
    };
    address_space = UserAddressSpace{};
    WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_SPAWN_PREFIX, process_id.value);
    return ProcessRuntimeStatus::Succeeded;
}

void WakeAfterDescriptionRelease(const KernelObjectReleaseResult &release_result) noexcept {
    if (!release_result.released_last_reference ||
        release_result.type != KernelObjectType::FileDescription) {
        return;
    }
    const FileDescriptionKind kind = static_cast<FileDescriptionKind>(release_result.variant);
    if (kind == FileDescriptionKind::PipeReader) {
        WakeRequiredThreads(WaitCondition::DescriptorWritable, WakeReason::ObjectClosed);
    } else if (kind == FileDescriptionKind::PipeWriter) {
        WakeRequiredThreads(WaitCondition::DescriptorReadable, WakeReason::ObjectClosed);
    }
}

void CloseProcessIoDescriptors(ProcessRuntimeProcess &process) noexcept {
    if (process.file_table.Destroy() != FileTableStatus::Succeeded) {
        HaltProcessor();
    }
    WakeRequiredThreads(WaitCondition::DescriptorReadable, WakeReason::ObjectClosed);
    WakeRequiredThreads(WaitCondition::DescriptorWritable, WakeReason::ObjectClosed);
}

[[nodiscard]] bool ReapExitedThreads() noexcept {
    const uint64_t current_stack_pointer = ReadStackPointer();
    for (uint64_t thread_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         thread_index < process_runtime_limits.thread_capacity; ++thread_index) {
        ThreadEntry thread{};
        if (thread_scheduler.ReadThread(thread_index, thread) != ThreadSchedulerStatus::Succeeded ||
            thread.state != ThreadState::Exited) {
            continue;
        }
        KernelStack stack{};
        const KernelStackManagerStatus read_status =
            GetKernelStackManager().Read(thread.kernel_stack_slot_index, stack);
        if (read_status == KernelStackManagerStatus::SlotNotActive) {
            return false;
        }
        if (read_status != KernelStackManagerStatus::Succeeded ||
            GetKernelStackManager().Contains(
                thread.kernel_stack_slot_index, current_stack_pointer,
                OS_KERNEL_PROCESS_RUNTIME_STACK_POINTER_PROBE_SIZE_BYTES) ||
            GetKernelStackManager().TryDestroy(thread.kernel_stack_slot_index) !=
                KernelStackManagerStatus::Succeeded) {
            return false;
        }
        if (thread.process_index >= process_runtime_limits.process_capacity) {
            return false;
        }
        ProcessRuntimeProcess &process = runtime_processes[thread.process_index];
        process.result.run_tick_count = thread.run_tick_count;
        process.result.dispatch_count = thread.dispatch_count;
        runtime_threads[thread_index].saved_frame = nullptr;
        runtime_threads[thread_index].active = false;

        const bool interrupts_were_enabled = scheduler_lock.Lock();
        const ThreadSchedulerStatus reap_thread_status =
            thread_scheduler.ReapExitedThread(thread_index);
        scheduler_lock.Unlock(interrupts_were_enabled);
        if (reap_thread_status != ThreadSchedulerStatus::Succeeded) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool CollectTerminalInitProcess() noexcept {
    ProcessTreeWaitResult wait_result{};
    if (process_tree.CollectInit(wait_result) != ProcessTreeStatus::Succeeded ||
        wait_result.process_index >= process_runtime_limits.process_capacity) {
        return false;
    }
    const bool interrupts_were_enabled = scheduler_lock.Lock();
    const ThreadSchedulerStatus reap_status =
        thread_scheduler.ReapZombieProcess(wait_result.process_index);
    scheduler_lock.Unlock(interrupts_were_enabled);
    if (reap_status != ThreadSchedulerStatus::Succeeded) {
        return false;
    }
    runtime_processes[wait_result.process_index].active = false;
    return true;
}

[[nodiscard]] bool CleanupCapacitySelfTestResources(const ProcessRuntimeLimits limits) noexcept {
    bool cleanup_succeeded = true;
    for (uint64_t thread_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         thread_index < limits.thread_capacity; ++thread_index) {
        if (!capacity_stack_active[thread_index]) {
            continue;
        }
        cleanup_succeeded = GetKernelStackManager().TryDestroy(thread_index) ==
                                KernelStackManagerStatus::Succeeded &&
                            cleanup_succeeded;
        capacity_stack_active[thread_index] = false;
    }
    for (uint64_t process_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         process_index < limits.process_capacity; ++process_index) {
        if (capacity_process_roots[process_index] == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
            continue;
        }
        cleanup_succeeded = DestroyUserPageTable(capacity_process_roots[process_index]) ==
                                KernelUserPageStatus::Succeeded &&
                            cleanup_succeeded;
        capacity_process_roots[process_index] = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    }
    return cleanup_succeeded;
}

[[nodiscard]] bool RunProcessThreadCapacitySelfTest(const ProcessRuntimeLimits limits) noexcept {
    for (uint64_t process_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         process_index < OS_KERNEL_PROCESS_CAPACITY_LIMIT; ++process_index) {
        capacity_process_roots[process_index] = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    }
    for (uint64_t thread_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         thread_index < OS_KERNEL_THREAD_CAPACITY_LIMIT; ++thread_index) {
        capacity_stack_active[thread_index] = false;
    }

    ResourceSnapshot before{};
    ResourceSnapshot active{};
    ResourceSnapshot after{};
    ResourceSnapshotDifference difference{};
    ThreadScheduler capacity_scheduler{};
    if (GetKernelResourceSnapshot(before) != ResourceSnapshotStatus::Succeeded ||
        capacity_scheduler.Initialize(
            capacity_process_entries, limits.process_capacity, capacity_thread_entries,
            limits.thread_capacity, limits.maximum_threads_per_process,
            OS_KERNEL_THREAD_DEFAULT_QUANTUM_TICKS) != ThreadSchedulerStatus::Succeeded) {
        return false;
    }

    for (uint64_t process_ordinal = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         process_ordinal < limits.process_capacity; ++process_ordinal) {
        uint64_t root_physical_address = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        uint64_t process_index = OS_KERNEL_PROCESS_INVALID_INDEX;
        ProcessId process_id{};
        if (CreateUserPageTable(root_physical_address) != KernelUserPageStatus::Succeeded) {
            static_cast<void>(CleanupCapacitySelfTestResources(limits));
            return false;
        }
        // 页表根一旦创建就先登记到回滚表；后续调度器校验失败也不能遗失本次资源。
        capacity_process_roots[process_ordinal] = root_physical_address;
        if (capacity_scheduler.CreateProcess(root_physical_address, process_index, process_id) !=
                ThreadSchedulerStatus::Succeeded ||
            process_index != process_ordinal ||
            process_id.value == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
            static_cast<void>(CleanupCapacitySelfTestResources(limits));
            return false;
        }
    }

    uint64_t created_thread_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    uint64_t process_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
    while (created_thread_count < limits.thread_capacity) {
        if (created_thread_count < limits.maximum_threads_per_process) {
            process_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
        } else if (created_thread_count < limits.maximum_threads_per_process +
                                              limits.process_capacity -
                                              OS_KERNEL_PROCESS_RUNTIME_COUNTER_INCREMENT) {
            process_index = created_thread_count - limits.maximum_threads_per_process +
                            OS_KERNEL_PROCESS_RUNTIME_COUNTER_INCREMENT;
        } else {
            const uint64_t remaining_process_count =
                limits.process_capacity - OS_KERNEL_PROCESS_RUNTIME_COUNTER_INCREMENT;
            process_index = OS_KERNEL_PROCESS_RUNTIME_COUNTER_INCREMENT +
                            (created_thread_count - limits.maximum_threads_per_process -
                             remaining_process_count) %
                                remaining_process_count;
        }

        const uint64_t kernel_stack_slot_index = created_thread_count;
        const uint64_t user_stack_pointer =
            OS_KERNEL_PROCESS_RUNTIME_CAPACITY_TEST_USER_STACK_BASE -
            created_thread_count * OS_KERNEL_PROCESS_RUNTIME_CAPACITY_TEST_USER_STACK_STRIDE_BYTES;
        uint64_t thread_index = OS_KERNEL_THREAD_INVALID_INDEX;
        ThreadId thread_id{};
        if (GetKernelStackManager().TryCreate(kernel_stack_slot_index) !=
            KernelStackManagerStatus::Succeeded) {
            static_cast<void>(CleanupCapacitySelfTestResources(limits));
            return false;
        }
        capacity_stack_active[kernel_stack_slot_index] = true;
        if (InitializeFxSaveArea(capacity_thread_extended_states[kernel_stack_slot_index]) !=
                ExtendedStateStatus::Succeeded ||
            capacity_scheduler.CreateThread(
                process_index, kernel_stack_slot_index, user_stack_pointer,
                OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE, OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
                thread_index, thread_id) != ThreadSchedulerStatus::Succeeded ||
            thread_index >= limits.thread_capacity ||
            thread_id.value == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
            static_cast<void>(CleanupCapacitySelfTestResources(limits));
            return false;
        }
        created_thread_count += OS_KERNEL_PROCESS_RUNTIME_COUNTER_INCREMENT;
    }

    const ThreadSchedulerStatistics active_statistics = capacity_scheduler.Statistics();
    const ResourceSnapshotSupplementalCounts active_supplemental_counts{
        .process_count = active_statistics.owned_process_count,
        .thread_count = active_statistics.owned_thread_count,
        .file_description_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
        .vnode_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
        .cache_page_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
        .block_request_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
    };
    if (active_statistics.owned_process_count != limits.process_capacity ||
        active_statistics.owned_thread_count != limits.thread_capacity ||
        GetKernelResourceSnapshot(active_supplemental_counts, active) !=
            ResourceSnapshotStatus::Succeeded ||
        active.process_count != limits.process_capacity ||
        active.thread_count != limits.thread_capacity ||
        active.kernel_stack_active_count != limits.thread_capacity) {
        static_cast<void>(CleanupCapacitySelfTestResources(limits));
        return false;
    }

    ThreadSchedulingDecision decision{};
    if (capacity_scheduler.Validate() != ThreadSchedulerStatus::Succeeded ||
        capacity_scheduler.Start(decision) != ThreadSchedulerStatus::Succeeded) {
        static_cast<void>(CleanupCapacitySelfTestResources(limits));
        return false;
    }
    for (uint64_t terminated_thread_count = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         terminated_thread_count < limits.thread_capacity; ++terminated_thread_count) {
        if (capacity_scheduler.TerminateCurrentThread(decision) !=
            ThreadSchedulerStatus::Succeeded) {
            static_cast<void>(CleanupCapacitySelfTestResources(limits));
            return false;
        }
    }
    if (!decision.completed || capacity_scheduler.Validate() != ThreadSchedulerStatus::Succeeded) {
        static_cast<void>(CleanupCapacitySelfTestResources(limits));
        return false;
    }

    for (uint64_t thread_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         thread_index < limits.thread_capacity; ++thread_index) {
        if (GetKernelStackManager().TryDestroy(thread_index) !=
            KernelStackManagerStatus::Succeeded) {
            static_cast<void>(CleanupCapacitySelfTestResources(limits));
            return false;
        }
        capacity_stack_active[thread_index] = false;
        if (capacity_scheduler.ReapExitedThread(thread_index) != ThreadSchedulerStatus::Succeeded) {
            static_cast<void>(CleanupCapacitySelfTestResources(limits));
            return false;
        }
    }
    for (uint64_t capacity_process_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         capacity_process_index < limits.process_capacity; ++capacity_process_index) {
        if (DestroyUserPageTable(capacity_process_roots[capacity_process_index]) !=
            KernelUserPageStatus::Succeeded) {
            static_cast<void>(CleanupCapacitySelfTestResources(limits));
            return false;
        }
        capacity_process_roots[capacity_process_index] = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        if (capacity_scheduler.ReapZombieProcess(capacity_process_index) !=
            ThreadSchedulerStatus::Succeeded) {
            static_cast<void>(CleanupCapacitySelfTestResources(limits));
            return false;
        }
    }

    const ThreadSchedulerStatistics statistics = capacity_scheduler.Statistics();
    const ResourceSnapshotSupplementalCounts final_supplemental_counts{
        .process_count = statistics.owned_process_count,
        .thread_count = statistics.owned_thread_count,
        .file_description_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
        .vnode_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
        .cache_page_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
        .block_request_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
    };
    if (capacity_scheduler.Validate() != ThreadSchedulerStatus::Succeeded ||
        statistics.owned_process_count != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        statistics.owned_thread_count != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        statistics.created_process_count != limits.process_capacity ||
        statistics.created_thread_count != limits.thread_capacity ||
        statistics.reaped_process_count != limits.process_capacity ||
        statistics.reaped_thread_count != limits.thread_capacity ||
        GetKernelResourceSnapshot(final_supplemental_counts, after) !=
            ResourceSnapshotStatus::Succeeded ||
        CompareResourceSnapshots(before, after, difference) != ResourceSnapshotStatus::Succeeded ||
        difference.changed_fields_mask != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        difference.changed_field_count != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        return false;
    }

    capacity_self_test_process_count = limits.process_capacity;
    capacity_self_test_thread_count = limits.thread_capacity;
    capacity_self_test_threads_per_process = limits.maximum_threads_per_process;
    return true;
}

[[nodiscard]] ExceptionFrame *
CompleteCurrentThread(ExceptionFrame &frame, const ProcessTerminationReason termination_reason,
                      const int64_t exit_code, const uint64_t page_fault_address) noexcept {
    const uint64_t thread_index = thread_scheduler.CurrentThreadIndex();
    ThreadEntry current_thread{};
    if (!process_scheduling_active || !CurrentFrameIsValid(thread_index, frame) ||
        thread_scheduler.ReadThread(thread_index, current_thread) !=
            ThreadSchedulerStatus::Succeeded ||
        current_thread.process_index >= process_runtime_limits.process_capacity) {
        HaltProcessor();
    }

    ProcessRuntimeProcess &process = runtime_processes[current_thread.process_index];
    ProcessRuntimeThread &runtime_thread = runtime_threads[thread_index];
    runtime_thread.saved_frame = &frame;
    if (SaveFxState(runtime_thread.extended_state) != ExtendedStateStatus::Succeeded) {
        HaltProcessor();
    }
    process.result.termination_reason = termination_reason;
    process.result.exit_code = exit_code;
    if (termination_reason == ProcessTerminationReason::Exception) {
        process.result.exception_vector = frame.vector;
        process.result.exception_error_code = frame.error_code;
        process.result.exception_instruction_pointer = frame.instruction_pointer;
        process.result.page_fault_address = page_fault_address;
    }
    CloseProcessIoDescriptors(process);
    if (process_vfs != nullptr && process.file_system_context.initialized &&
        process_vfs->ReleaseContext(process.file_system_context) != fs::Status::Succeeded) {
        HaltProcessor();
    }
    uint64_t reparented_process_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    const ProcessTreeTerminationReason tree_termination_reason =
        termination_reason == ProcessTerminationReason::Exited
            ? ProcessTreeTerminationReason::Exited
            : ProcessTreeTerminationReason::Exception;
    if (process_tree.MarkExited(current_thread.process_index,
                                ProcessTreeExitStatus{
                                    .termination_reason = tree_termination_reason,
                                    .exit_code = exit_code,
                                    .exception_vector = process.result.exception_vector,
                                },
                                reparented_process_count) != ProcessTreeStatus::Succeeded) {
        HaltProcessor();
    }
    WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_EXIT_PREFIX, process.result.process_id);
    if (reparented_process_count != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_REPARENT_PREFIX,
                                 reparented_process_count);
    }
    WakeRequiredThreads(WaitCondition::ChildProcess, WakeReason::ConditionSatisfied);

    ThreadSchedulingDecision decision{};
    const bool interrupts_were_enabled = scheduler_lock.Lock();
    const ThreadSchedulerStatus scheduler_status =
        thread_scheduler.TerminateCurrentThread(decision);
    scheduler_lock.Unlock(interrupts_were_enabled);
    if (scheduler_status != ThreadSchedulerStatus::Succeeded) {
        HaltProcessor();
    }

    SetActiveUserAddressSpace(nullptr);
    ActivateKernelPageTable();
    if (DestroyUserAddressSpace(process.address_space) != UserAddressSpaceStatus::Succeeded) {
        HaltProcessor();
    }

    if (decision.completed || !decision.switched) {
        const uint64_t swap_gs_required = GetCpuLocal().NativeSystemCallActive()
                                              ? OS_KERNEL_PROCESS_RUNTIME_BOOLEAN_TRUE
                                              : OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        if (decision.completed) {
            process_scheduling_active = false;
        }
        if (!SetPrivilegeStackPointer0(DefaultPrivilegeStackPointer0())) {
            HaltProcessor();
        }
        if (GetCpuLocal().ClearCurrentThread(DefaultPrivilegeStackPointer0()) !=
            CpuLocalStatus::Succeeded) {
            HaltProcessor();
        }
        OsKernelReturnFromUserMode(swap_gs_required);
    }
    if (!ActivateThread(decision.current_thread_index)) {
        HaltProcessor();
    }
    return runtime_threads[decision.current_thread_index].saved_frame;
}
}

ProcessRuntimeStatus InitializeProcessRuntime() noexcept {
    if (process_scheduling_active || process_runtime_initialized) {
        return ProcessRuntimeStatus::AlreadyActive;
    }
    if (!GetExtendedStateConfiguration().initialized) {
        return ProcessRuntimeStatus::ExtendedStateFailure;
    }
    if (GetCpuLocal().Validate() != CpuLocalStatus::Succeeded) {
        return ProcessRuntimeStatus::CpuLocalFailure;
    }
    if (!GetNativeSystemCallConfiguration().initialized) {
        return ProcessRuntimeStatus::NativeSystemCallFailure;
    }
    if (InitializeUserVirtualMemory() != UserAddressSpaceStatus::Succeeded) {
        return ProcessRuntimeStatus::AddressSpaceFailure;
    }
    process_runtime_limits =
        SelectProcessRuntimeLimits(GetKernelMemoryStatistics().managed_usable_memory_bytes);
    if (!RunProcessThreadCapacitySelfTest(process_runtime_limits)) {
        return ProcessRuntimeStatus::CapacitySelfTestFailure;
    }
    if (thread_scheduler.Initialize(process_entries, process_runtime_limits.process_capacity,
                                    thread_entries, process_runtime_limits.thread_capacity,
                                    process_runtime_limits.maximum_threads_per_process,
                                    OS_KERNEL_THREAD_DEFAULT_QUANTUM_TICKS) !=
        ThreadSchedulerStatus::Succeeded) {
        return ProcessRuntimeStatus::SchedulerFailure;
    }
    if (kernel_object_manager.Initialize(GetKernelHeap()) != KernelObjectStatus::Succeeded ||
        file_description_manager.Initialize(kernel_object_manager) !=
            FileDescriptionStatus::Succeeded) {
        return ProcessRuntimeStatus::DescriptorTableFailure;
    }
    ResetRuntimeStorage();
    frames_before_processes = GetPhysicalFrameAllocatorStatistics();
    frames_after_processes = PhysicalFrameAllocatorStatistics{};
    virtual_addresses_before_processes = GetKernelVirtualAddressAllocator().Statistics();
    virtual_addresses_after_processes = KernelVirtualAddressAllocatorStatistics{};
    kernel_stacks_before_processes = GetKernelStackManager().Statistics();
    kernel_stacks_after_processes = KernelStackManagerStatistics{};
    virtual_memory_areas_before_processes = GetUserVirtualMemoryPoolStatistics();
    virtual_memory_areas_after_processes = VirtualMemoryAreaPoolStatistics{};
    resource_snapshot_before_processes = ResourceSnapshot{};
    resource_snapshot_after_processes = ResourceSnapshot{};
    resource_snapshot_difference = ResourceSnapshotDifference{};
    if (GetKernelStackManager().Validate() != KernelStackManagerStatus::Succeeded ||
        kernel_stacks_before_processes.active_stack_count !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        virtual_memory_areas_before_processes.active_descriptor_count !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        virtual_memory_areas_before_processes.free_descriptor_count !=
            virtual_memory_areas_before_processes.capacity ||
        GetKernelResourceSnapshot(ResourceSnapshotSupplementalCounts{},
                                  resource_snapshot_before_processes) !=
            ResourceSnapshotStatus::Succeeded) {
        return ProcessRuntimeStatus::KernelStackFailure;
    }
    if (pipe_readable_wait_queue.Initialize(
            WaitQueueId{.value = OS_KERNEL_PROCESS_RUNTIME_PIPE_READABLE_QUEUE_ID}) !=
            WaitQueueStatus::Succeeded ||
        pipe_writable_wait_queue.Initialize(
            WaitQueueId{.value = OS_KERNEL_PROCESS_RUNTIME_PIPE_WRITABLE_QUEUE_ID}) !=
            WaitQueueStatus::Succeeded ||
        descriptor_readable_wait_queue.Initialize(
            WaitQueueId{.value = OS_KERNEL_PROCESS_RUNTIME_DESCRIPTOR_READABLE_QUEUE_ID}) !=
            WaitQueueStatus::Succeeded ||
        descriptor_writable_wait_queue.Initialize(
            WaitQueueId{.value = OS_KERNEL_PROCESS_RUNTIME_DESCRIPTOR_WRITABLE_QUEUE_ID}) !=
            WaitQueueStatus::Succeeded ||
        child_exit_wait_queue.Initialize(
            WaitQueueId{.value = OS_KERNEL_PROCESS_RUNTIME_CHILD_EXIT_QUEUE_ID}) !=
            WaitQueueStatus::Succeeded ||
        process_tree.Initialize(process_tree_entries, process_runtime_limits.process_capacity) !=
            ProcessTreeStatus::Succeeded) {
        return ProcessRuntimeStatus::SchedulerFailure;
    }
    program_argument_plan.Reset();
    process_pipe.Initialize();
    process_console_input.Initialize();
    pipe_reader_block_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    pipe_writer_block_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    pipe_end_of_file_observation_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    pipe_broken_observation_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    process_vfs = nullptr;
    process_runtime_initialized = true;
    return ProcessRuntimeStatus::Succeeded;
}

ProcessRuntimeStatus AttachProcessVfs(fs::Vfs &vfs) noexcept {
    if (!process_runtime_initialized) {
        return ProcessRuntimeStatus::NotInitialized;
    }
    if (process_scheduling_active) {
        return ProcessRuntimeStatus::AlreadyActive;
    }
    if (vfs.Validate() != fs::Status::Succeeded) {
        return ProcessRuntimeStatus::FileSystemFailure;
    }
    for (uint64_t process_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         process_index < process_runtime_limits.process_capacity; ++process_index) {
        ProcessRuntimeProcess &process = runtime_processes[process_index];
        if (process.active &&
            vfs.InitializeContext(process.file_system_context) != fs::Status::Succeeded) {
            for (uint64_t rollback_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
                 rollback_index < process_index; ++rollback_index) {
                ProcessRuntimeProcess &rollback_process = runtime_processes[rollback_index];
                if (rollback_process.active && rollback_process.file_system_context.initialized &&
                    vfs.ReleaseContext(rollback_process.file_system_context) !=
                        fs::Status::Succeeded) {
                    HaltProcessor();
                }
            }
            return ProcessRuntimeStatus::FileSystemFailure;
        }
    }
    process_vfs = &vfs;
    return ProcessRuntimeStatus::Succeeded;
}

ProcessRuntimeStatus CreateProcess(const UserProgramSelection selection,
                                   ProcessCreationResult &creation_result,
                                   UserElfValidationStatus &elf_validation_status,
                                   UserAddressSpaceStatus &address_space_status) noexcept {
    if (!process_runtime_initialized) {
        return ProcessRuntimeStatus::NotInitialized;
    }
    if (process_scheduling_active) {
        return ProcessRuntimeStatus::AlreadyActive;
    }
    const UserProgramImage image = SelectUserProgramImage(selection);
    UserAddressSpace address_space{};
    address_space_status = LoadUserAddressSpace(image.image, image.image_size_bytes, address_space,
                                                elf_validation_status);
    if (address_space_status != UserAddressSpaceStatus::Succeeded) {
        return address_space_status == UserAddressSpaceStatus::InvalidElf
                   ? ProcessRuntimeStatus::InvalidElf
                   : ProcessRuntimeStatus::AddressSpaceFailure;
    }
    if (PlanKernelProgramArguments(nullptr, OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE, nullptr,
                                   OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) !=
            ProcessRuntimeStatus::Succeeded ||
        !PopulateKernelProgramArguments(nullptr, OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE, nullptr,
                                        OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE, address_space)) {
        if (DestroyUserAddressSpace(address_space) != UserAddressSpaceStatus::Succeeded) {
            HaltProcessor();
        }
        return ProcessRuntimeStatus::InvalidArguments;
    }
    const ProcessRuntimeStatus register_status = RegisterRuntimeProcess(
        address_space, selection, OS_KERNEL_PROCESS_RUNTIME_INVALID_PARENT_INDEX, creation_result);
    if (register_status != ProcessRuntimeStatus::Succeeded &&
        DestroyUserAddressSpace(address_space) != UserAddressSpaceStatus::Succeeded) {
        HaltProcessor();
    }
    return register_status;
}

ProcessRuntimeStatus CreateInitialProcessFromPath(
    const uint8_t *const path, const uint64_t path_length_bytes,
    const KernelProgramString *const arguments, const uint64_t argument_count,
    const KernelProgramString *const environment, const uint64_t environment_count,
    ProcessCreationResult &creation_result, UserElfValidationStatus &elf_validation_status,
    UserAddressSpaceStatus &address_space_status) noexcept {
    if (!process_runtime_initialized || process_vfs == nullptr) {
        return ProcessRuntimeStatus::NotInitialized;
    }
    if (process_scheduling_active) {
        return ProcessRuntimeStatus::AlreadyActive;
    }
    const ProcessRuntimeStatus plan_status =
        PlanKernelProgramArguments(arguments, argument_count, environment, environment_count);
    if (plan_status != ProcessRuntimeStatus::Succeeded) {
        return plan_status;
    }
    fs::FsContext loading_context{};
    if (process_vfs->InitializeContext(loading_context) != fs::Status::Succeeded) {
        return ProcessRuntimeStatus::FileSystemFailure;
    }
    UserAddressSpace address_space{};
    ProcessRuntimeStatus status =
        LoadExecutableFromPath(loading_context, path, path_length_bytes, address_space,
                               elf_validation_status, address_space_status);
    if (status == ProcessRuntimeStatus::Succeeded &&
        !PopulateKernelProgramArguments(arguments, argument_count, environment, environment_count,
                                        address_space)) {
        status = ProcessRuntimeStatus::InvalidArguments;
    }
    if (process_vfs->ReleaseContext(loading_context) != fs::Status::Succeeded) {
        if (address_space.root_physical_address != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE &&
            DestroyUserAddressSpace(address_space) != UserAddressSpaceStatus::Succeeded) {
            HaltProcessor();
        }
        return ProcessRuntimeStatus::FileSystemFailure;
    }
    if (status == ProcessRuntimeStatus::Succeeded) {
        status =
            RegisterRuntimeProcess(address_space, UserProgramSelection::DiskExecutable,
                                   OS_KERNEL_PROCESS_RUNTIME_INVALID_PARENT_INDEX, creation_result);
    }
    if (status != ProcessRuntimeStatus::Succeeded &&
        address_space.root_physical_address != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE &&
        DestroyUserAddressSpace(address_space) != UserAddressSpaceStatus::Succeeded) {
        HaltProcessor();
    }
    return status;
}

ProcessRuntimeStatus SpawnCurrentProcess(const os::abi::ProcessLaunchRequest &request,
                                         uint64_t &process_id) noexcept {
    process_id = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if (!IsProcessSchedulingActive() || process_vfs == nullptr) {
        return ProcessRuntimeStatus::NotInitialized;
    }
    if (request.path_address == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        request.path_length_bytes == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        request.path_length_bytes > sizeof(launch_path_buffer) ||
        CopyFromUser(request.path_address, request.path_length_bytes, launch_path_buffer,
                     sizeof(launch_path_buffer)) != UserMemoryCopyStatus::Succeeded) {
        return ProcessRuntimeStatus::InvalidArguments;
    }
    const ProcessRuntimeStatus plan_status = PlanUserProgramArguments(request);
    if (plan_status != ProcessRuntimeStatus::Succeeded) {
        return plan_status;
    }
    ThreadEntry parent_thread{};
    ProcessEntry parent_process{};
    if (!ReadCurrentThreadAndProcess(parent_thread, parent_process)) {
        return ProcessRuntimeStatus::SchedulerFailure;
    }
    ProcessRuntimeProcess &parent_runtime_process = runtime_processes[parent_thread.process_index];
    UserAddressSpace address_space{};
    UserElfValidationStatus elf_validation_status = UserElfValidationStatus::Succeeded;
    UserAddressSpaceStatus address_space_status = UserAddressSpaceStatus::Succeeded;
    ProcessRuntimeStatus status = LoadExecutableFromPath(
        parent_runtime_process.file_system_context, launch_path_buffer, request.path_length_bytes,
        address_space, elf_validation_status, address_space_status);
    if (status == ProcessRuntimeStatus::Succeeded &&
        !PopulateUserProgramArguments(request, address_space)) {
        status = ProcessRuntimeStatus::InvalidArguments;
    }
    ProcessCreationResult creation_result{};
    if (status == ProcessRuntimeStatus::Succeeded) {
        status = RegisterRuntimeProcess(address_space, UserProgramSelection::DiskExecutable,
                                        parent_thread.process_index, creation_result);
    }
    if (status != ProcessRuntimeStatus::Succeeded &&
        address_space.root_physical_address != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE &&
        DestroyUserAddressSpace(address_space) != UserAddressSpaceStatus::Succeeded) {
        HaltProcessor();
    }
    if (status == ProcessRuntimeStatus::Succeeded) {
        process_id = creation_result.process_id;
    }
    return status;
}

ProcessRuntimeStatus ExecCurrentProcess(ExceptionFrame &frame,
                                        const os::abi::ProcessLaunchRequest &request) noexcept {
    const uint64_t thread_index = thread_scheduler.CurrentThreadIndex();
    if (!IsProcessSchedulingActive() || process_vfs == nullptr ||
        !CurrentFrameIsValid(thread_index, frame)) {
        return ProcessRuntimeStatus::NotInitialized;
    }
    if (request.path_address == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        request.path_length_bytes == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        request.path_length_bytes > sizeof(launch_path_buffer) ||
        CopyFromUser(request.path_address, request.path_length_bytes, launch_path_buffer,
                     sizeof(launch_path_buffer)) != UserMemoryCopyStatus::Succeeded) {
        return ProcessRuntimeStatus::InvalidArguments;
    }
    const ProcessRuntimeStatus plan_status = PlanUserProgramArguments(request);
    if (plan_status != ProcessRuntimeStatus::Succeeded) {
        return plan_status;
    }
    ThreadEntry thread{};
    ProcessEntry process_entry{};
    if (!ReadCurrentThreadAndProcess(thread, process_entry) ||
        thread_index >= process_runtime_limits.thread_capacity) {
        return ProcessRuntimeStatus::SchedulerFailure;
    }
    ProcessRuntimeProcess &process = runtime_processes[thread.process_index];
    UserAddressSpace candidate_address_space{};
    UserElfValidationStatus elf_validation_status = UserElfValidationStatus::Succeeded;
    UserAddressSpaceStatus address_space_status = UserAddressSpaceStatus::Succeeded;
    ProcessRuntimeStatus status = LoadExecutableFromPath(
        process.file_system_context, launch_path_buffer, request.path_length_bytes,
        candidate_address_space, elf_validation_status, address_space_status);
    if (status == ProcessRuntimeStatus::Succeeded &&
        !PopulateUserProgramArguments(request, candidate_address_space)) {
        status = ProcessRuntimeStatus::InvalidArguments;
    }
    if (status != ProcessRuntimeStatus::Succeeded) {
        if (candidate_address_space.root_physical_address !=
                OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE &&
            DestroyUserAddressSpace(candidate_address_space) != UserAddressSpaceStatus::Succeeded) {
            HaltProcessor();
        }
        return status;
    }

    UserAddressSpace previous_address_space = process.address_space;
    const uint64_t previous_entry_vector = frame.vector;
    SetActiveUserAddressSpace(nullptr);
    ActivateKernelPageTable();
    if (!ActivateUserPageTable(candidate_address_space.root_physical_address)) {
        if (!ActivateUserPageTable(previous_address_space.root_physical_address) ||
            DestroyUserAddressSpace(candidate_address_space) != UserAddressSpaceStatus::Succeeded) {
            HaltProcessor();
        }
        SetActiveUserAddressSpace(&process.address_space);
        return ProcessRuntimeStatus::PageTableActivationFailure;
    }
    if (!ActivateUserPageTable(previous_address_space.root_physical_address)) {
        HaltProcessor();
    }
    ActivateKernelPageTable();

    const bool interrupts_were_enabled = scheduler_lock.Lock();
    const ThreadSchedulerStatus commit_status = thread_scheduler.CommitProcessImage(
        thread.process_index, thread_index, candidate_address_space.root_physical_address,
        program_argument_plan.Layout().stack_pointer);
    scheduler_lock.Unlock(interrupts_were_enabled);
    if (commit_status != ThreadSchedulerStatus::Succeeded) {
        if (!ActivateUserPageTable(previous_address_space.root_physical_address) ||
            DestroyUserAddressSpace(candidate_address_space) != UserAddressSpaceStatus::Succeeded) {
            HaltProcessor();
        }
        SetActiveUserAddressSpace(&process.address_space);
        return ProcessRuntimeStatus::SchedulerFailure;
    }

    process.address_space = candidate_address_space;
    candidate_address_space = UserAddressSpace{};
    process.result.selection = UserProgramSelection::DiskExecutable;
    process.result.root_physical_address = process.address_space.root_physical_address;
    process.result.mapped_page_count = process.address_space.mapped_page_count;
    uint64_t closed_descriptor_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if (process.file_table.CloseOnExec(closed_descriptor_count) != FileTableStatus::Succeeded ||
        DestroyUserAddressSpace(previous_address_space) != UserAddressSpaceStatus::Succeeded ||
        !ActivateUserPageTable(process.address_space.root_physical_address)) {
        HaltProcessor();
    }
    SetActiveUserAddressSpace(&process.address_space);

    UserContext &context = AsUserContext(frame);
    context = UserContext{};
    context.common.register_rdi = program_argument_plan.Layout().argument_count;
    context.common.register_rsi = program_argument_plan.Layout().argument_vector_address;
    context.common.register_rdx = program_argument_plan.Layout().environment_vector_address;
    context.common.vector = previous_entry_vector;
    context.common.error_code = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    context.common.instruction_pointer = process.address_space.entry_virtual_address;
    context.common.code_segment = static_cast<uint64_t>(OS_KERNEL_DESCRIPTOR_USER_CODE_SELECTOR);
    context.common.flags = OS_KERNEL_PROCESS_RUNTIME_INITIAL_USER_FLAGS;
    context.stack_pointer = program_argument_plan.Layout().stack_pointer;
    context.stack_segment = static_cast<uint64_t>(OS_KERNEL_DESCRIPTOR_USER_DATA_SELECTOR);
    runtime_threads[thread_index].saved_frame = &frame;
    WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_EXEC_PREFIX, process.result.process_id);
    return ProcessRuntimeStatus::Succeeded;
}

ProcessWaitStatus TryWaitCurrentProcess(const uint64_t requested_process_id,
                                        os::abi::ProcessWaitResult &wait_result) noexcept {
    wait_result = os::abi::ProcessWaitResult{};
    if (!IsProcessSchedulingActive() ||
        requested_process_id == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        return ProcessWaitStatus::InvalidArgument;
    }
    if (!ReapExitedThreads()) {
        return ProcessWaitStatus::RuntimeFailure;
    }
    ThreadEntry parent_thread{};
    ProcessEntry parent_process{};
    if (!ReadCurrentThreadAndProcess(parent_thread, parent_process)) {
        return ProcessWaitStatus::RuntimeFailure;
    }
    ProcessTreeWaitResult tree_result{};
    const ProcessTreeStatus tree_status =
        process_tree.TryWait(parent_thread.process_index, requested_process_id, tree_result);
    if (tree_status == ProcessTreeStatus::ChildStillRunning) {
        return ProcessWaitStatus::WouldBlock;
    }
    if (tree_status == ProcessTreeStatus::NoMatchingChild) {
        return ProcessWaitStatus::NoChild;
    }
    if (tree_status != ProcessTreeStatus::Succeeded ||
        tree_result.process_index >= process_runtime_limits.process_capacity) {
        return ProcessWaitStatus::RuntimeFailure;
    }
    const bool interrupts_were_enabled = scheduler_lock.Lock();
    const ThreadSchedulerStatus reap_status =
        thread_scheduler.ReapZombieProcess(tree_result.process_index);
    scheduler_lock.Unlock(interrupts_were_enabled);
    if (reap_status != ThreadSchedulerStatus::Succeeded) {
        return ProcessWaitStatus::RuntimeFailure;
    }
    runtime_processes[tree_result.process_index].active = false;
    const os::abi::ProcessTerminationReason termination_reason =
        tree_result.exit_status.termination_reason == ProcessTreeTerminationReason::Exited
            ? os::abi::ProcessTerminationReason::Exited
            : os::abi::ProcessTerminationReason::Exception;
    wait_result = os::abi::ProcessWaitResult{
        .process_id = tree_result.process_id,
        .parent_process_id = parent_process.process_id.value,
        .termination_reason = termination_reason,
        .exit_code = tree_result.exit_status.exit_code,
        .exception_vector = tree_result.exit_status.exception_vector,
    };
    WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_WAIT_PREFIX, tree_result.process_id);
    return ProcessWaitStatus::Succeeded;
}

ProcessRuntimeStatus ExecuteProcesses() noexcept {
    if (!process_runtime_initialized) {
        return ProcessRuntimeStatus::NotInitialized;
    }
    if (process_scheduling_active) {
        return ProcessRuntimeStatus::AlreadyActive;
    }

    const bool interrupts_were_enabled = DisableInterrupts();
    process_scheduling_active = true;
    while (process_scheduling_active) {
        ThreadSchedulingDecision decision{};
        const bool scheduler_interrupts_were_enabled = scheduler_lock.Lock();
        const ThreadSchedulerStatus scheduler_status = thread_scheduler.Start(decision);
        scheduler_lock.Unlock(scheduler_interrupts_were_enabled);
        if (scheduler_status == ThreadSchedulerStatus::NoReadyThread) {
            if (decision.completed) {
                process_scheduling_active = false;
                break;
            }
            EnableInterruptsWaitAndDisable();
            continue;
        }
        if (scheduler_status != ThreadSchedulerStatus::Succeeded) {
            process_scheduling_active = false;
            RestoreInterrupts(interrupts_were_enabled);
            return ProcessRuntimeStatus::SchedulerFailure;
        }
        if (!ActivateThread(decision.current_thread_index)) {
            process_scheduling_active = false;
            RestoreInterrupts(interrupts_were_enabled);
            return ProcessRuntimeStatus::PageTableActivationFailure;
        }
        OsKernelEnterScheduledProcess(runtime_threads[decision.current_thread_index].saved_frame);
        if (ReadPageTableRoot() != GetKernelPageTableRoot()) {
            HaltProcessor();
        }
        if (!ReapExitedThreads() || (!process_scheduling_active && !CollectTerminalInitProcess())) {
            process_scheduling_active = false;
            RestoreInterrupts(interrupts_were_enabled);
            return ProcessRuntimeStatus::KernelStackFailure;
        }
    }
    frames_after_processes = GetPhysicalFrameAllocatorStatistics();
    virtual_addresses_after_processes = GetKernelVirtualAddressAllocator().Statistics();
    kernel_stacks_after_processes = GetKernelStackManager().Statistics();
    virtual_memory_areas_after_processes = GetUserVirtualMemoryPoolStatistics();
    if (GetKernelStackManager().Validate() != KernelStackManagerStatus::Succeeded ||
        kernel_stacks_after_processes.active_stack_count != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        virtual_memory_areas_after_processes.capacity !=
            virtual_memory_areas_before_processes.capacity ||
        virtual_memory_areas_after_processes.active_descriptor_count !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        virtual_memory_areas_after_processes.free_descriptor_count !=
            virtual_memory_areas_after_processes.capacity ||
        virtual_memory_areas_after_processes.successful_acquire_count <
            virtual_memory_areas_before_processes.successful_acquire_count ||
        virtual_memory_areas_after_processes.release_count <
            virtual_memory_areas_before_processes.release_count ||
        virtual_memory_areas_after_processes.successful_acquire_count -
                virtual_memory_areas_before_processes.successful_acquire_count !=
            virtual_memory_areas_after_processes.release_count -
                virtual_memory_areas_before_processes.release_count) {
        RestoreInterrupts(interrupts_were_enabled);
        return ProcessRuntimeStatus::ResourceLeakDetected;
    }
    const ThreadSchedulerStatistics final_scheduler_statistics = thread_scheduler.Statistics();
    fs::ResourceUsage vfs_resource_usage{};
    if (process_vfs == nullptr ||
        process_vfs->ReadResourceUsage(vfs_resource_usage) != fs::Status::Succeeded) {
        RestoreInterrupts(interrupts_were_enabled);
        return ProcessRuntimeStatus::FileSystemFailure;
    }
    const ResourceSnapshotSupplementalCounts supplemental_counts{
        .process_count = final_scheduler_statistics.owned_process_count,
        .thread_count = final_scheduler_statistics.owned_thread_count,
        .file_description_count = kernel_object_manager.Statistics().active_file_description_count,
        .vnode_count = vfs_resource_usage.vnode_count,
        .cache_page_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
        .block_request_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
    };
    if (thread_scheduler.Validate() != ThreadSchedulerStatus::Succeeded ||
        process_tree.Validate() != ProcessTreeStatus::Succeeded ||
        GetKernelResourceSnapshot(supplemental_counts, resource_snapshot_after_processes) !=
            ResourceSnapshotStatus::Succeeded ||
        !DiscountPersistentVfsResources(resource_snapshot_after_processes, vfs_resource_usage) ||
        CompareResourceSnapshots(resource_snapshot_before_processes,
                                 resource_snapshot_after_processes, resource_snapshot_difference) !=
            ResourceSnapshotStatus::Succeeded ||
        resource_snapshot_difference.changed_fields_mask != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        resource_snapshot_difference.changed_field_count != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        RestoreInterrupts(interrupts_were_enabled);
        return ProcessRuntimeStatus::ResourceLeakDetected;
    }
    RestoreInterrupts(interrupts_were_enabled);
    return ProcessRuntimeStatus::Succeeded;
}

ProcessRuntimeStatistics GetProcessRuntimeStatistics() noexcept {
    ProcessRuntimeStatistics statistics{
        .scheduler = thread_scheduler.Statistics(),
        .extended_state = GetExtendedStateConfiguration(),
        .configured_process_capacity = process_runtime_limits.process_capacity,
        .configured_thread_capacity = process_runtime_limits.thread_capacity,
        .configured_threads_per_process = process_runtime_limits.maximum_threads_per_process,
        .capacity_self_test_process_count = capacity_self_test_process_count,
        .capacity_self_test_thread_count = capacity_self_test_thread_count,
        .capacity_self_test_threads_per_process = capacity_self_test_threads_per_process,
        .frames_before_processes = frames_before_processes,
        .frames_after_processes = frames_after_processes,
        .virtual_addresses_before_processes = virtual_addresses_before_processes,
        .virtual_addresses_after_processes = virtual_addresses_after_processes,
        .kernel_stacks_before_processes = kernel_stacks_before_processes,
        .kernel_stacks_after_processes = kernel_stacks_after_processes,
        .virtual_memory_areas_before_processes = virtual_memory_areas_before_processes,
        .virtual_memory_areas_after_processes = virtual_memory_areas_after_processes,
        .resource_snapshot_before_processes = resource_snapshot_before_processes,
        .resource_snapshot_after_processes = resource_snapshot_after_processes,
        .resource_snapshot_difference = resource_snapshot_difference,
        .ipc =
            ProcessIpcStatistics{
                .pipe = process_pipe.Statistics(),
                .reader_block_count = pipe_reader_block_count,
                .writer_block_count = pipe_writer_block_count,
                .end_of_file_observation_count = pipe_end_of_file_observation_count,
                .broken_pipe_observation_count = pipe_broken_observation_count,
            },
        .process_tree = process_tree.Statistics(),
        .console_input = process_console_input.Statistics(),
        .object_manager = kernel_object_manager.Statistics(),
        .file_descriptions = file_description_manager.Statistics(),
        .file_tables = {},
        .processes = {},
    };
    for (uint64_t process_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         process_index < OS_KERNEL_PROCESS_RUNTIME_RESULT_CAPACITY; ++process_index) {
        statistics.processes[process_index] = runtime_processes[process_index].result;
        statistics.file_tables[process_index] =
            runtime_processes[process_index].file_table.Statistics();
    }
    return statistics;
}

bool IsProcessSchedulingActive() noexcept {
    return process_scheduling_active && thread_scheduler.IsActive();
}

uint64_t CurrentProcessId() noexcept {
    if (!IsProcessSchedulingActive()) {
        HaltProcessor();
    }
    ThreadEntry thread{};
    ProcessEntry process{};
    if (!ReadCurrentThreadAndProcess(thread, process)) {
        HaltProcessor();
    }
    return process.process_id.value;
}

uint64_t CurrentThreadId() noexcept {
    if (!IsProcessSchedulingActive()) {
        HaltProcessor();
    }
    ThreadId thread_id{};
    if (thread_scheduler.CurrentThreadId(thread_id) != ThreadSchedulerStatus::Succeeded) {
        HaltProcessor();
    }
    return thread_id.value;
}

UserProgramSelection CurrentProcessSelection() noexcept {
    if (!IsProcessSchedulingActive()) {
        HaltProcessor();
    }
    return CurrentRuntimeProcess().result.selection;
}

UserVirtualMemoryStatus MapCurrentProcessAnonymousMemory(const uint64_t requested_address,
                                                         const uint64_t length_bytes,
                                                         const uint64_t protection_flags,
                                                         const uint64_t map_flags,
                                                         uint64_t &mapped_address) noexcept {
    if (!IsProcessSchedulingActive()) {
        return UserVirtualMemoryStatus::NotInitialized;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    const UserVirtualMemoryStatus status =
        MapAnonymousMemory(process.address_space, requested_address, length_bytes, protection_flags,
                           map_flags, mapped_address);
    process.result.mapped_page_count = process.address_space.mapped_page_count;
    return status;
}

UserVirtualMemoryStatus UnmapCurrentProcessAnonymousMemory(const uint64_t address,
                                                           const uint64_t length_bytes) noexcept {
    if (!IsProcessSchedulingActive()) {
        return UserVirtualMemoryStatus::NotInitialized;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    const UserVirtualMemoryStatus status =
        UnmapAnonymousMemory(process.address_space, address, length_bytes);
    process.result.mapped_page_count = process.address_space.mapped_page_count;
    return status;
}

UserVirtualMemoryStatus SetCurrentProcessProgramBreak(const uint64_t requested_address,
                                                      uint64_t &program_break_address) noexcept {
    if (!IsProcessSchedulingActive()) {
        return UserVirtualMemoryStatus::NotInitialized;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    const UserVirtualMemoryStatus status =
        SetProgramBreak(process.address_space, requested_address, program_break_address);
    process.result.mapped_page_count = process.address_space.mapped_page_count;
    return status;
}

os::abi::VirtualMemoryStatistics GetCurrentProcessVirtualMemoryStatistics() noexcept {
    if (!IsProcessSchedulingActive()) {
        return os::abi::VirtualMemoryStatistics{};
    }
    return GetUserVirtualMemoryStatistics(CurrentRuntimeProcess().address_space);
}

void RecordCurrentProcessSystemCall() noexcept {
    if (!IsProcessSchedulingActive()) {
        HaltProcessor();
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    ++process.result.system_call_count;
}

bool CurrentProcessCanReadPipe() noexcept {
    return CurrentProcessSelection() == UserProgramSelection::IpcConsumer;
}

bool CurrentProcessCanWritePipe() noexcept {
    return CurrentProcessSelection() == UserProgramSelection::IpcProducer;
}

PipeStatus TryReadCurrentProcessPipe(uint8_t *destination, const uint64_t capacity_bytes,
                                     uint64_t &read_bytes) noexcept {
    if (!CurrentProcessCanReadPipe()) {
        read_bytes = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        return PipeStatus::InvalidArgument;
    }
    FileSystemStatus file_system_status = FileSystemStatus::Succeeded;
    return MapProcessIoToPipeStatus(
        TryReadCurrentProcessDescriptor(OS_KERNEL_FILE_TABLE_FIRST_DYNAMIC_DESCRIPTOR, destination,
                                        capacity_bytes, read_bytes, file_system_status));
}

PipeStatus TryWriteCurrentProcessPipe(const uint8_t *source, const uint64_t length_bytes,
                                      uint64_t &written_bytes) noexcept {
    if (!CurrentProcessCanWritePipe()) {
        written_bytes = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        return PipeStatus::InvalidArgument;
    }
    FileSystemStatus file_system_status = FileSystemStatus::Succeeded;
    return MapProcessIoToPipeStatus(
        TryWriteCurrentProcessDescriptor(OS_KERNEL_FILE_TABLE_FIRST_DYNAMIC_DESCRIPTOR, source,
                                         length_bytes, written_bytes, file_system_status));
}

PipeStatus CloseCurrentProcessPipeReader() noexcept {
    if (!CurrentProcessCanReadPipe()) {
        return PipeStatus::InvalidArgument;
    }
    FileSystemStatus file_system_status = FileSystemStatus::Succeeded;
    return MapProcessIoToPipeStatus(CloseCurrentProcessDescriptor(
        OS_KERNEL_FILE_TABLE_FIRST_DYNAMIC_DESCRIPTOR, file_system_status));
}

PipeStatus CloseCurrentProcessPipeWriter() noexcept {
    if (!CurrentProcessCanWritePipe()) {
        return PipeStatus::InvalidArgument;
    }
    FileSystemStatus file_system_status = FileSystemStatus::Succeeded;
    return MapProcessIoToPipeStatus(CloseCurrentProcessDescriptor(
        OS_KERNEL_FILE_TABLE_FIRST_DYNAMIC_DESCRIPTOR, file_system_status));
}

FileSystemStatus OpenCurrentProcessFile(const uint8_t *path, const uint64_t path_length_bytes,
                                        const fs::OpenOptions &options,
                                        uint64_t &file_descriptor) noexcept {
    file_descriptor = OS_KERNEL_PROCESS_RUNTIME_INVALID_FILE_DESCRIPTOR;
    if (!IsProcessSchedulingActive() || process_vfs == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    fs::OpenFile open_file{};
    const fs::Status status =
        process_vfs->Open(process.file_system_context, path, path_length_bytes, options, open_file);
    if (status != fs::Status::Succeeded) {
        return fs::ToFileSystemStatus(status);
    }
    const uint64_t file_status_flags =
        (options.readable ? OS_KERNEL_FILE_DESCRIPTION_READABLE_STATUS_FLAG
                          : OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) |
        (options.writable ? OS_KERNEL_FILE_DESCRIPTION_WRITABLE_STATUS_FLAG
                          : OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE);
    const FileDescriptionCreateRequest request{
        .kind = FileDescriptionKind::RegularFile,
        .file_status_flags = file_status_flags,
        .console_input = nullptr,
        .device_write_operation = nullptr,
        .device_write_context = nullptr,
        .pipe = nullptr,
        .vfs = process_vfs,
        .open_file = open_file,
    };
    KernelObjectReference reference{};
    if (file_description_manager.Create(request, reference) != FileDescriptionStatus::Succeeded) {
        if (process_vfs->Close(open_file) != fs::Status::Succeeded) {
            HaltProcessor();
        }
        return FileSystemStatus::DataCapacityExhausted;
    }
    const FileTableStatus install_status =
        process.file_table.Install(reference, OS_KERNEL_FILE_TABLE_FIRST_DYNAMIC_DESCRIPTOR,
                                   OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE, file_descriptor);
    return install_status == FileTableStatus::Succeeded ? FileSystemStatus::Succeeded
                                                        : FileSystemStatus::DataCapacityExhausted;
}

FileSystemStatus ReadCurrentProcessFile(const uint64_t file_descriptor, uint8_t *destination,
                                        const uint64_t capacity_bytes,
                                        uint64_t &read_bytes) noexcept {
    read_bytes = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if (!IsProcessSchedulingActive() || process_vfs == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    KernelObjectReference reference{};
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    if (process.file_table.Lookup(file_descriptor, reference) != FileTableStatus::Succeeded) {
        return FileSystemStatus::InvalidHandle;
    }
    FileDescriptionSnapshot snapshot{};
    if (file_description_manager.ReadSnapshot(reference, snapshot) !=
            FileDescriptionStatus::Succeeded ||
        snapshot.kind != FileDescriptionKind::RegularFile) {
        return FileSystemStatus::InvalidHandle;
    }
    FileSystemStatus file_system_status = FileSystemStatus::Succeeded;
    PipeStatus pipe_status = PipeStatus::Succeeded;
    const FileDescriptionStatus status = file_description_manager.TryRead(
        reference, destination, capacity_bytes, read_bytes, file_system_status, pipe_status);
    if (status == FileDescriptionStatus::Succeeded) {
        process.result.file_system_bytes_read += read_bytes;
    }
    return MapProcessIoToFileSystemStatus(MapFileDescriptionStatus(status), file_system_status);
}

FileSystemStatus WriteCurrentProcessFile(const uint64_t file_descriptor, const uint8_t *source,
                                         const uint64_t length_bytes,
                                         uint64_t &written_bytes) noexcept {
    written_bytes = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if (!IsProcessSchedulingActive() || process_vfs == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    KernelObjectReference reference{};
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    if (process.file_table.Lookup(file_descriptor, reference) != FileTableStatus::Succeeded) {
        return FileSystemStatus::InvalidHandle;
    }
    FileDescriptionSnapshot snapshot{};
    if (file_description_manager.ReadSnapshot(reference, snapshot) !=
            FileDescriptionStatus::Succeeded ||
        snapshot.kind != FileDescriptionKind::RegularFile) {
        return FileSystemStatus::InvalidHandle;
    }
    FileSystemStatus file_system_status = FileSystemStatus::Succeeded;
    PipeStatus pipe_status = PipeStatus::Succeeded;
    const FileDescriptionStatus status = file_description_manager.TryWrite(
        reference, source, length_bytes, written_bytes, file_system_status, pipe_status);
    if (status == FileDescriptionStatus::Succeeded) {
        process.result.file_system_bytes_written += written_bytes;
    }
    return MapProcessIoToFileSystemStatus(MapFileDescriptionStatus(status), file_system_status);
}

FileSystemStatus CloseCurrentProcessFile(const uint64_t file_descriptor) noexcept {
    if (!IsProcessSchedulingActive() || process_vfs == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    KernelObjectReference reference{};
    if (process.file_table.Lookup(file_descriptor, reference) != FileTableStatus::Succeeded) {
        return FileSystemStatus::InvalidHandle;
    }
    FileDescriptionSnapshot snapshot{};
    if (file_description_manager.ReadSnapshot(reference, snapshot) !=
            FileDescriptionStatus::Succeeded ||
        snapshot.kind != FileDescriptionKind::RegularFile ||
        reference.Reset() != KernelObjectStatus::Succeeded) {
        return FileSystemStatus::InvalidHandle;
    }
    FileSystemStatus file_system_status = FileSystemStatus::Succeeded;
    return MapProcessIoToFileSystemStatus(
        CloseCurrentProcessDescriptor(file_descriptor, file_system_status), file_system_status);
}

FileSystemStatus CreateCurrentProcessDirectory(const uint8_t *path,
                                               const uint64_t path_length_bytes) noexcept {
    if (!IsProcessSchedulingActive() || process_vfs == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    return fs::ToFileSystemStatus(
        process_vfs->CreateDirectory(process.file_system_context, path, path_length_bytes));
}

FileSystemStatus RemoveCurrentProcessFile(const uint8_t *path,
                                          const uint64_t path_length_bytes) noexcept {
    if (!IsProcessSchedulingActive() || process_vfs == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    return fs::ToFileSystemStatus(
        process_vfs->RemoveFile(process.file_system_context, path, path_length_bytes));
}

FileSystemStatus RemoveCurrentProcessDirectory(const uint8_t *path,
                                               const uint64_t path_length_bytes) noexcept {
    if (!IsProcessSchedulingActive() || process_vfs == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    return fs::ToFileSystemStatus(
        process_vfs->RemoveDirectory(process.file_system_context, path, path_length_bytes));
}

FileSystemStatus RenameCurrentProcessPath(const uint8_t *source_path,
                                          const uint64_t source_path_length_bytes,
                                          const uint8_t *destination_path,
                                          const uint64_t destination_path_length_bytes) noexcept {
    if (!IsProcessSchedulingActive() || process_vfs == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    return fs::ToFileSystemStatus(process_vfs->Rename(process.file_system_context, source_path,
                                                      source_path_length_bytes, destination_path,
                                                      destination_path_length_bytes, true));
}

FileSystemStatus TruncateCurrentProcessFile(const uint8_t *path, const uint64_t path_length_bytes,
                                            const uint64_t size_bytes) noexcept {
    if (!IsProcessSchedulingActive() || process_vfs == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    return fs::ToFileSystemStatus(
        process_vfs->Truncate(process.file_system_context, path, path_length_bytes, size_bytes));
}

FileSystemStatus StatCurrentProcessPath(const uint8_t *path, const uint64_t path_length_bytes,
                                        fs::NodeInformation &information) noexcept {
    information = fs::NodeInformation{};
    if (!IsProcessSchedulingActive() || process_vfs == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    return fs::ToFileSystemStatus(
        process_vfs->Stat(process.file_system_context, path, path_length_bytes, information));
}

FileSystemStatus SyncCurrentProcessFileSystem() noexcept {
    if (!IsProcessSchedulingActive() || process_vfs == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    return fs::ToFileSystemStatus(process_vfs->Sync());
}

FileSystemStatus ChangeCurrentProcessDirectory(const uint8_t *path,
                                               const uint64_t path_length_bytes) noexcept {
    if (!IsProcessSchedulingActive() || process_vfs == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    return fs::ToFileSystemStatus(
        process_vfs->ChangeDirectory(process.file_system_context, path, path_length_bytes));
}

FileSystemStatus GetCurrentProcessWorkingDirectory(uint8_t *const destination,
                                                   const uint64_t capacity_bytes,
                                                   uint64_t &path_length_bytes) noexcept {
    path_length_bytes = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if (!IsProcessSchedulingActive() || process_vfs == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    return fs::ToFileSystemStatus(process_vfs->GetWorkingDirectory(
        process.file_system_context, destination, capacity_bytes, path_length_bytes));
}

ProcessIoStatus TryReadCurrentProcessDescriptor(const uint64_t descriptor,
                                                uint8_t *const destination,
                                                const uint64_t capacity_bytes, uint64_t &read_bytes,
                                                FileSystemStatus &file_system_status) noexcept {
    read_bytes = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    file_system_status = FileSystemStatus::Succeeded;
    if (!IsProcessSchedulingActive()) {
        return ProcessIoStatus::InvalidArgument;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    KernelObjectReference reference{};
    const FileTableStatus lookup_status = process.file_table.Lookup(descriptor, reference);
    if (lookup_status != FileTableStatus::Succeeded) {
        return MapFileTableStatus(lookup_status);
    }
    FileDescriptionSnapshot snapshot{};
    if (file_description_manager.ReadSnapshot(reference, snapshot) !=
        FileDescriptionStatus::Succeeded) {
        return ProcessIoStatus::ObjectFailure;
    }
    PipeStatus pipe_status = PipeStatus::Succeeded;
    const FileDescriptionStatus description_status = file_description_manager.TryRead(
        reference, destination, capacity_bytes, read_bytes, file_system_status, pipe_status);
    const ProcessIoStatus status = MapFileDescriptionStatus(description_status);
    if (status == ProcessIoStatus::Succeeded) {
        if (snapshot.kind == FileDescriptionKind::ConsoleInput) {
            process.result.console_bytes_read += read_bytes;
        } else if (snapshot.kind == FileDescriptionKind::RegularFile) {
            process.result.file_system_bytes_read += read_bytes;
        } else if (snapshot.kind == FileDescriptionKind::PipeReader) {
            process.result.pipe_bytes_read += read_bytes;
            WakeRequiredThreads(WaitCondition::DescriptorWritable, WakeReason::ConditionSatisfied);
        }
    } else if (status == ProcessIoStatus::EndOfFile &&
               snapshot.kind == FileDescriptionKind::PipeReader) {
        ++pipe_end_of_file_observation_count;
    }
    return status;
}

ProcessIoStatus TryWriteCurrentProcessDescriptor(const uint64_t descriptor,
                                                 const uint8_t *const source,
                                                 const uint64_t length_bytes,
                                                 uint64_t &written_bytes,
                                                 FileSystemStatus &file_system_status) noexcept {
    written_bytes = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    file_system_status = FileSystemStatus::Succeeded;
    if (!IsProcessSchedulingActive()) {
        return ProcessIoStatus::InvalidArgument;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    KernelObjectReference reference{};
    const FileTableStatus lookup_status = process.file_table.Lookup(descriptor, reference);
    if (lookup_status != FileTableStatus::Succeeded) {
        return MapFileTableStatus(lookup_status);
    }
    FileDescriptionSnapshot snapshot{};
    if (file_description_manager.ReadSnapshot(reference, snapshot) !=
        FileDescriptionStatus::Succeeded) {
        return ProcessIoStatus::ObjectFailure;
    }
    PipeStatus pipe_status = PipeStatus::Succeeded;
    const FileDescriptionStatus description_status = file_description_manager.TryWrite(
        reference, source, length_bytes, written_bytes, file_system_status, pipe_status);
    const ProcessIoStatus status = MapFileDescriptionStatus(description_status);
    if (status == ProcessIoStatus::Succeeded) {
        if (snapshot.kind == FileDescriptionKind::ConsoleOutput ||
            snapshot.kind == FileDescriptionKind::ConsoleError) {
            process.result.console_bytes_written += written_bytes;
        } else if (snapshot.kind == FileDescriptionKind::RegularFile) {
            process.result.file_system_bytes_written += written_bytes;
        } else if (snapshot.kind == FileDescriptionKind::PipeWriter) {
            process.result.pipe_bytes_written += written_bytes;
            WakeRequiredThreads(WaitCondition::DescriptorReadable, WakeReason::ConditionSatisfied);
        }
    } else if (status == ProcessIoStatus::BrokenPipe &&
               snapshot.kind == FileDescriptionKind::PipeWriter) {
        ++pipe_broken_observation_count;
    }
    return status;
}

ProcessIoStatus CloseCurrentProcessDescriptor(const uint64_t descriptor,
                                              FileSystemStatus &file_system_status) noexcept {
    file_system_status = FileSystemStatus::Succeeded;
    if (!IsProcessSchedulingActive()) {
        return ProcessIoStatus::InvalidArgument;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    KernelObjectReleaseResult release_result{};
    const FileTableStatus close_status = process.file_table.Close(descriptor, release_result);
    if (close_status != FileTableStatus::Succeeded) {
        return MapFileTableStatus(close_status);
    }
    WakeAfterDescriptionRelease(release_result);
    return ProcessIoStatus::Succeeded;
}

ProcessIoStatus DuplicateCurrentProcessDescriptor(const uint64_t source_descriptor,
                                                  const uint64_t minimum_descriptor,
                                                  const uint64_t descriptor_flags,
                                                  uint64_t &destination_descriptor) noexcept {
    destination_descriptor = OS_KERNEL_PROCESS_RUNTIME_INVALID_FILE_DESCRIPTOR;
    if (!IsProcessSchedulingActive()) {
        return ProcessIoStatus::InvalidArgument;
    }
    return MapFileTableStatus(CurrentRuntimeProcess().file_table.Duplicate(
        source_descriptor, minimum_descriptor, descriptor_flags, destination_descriptor));
}

ProcessIoStatus GetCurrentProcessDescriptorFlags(const uint64_t descriptor,
                                                 uint64_t &descriptor_flags) noexcept {
    descriptor_flags = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if (!IsProcessSchedulingActive()) {
        return ProcessIoStatus::InvalidArgument;
    }
    return MapFileTableStatus(
        CurrentRuntimeProcess().file_table.GetDescriptorFlags(descriptor, descriptor_flags));
}

ProcessIoStatus SetCurrentProcessDescriptorFlags(const uint64_t descriptor,
                                                 const uint64_t descriptor_flags) noexcept {
    if (!IsProcessSchedulingActive()) {
        return ProcessIoStatus::InvalidArgument;
    }
    return MapFileTableStatus(
        CurrentRuntimeProcess().file_table.SetDescriptorFlags(descriptor, descriptor_flags));
}

ProcessIoStatus SetCurrentProcessDescriptorSoftLimit(const uint64_t soft_limit) noexcept {
    if (!IsProcessSchedulingActive()) {
        return ProcessIoStatus::InvalidArgument;
    }
    return MapFileTableStatus(CurrentRuntimeProcess().file_table.SetSoftLimit(soft_limit));
}

ProcessIoStatus GetCurrentProcessDescriptorLimits(uint64_t &soft_limit,
                                                  uint64_t &hard_limit) noexcept {
    soft_limit = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    hard_limit = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if (!IsProcessSchedulingActive()) {
        return ProcessIoStatus::InvalidArgument;
    }
    const FileTableStatistics statistics = CurrentRuntimeProcess().file_table.Statistics();
    soft_limit = statistics.soft_limit;
    hard_limit = statistics.hard_limit;
    return ProcessIoStatus::Succeeded;
}

FileSystemStatus OpenCurrentProcessDirectory(const uint8_t *const path,
                                             const uint64_t path_length_bytes,
                                             uint64_t &file_descriptor) noexcept {
    file_descriptor = OS_KERNEL_PROCESS_RUNTIME_INVALID_FILE_DESCRIPTOR;
    if (!IsProcessSchedulingActive() || process_vfs == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    fs::OpenFile open_file{};
    const fs::Status status =
        process_vfs->OpenDirectory(process.file_system_context, path, path_length_bytes, open_file);
    if (status != fs::Status::Succeeded) {
        return fs::ToFileSystemStatus(status);
    }
    const FileDescriptionCreateRequest request{
        .kind = FileDescriptionKind::Directory,
        .file_status_flags = OS_KERNEL_FILE_DESCRIPTION_READABLE_STATUS_FLAG,
        .console_input = nullptr,
        .device_write_operation = nullptr,
        .device_write_context = nullptr,
        .pipe = nullptr,
        .vfs = process_vfs,
        .open_file = open_file,
    };
    KernelObjectReference reference{};
    if (file_description_manager.Create(request, reference) != FileDescriptionStatus::Succeeded) {
        if (process_vfs->Close(open_file) != fs::Status::Succeeded) {
            HaltProcessor();
        }
        return FileSystemStatus::DataCapacityExhausted;
    }
    return process.file_table.Install(reference, OS_KERNEL_FILE_TABLE_FIRST_DYNAMIC_DESCRIPTOR,
                                      OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
                                      file_descriptor) == FileTableStatus::Succeeded
               ? FileSystemStatus::Succeeded
               : FileSystemStatus::DataCapacityExhausted;
}

FileSystemStatus ReadCurrentProcessDirectory(const uint64_t file_descriptor,
                                             fs::DirectoryEntry &entry,
                                             bool &end_of_directory) noexcept {
    entry = fs::DirectoryEntry{};
    end_of_directory = false;
    if (!IsProcessSchedulingActive() || process_vfs == nullptr) {
        return FileSystemStatus::InvalidHandle;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    KernelObjectReference reference{};
    if (process.file_table.Lookup(file_descriptor, reference) != FileTableStatus::Succeeded) {
        return FileSystemStatus::InvalidHandle;
    }
    FileDescriptionSnapshot snapshot{};
    if (file_description_manager.ReadSnapshot(reference, snapshot) !=
            FileDescriptionStatus::Succeeded ||
        snapshot.kind != FileDescriptionKind::Directory) {
        return FileSystemStatus::InvalidHandle;
    }
    FileSystemStatus file_system_status = FileSystemStatus::Succeeded;
    const FileDescriptionStatus read_status = file_description_manager.ReadDirectory(
        reference, entry, end_of_directory, file_system_status);
    return MapProcessIoToFileSystemStatus(MapFileDescriptionStatus(read_status),
                                          file_system_status);
}

ProcessIoStatus CurrentProcessDescriptorReadCanProgress(const uint64_t descriptor,
                                                        bool &can_progress) noexcept {
    can_progress = false;
    if (!IsProcessSchedulingActive()) {
        return ProcessIoStatus::InvalidArgument;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    KernelObjectReference reference{};
    const FileTableStatus lookup_status = process.file_table.Lookup(descriptor, reference);
    if (lookup_status != FileTableStatus::Succeeded) {
        return MapFileTableStatus(lookup_status);
    }
    FileDescriptionSnapshot snapshot{};
    if (file_description_manager.ReadSnapshot(reference, snapshot) !=
        FileDescriptionStatus::Succeeded) {
        return ProcessIoStatus::ObjectFailure;
    }
    const FileDescriptionStatus progress_status =
        file_description_manager.ReadCanProgress(reference, can_progress);
    if (progress_status == FileDescriptionStatus::Succeeded &&
        snapshot.kind == FileDescriptionKind::PipeReader && !can_progress) {
        ++pipe_reader_block_count;
    }
    return MapFileDescriptionStatus(progress_status);
}

ProcessIoStatus CurrentProcessDescriptorWriteCanProgress(const uint64_t descriptor,
                                                         bool &can_progress) noexcept {
    can_progress = false;
    if (!IsProcessSchedulingActive()) {
        return ProcessIoStatus::InvalidArgument;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    KernelObjectReference reference{};
    const FileTableStatus lookup_status = process.file_table.Lookup(descriptor, reference);
    if (lookup_status != FileTableStatus::Succeeded) {
        return MapFileTableStatus(lookup_status);
    }
    FileDescriptionSnapshot snapshot{};
    if (file_description_manager.ReadSnapshot(reference, snapshot) !=
        FileDescriptionStatus::Succeeded) {
        return ProcessIoStatus::ObjectFailure;
    }
    const FileDescriptionStatus progress_status =
        file_description_manager.WriteCanProgress(reference, can_progress);
    if (progress_status == FileDescriptionStatus::Succeeded &&
        snapshot.kind == FileDescriptionKind::PipeWriter && !can_progress) {
        ++pipe_writer_block_count;
    }
    return MapFileDescriptionStatus(progress_status);
}

void SubmitConsoleCharacter(const uint8_t character) noexcept {
    const ConsoleInputStatus submit_status = process_console_input.Submit(character);
    if (submit_status == ConsoleInputStatus::Succeeded && process_scheduling_active) {
        WakeRequiredThreads(WaitCondition::DescriptorReadable, WakeReason::ConditionSatisfied);
    }
}

bool ProcessPipeReadCanProgress() noexcept { return process_pipe.ReadCanProgress(); }

bool ProcessPipeWriteCanProgress() noexcept { return process_pipe.WriteCanProgress(); }

ProcessRuntimeStatus BlockCurrentThread(ExceptionFrame &frame, const WaitCondition wait_condition,
                                        ExceptionFrame *&resume_frame) noexcept {
    resume_frame = &frame;
    const uint64_t thread_index = thread_scheduler.CurrentThreadIndex();
    WaitQueue *const wait_queue = SelectWaitQueue(wait_condition);
    if (!IsProcessSchedulingActive() || wait_queue == nullptr ||
        !CurrentFrameIsValid(thread_index, frame)) {
        return ProcessRuntimeStatus::SchedulerFailure;
    }
    ProcessRuntimeThread &runtime_thread = runtime_threads[thread_index];
    runtime_thread.saved_frame = &frame;
    if (SaveFxState(runtime_thread.extended_state) != ExtendedStateStatus::Succeeded) {
        return ProcessRuntimeStatus::ExtendedStateFailure;
    }

    ThreadSchedulingDecision decision{};
    const bool interrupts_were_enabled = scheduler_lock.Lock();
    const ThreadSchedulerStatus status =
        thread_scheduler.BlockCurrentThread(*wait_queue, wait_condition, decision);
    scheduler_lock.Unlock(interrupts_were_enabled);
    if (status != ThreadSchedulerStatus::Succeeded) {
        return ProcessRuntimeStatus::SchedulerFailure;
    }
    if (wait_condition == WaitCondition::PipeReadable) {
        ++pipe_reader_block_count;
    } else if (wait_condition == WaitCondition::PipeWritable) {
        ++pipe_writer_block_count;
    }
    if (!decision.switched) {
        const uint64_t swap_gs_required = GetCpuLocal().NativeSystemCallActive()
                                              ? OS_KERNEL_PROCESS_RUNTIME_BOOLEAN_TRUE
                                              : OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        SetActiveUserAddressSpace(nullptr);
        ActivateKernelPageTable();
        if (!SetPrivilegeStackPointer0(DefaultPrivilegeStackPointer0())) {
            HaltProcessor();
        }
        if (GetCpuLocal().ClearCurrentThread(DefaultPrivilegeStackPointer0()) !=
            CpuLocalStatus::Succeeded) {
            HaltProcessor();
        }
        OsKernelReturnFromUserMode(swap_gs_required);
    }
    if (!ActivateThread(decision.current_thread_index)) {
        return ProcessRuntimeStatus::SchedulerFailure;
    }
    resume_frame = runtime_threads[decision.current_thread_index].saved_frame;
    return ProcessRuntimeStatus::Succeeded;
}

ProcessRuntimeStatus WakeThreads(const WaitCondition wait_condition, const WakeReason wake_reason,
                                 const uint64_t maximum_wake_count,
                                 uint64_t &woken_thread_count) noexcept {
    woken_thread_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    WaitQueue *const wait_queue = SelectWaitQueue(wait_condition);
    if (!process_runtime_initialized || !process_scheduling_active || wait_queue == nullptr) {
        return ProcessRuntimeStatus::SchedulerFailure;
    }
    const bool interrupts_were_enabled = scheduler_lock.Lock();
    const ThreadSchedulerStatus status =
        thread_scheduler.WakeMany(*wait_queue, wake_reason, maximum_wake_count, woken_thread_count);
    scheduler_lock.Unlock(interrupts_were_enabled);
    return status == ThreadSchedulerStatus::Succeeded ? ProcessRuntimeStatus::Succeeded
                                                      : ProcessRuntimeStatus::SchedulerFailure;
}

ExceptionFrame *HandleProcessTimerInterrupt(ExceptionFrame &frame) noexcept {
    if (!process_scheduling_active || !FrameOriginatedFromUser(frame)) {
        return &frame;
    }
    const uint64_t thread_index = thread_scheduler.CurrentThreadIndex();
    if (!CurrentFrameIsValid(thread_index, frame)) {
        HaltProcessor();
    }
    ProcessRuntimeThread &runtime_thread = runtime_threads[thread_index];
    runtime_thread.saved_frame = &frame;
    if (SaveFxState(runtime_thread.extended_state) != ExtendedStateStatus::Succeeded) {
        HaltProcessor();
    }

    ThreadSchedulingDecision decision{};
    const bool interrupts_were_enabled = scheduler_lock.Lock();
    const ThreadSchedulerStatus scheduler_status = thread_scheduler.HandleTimerTick(decision);
    scheduler_lock.Unlock(interrupts_were_enabled);
    if (scheduler_status != ThreadSchedulerStatus::Succeeded) {
        HaltProcessor();
    }
    if (!decision.switched) {
        return &frame;
    }
    if (!ActivateThread(decision.current_thread_index)) {
        HaltProcessor();
    }
    return runtime_threads[decision.current_thread_index].saved_frame;
}

ExceptionFrame *RescheduleBeforeUserReturn(ExceptionFrame &frame) noexcept {
    return HandleProcessTimerInterrupt(frame);
}

bool CurrentThreadOwnsUserContext(const ExceptionFrame &frame) noexcept {
    if (!process_scheduling_active) {
        return false;
    }
    return CurrentFrameIsValid(thread_scheduler.CurrentThreadIndex(), frame);
}

bool HandleCurrentProcessPageFault(ExceptionFrame &frame, const uint64_t fault_address) noexcept {
    const uint64_t thread_index = thread_scheduler.CurrentThreadIndex();
    if (!process_scheduling_active || !CurrentFrameIsValid(thread_index, frame)) {
        return false;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    const uint64_t previous_stack_growth_count =
        process.address_space.stack_growth_page_fault_count;
    const UserPageFaultStatus status = HandleUserPageFault(
        process.address_space, fault_address, frame.error_code, AsUserContext(frame).stack_pointer);
    if (status != UserPageFaultStatus::Handled) {
        return false;
    }
    process.result.mapped_page_count = process.address_space.mapped_page_count;

    // 只在 1、2、4、8……次故障打印，既保留增长轨迹，也避免大堆触页冲刷串口。
    const uint64_t demand_fault_count = process.address_space.demand_page_fault_count;
    if ((demand_fault_count & (demand_fault_count - OS_KERNEL_PROCESS_RUNTIME_COUNTER_INCREMENT)) ==
        OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_VM_DEMAND_FAULT_PREFIX,
                                 demand_fault_count);
    }
    if (process.address_space.stack_growth_page_fault_count != previous_stack_growth_count) {
        const uint64_t stack_growth_count = process.address_space.stack_growth_page_fault_count;
        if ((stack_growth_count &
             (stack_growth_count - OS_KERNEL_PROCESS_RUNTIME_COUNTER_INCREMENT)) ==
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
            WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_VM_STACK_GROWTH_PREFIX,
                                     stack_growth_count);
        }
    }
    return true;
}

ExceptionFrame *TerminateCurrentProcessFromExit(ExceptionFrame &frame,
                                                const int64_t exit_code) noexcept {
    return CompleteCurrentThread(frame, ProcessTerminationReason::Exited, exit_code,
                                 OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE);
}

ExceptionFrame *TerminateCurrentProcessFromException(ExceptionFrame &frame,
                                                     const uint64_t page_fault_address) noexcept {
    return CompleteCurrentThread(frame, ProcessTerminationReason::Exception,
                                 OS_KERNEL_PROCESS_RUNTIME_SUCCESS_EXIT_CODE, page_fault_address);
}

ExceptionFrame *TerminateCurrentProcessFromInvalidReturn(ExceptionFrame &frame) noexcept {
    frame.vector = OS_KERNEL_PROCESS_RUNTIME_INVALID_RETURN_VECTOR;
    frame.error_code = OS_KERNEL_PROCESS_RUNTIME_NORMALIZED_ERROR_CODE;
    frame.code_segment = static_cast<uint64_t>(OS_KERNEL_DESCRIPTOR_USER_CODE_SELECTOR);
    return CompleteCurrentThread(frame, ProcessTerminationReason::Exception,
                                 OS_KERNEL_PROCESS_RUNTIME_SUCCESS_EXIT_CODE,
                                 OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE);
}
}

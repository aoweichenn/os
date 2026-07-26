#include "os/kernel/process/process_runtime.hpp"

#include "os/kernel/arch/descriptor_tables.hpp"
#include "os/kernel/memory/memory_manager.hpp"
#include "os/kernel/arch/processor.hpp"
#include "os/kernel/device/serial_port.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_INITIAL_USER_FLAGS = 0x202ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX = 0ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_WAKE_ALL_COUNT = OS_KERNEL_PROCESS_CAPACITY;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_STACK_POINTER_PROBE_SIZE_BYTES = sizeof(uint64_t);
constexpr int64_t OS_KERNEL_PROCESS_RUNTIME_SUCCESS_EXIT_CODE = 0LL;

[[nodiscard]] ProcessIoStatus MapPipeIoStatus(const PipeStatus status) noexcept {
    if (status == PipeStatus::Succeeded) {
        return ProcessIoStatus::Succeeded;
    }
    if (status == PipeStatus::WouldBlock) {
        return ProcessIoStatus::WouldBlock;
    }
    if (status == PipeStatus::EndOfFile) {
        return ProcessIoStatus::EndOfFile;
    }
    if (status == PipeStatus::BrokenPipe) {
        return ProcessIoStatus::BrokenPipe;
    }
    if (status == PipeStatus::AlreadyClosed) {
        return ProcessIoStatus::InvalidDescriptor;
    }
    return ProcessIoStatus::InvalidArgument;
}

struct ProcessControlBlock final {
    UserAddressSpace address_space;
    ExceptionFrame *saved_frame;
    ProcessExecutionResult result;
    IoDescriptorTable descriptors;
    FileSystemHandle file_handles[OS_KERNEL_IO_DESCRIPTOR_CAPACITY];
};

ProcessScheduler process_scheduler;
Pipe process_pipe;
ConsoleInput process_console_input;
ProcessControlBlock process_control_blocks[OS_KERNEL_PROCESS_CAPACITY];
FileSystem *process_file_system;
PhysicalFrameAllocatorStatistics frames_before_processes;
PhysicalFrameAllocatorStatistics frames_after_processes;
KernelVirtualAddressAllocatorStatistics virtual_addresses_before_processes;
KernelVirtualAddressAllocatorStatistics virtual_addresses_after_processes;
KernelStackManagerStatistics kernel_stacks_before_processes;
KernelStackManagerStatistics kernel_stacks_after_processes;
ResourceSnapshot resource_snapshot_before_processes;
ResourceSnapshot resource_snapshot_after_processes;
ResourceSnapshotDifference resource_snapshot_difference;
uint64_t pipe_reader_block_count;
uint64_t pipe_writer_block_count;
uint64_t pipe_end_of_file_observation_count;
uint64_t pipe_broken_observation_count;
bool process_runtime_initialized;
bool process_scheduling_active;

extern "C" void OsKernelEnterScheduledProcess(ExceptionFrame *frame) noexcept;
extern "C" [[noreturn]] void OsKernelReturnFromUserMode() noexcept;

void ResetProcessControlBlocks() noexcept {
    for (uint64_t process_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         process_index < OS_KERNEL_PROCESS_CAPACITY; ++process_index) {
        process_control_blocks[process_index] = ProcessControlBlock{};
    }
}

[[nodiscard]] bool ReadProcessKernelStack(const uint64_t process_index,
                                          KernelStack &stack) noexcept {
    return GetKernelStackManager().Read(process_index, stack) ==
           KernelStackManagerStatus::Succeeded;
}

[[nodiscard]] bool BuildInitialContextFrame(const uint64_t process_index, const uint64_t process_id,
                                            const UserAddressSpace &address_space,
                                            ExceptionFrame *&saved_frame) noexcept {
    KernelStack stack{};
    if (!ReadProcessKernelStack(process_index, stack)) {
        return false;
    }
    const uint64_t stack_top_address = KernelStackTopAddress(stack);
    if (stack_top_address < OS_KERNEL_USER_PRIVILEGE_FRAME_SIZE_BYTES) {
        return false;
    }
    const uint64_t frame_address = stack_top_address - OS_KERNEL_USER_PRIVILEGE_FRAME_SIZE_BYTES;
    if (!GetKernelStackManager().Contains(process_index, frame_address,
                                          OS_KERNEL_USER_PRIVILEGE_FRAME_SIZE_BYTES)) {
        return false;
    }

    UserPrivilegeFrame *const frame = reinterpret_cast<UserPrivilegeFrame *>(frame_address);
    *frame = UserPrivilegeFrame{};
    frame->common.register_rdi = process_id;
    frame->common.vector = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    frame->common.error_code = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    frame->common.instruction_pointer = address_space.entry_virtual_address;
    frame->common.code_segment = static_cast<uint64_t>(OS_KERNEL_DESCRIPTOR_USER_CODE_SELECTOR);
    frame->common.flags = OS_KERNEL_PROCESS_RUNTIME_INITIAL_USER_FLAGS;
    frame->user_stack_pointer = address_space.stack_top_virtual_address;
    frame->user_stack_segment = static_cast<uint64_t>(OS_KERNEL_DESCRIPTOR_USER_DATA_SELECTOR);
    saved_frame = &frame->common;
    return true;
}

[[nodiscard]] bool CurrentFrameIsValid(const uint64_t process_index,
                                       const ExceptionFrame &frame) noexcept {
    if (process_index >= OS_KERNEL_PROCESS_CAPACITY || !FrameOriginatedFromUser(frame) ||
        !GetKernelStackManager().Contains(process_index, reinterpret_cast<uint64_t>(&frame),
                                          OS_KERNEL_USER_PRIVILEGE_FRAME_SIZE_BYTES)) {
        return false;
    }
    const ProcessControlBlock &process = process_control_blocks[process_index];
    return process.address_space.root_physical_address != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE &&
           ReadPageTableRoot() == process.address_space.root_physical_address;
}

[[nodiscard]] bool ActivateProcess(const uint64_t process_index) noexcept {
    if (process_index >= OS_KERNEL_PROCESS_CAPACITY) {
        return false;
    }
    ProcessControlBlock &process = process_control_blocks[process_index];
    KernelStack stack{};
    if (process.saved_frame == nullptr || !ReadProcessKernelStack(process_index, stack) ||
        !GetKernelStackManager().Contains(process_index,
                                          reinterpret_cast<uint64_t>(process.saved_frame),
                                          OS_KERNEL_USER_PRIVILEGE_FRAME_SIZE_BYTES) ||
        !ActivateUserPageTable(process.address_space.root_physical_address)) {
        return false;
    }
    if (!SetPrivilegeStackPointer0(KernelStackTopAddress(stack))) {
        ActivateKernelPageTable();
        return false;
    }
    return true;
}

void WakeRequiredProcesses(const ProcessWaitReason wait_reason) noexcept {
    uint64_t woken_process_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if (process_scheduler.WakeBlockedProcesses(
            wait_reason, OS_KERNEL_PROCESS_RUNTIME_WAKE_ALL_COUNT, woken_process_count) !=
        ProcessSchedulerStatus::Succeeded) {
        HaltProcessor();
    }
}

void CloseProcessIoDescriptors(ProcessControlBlock &process) noexcept {
    if (process_file_system == nullptr) {
        HaltProcessor();
    }
    for (uint64_t descriptor_index = OS_KERNEL_IO_FIRST_DYNAMIC_DESCRIPTOR;
         descriptor_index < OS_KERNEL_IO_DESCRIPTOR_CAPACITY; ++descriptor_index) {
        IoDescriptorKind descriptor_kind = IoDescriptorKind::Closed;
        if (process.descriptors.Lookup(descriptor_index, descriptor_kind) !=
            IoDescriptorStatus::Succeeded) {
            continue;
        }
        if ((descriptor_kind == IoDescriptorKind::RegularFile ||
             descriptor_kind == IoDescriptorKind::Directory) &&
            process_file_system->Close(process.file_handles[descriptor_index]) !=
                FileSystemStatus::Succeeded) {
            HaltProcessor();
        }
        if (descriptor_kind == IoDescriptorKind::PipeReader) {
            const PipeStatus close_status = process_pipe.CloseReader();
            if (close_status != PipeStatus::Succeeded &&
                close_status != PipeStatus::AlreadyClosed) {
                HaltProcessor();
            }
            WakeRequiredProcesses(ProcessWaitReason::DescriptorWritable);
        }
        if (descriptor_kind == IoDescriptorKind::PipeWriter) {
            const PipeStatus close_status = process_pipe.CloseWriter();
            if (close_status != PipeStatus::Succeeded &&
                close_status != PipeStatus::AlreadyClosed) {
                HaltProcessor();
            }
            WakeRequiredProcesses(ProcessWaitReason::DescriptorReadable);
        }
        IoDescriptorKind closed_kind = IoDescriptorKind::Closed;
        if (process.descriptors.Close(descriptor_index, closed_kind) !=
            IoDescriptorStatus::Succeeded) {
            HaltProcessor();
        }
    }
}

[[nodiscard]] bool ReapTerminatedKernelStacks() noexcept {
    const uint64_t current_stack_pointer = ReadStackPointer();
    for (uint64_t process_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         process_index < OS_KERNEL_PROCESS_CAPACITY; ++process_index) {
        ProcessSchedulerEntry scheduler_entry{};
        if (process_scheduler.ReadEntry(process_index, scheduler_entry) !=
                ProcessSchedulerStatus::Succeeded ||
            scheduler_entry.state != ProcessState::Terminated) {
            continue;
        }
        KernelStack stack{};
        const KernelStackManagerStatus read_status =
            GetKernelStackManager().Read(process_index, stack);
        if (read_status == KernelStackManagerStatus::SlotNotActive) {
            continue;
        }
        if (read_status != KernelStackManagerStatus::Succeeded ||
            GetKernelStackManager().Contains(
                process_index, current_stack_pointer,
                OS_KERNEL_PROCESS_RUNTIME_STACK_POINTER_PROBE_SIZE_BYTES) ||
            GetKernelStackManager().TryDestroy(process_index) !=
                KernelStackManagerStatus::Succeeded) {
            return false;
        }
        process_control_blocks[process_index].saved_frame = nullptr;
    }
    return true;
}

[[nodiscard]] ExceptionFrame *
CompleteCurrentProcess(ExceptionFrame &frame, const ProcessTerminationReason termination_reason,
                       const int64_t exit_code, const uint64_t page_fault_address) noexcept {
    const uint64_t process_index = process_scheduler.CurrentProcessIndex();
    if (!process_scheduling_active || !CurrentFrameIsValid(process_index, frame)) {
        HaltProcessor();
    }

    ProcessControlBlock &process = process_control_blocks[process_index];
    process.saved_frame = &frame;
    process.result.termination_reason = termination_reason;
    process.result.exit_code = exit_code;
    if (termination_reason == ProcessTerminationReason::Exception) {
        process.result.exception_vector = frame.vector;
        process.result.exception_error_code = frame.error_code;
        process.result.exception_instruction_pointer = frame.instruction_pointer;
        process.result.page_fault_address = page_fault_address;
    }
    CloseProcessIoDescriptors(process);

    ProcessSchedulingDecision decision{};
    if (process_scheduler.TerminateCurrentProcess(decision) != ProcessSchedulerStatus::Succeeded) {
        HaltProcessor();
    }

    ActivateKernelPageTable();
    if (DestroyUserAddressSpace(process.address_space) != UserAddressSpaceStatus::Succeeded) {
        HaltProcessor();
    }

    if (decision.completed) {
        process_scheduling_active = false;
        if (!SetPrivilegeStackPointer0(DefaultPrivilegeStackPointer0())) {
            HaltProcessor();
        }
        OsKernelReturnFromUserMode();
    }
    if (!decision.switched) {
        if (!SetPrivilegeStackPointer0(DefaultPrivilegeStackPointer0())) {
            HaltProcessor();
        }
        OsKernelReturnFromUserMode();
    }
    if (!ActivateProcess(decision.current_process_index)) {
        HaltProcessor();
    }
    return process_control_blocks[decision.current_process_index].saved_frame;
}
}

ProcessRuntimeStatus InitializeProcessRuntime() noexcept {
    if (process_scheduling_active) {
        return ProcessRuntimeStatus::AlreadyActive;
    }
    if (process_scheduler.Initialize(OS_KERNEL_PROCESS_DEFAULT_QUANTUM_TICKS) !=
        ProcessSchedulerStatus::Succeeded) {
        return ProcessRuntimeStatus::SchedulerFailure;
    }
    ResetProcessControlBlocks();
    frames_before_processes = GetPhysicalFrameAllocatorStatistics();
    frames_after_processes = PhysicalFrameAllocatorStatistics{};
    virtual_addresses_before_processes = GetKernelVirtualAddressAllocator().Statistics();
    virtual_addresses_after_processes = KernelVirtualAddressAllocatorStatistics{};
    kernel_stacks_before_processes = GetKernelStackManager().Statistics();
    kernel_stacks_after_processes = KernelStackManagerStatistics{};
    resource_snapshot_before_processes = ResourceSnapshot{};
    resource_snapshot_after_processes = ResourceSnapshot{};
    resource_snapshot_difference = ResourceSnapshotDifference{};
    if (GetKernelStackManager().Validate() != KernelStackManagerStatus::Succeeded ||
        kernel_stacks_before_processes.active_stack_count !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        GetKernelResourceSnapshot(resource_snapshot_before_processes) !=
            ResourceSnapshotStatus::Succeeded) {
        return ProcessRuntimeStatus::KernelStackFailure;
    }
    process_pipe.Initialize();
    process_console_input.Initialize();
    pipe_reader_block_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    pipe_writer_block_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    pipe_end_of_file_observation_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    pipe_broken_observation_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    process_file_system = nullptr;
    process_runtime_initialized = true;
    return ProcessRuntimeStatus::Succeeded;
}

ProcessRuntimeStatus AttachProcessFileSystem(FileSystem &file_system) noexcept {
    if (!process_runtime_initialized) {
        return ProcessRuntimeStatus::NotInitialized;
    }
    if (process_scheduling_active) {
        return ProcessRuntimeStatus::AlreadyActive;
    }
    process_file_system = &file_system;
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

    uint64_t process_index = OS_KERNEL_PROCESS_INVALID_INDEX;
    uint64_t process_id = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if (process_scheduler.CreateProcess(process_index, process_id) !=
        ProcessSchedulerStatus::Succeeded) {
        return ProcessRuntimeStatus::SchedulerFailure;
    }
    if (GetKernelStackManager().TryCreate(process_index) != KernelStackManagerStatus::Succeeded) {
        if (process_scheduler.DiscardReadyProcess(process_index) !=
            ProcessSchedulerStatus::Succeeded) {
            HaltProcessor();
        }
        return ProcessRuntimeStatus::KernelStackFailure;
    }

    const UserProgramImage image = SelectUserProgramImage(selection);
    UserAddressSpace address_space{};
    address_space_status = LoadUserAddressSpace(image.image, image.image_size_bytes, address_space,
                                                elf_validation_status);
    if (address_space_status != UserAddressSpaceStatus::Succeeded) {
        if (GetKernelStackManager().TryDestroy(process_index) !=
                KernelStackManagerStatus::Succeeded ||
            process_scheduler.DiscardReadyProcess(process_index) !=
                ProcessSchedulerStatus::Succeeded) {
            HaltProcessor();
        }
        return address_space_status == UserAddressSpaceStatus::InvalidElf
                   ? ProcessRuntimeStatus::InvalidElf
                   : ProcessRuntimeStatus::AddressSpaceFailure;
    }

    ExceptionFrame *saved_frame = nullptr;
    if (!BuildInitialContextFrame(process_index, process_id, address_space, saved_frame)) {
        if (DestroyUserAddressSpace(address_space) != UserAddressSpaceStatus::Succeeded ||
            GetKernelStackManager().TryDestroy(process_index) !=
                KernelStackManagerStatus::Succeeded ||
            process_scheduler.DiscardReadyProcess(process_index) !=
                ProcessSchedulerStatus::Succeeded) {
            HaltProcessor();
        }
        return ProcessRuntimeStatus::ContextFrameFailure;
    }

    process_control_blocks[process_index] = ProcessControlBlock{
        .address_space = address_space,
        .saved_frame = saved_frame,
        .result =
            ProcessExecutionResult{
                .process_id = process_id,
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
            },
        .descriptors = {},
        .file_handles = {},
    };
    process_control_blocks[process_index].descriptors.Initialize(
        selection == UserProgramSelection::IpcConsumer,
        selection == UserProgramSelection::IpcProducer);
    KernelStack kernel_stack{};
    if (!ReadProcessKernelStack(process_index, kernel_stack)) {
        HaltProcessor();
    }
    creation_result = ProcessCreationResult{
        .process_id = process_id,
        .process_index = process_index,
        .root_physical_address = address_space.root_physical_address,
        .entry_virtual_address = address_space.entry_virtual_address,
        .mapped_page_count = address_space.mapped_page_count,
        .kernel_stack_lower_guard_address = KernelStackLowerGuardAddress(kernel_stack),
        .kernel_stack_top_address = KernelStackTopAddress(kernel_stack),
        .kernel_stack_upper_guard_address = KernelStackUpperGuardAddress(kernel_stack),
    };
    return ProcessRuntimeStatus::Succeeded;
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
        ProcessSchedulingDecision decision{};
        const ProcessSchedulerStatus scheduler_status = process_scheduler.Start(decision);
        if (scheduler_status == ProcessSchedulerStatus::NoReadyProcess) {
            EnableInterruptsWaitAndDisable();
            continue;
        }
        if (scheduler_status != ProcessSchedulerStatus::Succeeded) {
            process_scheduling_active = false;
            RestoreInterrupts(interrupts_were_enabled);
            return ProcessRuntimeStatus::SchedulerFailure;
        }
        if (!ActivateProcess(decision.current_process_index)) {
            process_scheduling_active = false;
            RestoreInterrupts(interrupts_were_enabled);
            return ProcessRuntimeStatus::PageTableActivationFailure;
        }
        OsKernelEnterScheduledProcess(
            process_control_blocks[decision.current_process_index].saved_frame);
        if (ReadPageTableRoot() != GetKernelPageTableRoot()) {
            HaltProcessor();
        }
        if (!ReapTerminatedKernelStacks()) {
            process_scheduling_active = false;
            RestoreInterrupts(interrupts_were_enabled);
            return ProcessRuntimeStatus::KernelStackFailure;
        }
    }
    frames_after_processes = GetPhysicalFrameAllocatorStatistics();
    virtual_addresses_after_processes = GetKernelVirtualAddressAllocator().Statistics();
    kernel_stacks_after_processes = GetKernelStackManager().Statistics();
    if (GetKernelStackManager().Validate() != KernelStackManagerStatus::Succeeded ||
        kernel_stacks_after_processes.active_stack_count != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        RestoreInterrupts(interrupts_were_enabled);
        return ProcessRuntimeStatus::KernelStackFailure;
    }
    if (GetKernelResourceSnapshot(resource_snapshot_after_processes) !=
            ResourceSnapshotStatus::Succeeded ||
        CompareResourceSnapshots(resource_snapshot_before_processes,
                                 resource_snapshot_after_processes,
                                 resource_snapshot_difference) !=
            ResourceSnapshotStatus::Succeeded ||
        resource_snapshot_difference.changed_fields_mask !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        resource_snapshot_difference.changed_field_count !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        RestoreInterrupts(interrupts_were_enabled);
        return ProcessRuntimeStatus::ResourceLeakDetected;
    }
    RestoreInterrupts(interrupts_were_enabled);
    return ProcessRuntimeStatus::Succeeded;
}

ProcessRuntimeStatistics GetProcessRuntimeStatistics() noexcept {
    ProcessRuntimeStatistics statistics{
        .scheduler = process_scheduler.Statistics(),
        .frames_before_processes = frames_before_processes,
        .frames_after_processes = frames_after_processes,
        .virtual_addresses_before_processes = virtual_addresses_before_processes,
        .virtual_addresses_after_processes = virtual_addresses_after_processes,
        .kernel_stacks_before_processes = kernel_stacks_before_processes,
        .kernel_stacks_after_processes = kernel_stacks_after_processes,
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
        .console_input = process_console_input.Statistics(),
        .processes = {},
    };
    for (uint64_t process_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         process_index < OS_KERNEL_PROCESS_CAPACITY; ++process_index) {
        statistics.processes[process_index] = process_control_blocks[process_index].result;
        ProcessSchedulerEntry scheduler_entry{};
        if (process_scheduler.ReadEntry(process_index, scheduler_entry) ==
            ProcessSchedulerStatus::Succeeded) {
            statistics.processes[process_index].run_tick_count = scheduler_entry.run_tick_count;
            statistics.processes[process_index].dispatch_count = scheduler_entry.dispatch_count;
        }
    }
    return statistics;
}

bool IsProcessSchedulingActive() noexcept {
    return process_scheduling_active && process_scheduler.IsActive();
}

uint64_t CurrentProcessId() noexcept {
    if (!IsProcessSchedulingActive()) {
        HaltProcessor();
    }
    ProcessSchedulerEntry entry{};
    if (process_scheduler.ReadEntry(process_scheduler.CurrentProcessIndex(), entry) !=
        ProcessSchedulerStatus::Succeeded) {
        HaltProcessor();
    }
    return entry.process_id;
}

UserProgramSelection CurrentProcessSelection() noexcept {
    if (!IsProcessSchedulingActive()) {
        HaltProcessor();
    }
    return process_control_blocks[process_scheduler.CurrentProcessIndex()].result.selection;
}

void RecordCurrentProcessSystemCall() noexcept {
    if (!IsProcessSchedulingActive()) {
        HaltProcessor();
    }
    ProcessControlBlock &process = process_control_blocks[process_scheduler.CurrentProcessIndex()];
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
    const PipeStatus status = process_pipe.TryRead(destination, capacity_bytes, read_bytes);
    if (status == PipeStatus::Succeeded) {
        process_control_blocks[process_scheduler.CurrentProcessIndex()].result.pipe_bytes_read +=
            read_bytes;
        WakeRequiredProcesses(ProcessWaitReason::DescriptorWritable);
    } else if (status == PipeStatus::EndOfFile) {
        ++pipe_end_of_file_observation_count;
    }
    return status;
}

PipeStatus TryWriteCurrentProcessPipe(const uint8_t *source, const uint64_t length_bytes,
                                      uint64_t &written_bytes) noexcept {
    if (!CurrentProcessCanWritePipe()) {
        written_bytes = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        return PipeStatus::InvalidArgument;
    }
    const PipeStatus status = process_pipe.TryWrite(source, length_bytes, written_bytes);
    if (status == PipeStatus::Succeeded) {
        process_control_blocks[process_scheduler.CurrentProcessIndex()].result.pipe_bytes_written +=
            written_bytes;
        WakeRequiredProcesses(ProcessWaitReason::DescriptorReadable);
    } else if (status == PipeStatus::BrokenPipe) {
        ++pipe_broken_observation_count;
    }
    return status;
}

PipeStatus CloseCurrentProcessPipeReader() noexcept {
    if (!CurrentProcessCanReadPipe()) {
        return PipeStatus::InvalidArgument;
    }
    const PipeStatus status = process_pipe.CloseReader();
    if (status == PipeStatus::Succeeded) {
        WakeRequiredProcesses(ProcessWaitReason::PipeWritable);
        WakeRequiredProcesses(ProcessWaitReason::DescriptorWritable);
    }
    return status;
}

PipeStatus CloseCurrentProcessPipeWriter() noexcept {
    if (!CurrentProcessCanWritePipe()) {
        return PipeStatus::InvalidArgument;
    }
    const PipeStatus status = process_pipe.CloseWriter();
    if (status == PipeStatus::Succeeded) {
        WakeRequiredProcesses(ProcessWaitReason::PipeReadable);
        WakeRequiredProcesses(ProcessWaitReason::DescriptorReadable);
    }
    return status;
}

FileSystemStatus OpenCurrentProcessFile(const uint8_t *path, const uint64_t path_length_bytes,
                                        const FileSystemOpenOptions &options,
                                        uint64_t &file_descriptor) noexcept {
    file_descriptor = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if (!IsProcessSchedulingActive() || process_file_system == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    ProcessControlBlock &process = process_control_blocks[process_scheduler.CurrentProcessIndex()];
    FileSystemHandle handle{};
    const FileSystemStatus status =
        process_file_system->Open(path, path_length_bytes, options, handle);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    uint64_t available_descriptor = OS_KERNEL_IO_DESCRIPTOR_CAPACITY;
    if (process.descriptors.Allocate(IoDescriptorKind::RegularFile, available_descriptor) !=
        IoDescriptorStatus::Succeeded) {
        if (process_file_system->Close(handle) != FileSystemStatus::Succeeded) {
            HaltProcessor();
        }
        return FileSystemStatus::DataCapacityExhausted;
    }
    process.file_handles[available_descriptor] = handle;
    file_descriptor = available_descriptor;
    return FileSystemStatus::Succeeded;
}

FileSystemStatus ReadCurrentProcessFile(const uint64_t file_descriptor, uint8_t *destination,
                                        const uint64_t capacity_bytes,
                                        uint64_t &read_bytes) noexcept {
    read_bytes = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if (!IsProcessSchedulingActive() || process_file_system == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    if (file_descriptor >= OS_KERNEL_IO_DESCRIPTOR_CAPACITY) {
        return FileSystemStatus::InvalidHandle;
    }
    ProcessControlBlock &process = process_control_blocks[process_scheduler.CurrentProcessIndex()];
    IoDescriptorKind descriptor_kind = IoDescriptorKind::Closed;
    if (process.descriptors.Lookup(file_descriptor, descriptor_kind) !=
            IoDescriptorStatus::Succeeded ||
        descriptor_kind != IoDescriptorKind::RegularFile) {
        return FileSystemStatus::InvalidHandle;
    }
    const FileSystemStatus status = process_file_system->Read(
        process.file_handles[file_descriptor], destination, capacity_bytes, read_bytes);
    if (status == FileSystemStatus::Succeeded) {
        process.result.file_system_bytes_read += read_bytes;
    }
    return status;
}

FileSystemStatus WriteCurrentProcessFile(const uint64_t file_descriptor, const uint8_t *source,
                                         const uint64_t length_bytes,
                                         uint64_t &written_bytes) noexcept {
    written_bytes = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if (!IsProcessSchedulingActive() || process_file_system == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    if (file_descriptor >= OS_KERNEL_IO_DESCRIPTOR_CAPACITY) {
        return FileSystemStatus::InvalidHandle;
    }
    ProcessControlBlock &process = process_control_blocks[process_scheduler.CurrentProcessIndex()];
    IoDescriptorKind descriptor_kind = IoDescriptorKind::Closed;
    if (process.descriptors.Lookup(file_descriptor, descriptor_kind) !=
            IoDescriptorStatus::Succeeded ||
        descriptor_kind != IoDescriptorKind::RegularFile) {
        return FileSystemStatus::InvalidHandle;
    }
    const FileSystemStatus status = process_file_system->Write(
        process.file_handles[file_descriptor], source, length_bytes, written_bytes);
    if (status == FileSystemStatus::Succeeded) {
        process.result.file_system_bytes_written += written_bytes;
    }
    return status;
}

FileSystemStatus CloseCurrentProcessFile(const uint64_t file_descriptor) noexcept {
    if (!IsProcessSchedulingActive() || process_file_system == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    if (file_descriptor >= OS_KERNEL_IO_DESCRIPTOR_CAPACITY) {
        return FileSystemStatus::InvalidHandle;
    }
    ProcessControlBlock &process = process_control_blocks[process_scheduler.CurrentProcessIndex()];
    IoDescriptorKind descriptor_kind = IoDescriptorKind::Closed;
    if (process.descriptors.Lookup(file_descriptor, descriptor_kind) !=
            IoDescriptorStatus::Succeeded ||
        descriptor_kind != IoDescriptorKind::RegularFile) {
        return FileSystemStatus::InvalidHandle;
    }
    const FileSystemStatus close_status =
        process_file_system->Close(process.file_handles[file_descriptor]);
    if (close_status != FileSystemStatus::Succeeded) {
        return close_status;
    }
    IoDescriptorKind closed_kind = IoDescriptorKind::Closed;
    return process.descriptors.Close(file_descriptor, closed_kind) == IoDescriptorStatus::Succeeded
               ? FileSystemStatus::Succeeded
               : FileSystemStatus::InvalidHandle;
}

FileSystemStatus CreateCurrentProcessDirectory(const uint8_t *path,
                                               const uint64_t path_length_bytes) noexcept {
    if (!IsProcessSchedulingActive() || process_file_system == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    return process_file_system->CreateDirectory(path, path_length_bytes);
}

FileSystemStatus SyncCurrentProcessFileSystem() noexcept {
    if (!IsProcessSchedulingActive() || process_file_system == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    return process_file_system->Sync();
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
    ProcessControlBlock &process = process_control_blocks[process_scheduler.CurrentProcessIndex()];
    IoDescriptorKind descriptor_kind = IoDescriptorKind::Closed;
    if (process.descriptors.Lookup(descriptor, descriptor_kind) != IoDescriptorStatus::Succeeded) {
        return ProcessIoStatus::InvalidDescriptor;
    }
    if (descriptor_kind == IoDescriptorKind::ConsoleInput) {
        const ConsoleInputStatus status =
            process_console_input.TryRead(destination, capacity_bytes, read_bytes);
        if (status == ConsoleInputStatus::Empty) {
            return ProcessIoStatus::WouldBlock;
        }
        if (status != ConsoleInputStatus::Succeeded) {
            return ProcessIoStatus::InvalidArgument;
        }
        process.result.console_bytes_read += read_bytes;
        return ProcessIoStatus::Succeeded;
    }
    if (descriptor_kind == IoDescriptorKind::RegularFile) {
        file_system_status = process_file_system->Read(process.file_handles[descriptor],
                                                       destination, capacity_bytes, read_bytes);
        if (file_system_status != FileSystemStatus::Succeeded) {
            return ProcessIoStatus::FileSystemFailure;
        }
        process.result.file_system_bytes_read += read_bytes;
        return ProcessIoStatus::Succeeded;
    }
    if (descriptor_kind == IoDescriptorKind::PipeReader) {
        const PipeStatus status = process_pipe.TryRead(destination, capacity_bytes, read_bytes);
        if (status == PipeStatus::Succeeded) {
            process.result.pipe_bytes_read += read_bytes;
            WakeRequiredProcesses(ProcessWaitReason::DescriptorWritable);
        } else if (status == PipeStatus::EndOfFile) {
            ++pipe_end_of_file_observation_count;
        }
        return MapPipeIoStatus(status);
    }
    return ProcessIoStatus::PermissionDenied;
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
    ProcessControlBlock &process = process_control_blocks[process_scheduler.CurrentProcessIndex()];
    IoDescriptorKind descriptor_kind = IoDescriptorKind::Closed;
    if (process.descriptors.Lookup(descriptor, descriptor_kind) != IoDescriptorStatus::Succeeded) {
        return ProcessIoStatus::InvalidDescriptor;
    }
    if (descriptor_kind == IoDescriptorKind::ConsoleOutput ||
        descriptor_kind == IoDescriptorKind::ConsoleError) {
        const SerialPort serial_port{OS_KERNEL_SERIAL_COM1_BASE_PORT};
        while (written_bytes < length_bytes) {
            if (!serial_port.TryWriteByte(static_cast<char>(source[written_bytes]))) {
                return ProcessIoStatus::DeviceFailure;
            }
            ++written_bytes;
        }
        process.result.console_bytes_written += written_bytes;
        return ProcessIoStatus::Succeeded;
    }
    if (descriptor_kind == IoDescriptorKind::RegularFile) {
        file_system_status = process_file_system->Write(process.file_handles[descriptor], source,
                                                        length_bytes, written_bytes);
        if (file_system_status != FileSystemStatus::Succeeded) {
            return ProcessIoStatus::FileSystemFailure;
        }
        process.result.file_system_bytes_written += written_bytes;
        return ProcessIoStatus::Succeeded;
    }
    if (descriptor_kind == IoDescriptorKind::PipeWriter) {
        const PipeStatus status = process_pipe.TryWrite(source, length_bytes, written_bytes);
        if (status == PipeStatus::Succeeded) {
            process.result.pipe_bytes_written += written_bytes;
            WakeRequiredProcesses(ProcessWaitReason::DescriptorReadable);
        } else if (status == PipeStatus::BrokenPipe) {
            ++pipe_broken_observation_count;
        }
        return MapPipeIoStatus(status);
    }
    return ProcessIoStatus::PermissionDenied;
}

ProcessIoStatus CloseCurrentProcessDescriptor(const uint64_t descriptor,
                                              FileSystemStatus &file_system_status) noexcept {
    file_system_status = FileSystemStatus::Succeeded;
    if (!IsProcessSchedulingActive()) {
        return ProcessIoStatus::InvalidArgument;
    }
    ProcessControlBlock &process = process_control_blocks[process_scheduler.CurrentProcessIndex()];
    IoDescriptorKind descriptor_kind = IoDescriptorKind::Closed;
    const IoDescriptorStatus lookup_status =
        process.descriptors.Lookup(descriptor, descriptor_kind);
    if (lookup_status != IoDescriptorStatus::Succeeded) {
        return ProcessIoStatus::InvalidDescriptor;
    }
    if (descriptor < OS_KERNEL_IO_FIRST_DYNAMIC_DESCRIPTOR) {
        return ProcessIoStatus::PermissionDenied;
    }
    if (descriptor_kind == IoDescriptorKind::RegularFile ||
        descriptor_kind == IoDescriptorKind::Directory) {
        file_system_status = process_file_system->Close(process.file_handles[descriptor]);
        if (file_system_status != FileSystemStatus::Succeeded) {
            return ProcessIoStatus::FileSystemFailure;
        }
    } else if (descriptor_kind == IoDescriptorKind::PipeReader) {
        const PipeStatus status = process_pipe.CloseReader();
        if (status != PipeStatus::Succeeded) {
            return MapPipeIoStatus(status);
        }
        WakeRequiredProcesses(ProcessWaitReason::DescriptorWritable);
    } else if (descriptor_kind == IoDescriptorKind::PipeWriter) {
        const PipeStatus status = process_pipe.CloseWriter();
        if (status != PipeStatus::Succeeded) {
            return MapPipeIoStatus(status);
        }
        WakeRequiredProcesses(ProcessWaitReason::DescriptorReadable);
    } else {
        return ProcessIoStatus::PermissionDenied;
    }
    IoDescriptorKind closed_kind = IoDescriptorKind::Closed;
    if (process.descriptors.Close(descriptor, closed_kind) != IoDescriptorStatus::Succeeded) {
        HaltProcessor();
    }
    return ProcessIoStatus::Succeeded;
}

FileSystemStatus OpenCurrentProcessDirectory(const uint8_t *const path,
                                             const uint64_t path_length_bytes,
                                             uint64_t &file_descriptor) noexcept {
    file_descriptor = OS_KERNEL_IO_DESCRIPTOR_CAPACITY;
    if (!IsProcessSchedulingActive() || process_file_system == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    FileSystemHandle handle{};
    FileSystemStatus status = process_file_system->OpenDirectory(path, path_length_bytes, handle);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    ProcessControlBlock &process = process_control_blocks[process_scheduler.CurrentProcessIndex()];
    if (process.descriptors.Allocate(IoDescriptorKind::Directory, file_descriptor) !=
        IoDescriptorStatus::Succeeded) {
        if (process_file_system->Close(handle) != FileSystemStatus::Succeeded) {
            HaltProcessor();
        }
        return FileSystemStatus::DataCapacityExhausted;
    }
    process.file_handles[file_descriptor] = handle;
    return FileSystemStatus::Succeeded;
}

FileSystemStatus ReadCurrentProcessDirectory(const uint64_t file_descriptor,
                                             FileSystemDirectoryEntry &entry,
                                             bool &end_of_directory) noexcept {
    entry = FileSystemDirectoryEntry{};
    end_of_directory = false;
    if (!IsProcessSchedulingActive() || process_file_system == nullptr ||
        file_descriptor >= OS_KERNEL_IO_DESCRIPTOR_CAPACITY) {
        return FileSystemStatus::InvalidHandle;
    }
    ProcessControlBlock &process = process_control_blocks[process_scheduler.CurrentProcessIndex()];
    IoDescriptorKind descriptor_kind = IoDescriptorKind::Closed;
    if (process.descriptors.Lookup(file_descriptor, descriptor_kind) !=
            IoDescriptorStatus::Succeeded ||
        descriptor_kind != IoDescriptorKind::Directory) {
        return FileSystemStatus::InvalidHandle;
    }
    return process_file_system->ReadDirectory(process.file_handles[file_descriptor], entry,
                                              end_of_directory);
}

ProcessIoStatus CurrentProcessDescriptorReadCanProgress(const uint64_t descriptor,
                                                        bool &can_progress) noexcept {
    can_progress = false;
    if (!IsProcessSchedulingActive()) {
        return ProcessIoStatus::InvalidArgument;
    }
    ProcessControlBlock &process = process_control_blocks[process_scheduler.CurrentProcessIndex()];
    IoDescriptorKind descriptor_kind = IoDescriptorKind::Closed;
    if (process.descriptors.Lookup(descriptor, descriptor_kind) != IoDescriptorStatus::Succeeded) {
        return ProcessIoStatus::InvalidDescriptor;
    }
    if (descriptor_kind == IoDescriptorKind::ConsoleInput) {
        can_progress = process_console_input.ReadCanProgress();
        return ProcessIoStatus::Succeeded;
    }
    if (descriptor_kind == IoDescriptorKind::RegularFile ||
        descriptor_kind == IoDescriptorKind::Directory) {
        can_progress = true;
        return ProcessIoStatus::Succeeded;
    }
    if (descriptor_kind == IoDescriptorKind::PipeReader) {
        can_progress = process_pipe.ReadCanProgress();
        if (!can_progress) {
            ++pipe_reader_block_count;
        }
        return ProcessIoStatus::Succeeded;
    }
    return ProcessIoStatus::PermissionDenied;
}

ProcessIoStatus CurrentProcessDescriptorWriteCanProgress(const uint64_t descriptor,
                                                         bool &can_progress) noexcept {
    can_progress = false;
    if (!IsProcessSchedulingActive()) {
        return ProcessIoStatus::InvalidArgument;
    }
    ProcessControlBlock &process = process_control_blocks[process_scheduler.CurrentProcessIndex()];
    IoDescriptorKind descriptor_kind = IoDescriptorKind::Closed;
    if (process.descriptors.Lookup(descriptor, descriptor_kind) != IoDescriptorStatus::Succeeded) {
        return ProcessIoStatus::InvalidDescriptor;
    }
    if (descriptor_kind == IoDescriptorKind::ConsoleOutput ||
        descriptor_kind == IoDescriptorKind::ConsoleError ||
        descriptor_kind == IoDescriptorKind::RegularFile) {
        can_progress = true;
        return ProcessIoStatus::Succeeded;
    }
    if (descriptor_kind == IoDescriptorKind::PipeWriter) {
        can_progress = process_pipe.WriteCanProgress();
        if (!can_progress) {
            ++pipe_writer_block_count;
        }
        return ProcessIoStatus::Succeeded;
    }
    return ProcessIoStatus::PermissionDenied;
}

void SubmitConsoleCharacter(const uint8_t character) noexcept {
    const ConsoleInputStatus submit_status = process_console_input.Submit(character);
    if (submit_status == ConsoleInputStatus::Succeeded && process_scheduling_active) {
        WakeRequiredProcesses(ProcessWaitReason::DescriptorReadable);
    }
}

bool ProcessPipeReadCanProgress() noexcept { return process_pipe.ReadCanProgress(); }

bool ProcessPipeWriteCanProgress() noexcept { return process_pipe.WriteCanProgress(); }

ProcessRuntimeStatus BlockCurrentProcess(ExceptionFrame &frame, const ProcessWaitReason wait_reason,
                                         ExceptionFrame *&resume_frame) noexcept {
    resume_frame = &frame;
    if (!IsProcessSchedulingActive() ||
        !CurrentFrameIsValid(process_scheduler.CurrentProcessIndex(), frame)) {
        return ProcessRuntimeStatus::SchedulerFailure;
    }
    const uint64_t process_index = process_scheduler.CurrentProcessIndex();
    process_control_blocks[process_index].saved_frame = &frame;

    ProcessSchedulingDecision decision{};
    const ProcessSchedulerStatus status =
        process_scheduler.BlockCurrentProcess(wait_reason, decision);
    if (status == ProcessSchedulerStatus::NoReadyProcess) {
        return ProcessRuntimeStatus::NoReadyProcess;
    }
    if (status != ProcessSchedulerStatus::Succeeded) {
        return ProcessRuntimeStatus::SchedulerFailure;
    }
    if (wait_reason == ProcessWaitReason::PipeReadable) {
        ++pipe_reader_block_count;
    } else if (wait_reason == ProcessWaitReason::PipeWritable) {
        ++pipe_writer_block_count;
    }
    if (!decision.switched) {
        ActivateKernelPageTable();
        if (!SetPrivilegeStackPointer0(DefaultPrivilegeStackPointer0())) {
            HaltProcessor();
        }
        OsKernelReturnFromUserMode();
    }
    if (!ActivateProcess(decision.current_process_index)) {
        return ProcessRuntimeStatus::SchedulerFailure;
    }
    resume_frame = process_control_blocks[decision.current_process_index].saved_frame;
    return ProcessRuntimeStatus::Succeeded;
}

ProcessRuntimeStatus WakeProcesses(const ProcessWaitReason wait_reason,
                                   const uint64_t maximum_wake_count,
                                   uint64_t &woken_process_count) noexcept {
    if (!IsProcessSchedulingActive()) {
        return ProcessRuntimeStatus::SchedulerFailure;
    }
    return process_scheduler.WakeBlockedProcesses(wait_reason, maximum_wake_count,
                                                  woken_process_count) ==
                   ProcessSchedulerStatus::Succeeded
               ? ProcessRuntimeStatus::Succeeded
               : ProcessRuntimeStatus::SchedulerFailure;
}

ExceptionFrame *HandleProcessTimerInterrupt(ExceptionFrame &frame) noexcept {
    if (!process_scheduling_active || !FrameOriginatedFromUser(frame)) {
        return &frame;
    }
    const uint64_t process_index = process_scheduler.CurrentProcessIndex();
    if (!CurrentFrameIsValid(process_index, frame)) {
        HaltProcessor();
    }
    process_control_blocks[process_index].saved_frame = &frame;

    ProcessSchedulingDecision decision{};
    if (process_scheduler.HandleTimerTick(decision) != ProcessSchedulerStatus::Succeeded) {
        HaltProcessor();
    }
    if (!decision.switched) {
        return &frame;
    }
    if (!ActivateProcess(decision.current_process_index)) {
        HaltProcessor();
    }
    return process_control_blocks[decision.current_process_index].saved_frame;
}

ExceptionFrame *TerminateCurrentProcessFromExit(ExceptionFrame &frame,
                                                const int64_t exit_code) noexcept {
    return CompleteCurrentProcess(frame, ProcessTerminationReason::Exited, exit_code,
                                  OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE);
}

ExceptionFrame *TerminateCurrentProcessFromException(ExceptionFrame &frame,
                                                     const uint64_t page_fault_address) noexcept {
    return CompleteCurrentProcess(frame, ProcessTerminationReason::Exception,
                                  OS_KERNEL_PROCESS_RUNTIME_SUCCESS_EXIT_CODE, page_fault_address);
}
}

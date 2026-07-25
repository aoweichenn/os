#include "os/kernel/process_runtime.hpp"

#include "os/kernel/descriptor_tables.hpp"
#include "os/kernel/memory_manager.hpp"
#include "os/kernel/process_memory_layout.hpp"
#include "os/kernel/processor.hpp"
#include "os/kernel/serial_port.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_INITIAL_USER_FLAGS = 0x202ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX = 0ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_WAKE_ALL_COUNT = OS_KERNEL_PROCESS_CAPACITY;
constexpr int64_t OS_KERNEL_PROCESS_RUNTIME_SUCCESS_EXIT_CODE = 0LL;

[[nodiscard]] ProcessIoStatus MapPipeIoStatus(
    const PipeStatus status) noexcept {
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
    UserAddressSpace addressSpace;
    ExceptionFrame *savedFrame;
    ProcessExecutionResult result;
    IoDescriptorTable descriptors;
    FileSystemHandle fileHandles[OS_KERNEL_IO_DESCRIPTOR_CAPACITY];
};

ProcessScheduler processScheduler;
Pipe processPipe;
ConsoleInput processConsoleInput;
ProcessControlBlock processControlBlocks[OS_KERNEL_PROCESS_CAPACITY];
FileSystem *processFileSystem;
PhysicalFrameAllocatorStatistics framesBeforeProcesses;
PhysicalFrameAllocatorStatistics framesAfterProcesses;
uint64_t pipeReaderBlockCount;
uint64_t pipeWriterBlockCount;
uint64_t pipeEndOfFileObservationCount;
uint64_t pipeBrokenObservationCount;
bool processRuntimeInitialized;
bool processSchedulingActive;

extern "C" void osKernelEnterScheduledProcess(ExceptionFrame *frame) noexcept;
extern "C" [[noreturn]] void osKernelReturnFromUserMode() noexcept;

void ResetProcessControlBlocks() noexcept {
    for (uint64_t processIndex = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         processIndex < OS_KERNEL_PROCESS_CAPACITY; ++processIndex) {
        processControlBlocks[processIndex] = ProcessControlBlock{};
    }
}

[[nodiscard]] bool BuildInitialContextFrame(const uint64_t processIndex, const uint64_t processId,
                                            const UserAddressSpace &addressSpace,
                                            ExceptionFrame *&savedFrame) noexcept {
    const uint64_t stackTopAddress = ProcessKernelStackTopAddress(processIndex);
    if (stackTopAddress < OS_KERNEL_USER_PRIVILEGE_FRAME_SIZE_BYTES) {
        return false;
    }
    const uint64_t frameAddress = stackTopAddress - OS_KERNEL_USER_PRIVILEGE_FRAME_SIZE_BYTES;
    if (!ProcessKernelStackContains(processIndex, frameAddress,
                                    OS_KERNEL_USER_PRIVILEGE_FRAME_SIZE_BYTES)) {
        return false;
    }

    UserPrivilegeFrame *const frame = reinterpret_cast<UserPrivilegeFrame *>(frameAddress);
    *frame = UserPrivilegeFrame{};
    frame->common.registerRdi = processId;
    frame->common.vector = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    frame->common.errorCode = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    frame->common.instructionPointer = addressSpace.entryVirtualAddress;
    frame->common.codeSegment = static_cast<uint64_t>(OS_KERNEL_DESCRIPTOR_USER_CODE_SELECTOR);
    frame->common.flags = OS_KERNEL_PROCESS_RUNTIME_INITIAL_USER_FLAGS;
    frame->userStackPointer = addressSpace.stackTopVirtualAddress;
    frame->userStackSegment = static_cast<uint64_t>(OS_KERNEL_DESCRIPTOR_USER_DATA_SELECTOR);
    savedFrame = &frame->common;
    return true;
}

[[nodiscard]] bool CurrentFrameIsValid(const uint64_t processIndex,
                                       const ExceptionFrame &frame) noexcept {
    if (processIndex >= OS_KERNEL_PROCESS_CAPACITY || !FrameOriginatedFromUser(frame) ||
        !ProcessKernelStackContains(processIndex, reinterpret_cast<uint64_t>(&frame),
                                    OS_KERNEL_USER_PRIVILEGE_FRAME_SIZE_BYTES)) {
        return false;
    }
    const ProcessControlBlock &process = processControlBlocks[processIndex];
    return process.addressSpace.rootPhysicalAddress != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE &&
           ReadPageTableRoot() == process.addressSpace.rootPhysicalAddress;
}

[[nodiscard]] bool ActivateProcess(const uint64_t processIndex) noexcept {
    if (processIndex >= OS_KERNEL_PROCESS_CAPACITY) {
        return false;
    }
    ProcessControlBlock &process = processControlBlocks[processIndex];
    if (process.savedFrame == nullptr ||
        !ProcessKernelStackContains(processIndex, reinterpret_cast<uint64_t>(process.savedFrame),
                                    OS_KERNEL_USER_PRIVILEGE_FRAME_SIZE_BYTES) ||
        !ActivateUserPageTable(process.addressSpace.rootPhysicalAddress)) {
        return false;
    }
    if (!SetPrivilegeStackPointer0(ProcessKernelStackTopAddress(processIndex))) {
        ActivateKernelPageTable();
        return false;
    }
    return true;
}

void WakeRequiredProcesses(const ProcessWaitReason waitReason) noexcept {
    uint64_t wokenProcessCount = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if (processScheduler.WakeBlockedProcesses(waitReason, OS_KERNEL_PROCESS_RUNTIME_WAKE_ALL_COUNT,
                                              wokenProcessCount) !=
        ProcessSchedulerStatus::Succeeded) {
        HaltProcessor();
    }
}

void CloseProcessIoDescriptors(ProcessControlBlock &process) noexcept {
    if (processFileSystem == nullptr) {
        HaltProcessor();
    }
    for (uint64_t descriptorIndex = OS_KERNEL_IO_FIRST_DYNAMIC_DESCRIPTOR;
         descriptorIndex < OS_KERNEL_IO_DESCRIPTOR_CAPACITY;
         ++descriptorIndex) {
        IoDescriptorKind descriptorKind = IoDescriptorKind::Closed;
        if (process.descriptors.Lookup(descriptorIndex, descriptorKind) !=
            IoDescriptorStatus::Succeeded) {
            continue;
        }
        if ((descriptorKind == IoDescriptorKind::RegularFile ||
             descriptorKind == IoDescriptorKind::Directory) &&
            processFileSystem->Close(process.fileHandles[descriptorIndex]) !=
                FileSystemStatus::Succeeded) {
            HaltProcessor();
        }
        if (descriptorKind == IoDescriptorKind::PipeReader) {
            const PipeStatus closeStatus = processPipe.CloseReader();
            if (closeStatus != PipeStatus::Succeeded &&
                closeStatus != PipeStatus::AlreadyClosed) {
                HaltProcessor();
            }
            WakeRequiredProcesses(ProcessWaitReason::DescriptorWritable);
        }
        if (descriptorKind == IoDescriptorKind::PipeWriter) {
            const PipeStatus closeStatus = processPipe.CloseWriter();
            if (closeStatus != PipeStatus::Succeeded &&
                closeStatus != PipeStatus::AlreadyClosed) {
                HaltProcessor();
            }
            WakeRequiredProcesses(ProcessWaitReason::DescriptorReadable);
        }
        IoDescriptorKind closedKind = IoDescriptorKind::Closed;
        if (process.descriptors.Close(descriptorIndex, closedKind) !=
            IoDescriptorStatus::Succeeded) {
            HaltProcessor();
        }
    }
}

[[nodiscard]] ExceptionFrame *
CompleteCurrentProcess(ExceptionFrame &frame, const ProcessTerminationReason terminationReason,
                       const int64_t exitCode, const uint64_t pageFaultAddress) noexcept {
    const uint64_t processIndex = processScheduler.CurrentProcessIndex();
    if (!processSchedulingActive || !CurrentFrameIsValid(processIndex, frame)) {
        HaltProcessor();
    }

    ProcessControlBlock &process = processControlBlocks[processIndex];
    process.savedFrame = &frame;
    process.result.terminationReason = terminationReason;
    process.result.exitCode = exitCode;
    if (terminationReason == ProcessTerminationReason::Exception) {
        process.result.exceptionVector = frame.vector;
        process.result.exceptionErrorCode = frame.errorCode;
        process.result.exceptionInstructionPointer = frame.instructionPointer;
        process.result.pageFaultAddress = pageFaultAddress;
    }
    CloseProcessIoDescriptors(process);

    ProcessSchedulingDecision decision{};
    if (processScheduler.TerminateCurrentProcess(decision) != ProcessSchedulerStatus::Succeeded) {
        HaltProcessor();
    }

    ActivateKernelPageTable();
    if (DestroyUserAddressSpace(process.addressSpace) != UserAddressSpaceStatus::Succeeded) {
        HaltProcessor();
    }

    if (decision.completed) {
        processSchedulingActive = false;
        framesAfterProcesses = GetPhysicalFrameAllocatorStatistics();
        if (!SetPrivilegeStackPointer0(DefaultPrivilegeStackPointer0())) {
            HaltProcessor();
        }
        osKernelReturnFromUserMode();
    }
    if (!decision.switched) {
        if (!SetPrivilegeStackPointer0(DefaultPrivilegeStackPointer0())) {
            HaltProcessor();
        }
        osKernelReturnFromUserMode();
    }
    if (!ActivateProcess(decision.currentProcessIndex)) {
        HaltProcessor();
    }
    return processControlBlocks[decision.currentProcessIndex].savedFrame;
}

}

ProcessRuntimeStatus InitializeProcessRuntime() noexcept {
    if (processSchedulingActive) {
        return ProcessRuntimeStatus::AlreadyActive;
    }
    if (processScheduler.Initialize(OS_KERNEL_PROCESS_DEFAULT_QUANTUM_TICKS) !=
        ProcessSchedulerStatus::Succeeded) {
        return ProcessRuntimeStatus::SchedulerFailure;
    }
    ResetProcessControlBlocks();
    framesBeforeProcesses = GetPhysicalFrameAllocatorStatistics();
    framesAfterProcesses = PhysicalFrameAllocatorStatistics{};
    processPipe.Initialize();
    processConsoleInput.Initialize();
    pipeReaderBlockCount = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    pipeWriterBlockCount = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    pipeEndOfFileObservationCount = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    pipeBrokenObservationCount = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    processFileSystem = nullptr;
    processRuntimeInitialized = true;
    return ProcessRuntimeStatus::Succeeded;
}

ProcessRuntimeStatus AttachProcessFileSystem(FileSystem &fileSystem) noexcept {
    if (!processRuntimeInitialized) {
        return ProcessRuntimeStatus::NotInitialized;
    }
    if (processSchedulingActive) {
        return ProcessRuntimeStatus::AlreadyActive;
    }
    processFileSystem = &fileSystem;
    return ProcessRuntimeStatus::Succeeded;
}

ProcessRuntimeStatus CreateProcess(const UserProgramSelection selection,
                                   ProcessCreationResult &creationResult,
                                   UserElfValidationStatus &elfValidationStatus,
                                   UserAddressSpaceStatus &addressSpaceStatus) noexcept {
    if (!processRuntimeInitialized) {
        return ProcessRuntimeStatus::NotInitialized;
    }
    if (processSchedulingActive) {
        return ProcessRuntimeStatus::AlreadyActive;
    }

    uint64_t processIndex = OS_KERNEL_PROCESS_INVALID_INDEX;
    uint64_t processId = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if (processScheduler.CreateProcess(processIndex, processId) !=
        ProcessSchedulerStatus::Succeeded) {
        return ProcessRuntimeStatus::SchedulerFailure;
    }

    const UserProgramImage image = SelectUserProgramImage(selection);
    UserAddressSpace addressSpace{};
    addressSpaceStatus =
        LoadUserAddressSpace(image.image, image.imageSizeBytes, addressSpace, elfValidationStatus);
    if (addressSpaceStatus != UserAddressSpaceStatus::Succeeded) {
        if (processScheduler.DiscardReadyProcess(processIndex) !=
            ProcessSchedulerStatus::Succeeded) {
            HaltProcessor();
        }
        return addressSpaceStatus == UserAddressSpaceStatus::InvalidElf
                   ? ProcessRuntimeStatus::InvalidElf
                   : ProcessRuntimeStatus::AddressSpaceFailure;
    }

    ExceptionFrame *savedFrame = nullptr;
    if (!BuildInitialContextFrame(processIndex, processId, addressSpace, savedFrame)) {
        if (DestroyUserAddressSpace(addressSpace) != UserAddressSpaceStatus::Succeeded ||
            processScheduler.DiscardReadyProcess(processIndex) !=
                ProcessSchedulerStatus::Succeeded) {
            HaltProcessor();
        }
        return ProcessRuntimeStatus::ContextFrameFailure;
    }

    processControlBlocks[processIndex] = ProcessControlBlock{
        .addressSpace = addressSpace,
        .savedFrame = savedFrame,
        .result =
            ProcessExecutionResult{
                .processId = processId,
                .selection = selection,
                .terminationReason = ProcessTerminationReason::None,
                .exitCode = OS_KERNEL_PROCESS_RUNTIME_SUCCESS_EXIT_CODE,
                .exceptionVector = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
                .exceptionErrorCode = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
                .exceptionInstructionPointer = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
                .pageFaultAddress = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
                .systemCallCount = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
                .rootPhysicalAddress = addressSpace.rootPhysicalAddress,
                .mappedPageCount = addressSpace.mappedPageCount,
                .runTickCount = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
                .dispatchCount = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
                .pipeBytesRead = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
                .pipeBytesWritten = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
                .fileSystemBytesRead = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
                .fileSystemBytesWritten = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
                .consoleBytesRead = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
                .consoleBytesWritten = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
            },
        .descriptors = {},
        .fileHandles = {},
    };
    processControlBlocks[processIndex].descriptors.Initialize(
        selection == UserProgramSelection::IpcConsumer,
        selection == UserProgramSelection::IpcProducer);
    creationResult = ProcessCreationResult{
        .processId = processId,
        .processIndex = processIndex,
        .rootPhysicalAddress = addressSpace.rootPhysicalAddress,
        .entryVirtualAddress = addressSpace.entryVirtualAddress,
        .mappedPageCount = addressSpace.mappedPageCount,
    };
    return ProcessRuntimeStatus::Succeeded;
}

ProcessRuntimeStatus ExecuteProcesses() noexcept {
    if (!processRuntimeInitialized) {
        return ProcessRuntimeStatus::NotInitialized;
    }
    if (processSchedulingActive) {
        return ProcessRuntimeStatus::AlreadyActive;
    }

    const bool interruptsWereEnabled = DisableInterrupts();
    processSchedulingActive = true;
    while (processSchedulingActive) {
        ProcessSchedulingDecision decision{};
        const ProcessSchedulerStatus schedulerStatus =
            processScheduler.Start(decision);
        if (schedulerStatus == ProcessSchedulerStatus::NoReadyProcess) {
            EnableInterruptsWaitAndDisable();
            continue;
        }
        if (schedulerStatus != ProcessSchedulerStatus::Succeeded) {
            processSchedulingActive = false;
            RestoreInterrupts(interruptsWereEnabled);
            return ProcessRuntimeStatus::SchedulerFailure;
        }
        if (!ActivateProcess(decision.currentProcessIndex)) {
            processSchedulingActive = false;
            RestoreInterrupts(interruptsWereEnabled);
            return ProcessRuntimeStatus::PageTableActivationFailure;
        }
        osKernelEnterScheduledProcess(
            processControlBlocks[decision.currentProcessIndex].savedFrame);
        if (ReadPageTableRoot() != GetKernelPageTableRoot()) {
            HaltProcessor();
        }
    }
    RestoreInterrupts(interruptsWereEnabled);
    return ProcessRuntimeStatus::Succeeded;
}

ProcessRuntimeStatistics GetProcessRuntimeStatistics() noexcept {
    ProcessRuntimeStatistics statistics{
        .scheduler = processScheduler.Statistics(),
        .framesBeforeProcesses = framesBeforeProcesses,
        .framesAfterProcesses = framesAfterProcesses,
        .ipc =
            ProcessIpcStatistics{
                .pipe = processPipe.Statistics(),
                .readerBlockCount = pipeReaderBlockCount,
                .writerBlockCount = pipeWriterBlockCount,
                .endOfFileObservationCount = pipeEndOfFileObservationCount,
                .brokenPipeObservationCount = pipeBrokenObservationCount,
            },
        .consoleInput = processConsoleInput.Statistics(),
        .processes = {},
    };
    for (uint64_t processIndex = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         processIndex < OS_KERNEL_PROCESS_CAPACITY; ++processIndex) {
        statistics.processes[processIndex] = processControlBlocks[processIndex].result;
        ProcessSchedulerEntry schedulerEntry{};
        if (processScheduler.ReadEntry(processIndex, schedulerEntry) ==
            ProcessSchedulerStatus::Succeeded) {
            statistics.processes[processIndex].runTickCount = schedulerEntry.runTickCount;
            statistics.processes[processIndex].dispatchCount = schedulerEntry.dispatchCount;
        }
    }
    return statistics;
}

bool IsProcessSchedulingActive() noexcept {
    return processSchedulingActive && processScheduler.IsActive();
}

uint64_t CurrentProcessId() noexcept {
    if (!IsProcessSchedulingActive()) {
        HaltProcessor();
    }
    ProcessSchedulerEntry entry{};
    if (processScheduler.ReadEntry(processScheduler.CurrentProcessIndex(), entry) !=
        ProcessSchedulerStatus::Succeeded) {
        HaltProcessor();
    }
    return entry.processId;
}

UserProgramSelection CurrentProcessSelection() noexcept {
    if (!IsProcessSchedulingActive()) {
        HaltProcessor();
    }
    return processControlBlocks[processScheduler.CurrentProcessIndex()].result.selection;
}

void RecordCurrentProcessSystemCall() noexcept {
    if (!IsProcessSchedulingActive()) {
        HaltProcessor();
    }
    ProcessControlBlock &process = processControlBlocks[processScheduler.CurrentProcessIndex()];
    ++process.result.systemCallCount;
}

bool CurrentProcessCanReadPipe() noexcept {
    return CurrentProcessSelection() == UserProgramSelection::IpcConsumer;
}

bool CurrentProcessCanWritePipe() noexcept {
    return CurrentProcessSelection() == UserProgramSelection::IpcProducer;
}

PipeStatus TryReadCurrentProcessPipe(uint8_t *destination, const uint64_t capacityBytes,
                                     uint64_t &readBytes) noexcept {
    if (!CurrentProcessCanReadPipe()) {
        readBytes = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        return PipeStatus::InvalidArgument;
    }
    const PipeStatus status = processPipe.TryRead(destination, capacityBytes, readBytes);
    if (status == PipeStatus::Succeeded) {
        processControlBlocks[processScheduler.CurrentProcessIndex()].result.pipeBytesRead +=
            readBytes;
        WakeRequiredProcesses(ProcessWaitReason::DescriptorWritable);
    } else if (status == PipeStatus::EndOfFile) {
        ++pipeEndOfFileObservationCount;
    }
    return status;
}

PipeStatus TryWriteCurrentProcessPipe(const uint8_t *source, const uint64_t lengthBytes,
                                      uint64_t &writtenBytes) noexcept {
    if (!CurrentProcessCanWritePipe()) {
        writtenBytes = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        return PipeStatus::InvalidArgument;
    }
    const PipeStatus status = processPipe.TryWrite(source, lengthBytes, writtenBytes);
    if (status == PipeStatus::Succeeded) {
        processControlBlocks[processScheduler.CurrentProcessIndex()].result.pipeBytesWritten +=
            writtenBytes;
        WakeRequiredProcesses(ProcessWaitReason::DescriptorReadable);
    } else if (status == PipeStatus::BrokenPipe) {
        ++pipeBrokenObservationCount;
    }
    return status;
}

PipeStatus CloseCurrentProcessPipeReader() noexcept {
    if (!CurrentProcessCanReadPipe()) {
        return PipeStatus::InvalidArgument;
    }
    const PipeStatus status = processPipe.CloseReader();
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
    const PipeStatus status = processPipe.CloseWriter();
    if (status == PipeStatus::Succeeded) {
        WakeRequiredProcesses(ProcessWaitReason::PipeReadable);
        WakeRequiredProcesses(ProcessWaitReason::DescriptorReadable);
    }
    return status;
}

FileSystemStatus OpenCurrentProcessFile(
    const uint8_t *path, const uint64_t pathLengthBytes,
    const FileSystemOpenOptions &options, uint64_t &fileDescriptor) noexcept {
    fileDescriptor = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if (!IsProcessSchedulingActive() || processFileSystem == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    ProcessControlBlock &process =
        processControlBlocks[processScheduler.CurrentProcessIndex()];
    FileSystemHandle handle{};
    const FileSystemStatus status =
        processFileSystem->Open(path, pathLengthBytes, options, handle);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    uint64_t availableDescriptor = OS_KERNEL_IO_DESCRIPTOR_CAPACITY;
    if (process.descriptors.Allocate(IoDescriptorKind::RegularFile,
                                     availableDescriptor) !=
        IoDescriptorStatus::Succeeded) {
        if (processFileSystem->Close(handle) != FileSystemStatus::Succeeded) {
            HaltProcessor();
        }
        return FileSystemStatus::DataCapacityExhausted;
    }
    process.fileHandles[availableDescriptor] = handle;
    fileDescriptor = availableDescriptor;
    return FileSystemStatus::Succeeded;
}

FileSystemStatus ReadCurrentProcessFile(
    const uint64_t fileDescriptor, uint8_t *destination,
    const uint64_t capacityBytes, uint64_t &readBytes) noexcept {
    readBytes = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if (!IsProcessSchedulingActive() || processFileSystem == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    if (fileDescriptor >= OS_KERNEL_IO_DESCRIPTOR_CAPACITY) {
        return FileSystemStatus::InvalidHandle;
    }
    ProcessControlBlock &process =
        processControlBlocks[processScheduler.CurrentProcessIndex()];
    IoDescriptorKind descriptorKind = IoDescriptorKind::Closed;
    if (process.descriptors.Lookup(fileDescriptor, descriptorKind) !=
            IoDescriptorStatus::Succeeded ||
        descriptorKind != IoDescriptorKind::RegularFile) {
        return FileSystemStatus::InvalidHandle;
    }
    const FileSystemStatus status = processFileSystem->Read(
        process.fileHandles[fileDescriptor], destination, capacityBytes,
        readBytes);
    if (status == FileSystemStatus::Succeeded) {
        process.result.fileSystemBytesRead += readBytes;
    }
    return status;
}

FileSystemStatus WriteCurrentProcessFile(
    const uint64_t fileDescriptor, const uint8_t *source,
    const uint64_t lengthBytes, uint64_t &writtenBytes) noexcept {
    writtenBytes = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if (!IsProcessSchedulingActive() || processFileSystem == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    if (fileDescriptor >= OS_KERNEL_IO_DESCRIPTOR_CAPACITY) {
        return FileSystemStatus::InvalidHandle;
    }
    ProcessControlBlock &process =
        processControlBlocks[processScheduler.CurrentProcessIndex()];
    IoDescriptorKind descriptorKind = IoDescriptorKind::Closed;
    if (process.descriptors.Lookup(fileDescriptor, descriptorKind) !=
            IoDescriptorStatus::Succeeded ||
        descriptorKind != IoDescriptorKind::RegularFile) {
        return FileSystemStatus::InvalidHandle;
    }
    const FileSystemStatus status = processFileSystem->Write(
        process.fileHandles[fileDescriptor], source, lengthBytes,
        writtenBytes);
    if (status == FileSystemStatus::Succeeded) {
        process.result.fileSystemBytesWritten += writtenBytes;
    }
    return status;
}

FileSystemStatus
CloseCurrentProcessFile(const uint64_t fileDescriptor) noexcept {
    if (!IsProcessSchedulingActive() || processFileSystem == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    if (fileDescriptor >= OS_KERNEL_IO_DESCRIPTOR_CAPACITY) {
        return FileSystemStatus::InvalidHandle;
    }
    ProcessControlBlock &process =
        processControlBlocks[processScheduler.CurrentProcessIndex()];
    IoDescriptorKind descriptorKind = IoDescriptorKind::Closed;
    if (process.descriptors.Lookup(fileDescriptor, descriptorKind) !=
            IoDescriptorStatus::Succeeded ||
        descriptorKind != IoDescriptorKind::RegularFile) {
        return FileSystemStatus::InvalidHandle;
    }
    const FileSystemStatus closeStatus =
        processFileSystem->Close(process.fileHandles[fileDescriptor]);
    if (closeStatus != FileSystemStatus::Succeeded) {
        return closeStatus;
    }
    IoDescriptorKind closedKind = IoDescriptorKind::Closed;
    return process.descriptors.Close(fileDescriptor, closedKind) ==
                   IoDescriptorStatus::Succeeded
               ? FileSystemStatus::Succeeded
               : FileSystemStatus::InvalidHandle;
}

FileSystemStatus CreateCurrentProcessDirectory(
    const uint8_t *path, const uint64_t pathLengthBytes) noexcept {
    if (!IsProcessSchedulingActive() || processFileSystem == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    return processFileSystem->CreateDirectory(path, pathLengthBytes);
}

FileSystemStatus SyncCurrentProcessFileSystem() noexcept {
    if (!IsProcessSchedulingActive() || processFileSystem == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    return processFileSystem->Sync();
}

ProcessIoStatus TryReadCurrentProcessDescriptor(
    const uint64_t descriptor, uint8_t *const destination,
    const uint64_t capacityBytes, uint64_t &readBytes,
    FileSystemStatus &fileSystemStatus) noexcept {
    readBytes = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    fileSystemStatus = FileSystemStatus::Succeeded;
    if (!IsProcessSchedulingActive()) {
        return ProcessIoStatus::InvalidArgument;
    }
    ProcessControlBlock &process =
        processControlBlocks[processScheduler.CurrentProcessIndex()];
    IoDescriptorKind descriptorKind = IoDescriptorKind::Closed;
    if (process.descriptors.Lookup(descriptor, descriptorKind) !=
        IoDescriptorStatus::Succeeded) {
        return ProcessIoStatus::InvalidDescriptor;
    }
    if (descriptorKind == IoDescriptorKind::ConsoleInput) {
        const ConsoleInputStatus status =
            processConsoleInput.TryRead(destination, capacityBytes, readBytes);
        if (status == ConsoleInputStatus::Empty) {
            return ProcessIoStatus::WouldBlock;
        }
        if (status != ConsoleInputStatus::Succeeded) {
            return ProcessIoStatus::InvalidArgument;
        }
        process.result.consoleBytesRead += readBytes;
        return ProcessIoStatus::Succeeded;
    }
    if (descriptorKind == IoDescriptorKind::RegularFile) {
        fileSystemStatus = processFileSystem->Read(
            process.fileHandles[descriptor], destination, capacityBytes,
            readBytes);
        if (fileSystemStatus != FileSystemStatus::Succeeded) {
            return ProcessIoStatus::FileSystemFailure;
        }
        process.result.fileSystemBytesRead += readBytes;
        return ProcessIoStatus::Succeeded;
    }
    if (descriptorKind == IoDescriptorKind::PipeReader) {
        const PipeStatus status =
            processPipe.TryRead(destination, capacityBytes, readBytes);
        if (status == PipeStatus::Succeeded) {
            process.result.pipeBytesRead += readBytes;
            WakeRequiredProcesses(ProcessWaitReason::DescriptorWritable);
        } else if (status == PipeStatus::EndOfFile) {
            ++pipeEndOfFileObservationCount;
        }
        return MapPipeIoStatus(status);
    }
    return ProcessIoStatus::PermissionDenied;
}

ProcessIoStatus TryWriteCurrentProcessDescriptor(
    const uint64_t descriptor, const uint8_t *const source,
    const uint64_t lengthBytes, uint64_t &writtenBytes,
    FileSystemStatus &fileSystemStatus) noexcept {
    writtenBytes = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    fileSystemStatus = FileSystemStatus::Succeeded;
    if (!IsProcessSchedulingActive()) {
        return ProcessIoStatus::InvalidArgument;
    }
    ProcessControlBlock &process =
        processControlBlocks[processScheduler.CurrentProcessIndex()];
    IoDescriptorKind descriptorKind = IoDescriptorKind::Closed;
    if (process.descriptors.Lookup(descriptor, descriptorKind) !=
        IoDescriptorStatus::Succeeded) {
        return ProcessIoStatus::InvalidDescriptor;
    }
    if (descriptorKind == IoDescriptorKind::ConsoleOutput ||
        descriptorKind == IoDescriptorKind::ConsoleError) {
        const SerialPort serialPort{OS_KERNEL_SERIAL_COM1_BASE_PORT};
        while (writtenBytes < lengthBytes) {
            if (!serialPort.TryWriteByte(
                    static_cast<char>(source[writtenBytes]))) {
                return ProcessIoStatus::DeviceFailure;
            }
            ++writtenBytes;
        }
        process.result.consoleBytesWritten += writtenBytes;
        return ProcessIoStatus::Succeeded;
    }
    if (descriptorKind == IoDescriptorKind::RegularFile) {
        fileSystemStatus = processFileSystem->Write(
            process.fileHandles[descriptor], source, lengthBytes,
            writtenBytes);
        if (fileSystemStatus != FileSystemStatus::Succeeded) {
            return ProcessIoStatus::FileSystemFailure;
        }
        process.result.fileSystemBytesWritten += writtenBytes;
        return ProcessIoStatus::Succeeded;
    }
    if (descriptorKind == IoDescriptorKind::PipeWriter) {
        const PipeStatus status =
            processPipe.TryWrite(source, lengthBytes, writtenBytes);
        if (status == PipeStatus::Succeeded) {
            process.result.pipeBytesWritten += writtenBytes;
            WakeRequiredProcesses(ProcessWaitReason::DescriptorReadable);
        } else if (status == PipeStatus::BrokenPipe) {
            ++pipeBrokenObservationCount;
        }
        return MapPipeIoStatus(status);
    }
    return ProcessIoStatus::PermissionDenied;
}

ProcessIoStatus CloseCurrentProcessDescriptor(
    const uint64_t descriptor,
    FileSystemStatus &fileSystemStatus) noexcept {
    fileSystemStatus = FileSystemStatus::Succeeded;
    if (!IsProcessSchedulingActive()) {
        return ProcessIoStatus::InvalidArgument;
    }
    ProcessControlBlock &process =
        processControlBlocks[processScheduler.CurrentProcessIndex()];
    IoDescriptorKind descriptorKind = IoDescriptorKind::Closed;
    const IoDescriptorStatus lookupStatus =
        process.descriptors.Lookup(descriptor, descriptorKind);
    if (lookupStatus != IoDescriptorStatus::Succeeded) {
        return ProcessIoStatus::InvalidDescriptor;
    }
    if (descriptor < OS_KERNEL_IO_FIRST_DYNAMIC_DESCRIPTOR) {
        return ProcessIoStatus::PermissionDenied;
    }
    if (descriptorKind == IoDescriptorKind::RegularFile ||
        descriptorKind == IoDescriptorKind::Directory) {
        fileSystemStatus =
            processFileSystem->Close(process.fileHandles[descriptor]);
        if (fileSystemStatus != FileSystemStatus::Succeeded) {
            return ProcessIoStatus::FileSystemFailure;
        }
    } else if (descriptorKind == IoDescriptorKind::PipeReader) {
        const PipeStatus status = processPipe.CloseReader();
        if (status != PipeStatus::Succeeded) {
            return MapPipeIoStatus(status);
        }
        WakeRequiredProcesses(ProcessWaitReason::DescriptorWritable);
    } else if (descriptorKind == IoDescriptorKind::PipeWriter) {
        const PipeStatus status = processPipe.CloseWriter();
        if (status != PipeStatus::Succeeded) {
            return MapPipeIoStatus(status);
        }
        WakeRequiredProcesses(ProcessWaitReason::DescriptorReadable);
    } else {
        return ProcessIoStatus::PermissionDenied;
    }
    IoDescriptorKind closedKind = IoDescriptorKind::Closed;
    if (process.descriptors.Close(descriptor, closedKind) !=
        IoDescriptorStatus::Succeeded) {
        HaltProcessor();
    }
    return ProcessIoStatus::Succeeded;
}

FileSystemStatus OpenCurrentProcessDirectory(
    const uint8_t *const path, const uint64_t pathLengthBytes,
    uint64_t &fileDescriptor) noexcept {
    fileDescriptor = OS_KERNEL_IO_DESCRIPTOR_CAPACITY;
    if (!IsProcessSchedulingActive() || processFileSystem == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    FileSystemHandle handle{};
    FileSystemStatus status =
        processFileSystem->OpenDirectory(path, pathLengthBytes, handle);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    ProcessControlBlock &process =
        processControlBlocks[processScheduler.CurrentProcessIndex()];
    if (process.descriptors.Allocate(IoDescriptorKind::Directory,
                                     fileDescriptor) !=
        IoDescriptorStatus::Succeeded) {
        if (processFileSystem->Close(handle) != FileSystemStatus::Succeeded) {
            HaltProcessor();
        }
        return FileSystemStatus::DataCapacityExhausted;
    }
    process.fileHandles[fileDescriptor] = handle;
    return FileSystemStatus::Succeeded;
}

FileSystemStatus ReadCurrentProcessDirectory(
    const uint64_t fileDescriptor, FileSystemDirectoryEntry &entry,
    bool &endOfDirectory) noexcept {
    entry = FileSystemDirectoryEntry{};
    endOfDirectory = false;
    if (!IsProcessSchedulingActive() || processFileSystem == nullptr ||
        fileDescriptor >= OS_KERNEL_IO_DESCRIPTOR_CAPACITY) {
        return FileSystemStatus::InvalidHandle;
    }
    ProcessControlBlock &process =
        processControlBlocks[processScheduler.CurrentProcessIndex()];
    IoDescriptorKind descriptorKind = IoDescriptorKind::Closed;
    if (process.descriptors.Lookup(fileDescriptor, descriptorKind) !=
            IoDescriptorStatus::Succeeded ||
        descriptorKind != IoDescriptorKind::Directory) {
        return FileSystemStatus::InvalidHandle;
    }
    return processFileSystem->ReadDirectory(
        process.fileHandles[fileDescriptor], entry, endOfDirectory);
}

ProcessIoStatus CurrentProcessDescriptorReadCanProgress(
    const uint64_t descriptor, bool &canProgress) noexcept {
    canProgress = false;
    if (!IsProcessSchedulingActive()) {
        return ProcessIoStatus::InvalidArgument;
    }
    ProcessControlBlock &process =
        processControlBlocks[processScheduler.CurrentProcessIndex()];
    IoDescriptorKind descriptorKind = IoDescriptorKind::Closed;
    if (process.descriptors.Lookup(descriptor, descriptorKind) !=
        IoDescriptorStatus::Succeeded) {
        return ProcessIoStatus::InvalidDescriptor;
    }
    if (descriptorKind == IoDescriptorKind::ConsoleInput) {
        canProgress = processConsoleInput.ReadCanProgress();
        return ProcessIoStatus::Succeeded;
    }
    if (descriptorKind == IoDescriptorKind::RegularFile ||
        descriptorKind == IoDescriptorKind::Directory) {
        canProgress = true;
        return ProcessIoStatus::Succeeded;
    }
    if (descriptorKind == IoDescriptorKind::PipeReader) {
        canProgress = processPipe.ReadCanProgress();
        if (!canProgress) {
            ++pipeReaderBlockCount;
        }
        return ProcessIoStatus::Succeeded;
    }
    return ProcessIoStatus::PermissionDenied;
}

ProcessIoStatus CurrentProcessDescriptorWriteCanProgress(
    const uint64_t descriptor, bool &canProgress) noexcept {
    canProgress = false;
    if (!IsProcessSchedulingActive()) {
        return ProcessIoStatus::InvalidArgument;
    }
    ProcessControlBlock &process =
        processControlBlocks[processScheduler.CurrentProcessIndex()];
    IoDescriptorKind descriptorKind = IoDescriptorKind::Closed;
    if (process.descriptors.Lookup(descriptor, descriptorKind) !=
        IoDescriptorStatus::Succeeded) {
        return ProcessIoStatus::InvalidDescriptor;
    }
    if (descriptorKind == IoDescriptorKind::ConsoleOutput ||
        descriptorKind == IoDescriptorKind::ConsoleError ||
        descriptorKind == IoDescriptorKind::RegularFile) {
        canProgress = true;
        return ProcessIoStatus::Succeeded;
    }
    if (descriptorKind == IoDescriptorKind::PipeWriter) {
        canProgress = processPipe.WriteCanProgress();
        if (!canProgress) {
            ++pipeWriterBlockCount;
        }
        return ProcessIoStatus::Succeeded;
    }
    return ProcessIoStatus::PermissionDenied;
}

void SubmitConsoleCharacter(const uint8_t character) noexcept {
    const ConsoleInputStatus submitStatus =
        processConsoleInput.Submit(character);
    if (submitStatus == ConsoleInputStatus::Succeeded &&
        processSchedulingActive) {
        WakeRequiredProcesses(ProcessWaitReason::DescriptorReadable);
    }
}

bool ProcessPipeReadCanProgress() noexcept { return processPipe.ReadCanProgress(); }

bool ProcessPipeWriteCanProgress() noexcept { return processPipe.WriteCanProgress(); }

ProcessRuntimeStatus BlockCurrentProcess(ExceptionFrame &frame, const ProcessWaitReason waitReason,
                                         ExceptionFrame *&resumeFrame) noexcept {
    resumeFrame = &frame;
    if (!IsProcessSchedulingActive() ||
        !CurrentFrameIsValid(processScheduler.CurrentProcessIndex(), frame)) {
        return ProcessRuntimeStatus::SchedulerFailure;
    }
    const uint64_t processIndex = processScheduler.CurrentProcessIndex();
    processControlBlocks[processIndex].savedFrame = &frame;

    ProcessSchedulingDecision decision{};
    const ProcessSchedulerStatus status =
        processScheduler.BlockCurrentProcess(waitReason, decision);
    if (status == ProcessSchedulerStatus::NoReadyProcess) {
        return ProcessRuntimeStatus::NoReadyProcess;
    }
    if (status != ProcessSchedulerStatus::Succeeded) {
        return ProcessRuntimeStatus::SchedulerFailure;
    }
    if (waitReason == ProcessWaitReason::PipeReadable) {
        ++pipeReaderBlockCount;
    } else if (waitReason == ProcessWaitReason::PipeWritable) {
        ++pipeWriterBlockCount;
    }
    if (!decision.switched) {
        ActivateKernelPageTable();
        if (!SetPrivilegeStackPointer0(DefaultPrivilegeStackPointer0())) {
            HaltProcessor();
        }
        osKernelReturnFromUserMode();
    }
    if (!ActivateProcess(decision.currentProcessIndex)) {
        return ProcessRuntimeStatus::SchedulerFailure;
    }
    resumeFrame =
        processControlBlocks[decision.currentProcessIndex].savedFrame;
    return ProcessRuntimeStatus::Succeeded;
}

ProcessRuntimeStatus WakeProcesses(const ProcessWaitReason waitReason,
                                   const uint64_t maximumWakeCount,
                                   uint64_t &wokenProcessCount) noexcept {
    if (!IsProcessSchedulingActive()) {
        return ProcessRuntimeStatus::SchedulerFailure;
    }
    return processScheduler.WakeBlockedProcesses(waitReason, maximumWakeCount, wokenProcessCount) ==
                   ProcessSchedulerStatus::Succeeded
               ? ProcessRuntimeStatus::Succeeded
               : ProcessRuntimeStatus::SchedulerFailure;
}

ExceptionFrame *HandleProcessTimerInterrupt(ExceptionFrame &frame) noexcept {
    if (!processSchedulingActive || !FrameOriginatedFromUser(frame)) {
        return &frame;
    }
    const uint64_t processIndex = processScheduler.CurrentProcessIndex();
    if (!CurrentFrameIsValid(processIndex, frame)) {
        HaltProcessor();
    }
    processControlBlocks[processIndex].savedFrame = &frame;

    ProcessSchedulingDecision decision{};
    if (processScheduler.HandleTimerTick(decision) != ProcessSchedulerStatus::Succeeded) {
        HaltProcessor();
    }
    if (!decision.switched) {
        return &frame;
    }
    if (!ActivateProcess(decision.currentProcessIndex)) {
        HaltProcessor();
    }
    return processControlBlocks[decision.currentProcessIndex].savedFrame;
}

ExceptionFrame *TerminateCurrentProcessFromExit(ExceptionFrame &frame,
                                                const int64_t exitCode) noexcept {
    return CompleteCurrentProcess(frame, ProcessTerminationReason::Exited, exitCode,
                                  OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE);
}

ExceptionFrame *TerminateCurrentProcessFromException(ExceptionFrame &frame,
                                                     const uint64_t pageFaultAddress) noexcept {
    return CompleteCurrentProcess(frame, ProcessTerminationReason::Exception,
                                  OS_KERNEL_PROCESS_RUNTIME_SUCCESS_EXIT_CODE, pageFaultAddress);
}

}

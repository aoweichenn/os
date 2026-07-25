#include "os/kernel/process_runtime.hpp"

#include "os/kernel/descriptor_tables.hpp"
#include "os/kernel/memory_manager.hpp"
#include "os/kernel/process_memory_layout.hpp"
#include "os/kernel/processor.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_INITIAL_USER_FLAGS = 0x202ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX = 0ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_WAKE_ALL_COUNT = OS_KERNEL_PROCESS_CAPACITY;
constexpr int64_t OS_KERNEL_PROCESS_RUNTIME_SUCCESS_EXIT_CODE = 0LL;

struct ProcessControlBlock final {
    UserAddressSpace addressSpace;
    ExceptionFrame *savedFrame;
    ProcessExecutionResult result;
};

ProcessScheduler processScheduler;
Pipe processPipe;
ProcessControlBlock processControlBlocks[OS_KERNEL_PROCESS_CAPACITY];
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

void CloseProcessPipeEndpoints(const UserProgramSelection selection) noexcept {
    if (selection == UserProgramSelection::IpcProducer) {
        const PipeStatus status = processPipe.CloseWriter();
        if (status == PipeStatus::Succeeded) {
            WakeRequiredProcesses(ProcessWaitReason::PipeReadable);
        } else if (status != PipeStatus::AlreadyClosed) {
            HaltProcessor();
        }
    }
    if (selection == UserProgramSelection::IpcConsumer) {
        const PipeStatus status = processPipe.CloseReader();
        if (status == PipeStatus::Succeeded) {
            WakeRequiredProcesses(ProcessWaitReason::PipeWritable);
        } else if (status != PipeStatus::AlreadyClosed) {
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
    CloseProcessPipeEndpoints(process.result.selection);

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
    if (!decision.switched || !ActivateProcess(decision.currentProcessIndex)) {
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
    pipeReaderBlockCount = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    pipeWriterBlockCount = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    pipeEndOfFileObservationCount = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    pipeBrokenObservationCount = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    processRuntimeInitialized = true;
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
            },
    };
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
    ProcessSchedulingDecision decision{};
    if (processScheduler.Start(decision) != ProcessSchedulerStatus::Succeeded) {
        RestoreInterrupts(interruptsWereEnabled);
        return ProcessRuntimeStatus::SchedulerFailure;
    }
    processSchedulingActive = true;
    if (!ActivateProcess(decision.currentProcessIndex)) {
        processSchedulingActive = false;
        RestoreInterrupts(interruptsWereEnabled);
        return ProcessRuntimeStatus::PageTableActivationFailure;
    }
    osKernelEnterScheduledProcess(processControlBlocks[decision.currentProcessIndex].savedFrame);
    if (processSchedulingActive || ReadPageTableRoot() != GetKernelPageTableRoot()) {
        HaltProcessor();
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
    }
    return status;
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
    if (status != ProcessSchedulerStatus::Succeeded || !decision.switched ||
        !ActivateProcess(decision.currentProcessIndex)) {
        return ProcessRuntimeStatus::SchedulerFailure;
    }
    if (waitReason == ProcessWaitReason::PipeReadable) {
        ++pipeReaderBlockCount;
    } else if (waitReason == ProcessWaitReason::PipeWritable) {
        ++pipeWriterBlockCount;
    }
    resumeFrame = processControlBlocks[decision.currentProcessIndex].savedFrame;
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

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

struct ProcessControlBlock final {
    UserAddressSpace addressSpace;
    ExceptionFrame *savedFrame;
    ProcessExecutionResult result;
};

ProcessScheduler processScheduler;
ProcessControlBlock processControlBlocks[OS_KERNEL_PROCESS_CAPACITY];
PhysicalFrameAllocatorStatistics framesBeforeProcesses;
PhysicalFrameAllocatorStatistics framesAfterProcesses;
bool processRuntimeInitialized;
bool processSchedulingActive;

extern "C" void osKernelEnterScheduledProcess(ExceptionFrame *frame) noexcept;
extern "C" [[noreturn]] void osKernelReturnFromUserMode() noexcept;

void ResetProcessControlBlocks() noexcept {
    for (uint64_t processIndex = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         processIndex < OS_KERNEL_PROCESS_CAPACITY;
         ++processIndex) {
        processControlBlocks[processIndex] = ProcessControlBlock{};
    }
}

[[nodiscard]] bool BuildInitialContextFrame(const uint64_t processIndex,
                                            const uint64_t processId,
                                            const UserAddressSpace &addressSpace,
                                            ExceptionFrame *&savedFrame) noexcept {
    const uint64_t stackTopAddress = ProcessKernelStackTopAddress(processIndex);
    if (stackTopAddress < OS_KERNEL_USER_PRIVILEGE_FRAME_SIZE_BYTES) {
        return false;
    }
    const uint64_t frameAddress =
        stackTopAddress - OS_KERNEL_USER_PRIVILEGE_FRAME_SIZE_BYTES;
    if (!ProcessKernelStackContains(processIndex, frameAddress,
                                    OS_KERNEL_USER_PRIVILEGE_FRAME_SIZE_BYTES)) {
        return false;
    }

    UserPrivilegeFrame *const frame =
        reinterpret_cast<UserPrivilegeFrame *>(frameAddress);
    *frame = UserPrivilegeFrame{};
    frame->common.registerRdi = processId;
    frame->common.vector = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    frame->common.errorCode = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    frame->common.instructionPointer = addressSpace.entryVirtualAddress;
    frame->common.codeSegment =
        static_cast<uint64_t>(OS_KERNEL_DESCRIPTOR_USER_CODE_SELECTOR);
    frame->common.flags = OS_KERNEL_PROCESS_RUNTIME_INITIAL_USER_FLAGS;
    frame->userStackPointer = addressSpace.stackTopVirtualAddress;
    frame->userStackSegment =
        static_cast<uint64_t>(OS_KERNEL_DESCRIPTOR_USER_DATA_SELECTOR);
    savedFrame = &frame->common;
    return true;
}

[[nodiscard]] bool CurrentFrameIsValid(const uint64_t processIndex,
                                       const ExceptionFrame &frame) noexcept {
    if (processIndex >= OS_KERNEL_PROCESS_CAPACITY ||
        !FrameOriginatedFromUser(frame) ||
        !ProcessKernelStackContains(processIndex, reinterpret_cast<uint64_t>(&frame),
                                    OS_KERNEL_USER_PRIVILEGE_FRAME_SIZE_BYTES)) {
        return false;
    }
    const ProcessControlBlock &process = processControlBlocks[processIndex];
    return process.addressSpace.rootPhysicalAddress !=
               OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE &&
           ReadPageTableRoot() == process.addressSpace.rootPhysicalAddress;
}

[[nodiscard]] bool ActivateProcess(const uint64_t processIndex) noexcept {
    if (processIndex >= OS_KERNEL_PROCESS_CAPACITY) {
        return false;
    }
    ProcessControlBlock &process = processControlBlocks[processIndex];
    if (process.savedFrame == nullptr ||
        !ProcessKernelStackContains(processIndex,
                                    reinterpret_cast<uint64_t>(process.savedFrame),
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

[[nodiscard]] ExceptionFrame *
CompleteCurrentProcess(ExceptionFrame &frame,
                       const ProcessTerminationReason terminationReason,
                       const int64_t exitCode, const uint64_t pageFaultAddress) noexcept {
    const uint64_t processIndex = processScheduler.CurrentProcessIndex();
    if (!processSchedulingActive ||
        !CurrentFrameIsValid(processIndex, frame)) {
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

    ProcessSchedulingDecision decision{};
    if (processScheduler.TerminateCurrentProcess(decision) !=
        ProcessSchedulerStatus::Succeeded) {
        HaltProcessor();
    }

    ActivateKernelPageTable();
    if (DestroyUserAddressSpace(process.addressSpace) !=
        UserAddressSpaceStatus::Succeeded) {
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
    if (!decision.switched ||
        !ActivateProcess(decision.currentProcessIndex)) {
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
    processRuntimeInitialized = true;
    return ProcessRuntimeStatus::Succeeded;
}

ProcessRuntimeStatus
CreateProcess(const UserProgramSelection selection,
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
        LoadUserAddressSpace(image.image, image.imageSizeBytes, addressSpace,
                             elfValidationStatus);
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
        if (DestroyUserAddressSpace(addressSpace) !=
                UserAddressSpaceStatus::Succeeded ||
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
                .exitCode = 0LL,
                .exceptionVector = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
                .exceptionErrorCode = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
                .exceptionInstructionPointer = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
                .pageFaultAddress = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
                .systemCallCount = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
                .rootPhysicalAddress = addressSpace.rootPhysicalAddress,
                .mappedPageCount = addressSpace.mappedPageCount,
                .runTickCount = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
                .dispatchCount = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
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
    osKernelEnterScheduledProcess(
        processControlBlocks[decision.currentProcessIndex].savedFrame);
    if (processSchedulingActive ||
        ReadPageTableRoot() != GetKernelPageTableRoot()) {
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
        .processes = {},
    };
    for (uint64_t processIndex = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         processIndex < OS_KERNEL_PROCESS_CAPACITY;
         ++processIndex) {
        statistics.processes[processIndex] =
            processControlBlocks[processIndex].result;
        ProcessSchedulerEntry schedulerEntry{};
        if (processScheduler.ReadEntry(processIndex, schedulerEntry) ==
            ProcessSchedulerStatus::Succeeded) {
            statistics.processes[processIndex].runTickCount =
                schedulerEntry.runTickCount;
            statistics.processes[processIndex].dispatchCount =
                schedulerEntry.dispatchCount;
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

void RecordCurrentProcessSystemCall() noexcept {
    if (!IsProcessSchedulingActive()) {
        HaltProcessor();
    }
    ProcessControlBlock &process =
        processControlBlocks[processScheduler.CurrentProcessIndex()];
    ++process.result.systemCallCount;
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
    if (processScheduler.HandleTimerTick(decision) !=
        ProcessSchedulerStatus::Succeeded) {
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

ExceptionFrame *
TerminateCurrentProcessFromException(ExceptionFrame &frame,
                                     const uint64_t pageFaultAddress) noexcept {
    return CompleteCurrentProcess(frame, ProcessTerminationReason::Exception, 0LL,
                                  pageFaultAddress);
}

}

#pragma once

#include "os/kernel/exception_frame.hpp"
#include "os/kernel/physical_frame_allocator.hpp"
#include "os/kernel/pipe.hpp"
#include "os/kernel/process_scheduler.hpp"
#include "os/kernel/user_elf.hpp"
#include "os/kernel/user_memory.hpp"
#include "os/kernel/user_program_images.hpp"

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
    ContextFrameFailure,
    PageTableActivationFailure,
    NoReadyProcess,
};

struct ProcessCreationResult final {
    uint64_t processId;
    uint64_t processIndex;
    uint64_t rootPhysicalAddress;
    uint64_t entryVirtualAddress;
    uint64_t mappedPageCount;
};

struct ProcessExecutionResult final {
    uint64_t processId;
    UserProgramSelection selection;
    ProcessTerminationReason terminationReason;
    int64_t exitCode;
    uint64_t exceptionVector;
    uint64_t exceptionErrorCode;
    uint64_t exceptionInstructionPointer;
    uint64_t pageFaultAddress;
    uint64_t systemCallCount;
    uint64_t rootPhysicalAddress;
    uint64_t mappedPageCount;
    uint64_t runTickCount;
    uint64_t dispatchCount;
    uint64_t pipeBytesRead;
    uint64_t pipeBytesWritten;
};

struct ProcessIpcStatistics final {
    PipeStatistics pipe;
    uint64_t readerBlockCount;
    uint64_t writerBlockCount;
    uint64_t endOfFileObservationCount;
    uint64_t brokenPipeObservationCount;
};

struct ProcessRuntimeStatistics final {
    ProcessSchedulerStatistics scheduler;
    PhysicalFrameAllocatorStatistics framesBeforeProcesses;
    PhysicalFrameAllocatorStatistics framesAfterProcesses;
    ProcessIpcStatistics ipc;
    ProcessExecutionResult processes[OS_KERNEL_PROCESS_CAPACITY];
};

[[nodiscard]] ProcessRuntimeStatus InitializeProcessRuntime() noexcept;
[[nodiscard]] ProcessRuntimeStatus
CreateProcess(UserProgramSelection selection, ProcessCreationResult &creationResult,
              UserElfValidationStatus &elfValidationStatus,
              UserAddressSpaceStatus &addressSpaceStatus) noexcept;
[[nodiscard]] ProcessRuntimeStatus ExecuteProcesses() noexcept;
[[nodiscard]] ProcessRuntimeStatistics GetProcessRuntimeStatistics() noexcept;
[[nodiscard]] bool IsProcessSchedulingActive() noexcept;
[[nodiscard]] uint64_t CurrentProcessId() noexcept;
[[nodiscard]] UserProgramSelection CurrentProcessSelection() noexcept;
void RecordCurrentProcessSystemCall() noexcept;
[[nodiscard]] bool CurrentProcessCanReadPipe() noexcept;
[[nodiscard]] bool CurrentProcessCanWritePipe() noexcept;
[[nodiscard]] PipeStatus TryReadCurrentProcessPipe(uint8_t *destination, uint64_t capacityBytes,
                                                   uint64_t &readBytes) noexcept;
[[nodiscard]] PipeStatus TryWriteCurrentProcessPipe(const uint8_t *source, uint64_t lengthBytes,
                                                    uint64_t &writtenBytes) noexcept;
[[nodiscard]] PipeStatus CloseCurrentProcessPipeReader() noexcept;
[[nodiscard]] PipeStatus CloseCurrentProcessPipeWriter() noexcept;
[[nodiscard]] bool ProcessPipeReadCanProgress() noexcept;
[[nodiscard]] bool ProcessPipeWriteCanProgress() noexcept;
[[nodiscard]] ProcessRuntimeStatus BlockCurrentProcess(ExceptionFrame &frame,
                                                       ProcessWaitReason waitReason,
                                                       ExceptionFrame *&resumeFrame) noexcept;
[[nodiscard]] ProcessRuntimeStatus WakeProcesses(ProcessWaitReason waitReason,
                                                 uint64_t maximumWakeCount,
                                                 uint64_t &wokenProcessCount) noexcept;
[[nodiscard]] ExceptionFrame *HandleProcessTimerInterrupt(ExceptionFrame &frame) noexcept;
[[nodiscard]] ExceptionFrame *TerminateCurrentProcessFromExit(ExceptionFrame &frame,
                                                              int64_t exitCode) noexcept;
[[nodiscard]] ExceptionFrame *
TerminateCurrentProcessFromException(ExceptionFrame &frame, uint64_t pageFaultAddress) noexcept;

}

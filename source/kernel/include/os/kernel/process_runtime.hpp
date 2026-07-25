#pragma once

#include "os/kernel/exception_frame.hpp"
#include "os/kernel/physical_frame_allocator.hpp"
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
};

struct ProcessRuntimeStatistics final {
    ProcessSchedulerStatistics scheduler;
    PhysicalFrameAllocatorStatistics framesBeforeProcesses;
    PhysicalFrameAllocatorStatistics framesAfterProcesses;
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
void RecordCurrentProcessSystemCall() noexcept;
[[nodiscard]] ExceptionFrame *HandleProcessTimerInterrupt(ExceptionFrame &frame) noexcept;
[[nodiscard]] ExceptionFrame *TerminateCurrentProcessFromExit(ExceptionFrame &frame,
                                                              int64_t exitCode) noexcept;
[[nodiscard]] ExceptionFrame *
TerminateCurrentProcessFromException(ExceptionFrame &frame, uint64_t pageFaultAddress) noexcept;

}

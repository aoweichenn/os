#include "os/kernel/user_runtime.hpp"

#include "os/kernel/processor.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_USER_RUNTIME_EMPTY_VALUE = 0ULL;

bool userExecutionActive;
UserExecutionResult currentUserExecutionResult;

extern "C" void osKernelEnterUserMode(uint64_t entryVirtualAddress,
                                      uint64_t stackTopVirtualAddress) noexcept;
extern "C" [[noreturn]] void osKernelReturnFromUserMode() noexcept;

[[noreturn]] void ReturnFromTerminatedUserExecution() noexcept {
    userExecutionActive = false;
    osKernelReturnFromUserMode();
}

}

UserRuntimeStatus PrepareUserProgram(const UserProgramSelection selection,
                                     PreparedUserProgram &program,
                                     UserElfValidationStatus &elfValidationStatus,
                                     UserAddressSpaceStatus &addressSpaceStatus) noexcept {
    const UserProgramImage image = SelectUserProgramImage(selection);
    UserAddressSpace candidateAddressSpace{};
    addressSpaceStatus = LoadUserAddressSpace(image.image, image.imageSizeBytes,
                                              candidateAddressSpace, elfValidationStatus);
    if (addressSpaceStatus == UserAddressSpaceStatus::InvalidElf) {
        return UserRuntimeStatus::InvalidElf;
    }
    if (addressSpaceStatus != UserAddressSpaceStatus::Succeeded) {
        return UserRuntimeStatus::AddressSpaceFailure;
    }
    program = PreparedUserProgram{
        .selection = selection,
        .addressSpace = candidateAddressSpace,
    };
    return UserRuntimeStatus::Succeeded;
}

UserRuntimeStatus ExecuteUserProgram(const PreparedUserProgram &program,
                                     UserExecutionResult &result) noexcept {
    if (userExecutionActive) {
        return UserRuntimeStatus::AlreadyActive;
    }
    currentUserExecutionResult = UserExecutionResult{
        .terminationReason = UserTerminationReason::None,
        .exitCode = 0LL,
        .exceptionVector = OS_KERNEL_USER_RUNTIME_EMPTY_VALUE,
        .exceptionErrorCode = OS_KERNEL_USER_RUNTIME_EMPTY_VALUE,
        .exceptionInstructionPointer = OS_KERNEL_USER_RUNTIME_EMPTY_VALUE,
        .pageFaultAddress = OS_KERNEL_USER_RUNTIME_EMPTY_VALUE,
        .systemCallCount = OS_KERNEL_USER_RUNTIME_EMPTY_VALUE,
    };
    userExecutionActive = true;
    osKernelEnterUserMode(program.addressSpace.entryVirtualAddress,
                          program.addressSpace.stackTopVirtualAddress);
    if (userExecutionActive ||
        currentUserExecutionResult.terminationReason == UserTerminationReason::None) {
        HaltProcessor();
    }
    result = currentUserExecutionResult;
    return UserRuntimeStatus::Succeeded;
}

bool IsUserExecutionActive() noexcept { return userExecutionActive; }

void RecordUserSystemCall() noexcept {
    if (!userExecutionActive) {
        HaltProcessor();
    }
    ++currentUserExecutionResult.systemCallCount;
}

[[noreturn]] void TerminateUserExecutionFromExit(const int64_t exitCode) noexcept {
    if (!userExecutionActive) {
        HaltProcessor();
    }
    currentUserExecutionResult.terminationReason = UserTerminationReason::Exited;
    currentUserExecutionResult.exitCode = exitCode;
    ReturnFromTerminatedUserExecution();
}

[[noreturn]] void TerminateUserExecutionFromException(const ExceptionFrame &frame,
                                                      const uint64_t pageFaultAddress) noexcept {
    if (!userExecutionActive || !FrameOriginatedFromUser(frame)) {
        HaltProcessor();
    }
    currentUserExecutionResult.terminationReason = UserTerminationReason::Exception;
    currentUserExecutionResult.exceptionVector = frame.vector;
    currentUserExecutionResult.exceptionErrorCode = frame.errorCode;
    currentUserExecutionResult.exceptionInstructionPointer = frame.instructionPointer;
    currentUserExecutionResult.pageFaultAddress = pageFaultAddress;
    ReturnFromTerminatedUserExecution();
}

}

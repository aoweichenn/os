#pragma once

#include "os/kernel/exception_frame.hpp"
#include "os/kernel/user_elf.hpp"
#include "os/kernel/user_memory.hpp"
#include "os/kernel/user_program_images.hpp"

#include <stdint.h>

namespace os::kernel {

enum class UserTerminationReason : uint64_t {
    None,
    Exited,
    Exception,
};

enum class UserRuntimeStatus : uint64_t {
    Succeeded,
    InvalidElf,
    AddressSpaceFailure,
    AlreadyActive,
    UnexpectedReturn,
};

struct PreparedUserProgram final {
    UserProgramSelection selection;
    UserAddressSpace addressSpace;
};

struct UserExecutionResult final {
    UserTerminationReason terminationReason;
    int64_t exitCode;
    uint64_t exceptionVector;
    uint64_t exceptionErrorCode;
    uint64_t exceptionInstructionPointer;
    uint64_t pageFaultAddress;
    uint64_t systemCallCount;
};

[[nodiscard]] UserRuntimeStatus
PrepareUserProgram(UserProgramSelection selection, PreparedUserProgram &program,
                   UserElfValidationStatus &elfValidationStatus,
                   UserAddressSpaceStatus &addressSpaceStatus) noexcept;
[[nodiscard]] UserRuntimeStatus ExecuteUserProgram(const PreparedUserProgram &program,
                                                   UserExecutionResult &result) noexcept;
[[nodiscard]] bool IsUserExecutionActive() noexcept;
void RecordUserSystemCall() noexcept;
[[noreturn]] void TerminateUserExecutionFromExit(int64_t exitCode) noexcept;
[[noreturn]] void TerminateUserExecutionFromException(const ExceptionFrame &frame,
                                                      uint64_t pageFaultAddress) noexcept;

}

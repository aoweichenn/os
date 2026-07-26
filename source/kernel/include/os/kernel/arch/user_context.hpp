#pragma once

#include "os/kernel/arch/exception_frame.hpp"

#include <stddef.h>
#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_USER_CONTEXT_SIZE_BYTES = 176ULL;
inline constexpr uint64_t OS_KERNEL_USER_CONTEXT_INITIAL_VECTOR = 0ULL;
inline constexpr uint64_t OS_KERNEL_USER_CONTEXT_LEGACY_SYSTEM_CALL_VECTOR = 0x80ULL;
inline constexpr uint64_t OS_KERNEL_USER_CONTEXT_NATIVE_SYSTEM_CALL_VECTOR = 0x81ULL;
inline constexpr uint64_t OS_KERNEL_USER_CONTEXT_FIRST_HARDWARE_INTERRUPT_VECTOR = 32ULL;
inline constexpr uint64_t OS_KERNEL_USER_CONTEXT_HARDWARE_INTERRUPT_VECTOR_COUNT = 16ULL;
inline constexpr uint64_t OS_KERNEL_USER_CONTEXT_REQUIRED_FLAGS = 0x0000000000000002ULL;
inline constexpr uint64_t OS_KERNEL_USER_CONTEXT_VALID_FLAGS_MASK = 0x0000000000010ED7ULL;
inline constexpr uint64_t OS_KERNEL_USER_CONTEXT_SYSTEM_RETURN_FLAGS_MASK = 0x0000000000000AD7ULL;

enum class UserContextEntryMethod : uint64_t {
    Initial,
    LegacyInterrupt,
    NativeSystemCall,
    HardwareInterrupt,
    Invalid,
};

enum class UserReturnMethod : uint64_t {
    Rejected,
    InterruptReturn,
    SystemReturn,
};

enum class UserContextStatus : uint64_t {
    Succeeded,
    InvalidEntryMethod,
    InvalidVirtualAddressWidth,
    InvalidInstructionPointer,
    InvalidStackPointer,
    InvalidCodeSegment,
    InvalidStackSegment,
    InvalidFlags,
};

struct UserContext final {
    ExceptionFrame common;
    uint64_t stack_pointer;
    uint64_t stack_segment;
};

struct UserContextRequirements final {
    uint64_t virtual_address_width_bits;
    uint64_t user_code_segment;
    uint64_t user_stack_segment;
};

[[nodiscard]] UserContextEntryMethod
DecodeUserContextEntryMethod(const UserContext &context) noexcept;
[[nodiscard]] bool IsCanonicalVirtualAddress(uint64_t address,
                                             uint64_t virtual_address_width_bits) noexcept;
[[nodiscard]] bool IsLowerCanonicalUserAddress(uint64_t address,
                                               uint64_t virtual_address_width_bits) noexcept;
[[nodiscard]] UserContextStatus
ValidateUserContext(const UserContext &context,
                    const UserContextRequirements &requirements) noexcept;
[[nodiscard]] UserReturnMethod
SelectUserReturnMethod(const UserContext &context,
                       const UserContextRequirements &requirements) noexcept;
[[nodiscard]] UserContext &AsUserContext(ExceptionFrame &frame) noexcept;
[[nodiscard]] const UserContext &AsUserContext(const ExceptionFrame &frame) noexcept;

static_assert(sizeof(UserContext) == OS_KERNEL_USER_CONTEXT_SIZE_BYTES);
static_assert(offsetof(UserContext, common) == 0ULL);
static_assert(offsetof(UserContext, stack_pointer) == OS_KERNEL_EXCEPTION_FRAME_SIZE_BYTES);
static_assert(offsetof(UserContext, stack_segment) ==
              OS_KERNEL_EXCEPTION_FRAME_SIZE_BYTES + sizeof(uint64_t));

}

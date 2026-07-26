#include "os/kernel/arch/user_context.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_USER_CONTEXT_MINIMUM_VIRTUAL_ADDRESS_WIDTH_BITS = 48ULL;
constexpr uint64_t OS_KERNEL_USER_CONTEXT_MAXIMUM_VIRTUAL_ADDRESS_WIDTH_BITS = 57ULL;
constexpr uint64_t OS_KERNEL_USER_CONTEXT_SIGN_BIT_OFFSET = 1ULL;
constexpr uint64_t OS_KERNEL_USER_CONTEXT_EMPTY_ADDRESS = 0ULL;
constexpr uint64_t OS_KERNEL_USER_CONTEXT_EMPTY_FLAGS = 0ULL;

}

UserContextEntryMethod DecodeUserContextEntryMethod(const UserContext &context) noexcept {
    if (context.common.vector == OS_KERNEL_USER_CONTEXT_INITIAL_VECTOR) {
        return UserContextEntryMethod::Initial;
    }
    if (context.common.vector == OS_KERNEL_USER_CONTEXT_LEGACY_SYSTEM_CALL_VECTOR) {
        return UserContextEntryMethod::LegacyInterrupt;
    }
    if (context.common.vector == OS_KERNEL_USER_CONTEXT_NATIVE_SYSTEM_CALL_VECTOR) {
        return UserContextEntryMethod::NativeSystemCall;
    }
    if (context.common.vector >= OS_KERNEL_USER_CONTEXT_FIRST_HARDWARE_INTERRUPT_VECTOR &&
        context.common.vector < OS_KERNEL_USER_CONTEXT_FIRST_HARDWARE_INTERRUPT_VECTOR +
                                    OS_KERNEL_USER_CONTEXT_HARDWARE_INTERRUPT_VECTOR_COUNT) {
        return UserContextEntryMethod::HardwareInterrupt;
    }
    return UserContextEntryMethod::Invalid;
}

bool IsLowerCanonicalUserAddress(const uint64_t address,
                                 const uint64_t virtual_address_width_bits) noexcept {
    if (virtual_address_width_bits < OS_KERNEL_USER_CONTEXT_MINIMUM_VIRTUAL_ADDRESS_WIDTH_BITS ||
        virtual_address_width_bits > OS_KERNEL_USER_CONTEXT_MAXIMUM_VIRTUAL_ADDRESS_WIDTH_BITS) {
        return false;
    }
    const uint64_t lower_canonical_limit =
        1ULL << (virtual_address_width_bits - OS_KERNEL_USER_CONTEXT_SIGN_BIT_OFFSET);
    return address < lower_canonical_limit &&
           IsCanonicalVirtualAddress(address, virtual_address_width_bits);
}

bool IsCanonicalVirtualAddress(const uint64_t address,
                               const uint64_t virtual_address_width_bits) noexcept {
    if (virtual_address_width_bits < OS_KERNEL_USER_CONTEXT_MINIMUM_VIRTUAL_ADDRESS_WIDTH_BITS ||
        virtual_address_width_bits > OS_KERNEL_USER_CONTEXT_MAXIMUM_VIRTUAL_ADDRESS_WIDTH_BITS) {
        return false;
    }
    const uint64_t sign_bit =
        1ULL << (virtual_address_width_bits - OS_KERNEL_USER_CONTEXT_SIGN_BIT_OFFSET);
    const uint64_t low_address_mask = (sign_bit << OS_KERNEL_USER_CONTEXT_SIGN_BIT_OFFSET) -
                                      OS_KERNEL_USER_CONTEXT_SIGN_BIT_OFFSET;
    const uint64_t upper_address_mask = ~low_address_mask;
    const uint64_t expected_upper_address =
        (address & sign_bit) == OS_KERNEL_USER_CONTEXT_EMPTY_ADDRESS
            ? OS_KERNEL_USER_CONTEXT_EMPTY_ADDRESS
            : upper_address_mask;
    return (address & upper_address_mask) == expected_upper_address;
}

UserContextStatus ValidateUserContext(const UserContext &context,
                                      const UserContextRequirements &requirements) noexcept {
    if (DecodeUserContextEntryMethod(context) == UserContextEntryMethod::Invalid) {
        return UserContextStatus::InvalidEntryMethod;
    }
    if (requirements.virtual_address_width_bits <
            OS_KERNEL_USER_CONTEXT_MINIMUM_VIRTUAL_ADDRESS_WIDTH_BITS ||
        requirements.virtual_address_width_bits >
            OS_KERNEL_USER_CONTEXT_MAXIMUM_VIRTUAL_ADDRESS_WIDTH_BITS) {
        return UserContextStatus::InvalidVirtualAddressWidth;
    }
    if (!IsLowerCanonicalUserAddress(context.common.instruction_pointer,
                                     requirements.virtual_address_width_bits)) {
        return UserContextStatus::InvalidInstructionPointer;
    }
    if (context.stack_pointer == OS_KERNEL_USER_CONTEXT_EMPTY_ADDRESS ||
        !IsLowerCanonicalUserAddress(context.stack_pointer,
                                     requirements.virtual_address_width_bits)) {
        return UserContextStatus::InvalidStackPointer;
    }
    if (context.common.code_segment != requirements.user_code_segment) {
        return UserContextStatus::InvalidCodeSegment;
    }
    if (context.stack_segment != requirements.user_stack_segment) {
        return UserContextStatus::InvalidStackSegment;
    }
    if ((context.common.flags & OS_KERNEL_USER_CONTEXT_REQUIRED_FLAGS) !=
            OS_KERNEL_USER_CONTEXT_REQUIRED_FLAGS ||
        (context.common.flags & ~OS_KERNEL_USER_CONTEXT_VALID_FLAGS_MASK) !=
            OS_KERNEL_USER_CONTEXT_EMPTY_FLAGS) {
        return UserContextStatus::InvalidFlags;
    }
    return UserContextStatus::Succeeded;
}

UserReturnMethod SelectUserReturnMethod(const UserContext &context,
                                        const UserContextRequirements &requirements) noexcept {
    if (ValidateUserContext(context, requirements) != UserContextStatus::Succeeded) {
        return UserReturnMethod::Rejected;
    }
    if (DecodeUserContextEntryMethod(context) == UserContextEntryMethod::NativeSystemCall &&
        (context.common.flags & ~OS_KERNEL_USER_CONTEXT_SYSTEM_RETURN_FLAGS_MASK) ==
            OS_KERNEL_USER_CONTEXT_EMPTY_FLAGS) {
        return UserReturnMethod::SystemReturn;
    }
    return UserReturnMethod::InterruptReturn;
}

UserContext &AsUserContext(ExceptionFrame &frame) noexcept {
    return *reinterpret_cast<UserContext *>(&frame);
}

const UserContext &AsUserContext(const ExceptionFrame &frame) noexcept {
    return *reinterpret_cast<const UserContext *>(&frame);
}

}

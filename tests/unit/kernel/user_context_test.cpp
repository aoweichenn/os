#include "os/kernel/arch/user_context.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_USER_CONTEXT_SUITE_NAME = "kernel/user_context/unit";
constexpr std::string_view OS_TEST_USER_CONTEXT_ENTRY_MESSAGE =
    "初始、INT 0x80、SYSCALL 与硬件 IRQ 必须被准确分类";
constexpr std::string_view OS_TEST_USER_CONTEXT_CANONICAL_MESSAGE =
    "48 位规范地址边界必须区分用户低半区、空洞和高半区";
constexpr std::string_view OS_TEST_USER_CONTEXT_VALIDATION_MESSAGE =
    "可信用户现场必须通过段、地址与 RFLAGS 联合校验";
constexpr std::string_view OS_TEST_USER_CONTEXT_REJECTION_MESSAGE =
    "每类被篡改的返回字段必须产生精确拒绝原因";
constexpr std::string_view OS_TEST_USER_CONTEXT_RETURN_MESSAGE =
    "只有安全的原生现场可以选择 SYSRET，其余合法现场必须走 IRET";
constexpr std::string_view OS_TEST_USER_CONTEXT_LAYOUT_MESSAGE =
    "ExceptionFrame 与 UserContext 必须保持首地址可互换";

constexpr uint64_t OS_TEST_USER_CONTEXT_VIRTUAL_WIDTH_BITS = 48ULL;
constexpr uint64_t OS_TEST_USER_CONTEXT_INVALID_VIRTUAL_WIDTH_BITS = 47ULL;
constexpr uint64_t OS_TEST_USER_CONTEXT_USER_CODE_SEGMENT = 0x23ULL;
constexpr uint64_t OS_TEST_USER_CONTEXT_USER_STACK_SEGMENT = 0x1BULL;
constexpr uint64_t OS_TEST_USER_CONTEXT_KERNEL_CODE_SEGMENT = 0x08ULL;
constexpr uint64_t OS_TEST_USER_CONTEXT_KERNEL_STACK_SEGMENT = 0x10ULL;
constexpr uint64_t OS_TEST_USER_CONTEXT_INSTRUCTION_POINTER = 0x0000000040001000ULL;
constexpr uint64_t OS_TEST_USER_CONTEXT_STACK_POINTER = 0x00007FFFFFFEFFC0ULL;
constexpr uint64_t OS_TEST_USER_CONTEXT_INITIAL_FLAGS = 0x202ULL;
constexpr uint64_t OS_TEST_USER_CONTEXT_DIRECTION_FLAG = 0x400ULL;
constexpr uint64_t OS_TEST_USER_CONTEXT_RESUME_FLAG = 0x10000ULL;
constexpr uint64_t OS_TEST_USER_CONTEXT_FORBIDDEN_IO_PRIVILEGE_FLAGS = 0x3000ULL;
constexpr uint64_t OS_TEST_USER_CONTEXT_LOWER_CANONICAL_MAXIMUM = 0x00007FFFFFFFFFFFULL;
constexpr uint64_t OS_TEST_USER_CONTEXT_FIRST_NON_CANONICAL = 0x0000800000000000ULL;
constexpr uint64_t OS_TEST_USER_CONTEXT_UPPER_CANONICAL_MINIMUM = 0xFFFF800000000000ULL;
constexpr uint64_t OS_TEST_USER_CONTEXT_INVALID_VECTOR = 31ULL;
constexpr uint64_t OS_TEST_USER_CONTEXT_LAST_HARDWARE_VECTOR =
    os::kernel::OS_KERNEL_USER_CONTEXT_FIRST_HARDWARE_INTERRUPT_VECTOR +
    os::kernel::OS_KERNEL_USER_CONTEXT_HARDWARE_INTERRUPT_VECTOR_COUNT - 1ULL;

[[nodiscard]] os::kernel::UserContext BuildValidContext(const uint64_t vector) noexcept {
    os::kernel::UserContext context{};
    context.common.vector = vector;
    context.common.instruction_pointer = OS_TEST_USER_CONTEXT_INSTRUCTION_POINTER;
    context.common.code_segment = OS_TEST_USER_CONTEXT_USER_CODE_SEGMENT;
    context.common.flags = OS_TEST_USER_CONTEXT_INITIAL_FLAGS;
    context.stack_pointer = OS_TEST_USER_CONTEXT_STACK_POINTER;
    context.stack_segment = OS_TEST_USER_CONTEXT_USER_STACK_SEGMENT;
    return context;
}

[[nodiscard]] os::kernel::UserContextRequirements BuildRequirements() noexcept {
    return os::kernel::UserContextRequirements{
        .virtual_address_width_bits = OS_TEST_USER_CONTEXT_VIRTUAL_WIDTH_BITS,
        .user_code_segment = OS_TEST_USER_CONTEXT_USER_CODE_SEGMENT,
        .user_stack_segment = OS_TEST_USER_CONTEXT_USER_STACK_SEGMENT,
    };
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_USER_CONTEXT_SUITE_NAME};
    const os::kernel::UserContextRequirements requirements = BuildRequirements();

    const os::kernel::UserContext initial_context =
        BuildValidContext(os::kernel::OS_KERNEL_USER_CONTEXT_INITIAL_VECTOR);
    const os::kernel::UserContext legacy_context =
        BuildValidContext(os::kernel::OS_KERNEL_USER_CONTEXT_LEGACY_SYSTEM_CALL_VECTOR);
    const os::kernel::UserContext native_context =
        BuildValidContext(os::kernel::OS_KERNEL_USER_CONTEXT_NATIVE_SYSTEM_CALL_VECTOR);
    const os::kernel::UserContext first_irq_context =
        BuildValidContext(os::kernel::OS_KERNEL_USER_CONTEXT_FIRST_HARDWARE_INTERRUPT_VECTOR);
    const os::kernel::UserContext last_irq_context =
        BuildValidContext(OS_TEST_USER_CONTEXT_LAST_HARDWARE_VECTOR);
    const os::kernel::UserContext invalid_context =
        BuildValidContext(OS_TEST_USER_CONTEXT_INVALID_VECTOR);
    test_context.Expect(os::kernel::DecodeUserContextEntryMethod(initial_context) ==
                                os::kernel::UserContextEntryMethod::Initial &&
                            os::kernel::DecodeUserContextEntryMethod(legacy_context) ==
                                os::kernel::UserContextEntryMethod::LegacyInterrupt &&
                            os::kernel::DecodeUserContextEntryMethod(native_context) ==
                                os::kernel::UserContextEntryMethod::NativeSystemCall &&
                            os::kernel::DecodeUserContextEntryMethod(first_irq_context) ==
                                os::kernel::UserContextEntryMethod::HardwareInterrupt &&
                            os::kernel::DecodeUserContextEntryMethod(last_irq_context) ==
                                os::kernel::UserContextEntryMethod::HardwareInterrupt &&
                            os::kernel::DecodeUserContextEntryMethod(invalid_context) ==
                                os::kernel::UserContextEntryMethod::Invalid,
                        OS_TEST_USER_CONTEXT_ENTRY_MESSAGE);

    test_context.Expect(
        os::kernel::IsCanonicalVirtualAddress(OS_TEST_USER_CONTEXT_LOWER_CANONICAL_MAXIMUM,
                                              OS_TEST_USER_CONTEXT_VIRTUAL_WIDTH_BITS) &&
            os::kernel::IsLowerCanonicalUserAddress(OS_TEST_USER_CONTEXT_LOWER_CANONICAL_MAXIMUM,
                                                    OS_TEST_USER_CONTEXT_VIRTUAL_WIDTH_BITS) &&
            !os::kernel::IsCanonicalVirtualAddress(OS_TEST_USER_CONTEXT_FIRST_NON_CANONICAL,
                                                   OS_TEST_USER_CONTEXT_VIRTUAL_WIDTH_BITS) &&
            os::kernel::IsCanonicalVirtualAddress(OS_TEST_USER_CONTEXT_UPPER_CANONICAL_MINIMUM,
                                                  OS_TEST_USER_CONTEXT_VIRTUAL_WIDTH_BITS) &&
            !os::kernel::IsLowerCanonicalUserAddress(OS_TEST_USER_CONTEXT_UPPER_CANONICAL_MINIMUM,
                                                     OS_TEST_USER_CONTEXT_VIRTUAL_WIDTH_BITS) &&
            !os::kernel::IsCanonicalVirtualAddress(OS_TEST_USER_CONTEXT_INSTRUCTION_POINTER,
                                                   OS_TEST_USER_CONTEXT_INVALID_VIRTUAL_WIDTH_BITS),
        OS_TEST_USER_CONTEXT_CANONICAL_MESSAGE);

    test_context.Expect(os::kernel::ValidateUserContext(native_context, requirements) ==
                                os::kernel::UserContextStatus::Succeeded &&
                            os::kernel::ValidateUserContext(first_irq_context, requirements) ==
                                os::kernel::UserContextStatus::Succeeded,
                        OS_TEST_USER_CONTEXT_VALIDATION_MESSAGE);

    os::kernel::UserContext changed_context = invalid_context;
    const bool entry_rejected = os::kernel::ValidateUserContext(changed_context, requirements) ==
                                os::kernel::UserContextStatus::InvalidEntryMethod;
    os::kernel::UserContextRequirements changed_requirements = requirements;
    changed_requirements.virtual_address_width_bits =
        OS_TEST_USER_CONTEXT_INVALID_VIRTUAL_WIDTH_BITS;
    const bool width_rejected =
        os::kernel::ValidateUserContext(native_context, changed_requirements) ==
        os::kernel::UserContextStatus::InvalidVirtualAddressWidth;
    changed_context = native_context;
    changed_context.common.instruction_pointer = OS_TEST_USER_CONTEXT_FIRST_NON_CANONICAL;
    const bool instruction_rejected =
        os::kernel::ValidateUserContext(changed_context, requirements) ==
        os::kernel::UserContextStatus::InvalidInstructionPointer;
    changed_context = native_context;
    changed_context.stack_pointer = 0ULL;
    const bool stack_rejected = os::kernel::ValidateUserContext(changed_context, requirements) ==
                                os::kernel::UserContextStatus::InvalidStackPointer;
    changed_context = native_context;
    changed_context.common.code_segment = OS_TEST_USER_CONTEXT_KERNEL_CODE_SEGMENT;
    const bool code_segment_rejected =
        os::kernel::ValidateUserContext(changed_context, requirements) ==
        os::kernel::UserContextStatus::InvalidCodeSegment;
    changed_context = native_context;
    changed_context.stack_segment = OS_TEST_USER_CONTEXT_KERNEL_STACK_SEGMENT;
    const bool stack_segment_rejected =
        os::kernel::ValidateUserContext(changed_context, requirements) ==
        os::kernel::UserContextStatus::InvalidStackSegment;
    changed_context = native_context;
    changed_context.common.flags = OS_TEST_USER_CONTEXT_FORBIDDEN_IO_PRIVILEGE_FLAGS;
    test_context.Expect(entry_rejected && width_rejected && instruction_rejected &&
                            stack_rejected && code_segment_rejected && stack_segment_rejected &&
                            os::kernel::ValidateUserContext(changed_context, requirements) ==
                                os::kernel::UserContextStatus::InvalidFlags,
                        OS_TEST_USER_CONTEXT_REJECTION_MESSAGE);

    os::kernel::UserContext direction_flag_context = native_context;
    direction_flag_context.common.flags |= OS_TEST_USER_CONTEXT_DIRECTION_FLAG;
    os::kernel::UserContext resume_flag_context = native_context;
    resume_flag_context.common.flags |= OS_TEST_USER_CONTEXT_RESUME_FLAG;
    test_context.Expect(
        os::kernel::SelectUserReturnMethod(native_context, requirements) ==
                os::kernel::UserReturnMethod::SystemReturn &&
            os::kernel::SelectUserReturnMethod(legacy_context, requirements) ==
                os::kernel::UserReturnMethod::InterruptReturn &&
            os::kernel::SelectUserReturnMethod(first_irq_context, requirements) ==
                os::kernel::UserReturnMethod::InterruptReturn &&
            os::kernel::SelectUserReturnMethod(direction_flag_context, requirements) ==
                os::kernel::UserReturnMethod::InterruptReturn &&
            os::kernel::SelectUserReturnMethod(resume_flag_context, requirements) ==
                os::kernel::UserReturnMethod::InterruptReturn &&
            os::kernel::SelectUserReturnMethod(invalid_context, requirements) ==
                os::kernel::UserReturnMethod::Rejected,
        OS_TEST_USER_CONTEXT_RETURN_MESSAGE);

    os::kernel::UserContext mutable_context = native_context;
    test_context.Expect(
        &os::kernel::AsUserContext(mutable_context.common) == &mutable_context &&
            &os::kernel::AsUserContext(static_cast<const os::kernel::ExceptionFrame &>(
                mutable_context.common)) == &mutable_context,
        OS_TEST_USER_CONTEXT_LAYOUT_MESSAGE);

    return test_context.ExitCode();
}

#include "os/kernel/arch/native_system_call_layout.hpp"

#include "os/kernel/arch/processor_features.hpp"
#include "os/kernel/arch/user_context.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_NATIVE_SYSTEM_CALL_SELECTOR_PRIVILEGE_MASK = 0x03ULL;
constexpr uint64_t OS_KERNEL_NATIVE_SYSTEM_CALL_SELECTOR_INDEX_MASK = 0xFFFCULL;
constexpr uint64_t OS_KERNEL_NATIVE_SYSTEM_CALL_USER_PRIVILEGE = 0x03ULL;
constexpr uint64_t OS_KERNEL_NATIVE_SYSTEM_CALL_STACK_SELECTOR_INCREMENT = 0x08ULL;
constexpr uint64_t OS_KERNEL_NATIVE_SYSTEM_CALL_CODE_SELECTOR_INCREMENT = 0x10ULL;
constexpr uint64_t OS_KERNEL_NATIVE_SYSTEM_CALL_KERNEL_SELECTOR_SHIFT = 32ULL;
constexpr uint64_t OS_KERNEL_NATIVE_SYSTEM_CALL_USER_SELECTOR_SHIFT = 48ULL;
constexpr uint64_t OS_KERNEL_NATIVE_SYSTEM_CALL_EMPTY_ADDRESS = 0ULL;
constexpr uint64_t OS_KERNEL_NATIVE_SYSTEM_CALL_EMPTY_GS_BASE = 0ULL;

}

NativeSystemCallLayoutStatus BuildNativeSystemCallRegisterValues(
    const uint64_t current_extended_feature_enable_register, const uint64_t kernel_code_segment,
    const uint64_t user_stack_segment, const uint64_t user_code_segment,
    const uint64_t entry_instruction_pointer, const uint64_t cpu_local_address,
    const uint64_t virtual_address_width_bits, NativeSystemCallRegisterValues &values) noexcept {
    if (virtual_address_width_bits != OS_KERNEL_PROCESSOR_REQUIRED_VIRTUAL_ADDRESS_WIDTH_BITS) {
        return NativeSystemCallLayoutStatus::InvalidVirtualAddressWidth;
    }
    const uint64_t kernel_code_selector =
        kernel_code_segment & OS_KERNEL_NATIVE_SYSTEM_CALL_SELECTOR_INDEX_MASK;
    if (kernel_code_selector == OS_KERNEL_NATIVE_SYSTEM_CALL_EMPTY_ADDRESS ||
        (kernel_code_segment & OS_KERNEL_NATIVE_SYSTEM_CALL_SELECTOR_PRIVILEGE_MASK) !=
            OS_KERNEL_NATIVE_SYSTEM_CALL_EMPTY_ADDRESS) {
        return NativeSystemCallLayoutStatus::InvalidKernelCodeSegment;
    }

    const uint64_t user_stack_selector =
        user_stack_segment & OS_KERNEL_NATIVE_SYSTEM_CALL_SELECTOR_INDEX_MASK;
    if ((user_stack_segment & OS_KERNEL_NATIVE_SYSTEM_CALL_SELECTOR_PRIVILEGE_MASK) !=
            OS_KERNEL_NATIVE_SYSTEM_CALL_USER_PRIVILEGE ||
        (user_code_segment & OS_KERNEL_NATIVE_SYSTEM_CALL_SELECTOR_PRIVILEGE_MASK) !=
            OS_KERNEL_NATIVE_SYSTEM_CALL_USER_PRIVILEGE ||
        user_stack_selector < OS_KERNEL_NATIVE_SYSTEM_CALL_STACK_SELECTOR_INCREMENT) {
        return NativeSystemCallLayoutStatus::InvalidUserSegmentOrder;
    }
    const uint64_t system_return_selector =
        user_stack_selector - OS_KERNEL_NATIVE_SYSTEM_CALL_STACK_SELECTOR_INCREMENT;
    const uint64_t expected_user_stack_segment =
        (system_return_selector + OS_KERNEL_NATIVE_SYSTEM_CALL_STACK_SELECTOR_INCREMENT) |
        OS_KERNEL_NATIVE_SYSTEM_CALL_USER_PRIVILEGE;
    const uint64_t expected_user_code_segment =
        (system_return_selector + OS_KERNEL_NATIVE_SYSTEM_CALL_CODE_SELECTOR_INCREMENT) |
        OS_KERNEL_NATIVE_SYSTEM_CALL_USER_PRIVILEGE;
    if (expected_user_stack_segment != user_stack_segment ||
        expected_user_code_segment != user_code_segment) {
        return NativeSystemCallLayoutStatus::InvalidUserSegmentOrder;
    }
    if (entry_instruction_pointer == OS_KERNEL_NATIVE_SYSTEM_CALL_EMPTY_ADDRESS ||
        !IsCanonicalVirtualAddress(entry_instruction_pointer, virtual_address_width_bits)) {
        return NativeSystemCallLayoutStatus::InvalidEntryAddress;
    }
    if (cpu_local_address == OS_KERNEL_NATIVE_SYSTEM_CALL_EMPTY_ADDRESS ||
        !IsCanonicalVirtualAddress(cpu_local_address, virtual_address_width_bits)) {
        return NativeSystemCallLayoutStatus::InvalidCpuLocalAddress;
    }

    values = NativeSystemCallRegisterValues{
        .extended_feature_enable_register =
            current_extended_feature_enable_register | OS_KERNEL_NATIVE_SYSTEM_CALL_EFER_ENABLE_BIT,
        .segment_selector_register =
            (system_return_selector << OS_KERNEL_NATIVE_SYSTEM_CALL_USER_SELECTOR_SHIFT) |
            (kernel_code_selector << OS_KERNEL_NATIVE_SYSTEM_CALL_KERNEL_SELECTOR_SHIFT),
        .entry_instruction_pointer_register = entry_instruction_pointer,
        .flags_mask_register = OS_KERNEL_NATIVE_SYSTEM_CALL_FLAGS_MASK,
        .user_gs_base_register = OS_KERNEL_NATIVE_SYSTEM_CALL_EMPTY_GS_BASE,
        .kernel_gs_base_register = cpu_local_address,
    };
    return NativeSystemCallLayoutStatus::Succeeded;
}

NativeSystemCallLayoutStatus
ValidateNativeSystemCallRegisterValues(const NativeSystemCallRegisterValues &expected,
                                       const NativeSystemCallRegisterValues &observed) noexcept {
    return expected.extended_feature_enable_register == observed.extended_feature_enable_register &&
                   expected.segment_selector_register == observed.segment_selector_register &&
                   expected.entry_instruction_pointer_register ==
                       observed.entry_instruction_pointer_register &&
                   expected.flags_mask_register == observed.flags_mask_register &&
                   expected.user_gs_base_register == observed.user_gs_base_register &&
                   expected.kernel_gs_base_register == observed.kernel_gs_base_register
               ? NativeSystemCallLayoutStatus::Succeeded
               : NativeSystemCallLayoutStatus::InvalidRegisterValues;
}

}

#pragma once

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_NATIVE_SYSTEM_CALL_EFER_ENABLE_BIT = 0x0000000000000001ULL;
inline constexpr uint64_t OS_KERNEL_NATIVE_SYSTEM_CALL_FLAGS_MASK = 0x0000000000044700ULL;

enum class NativeSystemCallLayoutStatus : uint64_t {
    Succeeded,
    InvalidVirtualAddressWidth,
    InvalidKernelCodeSegment,
    InvalidUserSegmentOrder,
    InvalidEntryAddress,
    InvalidCpuLocalAddress,
    InvalidRegisterValues,
};

struct NativeSystemCallRegisterValues final {
    uint64_t extended_feature_enable_register;
    uint64_t segment_selector_register;
    uint64_t entry_instruction_pointer_register;
    uint64_t flags_mask_register;
    uint64_t user_gs_base_register;
    uint64_t kernel_gs_base_register;
};

[[nodiscard]] NativeSystemCallLayoutStatus
BuildNativeSystemCallRegisterValues(uint64_t current_extended_feature_enable_register,
                                    uint64_t kernel_code_segment, uint64_t user_stack_segment,
                                    uint64_t user_code_segment, uint64_t entry_instruction_pointer,
                                    uint64_t cpu_local_address, uint64_t virtual_address_width_bits,
                                    NativeSystemCallRegisterValues &values) noexcept;
[[nodiscard]] NativeSystemCallLayoutStatus
ValidateNativeSystemCallRegisterValues(const NativeSystemCallRegisterValues &expected,
                                       const NativeSystemCallRegisterValues &observed) noexcept;

}

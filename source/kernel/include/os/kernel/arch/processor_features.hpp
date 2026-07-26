#pragma once

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_PROCESSOR_FEATURE_LONG_MODE = 1ULL << 0ULL;
inline constexpr uint64_t OS_KERNEL_PROCESSOR_FEATURE_NO_EXECUTE = 1ULL << 1ULL;
inline constexpr uint64_t OS_KERNEL_PROCESSOR_FEATURE_FX_SAVE_RESTORE = 1ULL << 2ULL;
inline constexpr uint64_t OS_KERNEL_PROCESSOR_FEATURE_STREAMING_SIMD = 1ULL << 3ULL;
inline constexpr uint64_t OS_KERNEL_PROCESSOR_FEATURE_STREAMING_SIMD2 = 1ULL << 4ULL;
inline constexpr uint64_t OS_KERNEL_PROCESSOR_FEATURE_NATIVE_SYSTEM_CALL = 1ULL << 5ULL;
inline constexpr uint64_t OS_KERNEL_PROCESSOR_REQUIRED_FEATURES =
    OS_KERNEL_PROCESSOR_FEATURE_LONG_MODE | OS_KERNEL_PROCESSOR_FEATURE_NO_EXECUTE |
    OS_KERNEL_PROCESSOR_FEATURE_FX_SAVE_RESTORE | OS_KERNEL_PROCESSOR_FEATURE_STREAMING_SIMD |
    OS_KERNEL_PROCESSOR_FEATURE_STREAMING_SIMD2 | OS_KERNEL_PROCESSOR_FEATURE_NATIVE_SYSTEM_CALL;
inline constexpr uint64_t OS_KERNEL_PROCESSOR_REQUIRED_VIRTUAL_ADDRESS_WIDTH_BITS = 48ULL;
inline constexpr uint64_t OS_KERNEL_PROCESSOR_MINIMUM_PHYSICAL_ADDRESS_WIDTH_BITS = 36ULL;
inline constexpr uint64_t OS_KERNEL_PROCESSOR_MAXIMUM_PHYSICAL_ADDRESS_WIDTH_BITS = 52ULL;

enum class ProcessorFeatureStatus : uint64_t {
    Succeeded,
    MissingRequiredFeature,
    InvalidPhysicalAddressWidth,
    InvalidVirtualAddressWidth,
};

struct ProcessorFeatureLeaves final {
    uint32_t standard_maximum_leaf;
    uint32_t standard_feature_data;
    uint32_t extended_maximum_leaf;
    uint32_t extended_feature_data;
    uint32_t address_widths_accumulator;
};

struct ProcessorFeatureProfile final {
    uint64_t available_feature_mask;
    uint64_t missing_feature_mask;
    uint64_t physical_address_width_bits;
    uint64_t virtual_address_width_bits;
    bool long_mode;
    bool no_execute;
    bool fx_save_restore;
    bool streaming_simd;
    bool streaming_simd2;
    bool native_system_call;
};

[[nodiscard]] ProcessorFeatureProfile
DecodeProcessorFeatureProfile(const ProcessorFeatureLeaves &leaves) noexcept;
[[nodiscard]] ProcessorFeatureStatus
ValidateProcessorFeatureProfile(const ProcessorFeatureProfile &profile) noexcept;

}

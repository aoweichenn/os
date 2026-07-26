#include "os/kernel/arch/processor_features.hpp"

namespace os::kernel {

namespace {

constexpr uint32_t OS_KERNEL_PROCESSOR_FEATURE_STANDARD_FEATURES_LEAF = 0x00000001U;
constexpr uint32_t OS_KERNEL_PROCESSOR_FEATURE_EXTENDED_FEATURES_LEAF = 0x80000001U;
constexpr uint32_t OS_KERNEL_PROCESSOR_FEATURE_ADDRESS_WIDTHS_LEAF = 0x80000008U;
constexpr uint32_t OS_KERNEL_PROCESSOR_FEATURE_FX_SAVE_RESTORE_BIT = 0x01000000U;
constexpr uint32_t OS_KERNEL_PROCESSOR_FEATURE_STREAMING_SIMD_BIT = 0x02000000U;
constexpr uint32_t OS_KERNEL_PROCESSOR_FEATURE_STREAMING_SIMD2_BIT = 0x04000000U;
constexpr uint32_t OS_KERNEL_PROCESSOR_FEATURE_NATIVE_SYSTEM_CALL_BIT = 0x00000800U;
constexpr uint32_t OS_KERNEL_PROCESSOR_FEATURE_NO_EXECUTE_BIT = 0x00100000U;
constexpr uint32_t OS_KERNEL_PROCESSOR_FEATURE_LONG_MODE_BIT = 0x20000000U;
constexpr uint32_t OS_KERNEL_PROCESSOR_FEATURE_PHYSICAL_ADDRESS_WIDTH_MASK = 0x000000FFU;
constexpr uint32_t OS_KERNEL_PROCESSOR_FEATURE_VIRTUAL_ADDRESS_WIDTH_MASK = 0x0000FF00U;
constexpr uint64_t OS_KERNEL_PROCESSOR_FEATURE_VIRTUAL_ADDRESS_WIDTH_SHIFT = 8ULL;
constexpr uint64_t OS_KERNEL_PROCESSOR_FEATURE_EMPTY_MASK = 0ULL;

}

ProcessorFeatureProfile
DecodeProcessorFeatureProfile(const ProcessorFeatureLeaves &leaves) noexcept {
    const bool standard_features_available =
        leaves.standard_maximum_leaf >= OS_KERNEL_PROCESSOR_FEATURE_STANDARD_FEATURES_LEAF;
    const bool extended_features_available =
        leaves.extended_maximum_leaf >= OS_KERNEL_PROCESSOR_FEATURE_EXTENDED_FEATURES_LEAF;
    const bool address_widths_available =
        leaves.extended_maximum_leaf >= OS_KERNEL_PROCESSOR_FEATURE_ADDRESS_WIDTHS_LEAF;

    const bool long_mode =
        extended_features_available &&
        (leaves.extended_feature_data & OS_KERNEL_PROCESSOR_FEATURE_LONG_MODE_BIT) != 0U;
    const bool no_execute =
        extended_features_available &&
        (leaves.extended_feature_data & OS_KERNEL_PROCESSOR_FEATURE_NO_EXECUTE_BIT) != 0U;
    const bool fx_save_restore =
        standard_features_available &&
        (leaves.standard_feature_data & OS_KERNEL_PROCESSOR_FEATURE_FX_SAVE_RESTORE_BIT) != 0U;
    const bool streaming_simd =
        standard_features_available &&
        (leaves.standard_feature_data & OS_KERNEL_PROCESSOR_FEATURE_STREAMING_SIMD_BIT) != 0U;
    const bool streaming_simd2 =
        standard_features_available &&
        (leaves.standard_feature_data & OS_KERNEL_PROCESSOR_FEATURE_STREAMING_SIMD2_BIT) != 0U;
    const bool native_system_call =
        extended_features_available &&
        (leaves.extended_feature_data & OS_KERNEL_PROCESSOR_FEATURE_NATIVE_SYSTEM_CALL_BIT) != 0U;

    uint64_t available_feature_mask = OS_KERNEL_PROCESSOR_FEATURE_EMPTY_MASK;
    if (long_mode) {
        available_feature_mask |= OS_KERNEL_PROCESSOR_FEATURE_LONG_MODE;
    }
    if (no_execute) {
        available_feature_mask |= OS_KERNEL_PROCESSOR_FEATURE_NO_EXECUTE;
    }
    if (fx_save_restore) {
        available_feature_mask |= OS_KERNEL_PROCESSOR_FEATURE_FX_SAVE_RESTORE;
    }
    if (streaming_simd) {
        available_feature_mask |= OS_KERNEL_PROCESSOR_FEATURE_STREAMING_SIMD;
    }
    if (streaming_simd2) {
        available_feature_mask |= OS_KERNEL_PROCESSOR_FEATURE_STREAMING_SIMD2;
    }
    if (native_system_call) {
        available_feature_mask |= OS_KERNEL_PROCESSOR_FEATURE_NATIVE_SYSTEM_CALL;
    }

    const uint64_t physical_address_width_bits =
        address_widths_available ? leaves.address_widths_accumulator &
                                       OS_KERNEL_PROCESSOR_FEATURE_PHYSICAL_ADDRESS_WIDTH_MASK
                                 : OS_KERNEL_PROCESSOR_FEATURE_EMPTY_MASK;
    const uint64_t virtual_address_width_bits =
        address_widths_available ? (leaves.address_widths_accumulator &
                                    OS_KERNEL_PROCESSOR_FEATURE_VIRTUAL_ADDRESS_WIDTH_MASK) >>
                                       OS_KERNEL_PROCESSOR_FEATURE_VIRTUAL_ADDRESS_WIDTH_SHIFT
                                 : OS_KERNEL_PROCESSOR_FEATURE_EMPTY_MASK;

    return ProcessorFeatureProfile{
        .available_feature_mask = available_feature_mask,
        .missing_feature_mask = OS_KERNEL_PROCESSOR_REQUIRED_FEATURES & ~available_feature_mask,
        .physical_address_width_bits = physical_address_width_bits,
        .virtual_address_width_bits = virtual_address_width_bits,
        .long_mode = long_mode,
        .no_execute = no_execute,
        .fx_save_restore = fx_save_restore,
        .streaming_simd = streaming_simd,
        .streaming_simd2 = streaming_simd2,
        .native_system_call = native_system_call,
    };
}

ProcessorFeatureStatus
ValidateProcessorFeatureProfile(const ProcessorFeatureProfile &profile) noexcept {
    if (profile.missing_feature_mask != OS_KERNEL_PROCESSOR_FEATURE_EMPTY_MASK) {
        return ProcessorFeatureStatus::MissingRequiredFeature;
    }
    if (profile.physical_address_width_bits <
            OS_KERNEL_PROCESSOR_MINIMUM_PHYSICAL_ADDRESS_WIDTH_BITS ||
        profile.physical_address_width_bits >
            OS_KERNEL_PROCESSOR_MAXIMUM_PHYSICAL_ADDRESS_WIDTH_BITS) {
        return ProcessorFeatureStatus::InvalidPhysicalAddressWidth;
    }
    if (profile.virtual_address_width_bits !=
        OS_KERNEL_PROCESSOR_REQUIRED_VIRTUAL_ADDRESS_WIDTH_BITS) {
        return ProcessorFeatureStatus::InvalidVirtualAddressWidth;
    }
    return ProcessorFeatureStatus::Succeeded;
}

}

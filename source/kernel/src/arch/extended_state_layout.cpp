#include "os/kernel/arch/extended_state_layout.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_EXTENDED_STATE_EMPTY_REMAINDER = 0ULL;
constexpr uint32_t OS_KERNEL_EXTENDED_STATE_FEATURE_ABSENT = 0U;

}

ExtendedStateFeatures
DecodeExtendedStateFeatures(const uint32_t standard_feature_bits) noexcept {
    return ExtendedStateFeatures{
        .fx_save_restore =
            (standard_feature_bits & OS_KERNEL_EXTENDED_STATE_CPUID_FXSR_BIT) !=
            OS_KERNEL_EXTENDED_STATE_FEATURE_ABSENT,
        .sse = (standard_feature_bits & OS_KERNEL_EXTENDED_STATE_CPUID_SSE_BIT) !=
               OS_KERNEL_EXTENDED_STATE_FEATURE_ABSENT,
        .sse2 =
            (standard_feature_bits & OS_KERNEL_EXTENDED_STATE_CPUID_SSE2_BIT) !=
            OS_KERNEL_EXTENDED_STATE_FEATURE_ABSENT,
    };
}

bool RequiredExtendedStateFeaturesAvailable(
    const ExtendedStateFeatures &features) noexcept {
    return features.fx_save_restore && features.sse && features.sse2;
}

bool IsFxSaveAreaAligned(const FxSaveArea &area) noexcept {
    const uint64_t area_address = reinterpret_cast<uint64_t>(&area);
    return area_address % OS_KERNEL_EXTENDED_STATE_AREA_ALIGNMENT_BYTES ==
           OS_KERNEL_EXTENDED_STATE_EMPTY_REMAINDER;
}

}

#pragma once

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_EXTENDED_STATE_AREA_SIZE_BYTES = 512ULL;
inline constexpr uint64_t OS_KERNEL_EXTENDED_STATE_AREA_ALIGNMENT_BYTES = 16ULL;
inline constexpr uint32_t OS_KERNEL_EXTENDED_STATE_CPUID_FXSR_BIT = 0x01000000U;
inline constexpr uint32_t OS_KERNEL_EXTENDED_STATE_CPUID_SSE_BIT = 0x02000000U;
inline constexpr uint32_t OS_KERNEL_EXTENDED_STATE_CPUID_SSE2_BIT = 0x04000000U;

struct alignas(OS_KERNEL_EXTENDED_STATE_AREA_ALIGNMENT_BYTES) FxSaveArea final {
    uint8_t bytes[OS_KERNEL_EXTENDED_STATE_AREA_SIZE_BYTES];
};

struct ExtendedStateFeatures final {
    bool fx_save_restore;
    bool sse;
    bool sse2;
};

[[nodiscard]] ExtendedStateFeatures
DecodeExtendedStateFeatures(uint32_t standard_feature_bits) noexcept;
[[nodiscard]] bool
RequiredExtendedStateFeaturesAvailable(const ExtendedStateFeatures &features) noexcept;
[[nodiscard]] bool IsFxSaveAreaAligned(const FxSaveArea &area) noexcept;

static_assert(sizeof(FxSaveArea) == OS_KERNEL_EXTENDED_STATE_AREA_SIZE_BYTES);
static_assert(alignof(FxSaveArea) ==
              OS_KERNEL_EXTENDED_STATE_AREA_ALIGNMENT_BYTES);

}

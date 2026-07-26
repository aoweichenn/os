#include "os/kernel/arch/extended_state_layout.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_EXTENDED_STATE_SUITE_NAME =
    "kernel/extended_state_layout/unit";
constexpr std::string_view OS_TEST_EXTENDED_STATE_LAYOUT =
    "FXSAVE 区域必须固定为 512 字节并满足 16 字节硬件对齐";
constexpr std::string_view OS_TEST_EXTENDED_STATE_FEATURE_DECODE =
    "CPUID FXSR、SSE 和 SSE2 位必须独立解码且忽略无关位";
constexpr std::string_view OS_TEST_EXTENDED_STATE_REQUIRED_FEATURES =
    "只有 FXSR、SSE 与 SSE2 全部存在时才能启用完整扩展现场";
constexpr uint32_t OS_TEST_EXTENDED_STATE_UNRELATED_FEATURE_BIT =
    0x80000000U;
constexpr uint32_t OS_TEST_EXTENDED_STATE_ALL_REQUIRED_FEATURE_BITS =
    os::kernel::OS_KERNEL_EXTENDED_STATE_CPUID_FXSR_BIT |
    os::kernel::OS_KERNEL_EXTENDED_STATE_CPUID_SSE_BIT |
    os::kernel::OS_KERNEL_EXTENDED_STATE_CPUID_SSE2_BIT;

}

int main() {
    os::test::TestContext test_context{
        OS_TEST_EXTENDED_STATE_SUITE_NAME};
    os::kernel::FxSaveArea area{};
    test_context.Expect(
        sizeof(area) ==
                os::kernel::OS_KERNEL_EXTENDED_STATE_AREA_SIZE_BYTES &&
            alignof(os::kernel::FxSaveArea) ==
                os::kernel::OS_KERNEL_EXTENDED_STATE_AREA_ALIGNMENT_BYTES &&
            os::kernel::IsFxSaveAreaAligned(area),
        OS_TEST_EXTENDED_STATE_LAYOUT);

    const os::kernel::ExtendedStateFeatures no_features =
        os::kernel::DecodeExtendedStateFeatures(
            OS_TEST_EXTENDED_STATE_UNRELATED_FEATURE_BIT);
    const os::kernel::ExtendedStateFeatures all_features =
        os::kernel::DecodeExtendedStateFeatures(
            OS_TEST_EXTENDED_STATE_ALL_REQUIRED_FEATURE_BITS |
            OS_TEST_EXTENDED_STATE_UNRELATED_FEATURE_BIT);
    test_context.Expect(
        !no_features.fx_save_restore && !no_features.sse &&
            !no_features.sse2 && all_features.fx_save_restore &&
            all_features.sse && all_features.sse2,
        OS_TEST_EXTENDED_STATE_FEATURE_DECODE);

    const os::kernel::ExtendedStateFeatures missing_sse2 =
        os::kernel::DecodeExtendedStateFeatures(
            OS_TEST_EXTENDED_STATE_ALL_REQUIRED_FEATURE_BITS &
            ~os::kernel::OS_KERNEL_EXTENDED_STATE_CPUID_SSE2_BIT);
    test_context.Expect(
        os::kernel::RequiredExtendedStateFeaturesAvailable(all_features) &&
            !os::kernel::RequiredExtendedStateFeaturesAvailable(
                missing_sse2),
        OS_TEST_EXTENDED_STATE_REQUIRED_FEATURES);
    return test_context.ExitCode();
}

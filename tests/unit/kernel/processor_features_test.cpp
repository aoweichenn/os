#include "os/kernel/arch/processor_features.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_PROCESSOR_FEATURES_SUITE_NAME = "kernel/processor_features/unit";
constexpr std::string_view OS_TEST_PROCESSOR_FEATURES_DECODE_MESSAGE =
    "完整 CPUID 叶必须解码出冻结的 v1.3 处理器能力";
constexpr std::string_view OS_TEST_PROCESSOR_FEATURES_MISSING_MESSAGE =
    "缺少 SYSCALL/SYSRET 时必须给出精确缺失位";
constexpr std::string_view OS_TEST_PROCESSOR_FEATURES_LEAF_MESSAGE =
    "CPUID 叶不存在时不得误报任何能力";
constexpr std::string_view OS_TEST_PROCESSOR_FEATURES_PHYSICAL_WIDTH_MESSAGE =
    "物理地址宽度必须限制在页表实现支持的 36 至 52 位";
constexpr std::string_view OS_TEST_PROCESSOR_FEATURES_VIRTUAL_WIDTH_MESSAGE =
    "v1.3 必须冻结在四级分页对应的 48 位虚拟地址";

constexpr uint32_t OS_TEST_PROCESSOR_FEATURES_STANDARD_MAXIMUM_LEAF = 0x00000001U;
constexpr uint32_t OS_TEST_PROCESSOR_FEATURES_STANDARD_FEATURE_DATA = 0x07000000U;
constexpr uint32_t OS_TEST_PROCESSOR_FEATURES_EXTENDED_MAXIMUM_LEAF = 0x80000008U;
constexpr uint32_t OS_TEST_PROCESSOR_FEATURES_EXTENDED_FEATURE_DATA = 0x20100800U;
constexpr uint32_t OS_TEST_PROCESSOR_FEATURES_ADDRESS_WIDTHS = 0x00003028U;
constexpr uint32_t OS_TEST_PROCESSOR_FEATURES_NATIVE_SYSTEM_CALL_BIT = 0x00000800U;
constexpr uint64_t OS_TEST_PROCESSOR_FEATURES_EXPECTED_PHYSICAL_WIDTH_BITS = 40ULL;
constexpr uint64_t OS_TEST_PROCESSOR_FEATURES_EXPECTED_VIRTUAL_WIDTH_BITS = 48ULL;
constexpr uint64_t OS_TEST_PROCESSOR_FEATURES_TOO_SMALL_PHYSICAL_WIDTH_BITS = 35ULL;
constexpr uint64_t OS_TEST_PROCESSOR_FEATURES_TOO_LARGE_PHYSICAL_WIDTH_BITS = 53ULL;
constexpr uint64_t OS_TEST_PROCESSOR_FEATURES_UNSUPPORTED_VIRTUAL_WIDTH_BITS = 57ULL;
constexpr uint32_t OS_TEST_PROCESSOR_FEATURES_EMPTY_LEAF = 0U;

[[nodiscard]] os::kernel::ProcessorFeatureLeaves BuildCompleteLeaves() noexcept {
    return os::kernel::ProcessorFeatureLeaves{
        .standard_maximum_leaf = OS_TEST_PROCESSOR_FEATURES_STANDARD_MAXIMUM_LEAF,
        .standard_feature_data = OS_TEST_PROCESSOR_FEATURES_STANDARD_FEATURE_DATA,
        .extended_maximum_leaf = OS_TEST_PROCESSOR_FEATURES_EXTENDED_MAXIMUM_LEAF,
        .extended_feature_data = OS_TEST_PROCESSOR_FEATURES_EXTENDED_FEATURE_DATA,
        .address_widths_accumulator = OS_TEST_PROCESSOR_FEATURES_ADDRESS_WIDTHS,
    };
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_PROCESSOR_FEATURES_SUITE_NAME};

    const os::kernel::ProcessorFeatureProfile complete_profile =
        os::kernel::DecodeProcessorFeatureProfile(BuildCompleteLeaves());
    test_context.Expect(complete_profile.available_feature_mask ==
                                os::kernel::OS_KERNEL_PROCESSOR_REQUIRED_FEATURES &&
                            complete_profile.missing_feature_mask == 0ULL &&
                            complete_profile.physical_address_width_bits ==
                                OS_TEST_PROCESSOR_FEATURES_EXPECTED_PHYSICAL_WIDTH_BITS &&
                            complete_profile.virtual_address_width_bits ==
                                OS_TEST_PROCESSOR_FEATURES_EXPECTED_VIRTUAL_WIDTH_BITS &&
                            complete_profile.long_mode && complete_profile.no_execute &&
                            complete_profile.fx_save_restore && complete_profile.streaming_simd &&
                            complete_profile.streaming_simd2 &&
                            complete_profile.native_system_call &&
                            os::kernel::ValidateProcessorFeatureProfile(complete_profile) ==
                                os::kernel::ProcessorFeatureStatus::Succeeded,
                        OS_TEST_PROCESSOR_FEATURES_DECODE_MESSAGE);

    os::kernel::ProcessorFeatureLeaves missing_native_leaves = BuildCompleteLeaves();
    missing_native_leaves.extended_feature_data &=
        ~OS_TEST_PROCESSOR_FEATURES_NATIVE_SYSTEM_CALL_BIT;
    const os::kernel::ProcessorFeatureProfile missing_native_profile =
        os::kernel::DecodeProcessorFeatureProfile(missing_native_leaves);
    test_context.Expect(!missing_native_profile.native_system_call &&
                            missing_native_profile.missing_feature_mask ==
                                os::kernel::OS_KERNEL_PROCESSOR_FEATURE_NATIVE_SYSTEM_CALL &&
                            os::kernel::ValidateProcessorFeatureProfile(missing_native_profile) ==
                                os::kernel::ProcessorFeatureStatus::MissingRequiredFeature,
                        OS_TEST_PROCESSOR_FEATURES_MISSING_MESSAGE);

    const os::kernel::ProcessorFeatureProfile empty_profile =
        os::kernel::DecodeProcessorFeatureProfile(os::kernel::ProcessorFeatureLeaves{
            .standard_maximum_leaf = OS_TEST_PROCESSOR_FEATURES_EMPTY_LEAF,
            .standard_feature_data = OS_TEST_PROCESSOR_FEATURES_EMPTY_LEAF,
            .extended_maximum_leaf = OS_TEST_PROCESSOR_FEATURES_EMPTY_LEAF,
            .extended_feature_data = OS_TEST_PROCESSOR_FEATURES_EMPTY_LEAF,
            .address_widths_accumulator = OS_TEST_PROCESSOR_FEATURES_EMPTY_LEAF,
        });
    test_context.Expect(empty_profile.available_feature_mask == 0ULL &&
                            empty_profile.missing_feature_mask ==
                                os::kernel::OS_KERNEL_PROCESSOR_REQUIRED_FEATURES &&
                            empty_profile.physical_address_width_bits == 0ULL &&
                            empty_profile.virtual_address_width_bits == 0ULL,
                        OS_TEST_PROCESSOR_FEATURES_LEAF_MESSAGE);

    os::kernel::ProcessorFeatureProfile invalid_width_profile = complete_profile;
    invalid_width_profile.physical_address_width_bits =
        OS_TEST_PROCESSOR_FEATURES_TOO_SMALL_PHYSICAL_WIDTH_BITS;
    const bool small_width_rejected =
        os::kernel::ValidateProcessorFeatureProfile(invalid_width_profile) ==
        os::kernel::ProcessorFeatureStatus::InvalidPhysicalAddressWidth;
    invalid_width_profile.physical_address_width_bits =
        OS_TEST_PROCESSOR_FEATURES_TOO_LARGE_PHYSICAL_WIDTH_BITS;
    test_context.Expect(small_width_rejected &&
                            os::kernel::ValidateProcessorFeatureProfile(invalid_width_profile) ==
                                os::kernel::ProcessorFeatureStatus::InvalidPhysicalAddressWidth,
                        OS_TEST_PROCESSOR_FEATURES_PHYSICAL_WIDTH_MESSAGE);

    invalid_width_profile = complete_profile;
    invalid_width_profile.virtual_address_width_bits =
        OS_TEST_PROCESSOR_FEATURES_UNSUPPORTED_VIRTUAL_WIDTH_BITS;
    test_context.Expect(os::kernel::ValidateProcessorFeatureProfile(invalid_width_profile) ==
                            os::kernel::ProcessorFeatureStatus::InvalidVirtualAddressWidth,
                        OS_TEST_PROCESSOR_FEATURES_VIRTUAL_WIDTH_MESSAGE);

    return test_context.ExitCode();
}

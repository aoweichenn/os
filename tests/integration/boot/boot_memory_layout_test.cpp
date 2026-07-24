#include "os/foundation/address_range.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_INTEGRATION_SUITE_NAME =
    "foundation/boot_memory_layout/integration";
constexpr std::string_view OS_TEST_INTEGRATION_FIRMWARE_CREATION = "固件 ROM 区间应可表示";
constexpr std::string_view OS_TEST_INTEGRATION_STAGE1_CREATION = "Stage 1 加载区间应可表示";
constexpr std::string_view OS_TEST_INTEGRATION_KERNEL_CREATION = "内核加载区间应可表示";
constexpr std::string_view OS_TEST_INTEGRATION_RESET_VECTOR_IN_FIRMWARE =
    "CPU 复位向量必须位于固件 ROM 区间";
constexpr std::string_view OS_TEST_INTEGRATION_STAGE1_FIRMWARE_SEPARATION =
    "Stage 1 加载区间不得与固件 ROM 重叠";
constexpr std::string_view OS_TEST_INTEGRATION_KERNEL_FIRMWARE_SEPARATION =
    "内核加载区间不得与固件 ROM 重叠";
constexpr std::string_view OS_TEST_INTEGRATION_STAGE1_KERNEL_SEPARATION =
    "Stage 1 加载区间不得与内核加载区间重叠";

constexpr os::foundation::AddressValue OS_TEST_INTEGRATION_FIRMWARE_BEGIN =
    os::foundation::AddressValue{0xFFFE0000};
constexpr os::foundation::AddressValue OS_TEST_INTEGRATION_FIRMWARE_SIZE =
    os::foundation::AddressValue{0x00020000};
constexpr os::foundation::AddressValue OS_TEST_INTEGRATION_RESET_VECTOR =
    os::foundation::AddressValue{0xFFFFFFF0};
constexpr os::foundation::AddressValue OS_TEST_INTEGRATION_STAGE1_BEGIN =
    os::foundation::AddressValue{0x00007C00};
constexpr os::foundation::AddressValue OS_TEST_INTEGRATION_STAGE1_SIZE =
    os::foundation::AddressValue{0x00008000};
constexpr os::foundation::AddressValue OS_TEST_INTEGRATION_KERNEL_BEGIN =
    os::foundation::AddressValue{0x00100000};
constexpr os::foundation::AddressValue OS_TEST_INTEGRATION_KERNEL_SIZE =
    os::foundation::AddressValue{0x00200000};

}

auto main() -> int {
    os::test::TestContext testContext{OS_TEST_INTEGRATION_SUITE_NAME};

    os::foundation::AddressRange firmwareRange{};
    const auto firmwareStatus = os::foundation::AddressRange::tryCreate(
        os::foundation::PhysicalAddress{OS_TEST_INTEGRATION_FIRMWARE_BEGIN},
        os::foundation::ByteCount{OS_TEST_INTEGRATION_FIRMWARE_SIZE}, firmwareRange);
    testContext.expect(firmwareStatus == os::foundation::AddressRangeCreationStatus::Succeeded,
                       OS_TEST_INTEGRATION_FIRMWARE_CREATION);

    os::foundation::AddressRange stage1Range{};
    const auto stage1Status = os::foundation::AddressRange::tryCreate(
        os::foundation::PhysicalAddress{OS_TEST_INTEGRATION_STAGE1_BEGIN},
        os::foundation::ByteCount{OS_TEST_INTEGRATION_STAGE1_SIZE}, stage1Range);
    testContext.expect(stage1Status == os::foundation::AddressRangeCreationStatus::Succeeded,
                       OS_TEST_INTEGRATION_STAGE1_CREATION);

    os::foundation::AddressRange kernelRange{};
    const auto kernelStatus = os::foundation::AddressRange::tryCreate(
        os::foundation::PhysicalAddress{OS_TEST_INTEGRATION_KERNEL_BEGIN},
        os::foundation::ByteCount{OS_TEST_INTEGRATION_KERNEL_SIZE}, kernelRange);
    testContext.expect(kernelStatus == os::foundation::AddressRangeCreationStatus::Succeeded,
                       OS_TEST_INTEGRATION_KERNEL_CREATION);

    testContext.expect(
        firmwareRange.contains(os::foundation::PhysicalAddress{OS_TEST_INTEGRATION_RESET_VECTOR}),
        OS_TEST_INTEGRATION_RESET_VECTOR_IN_FIRMWARE);
    testContext.expect(!stage1Range.overlaps(firmwareRange),
                       OS_TEST_INTEGRATION_STAGE1_FIRMWARE_SEPARATION);
    testContext.expect(!kernelRange.overlaps(firmwareRange),
                       OS_TEST_INTEGRATION_KERNEL_FIRMWARE_SEPARATION);
    testContext.expect(!stage1Range.overlaps(kernelRange),
                       OS_TEST_INTEGRATION_STAGE1_KERNEL_SEPARATION);

    return testContext.exitCode();
}

#include "os/foundation/address_range.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_INTEGRATION_SUITE_NAME =
    "foundation/boot_memory_layout/integration";
constexpr std::string_view OS_TEST_INTEGRATION_FIRMWARE_CREATION = "固件 ROM 区间应可表示";
constexpr std::string_view OS_TEST_INTEGRATION_STAGE1_CREATION = "Stage 1 加载区间应可表示";
constexpr std::string_view OS_TEST_INTEGRATION_KERNEL_CREATION = "内核加载区间应可表示";
constexpr std::string_view OS_TEST_INTEGRATION_PAGE_TABLES_CREATION = "早期页表区间应可表示";
constexpr std::string_view OS_TEST_INTEGRATION_RESET_VECTOR_IN_FIRMWARE =
    "CPU 复位向量必须位于固件 ROM 区间";
constexpr std::string_view OS_TEST_INTEGRATION_STAGE1_FIRMWARE_SEPARATION =
    "Stage 1 加载区间不得与固件 ROM 重叠";
constexpr std::string_view OS_TEST_INTEGRATION_KERNEL_FIRMWARE_SEPARATION =
    "内核加载区间不得与固件 ROM 重叠";
constexpr std::string_view OS_TEST_INTEGRATION_STAGE1_KERNEL_SEPARATION =
    "Stage 1 加载区间不得与内核加载区间重叠";
constexpr std::string_view OS_TEST_INTEGRATION_STAGE1_PAGE_TABLES_SEPARATION =
    "Stage 1 最大加载区间不得与早期页表重叠";
constexpr std::string_view OS_TEST_INTEGRATION_PAGE_TABLES_KERNEL_SEPARATION =
    "早期页表不得与内核加载区间重叠";

constexpr os::foundation::AddressValue OS_TEST_INTEGRATION_FIRMWARE_BEGIN =
    os::foundation::AddressValue{0xFFFE0000};
constexpr os::foundation::AddressValue OS_TEST_INTEGRATION_FIRMWARE_SIZE =
    os::foundation::AddressValue{0x00020000};
constexpr os::foundation::AddressValue OS_TEST_INTEGRATION_RESET_VECTOR =
    os::foundation::AddressValue{0xFFFFFFF0};
constexpr os::foundation::AddressValue OS_TEST_INTEGRATION_STAGE1_BEGIN =
    os::foundation::AddressValue{0x00008000};
constexpr os::foundation::AddressValue OS_TEST_INTEGRATION_STAGE1_SIZE =
    os::foundation::AddressValue{0x00008000};
constexpr os::foundation::AddressValue OS_TEST_INTEGRATION_PAGE_TABLES_BEGIN =
    os::foundation::AddressValue{0x00010000};
constexpr os::foundation::AddressValue OS_TEST_INTEGRATION_PAGE_TABLES_SIZE =
    os::foundation::AddressValue{0x00003000};
constexpr os::foundation::AddressValue OS_TEST_INTEGRATION_KERNEL_BEGIN =
    os::foundation::AddressValue{0x00100000};
constexpr os::foundation::AddressValue OS_TEST_INTEGRATION_KERNEL_SIZE =
    os::foundation::AddressValue{0x00200000};

}

int main() {
    os::test::TestContext testContext{OS_TEST_INTEGRATION_SUITE_NAME};

    os::foundation::AddressRange firmwareRange{};
    const os::foundation::AddressRangeCreationStatus firmwareStatus =
        os::foundation::AddressRange::TryCreate(
            os::foundation::PhysicalAddress{OS_TEST_INTEGRATION_FIRMWARE_BEGIN},
            os::foundation::ByteCount{OS_TEST_INTEGRATION_FIRMWARE_SIZE}, firmwareRange);
    testContext.Expect(firmwareStatus == os::foundation::AddressRangeCreationStatus::Succeeded,
                       OS_TEST_INTEGRATION_FIRMWARE_CREATION);

    os::foundation::AddressRange stage1Range{};
    const os::foundation::AddressRangeCreationStatus stage1Status =
        os::foundation::AddressRange::TryCreate(
            os::foundation::PhysicalAddress{OS_TEST_INTEGRATION_STAGE1_BEGIN},
            os::foundation::ByteCount{OS_TEST_INTEGRATION_STAGE1_SIZE}, stage1Range);
    testContext.Expect(stage1Status == os::foundation::AddressRangeCreationStatus::Succeeded,
                       OS_TEST_INTEGRATION_STAGE1_CREATION);

    os::foundation::AddressRange pageTablesRange{};
    const os::foundation::AddressRangeCreationStatus pageTablesStatus =
        os::foundation::AddressRange::TryCreate(
            os::foundation::PhysicalAddress{OS_TEST_INTEGRATION_PAGE_TABLES_BEGIN},
            os::foundation::ByteCount{OS_TEST_INTEGRATION_PAGE_TABLES_SIZE}, pageTablesRange);
    testContext.Expect(pageTablesStatus == os::foundation::AddressRangeCreationStatus::Succeeded,
                       OS_TEST_INTEGRATION_PAGE_TABLES_CREATION);

    os::foundation::AddressRange kernelRange{};
    const os::foundation::AddressRangeCreationStatus kernelStatus =
        os::foundation::AddressRange::TryCreate(
            os::foundation::PhysicalAddress{OS_TEST_INTEGRATION_KERNEL_BEGIN},
            os::foundation::ByteCount{OS_TEST_INTEGRATION_KERNEL_SIZE}, kernelRange);
    testContext.Expect(kernelStatus == os::foundation::AddressRangeCreationStatus::Succeeded,
                       OS_TEST_INTEGRATION_KERNEL_CREATION);

    testContext.Expect(
        firmwareRange.Contains(os::foundation::PhysicalAddress{OS_TEST_INTEGRATION_RESET_VECTOR}),
        OS_TEST_INTEGRATION_RESET_VECTOR_IN_FIRMWARE);
    testContext.Expect(!stage1Range.Overlaps(firmwareRange),
                       OS_TEST_INTEGRATION_STAGE1_FIRMWARE_SEPARATION);
    testContext.Expect(!kernelRange.Overlaps(firmwareRange),
                       OS_TEST_INTEGRATION_KERNEL_FIRMWARE_SEPARATION);
    testContext.Expect(!stage1Range.Overlaps(kernelRange),
                       OS_TEST_INTEGRATION_STAGE1_KERNEL_SEPARATION);
    testContext.Expect(!stage1Range.Overlaps(pageTablesRange),
                       OS_TEST_INTEGRATION_STAGE1_PAGE_TABLES_SEPARATION);
    testContext.Expect(!pageTablesRange.Overlaps(kernelRange),
                       OS_TEST_INTEGRATION_PAGE_TABLES_KERNEL_SEPARATION);

    return testContext.ExitCode();
}

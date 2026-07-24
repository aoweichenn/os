#include "os/foundation/address_range.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_HANDOFF_LAYOUT_SUITE_NAME =
    "boot/kernel_handoff_layout/integration";
constexpr std::string_view OS_TEST_HANDOFF_LAYOUT_RANGE_CREATION = "所有交接内存区间都必须可表示";
constexpr std::string_view OS_TEST_HANDOFF_LAYOUT_STAGE1_PAGE_TABLES = "Stage 1 不得覆盖早期页表";
constexpr std::string_view OS_TEST_HANDOFF_LAYOUT_PAGE_TABLES_DESCRIPTOR =
    "早期页表不得覆盖 Kernel 描述符";
constexpr std::string_view OS_TEST_HANDOFF_LAYOUT_DESCRIPTOR_BOOT_INFO =
    "Kernel 描述符不得覆盖 BootInfo";
constexpr std::string_view OS_TEST_HANDOFF_LAYOUT_BOOT_INFO_SCRATCH =
    "BootInfo 不得覆盖加载器工作区";
constexpr std::string_view OS_TEST_HANDOFF_LAYOUT_SCRATCH_METADATA =
    "加载器工作区不得覆盖内存图元数据";
constexpr std::string_view OS_TEST_HANDOFF_LAYOUT_METADATA_FW_CFG =
    "内存图元数据不得覆盖 fw_cfg 暂存区";
constexpr std::string_view OS_TEST_HANDOFF_LAYOUT_FW_CFG_MEMORY_MAP =
    "fw_cfg 暂存区不得覆盖物理内存图";
constexpr std::string_view OS_TEST_HANDOFF_LAYOUT_MEMORY_MAP_STAGING =
    "物理内存图不得覆盖 ELF 暂存区";
constexpr std::string_view OS_TEST_HANDOFF_LAYOUT_STAGING_KERNEL = "ELF 暂存区不得覆盖内核装载区";
constexpr std::string_view OS_TEST_HANDOFF_LAYOUT_KERNEL_STACK = "内核装载区不得覆盖早期内核栈";
constexpr std::string_view OS_TEST_HANDOFF_LAYOUT_MAP_CONTAINS_STAGE1 = "身份映射必须覆盖 Stage 1";
constexpr std::string_view OS_TEST_HANDOFF_LAYOUT_MAP_CONTAINS_STAGING =
    "身份映射必须覆盖 ELF 暂存区末字节";
constexpr std::string_view OS_TEST_HANDOFF_LAYOUT_MAP_CONTAINS_KERNEL =
    "身份映射必须覆盖内核装载区末字节";
constexpr std::string_view OS_TEST_HANDOFF_LAYOUT_MAP_CONTAINS_STACK =
    "身份映射必须覆盖早期内核栈顶前一字节";

constexpr os::foundation::AddressValue OS_TEST_HANDOFF_LAYOUT_STAGE1_BEGIN =
    os::foundation::AddressValue{0x00008000};
constexpr os::foundation::AddressValue OS_TEST_HANDOFF_LAYOUT_STAGE1_SIZE =
    os::foundation::AddressValue{0x00008000};
constexpr os::foundation::AddressValue OS_TEST_HANDOFF_LAYOUT_PAGE_TABLES_BEGIN =
    os::foundation::AddressValue{0x00010000};
constexpr os::foundation::AddressValue OS_TEST_HANDOFF_LAYOUT_PAGE_TABLES_SIZE =
    os::foundation::AddressValue{0x00003000};
constexpr os::foundation::AddressValue OS_TEST_HANDOFF_LAYOUT_DESCRIPTOR_BEGIN =
    os::foundation::AddressValue{0x00013000};
constexpr os::foundation::AddressValue OS_TEST_HANDOFF_LAYOUT_DESCRIPTOR_SIZE =
    os::foundation::AddressValue{0x00000200};
constexpr os::foundation::AddressValue OS_TEST_HANDOFF_LAYOUT_BOOT_INFO_BEGIN =
    os::foundation::AddressValue{0x00014000};
constexpr os::foundation::AddressValue OS_TEST_HANDOFF_LAYOUT_BOOT_INFO_SIZE =
    os::foundation::AddressValue{0x00000068};
constexpr os::foundation::AddressValue OS_TEST_HANDOFF_LAYOUT_SCRATCH_BEGIN =
    os::foundation::AddressValue{0x00015000};
constexpr os::foundation::AddressValue OS_TEST_HANDOFF_LAYOUT_SCRATCH_SIZE =
    os::foundation::AddressValue{0x00001000};
constexpr os::foundation::AddressValue OS_TEST_HANDOFF_LAYOUT_METADATA_BEGIN =
    os::foundation::AddressValue{0x00016000};
constexpr os::foundation::AddressValue OS_TEST_HANDOFF_LAYOUT_METADATA_SIZE =
    os::foundation::AddressValue{0x00001000};
constexpr os::foundation::AddressValue OS_TEST_HANDOFF_LAYOUT_FW_CFG_SCRATCH_BEGIN =
    os::foundation::AddressValue{0x00017000};
constexpr os::foundation::AddressValue OS_TEST_HANDOFF_LAYOUT_FW_CFG_SCRATCH_SIZE =
    os::foundation::AddressValue{0x00001000};
constexpr os::foundation::AddressValue OS_TEST_HANDOFF_LAYOUT_MEMORY_MAP_BEGIN =
    os::foundation::AddressValue{0x00018000};
constexpr os::foundation::AddressValue OS_TEST_HANDOFF_LAYOUT_MEMORY_MAP_SIZE =
    os::foundation::AddressValue{0x00000C00};
constexpr os::foundation::AddressValue OS_TEST_HANDOFF_LAYOUT_STAGING_BEGIN =
    os::foundation::AddressValue{0x00020000};
constexpr os::foundation::AddressValue OS_TEST_HANDOFF_LAYOUT_STAGING_SIZE =
    os::foundation::AddressValue{0x00080000};
constexpr os::foundation::AddressValue OS_TEST_HANDOFF_LAYOUT_KERNEL_BEGIN =
    os::foundation::AddressValue{0x00100000};
constexpr os::foundation::AddressValue OS_TEST_HANDOFF_LAYOUT_KERNEL_SIZE =
    os::foundation::AddressValue{0x03E00000};
constexpr os::foundation::AddressValue OS_TEST_HANDOFF_LAYOUT_STACK_BEGIN =
    os::foundation::AddressValue{0x03FEF000};
constexpr os::foundation::AddressValue OS_TEST_HANDOFF_LAYOUT_STACK_SIZE =
    os::foundation::AddressValue{0x00010000};
constexpr os::foundation::AddressValue OS_TEST_HANDOFF_LAYOUT_MAP_BEGIN =
    os::foundation::AddressValue{0x00000000};
constexpr os::foundation::AddressValue OS_TEST_HANDOFF_LAYOUT_MAP_SIZE =
    os::foundation::AddressValue{0x04000000};
constexpr os::foundation::AddressValue OS_TEST_HANDOFF_LAYOUT_STAGING_LAST_ADDRESS =
    os::foundation::AddressValue{0x0009FFFF};
constexpr os::foundation::AddressValue OS_TEST_HANDOFF_LAYOUT_KERNEL_LAST_ADDRESS =
    os::foundation::AddressValue{0x03EFFFFF};
constexpr os::foundation::AddressValue OS_TEST_HANDOFF_LAYOUT_STACK_LAST_ADDRESS =
    os::foundation::AddressValue{0x03FFEFFF};

bool createRange(const os::foundation::AddressValue begin, const os::foundation::AddressValue size,
                 os::foundation::AddressRange &range) {
    return os::foundation::AddressRange::tryCreate(os::foundation::PhysicalAddress{begin},
                                                   os::foundation::ByteCount{size}, range) ==
           os::foundation::AddressRangeCreationStatus::Succeeded;
}

}

int main() {
    os::test::TestContext testContext{OS_TEST_HANDOFF_LAYOUT_SUITE_NAME};
    os::foundation::AddressRange stage1Range{};
    os::foundation::AddressRange pageTablesRange{};
    os::foundation::AddressRange descriptorRange{};
    os::foundation::AddressRange bootInfoRange{};
    os::foundation::AddressRange scratchRange{};
    os::foundation::AddressRange metadataRange{};
    os::foundation::AddressRange fwCfgScratchRange{};
    os::foundation::AddressRange memoryMapRange{};
    os::foundation::AddressRange stagingRange{};
    os::foundation::AddressRange kernelRange{};
    os::foundation::AddressRange stackRange{};
    os::foundation::AddressRange identityMapRange{};

    const bool allRangesCreated =
        createRange(OS_TEST_HANDOFF_LAYOUT_STAGE1_BEGIN, OS_TEST_HANDOFF_LAYOUT_STAGE1_SIZE,
                    stage1Range) &&
        createRange(OS_TEST_HANDOFF_LAYOUT_PAGE_TABLES_BEGIN,
                    OS_TEST_HANDOFF_LAYOUT_PAGE_TABLES_SIZE, pageTablesRange) &&
        createRange(OS_TEST_HANDOFF_LAYOUT_DESCRIPTOR_BEGIN, OS_TEST_HANDOFF_LAYOUT_DESCRIPTOR_SIZE,
                    descriptorRange) &&
        createRange(OS_TEST_HANDOFF_LAYOUT_BOOT_INFO_BEGIN, OS_TEST_HANDOFF_LAYOUT_BOOT_INFO_SIZE,
                    bootInfoRange) &&
        createRange(OS_TEST_HANDOFF_LAYOUT_SCRATCH_BEGIN, OS_TEST_HANDOFF_LAYOUT_SCRATCH_SIZE,
                    scratchRange) &&
        createRange(OS_TEST_HANDOFF_LAYOUT_METADATA_BEGIN, OS_TEST_HANDOFF_LAYOUT_METADATA_SIZE,
                    metadataRange) &&
        createRange(OS_TEST_HANDOFF_LAYOUT_FW_CFG_SCRATCH_BEGIN,
                    OS_TEST_HANDOFF_LAYOUT_FW_CFG_SCRATCH_SIZE, fwCfgScratchRange) &&
        createRange(OS_TEST_HANDOFF_LAYOUT_MEMORY_MAP_BEGIN, OS_TEST_HANDOFF_LAYOUT_MEMORY_MAP_SIZE,
                    memoryMapRange) &&
        createRange(OS_TEST_HANDOFF_LAYOUT_STAGING_BEGIN, OS_TEST_HANDOFF_LAYOUT_STAGING_SIZE,
                    stagingRange) &&
        createRange(OS_TEST_HANDOFF_LAYOUT_KERNEL_BEGIN, OS_TEST_HANDOFF_LAYOUT_KERNEL_SIZE,
                    kernelRange) &&
        createRange(OS_TEST_HANDOFF_LAYOUT_STACK_BEGIN, OS_TEST_HANDOFF_LAYOUT_STACK_SIZE,
                    stackRange) &&
        createRange(OS_TEST_HANDOFF_LAYOUT_MAP_BEGIN, OS_TEST_HANDOFF_LAYOUT_MAP_SIZE,
                    identityMapRange);
    testContext.expect(allRangesCreated, OS_TEST_HANDOFF_LAYOUT_RANGE_CREATION);

    testContext.expect(!stage1Range.overlaps(pageTablesRange),
                       OS_TEST_HANDOFF_LAYOUT_STAGE1_PAGE_TABLES);
    testContext.expect(!pageTablesRange.overlaps(descriptorRange),
                       OS_TEST_HANDOFF_LAYOUT_PAGE_TABLES_DESCRIPTOR);
    testContext.expect(!descriptorRange.overlaps(bootInfoRange),
                       OS_TEST_HANDOFF_LAYOUT_DESCRIPTOR_BOOT_INFO);
    testContext.expect(!bootInfoRange.overlaps(scratchRange),
                       OS_TEST_HANDOFF_LAYOUT_BOOT_INFO_SCRATCH);
    testContext.expect(!scratchRange.overlaps(metadataRange),
                       OS_TEST_HANDOFF_LAYOUT_SCRATCH_METADATA);
    testContext.expect(!metadataRange.overlaps(fwCfgScratchRange),
                       OS_TEST_HANDOFF_LAYOUT_METADATA_FW_CFG);
    testContext.expect(!fwCfgScratchRange.overlaps(memoryMapRange),
                       OS_TEST_HANDOFF_LAYOUT_FW_CFG_MEMORY_MAP);
    testContext.expect(!memoryMapRange.overlaps(stagingRange),
                       OS_TEST_HANDOFF_LAYOUT_MEMORY_MAP_STAGING);
    testContext.expect(!stagingRange.overlaps(kernelRange), OS_TEST_HANDOFF_LAYOUT_STAGING_KERNEL);
    testContext.expect(!kernelRange.overlaps(stackRange), OS_TEST_HANDOFF_LAYOUT_KERNEL_STACK);
    testContext.expect(identityMapRange.contains(
                           os::foundation::PhysicalAddress{OS_TEST_HANDOFF_LAYOUT_STAGE1_BEGIN}),
                       OS_TEST_HANDOFF_LAYOUT_MAP_CONTAINS_STAGE1);
    testContext.expect(identityMapRange.contains(os::foundation::PhysicalAddress{
                           OS_TEST_HANDOFF_LAYOUT_STAGING_LAST_ADDRESS}),
                       OS_TEST_HANDOFF_LAYOUT_MAP_CONTAINS_STAGING);
    testContext.expect(identityMapRange.contains(os::foundation::PhysicalAddress{
                           OS_TEST_HANDOFF_LAYOUT_KERNEL_LAST_ADDRESS}),
                       OS_TEST_HANDOFF_LAYOUT_MAP_CONTAINS_KERNEL);
    testContext.expect(identityMapRange.contains(os::foundation::PhysicalAddress{
                           OS_TEST_HANDOFF_LAYOUT_STACK_LAST_ADDRESS}),
                       OS_TEST_HANDOFF_LAYOUT_MAP_CONTAINS_STACK);

    return testContext.exitCode();
}

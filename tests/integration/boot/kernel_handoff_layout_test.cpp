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
constexpr std::string_view OS_TEST_HANDOFF_LAYOUT_STAGING_STACK =
    "高端 ELF 暂存区不得覆盖早期内核栈";
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
    os::foundation::AddressValue{0x03E00000};
constexpr os::foundation::AddressValue OS_TEST_HANDOFF_LAYOUT_STAGING_SIZE =
    os::foundation::AddressValue{0x00100000};
constexpr os::foundation::AddressValue OS_TEST_HANDOFF_LAYOUT_KERNEL_BEGIN =
    os::foundation::AddressValue{0x00100000};
constexpr os::foundation::AddressValue OS_TEST_HANDOFF_LAYOUT_KERNEL_SIZE =
    os::foundation::AddressValue{0x03D00000};
constexpr os::foundation::AddressValue OS_TEST_HANDOFF_LAYOUT_STACK_BEGIN =
    os::foundation::AddressValue{0x03FEF000};
constexpr os::foundation::AddressValue OS_TEST_HANDOFF_LAYOUT_STACK_SIZE =
    os::foundation::AddressValue{0x00010000};
constexpr os::foundation::AddressValue OS_TEST_HANDOFF_LAYOUT_MAP_BEGIN =
    os::foundation::AddressValue{0x00000000};
constexpr os::foundation::AddressValue OS_TEST_HANDOFF_LAYOUT_MAP_SIZE =
    os::foundation::AddressValue{0x04000000};
constexpr os::foundation::AddressValue OS_TEST_HANDOFF_LAYOUT_STAGING_LAST_ADDRESS =
    os::foundation::AddressValue{0x03EFFFFF};
constexpr os::foundation::AddressValue OS_TEST_HANDOFF_LAYOUT_KERNEL_LAST_ADDRESS =
    os::foundation::AddressValue{0x03DFFFFF};
constexpr os::foundation::AddressValue OS_TEST_HANDOFF_LAYOUT_STACK_LAST_ADDRESS =
    os::foundation::AddressValue{0x03FFEFFF};

bool CreateRange(const os::foundation::AddressValue begin, const os::foundation::AddressValue size,
                 os::foundation::AddressRange &range) {
    return os::foundation::AddressRange::TryCreate(os::foundation::PhysicalAddress{begin},
                                                   os::foundation::ByteCount{size}, range) ==
           os::foundation::AddressRangeCreationStatus::Succeeded;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_HANDOFF_LAYOUT_SUITE_NAME};
    os::foundation::AddressRange stage1_range{};
    os::foundation::AddressRange page_tables_range{};
    os::foundation::AddressRange descriptor_range{};
    os::foundation::AddressRange boot_info_range{};
    os::foundation::AddressRange scratch_range{};
    os::foundation::AddressRange metadata_range{};
    os::foundation::AddressRange fw_cfg_scratch_range{};
    os::foundation::AddressRange memory_map_range{};
    os::foundation::AddressRange staging_range{};
    os::foundation::AddressRange kernel_range{};
    os::foundation::AddressRange stack_range{};
    os::foundation::AddressRange identity_map_range{};

    const bool all_ranges_created =
        CreateRange(OS_TEST_HANDOFF_LAYOUT_STAGE1_BEGIN, OS_TEST_HANDOFF_LAYOUT_STAGE1_SIZE,
                    stage1_range) &&
        CreateRange(OS_TEST_HANDOFF_LAYOUT_PAGE_TABLES_BEGIN,
                    OS_TEST_HANDOFF_LAYOUT_PAGE_TABLES_SIZE, page_tables_range) &&
        CreateRange(OS_TEST_HANDOFF_LAYOUT_DESCRIPTOR_BEGIN, OS_TEST_HANDOFF_LAYOUT_DESCRIPTOR_SIZE,
                    descriptor_range) &&
        CreateRange(OS_TEST_HANDOFF_LAYOUT_BOOT_INFO_BEGIN, OS_TEST_HANDOFF_LAYOUT_BOOT_INFO_SIZE,
                    boot_info_range) &&
        CreateRange(OS_TEST_HANDOFF_LAYOUT_SCRATCH_BEGIN, OS_TEST_HANDOFF_LAYOUT_SCRATCH_SIZE,
                    scratch_range) &&
        CreateRange(OS_TEST_HANDOFF_LAYOUT_METADATA_BEGIN, OS_TEST_HANDOFF_LAYOUT_METADATA_SIZE,
                    metadata_range) &&
        CreateRange(OS_TEST_HANDOFF_LAYOUT_FW_CFG_SCRATCH_BEGIN,
                    OS_TEST_HANDOFF_LAYOUT_FW_CFG_SCRATCH_SIZE, fw_cfg_scratch_range) &&
        CreateRange(OS_TEST_HANDOFF_LAYOUT_MEMORY_MAP_BEGIN, OS_TEST_HANDOFF_LAYOUT_MEMORY_MAP_SIZE,
                    memory_map_range) &&
        CreateRange(OS_TEST_HANDOFF_LAYOUT_STAGING_BEGIN, OS_TEST_HANDOFF_LAYOUT_STAGING_SIZE,
                    staging_range) &&
        CreateRange(OS_TEST_HANDOFF_LAYOUT_KERNEL_BEGIN, OS_TEST_HANDOFF_LAYOUT_KERNEL_SIZE,
                    kernel_range) &&
        CreateRange(OS_TEST_HANDOFF_LAYOUT_STACK_BEGIN, OS_TEST_HANDOFF_LAYOUT_STACK_SIZE,
                    stack_range) &&
        CreateRange(OS_TEST_HANDOFF_LAYOUT_MAP_BEGIN, OS_TEST_HANDOFF_LAYOUT_MAP_SIZE,
                    identity_map_range);
    test_context.Expect(all_ranges_created, OS_TEST_HANDOFF_LAYOUT_RANGE_CREATION);

    test_context.Expect(!stage1_range.Overlaps(page_tables_range),
                        OS_TEST_HANDOFF_LAYOUT_STAGE1_PAGE_TABLES);
    test_context.Expect(!page_tables_range.Overlaps(descriptor_range),
                        OS_TEST_HANDOFF_LAYOUT_PAGE_TABLES_DESCRIPTOR);
    test_context.Expect(!descriptor_range.Overlaps(boot_info_range),
                        OS_TEST_HANDOFF_LAYOUT_DESCRIPTOR_BOOT_INFO);
    test_context.Expect(!boot_info_range.Overlaps(scratch_range),
                        OS_TEST_HANDOFF_LAYOUT_BOOT_INFO_SCRATCH);
    test_context.Expect(!scratch_range.Overlaps(metadata_range),
                        OS_TEST_HANDOFF_LAYOUT_SCRATCH_METADATA);
    test_context.Expect(!metadata_range.Overlaps(fw_cfg_scratch_range),
                        OS_TEST_HANDOFF_LAYOUT_METADATA_FW_CFG);
    test_context.Expect(!fw_cfg_scratch_range.Overlaps(memory_map_range),
                        OS_TEST_HANDOFF_LAYOUT_FW_CFG_MEMORY_MAP);
    test_context.Expect(!memory_map_range.Overlaps(staging_range),
                        OS_TEST_HANDOFF_LAYOUT_MEMORY_MAP_STAGING);
    test_context.Expect(!staging_range.Overlaps(kernel_range),
                        OS_TEST_HANDOFF_LAYOUT_STAGING_KERNEL);
    test_context.Expect(!staging_range.Overlaps(stack_range), OS_TEST_HANDOFF_LAYOUT_STAGING_STACK);
    test_context.Expect(identity_map_range.Contains(
                            os::foundation::PhysicalAddress{OS_TEST_HANDOFF_LAYOUT_STAGE1_BEGIN}),
                        OS_TEST_HANDOFF_LAYOUT_MAP_CONTAINS_STAGE1);
    test_context.Expect(identity_map_range.Contains(os::foundation::PhysicalAddress{
                            OS_TEST_HANDOFF_LAYOUT_STAGING_LAST_ADDRESS}),
                        OS_TEST_HANDOFF_LAYOUT_MAP_CONTAINS_STAGING);
    test_context.Expect(identity_map_range.Contains(os::foundation::PhysicalAddress{
                            OS_TEST_HANDOFF_LAYOUT_KERNEL_LAST_ADDRESS}),
                        OS_TEST_HANDOFF_LAYOUT_MAP_CONTAINS_KERNEL);
    test_context.Expect(identity_map_range.Contains(os::foundation::PhysicalAddress{
                            OS_TEST_HANDOFF_LAYOUT_STACK_LAST_ADDRESS}),
                        OS_TEST_HANDOFF_LAYOUT_MAP_CONTAINS_STACK);

    return test_context.ExitCode();
}

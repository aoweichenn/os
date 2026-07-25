#include "os/kernel/physical_memory_map.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_MEMORY_MAP_SUITE_NAME = "kernel/physical_memory_map/unit";
constexpr std::string_view OS_TEST_MEMORY_MAP_VALID = "排序且互不重叠的内存图必须通过";
constexpr std::string_view OS_TEST_MEMORY_MAP_SUMMARY = "内存图汇总必须区分可用与保留区域";
constexpr std::string_view OS_TEST_MEMORY_MAP_NULL = "空内存图指针必须被拒绝";
constexpr std::string_view OS_TEST_MEMORY_MAP_EMPTY = "空内存图必须被拒绝";
constexpr std::string_view OS_TEST_MEMORY_MAP_ZERO_LENGTH = "零长度区域必须被拒绝";
constexpr std::string_view OS_TEST_MEMORY_MAP_OVERFLOW = "地址范围溢出必须被拒绝";
constexpr std::string_view OS_TEST_MEMORY_MAP_UNSORTED = "未排序区域必须被拒绝";
constexpr std::string_view OS_TEST_MEMORY_MAP_OVERLAP = "重叠区域必须被拒绝";
constexpr std::string_view OS_TEST_MEMORY_MAP_NO_USABLE = "管理范围内没有可用 RAM 必须被拒绝";

constexpr uint64_t OS_TEST_MEMORY_MAP_MANAGED_LIMIT = 64ULL * 1024ULL * 1024ULL;
constexpr uint64_t OS_TEST_MEMORY_MAP_LOW_RAM_LENGTH = 0x000000000009FC00ULL;
constexpr uint64_t OS_TEST_MEMORY_MAP_RESERVED_BASE = 0x000000000009FC00ULL;
constexpr uint64_t OS_TEST_MEMORY_MAP_RESERVED_LENGTH = 0x0000000000060400ULL;
constexpr uint64_t OS_TEST_MEMORY_MAP_HIGH_RAM_BASE = 0x0000000000100000ULL;
constexpr uint64_t OS_TEST_MEMORY_MAP_HIGH_RAM_LENGTH = 0x0000000003F00000ULL;
constexpr uint64_t OS_TEST_MEMORY_MAP_HIGH_RESERVED_BASE = 0x000000FD00000000ULL;
constexpr uint64_t OS_TEST_MEMORY_MAP_HIGH_RESERVED_LENGTH = 0x0000000300000000ULL;
constexpr uint32_t OS_TEST_MEMORY_MAP_RESERVED_TYPE = 2U;
constexpr uint64_t OS_TEST_MEMORY_MAP_VALID_ENTRY_COUNT = 4ULL;
constexpr uint64_t OS_TEST_MEMORY_MAP_SINGLE_ENTRY_COUNT = 1ULL;
constexpr uint64_t OS_TEST_MEMORY_MAP_TWO_ENTRY_COUNT = 2ULL;
constexpr uint64_t OS_TEST_MEMORY_MAP_ADDRESS_BOUNDARY_OFFSET = 1ULL;
constexpr uint64_t OS_TEST_MEMORY_MAP_OVERFLOW_LENGTH_BYTES = 2ULL;
constexpr uint64_t OS_TEST_MEMORY_MAP_EXPECTED_USABLE_BYTES =
    OS_TEST_MEMORY_MAP_LOW_RAM_LENGTH + OS_TEST_MEMORY_MAP_HIGH_RAM_LENGTH;
constexpr uint64_t OS_TEST_MEMORY_MAP_EXPECTED_TOTAL_BYTES =
    OS_TEST_MEMORY_MAP_EXPECTED_USABLE_BYTES + OS_TEST_MEMORY_MAP_RESERVED_LENGTH +
    OS_TEST_MEMORY_MAP_HIGH_RESERVED_LENGTH;

}

int main() {
    os::test::TestContext testContext{OS_TEST_MEMORY_MAP_SUITE_NAME};
    const os::kernel::PhysicalMemoryMapEntry validEntries[] = {
        {
            .baseAddress = 0ULL,
            .lengthBytes = OS_TEST_MEMORY_MAP_LOW_RAM_LENGTH,
            .type = os::kernel::OS_KERNEL_MEMORY_MAP_USABLE_REGION_TYPE,
            .attributes = 0U,
        },
        {
            .baseAddress = OS_TEST_MEMORY_MAP_RESERVED_BASE,
            .lengthBytes = OS_TEST_MEMORY_MAP_RESERVED_LENGTH,
            .type = OS_TEST_MEMORY_MAP_RESERVED_TYPE,
            .attributes = 0U,
        },
        {
            .baseAddress = OS_TEST_MEMORY_MAP_HIGH_RAM_BASE,
            .lengthBytes = OS_TEST_MEMORY_MAP_HIGH_RAM_LENGTH,
            .type = os::kernel::OS_KERNEL_MEMORY_MAP_USABLE_REGION_TYPE,
            .attributes = 0U,
        },
        {
            .baseAddress = OS_TEST_MEMORY_MAP_HIGH_RESERVED_BASE,
            .lengthBytes = OS_TEST_MEMORY_MAP_HIGH_RESERVED_LENGTH,
            .type = OS_TEST_MEMORY_MAP_RESERVED_TYPE,
            .attributes = 0U,
        },
    };

    os::kernel::PhysicalMemorySummary summary{};
    testContext.Expect(os::kernel::ValidateAndSummarizePhysicalMemoryMap(
                           validEntries, OS_TEST_MEMORY_MAP_VALID_ENTRY_COUNT,
                           OS_TEST_MEMORY_MAP_MANAGED_LIMIT,
                           summary) == os::kernel::PhysicalMemoryMapValidationStatus::Succeeded,
                       OS_TEST_MEMORY_MAP_VALID);
    testContext.Expect(summary.totalBytes == OS_TEST_MEMORY_MAP_EXPECTED_TOTAL_BYTES &&
                           summary.usableBytes == OS_TEST_MEMORY_MAP_EXPECTED_USABLE_BYTES &&
                           summary.managedUsableBytes == OS_TEST_MEMORY_MAP_EXPECTED_USABLE_BYTES,
                       OS_TEST_MEMORY_MAP_SUMMARY);

    testContext.Expect(os::kernel::ValidateAndSummarizePhysicalMemoryMap(
                           nullptr, OS_TEST_MEMORY_MAP_VALID_ENTRY_COUNT,
                           OS_TEST_MEMORY_MAP_MANAGED_LIMIT,
                           summary) == os::kernel::PhysicalMemoryMapValidationStatus::NullEntries,
                       OS_TEST_MEMORY_MAP_NULL);
    testContext.Expect(os::kernel::ValidateAndSummarizePhysicalMemoryMap(
                           validEntries, 0ULL, OS_TEST_MEMORY_MAP_MANAGED_LIMIT, summary) ==
                           os::kernel::PhysicalMemoryMapValidationStatus::InvalidEntryCount,
                       OS_TEST_MEMORY_MAP_EMPTY);

    os::kernel::PhysicalMemoryMapEntry invalidEntries[] = {validEntries[0], validEntries[1]};
    invalidEntries[0].lengthBytes = 0ULL;
    testContext.Expect(os::kernel::ValidateAndSummarizePhysicalMemoryMap(
                           invalidEntries, OS_TEST_MEMORY_MAP_TWO_ENTRY_COUNT,
                           OS_TEST_MEMORY_MAP_MANAGED_LIMIT,
                           summary) == os::kernel::PhysicalMemoryMapValidationStatus::EmptyRegion,
                       OS_TEST_MEMORY_MAP_ZERO_LENGTH);

    invalidEntries[0] = validEntries[0];
    invalidEntries[0].baseAddress = UINT64_MAX;
    invalidEntries[0].lengthBytes = OS_TEST_MEMORY_MAP_OVERFLOW_LENGTH_BYTES;
    testContext.Expect(os::kernel::ValidateAndSummarizePhysicalMemoryMap(
                           invalidEntries, OS_TEST_MEMORY_MAP_SINGLE_ENTRY_COUNT,
                           OS_TEST_MEMORY_MAP_MANAGED_LIMIT, summary) ==
                           os::kernel::PhysicalMemoryMapValidationStatus::AddressOverflow,
                       OS_TEST_MEMORY_MAP_OVERFLOW);

    invalidEntries[0] = validEntries[1];
    invalidEntries[1] = validEntries[0];
    testContext.Expect(os::kernel::ValidateAndSummarizePhysicalMemoryMap(
                           invalidEntries, OS_TEST_MEMORY_MAP_TWO_ENTRY_COUNT,
                           OS_TEST_MEMORY_MAP_MANAGED_LIMIT, summary) ==
                           os::kernel::PhysicalMemoryMapValidationStatus::UnsortedRegions,
                       OS_TEST_MEMORY_MAP_UNSORTED);

    invalidEntries[0] = validEntries[0];
    invalidEntries[1] = validEntries[1];
    invalidEntries[1].baseAddress =
        OS_TEST_MEMORY_MAP_RESERVED_BASE - OS_TEST_MEMORY_MAP_ADDRESS_BOUNDARY_OFFSET;
    testContext.Expect(os::kernel::ValidateAndSummarizePhysicalMemoryMap(
                           invalidEntries, OS_TEST_MEMORY_MAP_TWO_ENTRY_COUNT,
                           OS_TEST_MEMORY_MAP_MANAGED_LIMIT, summary) ==
                           os::kernel::PhysicalMemoryMapValidationStatus::OverlappingRegions,
                       OS_TEST_MEMORY_MAP_OVERLAP);

    invalidEntries[0] = validEntries[1];
    testContext.Expect(os::kernel::ValidateAndSummarizePhysicalMemoryMap(
                           invalidEntries, OS_TEST_MEMORY_MAP_SINGLE_ENTRY_COUNT,
                           OS_TEST_MEMORY_MAP_MANAGED_LIMIT, summary) ==
                           os::kernel::PhysicalMemoryMapValidationStatus::NoManagedUsableMemory,
                       OS_TEST_MEMORY_MAP_NO_USABLE);

    return testContext.ExitCode();
}

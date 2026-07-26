#include "os/kernel/memory/physical_memory_map.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_MEMORY_MAP_SUITE_NAME = "kernel/physical_memory_map/unit";
constexpr std::string_view OS_TEST_MEMORY_MAP_VALID = "排序且互不重叠的内存图必须通过";
constexpr std::string_view OS_TEST_MEMORY_MAP_SUMMARY = "内存图汇总必须区分可用与保留区域";
constexpr std::string_view OS_TEST_MEMORY_MAP_USABLE_LIMIT = "高地址保留洞不得扩大可管理 RAM 上界";
constexpr std::string_view OS_TEST_MEMORY_MAP_NULL = "空内存图指针必须被拒绝";
constexpr std::string_view OS_TEST_MEMORY_MAP_EMPTY = "空内存图必须被拒绝";
constexpr std::string_view OS_TEST_MEMORY_MAP_ZERO_LENGTH = "零长度区域必须被拒绝";
constexpr std::string_view OS_TEST_MEMORY_MAP_OVERFLOW = "地址范围溢出必须被拒绝";
constexpr std::string_view OS_TEST_MEMORY_MAP_UNSORTED = "未排序区域必须被拒绝";
constexpr std::string_view OS_TEST_MEMORY_MAP_OVERLAP = "重叠区域必须被拒绝";
constexpr std::string_view OS_TEST_MEMORY_MAP_NO_USABLE = "管理范围内没有可用 RAM 必须被拒绝";
constexpr std::string_view OS_TEST_MEMORY_MAP_RANGE_SEARCH =
    "启动元数据搜索必须跳过保留区并满足对齐";
constexpr std::string_view OS_TEST_MEMORY_MAP_RANGE_SEARCH_EXHAUSTED =
    "可用 RAM 无法容纳请求时必须明确失败";
constexpr std::string_view OS_TEST_MEMORY_MAP_RANGE_INVALID_RESERVATION = "溢出的保留区必须被拒绝";

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
constexpr uint64_t OS_TEST_MEMORY_MAP_RANGE_RESERVATION_BEGIN = OS_TEST_MEMORY_MAP_HIGH_RAM_BASE;
constexpr uint64_t OS_TEST_MEMORY_MAP_RANGE_RESERVATION_LENGTH = 512ULL * 1024ULL;
constexpr uint64_t OS_TEST_MEMORY_MAP_RANGE_REQUIRED_LENGTH = 256ULL * 1024ULL;
constexpr uint64_t OS_TEST_MEMORY_MAP_RANGE_ALIGNMENT = 4ULL * 1024ULL;
constexpr uint64_t OS_TEST_MEMORY_MAP_RANGE_EXPECTED_BEGIN =
    OS_TEST_MEMORY_MAP_RANGE_RESERVATION_BEGIN + OS_TEST_MEMORY_MAP_RANGE_RESERVATION_LENGTH;
constexpr uint64_t OS_TEST_MEMORY_MAP_RANGE_MAXIMUM_ADDRESS =
    OS_TEST_MEMORY_MAP_HIGH_RAM_BASE + OS_TEST_MEMORY_MAP_HIGH_RAM_LENGTH;
constexpr uint64_t OS_TEST_MEMORY_MAP_EXPECTED_USABLE_BYTES =
    OS_TEST_MEMORY_MAP_LOW_RAM_LENGTH + OS_TEST_MEMORY_MAP_HIGH_RAM_LENGTH;
constexpr uint64_t OS_TEST_MEMORY_MAP_EXPECTED_TOTAL_BYTES =
    OS_TEST_MEMORY_MAP_EXPECTED_USABLE_BYTES + OS_TEST_MEMORY_MAP_RESERVED_LENGTH +
    OS_TEST_MEMORY_MAP_HIGH_RESERVED_LENGTH;
constexpr uint64_t OS_TEST_MEMORY_MAP_EXPECTED_HIGHEST_ADDRESS_EXCLUSIVE =
    OS_TEST_MEMORY_MAP_HIGH_RESERVED_BASE + OS_TEST_MEMORY_MAP_HIGH_RESERVED_LENGTH;
constexpr uint64_t OS_TEST_MEMORY_MAP_EXPECTED_HIGHEST_USABLE_ADDRESS_EXCLUSIVE =
    OS_TEST_MEMORY_MAP_HIGH_RAM_BASE + OS_TEST_MEMORY_MAP_HIGH_RAM_LENGTH;

}

int main() {
    os::test::TestContext test_context{OS_TEST_MEMORY_MAP_SUITE_NAME};
    const os::kernel::PhysicalMemoryMapEntry valid_entries[] = {
        {
            .base_address = 0ULL,
            .length_bytes = OS_TEST_MEMORY_MAP_LOW_RAM_LENGTH,
            .type = os::kernel::OS_KERNEL_MEMORY_MAP_USABLE_REGION_TYPE,
            .attributes = 0U,
        },
        {
            .base_address = OS_TEST_MEMORY_MAP_RESERVED_BASE,
            .length_bytes = OS_TEST_MEMORY_MAP_RESERVED_LENGTH,
            .type = OS_TEST_MEMORY_MAP_RESERVED_TYPE,
            .attributes = 0U,
        },
        {
            .base_address = OS_TEST_MEMORY_MAP_HIGH_RAM_BASE,
            .length_bytes = OS_TEST_MEMORY_MAP_HIGH_RAM_LENGTH,
            .type = os::kernel::OS_KERNEL_MEMORY_MAP_USABLE_REGION_TYPE,
            .attributes = 0U,
        },
        {
            .base_address = OS_TEST_MEMORY_MAP_HIGH_RESERVED_BASE,
            .length_bytes = OS_TEST_MEMORY_MAP_HIGH_RESERVED_LENGTH,
            .type = OS_TEST_MEMORY_MAP_RESERVED_TYPE,
            .attributes = 0U,
        },
    };

    os::kernel::PhysicalMemorySummary summary{};
    test_context.Expect(os::kernel::ValidateAndSummarizePhysicalMemoryMap(
                            valid_entries, OS_TEST_MEMORY_MAP_VALID_ENTRY_COUNT,
                            OS_TEST_MEMORY_MAP_MANAGED_LIMIT,
                            summary) == os::kernel::PhysicalMemoryMapValidationStatus::Succeeded,
                        OS_TEST_MEMORY_MAP_VALID);
    test_context.Expect(summary.total_bytes == OS_TEST_MEMORY_MAP_EXPECTED_TOTAL_BYTES &&
                            summary.usable_bytes == OS_TEST_MEMORY_MAP_EXPECTED_USABLE_BYTES &&
                            summary.managed_usable_bytes ==
                                OS_TEST_MEMORY_MAP_EXPECTED_USABLE_BYTES,
                        OS_TEST_MEMORY_MAP_SUMMARY);
    test_context.Expect(summary.highest_address_exclusive ==
                                OS_TEST_MEMORY_MAP_EXPECTED_HIGHEST_ADDRESS_EXCLUSIVE &&
                            summary.highest_usable_address_exclusive ==
                                OS_TEST_MEMORY_MAP_EXPECTED_HIGHEST_USABLE_ADDRESS_EXCLUSIVE,
                        OS_TEST_MEMORY_MAP_USABLE_LIMIT);

    test_context.Expect(os::kernel::ValidateAndSummarizePhysicalMemoryMap(
                            nullptr, OS_TEST_MEMORY_MAP_VALID_ENTRY_COUNT,
                            OS_TEST_MEMORY_MAP_MANAGED_LIMIT,
                            summary) == os::kernel::PhysicalMemoryMapValidationStatus::NullEntries,
                        OS_TEST_MEMORY_MAP_NULL);
    test_context.Expect(os::kernel::ValidateAndSummarizePhysicalMemoryMap(
                            valid_entries, 0ULL, OS_TEST_MEMORY_MAP_MANAGED_LIMIT, summary) ==
                            os::kernel::PhysicalMemoryMapValidationStatus::InvalidEntryCount,
                        OS_TEST_MEMORY_MAP_EMPTY);

    os::kernel::PhysicalMemoryMapEntry invalid_entries[] = {valid_entries[0], valid_entries[1]};
    invalid_entries[0].length_bytes = 0ULL;
    test_context.Expect(os::kernel::ValidateAndSummarizePhysicalMemoryMap(
                            invalid_entries, OS_TEST_MEMORY_MAP_TWO_ENTRY_COUNT,
                            OS_TEST_MEMORY_MAP_MANAGED_LIMIT,
                            summary) == os::kernel::PhysicalMemoryMapValidationStatus::EmptyRegion,
                        OS_TEST_MEMORY_MAP_ZERO_LENGTH);

    invalid_entries[0] = valid_entries[0];
    invalid_entries[0].base_address = UINT64_MAX;
    invalid_entries[0].length_bytes = OS_TEST_MEMORY_MAP_OVERFLOW_LENGTH_BYTES;
    test_context.Expect(os::kernel::ValidateAndSummarizePhysicalMemoryMap(
                            invalid_entries, OS_TEST_MEMORY_MAP_SINGLE_ENTRY_COUNT,
                            OS_TEST_MEMORY_MAP_MANAGED_LIMIT, summary) ==
                            os::kernel::PhysicalMemoryMapValidationStatus::AddressOverflow,
                        OS_TEST_MEMORY_MAP_OVERFLOW);

    invalid_entries[0] = valid_entries[1];
    invalid_entries[1] = valid_entries[0];
    test_context.Expect(os::kernel::ValidateAndSummarizePhysicalMemoryMap(
                            invalid_entries, OS_TEST_MEMORY_MAP_TWO_ENTRY_COUNT,
                            OS_TEST_MEMORY_MAP_MANAGED_LIMIT, summary) ==
                            os::kernel::PhysicalMemoryMapValidationStatus::UnsortedRegions,
                        OS_TEST_MEMORY_MAP_UNSORTED);

    invalid_entries[0] = valid_entries[0];
    invalid_entries[1] = valid_entries[1];
    invalid_entries[1].base_address =
        OS_TEST_MEMORY_MAP_RESERVED_BASE - OS_TEST_MEMORY_MAP_ADDRESS_BOUNDARY_OFFSET;
    test_context.Expect(os::kernel::ValidateAndSummarizePhysicalMemoryMap(
                            invalid_entries, OS_TEST_MEMORY_MAP_TWO_ENTRY_COUNT,
                            OS_TEST_MEMORY_MAP_MANAGED_LIMIT, summary) ==
                            os::kernel::PhysicalMemoryMapValidationStatus::OverlappingRegions,
                        OS_TEST_MEMORY_MAP_OVERLAP);

    invalid_entries[0] = valid_entries[1];
    test_context.Expect(os::kernel::ValidateAndSummarizePhysicalMemoryMap(
                            invalid_entries, OS_TEST_MEMORY_MAP_SINGLE_ENTRY_COUNT,
                            OS_TEST_MEMORY_MAP_MANAGED_LIMIT, summary) ==
                            os::kernel::PhysicalMemoryMapValidationStatus::NoManagedUsableMemory,
                        OS_TEST_MEMORY_MAP_NO_USABLE);

    const os::kernel::PhysicalMemoryRange reservations[] = {
        {
            .begin_address = OS_TEST_MEMORY_MAP_RANGE_RESERVATION_BEGIN,
            .length_bytes = OS_TEST_MEMORY_MAP_RANGE_RESERVATION_LENGTH,
        },
    };
    os::kernel::PhysicalMemoryRange selected_range{};
    test_context.Expect(
        os::kernel::FindUsablePhysicalMemoryRange(
            valid_entries, OS_TEST_MEMORY_MAP_VALID_ENTRY_COUNT, reservations,
            OS_TEST_MEMORY_MAP_SINGLE_ENTRY_COUNT, OS_TEST_MEMORY_MAP_HIGH_RAM_BASE,
            OS_TEST_MEMORY_MAP_RANGE_MAXIMUM_ADDRESS, OS_TEST_MEMORY_MAP_RANGE_REQUIRED_LENGTH,
            OS_TEST_MEMORY_MAP_RANGE_ALIGNMENT,
            selected_range) == os::kernel::PhysicalMemoryRangeSearchStatus::Succeeded &&
            selected_range.begin_address == OS_TEST_MEMORY_MAP_RANGE_EXPECTED_BEGIN &&
            selected_range.length_bytes == OS_TEST_MEMORY_MAP_RANGE_REQUIRED_LENGTH,
        OS_TEST_MEMORY_MAP_RANGE_SEARCH);
    test_context.Expect(
        os::kernel::FindUsablePhysicalMemoryRange(
            valid_entries, OS_TEST_MEMORY_MAP_VALID_ENTRY_COUNT, nullptr, 0ULL,
            OS_TEST_MEMORY_MAP_HIGH_RAM_BASE, OS_TEST_MEMORY_MAP_RANGE_MAXIMUM_ADDRESS,
            OS_TEST_MEMORY_MAP_MANAGED_LIMIT, OS_TEST_MEMORY_MAP_RANGE_ALIGNMENT,
            selected_range) == os::kernel::PhysicalMemoryRangeSearchStatus::NoSuitableRange,
        OS_TEST_MEMORY_MAP_RANGE_SEARCH_EXHAUSTED);
    const os::kernel::PhysicalMemoryRange invalid_reservation[] = {
        {
            .begin_address = UINT64_MAX,
            .length_bytes = OS_TEST_MEMORY_MAP_OVERFLOW_LENGTH_BYTES,
        },
    };
    test_context.Expect(
        os::kernel::FindUsablePhysicalMemoryRange(
            valid_entries, OS_TEST_MEMORY_MAP_VALID_ENTRY_COUNT, invalid_reservation,
            OS_TEST_MEMORY_MAP_SINGLE_ENTRY_COUNT, OS_TEST_MEMORY_MAP_HIGH_RAM_BASE,
            OS_TEST_MEMORY_MAP_RANGE_MAXIMUM_ADDRESS, OS_TEST_MEMORY_MAP_RANGE_REQUIRED_LENGTH,
            OS_TEST_MEMORY_MAP_RANGE_ALIGNMENT,
            selected_range) == os::kernel::PhysicalMemoryRangeSearchStatus::InvalidReservation,
        OS_TEST_MEMORY_MAP_RANGE_INVALID_RESERVATION);

    return test_context.ExitCode();
}

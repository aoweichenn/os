#include "os/kernel/kernel_virtual_address_allocator.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_KVA_SUITE_NAME = "kernel/kernel_virtual_address_allocator/unit";
constexpr std::string_view OS_TEST_KVA_INITIALIZATION =
    "初始化必须拒绝空元数据、空容量和跨 canonical 空洞的窗口";
constexpr std::string_view OS_TEST_KVA_RESERVATION = "保留区间必须保持排序并拒绝重叠、越界和释放";
constexpr std::string_view OS_TEST_KVA_BEST_FIT = "分配必须在满足页对齐后选择最小可用空洞";
constexpr std::string_view OS_TEST_KVA_RELEASE_DIAGNOSTICS =
    "释放必须精确诊断错误页数、内部地址和重复释放";
constexpr std::string_view OS_TEST_KVA_REUSE = "释放形成的空洞必须能够立即复用且不丢失所有权";
constexpr std::string_view OS_TEST_KVA_STATISTICS =
    "统计必须区分分配页、保留页、元数据容量和最大空洞";
constexpr std::string_view OS_TEST_KVA_METADATA_EXHAUSTION =
    "描述符耗尽必须与虚拟地址耗尽使用不同状态";
constexpr std::string_view OS_TEST_KVA_ADDRESS_EXHAUSTION =
    "窗口没有连续空洞时必须保持失败输出不变";
constexpr std::string_view OS_TEST_KVA_CORRUPTION = "外部元数据被破坏时完整性检查必须明确报错";

constexpr uint64_t OS_TEST_KVA_WINDOW_BASE = 0xFFFFC90000000000ULL;
constexpr uint64_t OS_TEST_KVA_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_KVA_SINGLE_UNIT = 1ULL;
constexpr uint64_t OS_TEST_KVA_WINDOW_PAGE_COUNT = 64ULL;
constexpr uint64_t OS_TEST_KVA_DESCRIPTOR_CAPACITY = 16ULL;
constexpr uint64_t OS_TEST_KVA_FIRST_RESERVATION_PAGE_INDEX = 0ULL;
constexpr uint64_t OS_TEST_KVA_FIRST_RESERVATION_PAGE_COUNT = 2ULL;
constexpr uint64_t OS_TEST_KVA_SECOND_RESERVATION_PAGE_INDEX = 24ULL;
constexpr uint64_t OS_TEST_KVA_SECOND_RESERVATION_PAGE_COUNT = 4ULL;
constexpr uint64_t OS_TEST_KVA_FIRST_ALLOCATION_PAGE_COUNT = 8ULL;
constexpr uint64_t OS_TEST_KVA_FIRST_ALLOCATION_ALIGNMENT_PAGE_COUNT = 8ULL;
constexpr uint64_t OS_TEST_KVA_FIRST_ALLOCATION_PAGE_INDEX = 8ULL;
constexpr uint64_t OS_TEST_KVA_SECOND_ALLOCATION_PAGE_COUNT = 4ULL;
constexpr uint64_t OS_TEST_KVA_SECOND_ALLOCATION_ALIGNMENT_PAGE_COUNT = 4ULL;
constexpr uint64_t OS_TEST_KVA_SECOND_ALLOCATION_PAGE_INDEX = 4ULL;
constexpr uint64_t OS_TEST_KVA_THIRD_ALLOCATION_PAGE_COUNT = 2ULL;
constexpr uint64_t OS_TEST_KVA_THIRD_ALLOCATION_ALIGNMENT_PAGE_COUNT = 1ULL;
constexpr uint64_t OS_TEST_KVA_THIRD_ALLOCATION_PAGE_INDEX = 2ULL;
constexpr uint64_t OS_TEST_KVA_REUSE_ALLOCATION_PAGE_COUNT = 3ULL;
constexpr uint64_t OS_TEST_KVA_REUSE_ALLOCATION_PAGE_INDEX = 4ULL;
constexpr uint64_t OS_TEST_KVA_EXPECTED_ALLOCATED_PAGE_COUNT = 14ULL;
constexpr uint64_t OS_TEST_KVA_EXPECTED_RESERVED_PAGE_COUNT = 6ULL;
constexpr uint64_t OS_TEST_KVA_EXPECTED_ACTIVE_DESCRIPTOR_COUNT = 5ULL;
constexpr uint64_t OS_TEST_KVA_EXPECTED_ACTIVE_ALLOCATION_COUNT = 3ULL;
constexpr uint64_t OS_TEST_KVA_EXPECTED_FREE_PAGE_COUNT = 44ULL;
constexpr uint64_t OS_TEST_KVA_EXPECTED_LARGEST_FREE_RANGE_PAGE_COUNT = 36ULL;
constexpr uint64_t OS_TEST_KVA_EXPECTED_SUCCESSFUL_ALLOCATION_COUNT = 4ULL;
constexpr uint64_t OS_TEST_KVA_EXPECTED_FINAL_DESCRIPTOR_COUNT = 2ULL;
constexpr uint64_t OS_TEST_KVA_INVALID_ALIGNMENT_PAGE_COUNT = 3ULL;
constexpr uint64_t OS_TEST_KVA_INVALID_OUTPUT_ADDRESS = 0x12345000ULL;
constexpr uint64_t OS_TEST_KVA_INVALID_OUTPUT_PAGE_COUNT = 0x55ULL;
constexpr uint64_t OS_TEST_KVA_CANONICAL_BOUNDARY_BASE = 0x00007FFFFFFFF000ULL;
constexpr uint64_t OS_TEST_KVA_CANONICAL_BOUNDARY_PAGE_COUNT = 2ULL;
constexpr uint64_t OS_TEST_KVA_METADATA_DESCRIPTOR_CAPACITY = 2ULL;
constexpr uint64_t OS_TEST_KVA_METADATA_WINDOW_PAGE_COUNT = 16ULL;
constexpr uint64_t OS_TEST_KVA_EXHAUSTION_DESCRIPTOR_CAPACITY = 4ULL;
constexpr uint64_t OS_TEST_KVA_EXHAUSTION_WINDOW_PAGE_COUNT = 4ULL;
constexpr uint64_t OS_TEST_KVA_EXHAUSTION_RESERVATION_PAGE_COUNT = 2ULL;

[[nodiscard]] uint64_t PageAddress(const uint64_t page_index) noexcept {
    return OS_TEST_KVA_WINDOW_BASE + page_index * os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_KVA_SUITE_NAME};
    os::kernel::KernelVirtualAddressRangeDescriptor descriptors[OS_TEST_KVA_DESCRIPTOR_CAPACITY]{};
    os::kernel::KernelVirtualAddressAllocator allocator{};
    os::kernel::KernelVirtualAddressRange unchanged_range{
        .begin_address = OS_TEST_KVA_INVALID_OUTPUT_ADDRESS,
        .page_count = OS_TEST_KVA_INVALID_OUTPUT_PAGE_COUNT,
    };
    const bool initialization_valid =
        allocator.TryAllocate(OS_TEST_KVA_FIRST_ALLOCATION_PAGE_COUNT,
                              OS_TEST_KVA_FIRST_ALLOCATION_ALIGNMENT_PAGE_COUNT, unchanged_range) ==
            os::kernel::KernelVirtualAddressAllocatorStatus::NotInitialized &&
        unchanged_range.begin_address == OS_TEST_KVA_INVALID_OUTPUT_ADDRESS &&
        unchanged_range.page_count == OS_TEST_KVA_INVALID_OUTPUT_PAGE_COUNT &&
        allocator.Initialize(OS_TEST_KVA_WINDOW_BASE, OS_TEST_KVA_WINDOW_PAGE_COUNT, nullptr,
                             OS_TEST_KVA_DESCRIPTOR_CAPACITY) ==
            os::kernel::KernelVirtualAddressAllocatorStatus::NullDescriptorStorage &&
        allocator.Initialize(OS_TEST_KVA_WINDOW_BASE, OS_TEST_KVA_WINDOW_PAGE_COUNT, descriptors,
                             OS_TEST_KVA_EMPTY_VALUE) ==
            os::kernel::KernelVirtualAddressAllocatorStatus::EmptyDescriptorCapacity &&
        allocator.Initialize(OS_TEST_KVA_CANONICAL_BOUNDARY_BASE,
                             OS_TEST_KVA_CANONICAL_BOUNDARY_PAGE_COUNT, descriptors,
                             OS_TEST_KVA_DESCRIPTOR_CAPACITY) ==
            os::kernel::KernelVirtualAddressAllocatorStatus::InvalidWindow &&
        allocator.Initialize(OS_TEST_KVA_WINDOW_BASE, OS_TEST_KVA_WINDOW_PAGE_COUNT, descriptors,
                             OS_TEST_KVA_DESCRIPTOR_CAPACITY) ==
            os::kernel::KernelVirtualAddressAllocatorStatus::Succeeded &&
        allocator.Initialize(OS_TEST_KVA_WINDOW_BASE, OS_TEST_KVA_WINDOW_PAGE_COUNT, descriptors,
                             OS_TEST_KVA_DESCRIPTOR_CAPACITY) ==
            os::kernel::KernelVirtualAddressAllocatorStatus::AlreadyInitialized;
    test_context.Expect(initialization_valid, OS_TEST_KVA_INITIALIZATION);

    const uint64_t first_reservation_address =
        PageAddress(OS_TEST_KVA_FIRST_RESERVATION_PAGE_INDEX);
    const uint64_t second_reservation_address =
        PageAddress(OS_TEST_KVA_SECOND_RESERVATION_PAGE_INDEX);
    const bool reservations_valid =
        allocator.ReserveRange(first_reservation_address,
                               OS_TEST_KVA_FIRST_RESERVATION_PAGE_COUNT) ==
            os::kernel::KernelVirtualAddressAllocatorStatus::Succeeded &&
        allocator.ReserveRange(second_reservation_address,
                               OS_TEST_KVA_SECOND_RESERVATION_PAGE_COUNT) ==
            os::kernel::KernelVirtualAddressAllocatorStatus::Succeeded &&
        allocator.ReserveRange(first_reservation_address +
                                   os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES,
                               OS_TEST_KVA_FIRST_RESERVATION_PAGE_COUNT) ==
            os::kernel::KernelVirtualAddressAllocatorStatus::RangeOverlap &&
        allocator.ReserveRange(OS_TEST_KVA_WINDOW_BASE -
                                   os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES,
                               OS_TEST_KVA_FIRST_RESERVATION_PAGE_COUNT) ==
            os::kernel::KernelVirtualAddressAllocatorStatus::InvalidRange &&
        allocator.TryRelease(os::kernel::KernelVirtualAddressRange{
            .begin_address = first_reservation_address,
            .page_count = OS_TEST_KVA_FIRST_RESERVATION_PAGE_COUNT,
        }) == os::kernel::KernelVirtualAddressAllocatorStatus::ReservedRange &&
        allocator.Validate() == os::kernel::KernelVirtualAddressAllocatorStatus::Succeeded;
    test_context.Expect(reservations_valid, OS_TEST_KVA_RESERVATION);

    os::kernel::KernelVirtualAddressRange first_allocation{};
    os::kernel::KernelVirtualAddressRange second_allocation{};
    os::kernel::KernelVirtualAddressRange third_allocation{};
    const bool best_fit_valid =
        allocator.TryAllocate(OS_TEST_KVA_FIRST_ALLOCATION_PAGE_COUNT,
                              OS_TEST_KVA_FIRST_ALLOCATION_ALIGNMENT_PAGE_COUNT,
                              first_allocation) ==
            os::kernel::KernelVirtualAddressAllocatorStatus::Succeeded &&
        first_allocation.begin_address == PageAddress(OS_TEST_KVA_FIRST_ALLOCATION_PAGE_INDEX) &&
        allocator.TryAllocate(OS_TEST_KVA_SECOND_ALLOCATION_PAGE_COUNT,
                              OS_TEST_KVA_SECOND_ALLOCATION_ALIGNMENT_PAGE_COUNT,
                              second_allocation) ==
            os::kernel::KernelVirtualAddressAllocatorStatus::Succeeded &&
        second_allocation.begin_address == PageAddress(OS_TEST_KVA_SECOND_ALLOCATION_PAGE_INDEX) &&
        allocator.TryAllocate(OS_TEST_KVA_THIRD_ALLOCATION_PAGE_COUNT,
                              OS_TEST_KVA_THIRD_ALLOCATION_ALIGNMENT_PAGE_COUNT,
                              third_allocation) ==
            os::kernel::KernelVirtualAddressAllocatorStatus::Succeeded &&
        third_allocation.begin_address == PageAddress(OS_TEST_KVA_THIRD_ALLOCATION_PAGE_INDEX) &&
        allocator.TryAllocate(OS_TEST_KVA_FIRST_ALLOCATION_PAGE_COUNT,
                              OS_TEST_KVA_INVALID_ALIGNMENT_PAGE_COUNT, unchanged_range) ==
            os::kernel::KernelVirtualAddressAllocatorStatus::InvalidAlignment &&
        unchanged_range.begin_address == OS_TEST_KVA_INVALID_OUTPUT_ADDRESS &&
        unchanged_range.page_count == OS_TEST_KVA_INVALID_OUTPUT_PAGE_COUNT;
    test_context.Expect(best_fit_valid, OS_TEST_KVA_BEST_FIT);

    const os::kernel::KernelVirtualAddressAllocatorStatistics active_statistics =
        allocator.Statistics();
    test_context.Expect(
        active_statistics.allocated_page_count == OS_TEST_KVA_EXPECTED_ALLOCATED_PAGE_COUNT &&
            active_statistics.reserved_page_count == OS_TEST_KVA_EXPECTED_RESERVED_PAGE_COUNT &&
            active_statistics.active_descriptor_count ==
                OS_TEST_KVA_EXPECTED_ACTIVE_DESCRIPTOR_COUNT &&
            active_statistics.active_allocation_count ==
                OS_TEST_KVA_EXPECTED_ACTIVE_ALLOCATION_COUNT &&
            active_statistics.free_page_count == OS_TEST_KVA_EXPECTED_FREE_PAGE_COUNT &&
            active_statistics.largest_free_range_page_count ==
                OS_TEST_KVA_EXPECTED_LARGEST_FREE_RANGE_PAGE_COUNT,
        OS_TEST_KVA_STATISTICS);

    const bool release_diagnostics_valid =
        allocator.TryRelease(os::kernel::KernelVirtualAddressRange{
            .begin_address = first_allocation.begin_address,
            .page_count = first_allocation.page_count - OS_TEST_KVA_SINGLE_UNIT,
        }) == os::kernel::KernelVirtualAddressAllocatorStatus::AllocationSizeMismatch &&
        allocator.TryRelease(os::kernel::KernelVirtualAddressRange{
            .begin_address =
                first_allocation.begin_address + os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES,
            .page_count = first_allocation.page_count - OS_TEST_KVA_SINGLE_UNIT,
        }) == os::kernel::KernelVirtualAddressAllocatorStatus::AllocationNotFound &&
        allocator.TryRelease(second_allocation) ==
            os::kernel::KernelVirtualAddressAllocatorStatus::Succeeded &&
        allocator.TryRelease(second_allocation) ==
            os::kernel::KernelVirtualAddressAllocatorStatus::AllocationNotFound;
    test_context.Expect(release_diagnostics_valid, OS_TEST_KVA_RELEASE_DIAGNOSTICS);

    os::kernel::KernelVirtualAddressRange reused_allocation{};
    const bool reuse_valid =
        allocator.TryAllocate(OS_TEST_KVA_REUSE_ALLOCATION_PAGE_COUNT,
                              OS_TEST_KVA_THIRD_ALLOCATION_ALIGNMENT_PAGE_COUNT,
                              reused_allocation) ==
            os::kernel::KernelVirtualAddressAllocatorStatus::Succeeded &&
        reused_allocation.begin_address == PageAddress(OS_TEST_KVA_REUSE_ALLOCATION_PAGE_INDEX) &&
        allocator.TryRelease(reused_allocation) ==
            os::kernel::KernelVirtualAddressAllocatorStatus::Succeeded &&
        allocator.TryRelease(third_allocation) ==
            os::kernel::KernelVirtualAddressAllocatorStatus::Succeeded &&
        allocator.TryRelease(first_allocation) ==
            os::kernel::KernelVirtualAddressAllocatorStatus::Succeeded &&
        allocator.Validate() == os::kernel::KernelVirtualAddressAllocatorStatus::Succeeded;
    test_context.Expect(reuse_valid, OS_TEST_KVA_REUSE);

    const os::kernel::KernelVirtualAddressAllocatorStatistics final_statistics =
        allocator.Statistics();
    test_context.Expect(
        final_statistics.allocated_page_count == OS_TEST_KVA_EMPTY_VALUE &&
            final_statistics.reserved_page_count == OS_TEST_KVA_EXPECTED_RESERVED_PAGE_COUNT &&
            final_statistics.active_allocation_count == OS_TEST_KVA_EMPTY_VALUE &&
            final_statistics.active_descriptor_count ==
                OS_TEST_KVA_EXPECTED_FINAL_DESCRIPTOR_COUNT &&
            final_statistics.successful_allocation_count ==
                OS_TEST_KVA_EXPECTED_SUCCESSFUL_ALLOCATION_COUNT &&
            final_statistics.release_count == final_statistics.successful_allocation_count &&
            final_statistics.peak_allocated_page_count ==
                OS_TEST_KVA_EXPECTED_ALLOCATED_PAGE_COUNT &&
            final_statistics.descriptor_capacity == OS_TEST_KVA_DESCRIPTOR_CAPACITY,
        OS_TEST_KVA_STATISTICS);

    os::kernel::KernelVirtualAddressRangeDescriptor
        metadata_descriptors[OS_TEST_KVA_METADATA_DESCRIPTOR_CAPACITY]{};
    os::kernel::KernelVirtualAddressAllocator metadata_allocator{};
    os::kernel::KernelVirtualAddressRange metadata_allocation{};
    const bool metadata_exhaustion_valid =
        metadata_allocator.Initialize(OS_TEST_KVA_WINDOW_BASE,
                                      OS_TEST_KVA_METADATA_WINDOW_PAGE_COUNT, metadata_descriptors,
                                      OS_TEST_KVA_METADATA_DESCRIPTOR_CAPACITY) ==
            os::kernel::KernelVirtualAddressAllocatorStatus::Succeeded &&
        metadata_allocator.ReserveRange(OS_TEST_KVA_WINDOW_BASE, OS_TEST_KVA_SINGLE_UNIT) ==
            os::kernel::KernelVirtualAddressAllocatorStatus::Succeeded &&
        metadata_allocator.TryAllocate(OS_TEST_KVA_SINGLE_UNIT, OS_TEST_KVA_SINGLE_UNIT,
                                       metadata_allocation) ==
            os::kernel::KernelVirtualAddressAllocatorStatus::Succeeded &&
        metadata_allocator.ReserveRange(OS_TEST_KVA_WINDOW_BASE, OS_TEST_KVA_SINGLE_UNIT) ==
            os::kernel::KernelVirtualAddressAllocatorStatus::RangeOverlap &&
        metadata_allocator.TryAllocate(OS_TEST_KVA_SINGLE_UNIT, OS_TEST_KVA_SINGLE_UNIT,
                                       unchanged_range) ==
            os::kernel::KernelVirtualAddressAllocatorStatus::MetadataExhausted &&
        unchanged_range.begin_address == OS_TEST_KVA_INVALID_OUTPUT_ADDRESS &&
        metadata_allocator.TryRelease(metadata_allocation) ==
            os::kernel::KernelVirtualAddressAllocatorStatus::Succeeded;
    test_context.Expect(metadata_exhaustion_valid, OS_TEST_KVA_METADATA_EXHAUSTION);

    os::kernel::KernelVirtualAddressRangeDescriptor
        exhaustion_descriptors[OS_TEST_KVA_EXHAUSTION_DESCRIPTOR_CAPACITY]{};
    os::kernel::KernelVirtualAddressAllocator exhaustion_allocator{};
    os::kernel::KernelVirtualAddressRange exhaustion_allocation{};
    const bool address_exhaustion_valid =
        exhaustion_allocator.Initialize(
            OS_TEST_KVA_WINDOW_BASE, OS_TEST_KVA_EXHAUSTION_WINDOW_PAGE_COUNT,
            exhaustion_descriptors, OS_TEST_KVA_EXHAUSTION_DESCRIPTOR_CAPACITY) ==
            os::kernel::KernelVirtualAddressAllocatorStatus::Succeeded &&
        exhaustion_allocator.ReserveRange(OS_TEST_KVA_WINDOW_BASE,
                                          OS_TEST_KVA_EXHAUSTION_RESERVATION_PAGE_COUNT) ==
            os::kernel::KernelVirtualAddressAllocatorStatus::Succeeded &&
        exhaustion_allocator.TryAllocate(OS_TEST_KVA_EXHAUSTION_RESERVATION_PAGE_COUNT,
                                         OS_TEST_KVA_SINGLE_UNIT, exhaustion_allocation) ==
            os::kernel::KernelVirtualAddressAllocatorStatus::Succeeded &&
        exhaustion_allocator.TryAllocate(OS_TEST_KVA_SINGLE_UNIT, OS_TEST_KVA_SINGLE_UNIT,
                                         unchanged_range) ==
            os::kernel::KernelVirtualAddressAllocatorStatus::OutOfVirtualAddressSpace &&
        unchanged_range.begin_address == OS_TEST_KVA_INVALID_OUTPUT_ADDRESS &&
        unchanged_range.page_count == OS_TEST_KVA_INVALID_OUTPUT_PAGE_COUNT;
    test_context.Expect(address_exhaustion_valid, OS_TEST_KVA_ADDRESS_EXHAUSTION);

    descriptors[OS_TEST_KVA_EMPTY_VALUE].page_count = OS_TEST_KVA_EMPTY_VALUE;
    test_context.Expect(allocator.Validate() ==
                            os::kernel::KernelVirtualAddressAllocatorStatus::CorruptedState,
                        OS_TEST_KVA_CORRUPTION);
    descriptors[OS_TEST_KVA_EMPTY_VALUE].page_count = OS_TEST_KVA_FIRST_RESERVATION_PAGE_COUNT;
    test_context.Expect(allocator.Validate() ==
                            os::kernel::KernelVirtualAddressAllocatorStatus::Succeeded,
                        OS_TEST_KVA_CORRUPTION);
    return test_context.ExitCode();
}

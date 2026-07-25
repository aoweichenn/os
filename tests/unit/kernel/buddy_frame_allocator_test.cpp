#include "os/kernel/physical_frame_allocator.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_BUDDY_UNIT_SUITE_NAME = "kernel/buddy_frame_allocator/unit";
constexpr std::string_view OS_TEST_BUDDY_UNIT_STORAGE_CALCULATION =
    "buddy 元数据尺寸必须覆盖每阶空闲与活动位图";
constexpr std::string_view OS_TEST_BUDDY_UNIT_INITIALIZATION =
    "初始化必须把不连续空闲区分解为最大对齐块";
constexpr std::string_view OS_TEST_BUDDY_UNIT_SPLIT_AND_MERGE =
    "小块申请释放必须产生可逆的递归分裂与合并";
constexpr std::string_view OS_TEST_BUDDY_UNIT_FAILURE_ATOMICITY =
    "耗尽与非法范围失败不得修改输出和统计";
constexpr std::string_view OS_TEST_BUDDY_UNIT_RELEASE_VALIDATION =
    "错阶、错位、重复和保留页释放必须明确失败";
constexpr std::string_view OS_TEST_BUDDY_UNIT_RESERVATION_FREEZE =
    "buddy 启用后不得旁路位图追加保留区";
constexpr std::string_view OS_TEST_BUDDY_UNIT_CONFIGURATION_FAILURES =
    "缺失、过小元数据和已有活动页必须阻止 buddy 初始化";
constexpr uint64_t OS_TEST_BUDDY_UNIT_PAGE_COUNT = 64ULL;
constexpr uint64_t OS_TEST_BUDDY_UNIT_MANAGED_SIZE_BYTES =
    OS_TEST_BUDDY_UNIT_PAGE_COUNT * os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_TEST_BUDDY_UNIT_STATE_STORAGE_SIZE_BYTES =
    OS_TEST_BUDDY_UNIT_PAGE_COUNT / 4ULL;
constexpr uint64_t OS_TEST_BUDDY_UNIT_BUDDY_STORAGE_SIZE_BYTES = 36ULL;
constexpr uint64_t OS_TEST_BUDDY_UNIT_SMALL_BUDDY_STORAGE_SIZE_BYTES =
    OS_TEST_BUDDY_UNIT_BUDDY_STORAGE_SIZE_BYTES - 1ULL;
constexpr uint64_t OS_TEST_BUDDY_UNIT_MEMORY_MAP_ENTRY_COUNT = 1ULL;
constexpr uint64_t OS_TEST_BUDDY_UNIT_RESERVED_PAGE_COUNT = 4ULL;
constexpr uint64_t OS_TEST_BUDDY_UNIT_RESERVED_SIZE_BYTES =
    OS_TEST_BUDDY_UNIT_RESERVED_PAGE_COUNT * os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_TEST_BUDDY_UNIT_EXPECTED_FREE_BLOCK_COUNT = 4ULL;
constexpr uint64_t OS_TEST_BUDDY_UNIT_EXPECTED_LARGEST_FREE_ORDER = 5ULL;
constexpr uint64_t OS_TEST_BUDDY_UNIT_SMALL_BLOCK_ORDER = 0ULL;
constexpr uint64_t OS_TEST_BUDDY_UNIT_LARGE_BLOCK_ORDER = 3ULL;
constexpr uint64_t OS_TEST_BUDDY_UNIT_UNAVAILABLE_BLOCK_ORDER = 6ULL;
constexpr uint64_t OS_TEST_BUDDY_UNIT_INVALID_BLOCK_ORDER =
    OS_TEST_BUDDY_UNIT_UNAVAILABLE_BLOCK_ORDER + 1ULL;
constexpr uint64_t OS_TEST_BUDDY_UNIT_EXPECTED_LARGE_BLOCK_PAGE_INDEX = 8ULL;
constexpr uint64_t OS_TEST_BUDDY_UNIT_SENTINEL_ADDRESS = 0xA55AA55AA55AA55AULL;
constexpr uint64_t OS_TEST_BUDDY_UNIT_INVALID_RANGE_BEGIN = OS_TEST_BUDDY_UNIT_MANAGED_SIZE_BYTES;
constexpr uint64_t OS_TEST_BUDDY_UNIT_INVALID_RANGE_END =
    OS_TEST_BUDDY_UNIT_INVALID_RANGE_BEGIN + os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_TEST_BUDDY_UNIT_INVALID_ALIGNMENT_OFFSET =
    os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;

[[nodiscard]] bool
EqualStatistics(const os::kernel::PhysicalFrameAllocatorStatistics left,
                const os::kernel::PhysicalFrameAllocatorStatistics right) noexcept {
    return left.managed_frame_count == right.managed_frame_count &&
           left.free_frame_count == right.free_frame_count &&
           left.allocated_frame_count == right.allocated_frame_count &&
           left.reserved_frame_count == right.reserved_frame_count;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_BUDDY_UNIT_SUITE_NAME};

    test_context.Expect(os::kernel::CalculatePhysicalFrameBuddyStorageSizeBytes(
                            OS_TEST_BUDDY_UNIT_MANAGED_SIZE_BYTES) ==
                            OS_TEST_BUDDY_UNIT_BUDDY_STORAGE_SIZE_BYTES,
                        OS_TEST_BUDDY_UNIT_STORAGE_CALCULATION);

    const os::kernel::PhysicalMemoryMapEntry configuration_memory_map[] = {
        {
            .base_address = 0ULL,
            .length_bytes = OS_TEST_BUDDY_UNIT_MANAGED_SIZE_BYTES,
            .type = os::kernel::OS_KERNEL_MEMORY_MAP_USABLE_REGION_TYPE,
            .attributes = 0U,
        },
    };
    uint8_t missing_buddy_state_storage[OS_TEST_BUDDY_UNIT_STATE_STORAGE_SIZE_BYTES]{};
    os::kernel::PhysicalFrameAllocator missing_buddy_allocator{
        missing_buddy_state_storage,
        OS_TEST_BUDDY_UNIT_STATE_STORAGE_SIZE_BYTES,
    };
    const bool missing_storage_rejected =
        missing_buddy_allocator.Initialize(configuration_memory_map,
                                           OS_TEST_BUDDY_UNIT_MEMORY_MAP_ENTRY_COUNT,
                                           OS_TEST_BUDDY_UNIT_MANAGED_SIZE_BYTES) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        missing_buddy_allocator.InitializeBuddy() ==
            os::kernel::PhysicalFrameAllocatorStatus::NullBuddyStorage;

    uint8_t small_buddy_state_storage[OS_TEST_BUDDY_UNIT_STATE_STORAGE_SIZE_BYTES]{};
    uint8_t small_buddy_storage[OS_TEST_BUDDY_UNIT_SMALL_BUDDY_STORAGE_SIZE_BYTES]{};
    os::kernel::PhysicalFrameAllocator small_buddy_allocator{
        small_buddy_state_storage,
        OS_TEST_BUDDY_UNIT_STATE_STORAGE_SIZE_BYTES,
    };
    const bool small_storage_rejected =
        small_buddy_allocator.ConfigureBuddyStorage(
            small_buddy_storage, OS_TEST_BUDDY_UNIT_SMALL_BUDDY_STORAGE_SIZE_BYTES) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        small_buddy_allocator.Initialize(configuration_memory_map,
                                         OS_TEST_BUDDY_UNIT_MEMORY_MAP_ENTRY_COUNT,
                                         OS_TEST_BUDDY_UNIT_MANAGED_SIZE_BYTES) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        small_buddy_allocator.InitializeBuddy() ==
            os::kernel::PhysicalFrameAllocatorStatus::InvalidBuddyStorageSize;

    uint8_t active_page_state_storage[OS_TEST_BUDDY_UNIT_STATE_STORAGE_SIZE_BYTES]{};
    uint8_t active_page_buddy_storage[OS_TEST_BUDDY_UNIT_BUDDY_STORAGE_SIZE_BYTES]{};
    os::kernel::PhysicalFrameAllocator active_page_allocator{
        active_page_state_storage,
        OS_TEST_BUDDY_UNIT_STATE_STORAGE_SIZE_BYTES,
    };
    os::kernel::PhysicalFrame legacy_frame{};
    const bool active_page_rejected =
        active_page_allocator.ConfigureBuddyStorage(active_page_buddy_storage,
                                                    OS_TEST_BUDDY_UNIT_BUDDY_STORAGE_SIZE_BYTES) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        active_page_allocator.Initialize(configuration_memory_map,
                                         OS_TEST_BUDDY_UNIT_MEMORY_MAP_ENTRY_COUNT,
                                         OS_TEST_BUDDY_UNIT_MANAGED_SIZE_BYTES) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        active_page_allocator.Allocate(legacy_frame) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        active_page_allocator.InitializeBuddy() ==
            os::kernel::PhysicalFrameAllocatorStatus::ExistingAllocationsPreventBuddyInitialization;
    test_context.Expect(missing_storage_rejected && small_storage_rejected && active_page_rejected,
                        OS_TEST_BUDDY_UNIT_CONFIGURATION_FAILURES);

    uint8_t state_storage[OS_TEST_BUDDY_UNIT_STATE_STORAGE_SIZE_BYTES]{};
    uint8_t buddy_storage[OS_TEST_BUDDY_UNIT_BUDDY_STORAGE_SIZE_BYTES]{};
    os::kernel::PhysicalFrameAllocator allocator{
        state_storage,
        OS_TEST_BUDDY_UNIT_STATE_STORAGE_SIZE_BYTES,
    };
    const os::kernel::PhysicalMemoryMapEntry memory_map[] = {
        {
            .base_address = 0ULL,
            .length_bytes = OS_TEST_BUDDY_UNIT_MANAGED_SIZE_BYTES,
            .type = os::kernel::OS_KERNEL_MEMORY_MAP_USABLE_REGION_TYPE,
            .attributes = 0U,
        },
    };
    const bool initialized =
        allocator.ConfigureBuddyStorage(buddy_storage,
                                        OS_TEST_BUDDY_UNIT_BUDDY_STORAGE_SIZE_BYTES) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        allocator.Initialize(memory_map, OS_TEST_BUDDY_UNIT_MEMORY_MAP_ENTRY_COUNT,
                             OS_TEST_BUDDY_UNIT_MANAGED_SIZE_BYTES) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        allocator.ReserveRange(0ULL, OS_TEST_BUDDY_UNIT_RESERVED_SIZE_BYTES) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        allocator.InitializeBuddy() == os::kernel::PhysicalFrameAllocatorStatus::Succeeded;
    const os::kernel::PhysicalFrameBuddyStatistics initial_buddy_statistics =
        allocator.BuddyStatistics();
    test_context.Expect(initialized &&
                            allocator.ValidateBuddy() ==
                                os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
                            initial_buddy_statistics.free_block_count ==
                                OS_TEST_BUDDY_UNIT_EXPECTED_FREE_BLOCK_COUNT &&
                            initial_buddy_statistics.largest_free_order ==
                                OS_TEST_BUDDY_UNIT_EXPECTED_LARGEST_FREE_ORDER &&
                            initial_buddy_statistics.active_block_count == 0ULL,
                        OS_TEST_BUDDY_UNIT_INITIALIZATION);

    const os::kernel::PhysicalFrameAllocatorStatistics baseline_statistics = allocator.Statistics();
    os::kernel::PhysicalFrameBlock large_block{};
    const bool large_block_allocated =
        allocator.AllocateBlock(OS_TEST_BUDDY_UNIT_LARGE_BLOCK_ORDER, large_block) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        large_block.physical_address == OS_TEST_BUDDY_UNIT_EXPECTED_LARGE_BLOCK_PAGE_INDEX *
                                            os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES &&
        large_block.order == OS_TEST_BUDDY_UNIT_LARGE_BLOCK_ORDER;
    os::kernel::PhysicalFrameBlock small_block{};
    const bool small_block_allocated =
        allocator.AllocateBlock(OS_TEST_BUDDY_UNIT_SMALL_BLOCK_ORDER, small_block) ==
        os::kernel::PhysicalFrameAllocatorStatus::Succeeded;
    const bool blocks_released =
        small_block_allocated &&
        allocator.ReleaseBlock(small_block) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        large_block_allocated &&
        allocator.ReleaseBlock(large_block) == os::kernel::PhysicalFrameAllocatorStatus::Succeeded;
    const os::kernel::PhysicalFrameBuddyStatistics round_trip_buddy_statistics =
        allocator.BuddyStatistics();
    test_context.Expect(
        blocks_released && EqualStatistics(allocator.Statistics(), baseline_statistics) &&
            round_trip_buddy_statistics.split_count > 0ULL &&
            round_trip_buddy_statistics.merge_count > 0ULL &&
            round_trip_buddy_statistics.active_block_count == 0ULL &&
            allocator.ValidateBuddy() == os::kernel::PhysicalFrameAllocatorStatus::Succeeded,
        OS_TEST_BUDDY_UNIT_SPLIT_AND_MERGE);

    os::kernel::PhysicalFrameBlock unchanged_block{
        .physical_address = OS_TEST_BUDDY_UNIT_SENTINEL_ADDRESS,
        .order = OS_TEST_BUDDY_UNIT_SENTINEL_ADDRESS,
    };
    const os::kernel::PhysicalFrameAllocatorStatistics before_failure_statistics =
        allocator.Statistics();
    const bool unavailable_order_rejected =
        allocator.AllocateBlock(OS_TEST_BUDDY_UNIT_UNAVAILABLE_BLOCK_ORDER, unchanged_block) ==
            os::kernel::PhysicalFrameAllocatorStatus::OutOfMemory &&
        unchanged_block.physical_address == OS_TEST_BUDDY_UNIT_SENTINEL_ADDRESS &&
        unchanged_block.order == OS_TEST_BUDDY_UNIT_SENTINEL_ADDRESS;
    const bool invalid_order_rejected =
        allocator.AllocateBlock(OS_TEST_BUDDY_UNIT_INVALID_BLOCK_ORDER, unchanged_block) ==
            os::kernel::PhysicalFrameAllocatorStatus::InvalidBlockOrder &&
        unchanged_block.physical_address == OS_TEST_BUDDY_UNIT_SENTINEL_ADDRESS &&
        unchanged_block.order == OS_TEST_BUDDY_UNIT_SENTINEL_ADDRESS;
    const bool invalid_range_rejected =
        allocator.AllocateBlockInRange(OS_TEST_BUDDY_UNIT_SMALL_BLOCK_ORDER,
                                       OS_TEST_BUDDY_UNIT_INVALID_RANGE_BEGIN,
                                       OS_TEST_BUDDY_UNIT_INVALID_RANGE_END, unchanged_block) ==
            os::kernel::PhysicalFrameAllocatorStatus::InvalidAllocationRange &&
        unchanged_block.physical_address == OS_TEST_BUDDY_UNIT_SENTINEL_ADDRESS &&
        unchanged_block.order == OS_TEST_BUDDY_UNIT_SENTINEL_ADDRESS;
    test_context.Expect(unavailable_order_rejected && invalid_order_rejected &&
                            invalid_range_rejected &&
                            EqualStatistics(allocator.Statistics(), before_failure_statistics),
                        OS_TEST_BUDDY_UNIT_FAILURE_ATOMICITY);

    os::kernel::PhysicalFrameBlock validation_block{};
    const bool validation_block_allocated =
        allocator.AllocateBlock(OS_TEST_BUDDY_UNIT_LARGE_BLOCK_ORDER, validation_block) ==
        os::kernel::PhysicalFrameAllocatorStatus::Succeeded;
    const os::kernel::PhysicalFrameAllocatorStatistics before_invalid_release_statistics =
        allocator.Statistics();
    const os::kernel::PhysicalFrameBlock wrong_order_block{
        .physical_address = validation_block.physical_address,
        .order = OS_TEST_BUDDY_UNIT_LARGE_BLOCK_ORDER - 1ULL,
    };
    const os::kernel::PhysicalFrameBlock wrong_alignment_block{
        .physical_address =
            validation_block.physical_address + OS_TEST_BUDDY_UNIT_INVALID_ALIGNMENT_OFFSET,
        .order = OS_TEST_BUDDY_UNIT_LARGE_BLOCK_ORDER,
    };
    const bool invalid_releases_rejected =
        validation_block_allocated &&
        allocator.ReleaseBlock(wrong_order_block) ==
            os::kernel::PhysicalFrameAllocatorStatus::AllocationOrderMismatch &&
        allocator.ReleaseBlock(wrong_alignment_block) ==
            os::kernel::PhysicalFrameAllocatorStatus::InvalidBlockAlignment &&
        allocator.Release(os::kernel::PhysicalFrame{.physical_address = 0ULL}) ==
            os::kernel::PhysicalFrameAllocatorStatus::FrameNotAllocated &&
        EqualStatistics(allocator.Statistics(), before_invalid_release_statistics) &&
        allocator.ReleaseBlock(validation_block) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        allocator.ReleaseBlock(validation_block) ==
            os::kernel::PhysicalFrameAllocatorStatus::FrameNotAllocated;
    test_context.Expect(invalid_releases_rejected &&
                            allocator.ValidateBuddy() ==
                                os::kernel::PhysicalFrameAllocatorStatus::Succeeded,
                        OS_TEST_BUDDY_UNIT_RELEASE_VALIDATION);

    test_context.Expect(
        allocator.ReserveRange(OS_TEST_BUDDY_UNIT_RESERVED_SIZE_BYTES,
                               os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) ==
                os::kernel::PhysicalFrameAllocatorStatus::ReservationAfterBuddyInitialization &&
            allocator.ConfigureBuddyStorage(buddy_storage,
                                            OS_TEST_BUDDY_UNIT_BUDDY_STORAGE_SIZE_BYTES) ==
                os::kernel::PhysicalFrameAllocatorStatus::BuddyAlreadyInitialized &&
            allocator.InitializeBuddy() ==
                os::kernel::PhysicalFrameAllocatorStatus::BuddyAlreadyInitialized,
        OS_TEST_BUDDY_UNIT_RESERVATION_FREEZE);

    return test_context.ExitCode();
}

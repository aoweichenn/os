#include "os/kernel/memory/physical_frame_allocator.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_BUDDY_LIFECYCLE_SUITE_NAME =
    "boot/buddy_frame_allocator_lifecycle/integration";
constexpr std::string_view OS_TEST_BUDDY_LIFECYCLE_CAPACITY_METADATA =
    "64 GiB 规格必须得到有界且精确的 buddy 元数据";
constexpr std::string_view OS_TEST_BUDDY_LIFECYCLE_HOLE_AND_RESERVATION =
    "E820 洞和启动保留区不得进入任何 buddy 空闲块";
constexpr std::string_view OS_TEST_BUDDY_LIFECYCLE_RANGE_ALLOCATION =
    "连续块必须完整落入高地址半开区间并保持阶对齐";
constexpr std::string_view OS_TEST_BUDDY_LIFECYCLE_BASELINE_RESTORED =
    "混合阶生命周期结束后页和块统计必须回到基线";
constexpr uint64_t OS_TEST_BUDDY_LIFECYCLE_PAGE_COUNT = 1024ULL;
constexpr uint64_t OS_TEST_BUDDY_LIFECYCLE_MANAGED_SIZE_BYTES =
    OS_TEST_BUDDY_LIFECYCLE_PAGE_COUNT * os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_TEST_BUDDY_LIFECYCLE_STATE_STORAGE_SIZE_BYTES =
    OS_TEST_BUDDY_LIFECYCLE_PAGE_COUNT / 4ULL;
constexpr uint64_t OS_TEST_BUDDY_LIFECYCLE_BUDDY_STORAGE_SIZE_BYTES = 516ULL;
constexpr uint64_t OS_TEST_BUDDY_LIFECYCLE_MEMORY_MAP_ENTRY_COUNT = 3ULL;
constexpr uint64_t OS_TEST_BUDDY_LIFECYCLE_LOW_USABLE_PAGE_COUNT = 256ULL;
constexpr uint64_t OS_TEST_BUDDY_LIFECYCLE_HOLE_PAGE_COUNT = 256ULL;
constexpr uint64_t OS_TEST_BUDDY_LIFECYCLE_HIGH_USABLE_PAGE_COUNT = 512ULL;
constexpr uint64_t OS_TEST_BUDDY_LIFECYCLE_LOW_USABLE_SIZE_BYTES =
    OS_TEST_BUDDY_LIFECYCLE_LOW_USABLE_PAGE_COUNT * os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_TEST_BUDDY_LIFECYCLE_HOLE_SIZE_BYTES =
    OS_TEST_BUDDY_LIFECYCLE_HOLE_PAGE_COUNT * os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_TEST_BUDDY_LIFECYCLE_HIGH_USABLE_BEGIN =
    OS_TEST_BUDDY_LIFECYCLE_LOW_USABLE_SIZE_BYTES + OS_TEST_BUDDY_LIFECYCLE_HOLE_SIZE_BYTES;
constexpr uint64_t OS_TEST_BUDDY_LIFECYCLE_HIGH_USABLE_SIZE_BYTES =
    OS_TEST_BUDDY_LIFECYCLE_HIGH_USABLE_PAGE_COUNT * os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_TEST_BUDDY_LIFECYCLE_RESERVED_PAGE_COUNT = 32ULL;
constexpr uint64_t OS_TEST_BUDDY_LIFECYCLE_RESERVED_SIZE_BYTES =
    OS_TEST_BUDDY_LIFECYCLE_RESERVED_PAGE_COUNT * os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_TEST_BUDDY_LIFECYCLE_EXPECTED_FREE_PAGE_COUNT =
    OS_TEST_BUDDY_LIFECYCLE_LOW_USABLE_PAGE_COUNT + OS_TEST_BUDDY_LIFECYCLE_HIGH_USABLE_PAGE_COUNT -
    OS_TEST_BUDDY_LIFECYCLE_RESERVED_PAGE_COUNT;
constexpr uint64_t OS_TEST_BUDDY_LIFECYCLE_RANGE_BLOCK_ORDER = 5ULL;
constexpr uint64_t OS_TEST_BUDDY_LIFECYCLE_RANGE_BEGIN_PAGE_INDEX = 600ULL;
constexpr uint64_t OS_TEST_BUDDY_LIFECYCLE_RANGE_END_PAGE_INDEX = 900ULL;
constexpr uint64_t OS_TEST_BUDDY_LIFECYCLE_RANGE_BEGIN =
    OS_TEST_BUDDY_LIFECYCLE_RANGE_BEGIN_PAGE_INDEX * os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_TEST_BUDDY_LIFECYCLE_RANGE_END =
    OS_TEST_BUDDY_LIFECYCLE_RANGE_END_PAGE_INDEX * os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_TEST_BUDDY_LIFECYCLE_RANGE_BLOCK_SIZE_BYTES =
    (1ULL << OS_TEST_BUDDY_LIFECYCLE_RANGE_BLOCK_ORDER) *
    os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_TEST_BUDDY_LIFECYCLE_SMALL_BLOCK_ORDER = 0ULL;
constexpr uint64_t OS_TEST_BUDDY_LIFECYCLE_MEDIUM_BLOCK_ORDER = 3ULL;
constexpr uint64_t OS_TEST_BUDDY_LIFECYCLE_LARGE_BLOCK_ORDER = 6ULL;
constexpr uint64_t OS_TEST_BUDDY_LIFECYCLE_PRIMARY_MEMORY_SIZE_BYTES =
    64ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr uint64_t OS_TEST_BUDDY_LIFECYCLE_PRIMARY_BUDDY_STORAGE_SIZE_BYTES =
    8ULL * 1024ULL * 1024ULL + 4ULL;
constexpr uint32_t OS_TEST_BUDDY_LIFECYCLE_RESERVED_MEMORY_TYPE = 2U;

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
    os::test::TestContext test_context{OS_TEST_BUDDY_LIFECYCLE_SUITE_NAME};

    test_context.Expect(os::kernel::CalculatePhysicalFrameBuddyStorageSizeBytes(
                            OS_TEST_BUDDY_LIFECYCLE_PRIMARY_MEMORY_SIZE_BYTES) ==
                            OS_TEST_BUDDY_LIFECYCLE_PRIMARY_BUDDY_STORAGE_SIZE_BYTES,
                        OS_TEST_BUDDY_LIFECYCLE_CAPACITY_METADATA);

    uint8_t state_storage[OS_TEST_BUDDY_LIFECYCLE_STATE_STORAGE_SIZE_BYTES]{};
    uint8_t buddy_storage[OS_TEST_BUDDY_LIFECYCLE_BUDDY_STORAGE_SIZE_BYTES]{};
    os::kernel::PhysicalFrameAllocator allocator{
        state_storage,
        OS_TEST_BUDDY_LIFECYCLE_STATE_STORAGE_SIZE_BYTES,
    };
    const os::kernel::PhysicalMemoryMapEntry memory_map[] = {
        {
            .base_address = 0ULL,
            .length_bytes = OS_TEST_BUDDY_LIFECYCLE_LOW_USABLE_SIZE_BYTES,
            .type = os::kernel::OS_KERNEL_MEMORY_MAP_USABLE_REGION_TYPE,
            .attributes = 0U,
        },
        {
            .base_address = OS_TEST_BUDDY_LIFECYCLE_LOW_USABLE_SIZE_BYTES,
            .length_bytes = OS_TEST_BUDDY_LIFECYCLE_HOLE_SIZE_BYTES,
            .type = OS_TEST_BUDDY_LIFECYCLE_RESERVED_MEMORY_TYPE,
            .attributes = 0U,
        },
        {
            .base_address = OS_TEST_BUDDY_LIFECYCLE_HIGH_USABLE_BEGIN,
            .length_bytes = OS_TEST_BUDDY_LIFECYCLE_HIGH_USABLE_SIZE_BYTES,
            .type = os::kernel::OS_KERNEL_MEMORY_MAP_USABLE_REGION_TYPE,
            .attributes = 0U,
        },
    };
    const bool initialized =
        allocator.ConfigureBuddyStorage(buddy_storage,
                                        OS_TEST_BUDDY_LIFECYCLE_BUDDY_STORAGE_SIZE_BYTES) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        allocator.Initialize(memory_map, OS_TEST_BUDDY_LIFECYCLE_MEMORY_MAP_ENTRY_COUNT,
                             OS_TEST_BUDDY_LIFECYCLE_MANAGED_SIZE_BYTES) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        allocator.ReserveRange(0ULL, OS_TEST_BUDDY_LIFECYCLE_RESERVED_SIZE_BYTES) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        allocator.InitializeBuddy() == os::kernel::PhysicalFrameAllocatorStatus::Succeeded;
    const os::kernel::PhysicalFrameAllocatorStatistics baseline_statistics = allocator.Statistics();
    test_context.Expect(initialized &&
                            baseline_statistics.free_frame_count ==
                                OS_TEST_BUDDY_LIFECYCLE_EXPECTED_FREE_PAGE_COUNT &&
                            allocator.ValidateBuddy() ==
                                os::kernel::PhysicalFrameAllocatorStatus::Succeeded,
                        OS_TEST_BUDDY_LIFECYCLE_HOLE_AND_RESERVATION);

    os::kernel::PhysicalFrameBlock range_block{};
    const bool range_block_valid =
        allocator.AllocateBlockInRange(OS_TEST_BUDDY_LIFECYCLE_RANGE_BLOCK_ORDER,
                                       OS_TEST_BUDDY_LIFECYCLE_RANGE_BEGIN,
                                       OS_TEST_BUDDY_LIFECYCLE_RANGE_END, range_block) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        range_block.physical_address >= OS_TEST_BUDDY_LIFECYCLE_RANGE_BEGIN &&
        range_block.physical_address + OS_TEST_BUDDY_LIFECYCLE_RANGE_BLOCK_SIZE_BYTES <=
            OS_TEST_BUDDY_LIFECYCLE_RANGE_END &&
        (range_block.physical_address & (OS_TEST_BUDDY_LIFECYCLE_RANGE_BLOCK_SIZE_BYTES - 1ULL)) ==
            0ULL;
    test_context.Expect(range_block_valid, OS_TEST_BUDDY_LIFECYCLE_RANGE_ALLOCATION);

    os::kernel::PhysicalFrameBlock small_block{};
    os::kernel::PhysicalFrameBlock medium_block{};
    os::kernel::PhysicalFrameBlock large_block{};
    const bool mixed_blocks_allocated =
        allocator.AllocateBlock(OS_TEST_BUDDY_LIFECYCLE_SMALL_BLOCK_ORDER, small_block) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        allocator.AllocateBlock(OS_TEST_BUDDY_LIFECYCLE_MEDIUM_BLOCK_ORDER, medium_block) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        allocator.AllocateBlock(OS_TEST_BUDDY_LIFECYCLE_LARGE_BLOCK_ORDER, large_block) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded;
    const bool mixed_blocks_released =
        mixed_blocks_allocated && range_block_valid &&
        allocator.ReleaseBlock(medium_block) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        allocator.ReleaseBlock(range_block) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        allocator.ReleaseBlock(small_block) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        allocator.ReleaseBlock(large_block) == os::kernel::PhysicalFrameAllocatorStatus::Succeeded;
    const os::kernel::PhysicalFrameBuddyStatistics final_buddy_statistics =
        allocator.BuddyStatistics();
    test_context.Expect(
        mixed_blocks_released && EqualStatistics(allocator.Statistics(), baseline_statistics) &&
            final_buddy_statistics.active_block_count == 0ULL &&
            final_buddy_statistics.successful_allocation_count ==
                final_buddy_statistics.release_count &&
            final_buddy_statistics.split_count > 0ULL &&
            final_buddy_statistics.merge_count > 0ULL &&
            allocator.ValidateBuddy() == os::kernel::PhysicalFrameAllocatorStatus::Succeeded,
        OS_TEST_BUDDY_LIFECYCLE_BASELINE_RESTORED);

    return test_context.ExitCode();
}

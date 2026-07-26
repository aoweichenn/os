#include "os/kernel/physical_frame_allocator.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_FRAME_ALLOCATOR_SUITE_NAME =
    "kernel/physical_frame_allocator/unit";
constexpr std::string_view OS_TEST_FRAME_ALLOCATOR_INITIALIZE = "有效内存图必须初始化分配器";
constexpr std::string_view OS_TEST_FRAME_ALLOCATOR_RESERVE = "保留区必须从空闲帧中扣除";
constexpr std::string_view OS_TEST_FRAME_ALLOCATOR_ORDER = "首次分配必须返回首个未保留页";
constexpr std::string_view OS_TEST_FRAME_ALLOCATOR_RELEASE = "释放后的页必须可以再次分配";
constexpr std::string_view OS_TEST_FRAME_ALLOCATOR_OWNERSHIP =
    "精确所有权查询必须只在单页活动分配期间成立";
constexpr std::string_view OS_TEST_FRAME_ALLOCATOR_DOUBLE_RELEASE = "重复释放必须被拒绝";
constexpr std::string_view OS_TEST_FRAME_ALLOCATOR_EXHAUSTION = "耗尽全部空闲页后必须返回失败";
constexpr std::string_view OS_TEST_FRAME_ALLOCATOR_RESERVED_RELEASE = "保留页不能通过释放接口回收";
constexpr std::string_view OS_TEST_FRAME_ALLOCATOR_RESERVATION_ATOMIC =
    "跨越已分配页的保留请求不能留下部分修改";
constexpr std::string_view OS_TEST_FRAME_ALLOCATOR_EMPTY_ALIGNED_RANGE =
    "没有完整页帧时初始化必须失败且保持未初始化";
constexpr std::string_view OS_TEST_FRAME_ALLOCATOR_STATE_SIZE_64_GIB =
    "64 GiB RAM 的二位页帧状态容量必须准确";
constexpr std::string_view OS_TEST_FRAME_ALLOCATOR_HIGH_RANGE =
    "范围分配必须返回 4 GiB 以上的可用页帧";
constexpr std::string_view OS_TEST_FRAME_ALLOCATOR_INVALID_RANGE = "未按页对齐的分配范围必须被拒绝";

constexpr uint64_t OS_TEST_FRAME_ALLOCATOR_PAGE_COUNT = 16ULL;
constexpr uint64_t OS_TEST_FRAME_ALLOCATOR_MEMORY_MAP_ENTRY_COUNT = 1ULL;
constexpr uint64_t OS_TEST_FRAME_ALLOCATOR_STORAGE_STATES_PER_BYTE = 4ULL;
constexpr uint64_t OS_TEST_FRAME_ALLOCATOR_MANAGED_SIZE_BYTES =
    OS_TEST_FRAME_ALLOCATOR_PAGE_COUNT * os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_TEST_FRAME_ALLOCATOR_STORAGE_SIZE_BYTES =
    OS_TEST_FRAME_ALLOCATOR_PAGE_COUNT / OS_TEST_FRAME_ALLOCATOR_STORAGE_STATES_PER_BYTE;
constexpr uint64_t OS_TEST_FRAME_ALLOCATOR_RESERVED_PAGE_COUNT = 4ULL;
constexpr uint64_t OS_TEST_FRAME_ALLOCATOR_RESERVED_SIZE_BYTES =
    OS_TEST_FRAME_ALLOCATOR_RESERVED_PAGE_COUNT * os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_TEST_FRAME_ALLOCATOR_EXPECTED_FIRST_ADDRESS =
    OS_TEST_FRAME_ALLOCATOR_RESERVED_SIZE_BYTES;
constexpr uint64_t OS_TEST_FRAME_ALLOCATOR_FREE_AFTER_RESERVATION =
    OS_TEST_FRAME_ALLOCATOR_PAGE_COUNT - OS_TEST_FRAME_ALLOCATOR_RESERVED_PAGE_COUNT;
constexpr uint64_t OS_TEST_FRAME_ALLOCATOR_SINGLE_PAGE_LENGTH =
    os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_TEST_FRAME_ALLOCATOR_TWO_PAGE_LENGTH =
    OS_TEST_FRAME_ALLOCATOR_SINGLE_PAGE_LENGTH * 2ULL;
constexpr uint64_t OS_TEST_FRAME_ALLOCATOR_NO_COMPLETE_FRAME_LENGTH_BYTES = 1ULL;
constexpr uint64_t OS_TEST_FRAME_ALLOCATOR_EXPECTED_ATOMIC_FREE_FRAME_COUNT =
    OS_TEST_FRAME_ALLOCATOR_PAGE_COUNT - 1ULL;
constexpr uint64_t OS_TEST_FRAME_ALLOCATOR_EXPECTED_ATOMIC_ALLOCATED_FRAME_COUNT = 1ULL;
constexpr uint64_t OS_TEST_FRAME_ALLOCATOR_PRIMARY_MEMORY_SIZE_BYTES =
    64ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr uint64_t OS_TEST_FRAME_ALLOCATOR_PRIMARY_STATE_STORAGE_SIZE_BYTES =
    4ULL * 1024ULL * 1024ULL;
constexpr uint64_t OS_TEST_FRAME_ALLOCATOR_HIGH_RANGE_BEGIN =
    4ULL * 1024ULL * 1024ULL * 1024ULL + os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_TEST_FRAME_ALLOCATOR_HIGH_RANGE_PAGE_COUNT = 2ULL;
constexpr uint64_t OS_TEST_FRAME_ALLOCATOR_HIGH_RANGE_LENGTH_BYTES =
    OS_TEST_FRAME_ALLOCATOR_HIGH_RANGE_PAGE_COUNT * os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_TEST_FRAME_ALLOCATOR_HIGH_MANAGED_LIMIT =
    OS_TEST_FRAME_ALLOCATOR_HIGH_RANGE_BEGIN + OS_TEST_FRAME_ALLOCATOR_HIGH_RANGE_LENGTH_BYTES;
constexpr uint64_t OS_TEST_FRAME_ALLOCATOR_HIGH_STATE_STORAGE_SIZE_BYTES =
    (OS_TEST_FRAME_ALLOCATOR_HIGH_MANAGED_LIMIT / os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES +
     OS_TEST_FRAME_ALLOCATOR_STORAGE_STATES_PER_BYTE - 1ULL) /
    OS_TEST_FRAME_ALLOCATOR_STORAGE_STATES_PER_BYTE;
constexpr uint64_t OS_TEST_FRAME_ALLOCATOR_UNALIGNED_OFFSET = 1ULL;

}

int main() {
    os::test::TestContext test_context{OS_TEST_FRAME_ALLOCATOR_SUITE_NAME};
    uint8_t state_storage[OS_TEST_FRAME_ALLOCATOR_STORAGE_SIZE_BYTES]{};
    os::kernel::PhysicalFrameAllocator allocator{state_storage,
                                                 OS_TEST_FRAME_ALLOCATOR_STORAGE_SIZE_BYTES};
    const os::kernel::PhysicalMemoryMapEntry memory_map[] = {
        {
            .base_address = 0ULL,
            .length_bytes = OS_TEST_FRAME_ALLOCATOR_MANAGED_SIZE_BYTES,
            .type = os::kernel::OS_KERNEL_MEMORY_MAP_USABLE_REGION_TYPE,
            .attributes = 0U,
        },
    };

    test_context.Expect(allocator.Initialize(memory_map,
                                             OS_TEST_FRAME_ALLOCATOR_MEMORY_MAP_ENTRY_COUNT,
                                             OS_TEST_FRAME_ALLOCATOR_MANAGED_SIZE_BYTES) ==
                            os::kernel::PhysicalFrameAllocatorStatus::Succeeded,
                        OS_TEST_FRAME_ALLOCATOR_INITIALIZE);
    test_context.Expect(allocator.ReserveRange(0ULL, OS_TEST_FRAME_ALLOCATOR_RESERVED_SIZE_BYTES) ==
                                os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
                            allocator.Statistics().free_frame_count ==
                                OS_TEST_FRAME_ALLOCATOR_FREE_AFTER_RESERVATION,
                        OS_TEST_FRAME_ALLOCATOR_RESERVE);

    os::kernel::PhysicalFrame first_frame{};
    test_context.Expect(
        allocator.Allocate(first_frame) == os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
            first_frame.physical_address == OS_TEST_FRAME_ALLOCATOR_EXPECTED_FIRST_ADDRESS,
        OS_TEST_FRAME_ALLOCATOR_ORDER);
    const bool first_frame_owned = allocator.OwnsAllocation(first_frame);
    const bool first_frame_released =
        allocator.Release(first_frame) == os::kernel::PhysicalFrameAllocatorStatus::Succeeded;
    test_context.Expect(first_frame_owned && first_frame_released &&
                            !allocator.OwnsAllocation(first_frame),
                        OS_TEST_FRAME_ALLOCATOR_OWNERSHIP);
    os::kernel::PhysicalFrame recycled_frame{};
    test_context.Expect(allocator.Allocate(recycled_frame) ==
                                os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
                            recycled_frame.physical_address == first_frame.physical_address,
                        OS_TEST_FRAME_ALLOCATOR_RELEASE);
    test_context.Expect(allocator.Release(recycled_frame) ==
                                os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
                            allocator.Release(recycled_frame) ==
                                os::kernel::PhysicalFrameAllocatorStatus::FrameNotAllocated,
                        OS_TEST_FRAME_ALLOCATOR_DOUBLE_RELEASE);

    test_context.Expect(allocator.Release(os::kernel::PhysicalFrame{.physical_address = 0ULL}) ==
                            os::kernel::PhysicalFrameAllocatorStatus::FrameNotAllocated,
                        OS_TEST_FRAME_ALLOCATOR_RESERVED_RELEASE);

    os::kernel::PhysicalFrame frame{};
    uint64_t successful_allocation_count = 0ULL;
    while (allocator.Allocate(frame) == os::kernel::PhysicalFrameAllocatorStatus::Succeeded) {
        ++successful_allocation_count;
    }
    test_context.Expect(
        successful_allocation_count == OS_TEST_FRAME_ALLOCATOR_FREE_AFTER_RESERVATION &&
            allocator.Allocate(frame) == os::kernel::PhysicalFrameAllocatorStatus::OutOfMemory,
        OS_TEST_FRAME_ALLOCATOR_EXHAUSTION);

    uint8_t atomic_state_storage[OS_TEST_FRAME_ALLOCATOR_STORAGE_SIZE_BYTES]{};
    os::kernel::PhysicalFrameAllocator atomic_allocator{
        atomic_state_storage,
        OS_TEST_FRAME_ALLOCATOR_STORAGE_SIZE_BYTES,
    };
    os::kernel::PhysicalFrame atomic_first_frame{};
    os::kernel::PhysicalFrame atomic_second_frame{};
    const bool atomic_setup_succeeded =
        atomic_allocator.Initialize(memory_map, OS_TEST_FRAME_ALLOCATOR_MEMORY_MAP_ENTRY_COUNT,
                                    OS_TEST_FRAME_ALLOCATOR_MANAGED_SIZE_BYTES) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        atomic_allocator.Allocate(atomic_first_frame) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        atomic_allocator.Allocate(atomic_second_frame) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        atomic_allocator.Release(atomic_first_frame) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded;
    const bool atomic_reservation_rejected =
        atomic_allocator.ReserveRange(atomic_first_frame.physical_address,
                                      OS_TEST_FRAME_ALLOCATOR_TWO_PAGE_LENGTH) ==
        os::kernel::PhysicalFrameAllocatorStatus::InvalidReservation;
    const os::kernel::PhysicalFrameAllocatorStatistics atomic_statistics =
        atomic_allocator.Statistics();
    test_context.Expect(atomic_setup_succeeded && atomic_reservation_rejected &&
                            atomic_statistics.free_frame_count ==
                                OS_TEST_FRAME_ALLOCATOR_EXPECTED_ATOMIC_FREE_FRAME_COUNT &&
                            atomic_statistics.allocated_frame_count ==
                                OS_TEST_FRAME_ALLOCATOR_EXPECTED_ATOMIC_ALLOCATED_FRAME_COUNT &&
                            atomic_statistics.reserved_frame_count == 0ULL,
                        OS_TEST_FRAME_ALLOCATOR_RESERVATION_ATOMIC);

    uint8_t empty_range_state_storage[OS_TEST_FRAME_ALLOCATOR_STORAGE_SIZE_BYTES]{};
    os::kernel::PhysicalFrameAllocator empty_range_allocator{
        empty_range_state_storage,
        OS_TEST_FRAME_ALLOCATOR_STORAGE_SIZE_BYTES,
    };
    const os::kernel::PhysicalMemoryMapEntry incomplete_page_memory_map[] = {
        {
            .base_address = 0ULL,
            .length_bytes = OS_TEST_FRAME_ALLOCATOR_NO_COMPLETE_FRAME_LENGTH_BYTES,
            .type = os::kernel::OS_KERNEL_MEMORY_MAP_USABLE_REGION_TYPE,
            .attributes = 0U,
        },
    };
    test_context.Expect(
        empty_range_allocator.Initialize(incomplete_page_memory_map,
                                         OS_TEST_FRAME_ALLOCATOR_MEMORY_MAP_ENTRY_COUNT,
                                         OS_TEST_FRAME_ALLOCATOR_MANAGED_SIZE_BYTES) ==
                os::kernel::PhysicalFrameAllocatorStatus::NoUsableFrames &&
            empty_range_allocator.Statistics().managed_frame_count == 0ULL,
        OS_TEST_FRAME_ALLOCATOR_EMPTY_ALIGNED_RANGE);

    test_context.Expect(os::kernel::CalculatePhysicalFrameStateStorageSizeBytes(
                            OS_TEST_FRAME_ALLOCATOR_PRIMARY_MEMORY_SIZE_BYTES) ==
                            OS_TEST_FRAME_ALLOCATOR_PRIMARY_STATE_STORAGE_SIZE_BYTES,
                        OS_TEST_FRAME_ALLOCATOR_STATE_SIZE_64_GIB);

    static uint8_t high_state_storage[OS_TEST_FRAME_ALLOCATOR_HIGH_STATE_STORAGE_SIZE_BYTES]{};
    os::kernel::PhysicalFrameAllocator high_allocator{
        high_state_storage,
        OS_TEST_FRAME_ALLOCATOR_HIGH_STATE_STORAGE_SIZE_BYTES,
    };
    const os::kernel::PhysicalMemoryMapEntry high_memory_map[] = {
        {
            .base_address = OS_TEST_FRAME_ALLOCATOR_HIGH_RANGE_BEGIN,
            .length_bytes = OS_TEST_FRAME_ALLOCATOR_HIGH_RANGE_LENGTH_BYTES,
            .type = os::kernel::OS_KERNEL_MEMORY_MAP_USABLE_REGION_TYPE,
            .attributes = 0U,
        },
    };
    os::kernel::PhysicalFrame high_frame{};
    test_context.Expect(
        high_allocator.Initialize(high_memory_map, OS_TEST_FRAME_ALLOCATOR_MEMORY_MAP_ENTRY_COUNT,
                                  OS_TEST_FRAME_ALLOCATOR_HIGH_MANAGED_LIMIT) ==
                os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
            high_allocator.AllocateInRange(OS_TEST_FRAME_ALLOCATOR_HIGH_RANGE_BEGIN,
                                           OS_TEST_FRAME_ALLOCATOR_HIGH_MANAGED_LIMIT,
                                           high_frame) ==
                os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
            high_frame.physical_address == OS_TEST_FRAME_ALLOCATOR_HIGH_RANGE_BEGIN &&
            high_allocator.Release(high_frame) ==
                os::kernel::PhysicalFrameAllocatorStatus::Succeeded,
        OS_TEST_FRAME_ALLOCATOR_HIGH_RANGE);
    test_context.Expect(high_allocator.AllocateInRange(OS_TEST_FRAME_ALLOCATOR_HIGH_RANGE_BEGIN +
                                                           OS_TEST_FRAME_ALLOCATOR_UNALIGNED_OFFSET,
                                                       OS_TEST_FRAME_ALLOCATOR_HIGH_MANAGED_LIMIT,
                                                       high_frame) ==
                            os::kernel::PhysicalFrameAllocatorStatus::InvalidAllocationRange,
                        OS_TEST_FRAME_ALLOCATOR_INVALID_RANGE);

    return test_context.ExitCode();
}

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
constexpr std::string_view OS_TEST_FRAME_ALLOCATOR_DOUBLE_RELEASE = "重复释放必须被拒绝";
constexpr std::string_view OS_TEST_FRAME_ALLOCATOR_EXHAUSTION = "耗尽全部空闲页后必须返回失败";
constexpr std::string_view OS_TEST_FRAME_ALLOCATOR_RESERVED_RELEASE = "保留页不能通过释放接口回收";
constexpr std::string_view OS_TEST_FRAME_ALLOCATOR_RESERVATION_ATOMIC =
    "跨越已分配页的保留请求不能留下部分修改";
constexpr std::string_view OS_TEST_FRAME_ALLOCATOR_EMPTY_ALIGNED_RANGE =
    "没有完整页帧时初始化必须失败且保持未初始化";

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

}

int main() {
    os::test::TestContext testContext{OS_TEST_FRAME_ALLOCATOR_SUITE_NAME};
    uint8_t stateStorage[OS_TEST_FRAME_ALLOCATOR_STORAGE_SIZE_BYTES]{};
    os::kernel::PhysicalFrameAllocator allocator{stateStorage,
                                                 OS_TEST_FRAME_ALLOCATOR_STORAGE_SIZE_BYTES};
    const os::kernel::PhysicalMemoryMapEntry memoryMap[] = {
        {
            .baseAddress = 0ULL,
            .lengthBytes = OS_TEST_FRAME_ALLOCATOR_MANAGED_SIZE_BYTES,
            .type = os::kernel::OS_KERNEL_MEMORY_MAP_USABLE_REGION_TYPE,
            .attributes = 0U,
        },
    };

    testContext.Expect(allocator.Initialize(memoryMap,
                                            OS_TEST_FRAME_ALLOCATOR_MEMORY_MAP_ENTRY_COUNT,
                                            OS_TEST_FRAME_ALLOCATOR_MANAGED_SIZE_BYTES) ==
                           os::kernel::PhysicalFrameAllocatorStatus::Succeeded,
                       OS_TEST_FRAME_ALLOCATOR_INITIALIZE);
    testContext.Expect(allocator.ReserveRange(0ULL, OS_TEST_FRAME_ALLOCATOR_RESERVED_SIZE_BYTES) ==
                               os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
                           allocator.Statistics().freeFrameCount ==
                               OS_TEST_FRAME_ALLOCATOR_FREE_AFTER_RESERVATION,
                       OS_TEST_FRAME_ALLOCATOR_RESERVE);

    os::kernel::PhysicalFrame firstFrame{};
    testContext.Expect(
        allocator.Allocate(firstFrame) == os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
            firstFrame.physicalAddress == OS_TEST_FRAME_ALLOCATOR_EXPECTED_FIRST_ADDRESS,
        OS_TEST_FRAME_ALLOCATOR_ORDER);
    testContext.Expect(allocator.Release(firstFrame) ==
                           os::kernel::PhysicalFrameAllocatorStatus::Succeeded,
                       OS_TEST_FRAME_ALLOCATOR_RELEASE);
    os::kernel::PhysicalFrame recycledFrame{};
    testContext.Expect(allocator.Allocate(recycledFrame) ==
                               os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
                           recycledFrame.physicalAddress == firstFrame.physicalAddress,
                       OS_TEST_FRAME_ALLOCATOR_RELEASE);
    testContext.Expect(allocator.Release(recycledFrame) ==
                               os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
                           allocator.Release(recycledFrame) ==
                               os::kernel::PhysicalFrameAllocatorStatus::FrameNotAllocated,
                       OS_TEST_FRAME_ALLOCATOR_DOUBLE_RELEASE);

    testContext.Expect(allocator.Release(os::kernel::PhysicalFrame{.physicalAddress = 0ULL}) ==
                           os::kernel::PhysicalFrameAllocatorStatus::FrameNotAllocated,
                       OS_TEST_FRAME_ALLOCATOR_RESERVED_RELEASE);

    os::kernel::PhysicalFrame frame{};
    uint64_t successfulAllocationCount = 0ULL;
    while (allocator.Allocate(frame) == os::kernel::PhysicalFrameAllocatorStatus::Succeeded) {
        ++successfulAllocationCount;
    }
    testContext.Expect(
        successfulAllocationCount == OS_TEST_FRAME_ALLOCATOR_FREE_AFTER_RESERVATION &&
            allocator.Allocate(frame) == os::kernel::PhysicalFrameAllocatorStatus::OutOfMemory,
        OS_TEST_FRAME_ALLOCATOR_EXHAUSTION);

    uint8_t atomicStateStorage[OS_TEST_FRAME_ALLOCATOR_STORAGE_SIZE_BYTES]{};
    os::kernel::PhysicalFrameAllocator atomicAllocator{
        atomicStateStorage,
        OS_TEST_FRAME_ALLOCATOR_STORAGE_SIZE_BYTES,
    };
    os::kernel::PhysicalFrame atomicFirstFrame{};
    os::kernel::PhysicalFrame atomicSecondFrame{};
    const bool atomicSetupSucceeded =
        atomicAllocator.Initialize(memoryMap, OS_TEST_FRAME_ALLOCATOR_MEMORY_MAP_ENTRY_COUNT,
                                   OS_TEST_FRAME_ALLOCATOR_MANAGED_SIZE_BYTES) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        atomicAllocator.Allocate(atomicFirstFrame) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        atomicAllocator.Allocate(atomicSecondFrame) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        atomicAllocator.Release(atomicFirstFrame) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded;
    const bool atomicReservationRejected =
        atomicAllocator.ReserveRange(atomicFirstFrame.physicalAddress,
                                     OS_TEST_FRAME_ALLOCATOR_TWO_PAGE_LENGTH) ==
        os::kernel::PhysicalFrameAllocatorStatus::InvalidReservation;
    const os::kernel::PhysicalFrameAllocatorStatistics atomicStatistics =
        atomicAllocator.Statistics();
    testContext.Expect(atomicSetupSucceeded && atomicReservationRejected &&
                           atomicStatistics.freeFrameCount ==
                               OS_TEST_FRAME_ALLOCATOR_EXPECTED_ATOMIC_FREE_FRAME_COUNT &&
                           atomicStatistics.allocatedFrameCount ==
                               OS_TEST_FRAME_ALLOCATOR_EXPECTED_ATOMIC_ALLOCATED_FRAME_COUNT &&
                           atomicStatistics.reservedFrameCount == 0ULL,
                       OS_TEST_FRAME_ALLOCATOR_RESERVATION_ATOMIC);

    uint8_t emptyRangeStateStorage[OS_TEST_FRAME_ALLOCATOR_STORAGE_SIZE_BYTES]{};
    os::kernel::PhysicalFrameAllocator emptyRangeAllocator{
        emptyRangeStateStorage,
        OS_TEST_FRAME_ALLOCATOR_STORAGE_SIZE_BYTES,
    };
    const os::kernel::PhysicalMemoryMapEntry incompletePageMemoryMap[] = {
        {
            .baseAddress = 0ULL,
            .lengthBytes = OS_TEST_FRAME_ALLOCATOR_NO_COMPLETE_FRAME_LENGTH_BYTES,
            .type = os::kernel::OS_KERNEL_MEMORY_MAP_USABLE_REGION_TYPE,
            .attributes = 0U,
        },
    };
    testContext.Expect(emptyRangeAllocator.Initialize(
                           incompletePageMemoryMap, OS_TEST_FRAME_ALLOCATOR_MEMORY_MAP_ENTRY_COUNT,
                           OS_TEST_FRAME_ALLOCATOR_MANAGED_SIZE_BYTES) ==
                               os::kernel::PhysicalFrameAllocatorStatus::NoUsableFrames &&
                           emptyRangeAllocator.Statistics().managedFrameCount == 0ULL,
                       OS_TEST_FRAME_ALLOCATOR_EMPTY_ALIGNED_RANGE);

    return testContext.ExitCode();
}

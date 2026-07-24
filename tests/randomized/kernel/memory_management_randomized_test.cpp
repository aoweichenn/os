#include "os/kernel/page_table.hpp"
#include "os/kernel/physical_frame_allocator.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_MEMORY_RANDOM_SUITE_NAME = "kernel/memory_management/randomized";
constexpr std::string_view OS_TEST_MEMORY_RANDOM_PAGE_ROUND_TRIP =
    "随机页表项必须保持物理地址和权限";
constexpr std::string_view OS_TEST_MEMORY_RANDOM_ALLOCATOR_UNIQUE = "随机分配不能返回仍在使用的页";
constexpr std::string_view OS_TEST_MEMORY_RANDOM_ALLOCATOR_COUNTS =
    "随机分配释放后的统计必须与参考模型一致";
constexpr uint64_t OS_TEST_MEMORY_RANDOM_SEED = 0x6D656D6F72793634ULL;
constexpr uint64_t OS_TEST_MEMORY_RANDOM_PAGE_ITERATION_COUNT = 8192ULL;
constexpr uint64_t OS_TEST_MEMORY_RANDOM_ALLOCATOR_ITERATION_COUNT = 4096ULL;
constexpr uint64_t OS_TEST_MEMORY_RANDOM_MULTIPLIER = 0x2545F4914F6CDD1DULL;
constexpr uint64_t OS_TEST_MEMORY_RANDOM_SHIFT_FIRST = 12ULL;
constexpr uint64_t OS_TEST_MEMORY_RANDOM_SHIFT_SECOND = 25ULL;
constexpr uint64_t OS_TEST_MEMORY_RANDOM_SHIFT_THIRD = 27ULL;
constexpr uint64_t OS_TEST_MEMORY_RANDOM_PHYSICAL_ADDRESS_MASK = 0x0000000FFFFFFFFFULL;
constexpr uint64_t OS_TEST_MEMORY_RANDOM_PAGE_OFFSET_MASK =
    os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES - 1ULL;
constexpr uint64_t OS_TEST_MEMORY_RANDOM_ALLOCATOR_PAGE_COUNT = 256ULL;
constexpr uint64_t OS_TEST_MEMORY_RANDOM_ALLOCATOR_RESERVED_PAGE_COUNT = 16ULL;
constexpr uint64_t OS_TEST_MEMORY_RANDOM_STORAGE_STATES_PER_BYTE = 4ULL;
constexpr uint64_t OS_TEST_MEMORY_RANDOM_ALLOCATOR_STORAGE_SIZE_BYTES =
    OS_TEST_MEMORY_RANDOM_ALLOCATOR_PAGE_COUNT / OS_TEST_MEMORY_RANDOM_STORAGE_STATES_PER_BYTE;
constexpr uint64_t OS_TEST_MEMORY_RANDOM_ALLOCATOR_MANAGED_SIZE_BYTES =
    OS_TEST_MEMORY_RANDOM_ALLOCATOR_PAGE_COUNT * os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_TEST_MEMORY_RANDOM_ALLOCATOR_RESERVED_SIZE_BYTES =
    OS_TEST_MEMORY_RANDOM_ALLOCATOR_RESERVED_PAGE_COUNT *
    os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_TEST_MEMORY_RANDOM_MEMORY_MAP_ENTRY_COUNT = 1ULL;
constexpr uint64_t OS_TEST_MEMORY_RANDOM_WRITABLE_PERMISSION_BIT = 0x1ULL;
constexpr uint64_t OS_TEST_MEMORY_RANDOM_EXECUTABLE_PERMISSION_BIT = 0x2ULL;
constexpr uint64_t OS_TEST_MEMORY_RANDOM_USER_PERMISSION_BIT = 0x4ULL;
constexpr uint64_t OS_TEST_MEMORY_RANDOM_ALLOCATION_DECISION_BIT = 0x1ULL;
constexpr uint64_t OS_TEST_MEMORY_RANDOM_NEXT_PAGE_OFFSET = 1ULL;

[[nodiscard]] uint64_t nextRandom(uint64_t &state) noexcept {
    state ^= state >> OS_TEST_MEMORY_RANDOM_SHIFT_FIRST;
    state ^= state << OS_TEST_MEMORY_RANDOM_SHIFT_SECOND;
    state ^= state >> OS_TEST_MEMORY_RANDOM_SHIFT_THIRD;
    state *= OS_TEST_MEMORY_RANDOM_MULTIPLIER;
    return state;
}

}

int main() {
    os::test::TestContext testContext{OS_TEST_MEMORY_RANDOM_SUITE_NAME};
    uint64_t randomState = OS_TEST_MEMORY_RANDOM_SEED;

    for (uint64_t iteration = 0ULL; iteration < OS_TEST_MEMORY_RANDOM_PAGE_ITERATION_COUNT;
         ++iteration) {
        const uint64_t physicalAddress = nextRandom(randomState) &
                                         OS_TEST_MEMORY_RANDOM_PHYSICAL_ADDRESS_MASK &
                                         ~OS_TEST_MEMORY_RANDOM_PAGE_OFFSET_MASK;
        const uint64_t permissionBits = nextRandom(randomState);
        const os::kernel::PagePermissions permissions{
            .writable = (permissionBits & OS_TEST_MEMORY_RANDOM_WRITABLE_PERMISSION_BIT) != 0ULL,
            .executable =
                (permissionBits & OS_TEST_MEMORY_RANDOM_EXECUTABLE_PERMISSION_BIT) != 0ULL,
            .userAccessible = (permissionBits & OS_TEST_MEMORY_RANDOM_USER_PERMISSION_BIT) != 0ULL,
        };
        const os::kernel::PageMapping mapping = os::kernel::decodePageTableLeafEntry(
            os::kernel::encodePageTableLeafEntry(physicalAddress, permissions));
        testContext.expectRandom(
            mapping.physicalAddress == physicalAddress &&
                mapping.permissions.writable == permissions.writable &&
                mapping.permissions.executable == permissions.executable &&
                mapping.permissions.userAccessible == permissions.userAccessible,
            OS_TEST_MEMORY_RANDOM_PAGE_ROUND_TRIP, OS_TEST_MEMORY_RANDOM_SEED, iteration);
    }

    uint8_t stateStorage[OS_TEST_MEMORY_RANDOM_ALLOCATOR_STORAGE_SIZE_BYTES]{};
    bool allocatedPages[OS_TEST_MEMORY_RANDOM_ALLOCATOR_PAGE_COUNT]{};
    os::kernel::PhysicalFrameAllocator allocator{
        stateStorage,
        OS_TEST_MEMORY_RANDOM_ALLOCATOR_STORAGE_SIZE_BYTES,
    };
    const os::kernel::PhysicalMemoryMapEntry memoryMap[] = {
        {
            .baseAddress = 0ULL,
            .lengthBytes = OS_TEST_MEMORY_RANDOM_ALLOCATOR_MANAGED_SIZE_BYTES,
            .type = os::kernel::OS_KERNEL_MEMORY_MAP_USABLE_REGION_TYPE,
            .attributes = 0U,
        },
    };
    if (allocator.initialize(memoryMap, OS_TEST_MEMORY_RANDOM_MEMORY_MAP_ENTRY_COUNT,
                             OS_TEST_MEMORY_RANDOM_ALLOCATOR_MANAGED_SIZE_BYTES) !=
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded ||
        allocator.reserveRange(0ULL, OS_TEST_MEMORY_RANDOM_ALLOCATOR_RESERVED_SIZE_BYTES) !=
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded) {
        testContext.expect(false, OS_TEST_MEMORY_RANDOM_ALLOCATOR_COUNTS);
        return testContext.exitCode();
    }

    uint64_t allocatedPageCount = 0ULL;
    const uint64_t allocatablePageCount = OS_TEST_MEMORY_RANDOM_ALLOCATOR_PAGE_COUNT -
                                          OS_TEST_MEMORY_RANDOM_ALLOCATOR_RESERVED_PAGE_COUNT;
    for (uint64_t iteration = 0ULL; iteration < OS_TEST_MEMORY_RANDOM_ALLOCATOR_ITERATION_COUNT;
         ++iteration) {
        const bool shouldAllocate =
            allocatedPageCount == 0ULL ||
            (allocatedPageCount < allocatablePageCount &&
             (nextRandom(randomState) & OS_TEST_MEMORY_RANDOM_ALLOCATION_DECISION_BIT) != 0ULL);
        if (shouldAllocate) {
            os::kernel::PhysicalFrame frame{};
            const bool allocationSucceeded =
                allocator.allocate(frame) == os::kernel::PhysicalFrameAllocatorStatus::Succeeded;
            const uint64_t frameIndex =
                frame.physicalAddress / os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
            const bool frameWasFree =
                allocationSucceeded &&
                frameIndex >= OS_TEST_MEMORY_RANDOM_ALLOCATOR_RESERVED_PAGE_COUNT &&
                frameIndex < OS_TEST_MEMORY_RANDOM_ALLOCATOR_PAGE_COUNT &&
                !allocatedPages[frameIndex];
            testContext.expectRandom(frameWasFree, OS_TEST_MEMORY_RANDOM_ALLOCATOR_UNIQUE,
                                     OS_TEST_MEMORY_RANDOM_SEED, iteration);
            if (frameWasFree) {
                allocatedPages[frameIndex] = true;
                ++allocatedPageCount;
            }
        } else {
            uint64_t frameIndex =
                nextRandom(randomState) % OS_TEST_MEMORY_RANDOM_ALLOCATOR_PAGE_COUNT;
            while (!allocatedPages[frameIndex]) {
                frameIndex = (frameIndex + OS_TEST_MEMORY_RANDOM_NEXT_PAGE_OFFSET) %
                             OS_TEST_MEMORY_RANDOM_ALLOCATOR_PAGE_COUNT;
            }
            const os::kernel::PhysicalFrame frame{
                .physicalAddress = frameIndex * os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES,
            };
            if (allocator.release(frame) == os::kernel::PhysicalFrameAllocatorStatus::Succeeded) {
                allocatedPages[frameIndex] = false;
                --allocatedPageCount;
            }
        }

        const os::kernel::PhysicalFrameAllocatorStatistics statistics = allocator.statistics();
        testContext.expectRandom(
            statistics.allocatedFrameCount == allocatedPageCount &&
                statistics.freeFrameCount == allocatablePageCount - allocatedPageCount &&
                statistics.reservedFrameCount ==
                    OS_TEST_MEMORY_RANDOM_ALLOCATOR_RESERVED_PAGE_COUNT,
            OS_TEST_MEMORY_RANDOM_ALLOCATOR_COUNTS, OS_TEST_MEMORY_RANDOM_SEED, iteration);
    }

    return testContext.exitCode();
}

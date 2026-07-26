#include "os/kernel/memory/page_table.hpp"
#include "os/kernel/memory/physical_frame_allocator.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_MEMORY_RANDOM_SUITE_NAME = "kernel/memory_management/randomized";
constexpr std::string_view OS_TEST_MEMORY_RANDOM_PAGE_ROUND_TRIP =
    "随机页表项必须保持物理地址和权限";
constexpr std::string_view OS_TEST_MEMORY_RANDOM_ALLOCATOR_UNIQUE = "随机分配不能返回仍在使用的页";
constexpr std::string_view OS_TEST_MEMORY_RANDOM_ALLOCATOR_COUNTS =
    "随机分配释放后的统计必须与参考模型一致";
constexpr std::string_view OS_TEST_MEMORY_RANDOM_HIGH_RANGE =
    "随机高地址窗口分配必须严格落在请求范围内";
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
constexpr uint64_t OS_TEST_MEMORY_RANDOM_CACHE_DISABLE_PERMISSION_BIT = 0x8ULL;
constexpr uint64_t OS_TEST_MEMORY_RANDOM_ALLOCATION_DECISION_BIT = 0x1ULL;
constexpr uint64_t OS_TEST_MEMORY_RANDOM_NEXT_PAGE_OFFSET = 1ULL;
constexpr uint64_t OS_TEST_MEMORY_RANDOM_HIGH_RANGE_ITERATION_COUNT = 1024ULL;
constexpr uint64_t OS_TEST_MEMORY_RANDOM_HIGH_RANGE_BEGIN = 4ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr uint64_t OS_TEST_MEMORY_RANDOM_HIGH_RANGE_PAGE_COUNT = 256ULL;
constexpr uint64_t OS_TEST_MEMORY_RANDOM_HIGH_RANGE_SIZE_BYTES =
    OS_TEST_MEMORY_RANDOM_HIGH_RANGE_PAGE_COUNT * os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_TEST_MEMORY_RANDOM_HIGH_MANAGED_LIMIT =
    OS_TEST_MEMORY_RANDOM_HIGH_RANGE_BEGIN + OS_TEST_MEMORY_RANDOM_HIGH_RANGE_SIZE_BYTES;
constexpr uint64_t OS_TEST_MEMORY_RANDOM_HIGH_STORAGE_SIZE_BYTES =
    (OS_TEST_MEMORY_RANDOM_HIGH_MANAGED_LIMIT / os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES +
     OS_TEST_MEMORY_RANDOM_STORAGE_STATES_PER_BYTE - 1ULL) /
    OS_TEST_MEMORY_RANDOM_STORAGE_STATES_PER_BYTE;

[[nodiscard]] uint64_t NextRandom(uint64_t &state) noexcept {
    state ^= state >> OS_TEST_MEMORY_RANDOM_SHIFT_FIRST;
    state ^= state << OS_TEST_MEMORY_RANDOM_SHIFT_SECOND;
    state ^= state >> OS_TEST_MEMORY_RANDOM_SHIFT_THIRD;
    state *= OS_TEST_MEMORY_RANDOM_MULTIPLIER;
    return state;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_MEMORY_RANDOM_SUITE_NAME};
    uint64_t random_state = OS_TEST_MEMORY_RANDOM_SEED;

    for (uint64_t iteration = 0ULL; iteration < OS_TEST_MEMORY_RANDOM_PAGE_ITERATION_COUNT;
         ++iteration) {
        const uint64_t physical_address = NextRandom(random_state) &
                                          OS_TEST_MEMORY_RANDOM_PHYSICAL_ADDRESS_MASK &
                                          ~OS_TEST_MEMORY_RANDOM_PAGE_OFFSET_MASK;
        const uint64_t permission_bits = NextRandom(random_state);
        const os::kernel::PagePermissions permissions{
            .writable = (permission_bits & OS_TEST_MEMORY_RANDOM_WRITABLE_PERMISSION_BIT) != 0ULL,
            .executable =
                (permission_bits & OS_TEST_MEMORY_RANDOM_EXECUTABLE_PERMISSION_BIT) != 0ULL,
            .user_accessible =
                (permission_bits & OS_TEST_MEMORY_RANDOM_USER_PERMISSION_BIT) != 0ULL,
            .cache_disabled =
                (permission_bits & OS_TEST_MEMORY_RANDOM_CACHE_DISABLE_PERMISSION_BIT) != 0ULL,
        };
        const os::kernel::PageMapping mapping = os::kernel::DecodePageTableLeafEntry(
            os::kernel::EncodePageTableLeafEntry(physical_address, permissions));
        test_context.ExpectRandom(
            mapping.physical_address == physical_address &&
                mapping.permissions.writable == permissions.writable &&
                mapping.permissions.executable == permissions.executable &&
                mapping.permissions.user_accessible == permissions.user_accessible &&
                mapping.permissions.cache_disabled == permissions.cache_disabled,
            OS_TEST_MEMORY_RANDOM_PAGE_ROUND_TRIP, OS_TEST_MEMORY_RANDOM_SEED, iteration);
    }

    uint8_t state_storage[OS_TEST_MEMORY_RANDOM_ALLOCATOR_STORAGE_SIZE_BYTES]{};
    bool allocated_pages[OS_TEST_MEMORY_RANDOM_ALLOCATOR_PAGE_COUNT]{};
    os::kernel::PhysicalFrameAllocator allocator{
        state_storage,
        OS_TEST_MEMORY_RANDOM_ALLOCATOR_STORAGE_SIZE_BYTES,
    };
    const os::kernel::PhysicalMemoryMapEntry memory_map[] = {
        {
            .base_address = 0ULL,
            .length_bytes = OS_TEST_MEMORY_RANDOM_ALLOCATOR_MANAGED_SIZE_BYTES,
            .type = os::kernel::OS_KERNEL_MEMORY_MAP_USABLE_REGION_TYPE,
            .attributes = 0U,
        },
    };
    if (allocator.Initialize(memory_map, OS_TEST_MEMORY_RANDOM_MEMORY_MAP_ENTRY_COUNT,
                             OS_TEST_MEMORY_RANDOM_ALLOCATOR_MANAGED_SIZE_BYTES) !=
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded ||
        allocator.ReserveRange(0ULL, OS_TEST_MEMORY_RANDOM_ALLOCATOR_RESERVED_SIZE_BYTES) !=
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded) {
        test_context.Expect(false, OS_TEST_MEMORY_RANDOM_ALLOCATOR_COUNTS);
        return test_context.ExitCode();
    }

    uint64_t allocated_page_count = 0ULL;
    const uint64_t allocatable_page_count = OS_TEST_MEMORY_RANDOM_ALLOCATOR_PAGE_COUNT -
                                            OS_TEST_MEMORY_RANDOM_ALLOCATOR_RESERVED_PAGE_COUNT;
    for (uint64_t iteration = 0ULL; iteration < OS_TEST_MEMORY_RANDOM_ALLOCATOR_ITERATION_COUNT;
         ++iteration) {
        const bool should_allocate =
            allocated_page_count == 0ULL ||
            (allocated_page_count < allocatable_page_count &&
             (NextRandom(random_state) & OS_TEST_MEMORY_RANDOM_ALLOCATION_DECISION_BIT) != 0ULL);
        if (should_allocate) {
            os::kernel::PhysicalFrame frame{};
            const bool allocation_succeeded =
                allocator.Allocate(frame) == os::kernel::PhysicalFrameAllocatorStatus::Succeeded;
            const uint64_t frame_index =
                frame.physical_address / os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
            const bool frame_was_free =
                allocation_succeeded &&
                frame_index >= OS_TEST_MEMORY_RANDOM_ALLOCATOR_RESERVED_PAGE_COUNT &&
                frame_index < OS_TEST_MEMORY_RANDOM_ALLOCATOR_PAGE_COUNT &&
                !allocated_pages[frame_index];
            test_context.ExpectRandom(frame_was_free, OS_TEST_MEMORY_RANDOM_ALLOCATOR_UNIQUE,
                                      OS_TEST_MEMORY_RANDOM_SEED, iteration);
            if (frame_was_free) {
                allocated_pages[frame_index] = true;
                ++allocated_page_count;
            }
        } else {
            uint64_t frame_index =
                NextRandom(random_state) % OS_TEST_MEMORY_RANDOM_ALLOCATOR_PAGE_COUNT;
            while (!allocated_pages[frame_index]) {
                frame_index = (frame_index + OS_TEST_MEMORY_RANDOM_NEXT_PAGE_OFFSET) %
                              OS_TEST_MEMORY_RANDOM_ALLOCATOR_PAGE_COUNT;
            }
            const os::kernel::PhysicalFrame frame{
                .physical_address = frame_index * os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES,
            };
            if (allocator.Release(frame) == os::kernel::PhysicalFrameAllocatorStatus::Succeeded) {
                allocated_pages[frame_index] = false;
                --allocated_page_count;
            }
        }

        const os::kernel::PhysicalFrameAllocatorStatistics statistics = allocator.Statistics();
        test_context.ExpectRandom(
            statistics.allocated_frame_count == allocated_page_count &&
                statistics.free_frame_count == allocatable_page_count - allocated_page_count &&
                statistics.reserved_frame_count ==
                    OS_TEST_MEMORY_RANDOM_ALLOCATOR_RESERVED_PAGE_COUNT,
            OS_TEST_MEMORY_RANDOM_ALLOCATOR_COUNTS, OS_TEST_MEMORY_RANDOM_SEED, iteration);
    }

    static uint8_t high_state_storage[OS_TEST_MEMORY_RANDOM_HIGH_STORAGE_SIZE_BYTES]{};
    os::kernel::PhysicalFrameAllocator high_allocator{
        high_state_storage,
        OS_TEST_MEMORY_RANDOM_HIGH_STORAGE_SIZE_BYTES,
    };
    const os::kernel::PhysicalMemoryMapEntry high_memory_map[] = {
        {
            .base_address = OS_TEST_MEMORY_RANDOM_HIGH_RANGE_BEGIN,
            .length_bytes = OS_TEST_MEMORY_RANDOM_HIGH_RANGE_SIZE_BYTES,
            .type = os::kernel::OS_KERNEL_MEMORY_MAP_USABLE_REGION_TYPE,
            .attributes = 0U,
        },
    };
    if (high_allocator.Initialize(high_memory_map, OS_TEST_MEMORY_RANDOM_MEMORY_MAP_ENTRY_COUNT,
                                  OS_TEST_MEMORY_RANDOM_HIGH_MANAGED_LIMIT) !=
        os::kernel::PhysicalFrameAllocatorStatus::Succeeded) {
        test_context.Expect(false, OS_TEST_MEMORY_RANDOM_HIGH_RANGE);
        return test_context.ExitCode();
    }
    for (uint64_t iteration = 0ULL; iteration < OS_TEST_MEMORY_RANDOM_HIGH_RANGE_ITERATION_COUNT;
         ++iteration) {
        const uint64_t first_page_index =
            NextRandom(random_state) % OS_TEST_MEMORY_RANDOM_HIGH_RANGE_PAGE_COUNT;
        const uint64_t available_page_count =
            OS_TEST_MEMORY_RANDOM_HIGH_RANGE_PAGE_COUNT - first_page_index;
        const uint64_t requested_page_count =
            NextRandom(random_state) % available_page_count + 1ULL;
        const uint64_t minimum_address =
            OS_TEST_MEMORY_RANDOM_HIGH_RANGE_BEGIN +
            first_page_index * os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
        const uint64_t maximum_address_exclusive =
            minimum_address + requested_page_count * os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
        os::kernel::PhysicalFrame high_frame{};
        const bool allocation_valid = high_allocator.AllocateInRange(
                                          minimum_address, maximum_address_exclusive, high_frame) ==
                                          os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
                                      high_frame.physical_address >= minimum_address &&
                                      high_frame.physical_address < maximum_address_exclusive;
        const bool release_valid =
            allocation_valid && high_allocator.Release(high_frame) ==
                                    os::kernel::PhysicalFrameAllocatorStatus::Succeeded;
        test_context.ExpectRandom(release_valid, OS_TEST_MEMORY_RANDOM_HIGH_RANGE,
                                  OS_TEST_MEMORY_RANDOM_SEED, iteration);
    }

    return test_context.ExitCode();
}

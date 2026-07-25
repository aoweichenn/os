#include "os/kernel/physical_frame_allocator.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_BUDDY_RANDOM_SUITE_NAME =
    "kernel/buddy_frame_allocator/randomized";
constexpr std::string_view OS_TEST_BUDDY_RANDOM_OPERATION =
    "固定种子随机申请释放必须与逐页参考模型一致";
constexpr std::string_view OS_TEST_BUDDY_RANDOM_FAILURE_ATOMICITY =
    "随机耗尽和非法释放必须保持统计不变";
constexpr std::string_view OS_TEST_BUDDY_RANDOM_VALIDATION =
    "周期性完整校验必须保持 buddy 与页状态一致";
constexpr std::string_view OS_TEST_BUDDY_RANDOM_FINAL_BASELINE =
    "十万步后释放全部活动块必须恢复初始基线";
constexpr uint64_t OS_TEST_BUDDY_RANDOM_SEED = 0x425544445936344DULL;
constexpr uint64_t OS_TEST_BUDDY_RANDOM_ITERATION_COUNT = 100000ULL;
constexpr uint64_t OS_TEST_BUDDY_RANDOM_PAGE_COUNT = 1024ULL;
constexpr uint64_t OS_TEST_BUDDY_RANDOM_STATE_STORAGE_SIZE_BYTES =
    OS_TEST_BUDDY_RANDOM_PAGE_COUNT / 4ULL;
constexpr uint64_t OS_TEST_BUDDY_RANDOM_BUDDY_STORAGE_SIZE_BYTES = 516ULL;
constexpr uint64_t OS_TEST_BUDDY_RANDOM_MANAGED_SIZE_BYTES =
    OS_TEST_BUDDY_RANDOM_PAGE_COUNT * os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_TEST_BUDDY_RANDOM_RESERVED_PAGE_COUNT = 16ULL;
constexpr uint64_t OS_TEST_BUDDY_RANDOM_RESERVED_SIZE_BYTES =
    OS_TEST_BUDDY_RANDOM_RESERVED_PAGE_COUNT * os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_TEST_BUDDY_RANDOM_MEMORY_MAP_ENTRY_COUNT = 1ULL;
constexpr uint64_t OS_TEST_BUDDY_RANDOM_MAXIMUM_REQUEST_ORDER = 7ULL;
constexpr uint64_t OS_TEST_BUDDY_RANDOM_ALLOCATION_DECISION_MASK = 0x03ULL;
constexpr uint64_t OS_TEST_BUDDY_RANDOM_ALLOCATION_DECISION_LIMIT = 0x02ULL;
constexpr uint64_t OS_TEST_BUDDY_RANDOM_VALIDATION_INTERVAL = 257ULL;
constexpr uint64_t OS_TEST_BUDDY_RANDOM_MULTIPLIER = 0x2545F4914F6CDD1DULL;
constexpr uint64_t OS_TEST_BUDDY_RANDOM_SHIFT_FIRST = 12ULL;
constexpr uint64_t OS_TEST_BUDDY_RANDOM_SHIFT_SECOND = 25ULL;
constexpr uint64_t OS_TEST_BUDDY_RANDOM_SHIFT_THIRD = 27ULL;
constexpr uint64_t OS_TEST_BUDDY_RANDOM_SENTINEL_ADDRESS = 0xD15EA5EDBADDCAFEULL;

struct ActiveBlock final {
    os::kernel::PhysicalFrameBlock block;
};

[[nodiscard]] uint64_t NextRandom(uint64_t &state) noexcept {
    state ^= state >> OS_TEST_BUDDY_RANDOM_SHIFT_FIRST;
    state ^= state << OS_TEST_BUDDY_RANDOM_SHIFT_SECOND;
    state ^= state >> OS_TEST_BUDDY_RANDOM_SHIFT_THIRD;
    state *= OS_TEST_BUDDY_RANDOM_MULTIPLIER;
    return state;
}

[[nodiscard]] bool
EqualStatistics(const os::kernel::PhysicalFrameAllocatorStatistics left,
                const os::kernel::PhysicalFrameAllocatorStatistics right) noexcept {
    return left.managed_frame_count == right.managed_frame_count &&
           left.free_frame_count == right.free_frame_count &&
           left.allocated_frame_count == right.allocated_frame_count &&
           left.reserved_frame_count == right.reserved_frame_count;
}

[[nodiscard]] bool IsReferenceRangeFree(const bool *allocated_pages,
                                        const uint64_t first_page_index,
                                        const uint64_t page_count) noexcept {
    for (uint64_t page_offset = 0ULL; page_offset < page_count; ++page_offset) {
        if (allocated_pages[first_page_index + page_offset]) {
            return false;
        }
    }
    return true;
}

void SetReferenceRange(bool *allocated_pages, const uint64_t first_page_index,
                       const uint64_t page_count, const bool allocated) noexcept {
    for (uint64_t page_offset = 0ULL; page_offset < page_count; ++page_offset) {
        allocated_pages[first_page_index + page_offset] = allocated;
    }
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_BUDDY_RANDOM_SUITE_NAME};
    uint8_t state_storage[OS_TEST_BUDDY_RANDOM_STATE_STORAGE_SIZE_BYTES]{};
    uint8_t buddy_storage[OS_TEST_BUDDY_RANDOM_BUDDY_STORAGE_SIZE_BYTES]{};
    bool allocated_pages[OS_TEST_BUDDY_RANDOM_PAGE_COUNT]{};
    ActiveBlock active_blocks[OS_TEST_BUDDY_RANDOM_PAGE_COUNT]{};
    os::kernel::PhysicalFrameAllocator allocator{
        state_storage,
        OS_TEST_BUDDY_RANDOM_STATE_STORAGE_SIZE_BYTES,
    };
    const os::kernel::PhysicalMemoryMapEntry memory_map[] = {
        {
            .base_address = 0ULL,
            .length_bytes = OS_TEST_BUDDY_RANDOM_MANAGED_SIZE_BYTES,
            .type = os::kernel::OS_KERNEL_MEMORY_MAP_USABLE_REGION_TYPE,
            .attributes = 0U,
        },
    };
    if (allocator.ConfigureBuddyStorage(buddy_storage,
                                        OS_TEST_BUDDY_RANDOM_BUDDY_STORAGE_SIZE_BYTES) !=
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded ||
        allocator.Initialize(memory_map, OS_TEST_BUDDY_RANDOM_MEMORY_MAP_ENTRY_COUNT,
                             OS_TEST_BUDDY_RANDOM_MANAGED_SIZE_BYTES) !=
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded ||
        allocator.ReserveRange(0ULL, OS_TEST_BUDDY_RANDOM_RESERVED_SIZE_BYTES) !=
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded ||
        allocator.InitializeBuddy() != os::kernel::PhysicalFrameAllocatorStatus::Succeeded) {
        test_context.Expect(false, OS_TEST_BUDDY_RANDOM_OPERATION);
        return test_context.ExitCode();
    }

    const os::kernel::PhysicalFrameAllocatorStatistics baseline_statistics = allocator.Statistics();
    uint64_t random_state = OS_TEST_BUDDY_RANDOM_SEED;
    uint64_t active_block_count = 0ULL;
    uint64_t reference_allocated_page_count = 0ULL;
    uint64_t observed_allocation_failure_count = 0ULL;

    for (uint64_t iteration = 0ULL; iteration < OS_TEST_BUDDY_RANDOM_ITERATION_COUNT; ++iteration) {
        const bool should_allocate =
            active_block_count == 0ULL ||
            (active_block_count < OS_TEST_BUDDY_RANDOM_PAGE_COUNT &&
             (NextRandom(random_state) & OS_TEST_BUDDY_RANDOM_ALLOCATION_DECISION_MASK) <
                 OS_TEST_BUDDY_RANDOM_ALLOCATION_DECISION_LIMIT);
        if (should_allocate) {
            const uint64_t order =
                NextRandom(random_state) % (OS_TEST_BUDDY_RANDOM_MAXIMUM_REQUEST_ORDER + 1ULL);
            os::kernel::PhysicalFrameBlock block{
                .physical_address = OS_TEST_BUDDY_RANDOM_SENTINEL_ADDRESS,
                .order = OS_TEST_BUDDY_RANDOM_SENTINEL_ADDRESS,
            };
            const os::kernel::PhysicalFrameAllocatorStatistics before_statistics =
                allocator.Statistics();
            const os::kernel::PhysicalFrameAllocatorStatus status =
                allocator.AllocateBlock(order, block);
            if (status == os::kernel::PhysicalFrameAllocatorStatus::Succeeded) {
                const uint64_t page_count = 1ULL << order;
                const uint64_t first_page_index =
                    block.physical_address / os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
                const bool allocation_valid =
                    block.order == order &&
                    first_page_index >= OS_TEST_BUDDY_RANDOM_RESERVED_PAGE_COUNT &&
                    first_page_index + page_count <= OS_TEST_BUDDY_RANDOM_PAGE_COUNT &&
                    (first_page_index & (page_count - 1ULL)) == 0ULL &&
                    IsReferenceRangeFree(allocated_pages, first_page_index, page_count);
                test_context.ExpectRandom(allocation_valid, OS_TEST_BUDDY_RANDOM_OPERATION,
                                          OS_TEST_BUDDY_RANDOM_SEED, iteration);
                if (!allocation_valid) {
                    return test_context.ExitCode();
                }
                SetReferenceRange(allocated_pages, first_page_index, page_count, true);
                reference_allocated_page_count += page_count;
                active_blocks[active_block_count] = ActiveBlock{.block = block};
                ++active_block_count;
            } else {
                ++observed_allocation_failure_count;
                test_context.ExpectRandom(
                    status == os::kernel::PhysicalFrameAllocatorStatus::OutOfMemory &&
                        block.physical_address == OS_TEST_BUDDY_RANDOM_SENTINEL_ADDRESS &&
                        block.order == OS_TEST_BUDDY_RANDOM_SENTINEL_ADDRESS &&
                        EqualStatistics(allocator.Statistics(), before_statistics),
                    OS_TEST_BUDDY_RANDOM_FAILURE_ATOMICITY, OS_TEST_BUDDY_RANDOM_SEED, iteration);
            }
        } else {
            const uint64_t active_index = NextRandom(random_state) % active_block_count;
            const os::kernel::PhysicalFrameBlock block = active_blocks[active_index].block;
            const uint64_t page_count = 1ULL << block.order;
            const uint64_t first_page_index =
                block.physical_address / os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
            const bool release_valid =
                allocator.ReleaseBlock(block) ==
                    os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
                allocator.ReleaseBlock(block) ==
                    os::kernel::PhysicalFrameAllocatorStatus::FrameNotAllocated;
            test_context.ExpectRandom(release_valid, OS_TEST_BUDDY_RANDOM_FAILURE_ATOMICITY,
                                      OS_TEST_BUDDY_RANDOM_SEED, iteration);
            if (!release_valid) {
                return test_context.ExitCode();
            }
            SetReferenceRange(allocated_pages, first_page_index, page_count, false);
            reference_allocated_page_count -= page_count;
            --active_block_count;
            active_blocks[active_index] = active_blocks[active_block_count];
        }

        const os::kernel::PhysicalFrameAllocatorStatistics statistics = allocator.Statistics();
        test_context.ExpectRandom(
            statistics.allocated_frame_count == reference_allocated_page_count &&
                statistics.free_frame_count ==
                    baseline_statistics.free_frame_count - reference_allocated_page_count &&
                statistics.reserved_frame_count == baseline_statistics.reserved_frame_count,
            OS_TEST_BUDDY_RANDOM_OPERATION, OS_TEST_BUDDY_RANDOM_SEED, iteration);
        if (iteration % OS_TEST_BUDDY_RANDOM_VALIDATION_INTERVAL == 0ULL) {
            test_context.ExpectRandom(
                allocator.ValidateBuddy() == os::kernel::PhysicalFrameAllocatorStatus::Succeeded,
                OS_TEST_BUDDY_RANDOM_VALIDATION, OS_TEST_BUDDY_RANDOM_SEED, iteration);
        }
    }

    while (active_block_count > 0ULL) {
        --active_block_count;
        const os::kernel::PhysicalFrameBlock block = active_blocks[active_block_count].block;
        if (allocator.ReleaseBlock(block) != os::kernel::PhysicalFrameAllocatorStatus::Succeeded) {
            test_context.Expect(false, OS_TEST_BUDDY_RANDOM_FINAL_BASELINE);
            return test_context.ExitCode();
        }
    }
    const os::kernel::PhysicalFrameBuddyStatistics buddy_statistics = allocator.BuddyStatistics();
    test_context.Expect(
        observed_allocation_failure_count > 0ULL &&
            EqualStatistics(allocator.Statistics(), baseline_statistics) &&
            buddy_statistics.active_block_count == 0ULL &&
            buddy_statistics.successful_allocation_count == buddy_statistics.release_count &&
            buddy_statistics.split_count > 0ULL && buddy_statistics.merge_count > 0ULL &&
            allocator.ValidateBuddy() == os::kernel::PhysicalFrameAllocatorStatus::Succeeded,
        OS_TEST_BUDDY_RANDOM_FINAL_BASELINE);

    return test_context.ExitCode();
}

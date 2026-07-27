#include "os/user/user_heap.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_USER_HEAP_RANDOM_SUITE_NAME = "user/user_heap/randomized";
constexpr std::string_view OS_TEST_USER_HEAP_RANDOM_OPERATIONS =
    "十万步申请释放必须保持内容、区间隔离和内部结构一致";
constexpr std::string_view OS_TEST_USER_HEAP_RANDOM_DRAIN =
    "释放全部随机分配后必须合并为单个空闲块并保持计数守恒";

constexpr uint64_t OS_TEST_USER_HEAP_RANDOM_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_USER_HEAP_RANDOM_SINGLE_UNIT = 1ULL;
constexpr uint64_t OS_TEST_USER_HEAP_RANDOM_PAGE_SIZE_BYTES = 4096ULL;
constexpr uint64_t OS_TEST_USER_HEAP_RANDOM_STORAGE_SIZE_BYTES = 4ULL * 1024ULL * 1024ULL;
constexpr uint64_t OS_TEST_USER_HEAP_RANDOM_GROWTH_QUANTUM_BYTES = 64ULL * 1024ULL;
constexpr uint64_t OS_TEST_USER_HEAP_RANDOM_SLOT_COUNT = 512ULL;
constexpr uint64_t OS_TEST_USER_HEAP_RANDOM_ITERATION_COUNT = 100000ULL;
constexpr uint64_t OS_TEST_USER_HEAP_RANDOM_MAXIMUM_SIZE_BYTES = 8192ULL;
constexpr uint64_t OS_TEST_USER_HEAP_RANDOM_ALLOCATE_PERCENT = 61ULL;
constexpr uint64_t OS_TEST_USER_HEAP_RANDOM_PERCENT_SCALE = 100ULL;
constexpr uint64_t OS_TEST_USER_HEAP_RANDOM_VALIDATION_INTERVAL = 251ULL;
constexpr uint64_t OS_TEST_USER_HEAP_RANDOM_SEED = 0x4845415031303030ULL;
constexpr uint64_t OS_TEST_USER_HEAP_RANDOM_SHIFT_FIRST = 12ULL;
constexpr uint64_t OS_TEST_USER_HEAP_RANDOM_SHIFT_SECOND = 25ULL;
constexpr uint64_t OS_TEST_USER_HEAP_RANDOM_SHIFT_THIRD = 27ULL;
constexpr uint64_t OS_TEST_USER_HEAP_RANDOM_MULTIPLIER = 0x2545F4914F6CDD1DULL;
constexpr uint8_t OS_TEST_USER_HEAP_RANDOM_FIRST_PATTERN = 0x39U;
constexpr uint8_t OS_TEST_USER_HEAP_RANDOM_LAST_PATTERN = 0xC7U;
constexpr int64_t OS_TEST_USER_HEAP_RANDOM_BREAK_FAILURE = -1LL;

struct ProgramBreakContext final {
    uint8_t *base_address;
    uint64_t capacity_bytes;
    uint64_t current_size_bytes;
};

struct AllocationRecord final {
    uint8_t *allocation;
    uint64_t size_bytes;
};

alignas(OS_TEST_USER_HEAP_RANDOM_PAGE_SIZE_BYTES) uint8_t
    heap_storage[OS_TEST_USER_HEAP_RANDOM_STORAGE_SIZE_BYTES]{};

[[nodiscard]] uint64_t NextRandom(uint64_t &state) noexcept {
    state ^= state >> OS_TEST_USER_HEAP_RANDOM_SHIFT_FIRST;
    state ^= state << OS_TEST_USER_HEAP_RANDOM_SHIFT_SECOND;
    state ^= state >> OS_TEST_USER_HEAP_RANDOM_SHIFT_THIRD;
    state *= OS_TEST_USER_HEAP_RANDOM_MULTIPLIER;
    return state;
}

[[nodiscard]] int64_t ProgramBreak(void *const context, const uint64_t requested_address) noexcept {
    if (context == nullptr) {
        return OS_TEST_USER_HEAP_RANDOM_BREAK_FAILURE;
    }
    ProgramBreakContext &break_context = *static_cast<ProgramBreakContext *>(context);
    const uint64_t base_address = reinterpret_cast<uint64_t>(break_context.base_address);
    if (requested_address == OS_TEST_USER_HEAP_RANDOM_EMPTY_VALUE) {
        return static_cast<int64_t>(base_address + break_context.current_size_bytes);
    }
    if (requested_address < base_address ||
        requested_address > base_address + break_context.capacity_bytes) {
        return OS_TEST_USER_HEAP_RANDOM_BREAK_FAILURE;
    }
    break_context.current_size_bytes = requested_address - base_address;
    return static_cast<int64_t>(requested_address);
}

[[nodiscard]] bool RecordContentIsValid(const AllocationRecord &record) noexcept {
    if (record.allocation == nullptr || record.size_bytes == OS_TEST_USER_HEAP_RANDOM_EMPTY_VALUE) {
        return false;
    }
    if (record.size_bytes == OS_TEST_USER_HEAP_RANDOM_SINGLE_UNIT) {
        return record.allocation[OS_TEST_USER_HEAP_RANDOM_EMPTY_VALUE] ==
               OS_TEST_USER_HEAP_RANDOM_LAST_PATTERN;
    }
    return record.allocation[OS_TEST_USER_HEAP_RANDOM_EMPTY_VALUE] ==
               OS_TEST_USER_HEAP_RANDOM_FIRST_PATTERN &&
           record.allocation[record.size_bytes - OS_TEST_USER_HEAP_RANDOM_SINGLE_UNIT] ==
               OS_TEST_USER_HEAP_RANDOM_LAST_PATTERN;
}

[[nodiscard]] bool RangesOverlap(const AllocationRecord &left,
                                 const AllocationRecord &right) noexcept {
    const uint64_t left_begin = reinterpret_cast<uint64_t>(left.allocation);
    const uint64_t right_begin = reinterpret_cast<uint64_t>(right.allocation);
    return left_begin < right_begin + right.size_bytes &&
           right_begin < left_begin + left.size_bytes;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_USER_HEAP_RANDOM_SUITE_NAME};
    ProgramBreakContext break_context{
        .base_address = heap_storage,
        .capacity_bytes = sizeof(heap_storage),
        .current_size_bytes = OS_TEST_USER_HEAP_RANDOM_EMPTY_VALUE,
    };
    os::user::UserHeap heap{};
    AllocationRecord records[OS_TEST_USER_HEAP_RANDOM_SLOT_COUNT]{};
    bool operations_valid =
        heap.Initialize(os::user::UserHeapConfiguration{
            .context = &break_context,
            .program_break_operation = ProgramBreak,
            .maximum_capacity_bytes = OS_TEST_USER_HEAP_RANDOM_STORAGE_SIZE_BYTES,
            .page_size_bytes = OS_TEST_USER_HEAP_RANDOM_PAGE_SIZE_BYTES,
            .growth_quantum_bytes = OS_TEST_USER_HEAP_RANDOM_GROWTH_QUANTUM_BYTES,
        }) == os::user::UserHeapStatus::Succeeded;
    uint64_t random_state = OS_TEST_USER_HEAP_RANDOM_SEED;
    for (uint64_t iteration = OS_TEST_USER_HEAP_RANDOM_EMPTY_VALUE;
         operations_valid && iteration < OS_TEST_USER_HEAP_RANDOM_ITERATION_COUNT; ++iteration) {
        const uint64_t slot_index = NextRandom(random_state) % OS_TEST_USER_HEAP_RANDOM_SLOT_COUNT;
        AllocationRecord &record = records[slot_index];
        const bool allocate_operation =
            record.allocation == nullptr &&
            NextRandom(random_state) % OS_TEST_USER_HEAP_RANDOM_PERCENT_SCALE <
                OS_TEST_USER_HEAP_RANDOM_ALLOCATE_PERCENT;
        if (allocate_operation) {
            const uint64_t size_bytes =
                NextRandom(random_state) % OS_TEST_USER_HEAP_RANDOM_MAXIMUM_SIZE_BYTES +
                OS_TEST_USER_HEAP_RANDOM_SINGLE_UNIT;
            void *allocation = nullptr;
            const os::user::UserHeapStatus status = heap.Allocate(size_bytes, allocation);
            if (status == os::user::UserHeapStatus::Succeeded) {
                record = AllocationRecord{
                    .allocation = static_cast<uint8_t *>(allocation),
                    .size_bytes = size_bytes,
                };
                for (uint64_t comparison_index = OS_TEST_USER_HEAP_RANDOM_EMPTY_VALUE;
                     comparison_index < OS_TEST_USER_HEAP_RANDOM_SLOT_COUNT; ++comparison_index) {
                    if (comparison_index != slot_index &&
                        records[comparison_index].allocation != nullptr &&
                        RangesOverlap(record, records[comparison_index])) {
                        operations_valid = false;
                    }
                }
                record.allocation[OS_TEST_USER_HEAP_RANDOM_EMPTY_VALUE] =
                    OS_TEST_USER_HEAP_RANDOM_FIRST_PATTERN;
                record.allocation[size_bytes - OS_TEST_USER_HEAP_RANDOM_SINGLE_UNIT] =
                    OS_TEST_USER_HEAP_RANDOM_LAST_PATTERN;
            } else if (status != os::user::UserHeapStatus::CapacityExhausted &&
                       status != os::user::UserHeapStatus::ProgramBreakFailed) {
                operations_valid = false;
            }
        } else if (record.allocation != nullptr) {
            operations_valid =
                RecordContentIsValid(record) &&
                heap.Release(record.allocation) == os::user::UserHeapStatus::Succeeded;
            record = AllocationRecord{};
        }

        if (operations_valid && iteration % OS_TEST_USER_HEAP_RANDOM_VALIDATION_INTERVAL ==
                                    OS_TEST_USER_HEAP_RANDOM_EMPTY_VALUE) {
            operations_valid = heap.Validate() == os::user::UserHeapStatus::Succeeded;
            for (uint64_t record_index = OS_TEST_USER_HEAP_RANDOM_EMPTY_VALUE;
                 operations_valid && record_index < OS_TEST_USER_HEAP_RANDOM_SLOT_COUNT;
                 ++record_index) {
                if (records[record_index].allocation != nullptr) {
                    operations_valid = RecordContentIsValid(records[record_index]);
                }
            }
        }
    }
    test_context.Expect(operations_valid, OS_TEST_USER_HEAP_RANDOM_OPERATIONS);

    bool drain_valid = operations_valid;
    for (uint64_t record_index = OS_TEST_USER_HEAP_RANDOM_EMPTY_VALUE;
         drain_valid && record_index < OS_TEST_USER_HEAP_RANDOM_SLOT_COUNT; ++record_index) {
        if (records[record_index].allocation == nullptr) {
            continue;
        }
        drain_valid =
            RecordContentIsValid(records[record_index]) &&
            heap.Release(records[record_index].allocation) == os::user::UserHeapStatus::Succeeded;
    }
    const os::user::UserHeapStatistics final_statistics = heap.Statistics();
    drain_valid =
        drain_valid && heap.Validate() == os::user::UserHeapStatus::Succeeded &&
        final_statistics.active_allocation_count == OS_TEST_USER_HEAP_RANDOM_EMPTY_VALUE &&
        final_statistics.active_requested_bytes == OS_TEST_USER_HEAP_RANDOM_EMPTY_VALUE &&
        final_statistics.free_block_count == OS_TEST_USER_HEAP_RANDOM_SINGLE_UNIT &&
        final_statistics.successful_allocation_count == final_statistics.release_count &&
        final_statistics.capacity_bytes == break_context.current_size_bytes &&
        final_statistics.capacity_bytes <= OS_TEST_USER_HEAP_RANDOM_STORAGE_SIZE_BYTES;
    test_context.Expect(drain_valid, OS_TEST_USER_HEAP_RANDOM_DRAIN);
    return test_context.ExitCode();
}

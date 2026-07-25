#include "os/kernel/kernel_heap.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

struct ActiveAllocation final {
    void *address;
    uint64_t size_bytes;
    uint64_t alignment_bytes;
    uint8_t first_pattern;
    uint8_t last_pattern;
    bool active;
};

constexpr std::string_view OS_TEST_KERNEL_HEAP_RANDOM_SUITE_NAME = "kernel/kernel_heap/randomized";
constexpr std::string_view OS_TEST_KERNEL_HEAP_RANDOM_OPERATION =
    "固定种子随机分配释放必须保持对齐、数据、统计和拓扑不变量";
constexpr std::string_view OS_TEST_KERNEL_HEAP_RANDOM_DRAIN =
    "随机序列结束后释放全部对象必须恢复完整空闲堆";

constexpr uint64_t OS_TEST_KERNEL_HEAP_RANDOM_SEED = 0x4845415056313031ULL;
constexpr uint64_t OS_TEST_KERNEL_HEAP_RANDOM_ITERATION_COUNT = 100000ULL;
constexpr uint64_t OS_TEST_KERNEL_HEAP_RANDOM_BUFFER_SIZE_BYTES = 64ULL * 1024ULL;
constexpr uint64_t OS_TEST_KERNEL_HEAP_RANDOM_HEADER_SIZE_BYTES = 48ULL;
constexpr uint64_t OS_TEST_KERNEL_HEAP_RANDOM_RECORD_COUNT = 256ULL;
constexpr uint64_t OS_TEST_KERNEL_HEAP_RANDOM_MAXIMUM_SIZE_BYTES = 512ULL;
constexpr uint64_t OS_TEST_KERNEL_HEAP_RANDOM_MAXIMUM_ALIGNMENT_SHIFT = 12ULL;
constexpr uint64_t OS_TEST_KERNEL_HEAP_RANDOM_VALIDATION_INTERVAL = 64ULL;
constexpr uint64_t OS_TEST_KERNEL_HEAP_RANDOM_ALLOCATION_DECISION_BIT = 1ULL;
constexpr uint64_t OS_TEST_KERNEL_HEAP_RANDOM_SHIFT_FIRST = 12ULL;
constexpr uint64_t OS_TEST_KERNEL_HEAP_RANDOM_SHIFT_SECOND = 25ULL;
constexpr uint64_t OS_TEST_KERNEL_HEAP_RANDOM_SHIFT_THIRD = 27ULL;
constexpr uint64_t OS_TEST_KERNEL_HEAP_RANDOM_MULTIPLIER = 0x2545F4914F6CDD1DULL;
constexpr uint64_t OS_TEST_KERNEL_HEAP_RANDOM_FIRST_PATTERN_MASK = 0xFFULL;
constexpr uint64_t OS_TEST_KERNEL_HEAP_RANDOM_LAST_PATTERN_SHIFT = 8ULL;
constexpr uint64_t OS_TEST_KERNEL_HEAP_RANDOM_LAST_PATTERN_MASK = 0xFFULL;
constexpr uint64_t OS_TEST_KERNEL_HEAP_RANDOM_NEXT_VALUE = 1ULL;
constexpr uint64_t OS_TEST_KERNEL_HEAP_RANDOM_INVALID_POINTER_VALUE = 0x5678ULL;

[[nodiscard]] uint64_t NextRandom(uint64_t &state) noexcept {
    state ^= state >> OS_TEST_KERNEL_HEAP_RANDOM_SHIFT_FIRST;
    state ^= state << OS_TEST_KERNEL_HEAP_RANDOM_SHIFT_SECOND;
    state ^= state >> OS_TEST_KERNEL_HEAP_RANDOM_SHIFT_THIRD;
    state *= OS_TEST_KERNEL_HEAP_RANDOM_MULTIPLIER;
    return state;
}

[[nodiscard]] uint64_t FindInactiveRecord(const ActiveAllocation *const records) noexcept {
    for (uint64_t record_index = 0ULL; record_index < OS_TEST_KERNEL_HEAP_RANDOM_RECORD_COUNT;
         ++record_index) {
        if (!records[record_index].active) {
            return record_index;
        }
    }
    return OS_TEST_KERNEL_HEAP_RANDOM_RECORD_COUNT;
}

[[nodiscard]] uint64_t FindActiveRecord(const ActiveAllocation *const records,
                                        const uint64_t ordinal) noexcept {
    uint64_t remaining_ordinal = ordinal;
    for (uint64_t record_index = 0ULL; record_index < OS_TEST_KERNEL_HEAP_RANDOM_RECORD_COUNT;
         ++record_index) {
        if (records[record_index].active) {
            if (remaining_ordinal == 0ULL) {
                return record_index;
            }
            --remaining_ordinal;
        }
    }
    return OS_TEST_KERNEL_HEAP_RANDOM_RECORD_COUNT;
}

[[nodiscard]] bool ActivePatternsValid(const ActiveAllocation *const records) noexcept {
    for (uint64_t record_index = 0ULL; record_index < OS_TEST_KERNEL_HEAP_RANDOM_RECORD_COUNT;
         ++record_index) {
        if (!records[record_index].active) {
            continue;
        }
        const uint8_t *const bytes =
            reinterpret_cast<const uint8_t *>(records[record_index].address);
        if (bytes[0ULL] != records[record_index].first_pattern ||
            bytes[records[record_index].size_bytes - OS_TEST_KERNEL_HEAP_RANDOM_NEXT_VALUE] !=
                records[record_index].last_pattern) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool DoesNotOverlapActiveRecords(const ActiveAllocation *const records,
                                               const uint64_t ignored_record_index,
                                               const uint64_t address,
                                               const uint64_t size_bytes) noexcept {
    const uint64_t end_address = address + size_bytes;
    for (uint64_t record_index = 0ULL; record_index < OS_TEST_KERNEL_HEAP_RANDOM_RECORD_COUNT;
         ++record_index) {
        if (!records[record_index].active || record_index == ignored_record_index) {
            continue;
        }
        const uint64_t active_begin_address =
            reinterpret_cast<uint64_t>(records[record_index].address);
        const uint64_t active_end_address = active_begin_address + records[record_index].size_bytes;
        if (address < active_end_address && active_begin_address < end_address) {
            return false;
        }
    }
    return true;
}
}

int main() {
    os::test::TestContext test_context{OS_TEST_KERNEL_HEAP_RANDOM_SUITE_NAME};
    alignas(4096) uint8_t heap_buffer[OS_TEST_KERNEL_HEAP_RANDOM_BUFFER_SIZE_BYTES]{};
    os::kernel::KernelHeap heap{};
    if (heap.Initialize(reinterpret_cast<uint64_t>(heap_buffer),
                        OS_TEST_KERNEL_HEAP_RANDOM_BUFFER_SIZE_BYTES) !=
        os::kernel::KernelHeapStatus::Succeeded) {
        test_context.Expect(false, OS_TEST_KERNEL_HEAP_RANDOM_OPERATION);
        return test_context.ExitCode();
    }

    ActiveAllocation records[OS_TEST_KERNEL_HEAP_RANDOM_RECORD_COUNT]{};
    uint64_t random_state = OS_TEST_KERNEL_HEAP_RANDOM_SEED;
    uint64_t active_allocation_count = 0ULL;
    uint64_t active_requested_bytes = 0ULL;
    uint64_t successful_allocation_count = 0ULL;
    uint64_t release_count = 0ULL;
    for (uint64_t iteration = 0ULL; iteration < OS_TEST_KERNEL_HEAP_RANDOM_ITERATION_COUNT;
         ++iteration) {
        const uint64_t inactive_record_index = FindInactiveRecord(records);
        const bool should_allocate =
            active_allocation_count == 0ULL ||
            (inactive_record_index < OS_TEST_KERNEL_HEAP_RANDOM_RECORD_COUNT &&
             (NextRandom(random_state) & OS_TEST_KERNEL_HEAP_RANDOM_ALLOCATION_DECISION_BIT) !=
                 0ULL);
        bool operation_valid = true;
        if (should_allocate) {
            const uint64_t random_value = NextRandom(random_state);
            const uint64_t requested_size_bytes =
                random_value % OS_TEST_KERNEL_HEAP_RANDOM_MAXIMUM_SIZE_BYTES +
                OS_TEST_KERNEL_HEAP_RANDOM_NEXT_VALUE;
            const uint64_t alignment_shift =
                NextRandom(random_state) % (OS_TEST_KERNEL_HEAP_RANDOM_MAXIMUM_ALIGNMENT_SHIFT +
                                            OS_TEST_KERNEL_HEAP_RANDOM_NEXT_VALUE);
            const uint64_t alignment_bytes = OS_TEST_KERNEL_HEAP_RANDOM_NEXT_VALUE
                                             << alignment_shift;
            void *allocation =
                reinterpret_cast<void *>(OS_TEST_KERNEL_HEAP_RANDOM_INVALID_POINTER_VALUE);
            const os::kernel::KernelHeapStatus allocation_status =
                heap.TryAllocate(requested_size_bytes, alignment_bytes, allocation);
            if (allocation_status == os::kernel::KernelHeapStatus::Succeeded) {
                const uint64_t allocation_address = reinterpret_cast<uint64_t>(allocation);
                operation_valid =
                    inactive_record_index < OS_TEST_KERNEL_HEAP_RANDOM_RECORD_COUNT &&
                    (allocation_address &
                     (alignment_bytes - OS_TEST_KERNEL_HEAP_RANDOM_NEXT_VALUE)) == 0ULL &&
                    DoesNotOverlapActiveRecords(records, inactive_record_index, allocation_address,
                                                requested_size_bytes);
                if (operation_valid) {
                    const uint8_t first_pattern = static_cast<uint8_t>(
                        random_value & OS_TEST_KERNEL_HEAP_RANDOM_FIRST_PATTERN_MASK);
                    const uint8_t last_pattern = static_cast<uint8_t>(
                        (random_value >> OS_TEST_KERNEL_HEAP_RANDOM_LAST_PATTERN_SHIFT) &
                        OS_TEST_KERNEL_HEAP_RANDOM_LAST_PATTERN_MASK);
                    uint8_t *const bytes = reinterpret_cast<uint8_t *>(allocation);
                    bytes[0ULL] = first_pattern;
                    bytes[requested_size_bytes - OS_TEST_KERNEL_HEAP_RANDOM_NEXT_VALUE] =
                        requested_size_bytes == OS_TEST_KERNEL_HEAP_RANDOM_NEXT_VALUE
                            ? first_pattern
                            : last_pattern;
                    records[inactive_record_index] = ActiveAllocation{
                        .address = allocation,
                        .size_bytes = requested_size_bytes,
                        .alignment_bytes = alignment_bytes,
                        .first_pattern = first_pattern,
                        .last_pattern =
                            requested_size_bytes == OS_TEST_KERNEL_HEAP_RANDOM_NEXT_VALUE
                                ? first_pattern
                                : last_pattern,
                        .active = true,
                    };
                    ++active_allocation_count;
                    active_requested_bytes += requested_size_bytes;
                    ++successful_allocation_count;
                }
            } else {
                operation_valid = allocation_status == os::kernel::KernelHeapStatus::OutOfMemory &&
                                  reinterpret_cast<uint64_t>(allocation) ==
                                      OS_TEST_KERNEL_HEAP_RANDOM_INVALID_POINTER_VALUE;
            }
        } else {
            const uint64_t active_ordinal = NextRandom(random_state) % active_allocation_count;
            const uint64_t record_index = FindActiveRecord(records, active_ordinal);
            operation_valid = record_index < OS_TEST_KERNEL_HEAP_RANDOM_RECORD_COUNT &&
                              heap.TryRelease(records[record_index].address) ==
                                  os::kernel::KernelHeapStatus::Succeeded;
            if (operation_valid) {
                active_requested_bytes -= records[record_index].size_bytes;
                records[record_index].active = false;
                --active_allocation_count;
                ++release_count;
            }
        }

        const os::kernel::KernelHeapStatistics statistics = heap.Statistics();
        const bool validation_due =
            iteration % OS_TEST_KERNEL_HEAP_RANDOM_VALIDATION_INTERVAL == 0ULL;
        operation_valid =
            operation_valid && ActivePatternsValid(records) &&
            statistics.allocation_count == active_allocation_count &&
            statistics.active_requested_bytes == active_requested_bytes &&
            statistics.successful_allocation_count == successful_allocation_count &&
            statistics.release_count == release_count &&
            statistics.remaining_bytes + statistics.consumed_bytes ==
                OS_TEST_KERNEL_HEAP_RANDOM_BUFFER_SIZE_BYTES &&
            (!validation_due || heap.Validate() == os::kernel::KernelHeapStatus::Succeeded);
        test_context.ExpectRandom(operation_valid, OS_TEST_KERNEL_HEAP_RANDOM_OPERATION,
                                  OS_TEST_KERNEL_HEAP_RANDOM_SEED, iteration);
    }

    bool drained = true;
    for (uint64_t record_index = 0ULL; record_index < OS_TEST_KERNEL_HEAP_RANDOM_RECORD_COUNT;
         ++record_index) {
        if (!records[record_index].active) {
            continue;
        }
        drained = drained && heap.TryRelease(records[record_index].address) ==
                                 os::kernel::KernelHeapStatus::Succeeded;
        records[record_index].active = false;
        ++release_count;
    }
    const os::kernel::KernelHeapStatistics final_statistics = heap.Statistics();
    test_context.Expect(
        drained && heap.Validate() == os::kernel::KernelHeapStatus::Succeeded &&
            final_statistics.consumed_bytes == 0ULL &&
            final_statistics.remaining_bytes == OS_TEST_KERNEL_HEAP_RANDOM_BUFFER_SIZE_BYTES &&
            final_statistics.allocation_count == 0ULL &&
            final_statistics.active_requested_bytes == 0ULL &&
            final_statistics.successful_allocation_count == successful_allocation_count &&
            final_statistics.release_count == release_count &&
            final_statistics.release_count == final_statistics.successful_allocation_count &&
            final_statistics.largest_free_allocation_bytes ==
                OS_TEST_KERNEL_HEAP_RANDOM_BUFFER_SIZE_BYTES -
                    OS_TEST_KERNEL_HEAP_RANDOM_HEADER_SIZE_BYTES,
        OS_TEST_KERNEL_HEAP_RANDOM_DRAIN);
    return test_context.ExitCode();
}

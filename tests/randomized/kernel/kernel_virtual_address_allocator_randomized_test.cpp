#include "os/kernel/kernel_virtual_address_allocator.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

enum class ModelPageState : uint8_t {
    Free,
    Allocated,
    Reserved,
};

struct ActiveRange final {
    os::kernel::KernelVirtualAddressRange range;
    bool active;
};

constexpr std::string_view OS_TEST_KVA_RANDOM_SUITE_NAME =
    "kernel/kernel_virtual_address_allocator/randomized";
constexpr std::string_view OS_TEST_KVA_RANDOM_OPERATION =
    "十万次固定种子操作必须与独立逐页模型保持一致";
constexpr std::string_view OS_TEST_KVA_RANDOM_DRAIN =
    "释放全部随机区间后必须只保留启动保留区并恢复最大空洞";

constexpr uint64_t OS_TEST_KVA_RANDOM_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_KVA_RANDOM_SINGLE_UNIT = 1ULL;
constexpr uint64_t OS_TEST_KVA_RANDOM_WINDOW_BASE = 0xFFFFC90000000000ULL;
constexpr uint64_t OS_TEST_KVA_RANDOM_WINDOW_PAGE_COUNT = 512ULL;
constexpr uint64_t OS_TEST_KVA_RANDOM_DESCRIPTOR_CAPACITY = 64ULL;
constexpr uint64_t OS_TEST_KVA_RANDOM_RECORD_CAPACITY = 64ULL;
constexpr uint64_t OS_TEST_KVA_RANDOM_RESERVATION_COUNT = 4ULL;
constexpr uint64_t OS_TEST_KVA_RANDOM_FIRST_RESERVATION_BEGIN = 0ULL;
constexpr uint64_t OS_TEST_KVA_RANDOM_FIRST_RESERVATION_PAGE_COUNT = 3ULL;
constexpr uint64_t OS_TEST_KVA_RANDOM_SECOND_RESERVATION_BEGIN = 97ULL;
constexpr uint64_t OS_TEST_KVA_RANDOM_SECOND_RESERVATION_PAGE_COUNT = 5ULL;
constexpr uint64_t OS_TEST_KVA_RANDOM_THIRD_RESERVATION_BEGIN = 300ULL;
constexpr uint64_t OS_TEST_KVA_RANDOM_THIRD_RESERVATION_PAGE_COUNT = 7ULL;
constexpr uint64_t OS_TEST_KVA_RANDOM_FOURTH_RESERVATION_BEGIN = 500ULL;
constexpr uint64_t OS_TEST_KVA_RANDOM_FOURTH_RESERVATION_PAGE_COUNT = 12ULL;
constexpr uint64_t OS_TEST_KVA_RANDOM_RESERVED_PAGE_COUNT =
    OS_TEST_KVA_RANDOM_FIRST_RESERVATION_PAGE_COUNT +
    OS_TEST_KVA_RANDOM_SECOND_RESERVATION_PAGE_COUNT +
    OS_TEST_KVA_RANDOM_THIRD_RESERVATION_PAGE_COUNT +
    OS_TEST_KVA_RANDOM_FOURTH_RESERVATION_PAGE_COUNT;
constexpr uint64_t OS_TEST_KVA_RANDOM_SEED = 0x4B564152414E444FULL;
constexpr uint64_t OS_TEST_KVA_RANDOM_ITERATION_COUNT = 100000ULL;
constexpr uint64_t OS_TEST_KVA_RANDOM_MAXIMUM_ALLOCATION_PAGE_COUNT = 24ULL;
constexpr uint64_t OS_TEST_KVA_RANDOM_MAXIMUM_ALIGNMENT_SHIFT = 5ULL;
constexpr uint64_t OS_TEST_KVA_RANDOM_VALIDATION_INTERVAL = 257ULL;
constexpr uint64_t OS_TEST_KVA_RANDOM_ALLOCATION_DECISION_BIT = 1ULL;
constexpr uint64_t OS_TEST_KVA_RANDOM_SHIFT_FIRST = 12ULL;
constexpr uint64_t OS_TEST_KVA_RANDOM_SHIFT_SECOND = 25ULL;
constexpr uint64_t OS_TEST_KVA_RANDOM_SHIFT_THIRD = 27ULL;
constexpr uint64_t OS_TEST_KVA_RANDOM_MULTIPLIER = 0x2545F4914F6CDD1DULL;
constexpr uint64_t OS_TEST_KVA_RANDOM_INVALID_OUTPUT_ADDRESS = 0x12345000ULL;
constexpr uint64_t OS_TEST_KVA_RANDOM_INVALID_OUTPUT_PAGE_COUNT = 0x66ULL;

[[nodiscard]] uint64_t NextRandom(uint64_t &state) noexcept {
    state ^= state >> OS_TEST_KVA_RANDOM_SHIFT_FIRST;
    state ^= state << OS_TEST_KVA_RANDOM_SHIFT_SECOND;
    state ^= state >> OS_TEST_KVA_RANDOM_SHIFT_THIRD;
    state *= OS_TEST_KVA_RANDOM_MULTIPLIER;
    return state;
}

[[nodiscard]] uint64_t PageAddress(const uint64_t page_index) noexcept {
    return OS_TEST_KVA_RANDOM_WINDOW_BASE +
           page_index * os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
}

[[nodiscard]] uint64_t FindInactiveRecord(const ActiveRange *const records) noexcept {
    for (uint64_t record_index = OS_TEST_KVA_RANDOM_EMPTY_VALUE;
         record_index < OS_TEST_KVA_RANDOM_RECORD_CAPACITY; ++record_index) {
        if (!records[record_index].active) {
            return record_index;
        }
    }
    return OS_TEST_KVA_RANDOM_RECORD_CAPACITY;
}

[[nodiscard]] uint64_t FindActiveRecord(const ActiveRange *const records,
                                        const uint64_t ordinal) noexcept {
    uint64_t remaining_ordinal = ordinal;
    for (uint64_t record_index = OS_TEST_KVA_RANDOM_EMPTY_VALUE;
         record_index < OS_TEST_KVA_RANDOM_RECORD_CAPACITY; ++record_index) {
        if (!records[record_index].active) {
            continue;
        }
        if (remaining_ordinal == OS_TEST_KVA_RANDOM_EMPTY_VALUE) {
            return record_index;
        }
        --remaining_ordinal;
    }
    return OS_TEST_KVA_RANDOM_RECORD_CAPACITY;
}

void MarkPages(ModelPageState *const page_states, const uint64_t begin_page_index,
               const uint64_t page_count, const ModelPageState state) noexcept {
    for (uint64_t page_offset = OS_TEST_KVA_RANDOM_EMPTY_VALUE; page_offset < page_count;
         ++page_offset) {
        page_states[begin_page_index + page_offset] = state;
    }
}

[[nodiscard]] bool TryFindBestFit(const ModelPageState *const page_states,
                                  const uint64_t page_count, const uint64_t alignment_page_count,
                                  uint64_t &begin_page_index) noexcept {
    bool range_found = false;
    uint64_t best_gap_page_count = UINT64_MAX;
    uint64_t scan_page_index = OS_TEST_KVA_RANDOM_EMPTY_VALUE;
    const uint64_t window_base_page_index =
        OS_TEST_KVA_RANDOM_WINDOW_BASE / os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
    const uint64_t alignment_mask = alignment_page_count - OS_TEST_KVA_RANDOM_SINGLE_UNIT;
    while (scan_page_index < OS_TEST_KVA_RANDOM_WINDOW_PAGE_COUNT) {
        while (scan_page_index < OS_TEST_KVA_RANDOM_WINDOW_PAGE_COUNT &&
               page_states[scan_page_index] != ModelPageState::Free) {
            ++scan_page_index;
        }
        const uint64_t gap_begin_page_index = scan_page_index;
        while (scan_page_index < OS_TEST_KVA_RANDOM_WINDOW_PAGE_COUNT &&
               page_states[scan_page_index] == ModelPageState::Free) {
            ++scan_page_index;
        }
        const uint64_t gap_end_page_index = scan_page_index;
        const uint64_t gap_page_count = gap_end_page_index - gap_begin_page_index;
        const uint64_t absolute_begin_page_index = window_base_page_index + gap_begin_page_index;
        const uint64_t aligned_absolute_page_index =
            (absolute_begin_page_index + alignment_mask) & ~alignment_mask;
        const uint64_t candidate_page_index = aligned_absolute_page_index - window_base_page_index;
        if (candidate_page_index >= gap_begin_page_index &&
            candidate_page_index <= gap_end_page_index &&
            page_count <= gap_end_page_index - candidate_page_index &&
            gap_page_count < best_gap_page_count) {
            range_found = true;
            begin_page_index = candidate_page_index;
            best_gap_page_count = gap_page_count;
            if (gap_page_count == page_count) {
                break;
            }
        }
    }
    return range_found;
}

[[nodiscard]] uint64_t CountPages(const ModelPageState *const page_states,
                                  const ModelPageState state) noexcept {
    uint64_t page_count = OS_TEST_KVA_RANDOM_EMPTY_VALUE;
    for (uint64_t page_index = OS_TEST_KVA_RANDOM_EMPTY_VALUE;
         page_index < OS_TEST_KVA_RANDOM_WINDOW_PAGE_COUNT; ++page_index) {
        if (page_states[page_index] == state) {
            ++page_count;
        }
    }
    return page_count;
}

[[nodiscard]] uint64_t
CalculateLargestFreeRangePageCount(const ModelPageState *const page_states) noexcept {
    uint64_t largest_page_count = OS_TEST_KVA_RANDOM_EMPTY_VALUE;
    uint64_t current_page_count = OS_TEST_KVA_RANDOM_EMPTY_VALUE;
    for (uint64_t page_index = OS_TEST_KVA_RANDOM_EMPTY_VALUE;
         page_index < OS_TEST_KVA_RANDOM_WINDOW_PAGE_COUNT; ++page_index) {
        if (page_states[page_index] == ModelPageState::Free) {
            ++current_page_count;
            if (current_page_count > largest_page_count) {
                largest_page_count = current_page_count;
            }
        } else {
            current_page_count = OS_TEST_KVA_RANDOM_EMPTY_VALUE;
        }
    }
    return largest_page_count;
}

[[nodiscard]] bool
StatisticsMatch(const os::kernel::KernelVirtualAddressAllocatorStatistics &statistics,
                const ModelPageState *const page_states, const uint64_t active_allocation_count,
                const uint64_t successful_allocation_count, const uint64_t release_count) noexcept {
    const uint64_t allocated_page_count = CountPages(page_states, ModelPageState::Allocated);
    const uint64_t free_page_count = CountPages(page_states, ModelPageState::Free);
    return statistics.capacity_page_count == OS_TEST_KVA_RANDOM_WINDOW_PAGE_COUNT &&
           statistics.allocated_page_count == allocated_page_count &&
           statistics.reserved_page_count == OS_TEST_KVA_RANDOM_RESERVED_PAGE_COUNT &&
           statistics.free_page_count == free_page_count &&
           statistics.active_allocation_count == active_allocation_count &&
           statistics.active_descriptor_count ==
               active_allocation_count + OS_TEST_KVA_RANDOM_RESERVATION_COUNT &&
           statistics.successful_allocation_count == successful_allocation_count &&
           statistics.release_count == release_count &&
           statistics.largest_free_range_page_count ==
               CalculateLargestFreeRangePageCount(page_states);
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_KVA_RANDOM_SUITE_NAME};
    os::kernel::KernelVirtualAddressRangeDescriptor
        descriptors[OS_TEST_KVA_RANDOM_DESCRIPTOR_CAPACITY]{};
    os::kernel::KernelVirtualAddressAllocator allocator{};
    ModelPageState page_states[OS_TEST_KVA_RANDOM_WINDOW_PAGE_COUNT]{};
    ActiveRange records[OS_TEST_KVA_RANDOM_RECORD_CAPACITY]{};

    bool initialized =
        allocator.Initialize(OS_TEST_KVA_RANDOM_WINDOW_BASE, OS_TEST_KVA_RANDOM_WINDOW_PAGE_COUNT,
                             descriptors, OS_TEST_KVA_RANDOM_DESCRIPTOR_CAPACITY) ==
        os::kernel::KernelVirtualAddressAllocatorStatus::Succeeded;
    const uint64_t reservation_begins[OS_TEST_KVA_RANDOM_RESERVATION_COUNT] = {
        OS_TEST_KVA_RANDOM_FIRST_RESERVATION_BEGIN,
        OS_TEST_KVA_RANDOM_SECOND_RESERVATION_BEGIN,
        OS_TEST_KVA_RANDOM_THIRD_RESERVATION_BEGIN,
        OS_TEST_KVA_RANDOM_FOURTH_RESERVATION_BEGIN,
    };
    const uint64_t reservation_page_counts[OS_TEST_KVA_RANDOM_RESERVATION_COUNT] = {
        OS_TEST_KVA_RANDOM_FIRST_RESERVATION_PAGE_COUNT,
        OS_TEST_KVA_RANDOM_SECOND_RESERVATION_PAGE_COUNT,
        OS_TEST_KVA_RANDOM_THIRD_RESERVATION_PAGE_COUNT,
        OS_TEST_KVA_RANDOM_FOURTH_RESERVATION_PAGE_COUNT,
    };
    for (uint64_t reservation_index = OS_TEST_KVA_RANDOM_EMPTY_VALUE;
         reservation_index < OS_TEST_KVA_RANDOM_RESERVATION_COUNT; ++reservation_index) {
        initialized = initialized &&
                      allocator.ReserveRange(PageAddress(reservation_begins[reservation_index]),
                                             reservation_page_counts[reservation_index]) ==
                          os::kernel::KernelVirtualAddressAllocatorStatus::Succeeded;
        MarkPages(page_states, reservation_begins[reservation_index],
                  reservation_page_counts[reservation_index], ModelPageState::Reserved);
    }
    if (!initialized) {
        test_context.Expect(false, OS_TEST_KVA_RANDOM_OPERATION);
        return test_context.ExitCode();
    }

    uint64_t random_state = OS_TEST_KVA_RANDOM_SEED;
    uint64_t active_allocation_count = OS_TEST_KVA_RANDOM_EMPTY_VALUE;
    uint64_t successful_allocation_count = OS_TEST_KVA_RANDOM_EMPTY_VALUE;
    uint64_t release_count = OS_TEST_KVA_RANDOM_EMPTY_VALUE;
    for (uint64_t iteration = OS_TEST_KVA_RANDOM_EMPTY_VALUE;
         iteration < OS_TEST_KVA_RANDOM_ITERATION_COUNT; ++iteration) {
        const uint64_t inactive_record_index = FindInactiveRecord(records);
        const bool should_allocate =
            active_allocation_count == OS_TEST_KVA_RANDOM_EMPTY_VALUE ||
            (inactive_record_index < OS_TEST_KVA_RANDOM_RECORD_CAPACITY &&
             (NextRandom(random_state) & OS_TEST_KVA_RANDOM_ALLOCATION_DECISION_BIT) !=
                 OS_TEST_KVA_RANDOM_EMPTY_VALUE);
        bool operation_valid = true;
        if (should_allocate) {
            const uint64_t page_count =
                NextRandom(random_state) % OS_TEST_KVA_RANDOM_MAXIMUM_ALLOCATION_PAGE_COUNT +
                OS_TEST_KVA_RANDOM_SINGLE_UNIT;
            const uint64_t alignment_page_count =
                OS_TEST_KVA_RANDOM_SINGLE_UNIT
                << (NextRandom(random_state) % OS_TEST_KVA_RANDOM_MAXIMUM_ALIGNMENT_SHIFT);
            uint64_t expected_begin_page_index = OS_TEST_KVA_RANDOM_EMPTY_VALUE;
            const bool model_range_found = TryFindBestFit(
                page_states, page_count, alignment_page_count, expected_begin_page_index);
            os::kernel::KernelVirtualAddressRange range{
                .begin_address = OS_TEST_KVA_RANDOM_INVALID_OUTPUT_ADDRESS,
                .page_count = OS_TEST_KVA_RANDOM_INVALID_OUTPUT_PAGE_COUNT,
            };
            const os::kernel::KernelVirtualAddressAllocatorStatus status =
                allocator.TryAllocate(page_count, alignment_page_count, range);
            const bool metadata_exhausted =
                active_allocation_count + OS_TEST_KVA_RANDOM_RESERVATION_COUNT ==
                OS_TEST_KVA_RANDOM_DESCRIPTOR_CAPACITY;
            if (metadata_exhausted) {
                operation_valid =
                    status == os::kernel::KernelVirtualAddressAllocatorStatus::MetadataExhausted &&
                    range.begin_address == OS_TEST_KVA_RANDOM_INVALID_OUTPUT_ADDRESS &&
                    range.page_count == OS_TEST_KVA_RANDOM_INVALID_OUTPUT_PAGE_COUNT;
            } else if (!model_range_found) {
                operation_valid =
                    status ==
                        os::kernel::KernelVirtualAddressAllocatorStatus::OutOfVirtualAddressSpace &&
                    range.begin_address == OS_TEST_KVA_RANDOM_INVALID_OUTPUT_ADDRESS &&
                    range.page_count == OS_TEST_KVA_RANDOM_INVALID_OUTPUT_PAGE_COUNT;
            } else {
                operation_valid =
                    status == os::kernel::KernelVirtualAddressAllocatorStatus::Succeeded &&
                    inactive_record_index < OS_TEST_KVA_RANDOM_RECORD_CAPACITY &&
                    range.begin_address == PageAddress(expected_begin_page_index) &&
                    range.page_count == page_count;
                if (operation_valid) {
                    MarkPages(page_states, expected_begin_page_index, page_count,
                              ModelPageState::Allocated);
                    records[inactive_record_index] = ActiveRange{
                        .range = range,
                        .active = true,
                    };
                    ++active_allocation_count;
                    ++successful_allocation_count;
                }
            }
        } else {
            const uint64_t active_ordinal = NextRandom(random_state) % active_allocation_count;
            const uint64_t record_index = FindActiveRecord(records, active_ordinal);
            operation_valid = record_index < OS_TEST_KVA_RANDOM_RECORD_CAPACITY &&
                              allocator.TryRelease(records[record_index].range) ==
                                  os::kernel::KernelVirtualAddressAllocatorStatus::Succeeded;
            if (operation_valid) {
                const uint64_t begin_page_index =
                    (records[record_index].range.begin_address - OS_TEST_KVA_RANDOM_WINDOW_BASE) /
                    os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
                MarkPages(page_states, begin_page_index, records[record_index].range.page_count,
                          ModelPageState::Free);
                records[record_index].active = false;
                --active_allocation_count;
                ++release_count;
            }
        }

        const bool validation_due =
            iteration % OS_TEST_KVA_RANDOM_VALIDATION_INTERVAL == OS_TEST_KVA_RANDOM_EMPTY_VALUE;
        operation_valid =
            operation_valid &&
            StatisticsMatch(allocator.Statistics(), page_states, active_allocation_count,
                            successful_allocation_count, release_count) &&
            (!validation_due ||
             allocator.Validate() == os::kernel::KernelVirtualAddressAllocatorStatus::Succeeded);
        test_context.ExpectRandom(operation_valid, OS_TEST_KVA_RANDOM_OPERATION,
                                  OS_TEST_KVA_RANDOM_SEED, iteration);
    }

    bool drained = true;
    for (uint64_t record_index = OS_TEST_KVA_RANDOM_EMPTY_VALUE;
         record_index < OS_TEST_KVA_RANDOM_RECORD_CAPACITY; ++record_index) {
        if (!records[record_index].active) {
            continue;
        }
        const bool range_released = allocator.TryRelease(records[record_index].range) ==
                                    os::kernel::KernelVirtualAddressAllocatorStatus::Succeeded;
        if (range_released) {
            const uint64_t begin_page_index =
                (records[record_index].range.begin_address - OS_TEST_KVA_RANDOM_WINDOW_BASE) /
                os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
            MarkPages(page_states, begin_page_index, records[record_index].range.page_count,
                      ModelPageState::Free);
            records[record_index].active = false;
            --active_allocation_count;
            ++release_count;
        }
        drained = range_released && drained;
    }
    test_context.Expect(
        drained &&
            allocator.Validate() == os::kernel::KernelVirtualAddressAllocatorStatus::Succeeded &&
            StatisticsMatch(allocator.Statistics(), page_states, active_allocation_count,
                            successful_allocation_count, release_count) &&
            active_allocation_count == OS_TEST_KVA_RANDOM_EMPTY_VALUE &&
            allocator.Statistics().active_descriptor_count == OS_TEST_KVA_RANDOM_RESERVATION_COUNT,
        OS_TEST_KVA_RANDOM_DRAIN);
    return test_context.ExitCode();
}

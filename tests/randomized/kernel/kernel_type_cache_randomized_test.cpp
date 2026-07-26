#include "os/kernel/kernel_type_cache.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

struct alignas(32) RandomizedObject final {
    uint64_t values[4];
};

struct ActiveObject final {
    RandomizedObject *object;
    uint64_t first_pattern;
    uint64_t last_pattern;
    bool active;
};

constexpr std::string_view OS_TEST_KERNEL_TYPE_CACHE_RANDOM_SUITE_NAME =
    "kernel/kernel_type_cache/randomized";
constexpr std::string_view OS_TEST_KERNEL_TYPE_CACHE_RANDOM_OPERATION =
    "固定种子十万步申请释放必须保持槽位、数据、统计和空闲链不变量";
constexpr std::string_view OS_TEST_KERNEL_TYPE_CACHE_RANDOM_DRAIN =
    "随机序列结束后必须释放全部槽位并归还堆后备块";

constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_RANDOM_SEED = 0x5459504543414348ULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_RANDOM_ITERATION_COUNT = 100000ULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_RANDOM_HEAP_SIZE_BYTES = 64ULL * 1024ULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_RANDOM_HEAP_ALIGNMENT_BYTES = 4096ULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_RANDOM_CAPACITY = 256ULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_RANDOM_VALIDATION_INTERVAL = 257ULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_RANDOM_DUPLICATE_RELEASE_INTERVAL = 509ULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_RANDOM_ALLOCATION_DECISION_BIT = 1ULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_RANDOM_SHIFT_FIRST = 12ULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_RANDOM_SHIFT_SECOND = 25ULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_RANDOM_SHIFT_THIRD = 27ULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_RANDOM_MULTIPLIER = 0x2545F4914F6CDD1DULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_RANDOM_FIRST_VALUE_INDEX = 0ULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_RANDOM_LAST_VALUE_INDEX = 3ULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_RANDOM_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_RANDOM_NEXT_VALUE = 1ULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_RANDOM_INVALID_POINTER_VALUE = 0x1234ULL;

[[nodiscard]] uint64_t NextRandom(uint64_t &state) noexcept {
    state ^= state >> OS_TEST_KERNEL_TYPE_CACHE_RANDOM_SHIFT_FIRST;
    state ^= state << OS_TEST_KERNEL_TYPE_CACHE_RANDOM_SHIFT_SECOND;
    state ^= state >> OS_TEST_KERNEL_TYPE_CACHE_RANDOM_SHIFT_THIRD;
    state *= OS_TEST_KERNEL_TYPE_CACHE_RANDOM_MULTIPLIER;
    return state;
}

[[nodiscard]] uint64_t FindInactiveRecord(const ActiveObject *const records) noexcept {
    for (uint64_t record_index = OS_TEST_KERNEL_TYPE_CACHE_RANDOM_EMPTY_VALUE;
         record_index < OS_TEST_KERNEL_TYPE_CACHE_RANDOM_CAPACITY; ++record_index) {
        if (!records[record_index].active) {
            return record_index;
        }
    }
    return OS_TEST_KERNEL_TYPE_CACHE_RANDOM_CAPACITY;
}

[[nodiscard]] uint64_t FindActiveRecord(const ActiveObject *const records,
                                        const uint64_t active_ordinal) noexcept {
    uint64_t remaining_ordinal = active_ordinal;
    for (uint64_t record_index = OS_TEST_KERNEL_TYPE_CACHE_RANDOM_EMPTY_VALUE;
         record_index < OS_TEST_KERNEL_TYPE_CACHE_RANDOM_CAPACITY; ++record_index) {
        if (!records[record_index].active) {
            continue;
        }
        if (remaining_ordinal == OS_TEST_KERNEL_TYPE_CACHE_RANDOM_EMPTY_VALUE) {
            return record_index;
        }
        --remaining_ordinal;
    }
    return OS_TEST_KERNEL_TYPE_CACHE_RANDOM_CAPACITY;
}

[[nodiscard]] bool ActiveObjectsValid(const ActiveObject *const records) noexcept {
    for (uint64_t record_index = OS_TEST_KERNEL_TYPE_CACHE_RANDOM_EMPTY_VALUE;
         record_index < OS_TEST_KERNEL_TYPE_CACHE_RANDOM_CAPACITY; ++record_index) {
        if (!records[record_index].active) {
            continue;
        }
        if (records[record_index]
                    .object->values[OS_TEST_KERNEL_TYPE_CACHE_RANDOM_FIRST_VALUE_INDEX] !=
                records[record_index].first_pattern ||
            records[record_index]
                    .object->values[OS_TEST_KERNEL_TYPE_CACHE_RANDOM_LAST_VALUE_INDEX] !=
                records[record_index].last_pattern) {
            return false;
        }
        for (uint64_t other_index = record_index + OS_TEST_KERNEL_TYPE_CACHE_RANDOM_NEXT_VALUE;
             other_index < OS_TEST_KERNEL_TYPE_CACHE_RANDOM_CAPACITY; ++other_index) {
            if (records[other_index].active &&
                records[record_index].object == records[other_index].object) {
                return false;
            }
        }
    }
    return true;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_KERNEL_TYPE_CACHE_RANDOM_SUITE_NAME};
    alignas(OS_TEST_KERNEL_TYPE_CACHE_RANDOM_HEAP_ALIGNMENT_BYTES)
        uint8_t heap_buffer[OS_TEST_KERNEL_TYPE_CACHE_RANDOM_HEAP_SIZE_BYTES]{};
    os::kernel::KernelHeap heap{};
    os::kernel::KernelTypeCache<RandomizedObject> cache{};
    if (heap.Initialize(reinterpret_cast<uint64_t>(heap_buffer),
                        OS_TEST_KERNEL_TYPE_CACHE_RANDOM_HEAP_SIZE_BYTES) !=
            os::kernel::KernelHeapStatus::Succeeded ||
        cache.Initialize(heap, OS_TEST_KERNEL_TYPE_CACHE_RANDOM_CAPACITY) !=
            os::kernel::KernelTypeCacheStatus::Succeeded) {
        test_context.Expect(false, OS_TEST_KERNEL_TYPE_CACHE_RANDOM_OPERATION);
        return test_context.ExitCode();
    }

    ActiveObject records[OS_TEST_KERNEL_TYPE_CACHE_RANDOM_CAPACITY]{};
    uint64_t random_state = OS_TEST_KERNEL_TYPE_CACHE_RANDOM_SEED;
    uint64_t active_object_count = OS_TEST_KERNEL_TYPE_CACHE_RANDOM_EMPTY_VALUE;
    uint64_t successful_allocation_count = OS_TEST_KERNEL_TYPE_CACHE_RANDOM_EMPTY_VALUE;
    uint64_t release_count = OS_TEST_KERNEL_TYPE_CACHE_RANDOM_EMPTY_VALUE;
    for (uint64_t iteration = OS_TEST_KERNEL_TYPE_CACHE_RANDOM_EMPTY_VALUE;
         iteration < OS_TEST_KERNEL_TYPE_CACHE_RANDOM_ITERATION_COUNT; ++iteration) {
        const uint64_t inactive_record_index = FindInactiveRecord(records);
        const bool should_allocate =
            active_object_count == OS_TEST_KERNEL_TYPE_CACHE_RANDOM_EMPTY_VALUE ||
            (inactive_record_index < OS_TEST_KERNEL_TYPE_CACHE_RANDOM_CAPACITY &&
             (NextRandom(random_state) &
              OS_TEST_KERNEL_TYPE_CACHE_RANDOM_ALLOCATION_DECISION_BIT) !=
                 OS_TEST_KERNEL_TYPE_CACHE_RANDOM_EMPTY_VALUE);
        bool operation_valid = true;
        if (should_allocate) {
            RandomizedObject *object = reinterpret_cast<RandomizedObject *>(
                OS_TEST_KERNEL_TYPE_CACHE_RANDOM_INVALID_POINTER_VALUE);
            operation_valid =
                cache.TryAcquire(object) == os::kernel::KernelTypeCacheStatus::Succeeded &&
                inactive_record_index < OS_TEST_KERNEL_TYPE_CACHE_RANDOM_CAPACITY;
            if (operation_valid) {
                const uint64_t first_pattern = NextRandom(random_state);
                const uint64_t last_pattern = NextRandom(random_state);
                object->values[OS_TEST_KERNEL_TYPE_CACHE_RANDOM_FIRST_VALUE_INDEX] = first_pattern;
                object->values[OS_TEST_KERNEL_TYPE_CACHE_RANDOM_LAST_VALUE_INDEX] = last_pattern;
                records[inactive_record_index] = ActiveObject{
                    .object = object,
                    .first_pattern = first_pattern,
                    .last_pattern = last_pattern,
                    .active = true,
                };
                ++active_object_count;
                ++successful_allocation_count;
            }
        } else {
            const uint64_t active_ordinal = NextRandom(random_state) % active_object_count;
            const uint64_t record_index = FindActiveRecord(records, active_ordinal);
            operation_valid = record_index < OS_TEST_KERNEL_TYPE_CACHE_RANDOM_CAPACITY;
            RandomizedObject *released_object = nullptr;
            if (operation_valid) {
                released_object = records[record_index].object;
            }
            operation_valid = operation_valid && cache.TryRelease(released_object) ==
                                                     os::kernel::KernelTypeCacheStatus::Succeeded;
            if (operation_valid) {
                records[record_index].active = false;
                --active_object_count;
                ++release_count;
                if (iteration % OS_TEST_KERNEL_TYPE_CACHE_RANDOM_DUPLICATE_RELEASE_INTERVAL ==
                    OS_TEST_KERNEL_TYPE_CACHE_RANDOM_EMPTY_VALUE) {
                    operation_valid = cache.TryRelease(released_object) ==
                                      os::kernel::KernelTypeCacheStatus::ObjectNotActive;
                }
            }
        }

        const os::kernel::KernelTypeCacheStatistics statistics = cache.Statistics();
        const bool validation_due =
            iteration % OS_TEST_KERNEL_TYPE_CACHE_RANDOM_VALIDATION_INTERVAL ==
            OS_TEST_KERNEL_TYPE_CACHE_RANDOM_EMPTY_VALUE;
        operation_valid =
            operation_valid && ActiveObjectsValid(records) &&
            statistics.active_object_count == active_object_count &&
            statistics.free_object_count + statistics.active_object_count ==
                OS_TEST_KERNEL_TYPE_CACHE_RANDOM_CAPACITY &&
            statistics.successful_allocation_count == successful_allocation_count &&
            statistics.release_count == release_count &&
            (!validation_due || cache.Validate() == os::kernel::KernelTypeCacheStatus::Succeeded);
        test_context.ExpectRandom(operation_valid, OS_TEST_KERNEL_TYPE_CACHE_RANDOM_OPERATION,
                                  OS_TEST_KERNEL_TYPE_CACHE_RANDOM_SEED, iteration);
    }

    bool drained = true;
    for (uint64_t record_index = OS_TEST_KERNEL_TYPE_CACHE_RANDOM_EMPTY_VALUE;
         record_index < OS_TEST_KERNEL_TYPE_CACHE_RANDOM_CAPACITY; ++record_index) {
        if (!records[record_index].active) {
            continue;
        }
        drained = drained && cache.TryRelease(records[record_index].object) ==
                                 os::kernel::KernelTypeCacheStatus::Succeeded;
        records[record_index].active = false;
        ++release_count;
    }
    const os::kernel::KernelTypeCacheStatistics final_statistics = cache.Statistics();
    const bool cache_drained =
        drained && cache.Validate() == os::kernel::KernelTypeCacheStatus::Succeeded &&
        final_statistics.active_object_count == OS_TEST_KERNEL_TYPE_CACHE_RANDOM_EMPTY_VALUE &&
        final_statistics.free_object_count == OS_TEST_KERNEL_TYPE_CACHE_RANDOM_CAPACITY &&
        final_statistics.successful_allocation_count == successful_allocation_count &&
        final_statistics.release_count == release_count &&
        final_statistics.release_count == final_statistics.successful_allocation_count;
    const bool cache_destroyed =
        cache_drained && cache.Destroy() == os::kernel::KernelTypeCacheStatus::Succeeded;
    const os::kernel::KernelHeapStatistics heap_statistics = heap.Statistics();
    test_context.Expect(
        cache_destroyed && heap.Validate() == os::kernel::KernelHeapStatus::Succeeded &&
            heap_statistics.consumed_bytes == OS_TEST_KERNEL_TYPE_CACHE_RANDOM_EMPTY_VALUE &&
            heap_statistics.allocation_count == OS_TEST_KERNEL_TYPE_CACHE_RANDOM_EMPTY_VALUE &&
            heap_statistics.release_count == heap_statistics.successful_allocation_count,
        OS_TEST_KERNEL_TYPE_CACHE_RANDOM_DRAIN);
    return test_context.ExitCode();
}

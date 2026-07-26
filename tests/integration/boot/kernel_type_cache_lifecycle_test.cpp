#include "os/kernel/kernel_type_cache.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

struct alignas(8) SmallObject final {
    uint64_t values[3];
};

struct alignas(64) CacheLineObject final {
    uint64_t values[8];
};

struct alignas(256) LargeObject final {
    uint64_t values[64];
};

constexpr std::string_view OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_SUITE_NAME =
    "boot/kernel_type_cache_lifecycle/integration";
constexpr std::string_view OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_INITIALIZATION =
    "多个尺寸与对齐的缓存必须共享同一个通用堆";
constexpr std::string_view OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_MIXED_OBJECTS =
    "跨缓存释放不能破坏仍活动对象的数据或类型对齐";
constexpr std::string_view OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_DESTROY_GUARD =
    "含活动对象的缓存不得提前归还后备块";
constexpr std::string_view OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_FINAL_STATE =
    "乱序销毁全部缓存后通用堆必须恢复单一连续空闲区";

constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_HEAP_SIZE_BYTES = 64ULL * 1024ULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_HEAP_ALIGNMENT_BYTES = 4096ULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_SMALL_CAPACITY = 64ULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_CACHE_LINE_CAPACITY = 32ULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_LARGE_CAPACITY = 8ULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_SMALL_ALIGNMENT_BYTES = 8ULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_CACHE_LINE_ALIGNMENT_BYTES = 64ULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_LARGE_ALIGNMENT_BYTES = 256ULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_FIRST_VALUE_INDEX = 0ULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_CACHE_LINE_LAST_VALUE_INDEX = 7ULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_LARGE_LAST_VALUE_INDEX = 63ULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_SMALL_PATTERN = 0x534D414C4C4F424AULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_CACHE_LINE_PATTERN = 0x43414348454C494EULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_LARGE_PATTERN = 0x4C415247454F424AULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_NEXT_VALUE = 1ULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_ALTERNATING_STEP = 2ULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_CACHE_COUNT = 3ULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_HEAP_HEADER_SIZE_BYTES = 48ULL;

}

int main() {
    os::test::TestContext test_context{OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_SUITE_NAME};
    alignas(OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_HEAP_ALIGNMENT_BYTES)
        uint8_t heap_buffer[OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_HEAP_SIZE_BYTES]{};
    os::kernel::KernelHeap heap{};
    os::kernel::KernelTypeCache<SmallObject> small_cache{};
    os::kernel::KernelTypeCache<CacheLineObject> cache_line_cache{};
    os::kernel::KernelTypeCache<LargeObject> large_cache{};

    const bool initialized =
        heap.Initialize(reinterpret_cast<uint64_t>(heap_buffer),
                        OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_HEAP_SIZE_BYTES) ==
            os::kernel::KernelHeapStatus::Succeeded &&
        small_cache.Initialize(heap, OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_SMALL_CAPACITY) ==
            os::kernel::KernelTypeCacheStatus::Succeeded &&
        cache_line_cache.Initialize(heap,
                                    OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_CACHE_LINE_CAPACITY) ==
            os::kernel::KernelTypeCacheStatus::Succeeded &&
        large_cache.Initialize(heap, OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_LARGE_CAPACITY) ==
            os::kernel::KernelTypeCacheStatus::Succeeded &&
        heap.Statistics().allocation_count == OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_CACHE_COUNT &&
        small_cache.Validate() == os::kernel::KernelTypeCacheStatus::Succeeded &&
        cache_line_cache.Validate() == os::kernel::KernelTypeCacheStatus::Succeeded &&
        large_cache.Validate() == os::kernel::KernelTypeCacheStatus::Succeeded;
    test_context.Expect(initialized, OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_INITIALIZATION);
    if (!initialized) {
        return test_context.ExitCode();
    }

    SmallObject *small_objects[OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_SMALL_CAPACITY]{};
    CacheLineObject *cache_line_objects[OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_CACHE_LINE_CAPACITY]{};
    LargeObject *large_objects[OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_LARGE_CAPACITY]{};
    bool mixed_objects_valid = true;
    for (uint64_t object_index = OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_EMPTY_VALUE;
         object_index < OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_SMALL_CAPACITY; ++object_index) {
        mixed_objects_valid = mixed_objects_valid &&
                              small_cache.TryAcquire(small_objects[object_index]) ==
                                  os::kernel::KernelTypeCacheStatus::Succeeded &&
                              (reinterpret_cast<uint64_t>(small_objects[object_index]) &
                               (OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_SMALL_ALIGNMENT_BYTES -
                                OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_NEXT_VALUE)) ==
                                  OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_EMPTY_VALUE;
        small_objects[object_index]->values[OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_FIRST_VALUE_INDEX] =
            OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_SMALL_PATTERN + object_index;
    }
    for (uint64_t object_index = OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_EMPTY_VALUE;
         object_index < OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_CACHE_LINE_CAPACITY; ++object_index) {
        mixed_objects_valid = mixed_objects_valid &&
                              cache_line_cache.TryAcquire(cache_line_objects[object_index]) ==
                                  os::kernel::KernelTypeCacheStatus::Succeeded &&
                              (reinterpret_cast<uint64_t>(cache_line_objects[object_index]) &
                               (OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_CACHE_LINE_ALIGNMENT_BYTES -
                                OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_NEXT_VALUE)) ==
                                  OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_EMPTY_VALUE;
        cache_line_objects[object_index]
            ->values[OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_FIRST_VALUE_INDEX] =
            OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_CACHE_LINE_PATTERN + object_index;
        cache_line_objects[object_index]
            ->values[OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_CACHE_LINE_LAST_VALUE_INDEX] =
            OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_CACHE_LINE_PATTERN - object_index;
    }
    for (uint64_t object_index = OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_EMPTY_VALUE;
         object_index < OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_LARGE_CAPACITY; ++object_index) {
        mixed_objects_valid = mixed_objects_valid &&
                              large_cache.TryAcquire(large_objects[object_index]) ==
                                  os::kernel::KernelTypeCacheStatus::Succeeded &&
                              (reinterpret_cast<uint64_t>(large_objects[object_index]) &
                               (OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_LARGE_ALIGNMENT_BYTES -
                                OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_NEXT_VALUE)) ==
                                  OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_EMPTY_VALUE;
        large_objects[object_index]->values[OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_FIRST_VALUE_INDEX] =
            OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_LARGE_PATTERN + object_index;
        large_objects[object_index]
            ->values[OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_LARGE_LAST_VALUE_INDEX] =
            OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_LARGE_PATTERN - object_index;
    }
    test_context.Expect(mixed_objects_valid &&
                            heap.Validate() == os::kernel::KernelHeapStatus::Succeeded,
                        OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_MIXED_OBJECTS);

    bool alternating_release_valid = true;
    for (uint64_t object_index = OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_EMPTY_VALUE;
         object_index < OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_SMALL_CAPACITY;
         object_index += OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_ALTERNATING_STEP) {
        alternating_release_valid =
            alternating_release_valid && small_cache.TryRelease(small_objects[object_index]) ==
                                             os::kernel::KernelTypeCacheStatus::Succeeded;
    }
    for (uint64_t object_index = OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_EMPTY_VALUE;
         object_index < OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_LARGE_CAPACITY;
         object_index += OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_ALTERNATING_STEP) {
        alternating_release_valid =
            alternating_release_valid && large_cache.TryRelease(large_objects[object_index]) ==
                                             os::kernel::KernelTypeCacheStatus::Succeeded;
    }
    bool preserved_patterns = true;
    for (uint64_t object_index = OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_EMPTY_VALUE;
         object_index < OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_CACHE_LINE_CAPACITY; ++object_index) {
        preserved_patterns =
            preserved_patterns &&
            cache_line_objects[object_index]
                    ->values[OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_FIRST_VALUE_INDEX] ==
                OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_CACHE_LINE_PATTERN + object_index &&
            cache_line_objects[object_index]
                    ->values[OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_CACHE_LINE_LAST_VALUE_INDEX] ==
                OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_CACHE_LINE_PATTERN - object_index;
    }
    test_context.Expect(
        alternating_release_valid && preserved_patterns &&
            small_cache.Validate() == os::kernel::KernelTypeCacheStatus::Succeeded &&
            cache_line_cache.Validate() == os::kernel::KernelTypeCacheStatus::Succeeded &&
            large_cache.Validate() == os::kernel::KernelTypeCacheStatus::Succeeded,
        OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_MIXED_OBJECTS);
    test_context.Expect(cache_line_cache.Destroy() ==
                            os::kernel::KernelTypeCacheStatus::ActiveObjectsRemain,
                        OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_DESTROY_GUARD);

    bool all_objects_released = true;
    for (uint64_t object_index = OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_NEXT_VALUE;
         object_index < OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_SMALL_CAPACITY;
         object_index += OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_ALTERNATING_STEP) {
        all_objects_released =
            all_objects_released &&
            small_objects[object_index]
                    ->values[OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_FIRST_VALUE_INDEX] ==
                OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_SMALL_PATTERN + object_index &&
            small_cache.TryRelease(small_objects[object_index]) ==
                os::kernel::KernelTypeCacheStatus::Succeeded;
    }
    for (uint64_t object_index = OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_EMPTY_VALUE;
         object_index < OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_CACHE_LINE_CAPACITY; ++object_index) {
        all_objects_released =
            all_objects_released && cache_line_cache.TryRelease(cache_line_objects[object_index]) ==
                                        os::kernel::KernelTypeCacheStatus::Succeeded;
    }
    for (uint64_t object_index = OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_NEXT_VALUE;
         object_index < OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_LARGE_CAPACITY;
         object_index += OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_ALTERNATING_STEP) {
        all_objects_released =
            all_objects_released &&
            large_objects[object_index]
                    ->values[OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_FIRST_VALUE_INDEX] ==
                OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_LARGE_PATTERN + object_index &&
            large_objects[object_index]
                    ->values[OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_LARGE_LAST_VALUE_INDEX] ==
                OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_LARGE_PATTERN - object_index &&
            large_cache.TryRelease(large_objects[object_index]) ==
                os::kernel::KernelTypeCacheStatus::Succeeded;
    }
    const bool caches_drained = all_objects_released &&
                                small_cache.Statistics().active_object_count ==
                                    OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_EMPTY_VALUE &&
                                cache_line_cache.Statistics().active_object_count ==
                                    OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_EMPTY_VALUE &&
                                large_cache.Statistics().active_object_count ==
                                    OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_EMPTY_VALUE;
    const bool caches_destroyed =
        caches_drained &&
        cache_line_cache.Destroy() == os::kernel::KernelTypeCacheStatus::Succeeded &&
        small_cache.Destroy() == os::kernel::KernelTypeCacheStatus::Succeeded &&
        large_cache.Destroy() == os::kernel::KernelTypeCacheStatus::Succeeded;
    const os::kernel::KernelHeapStatistics final_heap_statistics = heap.Statistics();
    test_context.Expect(caches_destroyed &&
                            heap.Validate() == os::kernel::KernelHeapStatus::Succeeded &&
                            final_heap_statistics.consumed_bytes ==
                                OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_EMPTY_VALUE &&
                            final_heap_statistics.allocation_count ==
                                OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_EMPTY_VALUE &&
                            final_heap_statistics.successful_allocation_count ==
                                OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_CACHE_COUNT &&
                            final_heap_statistics.release_count ==
                                final_heap_statistics.successful_allocation_count &&
                            final_heap_statistics.largest_free_allocation_bytes ==
                                OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_HEAP_SIZE_BYTES -
                                    OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_HEAP_HEADER_SIZE_BYTES,
                        OS_TEST_KERNEL_TYPE_CACHE_LIFECYCLE_FINAL_STATE);
    return test_context.ExitCode();
}

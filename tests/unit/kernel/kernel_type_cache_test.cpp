#include "os/kernel/memory/kernel_type_cache.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

struct alignas(64) CachedObject final {
    uint64_t values[8];
};

constexpr std::string_view OS_TEST_KERNEL_TYPE_CACHE_SUITE_NAME = "kernel/kernel_type_cache/unit";
constexpr std::string_view OS_TEST_KERNEL_TYPE_CACHE_UNINITIALIZED =
    "未初始化缓存必须拒绝申请、释放、销毁和验证";
constexpr std::string_view OS_TEST_KERNEL_TYPE_CACHE_INVALID_CONFIGURATION =
    "空对象、错误对齐、空容量和溢出必须在堆分配前失败";
constexpr std::string_view OS_TEST_KERNEL_TYPE_CACHE_BACKING_FAILURE =
    "堆容量不足必须保持缓存与堆状态不变";
constexpr std::string_view OS_TEST_KERNEL_TYPE_CACHE_INITIALIZATION =
    "类型缓存必须一次建立位图、对齐槽和空闲链";
constexpr std::string_view OS_TEST_KERNEL_TYPE_CACHE_COMPACT_OBJECT =
    "小于空闲链节点的对象必须扩展槽步长并正确处理非整字节位图";
constexpr std::string_view OS_TEST_KERNEL_TYPE_CACHE_EXHAUSTION =
    "缓存耗尽必须明确失败并保持输出指针不变";
constexpr std::string_view OS_TEST_KERNEL_TYPE_CACHE_INVALID_RELEASE =
    "空指针、内部指针、外部指针和活动对象销毁必须被拒绝";
constexpr std::string_view OS_TEST_KERNEL_TYPE_CACHE_LIFO_REUSE =
    "释放槽必须按 LIFO 顺序复用并拒绝重复释放";
constexpr std::string_view OS_TEST_KERNEL_TYPE_CACHE_FINAL_STATE =
    "全部对象释放并销毁后必须把唯一后备块归还通用堆";

constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_HEAP_SIZE_BYTES = 16ULL * 1024ULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_HEAP_ALIGNMENT_BYTES = 4096ULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_CAPACITY = 8ULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_OBJECT_ALIGNMENT_BYTES = 64ULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_BITMAP_SIZE_BYTES = 1ULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_OBJECT_STORAGE_OFFSET_BYTES = 64ULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_BACKING_SIZE_BYTES =
    OS_TEST_KERNEL_TYPE_CACHE_OBJECT_STORAGE_OFFSET_BYTES +
    sizeof(CachedObject) * OS_TEST_KERNEL_TYPE_CACHE_CAPACITY;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_INTERNAL_POINTER_OFFSET_BYTES = 8ULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_FIRST_VALUE_INDEX = 0ULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_LAST_VALUE_INDEX = 7ULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_FIRST_PATTERN = 0x5459504543414348ULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_LAST_PATTERN = 0x4F424A4543543031ULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_OVERSIZED_OBJECT_SIZE_BYTES = UINT64_MAX;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_OVERSIZED_CAPACITY = 2ULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_INVALID_ALIGNMENT_BYTES = 3ULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_NEXT_VALUE = 1ULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_BACKING_FAILURE_OBJECT_SIZE_BYTES = 4096ULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_BACKING_FAILURE_CAPACITY = 8ULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_COMPACT_OBJECT_SIZE_BYTES = 1ULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_COMPACT_OBJECT_ALIGNMENT_BYTES = 1ULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_COMPACT_CAPACITY = 9ULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_COMPACT_SLOT_STRIDE_BYTES = 8ULL;
constexpr uint64_t OS_TEST_KERNEL_TYPE_CACHE_COMPACT_BACKING_SIZE_BYTES = 80ULL;

[[nodiscard]] bool ObjectsAreUnique(CachedObject *const *objects) noexcept {
    for (uint64_t left_index = OS_TEST_KERNEL_TYPE_CACHE_EMPTY_VALUE;
         left_index < OS_TEST_KERNEL_TYPE_CACHE_CAPACITY; ++left_index) {
        for (uint64_t right_index = left_index + OS_TEST_KERNEL_TYPE_CACHE_NEXT_VALUE;
             right_index < OS_TEST_KERNEL_TYPE_CACHE_CAPACITY; ++right_index) {
            if (objects[left_index] == objects[right_index]) {
                return false;
            }
        }
    }
    return true;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_KERNEL_TYPE_CACHE_SUITE_NAME};
    alignas(OS_TEST_KERNEL_TYPE_CACHE_HEAP_ALIGNMENT_BYTES)
        uint8_t heap_buffer[OS_TEST_KERNEL_TYPE_CACHE_HEAP_SIZE_BYTES]{};
    os::kernel::KernelHeap heap{};
    const bool heap_initialized = heap.Initialize(reinterpret_cast<uint64_t>(heap_buffer),
                                                  OS_TEST_KERNEL_TYPE_CACHE_HEAP_SIZE_BYTES) ==
                                  os::kernel::KernelHeapStatus::Succeeded;
    if (!heap_initialized) {
        test_context.Expect(false, OS_TEST_KERNEL_TYPE_CACHE_INITIALIZATION);
        return test_context.ExitCode();
    }

    os::kernel::KernelFixedObjectCache uninitialized_cache{};
    void *uninitialized_object = nullptr;
    test_context.Expect(
        uninitialized_cache.TryAcquire(uninitialized_object) ==
                os::kernel::KernelTypeCacheStatus::NotInitialized &&
            uninitialized_cache.TryRelease(uninitialized_object) ==
                os::kernel::KernelTypeCacheStatus::NotInitialized &&
            uninitialized_cache.Destroy() == os::kernel::KernelTypeCacheStatus::NotInitialized &&
            uninitialized_cache.Validate() == os::kernel::KernelTypeCacheStatus::NotInitialized,
        OS_TEST_KERNEL_TYPE_CACHE_UNINITIALIZED);

    const os::kernel::KernelHeapStatistics heap_before_failures = heap.Statistics();
    os::kernel::KernelFixedObjectCache invalid_cache{};
    const bool invalid_configuration_rejected =
        invalid_cache.Initialize(heap, OS_TEST_KERNEL_TYPE_CACHE_EMPTY_VALUE,
                                 OS_TEST_KERNEL_TYPE_CACHE_OBJECT_ALIGNMENT_BYTES,
                                 OS_TEST_KERNEL_TYPE_CACHE_CAPACITY) ==
            os::kernel::KernelTypeCacheStatus::EmptyObjectSize &&
        invalid_cache.Initialize(heap, sizeof(CachedObject),
                                 OS_TEST_KERNEL_TYPE_CACHE_INVALID_ALIGNMENT_BYTES,
                                 OS_TEST_KERNEL_TYPE_CACHE_CAPACITY) ==
            os::kernel::KernelTypeCacheStatus::InvalidObjectAlignment &&
        invalid_cache.Initialize(heap, sizeof(CachedObject),
                                 OS_TEST_KERNEL_TYPE_CACHE_OBJECT_ALIGNMENT_BYTES,
                                 OS_TEST_KERNEL_TYPE_CACHE_EMPTY_VALUE) ==
            os::kernel::KernelTypeCacheStatus::EmptyCapacity &&
        invalid_cache.Initialize(heap, OS_TEST_KERNEL_TYPE_CACHE_OVERSIZED_OBJECT_SIZE_BYTES,
                                 OS_TEST_KERNEL_TYPE_CACHE_OBJECT_ALIGNMENT_BYTES,
                                 OS_TEST_KERNEL_TYPE_CACHE_OVERSIZED_CAPACITY) ==
            os::kernel::KernelTypeCacheStatus::SizeOverflow;
    test_context.Expect(invalid_configuration_rejected &&
                            heap.Statistics().successful_allocation_count ==
                                heap_before_failures.successful_allocation_count,
                        OS_TEST_KERNEL_TYPE_CACHE_INVALID_CONFIGURATION);

    os::kernel::KernelFixedObjectCache oversized_cache{};
    const bool backing_failure_preserved_state =
        oversized_cache.Initialize(heap,
                                   OS_TEST_KERNEL_TYPE_CACHE_BACKING_FAILURE_OBJECT_SIZE_BYTES,
                                   OS_TEST_KERNEL_TYPE_CACHE_OBJECT_ALIGNMENT_BYTES,
                                   OS_TEST_KERNEL_TYPE_CACHE_BACKING_FAILURE_CAPACITY) ==
            os::kernel::KernelTypeCacheStatus::BackingAllocationFailed &&
        oversized_cache.Validate() == os::kernel::KernelTypeCacheStatus::NotInitialized &&
        heap.Statistics().consumed_bytes == heap_before_failures.consumed_bytes &&
        heap.Validate() == os::kernel::KernelHeapStatus::Succeeded;
    test_context.Expect(backing_failure_preserved_state, OS_TEST_KERNEL_TYPE_CACHE_BACKING_FAILURE);

    alignas(OS_TEST_KERNEL_TYPE_CACHE_HEAP_ALIGNMENT_BYTES)
        uint8_t compact_heap_buffer[OS_TEST_KERNEL_TYPE_CACHE_HEAP_SIZE_BYTES]{};
    os::kernel::KernelHeap compact_heap{};
    os::kernel::KernelFixedObjectCache compact_cache{};
    const bool compact_initialized =
        compact_heap.Initialize(reinterpret_cast<uint64_t>(compact_heap_buffer),
                                OS_TEST_KERNEL_TYPE_CACHE_HEAP_SIZE_BYTES) ==
            os::kernel::KernelHeapStatus::Succeeded &&
        compact_cache.Initialize(compact_heap, OS_TEST_KERNEL_TYPE_CACHE_COMPACT_OBJECT_SIZE_BYTES,
                                 OS_TEST_KERNEL_TYPE_CACHE_COMPACT_OBJECT_ALIGNMENT_BYTES,
                                 OS_TEST_KERNEL_TYPE_CACHE_COMPACT_CAPACITY) ==
            os::kernel::KernelTypeCacheStatus::Succeeded;
    void *compact_objects[OS_TEST_KERNEL_TYPE_CACHE_COMPACT_CAPACITY]{};
    bool compact_lifecycle_valid = compact_initialized;
    for (uint64_t object_index = OS_TEST_KERNEL_TYPE_CACHE_EMPTY_VALUE;
         object_index < OS_TEST_KERNEL_TYPE_CACHE_COMPACT_CAPACITY; ++object_index) {
        compact_lifecycle_valid =
            compact_lifecycle_valid &&
            compact_cache.TryAcquire(compact_objects[object_index]) ==
                os::kernel::KernelTypeCacheStatus::Succeeded &&
            (reinterpret_cast<uint64_t>(compact_objects[object_index]) &
             (OS_TEST_KERNEL_TYPE_CACHE_COMPACT_SLOT_STRIDE_BYTES -
              OS_TEST_KERNEL_TYPE_CACHE_NEXT_VALUE)) == OS_TEST_KERNEL_TYPE_CACHE_EMPTY_VALUE;
    }
    for (uint64_t object_index = OS_TEST_KERNEL_TYPE_CACHE_EMPTY_VALUE;
         object_index < OS_TEST_KERNEL_TYPE_CACHE_COMPACT_CAPACITY; ++object_index) {
        compact_lifecycle_valid =
            compact_lifecycle_valid && compact_cache.TryRelease(compact_objects[object_index]) ==
                                           os::kernel::KernelTypeCacheStatus::Succeeded;
    }
    const os::kernel::KernelTypeCacheStatistics compact_statistics = compact_cache.Statistics();
    test_context.Expect(
        compact_lifecycle_valid &&
            compact_statistics.slot_stride_bytes ==
                OS_TEST_KERNEL_TYPE_CACHE_COMPACT_SLOT_STRIDE_BYTES &&
            compact_statistics.backing_storage_size_bytes ==
                OS_TEST_KERNEL_TYPE_CACHE_COMPACT_BACKING_SIZE_BYTES &&
            compact_cache.Validate() == os::kernel::KernelTypeCacheStatus::Succeeded &&
            compact_cache.Destroy() == os::kernel::KernelTypeCacheStatus::Succeeded &&
            compact_heap.Validate() == os::kernel::KernelHeapStatus::Succeeded,
        OS_TEST_KERNEL_TYPE_CACHE_COMPACT_OBJECT);

    os::kernel::KernelTypeCache<CachedObject> cache{};
    const bool cache_initialized = cache.Initialize(heap, OS_TEST_KERNEL_TYPE_CACHE_CAPACITY) ==
                                       os::kernel::KernelTypeCacheStatus::Succeeded &&
                                   cache.Initialize(heap, OS_TEST_KERNEL_TYPE_CACHE_CAPACITY) ==
                                       os::kernel::KernelTypeCacheStatus::AlreadyInitialized &&
                                   cache.Validate() == os::kernel::KernelTypeCacheStatus::Succeeded;
    const os::kernel::KernelTypeCacheStatistics initial_statistics = cache.Statistics();
    test_context.Expect(
        cache_initialized && initial_statistics.object_size_bytes == sizeof(CachedObject) &&
            initial_statistics.object_alignment_bytes ==
                OS_TEST_KERNEL_TYPE_CACHE_OBJECT_ALIGNMENT_BYTES &&
            initial_statistics.slot_stride_bytes == sizeof(CachedObject) &&
            initial_statistics.capacity == OS_TEST_KERNEL_TYPE_CACHE_CAPACITY &&
            initial_statistics.active_object_count == OS_TEST_KERNEL_TYPE_CACHE_EMPTY_VALUE &&
            initial_statistics.free_object_count == OS_TEST_KERNEL_TYPE_CACHE_CAPACITY &&
            initial_statistics.backing_storage_size_bytes ==
                OS_TEST_KERNEL_TYPE_CACHE_BACKING_SIZE_BYTES &&
            OS_TEST_KERNEL_TYPE_CACHE_BITMAP_SIZE_BYTES <
                OS_TEST_KERNEL_TYPE_CACHE_OBJECT_STORAGE_OFFSET_BYTES,
        OS_TEST_KERNEL_TYPE_CACHE_INITIALIZATION);
    if (!cache_initialized) {
        return test_context.ExitCode();
    }

    CachedObject *objects[OS_TEST_KERNEL_TYPE_CACHE_CAPACITY]{};
    bool all_objects_acquired = true;
    for (uint64_t object_index = OS_TEST_KERNEL_TYPE_CACHE_EMPTY_VALUE;
         object_index < OS_TEST_KERNEL_TYPE_CACHE_CAPACITY; ++object_index) {
        all_objects_acquired =
            all_objects_acquired &&
            cache.TryAcquire(objects[object_index]) ==
                os::kernel::KernelTypeCacheStatus::Succeeded &&
            (reinterpret_cast<uint64_t>(objects[object_index]) &
             (OS_TEST_KERNEL_TYPE_CACHE_OBJECT_ALIGNMENT_BYTES -
              OS_TEST_KERNEL_TYPE_CACHE_NEXT_VALUE)) == OS_TEST_KERNEL_TYPE_CACHE_EMPTY_VALUE;
        if (objects[object_index] != nullptr) {
            objects[object_index]->values[OS_TEST_KERNEL_TYPE_CACHE_FIRST_VALUE_INDEX] =
                OS_TEST_KERNEL_TYPE_CACHE_FIRST_PATTERN + object_index;
            objects[object_index]->values[OS_TEST_KERNEL_TYPE_CACHE_LAST_VALUE_INDEX] =
                OS_TEST_KERNEL_TYPE_CACHE_LAST_PATTERN + object_index;
        }
    }
    CachedObject *exhausted_output = objects[OS_TEST_KERNEL_TYPE_CACHE_FIRST_VALUE_INDEX];
    const bool exhaustion_rejected =
        all_objects_acquired && ObjectsAreUnique(objects) &&
        cache.TryAcquire(exhausted_output) == os::kernel::KernelTypeCacheStatus::OutOfObjects &&
        exhausted_output == objects[OS_TEST_KERNEL_TYPE_CACHE_FIRST_VALUE_INDEX];
    test_context.Expect(exhaustion_rejected, OS_TEST_KERNEL_TYPE_CACHE_EXHAUSTION);

    CachedObject *const internal_pointer = reinterpret_cast<CachedObject *>(
        reinterpret_cast<uint64_t>(objects[OS_TEST_KERNEL_TYPE_CACHE_FIRST_VALUE_INDEX]) +
        OS_TEST_KERNEL_TYPE_CACHE_INTERNAL_POINTER_OFFSET_BYTES);
    CachedObject *const external_pointer = reinterpret_cast<CachedObject *>(heap_buffer);
    test_context.Expect(
        cache.TryRelease(nullptr) == os::kernel::KernelTypeCacheStatus::NullObject &&
            cache.TryRelease(internal_pointer) ==
                os::kernel::KernelTypeCacheStatus::InvalidObject &&
            cache.TryRelease(external_pointer) ==
                os::kernel::KernelTypeCacheStatus::InvalidObject &&
            cache.Destroy() == os::kernel::KernelTypeCacheStatus::ActiveObjectsRemain,
        OS_TEST_KERNEL_TYPE_CACHE_INVALID_RELEASE);

    const CachedObject *const first_released_object =
        objects[OS_TEST_KERNEL_TYPE_CACHE_FIRST_VALUE_INDEX];
    const bool first_release_succeeded =
        cache.TryRelease(objects[OS_TEST_KERNEL_TYPE_CACHE_FIRST_VALUE_INDEX]) ==
        os::kernel::KernelTypeCacheStatus::Succeeded;
    const bool duplicate_release_rejected =
        cache.TryRelease(objects[OS_TEST_KERNEL_TYPE_CACHE_FIRST_VALUE_INDEX]) ==
        os::kernel::KernelTypeCacheStatus::ObjectNotActive;
    CachedObject *reused_object = nullptr;
    const bool released_slot_reused =
        cache.TryAcquire(reused_object) == os::kernel::KernelTypeCacheStatus::Succeeded &&
        reused_object == first_released_object &&
        cache.TryRelease(reused_object) == os::kernel::KernelTypeCacheStatus::Succeeded;
    test_context.Expect(first_release_succeeded && duplicate_release_rejected &&
                            released_slot_reused,
                        OS_TEST_KERNEL_TYPE_CACHE_LIFO_REUSE);

    bool remaining_objects_released = true;
    for (uint64_t object_index = OS_TEST_KERNEL_TYPE_CACHE_NEXT_VALUE;
         object_index < OS_TEST_KERNEL_TYPE_CACHE_CAPACITY; ++object_index) {
        remaining_objects_released =
            remaining_objects_released &&
            objects[object_index]->values[OS_TEST_KERNEL_TYPE_CACHE_FIRST_VALUE_INDEX] ==
                OS_TEST_KERNEL_TYPE_CACHE_FIRST_PATTERN + object_index &&
            objects[object_index]->values[OS_TEST_KERNEL_TYPE_CACHE_LAST_VALUE_INDEX] ==
                OS_TEST_KERNEL_TYPE_CACHE_LAST_PATTERN + object_index &&
            cache.TryRelease(objects[object_index]) == os::kernel::KernelTypeCacheStatus::Succeeded;
    }
    const os::kernel::KernelTypeCacheStatistics final_cache_statistics = cache.Statistics();
    const bool cache_drained =
        remaining_objects_released &&
        cache.Validate() == os::kernel::KernelTypeCacheStatus::Succeeded &&
        final_cache_statistics.active_object_count == OS_TEST_KERNEL_TYPE_CACHE_EMPTY_VALUE &&
        final_cache_statistics.free_object_count == OS_TEST_KERNEL_TYPE_CACHE_CAPACITY &&
        final_cache_statistics.successful_allocation_count ==
            OS_TEST_KERNEL_TYPE_CACHE_CAPACITY + OS_TEST_KERNEL_TYPE_CACHE_NEXT_VALUE &&
        final_cache_statistics.release_count ==
            final_cache_statistics.successful_allocation_count &&
        final_cache_statistics.peak_active_object_count == OS_TEST_KERNEL_TYPE_CACHE_CAPACITY;
    const bool cache_destroyed =
        cache_drained && cache.Destroy() == os::kernel::KernelTypeCacheStatus::Succeeded &&
        cache.Destroy() == os::kernel::KernelTypeCacheStatus::NotInitialized;
    const os::kernel::KernelHeapStatistics final_heap_statistics = heap.Statistics();
    test_context.Expect(
        cache_destroyed && heap.Validate() == os::kernel::KernelHeapStatus::Succeeded &&
            final_heap_statistics.consumed_bytes == OS_TEST_KERNEL_TYPE_CACHE_EMPTY_VALUE &&
            final_heap_statistics.allocation_count == OS_TEST_KERNEL_TYPE_CACHE_EMPTY_VALUE &&
            final_heap_statistics.active_requested_bytes == OS_TEST_KERNEL_TYPE_CACHE_EMPTY_VALUE &&
            final_heap_statistics.successful_allocation_count ==
                heap_before_failures.successful_allocation_count +
                    OS_TEST_KERNEL_TYPE_CACHE_NEXT_VALUE &&
            final_heap_statistics.release_count ==
                final_heap_statistics.successful_allocation_count,
        OS_TEST_KERNEL_TYPE_CACHE_FINAL_STATE);
    return test_context.ExitCode();
}

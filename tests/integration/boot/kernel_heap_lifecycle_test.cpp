#include "os/kernel/memory/kernel_heap.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_KERNEL_HEAP_LIFECYCLE_SUITE_NAME =
    "boot/kernel_heap_lifecycle/integration";
constexpr std::string_view OS_TEST_KERNEL_HEAP_LIFECYCLE_MIXED_ALLOCATIONS =
    "目标堆容量必须承载小对象、缓存行和页对齐对象";
constexpr std::string_view OS_TEST_KERNEL_HEAP_LIFECYCLE_PATTERNS =
    "分配、释放和合并不能破坏仍然活动对象的数据";
constexpr std::string_view OS_TEST_KERNEL_HEAP_LIFECYCLE_EXHAUSTION =
    "完整释放后必须能够分配最大连续负载";
constexpr std::string_view OS_TEST_KERNEL_HEAP_LIFECYCLE_FINAL_STATE =
    "目标堆生命周期结束后必须恢复为单一空闲区";

constexpr uint64_t OS_TEST_KERNEL_HEAP_LIFECYCLE_BUFFER_SIZE_BYTES = 64ULL * 1024ULL;
constexpr uint64_t OS_TEST_KERNEL_HEAP_LIFECYCLE_HEADER_SIZE_BYTES = 48ULL;
constexpr uint64_t OS_TEST_KERNEL_HEAP_LIFECYCLE_SMALL_SIZE_BYTES = 64ULL;
constexpr uint64_t OS_TEST_KERNEL_HEAP_LIFECYCLE_CACHE_SIZE_BYTES = 384ULL;
constexpr uint64_t OS_TEST_KERNEL_HEAP_LIFECYCLE_PAGE_SIZE_BYTES = 4096ULL;
constexpr uint64_t OS_TEST_KERNEL_HEAP_LIFECYCLE_SMALL_ALIGNMENT_BYTES = 16ULL;
constexpr uint64_t OS_TEST_KERNEL_HEAP_LIFECYCLE_CACHE_ALIGNMENT_BYTES = 64ULL;
constexpr uint64_t OS_TEST_KERNEL_HEAP_LIFECYCLE_PAGE_ALIGNMENT_BYTES = 4096ULL;
constexpr uint64_t OS_TEST_KERNEL_HEAP_LIFECYCLE_FIRST_PATTERN = 0x13579BDF2468ACE0ULL;
constexpr uint64_t OS_TEST_KERNEL_HEAP_LIFECYCLE_SECOND_PATTERN = 0xC001D00DC0FFEE11ULL;
constexpr uint64_t OS_TEST_KERNEL_HEAP_LIFECYCLE_THIRD_PATTERN = 0x5AA55AA5F00FF00FULL;

}

int main() {
    os::test::TestContext test_context{OS_TEST_KERNEL_HEAP_LIFECYCLE_SUITE_NAME};
    alignas(OS_TEST_KERNEL_HEAP_LIFECYCLE_PAGE_ALIGNMENT_BYTES)
        uint8_t heap_buffer[OS_TEST_KERNEL_HEAP_LIFECYCLE_BUFFER_SIZE_BYTES]{};
    os::kernel::KernelHeap heap{};
    const bool initialized = heap.Initialize(reinterpret_cast<uint64_t>(heap_buffer),
                                             OS_TEST_KERNEL_HEAP_LIFECYCLE_BUFFER_SIZE_BYTES) ==
                             os::kernel::KernelHeapStatus::Succeeded;

    void *small_allocation = nullptr;
    void *cache_allocation = nullptr;
    void *page_allocation = nullptr;
    const bool mixed_allocations_succeeded =
        initialized &&
        heap.TryAllocate(OS_TEST_KERNEL_HEAP_LIFECYCLE_SMALL_SIZE_BYTES,
                         OS_TEST_KERNEL_HEAP_LIFECYCLE_SMALL_ALIGNMENT_BYTES,
                         small_allocation) == os::kernel::KernelHeapStatus::Succeeded &&
        heap.TryAllocate(OS_TEST_KERNEL_HEAP_LIFECYCLE_CACHE_SIZE_BYTES,
                         OS_TEST_KERNEL_HEAP_LIFECYCLE_CACHE_ALIGNMENT_BYTES,
                         cache_allocation) == os::kernel::KernelHeapStatus::Succeeded &&
        heap.TryAllocate(OS_TEST_KERNEL_HEAP_LIFECYCLE_PAGE_SIZE_BYTES,
                         OS_TEST_KERNEL_HEAP_LIFECYCLE_PAGE_ALIGNMENT_BYTES,
                         page_allocation) == os::kernel::KernelHeapStatus::Succeeded &&
        (reinterpret_cast<uint64_t>(cache_allocation) &
         (OS_TEST_KERNEL_HEAP_LIFECYCLE_CACHE_ALIGNMENT_BYTES - 1ULL)) == 0ULL &&
        (reinterpret_cast<uint64_t>(page_allocation) &
         (OS_TEST_KERNEL_HEAP_LIFECYCLE_PAGE_ALIGNMENT_BYTES - 1ULL)) == 0ULL &&
        heap.Validate() == os::kernel::KernelHeapStatus::Succeeded;
    test_context.Expect(mixed_allocations_succeeded,
                        OS_TEST_KERNEL_HEAP_LIFECYCLE_MIXED_ALLOCATIONS);
    if (!mixed_allocations_succeeded) {
        return test_context.ExitCode();
    }

    uint64_t *const small_value = reinterpret_cast<uint64_t *>(small_allocation);
    uint64_t *const cache_value = reinterpret_cast<uint64_t *>(cache_allocation);
    uint64_t *const page_value = reinterpret_cast<uint64_t *>(page_allocation);
    *small_value = OS_TEST_KERNEL_HEAP_LIFECYCLE_FIRST_PATTERN;
    *cache_value = OS_TEST_KERNEL_HEAP_LIFECYCLE_SECOND_PATTERN;
    *page_value = OS_TEST_KERNEL_HEAP_LIFECYCLE_THIRD_PATTERN;
    const bool patterns_preserved =
        heap.TryRelease(cache_allocation) == os::kernel::KernelHeapStatus::Succeeded &&
        *small_value == OS_TEST_KERNEL_HEAP_LIFECYCLE_FIRST_PATTERN &&
        *page_value == OS_TEST_KERNEL_HEAP_LIFECYCLE_THIRD_PATTERN &&
        heap.TryRelease(small_allocation) == os::kernel::KernelHeapStatus::Succeeded &&
        *page_value == OS_TEST_KERNEL_HEAP_LIFECYCLE_THIRD_PATTERN &&
        heap.TryRelease(page_allocation) == os::kernel::KernelHeapStatus::Succeeded &&
        heap.Validate() == os::kernel::KernelHeapStatus::Succeeded;
    test_context.Expect(patterns_preserved, OS_TEST_KERNEL_HEAP_LIFECYCLE_PATTERNS);

    void *maximum_allocation = nullptr;
    const uint64_t maximum_payload_size_bytes = OS_TEST_KERNEL_HEAP_LIFECYCLE_BUFFER_SIZE_BYTES -
                                                OS_TEST_KERNEL_HEAP_LIFECYCLE_HEADER_SIZE_BYTES;
    const bool exhaustion_recovered =
        heap.TryAllocate(maximum_payload_size_bytes,
                         OS_TEST_KERNEL_HEAP_LIFECYCLE_SMALL_ALIGNMENT_BYTES,
                         maximum_allocation) == os::kernel::KernelHeapStatus::Succeeded &&
        heap.TryRelease(maximum_allocation) == os::kernel::KernelHeapStatus::Succeeded;
    test_context.Expect(exhaustion_recovered, OS_TEST_KERNEL_HEAP_LIFECYCLE_EXHAUSTION);

    const os::kernel::KernelHeapStatistics statistics = heap.Statistics();
    test_context.Expect(
        heap.Validate() == os::kernel::KernelHeapStatus::Succeeded &&
            statistics.capacity_bytes == OS_TEST_KERNEL_HEAP_LIFECYCLE_BUFFER_SIZE_BYTES &&
            statistics.consumed_bytes == 0ULL &&
            statistics.remaining_bytes == OS_TEST_KERNEL_HEAP_LIFECYCLE_BUFFER_SIZE_BYTES &&
            statistics.allocation_count == 0ULL && statistics.active_requested_bytes == 0ULL &&
            statistics.successful_allocation_count == 4ULL &&
            statistics.release_count == statistics.successful_allocation_count &&
            statistics.peak_consumed_bytes == OS_TEST_KERNEL_HEAP_LIFECYCLE_BUFFER_SIZE_BYTES &&
            statistics.largest_free_allocation_bytes == maximum_payload_size_bytes,
        OS_TEST_KERNEL_HEAP_LIFECYCLE_FINAL_STATE);
    return test_context.ExitCode();
}

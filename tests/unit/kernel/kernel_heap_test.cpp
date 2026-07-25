#include "os/kernel/kernel_heap.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_KERNEL_HEAP_SUITE_NAME = "kernel/kernel_heap/unit";
constexpr std::string_view OS_TEST_KERNEL_HEAP_INVALID_RANGE =
    "未对齐、过小和溢出的堆范围必须被拒绝";
constexpr std::string_view OS_TEST_KERNEL_HEAP_INITIAL_STATE =
    "初始化后必须只有一个覆盖全堆的空闲块";
constexpr std::string_view OS_TEST_KERNEL_HEAP_REINITIALIZATION =
    "已经初始化的堆不能覆盖现有元数据";
constexpr std::string_view OS_TEST_KERNEL_HEAP_ALIGNMENT = "所有支持的二次幂对齐都必须得到满足";
constexpr std::string_view OS_TEST_KERNEL_HEAP_FAILURE_ATOMIC =
    "非法或无空间请求不能修改输出和统计";
constexpr std::string_view OS_TEST_KERNEL_HEAP_INVALID_RELEASE =
    "空指针、区间外指针和内部指针必须被拒绝";
constexpr std::string_view OS_TEST_KERNEL_HEAP_DOUBLE_RELEASE = "同一分配不能被释放两次";
constexpr std::string_view OS_TEST_KERNEL_HEAP_COALESCING =
    "乱序释放后必须向前向后合并为完整空闲区";
constexpr std::string_view OS_TEST_KERNEL_HEAP_REUSE = "完整合并后的堆必须能够复用起始地址";
constexpr std::string_view OS_TEST_KERNEL_HEAP_BEST_FIT =
    "best-fit 必须优先选择能够容纳请求的最小空闲块";
constexpr std::string_view OS_TEST_KERNEL_HEAP_STATISTICS =
    "活动、累计、峰值和剩余空间统计必须一致";
constexpr std::string_view OS_TEST_KERNEL_HEAP_VALIDATION = "每个公开操作后的堆元数据必须保持一致";

constexpr uint64_t OS_TEST_KERNEL_HEAP_BUFFER_SIZE_BYTES = 4096ULL;
constexpr uint64_t OS_TEST_KERNEL_HEAP_MINIMUM_VALID_SIZE_BYTES = 64ULL;
constexpr uint64_t OS_TEST_KERNEL_HEAP_INVALID_SMALL_SIZE_BYTES =
    OS_TEST_KERNEL_HEAP_MINIMUM_VALID_SIZE_BYTES - 16ULL;
constexpr uint64_t OS_TEST_KERNEL_HEAP_INVALID_SIZE_BYTES =
    OS_TEST_KERNEL_HEAP_BUFFER_SIZE_BYTES - 1ULL;
constexpr uint64_t OS_TEST_KERNEL_HEAP_MISALIGNED_ADDRESS_OFFSET_BYTES = 1ULL;
constexpr uint64_t OS_TEST_KERNEL_HEAP_ALIGNMENT_COUNT = 5ULL;
constexpr uint64_t OS_TEST_KERNEL_HEAP_ALIGNMENTS[OS_TEST_KERNEL_HEAP_ALIGNMENT_COUNT] = {
    1ULL, 16ULL, 64ULL, 256ULL, 1024ULL,
};
constexpr uint64_t OS_TEST_KERNEL_HEAP_ALIGNMENT_MASK_DECREMENT = 1ULL;
constexpr uint64_t OS_TEST_KERNEL_HEAP_ALLOCATION_SIZE_BYTES = 37ULL;
constexpr uint64_t OS_TEST_KERNEL_HEAP_COALESCING_ALLOCATION_SIZE_BYTES = 96ULL;
constexpr uint64_t OS_TEST_KERNEL_HEAP_INVALID_ALIGNMENT_BYTES = 3ULL;
constexpr uint64_t OS_TEST_KERNEL_HEAP_INVALID_POINTER_VALUE = 0x1234ULL;
constexpr uint64_t OS_TEST_KERNEL_HEAP_INTERIOR_POINTER_OFFSET_BYTES = 16ULL;
constexpr uint64_t OS_TEST_KERNEL_HEAP_EXPECTED_HEADER_SIZE_BYTES = 48ULL;
constexpr uint64_t OS_TEST_KERNEL_HEAP_BEST_FIT_SMALL_SIZE_BYTES = 64ULL;
constexpr uint64_t OS_TEST_KERNEL_HEAP_BEST_FIT_LARGE_SIZE_BYTES = 192ULL;
constexpr uint64_t OS_TEST_KERNEL_HEAP_BEST_FIT_REQUEST_SIZE_BYTES = 32ULL;

[[nodiscard]] bool StatisticsEqual(const os::kernel::KernelHeapStatistics left,
                                   const os::kernel::KernelHeapStatistics right) noexcept {
    return left.capacity_bytes == right.capacity_bytes &&
           left.consumed_bytes == right.consumed_bytes &&
           left.remaining_bytes == right.remaining_bytes &&
           left.allocation_count == right.allocation_count &&
           left.active_requested_bytes == right.active_requested_bytes &&
           left.successful_allocation_count == right.successful_allocation_count &&
           left.release_count == right.release_count &&
           left.peak_consumed_bytes == right.peak_consumed_bytes &&
           left.largest_free_allocation_bytes == right.largest_free_allocation_bytes;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_KERNEL_HEAP_SUITE_NAME};
    alignas(16) uint8_t heap_buffer[OS_TEST_KERNEL_HEAP_BUFFER_SIZE_BYTES]{};

    os::kernel::KernelHeap invalid_heap{};
    const bool invalid_ranges_rejected =
        invalid_heap.Initialize(reinterpret_cast<uint64_t>(heap_buffer) +
                                    OS_TEST_KERNEL_HEAP_MISALIGNED_ADDRESS_OFFSET_BYTES,
                                OS_TEST_KERNEL_HEAP_BUFFER_SIZE_BYTES) ==
            os::kernel::KernelHeapStatus::InvalidRange &&
        invalid_heap.Initialize(reinterpret_cast<uint64_t>(heap_buffer),
                                OS_TEST_KERNEL_HEAP_INVALID_SMALL_SIZE_BYTES) ==
            os::kernel::KernelHeapStatus::InvalidRange &&
        invalid_heap.Initialize(reinterpret_cast<uint64_t>(heap_buffer),
                                OS_TEST_KERNEL_HEAP_INVALID_SIZE_BYTES) ==
            os::kernel::KernelHeapStatus::InvalidRange &&
        invalid_heap.Initialize(UINT64_MAX - OS_TEST_KERNEL_HEAP_INVALID_SMALL_SIZE_BYTES,
                                OS_TEST_KERNEL_HEAP_MINIMUM_VALID_SIZE_BYTES) ==
            os::kernel::KernelHeapStatus::InvalidRange;
    test_context.Expect(invalid_ranges_rejected, OS_TEST_KERNEL_HEAP_INVALID_RANGE);

    os::kernel::KernelHeap heap{};
    const bool initialized = heap.Initialize(reinterpret_cast<uint64_t>(heap_buffer),
                                             OS_TEST_KERNEL_HEAP_BUFFER_SIZE_BYTES) ==
                             os::kernel::KernelHeapStatus::Succeeded;
    const os::kernel::KernelHeapStatistics initial_statistics = heap.Statistics();
    test_context.Expect(
        initialized && initial_statistics.capacity_bytes == OS_TEST_KERNEL_HEAP_BUFFER_SIZE_BYTES &&
            initial_statistics.consumed_bytes == 0ULL &&
            initial_statistics.remaining_bytes == OS_TEST_KERNEL_HEAP_BUFFER_SIZE_BYTES &&
            initial_statistics.allocation_count == 0ULL &&
            initial_statistics.active_requested_bytes == 0ULL &&
            initial_statistics.successful_allocation_count == 0ULL &&
            initial_statistics.release_count == 0ULL &&
            initial_statistics.peak_consumed_bytes == 0ULL &&
            initial_statistics.largest_free_allocation_bytes ==
                OS_TEST_KERNEL_HEAP_BUFFER_SIZE_BYTES -
                    OS_TEST_KERNEL_HEAP_EXPECTED_HEADER_SIZE_BYTES,
        OS_TEST_KERNEL_HEAP_INITIAL_STATE);
    test_context.Expect(heap.Initialize(reinterpret_cast<uint64_t>(heap_buffer),
                                        OS_TEST_KERNEL_HEAP_BUFFER_SIZE_BYTES) ==
                                os::kernel::KernelHeapStatus::AlreadyInitialized &&
                            heap.Validate() == os::kernel::KernelHeapStatus::Succeeded,
                        OS_TEST_KERNEL_HEAP_REINITIALIZATION);

    void *aligned_allocations[OS_TEST_KERNEL_HEAP_ALIGNMENT_COUNT]{};
    bool alignments_valid = true;
    for (uint64_t alignment_index = 0ULL; alignment_index < OS_TEST_KERNEL_HEAP_ALIGNMENT_COUNT;
         ++alignment_index) {
        const uint64_t alignment_bytes = OS_TEST_KERNEL_HEAP_ALIGNMENTS[alignment_index];
        alignments_valid =
            alignments_valid &&
            heap.TryAllocate(OS_TEST_KERNEL_HEAP_ALLOCATION_SIZE_BYTES, alignment_bytes,
                             aligned_allocations[alignment_index]) ==
                os::kernel::KernelHeapStatus::Succeeded &&
            (reinterpret_cast<uint64_t>(aligned_allocations[alignment_index]) &
             (alignment_bytes - OS_TEST_KERNEL_HEAP_ALIGNMENT_MASK_DECREMENT)) == 0ULL &&
            heap.Validate() == os::kernel::KernelHeapStatus::Succeeded;
    }
    test_context.Expect(alignments_valid, OS_TEST_KERNEL_HEAP_ALIGNMENT);

    const os::kernel::KernelHeapStatistics statistics_before_failures = heap.Statistics();
    void *unchanged_allocation =
        reinterpret_cast<void *>(OS_TEST_KERNEL_HEAP_INVALID_POINTER_VALUE);
    const bool failures_atomic =
        heap.TryAllocate(0ULL, 1ULL, unchanged_allocation) ==
            os::kernel::KernelHeapStatus::EmptyAllocation &&
        reinterpret_cast<uint64_t>(unchanged_allocation) ==
            OS_TEST_KERNEL_HEAP_INVALID_POINTER_VALUE &&
        heap.TryAllocate(OS_TEST_KERNEL_HEAP_ALLOCATION_SIZE_BYTES,
                         OS_TEST_KERNEL_HEAP_INVALID_ALIGNMENT_BYTES,
                         unchanged_allocation) == os::kernel::KernelHeapStatus::InvalidAlignment &&
        reinterpret_cast<uint64_t>(unchanged_allocation) ==
            OS_TEST_KERNEL_HEAP_INVALID_POINTER_VALUE &&
        heap.TryAllocate(OS_TEST_KERNEL_HEAP_BUFFER_SIZE_BYTES, 1ULL, unchanged_allocation) ==
            os::kernel::KernelHeapStatus::OutOfMemory &&
        reinterpret_cast<uint64_t>(unchanged_allocation) ==
            OS_TEST_KERNEL_HEAP_INVALID_POINTER_VALUE &&
        StatisticsEqual(statistics_before_failures, heap.Statistics());
    test_context.Expect(failures_atomic, OS_TEST_KERNEL_HEAP_FAILURE_ATOMIC);

    const bool invalid_releases_rejected =
        heap.TryRelease(nullptr) == os::kernel::KernelHeapStatus::NullAllocation &&
        heap.TryRelease(reinterpret_cast<void *>(OS_TEST_KERNEL_HEAP_INVALID_POINTER_VALUE)) ==
            os::kernel::KernelHeapStatus::InvalidAllocation &&
        heap.TryRelease(
            reinterpret_cast<void *>(reinterpret_cast<uint64_t>(aligned_allocations[0ULL]) +
                                     OS_TEST_KERNEL_HEAP_INTERIOR_POINTER_OFFSET_BYTES)) ==
            os::kernel::KernelHeapStatus::InvalidAllocation;
    test_context.Expect(invalid_releases_rejected, OS_TEST_KERNEL_HEAP_INVALID_RELEASE);

    bool aligned_allocations_released = true;
    for (uint64_t alignment_index = 0ULL; alignment_index < OS_TEST_KERNEL_HEAP_ALIGNMENT_COUNT;
         ++alignment_index) {
        aligned_allocations_released = aligned_allocations_released &&
                                       heap.TryRelease(aligned_allocations[alignment_index]) ==
                                           os::kernel::KernelHeapStatus::Succeeded &&
                                       heap.Validate() == os::kernel::KernelHeapStatus::Succeeded;
    }
    test_context.Expect(aligned_allocations_released, OS_TEST_KERNEL_HEAP_VALIDATION);
    test_context.Expect(heap.TryRelease(aligned_allocations[0ULL]) ==
                            os::kernel::KernelHeapStatus::AllocationNotActive,
                        OS_TEST_KERNEL_HEAP_DOUBLE_RELEASE);

    void *first_allocation = nullptr;
    void *second_allocation = nullptr;
    void *third_allocation = nullptr;
    void *fourth_allocation = nullptr;
    const bool coalescing_allocations_succeeded =
        heap.TryAllocate(OS_TEST_KERNEL_HEAP_COALESCING_ALLOCATION_SIZE_BYTES, 16ULL,
                         first_allocation) == os::kernel::KernelHeapStatus::Succeeded &&
        heap.TryAllocate(OS_TEST_KERNEL_HEAP_COALESCING_ALLOCATION_SIZE_BYTES, 16ULL,
                         second_allocation) == os::kernel::KernelHeapStatus::Succeeded &&
        heap.TryAllocate(OS_TEST_KERNEL_HEAP_COALESCING_ALLOCATION_SIZE_BYTES, 16ULL,
                         third_allocation) == os::kernel::KernelHeapStatus::Succeeded &&
        heap.TryAllocate(OS_TEST_KERNEL_HEAP_COALESCING_ALLOCATION_SIZE_BYTES, 16ULL,
                         fourth_allocation) == os::kernel::KernelHeapStatus::Succeeded;
    const uint64_t first_allocation_address = reinterpret_cast<uint64_t>(first_allocation);
    const bool coalesced =
        coalescing_allocations_succeeded &&
        heap.TryRelease(second_allocation) == os::kernel::KernelHeapStatus::Succeeded &&
        heap.TryRelease(fourth_allocation) == os::kernel::KernelHeapStatus::Succeeded &&
        heap.TryRelease(third_allocation) == os::kernel::KernelHeapStatus::Succeeded &&
        heap.TryRelease(first_allocation) == os::kernel::KernelHeapStatus::Succeeded &&
        heap.Validate() == os::kernel::KernelHeapStatus::Succeeded &&
        heap.Statistics().largest_free_allocation_bytes ==
            OS_TEST_KERNEL_HEAP_BUFFER_SIZE_BYTES - OS_TEST_KERNEL_HEAP_EXPECTED_HEADER_SIZE_BYTES;
    test_context.Expect(coalesced, OS_TEST_KERNEL_HEAP_COALESCING);

    void *reused_allocation = nullptr;
    const bool reused =
        heap.TryAllocate(OS_TEST_KERNEL_HEAP_COALESCING_ALLOCATION_SIZE_BYTES, 16ULL,
                         reused_allocation) == os::kernel::KernelHeapStatus::Succeeded &&
        reinterpret_cast<uint64_t>(reused_allocation) == first_allocation_address &&
        heap.TryRelease(reused_allocation) == os::kernel::KernelHeapStatus::Succeeded;
    test_context.Expect(reused, OS_TEST_KERNEL_HEAP_REUSE);

    alignas(16) uint8_t best_fit_buffer[OS_TEST_KERNEL_HEAP_BUFFER_SIZE_BYTES]{};
    os::kernel::KernelHeap best_fit_heap{};
    void *small_candidate = nullptr;
    void *first_separator = nullptr;
    void *large_candidate = nullptr;
    void *second_separator = nullptr;
    void *best_fit_allocation = nullptr;
    const bool best_fit_valid =
        best_fit_heap.Initialize(reinterpret_cast<uint64_t>(best_fit_buffer),
                                 OS_TEST_KERNEL_HEAP_BUFFER_SIZE_BYTES) ==
            os::kernel::KernelHeapStatus::Succeeded &&
        best_fit_heap.TryAllocate(OS_TEST_KERNEL_HEAP_BEST_FIT_SMALL_SIZE_BYTES, 16ULL,
                                  small_candidate) == os::kernel::KernelHeapStatus::Succeeded &&
        best_fit_heap.TryAllocate(OS_TEST_KERNEL_HEAP_BEST_FIT_SMALL_SIZE_BYTES, 16ULL,
                                  first_separator) == os::kernel::KernelHeapStatus::Succeeded &&
        best_fit_heap.TryAllocate(OS_TEST_KERNEL_HEAP_BEST_FIT_LARGE_SIZE_BYTES, 16ULL,
                                  large_candidate) == os::kernel::KernelHeapStatus::Succeeded &&
        best_fit_heap.TryAllocate(OS_TEST_KERNEL_HEAP_BEST_FIT_SMALL_SIZE_BYTES, 16ULL,
                                  second_separator) == os::kernel::KernelHeapStatus::Succeeded &&
        best_fit_heap.TryRelease(small_candidate) == os::kernel::KernelHeapStatus::Succeeded &&
        best_fit_heap.TryRelease(large_candidate) == os::kernel::KernelHeapStatus::Succeeded &&
        best_fit_heap.TryAllocate(OS_TEST_KERNEL_HEAP_BEST_FIT_REQUEST_SIZE_BYTES, 16ULL,
                                  best_fit_allocation) == os::kernel::KernelHeapStatus::Succeeded &&
        best_fit_allocation == small_candidate &&
        best_fit_heap.TryRelease(best_fit_allocation) == os::kernel::KernelHeapStatus::Succeeded &&
        best_fit_heap.TryRelease(first_separator) == os::kernel::KernelHeapStatus::Succeeded &&
        best_fit_heap.TryRelease(second_separator) == os::kernel::KernelHeapStatus::Succeeded &&
        best_fit_heap.Validate() == os::kernel::KernelHeapStatus::Succeeded;
    test_context.Expect(best_fit_valid, OS_TEST_KERNEL_HEAP_BEST_FIT);

    const os::kernel::KernelHeapStatistics final_statistics = heap.Statistics();
    test_context.Expect(
        final_statistics.consumed_bytes == 0ULL &&
            final_statistics.remaining_bytes == OS_TEST_KERNEL_HEAP_BUFFER_SIZE_BYTES &&
            final_statistics.allocation_count == 0ULL &&
            final_statistics.active_requested_bytes == 0ULL &&
            final_statistics.successful_allocation_count ==
                OS_TEST_KERNEL_HEAP_ALIGNMENT_COUNT + 5ULL &&
            final_statistics.release_count == final_statistics.successful_allocation_count &&
            final_statistics.peak_consumed_bytes > 0ULL,
        OS_TEST_KERNEL_HEAP_STATISTICS);
    test_context.Expect(heap.Validate() == os::kernel::KernelHeapStatus::Succeeded,
                        OS_TEST_KERNEL_HEAP_VALIDATION);
    return test_context.ExitCode();
}

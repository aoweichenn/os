#include "os/kernel/kernel_heap.hpp"
#include "os/kernel/page_table.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_HEAP_PAGE_SUITE_NAME = "kernel/heap_and_page_layout/unit";
constexpr std::string_view OS_TEST_HEAP_PAGE_HEAP_INITIALIZE = "有效缓冲区必须初始化最小堆";
constexpr std::string_view OS_TEST_HEAP_PAGE_HEAP_ALIGNMENT = "分配结果必须满足请求对齐";
constexpr std::string_view OS_TEST_HEAP_PAGE_HEAP_FAILURE_ATOMIC = "堆分配失败不能修改输出指针";
constexpr std::string_view OS_TEST_HEAP_PAGE_HEAP_EXHAUSTION = "超出堆容量必须明确失败";
constexpr std::string_view OS_TEST_HEAP_PAGE_CANONICAL = "低半区与高半区地址必须判定为 canonical";
constexpr std::string_view OS_TEST_HEAP_PAGE_NON_CANONICAL = "48 位 canonical 空洞必须被拒绝";
constexpr std::string_view OS_TEST_HEAP_PAGE_INDICES = "四级页表索引必须按硬件位段提取";
constexpr std::string_view OS_TEST_HEAP_PAGE_ENTRY_ROUND_TRIP = "页表物理地址与权限必须往返";

constexpr uint64_t OS_TEST_HEAP_PAGE_BUFFER_SIZE_BYTES = 4096ULL;
constexpr uint64_t OS_TEST_HEAP_PAGE_FIRST_ALLOCATION_SIZE_BYTES = 3ULL;
constexpr uint64_t OS_TEST_HEAP_PAGE_MINIMUM_ALIGNMENT_BYTES = 1ULL;
constexpr uint64_t OS_TEST_HEAP_PAGE_INVALID_ALIGNMENT_BYTES = 3ULL;
constexpr uint64_t OS_TEST_HEAP_PAGE_SECOND_ALLOCATION_SIZE_BYTES = 64ULL;
constexpr uint64_t OS_TEST_HEAP_PAGE_SECOND_ALIGNMENT_BYTES = 64ULL;
constexpr uint64_t OS_TEST_HEAP_PAGE_INVALID_POINTER_VALUE = 0x1234ULL;
constexpr uint64_t OS_TEST_HEAP_PAGE_LOW_CANONICAL_ADDRESS = 0x00007FFFFFFFF000ULL;
constexpr uint64_t OS_TEST_HEAP_PAGE_HIGH_CANONICAL_ADDRESS = 0xFFFF800000000000ULL;
constexpr uint64_t OS_TEST_HEAP_PAGE_NON_CANONICAL_ADDRESS = 0x0000800000000000ULL;
constexpr uint64_t OS_TEST_HEAP_PAGE_INDEX_ADDRESS = 0xFFFF812345678000ULL;
constexpr uint64_t OS_TEST_HEAP_PAGE_EXPECTED_LEVEL4_INDEX = 0x102ULL;
constexpr uint64_t OS_TEST_HEAP_PAGE_EXPECTED_LEVEL3_INDEX = 0x08DULL;
constexpr uint64_t OS_TEST_HEAP_PAGE_EXPECTED_LEVEL2_INDEX = 0x02BULL;
constexpr uint64_t OS_TEST_HEAP_PAGE_EXPECTED_LEVEL1_INDEX = 0x078ULL;
constexpr uint64_t OS_TEST_HEAP_PAGE_PHYSICAL_ADDRESS = 0x0000000012345000ULL;
constexpr uint64_t OS_TEST_HEAP_PAGE_ALIGNMENT_MASK_DECREMENT = 1ULL;

}

int main() {
    os::test::TestContext testContext{OS_TEST_HEAP_PAGE_SUITE_NAME};
    alignas(OS_TEST_HEAP_PAGE_SECOND_ALIGNMENT_BYTES)
        uint8_t heapBuffer[OS_TEST_HEAP_PAGE_BUFFER_SIZE_BYTES]{};
    os::kernel::KernelHeap heap{};
    testContext.expect(heap.initialize(reinterpret_cast<uint64_t>(heapBuffer),
                                       OS_TEST_HEAP_PAGE_BUFFER_SIZE_BYTES) ==
                           os::kernel::KernelHeapStatus::Succeeded,
                       OS_TEST_HEAP_PAGE_HEAP_INITIALIZE);

    void *firstAllocation = nullptr;
    void *secondAllocation = nullptr;
    const bool allocationsSucceeded =
        heap.tryAllocate(OS_TEST_HEAP_PAGE_FIRST_ALLOCATION_SIZE_BYTES,
                         OS_TEST_HEAP_PAGE_MINIMUM_ALIGNMENT_BYTES,
                         firstAllocation) == os::kernel::KernelHeapStatus::Succeeded &&
        heap.tryAllocate(OS_TEST_HEAP_PAGE_SECOND_ALLOCATION_SIZE_BYTES,
                         OS_TEST_HEAP_PAGE_SECOND_ALIGNMENT_BYTES,
                         secondAllocation) == os::kernel::KernelHeapStatus::Succeeded;
    testContext.expect(allocationsSucceeded &&
                           (reinterpret_cast<uint64_t>(secondAllocation) &
                            (OS_TEST_HEAP_PAGE_SECOND_ALIGNMENT_BYTES -
                             OS_TEST_HEAP_PAGE_ALIGNMENT_MASK_DECREMENT)) == 0ULL,
                       OS_TEST_HEAP_PAGE_HEAP_ALIGNMENT);

    void *unchangedAllocation = reinterpret_cast<void *>(OS_TEST_HEAP_PAGE_INVALID_POINTER_VALUE);
    testContext.expect(heap.tryAllocate(OS_TEST_HEAP_PAGE_FIRST_ALLOCATION_SIZE_BYTES,
                                        OS_TEST_HEAP_PAGE_INVALID_ALIGNMENT_BYTES,
                                        unchangedAllocation) ==
                               os::kernel::KernelHeapStatus::InvalidAlignment &&
                           reinterpret_cast<uint64_t>(unchangedAllocation) ==
                               OS_TEST_HEAP_PAGE_INVALID_POINTER_VALUE,
                       OS_TEST_HEAP_PAGE_HEAP_FAILURE_ATOMIC);
    testContext.expect(heap.tryAllocate(OS_TEST_HEAP_PAGE_BUFFER_SIZE_BYTES,
                                        OS_TEST_HEAP_PAGE_SECOND_ALIGNMENT_BYTES,
                                        unchangedAllocation) ==
                           os::kernel::KernelHeapStatus::OutOfMemory,
                       OS_TEST_HEAP_PAGE_HEAP_EXHAUSTION);

    testContext.expect(
        os::kernel::isCanonicalVirtualAddress(OS_TEST_HEAP_PAGE_LOW_CANONICAL_ADDRESS) &&
            os::kernel::isCanonicalVirtualAddress(OS_TEST_HEAP_PAGE_HIGH_CANONICAL_ADDRESS),
        OS_TEST_HEAP_PAGE_CANONICAL);
    testContext.expect(
        !os::kernel::isCanonicalVirtualAddress(OS_TEST_HEAP_PAGE_NON_CANONICAL_ADDRESS),
        OS_TEST_HEAP_PAGE_NON_CANONICAL);

    const os::kernel::PageTableIndices indices =
        os::kernel::calculatePageTableIndices(OS_TEST_HEAP_PAGE_INDEX_ADDRESS);
    testContext.expect(indices.level4 == OS_TEST_HEAP_PAGE_EXPECTED_LEVEL4_INDEX &&
                           indices.level3 == OS_TEST_HEAP_PAGE_EXPECTED_LEVEL3_INDEX &&
                           indices.level2 == OS_TEST_HEAP_PAGE_EXPECTED_LEVEL2_INDEX &&
                           indices.level1 == OS_TEST_HEAP_PAGE_EXPECTED_LEVEL1_INDEX,
                       OS_TEST_HEAP_PAGE_INDICES);

    const os::kernel::PagePermissions permissions{
        .writable = true,
        .executable = false,
        .userAccessible = true,
    };
    const os::kernel::PageMapping mapping = os::kernel::decodePageTableLeafEntry(
        os::kernel::encodePageTableLeafEntry(OS_TEST_HEAP_PAGE_PHYSICAL_ADDRESS, permissions));
    testContext.expect(mapping.physicalAddress == OS_TEST_HEAP_PAGE_PHYSICAL_ADDRESS &&
                           mapping.permissions.writable == permissions.writable &&
                           mapping.permissions.executable == permissions.executable &&
                           mapping.permissions.userAccessible == permissions.userAccessible,
                       OS_TEST_HEAP_PAGE_ENTRY_ROUND_TRIP);

    return testContext.exitCode();
}

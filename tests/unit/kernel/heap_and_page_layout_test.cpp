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
constexpr std::string_view OS_TEST_HEAP_PAGE_DIRECT_MAP_ACCESS =
    "64 TiB 高半区物理直映访问环境必须有效";
constexpr std::string_view OS_TEST_HEAP_PAGE_DIRECT_MAP_OVERFLOW =
    "物理直映越过 canonical 或整数边界必须被拒绝";

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
constexpr uint64_t OS_TEST_HEAP_PAGE_DIRECT_MAP_BASE = 0xFFFF888000000000ULL;
constexpr uint64_t OS_TEST_HEAP_PAGE_DIRECT_MAP_CAPACITY_BYTES =
    64ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr uint64_t OS_TEST_HEAP_PAGE_DIRECT_MAP_ALLOCATION_LIMIT_BYTES =
    65ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr uint64_t OS_TEST_HEAP_PAGE_INVALID_DIRECT_MAP_CAPACITY_BYTES =
    128ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL;

}

int main() {
    os::test::TestContext test_context{OS_TEST_HEAP_PAGE_SUITE_NAME};
    alignas(OS_TEST_HEAP_PAGE_SECOND_ALIGNMENT_BYTES)
        uint8_t heap_buffer[OS_TEST_HEAP_PAGE_BUFFER_SIZE_BYTES]{};
    os::kernel::KernelHeap heap{};
    test_context.Expect(heap.Initialize(reinterpret_cast<uint64_t>(heap_buffer),
                                        OS_TEST_HEAP_PAGE_BUFFER_SIZE_BYTES) ==
                            os::kernel::KernelHeapStatus::Succeeded,
                        OS_TEST_HEAP_PAGE_HEAP_INITIALIZE);

    void *first_allocation = nullptr;
    void *second_allocation = nullptr;
    const bool allocations_succeeded =
        heap.TryAllocate(OS_TEST_HEAP_PAGE_FIRST_ALLOCATION_SIZE_BYTES,
                         OS_TEST_HEAP_PAGE_MINIMUM_ALIGNMENT_BYTES,
                         first_allocation) == os::kernel::KernelHeapStatus::Succeeded &&
        heap.TryAllocate(OS_TEST_HEAP_PAGE_SECOND_ALLOCATION_SIZE_BYTES,
                         OS_TEST_HEAP_PAGE_SECOND_ALIGNMENT_BYTES,
                         second_allocation) == os::kernel::KernelHeapStatus::Succeeded;
    test_context.Expect(allocations_succeeded &&
                            (reinterpret_cast<uint64_t>(second_allocation) &
                             (OS_TEST_HEAP_PAGE_SECOND_ALIGNMENT_BYTES -
                              OS_TEST_HEAP_PAGE_ALIGNMENT_MASK_DECREMENT)) == 0ULL,
                        OS_TEST_HEAP_PAGE_HEAP_ALIGNMENT);

    void *unchanged_allocation = reinterpret_cast<void *>(OS_TEST_HEAP_PAGE_INVALID_POINTER_VALUE);
    test_context.Expect(heap.TryAllocate(OS_TEST_HEAP_PAGE_FIRST_ALLOCATION_SIZE_BYTES,
                                         OS_TEST_HEAP_PAGE_INVALID_ALIGNMENT_BYTES,
                                         unchanged_allocation) ==
                                os::kernel::KernelHeapStatus::InvalidAlignment &&
                            reinterpret_cast<uint64_t>(unchanged_allocation) ==
                                OS_TEST_HEAP_PAGE_INVALID_POINTER_VALUE,
                        OS_TEST_HEAP_PAGE_HEAP_FAILURE_ATOMIC);
    test_context.Expect(heap.TryAllocate(OS_TEST_HEAP_PAGE_BUFFER_SIZE_BYTES,
                                         OS_TEST_HEAP_PAGE_SECOND_ALIGNMENT_BYTES,
                                         unchanged_allocation) ==
                            os::kernel::KernelHeapStatus::OutOfMemory,
                        OS_TEST_HEAP_PAGE_HEAP_EXHAUSTION);

    test_context.Expect(
        os::kernel::IsCanonicalVirtualAddress(OS_TEST_HEAP_PAGE_LOW_CANONICAL_ADDRESS) &&
            os::kernel::IsCanonicalVirtualAddress(OS_TEST_HEAP_PAGE_HIGH_CANONICAL_ADDRESS),
        OS_TEST_HEAP_PAGE_CANONICAL);
    test_context.Expect(
        !os::kernel::IsCanonicalVirtualAddress(OS_TEST_HEAP_PAGE_NON_CANONICAL_ADDRESS),
        OS_TEST_HEAP_PAGE_NON_CANONICAL);

    const os::kernel::PageTableIndices indices =
        os::kernel::CalculatePageTableIndices(OS_TEST_HEAP_PAGE_INDEX_ADDRESS);
    test_context.Expect(indices.level4 == OS_TEST_HEAP_PAGE_EXPECTED_LEVEL4_INDEX &&
                            indices.level3 == OS_TEST_HEAP_PAGE_EXPECTED_LEVEL3_INDEX &&
                            indices.level2 == OS_TEST_HEAP_PAGE_EXPECTED_LEVEL2_INDEX &&
                            indices.level1 == OS_TEST_HEAP_PAGE_EXPECTED_LEVEL1_INDEX,
                        OS_TEST_HEAP_PAGE_INDICES);

    const os::kernel::PagePermissions permissions{
        .writable = true,
        .executable = false,
        .user_accessible = true,
        .cache_disabled = true,
    };
    const os::kernel::PageMapping mapping = os::kernel::DecodePageTableLeafEntry(
        os::kernel::EncodePageTableLeafEntry(OS_TEST_HEAP_PAGE_PHYSICAL_ADDRESS, permissions));
    test_context.Expect(mapping.physical_address == OS_TEST_HEAP_PAGE_PHYSICAL_ADDRESS &&
                            mapping.permissions.writable == permissions.writable &&
                            mapping.permissions.executable == permissions.executable &&
                            mapping.permissions.user_accessible == permissions.user_accessible &&
                            mapping.permissions.cache_disabled == permissions.cache_disabled,
                        OS_TEST_HEAP_PAGE_ENTRY_ROUND_TRIP);

    test_context.Expect(
        os::kernel::IsPageTableMemoryAccessValid(os::kernel::PageTableMemoryAccess{
            .maximum_physical_address_exclusive = OS_TEST_HEAP_PAGE_DIRECT_MAP_CAPACITY_BYTES,
            .physical_memory_virtual_base = OS_TEST_HEAP_PAGE_DIRECT_MAP_BASE,
            .allocation_maximum_physical_address_exclusive =
                OS_TEST_HEAP_PAGE_DIRECT_MAP_ALLOCATION_LIMIT_BYTES,
            .invalidate_active_mappings = true,
        }),
        OS_TEST_HEAP_PAGE_DIRECT_MAP_ACCESS);
    test_context.Expect(!os::kernel::IsPageTableMemoryAccessValid(os::kernel::PageTableMemoryAccess{
                            .maximum_physical_address_exclusive =
                                OS_TEST_HEAP_PAGE_INVALID_DIRECT_MAP_CAPACITY_BYTES,
                            .physical_memory_virtual_base = OS_TEST_HEAP_PAGE_DIRECT_MAP_BASE,
                            .allocation_maximum_physical_address_exclusive =
                                OS_TEST_HEAP_PAGE_DIRECT_MAP_ALLOCATION_LIMIT_BYTES,
                            .invalidate_active_mappings = true,
                        }),
                        OS_TEST_HEAP_PAGE_DIRECT_MAP_OVERFLOW);

    return test_context.ExitCode();
}

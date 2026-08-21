#include <os/kernel/memory/file_page_cache.hpp>
#include <os/kernel/memory/kernel_heap.hpp>
#include <os/kernel/memory/physical_frame_allocator.hpp>
#include <os/kernel/memory/physical_memory_map.hpp>
#include <test_context.hpp>

#include <stdint.h>
#include <string_view>

namespace {

constexpr std::string_view OS_TEST_FILE_PAGE_CACHE_DYNAMIC_SUITE_NAME =
    "kernel/file_page_cache/dynamic_lifecycle";
constexpr std::string_view OS_TEST_FILE_PAGE_CACHE_DYNAMIC_CAPACITY =
    "动态 FilePageCache 必须在单一文件地址空间持有 8192 个权威页面";
constexpr std::string_view OS_TEST_FILE_PAGE_CACHE_DYNAMIC_RECLAIM =
    "裁剪销毁必须归还全部 frame、radix、页面和地址空间元数据";

constexpr uint64_t OS_TEST_FILE_PAGE_CACHE_DYNAMIC_PAGE_COUNT = 8192ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_CACHE_DYNAMIC_MANAGED_PAGE_COUNT =
    OS_TEST_FILE_PAGE_CACHE_DYNAMIC_PAGE_COUNT + 8ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_CACHE_DYNAMIC_STATE_VALUES_PER_BYTE = 4ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_CACHE_DYNAMIC_STATE_STORAGE_SIZE_BYTES =
    (OS_TEST_FILE_PAGE_CACHE_DYNAMIC_MANAGED_PAGE_COUNT +
     OS_TEST_FILE_PAGE_CACHE_DYNAMIC_STATE_VALUES_PER_BYTE - 1ULL) /
    OS_TEST_FILE_PAGE_CACHE_DYNAMIC_STATE_VALUES_PER_BYTE;
constexpr uint64_t OS_TEST_FILE_PAGE_CACHE_DYNAMIC_MEMORY_SIZE_BYTES =
    OS_TEST_FILE_PAGE_CACHE_DYNAMIC_MANAGED_PAGE_COUNT *
    os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_TEST_FILE_PAGE_CACHE_DYNAMIC_METADATA_SIZE_BYTES = 32ULL * 1024ULL * 1024ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_CACHE_DYNAMIC_ALIGNMENT_BYTES = 64ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_CACHE_DYNAMIC_SUPERBLOCK_IDENTIFIER = 173ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_CACHE_DYNAMIC_SUPERBLOCK_GENERATION = 5ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_CACHE_DYNAMIC_NODE_IDENTIFIER = 16381ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_CACHE_DYNAMIC_NODE_GENERATION = 29ULL;
constexpr uint8_t OS_TEST_FILE_PAGE_CACHE_DYNAMIC_FIRST_PATTERN = 0x6DU;
constexpr uint8_t OS_TEST_FILE_PAGE_CACHE_DYNAMIC_LAST_PATTERN = 0xD6U;

struct TestMemory final {
    uint8_t bytes[OS_TEST_FILE_PAGE_CACHE_DYNAMIC_MEMORY_SIZE_BYTES];
};

alignas(OS_TEST_FILE_PAGE_CACHE_DYNAMIC_ALIGNMENT_BYTES) TestMemory test_memory{};
alignas(OS_TEST_FILE_PAGE_CACHE_DYNAMIC_ALIGNMENT_BYTES) uint8_t
    metadata_heap_storage[OS_TEST_FILE_PAGE_CACHE_DYNAMIC_METADATA_SIZE_BYTES]{};

[[nodiscard]] uint8_t *AccessPage(void *const context, const uint64_t physical_address) noexcept {
    if (context == nullptr || physical_address > OS_TEST_FILE_PAGE_CACHE_DYNAMIC_MEMORY_SIZE_BYTES -
                                                     os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) {
        return nullptr;
    }
    TestMemory &memory = *static_cast<TestMemory *>(context);
    return memory.bytes + physical_address;
}

[[nodiscard]] bool ReadPage(void *const context, const os::kernel::FilePageIdentity &identity,
                            uint8_t *const destination, const uint64_t capacity_bytes) noexcept {
    static_cast<void>(context);
    if (destination == nullptr || capacity_bytes != os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) {
        return false;
    }
    destination[0] = static_cast<uint8_t>(OS_TEST_FILE_PAGE_CACHE_DYNAMIC_FIRST_PATTERN ^
                                          static_cast<uint8_t>(identity.page_index));
    destination[capacity_bytes - 1ULL] = OS_TEST_FILE_PAGE_CACHE_DYNAMIC_LAST_PATTERN;
    return true;
}

[[nodiscard]] os::kernel::FilePageIdentity MakeIdentity(const uint64_t page_index) noexcept {
    return os::kernel::FilePageIdentity{
        .file =
            {
                .superblock_identifier = OS_TEST_FILE_PAGE_CACHE_DYNAMIC_SUPERBLOCK_IDENTIFIER,
                .superblock_generation = OS_TEST_FILE_PAGE_CACHE_DYNAMIC_SUPERBLOCK_GENERATION,
                .node_identifier = OS_TEST_FILE_PAGE_CACHE_DYNAMIC_NODE_IDENTIFIER,
                .node_generation = OS_TEST_FILE_PAGE_CACHE_DYNAMIC_NODE_GENERATION,
            },
        .page_index = page_index,
    };
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_FILE_PAGE_CACHE_DYNAMIC_SUITE_NAME};
    uint8_t state_storage[OS_TEST_FILE_PAGE_CACHE_DYNAMIC_STATE_STORAGE_SIZE_BYTES]{};
    os::kernel::PhysicalFrameAllocator frame_allocator{
        state_storage,
        sizeof(state_storage),
    };
    const os::kernel::PhysicalMemoryMapEntry memory_map[] = {
        {
            .base_address = 0ULL,
            .length_bytes = OS_TEST_FILE_PAGE_CACHE_DYNAMIC_MEMORY_SIZE_BYTES,
            .type = os::kernel::OS_KERNEL_MEMORY_MAP_USABLE_REGION_TYPE,
            .attributes = 0U,
        },
    };
    os::kernel::KernelHeap metadata_heap{};
    os::kernel::FilePageCache cache{};
    const bool initialized =
        frame_allocator.Initialize(memory_map, 1ULL,
                                   OS_TEST_FILE_PAGE_CACHE_DYNAMIC_MEMORY_SIZE_BYTES) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        metadata_heap.Initialize(reinterpret_cast<uint64_t>(metadata_heap_storage),
                                 sizeof(metadata_heap_storage)) ==
            os::kernel::KernelHeapStatus::Succeeded &&
        cache.Initialize(metadata_heap, OS_TEST_FILE_PAGE_CACHE_DYNAMIC_PAGE_COUNT,
                         OS_TEST_FILE_PAGE_CACHE_DYNAMIC_PAGE_COUNT / 2ULL, frame_allocator,
                         &test_memory, AccessPage) == os::kernel::FilePageCacheStatus::Succeeded;
    const os::kernel::PhysicalFrameAllocatorStatistics frames_before = frame_allocator.Statistics();

    bool populated = initialized;
    for (uint64_t page_index = 0ULL;
         populated && page_index < OS_TEST_FILE_PAGE_CACHE_DYNAMIC_PAGE_COUNT; ++page_index) {
        uint64_t physical_address = 0ULL;
        bool cache_hit = true;
        const os::kernel::FilePageIdentity identity = MakeIdentity(page_index);
        populated =
            cache.Acquire(identity, &test_memory, ReadPage, physical_address, cache_hit) ==
                os::kernel::FilePageCacheStatus::Succeeded &&
            !cache_hit &&
            cache.Release(identity, physical_address) == os::kernel::FilePageCacheStatus::Succeeded;
    }
    uint64_t retained_physical_address = 0ULL;
    bool retained_cache_hit = false;
    const os::kernel::FilePageIdentity retained_identity =
        MakeIdentity(OS_TEST_FILE_PAGE_CACHE_DYNAMIC_PAGE_COUNT / 2ULL);
    const os::kernel::FilePageCacheStatistics populated_statistics = cache.Statistics();
    const bool capacity_valid =
        populated && populated_statistics.capacity == OS_TEST_FILE_PAGE_CACHE_DYNAMIC_PAGE_COUNT &&
        populated_statistics.resident_page_count == OS_TEST_FILE_PAGE_CACHE_DYNAMIC_PAGE_COUNT &&
        populated_statistics.address_space_count == 1ULL &&
        populated_statistics.metadata_allocation_failure_count == 0ULL &&
        cache.Acquire(retained_identity, &test_memory, ReadPage, retained_physical_address,
                      retained_cache_hit) == os::kernel::FilePageCacheStatus::Succeeded &&
        retained_cache_hit &&
        cache.Release(retained_identity, retained_physical_address) ==
            os::kernel::FilePageCacheStatus::Succeeded &&
        cache.Validate() == os::kernel::FilePageCacheStatus::Succeeded;
    test_context.Expect(capacity_valid, OS_TEST_FILE_PAGE_CACHE_DYNAMIC_CAPACITY);

    uint64_t reclaimed_page_count = 0ULL;
    const bool reclaimed =
        capacity_valid &&
        cache.Trim(0ULL, reclaimed_page_count) == os::kernel::FilePageCacheStatus::Succeeded &&
        reclaimed_page_count == OS_TEST_FILE_PAGE_CACHE_DYNAMIC_PAGE_COUNT &&
        cache.Statistics().resident_page_count == 0ULL &&
        cache.Statistics().address_space_count == 0ULL &&
        cache.Validate() == os::kernel::FilePageCacheStatus::Succeeded &&
        cache.Destroy() == os::kernel::FilePageCacheStatus::Succeeded &&
        metadata_heap.Validate() == os::kernel::KernelHeapStatus::Succeeded &&
        metadata_heap.Statistics().allocation_count == 0ULL &&
        frame_allocator.Statistics().allocated_frame_count == frames_before.allocated_frame_count &&
        frame_allocator.Statistics().free_frame_count == frames_before.free_frame_count;
    test_context.Expect(reclaimed, OS_TEST_FILE_PAGE_CACHE_DYNAMIC_RECLAIM);
    return test_context.ExitCode();
}

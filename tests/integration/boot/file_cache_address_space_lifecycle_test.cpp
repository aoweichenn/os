#include <os/kernel/memory/file_cache_address_space.hpp>
#include <os/kernel/memory/kernel_heap.hpp>
#include <os/kernel/memory/physical_frame_allocator.hpp>
#include <test_context.hpp>

#include <stdint.h>
#include <string_view>

namespace {

constexpr std::string_view OS_TEST_FILE_CACHE_LIFECYCLE_SUITE_NAME =
    "kernel/file_cache_address_space/lifecycle";
constexpr std::string_view OS_TEST_FILE_CACHE_LIFECYCLE_DYNAMIC_CAPACITY =
    "动态地址空间必须容纳超过旧 4096 项上限的连续与已标记页面";
constexpr std::string_view OS_TEST_FILE_CACHE_LIFECYCLE_RECLAIM =
    "逆序回收全部页面后 radix 节点、页面元数据与堆分配必须归零";

constexpr uint64_t OS_TEST_FILE_CACHE_LIFECYCLE_ALIGNMENT_BYTES = 64ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_LIFECYCLE_HEAP_SIZE_BYTES = 4ULL * 1024ULL * 1024ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_LIFECYCLE_PAGE_COUNT = 8192ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_LIFECYCLE_DIRTY_STEP = 257ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_LIFECYCLE_REFERENCE_STEP = 251ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_LIFECYCLE_VALIDATION_STEP = 1024ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_LIFECYCLE_SUPERBLOCK_IDENTIFIER = 101ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_LIFECYCLE_SUPERBLOCK_GENERATION = 3ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_LIFECYCLE_NODE_IDENTIFIER = 4099ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_LIFECYCLE_NODE_GENERATION = 17ULL;

alignas(OS_TEST_FILE_CACHE_LIFECYCLE_ALIGNMENT_BYTES) uint8_t
    heap_storage[OS_TEST_FILE_CACHE_LIFECYCLE_HEAP_SIZE_BYTES]{};

[[nodiscard]] uint64_t AddressOf(void *const pointer) noexcept {
    return reinterpret_cast<uint64_t>(pointer);
}

[[nodiscard]] uint64_t PhysicalAddressForPage(const uint64_t page_index) noexcept {
    return (page_index + 1ULL) * os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_FILE_CACHE_LIFECYCLE_SUITE_NAME};
    os::kernel::KernelHeap heap{};
    os::kernel::FileCacheAddressSpace address_space{};
    const os::kernel::FileCacheIdentity identity{
        .superblock_identifier = OS_TEST_FILE_CACHE_LIFECYCLE_SUPERBLOCK_IDENTIFIER,
        .superblock_generation = OS_TEST_FILE_CACHE_LIFECYCLE_SUPERBLOCK_GENERATION,
        .node_identifier = OS_TEST_FILE_CACHE_LIFECYCLE_NODE_IDENTIFIER,
        .node_generation = OS_TEST_FILE_CACHE_LIFECYCLE_NODE_GENERATION,
    };
    bool initialized = heap.Initialize(AddressOf(heap_storage), sizeof(heap_storage)) ==
                           os::kernel::KernelHeapStatus::Succeeded &&
                       address_space.Initialize(identity, heap) ==
                           os::kernel::FileCacheAddressSpaceStatus::Succeeded;

    bool populated = initialized;
    for (uint64_t page_index = 0ULL;
         populated && page_index < OS_TEST_FILE_CACHE_LIFECYCLE_PAGE_COUNT; ++page_index) {
        populated = address_space.Insert(page_index, PhysicalAddressForPage(page_index),
                                         os::kernel::FileCachePageState::Clean) ==
                    os::kernel::FileCacheAddressSpaceStatus::Succeeded;
        if (populated && page_index % OS_TEST_FILE_CACHE_LIFECYCLE_DIRTY_STEP == 0ULL) {
            populated = address_space.Transition(page_index, PhysicalAddressForPage(page_index),
                                                 os::kernel::FileCachePageState::Clean,
                                                 os::kernel::FileCachePageState::Dirty) ==
                        os::kernel::FileCacheAddressSpaceStatus::Succeeded;
        }
        if (populated && page_index % OS_TEST_FILE_CACHE_LIFECYCLE_REFERENCE_STEP == 0ULL) {
            populated = address_space.Retain(page_index, PhysicalAddressForPage(page_index)) ==
                        os::kernel::FileCacheAddressSpaceStatus::Succeeded;
        }
        if (populated &&
            (page_index + 1ULL) % OS_TEST_FILE_CACHE_LIFECYCLE_VALIDATION_STEP == 0ULL) {
            populated =
                address_space.Validate() == os::kernel::FileCacheAddressSpaceStatus::Succeeded;
        }
    }
    const os::kernel::FileCacheAddressSpaceStatistics populated_statistics =
        address_space.Statistics();
    os::kernel::FileCachePageSnapshot first_dirty_page{};
    const bool dynamic_capacity_valid =
        populated &&
        populated_statistics.resident_page_count == OS_TEST_FILE_CACHE_LIFECYCLE_PAGE_COUNT &&
        populated_statistics.peak_resident_page_count == OS_TEST_FILE_CACHE_LIFECYCLE_PAGE_COUNT &&
        populated_statistics.index.entry_count == OS_TEST_FILE_CACHE_LIFECYCLE_PAGE_COUNT &&
        populated_statistics.index.node_count < OS_TEST_FILE_CACHE_LIFECYCLE_PAGE_COUNT &&
        populated_statistics.index.current_root_level == 2ULL &&
        address_space.FindNext(0ULL, OS_TEST_FILE_CACHE_LIFECYCLE_PAGE_COUNT - 1ULL,
                               os::kernel::FileCachePageState::Dirty, first_dirty_page) ==
            os::kernel::FileCacheAddressSpaceStatus::Succeeded &&
        first_dirty_page.page_index == 0ULL &&
        address_space.Validate() == os::kernel::FileCacheAddressSpaceStatus::Succeeded;
    test_context.Expect(dynamic_capacity_valid, OS_TEST_FILE_CACHE_LIFECYCLE_DYNAMIC_CAPACITY);

    bool reclaimed = dynamic_capacity_valid;
    for (uint64_t remaining_page_count = OS_TEST_FILE_CACHE_LIFECYCLE_PAGE_COUNT;
         reclaimed && remaining_page_count != 0ULL; --remaining_page_count) {
        const uint64_t page_index = remaining_page_count - 1ULL;
        const uint64_t physical_address = PhysicalAddressForPage(page_index);
        if (page_index % OS_TEST_FILE_CACHE_LIFECYCLE_REFERENCE_STEP == 0ULL) {
            reclaimed = address_space.Release(page_index, physical_address) ==
                        os::kernel::FileCacheAddressSpaceStatus::Succeeded;
        }
        if (reclaimed && page_index % OS_TEST_FILE_CACHE_LIFECYCLE_DIRTY_STEP == 0ULL) {
            reclaimed = address_space.Transition(page_index, physical_address,
                                                 os::kernel::FileCachePageState::Dirty,
                                                 os::kernel::FileCachePageState::Writeback) ==
                            os::kernel::FileCacheAddressSpaceStatus::Succeeded &&
                        address_space.Transition(page_index, physical_address,
                                                 os::kernel::FileCachePageState::Writeback,
                                                 os::kernel::FileCachePageState::Clean) ==
                            os::kernel::FileCacheAddressSpaceStatus::Succeeded;
        }
        if (reclaimed) {
            reclaimed = address_space.Remove(page_index, physical_address) ==
                        os::kernel::FileCacheAddressSpaceStatus::Succeeded;
        }
        if (reclaimed &&
            remaining_page_count % OS_TEST_FILE_CACHE_LIFECYCLE_VALIDATION_STEP == 0ULL) {
            reclaimed =
                address_space.Validate() == os::kernel::FileCacheAddressSpaceStatus::Succeeded;
        }
    }
    const os::kernel::FileCacheAddressSpaceStatistics reclaimed_statistics =
        address_space.Statistics();
    reclaimed = reclaimed &&
                address_space.Validate() == os::kernel::FileCacheAddressSpaceStatus::Succeeded &&
                reclaimed_statistics.resident_page_count == 0ULL &&
                reclaimed_statistics.active_mapping_reference_count == 0ULL &&
                reclaimed_statistics.index.entry_count == 0ULL &&
                reclaimed_statistics.index.node_count == 0ULL &&
                reclaimed_statistics.removal_count == OS_TEST_FILE_CACHE_LIFECYCLE_PAGE_COUNT &&
                address_space.Destroy() == os::kernel::FileCacheAddressSpaceStatus::Succeeded &&
                heap.Validate() == os::kernel::KernelHeapStatus::Succeeded &&
                heap.Statistics().allocation_count == 0ULL &&
                heap.Statistics().active_requested_bytes == 0ULL;
    test_context.Expect(reclaimed, OS_TEST_FILE_CACHE_LIFECYCLE_RECLAIM);

    return test_context.ExitCode();
}

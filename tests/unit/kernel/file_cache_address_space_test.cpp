#include <os/kernel/memory/file_cache_address_space.hpp>
#include <os/kernel/memory/kernel_heap.hpp>
#include <os/kernel/memory/physical_frame_allocator.hpp>
#include <os/kernel/memory/sparse_page_index.hpp>
#include <test_context.hpp>

#include <stdint.h>
#include <string_view>

namespace {

constexpr std::string_view OS_TEST_FILE_CACHE_ADDRESS_SPACE_SUITE_NAME =
    "kernel/file_cache_address_space/unit";
constexpr std::string_view OS_TEST_FILE_CACHE_ADDRESS_SPACE_BOUNDARIES =
    "64 位 radix 边界必须按需扩层并保持唯一查找";
constexpr std::string_view OS_TEST_FILE_CACHE_ADDRESS_SPACE_MARKS =
    "Present、Dirty、Writeback 与 Error 标记必须独立聚合和有序查找";
constexpr std::string_view OS_TEST_FILE_CACHE_ADDRESS_SPACE_RECLAIM =
    "删除末项必须裁剪空分支并把动态元数据归还 KernelHeap";
constexpr std::string_view OS_TEST_FILE_CACHE_ADDRESS_SPACE_ROLLBACK =
    "节点分配失败必须回滚全部未发布分支且不保留条目";
constexpr std::string_view OS_TEST_FILE_CACHE_ADDRESS_SPACE_STATES =
    "文件缓存地址空间必须冻结引用和 Clean/Dirty/Writeback/Error 转换";
constexpr std::string_view OS_TEST_FILE_CACHE_ADDRESS_SPACE_DESTROY =
    "地址空间清空销毁后必须恢复堆活动分配基线";

constexpr uint64_t OS_TEST_FILE_CACHE_ADDRESS_SPACE_HEAP_SIZE_BYTES = 128ULL * 1024ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_ADDRESS_SPACE_TINY_HEAP_SIZE_BYTES = 2048ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_ADDRESS_SPACE_ALIGNMENT_BYTES = 64ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_ADDRESS_SPACE_ENTRY_COUNT = 7ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_ADDRESS_SPACE_FIRST_INDEX = 0ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_ADDRESS_SPACE_LAST_LEAF_INDEX = 63ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_ADDRESS_SPACE_SECOND_LEAF_INDEX = 64ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_ADDRESS_SPACE_SECOND_LEVEL_LAST_INDEX = 4095ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_ADDRESS_SPACE_THIRD_LEVEL_FIRST_INDEX = 4096ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_ADDRESS_SPACE_HIGH_INDEX = 1ULL << 42ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_ADDRESS_SPACE_MAXIMUM_INDEX = UINT64_MAX;
constexpr uint64_t OS_TEST_FILE_CACHE_ADDRESS_SPACE_SUPERBLOCK_IDENTIFIER = 23ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_ADDRESS_SPACE_SUPERBLOCK_GENERATION = 5ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_ADDRESS_SPACE_NODE_IDENTIFIER = 71ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_ADDRESS_SPACE_NODE_GENERATION = 11ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_ADDRESS_SPACE_FIRST_PHYSICAL_ADDRESS = 0x1000ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_ADDRESS_SPACE_SECOND_PHYSICAL_ADDRESS = 0x2000ULL;

alignas(OS_TEST_FILE_CACHE_ADDRESS_SPACE_ALIGNMENT_BYTES) uint8_t
    index_heap_storage[OS_TEST_FILE_CACHE_ADDRESS_SPACE_HEAP_SIZE_BYTES]{};
alignas(OS_TEST_FILE_CACHE_ADDRESS_SPACE_ALIGNMENT_BYTES) uint8_t
    address_space_heap_storage[OS_TEST_FILE_CACHE_ADDRESS_SPACE_HEAP_SIZE_BYTES]{};
alignas(OS_TEST_FILE_CACHE_ADDRESS_SPACE_ALIGNMENT_BYTES) uint8_t
    tiny_index_heap_storage[OS_TEST_FILE_CACHE_ADDRESS_SPACE_TINY_HEAP_SIZE_BYTES]{};
alignas(OS_TEST_FILE_CACHE_ADDRESS_SPACE_ALIGNMENT_BYTES) uint8_t
    tiny_address_space_heap_storage[OS_TEST_FILE_CACHE_ADDRESS_SPACE_TINY_HEAP_SIZE_BYTES]{};

constexpr uint64_t
    OS_TEST_FILE_CACHE_ADDRESS_SPACE_INDICES[OS_TEST_FILE_CACHE_ADDRESS_SPACE_ENTRY_COUNT] = {
        OS_TEST_FILE_CACHE_ADDRESS_SPACE_FIRST_INDEX,
        OS_TEST_FILE_CACHE_ADDRESS_SPACE_LAST_LEAF_INDEX,
        OS_TEST_FILE_CACHE_ADDRESS_SPACE_SECOND_LEAF_INDEX,
        OS_TEST_FILE_CACHE_ADDRESS_SPACE_SECOND_LEVEL_LAST_INDEX,
        OS_TEST_FILE_CACHE_ADDRESS_SPACE_THIRD_LEVEL_FIRST_INDEX,
        OS_TEST_FILE_CACHE_ADDRESS_SPACE_HIGH_INDEX,
        OS_TEST_FILE_CACHE_ADDRESS_SPACE_MAXIMUM_INDEX,
};

[[nodiscard]] uint64_t AddressOf(void *const pointer) noexcept {
    return reinterpret_cast<uint64_t>(pointer);
}

[[nodiscard]] os::kernel::FileCacheIdentity MakeIdentity() noexcept {
    return os::kernel::FileCacheIdentity{
        .superblock_identifier = OS_TEST_FILE_CACHE_ADDRESS_SPACE_SUPERBLOCK_IDENTIFIER,
        .superblock_generation = OS_TEST_FILE_CACHE_ADDRESS_SPACE_SUPERBLOCK_GENERATION,
        .node_identifier = OS_TEST_FILE_CACHE_ADDRESS_SPACE_NODE_IDENTIFIER,
        .node_generation = OS_TEST_FILE_CACHE_ADDRESS_SPACE_NODE_GENERATION,
    };
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_FILE_CACHE_ADDRESS_SPACE_SUITE_NAME};

    os::kernel::KernelHeap index_heap{};
    const bool index_heap_initialized =
        index_heap.Initialize(AddressOf(index_heap_storage), sizeof(index_heap_storage)) ==
        os::kernel::KernelHeapStatus::Succeeded;
    os::kernel::SparsePageIndex index{};
    const bool index_initialized =
        index_heap_initialized &&
        index.Initialize(index_heap) == os::kernel::SparsePageIndexStatus::Succeeded;
    uint64_t payloads[OS_TEST_FILE_CACHE_ADDRESS_SPACE_ENTRY_COUNT]{};
    bool boundaries_valid = index_initialized;
    for (uint64_t entry_index = 0ULL;
         boundaries_valid && entry_index < OS_TEST_FILE_CACHE_ADDRESS_SPACE_ENTRY_COUNT;
         ++entry_index) {
        payloads[entry_index] = OS_TEST_FILE_CACHE_ADDRESS_SPACE_INDICES[entry_index] ^
                                OS_TEST_FILE_CACHE_ADDRESS_SPACE_SUPERBLOCK_IDENTIFIER;
        boundaries_valid =
            index.Insert(OS_TEST_FILE_CACHE_ADDRESS_SPACE_INDICES[entry_index],
                         &payloads[entry_index]) == os::kernel::SparsePageIndexStatus::Succeeded;
    }
    for (uint64_t entry_index = 0ULL;
         boundaries_valid && entry_index < OS_TEST_FILE_CACHE_ADDRESS_SPACE_ENTRY_COUNT;
         ++entry_index) {
        void *entry = nullptr;
        boundaries_valid = index.Lookup(OS_TEST_FILE_CACHE_ADDRESS_SPACE_INDICES[entry_index],
                                        entry) == os::kernel::SparsePageIndexStatus::Succeeded &&
                           entry == &payloads[entry_index];
    }
    boundaries_valid =
        boundaries_valid &&
        index.Insert(OS_TEST_FILE_CACHE_ADDRESS_SPACE_FIRST_INDEX, &payloads[0]) ==
            os::kernel::SparsePageIndexStatus::AlreadyExists &&
        index.Validate() == os::kernel::SparsePageIndexStatus::Succeeded &&
        index.Statistics().entry_count == OS_TEST_FILE_CACHE_ADDRESS_SPACE_ENTRY_COUNT &&
        index.Statistics().current_root_level ==
            os::kernel::OS_KERNEL_SPARSE_PAGE_INDEX_MAXIMUM_ROOT_LEVEL;
    test_context.Expect(boundaries_valid, OS_TEST_FILE_CACHE_ADDRESS_SPACE_BOUNDARIES);

    bool marks_valid = index.SetMark(OS_TEST_FILE_CACHE_ADDRESS_SPACE_SECOND_LEAF_INDEX,
                                     os::kernel::SparsePageIndexMark::Dirty) ==
                           os::kernel::SparsePageIndexStatus::Succeeded &&
                       index.SetMark(OS_TEST_FILE_CACHE_ADDRESS_SPACE_THIRD_LEVEL_FIRST_INDEX,
                                     os::kernel::SparsePageIndexMark::Writeback) ==
                           os::kernel::SparsePageIndexStatus::Succeeded &&
                       index.SetMark(OS_TEST_FILE_CACHE_ADDRESS_SPACE_MAXIMUM_INDEX,
                                     os::kernel::SparsePageIndexMark::Error) ==
                           os::kernel::SparsePageIndexStatus::Succeeded &&
                       index.SetMark(OS_TEST_FILE_CACHE_ADDRESS_SPACE_MAXIMUM_INDEX,
                                     os::kernel::SparsePageIndexMark::Dirty) ==
                           os::kernel::SparsePageIndexStatus::Succeeded &&
                       index.SetMark(OS_TEST_FILE_CACHE_ADDRESS_SPACE_FIRST_INDEX,
                                     os::kernel::SparsePageIndexMark::Present) ==
                           os::kernel::SparsePageIndexStatus::InvalidMark;
    uint64_t found_page_index = 0ULL;
    void *found_entry = nullptr;
    marks_valid = marks_valid &&
                  index.FindNext(OS_TEST_FILE_CACHE_ADDRESS_SPACE_FIRST_INDEX,
                                 OS_TEST_FILE_CACHE_ADDRESS_SPACE_MAXIMUM_INDEX,
                                 os::kernel::SparsePageIndexMark::Dirty, found_page_index,
                                 found_entry) == os::kernel::SparsePageIndexStatus::Succeeded &&
                  found_page_index == OS_TEST_FILE_CACHE_ADDRESS_SPACE_SECOND_LEAF_INDEX &&
                  found_entry == &payloads[2] &&
                  index.FindNext(OS_TEST_FILE_CACHE_ADDRESS_SPACE_THIRD_LEVEL_FIRST_INDEX,
                                 OS_TEST_FILE_CACHE_ADDRESS_SPACE_MAXIMUM_INDEX,
                                 os::kernel::SparsePageIndexMark::Error, found_page_index,
                                 found_entry) == os::kernel::SparsePageIndexStatus::Succeeded &&
                  found_page_index == OS_TEST_FILE_CACHE_ADDRESS_SPACE_MAXIMUM_INDEX &&
                  found_entry == &payloads[6] &&
                  index.ClearMark(OS_TEST_FILE_CACHE_ADDRESS_SPACE_MAXIMUM_INDEX,
                                  os::kernel::SparsePageIndexMark::Dirty) ==
                      os::kernel::SparsePageIndexStatus::Succeeded &&
                  index.Validate() == os::kernel::SparsePageIndexStatus::Succeeded;
    const os::kernel::SparsePageIndexStatistics marked_statistics = index.Statistics();
    marks_valid = marks_valid && marked_statistics.dirty_entry_count == 1ULL &&
                  marked_statistics.writeback_entry_count == 1ULL &&
                  marked_statistics.error_entry_count == 1ULL;
    test_context.Expect(marks_valid, OS_TEST_FILE_CACHE_ADDRESS_SPACE_MARKS);

    bool reclaimed = true;
    for (uint64_t remaining_count = OS_TEST_FILE_CACHE_ADDRESS_SPACE_ENTRY_COUNT;
         reclaimed && remaining_count != 0ULL; --remaining_count) {
        const uint64_t entry_index = remaining_count - 1ULL;
        void *removed_entry = nullptr;
        reclaimed = index.Erase(OS_TEST_FILE_CACHE_ADDRESS_SPACE_INDICES[entry_index],
                                removed_entry) == os::kernel::SparsePageIndexStatus::Succeeded &&
                    removed_entry == &payloads[entry_index];
    }
    const os::kernel::SparsePageIndexStatistics reclaimed_statistics = index.Statistics();
    reclaimed = reclaimed && index.Validate() == os::kernel::SparsePageIndexStatus::Succeeded &&
                reclaimed_statistics.entry_count == 0ULL &&
                reclaimed_statistics.node_count == 0ULL &&
                reclaimed_statistics.root_shrink_count != 0ULL &&
                reclaimed_statistics.branch_prune_count != 0ULL &&
                index_heap.Statistics().allocation_count == 0ULL &&
                index.Destroy() == os::kernel::SparsePageIndexStatus::Succeeded;
    test_context.Expect(reclaimed, OS_TEST_FILE_CACHE_ADDRESS_SPACE_RECLAIM);

    os::kernel::KernelHeap tiny_index_heap{};
    const bool tiny_index_heap_initialized =
        tiny_index_heap.Initialize(AddressOf(tiny_index_heap_storage),
                                   sizeof(tiny_index_heap_storage)) ==
        os::kernel::KernelHeapStatus::Succeeded;
    os::kernel::SparsePageIndex failing_index{};
    const bool rollback_valid =
        tiny_index_heap_initialized &&
        failing_index.Initialize(tiny_index_heap) == os::kernel::SparsePageIndexStatus::Succeeded &&
        failing_index.Insert(OS_TEST_FILE_CACHE_ADDRESS_SPACE_MAXIMUM_INDEX, &payloads[0]) ==
            os::kernel::SparsePageIndexStatus::AllocationFailed &&
        failing_index.Validate() == os::kernel::SparsePageIndexStatus::Succeeded &&
        failing_index.Statistics().entry_count == 0ULL &&
        failing_index.Statistics().node_count == 0ULL &&
        failing_index.Statistics().allocation_failure_count == 1ULL &&
        failing_index.Statistics().rollback_node_release_count != 0ULL &&
        tiny_index_heap.Statistics().allocation_count == 0ULL &&
        failing_index.Destroy() == os::kernel::SparsePageIndexStatus::Succeeded;
    test_context.Expect(rollback_valid, OS_TEST_FILE_CACHE_ADDRESS_SPACE_ROLLBACK);

    os::kernel::KernelHeap address_space_heap{};
    const bool address_space_heap_initialized =
        address_space_heap.Initialize(AddressOf(address_space_heap_storage),
                                      sizeof(address_space_heap_storage)) ==
        os::kernel::KernelHeapStatus::Succeeded;
    os::kernel::FileCacheAddressSpace address_space{};
    const bool address_space_initialized =
        address_space_heap_initialized &&
        address_space.Initialize(MakeIdentity(), address_space_heap) ==
            os::kernel::FileCacheAddressSpaceStatus::Succeeded;
    bool state_contract_valid =
        address_space_initialized &&
        address_space.Insert(OS_TEST_FILE_CACHE_ADDRESS_SPACE_FIRST_INDEX,
                             OS_TEST_FILE_CACHE_ADDRESS_SPACE_FIRST_PHYSICAL_ADDRESS,
                             os::kernel::FileCachePageState::Clean) ==
            os::kernel::FileCacheAddressSpaceStatus::Succeeded &&
        address_space.Insert(OS_TEST_FILE_CACHE_ADDRESS_SPACE_MAXIMUM_INDEX,
                             OS_TEST_FILE_CACHE_ADDRESS_SPACE_SECOND_PHYSICAL_ADDRESS,
                             os::kernel::FileCachePageState::Dirty) ==
            os::kernel::FileCacheAddressSpaceStatus::Succeeded &&
        address_space.Retain(OS_TEST_FILE_CACHE_ADDRESS_SPACE_FIRST_INDEX,
                             OS_TEST_FILE_CACHE_ADDRESS_SPACE_FIRST_PHYSICAL_ADDRESS) ==
            os::kernel::FileCacheAddressSpaceStatus::Succeeded &&
        address_space.Remove(OS_TEST_FILE_CACHE_ADDRESS_SPACE_FIRST_INDEX,
                             OS_TEST_FILE_CACHE_ADDRESS_SPACE_FIRST_PHYSICAL_ADDRESS) ==
            os::kernel::FileCacheAddressSpaceStatus::PageBusy &&
        address_space.Release(OS_TEST_FILE_CACHE_ADDRESS_SPACE_FIRST_INDEX,
                              OS_TEST_FILE_CACHE_ADDRESS_SPACE_FIRST_PHYSICAL_ADDRESS) ==
            os::kernel::FileCacheAddressSpaceStatus::Succeeded &&
        address_space.Transition(OS_TEST_FILE_CACHE_ADDRESS_SPACE_FIRST_INDEX,
                                 OS_TEST_FILE_CACHE_ADDRESS_SPACE_FIRST_PHYSICAL_ADDRESS,
                                 os::kernel::FileCachePageState::Clean,
                                 os::kernel::FileCachePageState::Error) ==
            os::kernel::FileCacheAddressSpaceStatus::InvalidState &&
        address_space.Transition(OS_TEST_FILE_CACHE_ADDRESS_SPACE_FIRST_INDEX,
                                 OS_TEST_FILE_CACHE_ADDRESS_SPACE_FIRST_PHYSICAL_ADDRESS,
                                 os::kernel::FileCachePageState::Clean,
                                 os::kernel::FileCachePageState::Dirty) ==
            os::kernel::FileCacheAddressSpaceStatus::Succeeded &&
        address_space.Transition(OS_TEST_FILE_CACHE_ADDRESS_SPACE_FIRST_INDEX,
                                 OS_TEST_FILE_CACHE_ADDRESS_SPACE_FIRST_PHYSICAL_ADDRESS,
                                 os::kernel::FileCachePageState::Dirty,
                                 os::kernel::FileCachePageState::Writeback) ==
            os::kernel::FileCacheAddressSpaceStatus::Succeeded &&
        address_space.Transition(OS_TEST_FILE_CACHE_ADDRESS_SPACE_FIRST_INDEX,
                                 OS_TEST_FILE_CACHE_ADDRESS_SPACE_FIRST_PHYSICAL_ADDRESS,
                                 os::kernel::FileCachePageState::Writeback,
                                 os::kernel::FileCachePageState::Error) ==
            os::kernel::FileCacheAddressSpaceStatus::Succeeded &&
        address_space.Transition(OS_TEST_FILE_CACHE_ADDRESS_SPACE_FIRST_INDEX,
                                 OS_TEST_FILE_CACHE_ADDRESS_SPACE_FIRST_PHYSICAL_ADDRESS,
                                 os::kernel::FileCachePageState::Error,
                                 os::kernel::FileCachePageState::Writeback) ==
            os::kernel::FileCacheAddressSpaceStatus::Succeeded &&
        address_space.Transition(OS_TEST_FILE_CACHE_ADDRESS_SPACE_FIRST_INDEX,
                                 OS_TEST_FILE_CACHE_ADDRESS_SPACE_FIRST_PHYSICAL_ADDRESS,
                                 os::kernel::FileCachePageState::Writeback,
                                 os::kernel::FileCachePageState::Clean) ==
            os::kernel::FileCacheAddressSpaceStatus::Succeeded;
    os::kernel::FileCachePageSnapshot dirty_page{};
    state_contract_valid =
        state_contract_valid &&
        address_space.FindNext(OS_TEST_FILE_CACHE_ADDRESS_SPACE_FIRST_INDEX,
                               OS_TEST_FILE_CACHE_ADDRESS_SPACE_MAXIMUM_INDEX,
                               os::kernel::FileCachePageState::Dirty,
                               dirty_page) == os::kernel::FileCacheAddressSpaceStatus::Succeeded &&
        dirty_page.page_index == OS_TEST_FILE_CACHE_ADDRESS_SPACE_MAXIMUM_INDEX &&
        address_space.Remove(OS_TEST_FILE_CACHE_ADDRESS_SPACE_MAXIMUM_INDEX,
                             OS_TEST_FILE_CACHE_ADDRESS_SPACE_SECOND_PHYSICAL_ADDRESS) ==
            os::kernel::FileCacheAddressSpaceStatus::DirtyPagesRemain &&
        address_space.Transition(OS_TEST_FILE_CACHE_ADDRESS_SPACE_MAXIMUM_INDEX,
                                 OS_TEST_FILE_CACHE_ADDRESS_SPACE_SECOND_PHYSICAL_ADDRESS,
                                 os::kernel::FileCachePageState::Dirty,
                                 os::kernel::FileCachePageState::Writeback) ==
            os::kernel::FileCacheAddressSpaceStatus::Succeeded &&
        address_space.Transition(OS_TEST_FILE_CACHE_ADDRESS_SPACE_MAXIMUM_INDEX,
                                 OS_TEST_FILE_CACHE_ADDRESS_SPACE_SECOND_PHYSICAL_ADDRESS,
                                 os::kernel::FileCachePageState::Writeback,
                                 os::kernel::FileCachePageState::Clean) ==
            os::kernel::FileCacheAddressSpaceStatus::Succeeded &&
        address_space.Validate() == os::kernel::FileCacheAddressSpaceStatus::Succeeded;
    test_context.Expect(state_contract_valid, OS_TEST_FILE_CACHE_ADDRESS_SPACE_STATES);

    const bool address_space_destroyed =
        address_space.Remove(OS_TEST_FILE_CACHE_ADDRESS_SPACE_FIRST_INDEX,
                             OS_TEST_FILE_CACHE_ADDRESS_SPACE_FIRST_PHYSICAL_ADDRESS) ==
            os::kernel::FileCacheAddressSpaceStatus::Succeeded &&
        address_space.Remove(OS_TEST_FILE_CACHE_ADDRESS_SPACE_MAXIMUM_INDEX,
                             OS_TEST_FILE_CACHE_ADDRESS_SPACE_SECOND_PHYSICAL_ADDRESS) ==
            os::kernel::FileCacheAddressSpaceStatus::Succeeded &&
        address_space.Validate() == os::kernel::FileCacheAddressSpaceStatus::Succeeded &&
        address_space.Statistics().resident_page_count == 0ULL &&
        address_space.Destroy() == os::kernel::FileCacheAddressSpaceStatus::Succeeded &&
        address_space_heap.Validate() == os::kernel::KernelHeapStatus::Succeeded &&
        address_space_heap.Statistics().allocation_count == 0ULL;

    os::kernel::KernelHeap tiny_address_space_heap{};
    os::kernel::FileCacheAddressSpace failing_address_space{};
    const bool address_space_rollback =
        tiny_address_space_heap.Initialize(AddressOf(tiny_address_space_heap_storage),
                                           sizeof(tiny_address_space_heap_storage)) ==
            os::kernel::KernelHeapStatus::Succeeded &&
        failing_address_space.Initialize(MakeIdentity(), tiny_address_space_heap) ==
            os::kernel::FileCacheAddressSpaceStatus::Succeeded &&
        failing_address_space.Insert(OS_TEST_FILE_CACHE_ADDRESS_SPACE_MAXIMUM_INDEX,
                                     OS_TEST_FILE_CACHE_ADDRESS_SPACE_FIRST_PHYSICAL_ADDRESS,
                                     os::kernel::FileCachePageState::Clean) ==
            os::kernel::FileCacheAddressSpaceStatus::AllocationFailed &&
        failing_address_space.Validate() == os::kernel::FileCacheAddressSpaceStatus::Succeeded &&
        failing_address_space.Statistics().resident_page_count == 0ULL &&
        failing_address_space.Statistics().failed_insertion_count == 1ULL &&
        tiny_address_space_heap.Statistics().allocation_count == 0ULL &&
        failing_address_space.Destroy() == os::kernel::FileCacheAddressSpaceStatus::Succeeded;
    test_context.Expect(address_space_destroyed && address_space_rollback,
                        OS_TEST_FILE_CACHE_ADDRESS_SPACE_DESTROY);

    return test_context.ExitCode();
}

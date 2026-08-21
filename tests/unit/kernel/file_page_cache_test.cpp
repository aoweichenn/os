#include <os/kernel/memory/file_page_cache.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_FILE_PAGE_CACHE_SUITE_NAME = "kernel/file_page_cache/unit";
constexpr std::string_view OS_TEST_FILE_PAGE_CACHE_INITIALIZE =
    "缓存必须以固定容量和外部页帧访问器初始化";
constexpr std::string_view OS_TEST_FILE_PAGE_CACHE_HIT =
    "相同文件页必须共享唯一权威帧并累计映射引用";
constexpr std::string_view OS_TEST_FILE_PAGE_CACHE_LRU =
    "容量耗尽时必须回收最久未使用且没有映射引用的 clean 页";
constexpr std::string_view OS_TEST_FILE_PAGE_CACHE_BUSY = "全部槽位仍被映射时必须明确返回容量耗尽";
constexpr std::string_view OS_TEST_FILE_PAGE_CACHE_INVALIDATION =
    "失效必须先拒绝活动映射并在引用释放后回收目标文件全部页";
constexpr std::string_view OS_TEST_FILE_PAGE_CACHE_FAILURE = "来源读取失败不得留下缓存项或页帧泄漏";
constexpr std::string_view OS_TEST_FILE_PAGE_CACHE_WRITEBACK =
    "脏页必须受上限约束并经过 writeback 才能恢复为 clean";
constexpr std::string_view OS_TEST_FILE_PAGE_CACHE_WRITEBACK_FAILURE =
    "写回失败必须保留 Error 页且重试成功前不得淘汰";
constexpr std::string_view OS_TEST_FILE_PAGE_CACHE_DESTROY = "销毁后页帧统计必须恢复初始化基线";
constexpr std::string_view OS_TEST_FILE_PAGE_CACHE_METADATA_FAILURE =
    "动态地址空间元数据耗尽必须回滚 frame、record、page 和 radix 节点";
constexpr std::string_view OS_TEST_FILE_PAGE_CACHE_TRIM_COUNT =
    "裁剪必须报告真实归还给页帧分配器的 clean 页数";
constexpr std::string_view OS_TEST_FILE_PAGE_CACHE_TRUNCATE =
    "truncate 必须拒绝活动范围外映射、丢弃脏页并清零保留尾页";

constexpr uint64_t OS_TEST_FILE_PAGE_CACHE_MANAGED_PAGE_COUNT = 8ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_CACHE_STATE_VALUES_PER_BYTE = 4ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_CACHE_STATE_STORAGE_SIZE_BYTES =
    (OS_TEST_FILE_PAGE_CACHE_MANAGED_PAGE_COUNT + OS_TEST_FILE_PAGE_CACHE_STATE_VALUES_PER_BYTE -
     1ULL) /
    OS_TEST_FILE_PAGE_CACHE_STATE_VALUES_PER_BYTE;
constexpr uint64_t OS_TEST_FILE_PAGE_CACHE_MANAGED_SIZE_BYTES =
    OS_TEST_FILE_PAGE_CACHE_MANAGED_PAGE_COUNT * os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_TEST_FILE_PAGE_CACHE_MEMORY_MAP_ENTRY_COUNT = 1ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_CACHE_CAPACITY = 2ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_CACHE_DIRTY_PAGE_LIMIT = 1ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_CACHE_METADATA_HEAP_SIZE_BYTES = 64ULL * 1024ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_CACHE_METADATA_HEAP_ALIGNMENT_BYTES = 64ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_CACHE_TINY_METADATA_HEAP_SIZE_BYTES = 2048ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_CACHE_FIRST_PAGE_INDEX = 0ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_CACHE_SECOND_PAGE_INDEX = 1ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_CACHE_THIRD_PAGE_INDEX = 2ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_CACHE_FAILURE_PAGE_INDEX = 3ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_CACHE_SINGLE_REFERENCE = 1ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_CACHE_TWO_REFERENCES = 2ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_CACHE_SUPERBLOCK_IDENTIFIER = 11ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_CACHE_SUPERBLOCK_GENERATION = 7ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_CACHE_NODE_IDENTIFIER = 29ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_CACHE_NODE_GENERATION = 5ULL;
constexpr uint8_t OS_TEST_FILE_PAGE_CACHE_FIRST_PATTERN = 0x39U;
constexpr uint8_t OS_TEST_FILE_PAGE_CACHE_LAST_PATTERN = 0xC7U;

struct TestMemory final {
    uint8_t bytes[OS_TEST_FILE_PAGE_CACHE_MANAGED_SIZE_BYTES];
};

struct TestReader final {
    uint64_t failure_page_index;
    uint64_t read_count;
};

struct TestWriter final {
    uint64_t remaining_failure_count;
    uint64_t write_count;
};

[[nodiscard]] uint8_t *AccessPage(void *const context, const uint64_t physical_address) noexcept {
    if (context == nullptr || physical_address > OS_TEST_FILE_PAGE_CACHE_MANAGED_SIZE_BYTES -
                                                     os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) {
        return nullptr;
    }
    TestMemory &memory = *static_cast<TestMemory *>(context);
    return memory.bytes + physical_address;
}

[[nodiscard]] bool ReadPage(void *const context, const os::kernel::FilePageIdentity &identity,
                            uint8_t *const destination, const uint64_t capacity_bytes) noexcept {
    if (context == nullptr || destination == nullptr ||
        capacity_bytes != os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) {
        return false;
    }
    TestReader &reader = *static_cast<TestReader *>(context);
    if (identity.page_index == reader.failure_page_index) {
        return false;
    }
    ++reader.read_count;
    destination[OS_TEST_FILE_PAGE_CACHE_FIRST_PAGE_INDEX] =
        static_cast<uint8_t>(OS_TEST_FILE_PAGE_CACHE_FIRST_PATTERN + identity.page_index);
    destination[capacity_bytes - OS_TEST_FILE_PAGE_CACHE_SINGLE_REFERENCE] =
        static_cast<uint8_t>(OS_TEST_FILE_PAGE_CACHE_LAST_PATTERN - identity.page_index);
    return true;
}

[[nodiscard]] bool WritePage(void *const context, const os::kernel::FilePageIdentity &identity,
                             const uint8_t *const source, const uint64_t length_bytes) noexcept {
    if (context == nullptr || source == nullptr ||
        length_bytes != os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES ||
        identity.file.node_identifier != OS_TEST_FILE_PAGE_CACHE_NODE_IDENTIFIER) {
        return false;
    }
    TestWriter &writer = *static_cast<TestWriter *>(context);
    if (writer.remaining_failure_count != 0ULL) {
        --writer.remaining_failure_count;
        return false;
    }
    ++writer.write_count;
    return true;
}

[[nodiscard]] os::kernel::FilePageIdentity MakePageIdentity(const uint64_t page_index) noexcept {
    return os::kernel::FilePageIdentity{
        .file =
            {
                .superblock_identifier = OS_TEST_FILE_PAGE_CACHE_SUPERBLOCK_IDENTIFIER,
                .superblock_generation = OS_TEST_FILE_PAGE_CACHE_SUPERBLOCK_GENERATION,
                .node_identifier = OS_TEST_FILE_PAGE_CACHE_NODE_IDENTIFIER,
                .node_generation = OS_TEST_FILE_PAGE_CACHE_NODE_GENERATION,
            },
        .page_index = page_index,
    };
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_FILE_PAGE_CACHE_SUITE_NAME};
    uint8_t state_storage[OS_TEST_FILE_PAGE_CACHE_STATE_STORAGE_SIZE_BYTES]{};
    TestMemory memory{};
    os::kernel::PhysicalFrameAllocator frame_allocator{
        state_storage,
        OS_TEST_FILE_PAGE_CACHE_STATE_STORAGE_SIZE_BYTES,
    };
    const os::kernel::PhysicalMemoryMapEntry memory_map[] = {
        {
            .base_address = 0ULL,
            .length_bytes = OS_TEST_FILE_PAGE_CACHE_MANAGED_SIZE_BYTES,
            .type = os::kernel::OS_KERNEL_MEMORY_MAP_USABLE_REGION_TYPE,
            .attributes = 0U,
        },
    };
    const bool allocator_initialized =
        frame_allocator.Initialize(memory_map, OS_TEST_FILE_PAGE_CACHE_MEMORY_MAP_ENTRY_COUNT,
                                   OS_TEST_FILE_PAGE_CACHE_MANAGED_SIZE_BYTES) ==
        os::kernel::PhysicalFrameAllocatorStatus::Succeeded;
    const os::kernel::PhysicalFrameAllocatorStatistics frames_before = frame_allocator.Statistics();
    alignas(OS_TEST_FILE_PAGE_CACHE_METADATA_HEAP_ALIGNMENT_BYTES)
        uint8_t metadata_heap_storage[OS_TEST_FILE_PAGE_CACHE_METADATA_HEAP_SIZE_BYTES]{};
    os::kernel::KernelHeap metadata_heap{};
    os::kernel::FilePageCache cache{};
    const bool cache_initialized =
        allocator_initialized &&
        metadata_heap.Initialize(reinterpret_cast<uint64_t>(metadata_heap_storage),
                                 sizeof(metadata_heap_storage)) ==
            os::kernel::KernelHeapStatus::Succeeded &&
        cache.Initialize(metadata_heap, OS_TEST_FILE_PAGE_CACHE_CAPACITY,
                         OS_TEST_FILE_PAGE_CACHE_DIRTY_PAGE_LIMIT, frame_allocator, &memory,
                         AccessPage) == os::kernel::FilePageCacheStatus::Succeeded;
    test_context.Expect(cache_initialized, OS_TEST_FILE_PAGE_CACHE_INITIALIZE);

    TestReader reader{
        .failure_page_index = OS_TEST_FILE_PAGE_CACHE_FAILURE_PAGE_INDEX,
        .read_count = 0ULL,
    };
    const os::kernel::FilePageIdentity first_identity =
        MakePageIdentity(OS_TEST_FILE_PAGE_CACHE_FIRST_PAGE_INDEX);
    uint64_t first_physical_address = 0ULL;
    bool first_cache_hit = true;
    const bool first_loaded =
        cache.Acquire(first_identity, &reader, ReadPage, first_physical_address, first_cache_hit) ==
            os::kernel::FilePageCacheStatus::Succeeded &&
        !first_cache_hit;
    uint64_t shared_physical_address = 0ULL;
    bool shared_cache_hit = false;
    const bool shared =
        first_loaded &&
        cache.Acquire(first_identity, &reader, ReadPage, shared_physical_address,
                      shared_cache_hit) == os::kernel::FilePageCacheStatus::Succeeded &&
        shared_cache_hit && shared_physical_address == first_physical_address &&
        reader.read_count == OS_TEST_FILE_PAGE_CACHE_SINGLE_REFERENCE &&
        cache.Statistics().active_mapping_reference_count ==
            OS_TEST_FILE_PAGE_CACHE_TWO_REFERENCES &&
        memory.bytes[first_physical_address] == OS_TEST_FILE_PAGE_CACHE_FIRST_PATTERN &&
        memory.bytes[first_physical_address + os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES -
                     OS_TEST_FILE_PAGE_CACHE_SINGLE_REFERENCE] ==
            OS_TEST_FILE_PAGE_CACHE_LAST_PATTERN &&
        cache.Release(first_identity, shared_physical_address) ==
            os::kernel::FilePageCacheStatus::Succeeded &&
        cache.Release(first_identity, first_physical_address) ==
            os::kernel::FilePageCacheStatus::Succeeded;
    test_context.Expect(shared, OS_TEST_FILE_PAGE_CACHE_HIT);

    const os::kernel::FilePageIdentity second_identity =
        MakePageIdentity(OS_TEST_FILE_PAGE_CACHE_SECOND_PAGE_INDEX);
    uint64_t second_physical_address = 0ULL;
    bool second_cache_hit = false;
    const bool second_loaded =
        cache.Acquire(second_identity, &reader, ReadPage, second_physical_address,
                      second_cache_hit) == os::kernel::FilePageCacheStatus::Succeeded &&
        cache.Release(second_identity, second_physical_address) ==
            os::kernel::FilePageCacheStatus::Succeeded;
    bool first_reuse_hit = false;
    const bool first_touched =
        second_loaded &&
        cache.Acquire(first_identity, &reader, ReadPage, first_physical_address, first_reuse_hit) ==
            os::kernel::FilePageCacheStatus::Succeeded &&
        first_reuse_hit &&
        cache.Release(first_identity, first_physical_address) ==
            os::kernel::FilePageCacheStatus::Succeeded;
    const os::kernel::FilePageIdentity third_identity =
        MakePageIdentity(OS_TEST_FILE_PAGE_CACHE_THIRD_PAGE_INDEX);
    uint64_t third_physical_address = 0ULL;
    bool third_cache_hit = false;
    const bool least_recently_used_evicted =
        first_touched &&
        cache.Acquire(third_identity, &reader, ReadPage, third_physical_address, third_cache_hit) ==
            os::kernel::FilePageCacheStatus::Succeeded &&
        !third_cache_hit &&
        cache.Release(third_identity, third_physical_address) ==
            os::kernel::FilePageCacheStatus::Succeeded &&
        cache.Statistics().eviction_count == OS_TEST_FILE_PAGE_CACHE_SINGLE_REFERENCE;
    test_context.Expect(least_recently_used_evicted, OS_TEST_FILE_PAGE_CACHE_LRU);

    bool first_busy_hit = false;
    bool third_busy_hit = false;
    uint64_t busy_first_physical_address = 0ULL;
    uint64_t busy_third_physical_address = 0ULL;
    const bool all_entries_referenced =
        cache.Acquire(first_identity, &reader, ReadPage, busy_first_physical_address,
                      first_busy_hit) == os::kernel::FilePageCacheStatus::Succeeded &&
        cache.Acquire(third_identity, &reader, ReadPage, busy_third_physical_address,
                      third_busy_hit) == os::kernel::FilePageCacheStatus::Succeeded;
    uint64_t unavailable_physical_address = 0ULL;
    bool unavailable_cache_hit = false;
    const bool capacity_rejected =
        all_entries_referenced &&
        cache.Acquire(second_identity, &reader, ReadPage, unavailable_physical_address,
                      unavailable_cache_hit) == os::kernel::FilePageCacheStatus::CapacityExhausted;
    test_context.Expect(capacity_rejected, OS_TEST_FILE_PAGE_CACHE_BUSY);

    const os::kernel::FileIdentity file_identity = first_identity.file;
    const bool invalidation_guarded =
        cache.Invalidate(file_identity) == os::kernel::FilePageCacheStatus::EntryBusy &&
        cache.Release(first_identity, busy_first_physical_address) ==
            os::kernel::FilePageCacheStatus::Succeeded &&
        cache.Release(third_identity, busy_third_physical_address) ==
            os::kernel::FilePageCacheStatus::Succeeded &&
        cache.Invalidate(file_identity) == os::kernel::FilePageCacheStatus::Succeeded &&
        cache.Statistics().resident_page_count == 0ULL &&
        cache.Validate() == os::kernel::FilePageCacheStatus::Succeeded;
    test_context.Expect(invalidation_guarded, OS_TEST_FILE_PAGE_CACHE_INVALIDATION);

    const os::kernel::FilePageIdentity failure_identity =
        MakePageIdentity(OS_TEST_FILE_PAGE_CACHE_FAILURE_PAGE_INDEX);
    uint64_t failure_physical_address = 0ULL;
    bool failure_cache_hit = false;
    const os::kernel::PhysicalFrameAllocatorStatistics frames_before_failure =
        frame_allocator.Statistics();
    const bool failure_atomic =
        cache.Acquire(failure_identity, &reader, ReadPage, failure_physical_address,
                      failure_cache_hit) == os::kernel::FilePageCacheStatus::SourceReadFailed &&
        cache.Statistics().resident_page_count == 0ULL &&
        frame_allocator.Statistics().allocated_frame_count ==
            frames_before_failure.allocated_frame_count;
    test_context.Expect(failure_atomic, OS_TEST_FILE_PAGE_CACHE_FAILURE);

    bool writeback_first_cache_hit = false;
    bool writeback_second_cache_hit = false;
    const bool writeback_pages_loaded =
        cache.Acquire(first_identity, &reader, ReadPage, first_physical_address,
                      writeback_first_cache_hit) == os::kernel::FilePageCacheStatus::Succeeded &&
        cache.Release(first_identity, first_physical_address) ==
            os::kernel::FilePageCacheStatus::Succeeded &&
        cache.Acquire(second_identity, &reader, ReadPage, second_physical_address,
                      writeback_second_cache_hit) == os::kernel::FilePageCacheStatus::Succeeded &&
        cache.Release(second_identity, second_physical_address) ==
            os::kernel::FilePageCacheStatus::Succeeded;
    TestWriter writer{
        .remaining_failure_count = 1ULL,
        .write_count = 0ULL,
    };
    uint64_t written_page_count = 0ULL;
    const bool writeback_failure_preserved =
        writeback_pages_loaded &&
        cache.MarkDirty(first_identity, first_physical_address) ==
            os::kernel::FilePageCacheStatus::Succeeded &&
        cache.MarkDirty(second_identity, second_physical_address) ==
            os::kernel::FilePageCacheStatus::DirtyLimitReached &&
        cache.Invalidate(file_identity) == os::kernel::FilePageCacheStatus::DirtyPagesRemain &&
        cache.Writeback(&writer, WritePage, OS_TEST_FILE_PAGE_CACHE_CAPACITY, written_page_count) ==
            os::kernel::FilePageCacheStatus::SourceWriteFailed &&
        written_page_count == 0ULL && cache.Statistics().error_page_count == 1ULL &&
        cache.Invalidate(file_identity) == os::kernel::FilePageCacheStatus::DirtyPagesRemain;
    test_context.Expect(writeback_failure_preserved, OS_TEST_FILE_PAGE_CACHE_WRITEBACK_FAILURE);

    const bool writeback_completed =
        cache.Writeback(&writer, WritePage, OS_TEST_FILE_PAGE_CACHE_CAPACITY, written_page_count) ==
            os::kernel::FilePageCacheStatus::Succeeded &&
        written_page_count == 1ULL &&
        cache.MarkDirty(second_identity, second_physical_address) ==
            os::kernel::FilePageCacheStatus::Succeeded &&
        cache.Writeback(&writer, WritePage, OS_TEST_FILE_PAGE_CACHE_CAPACITY, written_page_count) ==
            os::kernel::FilePageCacheStatus::Succeeded &&
        written_page_count == 1ULL && writer.write_count == 2ULL &&
        cache.Statistics().dirty_page_count == 0ULL &&
        cache.Statistics().error_page_count == 0ULL &&
        cache.Statistics().successful_writeback_count == 2ULL &&
        cache.Statistics().failed_writeback_count == 1ULL &&
        cache.Validate() == os::kernel::FilePageCacheStatus::Succeeded;
    test_context.Expect(writeback_completed, OS_TEST_FILE_PAGE_CACHE_WRITEBACK);

    constexpr uint64_t OS_TEST_FILE_PAGE_CACHE_TRUNCATED_SIZE_BYTES = 100ULL;
    memory.bytes[first_physical_address + OS_TEST_FILE_PAGE_CACHE_TRUNCATED_SIZE_BYTES] =
        OS_TEST_FILE_PAGE_CACHE_LAST_PATTERN;
    bool truncate_cache_hit = false;
    uint64_t truncate_physical_address = 0ULL;
    uint64_t resolved_size_bytes = 0ULL;
    os::kernel::FilePageCacheEntry truncated_entry{};
    const bool truncate_consistent =
        cache.ObserveFileSize(file_identity,
                              OS_TEST_FILE_PAGE_CACHE_CAPACITY *
                                  os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) ==
            os::kernel::FilePageCacheStatus::Succeeded &&
        cache.Acquire(second_identity, &reader, ReadPage, truncate_physical_address,
                      truncate_cache_hit) == os::kernel::FilePageCacheStatus::Succeeded &&
        truncate_cache_hit &&
        cache.MarkDirty(second_identity, truncate_physical_address) ==
            os::kernel::FilePageCacheStatus::Succeeded &&
        cache.Truncate(file_identity, OS_TEST_FILE_PAGE_CACHE_TRUNCATED_SIZE_BYTES) ==
            os::kernel::FilePageCacheStatus::EntryBusy &&
        cache.Release(second_identity, truncate_physical_address) ==
            os::kernel::FilePageCacheStatus::Succeeded &&
        cache.Truncate(file_identity, OS_TEST_FILE_PAGE_CACHE_TRUNCATED_SIZE_BYTES) ==
            os::kernel::FilePageCacheStatus::Succeeded &&
        cache.ResolveFileSize(file_identity, UINT64_MAX, resolved_size_bytes) ==
            os::kernel::FilePageCacheStatus::Succeeded &&
        resolved_size_bytes == OS_TEST_FILE_PAGE_CACHE_TRUNCATED_SIZE_BYTES &&
        cache.ReadEntry(second_identity, truncated_entry) ==
            os::kernel::FilePageCacheStatus::MappingNotFound &&
        memory.bytes[first_physical_address + OS_TEST_FILE_PAGE_CACHE_TRUNCATED_SIZE_BYTES] == 0U &&
        cache.Statistics().resident_page_count == OS_TEST_FILE_PAGE_CACHE_SINGLE_REFERENCE &&
        cache.Statistics().dirty_page_count == 0ULL &&
        cache.Statistics().truncated_page_count == OS_TEST_FILE_PAGE_CACHE_SINGLE_REFERENCE &&
        cache.Statistics().truncated_tail_zero_count ==
            OS_TEST_FILE_PAGE_CACHE_SINGLE_REFERENCE &&
        cache.Validate() == os::kernel::FilePageCacheStatus::Succeeded;
    test_context.Expect(truncate_consistent, OS_TEST_FILE_PAGE_CACHE_TRUNCATE);

    uint64_t reclaimed_page_count = 0ULL;
    const bool trim_count_valid =
        cache.Trim(0ULL, reclaimed_page_count) == os::kernel::FilePageCacheStatus::Succeeded &&
        reclaimed_page_count == OS_TEST_FILE_PAGE_CACHE_SINGLE_REFERENCE &&
        cache.Statistics().resident_page_count == 0ULL;
    test_context.Expect(trim_count_valid, OS_TEST_FILE_PAGE_CACHE_TRIM_COUNT);

    const bool destroyed =
        cache.Destroy() == os::kernel::FilePageCacheStatus::Succeeded &&
        metadata_heap.Validate() == os::kernel::KernelHeapStatus::Succeeded &&
        metadata_heap.Statistics().allocation_count == 0ULL &&
        frame_allocator.Statistics().free_frame_count == frames_before.free_frame_count &&
        frame_allocator.Statistics().allocated_frame_count == frames_before.allocated_frame_count;
    test_context.Expect(destroyed, OS_TEST_FILE_PAGE_CACHE_DESTROY);

    alignas(OS_TEST_FILE_PAGE_CACHE_METADATA_HEAP_ALIGNMENT_BYTES)
        uint8_t tiny_metadata_heap_storage[OS_TEST_FILE_PAGE_CACHE_TINY_METADATA_HEAP_SIZE_BYTES]{};
    os::kernel::KernelHeap tiny_metadata_heap{};
    os::kernel::FilePageCache failing_cache{};
    TestReader successful_reader{
        .failure_page_index = OS_TEST_FILE_PAGE_CACHE_FAILURE_PAGE_INDEX,
        .read_count = 0ULL,
    };
    uint64_t unavailable_metadata_physical_address = 0ULL;
    bool unavailable_metadata_cache_hit = false;
    const os::kernel::PhysicalFrameAllocatorStatistics frames_before_metadata_failure =
        frame_allocator.Statistics();
    const bool metadata_failure_atomic =
        tiny_metadata_heap.Initialize(reinterpret_cast<uint64_t>(tiny_metadata_heap_storage),
                                      sizeof(tiny_metadata_heap_storage)) ==
            os::kernel::KernelHeapStatus::Succeeded &&
        failing_cache.Initialize(tiny_metadata_heap, 64ULL, 32ULL, frame_allocator, &memory,
                                 AccessPage) == os::kernel::FilePageCacheStatus::Succeeded &&
        failing_cache.Acquire(MakePageIdentity(UINT64_MAX), &successful_reader, ReadPage,
                              unavailable_metadata_physical_address,
                              unavailable_metadata_cache_hit) ==
            os::kernel::FilePageCacheStatus::MetadataAllocationFailed &&
        !unavailable_metadata_cache_hit &&
        failing_cache.Statistics().resident_page_count == 0ULL &&
        failing_cache.Statistics().address_space_count == 0ULL &&
        failing_cache.Statistics().metadata_allocation_failure_count == 1ULL &&
        failing_cache.Validate() == os::kernel::FilePageCacheStatus::Succeeded &&
        failing_cache.Destroy() == os::kernel::FilePageCacheStatus::Succeeded &&
        tiny_metadata_heap.Validate() == os::kernel::KernelHeapStatus::Succeeded &&
        tiny_metadata_heap.Statistics().allocation_count == 0ULL &&
        frame_allocator.Statistics().allocated_frame_count ==
            frames_before_metadata_failure.allocated_frame_count &&
        frame_allocator.Statistics().free_frame_count ==
            frames_before_metadata_failure.free_frame_count;
    test_context.Expect(metadata_failure_atomic, OS_TEST_FILE_PAGE_CACHE_METADATA_FAILURE);
    return test_context.ExitCode();
}

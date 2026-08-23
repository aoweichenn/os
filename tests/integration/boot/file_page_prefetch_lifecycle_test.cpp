#include <os/kernel/memory/file_page_cache.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_FILE_PAGE_PREFETCH_SUITE_NAME =
    "kernel/file_page_prefetch/lifecycle/integration";
constexpr std::string_view OS_TEST_FILE_PAGE_PREFETCH_USEFUL =
    "成功预取必须留下 one-shot 标记且首次 demand hit 精确消费一次";
constexpr std::string_view OS_TEST_FILE_PAGE_PREFETCH_EXISTING =
    "对既有 demand 页的预取不得伪造 prefetched 标记或 useful hit";
constexpr std::string_view OS_TEST_FILE_PAGE_PREFETCH_WASTE =
    "失效必须把未消费预取计为 waste，来源失败不得留下缓存页或标记";
constexpr std::string_view OS_TEST_FILE_PAGE_PREFETCH_LIFECYCLE =
    "预取、消费、失效和失败后缓存、堆与页帧必须回到一致状态";

constexpr uint64_t OS_TEST_FILE_PAGE_PREFETCH_MANAGED_PAGE_COUNT = 8ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_PREFETCH_STATE_VALUES_PER_BYTE = 4ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_PREFETCH_STATE_STORAGE_SIZE_BYTES =
    (OS_TEST_FILE_PAGE_PREFETCH_MANAGED_PAGE_COUNT +
     OS_TEST_FILE_PAGE_PREFETCH_STATE_VALUES_PER_BYTE - 1ULL) /
    OS_TEST_FILE_PAGE_PREFETCH_STATE_VALUES_PER_BYTE;
constexpr uint64_t OS_TEST_FILE_PAGE_PREFETCH_MEMORY_SIZE_BYTES =
    OS_TEST_FILE_PAGE_PREFETCH_MANAGED_PAGE_COUNT * os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_TEST_FILE_PAGE_PREFETCH_METADATA_SIZE_BYTES = 64ULL * 1024ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_PREFETCH_METADATA_ALIGNMENT_BYTES = 64ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_PREFETCH_CACHE_CAPACITY = 3ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_PREFETCH_DIRTY_LIMIT = 1ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_PREFETCH_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_PREFETCH_FIRST_VALUE = 1ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_PREFETCH_FAILURE_PAGE_INDEX = 2ULL;
constexpr uint8_t OS_TEST_FILE_PAGE_PREFETCH_PATTERN = 0xA9U;

struct TestMemory final {
    uint8_t bytes[OS_TEST_FILE_PAGE_PREFETCH_MEMORY_SIZE_BYTES];
};

struct TestReader final {
    uint64_t read_count;
};

struct TestFeedback final {
    uint64_t useful_page_count;
    uint64_t wasted_page_count;
};

[[nodiscard]] bool RecordFeedback(void *const context, const os::kernel::FileReadaheadPageTag &tag,
                                  const os::kernel::FileReadaheadFeedback &feedback) noexcept {
    if (context == nullptr || !os::kernel::FileReadaheadPageTagIsValid(tag)) {
        return false;
    }
    TestFeedback &recorded = *static_cast<TestFeedback *>(context);
    recorded.useful_page_count += feedback.useful_page_count;
    recorded.wasted_page_count += feedback.wasted_page_count;
    return true;
}

[[nodiscard]] uint8_t *AccessPage(void *const context, const uint64_t physical_address) noexcept {
    if (context == nullptr || physical_address > OS_TEST_FILE_PAGE_PREFETCH_MEMORY_SIZE_BYTES -
                                                     os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) {
        return nullptr;
    }
    TestMemory &memory = *static_cast<TestMemory *>(context);
    return memory.bytes + physical_address;
}

[[nodiscard]] bool ReadPage(void *const context, const os::kernel::FilePageIdentity &identity,
                            uint8_t *const destination, const uint64_t capacity_bytes) noexcept {
    if (context == nullptr || destination == nullptr ||
        capacity_bytes != os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES ||
        identity.page_index == OS_TEST_FILE_PAGE_PREFETCH_FAILURE_PAGE_INDEX) {
        return false;
    }
    TestReader &reader = *static_cast<TestReader *>(context);
    ++reader.read_count;
    destination[OS_TEST_FILE_PAGE_PREFETCH_EMPTY_VALUE] =
        static_cast<uint8_t>(OS_TEST_FILE_PAGE_PREFETCH_PATTERN + identity.page_index);
    return true;
}

[[nodiscard]] os::kernel::FilePageIdentity MakeIdentity(const uint64_t page_index) noexcept {
    return os::kernel::FilePageIdentity{
        .file =
            os::kernel::FileIdentity{
                .superblock_identifier = 13ULL,
                .superblock_generation = 3ULL,
                .node_identifier = 37ULL,
                .node_generation = 9ULL,
            },
        .page_index = page_index,
    };
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_FILE_PAGE_PREFETCH_SUITE_NAME};
    uint8_t state_storage[OS_TEST_FILE_PAGE_PREFETCH_STATE_STORAGE_SIZE_BYTES]{};
    TestMemory memory{};
    os::kernel::PhysicalFrameAllocator frame_allocator{
        state_storage,
        OS_TEST_FILE_PAGE_PREFETCH_STATE_STORAGE_SIZE_BYTES,
    };
    const os::kernel::PhysicalMemoryMapEntry memory_map[] = {
        {
            .base_address = OS_TEST_FILE_PAGE_PREFETCH_EMPTY_VALUE,
            .length_bytes = OS_TEST_FILE_PAGE_PREFETCH_MEMORY_SIZE_BYTES,
            .type = os::kernel::OS_KERNEL_MEMORY_MAP_USABLE_REGION_TYPE,
            .attributes = 0U,
        },
    };
    const bool allocator_initialized =
        frame_allocator.Initialize(memory_map, OS_TEST_FILE_PAGE_PREFETCH_FIRST_VALUE,
                                   OS_TEST_FILE_PAGE_PREFETCH_MEMORY_SIZE_BYTES) ==
        os::kernel::PhysicalFrameAllocatorStatus::Succeeded;
    const os::kernel::PhysicalFrameAllocatorStatistics frames_before = frame_allocator.Statistics();
    alignas(OS_TEST_FILE_PAGE_PREFETCH_METADATA_ALIGNMENT_BYTES)
        uint8_t metadata_storage[OS_TEST_FILE_PAGE_PREFETCH_METADATA_SIZE_BYTES]{};
    os::kernel::KernelHeap metadata_heap{};
    os::kernel::FilePageCache cache{};
    TestFeedback feedback{};
    const bool initialized =
        allocator_initialized &&
        metadata_heap.Initialize(reinterpret_cast<uint64_t>(metadata_storage),
                                 sizeof(metadata_storage)) ==
            os::kernel::KernelHeapStatus::Succeeded &&
        cache.Initialize(metadata_heap, OS_TEST_FILE_PAGE_PREFETCH_CACHE_CAPACITY,
                         OS_TEST_FILE_PAGE_PREFETCH_DIRTY_LIMIT, frame_allocator, &memory,
                         AccessPage) == os::kernel::FilePageCacheStatus::Succeeded &&
        cache.ConfigureReadaheadFeedback(os::kernel::FilePageReadaheadFeedbackOperations{
            .context = &feedback,
            .record = RecordFeedback,
        }) == os::kernel::FilePageCacheStatus::Succeeded;

    const os::kernel::FileReadaheadPageTag readahead_tag{
        .stream =
            os::kernel::FileReadaheadStreamToken{
                .slot_index = OS_TEST_FILE_PAGE_PREFETCH_FIRST_VALUE,
                .generation = OS_TEST_FILE_PAGE_PREFETCH_FIRST_VALUE,
            },
        .policy_generation = OS_TEST_FILE_PAGE_PREFETCH_FIRST_VALUE,
    };

    TestReader reader{};
    const os::kernel::FilePageIdentity first_identity = MakeIdentity(0ULL);
    uint64_t physical_address = OS_TEST_FILE_PAGE_PREFETCH_EMPTY_VALUE;
    bool cache_hit = true;
    bool prefetched_hit = true;
    const bool prefetched =
        initialized &&
        cache.Acquire(first_identity, &reader, ReadPage,
                      os::kernel::FilePageAcquireIntent::Prefetch, readahead_tag, physical_address,
                      cache_hit, prefetched_hit) == os::kernel::FilePageCacheStatus::Succeeded &&
        !cache_hit && !prefetched_hit &&
        cache.Release(first_identity, physical_address) ==
            os::kernel::FilePageCacheStatus::Succeeded;
    os::kernel::FilePageCacheEntry entry{};
    const os::kernel::FilePageCacheStatistics after_prefetch = cache.Statistics();
    test_context.Expect(
        prefetched &&
            cache.ReadEntry(first_identity, entry) == os::kernel::FilePageCacheStatus::Succeeded &&
            entry.prefetched &&
            after_prefetch.successful_prefetch_load_count ==
                OS_TEST_FILE_PAGE_PREFETCH_FIRST_VALUE &&
            after_prefetch.prefetched_page_count == OS_TEST_FILE_PAGE_PREFETCH_FIRST_VALUE,
        OS_TEST_FILE_PAGE_PREFETCH_USEFUL);

    cache_hit = false;
    prefetched_hit = false;
    const bool useful_hit =
        cache.Acquire(first_identity, &reader, ReadPage, os::kernel::FilePageAcquireIntent::Demand,
                      os::kernel::FileReadaheadPageTag{}, physical_address, cache_hit,
                      prefetched_hit) == os::kernel::FilePageCacheStatus::Succeeded &&
        cache_hit && prefetched_hit &&
        cache.Release(first_identity, physical_address) ==
            os::kernel::FilePageCacheStatus::Succeeded &&
        cache.ReadEntry(first_identity, entry) == os::kernel::FilePageCacheStatus::Succeeded &&
        !entry.prefetched &&
        cache.Statistics().prefetched_hit_count == OS_TEST_FILE_PAGE_PREFETCH_FIRST_VALUE &&
        cache.Statistics().prefetched_page_count == OS_TEST_FILE_PAGE_PREFETCH_EMPTY_VALUE;
    test_context.Expect(useful_hit, OS_TEST_FILE_PAGE_PREFETCH_USEFUL);

    cache_hit = false;
    prefetched_hit = true;
    const bool existing_not_retagged =
        cache.Acquire(first_identity, &reader, ReadPage,
                      os::kernel::FilePageAcquireIntent::Prefetch, readahead_tag, physical_address,
                      cache_hit, prefetched_hit) == os::kernel::FilePageCacheStatus::Succeeded &&
        cache_hit && !prefetched_hit &&
        cache.Release(first_identity, physical_address) ==
            os::kernel::FilePageCacheStatus::Succeeded &&
        cache.ReadEntry(first_identity, entry) == os::kernel::FilePageCacheStatus::Succeeded &&
        !entry.prefetched &&
        cache.Statistics().prefetch_existing_page_count == OS_TEST_FILE_PAGE_PREFETCH_FIRST_VALUE;
    test_context.Expect(existing_not_retagged, OS_TEST_FILE_PAGE_PREFETCH_EXISTING);

    const os::kernel::FilePageIdentity second_identity = MakeIdentity(1ULL);
    cache_hit = true;
    prefetched_hit = true;
    const bool second_prefetched =
        cache.Acquire(second_identity, &reader, ReadPage,
                      os::kernel::FilePageAcquireIntent::Prefetch, readahead_tag, physical_address,
                      cache_hit, prefetched_hit) == os::kernel::FilePageCacheStatus::Succeeded &&
        !cache_hit && !prefetched_hit &&
        cache.Release(second_identity, physical_address) ==
            os::kernel::FilePageCacheStatus::Succeeded;
    const os::kernel::FilePageIdentity failure_identity =
        MakeIdentity(OS_TEST_FILE_PAGE_PREFETCH_FAILURE_PAGE_INDEX);
    const os::kernel::FileReadaheadPageTag second_readahead_tag{
        .stream = readahead_tag.stream,
        .policy_generation = 2ULL,
    };
    uint64_t discarded_page_count = OS_TEST_FILE_PAGE_PREFETCH_EMPTY_VALUE;
    const bool waste_and_failure_accounted =
        second_prefetched &&
        cache.DiscardPrefetched(readahead_tag.stream, readahead_tag.policy_generation,
                                discarded_page_count) ==
            os::kernel::FilePageCacheStatus::Succeeded &&
        discarded_page_count == OS_TEST_FILE_PAGE_PREFETCH_FIRST_VALUE &&
        cache.ReadEntry(second_identity, entry) ==
            os::kernel::FilePageCacheStatus::MappingNotFound &&
        cache.Acquire(second_identity, &reader, ReadPage,
                      os::kernel::FilePageAcquireIntent::Prefetch, second_readahead_tag,
                      physical_address, cache_hit,
                      prefetched_hit) == os::kernel::FilePageCacheStatus::Succeeded &&
        cache.Release(second_identity, physical_address) ==
            os::kernel::FilePageCacheStatus::Succeeded &&
        cache.Invalidate(first_identity.file) == os::kernel::FilePageCacheStatus::Succeeded &&
        cache.Acquire(failure_identity, &reader, ReadPage,
                      os::kernel::FilePageAcquireIntent::Prefetch, readahead_tag, physical_address,
                      cache_hit,
                      prefetched_hit) == os::kernel::FilePageCacheStatus::SourceReadFailed &&
        cache.ReadEntry(failure_identity, entry) ==
            os::kernel::FilePageCacheStatus::MappingNotFound &&
        cache.Statistics().successful_prefetch_load_count == 3ULL &&
        cache.Statistics().prefetched_page_count == OS_TEST_FILE_PAGE_PREFETCH_EMPTY_VALUE &&
        cache.Statistics().prefetched_hit_count == OS_TEST_FILE_PAGE_PREFETCH_FIRST_VALUE &&
        cache.Statistics().wasted_prefetched_page_count == 2ULL &&
        cache.Statistics().failed_load_count == OS_TEST_FILE_PAGE_PREFETCH_FIRST_VALUE &&
        feedback.useful_page_count == OS_TEST_FILE_PAGE_PREFETCH_FIRST_VALUE &&
        feedback.wasted_page_count == 2ULL;
    test_context.Expect(waste_and_failure_accounted, OS_TEST_FILE_PAGE_PREFETCH_WASTE);

    const bool lifecycle_valid =
        cache.Validate() == os::kernel::FilePageCacheStatus::Succeeded &&
        cache.Destroy() == os::kernel::FilePageCacheStatus::Succeeded &&
        metadata_heap.Validate() == os::kernel::KernelHeapStatus::Succeeded &&
        metadata_heap.Statistics().allocation_count == OS_TEST_FILE_PAGE_PREFETCH_EMPTY_VALUE &&
        frame_allocator.Statistics().allocated_frame_count == frames_before.allocated_frame_count;
    test_context.Expect(lifecycle_valid, OS_TEST_FILE_PAGE_PREFETCH_LIFECYCLE);
    return test_context.ExitCode();
}

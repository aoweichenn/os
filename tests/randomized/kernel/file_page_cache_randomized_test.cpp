#include "os/kernel/memory/file_page_cache.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_FILE_CACHE_RANDOM_SUITE_NAME =
    "kernel/file_page_cache/randomized";
constexpr std::string_view OS_TEST_FILE_CACHE_RANDOM_REFERENCE =
    "十万步随机获取与释放必须保持唯一页权威、引用计数和硬容量一致";
constexpr std::string_view OS_TEST_FILE_CACHE_RANDOM_DRAIN =
    "随机引用排空并裁剪后必须完整归还全部页帧";

constexpr uint64_t OS_TEST_FILE_CACHE_RANDOM_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_RANDOM_SINGLE_UNIT = 1ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_RANDOM_MANAGED_PAGE_COUNT = 64ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_RANDOM_STATE_VALUES_PER_BYTE = 4ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_RANDOM_STATE_SIZE_BYTES =
    (OS_TEST_FILE_CACHE_RANDOM_MANAGED_PAGE_COUNT +
     OS_TEST_FILE_CACHE_RANDOM_STATE_VALUES_PER_BYTE -
     OS_TEST_FILE_CACHE_RANDOM_SINGLE_UNIT) /
    OS_TEST_FILE_CACHE_RANDOM_STATE_VALUES_PER_BYTE;
constexpr uint64_t OS_TEST_FILE_CACHE_RANDOM_MEMORY_SIZE_BYTES =
    OS_TEST_FILE_CACHE_RANDOM_MANAGED_PAGE_COUNT *
    os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_TEST_FILE_CACHE_RANDOM_CACHE_CAPACITY = 16ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_RANDOM_IDENTITY_COUNT = 64ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_RANDOM_REFERENCE_CAPACITY = 4096ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_RANDOM_ITERATION_COUNT = 100000ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_RANDOM_ACQUIRE_PERCENT = 57ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_RANDOM_PERCENT_SCALE = 100ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_RANDOM_SEED = 0x46494C4543414348ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_RANDOM_SHIFT_FIRST = 12ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_RANDOM_SHIFT_SECOND = 25ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_RANDOM_SHIFT_THIRD = 27ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_RANDOM_MULTIPLIER =
    0x2545F4914F6CDD1DULL;
constexpr uint64_t OS_TEST_FILE_CACHE_RANDOM_SUPERBLOCK_IDENTIFIER = 17ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_RANDOM_SUPERBLOCK_GENERATION = 9ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_RANDOM_NODE_IDENTIFIER = 41ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_RANDOM_NODE_GENERATION = 13ULL;

struct TestMemory final {
    uint8_t bytes[OS_TEST_FILE_CACHE_RANDOM_MEMORY_SIZE_BYTES];
};

struct ActiveReference final {
    os::kernel::FilePageIdentity identity;
    uint64_t physical_address;
};

[[nodiscard]] uint64_t NextRandom(uint64_t &state) noexcept {
    state ^= state >> OS_TEST_FILE_CACHE_RANDOM_SHIFT_FIRST;
    state ^= state << OS_TEST_FILE_CACHE_RANDOM_SHIFT_SECOND;
    state ^= state >> OS_TEST_FILE_CACHE_RANDOM_SHIFT_THIRD;
    state *= OS_TEST_FILE_CACHE_RANDOM_MULTIPLIER;
    return state;
}

[[nodiscard]] uint8_t *AccessPage(
    void *const context, const uint64_t physical_address) noexcept {
    if (context == nullptr ||
        physical_address >
            OS_TEST_FILE_CACHE_RANDOM_MEMORY_SIZE_BYTES -
                os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) {
        return nullptr;
    }
    TestMemory &memory = *static_cast<TestMemory *>(context);
    return memory.bytes + physical_address;
}

[[nodiscard]] bool ReadPage(
    void *const context,
    const os::kernel::FilePageIdentity &identity,
    uint8_t *const destination,
    const uint64_t capacity_bytes) noexcept {
    if (context == nullptr || destination == nullptr ||
        capacity_bytes != os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) {
        return false;
    }
    destination[OS_TEST_FILE_CACHE_RANDOM_EMPTY_VALUE] =
        static_cast<uint8_t>(identity.page_index);
    return true;
}

[[nodiscard]] os::kernel::FilePageIdentity MakeIdentity(
    const uint64_t page_index) noexcept {
    return os::kernel::FilePageIdentity{
        .file =
            {
                .superblock_identifier =
                    OS_TEST_FILE_CACHE_RANDOM_SUPERBLOCK_IDENTIFIER,
                .superblock_generation =
                    OS_TEST_FILE_CACHE_RANDOM_SUPERBLOCK_GENERATION,
                .node_identifier =
                    OS_TEST_FILE_CACHE_RANDOM_NODE_IDENTIFIER,
                .node_generation =
                    OS_TEST_FILE_CACHE_RANDOM_NODE_GENERATION,
            },
        .page_index = page_index,
    };
}

[[nodiscard]] bool EntriesAreConsistent(
    const os::kernel::FilePageCacheEntry *const entries,
    const os::kernel::FilePageCacheStatistics &statistics) noexcept {
    uint64_t resident_count = OS_TEST_FILE_CACHE_RANDOM_EMPTY_VALUE;
    uint64_t referenced_count = OS_TEST_FILE_CACHE_RANDOM_EMPTY_VALUE;
    uint64_t reference_count = OS_TEST_FILE_CACHE_RANDOM_EMPTY_VALUE;
    for (uint64_t entry_index = OS_TEST_FILE_CACHE_RANDOM_EMPTY_VALUE;
         entry_index < OS_TEST_FILE_CACHE_RANDOM_CACHE_CAPACITY;
         ++entry_index) {
        if (!entries[entry_index].active) {
            continue;
        }
        ++resident_count;
        reference_count += entries[entry_index].mapping_reference_count;
        if (entries[entry_index].mapping_reference_count !=
            OS_TEST_FILE_CACHE_RANDOM_EMPTY_VALUE) {
            ++referenced_count;
        }
        for (uint64_t comparison_index =
                 entry_index + OS_TEST_FILE_CACHE_RANDOM_SINGLE_UNIT;
             comparison_index <
             OS_TEST_FILE_CACHE_RANDOM_CACHE_CAPACITY;
             ++comparison_index) {
            if (entries[comparison_index].active &&
                entries[comparison_index].identity.page_index ==
                    entries[entry_index].identity.page_index) {
                return false;
            }
        }
    }
    return resident_count == statistics.resident_page_count &&
           referenced_count == statistics.referenced_page_count &&
           reference_count ==
               statistics.active_mapping_reference_count;
}

}

int main() {
    os::test::TestContext test_context{
        OS_TEST_FILE_CACHE_RANDOM_SUITE_NAME};
    uint8_t state_storage[OS_TEST_FILE_CACHE_RANDOM_STATE_SIZE_BYTES]{};
    TestMemory memory{};
    os::kernel::PhysicalFrameAllocator frame_allocator{
        state_storage,
        OS_TEST_FILE_CACHE_RANDOM_STATE_SIZE_BYTES,
    };
    const os::kernel::PhysicalMemoryMapEntry memory_map[] = {
        {
            .base_address = OS_TEST_FILE_CACHE_RANDOM_EMPTY_VALUE,
            .length_bytes = OS_TEST_FILE_CACHE_RANDOM_MEMORY_SIZE_BYTES,
            .type = os::kernel::OS_KERNEL_MEMORY_MAP_USABLE_REGION_TYPE,
            .attributes = 0U,
        },
    };
    const bool allocator_initialized =
        frame_allocator.Initialize(
            memory_map, OS_TEST_FILE_CACHE_RANDOM_SINGLE_UNIT,
            OS_TEST_FILE_CACHE_RANDOM_MEMORY_SIZE_BYTES) ==
        os::kernel::PhysicalFrameAllocatorStatus::Succeeded;
    const os::kernel::PhysicalFrameAllocatorStatistics baseline =
        frame_allocator.Statistics();
    os::kernel::FilePageCacheEntry
        entries[OS_TEST_FILE_CACHE_RANDOM_CACHE_CAPACITY]{};
    os::kernel::FilePageCache cache{};
    bool reference_valid =
        allocator_initialized &&
        cache.Initialize(entries,
                         OS_TEST_FILE_CACHE_RANDOM_CACHE_CAPACITY,
                         frame_allocator, &memory, AccessPage) ==
            os::kernel::FilePageCacheStatus::Succeeded;
    ActiveReference
        references[OS_TEST_FILE_CACHE_RANDOM_REFERENCE_CAPACITY]{};
    uint64_t reference_count = OS_TEST_FILE_CACHE_RANDOM_EMPTY_VALUE;
    uint64_t random_state = OS_TEST_FILE_CACHE_RANDOM_SEED;
    for (uint64_t iteration = OS_TEST_FILE_CACHE_RANDOM_EMPTY_VALUE;
         reference_valid &&
         iteration < OS_TEST_FILE_CACHE_RANDOM_ITERATION_COUNT;
         ++iteration) {
        const bool acquire =
            reference_count == OS_TEST_FILE_CACHE_RANDOM_EMPTY_VALUE ||
            (reference_count <
                 OS_TEST_FILE_CACHE_RANDOM_REFERENCE_CAPACITY &&
             NextRandom(random_state) %
                     OS_TEST_FILE_CACHE_RANDOM_PERCENT_SCALE <
                 OS_TEST_FILE_CACHE_RANDOM_ACQUIRE_PERCENT);
        if (acquire) {
            const os::kernel::FilePageIdentity identity = MakeIdentity(
                NextRandom(random_state) %
                OS_TEST_FILE_CACHE_RANDOM_IDENTITY_COUNT);
            bool identity_resident = false;
            bool load_slot_available = false;
            for (uint64_t entry_index =
                     OS_TEST_FILE_CACHE_RANDOM_EMPTY_VALUE;
                 entry_index <
                 OS_TEST_FILE_CACHE_RANDOM_CACHE_CAPACITY;
                 ++entry_index) {
                identity_resident =
                    identity_resident ||
                    (entries[entry_index].active &&
                     entries[entry_index].identity.page_index ==
                         identity.page_index);
                load_slot_available =
                    load_slot_available || !entries[entry_index].active ||
                    entries[entry_index].mapping_reference_count ==
                        OS_TEST_FILE_CACHE_RANDOM_EMPTY_VALUE;
            }
            uint64_t physical_address =
                OS_TEST_FILE_CACHE_RANDOM_EMPTY_VALUE;
            bool cache_hit = false;
            const os::kernel::FilePageCacheStatus status =
                cache.Acquire(identity, &memory, ReadPage,
                              physical_address, cache_hit);
            const bool should_succeed =
                identity_resident || load_slot_available;
            reference_valid =
                (should_succeed &&
                 status ==
                     os::kernel::FilePageCacheStatus::Succeeded &&
                 cache_hit == identity_resident) ||
                (!should_succeed &&
                 status ==
                     os::kernel::FilePageCacheStatus::CapacityExhausted);
            if (reference_valid && should_succeed) {
                references[reference_count] = ActiveReference{
                    .identity = identity,
                    .physical_address = physical_address,
                };
                ++reference_count;
            }
        } else {
            const uint64_t release_index =
                NextRandom(random_state) % reference_count;
            reference_valid =
                cache.Release(
                    references[release_index].identity,
                    references[release_index].physical_address) ==
                os::kernel::FilePageCacheStatus::Succeeded;
            references[release_index] =
                references[reference_count -
                           OS_TEST_FILE_CACHE_RANDOM_SINGLE_UNIT];
            --reference_count;
        }
        reference_valid =
            reference_valid &&
            cache.Validate() ==
                os::kernel::FilePageCacheStatus::Succeeded &&
            EntriesAreConsistent(entries, cache.Statistics());
    }
    test_context.Expect(reference_valid,
                        OS_TEST_FILE_CACHE_RANDOM_REFERENCE);

    bool drain_valid = reference_valid;
    while (drain_valid &&
           reference_count != OS_TEST_FILE_CACHE_RANDOM_EMPTY_VALUE) {
        --reference_count;
        drain_valid =
            cache.Release(references[reference_count].identity,
                          references[reference_count].physical_address) ==
            os::kernel::FilePageCacheStatus::Succeeded;
    }
    drain_valid =
        drain_valid &&
        cache.Trim(OS_TEST_FILE_CACHE_RANDOM_EMPTY_VALUE) ==
            os::kernel::FilePageCacheStatus::Succeeded &&
        cache.Validate() ==
            os::kernel::FilePageCacheStatus::Succeeded &&
        cache.Destroy() ==
            os::kernel::FilePageCacheStatus::Succeeded &&
        frame_allocator.Statistics().allocated_frame_count ==
            baseline.allocated_frame_count &&
        frame_allocator.Statistics().free_frame_count ==
            baseline.free_frame_count;
    test_context.Expect(drain_valid,
                        OS_TEST_FILE_CACHE_RANDOM_DRAIN);
    return test_context.ExitCode();
}

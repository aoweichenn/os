#include "os/kernel/memory/file_page_cache.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_FILE_CACHE_RANDOM_SUITE_NAME =
    "kernel/file_page_cache/randomized";
constexpr std::string_view OS_TEST_FILE_CACHE_RANDOM_REFERENCE =
    "十万步随机获取、释放、脏化与回写必须保持唯一页权威、引用计数和状态统计一致";
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
constexpr uint64_t OS_TEST_FILE_CACHE_RANDOM_MARK_DIRTY_PERCENT = 13ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_RANDOM_WRITEBACK_PERCENT = 7ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_RANDOM_PERCENT_SCALE = 100ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_RANDOM_WRITEBACK_MAXIMUM_PAGES = 4ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_RANDOM_WRITE_FAILURE_DIVISOR = 11ULL;
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

struct WriteContext final {
    uint64_t successful_write_count;
    uint64_t failed_write_count;
    bool fail_next_write;
    bool failure_consumed;
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

[[nodiscard]] bool WritePage(
    void *const context,
    const os::kernel::FilePageIdentity &identity,
    const uint8_t *const source,
    const uint64_t length_bytes) noexcept {
    if (context == nullptr || source == nullptr ||
        length_bytes != os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES ||
        source[OS_TEST_FILE_CACHE_RANDOM_EMPTY_VALUE] !=
            static_cast<uint8_t>(identity.page_index)) {
        return false;
    }
    WriteContext &write_context = *static_cast<WriteContext *>(context);
    if (write_context.fail_next_write) {
        write_context.fail_next_write = false;
        write_context.failure_consumed = true;
        ++write_context.failed_write_count;
        return false;
    }
    ++write_context.successful_write_count;
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
    uint64_t dirty_count = OS_TEST_FILE_CACHE_RANDOM_EMPTY_VALUE;
    uint64_t writeback_count = OS_TEST_FILE_CACHE_RANDOM_EMPTY_VALUE;
    uint64_t error_count = OS_TEST_FILE_CACHE_RANDOM_EMPTY_VALUE;
    for (uint64_t entry_index = OS_TEST_FILE_CACHE_RANDOM_EMPTY_VALUE;
         entry_index < OS_TEST_FILE_CACHE_RANDOM_CACHE_CAPACITY;
         ++entry_index) {
        if (entries[entry_index].state ==
            os::kernel::FilePageCacheEntryState::Empty) {
            continue;
        }
        ++resident_count;
        dirty_count +=
            entries[entry_index].state ==
                    os::kernel::FilePageCacheEntryState::Dirty
                ? OS_TEST_FILE_CACHE_RANDOM_SINGLE_UNIT
                : OS_TEST_FILE_CACHE_RANDOM_EMPTY_VALUE;
        writeback_count +=
            entries[entry_index].state ==
                    os::kernel::FilePageCacheEntryState::Writeback
                ? OS_TEST_FILE_CACHE_RANDOM_SINGLE_UNIT
                : OS_TEST_FILE_CACHE_RANDOM_EMPTY_VALUE;
        error_count +=
            entries[entry_index].state ==
                    os::kernel::FilePageCacheEntryState::Error
                ? OS_TEST_FILE_CACHE_RANDOM_SINGLE_UNIT
                : OS_TEST_FILE_CACHE_RANDOM_EMPTY_VALUE;
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
            if (entries[comparison_index].state !=
                    os::kernel::FilePageCacheEntryState::Empty &&
                entries[comparison_index].identity.page_index ==
                    entries[entry_index].identity.page_index) {
                return false;
            }
        }
    }
    return resident_count == statistics.resident_page_count &&
           referenced_count == statistics.referenced_page_count &&
           reference_count ==
               statistics.active_mapping_reference_count &&
           dirty_count == statistics.dirty_page_count &&
           writeback_count == statistics.writeback_page_count &&
           error_count == statistics.error_page_count;
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
                         OS_TEST_FILE_CACHE_RANDOM_CACHE_CAPACITY,
                         frame_allocator, &memory, AccessPage) ==
            os::kernel::FilePageCacheStatus::Succeeded;
    ActiveReference
        references[OS_TEST_FILE_CACHE_RANDOM_REFERENCE_CAPACITY]{};
    uint64_t reference_count = OS_TEST_FILE_CACHE_RANDOM_EMPTY_VALUE;
    uint64_t random_state = OS_TEST_FILE_CACHE_RANDOM_SEED;
    WriteContext write_context{};
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
                    (entries[entry_index].state !=
                         os::kernel::FilePageCacheEntryState::Empty &&
                     entries[entry_index].identity.page_index ==
                         identity.page_index);
                load_slot_available =
                    load_slot_available ||
                    entries[entry_index].state ==
                        os::kernel::FilePageCacheEntryState::Empty ||
                    (entries[entry_index].state ==
                         os::kernel::FilePageCacheEntryState::Clean &&
                     entries[entry_index].mapping_reference_count ==
                         OS_TEST_FILE_CACHE_RANDOM_EMPTY_VALUE);
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
        if (reference_valid &&
            reference_count != OS_TEST_FILE_CACHE_RANDOM_EMPTY_VALUE &&
            NextRandom(random_state) %
                    OS_TEST_FILE_CACHE_RANDOM_PERCENT_SCALE <
                OS_TEST_FILE_CACHE_RANDOM_MARK_DIRTY_PERCENT) {
            const ActiveReference &reference =
                references[NextRandom(random_state) % reference_count];
            const os::kernel::FilePageCacheStatus dirty_status =
                cache.MarkDirty(reference.identity,
                                reference.physical_address);
            reference_valid =
                dirty_status ==
                    os::kernel::FilePageCacheStatus::Succeeded ||
                dirty_status ==
                    os::kernel::FilePageCacheStatus::DirtyLimitReached;
        }
        if (reference_valid &&
            NextRandom(random_state) %
                    OS_TEST_FILE_CACHE_RANDOM_PERCENT_SCALE <
                OS_TEST_FILE_CACHE_RANDOM_WRITEBACK_PERCENT) {
            write_context.fail_next_write =
                NextRandom(random_state) %
                    OS_TEST_FILE_CACHE_RANDOM_WRITE_FAILURE_DIVISOR ==
                OS_TEST_FILE_CACHE_RANDOM_EMPTY_VALUE;
            write_context.failure_consumed = false;
            const uint64_t maximum_page_count =
                NextRandom(random_state) %
                    OS_TEST_FILE_CACHE_RANDOM_WRITEBACK_MAXIMUM_PAGES +
                OS_TEST_FILE_CACHE_RANDOM_SINGLE_UNIT;
            uint64_t written_page_count =
                OS_TEST_FILE_CACHE_RANDOM_EMPTY_VALUE;
            const os::kernel::FilePageCacheStatus writeback_status =
                cache.Writeback(&write_context, WritePage,
                                maximum_page_count,
                                written_page_count);
            reference_valid =
                written_page_count <= maximum_page_count &&
                ((write_context.failure_consumed &&
                  writeback_status ==
                      os::kernel::FilePageCacheStatus::SourceWriteFailed) ||
                 (!write_context.failure_consumed &&
                  writeback_status ==
                      os::kernel::FilePageCacheStatus::Succeeded));
            write_context.fail_next_write = false;
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
    uint64_t final_written_page_count =
        OS_TEST_FILE_CACHE_RANDOM_EMPTY_VALUE;
    drain_valid =
        drain_valid &&
        cache.Writeback(&write_context, WritePage, UINT64_MAX,
                        final_written_page_count) ==
            os::kernel::FilePageCacheStatus::Succeeded &&
        cache.Trim(OS_TEST_FILE_CACHE_RANDOM_EMPTY_VALUE) ==
            os::kernel::FilePageCacheStatus::Succeeded &&
        cache.Validate() ==
            os::kernel::FilePageCacheStatus::Succeeded &&
        cache.Statistics().mark_dirty_count !=
            OS_TEST_FILE_CACHE_RANDOM_EMPTY_VALUE &&
        cache.Statistics().successful_writeback_count !=
            OS_TEST_FILE_CACHE_RANDOM_EMPTY_VALUE &&
        cache.Statistics().failed_writeback_count !=
            OS_TEST_FILE_CACHE_RANDOM_EMPTY_VALUE &&
        write_context.successful_write_count ==
            cache.Statistics().successful_writeback_count &&
        write_context.failed_write_count ==
            cache.Statistics().failed_writeback_count &&
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

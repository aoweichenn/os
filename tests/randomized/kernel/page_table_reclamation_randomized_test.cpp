#include "page_table_test_environment.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

struct PageCoordinates final {
    uint64_t level4_slot;
    uint64_t level3_slot;
    uint64_t level2_slot;
    uint64_t level1_slot;
};

struct ExpectedTableCounts final {
    uint64_t level3_table_count;
    uint64_t level2_table_count;
    uint64_t level1_table_count;
    uint64_t active_page_count;
};

constexpr std::string_view OS_TEST_PAGE_TABLE_RANDOM_SUITE_NAME =
    "kernel/page_table_reclamation/randomized";
constexpr std::string_view OS_TEST_PAGE_TABLE_RANDOM_OPERATIONS =
    "十万次固定种子映射与撤销必须和独立页表层级模型一致";
constexpr std::string_view OS_TEST_PAGE_TABLE_RANDOM_DRAIN =
    "排空全部随机映射后必须只剩独占根页表帧";

constexpr uint64_t OS_TEST_PAGE_TABLE_RANDOM_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_PAGE_TABLE_RANDOM_SINGLE_ENTRY = 1ULL;
constexpr uint64_t OS_TEST_PAGE_TABLE_RANDOM_LEVEL4_BASE_INDEX = 4ULL;
constexpr uint64_t OS_TEST_PAGE_TABLE_RANDOM_LEVEL4_SLOT_COUNT = 4ULL;
constexpr uint64_t OS_TEST_PAGE_TABLE_RANDOM_LEVEL3_SLOT_COUNT = 2ULL;
constexpr uint64_t OS_TEST_PAGE_TABLE_RANDOM_LEVEL2_SLOT_COUNT = 4ULL;
constexpr uint64_t OS_TEST_PAGE_TABLE_RANDOM_LEVEL1_SLOT_COUNT = 32ULL;
constexpr uint64_t OS_TEST_PAGE_TABLE_RANDOM_PAGE_COUNT =
    OS_TEST_PAGE_TABLE_RANDOM_LEVEL4_SLOT_COUNT * OS_TEST_PAGE_TABLE_RANDOM_LEVEL3_SLOT_COUNT *
    OS_TEST_PAGE_TABLE_RANDOM_LEVEL2_SLOT_COUNT * OS_TEST_PAGE_TABLE_RANDOM_LEVEL1_SLOT_COUNT;
constexpr uint64_t OS_TEST_PAGE_TABLE_RANDOM_LEVEL2_GROUP_COUNT =
    OS_TEST_PAGE_TABLE_RANDOM_LEVEL4_SLOT_COUNT * OS_TEST_PAGE_TABLE_RANDOM_LEVEL3_SLOT_COUNT;
constexpr uint64_t OS_TEST_PAGE_TABLE_RANDOM_LEVEL1_GROUP_COUNT =
    OS_TEST_PAGE_TABLE_RANDOM_LEVEL2_GROUP_COUNT * OS_TEST_PAGE_TABLE_RANDOM_LEVEL2_SLOT_COUNT;
constexpr uint64_t OS_TEST_PAGE_TABLE_RANDOM_LEVEL1_SHIFT = 12ULL;
constexpr uint64_t OS_TEST_PAGE_TABLE_RANDOM_LEVEL2_SHIFT = 21ULL;
constexpr uint64_t OS_TEST_PAGE_TABLE_RANDOM_LEVEL3_SHIFT = 30ULL;
constexpr uint64_t OS_TEST_PAGE_TABLE_RANDOM_LEVEL4_SHIFT = 39ULL;
constexpr uint64_t OS_TEST_PAGE_TABLE_RANDOM_SEED = 0x5047545245434C4DULL;
constexpr uint64_t OS_TEST_PAGE_TABLE_RANDOM_ITERATION_COUNT = 100000ULL;
constexpr uint64_t OS_TEST_PAGE_TABLE_RANDOM_VALIDATION_INTERVAL = 257ULL;
constexpr uint64_t OS_TEST_PAGE_TABLE_RANDOM_SHIFT_FIRST = 12ULL;
constexpr uint64_t OS_TEST_PAGE_TABLE_RANDOM_SHIFT_SECOND = 25ULL;
constexpr uint64_t OS_TEST_PAGE_TABLE_RANDOM_SHIFT_THIRD = 27ULL;
constexpr uint64_t OS_TEST_PAGE_TABLE_RANDOM_MULTIPLIER = 0x2545F4914F6CDD1DULL;

constexpr os::kernel::PagePermissions OS_TEST_PAGE_TABLE_RANDOM_PERMISSIONS{
    .writable = true,
    .executable = false,
    .user_accessible = false,
    .cache_disabled = false,
};

[[nodiscard]] uint64_t NextRandom(uint64_t &state) noexcept {
    state ^= state >> OS_TEST_PAGE_TABLE_RANDOM_SHIFT_FIRST;
    state ^= state << OS_TEST_PAGE_TABLE_RANDOM_SHIFT_SECOND;
    state ^= state >> OS_TEST_PAGE_TABLE_RANDOM_SHIFT_THIRD;
    state *= OS_TEST_PAGE_TABLE_RANDOM_MULTIPLIER;
    return state;
}

[[nodiscard]] PageCoordinates Coordinates(const uint64_t page_index) noexcept {
    uint64_t remaining_index = page_index;
    const uint64_t level1_slot = remaining_index % OS_TEST_PAGE_TABLE_RANDOM_LEVEL1_SLOT_COUNT;
    remaining_index /= OS_TEST_PAGE_TABLE_RANDOM_LEVEL1_SLOT_COUNT;
    const uint64_t level2_slot = remaining_index % OS_TEST_PAGE_TABLE_RANDOM_LEVEL2_SLOT_COUNT;
    remaining_index /= OS_TEST_PAGE_TABLE_RANDOM_LEVEL2_SLOT_COUNT;
    const uint64_t level3_slot = remaining_index % OS_TEST_PAGE_TABLE_RANDOM_LEVEL3_SLOT_COUNT;
    remaining_index /= OS_TEST_PAGE_TABLE_RANDOM_LEVEL3_SLOT_COUNT;
    return PageCoordinates{
        .level4_slot = remaining_index,
        .level3_slot = level3_slot,
        .level2_slot = level2_slot,
        .level1_slot = level1_slot,
    };
}

[[nodiscard]] uint64_t VirtualAddress(const uint64_t page_index) noexcept {
    const PageCoordinates coordinates = Coordinates(page_index);
    return ((OS_TEST_PAGE_TABLE_RANDOM_LEVEL4_BASE_INDEX + coordinates.level4_slot)
            << OS_TEST_PAGE_TABLE_RANDOM_LEVEL4_SHIFT) |
           (coordinates.level3_slot << OS_TEST_PAGE_TABLE_RANDOM_LEVEL3_SHIFT) |
           (coordinates.level2_slot << OS_TEST_PAGE_TABLE_RANDOM_LEVEL2_SHIFT) |
           (coordinates.level1_slot << OS_TEST_PAGE_TABLE_RANDOM_LEVEL1_SHIFT);
}

[[nodiscard]] uint64_t Level2GroupIndex(const PageCoordinates coordinates) noexcept {
    return coordinates.level4_slot * OS_TEST_PAGE_TABLE_RANDOM_LEVEL3_SLOT_COUNT +
           coordinates.level3_slot;
}

[[nodiscard]] uint64_t Level1GroupIndex(const PageCoordinates coordinates) noexcept {
    return Level2GroupIndex(coordinates) * OS_TEST_PAGE_TABLE_RANDOM_LEVEL2_SLOT_COUNT +
           coordinates.level2_slot;
}

[[nodiscard]] ExpectedTableCounts CountExpectedTables(const bool *const active_pages) noexcept {
    bool active_level3_tables[OS_TEST_PAGE_TABLE_RANDOM_LEVEL4_SLOT_COUNT]{};
    bool active_level2_tables[OS_TEST_PAGE_TABLE_RANDOM_LEVEL2_GROUP_COUNT]{};
    bool active_level1_tables[OS_TEST_PAGE_TABLE_RANDOM_LEVEL1_GROUP_COUNT]{};
    ExpectedTableCounts counts{};
    for (uint64_t page_index = OS_TEST_PAGE_TABLE_RANDOM_EMPTY_VALUE;
         page_index < OS_TEST_PAGE_TABLE_RANDOM_PAGE_COUNT; ++page_index) {
        if (!active_pages[page_index]) {
            continue;
        }
        const PageCoordinates coordinates = Coordinates(page_index);
        active_level3_tables[coordinates.level4_slot] = true;
        active_level2_tables[Level2GroupIndex(coordinates)] = true;
        active_level1_tables[Level1GroupIndex(coordinates)] = true;
        ++counts.active_page_count;
    }
    for (uint64_t slot_index = OS_TEST_PAGE_TABLE_RANDOM_EMPTY_VALUE;
         slot_index < OS_TEST_PAGE_TABLE_RANDOM_LEVEL4_SLOT_COUNT; ++slot_index) {
        counts.level3_table_count += static_cast<uint64_t>(active_level3_tables[slot_index]);
    }
    for (uint64_t group_index = OS_TEST_PAGE_TABLE_RANDOM_EMPTY_VALUE;
         group_index < OS_TEST_PAGE_TABLE_RANDOM_LEVEL2_GROUP_COUNT; ++group_index) {
        counts.level2_table_count += static_cast<uint64_t>(active_level2_tables[group_index]);
    }
    for (uint64_t group_index = OS_TEST_PAGE_TABLE_RANDOM_EMPTY_VALUE;
         group_index < OS_TEST_PAGE_TABLE_RANDOM_LEVEL1_GROUP_COUNT; ++group_index) {
        counts.level1_table_count += static_cast<uint64_t>(active_level1_tables[group_index]);
    }
    return counts;
}

[[nodiscard]] uint64_t CountActiveInLevel1Group(const bool *const active_pages,
                                                const PageCoordinates target) noexcept {
    uint64_t active_count = OS_TEST_PAGE_TABLE_RANDOM_EMPTY_VALUE;
    for (uint64_t page_index = OS_TEST_PAGE_TABLE_RANDOM_EMPTY_VALUE;
         page_index < OS_TEST_PAGE_TABLE_RANDOM_PAGE_COUNT; ++page_index) {
        const PageCoordinates candidate = Coordinates(page_index);
        if (active_pages[page_index] && candidate.level4_slot == target.level4_slot &&
            candidate.level3_slot == target.level3_slot &&
            candidate.level2_slot == target.level2_slot) {
            ++active_count;
        }
    }
    return active_count;
}

[[nodiscard]] uint64_t CountActiveInLevel2Group(const bool *const active_pages,
                                                const PageCoordinates target) noexcept {
    uint64_t active_count = OS_TEST_PAGE_TABLE_RANDOM_EMPTY_VALUE;
    for (uint64_t page_index = OS_TEST_PAGE_TABLE_RANDOM_EMPTY_VALUE;
         page_index < OS_TEST_PAGE_TABLE_RANDOM_PAGE_COUNT; ++page_index) {
        const PageCoordinates candidate = Coordinates(page_index);
        if (active_pages[page_index] && candidate.level4_slot == target.level4_slot &&
            candidate.level3_slot == target.level3_slot) {
            ++active_count;
        }
    }
    return active_count;
}

[[nodiscard]] uint64_t CountActiveInLevel3Group(const bool *const active_pages,
                                                const PageCoordinates target) noexcept {
    uint64_t active_count = OS_TEST_PAGE_TABLE_RANDOM_EMPTY_VALUE;
    for (uint64_t page_index = OS_TEST_PAGE_TABLE_RANDOM_EMPTY_VALUE;
         page_index < OS_TEST_PAGE_TABLE_RANDOM_PAGE_COUNT; ++page_index) {
        const PageCoordinates candidate = Coordinates(page_index);
        if (active_pages[page_index] && candidate.level4_slot == target.level4_slot) {
            ++active_count;
        }
    }
    return active_count;
}

[[nodiscard]] bool ValidateModel(os::test::PageTableTestEnvironment &environment,
                                 const bool *const active_pages,
                                 const os::kernel::PhysicalFrame *const data_frames) noexcept {
    const ExpectedTableCounts expected = CountExpectedTables(active_pages);
    const os::kernel::PhysicalFrameAllocatorStatistics statistics =
        environment.FrameAllocator().Statistics();
    const uint64_t expected_allocated_frame_count =
        OS_TEST_PAGE_TABLE_RANDOM_SINGLE_ENTRY + expected.active_page_count +
        expected.level1_table_count + expected.level2_table_count + expected.level3_table_count;
    if (statistics.allocated_frame_count != expected_allocated_frame_count ||
        environment.FrameAllocator().ValidateBuddy() !=
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded) {
        return false;
    }
    for (uint64_t page_index = OS_TEST_PAGE_TABLE_RANDOM_EMPTY_VALUE;
         page_index < OS_TEST_PAGE_TABLE_RANDOM_PAGE_COUNT; ++page_index) {
        os::kernel::PageMapping mapping{};
        const os::kernel::PageTableStatus query_status =
            environment.PageTableManager().QueryPage(VirtualAddress(page_index), mapping);
        if (active_pages[page_index]) {
            if (query_status != os::kernel::PageTableStatus::Succeeded ||
                mapping.physical_address != data_frames[page_index].physical_address ||
                !mapping.permissions.writable || mapping.permissions.executable ||
                mapping.permissions.user_accessible || mapping.permissions.cache_disabled) {
                return false;
            }
        } else if (query_status != os::kernel::PageTableStatus::NotMapped) {
            return false;
        }
    }
    return true;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_PAGE_TABLE_RANDOM_SUITE_NAME};
    static os::test::PageTableTestEnvironment environment{
        os::kernel::PageTableRootKind::Exclusive,
    };
    bool active_pages[OS_TEST_PAGE_TABLE_RANDOM_PAGE_COUNT]{};
    os::kernel::PhysicalFrame data_frames[OS_TEST_PAGE_TABLE_RANDOM_PAGE_COUNT]{};
    bool operations_valid = environment.Initialize();
    uint64_t random_state = OS_TEST_PAGE_TABLE_RANDOM_SEED;
    uint64_t failure_iteration = OS_TEST_PAGE_TABLE_RANDOM_EMPTY_VALUE;

    for (uint64_t iteration = OS_TEST_PAGE_TABLE_RANDOM_EMPTY_VALUE;
         operations_valid && iteration < OS_TEST_PAGE_TABLE_RANDOM_ITERATION_COUNT; ++iteration) {
        failure_iteration = iteration;
        const uint64_t page_index = NextRandom(random_state) % OS_TEST_PAGE_TABLE_RANDOM_PAGE_COUNT;
        if (!active_pages[page_index]) {
            operations_valid =
                environment.FrameAllocator().Allocate(data_frames[page_index]) ==
                    os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
                environment.PageTableManager().MapPage(VirtualAddress(page_index),
                                                       data_frames[page_index].physical_address,
                                                       OS_TEST_PAGE_TABLE_RANDOM_PERMISSIONS) ==
                    os::kernel::PageTableStatus::Succeeded;
            if (operations_valid) {
                active_pages[page_index] = true;
            }
        } else {
            const PageCoordinates coordinates = Coordinates(page_index);
            const bool expect_level1_reclaim =
                CountActiveInLevel1Group(active_pages, coordinates) ==
                OS_TEST_PAGE_TABLE_RANDOM_SINGLE_ENTRY;
            const bool expect_level2_reclaim =
                CountActiveInLevel2Group(active_pages, coordinates) ==
                OS_TEST_PAGE_TABLE_RANDOM_SINGLE_ENTRY;
            const bool expect_level3_reclaim =
                CountActiveInLevel3Group(active_pages, coordinates) ==
                OS_TEST_PAGE_TABLE_RANDOM_SINGLE_ENTRY;
            os::kernel::PageTableUnmapResult result{};
            operations_valid =
                environment.PageTableManager().UnmapPage(VirtualAddress(page_index), result) ==
                    os::kernel::PageTableStatus::Succeeded &&
                result.reclaimed_level1_table_count ==
                    static_cast<uint64_t>(expect_level1_reclaim) &&
                result.reclaimed_level2_table_count ==
                    static_cast<uint64_t>(expect_level2_reclaim) &&
                result.reclaimed_level3_table_count ==
                    static_cast<uint64_t>(expect_level3_reclaim) &&
                result.reclaimed_table_frame_count ==
                    static_cast<uint64_t>(expect_level1_reclaim) +
                        static_cast<uint64_t>(expect_level2_reclaim) +
                        static_cast<uint64_t>(expect_level3_reclaim) &&
                environment.FrameAllocator().Release(data_frames[page_index]) ==
                    os::kernel::PhysicalFrameAllocatorStatus::Succeeded;
            if (operations_valid) {
                active_pages[page_index] = false;
            }
        }
        if (operations_valid && iteration % OS_TEST_PAGE_TABLE_RANDOM_VALIDATION_INTERVAL ==
                                    OS_TEST_PAGE_TABLE_RANDOM_EMPTY_VALUE) {
            operations_valid = ValidateModel(environment, active_pages, data_frames);
        }
    }
    test_context.ExpectRandom(operations_valid, OS_TEST_PAGE_TABLE_RANDOM_OPERATIONS,
                              OS_TEST_PAGE_TABLE_RANDOM_SEED, failure_iteration);

    bool drain_valid = operations_valid;
    for (uint64_t page_index = OS_TEST_PAGE_TABLE_RANDOM_EMPTY_VALUE;
         drain_valid && page_index < OS_TEST_PAGE_TABLE_RANDOM_PAGE_COUNT; ++page_index) {
        if (!active_pages[page_index]) {
            continue;
        }
        drain_valid = environment.PageTableManager().UnmapPage(VirtualAddress(page_index)) ==
                          os::kernel::PageTableStatus::Succeeded &&
                      environment.FrameAllocator().Release(data_frames[page_index]) ==
                          os::kernel::PhysicalFrameAllocatorStatus::Succeeded;
        active_pages[page_index] = false;
    }
    const os::kernel::PhysicalFrameAllocatorStatistics drained_statistics =
        environment.FrameAllocator().Statistics();
    drain_valid =
        drain_valid &&
        drained_statistics.allocated_frame_count == OS_TEST_PAGE_TABLE_RANDOM_SINGLE_ENTRY &&
        ValidateModel(environment, active_pages, data_frames);
    test_context.ExpectRandom(drain_valid, OS_TEST_PAGE_TABLE_RANDOM_DRAIN,
                              OS_TEST_PAGE_TABLE_RANDOM_SEED,
                              OS_TEST_PAGE_TABLE_RANDOM_ITERATION_COUNT);
    return test_context.ExitCode();
}

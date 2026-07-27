#include "os/kernel/memory/user_page_reference.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view
    OS_TEST_USER_PAGE_REFERENCE_RANDOM_SUITE_NAME =
        "kernel/user_page_reference/randomized";
constexpr std::string_view
    OS_TEST_USER_PAGE_REFERENCE_RANDOM_MODEL =
        "十万步 fork、退出与独占恢复必须和引用模型逐步一致";
constexpr std::string_view
    OS_TEST_USER_PAGE_REFERENCE_RANDOM_DRAIN =
        "随机状态排空后必须没有活动引用和元数据项";

constexpr uint64_t OS_TEST_USER_PAGE_REFERENCE_RANDOM_EMPTY_VALUE =
    0ULL;
constexpr uint64_t OS_TEST_USER_PAGE_REFERENCE_RANDOM_SINGLE_UNIT =
    1ULL;
constexpr uint64_t OS_TEST_USER_PAGE_REFERENCE_RANDOM_PAGE_SIZE_BYTES =
    4096ULL;
constexpr uint64_t OS_TEST_USER_PAGE_REFERENCE_RANDOM_PAGE_COUNT =
    32ULL;
constexpr uint64_t OS_TEST_USER_PAGE_REFERENCE_RANDOM_CAPACITY =
    OS_TEST_USER_PAGE_REFERENCE_RANDOM_PAGE_COUNT;
constexpr uint64_t OS_TEST_USER_PAGE_REFERENCE_RANDOM_ITERATION_COUNT =
    100000ULL;
constexpr uint64_t OS_TEST_USER_PAGE_REFERENCE_RANDOM_OPERATION_SCALE =
    100ULL;
constexpr uint64_t OS_TEST_USER_PAGE_REFERENCE_RANDOM_FORK_PERCENT =
    47ULL;
constexpr uint64_t OS_TEST_USER_PAGE_REFERENCE_RANDOM_RELEASE_PERCENT =
    83ULL;
constexpr uint64_t OS_TEST_USER_PAGE_REFERENCE_RANDOM_SEED =
    0x434F575245465331ULL;
constexpr uint64_t OS_TEST_USER_PAGE_REFERENCE_RANDOM_SHIFT_FIRST =
    12ULL;
constexpr uint64_t OS_TEST_USER_PAGE_REFERENCE_RANDOM_SHIFT_SECOND =
    25ULL;
constexpr uint64_t OS_TEST_USER_PAGE_REFERENCE_RANDOM_SHIFT_THIRD =
    27ULL;
constexpr uint64_t OS_TEST_USER_PAGE_REFERENCE_RANDOM_MULTIPLIER =
    0x2545F4914F6CDD1DULL;

[[nodiscard]] uint64_t NextRandom(uint64_t &state) noexcept {
    state ^= state >>
             OS_TEST_USER_PAGE_REFERENCE_RANDOM_SHIFT_FIRST;
    state ^= state <<
             OS_TEST_USER_PAGE_REFERENCE_RANDOM_SHIFT_SECOND;
    state ^= state >>
             OS_TEST_USER_PAGE_REFERENCE_RANDOM_SHIFT_THIRD;
    state *= OS_TEST_USER_PAGE_REFERENCE_RANDOM_MULTIPLIER;
    return state;
}

[[nodiscard]] uint64_t PhysicalAddress(
    const uint64_t page_index) noexcept {
    return (page_index +
            OS_TEST_USER_PAGE_REFERENCE_RANDOM_SINGLE_UNIT) *
           OS_TEST_USER_PAGE_REFERENCE_RANDOM_PAGE_SIZE_BYTES;
}

[[nodiscard]] bool ModelMatches(
    os::kernel::UserPageReferenceManager &manager,
    const uint64_t *const model_counts,
    const bool *const tracked) noexcept {
    uint64_t active_entry_count =
        OS_TEST_USER_PAGE_REFERENCE_RANDOM_EMPTY_VALUE;
    uint64_t active_reference_count =
        OS_TEST_USER_PAGE_REFERENCE_RANDOM_EMPTY_VALUE;
    for (uint64_t page_index =
             OS_TEST_USER_PAGE_REFERENCE_RANDOM_EMPTY_VALUE;
         page_index <
         OS_TEST_USER_PAGE_REFERENCE_RANDOM_PAGE_COUNT;
         ++page_index) {
        uint64_t actual_count =
            OS_TEST_USER_PAGE_REFERENCE_RANDOM_EMPTY_VALUE;
        const os::kernel::UserPageReferenceStatus read_status =
            manager.ReadReferenceCount(
                PhysicalAddress(page_index), actual_count);
        if (tracked[page_index]) {
            if (read_status !=
                    os::kernel::UserPageReferenceStatus::Succeeded ||
                actual_count != model_counts[page_index]) {
                return false;
            }
            ++active_entry_count;
            active_reference_count += model_counts[page_index];
        } else if (
            read_status !=
            os::kernel::UserPageReferenceStatus::
                ReferenceNotFound) {
            return false;
        }
    }
    const os::kernel::UserPageReferenceStatistics statistics =
        manager.Statistics();
    return manager.Validate() ==
               os::kernel::UserPageReferenceStatus::Succeeded &&
           statistics.active_entry_count == active_entry_count &&
           statistics.active_reference_count ==
               active_reference_count;
}

}

int main() {
    os::test::TestContext test_context{
        OS_TEST_USER_PAGE_REFERENCE_RANDOM_SUITE_NAME};
    os::kernel::UserPageReferenceEntry
        entries[OS_TEST_USER_PAGE_REFERENCE_RANDOM_CAPACITY]{};
    os::kernel::UserPageReferenceManager manager{};
    uint64_t model_counts
        [OS_TEST_USER_PAGE_REFERENCE_RANDOM_PAGE_COUNT]{};
    bool tracked[OS_TEST_USER_PAGE_REFERENCE_RANDOM_PAGE_COUNT]{};
    for (uint64_t page_index =
             OS_TEST_USER_PAGE_REFERENCE_RANDOM_EMPTY_VALUE;
         page_index <
         OS_TEST_USER_PAGE_REFERENCE_RANDOM_PAGE_COUNT;
         ++page_index) {
        model_counts[page_index] =
            OS_TEST_USER_PAGE_REFERENCE_RANDOM_SINGLE_UNIT;
    }
    bool model_valid =
        manager.Initialize(
            entries,
            OS_TEST_USER_PAGE_REFERENCE_RANDOM_CAPACITY,
            OS_TEST_USER_PAGE_REFERENCE_RANDOM_PAGE_SIZE_BYTES) ==
        os::kernel::UserPageReferenceStatus::Succeeded;
    uint64_t random_state =
        OS_TEST_USER_PAGE_REFERENCE_RANDOM_SEED;
    for (uint64_t iteration =
             OS_TEST_USER_PAGE_REFERENCE_RANDOM_EMPTY_VALUE;
         model_valid &&
         iteration <
             OS_TEST_USER_PAGE_REFERENCE_RANDOM_ITERATION_COUNT;
         ++iteration) {
        const uint64_t page_index =
            NextRandom(random_state) %
            OS_TEST_USER_PAGE_REFERENCE_RANDOM_PAGE_COUNT;
        const uint64_t operation =
            NextRandom(random_state) %
            OS_TEST_USER_PAGE_REFERENCE_RANDOM_OPERATION_SCALE;
        if (model_counts[page_index] ==
            OS_TEST_USER_PAGE_REFERENCE_RANDOM_EMPTY_VALUE) {
            model_counts[page_index] =
                OS_TEST_USER_PAGE_REFERENCE_RANDOM_SINGLE_UNIT;
        } else if (
            operation <
            OS_TEST_USER_PAGE_REFERENCE_RANDOM_FORK_PERCENT) {
            bool first_share = false;
            const os::kernel::UserPageReferenceStatus status =
                manager.RetainForFork(
                    PhysicalAddress(page_index), first_share);
            model_valid =
                status ==
                    os::kernel::UserPageReferenceStatus::
                        Succeeded &&
                first_share == !tracked[page_index];
            if (model_valid) {
                ++model_counts[page_index];
                tracked[page_index] = true;
            }
        } else if (
            operation <
            OS_TEST_USER_PAGE_REFERENCE_RANDOM_RELEASE_PERCENT) {
            bool release_frame = false;
            model_valid =
                manager.Release(
                    PhysicalAddress(page_index),
                    release_frame) ==
                os::kernel::UserPageReferenceStatus::Succeeded;
            if (model_valid) {
                --model_counts[page_index];
                if (model_counts[page_index] ==
                    OS_TEST_USER_PAGE_REFERENCE_RANDOM_EMPTY_VALUE) {
                    tracked[page_index] = false;
                    model_valid = release_frame;
                } else {
                    model_valid = !release_frame;
                }
            }
        } else if (
            tracked[page_index] &&
            model_counts[page_index] ==
                OS_TEST_USER_PAGE_REFERENCE_RANDOM_SINGLE_UNIT) {
            model_valid =
                manager.RestoreExclusive(
                    PhysicalAddress(page_index)) ==
                os::kernel::UserPageReferenceStatus::Succeeded;
            tracked[page_index] = false;
        }
        model_valid =
            model_valid &&
            ModelMatches(manager, model_counts, tracked);
    }
    test_context.Expect(
        model_valid,
        OS_TEST_USER_PAGE_REFERENCE_RANDOM_MODEL);

    bool drain_valid = model_valid;
    for (uint64_t page_index =
             OS_TEST_USER_PAGE_REFERENCE_RANDOM_EMPTY_VALUE;
         drain_valid &&
         page_index <
         OS_TEST_USER_PAGE_REFERENCE_RANDOM_PAGE_COUNT;
         ++page_index) {
        while (model_counts[page_index] !=
               OS_TEST_USER_PAGE_REFERENCE_RANDOM_EMPTY_VALUE) {
            bool release_frame = false;
            drain_valid =
                manager.Release(
                    PhysicalAddress(page_index),
                    release_frame) ==
                os::kernel::UserPageReferenceStatus::Succeeded;
            --model_counts[page_index];
            if (model_counts[page_index] ==
                OS_TEST_USER_PAGE_REFERENCE_RANDOM_EMPTY_VALUE) {
                tracked[page_index] = false;
                drain_valid = drain_valid && release_frame;
            } else {
                drain_valid = drain_valid && !release_frame;
            }
            if (!drain_valid) {
                break;
            }
        }
    }
    const os::kernel::UserPageReferenceStatistics final_statistics =
        manager.Statistics();
    drain_valid =
        drain_valid &&
        manager.Validate() ==
            os::kernel::UserPageReferenceStatus::Succeeded &&
        final_statistics.active_entry_count ==
            OS_TEST_USER_PAGE_REFERENCE_RANDOM_EMPTY_VALUE &&
        final_statistics.active_reference_count ==
            OS_TEST_USER_PAGE_REFERENCE_RANDOM_EMPTY_VALUE;
    test_context.Expect(
        drain_valid,
        OS_TEST_USER_PAGE_REFERENCE_RANDOM_DRAIN);
    return test_context.ExitCode();
}

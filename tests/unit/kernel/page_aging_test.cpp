#include <os/kernel/memory/page_aging.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_PAGE_AGING_SUITE_NAME = "kernel/page_aging/unit";
constexpr std::string_view OS_TEST_PAGE_AGING_INITIALIZATION =
    "初始化必须拒绝无效存储、容量和非二次幂 hash";
constexpr std::string_view OS_TEST_PAGE_AGING_TRANSITIONS =
    "连续未访问必须降级并成为候选，访问恢复必须提升";
constexpr std::string_view OS_TEST_PAGE_AGING_ALIASES =
    "同一物理页 alias 必须合并 Accessed OR 和 eligibility AND";
constexpr std::string_view OS_TEST_PAGE_AGING_RECONCILIATION =
    "未观察页、分类冲突、容量耗尽和 reset 必须保持队列/hash 守恒";
constexpr std::string_view OS_TEST_PAGE_AGING_FORGET = "成功回收必须忘记精确候选并同步分类计数";
constexpr std::string_view OS_TEST_PAGE_AGING_GENERATION =
    "同类物理帧代际变化必须撤销旧候选并重新经过冷却";

constexpr uint64_t OS_TEST_PAGE_AGING_CAPACITY = 8ULL;
constexpr uint64_t OS_TEST_PAGE_AGING_HASH_CAPACITY = 16ULL;
constexpr uint64_t OS_TEST_PAGE_AGING_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_PAGE_AGING_SINGLE_COUNT = 1ULL;
constexpr uint64_t OS_TEST_PAGE_AGING_TWO_COUNT = 2ULL;
constexpr uint64_t OS_TEST_PAGE_AGING_FILE_ADDRESS = 0x1000ULL;
constexpr uint64_t OS_TEST_PAGE_AGING_ANONYMOUS_ADDRESS = 0x2000ULL;

[[nodiscard]] bool InitializeManager(os::kernel::PageAgingManager &manager,
                                     os::kernel::PageAgingEntry *const entries,
                                     uint64_t *const hash) noexcept {
    return manager.Initialize(entries, OS_TEST_PAGE_AGING_CAPACITY, hash,
                              OS_TEST_PAGE_AGING_HASH_CAPACITY) ==
           os::kernel::PageAgingStatus::Succeeded;
}

[[nodiscard]] bool ValidateInitialization() noexcept {
    os::kernel::PageAgingManager manager{};
    os::kernel::PageAgingEntry entries[OS_TEST_PAGE_AGING_CAPACITY]{};
    uint64_t hash[OS_TEST_PAGE_AGING_HASH_CAPACITY]{};
    return manager.Initialize(nullptr, OS_TEST_PAGE_AGING_CAPACITY, hash,
                              OS_TEST_PAGE_AGING_HASH_CAPACITY) ==
               os::kernel::PageAgingStatus::NullEntryStorage &&
           manager.Initialize(entries, OS_TEST_PAGE_AGING_CAPACITY, nullptr,
                              OS_TEST_PAGE_AGING_HASH_CAPACITY) ==
               os::kernel::PageAgingStatus::NullHashStorage &&
           manager.Initialize(entries, OS_TEST_PAGE_AGING_EMPTY_VALUE, hash,
                              OS_TEST_PAGE_AGING_HASH_CAPACITY) ==
               os::kernel::PageAgingStatus::InvalidCapacity &&
           manager.Initialize(entries, OS_TEST_PAGE_AGING_CAPACITY, hash,
                              OS_TEST_PAGE_AGING_HASH_CAPACITY - 1ULL) ==
               os::kernel::PageAgingStatus::InvalidHashCapacity &&
           InitializeManager(manager, entries, hash) &&
           manager.Initialize(entries, OS_TEST_PAGE_AGING_CAPACITY, hash,
                              OS_TEST_PAGE_AGING_HASH_CAPACITY) ==
               os::kernel::PageAgingStatus::AlreadyInitialized &&
           manager.Observe(OS_TEST_PAGE_AGING_FILE_ADDRESS, os::kernel::PageAgingKind::File, true,
                           true) == os::kernel::PageAgingStatus::ObservationNotActive &&
           manager.BeginObservation() == os::kernel::PageAgingStatus::Succeeded &&
           manager.BeginObservation() == os::kernel::PageAgingStatus::ObservationAlreadyActive &&
           manager.Observe(OS_TEST_PAGE_AGING_FILE_ADDRESS + OS_TEST_PAGE_AGING_SINGLE_COUNT,
                           os::kernel::PageAgingKind::File, true,
                           true) == os::kernel::PageAgingStatus::InvalidPhysicalAddress &&
           manager.Observe(OS_TEST_PAGE_AGING_FILE_ADDRESS, os::kernel::PageAgingKind::None, true,
                           true) == os::kernel::PageAgingStatus::InvalidKind &&
           manager.CancelObservation() == os::kernel::PageAgingStatus::Succeeded &&
           manager.Validate() == os::kernel::PageAgingStatus::Succeeded;
}

[[nodiscard]] bool ValidateTransitions() noexcept {
    os::kernel::PageAgingManager manager{};
    os::kernel::PageAgingEntry entries[OS_TEST_PAGE_AGING_CAPACITY]{};
    uint64_t hash[OS_TEST_PAGE_AGING_HASH_CAPACITY]{};
    if (!InitializeManager(manager, entries, hash) ||
        manager.BeginObservation() != os::kernel::PageAgingStatus::Succeeded ||
        manager.Observe(OS_TEST_PAGE_AGING_FILE_ADDRESS, os::kernel::PageAgingKind::File, false,
                        true) != os::kernel::PageAgingStatus::Succeeded ||
        manager.EndObservation() != os::kernel::PageAgingStatus::Succeeded) {
        return false;
    }
    os::kernel::PageAgingEntrySnapshot entry{};
    if (manager.Read(OS_TEST_PAGE_AGING_FILE_ADDRESS, os::kernel::PageAgingKind::File, entry) !=
            os::kernel::PageAgingStatus::Succeeded ||
        entry.state != os::kernel::PageAgingState::Inactive || entry.reclaim_candidate ||
        manager.Statistics().reclaim_candidate_count != OS_TEST_PAGE_AGING_EMPTY_VALUE ||
        manager.BeginObservation() != os::kernel::PageAgingStatus::Succeeded ||
        manager.Observe(OS_TEST_PAGE_AGING_FILE_ADDRESS, os::kernel::PageAgingKind::File, false,
                        true) != os::kernel::PageAgingStatus::Succeeded ||
        manager.EndObservation() != os::kernel::PageAgingStatus::Succeeded ||
        manager.Statistics().reclaim_candidate_count != OS_TEST_PAGE_AGING_SINGLE_COUNT ||
        manager.Read(OS_TEST_PAGE_AGING_FILE_ADDRESS, os::kernel::PageAgingKind::File, entry) !=
            os::kernel::PageAgingStatus::Succeeded ||
        !entry.reclaim_candidate ||
        manager.BeginObservation() != os::kernel::PageAgingStatus::Succeeded ||
        manager.Observe(OS_TEST_PAGE_AGING_FILE_ADDRESS, os::kernel::PageAgingKind::File, true,
                        true) != os::kernel::PageAgingStatus::Succeeded ||
        manager.EndObservation() != os::kernel::PageAgingStatus::Succeeded ||
        manager.Read(OS_TEST_PAGE_AGING_FILE_ADDRESS, os::kernel::PageAgingKind::File, entry) !=
            os::kernel::PageAgingStatus::Succeeded) {
        return false;
    }
    const os::kernel::PageAgingStatistics statistics = manager.Statistics();
    return entry.state == os::kernel::PageAgingState::Active &&
           statistics.active_file_page_count == OS_TEST_PAGE_AGING_SINGLE_COUNT &&
           statistics.promotion_count == OS_TEST_PAGE_AGING_SINGLE_COUNT &&
           statistics.demotion_count == OS_TEST_PAGE_AGING_SINGLE_COUNT &&
           statistics.inactive_retention_count == OS_TEST_PAGE_AGING_SINGLE_COUNT &&
           manager.Validate() == os::kernel::PageAgingStatus::Succeeded;
}

[[nodiscard]] bool ValidateAliasAggregation() noexcept {
    os::kernel::PageAgingManager manager{};
    os::kernel::PageAgingEntry entries[OS_TEST_PAGE_AGING_CAPACITY]{};
    uint64_t hash[OS_TEST_PAGE_AGING_HASH_CAPACITY]{};
    os::kernel::PageAgingEntrySnapshot entry{};
    if (!InitializeManager(manager, entries, hash) ||
        manager.BeginObservation() != os::kernel::PageAgingStatus::Succeeded ||
        manager.Observe(OS_TEST_PAGE_AGING_FILE_ADDRESS, os::kernel::PageAgingKind::File, false,
                        true) != os::kernel::PageAgingStatus::Succeeded ||
        manager.Observe(OS_TEST_PAGE_AGING_FILE_ADDRESS, os::kernel::PageAgingKind::File, true,
                        false) != os::kernel::PageAgingStatus::Succeeded ||
        manager.EndObservation() != os::kernel::PageAgingStatus::Succeeded ||
        manager.Read(OS_TEST_PAGE_AGING_FILE_ADDRESS, os::kernel::PageAgingKind::File, entry) !=
            os::kernel::PageAgingStatus::Succeeded) {
        return false;
    }
    const os::kernel::PageAgingStatistics statistics = manager.Statistics();
    return entry.alias_observation_count == OS_TEST_PAGE_AGING_TWO_COUNT && entry.referenced &&
           !entry.reclaim_eligible && entry.state == os::kernel::PageAgingState::Active &&
           statistics.page_observation_count == OS_TEST_PAGE_AGING_SINGLE_COUNT &&
           statistics.alias_observation_count == OS_TEST_PAGE_AGING_TWO_COUNT &&
           manager.Validate() == os::kernel::PageAgingStatus::Succeeded;
}

[[nodiscard]] bool ValidateReconciliation() noexcept {
    os::kernel::PageAgingManager manager{};
    os::kernel::PageAgingEntry entries[OS_TEST_PAGE_AGING_CAPACITY]{};
    uint64_t hash[OS_TEST_PAGE_AGING_HASH_CAPACITY]{};
    if (!InitializeManager(manager, entries, hash) ||
        manager.BeginObservation() != os::kernel::PageAgingStatus::Succeeded ||
        manager.Observe(OS_TEST_PAGE_AGING_FILE_ADDRESS, os::kernel::PageAgingKind::File, true,
                        true) != os::kernel::PageAgingStatus::Succeeded ||
        manager.Observe(OS_TEST_PAGE_AGING_FILE_ADDRESS, os::kernel::PageAgingKind::Anonymous, true,
                        true) != os::kernel::PageAgingStatus::KindConflict ||
        manager.Observe(OS_TEST_PAGE_AGING_ANONYMOUS_ADDRESS, os::kernel::PageAgingKind::Anonymous,
                        true, true) != os::kernel::PageAgingStatus::Succeeded ||
        manager.EndObservation() != os::kernel::PageAgingStatus::Succeeded ||
        manager.BeginObservation() != os::kernel::PageAgingStatus::Succeeded ||
        manager.Observe(OS_TEST_PAGE_AGING_FILE_ADDRESS, os::kernel::PageAgingKind::Anonymous, true,
                        true) != os::kernel::PageAgingStatus::Succeeded ||
        manager.Observe(OS_TEST_PAGE_AGING_ANONYMOUS_ADDRESS, os::kernel::PageAgingKind::Anonymous,
                        true, true) != os::kernel::PageAgingStatus::Succeeded ||
        manager.EndObservation() != os::kernel::PageAgingStatus::Succeeded) {
        return false;
    }
    os::kernel::PageAgingEntrySnapshot missing_entry{};
    if (manager.Read(OS_TEST_PAGE_AGING_FILE_ADDRESS, os::kernel::PageAgingKind::File,
                     missing_entry) != os::kernel::PageAgingStatus::EntryNotFound ||
        manager.Read(OS_TEST_PAGE_AGING_FILE_ADDRESS, os::kernel::PageAgingKind::Anonymous,
                     missing_entry) != os::kernel::PageAgingStatus::Succeeded ||
        manager.Statistics().reclassification_count != OS_TEST_PAGE_AGING_SINGLE_COUNT ||
        manager.Reset() != os::kernel::PageAgingStatus::Succeeded) {
        return false;
    }
    const os::kernel::PageAgingStatistics statistics = manager.Statistics();
    return statistics.tracked_page_count == OS_TEST_PAGE_AGING_EMPTY_VALUE &&
           statistics.insertion_count ==
               OS_TEST_PAGE_AGING_TWO_COUNT + OS_TEST_PAGE_AGING_SINGLE_COUNT &&
           statistics.removal_count ==
               OS_TEST_PAGE_AGING_TWO_COUNT + OS_TEST_PAGE_AGING_SINGLE_COUNT &&
           statistics.reset_count == OS_TEST_PAGE_AGING_SINGLE_COUNT &&
           manager.Validate() == os::kernel::PageAgingStatus::Succeeded;
}

[[nodiscard]] bool ValidateForget() noexcept {
    os::kernel::PageAgingManager manager{};
    os::kernel::PageAgingEntry entries[OS_TEST_PAGE_AGING_CAPACITY]{};
    uint64_t hash[OS_TEST_PAGE_AGING_HASH_CAPACITY]{};
    if (!InitializeManager(manager, entries, hash)) {
        return false;
    }
    for (uint64_t round = OS_TEST_PAGE_AGING_EMPTY_VALUE; round < OS_TEST_PAGE_AGING_TWO_COUNT;
         ++round) {
        if (manager.BeginObservation() != os::kernel::PageAgingStatus::Succeeded ||
            manager.Observe(OS_TEST_PAGE_AGING_FILE_ADDRESS, os::kernel::PageAgingKind::File, false,
                            true) != os::kernel::PageAgingStatus::Succeeded ||
            manager.Observe(OS_TEST_PAGE_AGING_ANONYMOUS_ADDRESS,
                            os::kernel::PageAgingKind::Anonymous, false,
                            true) != os::kernel::PageAgingStatus::Succeeded ||
            manager.EndObservation() != os::kernel::PageAgingStatus::Succeeded) {
            return false;
        }
    }
    const os::kernel::PageAgingStatistics candidates = manager.Statistics();
    if (candidates.reclaim_candidate_count != OS_TEST_PAGE_AGING_TWO_COUNT ||
        candidates.file_reclaim_candidate_count != OS_TEST_PAGE_AGING_SINGLE_COUNT ||
        candidates.anonymous_reclaim_candidate_count != OS_TEST_PAGE_AGING_SINGLE_COUNT ||
        manager.Forget(OS_TEST_PAGE_AGING_FILE_ADDRESS, os::kernel::PageAgingKind::File) !=
            os::kernel::PageAgingStatus::Succeeded) {
        return false;
    }
    os::kernel::PageAgingEntrySnapshot missing{};
    const os::kernel::PageAgingStatistics forgotten = manager.Statistics();
    return manager.Read(OS_TEST_PAGE_AGING_FILE_ADDRESS, os::kernel::PageAgingKind::File,
                        missing) == os::kernel::PageAgingStatus::EntryNotFound &&
           manager.Forget(OS_TEST_PAGE_AGING_FILE_ADDRESS, os::kernel::PageAgingKind::File) ==
               os::kernel::PageAgingStatus::EntryNotFound &&
           forgotten.reclaim_candidate_count == OS_TEST_PAGE_AGING_SINGLE_COUNT &&
           forgotten.file_reclaim_candidate_count == OS_TEST_PAGE_AGING_EMPTY_VALUE &&
           forgotten.anonymous_reclaim_candidate_count == OS_TEST_PAGE_AGING_SINGLE_COUNT &&
           forgotten.forgotten_page_count == OS_TEST_PAGE_AGING_SINGLE_COUNT &&
           manager.Validate() == os::kernel::PageAgingStatus::Succeeded;
}

[[nodiscard]] bool ValidateGenerationRefresh() noexcept {
    os::kernel::PageAgingManager manager{};
    os::kernel::PageAgingEntry entries[OS_TEST_PAGE_AGING_CAPACITY]{};
    uint64_t hash[OS_TEST_PAGE_AGING_HASH_CAPACITY]{};
    if (!InitializeManager(manager, entries, hash)) {
        return false;
    }
    for (uint64_t round = OS_TEST_PAGE_AGING_EMPTY_VALUE; round < OS_TEST_PAGE_AGING_TWO_COUNT;
         ++round) {
        if (manager.BeginObservation() != os::kernel::PageAgingStatus::Succeeded ||
            manager.Observe(OS_TEST_PAGE_AGING_FILE_ADDRESS, os::kernel::PageAgingKind::File, false,
                            true, OS_TEST_PAGE_AGING_SINGLE_COUNT) !=
                os::kernel::PageAgingStatus::Succeeded ||
            manager.EndObservation() != os::kernel::PageAgingStatus::Succeeded) {
            return false;
        }
    }
    os::kernel::PageAgingEntrySnapshot refreshed{};
    return manager.Statistics().reclaim_candidate_count == OS_TEST_PAGE_AGING_SINGLE_COUNT &&
           manager.BeginObservation() == os::kernel::PageAgingStatus::Succeeded &&
           manager.Observe(OS_TEST_PAGE_AGING_FILE_ADDRESS, os::kernel::PageAgingKind::File, false,
                           true, OS_TEST_PAGE_AGING_TWO_COUNT) ==
               os::kernel::PageAgingStatus::Succeeded &&
           manager.EndObservation() == os::kernel::PageAgingStatus::Succeeded &&
           manager.Read(OS_TEST_PAGE_AGING_FILE_ADDRESS, os::kernel::PageAgingKind::File,
                        refreshed) == os::kernel::PageAgingStatus::Succeeded &&
           refreshed.identity_generation == OS_TEST_PAGE_AGING_TWO_COUNT &&
           refreshed.state == os::kernel::PageAgingState::Inactive &&
           manager.Statistics().reclaim_candidate_count == OS_TEST_PAGE_AGING_EMPTY_VALUE &&
           manager.Statistics().generation_refresh_count == OS_TEST_PAGE_AGING_SINGLE_COUNT &&
           manager.Validate() == os::kernel::PageAgingStatus::Succeeded;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_PAGE_AGING_SUITE_NAME};
    test_context.Expect(ValidateInitialization(), OS_TEST_PAGE_AGING_INITIALIZATION);
    test_context.Expect(ValidateTransitions(), OS_TEST_PAGE_AGING_TRANSITIONS);
    test_context.Expect(ValidateAliasAggregation(), OS_TEST_PAGE_AGING_ALIASES);
    test_context.Expect(ValidateReconciliation(), OS_TEST_PAGE_AGING_RECONCILIATION);
    test_context.Expect(ValidateForget(), OS_TEST_PAGE_AGING_FORGET);
    test_context.Expect(ValidateGenerationRefresh(), OS_TEST_PAGE_AGING_GENERATION);
    return test_context.ExitCode();
}

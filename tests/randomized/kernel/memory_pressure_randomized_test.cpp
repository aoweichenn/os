#include <os/kernel/memory/memory_pressure.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_MEMORY_PRESSURE_RANDOM_SUITE_NAME =
    "kernel/memory_pressure/randomized";
constexpr std::string_view OS_TEST_MEMORY_PRESSURE_RANDOM_ACCOUNTING =
    "十万步驻留与 commit 随机事务必须逐步满足容量、峰值和回滚守恒";
constexpr std::string_view OS_TEST_MEMORY_PRESSURE_RANDOM_OOM =
    "随机 OOM 候选必须与独立分数和确定性排序 oracle 一致";

constexpr uint64_t OS_TEST_MEMORY_PRESSURE_RANDOM_SEED = 0x4D454D5052455353ULL;
constexpr uint64_t OS_TEST_MEMORY_PRESSURE_RANDOM_SHIFT_FIRST = 12ULL;
constexpr uint64_t OS_TEST_MEMORY_PRESSURE_RANDOM_SHIFT_SECOND = 25ULL;
constexpr uint64_t OS_TEST_MEMORY_PRESSURE_RANDOM_SHIFT_THIRD = 27ULL;
constexpr uint64_t OS_TEST_MEMORY_PRESSURE_RANDOM_MULTIPLIER = 0x2545F4914F6CDD1DULL;
constexpr uint64_t OS_TEST_MEMORY_PRESSURE_RANDOM_ITERATION_COUNT = 100000ULL;
constexpr uint64_t OS_TEST_MEMORY_PRESSURE_RANDOM_RESIDENT_LIMIT = 4096ULL;
constexpr uint64_t OS_TEST_MEMORY_PRESSURE_RANDOM_MANAGED_PAGES = 8192ULL;
constexpr uint64_t OS_TEST_MEMORY_PRESSURE_RANDOM_SWAP_PAGES = 2048ULL;
constexpr uint64_t OS_TEST_MEMORY_PRESSURE_RANDOM_COMMIT_LIMIT = 6144ULL;
constexpr uint64_t OS_TEST_MEMORY_PRESSURE_RANDOM_MAXIMUM_TRANSACTION_PAGES = 31ULL;
constexpr uint64_t OS_TEST_MEMORY_PRESSURE_RANDOM_OOM_CANDIDATE_COUNT = 32ULL;
constexpr uint64_t OS_TEST_MEMORY_PRESSURE_RANDOM_OOM_ROUND_COUNT = 4096ULL;
constexpr uint64_t OS_TEST_MEMORY_PRESSURE_RANDOM_OOM_ALLOWED_PAGES = 8192ULL;

[[nodiscard]] uint64_t NextRandom(uint64_t &state) noexcept {
    state ^= state >> OS_TEST_MEMORY_PRESSURE_RANDOM_SHIFT_FIRST;
    state ^= state << OS_TEST_MEMORY_PRESSURE_RANDOM_SHIFT_SECOND;
    state ^= state >> OS_TEST_MEMORY_PRESSURE_RANDOM_SHIFT_THIRD;
    state *= OS_TEST_MEMORY_PRESSURE_RANDOM_MULTIPLIER;
    return state;
}

[[nodiscard]] uint64_t OracleScore(const uint64_t memory_page_count,
                                   const int64_t adjustment) noexcept {
    uint64_t base_score =
        memory_page_count >= OS_TEST_MEMORY_PRESSURE_RANDOM_OOM_ALLOWED_PAGES
            ? os::kernel::OS_KERNEL_MEMORY_PRESSURE_OOM_BASE_SCORE_MAXIMUM
            : (memory_page_count * os::kernel::OS_KERNEL_MEMORY_PRESSURE_OOM_BASE_SCORE_MAXIMUM) /
                  OS_TEST_MEMORY_PRESSURE_RANDOM_OOM_ALLOWED_PAGES;
    const int64_t adjusted_score = static_cast<int64_t>(base_score) + adjustment;
    if (adjusted_score <= 0LL) {
        return 0ULL;
    }
    const uint64_t score = static_cast<uint64_t>(adjusted_score);
    return score > os::kernel::OS_KERNEL_MEMORY_PRESSURE_OOM_ADJUSTED_SCORE_MAXIMUM
               ? os::kernel::OS_KERNEL_MEMORY_PRESSURE_OOM_ADJUSTED_SCORE_MAXIMUM
               : score;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_MEMORY_PRESSURE_RANDOM_SUITE_NAME};
    uint64_t random_state = OS_TEST_MEMORY_PRESSURE_RANDOM_SEED;

    os::kernel::MemoryPressureController controller{};
    os::kernel::MemoryOvercommitAccountant accountant{};
    bool accounting_valid =
        controller.Initialize(os::kernel::MemoryPressureConfiguration{
            .managed_page_count = OS_TEST_MEMORY_PRESSURE_RANDOM_MANAGED_PAGES,
            .initial_resident_page_count = 0ULL,
            .resident_limit_page_count = OS_TEST_MEMORY_PRESSURE_RANDOM_RESIDENT_LIMIT,
            .swap_page_count = OS_TEST_MEMORY_PRESSURE_RANDOM_SWAP_PAGES,
            .watermark_scale_factor =
                os::kernel::OS_KERNEL_MEMORY_PRESSURE_DEFAULT_WATERMARK_SCALE_FACTOR,
            .swappiness = os::kernel::OS_KERNEL_MEMORY_PRESSURE_DEFAULT_SWAPPINESS,
        }) == os::kernel::MemoryPressureStatus::Succeeded &&
        accountant.Initialize(os::kernel::MemoryOvercommitConfiguration{
            .mode = os::kernel::MemoryOvercommitMode::Heuristic,
            .physical_page_count = OS_TEST_MEMORY_PRESSURE_RANDOM_RESIDENT_LIMIT,
            .swap_page_count = OS_TEST_MEMORY_PRESSURE_RANDOM_SWAP_PAGES,
            .overcommit_ratio_percent =
                os::kernel::OS_KERNEL_MEMORY_PRESSURE_DEFAULT_OVERCOMMIT_RATIO_PERCENT,
            .admin_reserve_page_count = 0ULL,
            .user_reserve_page_count = 0ULL,
        }) == os::kernel::MemoryOvercommitStatus::Succeeded;
    uint64_t resident_model = 0ULL;
    uint64_t committed_model = 0ULL;
    for (uint64_t iteration = 0ULL;
         accounting_valid && iteration < OS_TEST_MEMORY_PRESSURE_RANDOM_ITERATION_COUNT;
         ++iteration) {
        const uint64_t page_count =
            NextRandom(random_state) % OS_TEST_MEMORY_PRESSURE_RANDOM_MAXIMUM_TRANSACTION_PAGES +
            1ULL;
        const bool allocate = resident_model == 0ULL || (NextRandom(random_state) & 1ULL) == 0ULL;
        if (allocate) {
            os::kernel::MemoryAllocationDecision decision{};
            const os::kernel::MemoryPressureStatus prepare_status = controller.PrepareAllocation(
                page_count, os::kernel::MemoryAllocationClass::Reclaim, decision);
            const bool resident_fits =
                page_count <= OS_TEST_MEMORY_PRESSURE_RANDOM_RESIDENT_LIMIT - resident_model;
            accounting_valid =
                prepare_status == os::kernel::MemoryPressureStatus::Succeeded &&
                decision.action == (resident_fits ? os::kernel::MemoryAllocationAction::Allow
                                                  : os::kernel::MemoryAllocationAction::Reject);
            if (accounting_valid && resident_fits) {
                accounting_valid = controller.CommitAllocation(page_count) ==
                                   os::kernel::MemoryPressureStatus::Succeeded;
                resident_model += page_count;
            }

            const os::kernel::MemoryOvercommitStatus commit_status =
                accountant.TryCommit(page_count, false);
            const bool commit_fits =
                page_count <= OS_TEST_MEMORY_PRESSURE_RANDOM_COMMIT_LIMIT - committed_model;
            accounting_valid =
                accounting_valid &&
                commit_status == (commit_fits
                                      ? os::kernel::MemoryOvercommitStatus::Succeeded
                                      : os::kernel::MemoryOvercommitStatus::CommitLimitExceeded);
            if (commit_fits) {
                committed_model += page_count;
            }
        } else {
            const uint64_t released_page_count =
                page_count < resident_model ? page_count : resident_model;
            accounting_valid = controller.ReleaseResident(released_page_count) ==
                               os::kernel::MemoryPressureStatus::Succeeded;
            resident_model -= released_page_count;
            const uint64_t uncommitted_page_count =
                page_count < committed_model ? page_count : committed_model;
            if (uncommitted_page_count != 0ULL) {
                accounting_valid =
                    accounting_valid && accountant.Uncommit(uncommitted_page_count) ==
                                            os::kernel::MemoryOvercommitStatus::Succeeded;
                committed_model -= uncommitted_page_count;
            }
        }
        accounting_valid = accounting_valid &&
                           controller.Statistics().resident_page_count == resident_model &&
                           accountant.Statistics().committed_page_count == committed_model &&
                           controller.Validate() == os::kernel::MemoryPressureStatus::Succeeded &&
                           accountant.Validate() == os::kernel::MemoryOvercommitStatus::Succeeded;
    }
    test_context.ExpectRandom(accounting_valid, OS_TEST_MEMORY_PRESSURE_RANDOM_ACCOUNTING,
                              OS_TEST_MEMORY_PRESSURE_RANDOM_SEED,
                              OS_TEST_MEMORY_PRESSURE_RANDOM_ITERATION_COUNT);

    bool oom_valid = true;
    os::kernel::OomCandidate candidates[OS_TEST_MEMORY_PRESSURE_RANDOM_OOM_CANDIDATE_COUNT]{};
    for (uint64_t round = 0ULL; oom_valid && round < OS_TEST_MEMORY_PRESSURE_RANDOM_OOM_ROUND_COUNT;
         ++round) {
        bool oracle_found = false;
        os::kernel::OomVictim oracle{};
        for (uint64_t candidate_index = 0ULL;
             candidate_index < OS_TEST_MEMORY_PRESSURE_RANDOM_OOM_CANDIDATE_COUNT;
             ++candidate_index) {
            const uint64_t process_id = candidate_index + 1ULL;
            const uint64_t resident_page_count = NextRandom(random_state) % 12000ULL;
            const uint64_t swapped_page_count = NextRandom(random_state) % 2048ULL;
            const int64_t adjustment =
                static_cast<int64_t>(NextRandom(random_state) % 2001ULL) - 1000LL;
            const bool active = (NextRandom(random_state) & 3ULL) != 0ULL;
            const bool protected_process = (NextRandom(random_state) & 15ULL) == 0ULL;
            candidates[candidate_index] = os::kernel::OomCandidate{
                .process_id = process_id,
                .resident_page_count = resident_page_count,
                .swapped_page_count = swapped_page_count,
                .score_adjustment = adjustment,
                .protected_process = protected_process,
                .active = active,
            };
            const uint64_t memory_page_count = resident_page_count + swapped_page_count;
            if (!active || protected_process || process_id == 1ULL || adjustment == -1000LL ||
                memory_page_count == 0ULL) {
                continue;
            }
            const uint64_t score = OracleScore(memory_page_count, adjustment);
            if (!oracle_found || score > oracle.score ||
                (score == oracle.score && memory_page_count > oracle.memory_page_count) ||
                (score == oracle.score && memory_page_count == oracle.memory_page_count &&
                 process_id < oracle.process_id)) {
                oracle = os::kernel::OomVictim{
                    .process_id = process_id,
                    .memory_page_count = memory_page_count,
                    .score = score,
                    .candidate_index = candidate_index,
                };
                oracle_found = true;
            }
        }
        os::kernel::OomVictim selected{};
        const os::kernel::OomSelectionStatus selection_status = os::kernel::SelectOomVictim(
            candidates, OS_TEST_MEMORY_PRESSURE_RANDOM_OOM_CANDIDATE_COUNT,
            OS_TEST_MEMORY_PRESSURE_RANDOM_OOM_ALLOWED_PAGES, selected);
        oom_valid = oracle_found
                        ? selection_status == os::kernel::OomSelectionStatus::Succeeded &&
                              selected.process_id == oracle.process_id &&
                              selected.memory_page_count == oracle.memory_page_count &&
                              selected.score == oracle.score &&
                              selected.candidate_index == oracle.candidate_index
                        : selection_status == os::kernel::OomSelectionStatus::NoEligibleProcess;
    }
    test_context.ExpectRandom(oom_valid, OS_TEST_MEMORY_PRESSURE_RANDOM_OOM,
                              OS_TEST_MEMORY_PRESSURE_RANDOM_SEED,
                              OS_TEST_MEMORY_PRESSURE_RANDOM_OOM_ROUND_COUNT);

    return test_context.ExitCode();
}

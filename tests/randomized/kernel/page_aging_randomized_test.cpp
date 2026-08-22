#include <os/kernel/memory/page_aging.hpp>
#include <os/kernel/memory/physical_frame_allocator.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_PAGE_AGING_RANDOMIZED_SUITE_NAME =
    "kernel/page_aging/randomized";
constexpr std::string_view OS_TEST_PAGE_AGING_RANDOMIZED_INVARIANT =
    "十万步 alias 聚合、冷热转换、消失页清理和 reset 必须匹配参考模型";
constexpr uint64_t OS_TEST_PAGE_AGING_RANDOMIZED_SEED = 0x5632394147494E47ULL;
constexpr uint64_t OS_TEST_PAGE_AGING_RANDOMIZED_MULTIPLIER = 6364136223846793005ULL;
constexpr uint64_t OS_TEST_PAGE_AGING_RANDOMIZED_INCREMENT = 1442695040888963407ULL;
constexpr uint64_t OS_TEST_PAGE_AGING_RANDOMIZED_CAPACITY = 32ULL;
constexpr uint64_t OS_TEST_PAGE_AGING_RANDOMIZED_HASH_CAPACITY = 64ULL;
constexpr uint64_t OS_TEST_PAGE_AGING_RANDOMIZED_IDENTITY_COUNT = 24ULL;
constexpr uint64_t OS_TEST_PAGE_AGING_RANDOMIZED_ROUND_COUNT = 2000ULL;
constexpr uint64_t OS_TEST_PAGE_AGING_RANDOMIZED_OBSERVATIONS_PER_ROUND = 50ULL;
constexpr uint64_t OS_TEST_PAGE_AGING_RANDOMIZED_RESET_INTERVAL = 37ULL;
constexpr uint64_t OS_TEST_PAGE_AGING_RANDOMIZED_ACCESSED_MASK = 0x100ULL;
constexpr uint64_t OS_TEST_PAGE_AGING_RANDOMIZED_ELIGIBLE_MASK = 0x200ULL;
constexpr uint64_t OS_TEST_PAGE_AGING_RANDOMIZED_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_PAGE_AGING_RANDOMIZED_SINGLE_UNIT = 1ULL;
constexpr uint64_t OS_TEST_PAGE_AGING_RANDOMIZED_VALIDATE_STAGE = 1ULL;
constexpr uint64_t OS_TEST_PAGE_AGING_RANDOMIZED_MISSING_STAGE_BASE = 100ULL;
constexpr uint64_t OS_TEST_PAGE_AGING_RANDOMIZED_ENTRY_STAGE_BASE = 200ULL;
constexpr uint64_t OS_TEST_PAGE_AGING_RANDOMIZED_ACTIVE_FILE_STAGE = 300ULL;
constexpr uint64_t OS_TEST_PAGE_AGING_RANDOMIZED_INACTIVE_FILE_STAGE = 301ULL;
constexpr uint64_t OS_TEST_PAGE_AGING_RANDOMIZED_ACTIVE_ANONYMOUS_STAGE = 302ULL;
constexpr uint64_t OS_TEST_PAGE_AGING_RANDOMIZED_INACTIVE_ANONYMOUS_STAGE = 303ULL;
constexpr uint64_t OS_TEST_PAGE_AGING_RANDOMIZED_CANDIDATE_STAGE = 304ULL;
constexpr uint64_t OS_TEST_PAGE_AGING_RANDOMIZED_TRACKED_STAGE = 305ULL;
constexpr uint64_t OS_TEST_PAGE_AGING_RANDOMIZED_ROUND_STAGE_STRIDE = 1000ULL;

struct ReferenceEntry final {
    os::kernel::PageAgingKind kind;
    os::kernel::PageAgingState state;
    uint64_t alias_observation_count;
    bool observed;
    bool referenced;
    bool reclaim_eligible;
    bool reclaim_candidate;
    bool tracked;
};

struct RandomizedModel final {
    os::kernel::PageAgingEntry entries[OS_TEST_PAGE_AGING_RANDOMIZED_CAPACITY];
    uint64_t hash[OS_TEST_PAGE_AGING_RANDOMIZED_HASH_CAPACITY];
    ReferenceEntry reference[OS_TEST_PAGE_AGING_RANDOMIZED_IDENTITY_COUNT];
    os::kernel::PageAgingManager manager;
};

[[nodiscard]] uint64_t NextRandom(uint64_t &state) noexcept {
    state =
        state * OS_TEST_PAGE_AGING_RANDOMIZED_MULTIPLIER + OS_TEST_PAGE_AGING_RANDOMIZED_INCREMENT;
    return state;
}

[[nodiscard]] uint64_t PhysicalAddress(const uint64_t identity_index) noexcept {
    return (identity_index + OS_TEST_PAGE_AGING_RANDOMIZED_SINGLE_UNIT) *
           os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
}

[[nodiscard]] os::kernel::PageAgingKind Kind(const uint64_t identity_index) noexcept {
    return (identity_index & OS_TEST_PAGE_AGING_RANDOMIZED_SINGLE_UNIT) ==
                   OS_TEST_PAGE_AGING_RANDOMIZED_EMPTY_VALUE
               ? os::kernel::PageAgingKind::File
               : os::kernel::PageAgingKind::Anonymous;
}

[[nodiscard]] bool InitializeModel(RandomizedModel &model) noexcept {
    if (model.manager.Initialize(model.entries, OS_TEST_PAGE_AGING_RANDOMIZED_CAPACITY, model.hash,
                                 OS_TEST_PAGE_AGING_RANDOMIZED_HASH_CAPACITY) !=
        os::kernel::PageAgingStatus::Succeeded) {
        return false;
    }
    for (uint64_t identity_index = OS_TEST_PAGE_AGING_RANDOMIZED_EMPTY_VALUE;
         identity_index < OS_TEST_PAGE_AGING_RANDOMIZED_IDENTITY_COUNT; ++identity_index) {
        model.reference[identity_index] = ReferenceEntry{
            .kind = Kind(identity_index),
            .state = os::kernel::PageAgingState::None,
            .alias_observation_count = OS_TEST_PAGE_AGING_RANDOMIZED_EMPTY_VALUE,
            .observed = false,
            .referenced = false,
            .reclaim_eligible = false,
            .reclaim_candidate = false,
            .tracked = false,
        };
    }
    return true;
}

void BeginReferenceRound(RandomizedModel &model) noexcept {
    for (ReferenceEntry &entry : model.reference) {
        entry.observed = false;
        entry.alias_observation_count = OS_TEST_PAGE_AGING_RANDOMIZED_EMPTY_VALUE;
        entry.referenced = false;
        entry.reclaim_eligible = false;
    }
}

[[nodiscard]] bool Observe(RandomizedModel &model, const uint64_t random_value) noexcept {
    const uint64_t identity_index = random_value % OS_TEST_PAGE_AGING_RANDOMIZED_IDENTITY_COUNT;
    const bool accessed = (random_value & OS_TEST_PAGE_AGING_RANDOMIZED_ACCESSED_MASK) !=
                          OS_TEST_PAGE_AGING_RANDOMIZED_EMPTY_VALUE;
    const bool reclaim_eligible = (random_value & OS_TEST_PAGE_AGING_RANDOMIZED_ELIGIBLE_MASK) !=
                                  OS_TEST_PAGE_AGING_RANDOMIZED_EMPTY_VALUE;
    ReferenceEntry &entry = model.reference[identity_index];
    if (model.manager.Observe(PhysicalAddress(identity_index), entry.kind, accessed,
                              reclaim_eligible) != os::kernel::PageAgingStatus::Succeeded) {
        return false;
    }
    if (!entry.tracked) {
        entry.tracked = true;
        entry.state = os::kernel::PageAgingState::Active;
    }
    if (!entry.observed) {
        entry.observed = true;
        entry.alias_observation_count = OS_TEST_PAGE_AGING_RANDOMIZED_SINGLE_UNIT;
        entry.referenced = accessed;
        entry.reclaim_eligible = reclaim_eligible;
    } else {
        ++entry.alias_observation_count;
        entry.referenced = entry.referenced || accessed;
        entry.reclaim_eligible = entry.reclaim_eligible && reclaim_eligible;
    }
    return true;
}

void EndReferenceRound(RandomizedModel &model) noexcept {
    for (ReferenceEntry &entry : model.reference) {
        if (!entry.tracked) {
            continue;
        }
        if (!entry.observed) {
            entry.tracked = false;
            entry.state = os::kernel::PageAgingState::None;
            entry.reclaim_candidate = false;
            continue;
        }
        if (entry.referenced) {
            entry.state = os::kernel::PageAgingState::Active;
            entry.reclaim_candidate = false;
        } else if (entry.state == os::kernel::PageAgingState::Active) {
            entry.state = os::kernel::PageAgingState::Inactive;
            entry.reclaim_candidate = false;
        } else {
            entry.reclaim_candidate = entry.reclaim_eligible;
        }
    }
}

[[nodiscard]] bool ValidateModel(const RandomizedModel &model, uint64_t &failure_stage) noexcept {
    failure_stage = OS_TEST_PAGE_AGING_RANDOMIZED_EMPTY_VALUE;
    if (model.manager.Validate() != os::kernel::PageAgingStatus::Succeeded) {
        failure_stage = OS_TEST_PAGE_AGING_RANDOMIZED_VALIDATE_STAGE;
        return false;
    }
    uint64_t active_file_count = OS_TEST_PAGE_AGING_RANDOMIZED_EMPTY_VALUE;
    uint64_t inactive_file_count = OS_TEST_PAGE_AGING_RANDOMIZED_EMPTY_VALUE;
    uint64_t active_anonymous_count = OS_TEST_PAGE_AGING_RANDOMIZED_EMPTY_VALUE;
    uint64_t inactive_anonymous_count = OS_TEST_PAGE_AGING_RANDOMIZED_EMPTY_VALUE;
    uint64_t reclaim_candidate_count = OS_TEST_PAGE_AGING_RANDOMIZED_EMPTY_VALUE;
    uint64_t file_reclaim_candidate_count = OS_TEST_PAGE_AGING_RANDOMIZED_EMPTY_VALUE;
    uint64_t anonymous_reclaim_candidate_count = OS_TEST_PAGE_AGING_RANDOMIZED_EMPTY_VALUE;
    for (uint64_t identity_index = OS_TEST_PAGE_AGING_RANDOMIZED_EMPTY_VALUE;
         identity_index < OS_TEST_PAGE_AGING_RANDOMIZED_IDENTITY_COUNT; ++identity_index) {
        const ReferenceEntry &expected = model.reference[identity_index];
        os::kernel::PageAgingEntrySnapshot actual{};
        const os::kernel::PageAgingStatus read_status =
            model.manager.Read(PhysicalAddress(identity_index), expected.kind, actual);
        if (!expected.tracked) {
            if (read_status != os::kernel::PageAgingStatus::EntryNotFound) {
                failure_stage = OS_TEST_PAGE_AGING_RANDOMIZED_MISSING_STAGE_BASE + identity_index;
                return false;
            }
            continue;
        }
        if (read_status != os::kernel::PageAgingStatus::Succeeded || actual.kind != expected.kind ||
            actual.state != expected.state ||
            actual.alias_observation_count != expected.alias_observation_count ||
            actual.referenced != expected.referenced ||
            actual.reclaim_eligible != expected.reclaim_eligible ||
            actual.reclaim_candidate != expected.reclaim_candidate) {
            failure_stage = OS_TEST_PAGE_AGING_RANDOMIZED_ENTRY_STAGE_BASE + identity_index;
            return false;
        }
        if (expected.kind == os::kernel::PageAgingKind::File) {
            if (expected.state == os::kernel::PageAgingState::Active) {
                ++active_file_count;
            } else {
                ++inactive_file_count;
            }
        } else if (expected.state == os::kernel::PageAgingState::Active) {
            ++active_anonymous_count;
        } else {
            ++inactive_anonymous_count;
        }
        if (expected.reclaim_candidate) {
            ++reclaim_candidate_count;
            if (expected.kind == os::kernel::PageAgingKind::File) {
                ++file_reclaim_candidate_count;
            } else {
                ++anonymous_reclaim_candidate_count;
            }
        }
    }
    const os::kernel::PageAgingStatistics statistics = model.manager.Statistics();
    if (statistics.active_file_page_count != active_file_count) {
        failure_stage = OS_TEST_PAGE_AGING_RANDOMIZED_ACTIVE_FILE_STAGE;
        return false;
    }
    if (statistics.inactive_file_page_count != inactive_file_count) {
        failure_stage = OS_TEST_PAGE_AGING_RANDOMIZED_INACTIVE_FILE_STAGE;
        return false;
    }
    if (statistics.active_anonymous_page_count != active_anonymous_count) {
        failure_stage = OS_TEST_PAGE_AGING_RANDOMIZED_ACTIVE_ANONYMOUS_STAGE;
        return false;
    }
    if (statistics.inactive_anonymous_page_count != inactive_anonymous_count) {
        failure_stage = OS_TEST_PAGE_AGING_RANDOMIZED_INACTIVE_ANONYMOUS_STAGE;
        return false;
    }
    if (statistics.reclaim_candidate_count != reclaim_candidate_count ||
        statistics.file_reclaim_candidate_count != file_reclaim_candidate_count ||
        statistics.anonymous_reclaim_candidate_count != anonymous_reclaim_candidate_count) {
        failure_stage = OS_TEST_PAGE_AGING_RANDOMIZED_CANDIDATE_STAGE;
        return false;
    }
    if (statistics.tracked_page_count != active_file_count + inactive_file_count +
                                             active_anonymous_count + inactive_anonymous_count) {
        failure_stage = OS_TEST_PAGE_AGING_RANDOMIZED_TRACKED_STAGE;
        return false;
    }
    return true;
}

void ResetReference(RandomizedModel &model) noexcept {
    for (ReferenceEntry &entry : model.reference) {
        entry.state = os::kernel::PageAgingState::None;
        entry.alias_observation_count = OS_TEST_PAGE_AGING_RANDOMIZED_EMPTY_VALUE;
        entry.observed = false;
        entry.referenced = false;
        entry.reclaim_eligible = false;
        entry.reclaim_candidate = false;
        entry.tracked = false;
    }
}

[[nodiscard]] bool RunRandomizedModel(uint64_t &failure_round) noexcept {
    RandomizedModel model{};
    if (!InitializeModel(model)) {
        failure_round = OS_TEST_PAGE_AGING_RANDOMIZED_EMPTY_VALUE;
        return false;
    }
    uint64_t random_state = OS_TEST_PAGE_AGING_RANDOMIZED_SEED;
    uint64_t failure_stage = OS_TEST_PAGE_AGING_RANDOMIZED_EMPTY_VALUE;
    for (uint64_t round = OS_TEST_PAGE_AGING_RANDOMIZED_EMPTY_VALUE;
         round < OS_TEST_PAGE_AGING_RANDOMIZED_ROUND_COUNT; ++round) {
        BeginReferenceRound(model);
        if (model.manager.BeginObservation() != os::kernel::PageAgingStatus::Succeeded) {
            failure_round = round;
            return false;
        }
        for (uint64_t observation = OS_TEST_PAGE_AGING_RANDOMIZED_EMPTY_VALUE;
             observation < OS_TEST_PAGE_AGING_RANDOMIZED_OBSERVATIONS_PER_ROUND; ++observation) {
            if (!Observe(model, NextRandom(random_state))) {
                failure_round = round;
                return false;
            }
        }
        if (model.manager.EndObservation() != os::kernel::PageAgingStatus::Succeeded) {
            failure_round = round;
            return false;
        }
        EndReferenceRound(model);
        if (!ValidateModel(model, failure_stage)) {
            failure_round =
                round * OS_TEST_PAGE_AGING_RANDOMIZED_ROUND_STAGE_STRIDE + failure_stage;
            return false;
        }
        if ((round + OS_TEST_PAGE_AGING_RANDOMIZED_SINGLE_UNIT) %
                OS_TEST_PAGE_AGING_RANDOMIZED_RESET_INTERVAL ==
            OS_TEST_PAGE_AGING_RANDOMIZED_EMPTY_VALUE) {
            if (model.manager.Reset() != os::kernel::PageAgingStatus::Succeeded) {
                failure_round = round;
                return false;
            }
            ResetReference(model);
        }
    }
    failure_round = OS_TEST_PAGE_AGING_RANDOMIZED_ROUND_COUNT;
    if (model.manager.Reset() != os::kernel::PageAgingStatus::Succeeded) {
        return false;
    }
    ResetReference(model);
    return ValidateModel(model, failure_stage);
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_PAGE_AGING_RANDOMIZED_SUITE_NAME};
    uint64_t failure_round = OS_TEST_PAGE_AGING_RANDOMIZED_EMPTY_VALUE;
    const bool invariant_held = RunRandomizedModel(failure_round);
    test_context.ExpectRandom(invariant_held, OS_TEST_PAGE_AGING_RANDOMIZED_INVARIANT,
                              OS_TEST_PAGE_AGING_RANDOMIZED_SEED, failure_round);
    return test_context.ExitCode();
}

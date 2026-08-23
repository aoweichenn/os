#include <os/kernel/memory/file_readahead_feedback.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_SUITE_NAME =
    "kernel/file_readahead_feedback/randomized";
constexpr std::string_view OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_INVARIANT =
    "固定种子十万步 register、task、feedback、retire、stale 和复用必须保持账本守恒";
constexpr uint64_t OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_SEED = 0x524146454544424BULL;
constexpr uint64_t OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_STEP_COUNT = 100000ULL;
constexpr uint64_t OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_MULTIPLIER = 6364136223846793005ULL;
constexpr uint64_t OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_INCREMENT = 1442695040888963407ULL;
constexpr uint64_t OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_CAPACITY = 17ULL;
constexpr uint64_t OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_OPERATION_COUNT = 7ULL;
constexpr uint64_t OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_REGISTER_OPERATION = 0ULL;
constexpr uint64_t OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_RETAIN_OPERATION = 1ULL;
constexpr uint64_t OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_RECORD_OPERATION = 2ULL;
constexpr uint64_t OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_TAKE_OPERATION = 3ULL;
constexpr uint64_t OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_RETIRE_OPERATION = 4ULL;
constexpr uint64_t OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_RELEASE_OPERATION = 5ULL;
constexpr uint64_t OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_FIRST_VALUE = 1ULL;
constexpr uint64_t OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_FEEDBACK_LIMIT = 5ULL;

struct ReferenceSlot final {
    os::kernel::FileReadaheadStreamToken token;
    os::kernel::FileReadaheadFeedback feedback;
    uint64_t active_task_count;
    os::kernel::FileReadaheadFeedbackSlotState state;
};

struct RandomizedModel final {
    os::kernel::FileReadaheadFeedbackSlot
        slots[OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_CAPACITY];
    ReferenceSlot reference[OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_CAPACITY];
    os::kernel::FileReadaheadFeedbackLedger ledger;
    uint64_t next_node_identifier;
};

[[nodiscard]] uint64_t NextRandom(uint64_t &state) noexcept {
    state = state * OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_MULTIPLIER +
            OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_INCREMENT;
    return state;
}

[[nodiscard]] os::kernel::FileCacheIdentity MakeIdentity(const uint64_t identifier) noexcept {
    return os::kernel::FileCacheIdentity{
        .superblock_identifier = 23ULL,
        .superblock_generation = 3ULL,
        .node_identifier = identifier,
        .node_generation = 11ULL,
    };
}

[[nodiscard]] uint64_t FindSlot(const RandomizedModel &model,
                                const os::kernel::FileReadaheadFeedbackSlotState state,
                                uint64_t ordinal, const bool require_task) noexcept {
    for (uint64_t slot_index = OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_EMPTY_VALUE;
         slot_index < OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_CAPACITY; ++slot_index) {
        const ReferenceSlot &slot = model.reference[slot_index];
        if (slot.state != state ||
            (require_task &&
             slot.active_task_count == OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_EMPTY_VALUE)) {
            continue;
        }
        if (ordinal == OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_EMPTY_VALUE) {
            return slot_index;
        }
        --ordinal;
    }
    return os::kernel::OS_KERNEL_FILE_READAHEAD_STREAM_INVALID_SLOT_INDEX;
}

[[nodiscard]] uint64_t CountSlots(const RandomizedModel &model,
                                  const os::kernel::FileReadaheadFeedbackSlotState state,
                                  const bool require_task = false) noexcept {
    uint64_t count = OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_EMPTY_VALUE;
    for (const ReferenceSlot &slot : model.reference) {
        if (slot.state == state &&
            (!require_task ||
             slot.active_task_count != OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_EMPTY_VALUE)) {
            ++count;
        }
    }
    return count;
}

[[nodiscard]] bool Register(RandomizedModel &model) noexcept {
    os::kernel::FileReadaheadStreamToken token{};
    const os::kernel::FileReadaheadFeedbackStatus status =
        model.ledger.RegisterStream(MakeIdentity(model.next_node_identifier++), token);
    const uint64_t free_count = CountSlots(model, os::kernel::FileReadaheadFeedbackSlotState::Free);
    if (free_count == OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_EMPTY_VALUE) {
        return status == os::kernel::FileReadaheadFeedbackStatus::CapacityExhausted;
    }
    if (status != os::kernel::FileReadaheadFeedbackStatus::Succeeded ||
        token.slot_index >= OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_CAPACITY ||
        model.reference[token.slot_index].state !=
            os::kernel::FileReadaheadFeedbackSlotState::Free ||
        token.generation <= model.reference[token.slot_index].token.generation) {
        return false;
    }
    model.reference[token.slot_index] = ReferenceSlot{
        .token = token,
        .feedback = os::kernel::FileReadaheadFeedback{},
        .active_task_count = OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_EMPTY_VALUE,
        .state = os::kernel::FileReadaheadFeedbackSlotState::Active,
    };
    return true;
}

[[nodiscard]] bool Retain(RandomizedModel &model, uint64_t &random_state) noexcept {
    const uint64_t count = CountSlots(model, os::kernel::FileReadaheadFeedbackSlotState::Active);
    if (count == OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_EMPTY_VALUE) {
        return true;
    }
    const uint64_t slot_index = FindSlot(model, os::kernel::FileReadaheadFeedbackSlotState::Active,
                                         NextRandom(random_state) % count, false);
    ReferenceSlot &slot = model.reference[slot_index];
    if (model.ledger.RetainTask(slot.token) != os::kernel::FileReadaheadFeedbackStatus::Succeeded) {
        return false;
    }
    ++slot.active_task_count;
    return true;
}

[[nodiscard]] bool Record(RandomizedModel &model, uint64_t &random_state) noexcept {
    const uint64_t active_count =
        CountSlots(model, os::kernel::FileReadaheadFeedbackSlotState::Active);
    const uint64_t retiring_count =
        CountSlots(model, os::kernel::FileReadaheadFeedbackSlotState::Retiring);
    const uint64_t count = active_count + retiring_count;
    if (count == OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_EMPTY_VALUE) {
        return true;
    }
    const uint64_t ordinal = NextRandom(random_state) % count;
    const os::kernel::FileReadaheadFeedbackSlotState state =
        ordinal < active_count ? os::kernel::FileReadaheadFeedbackSlotState::Active
                               : os::kernel::FileReadaheadFeedbackSlotState::Retiring;
    const uint64_t state_ordinal = ordinal < active_count ? ordinal : ordinal - active_count;
    ReferenceSlot &slot = model.reference[FindSlot(model, state, state_ordinal, false)];
    const os::kernel::FileReadaheadFeedback feedback{
        .useful_page_count =
            NextRandom(random_state) % OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_FEEDBACK_LIMIT,
        .wasted_page_count =
            NextRandom(random_state) % OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_FEEDBACK_LIMIT +
            OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_FIRST_VALUE,
    };
    if (model.ledger.Record(slot.token, feedback) !=
        os::kernel::FileReadaheadFeedbackStatus::Succeeded) {
        return false;
    }
    slot.feedback.useful_page_count += feedback.useful_page_count;
    slot.feedback.wasted_page_count += feedback.wasted_page_count;
    return true;
}

[[nodiscard]] bool Take(RandomizedModel &model, uint64_t &random_state) noexcept {
    const uint64_t count = CountSlots(model, os::kernel::FileReadaheadFeedbackSlotState::Active);
    if (count == OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_EMPTY_VALUE) {
        return true;
    }
    ReferenceSlot &slot =
        model.reference[FindSlot(model, os::kernel::FileReadaheadFeedbackSlotState::Active,
                                 NextRandom(random_state) % count, false)];
    os::kernel::FileReadaheadFeedback feedback{};
    if (model.ledger.Take(slot.token, feedback) !=
            os::kernel::FileReadaheadFeedbackStatus::Succeeded ||
        feedback.useful_page_count != slot.feedback.useful_page_count ||
        feedback.wasted_page_count != slot.feedback.wasted_page_count) {
        return false;
    }
    slot.feedback = os::kernel::FileReadaheadFeedback{};
    return true;
}

[[nodiscard]] bool Retire(RandomizedModel &model, uint64_t &random_state) noexcept {
    const uint64_t count = CountSlots(model, os::kernel::FileReadaheadFeedbackSlotState::Active);
    if (count == OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_EMPTY_VALUE) {
        return true;
    }
    ReferenceSlot &slot =
        model.reference[FindSlot(model, os::kernel::FileReadaheadFeedbackSlotState::Active,
                                 NextRandom(random_state) % count, false)];
    if (model.ledger.RetireStream(slot.token) !=
        os::kernel::FileReadaheadFeedbackStatus::Succeeded) {
        return false;
    }
    if (slot.active_task_count == OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_EMPTY_VALUE) {
        const os::kernel::FileReadaheadStreamToken token = slot.token;
        slot = ReferenceSlot{};
        slot.token = token;
    } else {
        slot.state = os::kernel::FileReadaheadFeedbackSlotState::Retiring;
    }
    return true;
}

[[nodiscard]] bool Release(RandomizedModel &model, uint64_t &random_state) noexcept {
    const uint64_t active_count =
        CountSlots(model, os::kernel::FileReadaheadFeedbackSlotState::Active, true);
    const uint64_t retiring_count =
        CountSlots(model, os::kernel::FileReadaheadFeedbackSlotState::Retiring, true);
    const uint64_t count = active_count + retiring_count;
    if (count == OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_EMPTY_VALUE) {
        return true;
    }
    const uint64_t ordinal = NextRandom(random_state) % count;
    const os::kernel::FileReadaheadFeedbackSlotState state =
        ordinal < active_count ? os::kernel::FileReadaheadFeedbackSlotState::Active
                               : os::kernel::FileReadaheadFeedbackSlotState::Retiring;
    ReferenceSlot &slot = model.reference[FindSlot(
        model, state, ordinal < active_count ? ordinal : ordinal - active_count, true)];
    if (model.ledger.ReleaseTask(slot.token) !=
        os::kernel::FileReadaheadFeedbackStatus::Succeeded) {
        return false;
    }
    --slot.active_task_count;
    if (slot.state == os::kernel::FileReadaheadFeedbackSlotState::Retiring &&
        slot.active_task_count == OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_EMPTY_VALUE) {
        const os::kernel::FileReadaheadStreamToken token = slot.token;
        slot = ReferenceSlot{};
        slot.token = token;
    }
    return true;
}

[[nodiscard]] bool RecordStale(RandomizedModel &model, uint64_t &random_state) noexcept {
    const uint64_t count = CountSlots(model, os::kernel::FileReadaheadFeedbackSlotState::Free);
    if (count == OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_EMPTY_VALUE) {
        return true;
    }
    const ReferenceSlot &slot =
        model.reference[FindSlot(model, os::kernel::FileReadaheadFeedbackSlotState::Free,
                                 NextRandom(random_state) % count, false)];
    const os::kernel::FileReadaheadStreamToken stale{
        .slot_index = slot.token.slot_index,
        .generation =
            slot.token.generation == OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_EMPTY_VALUE
                ? UINT64_MAX
                : slot.token.generation + OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_FIRST_VALUE,
    };
    return model.ledger.Record(
               stale,
               os::kernel::FileReadaheadFeedback{
                   .useful_page_count = OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_FIRST_VALUE,
                   .wasted_page_count = OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_FIRST_VALUE,
               }) == os::kernel::FileReadaheadFeedbackStatus::Succeeded;
}

[[nodiscard]] bool RunScenario() noexcept {
    RandomizedModel model{};
    model.next_node_identifier = OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_FIRST_VALUE;
    for (uint64_t slot_index = OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_EMPTY_VALUE;
         slot_index < OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_CAPACITY; ++slot_index) {
        model.reference[slot_index].token.slot_index = slot_index;
    }
    if (model.ledger.Initialize(model.slots, OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_CAPACITY) !=
        os::kernel::FileReadaheadFeedbackStatus::Succeeded) {
        return false;
    }
    uint64_t random_state = OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_SEED;
    for (uint64_t step = OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_EMPTY_VALUE;
         step < OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_STEP_COUNT; ++step) {
        const uint64_t operation =
            NextRandom(random_state) % OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_OPERATION_COUNT;
        const bool succeeded =
            operation == OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_REGISTER_OPERATION
                ? Register(model)
            : operation == OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_RETAIN_OPERATION
                ? Retain(model, random_state)
            : operation == OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_RECORD_OPERATION
                ? Record(model, random_state)
            : operation == OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_TAKE_OPERATION
                ? Take(model, random_state)
            : operation == OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_RETIRE_OPERATION
                ? Retire(model, random_state)
            : operation == OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_RELEASE_OPERATION
                ? Release(model, random_state)
                : RecordStale(model, random_state);
        if (!succeeded ||
            model.ledger.Validate() != os::kernel::FileReadaheadFeedbackStatus::Succeeded) {
            return false;
        }
    }
    for (uint64_t slot_index = OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_EMPTY_VALUE;
         slot_index < OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_CAPACITY; ++slot_index) {
        ReferenceSlot &slot = model.reference[slot_index];
        if (slot.state == os::kernel::FileReadaheadFeedbackSlotState::Active &&
            model.ledger.RetireStream(slot.token) !=
                os::kernel::FileReadaheadFeedbackStatus::Succeeded) {
            return false;
        }
        while (slot.active_task_count != OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_EMPTY_VALUE) {
            if (model.ledger.ReleaseTask(slot.token) !=
                os::kernel::FileReadaheadFeedbackStatus::Succeeded) {
                return false;
            }
            --slot.active_task_count;
        }
    }
    const os::kernel::FileReadaheadFeedbackStatistics statistics = model.ledger.Statistics();
    return statistics.active_stream_count ==
               OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_EMPTY_VALUE &&
           statistics.retiring_stream_count ==
               OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_EMPTY_VALUE &&
           statistics.active_task_count == OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_EMPTY_VALUE &&
           model.ledger.Validate() == os::kernel::FileReadaheadFeedbackStatus::Succeeded;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_SUITE_NAME};
    test_context.Expect(RunScenario(), OS_TEST_FILE_READAHEAD_FEEDBACK_RANDOMIZED_INVARIANT);
    return test_context.ExitCode();
}

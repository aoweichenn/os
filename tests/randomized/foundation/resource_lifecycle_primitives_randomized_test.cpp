#include "os/foundation/reference_counter.hpp"
#include "os/foundation/scope_rollback.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_RESOURCE_RANDOM_SUITE_NAME =
    "foundation/resource_lifecycle_primitives/randomized";
constexpr std::string_view OS_TEST_RESOURCE_RANDOM_REFERENCE_MODEL =
    "十万步引用操作必须与独立整数生命周期模型一致";
constexpr std::string_view OS_TEST_RESOURCE_RANDOM_ROLLBACK_MODEL =
    "十万组作用域动作必须完整遵守逆序、提交和失败汇总模型";

constexpr os::test::RandomSeed OS_TEST_RESOURCE_RANDOM_SEED =
    os::test::RandomSeed{0x5245534F55524345ULL};
constexpr os::test::TestCount OS_TEST_RESOURCE_RANDOM_CASE_COUNT =
    os::test::TestCount{100000ULL};
constexpr uint64_t OS_TEST_RESOURCE_RANDOM_MULTIPLIER = 6364136223846793005ULL;
constexpr uint64_t OS_TEST_RESOURCE_RANDOM_INCREMENT = 1442695040888963407ULL;
constexpr uint64_t OS_TEST_RESOURCE_RANDOM_EMPTY_COUNT = 0ULL;
constexpr uint64_t OS_TEST_RESOURCE_RANDOM_SINGLE_UNIT = 1ULL;
constexpr uint64_t OS_TEST_RESOURCE_RANDOM_REFERENCE_OPERATION_COUNT = 3ULL;
constexpr uint64_t OS_TEST_RESOURCE_RANDOM_INITIAL_REFERENCE_LIMIT = 16ULL;
constexpr uint64_t OS_TEST_RESOURCE_RANDOM_ROLLBACK_ACTION_CAPACITY = 8ULL;

struct RollbackTrace final {
    uint64_t values[OS_TEST_RESOURCE_RANDOM_ROLLBACK_ACTION_CAPACITY];
    uint64_t value_count;
};

struct RollbackContext final {
    RollbackTrace *trace;
    uint64_t value;
    bool succeed;
};

[[nodiscard]] uint64_t NextRandom(uint64_t &random_state) noexcept {
    random_state =
        random_state * OS_TEST_RESOURCE_RANDOM_MULTIPLIER +
        OS_TEST_RESOURCE_RANDOM_INCREMENT;
    return random_state;
}

[[nodiscard]] bool RecordRollback(void *const raw_context) noexcept {
    if (raw_context == nullptr) {
        return false;
    }
    RollbackContext *const context = static_cast<RollbackContext *>(raw_context);
    if (context->trace == nullptr ||
        context->trace->value_count >=
            OS_TEST_RESOURCE_RANDOM_ROLLBACK_ACTION_CAPACITY) {
        return false;
    }
    context->trace->values[context->trace->value_count] = context->value;
    ++context->trace->value_count;
    return context->succeed;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_RESOURCE_RANDOM_SUITE_NAME};
    uint64_t random_state = OS_TEST_RESOURCE_RANDOM_SEED;
    os::foundation::ReferenceCounter reference_counter{};
    uint64_t model_reference_count = OS_TEST_RESOURCE_RANDOM_EMPTY_COUNT;

    for (os::test::TestCount iteration = OS_TEST_RESOURCE_RANDOM_EMPTY_COUNT;
         iteration < OS_TEST_RESOURCE_RANDOM_CASE_COUNT; ++iteration) {
        const uint64_t operation =
            NextRandom(random_state) %
            OS_TEST_RESOURCE_RANDOM_REFERENCE_OPERATION_COUNT;
        bool reference_step_valid = true;
        if (operation == OS_TEST_RESOURCE_RANDOM_EMPTY_COUNT) {
            const uint64_t initial_reference_count =
                NextRandom(random_state) %
                    OS_TEST_RESOURCE_RANDOM_INITIAL_REFERENCE_LIMIT +
                OS_TEST_RESOURCE_RANDOM_SINGLE_UNIT;
            const os::foundation::ReferenceCounterStatus status =
                reference_counter.Start(initial_reference_count);
            if (model_reference_count == OS_TEST_RESOURCE_RANDOM_EMPTY_COUNT) {
                reference_step_valid =
                    status ==
                    os::foundation::ReferenceCounterStatus::Succeeded;
                model_reference_count = initial_reference_count;
            } else {
                reference_step_valid =
                    status ==
                    os::foundation::ReferenceCounterStatus::ActiveReferencesRemain;
            }
        } else if (operation == OS_TEST_RESOURCE_RANDOM_SINGLE_UNIT) {
            const os::foundation::ReferenceCounterStatus status =
                reference_counter.TryAcquire();
            if (model_reference_count == OS_TEST_RESOURCE_RANDOM_EMPTY_COUNT) {
                reference_step_valid =
                    status ==
                    os::foundation::ReferenceCounterStatus::ReferenceUnavailable;
            } else {
                reference_step_valid =
                    status ==
                    os::foundation::ReferenceCounterStatus::Succeeded;
                ++model_reference_count;
            }
        } else {
            bool released_last_reference =
                (NextRandom(random_state) & OS_TEST_RESOURCE_RANDOM_SINGLE_UNIT) !=
                OS_TEST_RESOURCE_RANDOM_EMPTY_COUNT;
            const bool output_before_failure = released_last_reference;
            const os::foundation::ReferenceCounterStatus status =
                reference_counter.TryRelease(released_last_reference);
            if (model_reference_count == OS_TEST_RESOURCE_RANDOM_EMPTY_COUNT) {
                reference_step_valid =
                    status ==
                        os::foundation::ReferenceCounterStatus::ReferenceUnavailable &&
                    released_last_reference == output_before_failure;
            } else {
                --model_reference_count;
                reference_step_valid =
                    status ==
                        os::foundation::ReferenceCounterStatus::Succeeded &&
                    released_last_reference ==
                        (model_reference_count ==
                         OS_TEST_RESOURCE_RANDOM_EMPTY_COUNT);
            }
        }
        reference_step_valid =
            reference_step_valid &&
            reference_counter.Count() == model_reference_count &&
            reference_counter.IsActive() ==
                (model_reference_count != OS_TEST_RESOURCE_RANDOM_EMPTY_COUNT);
        test_context.ExpectRandom(
            reference_step_valid, OS_TEST_RESOURCE_RANDOM_REFERENCE_MODEL,
            OS_TEST_RESOURCE_RANDOM_SEED, iteration);

        os::foundation::ScopeRollbackAction
            actions[OS_TEST_RESOURCE_RANDOM_ROLLBACK_ACTION_CAPACITY]{};
        RollbackContext
            contexts[OS_TEST_RESOURCE_RANDOM_ROLLBACK_ACTION_CAPACITY]{};
        RollbackTrace trace{};
        os::foundation::ScopeRollback rollback{};
        const uint64_t action_count =
            NextRandom(random_state) %
            (OS_TEST_RESOURCE_RANDOM_ROLLBACK_ACTION_CAPACITY +
             OS_TEST_RESOURCE_RANDOM_SINGLE_UNIT);
        bool rollback_step_valid =
            rollback.Initialize(
                actions, OS_TEST_RESOURCE_RANDOM_ROLLBACK_ACTION_CAPACITY) ==
            os::foundation::ScopeRollbackStatus::Succeeded;
        uint64_t expected_failure_count = OS_TEST_RESOURCE_RANDOM_EMPTY_COUNT;
        for (uint64_t action_index = OS_TEST_RESOURCE_RANDOM_EMPTY_COUNT;
             rollback_step_valid && action_index < action_count; ++action_index) {
            const bool action_succeeds =
                (NextRandom(random_state) & OS_TEST_RESOURCE_RANDOM_SINGLE_UNIT) ==
                OS_TEST_RESOURCE_RANDOM_EMPTY_COUNT;
            contexts[action_index] = RollbackContext{
                .trace = &trace,
                .value = action_index,
                .succeed = action_succeeds,
            };
            if (!action_succeeds) {
                ++expected_failure_count;
            }
            rollback_step_valid =
                rollback.TryPush(RecordRollback, &contexts[action_index]) ==
                os::foundation::ScopeRollbackStatus::Succeeded;
        }

        const bool commit =
            (NextRandom(random_state) & OS_TEST_RESOURCE_RANDOM_SINGLE_UNIT) !=
            OS_TEST_RESOURCE_RANDOM_EMPTY_COUNT;
        if (commit) {
            rollback_step_valid =
                rollback_step_valid &&
                rollback.Commit() ==
                    os::foundation::ScopeRollbackStatus::Succeeded &&
                rollback.State() ==
                    os::foundation::ScopeRollbackState::Committed &&
                trace.value_count == OS_TEST_RESOURCE_RANDOM_EMPTY_COUNT;
        } else {
            const os::foundation::ScopeRollbackStatus expected_status =
                expected_failure_count == OS_TEST_RESOURCE_RANDOM_EMPTY_COUNT
                    ? os::foundation::ScopeRollbackStatus::Succeeded
                    : os::foundation::ScopeRollbackStatus::ActionFailed;
            rollback_step_valid =
                rollback_step_valid &&
                rollback.TryRollback() == expected_status &&
                rollback.FailureCount() == expected_failure_count &&
                trace.value_count == action_count;
            for (uint64_t trace_index = OS_TEST_RESOURCE_RANDOM_EMPTY_COUNT;
                 rollback_step_valid && trace_index < action_count;
                 ++trace_index) {
                rollback_step_valid =
                    trace.values[trace_index] ==
                    action_count - trace_index -
                        OS_TEST_RESOURCE_RANDOM_SINGLE_UNIT;
            }
        }
        test_context.ExpectRandom(
            rollback_step_valid, OS_TEST_RESOURCE_RANDOM_ROLLBACK_MODEL,
            OS_TEST_RESOURCE_RANDOM_SEED, iteration);
    }

    return test_context.ExitCode();
}

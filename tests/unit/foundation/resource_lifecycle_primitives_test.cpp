#include "os/foundation/reference_counter.hpp"
#include "os/foundation/scope_rollback.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_RESOURCE_PRIMITIVES_SUITE_NAME =
    "foundation/resource_lifecycle_primitives/unit";
constexpr std::string_view OS_TEST_RESOURCE_PRIMITIVES_REFERENCE_LIFECYCLE =
    "引用计数必须只在显式生命周期内增减，并且仅有一次最后引用转换";
constexpr std::string_view OS_TEST_RESOURCE_PRIMITIVES_REFERENCE_FAILURES =
    "引用计数必须拒绝零起点、活动重启、空计数操作和上溢";
constexpr std::string_view OS_TEST_RESOURCE_PRIMITIVES_ROLLBACK_ORDER =
    "作用域回滚必须逆序执行全部动作，并汇总而不是短路单个失败";
constexpr std::string_view OS_TEST_RESOURCE_PRIMITIVES_ROLLBACK_COMMIT =
    "提交后必须清除动作且禁止再次提交或回滚";
constexpr std::string_view OS_TEST_RESOURCE_PRIMITIVES_ROLLBACK_DESTRUCTOR =
    "离开未提交作用域时析构安全网必须执行回滚";
constexpr std::string_view OS_TEST_RESOURCE_PRIMITIVES_ROLLBACK_FAILURES =
    "作用域回滚必须拒绝无存储、零容量、空动作和超容量操作";

constexpr uint64_t OS_TEST_RESOURCE_PRIMITIVES_EMPTY_COUNT = 0ULL;
constexpr uint64_t OS_TEST_RESOURCE_PRIMITIVES_SINGLE_REFERENCE = 1ULL;
constexpr uint64_t OS_TEST_RESOURCE_PRIMITIVES_SECOND_REFERENCE = 2ULL;
constexpr uint64_t OS_TEST_RESOURCE_PRIMITIVES_ACTION_CAPACITY = 3ULL;
constexpr uint64_t OS_TEST_RESOURCE_PRIMITIVES_FIRST_ACTION_VALUE = 11ULL;
constexpr uint64_t OS_TEST_RESOURCE_PRIMITIVES_SECOND_ACTION_VALUE = 22ULL;
constexpr uint64_t OS_TEST_RESOURCE_PRIMITIVES_THIRD_ACTION_VALUE = 33ULL;

struct RollbackTrace final {
    uint64_t values[OS_TEST_RESOURCE_PRIMITIVES_ACTION_CAPACITY];
    uint64_t value_count;
};

struct RollbackActionContext final {
    RollbackTrace *trace;
    uint64_t value;
    bool succeed;
};

[[nodiscard]] bool RecordRollbackAction(void *const raw_context) noexcept {
    if (raw_context == nullptr) {
        return false;
    }
    RollbackActionContext *const context =
        static_cast<RollbackActionContext *>(raw_context);
    if (context->trace == nullptr ||
        context->trace->value_count >= OS_TEST_RESOURCE_PRIMITIVES_ACTION_CAPACITY) {
        return false;
    }
    context->trace->values[context->trace->value_count] = context->value;
    ++context->trace->value_count;
    return context->succeed;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_RESOURCE_PRIMITIVES_SUITE_NAME};

    os::foundation::ReferenceCounter reference_counter{};
    bool released_last_reference = true;
    const bool reference_lifecycle_valid =
        reference_counter.Start(OS_TEST_RESOURCE_PRIMITIVES_SINGLE_REFERENCE) ==
            os::foundation::ReferenceCounterStatus::Succeeded &&
        reference_counter.TryAcquire() ==
            os::foundation::ReferenceCounterStatus::Succeeded &&
        reference_counter.Count() == OS_TEST_RESOURCE_PRIMITIVES_SECOND_REFERENCE &&
        reference_counter.TryRelease(released_last_reference) ==
            os::foundation::ReferenceCounterStatus::Succeeded &&
        !released_last_reference &&
        reference_counter.TryRelease(released_last_reference) ==
            os::foundation::ReferenceCounterStatus::Succeeded &&
        released_last_reference && !reference_counter.IsActive() &&
        reference_counter.Count() == OS_TEST_RESOURCE_PRIMITIVES_EMPTY_COUNT &&
        reference_counter.Start(OS_TEST_RESOURCE_PRIMITIVES_SECOND_REFERENCE) ==
            os::foundation::ReferenceCounterStatus::Succeeded;
    test_context.Expect(reference_lifecycle_valid,
                        OS_TEST_RESOURCE_PRIMITIVES_REFERENCE_LIFECYCLE);

    os::foundation::ReferenceCounter failure_counter{};
    bool unavailable_output = true;
    os::foundation::ReferenceCounter overflow_counter{};
    const bool reference_failures_valid =
        failure_counter.Start(OS_TEST_RESOURCE_PRIMITIVES_EMPTY_COUNT) ==
            os::foundation::ReferenceCounterStatus::EmptyInitialReferenceCount &&
        failure_counter.TryAcquire() ==
            os::foundation::ReferenceCounterStatus::ReferenceUnavailable &&
        failure_counter.TryRelease(unavailable_output) ==
            os::foundation::ReferenceCounterStatus::ReferenceUnavailable &&
        unavailable_output &&
        failure_counter.Start(OS_TEST_RESOURCE_PRIMITIVES_SINGLE_REFERENCE) ==
            os::foundation::ReferenceCounterStatus::Succeeded &&
        failure_counter.Start(OS_TEST_RESOURCE_PRIMITIVES_SINGLE_REFERENCE) ==
            os::foundation::ReferenceCounterStatus::ActiveReferencesRemain &&
        overflow_counter.Start(UINT64_MAX) ==
            os::foundation::ReferenceCounterStatus::Succeeded &&
        overflow_counter.TryAcquire() ==
            os::foundation::ReferenceCounterStatus::CounterOverflow &&
        overflow_counter.Count() == UINT64_MAX;
    test_context.Expect(reference_failures_valid,
                        OS_TEST_RESOURCE_PRIMITIVES_REFERENCE_FAILURES);

    os::foundation::ScopeRollbackAction
        rollback_actions[OS_TEST_RESOURCE_PRIMITIVES_ACTION_CAPACITY]{};
    RollbackTrace rollback_trace{};
    RollbackActionContext rollback_contexts[] = {
        {
            .trace = &rollback_trace,
            .value = OS_TEST_RESOURCE_PRIMITIVES_FIRST_ACTION_VALUE,
            .succeed = true,
        },
        {
            .trace = &rollback_trace,
            .value = OS_TEST_RESOURCE_PRIMITIVES_SECOND_ACTION_VALUE,
            .succeed = false,
        },
        {
            .trace = &rollback_trace,
            .value = OS_TEST_RESOURCE_PRIMITIVES_THIRD_ACTION_VALUE,
            .succeed = true,
        },
    };
    os::foundation::ScopeRollback rollback{};
    bool rollback_order_valid =
        rollback.Initialize(rollback_actions,
                            OS_TEST_RESOURCE_PRIMITIVES_ACTION_CAPACITY) ==
        os::foundation::ScopeRollbackStatus::Succeeded;
    for (uint64_t action_index = OS_TEST_RESOURCE_PRIMITIVES_EMPTY_COUNT;
         rollback_order_valid &&
         action_index < OS_TEST_RESOURCE_PRIMITIVES_ACTION_CAPACITY;
         ++action_index) {
        rollback_order_valid =
            rollback.TryPush(RecordRollbackAction,
                             &rollback_contexts[action_index]) ==
            os::foundation::ScopeRollbackStatus::Succeeded;
    }
    rollback_order_valid =
        rollback_order_valid &&
        rollback.TryRollback() ==
            os::foundation::ScopeRollbackStatus::ActionFailed &&
        rollback.State() == os::foundation::ScopeRollbackState::RollbackFailed &&
        rollback.ActionCount() == OS_TEST_RESOURCE_PRIMITIVES_EMPTY_COUNT &&
        rollback.FailureCount() == OS_TEST_RESOURCE_PRIMITIVES_SINGLE_REFERENCE &&
        rollback_trace.value_count == OS_TEST_RESOURCE_PRIMITIVES_ACTION_CAPACITY &&
        rollback_trace.values[OS_TEST_RESOURCE_PRIMITIVES_EMPTY_COUNT] ==
            OS_TEST_RESOURCE_PRIMITIVES_THIRD_ACTION_VALUE &&
        rollback_trace.values[OS_TEST_RESOURCE_PRIMITIVES_SINGLE_REFERENCE] ==
            OS_TEST_RESOURCE_PRIMITIVES_SECOND_ACTION_VALUE &&
        rollback_trace.values[OS_TEST_RESOURCE_PRIMITIVES_SECOND_REFERENCE] ==
            OS_TEST_RESOURCE_PRIMITIVES_FIRST_ACTION_VALUE;
    test_context.Expect(rollback_order_valid,
                        OS_TEST_RESOURCE_PRIMITIVES_ROLLBACK_ORDER);

    RollbackTrace committed_trace{};
    RollbackActionContext committed_context{
        .trace = &committed_trace,
        .value = OS_TEST_RESOURCE_PRIMITIVES_FIRST_ACTION_VALUE,
        .succeed = true,
    };
    os::foundation::ScopeRollbackAction
        committed_actions[OS_TEST_RESOURCE_PRIMITIVES_SINGLE_REFERENCE]{};
    os::foundation::ScopeRollback committed_rollback{};
    const bool commit_valid =
        committed_rollback.Initialize(
            committed_actions, OS_TEST_RESOURCE_PRIMITIVES_SINGLE_REFERENCE) ==
            os::foundation::ScopeRollbackStatus::Succeeded &&
        committed_rollback.TryPush(RecordRollbackAction, &committed_context) ==
            os::foundation::ScopeRollbackStatus::Succeeded &&
        committed_rollback.Commit() ==
            os::foundation::ScopeRollbackStatus::Succeeded &&
        committed_rollback.State() ==
            os::foundation::ScopeRollbackState::Committed &&
        committed_rollback.ActionCount() ==
            OS_TEST_RESOURCE_PRIMITIVES_EMPTY_COUNT &&
        committed_rollback.TryRollback() ==
            os::foundation::ScopeRollbackStatus::AlreadyFinalized &&
        committed_rollback.Commit() ==
            os::foundation::ScopeRollbackStatus::AlreadyFinalized &&
        committed_trace.value_count == OS_TEST_RESOURCE_PRIMITIVES_EMPTY_COUNT;
    test_context.Expect(commit_valid,
                        OS_TEST_RESOURCE_PRIMITIVES_ROLLBACK_COMMIT);

    RollbackTrace destructor_trace{};
    RollbackActionContext destructor_context{
        .trace = &destructor_trace,
        .value = OS_TEST_RESOURCE_PRIMITIVES_FIRST_ACTION_VALUE,
        .succeed = true,
    };
    {
        os::foundation::ScopeRollbackAction
            destructor_actions[OS_TEST_RESOURCE_PRIMITIVES_SINGLE_REFERENCE]{};
        os::foundation::ScopeRollback destructor_rollback{};
        static_cast<void>(destructor_rollback.Initialize(
            destructor_actions, OS_TEST_RESOURCE_PRIMITIVES_SINGLE_REFERENCE));
        static_cast<void>(
            destructor_rollback.TryPush(RecordRollbackAction, &destructor_context));
    }
    test_context.Expect(
        destructor_trace.value_count == OS_TEST_RESOURCE_PRIMITIVES_SINGLE_REFERENCE &&
            destructor_trace.values[OS_TEST_RESOURCE_PRIMITIVES_EMPTY_COUNT] ==
                OS_TEST_RESOURCE_PRIMITIVES_FIRST_ACTION_VALUE,
        OS_TEST_RESOURCE_PRIMITIVES_ROLLBACK_DESTRUCTOR);

    os::foundation::ScopeRollback invalid_rollback{};
    os::foundation::ScopeRollbackAction
        single_action[OS_TEST_RESOURCE_PRIMITIVES_SINGLE_REFERENCE]{};
    const bool rollback_failures_valid =
        invalid_rollback.TryPush(RecordRollbackAction, nullptr) ==
            os::foundation::ScopeRollbackStatus::NotInitialized &&
        invalid_rollback.TryRollback() ==
            os::foundation::ScopeRollbackStatus::NotInitialized &&
        invalid_rollback.Initialize(
            nullptr, OS_TEST_RESOURCE_PRIMITIVES_SINGLE_REFERENCE) ==
            os::foundation::ScopeRollbackStatus::NullActionStorage &&
        invalid_rollback.Initialize(single_action,
                                    OS_TEST_RESOURCE_PRIMITIVES_EMPTY_COUNT) ==
            os::foundation::ScopeRollbackStatus::EmptyActionCapacity &&
        invalid_rollback.Initialize(
            single_action, OS_TEST_RESOURCE_PRIMITIVES_SINGLE_REFERENCE) ==
            os::foundation::ScopeRollbackStatus::Succeeded &&
        invalid_rollback.Initialize(
            single_action, OS_TEST_RESOURCE_PRIMITIVES_SINGLE_REFERENCE) ==
            os::foundation::ScopeRollbackStatus::AlreadyInitialized &&
        invalid_rollback.TryPush(nullptr, nullptr) ==
            os::foundation::ScopeRollbackStatus::NullOperation &&
        invalid_rollback.TryPush(RecordRollbackAction, &destructor_context) ==
            os::foundation::ScopeRollbackStatus::Succeeded &&
        invalid_rollback.TryPush(RecordRollbackAction, &destructor_context) ==
            os::foundation::ScopeRollbackStatus::ActionCapacityExhausted &&
        invalid_rollback.TryRollback() ==
            os::foundation::ScopeRollbackStatus::Succeeded;
    test_context.Expect(rollback_failures_valid,
                        OS_TEST_RESOURCE_PRIMITIVES_ROLLBACK_FAILURES);

    return test_context.ExitCode();
}

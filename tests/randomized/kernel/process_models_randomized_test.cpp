#include "os/kernel/process/process_tree.hpp"
#include "os/kernel/process/program_arguments.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_PROCESS_MODELS_RANDOM_SUITE_NAME =
    "kernel/process_models/randomized";
constexpr std::string_view OS_TEST_PROCESS_MODELS_RANDOM_TREE_MESSAGE =
    "随机退出顺序下的 8192 个子进程必须精确回收到空树";
constexpr std::string_view OS_TEST_PROCESS_MODELS_RANDOM_ARGUMENT_MESSAGE =
    "随机 argc、envc 和长度组合必须生成连续、对齐且可重复复用的栈布局";

constexpr uint64_t OS_TEST_PROCESS_MODELS_RANDOM_SEED = 0x6A09E667F3BCC909ULL;
constexpr uint64_t OS_TEST_PROCESS_MODELS_RANDOM_SHIFT_FIRST = 12ULL;
constexpr uint64_t OS_TEST_PROCESS_MODELS_RANDOM_SHIFT_SECOND = 25ULL;
constexpr uint64_t OS_TEST_PROCESS_MODELS_RANDOM_SHIFT_THIRD = 27ULL;
constexpr uint64_t OS_TEST_PROCESS_MODELS_RANDOM_MULTIPLIER = 0x2545F4914F6CDD1DULL;
constexpr uint64_t OS_TEST_PROCESS_MODELS_RANDOM_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_PROCESS_MODELS_RANDOM_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_TEST_PROCESS_MODELS_RANDOM_TREE_CAPACITY = 33ULL;
constexpr uint64_t OS_TEST_PROCESS_MODELS_RANDOM_INIT_INDEX = 0ULL;
constexpr uint64_t OS_TEST_PROCESS_MODELS_RANDOM_INIT_PROCESS_ID = 1ULL;
constexpr uint64_t OS_TEST_PROCESS_MODELS_RANDOM_CHILD_COUNT =
    OS_TEST_PROCESS_MODELS_RANDOM_TREE_CAPACITY - OS_TEST_PROCESS_MODELS_RANDOM_COUNTER_INCREMENT;
constexpr uint64_t OS_TEST_PROCESS_MODELS_RANDOM_TREE_BATCH_COUNT = 256ULL;
constexpr uint64_t OS_TEST_PROCESS_MODELS_RANDOM_TREE_LIFECYCLE_COUNT =
    OS_TEST_PROCESS_MODELS_RANDOM_CHILD_COUNT * OS_TEST_PROCESS_MODELS_RANDOM_TREE_BATCH_COUNT;
constexpr uint64_t OS_TEST_PROCESS_MODELS_RANDOM_FIRST_CHILD_PROCESS_ID = 2ULL;
constexpr uint64_t OS_TEST_PROCESS_MODELS_RANDOM_ARGUMENT_ITERATION_COUNT = 4096ULL;
constexpr uint64_t OS_TEST_PROCESS_MODELS_RANDOM_MAXIMUM_VECTOR_COUNT = 16ULL;
constexpr uint64_t OS_TEST_PROCESS_MODELS_RANDOM_MAXIMUM_STRING_LENGTH_BYTES = 1024ULL;
constexpr uint64_t OS_TEST_PROCESS_MODELS_RANDOM_STACK_BOTTOM = 0x000000007FFC0000ULL;
constexpr uint64_t OS_TEST_PROCESS_MODELS_RANDOM_STACK_TOP = 0x0000000080000000ULL;
constexpr uint64_t OS_TEST_PROCESS_MODELS_RANDOM_STACK_ALIGNMENT_BYTES = 16ULL;
constexpr uint64_t OS_TEST_PROCESS_MODELS_RANDOM_STRING_TERMINATOR_SIZE_BYTES = 1ULL;
constexpr int64_t OS_TEST_PROCESS_MODELS_RANDOM_SUCCESS_EXIT_CODE = 0LL;

[[nodiscard]] uint64_t NextRandom(uint64_t &state) noexcept {
    state ^= state >> OS_TEST_PROCESS_MODELS_RANDOM_SHIFT_FIRST;
    state ^= state << OS_TEST_PROCESS_MODELS_RANDOM_SHIFT_SECOND;
    state ^= state >> OS_TEST_PROCESS_MODELS_RANDOM_SHIFT_THIRD;
    return state * OS_TEST_PROCESS_MODELS_RANDOM_MULTIPLIER;
}

[[nodiscard]] os::kernel::ProcessTreeExitStatus SuccessExitStatus() noexcept {
    return os::kernel::ProcessTreeExitStatus{
        .termination_reason = os::kernel::ProcessTreeTerminationReason::Exited,
        .exit_code = OS_TEST_PROCESS_MODELS_RANDOM_SUCCESS_EXIT_CODE,
        .exception_vector = OS_TEST_PROCESS_MODELS_RANDOM_EMPTY_VALUE,
    };
}

[[nodiscard]] bool ValidateRandomTree(uint64_t &random_state) noexcept {
    os::kernel::ProcessTreeEntry entries[OS_TEST_PROCESS_MODELS_RANDOM_TREE_CAPACITY]{};
    os::kernel::ProcessTree tree{};
    if (tree.Initialize(entries, OS_TEST_PROCESS_MODELS_RANDOM_TREE_CAPACITY) !=
            os::kernel::ProcessTreeStatus::Succeeded ||
        tree.RegisterInit(OS_TEST_PROCESS_MODELS_RANDOM_INIT_INDEX,
                          OS_TEST_PROCESS_MODELS_RANDOM_INIT_PROCESS_ID) !=
            os::kernel::ProcessTreeStatus::Succeeded) {
        return false;
    }
    uint64_t next_process_id = OS_TEST_PROCESS_MODELS_RANDOM_FIRST_CHILD_PROCESS_ID;
    uint64_t slot_order[OS_TEST_PROCESS_MODELS_RANDOM_CHILD_COUNT]{};
    for (uint64_t batch = OS_TEST_PROCESS_MODELS_RANDOM_EMPTY_VALUE;
         batch < OS_TEST_PROCESS_MODELS_RANDOM_TREE_BATCH_COUNT; ++batch) {
        for (uint64_t child_ordinal = OS_TEST_PROCESS_MODELS_RANDOM_EMPTY_VALUE;
             child_ordinal < OS_TEST_PROCESS_MODELS_RANDOM_CHILD_COUNT; ++child_ordinal) {
            const uint64_t child_index =
                child_ordinal + OS_TEST_PROCESS_MODELS_RANDOM_COUNTER_INCREMENT;
            slot_order[child_ordinal] = child_index;
            if (tree.RegisterChild(child_index, next_process_id + child_ordinal,
                                   OS_TEST_PROCESS_MODELS_RANDOM_INIT_INDEX) !=
                os::kernel::ProcessTreeStatus::Succeeded) {
                return false;
            }
        }
        for (uint64_t remaining_count = OS_TEST_PROCESS_MODELS_RANDOM_CHILD_COUNT;
             remaining_count > OS_TEST_PROCESS_MODELS_RANDOM_COUNTER_INCREMENT; --remaining_count) {
            const uint64_t swap_index = NextRandom(random_state) % remaining_count;
            const uint64_t final_index =
                remaining_count - OS_TEST_PROCESS_MODELS_RANDOM_COUNTER_INCREMENT;
            const uint64_t temporary = slot_order[swap_index];
            slot_order[swap_index] = slot_order[final_index];
            slot_order[final_index] = temporary;
        }
        for (uint64_t order_index = OS_TEST_PROCESS_MODELS_RANDOM_EMPTY_VALUE;
             order_index < OS_TEST_PROCESS_MODELS_RANDOM_CHILD_COUNT; ++order_index) {
            const uint64_t child_index = slot_order[order_index];
            const uint64_t child_process_id =
                next_process_id + child_index - OS_TEST_PROCESS_MODELS_RANDOM_COUNTER_INCREMENT;
            uint64_t reparented_process_count = OS_TEST_PROCESS_MODELS_RANDOM_EMPTY_VALUE;
            os::kernel::ProcessTreeWaitResult wait_result{};
            if (tree.MarkExited(child_index, SuccessExitStatus(), reparented_process_count) !=
                    os::kernel::ProcessTreeStatus::Succeeded ||
                tree.TryWait(OS_TEST_PROCESS_MODELS_RANDOM_INIT_INDEX, child_process_id,
                             wait_result) != os::kernel::ProcessTreeStatus::Succeeded ||
                wait_result.process_id != child_process_id) {
                return false;
            }
        }
        next_process_id += OS_TEST_PROCESS_MODELS_RANDOM_CHILD_COUNT;
        if (tree.Validate() != os::kernel::ProcessTreeStatus::Succeeded) {
            return false;
        }
    }
    uint64_t reparented_process_count = OS_TEST_PROCESS_MODELS_RANDOM_EMPTY_VALUE;
    os::kernel::ProcessTreeWaitResult init_result{};
    if (tree.MarkExited(OS_TEST_PROCESS_MODELS_RANDOM_INIT_INDEX, SuccessExitStatus(),
                        reparented_process_count) != os::kernel::ProcessTreeStatus::Succeeded ||
        tree.CollectInit(init_result) != os::kernel::ProcessTreeStatus::Succeeded ||
        tree.Validate() != os::kernel::ProcessTreeStatus::Succeeded) {
        return false;
    }
    const os::kernel::ProcessTreeStatistics statistics = tree.Statistics();
    return statistics.active_process_count == OS_TEST_PROCESS_MODELS_RANDOM_EMPTY_VALUE &&
           statistics.registered_process_count ==
               OS_TEST_PROCESS_MODELS_RANDOM_TREE_LIFECYCLE_COUNT +
                   OS_TEST_PROCESS_MODELS_RANDOM_INIT_PROCESS_ID &&
           statistics.wait_success_count == OS_TEST_PROCESS_MODELS_RANDOM_TREE_LIFECYCLE_COUNT;
}

[[nodiscard]] bool ValidateRandomArgumentPlans(uint64_t &random_state) noexcept {
    os::kernel::ProgramArgumentPlan plan{};
    for (uint64_t iteration = OS_TEST_PROCESS_MODELS_RANDOM_EMPTY_VALUE;
         iteration < OS_TEST_PROCESS_MODELS_RANDOM_ARGUMENT_ITERATION_COUNT; ++iteration) {
        plan.Reset();
        const uint64_t argument_count =
            NextRandom(random_state) % OS_TEST_PROCESS_MODELS_RANDOM_MAXIMUM_VECTOR_COUNT;
        const uint64_t environment_count =
            NextRandom(random_state) % OS_TEST_PROCESS_MODELS_RANDOM_MAXIMUM_VECTOR_COUNT;
        uint64_t expected_total_string_bytes = OS_TEST_PROCESS_MODELS_RANDOM_EMPTY_VALUE;
        for (uint64_t argument_index = OS_TEST_PROCESS_MODELS_RANDOM_EMPTY_VALUE;
             argument_index < argument_count; ++argument_index) {
            const uint64_t string_length_bytes =
                NextRandom(random_state) %
                OS_TEST_PROCESS_MODELS_RANDOM_MAXIMUM_STRING_LENGTH_BYTES;
            if (plan.AddArgument(string_length_bytes) !=
                os::kernel::ProgramArgumentStatus::Succeeded) {
                return false;
            }
            expected_total_string_bytes +=
                string_length_bytes + OS_TEST_PROCESS_MODELS_RANDOM_STRING_TERMINATOR_SIZE_BYTES;
        }
        for (uint64_t environment_index = OS_TEST_PROCESS_MODELS_RANDOM_EMPTY_VALUE;
             environment_index < environment_count; ++environment_index) {
            const uint64_t string_length_bytes =
                NextRandom(random_state) %
                OS_TEST_PROCESS_MODELS_RANDOM_MAXIMUM_STRING_LENGTH_BYTES;
            if (plan.AddEnvironment(string_length_bytes) !=
                os::kernel::ProgramArgumentStatus::Succeeded) {
                return false;
            }
            expected_total_string_bytes +=
                string_length_bytes + OS_TEST_PROCESS_MODELS_RANDOM_STRING_TERMINATOR_SIZE_BYTES;
        }
        if (plan.Finalize(OS_TEST_PROCESS_MODELS_RANDOM_STACK_BOTTOM,
                          OS_TEST_PROCESS_MODELS_RANDOM_STACK_TOP) !=
                os::kernel::ProgramArgumentStatus::Succeeded ||
            plan.Validate() != os::kernel::ProgramArgumentStatus::Succeeded ||
            plan.Layout().stack_pointer % OS_TEST_PROCESS_MODELS_RANDOM_STACK_ALIGNMENT_BYTES !=
                OS_TEST_PROCESS_MODELS_RANDOM_EMPTY_VALUE ||
            plan.Layout().total_string_bytes != expected_total_string_bytes ||
            plan.ArgumentCount() != argument_count ||
            plan.EnvironmentCount() != environment_count) {
            return false;
        }
    }
    return true;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_PROCESS_MODELS_RANDOM_SUITE_NAME};
    uint64_t random_state = OS_TEST_PROCESS_MODELS_RANDOM_SEED;
    test_context.ExpectRandom(
        ValidateRandomTree(random_state), OS_TEST_PROCESS_MODELS_RANDOM_TREE_MESSAGE,
        OS_TEST_PROCESS_MODELS_RANDOM_SEED, OS_TEST_PROCESS_MODELS_RANDOM_TREE_LIFECYCLE_COUNT);
    test_context.ExpectRandom(
        ValidateRandomArgumentPlans(random_state), OS_TEST_PROCESS_MODELS_RANDOM_ARGUMENT_MESSAGE,
        OS_TEST_PROCESS_MODELS_RANDOM_SEED, OS_TEST_PROCESS_MODELS_RANDOM_ARGUMENT_ITERATION_COUNT);
    return test_context.ExitCode();
}

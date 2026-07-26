#include "kernel_stack_test_environment.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_KERNEL_STACK_RANDOM_SUITE_NAME =
    "kernel/kernel_stack_manager/randomized";
constexpr std::string_view OS_TEST_KERNEL_STACK_RANDOM_INITIALIZATION =
    "随机栈模型所需的 buddy、KVA、页表和管理器必须完整初始化";
constexpr std::string_view OS_TEST_KERNEL_STACK_RANDOM_OPERATION =
    "每一步创建或销毁结果必须与独立逐页所有权模型一致";
constexpr std::string_view OS_TEST_KERNEL_STACK_RANDOM_INVARIANT =
    "周期校验必须保持映射、计数、峰值和三层资源所有权一致";
constexpr std::string_view OS_TEST_KERNEL_STACK_RANDOM_DRAIN =
    "排空全部随机栈后物理页、KVA 与管理器必须恢复暖机基线";

constexpr uint64_t OS_TEST_KERNEL_STACK_RANDOM_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_KERNEL_STACK_RANDOM_SINGLE_UNIT = 1ULL;
constexpr uint64_t OS_TEST_KERNEL_STACK_RANDOM_SLOT_CAPACITY = 64ULL;
constexpr uint64_t OS_TEST_KERNEL_STACK_RANDOM_WARMUP_SLOT_INDEX =
    OS_TEST_KERNEL_STACK_RANDOM_SLOT_CAPACITY - OS_TEST_KERNEL_STACK_RANDOM_SINGLE_UNIT;
constexpr uint64_t OS_TEST_KERNEL_STACK_RANDOM_OPERATION_COUNT = 100000ULL;
constexpr uint64_t OS_TEST_KERNEL_STACK_RANDOM_VALIDATION_INTERVAL = 257ULL;
constexpr uint64_t OS_TEST_KERNEL_STACK_RANDOM_SEED = 0x4B535441434B524EULL;
constexpr uint64_t OS_TEST_KERNEL_STACK_RANDOM_MULTIPLIER = 6364136223846793005ULL;
constexpr uint64_t OS_TEST_KERNEL_STACK_RANDOM_INCREMENT = 1442695040888963407ULL;
constexpr uint64_t OS_TEST_KERNEL_STACK_RANDOM_CREATE_MASK = 0x8000000000000000ULL;
constexpr uint64_t OS_TEST_KERNEL_STACK_RANDOM_RESERVED_PAGE_INDEX = 0ULL;
constexpr uint64_t OS_TEST_KERNEL_STACK_RANDOM_EXPECTED_INFRASTRUCTURE_FRAME_COUNT = 4ULL;

struct ModelStack final {
    uint64_t begin_page_index;
    bool active;
};

[[nodiscard]] uint64_t NextRandom(uint64_t &state) noexcept {
    state = state * OS_TEST_KERNEL_STACK_RANDOM_MULTIPLIER + OS_TEST_KERNEL_STACK_RANDOM_INCREMENT;
    return state;
}

[[nodiscard]] bool FindExpectedRange(const bool *owned_pages, uint64_t &begin_page_index) noexcept {
    bool range_found = false;
    uint64_t best_gap_page_count = UINT64_MAX;
    uint64_t page_index = OS_TEST_KERNEL_STACK_RANDOM_SINGLE_UNIT;
    while (page_index < os::test::OS_TEST_KERNEL_STACK_ENVIRONMENT_WINDOW_PAGE_COUNT) {
        if (owned_pages[page_index]) {
            ++page_index;
            continue;
        }
        const uint64_t gap_begin_page_index = page_index;
        while (page_index < os::test::OS_TEST_KERNEL_STACK_ENVIRONMENT_WINDOW_PAGE_COUNT &&
               !owned_pages[page_index]) {
            ++page_index;
        }
        const uint64_t gap_page_count = page_index - gap_begin_page_index;
        if (gap_page_count >= os::kernel::OS_KERNEL_STACK_RANGE_PAGE_COUNT &&
            gap_page_count < best_gap_page_count) {
            range_found = true;
            best_gap_page_count = gap_page_count;
            begin_page_index = gap_begin_page_index;
        }
    }
    return range_found;
}

void SetModelRange(bool *owned_pages, const uint64_t begin_page_index, const bool owned) noexcept {
    for (uint64_t page_offset = OS_TEST_KERNEL_STACK_RANDOM_EMPTY_VALUE;
         page_offset < os::kernel::OS_KERNEL_STACK_RANGE_PAGE_COUNT; ++page_offset) {
        owned_pages[begin_page_index + page_offset] = owned;
    }
}

[[nodiscard]] bool StatisticsMatch(os::test::KernelStackTestEnvironment &environment,
                                   const uint64_t active_stack_count,
                                   const uint64_t successful_creation_count,
                                   const uint64_t destruction_count) noexcept {
    const os::kernel::KernelStackManagerStatistics stack_statistics =
        environment.StackManager().Statistics();
    const os::kernel::KernelVirtualAddressAllocatorStatistics virtual_address_statistics =
        environment.VirtualAddressAllocator().Statistics();
    const os::kernel::PhysicalFrameAllocatorStatistics frame_statistics =
        environment.FrameAllocator().Statistics();
    return stack_statistics.active_stack_count == active_stack_count &&
           stack_statistics.active_mapped_page_count ==
               active_stack_count * os::kernel::OS_KERNEL_STACK_MAPPED_PAGE_COUNT &&
           stack_statistics.active_guard_page_count ==
               active_stack_count * os::kernel::OS_KERNEL_STACK_GUARD_PAGE_COUNT &&
           stack_statistics.successful_creation_count == successful_creation_count &&
           stack_statistics.destruction_count == destruction_count &&
           stack_statistics.peak_active_stack_count >= active_stack_count &&
           stack_statistics.peak_active_mapped_page_count >=
               stack_statistics.active_mapped_page_count &&
           virtual_address_statistics.allocated_page_count ==
               active_stack_count * os::kernel::OS_KERNEL_STACK_RANGE_PAGE_COUNT &&
           virtual_address_statistics.reserved_page_count ==
               OS_TEST_KERNEL_STACK_RANDOM_SINGLE_UNIT &&
           frame_statistics.allocated_frame_count ==
               OS_TEST_KERNEL_STACK_RANDOM_EXPECTED_INFRASTRUCTURE_FRAME_COUNT +
                   active_stack_count * os::kernel::OS_KERNEL_STACK_MAPPED_PAGE_COUNT &&
           environment.StackManager().Validate() == os::kernel::KernelStackManagerStatus::Succeeded;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_KERNEL_STACK_RANDOM_SUITE_NAME};
    static os::test::KernelStackTestEnvironment environment{};
    const bool initialized = environment.Initialize(OS_TEST_KERNEL_STACK_RANDOM_SLOT_CAPACITY);
    const bool warmed_up =
        initialized &&
        environment.StackManager().TryCreate(OS_TEST_KERNEL_STACK_RANDOM_WARMUP_SLOT_INDEX) ==
            os::kernel::KernelStackManagerStatus::Succeeded &&
        environment.StackManager().TryDestroy(OS_TEST_KERNEL_STACK_RANDOM_WARMUP_SLOT_INDEX) ==
            os::kernel::KernelStackManagerStatus::Succeeded;
    test_context.Expect(warmed_up, OS_TEST_KERNEL_STACK_RANDOM_INITIALIZATION);

    const os::kernel::PhysicalFrameAllocatorStatistics frames_before_random_operations =
        environment.FrameAllocator().Statistics();
    const os::kernel::KernelVirtualAddressAllocatorStatistics
        virtual_addresses_before_random_operations =
            environment.VirtualAddressAllocator().Statistics();
    const os::kernel::KernelStackManagerStatistics stacks_before_random_operations =
        environment.StackManager().Statistics();

    ModelStack model_stacks[OS_TEST_KERNEL_STACK_RANDOM_SLOT_CAPACITY]{};
    bool owned_pages[os::test::OS_TEST_KERNEL_STACK_ENVIRONMENT_WINDOW_PAGE_COUNT]{};
    owned_pages[OS_TEST_KERNEL_STACK_RANDOM_RESERVED_PAGE_INDEX] = true;
    uint64_t random_state = OS_TEST_KERNEL_STACK_RANDOM_SEED;
    uint64_t active_stack_count = OS_TEST_KERNEL_STACK_RANDOM_EMPTY_VALUE;
    uint64_t successful_creation_count = stacks_before_random_operations.successful_creation_count;
    uint64_t destruction_count = stacks_before_random_operations.destruction_count;
    bool operations_valid = warmed_up;
    bool invariants_valid = warmed_up;

    for (uint64_t operation_index = OS_TEST_KERNEL_STACK_RANDOM_EMPTY_VALUE;
         operations_valid && operation_index < OS_TEST_KERNEL_STACK_RANDOM_OPERATION_COUNT;
         ++operation_index) {
        const uint64_t operation_random_value = NextRandom(random_state);
        const uint64_t slot_index =
            NextRandom(random_state) % OS_TEST_KERNEL_STACK_RANDOM_SLOT_CAPACITY;
        const bool create_operation =
            (operation_random_value & OS_TEST_KERNEL_STACK_RANDOM_CREATE_MASK) ==
            OS_TEST_KERNEL_STACK_RANDOM_EMPTY_VALUE;
        ModelStack &model_stack = model_stacks[slot_index];

        if (create_operation) {
            if (model_stack.active) {
                operations_valid = environment.StackManager().TryCreate(slot_index) ==
                                   os::kernel::KernelStackManagerStatus::SlotAlreadyActive;
            } else {
                uint64_t expected_begin_page_index = OS_TEST_KERNEL_STACK_RANDOM_EMPTY_VALUE;
                operations_valid = FindExpectedRange(owned_pages, expected_begin_page_index) &&
                                   environment.StackManager().TryCreate(slot_index) ==
                                       os::kernel::KernelStackManagerStatus::Succeeded;
                os::kernel::KernelStack stack{};
                operations_valid = operations_valid &&
                                   environment.StackManager().Read(slot_index, stack) ==
                                       os::kernel::KernelStackManagerStatus::Succeeded &&
                                   (stack.virtual_range.begin_address -
                                    os::test::OS_TEST_KERNEL_STACK_ENVIRONMENT_WINDOW_BASE) /
                                           os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES ==
                                       expected_begin_page_index;
                if (operations_valid) {
                    model_stack = ModelStack{
                        .begin_page_index = expected_begin_page_index,
                        .active = true,
                    };
                    SetModelRange(owned_pages, expected_begin_page_index, true);
                    ++active_stack_count;
                    ++successful_creation_count;
                }
            }
        } else if (!model_stack.active) {
            operations_valid = environment.StackManager().TryDestroy(slot_index) ==
                               os::kernel::KernelStackManagerStatus::SlotNotActive;
        } else {
            operations_valid = environment.StackManager().TryDestroy(slot_index) ==
                               os::kernel::KernelStackManagerStatus::Succeeded;
            if (operations_valid) {
                SetModelRange(owned_pages, model_stack.begin_page_index, false);
                model_stack = ModelStack{};
                --active_stack_count;
                ++destruction_count;
            }
        }

        if (operations_valid && operation_index % OS_TEST_KERNEL_STACK_RANDOM_VALIDATION_INTERVAL ==
                                    OS_TEST_KERNEL_STACK_RANDOM_EMPTY_VALUE) {
            invariants_valid =
                invariants_valid && StatisticsMatch(environment, active_stack_count,
                                                    successful_creation_count, destruction_count);
        }
    }
    test_context.Expect(operations_valid, OS_TEST_KERNEL_STACK_RANDOM_OPERATION);
    test_context.Expect(invariants_valid, OS_TEST_KERNEL_STACK_RANDOM_INVARIANT);

    bool drain_valid = operations_valid;
    for (uint64_t slot_index = OS_TEST_KERNEL_STACK_RANDOM_EMPTY_VALUE;
         drain_valid && slot_index < OS_TEST_KERNEL_STACK_RANDOM_SLOT_CAPACITY; ++slot_index) {
        if (!model_stacks[slot_index].active) {
            continue;
        }
        drain_valid = environment.StackManager().TryDestroy(slot_index) ==
                      os::kernel::KernelStackManagerStatus::Succeeded;
        if (drain_valid) {
            SetModelRange(owned_pages, model_stacks[slot_index].begin_page_index, false);
            model_stacks[slot_index] = ModelStack{};
            --active_stack_count;
            ++destruction_count;
        }
    }
    const os::kernel::PhysicalFrameAllocatorStatistics frames_after_random_operations =
        environment.FrameAllocator().Statistics();
    const os::kernel::KernelVirtualAddressAllocatorStatistics
        virtual_addresses_after_random_operations =
            environment.VirtualAddressAllocator().Statistics();
    drain_valid = drain_valid && active_stack_count == OS_TEST_KERNEL_STACK_RANDOM_EMPTY_VALUE &&
                  owned_pages[OS_TEST_KERNEL_STACK_RANDOM_RESERVED_PAGE_INDEX] &&
                  frames_after_random_operations.free_frame_count ==
                      frames_before_random_operations.free_frame_count &&
                  frames_after_random_operations.allocated_frame_count ==
                      frames_before_random_operations.allocated_frame_count &&
                  virtual_addresses_after_random_operations.free_page_count ==
                      virtual_addresses_before_random_operations.free_page_count &&
                  virtual_addresses_after_random_operations.allocated_page_count ==
                      virtual_addresses_before_random_operations.allocated_page_count &&
                  successful_creation_count == destruction_count &&
                  StatisticsMatch(environment, active_stack_count, successful_creation_count,
                                  destruction_count);
    for (uint64_t page_index = OS_TEST_KERNEL_STACK_RANDOM_SINGLE_UNIT;
         drain_valid && page_index < os::test::OS_TEST_KERNEL_STACK_ENVIRONMENT_WINDOW_PAGE_COUNT;
         ++page_index) {
        drain_valid = !owned_pages[page_index];
    }
    test_context.Expect(drain_valid, OS_TEST_KERNEL_STACK_RANDOM_DRAIN);
    return test_context.ExitCode();
}

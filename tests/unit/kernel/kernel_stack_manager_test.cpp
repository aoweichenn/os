#include "kernel_stack_test_environment.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_KERNEL_STACK_SUITE_NAME = "kernel/kernel_stack_manager/unit";
constexpr std::string_view OS_TEST_KERNEL_STACK_INITIALIZATION =
    "初始化必须拒绝空存储、无效容量、无效物理访问并保持失败输出";
constexpr std::string_view OS_TEST_KERNEL_STACK_LAYOUT =
    "动态内核栈必须由下保护页、四个映射页和上保护页组成";
constexpr std::string_view OS_TEST_KERNEL_STACK_MAPPING =
    "四个数据页必须清零并使用 supervisor RW/NX 的独立物理页";
constexpr std::string_view OS_TEST_KERNEL_STACK_LIFECYCLE =
    "销毁必须逆序撤销映射、清零物理页并归还 KVA 所有权";
constexpr std::string_view OS_TEST_KERNEL_STACK_EXHAUSTION =
    "KVA 与物理页耗尽必须失败原子且不占用栈槽";
constexpr std::string_view OS_TEST_KERNEL_STACK_STALE_MAPPING =
    "KVA 空闲但页表残留映射时必须拒绝复用该虚拟区间";
constexpr std::string_view OS_TEST_KERNEL_STACK_CORRUPTION =
    "物理页、KVA 或叶映射所有权被外部破坏时必须先拒绝销毁并允许修复";

constexpr uint64_t OS_TEST_KERNEL_STACK_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_KERNEL_STACK_SINGLE_UNIT = 1ULL;
constexpr uint64_t OS_TEST_KERNEL_STACK_SLOT_CAPACITY = 4ULL;
constexpr uint64_t OS_TEST_KERNEL_STACK_WARMUP_SLOT_INDEX = 0ULL;
constexpr uint64_t OS_TEST_KERNEL_STACK_LIFECYCLE_SLOT_INDEX = 1ULL;
constexpr uint64_t OS_TEST_KERNEL_STACK_STALE_SLOT_INDEX = 2ULL;
constexpr uint64_t OS_TEST_KERNEL_STACK_INVALID_SLOT_INDEX = OS_TEST_KERNEL_STACK_SLOT_CAPACITY;
constexpr uint64_t OS_TEST_KERNEL_STACK_PROBE_SIZE_BYTES = sizeof(uint64_t);
constexpr uint64_t OS_TEST_KERNEL_STACK_FRAME_VALUE_COUNT =
    os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES / sizeof(uint64_t);
constexpr uint64_t OS_TEST_KERNEL_STACK_FIRST_PATTERN = 0x535441434B464952ULL;
constexpr uint64_t OS_TEST_KERNEL_STACK_LAST_PATTERN = 0x535441434B4C4153ULL;
constexpr uint64_t OS_TEST_KERNEL_STACK_EXPECTED_SUCCESSFUL_CREATION_COUNT = 2ULL;
constexpr uint64_t OS_TEST_KERNEL_STACK_EXPECTED_DESTRUCTION_COUNT = 2ULL;
constexpr uint64_t OS_TEST_KERNEL_STACK_EXTERNAL_FULL_RANGE_PAGE_COUNT =
    os::test::OS_TEST_KERNEL_STACK_ENVIRONMENT_WINDOW_PAGE_COUNT - OS_TEST_KERNEL_STACK_SINGLE_UNIT;

[[nodiscard]] bool FrameIsZero(const os::test::KernelStackTestEnvironment &environment,
                               const os::kernel::PhysicalFrame frame) noexcept {
    const uint64_t *const values =
        reinterpret_cast<const uint64_t *>(environment.PhysicalMemory() + frame.physical_address);
    for (uint64_t value_index = OS_TEST_KERNEL_STACK_EMPTY_VALUE;
         value_index < OS_TEST_KERNEL_STACK_FRAME_VALUE_COUNT; ++value_index) {
        if (values[value_index] != OS_TEST_KERNEL_STACK_EMPTY_VALUE) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool StackMappingsAreValid(os::test::KernelStackTestEnvironment &environment,
                                         const os::kernel::KernelStack &stack) noexcept {
    os::kernel::PageMapping mapping{};
    if (environment.PageTableManager().QueryPage(os::kernel::KernelStackLowerGuardAddress(stack),
                                                 mapping) !=
            os::kernel::PageTableStatus::NotMapped ||
        environment.PageTableManager().QueryPage(os::kernel::KernelStackUpperGuardAddress(stack),
                                                 mapping) !=
            os::kernel::PageTableStatus::NotMapped) {
        return false;
    }
    for (uint64_t data_page_index = OS_TEST_KERNEL_STACK_EMPTY_VALUE;
         data_page_index < os::kernel::OS_KERNEL_STACK_MAPPED_PAGE_COUNT; ++data_page_index) {
        const uint64_t virtual_address =
            os::kernel::KernelStackMappedBeginAddress(stack) +
            data_page_index * os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
        if (environment.PageTableManager().QueryPage(virtual_address, mapping) !=
                os::kernel::PageTableStatus::Succeeded ||
            mapping.physical_address != stack.physical_frames[data_page_index].physical_address ||
            mapping.page_size_bytes != os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES ||
            !mapping.permissions.writable || mapping.permissions.executable ||
            mapping.permissions.user_accessible || mapping.permissions.cache_disabled ||
            !FrameIsZero(environment, stack.physical_frames[data_page_index])) {
            return false;
        }
    }
    return true;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_KERNEL_STACK_SUITE_NAME};

    static uint8_t dummy_physical_memory[os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES]{};
    os::kernel::PhysicalFrameAllocator dummy_frame_allocator{};
    os::kernel::KernelVirtualAddressAllocator dummy_virtual_address_allocator{};
    os::kernel::PageTableManager dummy_page_table_manager{
        dummy_frame_allocator,
        os::kernel::PageTableMemoryAccess{},
    };
    os::kernel::KernelStackManager uninitialized_manager{
        dummy_frame_allocator,
        dummy_virtual_address_allocator,
        dummy_page_table_manager,
        os::kernel::KernelStackMemoryAccess{
            .physical_memory_virtual_base = reinterpret_cast<uint64_t>(dummy_physical_memory),
            .maximum_physical_address_exclusive = os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES,
        },
    };
    os::kernel::KernelStack dummy_stack_storage[OS_TEST_KERNEL_STACK_SINGLE_UNIT]{};
    os::kernel::KernelStack unchanged_stack{
        .virtual_range =
            {
                .begin_address = OS_TEST_KERNEL_STACK_FIRST_PATTERN,
                .page_count = OS_TEST_KERNEL_STACK_LAST_PATTERN,
            },
        .physical_frames = {},
        .mapped_page_count = OS_TEST_KERNEL_STACK_FIRST_PATTERN,
        .active = true,
    };
    const bool initialization_failures_valid =
        uninitialized_manager.TryCreate(OS_TEST_KERNEL_STACK_WARMUP_SLOT_INDEX) ==
            os::kernel::KernelStackManagerStatus::NotInitialized &&
        uninitialized_manager.Read(OS_TEST_KERNEL_STACK_WARMUP_SLOT_INDEX, unchanged_stack) ==
            os::kernel::KernelStackManagerStatus::NotInitialized &&
        unchanged_stack.virtual_range.begin_address == OS_TEST_KERNEL_STACK_FIRST_PATTERN &&
        uninitialized_manager.Initialize(nullptr, OS_TEST_KERNEL_STACK_SINGLE_UNIT) ==
            os::kernel::KernelStackManagerStatus::NullStackStorage &&
        uninitialized_manager.Initialize(dummy_stack_storage, OS_TEST_KERNEL_STACK_EMPTY_VALUE) ==
            os::kernel::KernelStackManagerStatus::EmptySlotCapacity &&
        uninitialized_manager.Initialize(dummy_stack_storage, UINT64_MAX) ==
            os::kernel::KernelStackManagerStatus::InvalidSlotCapacity;

    os::kernel::KernelStackManager invalid_memory_manager{
        dummy_frame_allocator,
        dummy_virtual_address_allocator,
        dummy_page_table_manager,
        os::kernel::KernelStackMemoryAccess{},
    };
    test_context.Expect(initialization_failures_valid &&
                            invalid_memory_manager.Initialize(dummy_stack_storage,
                                                              OS_TEST_KERNEL_STACK_SINGLE_UNIT) ==
                                os::kernel::KernelStackManagerStatus::InvalidMemoryAccess,
                        OS_TEST_KERNEL_STACK_INITIALIZATION);

    static os::test::KernelStackTestEnvironment environment{};
    const bool environment_initialized = environment.Initialize(OS_TEST_KERNEL_STACK_SLOT_CAPACITY);
    os::kernel::KernelStack inactive_output = unchanged_stack;
    const bool slot_boundaries_valid =
        environment_initialized &&
        environment.StackManager().Initialize(dummy_stack_storage,
                                              OS_TEST_KERNEL_STACK_SINGLE_UNIT) ==
            os::kernel::KernelStackManagerStatus::AlreadyInitialized &&
        environment.StackManager().TryCreate(OS_TEST_KERNEL_STACK_INVALID_SLOT_INDEX) ==
            os::kernel::KernelStackManagerStatus::InvalidSlotIndex &&
        environment.StackManager().TryDestroy(OS_TEST_KERNEL_STACK_INVALID_SLOT_INDEX) ==
            os::kernel::KernelStackManagerStatus::InvalidSlotIndex &&
        environment.StackManager().Read(OS_TEST_KERNEL_STACK_INVALID_SLOT_INDEX, inactive_output) ==
            os::kernel::KernelStackManagerStatus::InvalidSlotIndex &&
        environment.StackManager().Read(OS_TEST_KERNEL_STACK_LIFECYCLE_SLOT_INDEX,
                                        inactive_output) ==
            os::kernel::KernelStackManagerStatus::SlotNotActive &&
        inactive_output.virtual_range.begin_address == OS_TEST_KERNEL_STACK_FIRST_PATTERN;
    test_context.Expect(slot_boundaries_valid, OS_TEST_KERNEL_STACK_INITIALIZATION);

    const bool warmup_succeeded =
        environment.StackManager().TryCreate(OS_TEST_KERNEL_STACK_WARMUP_SLOT_INDEX) ==
            os::kernel::KernelStackManagerStatus::Succeeded &&
        environment.StackManager().TryDestroy(OS_TEST_KERNEL_STACK_WARMUP_SLOT_INDEX) ==
            os::kernel::KernelStackManagerStatus::Succeeded;
    const os::kernel::PhysicalFrameAllocatorStatistics frames_before_lifecycle =
        environment.FrameAllocator().Statistics();
    const os::kernel::KernelVirtualAddressAllocatorStatistics virtual_addresses_before_lifecycle =
        environment.VirtualAddressAllocator().Statistics();

    const bool creation_succeeded =
        warmup_succeeded &&
        environment.StackManager().TryCreate(OS_TEST_KERNEL_STACK_LIFECYCLE_SLOT_INDEX) ==
            os::kernel::KernelStackManagerStatus::Succeeded &&
        environment.StackManager().TryCreate(OS_TEST_KERNEL_STACK_LIFECYCLE_SLOT_INDEX) ==
            os::kernel::KernelStackManagerStatus::SlotAlreadyActive;
    os::kernel::KernelStack stack{};
    const bool stack_read =
        creation_succeeded &&
        environment.StackManager().Read(OS_TEST_KERNEL_STACK_LIFECYCLE_SLOT_INDEX, stack) ==
            os::kernel::KernelStackManagerStatus::Succeeded;
    const uint64_t expected_lower_guard_address =
        os::test::OS_TEST_KERNEL_STACK_ENVIRONMENT_WINDOW_BASE +
        os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
    const bool layout_valid =
        stack_read &&
        os::kernel::KernelStackLowerGuardAddress(stack) == expected_lower_guard_address &&
        os::kernel::KernelStackMappedBeginAddress(stack) ==
            expected_lower_guard_address + os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES &&
        os::kernel::KernelStackTopAddress(stack) ==
            os::kernel::KernelStackMappedBeginAddress(stack) +
                os::kernel::OS_KERNEL_STACK_SIZE_BYTES &&
        os::kernel::KernelStackUpperGuardAddress(stack) ==
            os::kernel::KernelStackTopAddress(stack) &&
        stack.virtual_range.page_count == os::kernel::OS_KERNEL_STACK_RANGE_PAGE_COUNT &&
        !environment.StackManager().Contains(OS_TEST_KERNEL_STACK_LIFECYCLE_SLOT_INDEX,
                                             os::kernel::KernelStackLowerGuardAddress(stack),
                                             OS_TEST_KERNEL_STACK_PROBE_SIZE_BYTES) &&
        environment.StackManager().Contains(OS_TEST_KERNEL_STACK_LIFECYCLE_SLOT_INDEX,
                                            os::kernel::KernelStackMappedBeginAddress(stack),
                                            os::kernel::OS_KERNEL_STACK_SIZE_BYTES) &&
        !environment.StackManager().Contains(OS_TEST_KERNEL_STACK_LIFECYCLE_SLOT_INDEX,
                                             os::kernel::KernelStackUpperGuardAddress(stack),
                                             OS_TEST_KERNEL_STACK_PROBE_SIZE_BYTES);
    test_context.Expect(layout_valid, OS_TEST_KERNEL_STACK_LAYOUT);
    test_context.Expect(stack_read && StackMappingsAreValid(environment, stack),
                        OS_TEST_KERNEL_STACK_MAPPING);

    for (uint64_t frame_index = OS_TEST_KERNEL_STACK_EMPTY_VALUE;
         frame_index < os::kernel::OS_KERNEL_STACK_MAPPED_PAGE_COUNT; ++frame_index) {
        uint64_t *const values = reinterpret_cast<uint64_t *>(
            environment.PhysicalMemory() + stack.physical_frames[frame_index].physical_address);
        values[OS_TEST_KERNEL_STACK_EMPTY_VALUE] = OS_TEST_KERNEL_STACK_FIRST_PATTERN + frame_index;
        values[OS_TEST_KERNEL_STACK_FRAME_VALUE_COUNT - OS_TEST_KERNEL_STACK_SINGLE_UNIT] =
            OS_TEST_KERNEL_STACK_LAST_PATTERN + frame_index;
    }
    const bool destruction_succeeded =
        environment.StackManager().TryDestroy(OS_TEST_KERNEL_STACK_LIFECYCLE_SLOT_INDEX) ==
            os::kernel::KernelStackManagerStatus::Succeeded &&
        environment.StackManager().TryDestroy(OS_TEST_KERNEL_STACK_LIFECYCLE_SLOT_INDEX) ==
            os::kernel::KernelStackManagerStatus::SlotNotActive;
    bool released_frames_zero = true;
    for (uint64_t frame_index = OS_TEST_KERNEL_STACK_EMPTY_VALUE;
         frame_index < os::kernel::OS_KERNEL_STACK_MAPPED_PAGE_COUNT; ++frame_index) {
        released_frames_zero =
            released_frames_zero && FrameIsZero(environment, stack.physical_frames[frame_index]);
    }
    const os::kernel::PhysicalFrameAllocatorStatistics frames_after_lifecycle =
        environment.FrameAllocator().Statistics();
    const os::kernel::KernelVirtualAddressAllocatorStatistics virtual_addresses_after_lifecycle =
        environment.VirtualAddressAllocator().Statistics();
    const os::kernel::KernelStackManagerStatistics stack_statistics =
        environment.StackManager().Statistics();
    test_context.Expect(
        destruction_succeeded && released_frames_zero &&
            frames_after_lifecycle.free_frame_count == frames_before_lifecycle.free_frame_count &&
            frames_after_lifecycle.allocated_frame_count ==
                frames_before_lifecycle.allocated_frame_count &&
            virtual_addresses_after_lifecycle.free_page_count ==
                virtual_addresses_before_lifecycle.free_page_count &&
            virtual_addresses_after_lifecycle.allocated_page_count ==
                virtual_addresses_before_lifecycle.allocated_page_count &&
            stack_statistics.active_stack_count == OS_TEST_KERNEL_STACK_EMPTY_VALUE &&
            stack_statistics.successful_creation_count ==
                OS_TEST_KERNEL_STACK_EXPECTED_SUCCESSFUL_CREATION_COUNT &&
            stack_statistics.destruction_count == OS_TEST_KERNEL_STACK_EXPECTED_DESTRUCTION_COUNT &&
            stack_statistics.peak_active_stack_count == OS_TEST_KERNEL_STACK_SINGLE_UNIT &&
            environment.StackManager().Validate() ==
                os::kernel::KernelStackManagerStatus::Succeeded,
        OS_TEST_KERNEL_STACK_LIFECYCLE);

    os::kernel::KernelVirtualAddressRange stale_range{};
    os::kernel::PhysicalFrame stale_frame{};
    const os::kernel::PagePermissions stale_permissions{
        .writable = true,
        .executable = false,
        .user_accessible = false,
        .cache_disabled = false,
    };
    const bool stale_mapping_created =
        environment.VirtualAddressAllocator().TryAllocate(
            os::kernel::OS_KERNEL_STACK_RANGE_PAGE_COUNT, OS_TEST_KERNEL_STACK_SINGLE_UNIT,
            stale_range) == os::kernel::KernelVirtualAddressAllocatorStatus::Succeeded &&
        environment.FrameAllocator().Allocate(stale_frame) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        environment.PageTableManager().MapPage(stale_range.begin_address,
                                               stale_frame.physical_address, stale_permissions) ==
            os::kernel::PageTableStatus::Succeeded &&
        environment.VirtualAddressAllocator().TryRelease(stale_range) ==
            os::kernel::KernelVirtualAddressAllocatorStatus::Succeeded;
    const bool stale_mapping_rejected =
        stale_mapping_created &&
        environment.StackManager().TryCreate(OS_TEST_KERNEL_STACK_STALE_SLOT_INDEX) ==
            os::kernel::KernelStackManagerStatus::VirtualRangeNotClear;
    const bool stale_mapping_cleaned =
        environment.PageTableManager().UnmapPage(stale_range.begin_address) ==
            os::kernel::PageTableStatus::Succeeded &&
        environment.FrameAllocator().Release(stale_frame) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        environment.StackManager().Validate() == os::kernel::KernelStackManagerStatus::Succeeded;
    test_context.Expect(stale_mapping_rejected && stale_mapping_cleaned,
                        OS_TEST_KERNEL_STACK_STALE_MAPPING);

    os::kernel::KernelVirtualAddressRange full_range{};
    const bool virtual_address_exhaustion_valid =
        environment.VirtualAddressAllocator().TryAllocate(
            OS_TEST_KERNEL_STACK_EXTERNAL_FULL_RANGE_PAGE_COUNT, OS_TEST_KERNEL_STACK_SINGLE_UNIT,
            full_range) == os::kernel::KernelVirtualAddressAllocatorStatus::Succeeded &&
        environment.StackManager().TryCreate(OS_TEST_KERNEL_STACK_STALE_SLOT_INDEX) ==
            os::kernel::KernelStackManagerStatus::VirtualAddressAllocationFailed &&
        environment.VirtualAddressAllocator().TryRelease(full_range) ==
            os::kernel::KernelVirtualAddressAllocatorStatus::Succeeded;

    static os::test::KernelStackTestEnvironment exhaustion_environment{};
    const bool exhaustion_environment_initialized =
        exhaustion_environment.Initialize(OS_TEST_KERNEL_STACK_SINGLE_UNIT);
    static os::kernel::PhysicalFrame
        exhausted_frames[os::test::OS_TEST_KERNEL_STACK_ENVIRONMENT_PHYSICAL_PAGE_COUNT]{};
    uint64_t exhausted_frame_count = OS_TEST_KERNEL_STACK_EMPTY_VALUE;
    while (
        exhaustion_environment_initialized &&
        exhausted_frame_count < os::test::OS_TEST_KERNEL_STACK_ENVIRONMENT_PHYSICAL_PAGE_COUNT &&
        exhaustion_environment.FrameAllocator().Allocate(exhausted_frames[exhausted_frame_count]) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded) {
        ++exhausted_frame_count;
    }
    const bool physical_exhaustion_valid =
        exhausted_frame_count != OS_TEST_KERNEL_STACK_EMPTY_VALUE &&
        exhaustion_environment.StackManager().TryCreate(OS_TEST_KERNEL_STACK_WARMUP_SLOT_INDEX) ==
            os::kernel::KernelStackManagerStatus::FrameAllocationFailed &&
        exhaustion_environment.StackManager().Statistics().active_stack_count ==
            OS_TEST_KERNEL_STACK_EMPTY_VALUE &&
        exhaustion_environment.StackManager().Statistics().successful_creation_count ==
            OS_TEST_KERNEL_STACK_EMPTY_VALUE &&
        exhaustion_environment.VirtualAddressAllocator().Statistics().allocated_page_count ==
            OS_TEST_KERNEL_STACK_EMPTY_VALUE;
    bool exhausted_frames_released = true;
    while (exhausted_frame_count > OS_TEST_KERNEL_STACK_EMPTY_VALUE) {
        --exhausted_frame_count;
        exhausted_frames_released = exhaustion_environment.FrameAllocator().Release(
                                        exhausted_frames[exhausted_frame_count]) ==
                                        os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
                                    exhausted_frames_released;
    }
    test_context.Expect(virtual_address_exhaustion_valid && physical_exhaustion_valid &&
                            exhausted_frames_released &&
                            exhaustion_environment.StackManager().Validate() ==
                                os::kernel::KernelStackManagerStatus::Succeeded,
                        OS_TEST_KERNEL_STACK_EXHAUSTION);

    const bool corruption_created =
        environment.StackManager().TryCreate(OS_TEST_KERNEL_STACK_STALE_SLOT_INDEX) ==
            os::kernel::KernelStackManagerStatus::Succeeded &&
        environment.StackManager().Read(OS_TEST_KERNEL_STACK_STALE_SLOT_INDEX, stack) ==
            os::kernel::KernelStackManagerStatus::Succeeded;
    const os::kernel::PhysicalFrame externally_released_frame =
        stack.physical_frames[OS_TEST_KERNEL_STACK_EMPTY_VALUE];
    const bool missing_frame_ownership_detected =
        corruption_created &&
        environment.FrameAllocator().Release(externally_released_frame) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        environment.StackManager().Validate() ==
            os::kernel::KernelStackManagerStatus::CorruptedState &&
        environment.StackManager().TryDestroy(OS_TEST_KERNEL_STACK_STALE_SLOT_INDEX) ==
            os::kernel::KernelStackManagerStatus::CorruptedState;
    os::kernel::PhysicalFrame restored_frame{};
    const bool frame_ownership_restored =
        missing_frame_ownership_detected &&
        environment.FrameAllocator().Allocate(restored_frame) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        restored_frame.physical_address == externally_released_frame.physical_address &&
        environment.StackManager().Validate() == os::kernel::KernelStackManagerStatus::Succeeded;
    const bool missing_ownership_detected =
        frame_ownership_restored &&
        environment.VirtualAddressAllocator().TryRelease(stack.virtual_range) ==
            os::kernel::KernelVirtualAddressAllocatorStatus::Succeeded &&
        environment.StackManager().Validate() ==
            os::kernel::KernelStackManagerStatus::CorruptedState &&
        environment.StackManager().TryDestroy(OS_TEST_KERNEL_STACK_STALE_SLOT_INDEX) ==
            os::kernel::KernelStackManagerStatus::CorruptedState;
    os::kernel::KernelVirtualAddressRange restored_range{};
    const bool ownership_restored =
        missing_ownership_detected &&
        environment.VirtualAddressAllocator().TryAllocate(
            os::kernel::OS_KERNEL_STACK_RANGE_PAGE_COUNT, OS_TEST_KERNEL_STACK_SINGLE_UNIT,
            restored_range) == os::kernel::KernelVirtualAddressAllocatorStatus::Succeeded &&
        restored_range.begin_address == stack.virtual_range.begin_address &&
        restored_range.page_count == stack.virtual_range.page_count &&
        environment.StackManager().Validate() == os::kernel::KernelStackManagerStatus::Succeeded;
    const bool mapping_corruption_created =
        ownership_restored && environment.PageTableManager().UnmapPage(
                                  os::kernel::KernelStackMappedBeginAddress(stack)) ==
                                  os::kernel::PageTableStatus::Succeeded;
    const bool mapping_corruption_detected =
        mapping_corruption_created &&
        environment.StackManager().Validate() ==
            os::kernel::KernelStackManagerStatus::CorruptedState &&
        environment.StackManager().TryDestroy(OS_TEST_KERNEL_STACK_STALE_SLOT_INDEX) ==
            os::kernel::KernelStackManagerStatus::CorruptedState;
    const bool corruption_repaired =
        mapping_corruption_detected &&
        environment.PageTableManager().MapPage(
            os::kernel::KernelStackMappedBeginAddress(stack),
            stack.physical_frames[OS_TEST_KERNEL_STACK_EMPTY_VALUE].physical_address,
            stale_permissions) == os::kernel::PageTableStatus::Succeeded &&
        environment.StackManager().Validate() == os::kernel::KernelStackManagerStatus::Succeeded &&
        environment.StackManager().TryDestroy(OS_TEST_KERNEL_STACK_STALE_SLOT_INDEX) ==
            os::kernel::KernelStackManagerStatus::Succeeded &&
        environment.StackManager().Validate() == os::kernel::KernelStackManagerStatus::Succeeded;
    test_context.Expect(corruption_repaired, OS_TEST_KERNEL_STACK_CORRUPTION);
    return test_context.ExitCode();
}

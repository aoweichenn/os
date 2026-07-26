#include "kernel_stack_test_environment.hpp"
#include "os/kernel/arch/exception_frame.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_KERNEL_STACK_LIFECYCLE_SUITE_NAME =
    "boot/kernel_stack_lifecycle/integration";
constexpr std::string_view OS_TEST_KERNEL_STACK_LIFECYCLE_CREATION =
    "四个动态栈必须取得连续 KVA 所有权、独立物理页和双侧保护页";
constexpr std::string_view OS_TEST_KERNEL_STACK_LIFECYCLE_PROCESS_ROOT =
    "进程 CR3 必须共享内核高半栈映射且保持 supervisor 权限";
constexpr std::string_view OS_TEST_KERNEL_STACK_LIFECYCLE_CONTEXT =
    "初始用户特权帧必须完整落在每个动态内核栈的映射区";
constexpr std::string_view OS_TEST_KERNEL_STACK_LIFECYCLE_RECLAIM =
    "安全点逆序销毁全部栈后物理页、KVA 和管理器统计必须恢复基线";

constexpr uint64_t OS_TEST_KERNEL_STACK_LIFECYCLE_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_KERNEL_STACK_LIFECYCLE_SINGLE_UNIT = 1ULL;
constexpr uint64_t OS_TEST_KERNEL_STACK_LIFECYCLE_PROCESS_COUNT = 4ULL;
constexpr uint64_t OS_TEST_KERNEL_STACK_LIFECYCLE_MANAGER_CAPACITY = 8ULL;
constexpr uint64_t OS_TEST_KERNEL_STACK_LIFECYCLE_STACK_ALIGNMENT_BYTES = 16ULL;
constexpr uint64_t OS_TEST_KERNEL_STACK_LIFECYCLE_TEMPLATE_VIRTUAL_ADDRESS =
    os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_TEST_KERNEL_STACK_LIFECYCLE_TEMPLATE_PHYSICAL_ADDRESS =
    OS_TEST_KERNEL_STACK_LIFECYCLE_EMPTY_VALUE;
constexpr uint64_t OS_TEST_KERNEL_STACK_LIFECYCLE_WARMUP_SLOT_INDEX =
    OS_TEST_KERNEL_STACK_LIFECYCLE_MANAGER_CAPACITY - OS_TEST_KERNEL_STACK_LIFECYCLE_SINGLE_UNIT;
constexpr uint64_t OS_TEST_KERNEL_STACK_LIFECYCLE_EXPECTED_CREATION_COUNT =
    OS_TEST_KERNEL_STACK_LIFECYCLE_PROCESS_COUNT + OS_TEST_KERNEL_STACK_LIFECYCLE_SINGLE_UNIT;
constexpr uint64_t OS_TEST_KERNEL_STACK_LIFECYCLE_EXPECTED_PEAK_MAPPED_PAGE_COUNT =
    OS_TEST_KERNEL_STACK_LIFECYCLE_PROCESS_COUNT * os::kernel::OS_KERNEL_STACK_MAPPED_PAGE_COUNT;
constexpr uint64_t OS_TEST_KERNEL_STACK_LIFECYCLE_FIRST_PATTERN = 0x4B535441434B4631ULL;
constexpr uint64_t OS_TEST_KERNEL_STACK_LIFECYCLE_LAST_PATTERN = 0x4B535441434B4C34ULL;

[[nodiscard]] uint64_t ExpectedLowerGuardAddress(const uint64_t process_index) noexcept {
    const uint64_t preceding_page_count =
        OS_TEST_KERNEL_STACK_LIFECYCLE_SINGLE_UNIT +
        process_index * os::kernel::OS_KERNEL_STACK_RANGE_PAGE_COUNT;
    return os::test::OS_TEST_KERNEL_STACK_ENVIRONMENT_WINDOW_BASE +
           preceding_page_count * os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
}

[[nodiscard]] bool MappingMatches(os::kernel::PageTableManager &page_table_manager,
                                  const os::kernel::KernelStack &stack,
                                  const uint64_t data_page_index) noexcept {
    os::kernel::PageMapping mapping{};
    const uint64_t virtual_address = os::kernel::KernelStackMappedBeginAddress(stack) +
                                     data_page_index * os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
    return page_table_manager.QueryPage(virtual_address, mapping) ==
               os::kernel::PageTableStatus::Succeeded &&
           mapping.physical_address == stack.physical_frames[data_page_index].physical_address &&
           mapping.page_size_bytes == os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES &&
           mapping.permissions.writable && !mapping.permissions.executable &&
           !mapping.permissions.user_accessible && !mapping.permissions.cache_disabled;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_KERNEL_STACK_LIFECYCLE_SUITE_NAME};
    static os::test::KernelStackTestEnvironment environment{};
    const bool initialized =
        environment.Initialize(OS_TEST_KERNEL_STACK_LIFECYCLE_MANAGER_CAPACITY);
    const os::kernel::PagePermissions template_mapping_permissions{
        .writable = true,
        .executable = false,
        .user_accessible = false,
        .cache_disabled = false,
    };
    const bool template_root_ready =
        initialized && environment.PageTableManager().MapPage(
                           OS_TEST_KERNEL_STACK_LIFECYCLE_TEMPLATE_VIRTUAL_ADDRESS,
                           OS_TEST_KERNEL_STACK_LIFECYCLE_TEMPLATE_PHYSICAL_ADDRESS,
                           template_mapping_permissions) == os::kernel::PageTableStatus::Succeeded;
    const bool warmed_up =
        template_root_ready &&
        environment.StackManager().TryCreate(OS_TEST_KERNEL_STACK_LIFECYCLE_WARMUP_SLOT_INDEX) ==
            os::kernel::KernelStackManagerStatus::Succeeded &&
        environment.StackManager().TryDestroy(OS_TEST_KERNEL_STACK_LIFECYCLE_WARMUP_SLOT_INDEX) ==
            os::kernel::KernelStackManagerStatus::Succeeded;
    const os::kernel::PhysicalFrameAllocatorStatistics frames_before_stacks =
        environment.FrameAllocator().Statistics();
    const os::kernel::KernelVirtualAddressAllocatorStatistics virtual_addresses_before_stacks =
        environment.VirtualAddressAllocator().Statistics();

    os::kernel::KernelStack stacks[OS_TEST_KERNEL_STACK_LIFECYCLE_PROCESS_COUNT]{};
    bool creation_valid = warmed_up;
    for (uint64_t process_index = OS_TEST_KERNEL_STACK_LIFECYCLE_EMPTY_VALUE;
         creation_valid && process_index < OS_TEST_KERNEL_STACK_LIFECYCLE_PROCESS_COUNT;
         ++process_index) {
        creation_valid = environment.StackManager().TryCreate(process_index) ==
                             os::kernel::KernelStackManagerStatus::Succeeded &&
                         environment.StackManager().Read(process_index, stacks[process_index]) ==
                             os::kernel::KernelStackManagerStatus::Succeeded &&
                         os::kernel::KernelStackLowerGuardAddress(stacks[process_index]) ==
                             ExpectedLowerGuardAddress(process_index) &&
                         os::kernel::KernelStackTopAddress(stacks[process_index]) %
                                 OS_TEST_KERNEL_STACK_LIFECYCLE_STACK_ALIGNMENT_BYTES ==
                             OS_TEST_KERNEL_STACK_LIFECYCLE_EMPTY_VALUE;

        os::kernel::PageMapping ignored_mapping{};
        creation_valid = creation_valid &&
                         environment.PageTableManager().QueryPage(
                             os::kernel::KernelStackLowerGuardAddress(stacks[process_index]),
                             ignored_mapping) == os::kernel::PageTableStatus::NotMapped &&
                         environment.PageTableManager().QueryPage(
                             os::kernel::KernelStackUpperGuardAddress(stacks[process_index]),
                             ignored_mapping) == os::kernel::PageTableStatus::NotMapped;
        for (uint64_t data_page_index = OS_TEST_KERNEL_STACK_LIFECYCLE_EMPTY_VALUE;
             creation_valid && data_page_index < os::kernel::OS_KERNEL_STACK_MAPPED_PAGE_COUNT;
             ++data_page_index) {
            creation_valid = MappingMatches(environment.PageTableManager(), stacks[process_index],
                                            data_page_index);
        }
    }
    test_context.Expect(creation_valid, OS_TEST_KERNEL_STACK_LIFECYCLE_CREATION);

    bool context_frames_valid = creation_valid;
    for (uint64_t process_index = OS_TEST_KERNEL_STACK_LIFECYCLE_EMPTY_VALUE;
         context_frames_valid && process_index < OS_TEST_KERNEL_STACK_LIFECYCLE_PROCESS_COUNT;
         ++process_index) {
        const uint64_t context_frame_virtual_address =
            os::kernel::KernelStackTopAddress(stacks[process_index]) -
            os::kernel::OS_KERNEL_USER_PRIVILEGE_FRAME_SIZE_BYTES;
        context_frames_valid =
            environment.StackManager().Contains(
                process_index, context_frame_virtual_address,
                os::kernel::OS_KERNEL_USER_PRIVILEGE_FRAME_SIZE_BYTES) &&
            context_frame_virtual_address / os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES ==
                (os::kernel::KernelStackTopAddress(stacks[process_index]) -
                 OS_TEST_KERNEL_STACK_LIFECYCLE_SINGLE_UNIT) /
                    os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
        const uint64_t context_frame_page_offset =
            os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES -
            os::kernel::OS_KERNEL_USER_PRIVILEGE_FRAME_SIZE_BYTES;
        const uint64_t last_frame_index = os::kernel::OS_KERNEL_STACK_MAPPED_PAGE_COUNT -
                                          OS_TEST_KERNEL_STACK_LIFECYCLE_SINGLE_UNIT;
        os::kernel::UserPrivilegeFrame *const context_frame =
            reinterpret_cast<os::kernel::UserPrivilegeFrame *>(
                environment.PhysicalMemory() +
                stacks[process_index].physical_frames[last_frame_index].physical_address +
                context_frame_page_offset);
        *context_frame = os::kernel::UserPrivilegeFrame{};
        context_frame->common.register_rdi =
            OS_TEST_KERNEL_STACK_LIFECYCLE_FIRST_PATTERN + process_index;
        context_frame->common.instruction_pointer =
            OS_TEST_KERNEL_STACK_LIFECYCLE_LAST_PATTERN + process_index;
        context_frames_valid = context_frames_valid &&
                               context_frame->common.register_rdi ==
                                   OS_TEST_KERNEL_STACK_LIFECYCLE_FIRST_PATTERN + process_index &&
                               context_frame->common.instruction_pointer ==
                                   OS_TEST_KERNEL_STACK_LIFECYCLE_LAST_PATTERN + process_index;
    }
    test_context.Expect(context_frames_valid, OS_TEST_KERNEL_STACK_LIFECYCLE_CONTEXT);

    os::kernel::PageTableManager process_page_table{
        environment.FrameAllocator(),
        os::kernel::PageTableMemoryAccess{
            .maximum_physical_address_exclusive =
                os::test::OS_TEST_KERNEL_STACK_ENVIRONMENT_PHYSICAL_MEMORY_SIZE_BYTES,
            .physical_memory_virtual_base =
                reinterpret_cast<uint64_t>(environment.PhysicalMemory()),
            .allocation_maximum_physical_address_exclusive =
                os::test::OS_TEST_KERNEL_STACK_ENVIRONMENT_PHYSICAL_MEMORY_SIZE_BYTES,
            .invalidate_active_mappings = false,
        },
        os::kernel::PageTableRootKind::Process,
    };
    bool process_root_valid = process_page_table.InitializeProcessRoot(
                                  environment.PageTableManager().RootPhysicalAddress()) ==
                              os::kernel::PageTableStatus::Succeeded;
    for (uint64_t process_index = OS_TEST_KERNEL_STACK_LIFECYCLE_EMPTY_VALUE;
         process_root_valid && process_index < OS_TEST_KERNEL_STACK_LIFECYCLE_PROCESS_COUNT;
         ++process_index) {
        os::kernel::PageMapping ignored_mapping{};
        process_root_valid = process_page_table.QueryPage(
                                 os::kernel::KernelStackLowerGuardAddress(stacks[process_index]),
                                 ignored_mapping) == os::kernel::PageTableStatus::NotMapped &&
                             process_page_table.QueryPage(
                                 os::kernel::KernelStackUpperGuardAddress(stacks[process_index]),
                                 ignored_mapping) == os::kernel::PageTableStatus::NotMapped;
        for (uint64_t data_page_index = OS_TEST_KERNEL_STACK_LIFECYCLE_EMPTY_VALUE;
             process_root_valid && data_page_index < os::kernel::OS_KERNEL_STACK_MAPPED_PAGE_COUNT;
             ++data_page_index) {
            process_root_valid =
                MappingMatches(process_page_table, stacks[process_index], data_page_index);
        }
    }
    process_root_valid =
        process_page_table.ReleaseProcessRoot() == os::kernel::PageTableStatus::Succeeded &&
        process_root_valid;
    test_context.Expect(process_root_valid, OS_TEST_KERNEL_STACK_LIFECYCLE_PROCESS_ROOT);

    bool destruction_valid = true;
    for (uint64_t remaining_process_count = OS_TEST_KERNEL_STACK_LIFECYCLE_PROCESS_COUNT;
         remaining_process_count > OS_TEST_KERNEL_STACK_LIFECYCLE_EMPTY_VALUE;
         --remaining_process_count) {
        destruction_valid =
            environment.StackManager().TryDestroy(remaining_process_count -
                                                  OS_TEST_KERNEL_STACK_LIFECYCLE_SINGLE_UNIT) ==
                os::kernel::KernelStackManagerStatus::Succeeded &&
            destruction_valid;
    }
    bool released_frames_zero = true;
    for (uint64_t process_index = OS_TEST_KERNEL_STACK_LIFECYCLE_EMPTY_VALUE;
         process_index < OS_TEST_KERNEL_STACK_LIFECYCLE_PROCESS_COUNT; ++process_index) {
        for (uint64_t frame_index = OS_TEST_KERNEL_STACK_LIFECYCLE_EMPTY_VALUE;
             frame_index < os::kernel::OS_KERNEL_STACK_MAPPED_PAGE_COUNT; ++frame_index) {
            const uint64_t *const values = reinterpret_cast<const uint64_t *>(
                environment.PhysicalMemory() +
                stacks[process_index].physical_frames[frame_index].physical_address);
            released_frames_zero =
                released_frames_zero && values[OS_TEST_KERNEL_STACK_LIFECYCLE_EMPTY_VALUE] ==
                                            OS_TEST_KERNEL_STACK_LIFECYCLE_EMPTY_VALUE;
        }
    }
    const os::kernel::PhysicalFrameAllocatorStatistics frames_after_stacks =
        environment.FrameAllocator().Statistics();
    const os::kernel::KernelVirtualAddressAllocatorStatistics virtual_addresses_after_stacks =
        environment.VirtualAddressAllocator().Statistics();
    const os::kernel::KernelStackManagerStatistics stack_statistics =
        environment.StackManager().Statistics();
    test_context.Expect(
        destruction_valid && released_frames_zero &&
            frames_after_stacks.free_frame_count == frames_before_stacks.free_frame_count &&
            frames_after_stacks.allocated_frame_count ==
                frames_before_stacks.allocated_frame_count &&
            virtual_addresses_after_stacks.free_page_count ==
                virtual_addresses_before_stacks.free_page_count &&
            virtual_addresses_after_stacks.allocated_page_count ==
                virtual_addresses_before_stacks.allocated_page_count &&
            virtual_addresses_after_stacks.successful_allocation_count ==
                virtual_addresses_before_stacks.successful_allocation_count +
                    OS_TEST_KERNEL_STACK_LIFECYCLE_PROCESS_COUNT &&
            virtual_addresses_after_stacks.release_count ==
                virtual_addresses_before_stacks.release_count +
                    OS_TEST_KERNEL_STACK_LIFECYCLE_PROCESS_COUNT &&
            stack_statistics.active_stack_count == OS_TEST_KERNEL_STACK_LIFECYCLE_EMPTY_VALUE &&
            stack_statistics.successful_creation_count ==
                OS_TEST_KERNEL_STACK_LIFECYCLE_EXPECTED_CREATION_COUNT &&
            stack_statistics.destruction_count ==
                OS_TEST_KERNEL_STACK_LIFECYCLE_EXPECTED_CREATION_COUNT &&
            stack_statistics.peak_active_stack_count ==
                OS_TEST_KERNEL_STACK_LIFECYCLE_PROCESS_COUNT &&
            stack_statistics.peak_active_mapped_page_count ==
                OS_TEST_KERNEL_STACK_LIFECYCLE_EXPECTED_PEAK_MAPPED_PAGE_COUNT &&
            environment.StackManager().Validate() ==
                os::kernel::KernelStackManagerStatus::Succeeded,
        OS_TEST_KERNEL_STACK_LIFECYCLE_RECLAIM);
    return test_context.ExitCode();
}

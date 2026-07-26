#include "page_table_test_environment.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_PAGE_TABLE_LIFECYCLE_SUITE_NAME =
    "boot/page_table_reclamation_lifecycle/integration";
constexpr std::string_view OS_TEST_PAGE_TABLE_LIFECYCLE_KERNEL_SHARED =
    "内核共享分支反复建立与拆除后必须只保留一个稳定三级表";
constexpr std::string_view OS_TEST_PAGE_TABLE_LIFECYCLE_PROCESS =
    "多轮进程地址空间建立、撤销与销毁必须恢复同一页帧基线";
constexpr std::string_view OS_TEST_PAGE_TABLE_LIFECYCLE_PROCESS_FALLBACK =
    "进程整体销毁必须递归回收尚未逐页撤销的自有映射";

constexpr uint64_t OS_TEST_PAGE_TABLE_LIFECYCLE_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_PAGE_TABLE_LIFECYCLE_SINGLE_ENTRY = 1ULL;
constexpr uint64_t OS_TEST_PAGE_TABLE_LIFECYCLE_TWO_ENTRIES = 2ULL;
constexpr uint64_t OS_TEST_PAGE_TABLE_LIFECYCLE_THREE_ENTRIES = 3ULL;
constexpr uint64_t OS_TEST_PAGE_TABLE_LIFECYCLE_FOUR_ENTRIES = 4ULL;
constexpr uint64_t OS_TEST_PAGE_TABLE_LIFECYCLE_KERNEL_ITERATION_COUNT = 128ULL;
constexpr uint64_t OS_TEST_PAGE_TABLE_LIFECYCLE_PROCESS_ITERATION_COUNT = 64ULL;
constexpr uint64_t OS_TEST_PAGE_TABLE_LIFECYCLE_KERNEL_LEVEL3_VARIATION_COUNT = 4ULL;
constexpr uint64_t OS_TEST_PAGE_TABLE_LIFECYCLE_KERNEL_LEVEL2_VARIATION_COUNT = 8ULL;
constexpr uint64_t OS_TEST_PAGE_TABLE_LIFECYCLE_LEVEL3_SHIFT = 30ULL;
constexpr uint64_t OS_TEST_PAGE_TABLE_LIFECYCLE_LEVEL2_SHIFT = 21ULL;
constexpr uint64_t OS_TEST_PAGE_TABLE_LIFECYCLE_KERNEL_BASE_ADDRESS = 0xFFFFC90000001000ULL;
constexpr uint64_t OS_TEST_PAGE_TABLE_LIFECYCLE_KERNEL_LOW_ADDRESS = 0x0000000000200000ULL;
constexpr uint64_t OS_TEST_PAGE_TABLE_LIFECYCLE_USER_PROGRAM_ADDRESS = 0x0000000040001000ULL;
constexpr uint64_t OS_TEST_PAGE_TABLE_LIFECYCLE_USER_STACK_ADDRESS = 0x00007F8000001000ULL;

constexpr os::kernel::PagePermissions OS_TEST_PAGE_TABLE_LIFECYCLE_KERNEL_PERMISSIONS{
    .writable = true,
    .executable = false,
    .user_accessible = false,
    .cache_disabled = false,
};
constexpr os::kernel::PagePermissions OS_TEST_PAGE_TABLE_LIFECYCLE_USER_PERMISSIONS{
    .writable = true,
    .executable = false,
    .user_accessible = true,
    .cache_disabled = false,
};

[[nodiscard]] uint64_t KernelTestAddress(const uint64_t iteration) noexcept {
    const uint64_t level3_variation =
        iteration % OS_TEST_PAGE_TABLE_LIFECYCLE_KERNEL_LEVEL3_VARIATION_COUNT;
    const uint64_t level2_variation =
        (iteration / OS_TEST_PAGE_TABLE_LIFECYCLE_KERNEL_LEVEL3_VARIATION_COUNT) %
        OS_TEST_PAGE_TABLE_LIFECYCLE_KERNEL_LEVEL2_VARIATION_COUNT;
    return OS_TEST_PAGE_TABLE_LIFECYCLE_KERNEL_BASE_ADDRESS +
           (level3_variation << OS_TEST_PAGE_TABLE_LIFECYCLE_LEVEL3_SHIFT) +
           (level2_variation << OS_TEST_PAGE_TABLE_LIFECYCLE_LEVEL2_SHIFT);
}

[[nodiscard]] bool IsNoTableReclaim(const os::kernel::PageTableUnmapResult result) noexcept {
    return result.reclaimed_table_frame_count == OS_TEST_PAGE_TABLE_LIFECYCLE_EMPTY_VALUE;
}

[[nodiscard]] bool IsLowerTwoTableReclaim(const os::kernel::PageTableUnmapResult result) noexcept {
    return result.reclaimed_level1_table_count == OS_TEST_PAGE_TABLE_LIFECYCLE_SINGLE_ENTRY &&
           result.reclaimed_level2_table_count == OS_TEST_PAGE_TABLE_LIFECYCLE_SINGLE_ENTRY &&
           result.reclaimed_level3_table_count == OS_TEST_PAGE_TABLE_LIFECYCLE_EMPTY_VALUE &&
           result.reclaimed_table_frame_count == OS_TEST_PAGE_TABLE_LIFECYCLE_TWO_ENTRIES;
}

[[nodiscard]] bool IsFullTableReclaim(const os::kernel::PageTableUnmapResult result) noexcept {
    return result.reclaimed_level1_table_count == OS_TEST_PAGE_TABLE_LIFECYCLE_SINGLE_ENTRY &&
           result.reclaimed_level2_table_count == OS_TEST_PAGE_TABLE_LIFECYCLE_SINGLE_ENTRY &&
           result.reclaimed_level3_table_count == OS_TEST_PAGE_TABLE_LIFECYCLE_SINGLE_ENTRY &&
           result.reclaimed_table_frame_count == OS_TEST_PAGE_TABLE_LIFECYCLE_THREE_ENTRIES;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_PAGE_TABLE_LIFECYCLE_SUITE_NAME};
    static os::test::PageTableTestEnvironment environment{
        os::kernel::PageTableRootKind::KernelShared,
    };
    const bool initialized = environment.Initialize();
    const os::kernel::PhysicalFrameAllocatorStatistics initial_statistics =
        environment.FrameAllocator().Statistics();
    bool kernel_lifecycle_valid = initialized;
    for (uint64_t iteration = OS_TEST_PAGE_TABLE_LIFECYCLE_EMPTY_VALUE;
         kernel_lifecycle_valid && iteration < OS_TEST_PAGE_TABLE_LIFECYCLE_KERNEL_ITERATION_COUNT;
         ++iteration) {
        os::kernel::PhysicalFrame data_frame{};
        os::kernel::PageTableUnmapResult result{};
        const uint64_t virtual_address = KernelTestAddress(iteration);
        kernel_lifecycle_valid =
            environment.FrameAllocator().Allocate(data_frame) ==
                os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
            environment.PageTableManager().MapPage(
                virtual_address, data_frame.physical_address,
                OS_TEST_PAGE_TABLE_LIFECYCLE_KERNEL_PERMISSIONS) ==
                os::kernel::PageTableStatus::Succeeded &&
            environment.PageTableManager().UnmapPage(virtual_address, result) ==
                os::kernel::PageTableStatus::Succeeded &&
            IsLowerTwoTableReclaim(result) &&
            environment.FrameAllocator().Release(data_frame) ==
                os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
            environment.FrameAllocator().Statistics().allocated_frame_count ==
                initial_statistics.allocated_frame_count +
                    OS_TEST_PAGE_TABLE_LIFECYCLE_SINGLE_ENTRY &&
            environment.FrameAllocator().ValidateBuddy() ==
                os::kernel::PhysicalFrameAllocatorStatus::Succeeded;
    }
    test_context.Expect(kernel_lifecycle_valid, OS_TEST_PAGE_TABLE_LIFECYCLE_KERNEL_SHARED);

    os::kernel::PhysicalFrame kernel_low_frame{};
    const bool process_template_ready =
        kernel_lifecycle_valid &&
        environment.FrameAllocator().Allocate(kernel_low_frame) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        environment.PageTableManager().MapPage(OS_TEST_PAGE_TABLE_LIFECYCLE_KERNEL_LOW_ADDRESS,
                                               kernel_low_frame.physical_address,
                                               OS_TEST_PAGE_TABLE_LIFECYCLE_KERNEL_PERMISSIONS) ==
            os::kernel::PageTableStatus::Succeeded;
    const os::kernel::PhysicalFrameAllocatorStatistics process_baseline =
        environment.FrameAllocator().Statistics();
    bool process_lifecycle_valid = process_template_ready;
    for (uint64_t iteration = OS_TEST_PAGE_TABLE_LIFECYCLE_EMPTY_VALUE;
         process_lifecycle_valid &&
         iteration < OS_TEST_PAGE_TABLE_LIFECYCLE_PROCESS_ITERATION_COUNT;
         ++iteration) {
        os::kernel::PageTableManager process_page_table{
            environment.FrameAllocator(),
            environment.MemoryAccess(),
            os::kernel::PageTableRootKind::Process,
        };
        os::kernel::PhysicalFrame data_frames[OS_TEST_PAGE_TABLE_LIFECYCLE_FOUR_ENTRIES]{};
        process_lifecycle_valid = process_page_table.InitializeProcessRoot(
                                      environment.PageTableManager().RootPhysicalAddress()) ==
                                  os::kernel::PageTableStatus::Succeeded;
        for (uint64_t frame_index = OS_TEST_PAGE_TABLE_LIFECYCLE_EMPTY_VALUE;
             process_lifecycle_valid && frame_index < OS_TEST_PAGE_TABLE_LIFECYCLE_FOUR_ENTRIES;
             ++frame_index) {
            process_lifecycle_valid =
                environment.FrameAllocator().Allocate(data_frames[frame_index]) ==
                os::kernel::PhysicalFrameAllocatorStatus::Succeeded;
        }
        process_lifecycle_valid =
            process_lifecycle_valid &&
            process_page_table.MapPage(
                OS_TEST_PAGE_TABLE_LIFECYCLE_USER_PROGRAM_ADDRESS,
                data_frames[OS_TEST_PAGE_TABLE_LIFECYCLE_EMPTY_VALUE].physical_address,
                OS_TEST_PAGE_TABLE_LIFECYCLE_USER_PERMISSIONS) ==
                os::kernel::PageTableStatus::Succeeded &&
            process_page_table.MapPage(
                OS_TEST_PAGE_TABLE_LIFECYCLE_USER_PROGRAM_ADDRESS +
                    os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES,
                data_frames[OS_TEST_PAGE_TABLE_LIFECYCLE_SINGLE_ENTRY].physical_address,
                OS_TEST_PAGE_TABLE_LIFECYCLE_USER_PERMISSIONS) ==
                os::kernel::PageTableStatus::Succeeded &&
            process_page_table.MapPage(
                OS_TEST_PAGE_TABLE_LIFECYCLE_USER_STACK_ADDRESS,
                data_frames[OS_TEST_PAGE_TABLE_LIFECYCLE_TWO_ENTRIES].physical_address,
                OS_TEST_PAGE_TABLE_LIFECYCLE_USER_PERMISSIONS) ==
                os::kernel::PageTableStatus::Succeeded &&
            process_page_table.MapPage(
                OS_TEST_PAGE_TABLE_LIFECYCLE_USER_STACK_ADDRESS +
                    os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES,
                data_frames[OS_TEST_PAGE_TABLE_LIFECYCLE_THREE_ENTRIES].physical_address,
                OS_TEST_PAGE_TABLE_LIFECYCLE_USER_PERMISSIONS) ==
                os::kernel::PageTableStatus::Succeeded;
        os::kernel::PageMapping borrowed_mapping{};
        process_lifecycle_valid =
            process_lifecycle_valid &&
            process_page_table.QueryPage(OS_TEST_PAGE_TABLE_LIFECYCLE_KERNEL_LOW_ADDRESS,
                                         borrowed_mapping) ==
                os::kernel::PageTableStatus::Succeeded &&
            borrowed_mapping.physical_address == kernel_low_frame.physical_address;

        os::kernel::PageTableUnmapResult program_first_result{};
        os::kernel::PageTableUnmapResult program_last_result{};
        os::kernel::PageTableUnmapResult stack_first_result{};
        os::kernel::PageTableUnmapResult stack_last_result{};
        process_lifecycle_valid =
            process_lifecycle_valid &&
            process_page_table.UnmapPage(OS_TEST_PAGE_TABLE_LIFECYCLE_USER_PROGRAM_ADDRESS,
                                         program_first_result) ==
                os::kernel::PageTableStatus::Succeeded &&
            IsNoTableReclaim(program_first_result) &&
            process_page_table.UnmapPage(OS_TEST_PAGE_TABLE_LIFECYCLE_USER_PROGRAM_ADDRESS +
                                             os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES,
                                         program_last_result) ==
                os::kernel::PageTableStatus::Succeeded &&
            IsLowerTwoTableReclaim(program_last_result) &&
            process_page_table.UnmapPage(OS_TEST_PAGE_TABLE_LIFECYCLE_USER_STACK_ADDRESS,
                                         stack_first_result) ==
                os::kernel::PageTableStatus::Succeeded &&
            IsNoTableReclaim(stack_first_result) &&
            process_page_table.UnmapPage(OS_TEST_PAGE_TABLE_LIFECYCLE_USER_STACK_ADDRESS +
                                             os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES,
                                         stack_last_result) ==
                os::kernel::PageTableStatus::Succeeded &&
            IsFullTableReclaim(stack_last_result);
        for (uint64_t frame_index = OS_TEST_PAGE_TABLE_LIFECYCLE_EMPTY_VALUE;
             process_lifecycle_valid && frame_index < OS_TEST_PAGE_TABLE_LIFECYCLE_FOUR_ENTRIES;
             ++frame_index) {
            process_lifecycle_valid =
                environment.FrameAllocator().Release(data_frames[frame_index]) ==
                os::kernel::PhysicalFrameAllocatorStatus::Succeeded;
        }
        process_lifecycle_valid =
            process_lifecycle_valid &&
            process_page_table.ReleaseProcessRoot() == os::kernel::PageTableStatus::Succeeded &&
            environment.FrameAllocator().Statistics().allocated_frame_count ==
                process_baseline.allocated_frame_count &&
            environment.FrameAllocator().ValidateBuddy() ==
                os::kernel::PhysicalFrameAllocatorStatus::Succeeded;
    }
    test_context.Expect(process_lifecycle_valid, OS_TEST_PAGE_TABLE_LIFECYCLE_PROCESS);

    const os::kernel::PhysicalFrameAllocatorStatistics fallback_baseline =
        environment.FrameAllocator().Statistics();
    os::kernel::PageTableManager fallback_process_page_table{
        environment.FrameAllocator(),
        environment.MemoryAccess(),
        os::kernel::PageTableRootKind::Process,
    };
    os::kernel::PhysicalFrame fallback_program_frame{};
    os::kernel::PhysicalFrame fallback_stack_frame{};
    const bool fallback_valid =
        process_lifecycle_valid &&
        fallback_process_page_table.InitializeProcessRoot(
            environment.PageTableManager().RootPhysicalAddress()) ==
            os::kernel::PageTableStatus::Succeeded &&
        environment.FrameAllocator().Allocate(fallback_program_frame) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        environment.FrameAllocator().Allocate(fallback_stack_frame) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        fallback_process_page_table.MapPage(OS_TEST_PAGE_TABLE_LIFECYCLE_USER_PROGRAM_ADDRESS,
                                            fallback_program_frame.physical_address,
                                            OS_TEST_PAGE_TABLE_LIFECYCLE_USER_PERMISSIONS) ==
            os::kernel::PageTableStatus::Succeeded &&
        fallback_process_page_table.MapPage(OS_TEST_PAGE_TABLE_LIFECYCLE_USER_STACK_ADDRESS,
                                            fallback_stack_frame.physical_address,
                                            OS_TEST_PAGE_TABLE_LIFECYCLE_USER_PERMISSIONS) ==
            os::kernel::PageTableStatus::Succeeded &&
        fallback_process_page_table.ReleaseProcessRoot() ==
            os::kernel::PageTableStatus::Succeeded &&
        environment.FrameAllocator().Statistics().allocated_frame_count ==
            fallback_baseline.allocated_frame_count &&
        environment.FrameAllocator().ValidateBuddy() ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded;
    test_context.Expect(fallback_valid, OS_TEST_PAGE_TABLE_LIFECYCLE_PROCESS_FALLBACK);
    return test_context.ExitCode();
}

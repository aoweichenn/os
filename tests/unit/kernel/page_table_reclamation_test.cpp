#include "page_table_test_environment.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_PAGE_TABLE_RECLAIM_SUITE_NAME =
    "kernel/page_table_reclamation/unit";
constexpr std::string_view OS_TEST_PAGE_TABLE_RECLAIM_ROOT_MODE =
    "页表根类型必须约束初始化入口并拒绝重复初始化";
constexpr std::string_view OS_TEST_PAGE_TABLE_RECLAIM_SINGLE_PAGE =
    "独占根最后一页撤销后必须逐级回收三级中间表";
constexpr std::string_view OS_TEST_PAGE_TABLE_RECLAIM_ADJACENT_PAGES =
    "同一末级表仍有映射时不得提前回收，最后一页才允许级联回收";
constexpr std::string_view OS_TEST_PAGE_TABLE_RECLAIM_SHARED_ROOT =
    "内核共享根必须回收一级和二级表，同时保留可能被进程引用的三级表";
constexpr std::string_view OS_TEST_PAGE_TABLE_RECLAIM_PROCESS_BOUNDARY =
    "进程根只能修改自有用户分支，并且必须保持借用的内核映射有效";
constexpr std::string_view OS_TEST_PAGE_TABLE_RECLAIM_PROCESS_RELEASE_CYCLE =
    "进程根递归销毁必须在释放任何祖先表前拒绝下级回指并允许修复后重试";
constexpr std::string_view OS_TEST_PAGE_TABLE_RECLAIM_ROLLBACK =
    "中间表分配失败必须逆序回滚帧和父项修改";
constexpr std::string_view OS_TEST_PAGE_TABLE_RECLAIM_OWNERSHIP =
    "不再归分配器所有的页表帧必须在改动映射前被拒绝";
constexpr std::string_view OS_TEST_PAGE_TABLE_RECLAIM_CORRUPTION =
    "越界子表地址和页表环必须返回错误而不能越界解引用";

constexpr uint64_t OS_TEST_PAGE_TABLE_RECLAIM_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_PAGE_TABLE_RECLAIM_SINGLE_ENTRY = 1ULL;
constexpr uint64_t OS_TEST_PAGE_TABLE_RECLAIM_TWO_ENTRIES = 2ULL;
constexpr uint64_t OS_TEST_PAGE_TABLE_RECLAIM_THREE_ENTRIES = 3ULL;
constexpr uint64_t OS_TEST_PAGE_TABLE_RECLAIM_FIVE_PAGES = 5ULL;
constexpr uint64_t OS_TEST_PAGE_TABLE_RECLAIM_ENTRY_PRESENT_WRITABLE = 0x3ULL;
constexpr uint64_t OS_TEST_PAGE_TABLE_RECLAIM_ENTRY_ADDRESS_MASK = 0x000FFFFFFFFFF000ULL;
constexpr uint64_t OS_TEST_PAGE_TABLE_RECLAIM_LEVEL4_SHIFT = 39ULL;
constexpr uint64_t OS_TEST_PAGE_TABLE_RECLAIM_LEVEL4_INDEX_MASK = 0x1FFULL;
constexpr uint64_t OS_TEST_PAGE_TABLE_RECLAIM_INVALID_ROOT_KIND_VALUE = 99ULL;
constexpr uint64_t OS_TEST_PAGE_TABLE_RECLAIM_RESULT_SENTINEL = 0x55ULL;
constexpr uint64_t OS_TEST_PAGE_TABLE_RECLAIM_BASIC_ADDRESS = 0x0000000200001000ULL;
constexpr uint64_t OS_TEST_PAGE_TABLE_RECLAIM_ADJACENT_ADDRESS = 0x0000000400002000ULL;
constexpr uint64_t OS_TEST_PAGE_TABLE_RECLAIM_KERNEL_SHARED_ADDRESS = 0xFFFFC90000001000ULL;
constexpr uint64_t OS_TEST_PAGE_TABLE_RECLAIM_KERNEL_LOW_ADDRESS = 0x0000000000200000ULL;
constexpr uint64_t OS_TEST_PAGE_TABLE_RECLAIM_USER_PROGRAM_ADDRESS = 0x0000000040001000ULL;
constexpr uint64_t OS_TEST_PAGE_TABLE_RECLAIM_USER_STACK_ADDRESS = 0x00007F8000001000ULL;
constexpr uint64_t OS_TEST_PAGE_TABLE_RECLAIM_ROLLBACK_ADDRESS = 0x0000020000001000ULL;
constexpr uint64_t OS_TEST_PAGE_TABLE_RECLAIM_OWNERSHIP_ADDRESS = 0x0000040000001000ULL;
constexpr uint64_t OS_TEST_PAGE_TABLE_RECLAIM_QUERY_OFFSET = 37ULL;

constexpr os::kernel::PagePermissions OS_TEST_PAGE_TABLE_RECLAIM_KERNEL_PERMISSIONS{
    .writable = true,
    .executable = false,
    .user_accessible = false,
    .cache_disabled = false,
};
constexpr os::kernel::PagePermissions OS_TEST_PAGE_TABLE_RECLAIM_USER_PERMISSIONS{
    .writable = true,
    .executable = false,
    .user_accessible = true,
    .cache_disabled = false,
};

[[nodiscard]] uint64_t ChildTableAddress(const uint64_t entry) noexcept {
    return entry & OS_TEST_PAGE_TABLE_RECLAIM_ENTRY_ADDRESS_MASK;
}

[[nodiscard]] uint64_t Level4Index(const uint64_t virtual_address) noexcept {
    return (virtual_address >> OS_TEST_PAGE_TABLE_RECLAIM_LEVEL4_SHIFT) &
           OS_TEST_PAGE_TABLE_RECLAIM_LEVEL4_INDEX_MASK;
}

[[nodiscard]] bool IsFullReclaim(const os::kernel::PageTableUnmapResult result) noexcept {
    return result.reclaimed_level1_table_count == OS_TEST_PAGE_TABLE_RECLAIM_SINGLE_ENTRY &&
           result.reclaimed_level2_table_count == OS_TEST_PAGE_TABLE_RECLAIM_SINGLE_ENTRY &&
           result.reclaimed_level3_table_count == OS_TEST_PAGE_TABLE_RECLAIM_SINGLE_ENTRY &&
           result.reclaimed_table_frame_count == OS_TEST_PAGE_TABLE_RECLAIM_THREE_ENTRIES;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_PAGE_TABLE_RECLAIM_SUITE_NAME};

    static os::test::PageTableTestEnvironment basic_environment{
        os::kernel::PageTableRootKind::Exclusive,
    };
    const bool basic_initialized = basic_environment.Initialize();
    os::kernel::PageTableManager invalid_root_manager{
        basic_environment.FrameAllocator(),
        basic_environment.MemoryAccess(),
        static_cast<os::kernel::PageTableRootKind>(
            OS_TEST_PAGE_TABLE_RECLAIM_INVALID_ROOT_KIND_VALUE),
    };
    os::kernel::PageTableManager process_mode_manager{
        basic_environment.FrameAllocator(),
        basic_environment.MemoryAccess(),
        os::kernel::PageTableRootKind::Process,
    };
    test_context.Expect(
        basic_initialized &&
            basic_environment.PageTableManager().RootKind() ==
                os::kernel::PageTableRootKind::Exclusive &&
            basic_environment.PageTableManager().Initialize() ==
                os::kernel::PageTableStatus::AlreadyInitialized &&
            basic_environment.PageTableManager().InitializeProcessRoot(
                basic_environment.PageTableManager().RootPhysicalAddress()) ==
                os::kernel::PageTableStatus::InvalidRootKind &&
            process_mode_manager.Initialize() == os::kernel::PageTableStatus::InvalidRootKind &&
            invalid_root_manager.Initialize() == os::kernel::PageTableStatus::InvalidRootKind,
        OS_TEST_PAGE_TABLE_RECLAIM_ROOT_MODE);

    const os::kernel::PhysicalFrameAllocatorStatistics basic_baseline =
        basic_environment.FrameAllocator().Statistics();
    os::kernel::PhysicalFrame basic_data_frame{};
    const bool basic_mapped =
        basic_initialized &&
        basic_environment.FrameAllocator().Allocate(basic_data_frame) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        basic_environment.PageTableManager().MapPage(
            OS_TEST_PAGE_TABLE_RECLAIM_BASIC_ADDRESS, basic_data_frame.physical_address,
            OS_TEST_PAGE_TABLE_RECLAIM_KERNEL_PERMISSIONS) ==
            os::kernel::PageTableStatus::Succeeded;
    os::kernel::PageMapping offset_mapping{};
    const bool offset_query_valid =
        basic_mapped &&
        basic_environment.PageTableManager().QueryPage(
            OS_TEST_PAGE_TABLE_RECLAIM_BASIC_ADDRESS + OS_TEST_PAGE_TABLE_RECLAIM_QUERY_OFFSET,
            offset_mapping) == os::kernel::PageTableStatus::Succeeded &&
        offset_mapping.physical_address ==
            basic_data_frame.physical_address + OS_TEST_PAGE_TABLE_RECLAIM_QUERY_OFFSET;
    os::kernel::PageTableUnmapResult basic_result{};
    const bool basic_unmapped = offset_query_valid &&
                                basic_environment.PageTableManager().UnmapPage(
                                    OS_TEST_PAGE_TABLE_RECLAIM_BASIC_ADDRESS, basic_result) ==
                                    os::kernel::PageTableStatus::Succeeded &&
                                IsFullReclaim(basic_result);
    os::kernel::PageTableUnmapResult preserved_result{
        .reclaimed_level1_table_count = OS_TEST_PAGE_TABLE_RECLAIM_RESULT_SENTINEL,
        .reclaimed_level2_table_count = OS_TEST_PAGE_TABLE_RECLAIM_RESULT_SENTINEL,
        .reclaimed_level3_table_count = OS_TEST_PAGE_TABLE_RECLAIM_RESULT_SENTINEL,
        .reclaimed_table_frame_count = OS_TEST_PAGE_TABLE_RECLAIM_RESULT_SENTINEL,
    };
    const bool failed_output_preserved =
        basic_environment.PageTableManager().UnmapPage(OS_TEST_PAGE_TABLE_RECLAIM_BASIC_ADDRESS,
                                                       preserved_result) ==
            os::kernel::PageTableStatus::NotMapped &&
        preserved_result.reclaimed_table_frame_count == OS_TEST_PAGE_TABLE_RECLAIM_RESULT_SENTINEL;
    const bool basic_released = basic_environment.FrameAllocator().Release(basic_data_frame) ==
                                os::kernel::PhysicalFrameAllocatorStatus::Succeeded;
    const os::kernel::PhysicalFrameAllocatorStatistics basic_after =
        basic_environment.FrameAllocator().Statistics();
    test_context.Expect(basic_unmapped && failed_output_preserved && basic_released &&
                            basic_after.allocated_frame_count ==
                                basic_baseline.allocated_frame_count &&
                            basic_after.free_frame_count == basic_baseline.free_frame_count,
                        OS_TEST_PAGE_TABLE_RECLAIM_SINGLE_PAGE);

    static os::test::PageTableTestEnvironment adjacent_environment{
        os::kernel::PageTableRootKind::Exclusive,
    };
    os::kernel::PhysicalFrame adjacent_frames[OS_TEST_PAGE_TABLE_RECLAIM_TWO_ENTRIES]{};
    bool adjacent_valid =
        adjacent_environment.Initialize() &&
        adjacent_environment.FrameAllocator().Allocate(adjacent_frames[0ULL]) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        adjacent_environment.FrameAllocator().Allocate(adjacent_frames[1ULL]) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        adjacent_environment.PageTableManager().MapPage(
            OS_TEST_PAGE_TABLE_RECLAIM_ADJACENT_ADDRESS, adjacent_frames[0ULL].physical_address,
            OS_TEST_PAGE_TABLE_RECLAIM_KERNEL_PERMISSIONS) ==
            os::kernel::PageTableStatus::Succeeded &&
        adjacent_environment.PageTableManager().MapPage(
            OS_TEST_PAGE_TABLE_RECLAIM_ADJACENT_ADDRESS +
                os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES,
            adjacent_frames[1ULL].physical_address,
            OS_TEST_PAGE_TABLE_RECLAIM_KERNEL_PERMISSIONS) ==
            os::kernel::PageTableStatus::Succeeded;
    os::kernel::PageTableUnmapResult first_adjacent_result{};
    os::kernel::PageTableUnmapResult last_adjacent_result{};
    adjacent_valid = adjacent_valid &&
                     adjacent_environment.PageTableManager().UnmapPage(
                         OS_TEST_PAGE_TABLE_RECLAIM_ADJACENT_ADDRESS, first_adjacent_result) ==
                         os::kernel::PageTableStatus::Succeeded &&
                     first_adjacent_result.reclaimed_table_frame_count ==
                         OS_TEST_PAGE_TABLE_RECLAIM_EMPTY_VALUE &&
                     adjacent_environment.PageTableManager().UnmapPage(
                         OS_TEST_PAGE_TABLE_RECLAIM_ADJACENT_ADDRESS +
                             os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES,
                         last_adjacent_result) == os::kernel::PageTableStatus::Succeeded &&
                     IsFullReclaim(last_adjacent_result) &&
                     adjacent_environment.FrameAllocator().Release(adjacent_frames[0ULL]) ==
                         os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
                     adjacent_environment.FrameAllocator().Release(adjacent_frames[1ULL]) ==
                         os::kernel::PhysicalFrameAllocatorStatus::Succeeded;
    test_context.Expect(adjacent_valid, OS_TEST_PAGE_TABLE_RECLAIM_ADJACENT_PAGES);

    static os::test::PageTableTestEnvironment shared_environment{
        os::kernel::PageTableRootKind::KernelShared,
    };
    const bool shared_initialized = shared_environment.Initialize();
    const os::kernel::PhysicalFrameAllocatorStatistics shared_baseline =
        shared_environment.FrameAllocator().Statistics();
    os::kernel::PhysicalFrame shared_data_frame{};
    os::kernel::PageTableUnmapResult shared_result{};
    const bool shared_valid =
        shared_initialized &&
        shared_environment.FrameAllocator().Allocate(shared_data_frame) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        shared_environment.PageTableManager().MapPage(
            OS_TEST_PAGE_TABLE_RECLAIM_KERNEL_SHARED_ADDRESS, shared_data_frame.physical_address,
            OS_TEST_PAGE_TABLE_RECLAIM_KERNEL_PERMISSIONS) ==
            os::kernel::PageTableStatus::Succeeded &&
        shared_environment.PageTableManager().UnmapPage(
            OS_TEST_PAGE_TABLE_RECLAIM_KERNEL_SHARED_ADDRESS, shared_result) ==
            os::kernel::PageTableStatus::Succeeded &&
        shared_result.reclaimed_level1_table_count == OS_TEST_PAGE_TABLE_RECLAIM_SINGLE_ENTRY &&
        shared_result.reclaimed_level2_table_count == OS_TEST_PAGE_TABLE_RECLAIM_SINGLE_ENTRY &&
        shared_result.reclaimed_level3_table_count == OS_TEST_PAGE_TABLE_RECLAIM_EMPTY_VALUE &&
        shared_result.reclaimed_table_frame_count == OS_TEST_PAGE_TABLE_RECLAIM_TWO_ENTRIES &&
        shared_environment.FrameAllocator().Release(shared_data_frame) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded;
    const os::kernel::PhysicalFrameAllocatorStatistics shared_after =
        shared_environment.FrameAllocator().Statistics();
    test_context.Expect(shared_valid && shared_after.allocated_frame_count ==
                                            shared_baseline.allocated_frame_count +
                                                OS_TEST_PAGE_TABLE_RECLAIM_SINGLE_ENTRY,
                        OS_TEST_PAGE_TABLE_RECLAIM_SHARED_ROOT);

    static os::test::PageTableTestEnvironment process_environment{
        os::kernel::PageTableRootKind::KernelShared,
    };
    os::kernel::PhysicalFrame kernel_low_frame{};
    const bool kernel_template_ready =
        process_environment.Initialize() &&
        process_environment.FrameAllocator().Allocate(kernel_low_frame) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        process_environment.PageTableManager().MapPage(
            OS_TEST_PAGE_TABLE_RECLAIM_KERNEL_LOW_ADDRESS, kernel_low_frame.physical_address,
            OS_TEST_PAGE_TABLE_RECLAIM_KERNEL_PERMISSIONS) ==
            os::kernel::PageTableStatus::Succeeded;
    const os::kernel::PhysicalFrameAllocatorStatistics process_baseline =
        process_environment.FrameAllocator().Statistics();
    os::kernel::PageTableManager process_page_table{
        process_environment.FrameAllocator(),
        process_environment.MemoryAccess(),
        os::kernel::PageTableRootKind::Process,
    };
    os::kernel::PhysicalFrame user_program_frame{};
    os::kernel::PhysicalFrame user_stack_frame{};
    bool process_valid = kernel_template_ready &&
                         process_page_table.InitializeProcessRoot(
                             process_environment.PageTableManager().RootPhysicalAddress()) ==
                             os::kernel::PageTableStatus::Succeeded &&
                         process_environment.FrameAllocator().Allocate(user_program_frame) ==
                             os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
                         process_environment.FrameAllocator().Allocate(user_stack_frame) ==
                             os::kernel::PhysicalFrameAllocatorStatus::Succeeded;
    os::kernel::PageMapping borrowed_mapping{};
    process_valid =
        process_valid &&
        process_page_table.QueryPage(OS_TEST_PAGE_TABLE_RECLAIM_KERNEL_LOW_ADDRESS,
                                     borrowed_mapping) == os::kernel::PageTableStatus::Succeeded &&
        borrowed_mapping.physical_address == kernel_low_frame.physical_address &&
        process_page_table.MapPage(OS_TEST_PAGE_TABLE_RECLAIM_KERNEL_LOW_ADDRESS,
                                   user_program_frame.physical_address,
                                   OS_TEST_PAGE_TABLE_RECLAIM_USER_PERMISSIONS) ==
            os::kernel::PageTableStatus::SharedBranchMutationDenied &&
        process_page_table.UnmapPage(OS_TEST_PAGE_TABLE_RECLAIM_KERNEL_LOW_ADDRESS) ==
            os::kernel::PageTableStatus::SharedBranchMutationDenied &&
        process_page_table.MapPage(OS_TEST_PAGE_TABLE_RECLAIM_USER_PROGRAM_ADDRESS,
                                   user_program_frame.physical_address,
                                   OS_TEST_PAGE_TABLE_RECLAIM_USER_PERMISSIONS) ==
            os::kernel::PageTableStatus::Succeeded &&
        process_page_table.MapPage(
            OS_TEST_PAGE_TABLE_RECLAIM_USER_STACK_ADDRESS, user_stack_frame.physical_address,
            OS_TEST_PAGE_TABLE_RECLAIM_USER_PERMISSIONS) == os::kernel::PageTableStatus::Succeeded;
    os::kernel::PageTableUnmapResult program_result{};
    os::kernel::PageTableUnmapResult stack_result{};
    process_valid =
        process_valid &&
        process_page_table.UnmapPage(OS_TEST_PAGE_TABLE_RECLAIM_USER_PROGRAM_ADDRESS,
                                     program_result) == os::kernel::PageTableStatus::Succeeded &&
        program_result.reclaimed_level1_table_count == OS_TEST_PAGE_TABLE_RECLAIM_SINGLE_ENTRY &&
        program_result.reclaimed_level2_table_count == OS_TEST_PAGE_TABLE_RECLAIM_SINGLE_ENTRY &&
        program_result.reclaimed_level3_table_count == OS_TEST_PAGE_TABLE_RECLAIM_EMPTY_VALUE &&
        process_page_table.UnmapPage(OS_TEST_PAGE_TABLE_RECLAIM_USER_STACK_ADDRESS, stack_result) ==
            os::kernel::PageTableStatus::Succeeded &&
        IsFullReclaim(stack_result) &&
        process_environment.FrameAllocator().Release(user_program_frame) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        process_environment.FrameAllocator().Release(user_stack_frame) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        process_page_table.ReleaseProcessRoot() == os::kernel::PageTableStatus::Succeeded;
    const os::kernel::PhysicalFrameAllocatorStatistics process_after =
        process_environment.FrameAllocator().Statistics();
    process_valid = process_valid &&
                    process_after.allocated_frame_count == process_baseline.allocated_frame_count &&
                    process_environment.PageTableManager().QueryPage(
                        OS_TEST_PAGE_TABLE_RECLAIM_KERNEL_LOW_ADDRESS, borrowed_mapping) ==
                        os::kernel::PageTableStatus::Succeeded &&
                    process_environment.PageTableManager().UnmapPage(
                        OS_TEST_PAGE_TABLE_RECLAIM_KERNEL_LOW_ADDRESS) ==
                        os::kernel::PageTableStatus::Succeeded &&
                    process_environment.FrameAllocator().Release(kernel_low_frame) ==
                        os::kernel::PhysicalFrameAllocatorStatus::Succeeded;
    test_context.Expect(process_valid, OS_TEST_PAGE_TABLE_RECLAIM_PROCESS_BOUNDARY);

    os::kernel::PageTableManager corrupted_process_page_table{
        process_environment.FrameAllocator(),
        process_environment.MemoryAccess(),
        os::kernel::PageTableRootKind::Process,
    };
    os::kernel::PhysicalFrame corrupted_process_data_frame{};
    bool process_cycle_valid =
        corrupted_process_page_table.InitializeProcessRoot(
            process_environment.PageTableManager().RootPhysicalAddress()) ==
            os::kernel::PageTableStatus::Succeeded &&
        process_environment.FrameAllocator().Allocate(corrupted_process_data_frame) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        corrupted_process_page_table.MapPage(OS_TEST_PAGE_TABLE_RECLAIM_USER_STACK_ADDRESS,
                                             corrupted_process_data_frame.physical_address,
                                             OS_TEST_PAGE_TABLE_RECLAIM_USER_PERMISSIONS) ==
            os::kernel::PageTableStatus::Succeeded;
    const os::kernel::PageTableIndices corrupted_process_indices =
        os::kernel::CalculatePageTableIndices(OS_TEST_PAGE_TABLE_RECLAIM_USER_STACK_ADDRESS);
    const uint64_t corrupted_process_root_address =
        corrupted_process_page_table.RootPhysicalAddress();
    uint64_t *const corrupted_process_root =
        process_environment.TableAt(corrupted_process_root_address);
    const uint64_t corrupted_process_level3_address =
        ChildTableAddress(corrupted_process_root[corrupted_process_indices.level4]);
    uint64_t *const corrupted_process_level3 =
        process_environment.TableAt(corrupted_process_level3_address);
    const uint64_t corrupted_process_level2_address =
        ChildTableAddress(corrupted_process_level3[corrupted_process_indices.level3]);
    uint64_t *const corrupted_process_level2 =
        process_environment.TableAt(corrupted_process_level2_address);
    const uint64_t preserved_corrupted_process_level2_entry =
        corrupted_process_level2[corrupted_process_indices.level2];
    corrupted_process_level2[corrupted_process_indices.level2] =
        corrupted_process_level3_address | OS_TEST_PAGE_TABLE_RECLAIM_ENTRY_PRESENT_WRITABLE;
    process_cycle_valid =
        process_cycle_valid &&
        corrupted_process_page_table.ReleaseProcessRoot() ==
            os::kernel::PageTableStatus::InvalidTableFrame &&
        corrupted_process_page_table.RootPhysicalAddress() == corrupted_process_root_address &&
        process_environment.FrameAllocator().OwnsAllocation(os::kernel::PhysicalFrame{
            .physical_address = corrupted_process_root_address,
        }) &&
        process_environment.FrameAllocator().OwnsAllocation(corrupted_process_data_frame);
    corrupted_process_level2[corrupted_process_indices.level2] =
        preserved_corrupted_process_level2_entry;
    process_cycle_valid =
        process_cycle_valid &&
        corrupted_process_page_table.ReleaseProcessRoot() ==
            os::kernel::PageTableStatus::Succeeded &&
        corrupted_process_page_table.RootPhysicalAddress() ==
            OS_TEST_PAGE_TABLE_RECLAIM_EMPTY_VALUE &&
        !process_environment.FrameAllocator().OwnsAllocation(corrupted_process_data_frame);
    test_context.Expect(process_cycle_valid, OS_TEST_PAGE_TABLE_RECLAIM_PROCESS_RELEASE_CYCLE);

    static os::test::PageTableTestEnvironment rollback_environment{
        os::kernel::PageTableRootKind::Exclusive,
    };
    const uint64_t constrained_limit =
        OS_TEST_PAGE_TABLE_RECLAIM_FIVE_PAGES * os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
    os::kernel::PhysicalFrame rollback_data_frame{};
    os::kernel::PhysicalFrame blocker_frames[OS_TEST_PAGE_TABLE_RECLAIM_TWO_ENTRIES]{};
    bool rollback_valid =
        rollback_environment.Initialize() &&
        rollback_environment.FrameAllocator().AllocateInRange(
            constrained_limit, os::test::OS_TEST_PAGE_TABLE_ENVIRONMENT_PHYSICAL_MEMORY_SIZE_BYTES,
            rollback_data_frame) == os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        rollback_environment.FrameAllocator().AllocateInRange(
            OS_TEST_PAGE_TABLE_RECLAIM_EMPTY_VALUE, constrained_limit, blocker_frames[0ULL]) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        rollback_environment.FrameAllocator().AllocateInRange(
            OS_TEST_PAGE_TABLE_RECLAIM_EMPTY_VALUE, constrained_limit, blocker_frames[1ULL]) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded;
    os::kernel::PageTableMemoryAccess constrained_access = rollback_environment.MemoryAccess();
    constrained_access.allocation_maximum_physical_address_exclusive = constrained_limit;
    rollback_valid = rollback_valid &&
                     rollback_environment.PageTableManager().SetMemoryAccess(constrained_access) ==
                         os::kernel::PageTableStatus::Succeeded;
    const os::kernel::PhysicalFrameAllocatorStatistics rollback_before =
        rollback_environment.FrameAllocator().Statistics();
    const uint64_t rollback_level4_index = Level4Index(OS_TEST_PAGE_TABLE_RECLAIM_ROLLBACK_ADDRESS);
    rollback_valid = rollback_valid && rollback_environment.PageTableManager().MapPage(
                                           OS_TEST_PAGE_TABLE_RECLAIM_ROLLBACK_ADDRESS,
                                           rollback_data_frame.physical_address,
                                           OS_TEST_PAGE_TABLE_RECLAIM_KERNEL_PERMISSIONS) ==
                                           os::kernel::PageTableStatus::FrameAllocationFailed;
    const os::kernel::PhysicalFrameAllocatorStatistics rollback_after =
        rollback_environment.FrameAllocator().Statistics();
    rollback_valid =
        rollback_valid &&
        rollback_before.allocated_frame_count == rollback_after.allocated_frame_count &&
        rollback_before.free_frame_count == rollback_after.free_frame_count &&
        rollback_environment.TableAt(
            rollback_environment.PageTableManager().RootPhysicalAddress())[rollback_level4_index] ==
            OS_TEST_PAGE_TABLE_RECLAIM_EMPTY_VALUE &&
        rollback_environment.PageTableManager().SetMemoryAccess(
            rollback_environment.MemoryAccess()) == os::kernel::PageTableStatus::Succeeded &&
        rollback_environment.FrameAllocator().Release(blocker_frames[0ULL]) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        rollback_environment.FrameAllocator().Release(blocker_frames[1ULL]) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        rollback_environment.FrameAllocator().Release(rollback_data_frame) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded;
    test_context.Expect(rollback_valid, OS_TEST_PAGE_TABLE_RECLAIM_ROLLBACK);

    static os::test::PageTableTestEnvironment ownership_environment{
        os::kernel::PageTableRootKind::Exclusive,
    };
    os::kernel::PhysicalFrame ownership_data_frame{};
    bool ownership_valid =
        ownership_environment.Initialize() &&
        ownership_environment.FrameAllocator().Allocate(ownership_data_frame) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        ownership_environment.PageTableManager().MapPage(
            OS_TEST_PAGE_TABLE_RECLAIM_OWNERSHIP_ADDRESS, ownership_data_frame.physical_address,
            OS_TEST_PAGE_TABLE_RECLAIM_KERNEL_PERMISSIONS) ==
            os::kernel::PageTableStatus::Succeeded;
    const os::kernel::PageTableIndices ownership_indices =
        os::kernel::CalculatePageTableIndices(OS_TEST_PAGE_TABLE_RECLAIM_OWNERSHIP_ADDRESS);
    uint64_t *const ownership_root = ownership_environment.TableAt(
        ownership_environment.PageTableManager().RootPhysicalAddress());
    const uint64_t ownership_level3_address =
        ChildTableAddress(ownership_root[ownership_indices.level4]);
    uint64_t *const ownership_level3 = ownership_environment.TableAt(ownership_level3_address);
    const uint64_t ownership_level2_address =
        ChildTableAddress(ownership_level3[ownership_indices.level3]);
    uint64_t *const ownership_level2 = ownership_environment.TableAt(ownership_level2_address);
    const uint64_t ownership_level1_address =
        ChildTableAddress(ownership_level2[ownership_indices.level2]);
    const uint64_t preserved_level2_entry = ownership_level2[ownership_indices.level2];
    ownership_valid = ownership_valid &&
                      ownership_environment.FrameAllocator().Release(os::kernel::PhysicalFrame{
                          .physical_address = ownership_level1_address}) ==
                          os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
                      ownership_environment.PageTableManager().UnmapPage(
                          OS_TEST_PAGE_TABLE_RECLAIM_OWNERSHIP_ADDRESS) ==
                          os::kernel::PageTableStatus::TableFrameNotOwned &&
                      ownership_level2[ownership_indices.level2] == preserved_level2_entry;
    os::kernel::PhysicalFrame restored_level1_frame{};
    ownership_valid =
        ownership_valid &&
        ownership_environment.FrameAllocator().AllocateInRange(
            ownership_level1_address,
            ownership_level1_address + os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES,
            restored_level1_frame) == os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        restored_level1_frame.physical_address == ownership_level1_address;
    os::kernel::PageTableUnmapResult restored_result{};
    ownership_valid = ownership_valid &&
                      ownership_environment.PageTableManager().UnmapPage(
                          OS_TEST_PAGE_TABLE_RECLAIM_OWNERSHIP_ADDRESS, restored_result) ==
                          os::kernel::PageTableStatus::Succeeded &&
                      IsFullReclaim(restored_result) &&
                      ownership_environment.FrameAllocator().Release(ownership_data_frame) ==
                          os::kernel::PhysicalFrameAllocatorStatus::Succeeded;
    test_context.Expect(ownership_valid, OS_TEST_PAGE_TABLE_RECLAIM_OWNERSHIP);

    const uint64_t corrupt_level4_index = Level4Index(OS_TEST_PAGE_TABLE_RECLAIM_OWNERSHIP_ADDRESS);
    os::kernel::PageMapping corrupt_mapping{};
    ownership_root[corrupt_level4_index] =
        os::test::OS_TEST_PAGE_TABLE_ENVIRONMENT_PHYSICAL_MEMORY_SIZE_BYTES |
        OS_TEST_PAGE_TABLE_RECLAIM_ENTRY_PRESENT_WRITABLE;
    const bool invalid_address_rejected =
        ownership_environment.PageTableManager().QueryPage(
            OS_TEST_PAGE_TABLE_RECLAIM_OWNERSHIP_ADDRESS, corrupt_mapping) ==
        os::kernel::PageTableStatus::InvalidTableFrame;
    ownership_root[corrupt_level4_index] =
        ownership_environment.PageTableManager().RootPhysicalAddress() |
        OS_TEST_PAGE_TABLE_RECLAIM_ENTRY_PRESENT_WRITABLE;
    const bool cycle_rejected =
        ownership_environment.PageTableManager().QueryPage(
            OS_TEST_PAGE_TABLE_RECLAIM_OWNERSHIP_ADDRESS, corrupt_mapping) ==
        os::kernel::PageTableStatus::InvalidTableFrame;
    ownership_root[corrupt_level4_index] = OS_TEST_PAGE_TABLE_RECLAIM_EMPTY_VALUE;
    test_context.Expect(invalid_address_rejected && cycle_rejected,
                        OS_TEST_PAGE_TABLE_RECLAIM_CORRUPTION);

    return test_context.ExitCode();
}

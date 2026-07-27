#include "page_table_test_environment.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view
    OS_TEST_COPY_ON_WRITE_PAGE_TABLE_SUITE_NAME =
        "kernel/copy_on_write_page_table/integration";
constexpr std::string_view
    OS_TEST_COPY_ON_WRITE_PAGE_TABLE_TRANSITION =
        "进程页必须从可写私有态降权为 COW，并能替换为新的独占可写页";
constexpr std::string_view
    OS_TEST_COPY_ON_WRITE_PAGE_TABLE_INVALID_PERMISSIONS_MESSAGE =
        "页表必须拒绝同时设置硬件可写位和软件 COW 位且保持旧映射不变";
constexpr std::string_view
    OS_TEST_COPY_ON_WRITE_PAGE_TABLE_RECLAIM =
        "替换和撤销映射后数据帧、进程页表与模板页表必须完整回收";

constexpr uint64_t OS_TEST_COPY_ON_WRITE_PAGE_TABLE_KERNEL_ADDRESS =
    0x0000000000200000ULL;
constexpr uint64_t OS_TEST_COPY_ON_WRITE_PAGE_TABLE_USER_ADDRESS =
    0x0000000040003000ULL;

constexpr os::kernel::PagePermissions
    OS_TEST_COPY_ON_WRITE_PAGE_TABLE_KERNEL_PERMISSIONS{
        .writable = true,
        .executable = false,
        .user_accessible = false,
        .cache_disabled = false,
        .copy_on_write = false,
    };
constexpr os::kernel::PagePermissions
    OS_TEST_COPY_ON_WRITE_PAGE_TABLE_WRITABLE_PERMISSIONS{
        .writable = true,
        .executable = false,
        .user_accessible = true,
        .cache_disabled = false,
        .copy_on_write = false,
    };
constexpr os::kernel::PagePermissions
    OS_TEST_COPY_ON_WRITE_PAGE_TABLE_SHARED_PERMISSIONS{
        .writable = false,
        .executable = false,
        .user_accessible = true,
        .cache_disabled = false,
        .copy_on_write = true,
    };
constexpr os::kernel::PagePermissions
    OS_TEST_COPY_ON_WRITE_PAGE_TABLE_INVALID_PERMISSIONS{
        .writable = true,
        .executable = false,
        .user_accessible = true,
        .cache_disabled = false,
        .copy_on_write = true,
    };

}

int main() {
    os::test::TestContext test_context{
        OS_TEST_COPY_ON_WRITE_PAGE_TABLE_SUITE_NAME};
    static os::test::PageTableTestEnvironment environment{
        os::kernel::PageTableRootKind::KernelShared,
    };
    os::kernel::PhysicalFrame kernel_frame{};
    const bool template_ready =
        environment.Initialize() &&
        environment.FrameAllocator().Allocate(kernel_frame) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        environment.PageTableManager().MapPage(
            OS_TEST_COPY_ON_WRITE_PAGE_TABLE_KERNEL_ADDRESS,
            kernel_frame.physical_address,
            OS_TEST_COPY_ON_WRITE_PAGE_TABLE_KERNEL_PERMISSIONS) ==
            os::kernel::PageTableStatus::Succeeded;
    const os::kernel::PhysicalFrameAllocatorStatistics baseline =
        environment.FrameAllocator().Statistics();

    os::kernel::PageTableManager process_page_table{
        environment.FrameAllocator(), environment.MemoryAccess(),
        os::kernel::PageTableRootKind::Process,
    };
    os::kernel::PhysicalFrame original_frame{};
    os::kernel::PhysicalFrame replacement_frame{};
    bool transition_valid =
        template_ready &&
        process_page_table.InitializeProcessRoot(
            environment.PageTableManager()
                .RootPhysicalAddress()) ==
            os::kernel::PageTableStatus::Succeeded &&
        environment.FrameAllocator().Allocate(original_frame) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        environment.FrameAllocator().Allocate(replacement_frame) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        process_page_table.MapPage(
            OS_TEST_COPY_ON_WRITE_PAGE_TABLE_USER_ADDRESS,
            original_frame.physical_address,
            OS_TEST_COPY_ON_WRITE_PAGE_TABLE_WRITABLE_PERMISSIONS) ==
            os::kernel::PageTableStatus::Succeeded;
    os::kernel::PageMapping mapping{};
    transition_valid =
        transition_valid &&
        process_page_table.ReplacePage(
            OS_TEST_COPY_ON_WRITE_PAGE_TABLE_USER_ADDRESS,
            original_frame.physical_address,
            OS_TEST_COPY_ON_WRITE_PAGE_TABLE_SHARED_PERMISSIONS) ==
            os::kernel::PageTableStatus::Succeeded &&
        process_page_table.QueryPage(
            OS_TEST_COPY_ON_WRITE_PAGE_TABLE_USER_ADDRESS,
            mapping) ==
            os::kernel::PageTableStatus::Succeeded &&
        mapping.physical_address ==
            original_frame.physical_address &&
        !mapping.permissions.writable &&
        mapping.permissions.copy_on_write;
    test_context.Expect(
        transition_valid,
        OS_TEST_COPY_ON_WRITE_PAGE_TABLE_TRANSITION);

    const bool invalid_permissions_rejected =
        transition_valid &&
        process_page_table.ReplacePage(
            OS_TEST_COPY_ON_WRITE_PAGE_TABLE_USER_ADDRESS,
            original_frame.physical_address,
            OS_TEST_COPY_ON_WRITE_PAGE_TABLE_INVALID_PERMISSIONS) ==
            os::kernel::PageTableStatus::InvalidMemoryAccess &&
        process_page_table.QueryPage(
            OS_TEST_COPY_ON_WRITE_PAGE_TABLE_USER_ADDRESS,
            mapping) ==
            os::kernel::PageTableStatus::Succeeded &&
        !mapping.permissions.writable &&
        mapping.permissions.copy_on_write;
    test_context.Expect(
        invalid_permissions_rejected,
        OS_TEST_COPY_ON_WRITE_PAGE_TABLE_INVALID_PERMISSIONS_MESSAGE);

    transition_valid =
        transition_valid &&
        process_page_table.ReplacePage(
            OS_TEST_COPY_ON_WRITE_PAGE_TABLE_USER_ADDRESS,
            replacement_frame.physical_address,
            OS_TEST_COPY_ON_WRITE_PAGE_TABLE_WRITABLE_PERMISSIONS) ==
            os::kernel::PageTableStatus::Succeeded &&
        process_page_table.QueryPage(
            OS_TEST_COPY_ON_WRITE_PAGE_TABLE_USER_ADDRESS,
            mapping) ==
            os::kernel::PageTableStatus::Succeeded &&
        mapping.physical_address ==
            replacement_frame.physical_address &&
        mapping.permissions.writable &&
        !mapping.permissions.copy_on_write;

    const bool resources_reclaimed =
        transition_valid &&
        process_page_table.UnmapPage(
            OS_TEST_COPY_ON_WRITE_PAGE_TABLE_USER_ADDRESS) ==
            os::kernel::PageTableStatus::Succeeded &&
        environment.FrameAllocator().Release(original_frame) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        environment.FrameAllocator().Release(replacement_frame) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        process_page_table.ReleaseProcessRoot() ==
            os::kernel::PageTableStatus::Succeeded;
    const os::kernel::PhysicalFrameAllocatorStatistics after =
        environment.FrameAllocator().Statistics();
    test_context.Expect(
        resources_reclaimed &&
            after.allocated_frame_count ==
                baseline.allocated_frame_count &&
            after.free_frame_count == baseline.free_frame_count,
        OS_TEST_COPY_ON_WRITE_PAGE_TABLE_RECLAIM);
    return test_context.ExitCode();
}

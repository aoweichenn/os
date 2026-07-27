#include "page_table_test_environment.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_SHARED_USER_PAGE_SUITE_NAME =
    "kernel/shared_user_page/lifecycle";
constexpr std::string_view OS_TEST_SHARED_USER_PAGE_MAPPING =
    "同一 clean 页帧必须能以只读权限映射到两个独立进程页表";
constexpr std::string_view OS_TEST_SHARED_USER_PAGE_RELEASE =
    "两个映射分别撤销后数据页只能释放一次且资源必须回到基线";

constexpr uint64_t OS_TEST_SHARED_USER_PAGE_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_SHARED_USER_PAGE_KERNEL_TEMPLATE_ADDRESS =
    0x0000000000100000ULL;
constexpr uint64_t OS_TEST_SHARED_USER_PAGE_FIRST_ADDRESS =
    0x0000000040100000ULL;
constexpr uint64_t OS_TEST_SHARED_USER_PAGE_SECOND_ADDRESS =
    0x0000000040200000ULL;

constexpr os::kernel::PagePermissions
    OS_TEST_SHARED_USER_PAGE_PERMISSIONS{
        .writable = false,
        .executable = false,
        .user_accessible = true,
        .cache_disabled = false,
    };
constexpr os::kernel::PagePermissions
    OS_TEST_SHARED_USER_PAGE_KERNEL_PERMISSIONS{
        .writable = true,
        .executable = false,
        .user_accessible = false,
        .cache_disabled = false,
    };

}

int main() {
    os::test::TestContext test_context{
        OS_TEST_SHARED_USER_PAGE_SUITE_NAME};
    static os::test::PageTableTestEnvironment environment{
        os::kernel::PageTableRootKind::KernelShared,
    };
    const bool initialized = environment.Initialize();
    os::kernel::PhysicalFrame kernel_template_frame{};
    const bool process_template_ready =
        initialized &&
        environment.FrameAllocator().Allocate(
            kernel_template_frame) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        environment.PageTableManager().MapPage(
            OS_TEST_SHARED_USER_PAGE_KERNEL_TEMPLATE_ADDRESS,
            kernel_template_frame.physical_address,
            OS_TEST_SHARED_USER_PAGE_KERNEL_PERMISSIONS) ==
            os::kernel::PageTableStatus::Succeeded;
    const os::kernel::PhysicalFrameAllocatorStatistics baseline =
        environment.FrameAllocator().Statistics();
    os::kernel::PageTableManager first_process{
        environment.FrameAllocator(), environment.MemoryAccess(),
        os::kernel::PageTableRootKind::Process,
    };
    os::kernel::PageTableManager second_process{
        environment.FrameAllocator(), environment.MemoryAccess(),
        os::kernel::PageTableRootKind::Process,
    };
    os::kernel::PhysicalFrame shared_frame{};
    const bool mapped =
        process_template_ready &&
        first_process.InitializeProcessRoot(
            environment.PageTableManager().RootPhysicalAddress()) ==
            os::kernel::PageTableStatus::Succeeded &&
        second_process.InitializeProcessRoot(
            environment.PageTableManager().RootPhysicalAddress()) ==
            os::kernel::PageTableStatus::Succeeded &&
        environment.FrameAllocator().Allocate(shared_frame) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        first_process.MapPage(
            OS_TEST_SHARED_USER_PAGE_FIRST_ADDRESS,
            shared_frame.physical_address,
            OS_TEST_SHARED_USER_PAGE_PERMISSIONS) ==
            os::kernel::PageTableStatus::Succeeded &&
        second_process.MapPage(
            OS_TEST_SHARED_USER_PAGE_SECOND_ADDRESS,
            shared_frame.physical_address,
            OS_TEST_SHARED_USER_PAGE_PERMISSIONS) ==
            os::kernel::PageTableStatus::Succeeded;
    os::kernel::PageMapping first_mapping{};
    os::kernel::PageMapping second_mapping{};
    const bool mapping_valid =
        mapped &&
        first_process.QueryPage(
            OS_TEST_SHARED_USER_PAGE_FIRST_ADDRESS,
            first_mapping) == os::kernel::PageTableStatus::Succeeded &&
        second_process.QueryPage(
            OS_TEST_SHARED_USER_PAGE_SECOND_ADDRESS,
            second_mapping) == os::kernel::PageTableStatus::Succeeded &&
        first_mapping.physical_address ==
            shared_frame.physical_address &&
        second_mapping.physical_address ==
            shared_frame.physical_address &&
        !first_mapping.permissions.writable &&
        !second_mapping.permissions.writable;
    test_context.Expect(mapping_valid,
                        OS_TEST_SHARED_USER_PAGE_MAPPING);

    const bool released =
        mapping_valid &&
        first_process.UnmapPage(
            OS_TEST_SHARED_USER_PAGE_FIRST_ADDRESS) ==
            os::kernel::PageTableStatus::Succeeded &&
        environment.FrameAllocator().OwnsAllocation(shared_frame) &&
        second_process.UnmapPage(
            OS_TEST_SHARED_USER_PAGE_SECOND_ADDRESS) ==
            os::kernel::PageTableStatus::Succeeded &&
        environment.FrameAllocator().OwnsAllocation(shared_frame) &&
        environment.FrameAllocator().Release(shared_frame) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        first_process.ReleaseProcessRoot() ==
            os::kernel::PageTableStatus::Succeeded &&
        second_process.ReleaseProcessRoot() ==
            os::kernel::PageTableStatus::Succeeded &&
        environment.FrameAllocator().Statistics().allocated_frame_count ==
            baseline.allocated_frame_count &&
        environment.FrameAllocator().Statistics().free_frame_count ==
            baseline.free_frame_count &&
        environment.PageTableManager().RootPhysicalAddress() !=
            OS_TEST_SHARED_USER_PAGE_EMPTY_VALUE;
    test_context.Expect(released,
                        OS_TEST_SHARED_USER_PAGE_RELEASE);
    return test_context.ExitCode();
}

#include "os/kernel/memory/virtual_memory_area.hpp"
#include "page_table_test_environment.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_USER_VM_LIFECYCLE_SUITE_NAME =
    "boot/user_virtual_memory_lifecycle/integration";
constexpr std::string_view OS_TEST_USER_VM_LIFECYCLE_RESERVATION_DESCRIPTION =
    "VMA 预留不得提前消耗物理页帧";
constexpr std::string_view OS_TEST_USER_VM_LIFECYCLE_DEMAND_DESCRIPTION =
    "首次触页必须建立用户映射且只为驻留页分配资源";
constexpr std::string_view OS_TEST_USER_VM_LIFECYCLE_RECLAIM_DESCRIPTION =
    "中段撤销、页表分支回收与地址空间销毁必须恢复全部资源基线";

constexpr uint64_t OS_TEST_USER_VM_LIFECYCLE_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_USER_VM_LIFECYCLE_EXPECTED_SPLIT_AREA_COUNT = 2ULL;
constexpr uint64_t OS_TEST_USER_VM_LIFECYCLE_EXPECTED_TABLE_RECLAIMS = 2ULL;
constexpr uint64_t OS_TEST_USER_VM_LIFECYCLE_DESCRIPTOR_CAPACITY = 64ULL;
constexpr uint64_t OS_TEST_USER_VM_LIFECYCLE_AREA_LIMIT = 32ULL;
constexpr uint64_t OS_TEST_USER_VM_LIFECYCLE_ITERATION_COUNT = 128ULL;
constexpr uint64_t OS_TEST_USER_VM_LIFECYCLE_AREA_PAGE_COUNT = 4ULL;
constexpr uint64_t OS_TEST_USER_VM_LIFECYCLE_KERNEL_ADDRESS = 0x0000000000200000ULL;
constexpr uint64_t OS_TEST_USER_VM_LIFECYCLE_AREA_ADDRESS = 0x0000000060000000ULL;
constexpr uint64_t OS_TEST_USER_VM_LIFECYCLE_TOUCH_PAGE_INDEX = 1ULL;

constexpr os::kernel::PagePermissions OS_TEST_USER_VM_LIFECYCLE_KERNEL_PERMISSIONS{
    .writable = true,
    .executable = false,
    .user_accessible = false,
    .cache_disabled = false,
};
constexpr os::kernel::PagePermissions OS_TEST_USER_VM_LIFECYCLE_USER_PERMISSIONS{
    .writable = true,
    .executable = false,
    .user_accessible = true,
    .cache_disabled = false,
};
constexpr os::kernel::VirtualMemoryAreaPermissions OS_TEST_USER_VM_LIFECYCLE_AREA_PERMISSIONS{
    .readable = true,
    .writable = true,
    .executable = false,
};

}

int main() {
    os::test::TestContext test_context{OS_TEST_USER_VM_LIFECYCLE_SUITE_NAME};
    static os::test::PageTableTestEnvironment environment{
        os::kernel::PageTableRootKind::KernelShared,
    };
    static os::kernel::VirtualMemoryAreaDescriptor
        descriptors[OS_TEST_USER_VM_LIFECYCLE_DESCRIPTOR_CAPACITY]{};
    os::kernel::VirtualMemoryAreaPool descriptor_pool{};

    os::kernel::PhysicalFrame kernel_frame{};
    const bool environment_ready =
        environment.Initialize() &&
        descriptor_pool.Initialize(descriptors, OS_TEST_USER_VM_LIFECYCLE_DESCRIPTOR_CAPACITY) ==
            os::kernel::VirtualMemoryAreaStatus::Succeeded &&
        environment.FrameAllocator().Allocate(kernel_frame) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        environment.PageTableManager().MapPage(
            OS_TEST_USER_VM_LIFECYCLE_KERNEL_ADDRESS, kernel_frame.physical_address,
            OS_TEST_USER_VM_LIFECYCLE_KERNEL_PERMISSIONS) == os::kernel::PageTableStatus::Succeeded;
    const os::kernel::PhysicalFrameAllocatorStatistics lifecycle_baseline =
        environment.FrameAllocator().Statistics();

    bool reservation_valid = environment_ready;
    bool demand_mapping_valid = environment_ready;
    bool reclamation_valid = environment_ready;
    for (uint64_t iteration = OS_TEST_USER_VM_LIFECYCLE_EMPTY_VALUE;
         reclamation_valid && iteration < OS_TEST_USER_VM_LIFECYCLE_ITERATION_COUNT; ++iteration) {
        os::kernel::PageTableManager process_page_table{
            environment.FrameAllocator(),
            environment.MemoryAccess(),
            os::kernel::PageTableRootKind::Process,
        };
        os::kernel::VirtualMemoryMap virtual_memory_map{};
        reclamation_valid = process_page_table.InitializeProcessRoot(
                                environment.PageTableManager().RootPhysicalAddress()) ==
                                os::kernel::PageTableStatus::Succeeded &&
                            virtual_memory_map.Initialize(
                                descriptor_pool, os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES,
                                OS_TEST_USER_VM_LIFECYCLE_AREA_LIMIT) ==
                                os::kernel::VirtualMemoryAreaStatus::Succeeded;
        const os::kernel::PhysicalFrameAllocatorStatistics reservation_baseline =
            environment.FrameAllocator().Statistics();
        const uint64_t area_end_address = OS_TEST_USER_VM_LIFECYCLE_AREA_ADDRESS +
                                          OS_TEST_USER_VM_LIFECYCLE_AREA_PAGE_COUNT *
                                              os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
        reservation_valid = reclamation_valid &&
                            virtual_memory_map.Insert(os::kernel::VirtualMemoryArea{
                                .begin_address = OS_TEST_USER_VM_LIFECYCLE_AREA_ADDRESS,
                                .end_address = area_end_address,
                                .permissions = OS_TEST_USER_VM_LIFECYCLE_AREA_PERMISSIONS,
                                .kind = os::kernel::VirtualMemoryAreaKind::Anonymous,
                            }) == os::kernel::VirtualMemoryAreaStatus::Succeeded &&
                            environment.FrameAllocator().Statistics().allocated_frame_count ==
                                reservation_baseline.allocated_frame_count;

        const uint64_t touch_address = OS_TEST_USER_VM_LIFECYCLE_AREA_ADDRESS +
                                       OS_TEST_USER_VM_LIFECYCLE_TOUCH_PAGE_INDEX *
                                           os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
        os::kernel::PhysicalFrame demand_frame{};
        os::kernel::PageMapping demand_mapping{};
        demand_mapping_valid =
            reservation_valid &&
            environment.FrameAllocator().Allocate(demand_frame) ==
                os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
            process_page_table.MapPage(touch_address, demand_frame.physical_address,
                                       OS_TEST_USER_VM_LIFECYCLE_USER_PERMISSIONS) ==
                os::kernel::PageTableStatus::Succeeded &&
            process_page_table.QueryPage(touch_address, demand_mapping) ==
                os::kernel::PageTableStatus::Succeeded &&
            demand_mapping.physical_address == demand_frame.physical_address &&
            demand_mapping.permissions.user_accessible && demand_mapping.permissions.writable &&
            !demand_mapping.permissions.executable;

        os::kernel::PageTableUnmapResult unmap_result{};
        reclamation_valid =
            demand_mapping_valid &&
            virtual_memory_map.Remove(touch_address,
                                      touch_address + os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES,
                                      os::kernel::VirtualMemoryAreaKind::Anonymous) ==
                os::kernel::VirtualMemoryAreaStatus::Succeeded &&
            virtual_memory_map.AreaCount() == OS_TEST_USER_VM_LIFECYCLE_EXPECTED_SPLIT_AREA_COUNT &&
            process_page_table.UnmapPage(touch_address, unmap_result) ==
                os::kernel::PageTableStatus::Succeeded &&
            unmap_result.reclaimed_table_frame_count ==
                OS_TEST_USER_VM_LIFECYCLE_EXPECTED_TABLE_RECLAIMS &&
            environment.FrameAllocator().Release(demand_frame) ==
                os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
            virtual_memory_map.Destroy() == os::kernel::VirtualMemoryAreaStatus::Succeeded &&
            process_page_table.ReleaseProcessRoot() == os::kernel::PageTableStatus::Succeeded &&
            environment.FrameAllocator().Statistics().allocated_frame_count ==
                lifecycle_baseline.allocated_frame_count &&
            descriptor_pool.Statistics().active_descriptor_count ==
                OS_TEST_USER_VM_LIFECYCLE_EMPTY_VALUE &&
            descriptor_pool.Validate() == os::kernel::VirtualMemoryAreaStatus::Succeeded &&
            environment.FrameAllocator().ValidateBuddy() ==
                os::kernel::PhysicalFrameAllocatorStatus::Succeeded;
    }

    const os::kernel::VirtualMemoryAreaPoolStatistics final_pool_statistics =
        descriptor_pool.Statistics();
    reclamation_valid =
        reclamation_valid &&
        final_pool_statistics.active_descriptor_count == OS_TEST_USER_VM_LIFECYCLE_EMPTY_VALUE &&
        final_pool_statistics.free_descriptor_count == final_pool_statistics.capacity &&
        final_pool_statistics.successful_acquire_count == final_pool_statistics.release_count;

    test_context.Expect(reservation_valid, OS_TEST_USER_VM_LIFECYCLE_RESERVATION_DESCRIPTION);
    test_context.Expect(demand_mapping_valid, OS_TEST_USER_VM_LIFECYCLE_DEMAND_DESCRIPTION);
    test_context.Expect(reclamation_valid, OS_TEST_USER_VM_LIFECYCLE_RECLAIM_DESCRIPTION);
    return test_context.ExitCode();
}

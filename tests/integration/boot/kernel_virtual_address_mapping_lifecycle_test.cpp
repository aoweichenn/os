#include "os/kernel/memory/kernel_virtual_address_allocator.hpp"
#include "os/kernel/memory/page_table.hpp"
#include "os/kernel/memory/physical_frame_allocator.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_KVA_MAPPING_SUITE_NAME =
    "boot/kernel_virtual_address_mapping_lifecycle/integration";
constexpr std::string_view OS_TEST_KVA_MAPPING_INITIALIZATION =
    "页帧、页表和虚拟地址三层所有权必须独立初始化";
constexpr std::string_view OS_TEST_KVA_MAPPING_GUARDS = "区间首尾保护页必须保持未映射";
constexpr std::string_view OS_TEST_KVA_MAPPING_QUERY =
    "数据页映射必须准确返回物理页和内核读写不可执行权限";
constexpr std::string_view OS_TEST_KVA_MAPPING_LIFECYCLE =
    "撤销映射、释放物理页和释放虚拟区间后必须恢复各层基线";

constexpr uint64_t OS_TEST_KVA_MAPPING_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_KVA_MAPPING_SINGLE_UNIT = 1ULL;
constexpr uint64_t OS_TEST_KVA_MAPPING_PHYSICAL_PAGE_COUNT = 512ULL;
constexpr uint64_t OS_TEST_KVA_MAPPING_PHYSICAL_MEMORY_SIZE_BYTES =
    OS_TEST_KVA_MAPPING_PHYSICAL_PAGE_COUNT * os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_TEST_KVA_MAPPING_STATES_PER_BYTE = 4ULL;
constexpr uint64_t OS_TEST_KVA_MAPPING_STATE_STORAGE_SIZE_BYTES =
    (OS_TEST_KVA_MAPPING_PHYSICAL_PAGE_COUNT + OS_TEST_KVA_MAPPING_STATES_PER_BYTE -
     OS_TEST_KVA_MAPPING_SINGLE_UNIT) /
    OS_TEST_KVA_MAPPING_STATES_PER_BYTE;
constexpr uint64_t OS_TEST_KVA_MAPPING_MEMORY_MAP_ENTRY_COUNT = 1ULL;
constexpr uint32_t OS_TEST_KVA_MAPPING_MEMORY_MAP_ATTRIBUTES = 0U;
constexpr uint64_t OS_TEST_KVA_MAPPING_RESERVED_PHYSICAL_PAGE_COUNT = 1ULL;
constexpr uint64_t OS_TEST_KVA_MAPPING_WINDOW_BASE = 0xFFFFC90000000000ULL;
constexpr uint64_t OS_TEST_KVA_MAPPING_WINDOW_PAGE_COUNT = 64ULL;
constexpr uint64_t OS_TEST_KVA_MAPPING_DESCRIPTOR_CAPACITY = 16ULL;
constexpr uint64_t OS_TEST_KVA_MAPPING_PERMANENT_GUARD_PAGE_COUNT = 1ULL;
constexpr uint64_t OS_TEST_KVA_MAPPING_RANGE_PAGE_COUNT = 6ULL;
constexpr uint64_t OS_TEST_KVA_MAPPING_ALIGNMENT_PAGE_COUNT = 8ULL;
constexpr uint64_t OS_TEST_KVA_MAPPING_EXPECTED_RANGE_PAGE_INDEX = 8ULL;
constexpr uint64_t OS_TEST_KVA_MAPPING_FIRST_DATA_PAGE_OFFSET = 1ULL;
constexpr uint64_t OS_TEST_KVA_MAPPING_DATA_PAGE_COUNT = 4ULL;
constexpr uint64_t OS_TEST_KVA_MAPPING_LAST_GUARD_PAGE_OFFSET =
    OS_TEST_KVA_MAPPING_RANGE_PAGE_COUNT - OS_TEST_KVA_MAPPING_SINGLE_UNIT;
constexpr uint64_t OS_TEST_KVA_MAPPING_EXPECTED_PAGE_TABLE_FRAME_COUNT = 2ULL;
constexpr uint64_t OS_TEST_KVA_MAPPING_EXPECTED_RECLAIMED_TABLE_FRAME_COUNT = 2ULL;
constexpr uint64_t OS_TEST_KVA_MAPPING_FIRST_PATTERN = 0x4B56414D41504631ULL;
constexpr uint64_t OS_TEST_KVA_MAPPING_LAST_PATTERN = 0x4B56414D41504C34ULL;

alignas(os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) uint8_t
    physical_memory[OS_TEST_KVA_MAPPING_PHYSICAL_MEMORY_SIZE_BYTES]{};

[[nodiscard]] uint64_t VirtualPageAddress(const os::kernel::KernelVirtualAddressRange range,
                                          const uint64_t page_offset) noexcept {
    return range.begin_address + page_offset * os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
}

}

// 宿主测试不执行 INVLPG；测试访问环境显式关闭失效动作，但链接仍需同名边界。
namespace os::kernel {
void InvalidatePage(const uint64_t virtual_address) noexcept { static_cast<void>(virtual_address); }
}

int main() {
    os::test::TestContext test_context{OS_TEST_KVA_MAPPING_SUITE_NAME};
    uint8_t state_storage[OS_TEST_KVA_MAPPING_STATE_STORAGE_SIZE_BYTES]{};
    os::kernel::PhysicalFrameAllocator frame_allocator{
        state_storage,
        OS_TEST_KVA_MAPPING_STATE_STORAGE_SIZE_BYTES,
    };
    const os::kernel::PhysicalMemoryMapEntry memory_map[] = {
        {
            .base_address = OS_TEST_KVA_MAPPING_EMPTY_VALUE,
            .length_bytes = OS_TEST_KVA_MAPPING_PHYSICAL_MEMORY_SIZE_BYTES,
            .type = os::kernel::OS_KERNEL_MEMORY_MAP_USABLE_REGION_TYPE,
            .attributes = OS_TEST_KVA_MAPPING_MEMORY_MAP_ATTRIBUTES,
        },
    };
    const bool frame_allocator_initialized =
        frame_allocator.Initialize(memory_map, OS_TEST_KVA_MAPPING_MEMORY_MAP_ENTRY_COUNT,
                                   OS_TEST_KVA_MAPPING_PHYSICAL_MEMORY_SIZE_BYTES) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        frame_allocator.ReserveRange(OS_TEST_KVA_MAPPING_EMPTY_VALUE,
                                     OS_TEST_KVA_MAPPING_RESERVED_PHYSICAL_PAGE_COUNT *
                                         os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded;
    os::kernel::PageTableManager page_table_manager{
        frame_allocator,
        os::kernel::PageTableMemoryAccess{
            .maximum_physical_address_exclusive = OS_TEST_KVA_MAPPING_PHYSICAL_MEMORY_SIZE_BYTES,
            .physical_memory_virtual_base = reinterpret_cast<uint64_t>(physical_memory),
            .allocation_maximum_physical_address_exclusive =
                OS_TEST_KVA_MAPPING_PHYSICAL_MEMORY_SIZE_BYTES,
            .invalidate_active_mappings = false,
        },
        os::kernel::PageTableRootKind::KernelShared,
    };
    const bool page_table_initialized =
        frame_allocator_initialized &&
        page_table_manager.Initialize() == os::kernel::PageTableStatus::Succeeded;

    os::kernel::KernelVirtualAddressRangeDescriptor
        descriptors[OS_TEST_KVA_MAPPING_DESCRIPTOR_CAPACITY]{};
    os::kernel::KernelVirtualAddressAllocator virtual_address_allocator{};
    const bool virtual_address_allocator_initialized =
        virtual_address_allocator.Initialize(OS_TEST_KVA_MAPPING_WINDOW_BASE,
                                             OS_TEST_KVA_MAPPING_WINDOW_PAGE_COUNT, descriptors,
                                             OS_TEST_KVA_MAPPING_DESCRIPTOR_CAPACITY) ==
            os::kernel::KernelVirtualAddressAllocatorStatus::Succeeded &&
        virtual_address_allocator.ReserveRange(OS_TEST_KVA_MAPPING_WINDOW_BASE,
                                               OS_TEST_KVA_MAPPING_PERMANENT_GUARD_PAGE_COUNT) ==
            os::kernel::KernelVirtualAddressAllocatorStatus::Succeeded;
    test_context.Expect(page_table_initialized && virtual_address_allocator_initialized,
                        OS_TEST_KVA_MAPPING_INITIALIZATION);

    const os::kernel::PhysicalFrameAllocatorStatistics frames_before_lifecycle =
        frame_allocator.Statistics();
    os::kernel::KernelVirtualAddressRange virtual_range{};
    bool lifecycle_valid =
        virtual_address_allocator.TryAllocate(
            OS_TEST_KVA_MAPPING_RANGE_PAGE_COUNT, OS_TEST_KVA_MAPPING_ALIGNMENT_PAGE_COUNT,
            virtual_range) == os::kernel::KernelVirtualAddressAllocatorStatus::Succeeded &&
        virtual_range.begin_address ==
            OS_TEST_KVA_MAPPING_WINDOW_BASE + OS_TEST_KVA_MAPPING_EXPECTED_RANGE_PAGE_INDEX *
                                                  os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;

    os::kernel::PhysicalFrame data_frames[OS_TEST_KVA_MAPPING_DATA_PAGE_COUNT]{};
    uint64_t allocated_data_page_count = OS_TEST_KVA_MAPPING_EMPTY_VALUE;
    for (uint64_t data_page_index = OS_TEST_KVA_MAPPING_EMPTY_VALUE;
         lifecycle_valid && data_page_index < OS_TEST_KVA_MAPPING_DATA_PAGE_COUNT;
         ++data_page_index) {
        lifecycle_valid = frame_allocator.Allocate(data_frames[data_page_index]) ==
                          os::kernel::PhysicalFrameAllocatorStatus::Succeeded;
        if (lifecycle_valid) {
            ++allocated_data_page_count;
        }
    }

    const os::kernel::PagePermissions kernel_data_permissions{
        .writable = true,
        .executable = false,
        .user_accessible = false,
        .cache_disabled = false,
    };
    uint64_t mapped_data_page_count = OS_TEST_KVA_MAPPING_EMPTY_VALUE;
    for (uint64_t data_page_index = OS_TEST_KVA_MAPPING_EMPTY_VALUE;
         lifecycle_valid && data_page_index < allocated_data_page_count; ++data_page_index) {
        lifecycle_valid =
            page_table_manager.MapPage(
                VirtualPageAddress(virtual_range,
                                   OS_TEST_KVA_MAPPING_FIRST_DATA_PAGE_OFFSET + data_page_index),
                data_frames[data_page_index].physical_address,
                kernel_data_permissions) == os::kernel::PageTableStatus::Succeeded;
        if (lifecycle_valid) {
            ++mapped_data_page_count;
        }
    }

    os::kernel::PageMapping ignored_mapping{};
    const bool guards_valid =
        lifecycle_valid &&
        page_table_manager.QueryPage(virtual_range.begin_address, ignored_mapping) ==
            os::kernel::PageTableStatus::NotMapped &&
        page_table_manager.QueryPage(
            VirtualPageAddress(virtual_range, OS_TEST_KVA_MAPPING_LAST_GUARD_PAGE_OFFSET),
            ignored_mapping) == os::kernel::PageTableStatus::NotMapped;
    test_context.Expect(guards_valid, OS_TEST_KVA_MAPPING_GUARDS);

    bool mappings_valid = lifecycle_valid;
    for (uint64_t data_page_index = OS_TEST_KVA_MAPPING_EMPTY_VALUE;
         mappings_valid && data_page_index < mapped_data_page_count; ++data_page_index) {
        os::kernel::PageMapping mapping{};
        mappings_valid =
            page_table_manager.QueryPage(
                VirtualPageAddress(virtual_range,
                                   OS_TEST_KVA_MAPPING_FIRST_DATA_PAGE_OFFSET + data_page_index),
                mapping) == os::kernel::PageTableStatus::Succeeded &&
            mapping.physical_address == data_frames[data_page_index].physical_address &&
            mapping.page_size_bytes == os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES &&
            mapping.permissions.writable && !mapping.permissions.executable &&
            !mapping.permissions.user_accessible && !mapping.permissions.cache_disabled;
    }
    volatile uint64_t *const first_physical_value = reinterpret_cast<volatile uint64_t *>(
        physical_memory + data_frames[OS_TEST_KVA_MAPPING_EMPTY_VALUE].physical_address);
    volatile uint64_t *const last_physical_value = reinterpret_cast<volatile uint64_t *>(
        physical_memory +
        data_frames[OS_TEST_KVA_MAPPING_DATA_PAGE_COUNT - OS_TEST_KVA_MAPPING_SINGLE_UNIT]
            .physical_address);
    *first_physical_value = OS_TEST_KVA_MAPPING_FIRST_PATTERN;
    *last_physical_value = OS_TEST_KVA_MAPPING_LAST_PATTERN;
    mappings_valid = mappings_valid && *first_physical_value == OS_TEST_KVA_MAPPING_FIRST_PATTERN &&
                     *last_physical_value == OS_TEST_KVA_MAPPING_LAST_PATTERN;
    test_context.Expect(mappings_valid, OS_TEST_KVA_MAPPING_QUERY);

    bool cleanup_valid = true;
    os::kernel::PageTableUnmapResult cleanup_result{};
    for (uint64_t mapped_page_count = mapped_data_page_count;
         mapped_page_count > OS_TEST_KVA_MAPPING_EMPTY_VALUE; --mapped_page_count) {
        const uint64_t data_page_index = mapped_page_count - OS_TEST_KVA_MAPPING_SINGLE_UNIT;
        cleanup_valid =
            page_table_manager.UnmapPage(
                VirtualPageAddress(virtual_range,
                                   OS_TEST_KVA_MAPPING_FIRST_DATA_PAGE_OFFSET + data_page_index),
                cleanup_result) == os::kernel::PageTableStatus::Succeeded &&
            cleanup_valid;
    }
    for (uint64_t allocated_page_count = allocated_data_page_count;
         allocated_page_count > OS_TEST_KVA_MAPPING_EMPTY_VALUE; --allocated_page_count) {
        cleanup_valid = frame_allocator.Release(
                            data_frames[allocated_page_count - OS_TEST_KVA_MAPPING_SINGLE_UNIT]) ==
                            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
                        cleanup_valid;
    }
    cleanup_valid = virtual_address_allocator.TryRelease(virtual_range) ==
                        os::kernel::KernelVirtualAddressAllocatorStatus::Succeeded &&
                    cleanup_valid;

    const os::kernel::PhysicalFrameAllocatorStatistics frames_after_lifecycle =
        frame_allocator.Statistics();
    const os::kernel::KernelVirtualAddressAllocatorStatistics virtual_addresses_after_lifecycle =
        virtual_address_allocator.Statistics();
    test_context.Expect(cleanup_valid &&
                            cleanup_result.reclaimed_table_frame_count ==
                                OS_TEST_KVA_MAPPING_EXPECTED_RECLAIMED_TABLE_FRAME_COUNT &&
                            frames_after_lifecycle.allocated_frame_count ==
                                OS_TEST_KVA_MAPPING_EXPECTED_PAGE_TABLE_FRAME_COUNT &&
                            frames_after_lifecycle.free_frame_count +
                                    OS_TEST_KVA_MAPPING_EXPECTED_PAGE_TABLE_FRAME_COUNT ==
                                frames_before_lifecycle.free_frame_count +
                                    frames_before_lifecycle.allocated_frame_count &&
                            virtual_addresses_after_lifecycle.active_allocation_count ==
                                OS_TEST_KVA_MAPPING_EMPTY_VALUE &&
                            virtual_addresses_after_lifecycle.reservation_count ==
                                OS_TEST_KVA_MAPPING_PERMANENT_GUARD_PAGE_COUNT &&
                            virtual_addresses_after_lifecycle.successful_allocation_count ==
                                OS_TEST_KVA_MAPPING_SINGLE_UNIT &&
                            virtual_addresses_after_lifecycle.release_count ==
                                OS_TEST_KVA_MAPPING_SINGLE_UNIT &&
                            virtual_address_allocator.Validate() ==
                                os::kernel::KernelVirtualAddressAllocatorStatus::Succeeded,
                        OS_TEST_KVA_MAPPING_LIFECYCLE);
    return test_context.ExitCode();
}

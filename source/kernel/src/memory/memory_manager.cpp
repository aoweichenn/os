#include "os/kernel/memory/memory_manager.hpp"

#include "os/foundation/reference_counter.hpp"
#include "os/foundation/scope_rollback.hpp"
#include "os/kernel/arch/descriptor_tables.hpp"
#include "os/kernel/memory/page_table.hpp"
#include "os/kernel/memory/physical_frame_allocator.hpp"
#include "os/kernel/memory/physical_memory_map.hpp"
#include "os/kernel/arch/processor.hpp"
#include "os/kernel/user/user_elf.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_MEMORY_LOW_PLATFORM_RESERVATION_SIZE_BYTES = 1ULL * 1024ULL * 1024ULL;
constexpr uint64_t OS_KERNEL_MEMORY_EARLY_STACK_SIZE_BYTES = 64ULL * 1024ULL;
constexpr uint64_t OS_KERNEL_MEMORY_PAGE_MASK = OS_KERNEL_MEMORY_PAGE_SIZE_BYTES - 1ULL;
constexpr uint64_t OS_KERNEL_MEMORY_LARGE_PAGE_MASK =
    OS_KERNEL_PAGE_TABLE_LARGE_PAGE_SIZE_BYTES - 1ULL;
constexpr uint64_t OS_KERNEL_MEMORY_HEAP_PAGE_COUNT =
    OS_KERNEL_MEMORY_HEAP_SIZE_BYTES / OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_KERNEL_MEMORY_BOOTSTRAP_RESERVATION_COUNT = 3ULL;
constexpr uint64_t OS_KERNEL_MEMORY_BOOTSTRAP_METADATA_ALIGNMENT_BYTES =
    OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_KERNEL_MEMORY_HIGH_FRAME_MINIMUM_ADDRESS =
    4ULL * 1024ULL * 1024ULL * 1024ULL + OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_KERNEL_MEMORY_HEAP_SELF_TEST_SMALL_SIZE_BYTES = 64ULL;
constexpr uint64_t OS_KERNEL_MEMORY_HEAP_SELF_TEST_SMALL_ALIGNMENT_BYTES = 16ULL;
constexpr uint64_t OS_KERNEL_MEMORY_HEAP_SELF_TEST_PAGE_SIZE_BYTES =
    OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_KERNEL_MEMORY_HEAP_SELF_TEST_PAGE_ALIGNMENT_BYTES =
    OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_KERNEL_MEMORY_HEAP_SELF_TEST_FIRST_PATTERN = 0x13579BDF2468ACE0ULL;
constexpr uint64_t OS_KERNEL_MEMORY_HEAP_SELF_TEST_SECOND_PATTERN = 0xC001D00DC0FFEE11ULL;
constexpr uint64_t OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_CAPACITY = 32ULL;
constexpr uint64_t OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_ALIGNMENT_BYTES = 64ULL;
constexpr uint64_t OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_VALUE_COUNT = 8ULL;
constexpr uint64_t OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_FIRST_VALUE_INDEX =
    OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_EMPTY_VALUE;
constexpr uint64_t OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_LAST_VALUE_INDEX =
    OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_VALUE_COUNT -
    OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_COUNTER_INCREMENT;
constexpr uint64_t OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_ALTERNATING_STEP = 2ULL;
constexpr uint64_t OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_FIRST_PATTERN = 0x5459504543414348ULL;
constexpr uint64_t OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_LAST_PATTERN = 0x53454C4654455354ULL;
constexpr uint64_t OS_KERNEL_MEMORY_HIGH_FRAME_SELF_TEST_FIRST_PATTERN = 0x484947484652414DULL;
constexpr uint64_t OS_KERNEL_MEMORY_HIGH_FRAME_SELF_TEST_SECOND_PATTERN = 0x4449524543544D50ULL;
constexpr uint64_t OS_KERNEL_MEMORY_HIGH_FRAME_SELF_TEST_FIRST_VALUE_INDEX = 0ULL;
constexpr uint64_t OS_KERNEL_MEMORY_HIGH_FRAME_SELF_TEST_SECOND_VALUE_INDEX = 1ULL;
constexpr uint64_t OS_KERNEL_MEMORY_BUDDY_SELF_TEST_ORDER = 3ULL;
constexpr uint64_t OS_KERNEL_MEMORY_BUDDY_SELF_TEST_FRAME_COUNT =
    1ULL << OS_KERNEL_MEMORY_BUDDY_SELF_TEST_ORDER;
constexpr uint64_t OS_KERNEL_MEMORY_BUDDY_SELF_TEST_SIZE_BYTES =
    OS_KERNEL_MEMORY_BUDDY_SELF_TEST_FRAME_COUNT * OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_KERNEL_MEMORY_BUDDY_SELF_TEST_FIRST_PATTERN = 0x4255444459464952ULL;
constexpr uint64_t OS_KERNEL_MEMORY_BUDDY_SELF_TEST_LAST_PATTERN = 0x42554444594C4153ULL;
constexpr uint64_t OS_KERNEL_MEMORY_KVA_PAGE_COUNT =
    OS_KERNEL_MEMORY_KVA_CAPACITY_BYTES / OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_KERNEL_MEMORY_KVA_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_MEMORY_KVA_SINGLE_UNIT = 1ULL;
constexpr uint64_t OS_KERNEL_MEMORY_KVA_PERMANENT_GUARD_PAGE_COUNT =
    OS_KERNEL_MEMORY_KVA_SINGLE_UNIT;
constexpr uint64_t OS_KERNEL_MEMORY_KVA_WARMUP_PAGE_COUNT = OS_KERNEL_MEMORY_KVA_SINGLE_UNIT;
constexpr uint64_t OS_KERNEL_MEMORY_KVA_WARMUP_ALIGNMENT_PAGE_COUNT =
    OS_KERNEL_MEMORY_KVA_SINGLE_UNIT;
constexpr uint64_t OS_KERNEL_MEMORY_KVA_SELF_TEST_BLOCK_ORDER = 2ULL;
constexpr uint64_t OS_KERNEL_MEMORY_KVA_SELF_TEST_MAPPED_PAGE_COUNT =
    OS_KERNEL_MEMORY_KVA_SINGLE_UNIT << OS_KERNEL_MEMORY_KVA_SELF_TEST_BLOCK_ORDER;
constexpr uint64_t OS_KERNEL_MEMORY_KVA_SELF_TEST_GUARD_PAGE_COUNT = 2ULL;
constexpr uint64_t OS_KERNEL_MEMORY_KVA_SELF_TEST_RANGE_PAGE_COUNT =
    OS_KERNEL_MEMORY_KVA_SELF_TEST_MAPPED_PAGE_COUNT +
    OS_KERNEL_MEMORY_KVA_SELF_TEST_GUARD_PAGE_COUNT;
constexpr uint64_t OS_KERNEL_MEMORY_KVA_SELF_TEST_ALIGNMENT_PAGE_COUNT = 8ULL;
constexpr uint64_t OS_KERNEL_MEMORY_KVA_SELF_TEST_FIRST_MAPPED_PAGE_OFFSET =
    OS_KERNEL_MEMORY_KVA_SINGLE_UNIT;
constexpr uint64_t OS_KERNEL_MEMORY_KVA_SELF_TEST_LAST_GUARD_PAGE_OFFSET =
    OS_KERNEL_MEMORY_KVA_SELF_TEST_RANGE_PAGE_COUNT - OS_KERNEL_MEMORY_KVA_SINGLE_UNIT;
constexpr uint64_t OS_KERNEL_MEMORY_KVA_SELF_TEST_FIRST_PATTERN = 0x4B564153454C4631ULL;
constexpr uint64_t OS_KERNEL_MEMORY_KVA_SELF_TEST_LAST_PATTERN = 0x4B564153454C4634ULL;
constexpr uint64_t OS_KERNEL_MEMORY_PAGE_TABLE_RECLAIM_SELF_TEST_PHASE_COUNT = 2ULL;
constexpr uint64_t OS_KERNEL_MEMORY_PAGE_TABLE_RECLAIM_SELF_TEST_EXPECTED_LEVEL1_TABLE_COUNT =
    OS_KERNEL_MEMORY_PAGE_TABLE_RECLAIM_SELF_TEST_PHASE_COUNT;
constexpr uint64_t OS_KERNEL_MEMORY_PAGE_TABLE_RECLAIM_SELF_TEST_EXPECTED_LEVEL2_TABLE_COUNT =
    OS_KERNEL_MEMORY_PAGE_TABLE_RECLAIM_SELF_TEST_PHASE_COUNT;
constexpr uint64_t OS_KERNEL_MEMORY_PAGE_TABLE_RECLAIM_SELF_TEST_EXPECTED_LEVEL3_TABLE_COUNT =
    OS_KERNEL_MEMORY_KVA_EMPTY_VALUE;
constexpr uint64_t
    OS_KERNEL_MEMORY_PAGE_TABLE_RECLAIM_SELF_TEST_EXPECTED_RETAINED_LEVEL3_TABLE_COUNT =
        OS_KERNEL_MEMORY_KVA_SINGLE_UNIT;
constexpr uint64_t OS_KERNEL_MEMORY_KVA_SELF_TEST_EXPECTED_BUDDY_OPERATION_COUNT =
    OS_KERNEL_MEMORY_KVA_SINGLE_UNIT +
    OS_KERNEL_MEMORY_PAGE_TABLE_RECLAIM_SELF_TEST_EXPECTED_LEVEL1_TABLE_COUNT /
        OS_KERNEL_MEMORY_PAGE_TABLE_RECLAIM_SELF_TEST_PHASE_COUNT +
    OS_KERNEL_MEMORY_PAGE_TABLE_RECLAIM_SELF_TEST_EXPECTED_LEVEL2_TABLE_COUNT /
        OS_KERNEL_MEMORY_PAGE_TABLE_RECLAIM_SELF_TEST_PHASE_COUNT;
constexpr uint64_t OS_KERNEL_MEMORY_MINIMUM_PAGE_TABLE_PHYSICAL_LIMIT =
    OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_KERNEL_MEMORY_RESOURCE_SELF_TEST_ACTION_CAPACITY = 1ULL;
constexpr uint64_t OS_KERNEL_MEMORY_RESOURCE_SELF_TEST_INITIAL_REFERENCE_COUNT = 1ULL;
constexpr uint64_t OS_KERNEL_MEMORY_RESOURCE_SELF_TEST_LAST_STACK_SLOT =
    OS_KERNEL_MEMORY_KERNEL_STACK_SLOT_CAPACITY - 1ULL;
constexpr uint64_t OS_KERNEL_MEMORY_RESOURCE_SELF_TEST_PASSED_VALUE = 1ULL;
constexpr uint64_t OS_KERNEL_MEMORY_RESOURCE_SELF_TEST_EMPTY_VALUE = 0ULL;

static_assert(OS_KERNEL_MEMORY_KVA_CAPACITY_BYTES % OS_KERNEL_MEMORY_PAGE_SIZE_BYTES ==
              OS_KERNEL_MEMORY_KVA_EMPTY_VALUE);

KernelMemoryStatistics current_kernel_memory_statistics{};
uint64_t current_managed_physical_address_limit = 0ULL;
uint64_t current_page_table_physical_address_limit = 0ULL;
bool direct_map_active = false;
KernelVirtualAddressRangeDescriptor
    kernel_virtual_address_descriptors[OS_KERNEL_MEMORY_KVA_DESCRIPTOR_CAPACITY]{};
KernelStack kernel_stack_storage[OS_KERNEL_MEMORY_KERNEL_STACK_SLOT_CAPACITY]{};

struct DirectMapStatistics final {
    uint64_t mapped_bytes;
    uint64_t large_page_count;
    uint64_t small_page_count;
};

struct alignas(OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_ALIGNMENT_BYTES)
    TypeCacheSelfTestObject final {
    uint64_t values[OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_VALUE_COUNT];
};

struct KernelVirtualAddressSelfTestStatistics final {
    uint64_t virtual_address;
    uint64_t physical_address;
    uint64_t mapped_page_count;
    uint64_t guard_page_count;
};

struct PageTableReclamationSelfTestStatistics final {
    uint64_t reclaimed_level1_table_count;
    uint64_t reclaimed_level2_table_count;
    uint64_t reclaimed_level3_table_count;
    uint64_t retained_shared_level3_table_count;
};

struct ResourceLifecycleSelfTestStatistics final {
    uint64_t tracked_field_count;
    uint64_t changed_fields_mask;
    uint64_t reference_counter_passed;
    uint64_t scope_rollback_passed;
    uint64_t resource_snapshot_passed;
};

struct KernelStackRollbackContext final {
    KernelStackManager *manager;
    uint64_t slot_index;
};

enum class ResourceLifecycleSelfTestStatus : uint64_t {
    Succeeded,
    BaselineSnapshotFailed,
    RollbackInitializationFailed,
    KernelStackCreationFailed,
    RollbackRegistrationFailed,
    ReferenceCounterFailed,
    ScopeRollbackFailed,
    FinalSnapshotFailed,
};

extern "C" uint8_t os_kernel_image_start[];
extern "C" uint8_t os_kernel_image_end[];
extern "C" uint8_t os_kernel_text_start[];
extern "C" uint8_t os_kernel_text_end[];
extern "C" uint8_t os_kernel_read_only_data_start[];
extern "C" uint8_t os_kernel_read_only_data_end[];
extern "C" uint8_t os_kernel_writable_data_start[];
extern "C" uint8_t os_kernel_writable_data_end[];

[[nodiscard]] uint64_t AddressOf(uint8_t *symbol) noexcept {
    return reinterpret_cast<uint64_t>(symbol);
}

[[nodiscard]] uint64_t AlignUpToPage(const uint64_t address) noexcept {
    return (address + OS_KERNEL_MEMORY_PAGE_MASK) & ~OS_KERNEL_MEMORY_PAGE_MASK;
}

[[nodiscard]] uint64_t AlignDownToPage(const uint64_t address) noexcept {
    return address & ~OS_KERNEL_MEMORY_PAGE_MASK;
}

[[nodiscard]] uint64_t Minimum(const uint64_t left, const uint64_t right) noexcept {
    return left < right ? left : right;
}

[[nodiscard]] PhysicalFrameAllocator &FrameAllocator() noexcept {
    static PhysicalFrameAllocator allocator{};
    return allocator;
}

[[nodiscard]] PageTableManager &GetPageTableManager() noexcept {
    static PageTableManager manager{
        FrameAllocator(),
        PageTableMemoryAccess{
            .maximum_physical_address_exclusive =
                OS_KERNEL_MEMORY_MINIMUM_PAGE_TABLE_PHYSICAL_LIMIT,
            .physical_memory_virtual_base = 0ULL,
            .allocation_maximum_physical_address_exclusive =
                OS_KERNEL_MEMORY_MINIMUM_PAGE_TABLE_PHYSICAL_LIMIT,
            .invalidate_active_mappings = false,
        },
        PageTableRootKind::KernelShared,
    };
    return manager;
}

[[nodiscard]] PageTableMemoryAccess ActivePageTableMemoryAccess() noexcept {
    return PageTableMemoryAccess{
        .maximum_physical_address_exclusive = current_page_table_physical_address_limit,
        .physical_memory_virtual_base = OS_KERNEL_MEMORY_DIRECT_MAP_VIRTUAL_BASE,
        .allocation_maximum_physical_address_exclusive = current_managed_physical_address_limit,
        .invalidate_active_mappings = true,
    };
}

[[nodiscard]] bool IsGuardPage(const uint64_t page_address, const BootInfo &boot_info) noexcept {
    const uint64_t early_stack_guard_address =
        boot_info.kernel_stack_top_physical_address - OS_KERNEL_MEMORY_EARLY_STACK_SIZE_BYTES;
    if (page_address == early_stack_guard_address) {
        return true;
    }
    for (uint64_t guard_page_index = 0ULL;
         guard_page_index < OS_KERNEL_DESCRIPTOR_INTERRUPT_STACK_GUARD_PAGE_COUNT;
         ++guard_page_index) {
        if (page_address == InterruptStackGuardPageAddress(guard_page_index)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] PagePermissions IdentityPermissions(const uint64_t page_address) noexcept {
    const uint64_t text_begin = AddressOf(os_kernel_text_start);
    const uint64_t text_end = AlignUpToPage(AddressOf(os_kernel_text_end));
    if (page_address >= text_begin && page_address < text_end) {
        return PagePermissions{
            .writable = false,
            .executable = true,
            .user_accessible = false,
            .cache_disabled = false,
        };
    }
    const uint64_t read_only_begin = AddressOf(os_kernel_read_only_data_start);
    const uint64_t read_only_end = AlignUpToPage(AddressOf(os_kernel_read_only_data_end));
    if (page_address >= read_only_begin && page_address < read_only_end) {
        return PagePermissions{
            .writable = false,
            .executable = false,
            .user_accessible = false,
            .cache_disabled = false,
        };
    }
    return PagePermissions{
        .writable = true,
        .executable = false,
        .user_accessible = false,
        .cache_disabled = false,
    };
}

[[nodiscard]] bool ReserveBootCriticalRanges(const BootInfo &boot_info) noexcept {
    PhysicalFrameAllocator &allocator = FrameAllocator();
    const uint64_t kernel_begin = AddressOf(os_kernel_image_start);
    const uint64_t kernel_end = AddressOf(os_kernel_image_end);
    const uint64_t early_stack_begin =
        boot_info.kernel_stack_top_physical_address - OS_KERNEL_MEMORY_EARLY_STACK_SIZE_BYTES;
    return allocator.ReserveRange(0ULL, OS_KERNEL_MEMORY_LOW_PLATFORM_RESERVATION_SIZE_BYTES) ==
               PhysicalFrameAllocatorStatus::Succeeded &&
           allocator.ReserveRange(kernel_begin, kernel_end - kernel_begin) ==
               PhysicalFrameAllocatorStatus::Succeeded &&
           allocator.ReserveRange(early_stack_begin, OS_KERNEL_MEMORY_EARLY_STACK_SIZE_BYTES) ==
               PhysicalFrameAllocatorStatus::Succeeded;
}

[[nodiscard]] bool FindFrameAllocatorMetadataRange(const PhysicalMemoryMapEntry *memory_map,
                                                   const uint64_t memory_map_entry_count,
                                                   const BootInfo &boot_info,
                                                   const uint64_t required_storage_size_bytes,
                                                   PhysicalMemoryRange &storage_range) noexcept {
    const uint64_t kernel_begin = AddressOf(os_kernel_image_start);
    const uint64_t kernel_end = AddressOf(os_kernel_image_end);
    const uint64_t early_stack_begin =
        boot_info.kernel_stack_top_physical_address - OS_KERNEL_MEMORY_EARLY_STACK_SIZE_BYTES;
    const PhysicalMemoryRange reservations[OS_KERNEL_MEMORY_BOOTSTRAP_RESERVATION_COUNT] = {
        {
            .begin_address = 0ULL,
            .length_bytes = OS_KERNEL_MEMORY_LOW_PLATFORM_RESERVATION_SIZE_BYTES,
        },
        {
            .begin_address = kernel_begin,
            .length_bytes = kernel_end - kernel_begin,
        },
        {
            .begin_address = early_stack_begin,
            .length_bytes = OS_KERNEL_MEMORY_EARLY_STACK_SIZE_BYTES,
        },
    };
    return FindUsablePhysicalMemoryRange(
               memory_map, memory_map_entry_count, reservations,
               OS_KERNEL_MEMORY_BOOTSTRAP_RESERVATION_COUNT,
               OS_KERNEL_MEMORY_LOW_PLATFORM_RESERVATION_SIZE_BYTES,
               boot_info.identity_mapped_size_bytes, required_storage_size_bytes,
               OS_KERNEL_MEMORY_BOOTSTRAP_METADATA_ALIGNMENT_BYTES,
               storage_range) == PhysicalMemoryRangeSearchStatus::Succeeded;
}

[[nodiscard]] bool ReserveFrameAllocatorMetadata(const PhysicalMemoryRange storage_range) noexcept {
    return FrameAllocator().ReserveRange(storage_range.begin_address, storage_range.length_bytes) ==
           PhysicalFrameAllocatorStatus::Succeeded;
}

[[nodiscard]] bool MapIdentityRange(const BootInfo &boot_info) noexcept {
    PageTableManager &manager = GetPageTableManager();
    for (uint64_t page_address = 0ULL; page_address < boot_info.identity_mapped_size_bytes;
         page_address += OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) {
        if (page_address == 0ULL || IsGuardPage(page_address, boot_info)) {
            continue;
        }
        if (manager.MapPage(page_address, page_address, IdentityPermissions(page_address)) !=
            PageTableStatus::Succeeded) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool MapDirectPage(const uint64_t physical_address,
                                 DirectMapStatistics &statistics) noexcept {
    const uint64_t virtual_address = OS_KERNEL_MEMORY_DIRECT_MAP_VIRTUAL_BASE + physical_address;
    const PagePermissions direct_map_permissions{
        .writable = true,
        .executable = false,
        .user_accessible = false,
        .cache_disabled = false,
    };
    if (GetPageTableManager().MapPage(virtual_address, physical_address, direct_map_permissions) !=
        PageTableStatus::Succeeded) {
        return false;
    }
    statistics.mapped_bytes += OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
    ++statistics.small_page_count;
    return true;
}

[[nodiscard]] bool MapDirectLargePage(const uint64_t physical_address,
                                      DirectMapStatistics &statistics) noexcept {
    const uint64_t virtual_address = OS_KERNEL_MEMORY_DIRECT_MAP_VIRTUAL_BASE + physical_address;
    const PagePermissions direct_map_permissions{
        .writable = true,
        .executable = false,
        .user_accessible = false,
        .cache_disabled = false,
    };
    if (GetPageTableManager().MapLargePage(virtual_address, physical_address,
                                           direct_map_permissions) != PageTableStatus::Succeeded) {
        return false;
    }
    statistics.mapped_bytes += OS_KERNEL_PAGE_TABLE_LARGE_PAGE_SIZE_BYTES;
    ++statistics.large_page_count;
    return true;
}

[[nodiscard]] bool MapDirectMemoryRange(const PhysicalMemoryMapEntry &entry,
                                        DirectMapStatistics &statistics) noexcept {
    if (entry.type != OS_KERNEL_MEMORY_MAP_USABLE_REGION_TYPE ||
        entry.base_address >= current_managed_physical_address_limit) {
        return true;
    }
    const uint64_t entry_end_address = entry.base_address + entry.length_bytes;
    uint64_t physical_address = AlignUpToPage(entry.base_address);
    const uint64_t mapped_end_address =
        AlignDownToPage(Minimum(entry_end_address, current_managed_physical_address_limit));
    while (physical_address < mapped_end_address &&
           (physical_address & OS_KERNEL_MEMORY_LARGE_PAGE_MASK) != 0ULL) {
        if (!MapDirectPage(physical_address, statistics)) {
            return false;
        }
        physical_address += OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
    }
    while (physical_address < mapped_end_address &&
           OS_KERNEL_PAGE_TABLE_LARGE_PAGE_SIZE_BYTES <= mapped_end_address - physical_address) {
        if (!MapDirectLargePage(physical_address, statistics)) {
            return false;
        }
        physical_address += OS_KERNEL_PAGE_TABLE_LARGE_PAGE_SIZE_BYTES;
    }
    while (physical_address < mapped_end_address) {
        if (!MapDirectPage(physical_address, statistics)) {
            return false;
        }
        physical_address += OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
    }
    return true;
}

[[nodiscard]] bool MapDirectMemory(const PhysicalMemoryMapEntry *memory_map,
                                   const uint64_t memory_map_entry_count,
                                   DirectMapStatistics &statistics) noexcept {
    for (uint64_t entry_index = 0ULL; entry_index < memory_map_entry_count; ++entry_index) {
        if (!MapDirectMemoryRange(memory_map[entry_index], statistics)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool MapKernelHeap() noexcept {
    PhysicalFrameAllocator &allocator = FrameAllocator();
    PageTableManager &manager = GetPageTableManager();
    const PagePermissions heap_permissions{
        .writable = true,
        .executable = false,
        .user_accessible = false,
        .cache_disabled = false,
    };
    for (uint64_t page_index = 0ULL; page_index < OS_KERNEL_MEMORY_HEAP_PAGE_COUNT; ++page_index) {
        PhysicalFrame frame{};
        if (allocator.Allocate(frame) != PhysicalFrameAllocatorStatus::Succeeded) {
            return false;
        }
        const uint64_t virtual_address =
            OS_KERNEL_MEMORY_HEAP_VIRTUAL_BASE + page_index * OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
        if (manager.MapPage(virtual_address, frame.physical_address, heap_permissions) !=
            PageTableStatus::Succeeded) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool MapLocalApicRegisters() noexcept {
    if (!ProcessorSupportsLocalApic()) {
        return true;
    }
    const uint64_t local_apic_address = LocalApicPhysicalAddress();
    if (local_apic_address == 0ULL || (local_apic_address & OS_KERNEL_MEMORY_PAGE_MASK) != 0ULL) {
        return false;
    }
    const PagePermissions device_permissions{
        .writable = true,
        .executable = false,
        .user_accessible = false,
        .cache_disabled = true,
    };
    return GetPageTableManager().MapPage(local_apic_address, local_apic_address,
                                         device_permissions) == PageTableStatus::Succeeded;
}

[[nodiscard]] bool MapWriteProtectionTestPage() noexcept {
    PhysicalFrame frame{};
    if (FrameAllocator().Allocate(frame) != PhysicalFrameAllocatorStatus::Succeeded) {
        return false;
    }
    const PagePermissions read_only_permissions{
        .writable = false,
        .executable = false,
        .user_accessible = false,
        .cache_disabled = false,
    };
    PageTableManager &manager = GetPageTableManager();
    if (manager.MapPage(OS_KERNEL_MEMORY_WRITE_PROTECTION_TEST_VIRTUAL_ADDRESS,
                        frame.physical_address,
                        read_only_permissions) != PageTableStatus::Succeeded ||
        manager.MapPage(OS_KERNEL_MEMORY_WRITE_PROTECTION_TEST_VIRTUAL_ADDRESS,
                        frame.physical_address,
                        read_only_permissions) != PageTableStatus::AlreadyMapped ||
        manager.UnmapPage(OS_KERNEL_MEMORY_WRITE_PROTECTION_TEST_VIRTUAL_ADDRESS) !=
            PageTableStatus::Succeeded) {
        return false;
    }
    PageMapping removed_mapping{};
    if (manager.QueryPage(OS_KERNEL_MEMORY_WRITE_PROTECTION_TEST_VIRTUAL_ADDRESS,
                          removed_mapping) != PageTableStatus::NotMapped) {
        return false;
    }
    return manager.MapPage(OS_KERNEL_MEMORY_WRITE_PROTECTION_TEST_VIRTUAL_ADDRESS,
                           frame.physical_address,
                           read_only_permissions) == PageTableStatus::Succeeded;
}

[[nodiscard]] bool ValidateMapping(const uint64_t virtual_address,
                                   const uint64_t expected_physical_address,
                                   const PagePermissions expected_permissions) noexcept {
    PageMapping mapping{};
    if (GetPageTableManager().QueryPage(virtual_address, mapping) != PageTableStatus::Succeeded) {
        return false;
    }
    return mapping.physical_address == expected_physical_address &&
           mapping.permissions.writable == expected_permissions.writable &&
           mapping.permissions.executable == expected_permissions.executable &&
           mapping.permissions.user_accessible == expected_permissions.user_accessible &&
           mapping.permissions.cache_disabled == expected_permissions.cache_disabled;
}

[[nodiscard]] bool ValidatePermissions(const uint64_t virtual_address,
                                       const PagePermissions expected_permissions) noexcept {
    PageMapping mapping{};
    if (GetPageTableManager().QueryPage(virtual_address, mapping) != PageTableStatus::Succeeded) {
        return false;
    }
    return mapping.physical_address != 0ULL &&
           mapping.permissions.writable == expected_permissions.writable &&
           mapping.permissions.executable == expected_permissions.executable &&
           mapping.permissions.user_accessible == expected_permissions.user_accessible &&
           mapping.permissions.cache_disabled == expected_permissions.cache_disabled;
}

[[nodiscard]] bool
ValidateKernelMappings(const BootInfo &boot_info,
                       const PhysicalMemoryRange frame_allocator_metadata_range) noexcept {
    const PagePermissions text_permissions{
        .writable = false,
        .executable = true,
        .user_accessible = false,
        .cache_disabled = false,
    };
    const PagePermissions read_only_permissions{
        .writable = false,
        .executable = false,
        .user_accessible = false,
        .cache_disabled = false,
    };
    const PagePermissions writable_permissions{
        .writable = true,
        .executable = false,
        .user_accessible = false,
        .cache_disabled = false,
    };
    PageMapping ignored_mapping{};
    if (!ValidateMapping(AddressOf(os_kernel_text_start), AddressOf(os_kernel_text_start),
                         text_permissions) ||
        !ValidateMapping(AddressOf(os_kernel_read_only_data_start),
                         AddressOf(os_kernel_read_only_data_start), read_only_permissions) ||
        !ValidateMapping(AddressOf(os_kernel_writable_data_start),
                         AddressOf(os_kernel_writable_data_start), writable_permissions) ||
        !ValidatePermissions(OS_KERNEL_MEMORY_HEAP_VIRTUAL_BASE, writable_permissions) ||
        !ValidatePermissions(OS_KERNEL_MEMORY_WRITE_PROTECTION_TEST_VIRTUAL_ADDRESS,
                             read_only_permissions) ||
        !ValidateMapping(OS_KERNEL_MEMORY_DIRECT_MAP_VIRTUAL_BASE +
                             frame_allocator_metadata_range.begin_address,
                         frame_allocator_metadata_range.begin_address, writable_permissions)) {
        return false;
    }
    if (ProcessorSupportsLocalApic()) {
        const PagePermissions device_permissions{
            .writable = true,
            .executable = false,
            .user_accessible = false,
            .cache_disabled = true,
        };
        const uint64_t local_apic_address = LocalApicPhysicalAddress();
        if (!ValidateMapping(local_apic_address, local_apic_address, device_permissions)) {
            return false;
        }
    }
    if (GetPageTableManager().QueryPage(0ULL, ignored_mapping) != PageTableStatus::NotMapped ||
        GetPageTableManager().QueryPage(OS_KERNEL_PROCESSOR_UNMAPPED_TEST_ADDRESS,
                                        ignored_mapping) != PageTableStatus::NotMapped) {
        return false;
    }
    const uint64_t early_stack_guard_address =
        boot_info.kernel_stack_top_physical_address - OS_KERNEL_MEMORY_EARLY_STACK_SIZE_BYTES;
    if (GetPageTableManager().QueryPage(early_stack_guard_address, ignored_mapping) !=
        PageTableStatus::NotMapped) {
        return false;
    }
    for (uint64_t guard_page_index = 0ULL;
         guard_page_index < OS_KERNEL_DESCRIPTOR_INTERRUPT_STACK_GUARD_PAGE_COUNT;
         ++guard_page_index) {
        if (GetPageTableManager().QueryPage(InterruptStackGuardPageAddress(guard_page_index),
                                            ignored_mapping) != PageTableStatus::NotMapped) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool RunHeapSelfTest() noexcept {
    KernelHeap &heap = GetKernelHeap();
    void *small_allocation = nullptr;
    if (heap.TryAllocate(OS_KERNEL_MEMORY_HEAP_SELF_TEST_SMALL_SIZE_BYTES,
                         OS_KERNEL_MEMORY_HEAP_SELF_TEST_SMALL_ALIGNMENT_BYTES,
                         small_allocation) != KernelHeapStatus::Succeeded) {
        return false;
    }
    void *page_allocation = nullptr;
    if (heap.TryAllocate(OS_KERNEL_MEMORY_HEAP_SELF_TEST_PAGE_SIZE_BYTES,
                         OS_KERNEL_MEMORY_HEAP_SELF_TEST_PAGE_ALIGNMENT_BYTES,
                         page_allocation) != KernelHeapStatus::Succeeded) {
        static_cast<void>(heap.TryRelease(small_allocation));
        return false;
    }
    volatile uint64_t *const small_value = reinterpret_cast<volatile uint64_t *>(small_allocation);
    volatile uint64_t *const page_value = reinterpret_cast<volatile uint64_t *>(page_allocation);
    *small_value = OS_KERNEL_MEMORY_HEAP_SELF_TEST_FIRST_PATTERN;
    *page_value = OS_KERNEL_MEMORY_HEAP_SELF_TEST_SECOND_PATTERN;
    const bool patterns_valid = *small_value == OS_KERNEL_MEMORY_HEAP_SELF_TEST_FIRST_PATTERN &&
                                *page_value == OS_KERNEL_MEMORY_HEAP_SELF_TEST_SECOND_PATTERN;
    const bool page_released = heap.TryRelease(page_allocation) == KernelHeapStatus::Succeeded;
    const bool small_released = heap.TryRelease(small_allocation) == KernelHeapStatus::Succeeded;
    const KernelHeapStatistics statistics = heap.Statistics();
    return patterns_valid && page_released && small_released &&
           heap.Validate() == KernelHeapStatus::Succeeded &&
           statistics.capacity_bytes == OS_KERNEL_MEMORY_HEAP_SIZE_BYTES &&
           statistics.consumed_bytes == 0ULL && statistics.allocation_count == 0ULL &&
           statistics.active_requested_bytes == 0ULL &&
           statistics.successful_allocation_count == 2ULL &&
           statistics.release_count == statistics.successful_allocation_count &&
           statistics.peak_consumed_bytes > 0ULL && statistics.largest_free_allocation_bytes > 0ULL;
}

[[nodiscard]] bool RunTypeCacheSelfTest(KernelTypeCacheStatistics &type_cache_statistics) noexcept {
    KernelHeap &heap = GetKernelHeap();
    const KernelHeapStatistics heap_before_test = heap.Statistics();
    KernelTypeCache<TypeCacheSelfTestObject> cache{};
    if (cache.Initialize(heap, OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_CAPACITY) !=
        KernelTypeCacheStatus::Succeeded) {
        return false;
    }

    TypeCacheSelfTestObject *objects[OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_CAPACITY]{};
    for (uint64_t object_index = OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_EMPTY_VALUE;
         object_index < OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_CAPACITY; ++object_index) {
        if (cache.TryAcquire(objects[object_index]) != KernelTypeCacheStatus::Succeeded ||
            (reinterpret_cast<uint64_t>(objects[object_index]) &
             (OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_ALIGNMENT_BYTES -
              OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_COUNTER_INCREMENT)) !=
                OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_EMPTY_VALUE) {
            return false;
        }
        objects[object_index]->values[OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_FIRST_VALUE_INDEX] =
            OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_FIRST_PATTERN + object_index;
        objects[object_index]->values[OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_LAST_VALUE_INDEX] =
            OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_LAST_PATTERN - object_index;
    }

    TypeCacheSelfTestObject *exhausted_output =
        objects[OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_FIRST_VALUE_INDEX];
    if (cache.TryAcquire(exhausted_output) != KernelTypeCacheStatus::OutOfObjects ||
        exhausted_output != objects[OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_FIRST_VALUE_INDEX]) {
        return false;
    }
    for (uint64_t object_index = OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_EMPTY_VALUE;
         object_index < OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_CAPACITY; ++object_index) {
        if (objects[object_index]
                    ->values[OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_FIRST_VALUE_INDEX] !=
                OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_FIRST_PATTERN + object_index ||
            objects[object_index]->values[OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_LAST_VALUE_INDEX] !=
                OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_LAST_PATTERN - object_index) {
            return false;
        }
    }

    for (uint64_t object_index = OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_EMPTY_VALUE;
         object_index < OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_CAPACITY;
         object_index += OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_ALTERNATING_STEP) {
        if (cache.TryRelease(objects[object_index]) != KernelTypeCacheStatus::Succeeded) {
            return false;
        }
    }
    if (cache.TryRelease(objects[OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_FIRST_VALUE_INDEX]) !=
        KernelTypeCacheStatus::ObjectNotActive) {
        return false;
    }
    for (uint64_t object_index = OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_COUNTER_INCREMENT;
         object_index < OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_CAPACITY;
         object_index += OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_ALTERNATING_STEP) {
        if (cache.TryRelease(objects[object_index]) != KernelTypeCacheStatus::Succeeded) {
            return false;
        }
    }

    TypeCacheSelfTestObject *reused_object = nullptr;
    if (cache.TryAcquire(reused_object) != KernelTypeCacheStatus::Succeeded ||
        reused_object != objects[OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_CAPACITY -
                                 OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_COUNTER_INCREMENT] ||
        cache.TryRelease(reused_object) != KernelTypeCacheStatus::Succeeded ||
        cache.Validate() != KernelTypeCacheStatus::Succeeded) {
        return false;
    }
    type_cache_statistics = cache.Statistics();
    const bool statistics_valid =
        type_cache_statistics.object_size_bytes == sizeof(TypeCacheSelfTestObject) &&
        type_cache_statistics.object_alignment_bytes ==
            OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_ALIGNMENT_BYTES &&
        type_cache_statistics.slot_stride_bytes == sizeof(TypeCacheSelfTestObject) &&
        type_cache_statistics.capacity == OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_CAPACITY &&
        type_cache_statistics.active_object_count ==
            OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_EMPTY_VALUE &&
        type_cache_statistics.free_object_count == OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_CAPACITY &&
        type_cache_statistics.successful_allocation_count ==
            OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_CAPACITY +
                OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_COUNTER_INCREMENT &&
        type_cache_statistics.release_count == type_cache_statistics.successful_allocation_count &&
        type_cache_statistics.peak_active_object_count ==
            OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_CAPACITY &&
        type_cache_statistics.backing_storage_size_bytes >
            OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_EMPTY_VALUE;
    if (!statistics_valid || cache.Destroy() != KernelTypeCacheStatus::Succeeded) {
        return false;
    }

    const KernelHeapStatistics heap_after_test = heap.Statistics();
    return heap.Validate() == KernelHeapStatus::Succeeded &&
           heap_after_test.capacity_bytes == heap_before_test.capacity_bytes &&
           heap_after_test.consumed_bytes == heap_before_test.consumed_bytes &&
           heap_after_test.allocation_count == heap_before_test.allocation_count &&
           heap_after_test.active_requested_bytes == heap_before_test.active_requested_bytes &&
           heap_after_test.successful_allocation_count ==
               heap_before_test.successful_allocation_count +
                   OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_COUNTER_INCREMENT &&
           heap_after_test.release_count ==
               heap_before_test.release_count +
                   OS_KERNEL_MEMORY_TYPE_CACHE_SELF_TEST_COUNTER_INCREMENT &&
           heap_after_test.largest_free_allocation_bytes ==
               heap_before_test.largest_free_allocation_bytes;
}

[[nodiscard]] uint64_t KernelVirtualPageAddress(const KernelVirtualAddressRange range,
                                                const uint64_t page_offset) noexcept {
    return range.begin_address + page_offset * OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
}

[[nodiscard]] bool WarmUpKernelVirtualAddressPageTables(
    PageTableReclamationSelfTestStatistics &page_table_statistics) noexcept {
    page_table_statistics = PageTableReclamationSelfTestStatistics{};
    KernelVirtualAddressAllocator &virtual_address_allocator = GetKernelVirtualAddressAllocator();
    const PhysicalFrameAllocatorStatistics frames_before_warmup = FrameAllocator().Statistics();
    KernelVirtualAddressRange warmup_range{};
    if (virtual_address_allocator.TryAllocate(OS_KERNEL_MEMORY_KVA_WARMUP_PAGE_COUNT,
                                              OS_KERNEL_MEMORY_KVA_WARMUP_ALIGNMENT_PAGE_COUNT,
                                              warmup_range) !=
        KernelVirtualAddressAllocatorStatus::Succeeded) {
        return false;
    }

    PhysicalFrame warmup_frame{};
    if (FrameAllocator().Allocate(warmup_frame) != PhysicalFrameAllocatorStatus::Succeeded) {
        static_cast<void>(virtual_address_allocator.TryRelease(warmup_range));
        return false;
    }
    const PagePermissions permissions{
        .writable = true,
        .executable = false,
        .user_accessible = false,
        .cache_disabled = false,
    };
    const bool mapping_created =
        GetPageTableManager().MapPage(warmup_range.begin_address, warmup_frame.physical_address,
                                      permissions) == PageTableStatus::Succeeded;
    bool mapping_valid = mapping_created;
    if (mapping_created) {
        PageMapping mapping{};
        volatile uint64_t *const value =
            reinterpret_cast<volatile uint64_t *>(warmup_range.begin_address);
        *value = OS_KERNEL_MEMORY_KVA_SELF_TEST_FIRST_PATTERN;
        mapping_valid = *value == OS_KERNEL_MEMORY_KVA_SELF_TEST_FIRST_PATTERN &&
                        GetPageTableManager().QueryPage(warmup_range.begin_address, mapping) ==
                            PageTableStatus::Succeeded &&
                        mapping.physical_address == warmup_frame.physical_address &&
                        mapping.permissions.writable && !mapping.permissions.executable &&
                        !mapping.permissions.user_accessible;
    }

    PageTableUnmapResult unmap_result{};
    const bool mapping_released =
        mapping_created &&
        GetPageTableManager().UnmapPage(warmup_range.begin_address, unmap_result) ==
            PageTableStatus::Succeeded;
    const bool frame_released =
        FrameAllocator().Release(warmup_frame) == PhysicalFrameAllocatorStatus::Succeeded;
    const bool range_released = virtual_address_allocator.TryRelease(warmup_range) ==
                                KernelVirtualAddressAllocatorStatus::Succeeded;
    PageMapping ignored_mapping{};
    const bool mapping_absent =
        GetPageTableManager().QueryPage(warmup_range.begin_address, ignored_mapping) ==
        PageTableStatus::NotMapped;
    const PhysicalFrameAllocatorStatistics frames_after_warmup = FrameAllocator().Statistics();
    const PageTableReclamationSelfTestStatistics candidate_statistics{
        .reclaimed_level1_table_count = unmap_result.reclaimed_level1_table_count,
        .reclaimed_level2_table_count = unmap_result.reclaimed_level2_table_count,
        .reclaimed_level3_table_count = unmap_result.reclaimed_level3_table_count,
        .retained_shared_level3_table_count =
            frames_after_warmup.allocated_frame_count - frames_before_warmup.allocated_frame_count,
    };
    const bool statistics_valid =
        candidate_statistics.reclaimed_level1_table_count == OS_KERNEL_MEMORY_KVA_SINGLE_UNIT &&
        candidate_statistics.reclaimed_level2_table_count == OS_KERNEL_MEMORY_KVA_SINGLE_UNIT &&
        candidate_statistics.reclaimed_level3_table_count == OS_KERNEL_MEMORY_KVA_EMPTY_VALUE &&
        candidate_statistics.retained_shared_level3_table_count ==
            OS_KERNEL_MEMORY_KVA_SINGLE_UNIT &&
        frames_after_warmup.free_frame_count + OS_KERNEL_MEMORY_KVA_SINGLE_UNIT ==
            frames_before_warmup.free_frame_count &&
        frames_after_warmup.reserved_frame_count == frames_before_warmup.reserved_frame_count;
    if (mapping_valid && mapping_released && frame_released && range_released && mapping_absent &&
        statistics_valid) {
        page_table_statistics = candidate_statistics;
        return true;
    }
    return false;
}

[[nodiscard]] bool RunKernelVirtualAddressSelfTest(
    KernelVirtualAddressSelfTestStatistics &self_test_statistics,
    PageTableReclamationSelfTestStatistics &page_table_statistics) noexcept {
    self_test_statistics = KernelVirtualAddressSelfTestStatistics{};
    page_table_statistics = PageTableReclamationSelfTestStatistics{};
    if (!WarmUpKernelVirtualAddressPageTables(page_table_statistics)) {
        return false;
    }

    KernelVirtualAddressAllocator &virtual_address_allocator = GetKernelVirtualAddressAllocator();
    PhysicalFrameAllocator &frame_allocator = FrameAllocator();
    const PhysicalFrameAllocatorStatistics frames_before_test = frame_allocator.Statistics();
    const PhysicalFrameBuddyStatistics buddy_before_test = frame_allocator.BuddyStatistics();
    const KernelVirtualAddressAllocatorStatistics virtual_addresses_before_test =
        virtual_address_allocator.Statistics();

    KernelVirtualAddressRange virtual_range{};
    if (virtual_address_allocator.TryAllocate(OS_KERNEL_MEMORY_KVA_SELF_TEST_RANGE_PAGE_COUNT,
                                              OS_KERNEL_MEMORY_KVA_SELF_TEST_ALIGNMENT_PAGE_COUNT,
                                              virtual_range) !=
        KernelVirtualAddressAllocatorStatus::Succeeded) {
        return false;
    }
    PhysicalFrameBlock physical_block{};
    if (frame_allocator.AllocateBlock(OS_KERNEL_MEMORY_KVA_SELF_TEST_BLOCK_ORDER, physical_block) !=
        PhysicalFrameAllocatorStatus::Succeeded) {
        static_cast<void>(virtual_address_allocator.TryRelease(virtual_range));
        return false;
    }

    const PagePermissions permissions{
        .writable = true,
        .executable = false,
        .user_accessible = false,
        .cache_disabled = false,
    };
    bool mappings_created = true;
    uint64_t mapped_page_count = OS_KERNEL_MEMORY_KVA_EMPTY_VALUE;
    for (uint64_t mapped_page_index = OS_KERNEL_MEMORY_KVA_EMPTY_VALUE;
         mapped_page_index < OS_KERNEL_MEMORY_KVA_SELF_TEST_MAPPED_PAGE_COUNT;
         ++mapped_page_index) {
        const uint64_t virtual_address = KernelVirtualPageAddress(
            virtual_range,
            OS_KERNEL_MEMORY_KVA_SELF_TEST_FIRST_MAPPED_PAGE_OFFSET + mapped_page_index);
        const uint64_t physical_address =
            physical_block.physical_address + mapped_page_index * OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
        if (GetPageTableManager().MapPage(virtual_address, physical_address, permissions) !=
            PageTableStatus::Succeeded) {
            mappings_created = false;
            break;
        }
        ++mapped_page_count;
    }

    bool mappings_valid =
        mappings_created && mapped_page_count == OS_KERNEL_MEMORY_KVA_SELF_TEST_MAPPED_PAGE_COUNT;
    PageMapping ignored_mapping{};
    mappings_valid =
        mappings_valid &&
        GetPageTableManager().QueryPage(virtual_range.begin_address, ignored_mapping) ==
            PageTableStatus::NotMapped &&
        GetPageTableManager().QueryPage(
            KernelVirtualPageAddress(virtual_range,
                                     OS_KERNEL_MEMORY_KVA_SELF_TEST_LAST_GUARD_PAGE_OFFSET),
            ignored_mapping) == PageTableStatus::NotMapped;
    for (uint64_t mapped_page_index = OS_KERNEL_MEMORY_KVA_EMPTY_VALUE;
         mappings_valid && mapped_page_index < OS_KERNEL_MEMORY_KVA_SELF_TEST_MAPPED_PAGE_COUNT;
         ++mapped_page_index) {
        PageMapping mapping{};
        mappings_valid =
            GetPageTableManager().QueryPage(
                KernelVirtualPageAddress(virtual_range,
                                         OS_KERNEL_MEMORY_KVA_SELF_TEST_FIRST_MAPPED_PAGE_OFFSET +
                                             mapped_page_index),
                mapping) == PageTableStatus::Succeeded &&
            mapping.physical_address == physical_block.physical_address +
                                            mapped_page_index * OS_KERNEL_MEMORY_PAGE_SIZE_BYTES &&
            mapping.page_size_bytes == OS_KERNEL_MEMORY_PAGE_SIZE_BYTES &&
            mapping.permissions.writable && !mapping.permissions.executable &&
            !mapping.permissions.user_accessible && !mapping.permissions.cache_disabled;
    }
    if (mappings_valid) {
        volatile uint64_t *const first_value =
            reinterpret_cast<volatile uint64_t *>(KernelVirtualPageAddress(
                virtual_range, OS_KERNEL_MEMORY_KVA_SELF_TEST_FIRST_MAPPED_PAGE_OFFSET));
        volatile uint64_t *const last_value =
            reinterpret_cast<volatile uint64_t *>(KernelVirtualPageAddress(
                virtual_range, OS_KERNEL_MEMORY_KVA_SELF_TEST_FIRST_MAPPED_PAGE_OFFSET +
                                   OS_KERNEL_MEMORY_KVA_SELF_TEST_MAPPED_PAGE_COUNT -
                                   OS_KERNEL_MEMORY_KVA_SINGLE_UNIT));
        *first_value = OS_KERNEL_MEMORY_KVA_SELF_TEST_FIRST_PATTERN;
        *last_value = OS_KERNEL_MEMORY_KVA_SELF_TEST_LAST_PATTERN;
        mappings_valid = *first_value == OS_KERNEL_MEMORY_KVA_SELF_TEST_FIRST_PATTERN &&
                         *last_value == OS_KERNEL_MEMORY_KVA_SELF_TEST_LAST_PATTERN;
    }

    bool cleanup_valid = true;
    for (uint64_t remaining_mapping_count = mapped_page_count;
         remaining_mapping_count > OS_KERNEL_MEMORY_KVA_EMPTY_VALUE; --remaining_mapping_count) {
        const uint64_t mapped_page_index =
            remaining_mapping_count - OS_KERNEL_MEMORY_KVA_SINGLE_UNIT;
        PageTableUnmapResult unmap_result{};
        const bool mapping_released =
            GetPageTableManager().UnmapPage(
                KernelVirtualPageAddress(virtual_range,
                                         OS_KERNEL_MEMORY_KVA_SELF_TEST_FIRST_MAPPED_PAGE_OFFSET +
                                             mapped_page_index),
                unmap_result) == PageTableStatus::Succeeded;
        page_table_statistics.reclaimed_level1_table_count +=
            unmap_result.reclaimed_level1_table_count;
        page_table_statistics.reclaimed_level2_table_count +=
            unmap_result.reclaimed_level2_table_count;
        page_table_statistics.reclaimed_level3_table_count +=
            unmap_result.reclaimed_level3_table_count;
        cleanup_valid = mapping_released && cleanup_valid;
    }
    const bool physical_block_released =
        frame_allocator.ReleaseBlock(physical_block) == PhysicalFrameAllocatorStatus::Succeeded;
    const bool virtual_range_released = virtual_address_allocator.TryRelease(virtual_range) ==
                                        KernelVirtualAddressAllocatorStatus::Succeeded;
    cleanup_valid = cleanup_valid && physical_block_released && virtual_range_released;

    const PhysicalFrameAllocatorStatistics frames_after_test = frame_allocator.Statistics();
    const PhysicalFrameBuddyStatistics buddy_after_test = frame_allocator.BuddyStatistics();
    const KernelVirtualAddressAllocatorStatistics virtual_addresses_after_test =
        virtual_address_allocator.Statistics();
    self_test_statistics = KernelVirtualAddressSelfTestStatistics{
        .virtual_address = virtual_range.begin_address,
        .physical_address = physical_block.physical_address,
        .mapped_page_count = mapped_page_count,
        .guard_page_count = OS_KERNEL_MEMORY_KVA_SELF_TEST_GUARD_PAGE_COUNT,
    };
    return mappings_valid && cleanup_valid &&
           frames_after_test.free_frame_count == frames_before_test.free_frame_count &&
           frames_after_test.allocated_frame_count == frames_before_test.allocated_frame_count &&
           frames_after_test.reserved_frame_count == frames_before_test.reserved_frame_count &&
           buddy_after_test.active_block_count == buddy_before_test.active_block_count &&
           buddy_after_test.successful_allocation_count ==
               buddy_before_test.successful_allocation_count +
                   OS_KERNEL_MEMORY_KVA_SELF_TEST_EXPECTED_BUDDY_OPERATION_COUNT &&
           buddy_after_test.release_count ==
               buddy_before_test.release_count +
                   OS_KERNEL_MEMORY_KVA_SELF_TEST_EXPECTED_BUDDY_OPERATION_COUNT &&
           page_table_statistics.reclaimed_level1_table_count ==
               OS_KERNEL_MEMORY_PAGE_TABLE_RECLAIM_SELF_TEST_EXPECTED_LEVEL1_TABLE_COUNT &&
           page_table_statistics.reclaimed_level2_table_count ==
               OS_KERNEL_MEMORY_PAGE_TABLE_RECLAIM_SELF_TEST_EXPECTED_LEVEL2_TABLE_COUNT &&
           page_table_statistics.reclaimed_level3_table_count ==
               OS_KERNEL_MEMORY_PAGE_TABLE_RECLAIM_SELF_TEST_EXPECTED_LEVEL3_TABLE_COUNT &&
           page_table_statistics.retained_shared_level3_table_count ==
               OS_KERNEL_MEMORY_PAGE_TABLE_RECLAIM_SELF_TEST_EXPECTED_RETAINED_LEVEL3_TABLE_COUNT &&
           virtual_addresses_after_test.allocated_page_count == OS_KERNEL_MEMORY_KVA_EMPTY_VALUE &&
           virtual_addresses_after_test.reserved_page_count ==
               OS_KERNEL_MEMORY_KVA_PERMANENT_GUARD_PAGE_COUNT &&
           virtual_addresses_after_test.active_allocation_count ==
               OS_KERNEL_MEMORY_KVA_EMPTY_VALUE &&
           virtual_addresses_after_test.active_descriptor_count ==
               virtual_addresses_before_test.active_descriptor_count &&
           virtual_addresses_after_test.successful_allocation_count ==
               virtual_addresses_before_test.successful_allocation_count +
                   OS_KERNEL_MEMORY_KVA_SINGLE_UNIT &&
           virtual_addresses_after_test.release_count ==
               virtual_addresses_before_test.release_count + OS_KERNEL_MEMORY_KVA_SINGLE_UNIT &&
           virtual_address_allocator.Validate() == KernelVirtualAddressAllocatorStatus::Succeeded &&
           frame_allocator.ValidateBuddy() == PhysicalFrameAllocatorStatus::Succeeded;
}

[[nodiscard]] bool RunHighMemorySelfTest(uint64_t &test_physical_address) noexcept {
    test_physical_address = 0ULL;
    if (current_managed_physical_address_limit <= OS_KERNEL_MEMORY_HIGH_FRAME_MINIMUM_ADDRESS) {
        return true;
    }
    PhysicalFrame frame{};
    if (FrameAllocator().AllocateInRange(OS_KERNEL_MEMORY_HIGH_FRAME_MINIMUM_ADDRESS,
                                         current_managed_physical_address_limit,
                                         frame) != PhysicalFrameAllocatorStatus::Succeeded) {
        return false;
    }
    const uint64_t direct_map_address = PhysicalMemoryDirectMapAddress(frame.physical_address);
    PageMapping mapping{};
    if (direct_map_address == 0ULL ||
        GetPageTableManager().QueryPage(direct_map_address, mapping) !=
            PageTableStatus::Succeeded ||
        mapping.physical_address != frame.physical_address || !mapping.permissions.writable ||
        mapping.permissions.executable || mapping.permissions.user_accessible ||
        mapping.permissions.cache_disabled) {
        static_cast<void>(FrameAllocator().Release(frame));
        return false;
    }
    volatile uint64_t *const values = reinterpret_cast<volatile uint64_t *>(direct_map_address);
    values[OS_KERNEL_MEMORY_HIGH_FRAME_SELF_TEST_FIRST_VALUE_INDEX] =
        OS_KERNEL_MEMORY_HIGH_FRAME_SELF_TEST_FIRST_PATTERN;
    values[OS_KERNEL_MEMORY_HIGH_FRAME_SELF_TEST_SECOND_VALUE_INDEX] =
        OS_KERNEL_MEMORY_HIGH_FRAME_SELF_TEST_SECOND_PATTERN;
    const bool patterns_valid = values[OS_KERNEL_MEMORY_HIGH_FRAME_SELF_TEST_FIRST_VALUE_INDEX] ==
                                    OS_KERNEL_MEMORY_HIGH_FRAME_SELF_TEST_FIRST_PATTERN &&
                                values[OS_KERNEL_MEMORY_HIGH_FRAME_SELF_TEST_SECOND_VALUE_INDEX] ==
                                    OS_KERNEL_MEMORY_HIGH_FRAME_SELF_TEST_SECOND_PATTERN;
    test_physical_address = frame.physical_address;
    const PhysicalFrameAllocatorStatus release_status = FrameAllocator().Release(frame);
    return patterns_valid && release_status == PhysicalFrameAllocatorStatus::Succeeded;
}

[[nodiscard]] bool RunBuddySelfTest(uint64_t &test_physical_address) noexcept {
    test_physical_address = 0ULL;
    PhysicalFrameAllocator &allocator = FrameAllocator();
    const PhysicalFrameAllocatorStatistics frames_before_test = allocator.Statistics();
    const PhysicalFrameBuddyStatistics buddy_before_test = allocator.BuddyStatistics();
    const uint64_t minimum_address =
        current_managed_physical_address_limit > OS_KERNEL_MEMORY_HIGH_FRAME_MINIMUM_ADDRESS +
                                                     OS_KERNEL_MEMORY_BUDDY_SELF_TEST_SIZE_BYTES
            ? OS_KERNEL_MEMORY_HIGH_FRAME_MINIMUM_ADDRESS
            : 0ULL;
    PhysicalFrameBlock block{};
    if (allocator.AllocateBlockInRange(OS_KERNEL_MEMORY_BUDDY_SELF_TEST_ORDER, minimum_address,
                                       current_managed_physical_address_limit,
                                       block) != PhysicalFrameAllocatorStatus::Succeeded) {
        return false;
    }

    const uint64_t last_frame_physical_address = block.physical_address +
                                                 OS_KERNEL_MEMORY_BUDDY_SELF_TEST_SIZE_BYTES -
                                                 OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
    const uint64_t first_direct_map_address =
        PhysicalMemoryDirectMapAddress(block.physical_address);
    const uint64_t last_direct_map_address =
        PhysicalMemoryDirectMapAddress(last_frame_physical_address);
    PageMapping first_mapping{};
    PageMapping last_mapping{};
    if (first_direct_map_address == 0ULL || last_direct_map_address == 0ULL ||
        GetPageTableManager().QueryPage(first_direct_map_address, first_mapping) !=
            PageTableStatus::Succeeded ||
        GetPageTableManager().QueryPage(last_direct_map_address, last_mapping) !=
            PageTableStatus::Succeeded ||
        first_mapping.physical_address != block.physical_address ||
        last_mapping.physical_address != last_frame_physical_address ||
        !first_mapping.permissions.writable || first_mapping.permissions.executable ||
        !last_mapping.permissions.writable || last_mapping.permissions.executable) {
        static_cast<void>(allocator.ReleaseBlock(block));
        return false;
    }

    volatile uint64_t *const first_value =
        reinterpret_cast<volatile uint64_t *>(first_direct_map_address);
    volatile uint64_t *const last_value =
        reinterpret_cast<volatile uint64_t *>(last_direct_map_address);
    *first_value = OS_KERNEL_MEMORY_BUDDY_SELF_TEST_FIRST_PATTERN;
    *last_value = OS_KERNEL_MEMORY_BUDDY_SELF_TEST_LAST_PATTERN;
    const bool patterns_valid = *first_value == OS_KERNEL_MEMORY_BUDDY_SELF_TEST_FIRST_PATTERN &&
                                *last_value == OS_KERNEL_MEMORY_BUDDY_SELF_TEST_LAST_PATTERN;
    test_physical_address = block.physical_address;
    if (allocator.ReleaseBlock(block) != PhysicalFrameAllocatorStatus::Succeeded) {
        return false;
    }
    const PhysicalFrameAllocatorStatistics frames_after_test = allocator.Statistics();
    const PhysicalFrameBuddyStatistics buddy_after_test = allocator.BuddyStatistics();
    return patterns_valid &&
           frames_after_test.free_frame_count == frames_before_test.free_frame_count &&
           frames_after_test.allocated_frame_count == frames_before_test.allocated_frame_count &&
           frames_after_test.reserved_frame_count == frames_before_test.reserved_frame_count &&
           buddy_after_test.active_block_count == buddy_before_test.active_block_count &&
           buddy_after_test.successful_allocation_count ==
               buddy_before_test.successful_allocation_count + 1ULL &&
           buddy_after_test.release_count == buddy_before_test.release_count + 1ULL &&
           allocator.ValidateBuddy() == PhysicalFrameAllocatorStatus::Succeeded;
}

[[nodiscard]] bool DestroyKernelStack(void *const raw_context) noexcept {
    if (raw_context == nullptr) {
        return false;
    }
    KernelStackRollbackContext *const context =
        static_cast<KernelStackRollbackContext *>(raw_context);
    return context->manager != nullptr &&
           context->manager->TryDestroy(context->slot_index) ==
               KernelStackManagerStatus::Succeeded;
}

[[nodiscard]] ResourceLifecycleSelfTestStatus RunResourceLifecycleSelfTest(
    ResourceLifecycleSelfTestStatistics &self_test_statistics) noexcept {
    self_test_statistics = ResourceLifecycleSelfTestStatistics{
        .tracked_field_count = OS_KERNEL_RESOURCE_SNAPSHOT_TRACKED_FIELD_COUNT,
        .changed_fields_mask = OS_KERNEL_RESOURCE_SNAPSHOT_ALL_FIELDS_MASK,
        .reference_counter_passed =
            OS_KERNEL_MEMORY_RESOURCE_SELF_TEST_EMPTY_VALUE,
        .scope_rollback_passed =
            OS_KERNEL_MEMORY_RESOURCE_SELF_TEST_EMPTY_VALUE,
        .resource_snapshot_passed =
            OS_KERNEL_MEMORY_RESOURCE_SELF_TEST_EMPTY_VALUE,
    };

    ResourceSnapshot before{};
    if (GetKernelResourceSnapshot(before) != ResourceSnapshotStatus::Succeeded) {
        return ResourceLifecycleSelfTestStatus::BaselineSnapshotFailed;
    }

    os::foundation::ScopeRollbackAction
        rollback_actions[OS_KERNEL_MEMORY_RESOURCE_SELF_TEST_ACTION_CAPACITY]{};
    os::foundation::ScopeRollback rollback{};
    if (rollback.Initialize(
            rollback_actions, OS_KERNEL_MEMORY_RESOURCE_SELF_TEST_ACTION_CAPACITY) !=
        os::foundation::ScopeRollbackStatus::Succeeded) {
        return ResourceLifecycleSelfTestStatus::RollbackInitializationFailed;
    }
    if (GetKernelStackManager().TryCreate(
            OS_KERNEL_MEMORY_RESOURCE_SELF_TEST_LAST_STACK_SLOT) !=
        KernelStackManagerStatus::Succeeded) {
        return ResourceLifecycleSelfTestStatus::KernelStackCreationFailed;
    }
    KernelStackRollbackContext stack_context{
        .manager = &GetKernelStackManager(),
        .slot_index = OS_KERNEL_MEMORY_RESOURCE_SELF_TEST_LAST_STACK_SLOT,
    };
    if (rollback.TryPush(DestroyKernelStack, &stack_context) !=
        os::foundation::ScopeRollbackStatus::Succeeded) {
        static_cast<void>(GetKernelStackManager().TryDestroy(
            OS_KERNEL_MEMORY_RESOURCE_SELF_TEST_LAST_STACK_SLOT));
        static_cast<void>(rollback.Commit());
        return ResourceLifecycleSelfTestStatus::RollbackRegistrationFailed;
    }

    os::foundation::ReferenceCounter reference_counter{};
    bool released_last_reference = true;
    bool unavailable_release_output = false;
    os::foundation::ReferenceCounter overflow_counter{};
    const bool reference_counter_valid =
        reference_counter.Start(
            OS_KERNEL_MEMORY_RESOURCE_SELF_TEST_INITIAL_REFERENCE_COUNT) ==
            os::foundation::ReferenceCounterStatus::Succeeded &&
        reference_counter.Start(
            OS_KERNEL_MEMORY_RESOURCE_SELF_TEST_INITIAL_REFERENCE_COUNT) ==
            os::foundation::ReferenceCounterStatus::ActiveReferencesRemain &&
        reference_counter.TryAcquire() ==
            os::foundation::ReferenceCounterStatus::Succeeded &&
        reference_counter.TryRelease(released_last_reference) ==
            os::foundation::ReferenceCounterStatus::Succeeded &&
        !released_last_reference &&
        reference_counter.TryRelease(released_last_reference) ==
            os::foundation::ReferenceCounterStatus::Succeeded &&
        released_last_reference && !reference_counter.IsActive() &&
        reference_counter.TryAcquire() ==
            os::foundation::ReferenceCounterStatus::ReferenceUnavailable &&
        reference_counter.TryRelease(unavailable_release_output) ==
            os::foundation::ReferenceCounterStatus::ReferenceUnavailable &&
        !unavailable_release_output &&
        overflow_counter.Start(UINT64_MAX) ==
            os::foundation::ReferenceCounterStatus::Succeeded &&
        overflow_counter.TryAcquire() ==
            os::foundation::ReferenceCounterStatus::CounterOverflow;
    self_test_statistics.reference_counter_passed =
        reference_counter_valid
            ? OS_KERNEL_MEMORY_RESOURCE_SELF_TEST_PASSED_VALUE
            : OS_KERNEL_MEMORY_RESOURCE_SELF_TEST_EMPTY_VALUE;

    const bool scope_rollback_valid =
        rollback.TryRollback() == os::foundation::ScopeRollbackStatus::Succeeded &&
        rollback.State() == os::foundation::ScopeRollbackState::RolledBack &&
        rollback.FailureCount() ==
            OS_KERNEL_MEMORY_RESOURCE_SELF_TEST_EMPTY_VALUE &&
        GetKernelStackManager().Validate() ==
            KernelStackManagerStatus::Succeeded;
    self_test_statistics.scope_rollback_passed =
        scope_rollback_valid ? OS_KERNEL_MEMORY_RESOURCE_SELF_TEST_PASSED_VALUE
                             : OS_KERNEL_MEMORY_RESOURCE_SELF_TEST_EMPTY_VALUE;

    ResourceSnapshot after{};
    ResourceSnapshotDifference difference{
        .changed_fields_mask = OS_KERNEL_RESOURCE_SNAPSHOT_ALL_FIELDS_MASK,
        .changed_field_count = OS_KERNEL_RESOURCE_SNAPSHOT_TRACKED_FIELD_COUNT,
    };
    const bool resource_snapshot_valid =
        GetKernelResourceSnapshot(after) == ResourceSnapshotStatus::Succeeded &&
        CompareResourceSnapshots(before, after, difference) ==
            ResourceSnapshotStatus::Succeeded &&
        difference.changed_fields_mask ==
            OS_KERNEL_MEMORY_RESOURCE_SELF_TEST_EMPTY_VALUE &&
        difference.changed_field_count ==
            OS_KERNEL_MEMORY_RESOURCE_SELF_TEST_EMPTY_VALUE;
    self_test_statistics.changed_fields_mask = difference.changed_fields_mask;
    self_test_statistics.resource_snapshot_passed =
        resource_snapshot_valid ? OS_KERNEL_MEMORY_RESOURCE_SELF_TEST_PASSED_VALUE
                                : OS_KERNEL_MEMORY_RESOURCE_SELF_TEST_EMPTY_VALUE;
    if (!reference_counter_valid) {
        return ResourceLifecycleSelfTestStatus::ReferenceCounterFailed;
    }
    if (!scope_rollback_valid) {
        return ResourceLifecycleSelfTestStatus::ScopeRollbackFailed;
    }
    if (!resource_snapshot_valid) {
        return ResourceLifecycleSelfTestStatus::FinalSnapshotFailed;
    }
    return ResourceLifecycleSelfTestStatus::Succeeded;
}
}

KernelMemoryInitializationStatus InitializeKernelMemory(const BootInfo &boot_info) noexcept {
    const PhysicalMemoryMapEntry *const memory_map =
        reinterpret_cast<const PhysicalMemoryMapEntry *>(boot_info.physical_memory_map_address);
    const uint64_t physical_address_width_bits = ProcessorPhysicalAddressWidthBits();
    const uint64_t virtual_address_width_bits = ProcessorVirtualAddressWidthBits();
    const uint64_t five_level_paging_supported = ProcessorSupportsFiveLevelPaging() ? 1ULL : 0ULL;
    const uint64_t processor_physical_address_limit = ProcessorMaximumPhysicalAddressExclusive();
    if (physical_address_width_bits == 0ULL || virtual_address_width_bits == 0ULL ||
        processor_physical_address_limit == 0ULL) {
        return KernelMemoryInitializationStatus::InvalidProcessorAddressWidth;
    }
    current_page_table_physical_address_limit =
        Minimum(processor_physical_address_limit, OS_KERNEL_MEMORY_DIRECT_MAP_CAPACITY_BYTES);

    PhysicalMemorySummary memory_summary{};
    if (ValidateAndSummarizePhysicalMemoryMap(memory_map, boot_info.physical_memory_map_entry_count,
                                              current_page_table_physical_address_limit,
                                              memory_summary) !=
        PhysicalMemoryMapValidationStatus::Succeeded) {
        return KernelMemoryInitializationStatus::InvalidMemoryMap;
    }
    current_managed_physical_address_limit =
        AlignDownToPage(Minimum(memory_summary.highest_usable_address_exclusive,
                                current_page_table_physical_address_limit));
    if (current_managed_physical_address_limit == 0ULL ||
        current_managed_physical_address_limit < boot_info.identity_mapped_size_bytes) {
        return KernelMemoryInitializationStatus::InvalidManagedPhysicalAddressLimit;
    }
    if (ValidateAndSummarizePhysicalMemoryMap(memory_map, boot_info.physical_memory_map_entry_count,
                                              current_managed_physical_address_limit,
                                              memory_summary) !=
        PhysicalMemoryMapValidationStatus::Succeeded) {
        return KernelMemoryInitializationStatus::InvalidMemoryMap;
    }

    const uint64_t required_frame_state_storage_size_bytes =
        CalculatePhysicalFrameStateStorageSizeBytes(current_managed_physical_address_limit);
    const uint64_t frame_state_storage_size_bytes =
        AlignUpToPage(required_frame_state_storage_size_bytes);
    const uint64_t required_buddy_storage_size_bytes =
        CalculatePhysicalFrameBuddyStorageSizeBytes(current_managed_physical_address_limit);
    const uint64_t buddy_storage_size_bytes = AlignUpToPage(required_buddy_storage_size_bytes);
    if (required_frame_state_storage_size_bytes == 0ULL || frame_state_storage_size_bytes == 0ULL ||
        required_buddy_storage_size_bytes == 0ULL || buddy_storage_size_bytes == 0ULL ||
        frame_state_storage_size_bytes > UINT64_MAX - buddy_storage_size_bytes) {
        return KernelMemoryInitializationStatus::FrameStateStorageUnavailable;
    }
    const uint64_t frame_allocator_metadata_size_bytes =
        frame_state_storage_size_bytes + buddy_storage_size_bytes;
    PhysicalMemoryRange frame_allocator_metadata_range{};
    if (!FindFrameAllocatorMetadataRange(memory_map, boot_info.physical_memory_map_entry_count,
                                         boot_info, frame_allocator_metadata_size_bytes,
                                         frame_allocator_metadata_range)) {
        return KernelMemoryInitializationStatus::FrameStateStorageUnavailable;
    }
    const PhysicalMemoryRange frame_state_storage_range{
        .begin_address = frame_allocator_metadata_range.begin_address,
        .length_bytes = frame_state_storage_size_bytes,
    };
    const PhysicalMemoryRange buddy_storage_range{
        .begin_address =
            frame_allocator_metadata_range.begin_address + frame_state_storage_size_bytes,
        .length_bytes = buddy_storage_size_bytes,
    };
    if (FrameAllocator().ConfigureStateStorage(
            reinterpret_cast<uint8_t *>(frame_state_storage_range.begin_address),
            frame_state_storage_range.length_bytes) != PhysicalFrameAllocatorStatus::Succeeded) {
        return KernelMemoryInitializationStatus::FrameAllocatorConfigurationFailed;
    }
    if (FrameAllocator().ConfigureBuddyStorage(
            reinterpret_cast<uint8_t *>(buddy_storage_range.begin_address),
            buddy_storage_range.length_bytes) != PhysicalFrameAllocatorStatus::Succeeded) {
        return KernelMemoryInitializationStatus::BuddyAllocatorConfigurationFailed;
    }
    if (FrameAllocator().Initialize(memory_map, boot_info.physical_memory_map_entry_count,
                                    current_managed_physical_address_limit) !=
        PhysicalFrameAllocatorStatus::Succeeded) {
        return KernelMemoryInitializationStatus::FrameAllocatorInitializationFailed;
    }
    if (!ReserveBootCriticalRanges(boot_info) ||
        !ReserveFrameAllocatorMetadata(frame_allocator_metadata_range)) {
        return KernelMemoryInitializationStatus::ReservationFailed;
    }
    if (FrameAllocator().InitializeBuddy() != PhysicalFrameAllocatorStatus::Succeeded) {
        return KernelMemoryInitializationStatus::BuddyAllocatorInitializationFailed;
    }
    if (GetPageTableManager().SetMemoryAccess(PageTableMemoryAccess{
            .maximum_physical_address_exclusive = current_page_table_physical_address_limit,
            .physical_memory_virtual_base = 0ULL,
            .allocation_maximum_physical_address_exclusive = boot_info.identity_mapped_size_bytes,
            .invalidate_active_mappings = false,
        }) != PageTableStatus::Succeeded) {
        return KernelMemoryInitializationStatus::PageTableMemoryAccessFailed;
    }
    if (GetPageTableManager().Initialize() != PageTableStatus::Succeeded) {
        return KernelMemoryInitializationStatus::PageTableInitializationFailed;
    }
    if (!MapIdentityRange(boot_info)) {
        return KernelMemoryInitializationStatus::IdentityMappingFailed;
    }
    DirectMapStatistics direct_map_statistics{};
    if (!MapDirectMemory(memory_map, boot_info.physical_memory_map_entry_count,
                         direct_map_statistics)) {
        return KernelMemoryInitializationStatus::DirectMapMappingFailed;
    }
    if (!MapLocalApicRegisters()) {
        return KernelMemoryInitializationStatus::LocalApicMappingFailed;
    }
    if (!MapKernelHeap()) {
        return KernelMemoryInitializationStatus::HeapMappingFailed;
    }
    if (!MapWriteProtectionTestPage()) {
        return KernelMemoryInitializationStatus::ProtectionTestMappingFailed;
    }
    if (!EnableKernelMemoryProtection()) {
        return KernelMemoryInitializationStatus::MemoryProtectionUnsupported;
    }
    ActivatePageTable(GetPageTableManager().RootPhysicalAddress());
    if (ReadPageTableRoot() != GetPageTableManager().RootPhysicalAddress() ||
        !KernelMemoryProtectionEnabled()) {
        return KernelMemoryInitializationStatus::PageTableActivationFailed;
    }
    if (GetPageTableManager().SetMemoryAccess(ActivePageTableMemoryAccess()) !=
        PageTableStatus::Succeeded) {
        return KernelMemoryInitializationStatus::PageTableMemoryAccessFailed;
    }
    direct_map_active = true;
    if (!ValidateKernelMappings(boot_info, frame_allocator_metadata_range)) {
        return KernelMemoryInitializationStatus::PermissionValidationFailed;
    }
    if (GetKernelHeap().Initialize(OS_KERNEL_MEMORY_HEAP_VIRTUAL_BASE,
                                   OS_KERNEL_MEMORY_HEAP_SIZE_BYTES) !=
        KernelHeapStatus::Succeeded) {
        return KernelMemoryInitializationStatus::HeapInitializationFailed;
    }
    if (!RunHeapSelfTest()) {
        return KernelMemoryInitializationStatus::HeapSelfTestFailed;
    }
    KernelTypeCacheStatistics type_cache_statistics{};
    if (!RunTypeCacheSelfTest(type_cache_statistics)) {
        return KernelMemoryInitializationStatus::TypeCacheSelfTestFailed;
    }
    uint64_t high_memory_test_physical_address = 0ULL;
    if (!RunHighMemorySelfTest(high_memory_test_physical_address)) {
        return KernelMemoryInitializationStatus::HighMemorySelfTestFailed;
    }
    uint64_t buddy_self_test_physical_address = 0ULL;
    if (!RunBuddySelfTest(buddy_self_test_physical_address)) {
        return KernelMemoryInitializationStatus::BuddySelfTestFailed;
    }
    if (GetKernelVirtualAddressAllocator().Initialize(
            OS_KERNEL_MEMORY_KVA_VIRTUAL_BASE, OS_KERNEL_MEMORY_KVA_PAGE_COUNT,
            kernel_virtual_address_descriptors, OS_KERNEL_MEMORY_KVA_DESCRIPTOR_CAPACITY) !=
            KernelVirtualAddressAllocatorStatus::Succeeded ||
        GetKernelVirtualAddressAllocator().ReserveRange(
            OS_KERNEL_MEMORY_KVA_VIRTUAL_BASE, OS_KERNEL_MEMORY_KVA_PERMANENT_GUARD_PAGE_COUNT) !=
            KernelVirtualAddressAllocatorStatus::Succeeded) {
        return KernelMemoryInitializationStatus::KvaInitializationFailed;
    }
    KernelVirtualAddressSelfTestStatistics kva_self_test_statistics{};
    PageTableReclamationSelfTestStatistics page_table_self_test_statistics{};
    if (!RunKernelVirtualAddressSelfTest(kva_self_test_statistics,
                                         page_table_self_test_statistics)) {
        return KernelMemoryInitializationStatus::KvaSelfTestFailed;
    }
    if (GetKernelStackManager().Initialize(kernel_stack_storage,
                                           OS_KERNEL_MEMORY_KERNEL_STACK_SLOT_CAPACITY) !=
        KernelStackManagerStatus::Succeeded) {
        return KernelMemoryInitializationStatus::KernelStackManagerInitializationFailed;
    }
    ResourceLifecycleSelfTestStatistics resource_lifecycle_statistics{};
    const ResourceLifecycleSelfTestStatus resource_lifecycle_status =
        RunResourceLifecycleSelfTest(resource_lifecycle_statistics);
    switch (resource_lifecycle_status) {
    case ResourceLifecycleSelfTestStatus::Succeeded:
        break;
    case ResourceLifecycleSelfTestStatus::BaselineSnapshotFailed:
        return KernelMemoryInitializationStatus::ResourceLifecycleBaselineSnapshotFailed;
    case ResourceLifecycleSelfTestStatus::RollbackInitializationFailed:
        return KernelMemoryInitializationStatus::ResourceLifecycleRollbackInitializationFailed;
    case ResourceLifecycleSelfTestStatus::KernelStackCreationFailed:
        return KernelMemoryInitializationStatus::ResourceLifecycleStackCreationFailed;
    case ResourceLifecycleSelfTestStatus::RollbackRegistrationFailed:
        return KernelMemoryInitializationStatus::ResourceLifecycleRollbackRegistrationFailed;
    case ResourceLifecycleSelfTestStatus::ReferenceCounterFailed:
        return KernelMemoryInitializationStatus::ReferenceCounterSelfTestFailed;
    case ResourceLifecycleSelfTestStatus::ScopeRollbackFailed:
        return KernelMemoryInitializationStatus::ScopeRollbackSelfTestFailed;
    case ResourceLifecycleSelfTestStatus::FinalSnapshotFailed:
        return KernelMemoryInitializationStatus::ResourceSnapshotSelfTestFailed;
    }

    const PhysicalFrameAllocatorStatistics frame_statistics = FrameAllocator().Statistics();
    const PhysicalFrameBuddyStatistics buddy_statistics = FrameAllocator().BuddyStatistics();
    const uint64_t managed_usable_memory_bytes =
        (frame_statistics.free_frame_count + frame_statistics.allocated_frame_count +
         frame_statistics.reserved_frame_count) *
        OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
    const KernelHeapStatistics heap_statistics = GetKernelHeap().Statistics();
    const KernelVirtualAddressAllocatorStatistics kva_statistics =
        GetKernelVirtualAddressAllocator().Statistics();
    current_kernel_memory_statistics = KernelMemoryStatistics{
        .memory_map_entry_count = boot_info.physical_memory_map_entry_count,
        .described_address_bytes = memory_summary.total_bytes,
        .reported_usable_memory_bytes = memory_summary.usable_bytes,
        .managed_usable_memory_bytes = managed_usable_memory_bytes,
        .managed_physical_address_limit = current_managed_physical_address_limit,
        .physical_address_width_bits = physical_address_width_bits,
        .virtual_address_width_bits = virtual_address_width_bits,
        .five_level_paging_supported = five_level_paging_supported,
        .frame_state_storage_physical_address = frame_state_storage_range.begin_address,
        .frame_state_storage_size_bytes = frame_state_storage_range.length_bytes,
        .buddy_storage_physical_address = buddy_storage_range.begin_address,
        .buddy_storage_size_bytes = buddy_storage_range.length_bytes,
        .buddy_maximum_order = buddy_statistics.maximum_order,
        .buddy_free_block_count = buddy_statistics.free_block_count,
        .buddy_active_block_count = buddy_statistics.active_block_count,
        .buddy_successful_allocation_count = buddy_statistics.successful_allocation_count,
        .buddy_release_count = buddy_statistics.release_count,
        .buddy_split_count = buddy_statistics.split_count,
        .buddy_merge_count = buddy_statistics.merge_count,
        .buddy_largest_free_order = buddy_statistics.largest_free_order,
        .buddy_self_test_physical_address = buddy_self_test_physical_address,
        .buddy_self_test_order = OS_KERNEL_MEMORY_BUDDY_SELF_TEST_ORDER,
        .direct_map_mapped_bytes = direct_map_statistics.mapped_bytes,
        .direct_map_large_page_count = direct_map_statistics.large_page_count,
        .direct_map_small_page_count = direct_map_statistics.small_page_count,
        .high_memory_test_physical_address = high_memory_test_physical_address,
        .free_frame_count = frame_statistics.free_frame_count,
        .allocated_frame_count = frame_statistics.allocated_frame_count,
        .reserved_frame_count = frame_statistics.reserved_frame_count,
        .page_table_root_physical_address = GetPageTableManager().RootPhysicalAddress(),
        .page_table_reclaimed_level1_table_count =
            page_table_self_test_statistics.reclaimed_level1_table_count,
        .page_table_reclaimed_level2_table_count =
            page_table_self_test_statistics.reclaimed_level2_table_count,
        .page_table_reclaimed_level3_table_count =
            page_table_self_test_statistics.reclaimed_level3_table_count,
        .page_table_retained_shared_level3_table_count =
            page_table_self_test_statistics.retained_shared_level3_table_count,
        .heap_capacity_bytes = heap_statistics.capacity_bytes,
        .heap_consumed_bytes = heap_statistics.consumed_bytes,
        .heap_active_allocation_count = heap_statistics.allocation_count,
        .heap_successful_allocation_count = heap_statistics.successful_allocation_count,
        .heap_release_count = heap_statistics.release_count,
        .heap_peak_consumed_bytes = heap_statistics.peak_consumed_bytes,
        .heap_largest_free_allocation_bytes = heap_statistics.largest_free_allocation_bytes,
        .type_cache_object_size_bytes = type_cache_statistics.object_size_bytes,
        .type_cache_object_alignment_bytes = type_cache_statistics.object_alignment_bytes,
        .type_cache_slot_stride_bytes = type_cache_statistics.slot_stride_bytes,
        .type_cache_capacity = type_cache_statistics.capacity,
        .type_cache_backing_storage_size_bytes = type_cache_statistics.backing_storage_size_bytes,
        .type_cache_active_object_count = type_cache_statistics.active_object_count,
        .type_cache_free_object_count = type_cache_statistics.free_object_count,
        .type_cache_successful_allocation_count = type_cache_statistics.successful_allocation_count,
        .type_cache_release_count = type_cache_statistics.release_count,
        .type_cache_peak_active_object_count = type_cache_statistics.peak_active_object_count,
        .kva_window_begin_address = kva_statistics.window_begin_address,
        .kva_window_size_bytes = kva_statistics.window_size_bytes,
        .kva_descriptor_capacity = kva_statistics.descriptor_capacity,
        .kva_active_descriptor_count = kva_statistics.active_descriptor_count,
        .kva_free_page_count = kva_statistics.free_page_count,
        .kva_allocated_page_count = kva_statistics.allocated_page_count,
        .kva_reserved_page_count = kva_statistics.reserved_page_count,
        .kva_successful_allocation_count = kva_statistics.successful_allocation_count,
        .kva_release_count = kva_statistics.release_count,
        .kva_peak_allocated_page_count = kva_statistics.peak_allocated_page_count,
        .kva_largest_free_range_page_count = kva_statistics.largest_free_range_page_count,
        .kva_self_test_virtual_address = kva_self_test_statistics.virtual_address,
        .kva_self_test_physical_address = kva_self_test_statistics.physical_address,
        .kva_self_test_mapped_page_count = kva_self_test_statistics.mapped_page_count,
        .kva_self_test_guard_page_count = kva_self_test_statistics.guard_page_count,
        .resource_snapshot_tracked_field_count =
            resource_lifecycle_statistics.tracked_field_count,
        .resource_snapshot_changed_fields_mask =
            resource_lifecycle_statistics.changed_fields_mask,
        .reference_counter_self_test_passed =
            resource_lifecycle_statistics.reference_counter_passed,
        .scope_rollback_self_test_passed =
            resource_lifecycle_statistics.scope_rollback_passed,
        .resource_snapshot_self_test_passed =
            resource_lifecycle_statistics.resource_snapshot_passed,
    };
    return KernelMemoryInitializationStatus::Succeeded;
}

const KernelMemoryStatistics &GetKernelMemoryStatistics() noexcept {
    return current_kernel_memory_statistics;
}

PhysicalFrameAllocatorStatistics GetPhysicalFrameAllocatorStatistics() noexcept {
    return FrameAllocator().Statistics();
}

uint64_t PhysicalMemoryDirectMapAddress(const uint64_t physical_address) noexcept {
    if (!direct_map_active || physical_address >= current_managed_physical_address_limit ||
        physical_address > UINT64_MAX - OS_KERNEL_MEMORY_DIRECT_MAP_VIRTUAL_BASE) {
        return 0ULL;
    }
    return OS_KERNEL_MEMORY_DIRECT_MAP_VIRTUAL_BASE + physical_address;
}

KernelHeap &GetKernelHeap() noexcept {
    static KernelHeap heap{};
    return heap;
}

KernelVirtualAddressAllocator &GetKernelVirtualAddressAllocator() noexcept {
    static KernelVirtualAddressAllocator allocator{};
    return allocator;
}

KernelStackManager &GetKernelStackManager() noexcept {
    static KernelStackManager manager{
        FrameAllocator(),
        GetKernelVirtualAddressAllocator(),
        GetPageTableManager(),
        KernelStackMemoryAccess{
            .physical_memory_virtual_base = OS_KERNEL_MEMORY_DIRECT_MAP_VIRTUAL_BASE,
            .maximum_physical_address_exclusive = current_managed_physical_address_limit,
        },
    };
    return manager;
}

ResourceSnapshotStatus
GetKernelResourceSnapshot(ResourceSnapshot &snapshot) noexcept {
    return GetKernelResourceSnapshot(ResourceSnapshotSupplementalCounts{},
                                     snapshot);
}

ResourceSnapshotStatus GetKernelResourceSnapshot(
    const ResourceSnapshotSupplementalCounts &supplemental_counts,
    ResourceSnapshot &snapshot) noexcept {
    return CreateResourceSnapshot(
        FrameAllocator().Statistics(), FrameAllocator().BuddyStatistics(),
        GetKernelHeap().Statistics(),
        GetKernelVirtualAddressAllocator().Statistics(),
        GetKernelStackManager().Statistics(), supplemental_counts, snapshot);
}

KernelUserPageStatus CreateUserPageTable(uint64_t &root_physical_address) noexcept {
    PageTableManager process_page_table{FrameAllocator(), ActivePageTableMemoryAccess(),
                                        PageTableRootKind::Process};
    if (process_page_table.InitializeProcessRoot(GetPageTableManager().RootPhysicalAddress()) !=
        PageTableStatus::Succeeded) {
        return KernelUserPageStatus::PageTableCreationFailed;
    }
    root_physical_address = process_page_table.RootPhysicalAddress();
    return KernelUserPageStatus::Succeeded;
}

KernelUserPageStatus DestroyUserPageTable(const uint64_t root_physical_address) noexcept {
    if (root_physical_address == 0ULL ||
        (root_physical_address & OS_KERNEL_MEMORY_PAGE_MASK) != 0ULL ||
        root_physical_address == GetPageTableManager().RootPhysicalAddress() ||
        root_physical_address == ReadPageTableRoot()) {
        return KernelUserPageStatus::InvalidPageTableRoot;
    }
    PageTableManager process_page_table{FrameAllocator(), root_physical_address,
                                        ActivePageTableMemoryAccess(), PageTableRootKind::Process};
    return process_page_table.ReleaseProcessRoot() == PageTableStatus::Succeeded
               ? KernelUserPageStatus::Succeeded
               : KernelUserPageStatus::PageTableDestructionFailed;
}

KernelUserPageStatus AllocateAndMapUserPage(const uint64_t root_physical_address,
                                            const uint64_t virtual_address, const bool writable,
                                            const bool executable,
                                            uint64_t &physical_address) noexcept {
    if (root_physical_address == 0ULL ||
        (root_physical_address & OS_KERNEL_MEMORY_PAGE_MASK) != 0ULL) {
        return KernelUserPageStatus::InvalidPageTableRoot;
    }
    if ((virtual_address & OS_KERNEL_MEMORY_PAGE_MASK) != 0ULL ||
        !IsUserVirtualAddressRange(virtual_address, OS_KERNEL_MEMORY_PAGE_SIZE_BYTES)) {
        return KernelUserPageStatus::InvalidVirtualAddress;
    }
    if (writable && executable) {
        return KernelUserPageStatus::InvalidPermissions;
    }
    PhysicalFrame frame{};
    if (FrameAllocator().Allocate(frame) != PhysicalFrameAllocatorStatus::Succeeded) {
        return KernelUserPageStatus::FrameAllocationFailed;
    }
    const PagePermissions permissions{
        .writable = writable,
        .executable = executable,
        .user_accessible = true,
        .cache_disabled = false,
    };
    PageTableManager process_page_table{FrameAllocator(), root_physical_address,
                                        ActivePageTableMemoryAccess(), PageTableRootKind::Process};
    if (process_page_table.MapPage(virtual_address, frame.physical_address, permissions) !=
        PageTableStatus::Succeeded) {
        static_cast<void>(FrameAllocator().Release(frame));
        return KernelUserPageStatus::PageMappingFailed;
    }
    physical_address = frame.physical_address;
    return KernelUserPageStatus::Succeeded;
}

KernelUserPageStatus ReleaseUserPage(const uint64_t root_physical_address,
                                     const uint64_t virtual_address) noexcept {
    if (root_physical_address == 0ULL ||
        (root_physical_address & OS_KERNEL_MEMORY_PAGE_MASK) != 0ULL) {
        return KernelUserPageStatus::InvalidPageTableRoot;
    }
    if ((virtual_address & OS_KERNEL_MEMORY_PAGE_MASK) != 0ULL ||
        !IsUserVirtualAddressRange(virtual_address, OS_KERNEL_MEMORY_PAGE_SIZE_BYTES)) {
        return KernelUserPageStatus::InvalidVirtualAddress;
    }
    PageTableManager process_page_table{FrameAllocator(), root_physical_address,
                                        ActivePageTableMemoryAccess(), PageTableRootKind::Process};
    PageMapping mapping{};
    if (process_page_table.QueryPage(virtual_address, mapping) != PageTableStatus::Succeeded) {
        return KernelUserPageStatus::PageNotMapped;
    }
    if (!mapping.permissions.user_accessible) {
        return KernelUserPageStatus::NotUserAccessible;
    }
    if (process_page_table.UnmapPage(virtual_address) != PageTableStatus::Succeeded) {
        return KernelUserPageStatus::PageUnmappingFailed;
    }
    if (FrameAllocator().Release(PhysicalFrame{.physical_address = mapping.physical_address}) !=
        PhysicalFrameAllocatorStatus::Succeeded) {
        return KernelUserPageStatus::FrameReleaseFailed;
    }
    return KernelUserPageStatus::Succeeded;
}

PageTableStatus QueryAddressSpacePage(const uint64_t root_physical_address,
                                      const uint64_t virtual_address,
                                      PageMapping &mapping) noexcept {
    if (root_physical_address == 0ULL ||
        (root_physical_address & OS_KERNEL_MEMORY_PAGE_MASK) != 0ULL) {
        return PageTableStatus::NotInitialized;
    }
    const PageTableRootKind root_kind =
        root_physical_address == GetPageTableManager().RootPhysicalAddress()
            ? PageTableRootKind::KernelShared
            : PageTableRootKind::Process;
    PageTableManager page_table{FrameAllocator(), root_physical_address,
                                ActivePageTableMemoryAccess(), root_kind};
    return page_table.QueryPage(virtual_address, mapping);
}

PageTableStatus QueryActivePage(const uint64_t virtual_address, PageMapping &mapping) noexcept {
    return QueryAddressSpacePage(ReadPageTableRoot(), virtual_address, mapping);
}

uint64_t GetKernelPageTableRoot() noexcept { return GetPageTableManager().RootPhysicalAddress(); }

bool ActivateUserPageTable(const uint64_t root_physical_address) noexcept {
    if (root_physical_address == 0ULL ||
        (root_physical_address & OS_KERNEL_MEMORY_PAGE_MASK) != 0ULL ||
        root_physical_address == GetPageTableManager().RootPhysicalAddress()) {
        return false;
    }
    ActivatePageTable(root_physical_address);
    return ReadPageTableRoot() == root_physical_address;
}

void ActivateKernelPageTable() noexcept {
    ActivatePageTable(GetPageTableManager().RootPhysicalAddress());
}

}

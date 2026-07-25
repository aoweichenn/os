#include "os/kernel/memory_manager.hpp"

#include "os/kernel/descriptor_tables.hpp"
#include "os/kernel/page_table.hpp"
#include "os/kernel/physical_frame_allocator.hpp"
#include "os/kernel/physical_memory_map.hpp"
#include "os/kernel/process_memory_layout.hpp"
#include "os/kernel/processor.hpp"
#include "os/kernel/user_elf.hpp"

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
constexpr uint64_t OS_KERNEL_MEMORY_MINIMUM_PAGE_TABLE_PHYSICAL_LIMIT =
    OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;

KernelMemoryStatistics current_kernel_memory_statistics{};
uint64_t current_managed_physical_address_limit = 0ULL;
uint64_t current_page_table_physical_address_limit = 0ULL;
bool direct_map_active = false;

struct DirectMapStatistics final {
    uint64_t mapped_bytes;
    uint64_t large_page_count;
    uint64_t small_page_count;
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
    for (uint64_t process_index = 0ULL; process_index < OS_KERNEL_PROCESS_CAPACITY;
         ++process_index) {
        if (page_address == ProcessKernelStackGuardPageAddress(process_index)) {
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
    for (uint64_t process_index = 0ULL; process_index < OS_KERNEL_PROCESS_CAPACITY;
         ++process_index) {
        if (GetPageTableManager().QueryPage(ProcessKernelStackGuardPageAddress(process_index),
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
    uint64_t high_memory_test_physical_address = 0ULL;
    if (!RunHighMemorySelfTest(high_memory_test_physical_address)) {
        return KernelMemoryInitializationStatus::HighMemorySelfTestFailed;
    }
    uint64_t buddy_self_test_physical_address = 0ULL;
    if (!RunBuddySelfTest(buddy_self_test_physical_address)) {
        return KernelMemoryInitializationStatus::BuddySelfTestFailed;
    }

    const PhysicalFrameAllocatorStatistics frame_statistics = FrameAllocator().Statistics();
    const PhysicalFrameBuddyStatistics buddy_statistics = FrameAllocator().BuddyStatistics();
    const uint64_t managed_usable_memory_bytes =
        (frame_statistics.free_frame_count + frame_statistics.allocated_frame_count +
         frame_statistics.reserved_frame_count) *
        OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
    const KernelHeapStatistics heap_statistics = GetKernelHeap().Statistics();
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
        .heap_capacity_bytes = heap_statistics.capacity_bytes,
        .heap_consumed_bytes = heap_statistics.consumed_bytes,
        .heap_active_allocation_count = heap_statistics.allocation_count,
        .heap_successful_allocation_count = heap_statistics.successful_allocation_count,
        .heap_release_count = heap_statistics.release_count,
        .heap_peak_consumed_bytes = heap_statistics.peak_consumed_bytes,
        .heap_largest_free_allocation_bytes = heap_statistics.largest_free_allocation_bytes,
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

KernelUserPageStatus CreateUserPageTable(uint64_t &root_physical_address) noexcept {
    PageTableManager process_page_table{FrameAllocator(), ActivePageTableMemoryAccess()};
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
                                        ActivePageTableMemoryAccess()};
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
                                        ActivePageTableMemoryAccess()};
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
                                        ActivePageTableMemoryAccess()};
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
    PageTableManager page_table{FrameAllocator(), root_physical_address,
                                ActivePageTableMemoryAccess()};
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

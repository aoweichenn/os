#pragma once

#include "os/kernel/boot_info.hpp"
#include "os/kernel/kernel_heap.hpp"
#include "os/kernel/page_table.hpp"

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_MEMORY_HEAP_VIRTUAL_BASE = 0xFFFF800000000000ULL;
inline constexpr uint64_t OS_KERNEL_MEMORY_HEAP_SIZE_BYTES = 64ULL * 1024ULL;
inline constexpr uint64_t OS_KERNEL_MEMORY_WRITE_PROTECTION_TEST_VIRTUAL_ADDRESS =
    0xFFFF800000100000ULL;
inline constexpr uint64_t OS_KERNEL_MEMORY_DIRECT_MAP_VIRTUAL_BASE = 0xFFFF888000000000ULL;
inline constexpr uint64_t OS_KERNEL_MEMORY_DIRECT_MAP_CAPACITY_BYTES =
    64ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL;

struct KernelMemoryStatistics final {
    uint64_t memory_map_entry_count;
    uint64_t described_address_bytes;
    uint64_t reported_usable_memory_bytes;
    uint64_t managed_usable_memory_bytes;
    uint64_t managed_physical_address_limit;
    uint64_t physical_address_width_bits;
    uint64_t virtual_address_width_bits;
    uint64_t five_level_paging_supported;
    uint64_t frame_state_storage_physical_address;
    uint64_t frame_state_storage_size_bytes;
    uint64_t buddy_storage_physical_address;
    uint64_t buddy_storage_size_bytes;
    uint64_t buddy_maximum_order;
    uint64_t buddy_free_block_count;
    uint64_t buddy_active_block_count;
    uint64_t buddy_successful_allocation_count;
    uint64_t buddy_release_count;
    uint64_t buddy_split_count;
    uint64_t buddy_merge_count;
    uint64_t buddy_largest_free_order;
    uint64_t buddy_self_test_physical_address;
    uint64_t buddy_self_test_order;
    uint64_t direct_map_mapped_bytes;
    uint64_t direct_map_large_page_count;
    uint64_t direct_map_small_page_count;
    uint64_t high_memory_test_physical_address;
    uint64_t free_frame_count;
    uint64_t allocated_frame_count;
    uint64_t reserved_frame_count;
    uint64_t page_table_root_physical_address;
    uint64_t heap_capacity_bytes;
    uint64_t heap_consumed_bytes;
    uint64_t heap_active_allocation_count;
    uint64_t heap_successful_allocation_count;
    uint64_t heap_release_count;
    uint64_t heap_peak_consumed_bytes;
    uint64_t heap_largest_free_allocation_bytes;
};

enum class KernelMemoryInitializationStatus : uint64_t {
    Succeeded,
    InvalidMemoryMap,
    InvalidProcessorAddressWidth,
    InvalidManagedPhysicalAddressLimit,
    FrameStateStorageUnavailable,
    FrameAllocatorConfigurationFailed,
    FrameAllocatorInitializationFailed,
    BuddyAllocatorConfigurationFailed,
    BuddyAllocatorInitializationFailed,
    ReservationFailed,
    PageTableInitializationFailed,
    IdentityMappingFailed,
    DirectMapMappingFailed,
    HeapMappingFailed,
    ProtectionTestMappingFailed,
    MemoryProtectionUnsupported,
    PageTableActivationFailed,
    PageTableMemoryAccessFailed,
    PermissionValidationFailed,
    HeapInitializationFailed,
    HeapSelfTestFailed,
    HighMemorySelfTestFailed,
    BuddySelfTestFailed,
    LocalApicMappingFailed,
};

enum class KernelUserPageStatus : uint64_t {
    Succeeded,
    InvalidVirtualAddress,
    InvalidPermissions,
    InvalidPageTableRoot,
    PageTableCreationFailed,
    FrameAllocationFailed,
    PageMappingFailed,
    PageNotMapped,
    NotUserAccessible,
    PageUnmappingFailed,
    FrameReleaseFailed,
    PageTableDestructionFailed,
};

[[nodiscard]] KernelMemoryInitializationStatus
InitializeKernelMemory(const BootInfo &boot_info) noexcept;
[[nodiscard]] const KernelMemoryStatistics &GetKernelMemoryStatistics() noexcept;
[[nodiscard]] PhysicalFrameAllocatorStatistics GetPhysicalFrameAllocatorStatistics() noexcept;
[[nodiscard]] uint64_t PhysicalMemoryDirectMapAddress(uint64_t physical_address) noexcept;
[[nodiscard]] KernelHeap &GetKernelHeap() noexcept;
[[nodiscard]] KernelUserPageStatus CreateUserPageTable(uint64_t &root_physical_address) noexcept;
[[nodiscard]] KernelUserPageStatus DestroyUserPageTable(uint64_t root_physical_address) noexcept;
[[nodiscard]] KernelUserPageStatus AllocateAndMapUserPage(uint64_t root_physical_address,
                                                          uint64_t virtual_address, bool writable,
                                                          bool executable,
                                                          uint64_t &physical_address) noexcept;
[[nodiscard]] KernelUserPageStatus ReleaseUserPage(uint64_t root_physical_address,
                                                   uint64_t virtual_address) noexcept;
[[nodiscard]] PageTableStatus QueryAddressSpacePage(uint64_t root_physical_address,
                                                    uint64_t virtual_address,
                                                    PageMapping &mapping) noexcept;
[[nodiscard]] PageTableStatus QueryActivePage(uint64_t virtual_address,
                                              PageMapping &mapping) noexcept;
[[nodiscard]] uint64_t GetKernelPageTableRoot() noexcept;
[[nodiscard]] bool ActivateUserPageTable(uint64_t root_physical_address) noexcept;
void ActivateKernelPageTable() noexcept;

}

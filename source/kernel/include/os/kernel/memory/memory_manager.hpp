#pragma once

#include "os/kernel/boot/boot_info.hpp"
#include "os/kernel/memory/kernel_heap.hpp"
#include "os/kernel/memory/kernel_stack_manager.hpp"
#include "os/kernel/memory/kernel_type_cache.hpp"
#include "os/kernel/memory/kernel_virtual_address_allocator.hpp"
#include "os/kernel/memory/page_table.hpp"
#include "os/kernel/memory/resource_snapshot.hpp"

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_MEMORY_HEAP_VIRTUAL_BASE = 0xFFFF800000000000ULL;
inline constexpr uint64_t OS_KERNEL_MEMORY_HEAP_SIZE_BYTES = 512ULL * 1024ULL;
inline constexpr uint64_t OS_KERNEL_MEMORY_WRITE_PROTECTION_TEST_VIRTUAL_ADDRESS =
    0xFFFF800000100000ULL;
inline constexpr uint64_t OS_KERNEL_MEMORY_DIRECT_MAP_VIRTUAL_BASE = 0xFFFF888000000000ULL;
inline constexpr uint64_t OS_KERNEL_MEMORY_DIRECT_MAP_CAPACITY_BYTES =
    64ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL;
inline constexpr uint64_t OS_KERNEL_MEMORY_KVA_VIRTUAL_BASE = 0xFFFFC90000000000ULL;
inline constexpr uint64_t OS_KERNEL_MEMORY_KVA_CAPACITY_BYTES =
    32ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL;
inline constexpr uint64_t OS_KERNEL_MEMORY_KVA_DESCRIPTOR_CAPACITY = 1024ULL;
inline constexpr uint64_t OS_KERNEL_MEMORY_KERNEL_STACK_SLOT_CAPACITY = 512ULL;

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
    uint64_t page_table_reclaimed_level1_table_count;
    uint64_t page_table_reclaimed_level2_table_count;
    uint64_t page_table_reclaimed_level3_table_count;
    uint64_t page_table_retained_shared_level3_table_count;
    uint64_t heap_capacity_bytes;
    uint64_t heap_consumed_bytes;
    uint64_t heap_active_allocation_count;
    uint64_t heap_successful_allocation_count;
    uint64_t heap_release_count;
    uint64_t heap_peak_consumed_bytes;
    uint64_t heap_largest_free_allocation_bytes;
    uint64_t type_cache_object_size_bytes;
    uint64_t type_cache_object_alignment_bytes;
    uint64_t type_cache_slot_stride_bytes;
    uint64_t type_cache_capacity;
    uint64_t type_cache_backing_storage_size_bytes;
    uint64_t type_cache_active_object_count;
    uint64_t type_cache_free_object_count;
    uint64_t type_cache_successful_allocation_count;
    uint64_t type_cache_release_count;
    uint64_t type_cache_peak_active_object_count;
    uint64_t kva_window_begin_address;
    uint64_t kva_window_size_bytes;
    uint64_t kva_descriptor_capacity;
    uint64_t kva_active_descriptor_count;
    uint64_t kva_free_page_count;
    uint64_t kva_allocated_page_count;
    uint64_t kva_reserved_page_count;
    uint64_t kva_successful_allocation_count;
    uint64_t kva_release_count;
    uint64_t kva_peak_allocated_page_count;
    uint64_t kva_largest_free_range_page_count;
    uint64_t kva_self_test_virtual_address;
    uint64_t kva_self_test_physical_address;
    uint64_t kva_self_test_mapped_page_count;
    uint64_t kva_self_test_guard_page_count;
    uint64_t resource_snapshot_tracked_field_count;
    uint64_t resource_snapshot_changed_fields_mask;
    uint64_t reference_counter_self_test_passed;
    uint64_t scope_rollback_self_test_passed;
    uint64_t resource_snapshot_self_test_passed;
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
    TypeCacheSelfTestFailed,
    FileCacheAddressSpaceSelfTestFailed,
    KvaInitializationFailed,
    KvaSelfTestFailed,
    KernelStackManagerInitializationFailed,
    ResourceLifecycleBaselineSnapshotFailed,
    ResourceLifecycleRollbackInitializationFailed,
    ResourceLifecycleStackCreationFailed,
    ResourceLifecycleRollbackRegistrationFailed,
    ReferenceCounterSelfTestFailed,
    ScopeRollbackSelfTestFailed,
    ResourceSnapshotSelfTestFailed,
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
    FrameNotOwned,
    PageMappingFailed,
    PageNotMapped,
    NotUserAccessible,
    PageUnmappingFailed,
    FrameReleaseFailed,
    PageTableDestructionFailed,
};

struct KernelMmioMapping final {
    uint64_t virtual_address;
    uint64_t physical_address;
    uint64_t page_count;
    bool active;
};

struct KernelPageAllocation final {
    uint64_t virtual_address;
    uint64_t page_count;
    bool active;
};

enum class KernelPageAllocationStatus : uint64_t {
    Succeeded,
    NotInitialized,
    InvalidPageCount,
    AlreadyActive,
    VirtualAddressAllocationFailed,
    FrameAllocationFailed,
    PageMappingFailed,
    PageUnmappingFailed,
    FrameReleaseFailed,
    VirtualAddressReleaseFailed,
    RollbackFailed,
};

enum class KernelMmioStatus : uint64_t {
    Succeeded,
    NotInitialized,
    InvalidRange,
    VirtualAddressAllocationFailed,
    PageMappingFailed,
    PageUnmappingFailed,
    VirtualAddressReleaseFailed,
    RollbackFailed,
};

[[nodiscard]] KernelMemoryInitializationStatus
InitializeKernelMemory(const BootInfo &boot_info) noexcept;
[[nodiscard]] const KernelMemoryStatistics &GetKernelMemoryStatistics() noexcept;
[[nodiscard]] PhysicalFrameAllocator &GetKernelPhysicalFrameAllocator() noexcept;
[[nodiscard]] PhysicalFrameAllocatorStatistics GetPhysicalFrameAllocatorStatistics() noexcept;
[[nodiscard]] uint64_t PhysicalMemoryDirectMapAddress(uint64_t physical_address) noexcept;
[[nodiscard]] KernelMmioStatus MapKernelMmio(uint64_t physical_address, uint64_t size_bytes,
                                             KernelMmioMapping &mapping) noexcept;
[[nodiscard]] KernelMmioStatus UnmapKernelMmio(KernelMmioMapping &mapping) noexcept;
[[nodiscard]] KernelPageAllocationStatus
AllocateKernelPages(uint64_t page_count, KernelPageAllocation &allocation) noexcept;
[[nodiscard]] KernelPageAllocationStatus
ReleaseKernelPages(KernelPageAllocation &allocation) noexcept;
[[nodiscard]] KernelHeap &GetKernelHeap() noexcept;
[[nodiscard]] KernelVirtualAddressAllocator &GetKernelVirtualAddressAllocator() noexcept;
[[nodiscard]] KernelStackManager &GetKernelStackManager() noexcept;
[[nodiscard]] ResourceSnapshotStatus GetKernelResourceSnapshot(ResourceSnapshot &snapshot) noexcept;
[[nodiscard]] ResourceSnapshotStatus
GetKernelResourceSnapshot(const ResourceSnapshotSupplementalCounts &supplemental_counts,
                          ResourceSnapshot &snapshot) noexcept;
[[nodiscard]] KernelUserPageStatus CreateUserPageTable(uint64_t &root_physical_address) noexcept;
[[nodiscard]] KernelUserPageStatus DestroyUserPageTable(uint64_t root_physical_address) noexcept;
[[nodiscard]] KernelUserPageStatus AllocateAndMapUserPage(uint64_t root_physical_address,
                                                          uint64_t virtual_address, bool writable,
                                                          bool executable,
                                                          uint64_t &physical_address) noexcept;
[[nodiscard]] KernelUserPageStatus MapExistingUserPage(uint64_t root_physical_address,
                                                       uint64_t virtual_address,
                                                       uint64_t physical_address, bool writable,
                                                       bool executable,
                                                       bool copy_on_write = false) noexcept;
[[nodiscard]] KernelUserPageStatus ReplaceUserPage(uint64_t root_physical_address,
                                                   uint64_t virtual_address,
                                                   uint64_t physical_address, bool writable,
                                                   bool executable,
                                                   bool copy_on_write) noexcept;
[[nodiscard]] KernelUserPageStatus UnmapUserPage(uint64_t root_physical_address,
                                                 uint64_t virtual_address,
                                                 uint64_t &physical_address,
                                                 uint64_t &reclaimed_table_frame_count) noexcept;
[[nodiscard]] KernelUserPageStatus ReleaseUserPage(uint64_t root_physical_address,
                                                   uint64_t virtual_address,
                                                   uint64_t &reclaimed_table_frame_count) noexcept;
[[nodiscard]] PageTableStatus QueryAddressSpacePage(uint64_t root_physical_address,
                                                    uint64_t virtual_address,
                                                    PageMapping &mapping) noexcept;
[[nodiscard]] PageTableStatus TestAndClearAddressSpacePageAccessed(uint64_t root_physical_address,
                                                                   uint64_t virtual_address,
                                                                   PageMapping &mapping,
                                                                   bool &accessed) noexcept;
[[nodiscard]] PageTableStatus QueryActivePage(uint64_t virtual_address,
                                              PageMapping &mapping) noexcept;
[[nodiscard]] uint64_t GetKernelPageTableRoot() noexcept;
[[nodiscard]] bool ActivateUserPageTable(uint64_t root_physical_address) noexcept;
void ActivateKernelPageTable() noexcept;

}

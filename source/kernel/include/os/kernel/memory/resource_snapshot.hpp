#pragma once

#include "os/kernel/memory/kernel_heap.hpp"
#include "os/kernel/memory/kernel_stack_manager.hpp"
#include "os/kernel/memory/kernel_virtual_address_allocator.hpp"
#include "os/kernel/memory/physical_frame_allocator.hpp"

#include <stdint.h>

namespace os::kernel {

enum class ResourceSnapshotField : uint64_t {
    ManagedFrameCount,
    FreeFrameCount,
    AllocatedFrameCount,
    ReservedFrameCount,
    BuddyActiveBlockCount,
    HeapCapacityBytes,
    HeapConsumedBytes,
    HeapActiveRequestedBytes,
    HeapAllocationCount,
    VirtualAddressCapacityPageCount,
    VirtualAddressFreePageCount,
    VirtualAddressAllocatedPageCount,
    VirtualAddressReservedPageCount,
    VirtualAddressActiveDescriptorCount,
    VirtualAddressActiveAllocationCount,
    VirtualAddressReservationCount,
    KernelStackSlotCapacity,
    KernelStackActiveCount,
    KernelStackMappedPageCount,
    KernelStackGuardPageCount,
    ProcessCount,
    ThreadCount,
    FileDescriptionCount,
    VnodeCount,
    CachePageCount,
    BlockRequestCount,
    Count,
};

inline constexpr uint64_t OS_KERNEL_RESOURCE_SNAPSHOT_TRACKED_FIELD_COUNT =
    static_cast<uint64_t>(ResourceSnapshotField::Count);
inline constexpr uint64_t OS_KERNEL_RESOURCE_SNAPSHOT_SINGLE_FIELD_MASK = 1ULL;
inline constexpr uint64_t OS_KERNEL_RESOURCE_SNAPSHOT_UINT64_BIT_COUNT =
    sizeof(uint64_t) * 8ULL;
static_assert(OS_KERNEL_RESOURCE_SNAPSHOT_TRACKED_FIELD_COUNT <
              OS_KERNEL_RESOURCE_SNAPSHOT_UINT64_BIT_COUNT);
inline constexpr uint64_t OS_KERNEL_RESOURCE_SNAPSHOT_ALL_FIELDS_MASK =
    (OS_KERNEL_RESOURCE_SNAPSHOT_SINGLE_FIELD_MASK
     << OS_KERNEL_RESOURCE_SNAPSHOT_TRACKED_FIELD_COUNT) -
    OS_KERNEL_RESOURCE_SNAPSHOT_SINGLE_FIELD_MASK;

struct ResourceSnapshotSupplementalCounts final {
    uint64_t process_count;
    uint64_t thread_count;
    uint64_t file_description_count;
    uint64_t vnode_count;
    uint64_t cache_page_count;
    uint64_t block_request_count;
};

struct ResourceSnapshot final {
    uint64_t managed_frame_count;
    uint64_t free_frame_count;
    uint64_t allocated_frame_count;
    uint64_t reserved_frame_count;
    uint64_t buddy_active_block_count;
    uint64_t heap_capacity_bytes;
    uint64_t heap_consumed_bytes;
    uint64_t heap_active_requested_bytes;
    uint64_t heap_allocation_count;
    uint64_t virtual_address_capacity_page_count;
    uint64_t virtual_address_free_page_count;
    uint64_t virtual_address_allocated_page_count;
    uint64_t virtual_address_reserved_page_count;
    uint64_t virtual_address_active_descriptor_count;
    uint64_t virtual_address_active_allocation_count;
    uint64_t virtual_address_reservation_count;
    uint64_t kernel_stack_slot_capacity;
    uint64_t kernel_stack_active_count;
    uint64_t kernel_stack_mapped_page_count;
    uint64_t kernel_stack_guard_page_count;
    uint64_t process_count;
    uint64_t thread_count;
    uint64_t file_description_count;
    uint64_t vnode_count;
    uint64_t cache_page_count;
    uint64_t block_request_count;
};

struct ResourceSnapshotDifference final {
    uint64_t changed_fields_mask;
    uint64_t changed_field_count;
};

enum class ResourceSnapshotStatus : uint64_t {
    Succeeded,
    InvalidFrameAccounting,
    InvalidBuddyAccounting,
    InvalidHeapAccounting,
    InvalidVirtualAddressAccounting,
    InvalidKernelStackAccounting,
    InvalidTrackedFieldCount,
};

[[nodiscard]] uint64_t ResourceSnapshotFieldMask(ResourceSnapshotField field) noexcept;
[[nodiscard]] ResourceSnapshotStatus
ValidateResourceSnapshot(const ResourceSnapshot &snapshot) noexcept;
[[nodiscard]] ResourceSnapshotStatus
CreateResourceSnapshot(const PhysicalFrameAllocatorStatistics &frame_statistics,
                       const PhysicalFrameBuddyStatistics &buddy_statistics,
                       const KernelHeapStatistics &heap_statistics,
                       const KernelVirtualAddressAllocatorStatistics &virtual_address_statistics,
                       const KernelStackManagerStatistics &kernel_stack_statistics,
                       const ResourceSnapshotSupplementalCounts &supplemental_counts,
                       ResourceSnapshot &snapshot) noexcept;
[[nodiscard]] ResourceSnapshotStatus
CompareResourceSnapshots(const ResourceSnapshot &before, const ResourceSnapshot &after,
                         ResourceSnapshotDifference &difference) noexcept;
[[nodiscard]] bool ResourceSnapshotsMatch(const ResourceSnapshot &before,
                                          const ResourceSnapshot &after) noexcept;

}

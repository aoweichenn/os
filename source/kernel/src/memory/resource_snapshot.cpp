#include "os/kernel/memory/resource_snapshot.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_RESOURCE_SNAPSHOT_EMPTY_COUNT = 0ULL;

[[nodiscard]] bool TryAdd(const uint64_t left, const uint64_t right,
                          uint64_t &sum) noexcept {
    if (left > UINT64_MAX - right) {
        return false;
    }
    sum = left + right;
    return true;
}

void RecordDifference(const uint64_t before, const uint64_t after,
                      const ResourceSnapshotField field,
                      ResourceSnapshotDifference &difference) noexcept {
    if (before == after) {
        return;
    }
    difference.changed_fields_mask |= ResourceSnapshotFieldMask(field);
    ++difference.changed_field_count;
}

}

uint64_t ResourceSnapshotFieldMask(const ResourceSnapshotField field) noexcept {
    const uint64_t field_index = static_cast<uint64_t>(field);
    return field_index < OS_KERNEL_RESOURCE_SNAPSHOT_TRACKED_FIELD_COUNT
                   && field_index < OS_KERNEL_RESOURCE_SNAPSHOT_UINT64_BIT_COUNT
               ? OS_KERNEL_RESOURCE_SNAPSHOT_SINGLE_FIELD_MASK << field_index
               : OS_KERNEL_RESOURCE_SNAPSHOT_EMPTY_COUNT;
}

ResourceSnapshotStatus
ValidateResourceSnapshot(const ResourceSnapshot &snapshot) noexcept {
    if (OS_KERNEL_RESOURCE_SNAPSHOT_TRACKED_FIELD_COUNT >=
        OS_KERNEL_RESOURCE_SNAPSHOT_UINT64_BIT_COUNT) {
        return ResourceSnapshotStatus::InvalidTrackedFieldCount;
    }

    uint64_t accounted_frame_count = OS_KERNEL_RESOURCE_SNAPSHOT_EMPTY_COUNT;
    if (!TryAdd(snapshot.free_frame_count, snapshot.allocated_frame_count,
                accounted_frame_count) ||
        !TryAdd(accounted_frame_count, snapshot.reserved_frame_count,
                accounted_frame_count) ||
        accounted_frame_count > snapshot.managed_frame_count) {
        return ResourceSnapshotStatus::InvalidFrameAccounting;
    }
    if (snapshot.buddy_active_block_count > snapshot.allocated_frame_count) {
        return ResourceSnapshotStatus::InvalidBuddyAccounting;
    }
    if (snapshot.heap_consumed_bytes > snapshot.heap_capacity_bytes ||
        snapshot.heap_active_requested_bytes > snapshot.heap_consumed_bytes) {
        return ResourceSnapshotStatus::InvalidHeapAccounting;
    }

    uint64_t accounted_virtual_page_count = OS_KERNEL_RESOURCE_SNAPSHOT_EMPTY_COUNT;
    if (!TryAdd(snapshot.virtual_address_free_page_count,
                snapshot.virtual_address_allocated_page_count,
                accounted_virtual_page_count) ||
        !TryAdd(accounted_virtual_page_count,
                snapshot.virtual_address_reserved_page_count,
                accounted_virtual_page_count) ||
        accounted_virtual_page_count !=
            snapshot.virtual_address_capacity_page_count ||
        snapshot.virtual_address_active_allocation_count >
            snapshot.virtual_address_active_descriptor_count ||
        snapshot.virtual_address_reservation_count >
            snapshot.virtual_address_active_descriptor_count) {
        return ResourceSnapshotStatus::InvalidVirtualAddressAccounting;
    }

    if (snapshot.kernel_stack_active_count > snapshot.kernel_stack_slot_capacity ||
        snapshot.kernel_stack_active_count >
            UINT64_MAX / OS_KERNEL_STACK_MAPPED_PAGE_COUNT ||
        snapshot.kernel_stack_mapped_page_count !=
            snapshot.kernel_stack_active_count * OS_KERNEL_STACK_MAPPED_PAGE_COUNT ||
        snapshot.kernel_stack_active_count >
            UINT64_MAX / OS_KERNEL_STACK_GUARD_PAGE_COUNT ||
        snapshot.kernel_stack_guard_page_count !=
            snapshot.kernel_stack_active_count * OS_KERNEL_STACK_GUARD_PAGE_COUNT) {
        return ResourceSnapshotStatus::InvalidKernelStackAccounting;
    }
    return ResourceSnapshotStatus::Succeeded;
}

ResourceSnapshotStatus
CreateResourceSnapshot(
    const PhysicalFrameAllocatorStatistics &frame_statistics,
    const PhysicalFrameBuddyStatistics &buddy_statistics,
    const KernelHeapStatistics &heap_statistics,
    const KernelVirtualAddressAllocatorStatistics &virtual_address_statistics,
    const KernelStackManagerStatistics &kernel_stack_statistics,
    const ResourceSnapshotSupplementalCounts &supplemental_counts,
    ResourceSnapshot &snapshot) noexcept {
    const ResourceSnapshot candidate{
        .managed_frame_count = frame_statistics.managed_frame_count,
        .free_frame_count = frame_statistics.free_frame_count,
        .allocated_frame_count = frame_statistics.allocated_frame_count,
        .reserved_frame_count = frame_statistics.reserved_frame_count,
        .buddy_active_block_count = buddy_statistics.active_block_count,
        .heap_capacity_bytes = heap_statistics.capacity_bytes,
        .heap_consumed_bytes = heap_statistics.consumed_bytes,
        .heap_active_requested_bytes = heap_statistics.active_requested_bytes,
        .heap_allocation_count = heap_statistics.allocation_count,
        .virtual_address_capacity_page_count =
            virtual_address_statistics.capacity_page_count,
        .virtual_address_free_page_count = virtual_address_statistics.free_page_count,
        .virtual_address_allocated_page_count =
            virtual_address_statistics.allocated_page_count,
        .virtual_address_reserved_page_count =
            virtual_address_statistics.reserved_page_count,
        .virtual_address_active_descriptor_count =
            virtual_address_statistics.active_descriptor_count,
        .virtual_address_active_allocation_count =
            virtual_address_statistics.active_allocation_count,
        .virtual_address_reservation_count =
            virtual_address_statistics.reservation_count,
        .kernel_stack_slot_capacity = kernel_stack_statistics.slot_capacity,
        .kernel_stack_active_count = kernel_stack_statistics.active_stack_count,
        .kernel_stack_mapped_page_count =
            kernel_stack_statistics.active_mapped_page_count,
        .kernel_stack_guard_page_count =
            kernel_stack_statistics.active_guard_page_count,
        .process_count = supplemental_counts.process_count,
        .thread_count = supplemental_counts.thread_count,
        .file_description_count = supplemental_counts.file_description_count,
        .vnode_count = supplemental_counts.vnode_count,
        .cache_page_count = supplemental_counts.cache_page_count,
        .block_request_count = supplemental_counts.block_request_count,
    };
    const ResourceSnapshotStatus status = ValidateResourceSnapshot(candidate);
    if (status == ResourceSnapshotStatus::Succeeded) {
        snapshot = candidate;
    }
    return status;
}

ResourceSnapshotStatus
CompareResourceSnapshots(const ResourceSnapshot &before, const ResourceSnapshot &after,
                         ResourceSnapshotDifference &difference) noexcept {
    const ResourceSnapshotStatus before_status = ValidateResourceSnapshot(before);
    if (before_status != ResourceSnapshotStatus::Succeeded) {
        return before_status;
    }
    const ResourceSnapshotStatus after_status = ValidateResourceSnapshot(after);
    if (after_status != ResourceSnapshotStatus::Succeeded) {
        return after_status;
    }

    ResourceSnapshotDifference candidate{};
    RecordDifference(before.managed_frame_count, after.managed_frame_count,
                     ResourceSnapshotField::ManagedFrameCount, candidate);
    RecordDifference(before.free_frame_count, after.free_frame_count,
                     ResourceSnapshotField::FreeFrameCount, candidate);
    RecordDifference(before.allocated_frame_count, after.allocated_frame_count,
                     ResourceSnapshotField::AllocatedFrameCount, candidate);
    RecordDifference(before.reserved_frame_count, after.reserved_frame_count,
                     ResourceSnapshotField::ReservedFrameCount, candidate);
    RecordDifference(before.buddy_active_block_count, after.buddy_active_block_count,
                     ResourceSnapshotField::BuddyActiveBlockCount, candidate);
    RecordDifference(before.heap_capacity_bytes, after.heap_capacity_bytes,
                     ResourceSnapshotField::HeapCapacityBytes, candidate);
    RecordDifference(before.heap_consumed_bytes, after.heap_consumed_bytes,
                     ResourceSnapshotField::HeapConsumedBytes, candidate);
    RecordDifference(before.heap_active_requested_bytes,
                     after.heap_active_requested_bytes,
                     ResourceSnapshotField::HeapActiveRequestedBytes, candidate);
    RecordDifference(before.heap_allocation_count, after.heap_allocation_count,
                     ResourceSnapshotField::HeapAllocationCount, candidate);
    RecordDifference(before.virtual_address_capacity_page_count,
                     after.virtual_address_capacity_page_count,
                     ResourceSnapshotField::VirtualAddressCapacityPageCount, candidate);
    RecordDifference(before.virtual_address_free_page_count,
                     after.virtual_address_free_page_count,
                     ResourceSnapshotField::VirtualAddressFreePageCount, candidate);
    RecordDifference(before.virtual_address_allocated_page_count,
                     after.virtual_address_allocated_page_count,
                     ResourceSnapshotField::VirtualAddressAllocatedPageCount, candidate);
    RecordDifference(before.virtual_address_reserved_page_count,
                     after.virtual_address_reserved_page_count,
                     ResourceSnapshotField::VirtualAddressReservedPageCount, candidate);
    RecordDifference(before.virtual_address_active_descriptor_count,
                     after.virtual_address_active_descriptor_count,
                     ResourceSnapshotField::VirtualAddressActiveDescriptorCount, candidate);
    RecordDifference(before.virtual_address_active_allocation_count,
                     after.virtual_address_active_allocation_count,
                     ResourceSnapshotField::VirtualAddressActiveAllocationCount, candidate);
    RecordDifference(before.virtual_address_reservation_count,
                     after.virtual_address_reservation_count,
                     ResourceSnapshotField::VirtualAddressReservationCount, candidate);
    RecordDifference(before.kernel_stack_slot_capacity,
                     after.kernel_stack_slot_capacity,
                     ResourceSnapshotField::KernelStackSlotCapacity, candidate);
    RecordDifference(before.kernel_stack_active_count,
                     after.kernel_stack_active_count,
                     ResourceSnapshotField::KernelStackActiveCount, candidate);
    RecordDifference(before.kernel_stack_mapped_page_count,
                     after.kernel_stack_mapped_page_count,
                     ResourceSnapshotField::KernelStackMappedPageCount, candidate);
    RecordDifference(before.kernel_stack_guard_page_count,
                     after.kernel_stack_guard_page_count,
                     ResourceSnapshotField::KernelStackGuardPageCount, candidate);
    RecordDifference(before.process_count, after.process_count,
                     ResourceSnapshotField::ProcessCount, candidate);
    RecordDifference(before.thread_count, after.thread_count,
                     ResourceSnapshotField::ThreadCount, candidate);
    RecordDifference(before.file_description_count, after.file_description_count,
                     ResourceSnapshotField::FileDescriptionCount, candidate);
    RecordDifference(before.vnode_count, after.vnode_count,
                     ResourceSnapshotField::VnodeCount, candidate);
    RecordDifference(before.cache_page_count, after.cache_page_count,
                     ResourceSnapshotField::CachePageCount, candidate);
    RecordDifference(before.block_request_count, after.block_request_count,
                     ResourceSnapshotField::BlockRequestCount, candidate);
    difference = candidate;
    return ResourceSnapshotStatus::Succeeded;
}

bool ResourceSnapshotsMatch(const ResourceSnapshot &before,
                            const ResourceSnapshot &after) noexcept {
    ResourceSnapshotDifference difference{};
    return CompareResourceSnapshots(before, after, difference) ==
               ResourceSnapshotStatus::Succeeded &&
           difference.changed_fields_mask == OS_KERNEL_RESOURCE_SNAPSHOT_EMPTY_COUNT &&
           difference.changed_field_count == OS_KERNEL_RESOURCE_SNAPSHOT_EMPTY_COUNT;
}

}

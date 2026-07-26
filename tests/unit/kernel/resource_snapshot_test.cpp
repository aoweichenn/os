#include "os/kernel/memory/resource_snapshot.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_RESOURCE_SNAPSHOT_SUITE_NAME =
    "kernel/resource_snapshot/unit";
constexpr std::string_view OS_TEST_RESOURCE_SNAPSHOT_FIELD_MASKS =
    "全部资源字段必须映射到唯一且完整的六十四位差异位";
constexpr std::string_view OS_TEST_RESOURCE_SNAPSHOT_CREATION =
    "资源统计必须生成通过内部所有权守恒校验的快照";
constexpr std::string_view OS_TEST_RESOURCE_SNAPSHOT_EXACT_MATCH =
    "相同快照必须产生零差异并被判定为完全匹配";
constexpr std::string_view OS_TEST_RESOURCE_SNAPSHOT_DIFFERENCE =
    "资源转移必须精确标出变化字段而不混入累计计数";
constexpr std::string_view OS_TEST_RESOURCE_SNAPSHOT_INVALID_ACCOUNTING =
    "无效所有权账本必须被拒绝且不得改写调用方输出";

constexpr uint64_t OS_TEST_RESOURCE_SNAPSHOT_EMPTY_COUNT = 0ULL;
constexpr uint64_t OS_TEST_RESOURCE_SNAPSHOT_SINGLE_UNIT = 1ULL;
constexpr uint64_t OS_TEST_RESOURCE_SNAPSHOT_MANAGED_FRAME_COUNT = 110ULL;
constexpr uint64_t OS_TEST_RESOURCE_SNAPSHOT_FREE_FRAME_COUNT = 60ULL;
constexpr uint64_t OS_TEST_RESOURCE_SNAPSHOT_ALLOCATED_FRAME_COUNT = 30ULL;
constexpr uint64_t OS_TEST_RESOURCE_SNAPSHOT_RESERVED_FRAME_COUNT = 10ULL;
constexpr uint64_t OS_TEST_RESOURCE_SNAPSHOT_BUDDY_ACTIVE_BLOCK_COUNT = 5ULL;
constexpr uint64_t OS_TEST_RESOURCE_SNAPSHOT_HEAP_CAPACITY_BYTES = 4096ULL;
constexpr uint64_t OS_TEST_RESOURCE_SNAPSHOT_HEAP_CONSUMED_BYTES = 512ULL;
constexpr uint64_t OS_TEST_RESOURCE_SNAPSHOT_HEAP_REQUESTED_BYTES = 256ULL;
constexpr uint64_t OS_TEST_RESOURCE_SNAPSHOT_HEAP_ALLOCATION_COUNT = 2ULL;
constexpr uint64_t OS_TEST_RESOURCE_SNAPSHOT_KVA_CAPACITY_PAGE_COUNT = 100ULL;
constexpr uint64_t OS_TEST_RESOURCE_SNAPSHOT_KVA_FREE_PAGE_COUNT = 70ULL;
constexpr uint64_t OS_TEST_RESOURCE_SNAPSHOT_KVA_ALLOCATED_PAGE_COUNT = 20ULL;
constexpr uint64_t OS_TEST_RESOURCE_SNAPSHOT_KVA_RESERVED_PAGE_COUNT = 10ULL;
constexpr uint64_t OS_TEST_RESOURCE_SNAPSHOT_KVA_DESCRIPTOR_COUNT = 5ULL;
constexpr uint64_t OS_TEST_RESOURCE_SNAPSHOT_KVA_ALLOCATION_COUNT = 3ULL;
constexpr uint64_t OS_TEST_RESOURCE_SNAPSHOT_KVA_RESERVATION_COUNT = 2ULL;
constexpr uint64_t OS_TEST_RESOURCE_SNAPSHOT_STACK_SLOT_CAPACITY = 16ULL;
constexpr uint64_t OS_TEST_RESOURCE_SNAPSHOT_STACK_ACTIVE_COUNT = 2ULL;
constexpr uint64_t OS_TEST_RESOURCE_SNAPSHOT_KVA_TRANSFER_PAGE_COUNT = 6ULL;
constexpr uint64_t OS_TEST_RESOURCE_SNAPSHOT_EXPECTED_CHANGED_FIELD_COUNT = 9ULL;
constexpr uint64_t OS_TEST_RESOURCE_SNAPSHOT_SENTINEL_MASK = 0xA5A5A5A5A5A5A5A5ULL;
constexpr uint64_t OS_TEST_RESOURCE_SNAPSHOT_SENTINEL_COUNT = 0x5A5A5A5A5A5A5A5AULL;

[[nodiscard]] os::kernel::ResourceSnapshot CreateValidSnapshot() noexcept {
    os::kernel::ResourceSnapshot snapshot{};
    static_cast<void>(os::kernel::CreateResourceSnapshot(
        os::kernel::PhysicalFrameAllocatorStatistics{
            .managed_frame_count = OS_TEST_RESOURCE_SNAPSHOT_MANAGED_FRAME_COUNT,
            .free_frame_count = OS_TEST_RESOURCE_SNAPSHOT_FREE_FRAME_COUNT,
            .allocated_frame_count =
                OS_TEST_RESOURCE_SNAPSHOT_ALLOCATED_FRAME_COUNT,
            .reserved_frame_count = OS_TEST_RESOURCE_SNAPSHOT_RESERVED_FRAME_COUNT,
        },
        os::kernel::PhysicalFrameBuddyStatistics{
            .metadata_storage_size_bytes = OS_TEST_RESOURCE_SNAPSHOT_EMPTY_COUNT,
            .maximum_order = OS_TEST_RESOURCE_SNAPSHOT_EMPTY_COUNT,
            .free_block_count = OS_TEST_RESOURCE_SNAPSHOT_EMPTY_COUNT,
            .active_block_count =
                OS_TEST_RESOURCE_SNAPSHOT_BUDDY_ACTIVE_BLOCK_COUNT,
            .successful_allocation_count = OS_TEST_RESOURCE_SNAPSHOT_EMPTY_COUNT,
            .release_count = OS_TEST_RESOURCE_SNAPSHOT_EMPTY_COUNT,
            .split_count = OS_TEST_RESOURCE_SNAPSHOT_EMPTY_COUNT,
            .merge_count = OS_TEST_RESOURCE_SNAPSHOT_EMPTY_COUNT,
            .largest_free_order = OS_TEST_RESOURCE_SNAPSHOT_EMPTY_COUNT,
        },
        os::kernel::KernelHeapStatistics{
            .capacity_bytes = OS_TEST_RESOURCE_SNAPSHOT_HEAP_CAPACITY_BYTES,
            .consumed_bytes = OS_TEST_RESOURCE_SNAPSHOT_HEAP_CONSUMED_BYTES,
            .remaining_bytes =
                OS_TEST_RESOURCE_SNAPSHOT_HEAP_CAPACITY_BYTES -
                OS_TEST_RESOURCE_SNAPSHOT_HEAP_CONSUMED_BYTES,
            .allocation_count =
                OS_TEST_RESOURCE_SNAPSHOT_HEAP_ALLOCATION_COUNT,
            .active_requested_bytes =
                OS_TEST_RESOURCE_SNAPSHOT_HEAP_REQUESTED_BYTES,
            .successful_allocation_count = OS_TEST_RESOURCE_SNAPSHOT_EMPTY_COUNT,
            .release_count = OS_TEST_RESOURCE_SNAPSHOT_EMPTY_COUNT,
            .peak_consumed_bytes =
                OS_TEST_RESOURCE_SNAPSHOT_HEAP_CONSUMED_BYTES,
            .largest_free_allocation_bytes = OS_TEST_RESOURCE_SNAPSHOT_EMPTY_COUNT,
        },
        os::kernel::KernelVirtualAddressAllocatorStatistics{
            .window_begin_address = OS_TEST_RESOURCE_SNAPSHOT_EMPTY_COUNT,
            .window_size_bytes = OS_TEST_RESOURCE_SNAPSHOT_EMPTY_COUNT,
            .capacity_page_count =
                OS_TEST_RESOURCE_SNAPSHOT_KVA_CAPACITY_PAGE_COUNT,
            .free_page_count = OS_TEST_RESOURCE_SNAPSHOT_KVA_FREE_PAGE_COUNT,
            .allocated_page_count =
                OS_TEST_RESOURCE_SNAPSHOT_KVA_ALLOCATED_PAGE_COUNT,
            .reserved_page_count =
                OS_TEST_RESOURCE_SNAPSHOT_KVA_RESERVED_PAGE_COUNT,
            .descriptor_capacity = OS_TEST_RESOURCE_SNAPSHOT_KVA_DESCRIPTOR_COUNT,
            .active_descriptor_count =
                OS_TEST_RESOURCE_SNAPSHOT_KVA_DESCRIPTOR_COUNT,
            .active_allocation_count =
                OS_TEST_RESOURCE_SNAPSHOT_KVA_ALLOCATION_COUNT,
            .reservation_count =
                OS_TEST_RESOURCE_SNAPSHOT_KVA_RESERVATION_COUNT,
            .successful_allocation_count = OS_TEST_RESOURCE_SNAPSHOT_EMPTY_COUNT,
            .release_count = OS_TEST_RESOURCE_SNAPSHOT_EMPTY_COUNT,
            .peak_allocated_page_count =
                OS_TEST_RESOURCE_SNAPSHOT_KVA_ALLOCATED_PAGE_COUNT,
            .peak_active_allocation_count =
                OS_TEST_RESOURCE_SNAPSHOT_KVA_ALLOCATION_COUNT,
            .largest_free_range_page_count =
                OS_TEST_RESOURCE_SNAPSHOT_KVA_FREE_PAGE_COUNT,
        },
        os::kernel::KernelStackManagerStatistics{
            .slot_capacity = OS_TEST_RESOURCE_SNAPSHOT_STACK_SLOT_CAPACITY,
            .active_stack_count =
                OS_TEST_RESOURCE_SNAPSHOT_STACK_ACTIVE_COUNT,
            .active_mapped_page_count =
                OS_TEST_RESOURCE_SNAPSHOT_STACK_ACTIVE_COUNT *
                os::kernel::OS_KERNEL_STACK_MAPPED_PAGE_COUNT,
            .active_guard_page_count =
                OS_TEST_RESOURCE_SNAPSHOT_STACK_ACTIVE_COUNT *
                os::kernel::OS_KERNEL_STACK_GUARD_PAGE_COUNT,
            .successful_creation_count = OS_TEST_RESOURCE_SNAPSHOT_EMPTY_COUNT,
            .destruction_count = OS_TEST_RESOURCE_SNAPSHOT_EMPTY_COUNT,
            .peak_active_stack_count =
                OS_TEST_RESOURCE_SNAPSHOT_STACK_ACTIVE_COUNT,
            .peak_active_mapped_page_count =
                OS_TEST_RESOURCE_SNAPSHOT_STACK_ACTIVE_COUNT *
                os::kernel::OS_KERNEL_STACK_MAPPED_PAGE_COUNT,
        },
        os::kernel::ResourceSnapshotSupplementalCounts{},
        snapshot));
    return snapshot;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_RESOURCE_SNAPSHOT_SUITE_NAME};

    uint64_t accumulated_mask = OS_TEST_RESOURCE_SNAPSHOT_EMPTY_COUNT;
    bool field_masks_valid = true;
    for (uint64_t field_index = OS_TEST_RESOURCE_SNAPSHOT_EMPTY_COUNT;
         field_masks_valid &&
         field_index < os::kernel::OS_KERNEL_RESOURCE_SNAPSHOT_TRACKED_FIELD_COUNT;
         ++field_index) {
        const uint64_t field_mask = os::kernel::ResourceSnapshotFieldMask(
            static_cast<os::kernel::ResourceSnapshotField>(field_index));
        field_masks_valid =
            field_mask != OS_TEST_RESOURCE_SNAPSHOT_EMPTY_COUNT &&
            (field_mask & (field_mask - OS_TEST_RESOURCE_SNAPSHOT_SINGLE_UNIT)) ==
                OS_TEST_RESOURCE_SNAPSHOT_EMPTY_COUNT &&
            (accumulated_mask & field_mask) ==
                OS_TEST_RESOURCE_SNAPSHOT_EMPTY_COUNT;
        accumulated_mask |= field_mask;
    }
    field_masks_valid =
        field_masks_valid &&
        accumulated_mask ==
            os::kernel::OS_KERNEL_RESOURCE_SNAPSHOT_ALL_FIELDS_MASK &&
        os::kernel::ResourceSnapshotFieldMask(
            os::kernel::ResourceSnapshotField::Count) ==
            OS_TEST_RESOURCE_SNAPSHOT_EMPTY_COUNT;
    test_context.Expect(field_masks_valid,
                        OS_TEST_RESOURCE_SNAPSHOT_FIELD_MASKS);

    const os::kernel::ResourceSnapshot baseline = CreateValidSnapshot();
    test_context.Expect(
        os::kernel::ValidateResourceSnapshot(baseline) ==
                os::kernel::ResourceSnapshotStatus::Succeeded &&
            baseline.managed_frame_count ==
                OS_TEST_RESOURCE_SNAPSHOT_MANAGED_FRAME_COUNT &&
            baseline.kernel_stack_mapped_page_count ==
                OS_TEST_RESOURCE_SNAPSHOT_STACK_ACTIVE_COUNT *
                    os::kernel::OS_KERNEL_STACK_MAPPED_PAGE_COUNT,
        OS_TEST_RESOURCE_SNAPSHOT_CREATION);

    os::kernel::ResourceSnapshotDifference exact_difference{
        .changed_fields_mask = OS_TEST_RESOURCE_SNAPSHOT_SENTINEL_MASK,
        .changed_field_count = OS_TEST_RESOURCE_SNAPSHOT_SENTINEL_COUNT,
    };
    const bool exact_match_valid =
        os::kernel::CompareResourceSnapshots(
            baseline, baseline, exact_difference) ==
            os::kernel::ResourceSnapshotStatus::Succeeded &&
        exact_difference.changed_fields_mask ==
            OS_TEST_RESOURCE_SNAPSHOT_EMPTY_COUNT &&
        exact_difference.changed_field_count ==
            OS_TEST_RESOURCE_SNAPSHOT_EMPTY_COUNT &&
        os::kernel::ResourceSnapshotsMatch(baseline, baseline);
    test_context.Expect(exact_match_valid,
                        OS_TEST_RESOURCE_SNAPSHOT_EXACT_MATCH);

    os::kernel::ResourceSnapshot changed = baseline;
    --changed.free_frame_count;
    ++changed.allocated_frame_count;
    changed.virtual_address_free_page_count -=
        OS_TEST_RESOURCE_SNAPSHOT_KVA_TRANSFER_PAGE_COUNT;
    changed.virtual_address_allocated_page_count +=
        OS_TEST_RESOURCE_SNAPSHOT_KVA_TRANSFER_PAGE_COUNT;
    ++changed.virtual_address_active_descriptor_count;
    ++changed.virtual_address_active_allocation_count;
    ++changed.kernel_stack_active_count;
    changed.kernel_stack_mapped_page_count +=
        os::kernel::OS_KERNEL_STACK_MAPPED_PAGE_COUNT;
    changed.kernel_stack_guard_page_count +=
        os::kernel::OS_KERNEL_STACK_GUARD_PAGE_COUNT;
    os::kernel::ResourceSnapshotDifference changed_difference{};
    const uint64_t expected_changed_mask =
        os::kernel::ResourceSnapshotFieldMask(
            os::kernel::ResourceSnapshotField::FreeFrameCount) |
        os::kernel::ResourceSnapshotFieldMask(
            os::kernel::ResourceSnapshotField::AllocatedFrameCount) |
        os::kernel::ResourceSnapshotFieldMask(
            os::kernel::ResourceSnapshotField::VirtualAddressFreePageCount) |
        os::kernel::ResourceSnapshotFieldMask(
            os::kernel::ResourceSnapshotField::VirtualAddressAllocatedPageCount) |
        os::kernel::ResourceSnapshotFieldMask(
            os::kernel::ResourceSnapshotField::VirtualAddressActiveDescriptorCount) |
        os::kernel::ResourceSnapshotFieldMask(
            os::kernel::ResourceSnapshotField::VirtualAddressActiveAllocationCount) |
        os::kernel::ResourceSnapshotFieldMask(
            os::kernel::ResourceSnapshotField::KernelStackActiveCount) |
        os::kernel::ResourceSnapshotFieldMask(
            os::kernel::ResourceSnapshotField::KernelStackMappedPageCount) |
        os::kernel::ResourceSnapshotFieldMask(
            os::kernel::ResourceSnapshotField::KernelStackGuardPageCount);
    const bool difference_valid =
        os::kernel::CompareResourceSnapshots(
            baseline, changed, changed_difference) ==
            os::kernel::ResourceSnapshotStatus::Succeeded &&
        changed_difference.changed_fields_mask == expected_changed_mask &&
        changed_difference.changed_field_count ==
            OS_TEST_RESOURCE_SNAPSHOT_EXPECTED_CHANGED_FIELD_COUNT &&
        !os::kernel::ResourceSnapshotsMatch(baseline, changed);
    test_context.Expect(difference_valid,
                        OS_TEST_RESOURCE_SNAPSHOT_DIFFERENCE);

    os::kernel::ResourceSnapshot invalid = baseline;
    invalid.free_frame_count =
        OS_TEST_RESOURCE_SNAPSHOT_MANAGED_FRAME_COUNT;
    os::kernel::ResourceSnapshotDifference preserved_difference{
        .changed_fields_mask = OS_TEST_RESOURCE_SNAPSHOT_SENTINEL_MASK,
        .changed_field_count = OS_TEST_RESOURCE_SNAPSHOT_SENTINEL_COUNT,
    };
    const bool invalid_accounting_valid =
        os::kernel::ValidateResourceSnapshot(invalid) ==
            os::kernel::ResourceSnapshotStatus::InvalidFrameAccounting &&
        os::kernel::CompareResourceSnapshots(
            invalid, baseline, preserved_difference) ==
            os::kernel::ResourceSnapshotStatus::InvalidFrameAccounting &&
        preserved_difference.changed_fields_mask ==
            OS_TEST_RESOURCE_SNAPSHOT_SENTINEL_MASK &&
        preserved_difference.changed_field_count ==
            OS_TEST_RESOURCE_SNAPSHOT_SENTINEL_COUNT;
    test_context.Expect(invalid_accounting_valid,
                        OS_TEST_RESOURCE_SNAPSHOT_INVALID_ACCOUNTING);

    return test_context.ExitCode();
}

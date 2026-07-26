#include "kernel_stack_test_environment.hpp"
#include "os/kernel/memory/resource_snapshot.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_RESOURCE_SNAPSHOT_LIFECYCLE_SUITE_NAME =
    "boot/resource_snapshot_lifecycle/integration";
constexpr std::string_view OS_TEST_RESOURCE_SNAPSHOT_LIFECYCLE_INITIALIZATION =
    "资源快照集成环境必须完成 buddy、页表、KVA 和动态栈暖机";
constexpr std::string_view OS_TEST_RESOURCE_SNAPSHOT_LIFECYCLE_ACTIVE_DIFFERENCE =
    "活动内核栈必须同时改变物理页、KVA 与栈所有权字段";
constexpr std::string_view OS_TEST_RESOURCE_SNAPSHOT_LIFECYCLE_RECLAIM =
    "逆序销毁全部栈后完整资源快照必须精确恢复暖机基线";

constexpr uint64_t OS_TEST_RESOURCE_SNAPSHOT_LIFECYCLE_EMPTY_COUNT = 0ULL;
constexpr uint64_t OS_TEST_RESOURCE_SNAPSHOT_LIFECYCLE_SINGLE_UNIT = 1ULL;
constexpr uint64_t OS_TEST_RESOURCE_SNAPSHOT_LIFECYCLE_STACK_COUNT = 4ULL;
constexpr uint64_t OS_TEST_RESOURCE_SNAPSHOT_LIFECYCLE_SLOT_CAPACITY = 8ULL;
constexpr uint64_t OS_TEST_RESOURCE_SNAPSHOT_LIFECYCLE_WARMUP_SLOT_INDEX =
    OS_TEST_RESOURCE_SNAPSHOT_LIFECYCLE_SLOT_CAPACITY -
    OS_TEST_RESOURCE_SNAPSHOT_LIFECYCLE_SINGLE_UNIT;

[[nodiscard]] os::kernel::ResourceSnapshotStatus
CaptureSnapshot(os::test::KernelStackTestEnvironment &environment,
                os::kernel::ResourceSnapshot &snapshot) noexcept {
    return os::kernel::CreateResourceSnapshot(
        environment.FrameAllocator().Statistics(),
        environment.FrameAllocator().BuddyStatistics(),
        os::kernel::KernelHeapStatistics{},
        environment.VirtualAddressAllocator().Statistics(),
        environment.StackManager().Statistics(),
        os::kernel::ResourceSnapshotSupplementalCounts{}, snapshot);
}

}

int main() {
    os::test::TestContext test_context{
        OS_TEST_RESOURCE_SNAPSHOT_LIFECYCLE_SUITE_NAME};
    static os::test::KernelStackTestEnvironment environment{};
    const bool initialized =
        environment.Initialize(
            OS_TEST_RESOURCE_SNAPSHOT_LIFECYCLE_SLOT_CAPACITY) &&
        environment.StackManager().TryCreate(
            OS_TEST_RESOURCE_SNAPSHOT_LIFECYCLE_WARMUP_SLOT_INDEX) ==
            os::kernel::KernelStackManagerStatus::Succeeded &&
        environment.StackManager().TryDestroy(
            OS_TEST_RESOURCE_SNAPSHOT_LIFECYCLE_WARMUP_SLOT_INDEX) ==
            os::kernel::KernelStackManagerStatus::Succeeded;
    os::kernel::ResourceSnapshot baseline{};
    const bool baseline_valid =
        initialized &&
        CaptureSnapshot(environment, baseline) ==
            os::kernel::ResourceSnapshotStatus::Succeeded;
    test_context.Expect(
        baseline_valid,
        OS_TEST_RESOURCE_SNAPSHOT_LIFECYCLE_INITIALIZATION);

    bool creation_valid = baseline_valid;
    for (uint64_t slot_index =
             OS_TEST_RESOURCE_SNAPSHOT_LIFECYCLE_EMPTY_COUNT;
         creation_valid &&
         slot_index < OS_TEST_RESOURCE_SNAPSHOT_LIFECYCLE_STACK_COUNT;
         ++slot_index) {
        creation_valid =
            environment.StackManager().TryCreate(slot_index) ==
            os::kernel::KernelStackManagerStatus::Succeeded;
    }
    os::kernel::ResourceSnapshot active{};
    os::kernel::ResourceSnapshotDifference active_difference{};
    const uint64_t required_active_mask =
        os::kernel::ResourceSnapshotFieldMask(
            os::kernel::ResourceSnapshotField::FreeFrameCount) |
        os::kernel::ResourceSnapshotFieldMask(
            os::kernel::ResourceSnapshotField::AllocatedFrameCount) |
        os::kernel::ResourceSnapshotFieldMask(
            os::kernel::ResourceSnapshotField::BuddyActiveBlockCount) |
        os::kernel::ResourceSnapshotFieldMask(
            os::kernel::ResourceSnapshotField::VirtualAddressFreePageCount) |
        os::kernel::ResourceSnapshotFieldMask(
            os::kernel::ResourceSnapshotField::VirtualAddressAllocatedPageCount) |
        os::kernel::ResourceSnapshotFieldMask(
            os::kernel::ResourceSnapshotField::VirtualAddressActiveAllocationCount) |
        os::kernel::ResourceSnapshotFieldMask(
            os::kernel::ResourceSnapshotField::KernelStackActiveCount) |
        os::kernel::ResourceSnapshotFieldMask(
            os::kernel::ResourceSnapshotField::KernelStackMappedPageCount) |
        os::kernel::ResourceSnapshotFieldMask(
            os::kernel::ResourceSnapshotField::KernelStackGuardPageCount);
    const bool active_difference_valid =
        creation_valid &&
        CaptureSnapshot(environment, active) ==
            os::kernel::ResourceSnapshotStatus::Succeeded &&
        os::kernel::CompareResourceSnapshots(
            baseline, active, active_difference) ==
            os::kernel::ResourceSnapshotStatus::Succeeded &&
        (active_difference.changed_fields_mask & required_active_mask) ==
            required_active_mask &&
        active.kernel_stack_active_count ==
            OS_TEST_RESOURCE_SNAPSHOT_LIFECYCLE_STACK_COUNT;
    test_context.Expect(
        active_difference_valid,
        OS_TEST_RESOURCE_SNAPSHOT_LIFECYCLE_ACTIVE_DIFFERENCE);

    bool destruction_valid = creation_valid;
    for (uint64_t remaining_stack_count =
             OS_TEST_RESOURCE_SNAPSHOT_LIFECYCLE_STACK_COUNT;
         destruction_valid &&
         remaining_stack_count >
             OS_TEST_RESOURCE_SNAPSHOT_LIFECYCLE_EMPTY_COUNT;
         --remaining_stack_count) {
        destruction_valid =
            environment.StackManager().TryDestroy(
                remaining_stack_count -
                OS_TEST_RESOURCE_SNAPSHOT_LIFECYCLE_SINGLE_UNIT) ==
            os::kernel::KernelStackManagerStatus::Succeeded;
    }
    os::kernel::ResourceSnapshot after{};
    os::kernel::ResourceSnapshotDifference final_difference{};
    const bool reclaim_valid =
        destruction_valid &&
        CaptureSnapshot(environment, after) ==
            os::kernel::ResourceSnapshotStatus::Succeeded &&
        os::kernel::CompareResourceSnapshots(
            baseline, after, final_difference) ==
            os::kernel::ResourceSnapshotStatus::Succeeded &&
        final_difference.changed_fields_mask ==
            OS_TEST_RESOURCE_SNAPSHOT_LIFECYCLE_EMPTY_COUNT &&
        final_difference.changed_field_count ==
            OS_TEST_RESOURCE_SNAPSHOT_LIFECYCLE_EMPTY_COUNT &&
        os::kernel::ResourceSnapshotsMatch(baseline, after) &&
        environment.StackManager().Validate() ==
            os::kernel::KernelStackManagerStatus::Succeeded;
    test_context.Expect(
        reclaim_valid, OS_TEST_RESOURCE_SNAPSHOT_LIFECYCLE_RECLAIM);

    return test_context.ExitCode();
}

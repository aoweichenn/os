#include "os/kernel/memory/user_page_reference.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_USER_PAGE_REFERENCE_SUITE_NAME =
    "kernel/user_page_reference/unit";
constexpr std::string_view OS_TEST_USER_PAGE_REFERENCE_INITIALIZATION =
    "页引用表必须拒绝无效存储、无效容量与重复初始化";
constexpr std::string_view OS_TEST_USER_PAGE_REFERENCE_LIFECYCLE =
    "首次 fork、嵌套 fork、释放与独占恢复必须保持精确引用状态";
constexpr std::string_view OS_TEST_USER_PAGE_REFERENCE_CAPACITY =
    "容量耗尽不得改变已有页的引用计数或统计";
constexpr std::string_view OS_TEST_USER_PAGE_REFERENCE_VALIDATION =
    "一致性检查必须发现外部存储中的重复页和损坏空槽";

constexpr uint64_t OS_TEST_USER_PAGE_REFERENCE_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_USER_PAGE_REFERENCE_SINGLE_UNIT = 1ULL;
constexpr uint64_t OS_TEST_USER_PAGE_REFERENCE_SHARED_COUNT = 2ULL;
constexpr uint64_t OS_TEST_USER_PAGE_REFERENCE_NESTED_COUNT = 3ULL;
constexpr uint64_t OS_TEST_USER_PAGE_REFERENCE_CAPACITY_VALUE = 2ULL;
constexpr uint64_t OS_TEST_USER_PAGE_REFERENCE_PAGE_SIZE_BYTES = 4096ULL;
constexpr uint64_t OS_TEST_USER_PAGE_REFERENCE_FIRST_ADDRESS =
    OS_TEST_USER_PAGE_REFERENCE_PAGE_SIZE_BYTES;
constexpr uint64_t OS_TEST_USER_PAGE_REFERENCE_SECOND_ADDRESS =
    OS_TEST_USER_PAGE_REFERENCE_FIRST_ADDRESS +
    OS_TEST_USER_PAGE_REFERENCE_PAGE_SIZE_BYTES;
constexpr uint64_t OS_TEST_USER_PAGE_REFERENCE_THIRD_ADDRESS =
    OS_TEST_USER_PAGE_REFERENCE_SECOND_ADDRESS +
    OS_TEST_USER_PAGE_REFERENCE_PAGE_SIZE_BYTES;
constexpr uint64_t OS_TEST_USER_PAGE_REFERENCE_MISALIGNED_ADDRESS =
    OS_TEST_USER_PAGE_REFERENCE_FIRST_ADDRESS +
    OS_TEST_USER_PAGE_REFERENCE_SINGLE_UNIT;

}

int main() {
    os::test::TestContext test_context{
        OS_TEST_USER_PAGE_REFERENCE_SUITE_NAME};

    os::kernel::UserPageReferenceManager invalid_manager{};
    os::kernel::UserPageReferenceEntry
        invalid_entries[OS_TEST_USER_PAGE_REFERENCE_CAPACITY_VALUE]{};
    const bool initialization_rejected =
        invalid_manager.Initialize(
            nullptr, OS_TEST_USER_PAGE_REFERENCE_CAPACITY_VALUE,
            OS_TEST_USER_PAGE_REFERENCE_PAGE_SIZE_BYTES) ==
            os::kernel::UserPageReferenceStatus::InvalidStorage &&
        invalid_manager.Initialize(
            invalid_entries,
            OS_TEST_USER_PAGE_REFERENCE_EMPTY_VALUE,
            OS_TEST_USER_PAGE_REFERENCE_PAGE_SIZE_BYTES) ==
            os::kernel::UserPageReferenceStatus::InvalidCapacity &&
        invalid_manager.Initialize(
            invalid_entries,
            OS_TEST_USER_PAGE_REFERENCE_CAPACITY_VALUE,
            OS_TEST_USER_PAGE_REFERENCE_PAGE_SIZE_BYTES) ==
            os::kernel::UserPageReferenceStatus::Succeeded &&
        invalid_manager.Initialize(
            invalid_entries,
            OS_TEST_USER_PAGE_REFERENCE_CAPACITY_VALUE,
            OS_TEST_USER_PAGE_REFERENCE_PAGE_SIZE_BYTES) ==
            os::kernel::UserPageReferenceStatus::AlreadyInitialized;
    test_context.Expect(initialization_rejected,
                        OS_TEST_USER_PAGE_REFERENCE_INITIALIZATION);

    os::kernel::UserPageReferenceEntry
        entries[OS_TEST_USER_PAGE_REFERENCE_CAPACITY_VALUE]{};
    os::kernel::UserPageReferenceManager manager{};
    const bool initialized =
        manager.Initialize(
            entries, OS_TEST_USER_PAGE_REFERENCE_CAPACITY_VALUE,
            OS_TEST_USER_PAGE_REFERENCE_PAGE_SIZE_BYTES) ==
        os::kernel::UserPageReferenceStatus::Succeeded;
    bool first_share = false;
    uint64_t reference_count =
        OS_TEST_USER_PAGE_REFERENCE_EMPTY_VALUE;
    bool release_frame = true;
    const bool lifecycle_valid =
        initialized &&
        manager.RetainForFork(
            OS_TEST_USER_PAGE_REFERENCE_MISALIGNED_ADDRESS,
            first_share) ==
            os::kernel::UserPageReferenceStatus::
                InvalidPhysicalAddress &&
        manager.RetainForFork(
            OS_TEST_USER_PAGE_REFERENCE_FIRST_ADDRESS,
            first_share) ==
            os::kernel::UserPageReferenceStatus::Succeeded &&
        first_share &&
        manager.ReadReferenceCount(
            OS_TEST_USER_PAGE_REFERENCE_FIRST_ADDRESS,
            reference_count) ==
            os::kernel::UserPageReferenceStatus::Succeeded &&
        reference_count ==
            OS_TEST_USER_PAGE_REFERENCE_SHARED_COUNT &&
        manager.RetainForFork(
            OS_TEST_USER_PAGE_REFERENCE_FIRST_ADDRESS,
            first_share) ==
            os::kernel::UserPageReferenceStatus::Succeeded &&
        !first_share &&
        manager.ReadReferenceCount(
            OS_TEST_USER_PAGE_REFERENCE_FIRST_ADDRESS,
            reference_count) ==
            os::kernel::UserPageReferenceStatus::Succeeded &&
        reference_count ==
            OS_TEST_USER_PAGE_REFERENCE_NESTED_COUNT &&
        manager.Release(
            OS_TEST_USER_PAGE_REFERENCE_FIRST_ADDRESS,
            release_frame) ==
            os::kernel::UserPageReferenceStatus::Succeeded &&
        !release_frame &&
        manager.Release(
            OS_TEST_USER_PAGE_REFERENCE_FIRST_ADDRESS,
            release_frame) ==
            os::kernel::UserPageReferenceStatus::Succeeded &&
        !release_frame &&
        manager.RestoreExclusive(
            OS_TEST_USER_PAGE_REFERENCE_FIRST_ADDRESS) ==
            os::kernel::UserPageReferenceStatus::Succeeded &&
        manager.ReadReferenceCount(
            OS_TEST_USER_PAGE_REFERENCE_FIRST_ADDRESS,
            reference_count) ==
            os::kernel::UserPageReferenceStatus::
                ReferenceNotFound &&
        manager.Release(
            OS_TEST_USER_PAGE_REFERENCE_FIRST_ADDRESS,
            release_frame) ==
            os::kernel::UserPageReferenceStatus::Succeeded &&
        release_frame &&
        manager.Validate() ==
            os::kernel::UserPageReferenceStatus::Succeeded;
    test_context.Expect(lifecycle_valid,
                        OS_TEST_USER_PAGE_REFERENCE_LIFECYCLE);

    bool second_first_share = false;
    bool third_first_share = false;
    const bool capacity_valid =
        manager.RetainForFork(
            OS_TEST_USER_PAGE_REFERENCE_FIRST_ADDRESS,
            first_share) ==
            os::kernel::UserPageReferenceStatus::Succeeded &&
        manager.RetainForFork(
            OS_TEST_USER_PAGE_REFERENCE_SECOND_ADDRESS,
            second_first_share) ==
            os::kernel::UserPageReferenceStatus::Succeeded &&
        manager.RetainForFork(
            OS_TEST_USER_PAGE_REFERENCE_THIRD_ADDRESS,
            third_first_share) ==
            os::kernel::UserPageReferenceStatus::
                CapacityExhausted &&
        !third_first_share &&
        manager.ReadReferenceCount(
            OS_TEST_USER_PAGE_REFERENCE_FIRST_ADDRESS,
            reference_count) ==
            os::kernel::UserPageReferenceStatus::Succeeded &&
        reference_count ==
            OS_TEST_USER_PAGE_REFERENCE_SHARED_COUNT &&
        manager.Statistics().active_entry_count ==
            OS_TEST_USER_PAGE_REFERENCE_CAPACITY_VALUE &&
        manager.Statistics().active_reference_count ==
            OS_TEST_USER_PAGE_REFERENCE_CAPACITY_VALUE *
                OS_TEST_USER_PAGE_REFERENCE_SHARED_COUNT &&
        manager.Validate() ==
            os::kernel::UserPageReferenceStatus::Succeeded;
    test_context.Expect(capacity_valid,
                        OS_TEST_USER_PAGE_REFERENCE_CAPACITY);

    entries[OS_TEST_USER_PAGE_REFERENCE_SINGLE_UNIT] =
        entries[OS_TEST_USER_PAGE_REFERENCE_EMPTY_VALUE];
    const bool validation_detected_corruption =
        manager.Validate() ==
        os::kernel::UserPageReferenceStatus::Corrupt;
    test_context.Expect(validation_detected_corruption,
                        OS_TEST_USER_PAGE_REFERENCE_VALIDATION);

    return test_context.ExitCode();
}

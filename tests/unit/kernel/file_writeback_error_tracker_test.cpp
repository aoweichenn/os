#include <os/kernel/memory/file_writeback_error_tracker.hpp>
#include <os/kernel/memory/kernel_heap.hpp>
#include <test_context.hpp>

#include <stdint.h>
#include <string_view>

namespace {

constexpr std::string_view OS_TEST_FILE_WRITEBACK_ERROR_TRACKER_SUITE_NAME =
    "kernel/file_writeback_error_tracker/unit";
constexpr std::string_view OS_TEST_FILE_WRITEBACK_ERROR_TRACKER_REGISTRATION =
    "独立打开实例必须采样当前序列且共享同一文件错误源";
constexpr std::string_view OS_TEST_FILE_WRITEBACK_ERROR_TRACKER_REPORTING =
    "一次写回错误必须相对各打开实例游标报告并允许游标前移";
constexpr std::string_view OS_TEST_FILE_WRITEBACK_ERROR_TRACKER_LIFETIME =
    "最后一个打开实例关闭后必须释放记录且新打开不继承历史错误";

constexpr uint64_t OS_TEST_FILE_WRITEBACK_ERROR_TRACKER_HEAP_SIZE_BYTES = 16ULL * 1024ULL;
constexpr uint64_t OS_TEST_FILE_WRITEBACK_ERROR_TRACKER_HEAP_ALIGNMENT_BYTES = 64ULL;
constexpr uint64_t OS_TEST_FILE_WRITEBACK_ERROR_TRACKER_SUPERBLOCK_IDENTIFIER = 7ULL;
constexpr uint64_t OS_TEST_FILE_WRITEBACK_ERROR_TRACKER_SUPERBLOCK_GENERATION = 3ULL;
constexpr uint64_t OS_TEST_FILE_WRITEBACK_ERROR_TRACKER_FIRST_NODE_IDENTIFIER = 11ULL;
constexpr uint64_t OS_TEST_FILE_WRITEBACK_ERROR_TRACKER_SECOND_NODE_IDENTIFIER = 13ULL;
constexpr uint64_t OS_TEST_FILE_WRITEBACK_ERROR_TRACKER_NODE_GENERATION = 5ULL;

alignas(OS_TEST_FILE_WRITEBACK_ERROR_TRACKER_HEAP_ALIGNMENT_BYTES)
    uint8_t tracker_heap_storage[OS_TEST_FILE_WRITEBACK_ERROR_TRACKER_HEAP_SIZE_BYTES]{};

[[nodiscard]] uint64_t AddressOf(void *const pointer) noexcept {
    return reinterpret_cast<uint64_t>(pointer);
}

[[nodiscard]] os::kernel::FileCacheIdentity MakeIdentity(const uint64_t node_identifier) noexcept {
    return os::kernel::FileCacheIdentity{
        .superblock_identifier = OS_TEST_FILE_WRITEBACK_ERROR_TRACKER_SUPERBLOCK_IDENTIFIER,
        .superblock_generation = OS_TEST_FILE_WRITEBACK_ERROR_TRACKER_SUPERBLOCK_GENERATION,
        .node_identifier = node_identifier,
        .node_generation = OS_TEST_FILE_WRITEBACK_ERROR_TRACKER_NODE_GENERATION,
    };
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_FILE_WRITEBACK_ERROR_TRACKER_SUITE_NAME};

    os::kernel::KernelHeap heap{};
    os::kernel::FileWritebackErrorTracker tracker{};
    const os::kernel::FileCacheIdentity first_identity =
        MakeIdentity(OS_TEST_FILE_WRITEBACK_ERROR_TRACKER_FIRST_NODE_IDENTIFIER);
    const os::kernel::FileCacheIdentity second_identity =
        MakeIdentity(OS_TEST_FILE_WRITEBACK_ERROR_TRACKER_SECOND_NODE_IDENTIFIER);
    uint64_t first_cursor = UINT64_MAX;
    uint64_t duplicate_cursor = UINT64_MAX;
    uint64_t second_cursor = UINT64_MAX;
    const bool registration_valid =
        heap.Initialize(AddressOf(tracker_heap_storage), sizeof(tracker_heap_storage)) ==
            os::kernel::KernelHeapStatus::Succeeded &&
        tracker.Initialize(heap) ==
            os::kernel::FileWritebackErrorTrackerStatus::Succeeded &&
        tracker.Register(os::kernel::FileCacheIdentity{}, first_cursor) ==
            os::kernel::FileWritebackErrorTrackerStatus::InvalidIdentity &&
        tracker.Record(first_identity, os::kernel::FileWritebackError::None) ==
            os::kernel::FileWritebackErrorTrackerStatus::InvalidError &&
        tracker.Register(first_identity, first_cursor) ==
            os::kernel::FileWritebackErrorTrackerStatus::Succeeded &&
        tracker.Register(first_identity, duplicate_cursor) ==
            os::kernel::FileWritebackErrorTrackerStatus::Succeeded &&
        tracker.Register(second_identity, second_cursor) ==
            os::kernel::FileWritebackErrorTrackerStatus::Succeeded &&
        first_cursor == 0ULL && duplicate_cursor == 0ULL && second_cursor == 0ULL &&
        tracker.Statistics().active_record_count == 2ULL &&
        tracker.Statistics().active_open_description_count == 3ULL &&
        tracker.Validate() == os::kernel::FileWritebackErrorTrackerStatus::Succeeded;
    test_context.Expect(registration_valid, OS_TEST_FILE_WRITEBACK_ERROR_TRACKER_REGISTRATION);

    uint64_t current_sequence = UINT64_MAX;
    os::kernel::FileWritebackError observed_error = os::kernel::FileWritebackError::None;
    const bool reporting_valid =
        tracker.Record(first_identity, os::kernel::FileWritebackError::InputOutput) ==
            os::kernel::FileWritebackErrorTrackerStatus::Succeeded &&
        tracker.Check(first_identity, first_cursor, current_sequence, observed_error) ==
            os::kernel::FileWritebackErrorTrackerStatus::Succeeded &&
        current_sequence == 1ULL &&
        observed_error == os::kernel::FileWritebackError::InputOutput &&
        tracker.Check(first_identity, current_sequence, first_cursor, observed_error) ==
            os::kernel::FileWritebackErrorTrackerStatus::Succeeded &&
        first_cursor == 1ULL && observed_error == os::kernel::FileWritebackError::None &&
        tracker.Check(first_identity, duplicate_cursor, current_sequence, observed_error) ==
            os::kernel::FileWritebackErrorTrackerStatus::Succeeded &&
        current_sequence == 1ULL &&
        observed_error == os::kernel::FileWritebackError::InputOutput &&
        tracker.Statistics().recorded_error_count == 1ULL &&
        tracker.Statistics().reported_error_count == 2ULL;
    test_context.Expect(reporting_valid, OS_TEST_FILE_WRITEBACK_ERROR_TRACKER_REPORTING);

    uint64_t reopened_cursor = UINT64_MAX;
    const bool lifetime_valid =
        tracker.Unregister(first_identity) ==
            os::kernel::FileWritebackErrorTrackerStatus::Succeeded &&
        tracker.Unregister(first_identity) ==
            os::kernel::FileWritebackErrorTrackerStatus::Succeeded &&
        tracker.Unregister(second_identity) ==
            os::kernel::FileWritebackErrorTrackerStatus::Succeeded &&
        tracker.Statistics().active_record_count == 0ULL &&
        tracker.Statistics().active_open_description_count == 0ULL &&
        tracker.Register(first_identity, reopened_cursor) ==
            os::kernel::FileWritebackErrorTrackerStatus::Succeeded &&
        reopened_cursor == 0ULL &&
        tracker.Unregister(first_identity) ==
            os::kernel::FileWritebackErrorTrackerStatus::Succeeded &&
        tracker.Validate() == os::kernel::FileWritebackErrorTrackerStatus::Succeeded &&
        tracker.Destroy() == os::kernel::FileWritebackErrorTrackerStatus::Succeeded &&
        heap.Validate() == os::kernel::KernelHeapStatus::Succeeded &&
        heap.Statistics().allocation_count == 0ULL;
    test_context.Expect(lifetime_valid, OS_TEST_FILE_WRITEBACK_ERROR_TRACKER_LIFETIME);

    return test_context.ExitCode();
}

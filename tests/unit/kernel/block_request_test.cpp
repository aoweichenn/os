#include "os/kernel/device/block_request.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_BLOCK_REQUEST_SUITE_NAME =
    "kernel/block_request/unit";
constexpr std::string_view OS_TEST_BLOCK_REQUEST_VALIDATION =
    "块请求必须拒绝非法缓冲区、长度、LBA、截止时间和操作类型";
constexpr std::string_view OS_TEST_BLOCK_REQUEST_FIFO =
    "块请求队列必须按 FIFO 顺序单飞签发";
constexpr std::string_view OS_TEST_BLOCK_REQUEST_RESOLUTION =
    "已完成请求只能由完成、超时或取消中的一个路径解析";
constexpr std::string_view OS_TEST_BLOCK_REQUEST_CAPACITY_LIFECYCLE =
    "容量耗尽必须显式拒绝且回收后槽位可复用";

constexpr uint64_t OS_TEST_BLOCK_REQUEST_CAPACITY = 3ULL;
constexpr uint64_t OS_TEST_BLOCK_REQUEST_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_BLOCK_REQUEST_SINGLE_UNIT = 1ULL;
constexpr uint64_t OS_TEST_BLOCK_REQUEST_FIRST_LBA = 11ULL;
constexpr uint64_t OS_TEST_BLOCK_REQUEST_SECOND_LBA = 12ULL;
constexpr uint64_t OS_TEST_BLOCK_REQUEST_FIRST_DEADLINE_NS = 100ULL;
constexpr uint64_t OS_TEST_BLOCK_REQUEST_SECOND_DEADLINE_NS = 200ULL;
constexpr uint64_t OS_TEST_BLOCK_REQUEST_OWNER_THREAD_INDEX = 7ULL;
constexpr uint64_t OS_TEST_BLOCK_REQUEST_BEFORE_DEADLINE_NS = 99ULL;
constexpr uint64_t OS_TEST_BLOCK_REQUEST_AT_DEADLINE_NS = 100ULL;

}

int main() {
    os::test::TestContext test_context{OS_TEST_BLOCK_REQUEST_SUITE_NAME};
    os::kernel::BlockRequest storage[OS_TEST_BLOCK_REQUEST_CAPACITY]{};
    os::kernel::BlockRequestQueue queue{};
    uint8_t sector[os::kernel::OS_KERNEL_DEVICE_ATA_SECTOR_SIZE_BYTES]{};
    uint64_t request_identifier = OS_TEST_BLOCK_REQUEST_EMPTY_VALUE;

    const bool validation_passed =
        queue.Initialize(storage, OS_TEST_BLOCK_REQUEST_CAPACITY) ==
            os::kernel::BlockRequestQueueStatus::Succeeded &&
        queue.Submit(os::kernel::BlockOperation::Read, OS_TEST_BLOCK_REQUEST_FIRST_LBA, nullptr,
                     os::kernel::OS_KERNEL_DEVICE_ATA_SECTOR_SIZE_BYTES,
                     OS_TEST_BLOCK_REQUEST_OWNER_THREAD_INDEX,
                     OS_TEST_BLOCK_REQUEST_FIRST_DEADLINE_NS, request_identifier) ==
            os::kernel::BlockRequestQueueStatus::InvalidRequest &&
        queue.Submit(os::kernel::BlockOperation::Read, OS_TEST_BLOCK_REQUEST_FIRST_LBA, sector,
                     os::kernel::OS_KERNEL_DEVICE_ATA_SECTOR_SIZE_BYTES -
                         OS_TEST_BLOCK_REQUEST_SINGLE_UNIT,
                     OS_TEST_BLOCK_REQUEST_OWNER_THREAD_INDEX,
                     OS_TEST_BLOCK_REQUEST_FIRST_DEADLINE_NS, request_identifier) ==
            os::kernel::BlockRequestQueueStatus::InvalidRequest &&
        queue.Submit(os::kernel::BlockOperation::Write,
                     os::kernel::OS_KERNEL_DEVICE_ATA_MAXIMUM_LBA28 +
                         OS_TEST_BLOCK_REQUEST_SINGLE_UNIT,
                     sector,
                     os::kernel::OS_KERNEL_DEVICE_ATA_SECTOR_SIZE_BYTES,
                     OS_TEST_BLOCK_REQUEST_OWNER_THREAD_INDEX,
                     OS_TEST_BLOCK_REQUEST_FIRST_DEADLINE_NS, request_identifier) ==
            os::kernel::BlockRequestQueueStatus::InvalidRequest &&
        queue.Submit(static_cast<os::kernel::BlockOperation>(UINT64_MAX),
                     OS_TEST_BLOCK_REQUEST_FIRST_LBA, sector,
                     os::kernel::OS_KERNEL_DEVICE_ATA_SECTOR_SIZE_BYTES,
                     OS_TEST_BLOCK_REQUEST_OWNER_THREAD_INDEX,
                     OS_TEST_BLOCK_REQUEST_FIRST_DEADLINE_NS, request_identifier) ==
            os::kernel::BlockRequestQueueStatus::InvalidRequest &&
        queue.Submit(os::kernel::BlockOperation::Flush, OS_TEST_BLOCK_REQUEST_EMPTY_VALUE, nullptr,
                     OS_TEST_BLOCK_REQUEST_EMPTY_VALUE,
                     OS_TEST_BLOCK_REQUEST_OWNER_THREAD_INDEX,
                     OS_TEST_BLOCK_REQUEST_EMPTY_VALUE, request_identifier) ==
            os::kernel::BlockRequestQueueStatus::InvalidRequest;
    test_context.Expect(validation_passed, OS_TEST_BLOCK_REQUEST_VALIDATION);

    uint64_t first_identifier = OS_TEST_BLOCK_REQUEST_EMPTY_VALUE;
    uint64_t second_identifier = OS_TEST_BLOCK_REQUEST_EMPTY_VALUE;
    os::kernel::BlockRequest issued_request{};
    bool issued = false;
    const bool fifo_passed =
        queue.Submit(os::kernel::BlockOperation::Read, OS_TEST_BLOCK_REQUEST_FIRST_LBA, sector,
                     os::kernel::OS_KERNEL_DEVICE_ATA_SECTOR_SIZE_BYTES,
                     OS_TEST_BLOCK_REQUEST_OWNER_THREAD_INDEX,
                     OS_TEST_BLOCK_REQUEST_FIRST_DEADLINE_NS, first_identifier) ==
            os::kernel::BlockRequestQueueStatus::Succeeded &&
        queue.Submit(os::kernel::BlockOperation::Write, OS_TEST_BLOCK_REQUEST_SECOND_LBA, sector,
                     os::kernel::OS_KERNEL_DEVICE_ATA_SECTOR_SIZE_BYTES,
                     OS_TEST_BLOCK_REQUEST_OWNER_THREAD_INDEX,
                     OS_TEST_BLOCK_REQUEST_SECOND_DEADLINE_NS, second_identifier) ==
            os::kernel::BlockRequestQueueStatus::Succeeded &&
        queue.IssueNext(issued_request, issued) ==
            os::kernel::BlockRequestQueueStatus::Succeeded &&
        issued && issued_request.identifier == first_identifier &&
        queue.IssueNext(issued_request, issued) ==
            os::kernel::BlockRequestQueueStatus::Succeeded &&
        !issued;
    test_context.Expect(fifo_passed, OS_TEST_BLOCK_REQUEST_FIFO);

    os::kernel::BlockRequest timed_out_request{};
    bool resolved = false;
    const bool resolution_passed =
        queue.ResolveTimeout(OS_TEST_BLOCK_REQUEST_BEFORE_DEADLINE_NS, timed_out_request,
                             resolved) ==
            os::kernel::BlockRequestQueueStatus::Succeeded &&
        !resolved &&
        queue.ResolveTimeout(OS_TEST_BLOCK_REQUEST_AT_DEADLINE_NS, timed_out_request, resolved) ==
            os::kernel::BlockRequestQueueStatus::Succeeded &&
        resolved && timed_out_request.identifier == first_identifier &&
        timed_out_request.state == os::kernel::BlockRequestState::Completed &&
        timed_out_request.result == os::kernel::BlockRequestResult::TimedOut &&
        queue.Complete(first_identifier, os::kernel::BlockRequestResult::Succeeded) ==
            os::kernel::BlockRequestQueueStatus::RequestAlreadyResolved &&
        queue.CancelQueued(second_identifier) ==
            os::kernel::BlockRequestQueueStatus::Succeeded &&
        queue.CancelQueued(second_identifier) ==
            os::kernel::BlockRequestQueueStatus::RequestAlreadyResolved;
    test_context.Expect(resolution_passed, OS_TEST_BLOCK_REQUEST_RESOLUTION);

    const bool capacity_passed =
        queue.Reap(first_identifier) == os::kernel::BlockRequestQueueStatus::Succeeded &&
        queue.Reap(second_identifier) == os::kernel::BlockRequestQueueStatus::Succeeded &&
        queue.Submit(os::kernel::BlockOperation::Flush, OS_TEST_BLOCK_REQUEST_EMPTY_VALUE, nullptr,
                     OS_TEST_BLOCK_REQUEST_EMPTY_VALUE,
                     OS_TEST_BLOCK_REQUEST_OWNER_THREAD_INDEX,
                     OS_TEST_BLOCK_REQUEST_FIRST_DEADLINE_NS, first_identifier) ==
            os::kernel::BlockRequestQueueStatus::Succeeded &&
        queue.Submit(os::kernel::BlockOperation::Flush, OS_TEST_BLOCK_REQUEST_EMPTY_VALUE, nullptr,
                     OS_TEST_BLOCK_REQUEST_EMPTY_VALUE,
                     OS_TEST_BLOCK_REQUEST_OWNER_THREAD_INDEX,
                     OS_TEST_BLOCK_REQUEST_FIRST_DEADLINE_NS, second_identifier) ==
            os::kernel::BlockRequestQueueStatus::Succeeded &&
        queue.Submit(os::kernel::BlockOperation::Flush, OS_TEST_BLOCK_REQUEST_EMPTY_VALUE, nullptr,
                     OS_TEST_BLOCK_REQUEST_EMPTY_VALUE,
                     OS_TEST_BLOCK_REQUEST_OWNER_THREAD_INDEX,
                     OS_TEST_BLOCK_REQUEST_FIRST_DEADLINE_NS, request_identifier) ==
            os::kernel::BlockRequestQueueStatus::Succeeded &&
        queue.Submit(os::kernel::BlockOperation::Flush, OS_TEST_BLOCK_REQUEST_EMPTY_VALUE, nullptr,
                     OS_TEST_BLOCK_REQUEST_EMPTY_VALUE,
                     OS_TEST_BLOCK_REQUEST_OWNER_THREAD_INDEX,
                     OS_TEST_BLOCK_REQUEST_FIRST_DEADLINE_NS, request_identifier) ==
            os::kernel::BlockRequestQueueStatus::CapacityExhausted &&
        queue.Validate() == os::kernel::BlockRequestQueueStatus::Succeeded &&
        queue.Statistics().capacity_rejection_count ==
            OS_TEST_BLOCK_REQUEST_SINGLE_UNIT;
    test_context.Expect(capacity_passed, OS_TEST_BLOCK_REQUEST_CAPACITY_LIFECYCLE);
    return test_context.ExitCode();
}

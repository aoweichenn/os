#include <os/kernel/device/block_request.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_BLOCK_REQUEST_SUITE_NAME =
    "kernel/block_request/unit";
constexpr std::string_view OS_TEST_BLOCK_REQUEST_GEOMETRY =
    "块请求队列必须拒绝不完整或超出队列容量的设备几何";
constexpr std::string_view OS_TEST_BLOCK_REQUEST_VALIDATION =
    "块请求必须依据设备几何拒绝非法缓冲区、范围、能力和截止时间";
constexpr std::string_view OS_TEST_BLOCK_REQUEST_CONCURRENCY =
    "块请求队列必须允许设备深度内并发签发并支持乱序完成";
constexpr std::string_view OS_TEST_BLOCK_REQUEST_RESOLUTION =
    "并发请求的完成、超时和取消仍必须只有一个解析路径获胜";
constexpr std::string_view OS_TEST_BLOCK_REQUEST_COMPLETION_DELIVERY =
    "完成通知必须按解析顺序交付完整所有者和请求结果且空队列不伪造事件";
constexpr std::string_view OS_TEST_BLOCK_REQUEST_CAPACITY_LIFECYCLE =
    "容量耗尽必须显式拒绝且回收后槽位可复用";

constexpr uint64_t OS_TEST_BLOCK_REQUEST_CAPACITY = 3ULL;
constexpr uint64_t OS_TEST_BLOCK_REQUEST_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_BLOCK_REQUEST_SINGLE_UNIT = 1ULL;
constexpr uint64_t OS_TEST_BLOCK_REQUEST_LOGICAL_BLOCK_SIZE_BYTES = 4096ULL;
constexpr uint64_t OS_TEST_BLOCK_REQUEST_LOGICAL_BLOCK_COUNT = 128ULL;
constexpr uint64_t OS_TEST_BLOCK_REQUEST_MAXIMUM_TRANSFER_BLOCK_COUNT = 4ULL;
constexpr uint64_t OS_TEST_BLOCK_REQUEST_MAXIMUM_OUTSTANDING_REQUEST_COUNT = 2ULL;
constexpr uint64_t OS_TEST_BLOCK_REQUEST_FIRST_LBA = 11ULL;
constexpr uint64_t OS_TEST_BLOCK_REQUEST_SECOND_LBA = 12ULL;
constexpr uint64_t OS_TEST_BLOCK_REQUEST_LAST_LBA =
    OS_TEST_BLOCK_REQUEST_LOGICAL_BLOCK_COUNT - OS_TEST_BLOCK_REQUEST_SINGLE_UNIT;
constexpr uint64_t OS_TEST_BLOCK_REQUEST_FIRST_TRANSFER_BLOCK_COUNT = 2ULL;
constexpr uint64_t OS_TEST_BLOCK_REQUEST_FIRST_DEADLINE_NS = 100ULL;
constexpr uint64_t OS_TEST_BLOCK_REQUEST_SECOND_DEADLINE_NS = 200ULL;
constexpr uint64_t OS_TEST_BLOCK_REQUEST_THIRD_DEADLINE_NS = 150ULL;
constexpr uint64_t OS_TEST_BLOCK_REQUEST_OWNER_THREAD_INDEX = 7ULL;
constexpr uint64_t OS_TEST_BLOCK_REQUEST_BEFORE_DEADLINE_NS = 99ULL;
constexpr uint64_t OS_TEST_BLOCK_REQUEST_AT_DEADLINE_NS = 100ULL;
constexpr uint64_t OS_TEST_BLOCK_REQUEST_EXPECTED_COMPLETION_DELIVERY_COUNT = 3ULL;

constexpr os::kernel::BlockDeviceGeometry OS_TEST_BLOCK_REQUEST_GEOMETRY_VALUE{
    .logical_block_size_bytes = OS_TEST_BLOCK_REQUEST_LOGICAL_BLOCK_SIZE_BYTES,
    .logical_block_count = OS_TEST_BLOCK_REQUEST_LOGICAL_BLOCK_COUNT,
    .maximum_transfer_block_count = OS_TEST_BLOCK_REQUEST_MAXIMUM_TRANSFER_BLOCK_COUNT,
    .maximum_outstanding_request_count =
        OS_TEST_BLOCK_REQUEST_MAXIMUM_OUTSTANDING_REQUEST_COUNT,
    .write_supported = true,
    .flush_supported = true,
};
constexpr os::kernel::BlockDeviceGeometry OS_TEST_BLOCK_REQUEST_READ_ONLY_GEOMETRY{
    .logical_block_size_bytes = OS_TEST_BLOCK_REQUEST_LOGICAL_BLOCK_SIZE_BYTES,
    .logical_block_count = OS_TEST_BLOCK_REQUEST_LOGICAL_BLOCK_COUNT,
    .maximum_transfer_block_count = OS_TEST_BLOCK_REQUEST_SINGLE_UNIT,
    .maximum_outstanding_request_count = OS_TEST_BLOCK_REQUEST_SINGLE_UNIT,
    .write_supported = false,
    .flush_supported = false,
};

[[nodiscard]] bool TakeExpectedCompletion(
    os::kernel::BlockRequestQueue &queue, const uint64_t request_identifier,
    const os::kernel::BlockOperation operation, const uint64_t logical_block_address,
    const uint64_t logical_block_count, const os::kernel::BlockRequestResult result) noexcept {
    os::kernel::BlockCompletion completion{};
    bool available = false;
    return queue.TakeCompletion(completion, available) ==
               os::kernel::BlockRequestQueueStatus::Succeeded &&
           available && completion.request_identifier == request_identifier &&
           completion.operation == operation &&
           completion.logical_block_address == logical_block_address &&
           completion.logical_block_count == logical_block_count &&
           completion.owner_thread_index == OS_TEST_BLOCK_REQUEST_OWNER_THREAD_INDEX &&
           completion.result == result;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_BLOCK_REQUEST_SUITE_NAME};
    os::kernel::BlockRequest storage[OS_TEST_BLOCK_REQUEST_CAPACITY]{};
    os::kernel::BlockRequestQueue queue{};
    uint8_t transfer_buffer[OS_TEST_BLOCK_REQUEST_LOGICAL_BLOCK_SIZE_BYTES *
                            OS_TEST_BLOCK_REQUEST_MAXIMUM_TRANSFER_BLOCK_COUNT]{};
    uint64_t request_identifier = OS_TEST_BLOCK_REQUEST_EMPTY_VALUE;
    os::kernel::BlockRequest read_only_storage[OS_TEST_BLOCK_REQUEST_SINGLE_UNIT]{};
    os::kernel::BlockRequestQueue read_only_queue{};
    uint64_t read_only_identifier = OS_TEST_BLOCK_REQUEST_EMPTY_VALUE;

    os::kernel::BlockDeviceGeometry invalid_geometry = OS_TEST_BLOCK_REQUEST_GEOMETRY_VALUE;
    invalid_geometry.logical_block_size_bytes = OS_TEST_BLOCK_REQUEST_EMPTY_VALUE;
    os::kernel::BlockRequestQueue invalid_queue{};
    const bool geometry_passed =
        invalid_queue.Initialize(storage, OS_TEST_BLOCK_REQUEST_CAPACITY, invalid_geometry) ==
            os::kernel::BlockRequestQueueStatus::InvalidGeometry &&
        invalid_queue.Initialize(
            storage, OS_TEST_BLOCK_REQUEST_CAPACITY,
            os::kernel::BlockDeviceGeometry{
                .logical_block_size_bytes = OS_TEST_BLOCK_REQUEST_LOGICAL_BLOCK_SIZE_BYTES,
                .logical_block_count = OS_TEST_BLOCK_REQUEST_LOGICAL_BLOCK_COUNT,
                .maximum_transfer_block_count =
                    OS_TEST_BLOCK_REQUEST_MAXIMUM_TRANSFER_BLOCK_COUNT,
                .maximum_outstanding_request_count = OS_TEST_BLOCK_REQUEST_CAPACITY +
                                                       OS_TEST_BLOCK_REQUEST_SINGLE_UNIT,
                .write_supported = true,
                .flush_supported = true,
            }) == os::kernel::BlockRequestQueueStatus::InvalidGeometry &&
        queue.Initialize(storage, OS_TEST_BLOCK_REQUEST_CAPACITY,
                         OS_TEST_BLOCK_REQUEST_GEOMETRY_VALUE) ==
            os::kernel::BlockRequestQueueStatus::Succeeded;
    test_context.Expect(geometry_passed, OS_TEST_BLOCK_REQUEST_GEOMETRY);

    const bool validation_passed =
        read_only_queue.Initialize(read_only_storage, OS_TEST_BLOCK_REQUEST_SINGLE_UNIT,
                                   OS_TEST_BLOCK_REQUEST_READ_ONLY_GEOMETRY) ==
            os::kernel::BlockRequestQueueStatus::Succeeded &&
        read_only_queue.Submit(
            os::kernel::BlockOperation::Write, OS_TEST_BLOCK_REQUEST_FIRST_LBA, transfer_buffer,
            OS_TEST_BLOCK_REQUEST_LOGICAL_BLOCK_SIZE_BYTES,
            OS_TEST_BLOCK_REQUEST_OWNER_THREAD_INDEX, OS_TEST_BLOCK_REQUEST_FIRST_DEADLINE_NS,
            read_only_identifier) == os::kernel::BlockRequestQueueStatus::InvalidRequest &&
        read_only_queue.Submit(
            os::kernel::BlockOperation::Flush, OS_TEST_BLOCK_REQUEST_EMPTY_VALUE, nullptr,
            OS_TEST_BLOCK_REQUEST_EMPTY_VALUE, OS_TEST_BLOCK_REQUEST_OWNER_THREAD_INDEX,
            OS_TEST_BLOCK_REQUEST_FIRST_DEADLINE_NS, read_only_identifier) ==
            os::kernel::BlockRequestQueueStatus::InvalidRequest &&
        read_only_queue.Submit(
            os::kernel::BlockOperation::Read, OS_TEST_BLOCK_REQUEST_FIRST_LBA, transfer_buffer,
            OS_TEST_BLOCK_REQUEST_LOGICAL_BLOCK_SIZE_BYTES,
            OS_TEST_BLOCK_REQUEST_OWNER_THREAD_INDEX, OS_TEST_BLOCK_REQUEST_FIRST_DEADLINE_NS,
            read_only_identifier) == os::kernel::BlockRequestQueueStatus::Succeeded &&
        read_only_queue.CancelQueued(read_only_identifier) ==
            os::kernel::BlockRequestQueueStatus::Succeeded &&
        TakeExpectedCompletion(read_only_queue, read_only_identifier,
                               os::kernel::BlockOperation::Read,
                               OS_TEST_BLOCK_REQUEST_FIRST_LBA,
                               OS_TEST_BLOCK_REQUEST_SINGLE_UNIT,
                               os::kernel::BlockRequestResult::Cancelled) &&
        queue.Submit(os::kernel::BlockOperation::Read, OS_TEST_BLOCK_REQUEST_FIRST_LBA, nullptr,
                     OS_TEST_BLOCK_REQUEST_LOGICAL_BLOCK_SIZE_BYTES,
                     OS_TEST_BLOCK_REQUEST_OWNER_THREAD_INDEX,
                     OS_TEST_BLOCK_REQUEST_FIRST_DEADLINE_NS, request_identifier) ==
            os::kernel::BlockRequestQueueStatus::InvalidRequest &&
        queue.Submit(os::kernel::BlockOperation::Read, OS_TEST_BLOCK_REQUEST_FIRST_LBA,
                     transfer_buffer, OS_TEST_BLOCK_REQUEST_LOGICAL_BLOCK_SIZE_BYTES -
                                          OS_TEST_BLOCK_REQUEST_SINGLE_UNIT,
                     OS_TEST_BLOCK_REQUEST_OWNER_THREAD_INDEX,
                     OS_TEST_BLOCK_REQUEST_FIRST_DEADLINE_NS, request_identifier) ==
            os::kernel::BlockRequestQueueStatus::InvalidRequest &&
        queue.Submit(os::kernel::BlockOperation::Read, OS_TEST_BLOCK_REQUEST_FIRST_LBA,
                     transfer_buffer,
                     OS_TEST_BLOCK_REQUEST_LOGICAL_BLOCK_SIZE_BYTES *
                         (OS_TEST_BLOCK_REQUEST_MAXIMUM_TRANSFER_BLOCK_COUNT +
                          OS_TEST_BLOCK_REQUEST_SINGLE_UNIT),
                     OS_TEST_BLOCK_REQUEST_OWNER_THREAD_INDEX,
                     OS_TEST_BLOCK_REQUEST_FIRST_DEADLINE_NS, request_identifier) ==
            os::kernel::BlockRequestQueueStatus::InvalidRequest &&
        queue.Submit(os::kernel::BlockOperation::Write, OS_TEST_BLOCK_REQUEST_LAST_LBA,
                     transfer_buffer,
                     OS_TEST_BLOCK_REQUEST_LOGICAL_BLOCK_SIZE_BYTES *
                         OS_TEST_BLOCK_REQUEST_FIRST_TRANSFER_BLOCK_COUNT,
                     OS_TEST_BLOCK_REQUEST_OWNER_THREAD_INDEX,
                     OS_TEST_BLOCK_REQUEST_FIRST_DEADLINE_NS, request_identifier) ==
            os::kernel::BlockRequestQueueStatus::InvalidRequest &&
        queue.Submit(static_cast<os::kernel::BlockOperation>(UINT64_MAX),
                     OS_TEST_BLOCK_REQUEST_FIRST_LBA, transfer_buffer,
                     OS_TEST_BLOCK_REQUEST_LOGICAL_BLOCK_SIZE_BYTES,
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
    uint64_t third_identifier = OS_TEST_BLOCK_REQUEST_EMPTY_VALUE;
    os::kernel::BlockRequest issued_request{};
    bool issued = false;
    const bool concurrency_passed =
        queue.Submit(os::kernel::BlockOperation::Read, OS_TEST_BLOCK_REQUEST_FIRST_LBA,
                     transfer_buffer,
                     OS_TEST_BLOCK_REQUEST_LOGICAL_BLOCK_SIZE_BYTES *
                         OS_TEST_BLOCK_REQUEST_FIRST_TRANSFER_BLOCK_COUNT,
                     OS_TEST_BLOCK_REQUEST_OWNER_THREAD_INDEX,
                     OS_TEST_BLOCK_REQUEST_FIRST_DEADLINE_NS, first_identifier) ==
            os::kernel::BlockRequestQueueStatus::Succeeded &&
        queue.Submit(os::kernel::BlockOperation::Write, OS_TEST_BLOCK_REQUEST_SECOND_LBA,
                     transfer_buffer, OS_TEST_BLOCK_REQUEST_LOGICAL_BLOCK_SIZE_BYTES,
                     OS_TEST_BLOCK_REQUEST_OWNER_THREAD_INDEX,
                     OS_TEST_BLOCK_REQUEST_SECOND_DEADLINE_NS, second_identifier) ==
            os::kernel::BlockRequestQueueStatus::Succeeded &&
        queue.Submit(os::kernel::BlockOperation::Flush, OS_TEST_BLOCK_REQUEST_EMPTY_VALUE, nullptr,
                     OS_TEST_BLOCK_REQUEST_EMPTY_VALUE,
                     OS_TEST_BLOCK_REQUEST_OWNER_THREAD_INDEX,
                     OS_TEST_BLOCK_REQUEST_THIRD_DEADLINE_NS, third_identifier) ==
            os::kernel::BlockRequestQueueStatus::Succeeded &&
        queue.IssueNext(issued_request, issued) ==
            os::kernel::BlockRequestQueueStatus::Succeeded &&
        issued && issued_request.identifier == first_identifier &&
        issued_request.logical_block_count ==
            OS_TEST_BLOCK_REQUEST_FIRST_TRANSFER_BLOCK_COUNT &&
        queue.IssueNext(issued_request, issued) ==
            os::kernel::BlockRequestQueueStatus::Succeeded &&
        issued && issued_request.identifier == second_identifier &&
        queue.IssueNext(issued_request, issued) ==
            os::kernel::BlockRequestQueueStatus::Succeeded &&
        !issued &&
        queue.Complete(second_identifier, os::kernel::BlockRequestResult::Succeeded) ==
            os::kernel::BlockRequestQueueStatus::Succeeded &&
        queue.IssueNext(issued_request, issued) ==
            os::kernel::BlockRequestQueueStatus::Succeeded &&
        issued && issued_request.identifier == third_identifier &&
        issued_request.logical_block_count == OS_TEST_BLOCK_REQUEST_EMPTY_VALUE &&
        queue.Statistics().peak_issued_request_count ==
            OS_TEST_BLOCK_REQUEST_MAXIMUM_OUTSTANDING_REQUEST_COUNT;
    test_context.Expect(concurrency_passed, OS_TEST_BLOCK_REQUEST_CONCURRENCY);

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
        queue.CancelQueued(third_identifier) ==
            os::kernel::BlockRequestQueueStatus::RequestNotQueued &&
        queue.Complete(third_identifier,
                       static_cast<os::kernel::BlockRequestResult>(UINT64_MAX)) ==
            os::kernel::BlockRequestQueueStatus::InvalidRequest &&
        queue.Complete(third_identifier, os::kernel::BlockRequestResult::DeviceError) ==
            os::kernel::BlockRequestQueueStatus::Succeeded &&
        queue.Validate() == os::kernel::BlockRequestQueueStatus::Succeeded;
    test_context.Expect(resolution_passed, OS_TEST_BLOCK_REQUEST_RESOLUTION);

    os::kernel::BlockCompletion no_completion{};
    bool completion_available = true;
    const bool completion_delivery_passed =
        TakeExpectedCompletion(queue, second_identifier, os::kernel::BlockOperation::Write,
                               OS_TEST_BLOCK_REQUEST_SECOND_LBA,
                               OS_TEST_BLOCK_REQUEST_SINGLE_UNIT,
                               os::kernel::BlockRequestResult::Succeeded) &&
        TakeExpectedCompletion(queue, first_identifier, os::kernel::BlockOperation::Read,
                               OS_TEST_BLOCK_REQUEST_FIRST_LBA,
                               OS_TEST_BLOCK_REQUEST_FIRST_TRANSFER_BLOCK_COUNT,
                               os::kernel::BlockRequestResult::TimedOut) &&
        TakeExpectedCompletion(queue, third_identifier, os::kernel::BlockOperation::Flush,
                               OS_TEST_BLOCK_REQUEST_EMPTY_VALUE,
                               OS_TEST_BLOCK_REQUEST_EMPTY_VALUE,
                               os::kernel::BlockRequestResult::DeviceError) &&
        queue.TakeCompletion(no_completion, completion_available) ==
            os::kernel::BlockRequestQueueStatus::Succeeded &&
        !completion_available &&
        no_completion.request_identifier == OS_TEST_BLOCK_REQUEST_EMPTY_VALUE;
    test_context.Expect(completion_delivery_passed, OS_TEST_BLOCK_REQUEST_COMPLETION_DELIVERY);

    const bool capacity_passed =
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
                     OS_TEST_BLOCK_REQUEST_FIRST_DEADLINE_NS, third_identifier) ==
            os::kernel::BlockRequestQueueStatus::Succeeded &&
        queue.Submit(os::kernel::BlockOperation::Flush, OS_TEST_BLOCK_REQUEST_EMPTY_VALUE, nullptr,
                     OS_TEST_BLOCK_REQUEST_EMPTY_VALUE,
                     OS_TEST_BLOCK_REQUEST_OWNER_THREAD_INDEX,
                     OS_TEST_BLOCK_REQUEST_FIRST_DEADLINE_NS, request_identifier) ==
            os::kernel::BlockRequestQueueStatus::CapacityExhausted &&
        queue.Validate() == os::kernel::BlockRequestQueueStatus::Succeeded &&
        queue.Statistics().capacity_rejection_count == OS_TEST_BLOCK_REQUEST_SINGLE_UNIT &&
        queue.Statistics().completion_delivery_count ==
            OS_TEST_BLOCK_REQUEST_EXPECTED_COMPLETION_DELIVERY_COUNT &&
        queue.Statistics().geometry.logical_block_size_bytes ==
            OS_TEST_BLOCK_REQUEST_LOGICAL_BLOCK_SIZE_BYTES;
    test_context.Expect(capacity_passed, OS_TEST_BLOCK_REQUEST_CAPACITY_LIFECYCLE);
    return test_context.ExitCode();
}

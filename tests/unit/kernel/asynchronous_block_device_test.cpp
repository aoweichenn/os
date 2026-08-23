#include <asynchronous_block_device_test_driver.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_ASYNC_BLOCK_DEVICE_SUITE_NAME =
    "kernel/asynchronous_block_device/unit";
constexpr std::string_view OS_TEST_ASYNC_BLOCK_DEVICE_TYPE_ERASURE =
    "异步块设备类型擦除必须保留几何、请求字段和明确失败状态";
constexpr std::string_view OS_TEST_ASYNC_BLOCK_DEVICE_LIFECYCLE =
    "submit、cancel、timeout 和 completion 必须共享单赢家有序生命周期";
constexpr uint64_t OS_TEST_ASYNC_BLOCK_DEVICE_CAPACITY = 3ULL;
constexpr uint64_t OS_TEST_ASYNC_BLOCK_DEVICE_BLOCK_SIZE_BYTES = 4096ULL;
constexpr uint64_t OS_TEST_ASYNC_BLOCK_DEVICE_BLOCK_COUNT = 256ULL;
constexpr uint64_t OS_TEST_ASYNC_BLOCK_DEVICE_MAXIMUM_TRANSFER_BLOCK_COUNT = 4ULL;
constexpr uint64_t OS_TEST_ASYNC_BLOCK_DEVICE_MAXIMUM_OUTSTANDING_REQUEST_COUNT = 2ULL;
constexpr uint64_t OS_TEST_ASYNC_BLOCK_DEVICE_OWNER_THREAD_INDEX = 9ULL;
constexpr uint64_t OS_TEST_ASYNC_BLOCK_DEVICE_FIRST_LBA = 21ULL;
constexpr uint64_t OS_TEST_ASYNC_BLOCK_DEVICE_SECOND_LBA = 22ULL;
constexpr uint64_t OS_TEST_ASYNC_BLOCK_DEVICE_FIRST_DEADLINE_NS = 100ULL;
constexpr uint64_t OS_TEST_ASYNC_BLOCK_DEVICE_SECOND_DEADLINE_NS = 200ULL;
constexpr uint64_t OS_TEST_ASYNC_BLOCK_DEVICE_THIRD_DEADLINE_NS = 300ULL;
constexpr uint64_t OS_TEST_ASYNC_BLOCK_DEVICE_BEFORE_DEADLINE_NS = 99ULL;
constexpr uint64_t OS_TEST_ASYNC_BLOCK_DEVICE_AT_DEADLINE_NS = 100ULL;
constexpr uint64_t OS_TEST_ASYNC_BLOCK_DEVICE_EMPTY_VALUE = 0ULL;
constexpr os::kernel::BlockDeviceGeometry OS_TEST_ASYNC_BLOCK_DEVICE_GEOMETRY{
    .logical_block_size_bytes = OS_TEST_ASYNC_BLOCK_DEVICE_BLOCK_SIZE_BYTES,
    .logical_block_count = OS_TEST_ASYNC_BLOCK_DEVICE_BLOCK_COUNT,
    .maximum_transfer_block_count = OS_TEST_ASYNC_BLOCK_DEVICE_MAXIMUM_TRANSFER_BLOCK_COUNT,
    .maximum_outstanding_request_count =
        OS_TEST_ASYNC_BLOCK_DEVICE_MAXIMUM_OUTSTANDING_REQUEST_COUNT,
    .write_supported = true,
    .flush_supported = true,
};

[[nodiscard]] bool TakeExpectedCompletion(os::kernel::AsynchronousBlockDevice &device,
                                          const uint64_t request_identifier,
                                          const os::kernel::BlockOperation operation,
                                          const os::kernel::BlockRequestResult result) noexcept {
    os::kernel::BlockCompletion completion{};
    bool available = false;
    return device.TakeCompletion(completion, available) ==
               os::kernel::AsynchronousBlockDeviceStatus::Succeeded &&
           available && completion.request_identifier == request_identifier &&
           completion.operation == operation &&
           completion.owner_thread_index == OS_TEST_ASYNC_BLOCK_DEVICE_OWNER_THREAD_INDEX &&
           completion.result == result;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_ASYNC_BLOCK_DEVICE_SUITE_NAME};
    os::test::AsynchronousBlockDeviceTestDriver driver{};
    os::kernel::AsynchronousBlockDevice &device = driver;
    uint8_t buffer[OS_TEST_ASYNC_BLOCK_DEVICE_BLOCK_SIZE_BYTES]{};
    uint64_t invalid_identifier = OS_TEST_ASYNC_BLOCK_DEVICE_EMPTY_VALUE;
    const os::kernel::BlockDeviceGeometry empty_geometry = device.Geometry();
    const bool type_erasure_passed =
        empty_geometry.logical_block_size_bytes == OS_TEST_ASYNC_BLOCK_DEVICE_EMPTY_VALUE &&
        device.Submit(os::kernel::BlockOperation::Read, OS_TEST_ASYNC_BLOCK_DEVICE_FIRST_LBA,
                      buffer, sizeof(buffer), OS_TEST_ASYNC_BLOCK_DEVICE_OWNER_THREAD_INDEX,
                      OS_TEST_ASYNC_BLOCK_DEVICE_FIRST_DEADLINE_NS,
                      invalid_identifier) == os::kernel::AsynchronousBlockDeviceStatus::NotReady &&
        driver.Initialize(OS_TEST_ASYNC_BLOCK_DEVICE_CAPACITY,
                          OS_TEST_ASYNC_BLOCK_DEVICE_GEOMETRY) ==
            os::kernel::AsynchronousBlockDeviceStatus::Succeeded &&
        device.Geometry().logical_block_size_bytes == OS_TEST_ASYNC_BLOCK_DEVICE_BLOCK_SIZE_BYTES &&
        device.Geometry().maximum_outstanding_request_count ==
            OS_TEST_ASYNC_BLOCK_DEVICE_MAXIMUM_OUTSTANDING_REQUEST_COUNT &&
        device.Submit(os::kernel::BlockOperation::Read, OS_TEST_ASYNC_BLOCK_DEVICE_FIRST_LBA,
                      nullptr, sizeof(buffer), OS_TEST_ASYNC_BLOCK_DEVICE_OWNER_THREAD_INDEX,
                      OS_TEST_ASYNC_BLOCK_DEVICE_FIRST_DEADLINE_NS, invalid_identifier) ==
            os::kernel::AsynchronousBlockDeviceStatus::InvalidRequest;
    test_context.Expect(type_erasure_passed, OS_TEST_ASYNC_BLOCK_DEVICE_TYPE_ERASURE);

    uint64_t first_identifier = OS_TEST_ASYNC_BLOCK_DEVICE_EMPTY_VALUE;
    uint64_t second_identifier = OS_TEST_ASYNC_BLOCK_DEVICE_EMPTY_VALUE;
    uint64_t third_identifier = OS_TEST_ASYNC_BLOCK_DEVICE_EMPTY_VALUE;
    os::kernel::BlockRequest request{};
    bool issued = false;
    os::kernel::BlockCompletion no_completion{};
    bool completion_available = true;
    const bool lifecycle_passed =
        device.Submit(os::kernel::BlockOperation::Read, OS_TEST_ASYNC_BLOCK_DEVICE_FIRST_LBA,
                      buffer, sizeof(buffer), OS_TEST_ASYNC_BLOCK_DEVICE_OWNER_THREAD_INDEX,
                      OS_TEST_ASYNC_BLOCK_DEVICE_FIRST_DEADLINE_NS,
                      first_identifier) == os::kernel::AsynchronousBlockDeviceStatus::Succeeded &&
        device.Submit(os::kernel::BlockOperation::Write, OS_TEST_ASYNC_BLOCK_DEVICE_SECOND_LBA,
                      buffer, sizeof(buffer), OS_TEST_ASYNC_BLOCK_DEVICE_OWNER_THREAD_INDEX,
                      OS_TEST_ASYNC_BLOCK_DEVICE_SECOND_DEADLINE_NS,
                      second_identifier) == os::kernel::AsynchronousBlockDeviceStatus::Succeeded &&
        device.Submit(os::kernel::BlockOperation::Flush, OS_TEST_ASYNC_BLOCK_DEVICE_EMPTY_VALUE,
                      nullptr, OS_TEST_ASYNC_BLOCK_DEVICE_EMPTY_VALUE,
                      OS_TEST_ASYNC_BLOCK_DEVICE_OWNER_THREAD_INDEX,
                      OS_TEST_ASYNC_BLOCK_DEVICE_THIRD_DEADLINE_NS,
                      third_identifier) == os::kernel::AsynchronousBlockDeviceStatus::Succeeded &&
        device.Submit(os::kernel::BlockOperation::Flush, OS_TEST_ASYNC_BLOCK_DEVICE_EMPTY_VALUE,
                      nullptr, OS_TEST_ASYNC_BLOCK_DEVICE_EMPTY_VALUE,
                      OS_TEST_ASYNC_BLOCK_DEVICE_OWNER_THREAD_INDEX,
                      OS_TEST_ASYNC_BLOCK_DEVICE_THIRD_DEADLINE_NS, invalid_identifier) ==
            os::kernel::AsynchronousBlockDeviceStatus::CapacityExhausted &&
        driver.IssueNext(request, issued) == os::kernel::AsynchronousBlockDeviceStatus::Succeeded &&
        issued && request.identifier == first_identifier &&
        driver.IssueNext(request, issued) == os::kernel::AsynchronousBlockDeviceStatus::Succeeded &&
        issued && request.identifier == second_identifier &&
        driver.Complete(second_identifier, os::kernel::BlockRequestResult::Succeeded) ==
            os::kernel::AsynchronousBlockDeviceStatus::Succeeded &&
        device.Cancel(third_identifier) == os::kernel::AsynchronousBlockDeviceStatus::Succeeded &&
        device.Cancel(first_identifier) ==
            os::kernel::AsynchronousBlockDeviceStatus::RequestInProgress &&
        device.ResolveTimeouts(OS_TEST_ASYNC_BLOCK_DEVICE_BEFORE_DEADLINE_NS) ==
            os::kernel::AsynchronousBlockDeviceStatus::Succeeded &&
        device.ResolveTimeouts(OS_TEST_ASYNC_BLOCK_DEVICE_AT_DEADLINE_NS) ==
            os::kernel::AsynchronousBlockDeviceStatus::Succeeded &&
        TakeExpectedCompletion(device, second_identifier, os::kernel::BlockOperation::Write,
                               os::kernel::BlockRequestResult::Succeeded) &&
        TakeExpectedCompletion(device, third_identifier, os::kernel::BlockOperation::Flush,
                               os::kernel::BlockRequestResult::Cancelled) &&
        TakeExpectedCompletion(device, first_identifier, os::kernel::BlockOperation::Read,
                               os::kernel::BlockRequestResult::TimedOut) &&
        device.TakeCompletion(no_completion, completion_available) ==
            os::kernel::AsynchronousBlockDeviceStatus::Succeeded &&
        !completion_available &&
        device.Cancel(first_identifier) ==
            os::kernel::AsynchronousBlockDeviceStatus::RequestNotFound &&
        driver.Validate() == os::kernel::BlockRequestQueueStatus::Succeeded &&
        driver.Statistics().active_request_count == OS_TEST_ASYNC_BLOCK_DEVICE_EMPTY_VALUE;
    test_context.Expect(lifecycle_passed, OS_TEST_ASYNC_BLOCK_DEVICE_LIFECYCLE);
    return test_context.ExitCode();
}

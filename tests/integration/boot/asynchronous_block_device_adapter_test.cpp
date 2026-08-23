#include <asynchronous_block_device_test_driver.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_ASYNC_ADAPTER_SUITE_NAME =
    "kernel/asynchronous_block_device/integration";
constexpr std::string_view OS_TEST_ASYNC_ADAPTER_DRIVER_PARITY =
    "单深度和多深度驱动必须通过同一异步块设备接口保持完成与超时语义";
constexpr uint64_t OS_TEST_ASYNC_ADAPTER_BLOCK_SIZE_BYTES = 512ULL;
constexpr uint64_t OS_TEST_ASYNC_ADAPTER_BLOCK_COUNT = 4096ULL;
constexpr uint64_t OS_TEST_ASYNC_ADAPTER_MAXIMUM_TRANSFER_BLOCK_COUNT = 8ULL;
constexpr uint64_t OS_TEST_ASYNC_ADAPTER_SERIAL_DEPTH = 1ULL;
constexpr uint64_t OS_TEST_ASYNC_ADAPTER_PARALLEL_DEPTH = 3ULL;
constexpr uint64_t OS_TEST_ASYNC_ADAPTER_REQUEST_CAPACITY = 4ULL;
constexpr uint64_t OS_TEST_ASYNC_ADAPTER_OWNER_THREAD_INDEX = 14ULL;
constexpr uint64_t OS_TEST_ASYNC_ADAPTER_FIRST_LBA = 40ULL;
constexpr uint64_t OS_TEST_ASYNC_ADAPTER_SECOND_LBA = 48ULL;
constexpr uint64_t OS_TEST_ASYNC_ADAPTER_DEADLINE_NS = 500ULL;
constexpr uint64_t OS_TEST_ASYNC_ADAPTER_EMPTY_VALUE = 0ULL;

[[nodiscard]] os::kernel::BlockDeviceGeometry
MakeGeometry(const uint64_t maximum_outstanding_request_count) noexcept {
    return os::kernel::BlockDeviceGeometry{
        .logical_block_size_bytes = OS_TEST_ASYNC_ADAPTER_BLOCK_SIZE_BYTES,
        .logical_block_count = OS_TEST_ASYNC_ADAPTER_BLOCK_COUNT,
        .maximum_transfer_block_count = OS_TEST_ASYNC_ADAPTER_MAXIMUM_TRANSFER_BLOCK_COUNT,
        .maximum_outstanding_request_count = maximum_outstanding_request_count,
        .write_supported = true,
        .flush_supported = true,
    };
}

[[nodiscard]] bool TakeExpected(os::kernel::AsynchronousBlockDevice &device,
                                const uint64_t request_identifier,
                                const os::kernel::BlockRequestResult result) noexcept {
    os::kernel::BlockCompletion completion{};
    bool available = false;
    return device.TakeCompletion(completion, available) ==
               os::kernel::AsynchronousBlockDeviceStatus::Succeeded &&
           available && completion.request_identifier == request_identifier &&
           completion.owner_thread_index == OS_TEST_ASYNC_ADAPTER_OWNER_THREAD_INDEX &&
           completion.result == result;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_ASYNC_ADAPTER_SUITE_NAME};
    os::test::AsynchronousBlockDeviceTestDriver serial_driver{};
    os::test::AsynchronousBlockDeviceTestDriver parallel_driver{};
    os::kernel::AsynchronousBlockDevice &serial_device = serial_driver;
    os::kernel::AsynchronousBlockDevice &parallel_device = parallel_driver;
    uint8_t first_buffer[OS_TEST_ASYNC_ADAPTER_BLOCK_SIZE_BYTES]{};
    uint8_t second_buffer[OS_TEST_ASYNC_ADAPTER_BLOCK_SIZE_BYTES]{};
    uint64_t serial_identifier = OS_TEST_ASYNC_ADAPTER_EMPTY_VALUE;
    uint64_t first_parallel_identifier = OS_TEST_ASYNC_ADAPTER_EMPTY_VALUE;
    uint64_t second_parallel_identifier = OS_TEST_ASYNC_ADAPTER_EMPTY_VALUE;
    os::kernel::BlockRequest request{};
    bool issued = false;
    const bool parity_passed =
        serial_driver.Initialize(OS_TEST_ASYNC_ADAPTER_REQUEST_CAPACITY,
                                 MakeGeometry(OS_TEST_ASYNC_ADAPTER_SERIAL_DEPTH)) ==
            os::kernel::AsynchronousBlockDeviceStatus::Succeeded &&
        parallel_driver.Initialize(OS_TEST_ASYNC_ADAPTER_REQUEST_CAPACITY,
                                   MakeGeometry(OS_TEST_ASYNC_ADAPTER_PARALLEL_DEPTH)) ==
            os::kernel::AsynchronousBlockDeviceStatus::Succeeded &&
        serial_device.Submit(os::kernel::BlockOperation::Read, OS_TEST_ASYNC_ADAPTER_FIRST_LBA,
                             first_buffer, sizeof(first_buffer),
                             OS_TEST_ASYNC_ADAPTER_OWNER_THREAD_INDEX,
                             OS_TEST_ASYNC_ADAPTER_DEADLINE_NS, serial_identifier) ==
            os::kernel::AsynchronousBlockDeviceStatus::Succeeded &&
        parallel_device.Submit(os::kernel::BlockOperation::Read, OS_TEST_ASYNC_ADAPTER_FIRST_LBA,
                               first_buffer, sizeof(first_buffer),
                               OS_TEST_ASYNC_ADAPTER_OWNER_THREAD_INDEX,
                               OS_TEST_ASYNC_ADAPTER_DEADLINE_NS, first_parallel_identifier) ==
            os::kernel::AsynchronousBlockDeviceStatus::Succeeded &&
        parallel_device.Submit(os::kernel::BlockOperation::Write, OS_TEST_ASYNC_ADAPTER_SECOND_LBA,
                               second_buffer, sizeof(second_buffer),
                               OS_TEST_ASYNC_ADAPTER_OWNER_THREAD_INDEX,
                               OS_TEST_ASYNC_ADAPTER_DEADLINE_NS, second_parallel_identifier) ==
            os::kernel::AsynchronousBlockDeviceStatus::Succeeded &&
        serial_driver.IssueNext(request, issued) ==
            os::kernel::AsynchronousBlockDeviceStatus::Succeeded &&
        issued && request.identifier == serial_identifier &&
        serial_device.ResolveTimeouts(OS_TEST_ASYNC_ADAPTER_DEADLINE_NS) ==
            os::kernel::AsynchronousBlockDeviceStatus::Succeeded &&
        parallel_driver.IssueNext(request, issued) ==
            os::kernel::AsynchronousBlockDeviceStatus::Succeeded &&
        issued && request.identifier == first_parallel_identifier &&
        parallel_driver.IssueNext(request, issued) ==
            os::kernel::AsynchronousBlockDeviceStatus::Succeeded &&
        issued && request.identifier == second_parallel_identifier &&
        parallel_driver.Complete(second_parallel_identifier,
                                 os::kernel::BlockRequestResult::Succeeded) ==
            os::kernel::AsynchronousBlockDeviceStatus::Succeeded &&
        parallel_driver.Complete(first_parallel_identifier,
                                 os::kernel::BlockRequestResult::DeviceError) ==
            os::kernel::AsynchronousBlockDeviceStatus::Succeeded &&
        TakeExpected(serial_device, serial_identifier, os::kernel::BlockRequestResult::TimedOut) &&
        TakeExpected(parallel_device, second_parallel_identifier,
                     os::kernel::BlockRequestResult::Succeeded) &&
        TakeExpected(parallel_device, first_parallel_identifier,
                     os::kernel::BlockRequestResult::DeviceError) &&
        serial_driver.Validate() == os::kernel::BlockRequestQueueStatus::Succeeded &&
        parallel_driver.Validate() == os::kernel::BlockRequestQueueStatus::Succeeded;
    test_context.Expect(parity_passed, OS_TEST_ASYNC_ADAPTER_DRIVER_PARITY);
    return test_context.ExitCode();
}

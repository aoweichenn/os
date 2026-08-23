#include <asynchronous_block_device_test_driver.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_ASYNC_RANDOM_SUITE_NAME =
    "kernel/asynchronous_block_device/randomized";
constexpr std::string_view OS_TEST_ASYNC_RANDOM_REFERENCE_MODEL =
    "十万步类型擦除异步设备模型不得丢失、重复或错序交付请求";
constexpr uint64_t OS_TEST_ASYNC_RANDOM_CAPACITY = 32ULL;
constexpr uint64_t OS_TEST_ASYNC_RANDOM_OPERATION_COUNT = 100000ULL;
constexpr uint64_t OS_TEST_ASYNC_RANDOM_OPERATION_KIND_COUNT = 7ULL;
constexpr uint64_t OS_TEST_ASYNC_RANDOM_SUBMIT_OPERATION = 0ULL;
constexpr uint64_t OS_TEST_ASYNC_RANDOM_ISSUE_OPERATION = 1ULL;
constexpr uint64_t OS_TEST_ASYNC_RANDOM_COMPLETE_OPERATION = 2ULL;
constexpr uint64_t OS_TEST_ASYNC_RANDOM_FAIL_OPERATION = 3ULL;
constexpr uint64_t OS_TEST_ASYNC_RANDOM_TIMEOUT_OPERATION = 4ULL;
constexpr uint64_t OS_TEST_ASYNC_RANDOM_CANCEL_OPERATION = 5ULL;
constexpr uint64_t OS_TEST_ASYNC_RANDOM_TAKE_OPERATION = 6ULL;
constexpr uint64_t OS_TEST_ASYNC_RANDOM_BLOCK_OPERATION_KIND_COUNT = 3ULL;
constexpr uint64_t OS_TEST_ASYNC_RANDOM_BLOCK_SIZE_BYTES = 4096ULL;
constexpr uint64_t OS_TEST_ASYNC_RANDOM_BLOCK_COUNT = 65536ULL;
constexpr uint64_t OS_TEST_ASYNC_RANDOM_MAXIMUM_TRANSFER_BLOCK_COUNT = 8ULL;
constexpr uint64_t OS_TEST_ASYNC_RANDOM_MAXIMUM_OUTSTANDING_REQUEST_COUNT = 4ULL;
constexpr uint64_t OS_TEST_ASYNC_RANDOM_OWNER_THREAD_INDEX = 23ULL;
constexpr uint64_t OS_TEST_ASYNC_RANDOM_DEADLINE_STEP_NS = 64ULL;
constexpr uint64_t OS_TEST_ASYNC_RANDOM_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_ASYNC_RANDOM_INITIAL_VALUE = 1ULL;
constexpr uint64_t OS_TEST_ASYNC_RANDOM_SEED = 0x4153594E43494F32ULL;
constexpr uint64_t OS_TEST_ASYNC_RANDOM_MULTIPLIER = 6364136223846793005ULL;
constexpr uint64_t OS_TEST_ASYNC_RANDOM_INCREMENT = 1442695040888963407ULL;
constexpr os::kernel::BlockDeviceGeometry OS_TEST_ASYNC_RANDOM_GEOMETRY{
    .logical_block_size_bytes = OS_TEST_ASYNC_RANDOM_BLOCK_SIZE_BYTES,
    .logical_block_count = OS_TEST_ASYNC_RANDOM_BLOCK_COUNT,
    .maximum_transfer_block_count = OS_TEST_ASYNC_RANDOM_MAXIMUM_TRANSFER_BLOCK_COUNT,
    .maximum_outstanding_request_count = OS_TEST_ASYNC_RANDOM_MAXIMUM_OUTSTANDING_REQUEST_COUNT,
    .write_supported = true,
    .flush_supported = true,
};

struct ReferenceRequest final {
    uint64_t identifier;
    os::kernel::BlockOperation operation;
    uint64_t logical_block_address;
    uint64_t logical_block_count;
    uint64_t deadline_nanoseconds;
    uint64_t completion_sequence;
    os::kernel::BlockRequestState state;
    os::kernel::BlockRequestResult result;
};

[[nodiscard]] uint64_t NextRandom(uint64_t &state) noexcept {
    state = state * OS_TEST_ASYNC_RANDOM_MULTIPLIER + OS_TEST_ASYNC_RANDOM_INCREMENT;
    return state;
}

[[nodiscard]] uint64_t FindState(const ReferenceRequest *const requests,
                                 const os::kernel::BlockRequestState state) noexcept {
    uint64_t selected_index = OS_TEST_ASYNC_RANDOM_CAPACITY;
    for (uint64_t request_index = 0ULL; request_index < OS_TEST_ASYNC_RANDOM_CAPACITY;
         ++request_index) {
        if (requests[request_index].state == state &&
            (selected_index == OS_TEST_ASYNC_RANDOM_CAPACITY ||
             requests[request_index].identifier < requests[selected_index].identifier)) {
            selected_index = request_index;
        }
    }
    return selected_index;
}

[[nodiscard]] uint64_t FindExpired(const ReferenceRequest *const requests,
                                   const uint64_t now_nanoseconds) noexcept {
    uint64_t selected_index = OS_TEST_ASYNC_RANDOM_CAPACITY;
    for (uint64_t request_index = 0ULL; request_index < OS_TEST_ASYNC_RANDOM_CAPACITY;
         ++request_index) {
        if (requests[request_index].state != os::kernel::BlockRequestState::Issued ||
            requests[request_index].deadline_nanoseconds > now_nanoseconds) {
            continue;
        }
        if (selected_index == OS_TEST_ASYNC_RANDOM_CAPACITY ||
            requests[request_index].deadline_nanoseconds <
                requests[selected_index].deadline_nanoseconds ||
            (requests[request_index].deadline_nanoseconds ==
                 requests[selected_index].deadline_nanoseconds &&
             requests[request_index].identifier < requests[selected_index].identifier)) {
            selected_index = request_index;
        }
    }
    return selected_index;
}

[[nodiscard]] uint64_t FindCompletion(const ReferenceRequest *const requests) noexcept {
    uint64_t selected_index = OS_TEST_ASYNC_RANDOM_CAPACITY;
    for (uint64_t request_index = 0ULL; request_index < OS_TEST_ASYNC_RANDOM_CAPACITY;
         ++request_index) {
        if (requests[request_index].state != os::kernel::BlockRequestState::Completed) {
            continue;
        }
        if (selected_index == OS_TEST_ASYNC_RANDOM_CAPACITY ||
            requests[request_index].completion_sequence <
                requests[selected_index].completion_sequence) {
            selected_index = request_index;
        }
    }
    return selected_index;
}

[[nodiscard]] uint64_t CountState(const ReferenceRequest *const requests,
                                  const os::kernel::BlockRequestState state) noexcept {
    uint64_t count = 0ULL;
    for (uint64_t request_index = 0ULL; request_index < OS_TEST_ASYNC_RANDOM_CAPACITY;
         ++request_index) {
        if (requests[request_index].state == state) {
            ++count;
        }
    }
    return count;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_ASYNC_RANDOM_SUITE_NAME};
    os::test::AsynchronousBlockDeviceTestDriver driver{};
    os::kernel::AsynchronousBlockDevice &device = driver;
    ReferenceRequest reference[OS_TEST_ASYNC_RANDOM_CAPACITY]{};
    uint8_t buffer[OS_TEST_ASYNC_RANDOM_BLOCK_SIZE_BYTES *
                   OS_TEST_ASYNC_RANDOM_MAXIMUM_TRANSFER_BLOCK_COUNT]{};
    uint64_t random_state = OS_TEST_ASYNC_RANDOM_SEED;
    uint64_t now_nanoseconds = OS_TEST_ASYNC_RANDOM_INITIAL_VALUE;
    uint64_t next_completion_sequence = OS_TEST_ASYNC_RANDOM_INITIAL_VALUE;
    uint64_t delivered_completion_count = OS_TEST_ASYNC_RANDOM_EMPTY_VALUE;
    bool consistent =
        driver.Initialize(OS_TEST_ASYNC_RANDOM_CAPACITY, OS_TEST_ASYNC_RANDOM_GEOMETRY) ==
        os::kernel::AsynchronousBlockDeviceStatus::Succeeded;

    for (uint64_t operation_index = 0ULL;
         consistent && operation_index < OS_TEST_ASYNC_RANDOM_OPERATION_COUNT; ++operation_index) {
        const uint64_t operation_kind =
            NextRandom(random_state) % OS_TEST_ASYNC_RANDOM_OPERATION_KIND_COUNT;
        if (operation_kind == OS_TEST_ASYNC_RANDOM_SUBMIT_OPERATION) {
            const uint64_t unused_index =
                FindState(reference, os::kernel::BlockRequestState::Unused);
            if (unused_index < OS_TEST_ASYNC_RANDOM_CAPACITY) {
                const os::kernel::BlockOperation operation =
                    static_cast<os::kernel::BlockOperation>(
                        NextRandom(random_state) % OS_TEST_ASYNC_RANDOM_BLOCK_OPERATION_KIND_COUNT);
                const bool flush = operation == os::kernel::BlockOperation::Flush;
                const uint64_t logical_block_count =
                    flush ? OS_TEST_ASYNC_RANDOM_EMPTY_VALUE
                          : OS_TEST_ASYNC_RANDOM_INITIAL_VALUE +
                                NextRandom(random_state) %
                                    OS_TEST_ASYNC_RANDOM_MAXIMUM_TRANSFER_BLOCK_COUNT;
                const uint64_t logical_block_address =
                    flush ? OS_TEST_ASYNC_RANDOM_EMPTY_VALUE
                          : NextRandom(random_state) %
                                (OS_TEST_ASYNC_RANDOM_BLOCK_COUNT - logical_block_count +
                                 OS_TEST_ASYNC_RANDOM_INITIAL_VALUE);
                const uint64_t deadline_nanoseconds =
                    now_nanoseconds + OS_TEST_ASYNC_RANDOM_DEADLINE_STEP_NS +
                    NextRandom(random_state) % OS_TEST_ASYNC_RANDOM_DEADLINE_STEP_NS;
                uint64_t request_identifier = OS_TEST_ASYNC_RANDOM_EMPTY_VALUE;
                consistent =
                    device.Submit(
                        operation, logical_block_address, flush ? nullptr : buffer,
                        flush ? OS_TEST_ASYNC_RANDOM_EMPTY_VALUE
                              : logical_block_count * OS_TEST_ASYNC_RANDOM_BLOCK_SIZE_BYTES,
                        OS_TEST_ASYNC_RANDOM_OWNER_THREAD_INDEX, deadline_nanoseconds,
                        request_identifier) == os::kernel::AsynchronousBlockDeviceStatus::Succeeded;
                if (consistent) {
                    reference[unused_index] = ReferenceRequest{
                        .identifier = request_identifier,
                        .operation = operation,
                        .logical_block_address = logical_block_address,
                        .logical_block_count = logical_block_count,
                        .deadline_nanoseconds = deadline_nanoseconds,
                        .completion_sequence = OS_TEST_ASYNC_RANDOM_EMPTY_VALUE,
                        .state = os::kernel::BlockRequestState::Queued,
                        .result = os::kernel::BlockRequestResult::None,
                    };
                }
            }
        } else if (operation_kind == OS_TEST_ASYNC_RANDOM_ISSUE_OPERATION) {
            const uint64_t expected_index =
                FindState(reference, os::kernel::BlockRequestState::Queued);
            const uint64_t issued_count =
                CountState(reference, os::kernel::BlockRequestState::Issued);
            os::kernel::BlockRequest request{};
            bool issued = false;
            consistent =
                driver.IssueNext(request, issued) ==
                    os::kernel::AsynchronousBlockDeviceStatus::Succeeded &&
                issued == (expected_index < OS_TEST_ASYNC_RANDOM_CAPACITY &&
                           issued_count < OS_TEST_ASYNC_RANDOM_MAXIMUM_OUTSTANDING_REQUEST_COUNT) &&
                (!issued || request.identifier == reference[expected_index].identifier);
            if (consistent && issued) {
                reference[expected_index].state = os::kernel::BlockRequestState::Issued;
            }
        } else if (operation_kind == OS_TEST_ASYNC_RANDOM_COMPLETE_OPERATION ||
                   operation_kind == OS_TEST_ASYNC_RANDOM_FAIL_OPERATION) {
            const uint64_t issued_index =
                FindState(reference, os::kernel::BlockRequestState::Issued);
            if (issued_index < OS_TEST_ASYNC_RANDOM_CAPACITY) {
                const os::kernel::BlockRequestResult result =
                    operation_kind == OS_TEST_ASYNC_RANDOM_COMPLETE_OPERATION
                        ? os::kernel::BlockRequestResult::Succeeded
                        : os::kernel::BlockRequestResult::DeviceError;
                consistent = driver.Complete(reference[issued_index].identifier, result) ==
                             os::kernel::AsynchronousBlockDeviceStatus::Succeeded;
                reference[issued_index].state = os::kernel::BlockRequestState::Completed;
                reference[issued_index].result = result;
                reference[issued_index].completion_sequence = next_completion_sequence;
                ++next_completion_sequence;
            }
        } else if (operation_kind == OS_TEST_ASYNC_RANDOM_TIMEOUT_OPERATION) {
            now_nanoseconds += NextRandom(random_state) % OS_TEST_ASYNC_RANDOM_DEADLINE_STEP_NS;
            const uint64_t expired_index = FindExpired(reference, now_nanoseconds);
            consistent = device.ResolveTimeouts(now_nanoseconds) ==
                         os::kernel::AsynchronousBlockDeviceStatus::Succeeded;
            if (consistent && expired_index < OS_TEST_ASYNC_RANDOM_CAPACITY) {
                reference[expired_index].state = os::kernel::BlockRequestState::Completed;
                reference[expired_index].result = os::kernel::BlockRequestResult::TimedOut;
                reference[expired_index].completion_sequence = next_completion_sequence;
                ++next_completion_sequence;
            }
        } else if (operation_kind == OS_TEST_ASYNC_RANDOM_CANCEL_OPERATION) {
            const uint64_t queued_index =
                FindState(reference, os::kernel::BlockRequestState::Queued);
            if (queued_index < OS_TEST_ASYNC_RANDOM_CAPACITY) {
                consistent = device.Cancel(reference[queued_index].identifier) ==
                             os::kernel::AsynchronousBlockDeviceStatus::Succeeded;
                reference[queued_index].state = os::kernel::BlockRequestState::Completed;
                reference[queued_index].result = os::kernel::BlockRequestResult::Cancelled;
                reference[queued_index].completion_sequence = next_completion_sequence;
                ++next_completion_sequence;
            }
        } else if (operation_kind == OS_TEST_ASYNC_RANDOM_TAKE_OPERATION) {
            const uint64_t completed_index = FindCompletion(reference);
            os::kernel::BlockCompletion completion{};
            bool available = false;
            consistent = device.TakeCompletion(completion, available) ==
                             os::kernel::AsynchronousBlockDeviceStatus::Succeeded &&
                         available == (completed_index < OS_TEST_ASYNC_RANDOM_CAPACITY);
            if (consistent && available) {
                const ReferenceRequest &expected = reference[completed_index];
                consistent =
                    completion.request_identifier == expected.identifier &&
                    completion.operation == expected.operation &&
                    completion.logical_block_address == expected.logical_block_address &&
                    completion.logical_block_count == expected.logical_block_count &&
                    completion.owner_thread_index == OS_TEST_ASYNC_RANDOM_OWNER_THREAD_INDEX &&
                    completion.result == expected.result;
                reference[completed_index] = ReferenceRequest{};
                ++delivered_completion_count;
            }
        } else {
            consistent = false;
        }
        if (operation_index % OS_TEST_ASYNC_RANDOM_CAPACITY == 0ULL &&
            driver.Validate() != os::kernel::BlockRequestQueueStatus::Succeeded) {
            consistent = false;
        }
    }

    for (uint64_t request_index = 0ULL; consistent && request_index < OS_TEST_ASYNC_RANDOM_CAPACITY;
         ++request_index) {
        if (reference[request_index].state == os::kernel::BlockRequestState::Queued) {
            consistent = device.Cancel(reference[request_index].identifier) ==
                         os::kernel::AsynchronousBlockDeviceStatus::Succeeded;
            reference[request_index].state = os::kernel::BlockRequestState::Completed;
            reference[request_index].result = os::kernel::BlockRequestResult::Cancelled;
            reference[request_index].completion_sequence = next_completion_sequence;
            ++next_completion_sequence;
        }
    }
    uint64_t issued_index = FindState(reference, os::kernel::BlockRequestState::Issued);
    while (consistent && issued_index < OS_TEST_ASYNC_RANDOM_CAPACITY) {
        consistent = driver.Complete(reference[issued_index].identifier,
                                     os::kernel::BlockRequestResult::Succeeded) ==
                     os::kernel::AsynchronousBlockDeviceStatus::Succeeded;
        reference[issued_index].state = os::kernel::BlockRequestState::Completed;
        reference[issued_index].result = os::kernel::BlockRequestResult::Succeeded;
        reference[issued_index].completion_sequence = next_completion_sequence;
        ++next_completion_sequence;
        issued_index = FindState(reference, os::kernel::BlockRequestState::Issued);
    }
    uint64_t completed_index = FindCompletion(reference);
    while (consistent && completed_index < OS_TEST_ASYNC_RANDOM_CAPACITY) {
        os::kernel::BlockCompletion completion{};
        bool available = false;
        const ReferenceRequest &expected = reference[completed_index];
        consistent = device.TakeCompletion(completion, available) ==
                         os::kernel::AsynchronousBlockDeviceStatus::Succeeded &&
                     available && completion.request_identifier == expected.identifier &&
                     completion.result == expected.result;
        reference[completed_index] = ReferenceRequest{};
        ++delivered_completion_count;
        completed_index = FindCompletion(reference);
    }

    const os::kernel::BlockRequestQueueStatistics statistics = driver.Statistics();
    test_context.ExpectRandom(
        consistent && driver.Validate() == os::kernel::BlockRequestQueueStatus::Succeeded &&
            statistics.active_request_count == OS_TEST_ASYNC_RANDOM_EMPTY_VALUE &&
            statistics.submission_count == statistics.reap_count &&
            statistics.completion_delivery_count == delivered_completion_count,
        OS_TEST_ASYNC_RANDOM_REFERENCE_MODEL, OS_TEST_ASYNC_RANDOM_SEED,
        OS_TEST_ASYNC_RANDOM_OPERATION_COUNT);
    return test_context.ExitCode();
}

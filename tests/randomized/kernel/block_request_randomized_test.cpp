#include <os/kernel/device/block_request.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_BLOCK_RANDOM_SUITE_NAME =
    "kernel/block_request/randomized";
constexpr std::string_view OS_TEST_BLOCK_RANDOM_REFERENCE_MODEL =
    "十万步多深度块请求模型不得乱序签发、重复解析、越界或泄漏请求";
constexpr uint64_t OS_TEST_BLOCK_RANDOM_CAPACITY = 32ULL;
constexpr uint64_t OS_TEST_BLOCK_RANDOM_OPERATION_COUNT = 100000ULL;
constexpr uint64_t OS_TEST_BLOCK_RANDOM_OPERATION_KIND_COUNT = 7ULL;
constexpr uint64_t OS_TEST_BLOCK_RANDOM_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_BLOCK_RANDOM_INITIAL_TIME_NS = 1ULL;
constexpr uint64_t OS_TEST_BLOCK_RANDOM_SUBMIT_OPERATION = 0ULL;
constexpr uint64_t OS_TEST_BLOCK_RANDOM_ISSUE_OPERATION = 1ULL;
constexpr uint64_t OS_TEST_BLOCK_RANDOM_SUCCEED_OPERATION = 2ULL;
constexpr uint64_t OS_TEST_BLOCK_RANDOM_FAIL_OPERATION = 3ULL;
constexpr uint64_t OS_TEST_BLOCK_RANDOM_TIMEOUT_OPERATION = 4ULL;
constexpr uint64_t OS_TEST_BLOCK_RANDOM_CANCEL_OPERATION = 5ULL;
constexpr uint64_t OS_TEST_BLOCK_RANDOM_BLOCK_OPERATION_KIND_COUNT = 3ULL;
constexpr uint64_t OS_TEST_BLOCK_RANDOM_LOGICAL_BLOCK_SIZE_BYTES = 4096ULL;
constexpr uint64_t OS_TEST_BLOCK_RANDOM_LOGICAL_BLOCK_COUNT = 65536ULL;
constexpr uint64_t OS_TEST_BLOCK_RANDOM_MAXIMUM_TRANSFER_BLOCK_COUNT = 8ULL;
constexpr uint64_t OS_TEST_BLOCK_RANDOM_MAXIMUM_OUTSTANDING_REQUEST_COUNT = 4ULL;
constexpr uint64_t OS_TEST_BLOCK_RANDOM_DEADLINE_STEP_NS = 64ULL;
constexpr uint64_t OS_TEST_BLOCK_RANDOM_OWNER_THREAD_INDEX = 17ULL;
constexpr uint64_t OS_TEST_BLOCK_RANDOM_SEED = 0x424C4F434B494F31ULL;
constexpr uint64_t OS_TEST_BLOCK_RANDOM_MULTIPLIER = 6364136223846793005ULL;
constexpr uint64_t OS_TEST_BLOCK_RANDOM_INCREMENT = 1442695040888963407ULL;
constexpr os::kernel::BlockDeviceGeometry OS_TEST_BLOCK_RANDOM_GEOMETRY{
    .logical_block_size_bytes = OS_TEST_BLOCK_RANDOM_LOGICAL_BLOCK_SIZE_BYTES,
    .logical_block_count = OS_TEST_BLOCK_RANDOM_LOGICAL_BLOCK_COUNT,
    .maximum_transfer_block_count = OS_TEST_BLOCK_RANDOM_MAXIMUM_TRANSFER_BLOCK_COUNT,
    .maximum_outstanding_request_count =
        OS_TEST_BLOCK_RANDOM_MAXIMUM_OUTSTANDING_REQUEST_COUNT,
    .write_supported = true,
    .flush_supported = true,
};

struct ReferenceRequest final {
    uint64_t identifier;
    uint64_t deadline_nanoseconds;
    os::kernel::BlockRequestState state;
};

[[nodiscard]] uint64_t NextRandom(uint64_t &state) noexcept {
    state = state * OS_TEST_BLOCK_RANDOM_MULTIPLIER + OS_TEST_BLOCK_RANDOM_INCREMENT;
    return state;
}

[[nodiscard]] uint64_t FindReference(const ReferenceRequest *const requests,
                                     const os::kernel::BlockRequestState state) noexcept {
    uint64_t selected_index = OS_TEST_BLOCK_RANDOM_CAPACITY;
    for (uint64_t request_index = OS_TEST_BLOCK_RANDOM_EMPTY_VALUE;
         request_index < OS_TEST_BLOCK_RANDOM_CAPACITY; ++request_index) {
        if (requests[request_index].state == state &&
            (selected_index == OS_TEST_BLOCK_RANDOM_CAPACITY ||
             requests[request_index].identifier < requests[selected_index].identifier)) {
            selected_index = request_index;
        }
    }
    return selected_index;
}

[[nodiscard]] uint64_t FindUnused(const ReferenceRequest *const requests) noexcept {
    return FindReference(requests, os::kernel::BlockRequestState::Unused);
}

[[nodiscard]] uint64_t CountReferences(const ReferenceRequest *const requests,
                                       const os::kernel::BlockRequestState state) noexcept {
    uint64_t count = OS_TEST_BLOCK_RANDOM_EMPTY_VALUE;
    for (uint64_t request_index = OS_TEST_BLOCK_RANDOM_EMPTY_VALUE;
         request_index < OS_TEST_BLOCK_RANDOM_CAPACITY; ++request_index) {
        if (requests[request_index].state == state) {
            ++count;
        }
    }
    return count;
}

[[nodiscard]] uint64_t FindExpiredReference(const ReferenceRequest *const requests,
                                            const uint64_t now_nanoseconds) noexcept {
    uint64_t selected_index = OS_TEST_BLOCK_RANDOM_CAPACITY;
    for (uint64_t request_index = OS_TEST_BLOCK_RANDOM_EMPTY_VALUE;
         request_index < OS_TEST_BLOCK_RANDOM_CAPACITY; ++request_index) {
        if (requests[request_index].state != os::kernel::BlockRequestState::Issued ||
            requests[request_index].deadline_nanoseconds > now_nanoseconds) {
            continue;
        }
        if (selected_index == OS_TEST_BLOCK_RANDOM_CAPACITY ||
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

}

int main() {
    os::test::TestContext test_context{OS_TEST_BLOCK_RANDOM_SUITE_NAME};
    os::kernel::BlockRequest storage[OS_TEST_BLOCK_RANDOM_CAPACITY]{};
    ReferenceRequest reference[OS_TEST_BLOCK_RANDOM_CAPACITY]{};
    uint8_t transfer_buffer[OS_TEST_BLOCK_RANDOM_LOGICAL_BLOCK_SIZE_BYTES *
                            OS_TEST_BLOCK_RANDOM_MAXIMUM_TRANSFER_BLOCK_COUNT]{};
    os::kernel::BlockRequestQueue queue{};
    uint64_t random_state = OS_TEST_BLOCK_RANDOM_SEED;
    uint64_t now_nanoseconds = OS_TEST_BLOCK_RANDOM_INITIAL_TIME_NS;
    bool consistent =
        queue.Initialize(storage, OS_TEST_BLOCK_RANDOM_CAPACITY,
                         OS_TEST_BLOCK_RANDOM_GEOMETRY) ==
        os::kernel::BlockRequestQueueStatus::Succeeded;

    for (uint64_t operation_index = OS_TEST_BLOCK_RANDOM_EMPTY_VALUE;
         consistent && operation_index < OS_TEST_BLOCK_RANDOM_OPERATION_COUNT;
         ++operation_index) {
        const uint64_t operation_kind =
            NextRandom(random_state) % OS_TEST_BLOCK_RANDOM_OPERATION_KIND_COUNT;
        if (operation_kind == OS_TEST_BLOCK_RANDOM_SUBMIT_OPERATION) {
            const uint64_t unused_index = FindUnused(reference);
            if (unused_index < OS_TEST_BLOCK_RANDOM_CAPACITY) {
                uint64_t identifier = OS_TEST_BLOCK_RANDOM_EMPTY_VALUE;
                const os::kernel::BlockOperation operation =
                    static_cast<os::kernel::BlockOperation>(
                        NextRandom(random_state) %
                        OS_TEST_BLOCK_RANDOM_BLOCK_OPERATION_KIND_COUNT);
                const bool flush = operation == os::kernel::BlockOperation::Flush;
                const uint64_t transfer_block_count =
                    flush ? OS_TEST_BLOCK_RANDOM_EMPTY_VALUE
                          : OS_TEST_BLOCK_RANDOM_INITIAL_TIME_NS +
                                NextRandom(random_state) %
                                    OS_TEST_BLOCK_RANDOM_MAXIMUM_TRANSFER_BLOCK_COUNT;
                const uint64_t logical_block_address =
                    flush ? OS_TEST_BLOCK_RANDOM_EMPTY_VALUE
                          : NextRandom(random_state) %
                                (OS_TEST_BLOCK_RANDOM_LOGICAL_BLOCK_COUNT -
                                 transfer_block_count + OS_TEST_BLOCK_RANDOM_INITIAL_TIME_NS);
                const uint64_t deadline_nanoseconds =
                    now_nanoseconds + OS_TEST_BLOCK_RANDOM_DEADLINE_STEP_NS +
                    NextRandom(random_state) % OS_TEST_BLOCK_RANDOM_DEADLINE_STEP_NS;
                consistent =
                    queue.Submit(operation,
                                 logical_block_address,
                                 flush ? nullptr : transfer_buffer,
                                 flush ? OS_TEST_BLOCK_RANDOM_EMPTY_VALUE
                                       : transfer_block_count *
                                             OS_TEST_BLOCK_RANDOM_LOGICAL_BLOCK_SIZE_BYTES,
                                 OS_TEST_BLOCK_RANDOM_OWNER_THREAD_INDEX,
                                 deadline_nanoseconds, identifier) ==
                    os::kernel::BlockRequestQueueStatus::Succeeded;
                if (consistent) {
                    reference[unused_index] = ReferenceRequest{
                        .identifier = identifier,
                        .deadline_nanoseconds = deadline_nanoseconds,
                        .state = os::kernel::BlockRequestState::Queued,
                    };
                }
            }
        } else if (operation_kind == OS_TEST_BLOCK_RANDOM_ISSUE_OPERATION) {
            os::kernel::BlockRequest request{};
            bool issued = false;
            const uint64_t expected_index =
                FindReference(reference, os::kernel::BlockRequestState::Queued);
            const uint64_t issued_request_count =
                CountReferences(reference, os::kernel::BlockRequestState::Issued);
            consistent =
                queue.IssueNext(request, issued) ==
                    os::kernel::BlockRequestQueueStatus::Succeeded &&
                issued == (issued_request_count <
                               OS_TEST_BLOCK_RANDOM_MAXIMUM_OUTSTANDING_REQUEST_COUNT &&
                           expected_index < OS_TEST_BLOCK_RANDOM_CAPACITY) &&
                (!issued || request.identifier == reference[expected_index].identifier);
            if (consistent && issued) {
                reference[expected_index].state = os::kernel::BlockRequestState::Issued;
            }
        } else if (operation_kind == OS_TEST_BLOCK_RANDOM_SUCCEED_OPERATION ||
                   operation_kind == OS_TEST_BLOCK_RANDOM_FAIL_OPERATION) {
            const uint64_t issued_index =
                FindReference(reference, os::kernel::BlockRequestState::Issued);
            if (issued_index < OS_TEST_BLOCK_RANDOM_CAPACITY) {
                const os::kernel::BlockRequestResult result =
                    operation_kind == OS_TEST_BLOCK_RANDOM_SUCCEED_OPERATION
                        ? os::kernel::BlockRequestResult::Succeeded
                        : os::kernel::BlockRequestResult::DeviceError;
                consistent =
                    queue.Complete(reference[issued_index].identifier, result) ==
                    os::kernel::BlockRequestQueueStatus::Succeeded;
                reference[issued_index].state = os::kernel::BlockRequestState::Completed;
            }
        } else if (operation_kind == OS_TEST_BLOCK_RANDOM_TIMEOUT_OPERATION) {
            now_nanoseconds += NextRandom(random_state) %
                               OS_TEST_BLOCK_RANDOM_DEADLINE_STEP_NS;
            const uint64_t issued_index = FindExpiredReference(reference, now_nanoseconds);
            os::kernel::BlockRequest request{};
            bool resolved = false;
            consistent =
                queue.ResolveTimeout(now_nanoseconds, request, resolved) ==
                    os::kernel::BlockRequestQueueStatus::Succeeded &&
                resolved ==
                    (issued_index < OS_TEST_BLOCK_RANDOM_CAPACITY);
            if (consistent && resolved) {
                consistent =
                    request.identifier == reference[issued_index].identifier &&
                    request.result == os::kernel::BlockRequestResult::TimedOut;
                reference[issued_index].state = os::kernel::BlockRequestState::Completed;
            }
        } else if (operation_kind == OS_TEST_BLOCK_RANDOM_CANCEL_OPERATION) {
            const uint64_t queued_index =
                FindReference(reference, os::kernel::BlockRequestState::Queued);
            if (queued_index < OS_TEST_BLOCK_RANDOM_CAPACITY) {
                consistent =
                    queue.CancelQueued(reference[queued_index].identifier) ==
                    os::kernel::BlockRequestQueueStatus::Succeeded;
                reference[queued_index].state = os::kernel::BlockRequestState::Completed;
            }
        } else {
            const uint64_t completed_index =
                FindReference(reference, os::kernel::BlockRequestState::Completed);
            if (completed_index < OS_TEST_BLOCK_RANDOM_CAPACITY) {
                consistent =
                    queue.Reap(reference[completed_index].identifier) ==
                    os::kernel::BlockRequestQueueStatus::Succeeded;
                reference[completed_index] = ReferenceRequest{};
            }
        }
        if (operation_index % OS_TEST_BLOCK_RANDOM_CAPACITY ==
                OS_TEST_BLOCK_RANDOM_EMPTY_VALUE &&
            queue.Validate() != os::kernel::BlockRequestQueueStatus::Succeeded) {
            consistent = false;
        }
    }

    for (uint64_t request_index = OS_TEST_BLOCK_RANDOM_EMPTY_VALUE;
         consistent && request_index < OS_TEST_BLOCK_RANDOM_CAPACITY; ++request_index) {
        if (reference[request_index].state == os::kernel::BlockRequestState::Queued) {
            consistent =
                queue.CancelQueued(reference[request_index].identifier) ==
                os::kernel::BlockRequestQueueStatus::Succeeded;
            reference[request_index].state = os::kernel::BlockRequestState::Completed;
        }
    }
    uint64_t issued_index = FindReference(reference, os::kernel::BlockRequestState::Issued);
    while (consistent && issued_index < OS_TEST_BLOCK_RANDOM_CAPACITY) {
        consistent = queue.Complete(reference[issued_index].identifier,
                                    os::kernel::BlockRequestResult::Succeeded) ==
                     os::kernel::BlockRequestQueueStatus::Succeeded;
        reference[issued_index].state = os::kernel::BlockRequestState::Completed;
        issued_index = FindReference(reference, os::kernel::BlockRequestState::Issued);
    }
    for (uint64_t request_index = OS_TEST_BLOCK_RANDOM_EMPTY_VALUE;
         consistent && request_index < OS_TEST_BLOCK_RANDOM_CAPACITY; ++request_index) {
        if (reference[request_index].state == os::kernel::BlockRequestState::Completed) {
            consistent =
                queue.Reap(reference[request_index].identifier) ==
                os::kernel::BlockRequestQueueStatus::Succeeded;
            reference[request_index] = ReferenceRequest{};
        }
    }

    const os::kernel::BlockRequestQueueStatistics statistics = queue.Statistics();
    test_context.ExpectRandom(
        consistent &&
            queue.Validate() == os::kernel::BlockRequestQueueStatus::Succeeded &&
            statistics.active_request_count == OS_TEST_BLOCK_RANDOM_EMPTY_VALUE &&
            statistics.submission_count ==
                statistics.successful_completion_count +
                    statistics.device_error_completion_count +
                    statistics.timeout_completion_count +
                    statistics.cancellation_count &&
            statistics.submission_count == statistics.reap_count &&
            statistics.issued_request_count == OS_TEST_BLOCK_RANDOM_EMPTY_VALUE &&
            statistics.peak_issued_request_count > OS_TEST_BLOCK_RANDOM_EMPTY_VALUE &&
            statistics.peak_issued_request_count <=
                OS_TEST_BLOCK_RANDOM_MAXIMUM_OUTSTANDING_REQUEST_COUNT,
        OS_TEST_BLOCK_RANDOM_REFERENCE_MODEL, OS_TEST_BLOCK_RANDOM_SEED,
        OS_TEST_BLOCK_RANDOM_OPERATION_COUNT);
    return test_context.ExitCode();
}

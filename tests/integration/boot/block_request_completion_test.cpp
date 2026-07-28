#include "os/kernel/device/block_request.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_BLOCK_COMPLETION_SUITE_NAME =
    "kernel/block_request_completion/integration";
constexpr std::string_view OS_TEST_BLOCK_COMPLETION_SINGLE_WINNER =
    "IRQ 完成与时钟超时竞争时必须只有第一个解析路径获胜";
constexpr std::string_view OS_TEST_BLOCK_COMPLETION_FORWARD_PROGRESS =
    "前一请求解析后必须继续签发队列头且保留设备错误";
constexpr uint64_t OS_TEST_BLOCK_COMPLETION_CAPACITY = 4ULL;
constexpr uint64_t OS_TEST_BLOCK_COMPLETION_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_BLOCK_COMPLETION_SINGLE_EVENT_COUNT = 1ULL;
constexpr uint64_t OS_TEST_BLOCK_COMPLETION_DEADLINE_NS = 250ULL;
constexpr uint64_t OS_TEST_BLOCK_COMPLETION_AFTER_DEADLINE_NS = 251ULL;
constexpr uint64_t OS_TEST_BLOCK_COMPLETION_OWNER_THREAD_INDEX = 9ULL;

}

int main() {
    os::test::TestContext test_context{OS_TEST_BLOCK_COMPLETION_SUITE_NAME};
    os::kernel::BlockRequest storage[OS_TEST_BLOCK_COMPLETION_CAPACITY]{};
    os::kernel::BlockRequestQueue queue{};
    uint64_t first_identifier = OS_TEST_BLOCK_COMPLETION_EMPTY_VALUE;
    uint64_t second_identifier = OS_TEST_BLOCK_COMPLETION_EMPTY_VALUE;
    os::kernel::BlockRequest request{};
    bool issued = false;
    bool resolved = false;

    const bool setup_succeeded =
        queue.Initialize(storage, OS_TEST_BLOCK_COMPLETION_CAPACITY) ==
            os::kernel::BlockRequestQueueStatus::Succeeded &&
        queue.Submit(os::kernel::BlockOperation::Flush, OS_TEST_BLOCK_COMPLETION_EMPTY_VALUE,
                     nullptr, OS_TEST_BLOCK_COMPLETION_EMPTY_VALUE,
                     OS_TEST_BLOCK_COMPLETION_OWNER_THREAD_INDEX,
                     OS_TEST_BLOCK_COMPLETION_DEADLINE_NS, first_identifier) ==
            os::kernel::BlockRequestQueueStatus::Succeeded &&
        queue.Submit(os::kernel::BlockOperation::Flush, OS_TEST_BLOCK_COMPLETION_EMPTY_VALUE,
                     nullptr, OS_TEST_BLOCK_COMPLETION_EMPTY_VALUE,
                     OS_TEST_BLOCK_COMPLETION_OWNER_THREAD_INDEX,
                     OS_TEST_BLOCK_COMPLETION_DEADLINE_NS, second_identifier) ==
            os::kernel::BlockRequestQueueStatus::Succeeded &&
        queue.IssueNext(request, issued) == os::kernel::BlockRequestQueueStatus::Succeeded &&
        issued && request.identifier == first_identifier;
    const bool irq_wins =
        setup_succeeded &&
        queue.Complete(first_identifier, os::kernel::BlockRequestResult::Succeeded) ==
            os::kernel::BlockRequestQueueStatus::Succeeded &&
        queue.ResolveTimeout(OS_TEST_BLOCK_COMPLETION_AFTER_DEADLINE_NS, request, resolved) ==
            os::kernel::BlockRequestQueueStatus::Succeeded &&
        !resolved &&
        queue.Complete(first_identifier, os::kernel::BlockRequestResult::TimedOut) ==
            os::kernel::BlockRequestQueueStatus::RequestAlreadyResolved;
    test_context.Expect(irq_wins, OS_TEST_BLOCK_COMPLETION_SINGLE_WINNER);

    const bool forward_progress =
        queue.IssueNext(request, issued) == os::kernel::BlockRequestQueueStatus::Succeeded &&
        issued && request.identifier == second_identifier &&
        queue.Complete(second_identifier, os::kernel::BlockRequestResult::DeviceError) ==
            os::kernel::BlockRequestQueueStatus::Succeeded &&
        queue.Read(second_identifier, request) ==
            os::kernel::BlockRequestQueueStatus::Succeeded &&
        request.result == os::kernel::BlockRequestResult::DeviceError &&
        queue.Validate() == os::kernel::BlockRequestQueueStatus::Succeeded &&
        queue.Statistics().successful_completion_count ==
            OS_TEST_BLOCK_COMPLETION_SINGLE_EVENT_COUNT &&
        queue.Statistics().device_error_completion_count ==
            OS_TEST_BLOCK_COMPLETION_SINGLE_EVENT_COUNT &&
        queue.Statistics().duplicate_resolution_count ==
            OS_TEST_BLOCK_COMPLETION_SINGLE_EVENT_COUNT;
    test_context.Expect(forward_progress, OS_TEST_BLOCK_COMPLETION_FORWARD_PROGRESS);
    return test_context.ExitCode();
}

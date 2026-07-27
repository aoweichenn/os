#include "os/kernel/time/deadline_queue.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_DEADLINE_QUEUE_SUITE_NAME =
    "kernel/time/deadline_queue/unit";
constexpr std::string_view OS_TEST_DEADLINE_QUEUE_STABLE_ORDER =
    "deadline 必须按绝对时间和登记顺序稳定到期";
constexpr std::string_view OS_TEST_DEADLINE_QUEUE_CANCELLATION =
    "条件赢家取消 deadline 后不得再次到期";
constexpr std::string_view OS_TEST_DEADLINE_QUEUE_BOUNDARIES =
    "重复登记、非法 Thread、活动队列 reset 必须明确失败";
constexpr uint64_t OS_TEST_DEADLINE_QUEUE_CAPACITY = 8ULL;
constexpr uint64_t OS_TEST_DEADLINE_QUEUE_FIRST_THREAD = 1ULL;
constexpr uint64_t OS_TEST_DEADLINE_QUEUE_SECOND_THREAD = 2ULL;
constexpr uint64_t OS_TEST_DEADLINE_QUEUE_THIRD_THREAD = 3ULL;
constexpr uint64_t OS_TEST_DEADLINE_QUEUE_EARLY_DEADLINE_NS = 100ULL;
constexpr uint64_t OS_TEST_DEADLINE_QUEUE_LATE_DEADLINE_NS = 200ULL;
constexpr uint64_t OS_TEST_DEADLINE_QUEUE_BEFORE_DEADLINE_NS = 99ULL;

}

int main() {
    os::test::TestContext test_context{
        OS_TEST_DEADLINE_QUEUE_SUITE_NAME};
    os::kernel::DeadlineQueue queue{};
    bool valid =
        queue.Initialize(OS_TEST_DEADLINE_QUEUE_CAPACITY) ==
            os::kernel::DeadlineQueueStatus::Succeeded &&
        queue.Schedule(OS_TEST_DEADLINE_QUEUE_SECOND_THREAD,
                       OS_TEST_DEADLINE_QUEUE_LATE_DEADLINE_NS) ==
            os::kernel::DeadlineQueueStatus::Succeeded &&
        queue.Schedule(OS_TEST_DEADLINE_QUEUE_FIRST_THREAD,
                       OS_TEST_DEADLINE_QUEUE_EARLY_DEADLINE_NS) ==
            os::kernel::DeadlineQueueStatus::Succeeded &&
        queue.Schedule(OS_TEST_DEADLINE_QUEUE_THIRD_THREAD,
                       OS_TEST_DEADLINE_QUEUE_EARLY_DEADLINE_NS) ==
            os::kernel::DeadlineQueueStatus::Succeeded;

    uint64_t expired_thread_index =
        os::kernel::OS_KERNEL_DEADLINE_QUEUE_INVALID_THREAD_INDEX;
    bool expired = true;
    valid =
        valid &&
        queue.PeekExpired(OS_TEST_DEADLINE_QUEUE_BEFORE_DEADLINE_NS,
                          expired_thread_index, expired) ==
            os::kernel::DeadlineQueueStatus::Succeeded &&
        !expired &&
        queue.PeekExpired(OS_TEST_DEADLINE_QUEUE_EARLY_DEADLINE_NS,
                          expired_thread_index, expired) ==
            os::kernel::DeadlineQueueStatus::Succeeded &&
        expired &&
        expired_thread_index == OS_TEST_DEADLINE_QUEUE_FIRST_THREAD &&
        queue.Resolve(OS_TEST_DEADLINE_QUEUE_FIRST_THREAD,
                      os::kernel::DeadlineResolution::Expired) ==
            os::kernel::DeadlineQueueStatus::Succeeded &&
        queue.PeekExpired(OS_TEST_DEADLINE_QUEUE_EARLY_DEADLINE_NS,
                          expired_thread_index, expired) ==
            os::kernel::DeadlineQueueStatus::Succeeded &&
        expired &&
        expired_thread_index == OS_TEST_DEADLINE_QUEUE_THIRD_THREAD;
    test_context.Expect(valid, OS_TEST_DEADLINE_QUEUE_STABLE_ORDER);

    bool scheduled = false;
    const bool cancellation_valid =
        queue.Resolve(OS_TEST_DEADLINE_QUEUE_THIRD_THREAD,
                      os::kernel::DeadlineResolution::Cancelled) ==
            os::kernel::DeadlineQueueStatus::Succeeded &&
        queue.Contains(OS_TEST_DEADLINE_QUEUE_THIRD_THREAD, scheduled) ==
            os::kernel::DeadlineQueueStatus::Succeeded &&
        !scheduled &&
        queue.PeekExpired(OS_TEST_DEADLINE_QUEUE_EARLY_DEADLINE_NS,
                          expired_thread_index, expired) ==
            os::kernel::DeadlineQueueStatus::Succeeded &&
        !expired;
    test_context.Expect(cancellation_valid,
                        OS_TEST_DEADLINE_QUEUE_CANCELLATION);

    const bool boundaries_valid =
        queue.Schedule(OS_TEST_DEADLINE_QUEUE_SECOND_THREAD,
                       OS_TEST_DEADLINE_QUEUE_LATE_DEADLINE_NS) ==
            os::kernel::DeadlineQueueStatus::AlreadyScheduled &&
        queue.Schedule(OS_TEST_DEADLINE_QUEUE_CAPACITY,
                       OS_TEST_DEADLINE_QUEUE_LATE_DEADLINE_NS) ==
            os::kernel::DeadlineQueueStatus::InvalidThreadIndex &&
        queue.Reset() ==
            os::kernel::DeadlineQueueStatus::ActiveEntriesRemain &&
        queue.Resolve(OS_TEST_DEADLINE_QUEUE_SECOND_THREAD,
                      os::kernel::DeadlineResolution::Expired) ==
            os::kernel::DeadlineQueueStatus::Succeeded &&
        queue.Validate() == os::kernel::DeadlineQueueStatus::Succeeded &&
        queue.Reset() == os::kernel::DeadlineQueueStatus::Succeeded;
    test_context.Expect(boundaries_valid,
                        OS_TEST_DEADLINE_QUEUE_BOUNDARIES);
    return test_context.ExitCode();
}

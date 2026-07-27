#include "os/kernel/time/deadline_queue.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_DEADLINE_RANDOM_SUITE_NAME =
    "kernel/time/deadline_queue/randomized";
constexpr std::string_view OS_TEST_DEADLINE_RANDOM_REFERENCE_MODEL =
    "十万步虚拟时间模型不得丢失、重复到期或残留 deadline";
constexpr uint64_t OS_TEST_DEADLINE_RANDOM_CAPACITY = 64ULL;
constexpr uint64_t OS_TEST_DEADLINE_RANDOM_OPERATION_COUNT = 100000ULL;
constexpr uint64_t OS_TEST_DEADLINE_RANDOM_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_DEADLINE_RANDOM_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_TEST_DEADLINE_RANDOM_MAXIMUM_DELTA_NS = 1024ULL;
constexpr uint64_t OS_TEST_DEADLINE_RANDOM_OPERATION_KIND_COUNT = 4ULL;
constexpr uint64_t OS_TEST_DEADLINE_RANDOM_SEED = 0x444541444C494E45ULL;
constexpr uint64_t OS_TEST_DEADLINE_RANDOM_MULTIPLIER =
    6364136223846793005ULL;
constexpr uint64_t OS_TEST_DEADLINE_RANDOM_INCREMENT =
    1442695040888963407ULL;

struct ReferenceDeadline final {
    uint64_t deadline_nanoseconds;
    uint64_t registration_sequence;
    bool active;
};

[[nodiscard]] uint64_t NextRandom(uint64_t &state) noexcept {
    state =
        state * OS_TEST_DEADLINE_RANDOM_MULTIPLIER +
        OS_TEST_DEADLINE_RANDOM_INCREMENT;
    return state;
}

[[nodiscard]] uint64_t FindReferenceHead(
    const ReferenceDeadline *const deadlines) noexcept {
    uint64_t head_thread_index =
        os::kernel::OS_KERNEL_DEADLINE_QUEUE_INVALID_THREAD_INDEX;
    for (uint64_t thread_index = OS_TEST_DEADLINE_RANDOM_EMPTY_VALUE;
         thread_index < OS_TEST_DEADLINE_RANDOM_CAPACITY; ++thread_index) {
        if (!deadlines[thread_index].active) {
            continue;
        }
        if (head_thread_index ==
                os::kernel::OS_KERNEL_DEADLINE_QUEUE_INVALID_THREAD_INDEX ||
            deadlines[thread_index].deadline_nanoseconds <
                deadlines[head_thread_index].deadline_nanoseconds ||
            (deadlines[thread_index].deadline_nanoseconds ==
                 deadlines[head_thread_index].deadline_nanoseconds &&
             deadlines[thread_index].registration_sequence <
                 deadlines[head_thread_index].registration_sequence)) {
            head_thread_index = thread_index;
        }
    }
    return head_thread_index;
}

}

int main() {
    os::test::TestContext test_context{
        OS_TEST_DEADLINE_RANDOM_SUITE_NAME};
    os::kernel::DeadlineQueue queue{};
    ReferenceDeadline reference[OS_TEST_DEADLINE_RANDOM_CAPACITY]{};
    bool consistent =
        queue.Initialize(OS_TEST_DEADLINE_RANDOM_CAPACITY) ==
        os::kernel::DeadlineQueueStatus::Succeeded;
    uint64_t random_state = OS_TEST_DEADLINE_RANDOM_SEED;
    uint64_t now_nanoseconds = OS_TEST_DEADLINE_RANDOM_EMPTY_VALUE;
    uint64_t next_sequence = OS_TEST_DEADLINE_RANDOM_COUNTER_INCREMENT;

    for (uint64_t operation_index = OS_TEST_DEADLINE_RANDOM_EMPTY_VALUE;
         consistent &&
         operation_index < OS_TEST_DEADLINE_RANDOM_OPERATION_COUNT;
         ++operation_index) {
        const uint64_t operation_kind =
            NextRandom(random_state) %
            OS_TEST_DEADLINE_RANDOM_OPERATION_KIND_COUNT;
        const uint64_t thread_index =
            NextRandom(random_state) % OS_TEST_DEADLINE_RANDOM_CAPACITY;
        if (operation_kind == OS_TEST_DEADLINE_RANDOM_EMPTY_VALUE) {
            if (!reference[thread_index].active) {
                const uint64_t deadline_nanoseconds =
                    now_nanoseconds +
                    NextRandom(random_state) %
                        OS_TEST_DEADLINE_RANDOM_MAXIMUM_DELTA_NS;
                consistent =
                    queue.Schedule(thread_index, deadline_nanoseconds) ==
                    os::kernel::DeadlineQueueStatus::Succeeded;
                reference[thread_index] = ReferenceDeadline{
                    .deadline_nanoseconds = deadline_nanoseconds,
                    .registration_sequence = next_sequence,
                    .active = true,
                };
                next_sequence +=
                    OS_TEST_DEADLINE_RANDOM_COUNTER_INCREMENT;
            }
        } else if (operation_kind ==
                   OS_TEST_DEADLINE_RANDOM_COUNTER_INCREMENT) {
            if (reference[thread_index].active) {
                consistent =
                    queue.Resolve(
                        thread_index,
                        os::kernel::DeadlineResolution::Cancelled) ==
                    os::kernel::DeadlineQueueStatus::Succeeded;
                reference[thread_index].active = false;
            }
        } else {
            now_nanoseconds +=
                NextRandom(random_state) %
                OS_TEST_DEADLINE_RANDOM_MAXIMUM_DELTA_NS;
            while (consistent) {
                const uint64_t reference_head =
                    FindReferenceHead(reference);
                const bool reference_expired =
                    reference_head !=
                        os::kernel::
                            OS_KERNEL_DEADLINE_QUEUE_INVALID_THREAD_INDEX &&
                    reference[reference_head].deadline_nanoseconds <=
                        now_nanoseconds;
                uint64_t queue_head =
                    os::kernel::
                        OS_KERNEL_DEADLINE_QUEUE_INVALID_THREAD_INDEX;
                bool queue_expired = false;
                consistent =
                    queue.PeekExpired(now_nanoseconds, queue_head,
                                      queue_expired) ==
                        os::kernel::DeadlineQueueStatus::Succeeded &&
                    queue_expired == reference_expired &&
                    (!queue_expired || queue_head == reference_head);
                if (!consistent || !queue_expired) {
                    break;
                }
                consistent =
                    queue.Resolve(
                        queue_head,
                        os::kernel::DeadlineResolution::Expired) ==
                    os::kernel::DeadlineQueueStatus::Succeeded;
                reference[queue_head].active = false;
            }
        }
        if (operation_index % OS_TEST_DEADLINE_RANDOM_CAPACITY ==
                OS_TEST_DEADLINE_RANDOM_EMPTY_VALUE &&
            queue.Validate() !=
                os::kernel::DeadlineQueueStatus::Succeeded) {
            consistent = false;
        }
    }

    now_nanoseconds = UINT64_MAX;
    while (consistent) {
        const uint64_t reference_head = FindReferenceHead(reference);
        uint64_t queue_head =
            os::kernel::OS_KERNEL_DEADLINE_QUEUE_INVALID_THREAD_INDEX;
        bool queue_expired = false;
        consistent =
            queue.PeekExpired(now_nanoseconds, queue_head, queue_expired) ==
                os::kernel::DeadlineQueueStatus::Succeeded &&
            (reference_head !=
                 os::kernel::OS_KERNEL_DEADLINE_QUEUE_INVALID_THREAD_INDEX) ==
                queue_expired &&
            (!queue_expired || queue_head == reference_head);
        if (!consistent || !queue_expired) {
            break;
        }
        consistent =
            queue.Resolve(queue_head,
                          os::kernel::DeadlineResolution::Expired) ==
            os::kernel::DeadlineQueueStatus::Succeeded;
        reference[queue_head].active = false;
    }

    const os::kernel::DeadlineQueueStatistics statistics =
        queue.Statistics();
    test_context.ExpectRandom(
        consistent &&
            queue.Validate() ==
                os::kernel::DeadlineQueueStatus::Succeeded &&
            statistics.active_entry_count ==
                OS_TEST_DEADLINE_RANDOM_EMPTY_VALUE &&
            statistics.schedule_count ==
                statistics.expiration_count +
                    statistics.cancellation_count,
        OS_TEST_DEADLINE_RANDOM_REFERENCE_MODEL,
        OS_TEST_DEADLINE_RANDOM_SEED,
        OS_TEST_DEADLINE_RANDOM_OPERATION_COUNT);
    return test_context.ExitCode();
}

#include <os/kernel/process/file_readahead_request.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_FILE_READAHEAD_REQUEST_RANDOMIZED_SUITE_NAME =
    "kernel/file_readahead_request/randomized";
constexpr std::string_view OS_TEST_FILE_READAHEAD_REQUEST_RANDOMIZED_INVARIANT =
    "固定种子十万步 enqueue、FIFO acquire、乱序 complete、满载拒绝和槽位复用必须匹配参考模型";
constexpr uint64_t OS_TEST_FILE_READAHEAD_REQUEST_RANDOMIZED_SEED = 0x5245414441484541ULL;
constexpr uint64_t OS_TEST_FILE_READAHEAD_REQUEST_RANDOMIZED_STEP_COUNT = 100000ULL;
constexpr uint64_t OS_TEST_FILE_READAHEAD_REQUEST_RANDOMIZED_MULTIPLIER = 6364136223846793005ULL;
constexpr uint64_t OS_TEST_FILE_READAHEAD_REQUEST_RANDOMIZED_INCREMENT = 1442695040888963407ULL;
constexpr uint64_t OS_TEST_FILE_READAHEAD_REQUEST_RANDOMIZED_CAPACITY = 17ULL;
constexpr uint64_t OS_TEST_FILE_READAHEAD_REQUEST_RANDOMIZED_OPERATION_COUNT = 3ULL;
constexpr uint64_t OS_TEST_FILE_READAHEAD_REQUEST_RANDOMIZED_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_FILE_READAHEAD_REQUEST_RANDOMIZED_FIRST_VALUE = 1ULL;

enum class ReferenceState : uint64_t {
    Free,
    Queued,
    Running,
};

struct ReferenceSlot final {
    os::kernel::FileReadaheadRequestToken token;
    uint64_t request_identifier;
    ReferenceState state;
};

struct RandomizedModel final {
    os::kernel::FileReadaheadRequestSlot slots[OS_TEST_FILE_READAHEAD_REQUEST_RANDOMIZED_CAPACITY];
    uint64_t ready_storage[OS_TEST_FILE_READAHEAD_REQUEST_RANDOMIZED_CAPACITY];
    ReferenceSlot reference[OS_TEST_FILE_READAHEAD_REQUEST_RANDOMIZED_CAPACITY];
    uint64_t fifo[OS_TEST_FILE_READAHEAD_REQUEST_RANDOMIZED_CAPACITY];
    os::kernel::FileReadaheadRequestQueue queue;
    os::kernel::fs::Vfs vfs;
    uint64_t fifo_head;
    uint64_t fifo_tail;
    uint64_t queued_count;
    uint64_t running_count;
    uint64_t next_request_identifier;
    uint64_t enqueue_count;
    uint64_t acquisition_count;
    uint64_t completion_count;
    uint64_t capacity_rejection_count;
    uint64_t peak_active_count;
};

[[nodiscard]] uint64_t NextRandom(uint64_t &state) noexcept {
    state = state * OS_TEST_FILE_READAHEAD_REQUEST_RANDOMIZED_MULTIPLIER +
            OS_TEST_FILE_READAHEAD_REQUEST_RANDOMIZED_INCREMENT;
    return state;
}

[[nodiscard]] os::kernel::FileReadaheadRequest
MakeRequest(RandomizedModel &model, const uint64_t request_identifier) noexcept {
    return os::kernel::FileReadaheadRequest{
        .vfs = &model.vfs,
        .open_file =
            os::kernel::fs::OpenFile{
                .path =
                    os::kernel::fs::Path{
                        .mount_identifier = OS_TEST_FILE_READAHEAD_REQUEST_RANDOMIZED_FIRST_VALUE,
                        .vnode =
                            os::kernel::fs::Vnode{
                                .superblock = nullptr,
                                .identifier = 41ULL,
                                .generation = 7ULL,
                                .type = os::kernel::fs::NodeType::RegularFile,
                            },
                    },
                .offset_bytes = OS_TEST_FILE_READAHEAD_REQUEST_RANDOMIZED_EMPTY_VALUE,
                .readable = true,
                .writable = false,
                .open = true,
            },
        .start_page_index = request_identifier,
        .page_count = OS_TEST_FILE_READAHEAD_REQUEST_RANDOMIZED_FIRST_VALUE,
        .policy_generation =
            request_identifier + OS_TEST_FILE_READAHEAD_REQUEST_RANDOMIZED_FIRST_VALUE,
    };
}

[[nodiscard]] bool Initialize(RandomizedModel &model) noexcept {
    model.next_request_identifier = OS_TEST_FILE_READAHEAD_REQUEST_RANDOMIZED_FIRST_VALUE;
    return model.queue.Initialize(model.slots, model.ready_storage,
                                  OS_TEST_FILE_READAHEAD_REQUEST_RANDOMIZED_CAPACITY) ==
           os::kernel::FileReadaheadRequestStatus::Succeeded;
}

void UpdatePeak(RandomizedModel &model) noexcept {
    const uint64_t active_count = model.queued_count + model.running_count;
    if (active_count > model.peak_active_count) {
        model.peak_active_count = active_count;
    }
}

[[nodiscard]] bool Enqueue(RandomizedModel &model) noexcept {
    const uint64_t request_identifier = model.next_request_identifier++;
    os::kernel::FileReadaheadRequestToken token{};
    const os::kernel::FileReadaheadRequestStatus status =
        model.queue.Enqueue(MakeRequest(model, request_identifier), token);
    if (model.queued_count + model.running_count ==
        OS_TEST_FILE_READAHEAD_REQUEST_RANDOMIZED_CAPACITY) {
        ++model.capacity_rejection_count;
        return status == os::kernel::FileReadaheadRequestStatus::CapacityExhausted;
    }
    if (status != os::kernel::FileReadaheadRequestStatus::Succeeded ||
        token.slot_index >= OS_TEST_FILE_READAHEAD_REQUEST_RANDOMIZED_CAPACITY ||
        token.generation == OS_TEST_FILE_READAHEAD_REQUEST_RANDOMIZED_EMPTY_VALUE ||
        model.reference[token.slot_index].state != ReferenceState::Free) {
        return false;
    }
    model.reference[token.slot_index] = ReferenceSlot{
        .token = token,
        .request_identifier = request_identifier,
        .state = ReferenceState::Queued,
    };
    model.fifo[model.fifo_tail] = request_identifier;
    model.fifo_tail = (model.fifo_tail + OS_TEST_FILE_READAHEAD_REQUEST_RANDOMIZED_FIRST_VALUE) %
                      OS_TEST_FILE_READAHEAD_REQUEST_RANDOMIZED_CAPACITY;
    ++model.queued_count;
    ++model.enqueue_count;
    UpdatePeak(model);
    return true;
}

[[nodiscard]] bool Acquire(RandomizedModel &model) noexcept {
    os::kernel::FileReadaheadRequestToken token{};
    os::kernel::FileReadaheadRequest request{};
    const os::kernel::FileReadaheadRequestStatus status = model.queue.Acquire(token, request);
    if (model.queued_count == OS_TEST_FILE_READAHEAD_REQUEST_RANDOMIZED_EMPTY_VALUE) {
        return status == os::kernel::FileReadaheadRequestStatus::NoQueuedRequest;
    }
    const uint64_t expected_identifier = model.fifo[model.fifo_head];
    if (status != os::kernel::FileReadaheadRequestStatus::Succeeded ||
        token.slot_index >= OS_TEST_FILE_READAHEAD_REQUEST_RANDOMIZED_CAPACITY ||
        model.reference[token.slot_index].state != ReferenceState::Queued ||
        model.reference[token.slot_index].request_identifier != expected_identifier ||
        model.reference[token.slot_index].token.generation != token.generation ||
        request.vfs != &model.vfs || request.start_page_index != expected_identifier ||
        request.policy_generation !=
            expected_identifier + OS_TEST_FILE_READAHEAD_REQUEST_RANDOMIZED_FIRST_VALUE) {
        return false;
    }
    model.fifo_head = (model.fifo_head + OS_TEST_FILE_READAHEAD_REQUEST_RANDOMIZED_FIRST_VALUE) %
                      OS_TEST_FILE_READAHEAD_REQUEST_RANDOMIZED_CAPACITY;
    --model.queued_count;
    ++model.running_count;
    ++model.acquisition_count;
    model.reference[token.slot_index].state = ReferenceState::Running;
    return true;
}

[[nodiscard]] uint64_t FindRunningSlot(const RandomizedModel &model,
                                       const uint64_t ordinal) noexcept {
    uint64_t remaining = ordinal;
    for (uint64_t slot_index = OS_TEST_FILE_READAHEAD_REQUEST_RANDOMIZED_EMPTY_VALUE;
         slot_index < OS_TEST_FILE_READAHEAD_REQUEST_RANDOMIZED_CAPACITY; ++slot_index) {
        if (model.reference[slot_index].state != ReferenceState::Running) {
            continue;
        }
        if (remaining == OS_TEST_FILE_READAHEAD_REQUEST_RANDOMIZED_EMPTY_VALUE) {
            return slot_index;
        }
        --remaining;
    }
    return os::kernel::OS_KERNEL_FILE_READAHEAD_REQUEST_INVALID_SLOT_INDEX;
}

[[nodiscard]] bool Complete(RandomizedModel &model, uint64_t &random_state) noexcept {
    if (model.running_count == OS_TEST_FILE_READAHEAD_REQUEST_RANDOMIZED_EMPTY_VALUE) {
        return true;
    }
    const uint64_t slot_index =
        FindRunningSlot(model, NextRandom(random_state) % model.running_count);
    if (slot_index >= OS_TEST_FILE_READAHEAD_REQUEST_RANDOMIZED_CAPACITY) {
        return false;
    }
    const os::kernel::FileReadaheadRequestToken token = model.reference[slot_index].token;
    if (model.queue.Complete(token) != os::kernel::FileReadaheadRequestStatus::Succeeded ||
        model.queue.Complete(token) != os::kernel::FileReadaheadRequestStatus::InvalidToken) {
        return false;
    }
    model.reference[slot_index] = ReferenceSlot{};
    --model.running_count;
    ++model.completion_count;
    return true;
}

[[nodiscard]] bool StatisticsMatch(const RandomizedModel &model) noexcept {
    const os::kernel::FileReadaheadRequestStatistics statistics = model.queue.Statistics();
    return statistics.capacity == OS_TEST_FILE_READAHEAD_REQUEST_RANDOMIZED_CAPACITY &&
           statistics.active_request_count == model.queued_count + model.running_count &&
           statistics.queued_request_count == model.queued_count &&
           statistics.running_request_count == model.running_count &&
           statistics.peak_active_request_count == model.peak_active_count &&
           statistics.enqueue_count == model.enqueue_count &&
           statistics.acquisition_count == model.acquisition_count &&
           statistics.completion_count == model.completion_count &&
           statistics.capacity_rejection_count == model.capacity_rejection_count;
}

[[nodiscard]] bool RunRandomizedScenario() noexcept {
    RandomizedModel model{};
    if (!Initialize(model)) {
        return false;
    }
    uint64_t random_state = OS_TEST_FILE_READAHEAD_REQUEST_RANDOMIZED_SEED;
    for (uint64_t step = OS_TEST_FILE_READAHEAD_REQUEST_RANDOMIZED_EMPTY_VALUE;
         step < OS_TEST_FILE_READAHEAD_REQUEST_RANDOMIZED_STEP_COUNT; ++step) {
        const uint64_t operation =
            NextRandom(random_state) % OS_TEST_FILE_READAHEAD_REQUEST_RANDOMIZED_OPERATION_COUNT;
        const bool succeeded = operation == OS_TEST_FILE_READAHEAD_REQUEST_RANDOMIZED_EMPTY_VALUE
                                   ? Enqueue(model)
                               : operation == OS_TEST_FILE_READAHEAD_REQUEST_RANDOMIZED_FIRST_VALUE
                                   ? Acquire(model)
                                   : Complete(model, random_state);
        if (!succeeded || !StatisticsMatch(model) ||
            model.queue.Validate() != os::kernel::FileReadaheadRequestStatus::Succeeded) {
            return false;
        }
    }
    while (model.queued_count != OS_TEST_FILE_READAHEAD_REQUEST_RANDOMIZED_EMPTY_VALUE) {
        if (!Acquire(model)) {
            return false;
        }
    }
    while (model.running_count != OS_TEST_FILE_READAHEAD_REQUEST_RANDOMIZED_EMPTY_VALUE) {
        if (!Complete(model, random_state)) {
            return false;
        }
    }
    return StatisticsMatch(model) && model.enqueue_count == model.completion_count &&
           model.queue.Validate() == os::kernel::FileReadaheadRequestStatus::Succeeded;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_FILE_READAHEAD_REQUEST_RANDOMIZED_SUITE_NAME};
    test_context.Expect(RunRandomizedScenario(),
                        OS_TEST_FILE_READAHEAD_REQUEST_RANDOMIZED_INVARIANT);
    return test_context.ExitCode();
}

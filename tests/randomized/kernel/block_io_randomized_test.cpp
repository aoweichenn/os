#include <os/kernel/process/block_io.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_BLOCK_IO_RANDOM_SUITE_NAME = "kernel/block_io/randomized";
constexpr std::string_view OS_TEST_BLOCK_IO_RANDOM_REFERENCE_MODEL =
    "十万步 BlockIo 模型必须保持 lost-wakeup、单赢家、abandon 和 generation 守恒";
constexpr uint64_t OS_TEST_BLOCK_IO_RANDOM_CAPACITY = 32ULL;
constexpr uint64_t OS_TEST_BLOCK_IO_RANDOM_OPERATION_COUNT = 100000ULL;
constexpr uint64_t OS_TEST_BLOCK_IO_RANDOM_OPERATION_KIND_COUNT = 6ULL;
constexpr uint64_t OS_TEST_BLOCK_IO_RANDOM_REGISTER_OPERATION = 0ULL;
constexpr uint64_t OS_TEST_BLOCK_IO_RANDOM_PREPARE_OPERATION = 1ULL;
constexpr uint64_t OS_TEST_BLOCK_IO_RANDOM_COMPLETE_OPERATION = 2ULL;
constexpr uint64_t OS_TEST_BLOCK_IO_RANDOM_TAKE_OPERATION = 3ULL;
constexpr uint64_t OS_TEST_BLOCK_IO_RANDOM_ABANDON_OPERATION = 4ULL;
constexpr uint64_t OS_TEST_BLOCK_IO_RANDOM_LATE_COMPLETE_OPERATION = 5ULL;
constexpr uint64_t OS_TEST_BLOCK_IO_RANDOM_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_BLOCK_IO_RANDOM_FIRST_REQUEST_IDENTIFIER = 1ULL;
constexpr uint64_t OS_TEST_BLOCK_IO_RANDOM_SEED = 0x424C4F434B494F33ULL;
constexpr uint64_t OS_TEST_BLOCK_IO_RANDOM_MULTIPLIER = 6364136223846793005ULL;
constexpr uint64_t OS_TEST_BLOCK_IO_RANDOM_INCREMENT = 1442695040888963407ULL;
constexpr uint64_t OS_TEST_BLOCK_IO_RANDOM_TERMINAL_RESULT_COUNT = 4ULL;

struct ReferenceRequest final {
    uint64_t request_identifier;
    os::kernel::BlockIoTicket ticket;
    os::kernel::BlockIoState state;
    os::kernel::BlockRequestResult result;
};

[[nodiscard]] uint64_t NextRandom(uint64_t &state) noexcept {
    state = state * OS_TEST_BLOCK_IO_RANDOM_MULTIPLIER + OS_TEST_BLOCK_IO_RANDOM_INCREMENT;
    return state;
}

[[nodiscard]] os::kernel::BlockRequestResult RandomTerminalResult(uint64_t &state) noexcept {
    return static_cast<os::kernel::BlockRequestResult>(
        static_cast<uint64_t>(os::kernel::BlockRequestResult::Succeeded) +
        NextRandom(state) % OS_TEST_BLOCK_IO_RANDOM_TERMINAL_RESULT_COUNT);
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_BLOCK_IO_RANDOM_SUITE_NAME};
    os::kernel::BlockIoSlot storage[OS_TEST_BLOCK_IO_RANDOM_CAPACITY]{};
    os::kernel::BlockIoCoordinator coordinator{};
    ReferenceRequest reference[OS_TEST_BLOCK_IO_RANDOM_CAPACITY]{};
    uint64_t random_state = OS_TEST_BLOCK_IO_RANDOM_SEED;
    uint64_t next_request_identifier = OS_TEST_BLOCK_IO_RANDOM_FIRST_REQUEST_IDENTIFIER;
    bool consistent = coordinator.Initialize(storage, OS_TEST_BLOCK_IO_RANDOM_CAPACITY) ==
                      os::kernel::BlockIoStatus::Succeeded;

    for (uint64_t operation_index = OS_TEST_BLOCK_IO_RANDOM_EMPTY_VALUE;
         consistent && operation_index < OS_TEST_BLOCK_IO_RANDOM_OPERATION_COUNT;
         ++operation_index) {
        const uint64_t owner_thread_index =
            NextRandom(random_state) % OS_TEST_BLOCK_IO_RANDOM_CAPACITY;
        ReferenceRequest &request = reference[owner_thread_index];
        const uint64_t operation_kind =
            NextRandom(random_state) % OS_TEST_BLOCK_IO_RANDOM_OPERATION_KIND_COUNT;
        if (operation_kind == OS_TEST_BLOCK_IO_RANDOM_REGISTER_OPERATION) {
            if (request.state == os::kernel::BlockIoState::Free) {
                os::kernel::BlockIoTicket ticket{};
                consistent = coordinator.Register(next_request_identifier, owner_thread_index,
                                                  ticket) == os::kernel::BlockIoStatus::Succeeded;
                if (consistent) {
                    request = ReferenceRequest{
                        .request_identifier = next_request_identifier,
                        .ticket = ticket,
                        .state = os::kernel::BlockIoState::Registered,
                        .result = os::kernel::BlockRequestResult::None,
                    };
                    ++next_request_identifier;
                }
            }
        } else if (operation_kind == OS_TEST_BLOCK_IO_RANDOM_PREPARE_OPERATION) {
            if (request.state == os::kernel::BlockIoState::Registered) {
                bool wait_required = false;
                consistent = coordinator.PrepareWait(request.ticket, wait_required) ==
                                 os::kernel::BlockIoStatus::Succeeded &&
                             wait_required;
                request.state = os::kernel::BlockIoState::Waiting;
            } else if (request.state == os::kernel::BlockIoState::Completed) {
                bool wait_required = true;
                consistent = coordinator.PrepareWait(request.ticket, wait_required) ==
                                 os::kernel::BlockIoStatus::Succeeded &&
                             !wait_required;
            }
        } else if (operation_kind == OS_TEST_BLOCK_IO_RANDOM_COMPLETE_OPERATION) {
            if (request.state == os::kernel::BlockIoState::Registered ||
                request.state == os::kernel::BlockIoState::Waiting) {
                const bool expected_wake = request.state == os::kernel::BlockIoState::Waiting;
                const os::kernel::BlockRequestResult result = RandomTerminalResult(random_state);
                os::kernel::BlockIoCompletionDecision decision{};
                consistent =
                    coordinator.Complete(owner_thread_index, request.request_identifier, result,
                                         decision) == os::kernel::BlockIoStatus::Succeeded &&
                    decision.wake_required == expected_wake && !decision.abandoned &&
                    decision.owner_thread_index == owner_thread_index;
                request.state = os::kernel::BlockIoState::Completed;
                request.result = result;
            }
        } else if (operation_kind == OS_TEST_BLOCK_IO_RANDOM_TAKE_OPERATION) {
            if (request.state == os::kernel::BlockIoState::Completed) {
                os::kernel::BlockRequestResult result = os::kernel::BlockRequestResult::None;
                consistent = coordinator.TakeResult(request.ticket, result) ==
                                 os::kernel::BlockIoStatus::Succeeded &&
                             result == request.result;
                request = ReferenceRequest{};
            }
        } else if (operation_kind == OS_TEST_BLOCK_IO_RANDOM_ABANDON_OPERATION) {
            if (request.state == os::kernel::BlockIoState::Registered ||
                request.state == os::kernel::BlockIoState::Waiting) {
                consistent =
                    coordinator.Abandon(request.ticket) == os::kernel::BlockIoStatus::Succeeded;
                request.state = os::kernel::BlockIoState::Abandoned;
            }
        } else if (operation_kind == OS_TEST_BLOCK_IO_RANDOM_LATE_COMPLETE_OPERATION) {
            if (request.state == os::kernel::BlockIoState::Abandoned) {
                os::kernel::BlockIoCompletionDecision decision{};
                consistent =
                    coordinator.Complete(owner_thread_index, request.request_identifier,
                                         os::kernel::BlockRequestResult::Cancelled,
                                         decision) == os::kernel::BlockIoStatus::Succeeded &&
                    !decision.wake_required && decision.abandoned;
                const os::kernel::BlockIoTicket stale_ticket = request.ticket;
                request = ReferenceRequest{};
                bool wait_required = false;
                consistent = consistent && coordinator.PrepareWait(stale_ticket, wait_required) ==
                                               os::kernel::BlockIoStatus::InvalidTicket;
            }
        } else {
            consistent = false;
        }
        if (operation_index % OS_TEST_BLOCK_IO_RANDOM_CAPACITY ==
                OS_TEST_BLOCK_IO_RANDOM_EMPTY_VALUE &&
            coordinator.Validate() != os::kernel::BlockIoStatus::Succeeded) {
            consistent = false;
        }
    }

    for (uint64_t owner_thread_index = OS_TEST_BLOCK_IO_RANDOM_EMPTY_VALUE;
         consistent && owner_thread_index < OS_TEST_BLOCK_IO_RANDOM_CAPACITY;
         ++owner_thread_index) {
        ReferenceRequest &request = reference[owner_thread_index];
        if (request.state == os::kernel::BlockIoState::Registered ||
            request.state == os::kernel::BlockIoState::Waiting) {
            os::kernel::BlockIoCompletionDecision decision{};
            consistent = coordinator.Complete(owner_thread_index, request.request_identifier,
                                              os::kernel::BlockRequestResult::Succeeded,
                                              decision) == os::kernel::BlockIoStatus::Succeeded;
            request.state = os::kernel::BlockIoState::Completed;
            request.result = os::kernel::BlockRequestResult::Succeeded;
        }
        if (request.state == os::kernel::BlockIoState::Completed) {
            os::kernel::BlockRequestResult result = os::kernel::BlockRequestResult::None;
            consistent = coordinator.TakeResult(request.ticket, result) ==
                             os::kernel::BlockIoStatus::Succeeded &&
                         result == request.result;
            request = ReferenceRequest{};
        } else if (request.state == os::kernel::BlockIoState::Abandoned) {
            os::kernel::BlockIoCompletionDecision decision{};
            consistent = coordinator.Complete(owner_thread_index, request.request_identifier,
                                              os::kernel::BlockRequestResult::Cancelled,
                                              decision) == os::kernel::BlockIoStatus::Succeeded &&
                         decision.abandoned;
            request = ReferenceRequest{};
        }
    }

    const os::kernel::BlockIoStatistics statistics = coordinator.Statistics();
    test_context.ExpectRandom(
        consistent && coordinator.Validate() == os::kernel::BlockIoStatus::Succeeded &&
            statistics.active_request_count == OS_TEST_BLOCK_IO_RANDOM_EMPTY_VALUE &&
            statistics.registration_count ==
                statistics.result_take_count + statistics.late_completion_count,
        OS_TEST_BLOCK_IO_RANDOM_REFERENCE_MODEL, OS_TEST_BLOCK_IO_RANDOM_SEED,
        OS_TEST_BLOCK_IO_RANDOM_OPERATION_COUNT);
    return test_context.ExitCode();
}

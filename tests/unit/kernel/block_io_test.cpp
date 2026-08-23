#include <os/kernel/process/block_io.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_BLOCK_IO_SUITE_NAME = "kernel/block_io/unit";
constexpr std::string_view OS_TEST_BLOCK_IO_LOST_WAKE =
    "完成先于 wait commit 时不得丢失完成或错误唤醒其他线程";
constexpr std::string_view OS_TEST_BLOCK_IO_WAIT_WAKE =
    "wait commit 后完成必须精确唤醒 owner 并只允许一次结果消费";
constexpr std::string_view OS_TEST_BLOCK_IO_ABANDON =
    "owner 遗弃后迟到完成必须回收槽且旧 generation ticket 失效";
constexpr uint64_t OS_TEST_BLOCK_IO_CAPACITY = 3ULL;
constexpr uint64_t OS_TEST_BLOCK_IO_FIRST_REQUEST_IDENTIFIER = 11ULL;
constexpr uint64_t OS_TEST_BLOCK_IO_SECOND_REQUEST_IDENTIFIER = 12ULL;
constexpr uint64_t OS_TEST_BLOCK_IO_THIRD_REQUEST_IDENTIFIER = 13ULL;
constexpr uint64_t OS_TEST_BLOCK_IO_FIRST_OWNER_THREAD_INDEX = 4ULL;
constexpr uint64_t OS_TEST_BLOCK_IO_SECOND_OWNER_THREAD_INDEX = 5ULL;
constexpr uint64_t OS_TEST_BLOCK_IO_THIRD_OWNER_THREAD_INDEX = 6ULL;
constexpr uint64_t OS_TEST_BLOCK_IO_EMPTY_VALUE = 0ULL;

}

int main() {
    os::test::TestContext test_context{OS_TEST_BLOCK_IO_SUITE_NAME};
    os::kernel::BlockIoSlot storage[OS_TEST_BLOCK_IO_CAPACITY]{};
    os::kernel::BlockIoCoordinator coordinator{};
    os::kernel::BlockIoTicket first_ticket{};
    os::kernel::BlockIoCompletionDecision decision{};
    bool wait_required = true;
    os::kernel::BlockRequestResult result = os::kernel::BlockRequestResult::None;
    const bool lost_wake_passed =
        coordinator.Register(OS_TEST_BLOCK_IO_FIRST_REQUEST_IDENTIFIER,
                             OS_TEST_BLOCK_IO_FIRST_OWNER_THREAD_INDEX,
                             first_ticket) == os::kernel::BlockIoStatus::NotInitialized &&
        coordinator.Initialize(storage, OS_TEST_BLOCK_IO_CAPACITY) ==
            os::kernel::BlockIoStatus::Succeeded &&
        coordinator.Register(OS_TEST_BLOCK_IO_FIRST_REQUEST_IDENTIFIER,
                             OS_TEST_BLOCK_IO_FIRST_OWNER_THREAD_INDEX,
                             first_ticket) == os::kernel::BlockIoStatus::Succeeded &&
        coordinator.Complete(OS_TEST_BLOCK_IO_FIRST_OWNER_THREAD_INDEX,
                             OS_TEST_BLOCK_IO_FIRST_REQUEST_IDENTIFIER,
                             os::kernel::BlockRequestResult::Succeeded,
                             decision) == os::kernel::BlockIoStatus::Succeeded &&
        !decision.wake_required && !decision.abandoned &&
        decision.owner_thread_index == OS_TEST_BLOCK_IO_FIRST_OWNER_THREAD_INDEX &&
        coordinator.PrepareWait(first_ticket, wait_required) ==
            os::kernel::BlockIoStatus::Succeeded &&
        !wait_required &&
        coordinator.TakeResult(first_ticket, result) == os::kernel::BlockIoStatus::Succeeded &&
        result == os::kernel::BlockRequestResult::Succeeded &&
        coordinator.TakeResult(first_ticket, result) == os::kernel::BlockIoStatus::InvalidTicket;
    test_context.Expect(lost_wake_passed, OS_TEST_BLOCK_IO_LOST_WAKE);

    os::kernel::BlockIoTicket second_ticket{};
    wait_required = false;
    const bool wait_wake_passed =
        coordinator.Register(OS_TEST_BLOCK_IO_SECOND_REQUEST_IDENTIFIER,
                             OS_TEST_BLOCK_IO_SECOND_OWNER_THREAD_INDEX,
                             second_ticket) == os::kernel::BlockIoStatus::Succeeded &&
        coordinator.Register(OS_TEST_BLOCK_IO_THIRD_REQUEST_IDENTIFIER,
                             OS_TEST_BLOCK_IO_SECOND_OWNER_THREAD_INDEX,
                             first_ticket) == os::kernel::BlockIoStatus::RequestAlreadyRegistered &&
        coordinator.PrepareWait(second_ticket, wait_required) ==
            os::kernel::BlockIoStatus::Succeeded &&
        wait_required &&
        coordinator.Complete(OS_TEST_BLOCK_IO_SECOND_OWNER_THREAD_INDEX,
                             OS_TEST_BLOCK_IO_SECOND_REQUEST_IDENTIFIER,
                             os::kernel::BlockRequestResult::TimedOut,
                             decision) == os::kernel::BlockIoStatus::Succeeded &&
        decision.wake_required && !decision.abandoned &&
        decision.owner_thread_index == OS_TEST_BLOCK_IO_SECOND_OWNER_THREAD_INDEX &&
        coordinator.Complete(OS_TEST_BLOCK_IO_SECOND_OWNER_THREAD_INDEX,
                             OS_TEST_BLOCK_IO_SECOND_REQUEST_IDENTIFIER,
                             os::kernel::BlockRequestResult::Succeeded,
                             decision) == os::kernel::BlockIoStatus::RequestAlreadyResolved &&
        coordinator.TakeResult(second_ticket, result) == os::kernel::BlockIoStatus::Succeeded &&
        result == os::kernel::BlockRequestResult::TimedOut;
    test_context.Expect(wait_wake_passed, OS_TEST_BLOCK_IO_WAIT_WAKE);

    os::kernel::BlockIoTicket third_ticket{};
    wait_required = false;
    const bool abandon_passed =
        coordinator.Register(OS_TEST_BLOCK_IO_THIRD_REQUEST_IDENTIFIER,
                             OS_TEST_BLOCK_IO_THIRD_OWNER_THREAD_INDEX,
                             third_ticket) == os::kernel::BlockIoStatus::Succeeded &&
        coordinator.PrepareWait(third_ticket, wait_required) ==
            os::kernel::BlockIoStatus::Succeeded &&
        wait_required &&
        coordinator.Abandon(third_ticket) == os::kernel::BlockIoStatus::Succeeded &&
        coordinator.Complete(OS_TEST_BLOCK_IO_THIRD_OWNER_THREAD_INDEX,
                             OS_TEST_BLOCK_IO_THIRD_REQUEST_IDENTIFIER,
                             os::kernel::BlockRequestResult::Cancelled,
                             decision) == os::kernel::BlockIoStatus::Succeeded &&
        !decision.wake_required && decision.abandoned &&
        coordinator.PrepareWait(third_ticket, wait_required) ==
            os::kernel::BlockIoStatus::InvalidTicket &&
        coordinator.Validate() == os::kernel::BlockIoStatus::Succeeded &&
        coordinator.Statistics().active_request_count == OS_TEST_BLOCK_IO_EMPTY_VALUE &&
        coordinator.Statistics().immediate_completion_count == 1ULL &&
        coordinator.Statistics().wake_required_count == 1ULL &&
        coordinator.Statistics().late_completion_count == 1ULL;
    test_context.Expect(abandon_passed, OS_TEST_BLOCK_IO_ABANDON);
    return test_context.ExitCode();
}

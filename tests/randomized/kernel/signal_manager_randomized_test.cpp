#include "os/kernel/process/signal_manager.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_SIGNAL_RANDOM_SUITE_NAME = "kernel/signal_manager/randomized";
constexpr std::string_view OS_TEST_SIGNAL_RANDOM_INVARIANT_MESSAGE =
    "十万步 send/mask/deliver 模型中同一普通信号不得重复归属或破坏 frame 状态";
constexpr uint64_t OS_TEST_SIGNAL_RANDOM_PROCESS_CAPACITY = 2ULL;
constexpr uint64_t OS_TEST_SIGNAL_RANDOM_THREAD_CAPACITY = 8ULL;
constexpr uint64_t OS_TEST_SIGNAL_RANDOM_OPERATION_COUNT = 100000ULL;
constexpr uint64_t OS_TEST_SIGNAL_RANDOM_SEED = 0x5349474E414C5632ULL;
constexpr uint64_t OS_TEST_SIGNAL_RANDOM_MULTIPLIER = 6364136223846793005ULL;
constexpr uint64_t OS_TEST_SIGNAL_RANDOM_INCREMENT = 1442695040888963407ULL;
constexpr uint64_t OS_TEST_SIGNAL_RANDOM_PROCESS_ID = 71ULL;
constexpr uint64_t OS_TEST_SIGNAL_RANDOM_HANDLER_ADDRESS = 0x40001000ULL;
constexpr uint64_t OS_TEST_SIGNAL_RANDOM_RESTORER_ADDRESS = 0x40002000ULL;
constexpr uint64_t OS_TEST_SIGNAL_RANDOM_FRAME_BASE = 0x70000000ULL;
constexpr uint64_t OS_TEST_SIGNAL_RANDOM_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_SIGNAL_RANDOM_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_TEST_SIGNAL_RANDOM_CHOICE_MASK = 1ULL;
constexpr uint64_t OS_TEST_SIGNAL_RANDOM_FRAME_STRIDE_BYTES = 0x1000ULL;
constexpr uint64_t OS_TEST_SIGNAL_RANDOM_MAXIMUM_PENDING_OWNER_COUNT = 1ULL;

enum class SignalRandomOperation : uint64_t {
    Send,
    SetMask,
    BeginDelivery,
    CompleteDelivery,
    Count,
};

[[nodiscard]] uint64_t NextRandom(uint64_t &state) noexcept {
    state = state * OS_TEST_SIGNAL_RANDOM_MULTIPLIER + OS_TEST_SIGNAL_RANDOM_INCREMENT;
    return state;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_SIGNAL_RANDOM_SUITE_NAME};
    os::kernel::SignalProcessState processes[OS_TEST_SIGNAL_RANDOM_PROCESS_CAPACITY]{};
    os::kernel::SignalThreadState threads[OS_TEST_SIGNAL_RANDOM_THREAD_CAPACITY]{};
    os::kernel::SignalManager manager{};
    bool consistent =
        manager.Initialize(processes, OS_TEST_SIGNAL_RANDOM_PROCESS_CAPACITY, threads,
                           OS_TEST_SIGNAL_RANDOM_THREAD_CAPACITY) ==
            os::kernel::SignalManagerStatus::Succeeded &&
        manager.RegisterProcess(OS_TEST_SIGNAL_RANDOM_EMPTY_VALUE, OS_TEST_SIGNAL_RANDOM_PROCESS_ID,
                                OS_TEST_SIGNAL_RANDOM_PROCESS_ID) ==
            os::kernel::SignalManagerStatus::Succeeded;
    for (uint64_t thread_index = OS_TEST_SIGNAL_RANDOM_EMPTY_VALUE;
         consistent && thread_index < OS_TEST_SIGNAL_RANDOM_THREAD_CAPACITY; ++thread_index) {
        consistent = manager.RegisterThread(thread_index, OS_TEST_SIGNAL_RANDOM_EMPTY_VALUE,
                                            thread_index + OS_TEST_SIGNAL_RANDOM_COUNTER_INCREMENT,
                                            OS_TEST_SIGNAL_RANDOM_EMPTY_VALUE) ==
                     os::kernel::SignalManagerStatus::Succeeded;
    }
    const os::abi::SignalAction action{
        .disposition = os::abi::SignalDisposition::Handler,
        .handler_address = OS_TEST_SIGNAL_RANDOM_HANDLER_ADDRESS,
        .restorer_address = OS_TEST_SIGNAL_RANDOM_RESTORER_ADDRESS,
        .additional_mask = OS_TEST_SIGNAL_RANDOM_EMPTY_VALUE,
        .flags = OS_TEST_SIGNAL_RANDOM_EMPTY_VALUE,
    };
    os::abi::SignalAction previous_action{};
    consistent =
        consistent &&
        manager.SetAction(OS_TEST_SIGNAL_RANDOM_EMPTY_VALUE, os::abi::OS_ABI_SIGNAL_USER1_NUMBER,
                          action, previous_action) == os::kernel::SignalManagerStatus::Succeeded &&
        manager.SetAction(OS_TEST_SIGNAL_RANDOM_EMPTY_VALUE, os::abi::OS_ABI_SIGNAL_USER2_NUMBER,
                          action, previous_action) == os::kernel::SignalManagerStatus::Succeeded;
    uint64_t random_state = OS_TEST_SIGNAL_RANDOM_SEED;
    for (uint64_t operation_index = OS_TEST_SIGNAL_RANDOM_EMPTY_VALUE;
         consistent && operation_index < OS_TEST_SIGNAL_RANDOM_OPERATION_COUNT; ++operation_index) {
        const SignalRandomOperation operation_kind = static_cast<SignalRandomOperation>(
            NextRandom(random_state) % static_cast<uint64_t>(SignalRandomOperation::Count));
        const uint64_t thread_index =
            NextRandom(random_state) % OS_TEST_SIGNAL_RANDOM_THREAD_CAPACITY;
        const uint64_t signal_number =
            (NextRandom(random_state) & OS_TEST_SIGNAL_RANDOM_CHOICE_MASK) ==
                    OS_TEST_SIGNAL_RANDOM_EMPTY_VALUE
                ? os::abi::OS_ABI_SIGNAL_USER1_NUMBER
                : os::abi::OS_ABI_SIGNAL_USER2_NUMBER;
        if (operation_kind == SignalRandomOperation::Send) {
            uint64_t selected_thread_index = os::kernel::OS_KERNEL_SIGNAL_INVALID_INDEX;
            consistent = manager.SendToProcess(OS_TEST_SIGNAL_RANDOM_PROCESS_ID, signal_number,
                                               selected_thread_index) ==
                         os::kernel::SignalManagerStatus::Succeeded;
        } else if (operation_kind == SignalRandomOperation::SetMask) {
            uint64_t previous_mask = OS_TEST_SIGNAL_RANDOM_EMPTY_VALUE;
            const uint64_t mask = (NextRandom(random_state) & OS_TEST_SIGNAL_RANDOM_CHOICE_MASK) ==
                                          OS_TEST_SIGNAL_RANDOM_EMPTY_VALUE
                                      ? OS_TEST_SIGNAL_RANDOM_EMPTY_VALUE
                                      : os::abi::SignalBit(signal_number);
            consistent = manager.SetThreadMask(thread_index, mask, previous_mask) ==
                         os::kernel::SignalManagerStatus::Succeeded;
        } else if (operation_kind == SignalRandomOperation::BeginDelivery) {
            os::kernel::SignalDelivery delivery{};
            consistent = manager.BeginThreadDelivery(thread_index, delivery) ==
                         os::kernel::SignalManagerStatus::Succeeded;
            if (consistent && delivery.kind == os::kernel::SignalDeliveryKind::UserHandler) {
                os::kernel::SignalThreadState thread{};
                const uint64_t frame_address =
                    OS_TEST_SIGNAL_RANDOM_FRAME_BASE +
                    thread_index * OS_TEST_SIGNAL_RANDOM_FRAME_STRIDE_BYTES;
                consistent = manager.ReadThread(thread_index, thread) ==
                                 os::kernel::SignalManagerStatus::Succeeded &&
                             manager.CommitHandlerFrame(thread_index, frame_address,
                                                        thread.active_frame_cookie) ==
                                 os::kernel::SignalManagerStatus::Succeeded;
            }
        } else {
            os::kernel::SignalThreadState thread{};
            consistent = manager.ReadThread(thread_index, thread) ==
                         os::kernel::SignalManagerStatus::Succeeded;
            if (consistent && thread.frame_active &&
                thread.active_frame_address != OS_TEST_SIGNAL_RANDOM_EMPTY_VALUE) {
                consistent =
                    manager.CompleteHandlerFrame(
                        thread_index, thread.active_frame_address, thread.active_frame_cookie,
                        thread.active_signal_number, thread.active_restorer_address,
                        thread.active_previous_mask) == os::kernel::SignalManagerStatus::Succeeded;
            }
        }
        if (consistent && operation_index % OS_TEST_SIGNAL_RANDOM_THREAD_CAPACITY ==
                              OS_TEST_SIGNAL_RANDOM_EMPTY_VALUE) {
            consistent = manager.Validate() == os::kernel::SignalManagerStatus::Succeeded;
            for (uint64_t checked_signal :
                 {os::abi::OS_ABI_SIGNAL_USER1_NUMBER, os::abi::OS_ABI_SIGNAL_USER2_NUMBER}) {
                uint64_t owner_count = OS_TEST_SIGNAL_RANDOM_EMPTY_VALUE;
                const uint64_t signal_bit = os::abi::SignalBit(checked_signal);
                os::kernel::SignalProcessState process{};
                consistent =
                    consistent && manager.ReadProcess(OS_TEST_SIGNAL_RANDOM_EMPTY_VALUE, process) ==
                                      os::kernel::SignalManagerStatus::Succeeded;
                owner_count +=
                    (process.pending_set & signal_bit) != OS_TEST_SIGNAL_RANDOM_EMPTY_VALUE
                        ? OS_TEST_SIGNAL_RANDOM_COUNTER_INCREMENT
                        : OS_TEST_SIGNAL_RANDOM_EMPTY_VALUE;
                for (uint64_t checked_thread = OS_TEST_SIGNAL_RANDOM_EMPTY_VALUE;
                     consistent && checked_thread < OS_TEST_SIGNAL_RANDOM_THREAD_CAPACITY;
                     ++checked_thread) {
                    os::kernel::SignalThreadState thread{};
                    consistent = manager.ReadThread(checked_thread, thread) ==
                                 os::kernel::SignalManagerStatus::Succeeded;
                    owner_count +=
                        (thread.pending_set & signal_bit) != OS_TEST_SIGNAL_RANDOM_EMPTY_VALUE
                            ? OS_TEST_SIGNAL_RANDOM_COUNTER_INCREMENT
                            : OS_TEST_SIGNAL_RANDOM_EMPTY_VALUE;
                }
                consistent =
                    consistent && owner_count <= OS_TEST_SIGNAL_RANDOM_MAXIMUM_PENDING_OWNER_COUNT;
            }
        }
    }
    test_context.ExpectRandom(consistent &&
                                  manager.Validate() == os::kernel::SignalManagerStatus::Succeeded,
                              OS_TEST_SIGNAL_RANDOM_INVARIANT_MESSAGE, OS_TEST_SIGNAL_RANDOM_SEED,
                              OS_TEST_SIGNAL_RANDOM_OPERATION_COUNT);
    return test_context.ExitCode();
}

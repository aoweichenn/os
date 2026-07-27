#include "os/kernel/process/signal_manager.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_SIGNAL_MANAGER_SUITE_NAME = "kernel/signal_manager/unit";
constexpr std::string_view OS_TEST_SIGNAL_MANAGER_SELECTION_MESSAGE =
    "Process 普通信号只能选择一个未屏蔽 Thread 且重复发送必须合并";
constexpr std::string_view OS_TEST_SIGNAL_MANAGER_FRAME_MESSAGE =
    "handler frame 必须校验身份，活动期间再次发送必须形成一个新 pending";
constexpr std::string_view OS_TEST_SIGNAL_MANAGER_LIFECYCLE_MESSAGE =
    "fork 必须复制 disposition/mask/group，exec 必须重置 handler 并清理兄弟";
constexpr uint64_t OS_TEST_SIGNAL_MANAGER_PROCESS_CAPACITY = 4ULL;
constexpr uint64_t OS_TEST_SIGNAL_MANAGER_THREAD_CAPACITY = 8ULL;
constexpr uint64_t OS_TEST_SIGNAL_MANAGER_PARENT_PROCESS_INDEX = 0ULL;
constexpr uint64_t OS_TEST_SIGNAL_MANAGER_CHILD_PROCESS_INDEX = 1ULL;
constexpr uint64_t OS_TEST_SIGNAL_MANAGER_PARENT_PROCESS_ID = 41ULL;
constexpr uint64_t OS_TEST_SIGNAL_MANAGER_CHILD_PROCESS_ID = 42ULL;
constexpr uint64_t OS_TEST_SIGNAL_MANAGER_PARENT_THREAD_INDEX = 0ULL;
constexpr uint64_t OS_TEST_SIGNAL_MANAGER_SECOND_THREAD_INDEX = 1ULL;
constexpr uint64_t OS_TEST_SIGNAL_MANAGER_CHILD_THREAD_INDEX = 2ULL;
constexpr uint64_t OS_TEST_SIGNAL_MANAGER_PARENT_THREAD_ID = 101ULL;
constexpr uint64_t OS_TEST_SIGNAL_MANAGER_SECOND_THREAD_ID = 102ULL;
constexpr uint64_t OS_TEST_SIGNAL_MANAGER_CHILD_THREAD_ID = 103ULL;
constexpr uint64_t OS_TEST_SIGNAL_MANAGER_HANDLER_ADDRESS = 0x40001000ULL;
constexpr uint64_t OS_TEST_SIGNAL_MANAGER_RESTORER_ADDRESS = 0x40002000ULL;
constexpr uint64_t OS_TEST_SIGNAL_MANAGER_FRAME_ADDRESS = 0x70001000ULL;
constexpr uint64_t OS_TEST_SIGNAL_MANAGER_FRAME_COOKIE = 1ULL;
constexpr uint64_t OS_TEST_SIGNAL_MANAGER_SECOND_FRAME_COOKIE = 2ULL;
constexpr uint64_t OS_TEST_SIGNAL_MANAGER_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_SIGNAL_MANAGER_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_TEST_SIGNAL_MANAGER_EXPECTED_COALESCED_COUNT = 1ULL;
constexpr uint64_t OS_TEST_SIGNAL_MANAGER_USER1_ACTION_INDEX =
    os::abi::OS_ABI_SIGNAL_USER1_NUMBER - os::abi::OS_ABI_SIGNAL_MINIMUM_NUMBER;

}

int main() {
    os::test::TestContext test_context{OS_TEST_SIGNAL_MANAGER_SUITE_NAME};
    os::kernel::SignalProcessState processes[OS_TEST_SIGNAL_MANAGER_PROCESS_CAPACITY]{};
    os::kernel::SignalThreadState threads[OS_TEST_SIGNAL_MANAGER_THREAD_CAPACITY]{};
    os::kernel::SignalManager manager{};
    const uint64_t user1_bit = os::abi::SignalBit(os::abi::OS_ABI_SIGNAL_USER1_NUMBER);
    bool valid = manager.Initialize(processes, OS_TEST_SIGNAL_MANAGER_PROCESS_CAPACITY, threads,
                                    OS_TEST_SIGNAL_MANAGER_THREAD_CAPACITY) ==
                     os::kernel::SignalManagerStatus::Succeeded &&
                 manager.RegisterProcess(OS_TEST_SIGNAL_MANAGER_PARENT_PROCESS_INDEX,
                                         OS_TEST_SIGNAL_MANAGER_PARENT_PROCESS_ID,
                                         OS_TEST_SIGNAL_MANAGER_PARENT_PROCESS_ID) ==
                     os::kernel::SignalManagerStatus::Succeeded &&
                 manager.RegisterThread(OS_TEST_SIGNAL_MANAGER_PARENT_THREAD_INDEX,
                                        OS_TEST_SIGNAL_MANAGER_PARENT_PROCESS_INDEX,
                                        OS_TEST_SIGNAL_MANAGER_PARENT_THREAD_ID,
                                        user1_bit) == os::kernel::SignalManagerStatus::Succeeded &&
                 manager.RegisterThread(OS_TEST_SIGNAL_MANAGER_SECOND_THREAD_INDEX,
                                        OS_TEST_SIGNAL_MANAGER_PARENT_PROCESS_INDEX,
                                        OS_TEST_SIGNAL_MANAGER_SECOND_THREAD_ID,
                                        OS_TEST_SIGNAL_MANAGER_EMPTY_VALUE) ==
                     os::kernel::SignalManagerStatus::Succeeded;
    const os::abi::SignalAction handler_action{
        .disposition = os::abi::SignalDisposition::Handler,
        .handler_address = OS_TEST_SIGNAL_MANAGER_HANDLER_ADDRESS,
        .restorer_address = OS_TEST_SIGNAL_MANAGER_RESTORER_ADDRESS,
        .additional_mask = OS_TEST_SIGNAL_MANAGER_EMPTY_VALUE,
        .flags = os::abi::OS_ABI_SIGNAL_ACTION_RESTART_WAIT_FLAG,
    };
    os::abi::SignalAction previous_action{};
    valid =
        valid && manager.SetAction(OS_TEST_SIGNAL_MANAGER_PARENT_PROCESS_INDEX,
                                   os::abi::OS_ABI_SIGNAL_USER1_NUMBER, handler_action,
                                   previous_action) == os::kernel::SignalManagerStatus::Succeeded;
    uint64_t selected_thread_index = os::kernel::OS_KERNEL_SIGNAL_INVALID_INDEX;
    valid = valid &&
            manager.SendToProcess(OS_TEST_SIGNAL_MANAGER_PARENT_PROCESS_ID,
                                  os::abi::OS_ABI_SIGNAL_USER1_NUMBER, selected_thread_index) ==
                os::kernel::SignalManagerStatus::Succeeded &&
            selected_thread_index == OS_TEST_SIGNAL_MANAGER_SECOND_THREAD_INDEX;
    uint64_t duplicate_selection = os::kernel::OS_KERNEL_SIGNAL_INVALID_INDEX;
    valid = valid &&
            manager.SendToProcess(OS_TEST_SIGNAL_MANAGER_PARENT_PROCESS_ID,
                                  os::abi::OS_ABI_SIGNAL_USER1_NUMBER, duplicate_selection) ==
                os::kernel::SignalManagerStatus::Succeeded &&
            duplicate_selection == os::kernel::OS_KERNEL_SIGNAL_INVALID_INDEX &&
            manager.Statistics().coalesced_signal_count ==
                OS_TEST_SIGNAL_MANAGER_EXPECTED_COALESCED_COUNT;
    test_context.Expect(valid, OS_TEST_SIGNAL_MANAGER_SELECTION_MESSAGE);

    os::kernel::SignalDelivery delivery{};
    valid = valid &&
            manager.BeginThreadDelivery(OS_TEST_SIGNAL_MANAGER_SECOND_THREAD_INDEX, delivery) ==
                os::kernel::SignalManagerStatus::Succeeded &&
            delivery.kind == os::kernel::SignalDeliveryKind::UserHandler &&
            delivery.signal_number == os::abi::OS_ABI_SIGNAL_USER1_NUMBER &&
            manager.CommitHandlerFrame(OS_TEST_SIGNAL_MANAGER_SECOND_THREAD_INDEX,
                                       OS_TEST_SIGNAL_MANAGER_FRAME_ADDRESS,
                                       OS_TEST_SIGNAL_MANAGER_FRAME_COOKIE) ==
                os::kernel::SignalManagerStatus::Succeeded &&
            manager.ValidateHandlerFrame(
                OS_TEST_SIGNAL_MANAGER_SECOND_THREAD_INDEX, OS_TEST_SIGNAL_MANAGER_FRAME_ADDRESS,
                OS_TEST_SIGNAL_MANAGER_FRAME_COOKIE, os::abi::OS_ABI_SIGNAL_USER1_NUMBER,
                OS_TEST_SIGNAL_MANAGER_RESTORER_ADDRESS,
                OS_TEST_SIGNAL_MANAGER_EMPTY_VALUE) == os::kernel::SignalManagerStatus::Succeeded &&
            manager.ValidateHandlerFrame(
                OS_TEST_SIGNAL_MANAGER_SECOND_THREAD_INDEX,
                OS_TEST_SIGNAL_MANAGER_FRAME_ADDRESS + OS_TEST_SIGNAL_MANAGER_COUNTER_INCREMENT,
                OS_TEST_SIGNAL_MANAGER_FRAME_COOKIE, os::abi::OS_ABI_SIGNAL_USER1_NUMBER,
                OS_TEST_SIGNAL_MANAGER_RESTORER_ADDRESS, OS_TEST_SIGNAL_MANAGER_EMPTY_VALUE) ==
                os::kernel::SignalManagerStatus::SignalFrameMismatch;
    uint64_t active_handler_selection = os::kernel::OS_KERNEL_SIGNAL_INVALID_INDEX;
    valid = valid &&
            manager.SendToProcess(OS_TEST_SIGNAL_MANAGER_PARENT_PROCESS_ID,
                                  os::abi::OS_ABI_SIGNAL_USER1_NUMBER, active_handler_selection) ==
                os::kernel::SignalManagerStatus::Succeeded &&
            active_handler_selection == os::kernel::OS_KERNEL_SIGNAL_INVALID_INDEX &&
            manager.CompleteHandlerFrame(
                OS_TEST_SIGNAL_MANAGER_SECOND_THREAD_INDEX, OS_TEST_SIGNAL_MANAGER_FRAME_ADDRESS,
                OS_TEST_SIGNAL_MANAGER_FRAME_COOKIE, os::abi::OS_ABI_SIGNAL_USER1_NUMBER,
                OS_TEST_SIGNAL_MANAGER_RESTORER_ADDRESS,
                OS_TEST_SIGNAL_MANAGER_EMPTY_VALUE) == os::kernel::SignalManagerStatus::Succeeded;
    os::kernel::SignalDelivery repeated_delivery{};
    valid = valid &&
            manager.BeginThreadDelivery(OS_TEST_SIGNAL_MANAGER_SECOND_THREAD_INDEX,
                                        repeated_delivery) ==
                os::kernel::SignalManagerStatus::Succeeded &&
            repeated_delivery.kind == os::kernel::SignalDeliveryKind::UserHandler &&
            repeated_delivery.signal_number == os::abi::OS_ABI_SIGNAL_USER1_NUMBER &&
            manager.CommitHandlerFrame(OS_TEST_SIGNAL_MANAGER_SECOND_THREAD_INDEX,
                                       OS_TEST_SIGNAL_MANAGER_FRAME_ADDRESS,
                                       OS_TEST_SIGNAL_MANAGER_SECOND_FRAME_COOKIE) ==
                os::kernel::SignalManagerStatus::Succeeded &&
            manager.CompleteHandlerFrame(
                OS_TEST_SIGNAL_MANAGER_SECOND_THREAD_INDEX, OS_TEST_SIGNAL_MANAGER_FRAME_ADDRESS,
                OS_TEST_SIGNAL_MANAGER_SECOND_FRAME_COOKIE, os::abi::OS_ABI_SIGNAL_USER1_NUMBER,
                OS_TEST_SIGNAL_MANAGER_RESTORER_ADDRESS,
                OS_TEST_SIGNAL_MANAGER_EMPTY_VALUE) == os::kernel::SignalManagerStatus::Succeeded;
    test_context.Expect(valid, OS_TEST_SIGNAL_MANAGER_FRAME_MESSAGE);

    valid = valid &&
            manager.ForkProcess(OS_TEST_SIGNAL_MANAGER_PARENT_PROCESS_INDEX,
                                OS_TEST_SIGNAL_MANAGER_CHILD_PROCESS_INDEX,
                                OS_TEST_SIGNAL_MANAGER_CHILD_PROCESS_ID) ==
                os::kernel::SignalManagerStatus::Succeeded &&
            manager.RegisterThread(OS_TEST_SIGNAL_MANAGER_CHILD_THREAD_INDEX,
                                   OS_TEST_SIGNAL_MANAGER_CHILD_PROCESS_INDEX,
                                   OS_TEST_SIGNAL_MANAGER_CHILD_THREAD_ID,
                                   user1_bit) == os::kernel::SignalManagerStatus::Succeeded;
    os::kernel::SignalProcessState child_process{};
    os::kernel::SignalThreadState child_thread{};
    valid = valid &&
            manager.ReadProcess(OS_TEST_SIGNAL_MANAGER_CHILD_PROCESS_INDEX, child_process) ==
                os::kernel::SignalManagerStatus::Succeeded &&
            child_process.process_group_id == OS_TEST_SIGNAL_MANAGER_PARENT_PROCESS_ID &&
            child_process.actions[OS_TEST_SIGNAL_MANAGER_USER1_ACTION_INDEX].disposition ==
                os::abi::SignalDisposition::Handler &&
            manager.ReadThread(OS_TEST_SIGNAL_MANAGER_CHILD_THREAD_INDEX, child_thread) ==
                os::kernel::SignalManagerStatus::Succeeded &&
            child_thread.signal_mask == user1_bit &&
            manager.ExecProcess(OS_TEST_SIGNAL_MANAGER_CHILD_PROCESS_INDEX,
                                OS_TEST_SIGNAL_MANAGER_CHILD_THREAD_INDEX) ==
                os::kernel::SignalManagerStatus::Succeeded &&
            manager.ReadProcess(OS_TEST_SIGNAL_MANAGER_CHILD_PROCESS_INDEX, child_process) ==
                os::kernel::SignalManagerStatus::Succeeded &&
            child_process.actions[OS_TEST_SIGNAL_MANAGER_USER1_ACTION_INDEX].disposition ==
                os::abi::SignalDisposition::Default &&
            manager.Validate() == os::kernel::SignalManagerStatus::Succeeded;
    test_context.Expect(valid, OS_TEST_SIGNAL_MANAGER_LIFECYCLE_MESSAGE);
    return test_context.ExitCode();
}

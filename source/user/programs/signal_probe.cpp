#include "os/user/system_call.hpp"

#include "os/abi/signal.hpp"
#include "os/abi/system_call.hpp"
#include "os/abi/time.hpp"

#include <stdint.h>

namespace {

constexpr char OS_USER_SIGNAL_PROBE_STARTED_MESSAGE[] = "[OS][USER][SIGNAL] STARTED\r\n";
constexpr char OS_USER_SIGNAL_PROBE_RESTART_MESSAGE[] =
    "[OS][USER][SIGNAL] RESTART_WAIT_VERIFIED\r\n";
constexpr char OS_USER_SIGNAL_PROBE_COALESCED_MESSAGE[] =
    "[OS][USER][SIGNAL] MASK_COALESCE_VERIFIED\r\n";
constexpr char OS_USER_SIGNAL_PROBE_GROUP_MESSAGE[] =
    "[OS][USER][SIGNAL] PROCESS_GROUP_VERIFIED\r\n";
constexpr char OS_USER_SIGNAL_PROBE_FORK_MESSAGE[] =
    "[OS][USER][SIGNAL] FORK_INHERITANCE_VERIFIED\r\n";
constexpr char OS_USER_SIGNAL_PROBE_BAD_FRAME_MESSAGE[] =
    "[OS][USER][SIGNAL] BAD_FRAME_ISOLATED\r\n";
constexpr char OS_USER_SIGNAL_PROBE_DEFAULT_MESSAGE[] =
    "[OS][USER][SIGNAL] DEFAULT_TERMINATION_VERIFIED\r\n";
constexpr char OS_USER_SIGNAL_PROBE_COMPLETED_MESSAGE[] = "[OS][USER][SIGNAL] COMPLETED\r\n";
constexpr uint64_t OS_USER_SIGNAL_PROBE_STRING_TERMINATOR_BYTES = 1ULL;
constexpr uint64_t OS_USER_SIGNAL_PROBE_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_USER_SIGNAL_PROBE_FIRST_HANDLER_COUNT = 1ULL;
constexpr uint64_t OS_USER_SIGNAL_PROBE_SECOND_HANDLER_COUNT = 2ULL;
constexpr uint64_t OS_USER_SIGNAL_PROBE_CHILD_HANDLER_COUNT = 3ULL;
constexpr uint64_t OS_USER_SIGNAL_PROBE_WORKER_DELAY_NS =
    10ULL * os::abi::OS_ABI_TIME_NANOSECONDS_PER_MILLISECOND;
constexpr uint8_t OS_USER_SIGNAL_PROBE_TRANSFER_BYTE = 0x53U;
constexpr int64_t OS_USER_SIGNAL_PROBE_SUCCESS_RESULT = 0LL;
constexpr int64_t OS_USER_SIGNAL_PROBE_CHILD_FORK_RESULT = 0LL;
constexpr int64_t OS_USER_SIGNAL_PROBE_SUCCESS_EXIT_CODE = 0LL;
constexpr int64_t OS_USER_SIGNAL_PROBE_FAILURE_EXIT_CODE = 1LL;
constexpr int64_t OS_USER_SIGNAL_PROBE_FIRST_ERROR_RESULT = -1LL;
constexpr uint64_t OS_USER_SIGNAL_PROBE_INVALID_RETURN_VECTOR = 13ULL;

uint64_t handler_count;

template <uint64_t MessageSizeBytes>
[[nodiscard]] bool WriteMessage(const char (&message)[MessageSizeBytes]) noexcept {
    return os::user::WriteLog(message,
                              MessageSizeBytes - OS_USER_SIGNAL_PROBE_STRING_TERMINATOR_BYTES) >
           OS_USER_SIGNAL_PROBE_FIRST_ERROR_RESULT;
}

void CountSignal(const uint64_t signal_number, os::abi::SignalFrame *const signal_frame) noexcept {
    if (signal_number == os::abi::OS_ABI_SIGNAL_USER1_NUMBER && signal_frame != nullptr &&
        signal_frame->signal_number == signal_number) {
        handler_count += OS_USER_SIGNAL_PROBE_FIRST_HANDLER_COUNT;
    }
}

void CorruptSignalFrame(const uint64_t signal_number,
                        os::abi::SignalFrame *const signal_frame) noexcept {
    if (signal_number == os::abi::OS_ABI_SIGNAL_USER2_NUMBER && signal_frame != nullptr) {
        signal_frame->magic = OS_USER_SIGNAL_PROBE_EMPTY_VALUE;
    }
}

[[noreturn]] void SendAndWrite(const uint64_t parent_process_id,
                               const uint64_t writer_descriptor) noexcept {
    if (os::user::SleepFor(OS_USER_SIGNAL_PROBE_WORKER_DELAY_NS) !=
            OS_USER_SIGNAL_PROBE_SUCCESS_RESULT ||
        os::user::SendProcessSignal(parent_process_id, os::abi::OS_ABI_SIGNAL_USER1_NUMBER) !=
            OS_USER_SIGNAL_PROBE_SUCCESS_RESULT ||
        os::user::SleepFor(OS_USER_SIGNAL_PROBE_WORKER_DELAY_NS) !=
            OS_USER_SIGNAL_PROBE_SUCCESS_RESULT ||
        os::user::WriteDescriptor(writer_descriptor, &OS_USER_SIGNAL_PROBE_TRANSFER_BYTE,
                                  sizeof(OS_USER_SIGNAL_PROBE_TRANSFER_BYTE)) !=
            static_cast<int64_t>(sizeof(OS_USER_SIGNAL_PROBE_TRANSFER_BYTE))) {
        os::user::ExitProcess(OS_USER_SIGNAL_PROBE_FAILURE_EXIT_CODE);
    }
    os::user::ExitProcess(OS_USER_SIGNAL_PROBE_SUCCESS_EXIT_CODE);
}

[[nodiscard]] bool VerifyRestartedWait() noexcept {
    const uint64_t blocked_signal_set = os::abi::SignalBit(os::abi::OS_ABI_SIGNAL_USER1_NUMBER);
    uint64_t previous_mask = OS_USER_SIGNAL_PROBE_EMPTY_VALUE;
    if (os::user::SetSignalMask(blocked_signal_set, &previous_mask) !=
        OS_USER_SIGNAL_PROBE_SUCCESS_RESULT) {
        return false;
    }
    os::abi::PipeDescriptorPair descriptors{};
    if (os::user::CreatePipe(descriptors) != OS_USER_SIGNAL_PROBE_SUCCESS_RESULT) {
        return false;
    }
    const uint64_t parent_process_id = os::user::GetProcessId();
    const int64_t child_process_id = os::user::ForkProcess();
    if (child_process_id == OS_USER_SIGNAL_PROBE_CHILD_FORK_RESULT) {
        SendAndWrite(parent_process_id, descriptors.writer_descriptor);
    }
    if (child_process_id <= OS_USER_SIGNAL_PROBE_FIRST_ERROR_RESULT ||
        os::user::SetSignalMask(previous_mask, nullptr) != OS_USER_SIGNAL_PROBE_SUCCESS_RESULT) {
        return false;
    }
    uint8_t observed_byte = OS_USER_SIGNAL_PROBE_EMPTY_VALUE;
    const int64_t read_result = os::user::ReadDescriptor(descriptors.reader_descriptor,
                                                         &observed_byte, sizeof(observed_byte));
    os::abi::ProcessWaitResult wait_result{};
    return read_result == static_cast<int64_t>(sizeof(observed_byte)) &&
           observed_byte == OS_USER_SIGNAL_PROBE_TRANSFER_BYTE &&
           handler_count == OS_USER_SIGNAL_PROBE_FIRST_HANDLER_COUNT &&
           os::user::WaitProcess(static_cast<uint64_t>(child_process_id), wait_result) ==
               child_process_id &&
           wait_result.termination_reason == os::abi::ProcessTerminationReason::Exited &&
           wait_result.exit_code == OS_USER_SIGNAL_PROBE_SUCCESS_EXIT_CODE &&
           os::user::CloseDescriptor(descriptors.reader_descriptor) ==
               OS_USER_SIGNAL_PROBE_SUCCESS_RESULT &&
           os::user::CloseDescriptor(descriptors.writer_descriptor) ==
               OS_USER_SIGNAL_PROBE_SUCCESS_RESULT;
}

[[nodiscard]] bool VerifyMaskAndCoalescing() noexcept {
    const uint64_t signal_set = os::abi::SignalBit(os::abi::OS_ABI_SIGNAL_USER1_NUMBER);
    uint64_t previous_mask = OS_USER_SIGNAL_PROBE_EMPTY_VALUE;
    const uint64_t process_id = os::user::GetProcessId();
    if (os::user::SetSignalMask(signal_set, &previous_mask) !=
            OS_USER_SIGNAL_PROBE_SUCCESS_RESULT ||
        os::user::SendProcessSignal(process_id, os::abi::OS_ABI_SIGNAL_USER1_NUMBER) !=
            OS_USER_SIGNAL_PROBE_SUCCESS_RESULT ||
        os::user::SendProcessSignal(process_id, os::abi::OS_ABI_SIGNAL_USER1_NUMBER) !=
            OS_USER_SIGNAL_PROBE_SUCCESS_RESULT ||
        handler_count != OS_USER_SIGNAL_PROBE_FIRST_HANDLER_COUNT ||
        os::user::SetSignalMask(previous_mask, nullptr) != OS_USER_SIGNAL_PROBE_SUCCESS_RESULT) {
        return false;
    }
    return handler_count == OS_USER_SIGNAL_PROBE_SECOND_HANDLER_COUNT;
}

[[nodiscard]] bool VerifyProcessGroup() noexcept {
    const os::abi::SignalAction ignore_action{
        .disposition = os::abi::SignalDisposition::Ignore,
        .handler_address = OS_USER_SIGNAL_PROBE_EMPTY_VALUE,
        .restorer_address = OS_USER_SIGNAL_PROBE_EMPTY_VALUE,
        .additional_mask = OS_USER_SIGNAL_PROBE_EMPTY_VALUE,
        .flags = OS_USER_SIGNAL_PROBE_EMPTY_VALUE,
    };
    if (os::user::SetProcessGroup(OS_USER_SIGNAL_PROBE_EMPTY_VALUE) !=
        OS_USER_SIGNAL_PROBE_SUCCESS_RESULT) {
        return false;
    }
    const int64_t process_group_id = os::user::GetProcessGroup();
    return process_group_id == static_cast<int64_t>(os::user::GetProcessId()) &&
           os::user::SetSignalAction(os::abi::OS_ABI_SIGNAL_USER2_NUMBER, ignore_action, nullptr) ==
               OS_USER_SIGNAL_PROBE_SUCCESS_RESULT &&
           os::user::SendProcessGroupSignal(static_cast<uint64_t>(process_group_id),
                                            os::abi::OS_ABI_SIGNAL_USER2_NUMBER) >
               OS_USER_SIGNAL_PROBE_FIRST_ERROR_RESULT;
}

[[nodiscard]] bool WaitForExpectedChild(const uint64_t process_id,
                                        const os::abi::ProcessTerminationReason reason,
                                        const uint64_t detail) noexcept {
    os::abi::ProcessWaitResult wait_result{};
    return os::user::WaitProcess(process_id, wait_result) == static_cast<int64_t>(process_id) &&
           wait_result.termination_reason == reason && wait_result.exception_vector == detail;
}

[[nodiscard]] bool VerifyForkInheritance() noexcept {
    const int64_t child_process_id = os::user::ForkProcess();
    if (child_process_id == OS_USER_SIGNAL_PROBE_CHILD_FORK_RESULT) {
        if (os::user::SendProcessSignal(os::user::GetProcessId(),
                                        os::abi::OS_ABI_SIGNAL_USER1_NUMBER) !=
                OS_USER_SIGNAL_PROBE_SUCCESS_RESULT ||
            handler_count != OS_USER_SIGNAL_PROBE_CHILD_HANDLER_COUNT) {
            os::user::ExitProcess(OS_USER_SIGNAL_PROBE_FAILURE_EXIT_CODE);
        }
        os::user::ExitProcess(OS_USER_SIGNAL_PROBE_SUCCESS_EXIT_CODE);
    }
    return child_process_id > OS_USER_SIGNAL_PROBE_FIRST_ERROR_RESULT &&
           WaitForExpectedChild(static_cast<uint64_t>(child_process_id),
                                os::abi::ProcessTerminationReason::Exited,
                                OS_USER_SIGNAL_PROBE_EMPTY_VALUE) &&
           handler_count == OS_USER_SIGNAL_PROBE_SECOND_HANDLER_COUNT;
}

[[nodiscard]] bool VerifyMalformedFrameIsolation() noexcept {
    const int64_t child_process_id = os::user::ForkProcess();
    if (child_process_id == OS_USER_SIGNAL_PROBE_CHILD_FORK_RESULT) {
        if (os::user::InstallSignalHandler(os::abi::OS_ABI_SIGNAL_USER2_NUMBER, CorruptSignalFrame,
                                           OS_USER_SIGNAL_PROBE_EMPTY_VALUE,
                                           OS_USER_SIGNAL_PROBE_EMPTY_VALUE,
                                           nullptr) != OS_USER_SIGNAL_PROBE_SUCCESS_RESULT) {
            os::user::ExitProcess(OS_USER_SIGNAL_PROBE_FAILURE_EXIT_CODE);
        }
        static_cast<void>(os::user::SendProcessSignal(os::user::GetProcessId(),
                                                      os::abi::OS_ABI_SIGNAL_USER2_NUMBER));
        os::user::ExitProcess(OS_USER_SIGNAL_PROBE_FAILURE_EXIT_CODE);
    }
    return child_process_id > OS_USER_SIGNAL_PROBE_FIRST_ERROR_RESULT &&
           WaitForExpectedChild(static_cast<uint64_t>(child_process_id),
                                os::abi::ProcessTerminationReason::Exception,
                                OS_USER_SIGNAL_PROBE_INVALID_RETURN_VECTOR);
}

[[nodiscard]] bool VerifyDefaultTermination() noexcept {
    const int64_t child_process_id = os::user::ForkProcess();
    if (child_process_id == OS_USER_SIGNAL_PROBE_CHILD_FORK_RESULT) {
        static_cast<void>(os::user::SendProcessSignal(os::user::GetProcessId(),
                                                      os::abi::OS_ABI_SIGNAL_TERMINATE_NUMBER));
        os::user::ExitProcess(OS_USER_SIGNAL_PROBE_FAILURE_EXIT_CODE);
    }
    return child_process_id > OS_USER_SIGNAL_PROBE_FIRST_ERROR_RESULT &&
           WaitForExpectedChild(static_cast<uint64_t>(child_process_id),
                                os::abi::ProcessTerminationReason::Signal,
                                os::abi::OS_ABI_SIGNAL_TERMINATE_NUMBER);
}

}

extern "C" [[noreturn, gnu::section(".text.os_user_entry")]]
void OsUserEntry(uint64_t, const char *const *const, const char *const *const) noexcept {
    if (!WriteMessage(OS_USER_SIGNAL_PROBE_STARTED_MESSAGE) ||
        os::user::InstallSignalHandler(os::abi::OS_ABI_SIGNAL_USER1_NUMBER, CountSignal,
                                       OS_USER_SIGNAL_PROBE_EMPTY_VALUE,
                                       os::abi::OS_ABI_SIGNAL_ACTION_RESTART_WAIT_FLAG,
                                       nullptr) != OS_USER_SIGNAL_PROBE_SUCCESS_RESULT ||
        !VerifyRestartedWait() || !WriteMessage(OS_USER_SIGNAL_PROBE_RESTART_MESSAGE) ||
        !VerifyMaskAndCoalescing() || !WriteMessage(OS_USER_SIGNAL_PROBE_COALESCED_MESSAGE) ||
        !VerifyProcessGroup() || !WriteMessage(OS_USER_SIGNAL_PROBE_GROUP_MESSAGE) ||
        !VerifyForkInheritance() || !WriteMessage(OS_USER_SIGNAL_PROBE_FORK_MESSAGE) ||
        !VerifyMalformedFrameIsolation() || !WriteMessage(OS_USER_SIGNAL_PROBE_BAD_FRAME_MESSAGE) ||
        !VerifyDefaultTermination() || !WriteMessage(OS_USER_SIGNAL_PROBE_DEFAULT_MESSAGE) ||
        !WriteMessage(OS_USER_SIGNAL_PROBE_COMPLETED_MESSAGE)) {
        os::user::ExitProcess(OS_USER_SIGNAL_PROBE_FAILURE_EXIT_CODE);
    }
    os::user::ExitProcess(OS_USER_SIGNAL_PROBE_SUCCESS_EXIT_CODE);
}

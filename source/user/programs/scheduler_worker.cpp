#include "os/user/extended_state.hpp"
#include "os/user/system_call.hpp"

#include "os/abi/system_call.hpp"

#include <stdint.h>

namespace {

constexpr char OS_USER_SCHEDULER_PID2_STEP1_MESSAGE[] = "[OS][USER][PID2] WORKER_STEP_1\r\n";
constexpr char OS_USER_SCHEDULER_PID2_STEP2_MESSAGE[] = "[OS][USER][PID2] WORKER_STEP_2\r\n";
constexpr char OS_USER_SCHEDULER_PID2_STEP3_MESSAGE[] = "[OS][USER][PID2] WORKER_STEP_3\r\n";
constexpr char OS_USER_SCHEDULER_PID3_STEP1_MESSAGE[] = "[OS][USER][PID3] WORKER_STEP_1\r\n";
constexpr char OS_USER_SCHEDULER_PID3_STEP2_MESSAGE[] = "[OS][USER][PID3] WORKER_STEP_2\r\n";
constexpr char OS_USER_SCHEDULER_PID3_STEP3_MESSAGE[] = "[OS][USER][PID3] WORKER_STEP_3\r\n";
constexpr char OS_USER_SCHEDULER_PID4_STEP1_MESSAGE[] = "[OS][USER][PID4] WORKER_STEP_1\r\n";
constexpr char OS_USER_SCHEDULER_PID4_STEP2_MESSAGE[] = "[OS][USER][PID4] WORKER_STEP_2\r\n";
constexpr char OS_USER_SCHEDULER_PID4_STEP3_MESSAGE[] = "[OS][USER][PID4] WORKER_STEP_3\r\n";
constexpr char OS_USER_SCHEDULER_ADDRESS_SPACE_ISOLATED_MESSAGE[] =
    "[OS][USER] ADDRESS_SPACE_ISOLATED\r\n";
constexpr char OS_USER_SCHEDULER_DUAL_ENTRY_EQUIVALENT_MESSAGE[] =
    "[OS][USER] DUAL_SYSCALL_ENTRY_EQUIVALENT\r\n";
constexpr char OS_USER_SCHEDULER_SYSTEM_RETURN_MESSAGE[] = "[OS][USER] SYSRET_RETURNED\r\n";
constexpr char OS_USER_SCHEDULER_INTERRUPT_FALLBACK_MESSAGE[] =
    "[OS][USER] SYSCALL_IRET_FALLBACK_RETURNED\r\n";
constexpr char OS_USER_SCHEDULER_FILE_DESCRIPTION_MODEL_MESSAGE[] =
    "[OS][USER][PID4] FILE_DESCRIPTION_MODEL_OK\r\n";
constexpr char OS_USER_SCHEDULER_FILE_DESCRIPTION_PATH[] = "/fdv14.bin";
constexpr uint8_t OS_USER_SCHEDULER_FILE_DESCRIPTION_PAYLOAD[] = {
    static_cast<uint8_t>('A'), static_cast<uint8_t>('B'), static_cast<uint8_t>('C'),
    static_cast<uint8_t>('D'), static_cast<uint8_t>('E'), static_cast<uint8_t>('F'),
    static_cast<uint8_t>('G'), static_cast<uint8_t>('H'),
};
constexpr uint64_t OS_USER_SCHEDULER_FIRST_WORKER_PROCESS_ID = 2ULL;
constexpr uint64_t OS_USER_SCHEDULER_SECOND_WORKER_PROCESS_ID = 3ULL;
constexpr uint64_t OS_USER_SCHEDULER_THIRD_WORKER_PROCESS_ID = 4ULL;
constexpr uint64_t OS_USER_SCHEDULER_FIRST_ROUND = 1ULL;
constexpr uint64_t OS_USER_SCHEDULER_SECOND_ROUND = 2ULL;
constexpr uint64_t OS_USER_SCHEDULER_ROUND_COUNT = 3ULL;
constexpr uint64_t OS_USER_SCHEDULER_ITERATIONS_PER_ROUND = 1500000ULL;
constexpr uint64_t OS_USER_SCHEDULER_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_USER_SCHEDULER_UNUSED_SYSTEM_CALL_ARGUMENT = 0ULL;
constexpr uint64_t OS_USER_SCHEDULER_UNKNOWN_SYSTEM_CALL_NUMBER = UINT64_MAX;
constexpr uint64_t OS_USER_SCHEDULER_STRING_TERMINATOR_SIZE_BYTES = 1ULL;
constexpr uint64_t OS_USER_SCHEDULER_FILE_DESCRIPTION_COMPATIBLE_DUPLICATE_MINIMUM = 8ULL;
constexpr uint64_t OS_USER_SCHEDULER_FILE_DESCRIPTION_STRESS_DUPLICATE_MINIMUM = 64ULL;
constexpr uint64_t OS_USER_SCHEDULER_FILE_DESCRIPTION_MINIMUM_HARD_LIMIT = 64ULL;
constexpr uint64_t OS_USER_SCHEDULER_FILE_DESCRIPTION_TRANSFER_SIZE_BYTES = 3ULL;
constexpr uint64_t OS_USER_SCHEDULER_FILE_DESCRIPTION_SECOND_OFFSET_BYTES = 3ULL;
constexpr uint64_t OS_USER_SCHEDULER_FILE_DESCRIPTION_FIRST_DESCRIPTOR =
    os::abi::OS_ABI_FIRST_DYNAMIC_DESCRIPTOR;
constexpr uint64_t OS_USER_SCHEDULER_FILE_DESCRIPTION_SECOND_DESCRIPTOR =
    OS_USER_SCHEDULER_FILE_DESCRIPTION_FIRST_DESCRIPTOR + 1ULL;
constexpr int64_t OS_USER_SCHEDULER_SUCCESS_EXIT_CODE = 0LL;
constexpr int64_t OS_USER_SCHEDULER_FAILURE_EXIT_CODE = 1LL;
constexpr int64_t OS_USER_SCHEDULER_FIRST_ERROR_RESULT = -1LL;

volatile uint64_t worker_counter;

template <uint64_t MessageSizeBytes>
[[nodiscard]] bool WriteMessage(const char (&message)[MessageSizeBytes]) noexcept {
    return os::user::WriteLog(message,
                              MessageSizeBytes - OS_USER_SCHEDULER_STRING_TERMINATOR_SIZE_BYTES) >
           OS_USER_SCHEDULER_FIRST_ERROR_RESULT;
}

[[nodiscard]] bool WriteProgressMessage(const uint64_t process_id, const uint64_t round) noexcept {
    if (process_id == OS_USER_SCHEDULER_FIRST_WORKER_PROCESS_ID) {
        if (round == OS_USER_SCHEDULER_FIRST_ROUND) {
            return WriteMessage(OS_USER_SCHEDULER_PID2_STEP1_MESSAGE);
        }
        if (round == OS_USER_SCHEDULER_SECOND_ROUND) {
            return WriteMessage(OS_USER_SCHEDULER_PID2_STEP2_MESSAGE);
        }
        return WriteMessage(OS_USER_SCHEDULER_PID2_STEP3_MESSAGE);
    }
    if (process_id == OS_USER_SCHEDULER_SECOND_WORKER_PROCESS_ID) {
        if (round == OS_USER_SCHEDULER_FIRST_ROUND) {
            return WriteMessage(OS_USER_SCHEDULER_PID3_STEP1_MESSAGE);
        }
        if (round == OS_USER_SCHEDULER_SECOND_ROUND) {
            return WriteMessage(OS_USER_SCHEDULER_PID3_STEP2_MESSAGE);
        }
        return WriteMessage(OS_USER_SCHEDULER_PID3_STEP3_MESSAGE);
    }
    if (process_id == OS_USER_SCHEDULER_THIRD_WORKER_PROCESS_ID) {
        if (round == OS_USER_SCHEDULER_FIRST_ROUND) {
            return WriteMessage(OS_USER_SCHEDULER_PID4_STEP1_MESSAGE);
        }
        if (round == OS_USER_SCHEDULER_SECOND_ROUND) {
            return WriteMessage(OS_USER_SCHEDULER_PID4_STEP2_MESSAGE);
        }
        return WriteMessage(OS_USER_SCHEDULER_PID4_STEP3_MESSAGE);
    }
    return false;
}

[[nodiscard]] bool RunWorkRound(const uint64_t round) noexcept {
    for (uint64_t iteration = 0ULL; iteration < OS_USER_SCHEDULER_ITERATIONS_PER_ROUND;
         ++iteration) {
        worker_counter = worker_counter + OS_USER_SCHEDULER_COUNTER_INCREMENT;
    }
    return worker_counter == round * OS_USER_SCHEDULER_ITERATIONS_PER_ROUND;
}

[[nodiscard]] bool ValidateSystemCallEntries(const uint64_t process_id) noexcept {
    const uint64_t get_process_id_number =
        static_cast<uint64_t>(os::abi::SystemCallNumber::GetProcessId);
    const int64_t legacy_process_id = os::user::InvokeLegacySystemCall(
        get_process_id_number, OS_USER_SCHEDULER_UNUSED_SYSTEM_CALL_ARGUMENT,
        OS_USER_SCHEDULER_UNUSED_SYSTEM_CALL_ARGUMENT,
        OS_USER_SCHEDULER_UNUSED_SYSTEM_CALL_ARGUMENT);
    const int64_t native_unknown_result = os::user::InvokeSystemCall(
        OS_USER_SCHEDULER_UNKNOWN_SYSTEM_CALL_NUMBER, OS_USER_SCHEDULER_UNUSED_SYSTEM_CALL_ARGUMENT,
        OS_USER_SCHEDULER_UNUSED_SYSTEM_CALL_ARGUMENT,
        OS_USER_SCHEDULER_UNUSED_SYSTEM_CALL_ARGUMENT);
    const int64_t legacy_unknown_result = os::user::InvokeLegacySystemCall(
        OS_USER_SCHEDULER_UNKNOWN_SYSTEM_CALL_NUMBER, OS_USER_SCHEDULER_UNUSED_SYSTEM_CALL_ARGUMENT,
        OS_USER_SCHEDULER_UNUSED_SYSTEM_CALL_ARGUMENT,
        OS_USER_SCHEDULER_UNUSED_SYSTEM_CALL_ARGUMENT);
    const int64_t fallback_process_id = os::user::InvokeSystemCallWithDirectionFlag(
        get_process_id_number, OS_USER_SCHEDULER_UNUSED_SYSTEM_CALL_ARGUMENT,
        OS_USER_SCHEDULER_UNUSED_SYSTEM_CALL_ARGUMENT,
        OS_USER_SCHEDULER_UNUSED_SYSTEM_CALL_ARGUMENT);
    return legacy_process_id == static_cast<int64_t>(process_id) &&
           fallback_process_id == static_cast<int64_t>(process_id) &&
           native_unknown_result == os::abi::OS_ABI_SYSTEM_CALL_RESULT_UNKNOWN_NUMBER &&
           legacy_unknown_result == native_unknown_result &&
           WriteMessage(OS_USER_SCHEDULER_DUAL_ENTRY_EQUIVALENT_MESSAGE) &&
           WriteMessage(OS_USER_SCHEDULER_SYSTEM_RETURN_MESSAGE) &&
           WriteMessage(OS_USER_SCHEDULER_INTERRUPT_FALLBACK_MESSAGE);
}

[[nodiscard]] bool BytesEqual(const uint8_t *const left, const uint8_t *const right,
                              const uint64_t length_bytes) noexcept {
    if (left == nullptr || right == nullptr) {
        return false;
    }
    for (uint64_t byte_index = OS_USER_SCHEDULER_UNUSED_SYSTEM_CALL_ARGUMENT;
         byte_index < length_bytes; ++byte_index) {
        if (left[byte_index] != right[byte_index]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool ValidateFileDescriptionModel(const uint64_t process_id) noexcept {
    if (process_id != OS_USER_SCHEDULER_THIRD_WORKER_PROCESS_ID) {
        return true;
    }
    const int64_t soft_limit = os::user::GetDescriptorSoftLimit();
    const int64_t hard_limit = os::user::GetDescriptorHardLimit();
    if (soft_limit != hard_limit ||
        hard_limit < static_cast<int64_t>(OS_USER_SCHEDULER_FILE_DESCRIPTION_MINIMUM_HARD_LIMIT)) {
        return false;
    }
    const uint64_t duplicate_minimum =
        hard_limit >
                static_cast<int64_t>(OS_USER_SCHEDULER_FILE_DESCRIPTION_STRESS_DUPLICATE_MINIMUM)
            ? OS_USER_SCHEDULER_FILE_DESCRIPTION_STRESS_DUPLICATE_MINIMUM
            : OS_USER_SCHEDULER_FILE_DESCRIPTION_COMPATIBLE_DUPLICATE_MINIMUM;

    const uint64_t create_flags = os::abi::OS_ABI_FILE_OPEN_WRITE_FLAG |
                                  os::abi::OS_ABI_FILE_OPEN_CREATE_FLAG |
                                  os::abi::OS_ABI_FILE_OPEN_TRUNCATE_FLAG;
    const int64_t write_descriptor =
        os::user::OpenFile(OS_USER_SCHEDULER_FILE_DESCRIPTION_PATH,
                           sizeof(OS_USER_SCHEDULER_FILE_DESCRIPTION_PATH) -
                               OS_USER_SCHEDULER_STRING_TERMINATOR_SIZE_BYTES,
                           create_flags);
    if (write_descriptor !=
            static_cast<int64_t>(OS_USER_SCHEDULER_FILE_DESCRIPTION_FIRST_DESCRIPTOR) ||
        os::user::WriteDescriptor(static_cast<uint64_t>(write_descriptor),
                                  OS_USER_SCHEDULER_FILE_DESCRIPTION_PAYLOAD,
                                  sizeof(OS_USER_SCHEDULER_FILE_DESCRIPTION_PAYLOAD)) !=
            static_cast<int64_t>(sizeof(OS_USER_SCHEDULER_FILE_DESCRIPTION_PAYLOAD)) ||
        os::user::CloseDescriptor(static_cast<uint64_t>(write_descriptor)) !=
            OS_USER_SCHEDULER_SUCCESS_EXIT_CODE) {
        return false;
    }

    const int64_t shared_descriptor =
        os::user::OpenFile(OS_USER_SCHEDULER_FILE_DESCRIPTION_PATH,
                           sizeof(OS_USER_SCHEDULER_FILE_DESCRIPTION_PATH) -
                               OS_USER_SCHEDULER_STRING_TERMINATOR_SIZE_BYTES,
                           os::abi::OS_ABI_FILE_OPEN_READ_FLAG);
    const int64_t duplicate_descriptor =
        shared_descriptor < OS_USER_SCHEDULER_SUCCESS_EXIT_CODE
            ? shared_descriptor
            : os::user::DuplicateDescriptor(static_cast<uint64_t>(shared_descriptor),
                                            duplicate_minimum,
                                            os::abi::OS_ABI_FILE_DESCRIPTOR_CLOSE_ON_EXEC_FLAG);
    const int64_t independent_descriptor =
        os::user::OpenFile(OS_USER_SCHEDULER_FILE_DESCRIPTION_PATH,
                           sizeof(OS_USER_SCHEDULER_FILE_DESCRIPTION_PATH) -
                               OS_USER_SCHEDULER_STRING_TERMINATOR_SIZE_BYTES,
                           os::abi::OS_ABI_FILE_OPEN_READ_FLAG);
    if (shared_descriptor !=
            static_cast<int64_t>(OS_USER_SCHEDULER_FILE_DESCRIPTION_FIRST_DESCRIPTOR) ||
        duplicate_descriptor < static_cast<int64_t>(duplicate_minimum) ||
        independent_descriptor !=
            static_cast<int64_t>(OS_USER_SCHEDULER_FILE_DESCRIPTION_SECOND_DESCRIPTOR)) {
        return false;
    }

    uint8_t first_bytes[OS_USER_SCHEDULER_FILE_DESCRIPTION_TRANSFER_SIZE_BYTES]{};
    uint8_t second_bytes[OS_USER_SCHEDULER_FILE_DESCRIPTION_TRANSFER_SIZE_BYTES]{};
    uint8_t independent_bytes[OS_USER_SCHEDULER_FILE_DESCRIPTION_TRANSFER_SIZE_BYTES]{};
    if (os::user::ReadDescriptor(static_cast<uint64_t>(shared_descriptor), first_bytes,
                                 sizeof(first_bytes)) !=
            static_cast<int64_t>(sizeof(first_bytes)) ||
        os::user::ReadDescriptor(static_cast<uint64_t>(duplicate_descriptor), second_bytes,
                                 sizeof(second_bytes)) !=
            static_cast<int64_t>(sizeof(second_bytes)) ||
        os::user::ReadDescriptor(static_cast<uint64_t>(independent_descriptor), independent_bytes,
                                 sizeof(independent_bytes)) !=
            static_cast<int64_t>(sizeof(independent_bytes)) ||
        !BytesEqual(first_bytes, OS_USER_SCHEDULER_FILE_DESCRIPTION_PAYLOAD, sizeof(first_bytes)) ||
        !BytesEqual(second_bytes,
                    OS_USER_SCHEDULER_FILE_DESCRIPTION_PAYLOAD +
                        OS_USER_SCHEDULER_FILE_DESCRIPTION_SECOND_OFFSET_BYTES,
                    sizeof(second_bytes)) ||
        !BytesEqual(independent_bytes, OS_USER_SCHEDULER_FILE_DESCRIPTION_PAYLOAD,
                    sizeof(independent_bytes))) {
        return false;
    }

    const int64_t shared_flags =
        os::user::GetDescriptorFlags(static_cast<uint64_t>(shared_descriptor));
    const int64_t duplicate_flags =
        os::user::GetDescriptorFlags(static_cast<uint64_t>(duplicate_descriptor));
    if (shared_flags != OS_USER_SCHEDULER_UNUSED_SYSTEM_CALL_ARGUMENT ||
        duplicate_flags !=
            static_cast<int64_t>(os::abi::OS_ABI_FILE_DESCRIPTOR_CLOSE_ON_EXEC_FLAG) ||
        os::user::SetDescriptorFlags(static_cast<uint64_t>(shared_descriptor),
                                     os::abi::OS_ABI_FILE_DESCRIPTOR_CLOSE_ON_EXEC_FLAG) !=
            OS_USER_SCHEDULER_SUCCESS_EXIT_CODE ||
        os::user::GetDescriptorFlags(static_cast<uint64_t>(shared_descriptor)) !=
            static_cast<int64_t>(os::abi::OS_ABI_FILE_DESCRIPTOR_CLOSE_ON_EXEC_FLAG)) {
        return false;
    }

    if (os::user::SetDescriptorSoftLimit(duplicate_minimum) !=
            OS_USER_SCHEDULER_SUCCESS_EXIT_CODE ||
        os::user::DuplicateDescriptor(static_cast<uint64_t>(shared_descriptor), duplicate_minimum,
                                      OS_USER_SCHEDULER_UNUSED_SYSTEM_CALL_ARGUMENT) !=
            os::abi::OS_ABI_SYSTEM_CALL_RESULT_DESCRIPTOR_LIMIT_EXCEEDED ||
        os::user::SetDescriptorSoftLimit(static_cast<uint64_t>(hard_limit)) !=
            OS_USER_SCHEDULER_SUCCESS_EXIT_CODE) {
        return false;
    }

    if (os::user::CloseDescriptor(static_cast<uint64_t>(independent_descriptor)) !=
        OS_USER_SCHEDULER_SUCCESS_EXIT_CODE) {
        return false;
    }
    const int64_t reused_descriptor =
        os::user::OpenFile(OS_USER_SCHEDULER_FILE_DESCRIPTION_PATH,
                           sizeof(OS_USER_SCHEDULER_FILE_DESCRIPTION_PATH) -
                               OS_USER_SCHEDULER_STRING_TERMINATOR_SIZE_BYTES,
                           os::abi::OS_ABI_FILE_OPEN_READ_FLAG);
    const bool close_succeeded =
        reused_descriptor ==
            static_cast<int64_t>(OS_USER_SCHEDULER_FILE_DESCRIPTION_SECOND_DESCRIPTOR) &&
        os::user::CloseDescriptor(static_cast<uint64_t>(reused_descriptor)) ==
            OS_USER_SCHEDULER_SUCCESS_EXIT_CODE &&
        os::user::CloseDescriptor(static_cast<uint64_t>(duplicate_descriptor)) ==
            OS_USER_SCHEDULER_SUCCESS_EXIT_CODE &&
        os::user::CloseDescriptor(static_cast<uint64_t>(shared_descriptor)) ==
            OS_USER_SCHEDULER_SUCCESS_EXIT_CODE;
    return close_succeeded && WriteMessage(OS_USER_SCHEDULER_FILE_DESCRIPTION_MODEL_MESSAGE);
}

}

extern "C" [[noreturn, gnu::section(".text.os_user_entry")]] void OsUserEntry() noexcept {
    const uint64_t process_id = os::user::GetProcessId();
    if (!ValidateSystemCallEntries(process_id) || !ValidateFileDescriptionModel(process_id) ||
        !os::user::InitializeExtendedStateIsolationTest(process_id)) {
        os::user::ExitProcess(OS_USER_SCHEDULER_FAILURE_EXIT_CODE);
    }
    for (uint64_t round = OS_USER_SCHEDULER_FIRST_ROUND; round <= OS_USER_SCHEDULER_ROUND_COUNT;
         ++round) {
        if (!RunWorkRound(round) || !os::user::ValidateExtendedStateIsolationTest(process_id) ||
            !WriteProgressMessage(process_id, round)) {
            os::user::ExitProcess(OS_USER_SCHEDULER_FAILURE_EXIT_CODE);
        }
    }
    if (!WriteMessage(OS_USER_SCHEDULER_ADDRESS_SPACE_ISOLATED_MESSAGE) ||
        !os::user::CompleteExtendedStateIsolationTest(process_id)) {
        os::user::ExitProcess(OS_USER_SCHEDULER_FAILURE_EXIT_CODE);
    }
    os::user::ExitProcess(OS_USER_SCHEDULER_SUCCESS_EXIT_CODE);
}

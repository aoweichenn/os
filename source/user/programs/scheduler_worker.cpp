#include "os/user/extended_state.hpp"
#include "os/user/system_call.hpp"

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
constexpr uint64_t OS_USER_SCHEDULER_FIRST_WORKER_PROCESS_ID = 2ULL;
constexpr uint64_t OS_USER_SCHEDULER_SECOND_WORKER_PROCESS_ID = 3ULL;
constexpr uint64_t OS_USER_SCHEDULER_THIRD_WORKER_PROCESS_ID = 4ULL;
constexpr uint64_t OS_USER_SCHEDULER_FIRST_ROUND = 1ULL;
constexpr uint64_t OS_USER_SCHEDULER_SECOND_ROUND = 2ULL;
constexpr uint64_t OS_USER_SCHEDULER_ROUND_COUNT = 3ULL;
constexpr uint64_t OS_USER_SCHEDULER_ITERATIONS_PER_ROUND = 1500000ULL;
constexpr uint64_t OS_USER_SCHEDULER_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_USER_SCHEDULER_STRING_TERMINATOR_SIZE_BYTES = 1ULL;
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

}

extern "C" [[noreturn, gnu::section(".text.os_user_entry")]] void OsUserEntry() noexcept {
    const uint64_t process_id = os::user::GetProcessId();
    if (!os::user::InitializeExtendedStateIsolationTest(process_id)) {
        os::user::ExitProcess(OS_USER_SCHEDULER_FAILURE_EXIT_CODE);
    }
    for (uint64_t round = OS_USER_SCHEDULER_FIRST_ROUND; round <= OS_USER_SCHEDULER_ROUND_COUNT;
         ++round) {
        if (!RunWorkRound(round) ||
            !os::user::ValidateExtendedStateIsolationTest(process_id) ||
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

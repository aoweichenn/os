#include "os/user/synchronization.hpp"
#include "os/user/system_call.hpp"
#include "os/user/thread.hpp"

#include "os/abi/system_call.hpp"
#include "os/abi/time.hpp"

#include <stdint.h>

namespace {

constexpr char OS_USER_TIME_PROBE_STARTED_MESSAGE[] =
    "[OS][USER][TIME] STARTED\r\n";
constexpr char OS_USER_TIME_PROBE_START_PREFIX[] =
    "[OS][USER][TIME] MONOTONIC_START_NS=";
constexpr char OS_USER_TIME_PROBE_WAKE_PREFIX[] =
    "[OS][USER][TIME] MONOTONIC_WAKE_NS=";
constexpr char OS_USER_TIME_PROBE_SLEEP_MESSAGE[] =
    "[OS][USER][TIME] SLEEP_VERIFIED\r\n";
constexpr char OS_USER_TIME_PROBE_FUTEX_TIMEOUT_MESSAGE[] =
    "[OS][USER][TIME] FUTEX_TIMEOUT_VERIFIED\r\n";
constexpr char OS_USER_TIME_PROBE_CONDITION_MESSAGE[] =
    "[OS][USER][TIME] CONDITION_WON_BEFORE_DEADLINE\r\n";
constexpr char OS_USER_TIME_PROBE_SINGLE_THREAD_MESSAGE[] =
    "[OS][USER][TIME] CONDITION_SINGLE_THREAD_PROFILE\r\n";
constexpr char OS_USER_TIME_PROBE_CONDITION_TIMEOUT_MESSAGE[] =
    "[OS][USER][TIME] CONDITION_TIMEOUT_VERIFIED\r\n";
constexpr char OS_USER_TIME_PROBE_COMPLETED_MESSAGE[] =
    "[OS][USER][TIME] COMPLETED\r\n";
constexpr uint64_t OS_USER_TIME_PROBE_STRING_TERMINATOR_BYTES = 1ULL;
constexpr uint64_t OS_USER_TIME_PROBE_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_USER_TIME_PROBE_HEX_DIGIT_COUNT = 16ULL;
constexpr uint64_t OS_USER_TIME_PROBE_HEX_DIGIT_BITS = 4ULL;
constexpr uint64_t OS_USER_TIME_PROBE_HEX_DIGIT_MASK = 0x0FULL;
constexpr uint64_t OS_USER_TIME_PROBE_HEX_PREFIX_BYTES = 2ULL;
constexpr uint64_t OS_USER_TIME_PROBE_LINE_ENDING_BYTES = 2ULL;
constexpr uint64_t OS_USER_TIME_PROBE_SLEEP_DURATION_NS =
    20ULL * os::abi::OS_ABI_TIME_NANOSECONDS_PER_MILLISECOND;
constexpr uint64_t OS_USER_TIME_PROBE_FUTEX_TIMEOUT_NS =
    10ULL * os::abi::OS_ABI_TIME_NANOSECONDS_PER_MILLISECOND;
constexpr uint64_t OS_USER_TIME_PROBE_WORKER_DELAY_NS =
    10ULL * os::abi::OS_ABI_TIME_NANOSECONDS_PER_MILLISECOND;
constexpr uint64_t OS_USER_TIME_PROBE_CONDITION_DEADLINE_NS =
    200ULL * os::abi::OS_ABI_TIME_NANOSECONDS_PER_MILLISECOND;
constexpr uint64_t OS_USER_TIME_PROBE_CONDITION_TIMEOUT_NS =
    10ULL * os::abi::OS_ABI_TIME_NANOSECONDS_PER_MILLISECOND;
constexpr uint64_t OS_USER_TIME_PROBE_MAXIMUM_SLEEP_ELAPSED_NS =
    os::abi::OS_ABI_TIME_NANOSECONDS_PER_SECOND;
constexpr uint64_t OS_USER_TIME_PROBE_FIRST_INDEX = 0ULL;
constexpr uint64_t OS_USER_TIME_PROBE_WORKER_EXIT_VALUE = 0x54494D45ULL;
constexpr int64_t OS_USER_TIME_PROBE_SUCCESS_RESULT = 0LL;
constexpr int64_t OS_USER_TIME_PROBE_FIRST_ERROR_RESULT = -1LL;
constexpr int64_t OS_USER_TIME_PROBE_SUCCESS_EXIT_CODE = 0LL;
constexpr int64_t OS_USER_TIME_PROBE_FAILURE_EXIT_CODE = 1LL;
constexpr char OS_USER_TIME_PROBE_HEX_DIGITS[] = "0123456789ABCDEF";
constexpr char OS_USER_TIME_PROBE_HEX_PREFIX_FIRST = '0';
constexpr char OS_USER_TIME_PROBE_HEX_PREFIX_SECOND = 'x';
constexpr char OS_USER_TIME_PROBE_CARRIAGE_RETURN = '\r';
constexpr char OS_USER_TIME_PROBE_LINE_FEED = '\n';

alignas(os::abi::OS_ABI_THREAD_LOCAL_STORAGE_ALIGNMENT_BYTES)
    os::user::ThreadRuntimeState main_runtime_state;
os::user::Mutex condition_mutex;
os::user::ConditionVariable condition_variable;
os::user::Thread notifier_thread;
bool condition_ready;
bool condition_single_thread_profile;

template <uint64_t MessageSizeBytes>
[[nodiscard]] bool
WriteMessage(const char (&message)[MessageSizeBytes]) noexcept {
    return os::user::WriteLog(
               message,
               MessageSizeBytes -
                   OS_USER_TIME_PROBE_STRING_TERMINATOR_BYTES) >
           OS_USER_TIME_PROBE_FIRST_ERROR_RESULT;
}

template <uint64_t PrefixSizeBytes>
[[nodiscard]] bool WriteHexValue(
    const char (&prefix)[PrefixSizeBytes], const uint64_t value) noexcept {
    constexpr uint64_t prefix_length_bytes =
        PrefixSizeBytes - OS_USER_TIME_PROBE_STRING_TERMINATOR_BYTES;
    char line[prefix_length_bytes + OS_USER_TIME_PROBE_HEX_PREFIX_BYTES +
              OS_USER_TIME_PROBE_HEX_DIGIT_COUNT +
              OS_USER_TIME_PROBE_LINE_ENDING_BYTES]{};
    for (uint64_t byte_index = OS_USER_TIME_PROBE_FIRST_INDEX;
         byte_index < prefix_length_bytes; ++byte_index) {
        line[byte_index] = prefix[byte_index];
    }
    uint64_t output_index = prefix_length_bytes;
    line[output_index++] = OS_USER_TIME_PROBE_HEX_PREFIX_FIRST;
    line[output_index++] = OS_USER_TIME_PROBE_HEX_PREFIX_SECOND;
    for (uint64_t digit_index = OS_USER_TIME_PROBE_FIRST_INDEX;
         digit_index < OS_USER_TIME_PROBE_HEX_DIGIT_COUNT;
        ++digit_index) {
        const uint64_t shift_bits =
            (OS_USER_TIME_PROBE_HEX_DIGIT_COUNT - digit_index -
             OS_USER_TIME_PROBE_COUNTER_INCREMENT) *
            OS_USER_TIME_PROBE_HEX_DIGIT_BITS;
        line[output_index++] = OS_USER_TIME_PROBE_HEX_DIGITS[
            (value >> shift_bits) & OS_USER_TIME_PROBE_HEX_DIGIT_MASK];
    }
    line[output_index++] = OS_USER_TIME_PROBE_CARRIAGE_RETURN;
    line[output_index] = OS_USER_TIME_PROBE_LINE_FEED;
    return os::user::WriteLog(line, sizeof(line)) >
           OS_USER_TIME_PROBE_FIRST_ERROR_RESULT;
}

[[nodiscard]] uint64_t RunNotifier(void *) noexcept {
    if (os::user::SleepFor(OS_USER_TIME_PROBE_WORKER_DELAY_NS) !=
            OS_USER_TIME_PROBE_SUCCESS_RESULT ||
        !condition_mutex.Lock()) {
        return UINT64_MAX;
    }
    condition_ready = true;
    condition_variable.NotifyOne();
    condition_mutex.Unlock();
    return OS_USER_TIME_PROBE_WORKER_EXIT_VALUE;
}

[[nodiscard]] bool VerifyConditionWinner() noexcept {
    condition_ready = false;
    if (!condition_mutex.Lock()) {
        return false;
    }
    if (!notifier_thread.Create(RunNotifier, nullptr)) {
        condition_single_thread_profile = true;
        condition_mutex.Unlock();
        return true;
    }
    const uint64_t now_nanoseconds = os::user::GetMonotonicTime();
    const uint64_t deadline_nanoseconds =
        now_nanoseconds > UINT64_MAX -
                              OS_USER_TIME_PROBE_CONDITION_DEADLINE_NS
            ? UINT64_MAX
            : now_nanoseconds +
                  OS_USER_TIME_PROBE_CONDITION_DEADLINE_NS;
    while (!condition_ready) {
        if (condition_variable.WaitUntil(
                condition_mutex, deadline_nanoseconds) !=
            os::user::ConditionWaitResult::ConditionSatisfied) {
            condition_mutex.Unlock();
            return false;
        }
    }
    condition_mutex.Unlock();
    uint64_t exit_value = OS_USER_TIME_PROBE_FIRST_INDEX;
    return notifier_thread.Join(exit_value) &&
           exit_value == OS_USER_TIME_PROBE_WORKER_EXIT_VALUE;
}

[[nodiscard]] bool VerifyConditionTimeout() noexcept {
    if (!condition_mutex.Lock()) {
        return false;
    }
    const uint64_t now_nanoseconds = os::user::GetMonotonicTime();
    const uint64_t deadline_nanoseconds =
        now_nanoseconds >
                UINT64_MAX -
                    OS_USER_TIME_PROBE_CONDITION_TIMEOUT_NS
            ? UINT64_MAX
            : now_nanoseconds +
                  OS_USER_TIME_PROBE_CONDITION_TIMEOUT_NS;
    const os::user::ConditionWaitResult wait_result =
        condition_variable.WaitUntil(condition_mutex,
                                     deadline_nanoseconds);
    condition_mutex.Unlock();
    return wait_result == os::user::ConditionWaitResult::TimedOut;
}

}

extern "C" [[noreturn, gnu::section(".text.os_user_entry")]]
void OsUserEntry(uint64_t, const char *const *const,
                 const char *const *const) noexcept {
    if (!os::user::InitializeMainThreadRuntime(main_runtime_state) ||
        !WriteMessage(OS_USER_TIME_PROBE_STARTED_MESSAGE)) {
        os::user::ExitProcess(OS_USER_TIME_PROBE_FAILURE_EXIT_CODE);
    }
    const uint64_t start_nanoseconds = os::user::GetMonotonicTime();
    if (!WriteHexValue(OS_USER_TIME_PROBE_START_PREFIX,
                       start_nanoseconds) ||
        os::user::SleepFor(OS_USER_TIME_PROBE_SLEEP_DURATION_NS) !=
            OS_USER_TIME_PROBE_SUCCESS_RESULT) {
        os::user::ExitProcess(OS_USER_TIME_PROBE_FAILURE_EXIT_CODE);
    }
    const uint64_t wake_nanoseconds = os::user::GetMonotonicTime();
    if (wake_nanoseconds < start_nanoseconds) {
        os::user::ExitProcess(OS_USER_TIME_PROBE_FAILURE_EXIT_CODE);
    }
    const uint64_t elapsed_nanoseconds =
        wake_nanoseconds - start_nanoseconds;
    if (elapsed_nanoseconds < OS_USER_TIME_PROBE_SLEEP_DURATION_NS ||
        elapsed_nanoseconds >
            OS_USER_TIME_PROBE_MAXIMUM_SLEEP_ELAPSED_NS ||
        !WriteHexValue(OS_USER_TIME_PROBE_WAKE_PREFIX,
                       wake_nanoseconds) ||
        !WriteMessage(OS_USER_TIME_PROBE_SLEEP_MESSAGE)) {
        os::user::ExitProcess(OS_USER_TIME_PROBE_FAILURE_EXIT_CODE);
    }

    uint32_t futex_word = 0U;
    const uint64_t futex_now_nanoseconds =
        os::user::GetMonotonicTime();
    const uint64_t futex_deadline_nanoseconds =
        futex_now_nanoseconds >
                UINT64_MAX -
                    OS_USER_TIME_PROBE_FUTEX_TIMEOUT_NS
            ? UINT64_MAX
            : futex_now_nanoseconds +
                  OS_USER_TIME_PROBE_FUTEX_TIMEOUT_NS;
    if (os::user::WaitPrivateFutexUntil(
            &futex_word, futex_word,
            futex_deadline_nanoseconds) !=
            os::abi::OS_ABI_SYSTEM_CALL_RESULT_TIMED_OUT ||
        !WriteMessage(OS_USER_TIME_PROBE_FUTEX_TIMEOUT_MESSAGE) ||
        !VerifyConditionWinner() ||
        !(condition_single_thread_profile
              ? WriteMessage(OS_USER_TIME_PROBE_SINGLE_THREAD_MESSAGE)
              : WriteMessage(OS_USER_TIME_PROBE_CONDITION_MESSAGE)) ||
        !VerifyConditionTimeout() ||
        !WriteMessage(
            OS_USER_TIME_PROBE_CONDITION_TIMEOUT_MESSAGE) ||
        !WriteMessage(OS_USER_TIME_PROBE_COMPLETED_MESSAGE)) {
        os::user::ExitProcess(OS_USER_TIME_PROBE_FAILURE_EXIT_CODE);
    }
    os::user::ExitProcess(OS_USER_TIME_PROBE_SUCCESS_EXIT_CODE);
}

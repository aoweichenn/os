#include "os/user/synchronization.hpp"
#include "os/user/system_call.hpp"
#include "os/user/thread.hpp"

#include "os/abi/thread.hpp"

#include <stdint.h>

namespace {

constexpr char OS_USER_THREAD_PROBE_STARTED_MESSAGE[] = "[OS][USER][THREAD] STARTED\r\n";
constexpr char OS_USER_THREAD_PROBE_BOOTSTRAP_MESSAGE[] =
    "[OS][USER][THREAD] BOOTSTRAP_SINGLE_THREAD_VERIFIED\r\n";
constexpr char OS_USER_THREAD_PROBE_FUNCTIONAL_READY_MESSAGE[] =
    "[OS][USER][THREAD] FUNCTIONAL_32_READY\r\n";
constexpr char OS_USER_THREAD_PROBE_CAPACITY_READY_MESSAGE[] =
    "[OS][USER][THREAD] CAPACITY_64_READY\r\n";
constexpr char OS_USER_THREAD_PROBE_TLS_MESSAGE[] = "[OS][USER][THREAD] TLS_ISOLATED\r\n";
constexpr char OS_USER_THREAD_PROBE_FUTEX_MESSAGE[] =
    "[OS][USER][THREAD] FUTEX_SYNCHRONIZATION_VERIFIED\r\n";
constexpr char OS_USER_THREAD_PROBE_JOIN_MESSAGE[] = "[OS][USER][THREAD] JOIN_RECLAIMED\r\n";
constexpr char OS_USER_THREAD_PROBE_COMPLETED_MESSAGE[] = "[OS][USER][THREAD] COMPLETED\r\n";
constexpr uint64_t OS_USER_THREAD_PROBE_STRING_TERMINATOR_BYTES = 1ULL;
constexpr uint64_t OS_USER_THREAD_PROBE_FUNCTIONAL_TOTAL_THREAD_COUNT = 32ULL;
constexpr uint64_t OS_USER_THREAD_PROBE_CAPACITY_TOTAL_THREAD_COUNT = 64ULL;
constexpr uint64_t OS_USER_THREAD_PROBE_MAIN_THREAD_COUNT = 1ULL;
constexpr uint64_t OS_USER_THREAD_PROBE_FUNCTIONAL_WORKER_COUNT =
    OS_USER_THREAD_PROBE_FUNCTIONAL_TOTAL_THREAD_COUNT - OS_USER_THREAD_PROBE_MAIN_THREAD_COUNT;
constexpr uint64_t OS_USER_THREAD_PROBE_CAPACITY_WORKER_COUNT =
    OS_USER_THREAD_PROBE_CAPACITY_TOTAL_THREAD_COUNT - OS_USER_THREAD_PROBE_MAIN_THREAD_COUNT;
constexpr uint64_t OS_USER_THREAD_PROBE_FIRST_INDEX = 0ULL;
constexpr uint64_t OS_USER_THREAD_PROBE_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_USER_THREAD_PROBE_WORK_ITERATIONS = 32ULL;
constexpr uint64_t OS_USER_THREAD_PROBE_EXIT_VALUE_BASE = 0x1200ULL;
constexpr int64_t OS_USER_THREAD_PROBE_SUCCESS_EXIT_CODE = 0LL;
constexpr int64_t OS_USER_THREAD_PROBE_FAILURE_EXIT_CODE = 1LL;
constexpr int64_t OS_USER_THREAD_PROBE_FIRST_ERROR_RESULT = -1LL;

struct WorkerArgument final {
    uint64_t worker_index;
};

alignas(os::abi::OS_ABI_THREAD_LOCAL_STORAGE_ALIGNMENT_BYTES)
    os::user::ThreadRuntimeState main_runtime_state;
os::user::Thread worker_threads[OS_USER_THREAD_PROBE_CAPACITY_WORKER_COUNT];
WorkerArgument worker_arguments[OS_USER_THREAD_PROBE_CAPACITY_WORKER_COUNT];
uint64_t worker_thread_ids[OS_USER_THREAD_PROBE_CAPACITY_WORKER_COUNT];
os::user::Thread limit_probe_thread;
os::user::Mutex state_mutex;
os::user::ConditionVariable state_changed;
os::user::Once once;
uint64_t ready_worker_count;
uint64_t protected_counter;
uint64_t once_execution_count;
uint64_t tls_failure_count;
bool workers_released;

template <uint64_t MessageSizeBytes>
[[nodiscard]] bool WriteMessage(const char (&message)[MessageSizeBytes]) noexcept {
    return os::user::WriteLog(message,
                              MessageSizeBytes - OS_USER_THREAD_PROBE_STRING_TERMINATOR_BYTES) >
           OS_USER_THREAD_PROBE_FIRST_ERROR_RESULT;
}

void RunOnce(void *const argument) noexcept {
    auto *const execution_count = static_cast<uint64_t *>(argument);
    *execution_count += OS_USER_THREAD_PROBE_COUNTER_INCREMENT;
}

[[nodiscard]] uint64_t RunWorker(void *const argument) noexcept {
    const auto *const worker_argument = static_cast<const WorkerArgument *>(argument);
    os::user::ThreadRuntimeState *const initial_runtime_state =
        os::user::CurrentThreadRuntimeState();
    const uint64_t initial_thread_id = os::user::GetThreadId();
    if (worker_argument == nullptr || initial_runtime_state == nullptr ||
        initial_runtime_state->thread_id != initial_thread_id) {
        if (!state_mutex.Lock()) {
            return UINT64_MAX;
        }
        ++tls_failure_count;
        state_mutex.Unlock();
        return UINT64_MAX;
    }

    if (!state_mutex.Lock()) {
        return UINT64_MAX;
    }
    ++ready_worker_count;
    state_changed.NotifyAll();
    while (!workers_released) {
        if (!state_changed.Wait(state_mutex)) {
            ++tls_failure_count;
            break;
        }
    }
    state_mutex.Unlock();

    if (!once.Call(RunOnce, &once_execution_count)) {
        if (!state_mutex.Lock()) {
            return UINT64_MAX;
        }
        ++tls_failure_count;
        state_mutex.Unlock();
    }
    for (uint64_t iteration = OS_USER_THREAD_PROBE_FIRST_INDEX;
         iteration < OS_USER_THREAD_PROBE_WORK_ITERATIONS; ++iteration) {
        if (!state_mutex.Lock()) {
            return UINT64_MAX;
        }
        ++protected_counter;
        state_mutex.Unlock();
    }
    if (os::user::CurrentThreadRuntimeState() != initial_runtime_state ||
        os::user::GetThreadId() != initial_thread_id) {
        if (!state_mutex.Lock()) {
            return UINT64_MAX;
        }
        ++tls_failure_count;
        state_mutex.Unlock();
    }
    return OS_USER_THREAD_PROBE_EXIT_VALUE_BASE + worker_argument->worker_index;
}

[[nodiscard]] bool ThreadIdentifiersAreUnique(const uint64_t worker_count) noexcept {
    for (uint64_t left_index = OS_USER_THREAD_PROBE_FIRST_INDEX; left_index < worker_count;
         ++left_index) {
        if (worker_thread_ids[left_index] == OS_USER_THREAD_PROBE_FIRST_INDEX) {
            return false;
        }
        for (uint64_t right_index = left_index + OS_USER_THREAD_PROBE_COUNTER_INCREMENT;
             right_index < worker_count; ++right_index) {
            if (worker_thread_ids[left_index] == worker_thread_ids[right_index]) {
                return false;
            }
        }
    }
    return true;
}

}

extern "C" [[noreturn, gnu::section(".text.os_user_entry")]]
void OsUserEntry(uint64_t, const char *const *const, const char *const *const) noexcept {
    if (!os::user::InitializeMainThreadRuntime(main_runtime_state) ||
        !WriteMessage(OS_USER_THREAD_PROBE_STARTED_MESSAGE)) {
        os::user::ExitProcess(OS_USER_THREAD_PROBE_FAILURE_EXIT_CODE);
    }
    uint64_t worker_count = OS_USER_THREAD_PROBE_FIRST_INDEX;
    for (; worker_count < OS_USER_THREAD_PROBE_CAPACITY_WORKER_COUNT; ++worker_count) {
        worker_arguments[worker_count] = WorkerArgument{.worker_index = worker_count};
        if (!worker_threads[worker_count].Create(RunWorker, &worker_arguments[worker_count])) {
            break;
        }
        worker_thread_ids[worker_count] = worker_threads[worker_count].Id();
    }
    if (worker_count == OS_USER_THREAD_PROBE_FIRST_INDEX) {
        if (!WriteMessage(OS_USER_THREAD_PROBE_BOOTSTRAP_MESSAGE) ||
            !WriteMessage(OS_USER_THREAD_PROBE_COMPLETED_MESSAGE)) {
            os::user::ExitProcess(OS_USER_THREAD_PROBE_FAILURE_EXIT_CODE);
        }
        os::user::ExitProcess(OS_USER_THREAD_PROBE_SUCCESS_EXIT_CODE);
    }
    const bool functional_profile = worker_count == OS_USER_THREAD_PROBE_FUNCTIONAL_WORKER_COUNT;
    const bool capacity_profile = worker_count == OS_USER_THREAD_PROBE_CAPACITY_WORKER_COUNT;
    if ((!functional_profile && !capacity_profile) || !ThreadIdentifiersAreUnique(worker_count)) {
        os::user::ExitProcess(OS_USER_THREAD_PROBE_FAILURE_EXIT_CODE);
    }
    if (capacity_profile &&
        limit_probe_thread.Create(RunWorker, &worker_arguments[OS_USER_THREAD_PROBE_FIRST_INDEX])) {
        os::user::ExitProcess(OS_USER_THREAD_PROBE_FAILURE_EXIT_CODE);
    }

    if (!state_mutex.Lock()) {
        os::user::ExitProcess(OS_USER_THREAD_PROBE_FAILURE_EXIT_CODE);
    }
    while (ready_worker_count < worker_count) {
        if (!state_changed.Wait(state_mutex)) {
            state_mutex.Unlock();
            os::user::ExitProcess(OS_USER_THREAD_PROBE_FAILURE_EXIT_CODE);
        }
    }
    workers_released = true;
    state_changed.NotifyAll();
    state_mutex.Unlock();
    if ((functional_profile && !WriteMessage(OS_USER_THREAD_PROBE_FUNCTIONAL_READY_MESSAGE)) ||
        (capacity_profile && !WriteMessage(OS_USER_THREAD_PROBE_CAPACITY_READY_MESSAGE))) {
        os::user::ExitProcess(OS_USER_THREAD_PROBE_FAILURE_EXIT_CODE);
    }

    for (uint64_t worker_index = OS_USER_THREAD_PROBE_FIRST_INDEX; worker_index < worker_count;
         ++worker_index) {
        uint64_t exit_value = OS_USER_THREAD_PROBE_FIRST_INDEX;
        if (!worker_threads[worker_index].Join(exit_value) ||
            exit_value != OS_USER_THREAD_PROBE_EXIT_VALUE_BASE + worker_index) {
            os::user::ExitProcess(OS_USER_THREAD_PROBE_FAILURE_EXIT_CODE);
        }
    }
    const uint64_t expected_counter = worker_count * OS_USER_THREAD_PROBE_WORK_ITERATIONS;
    if (tls_failure_count != OS_USER_THREAD_PROBE_FIRST_INDEX ||
        once_execution_count != OS_USER_THREAD_PROBE_COUNTER_INCREMENT ||
        protected_counter != expected_counter || !WriteMessage(OS_USER_THREAD_PROBE_TLS_MESSAGE) ||
        !WriteMessage(OS_USER_THREAD_PROBE_FUTEX_MESSAGE) ||
        !WriteMessage(OS_USER_THREAD_PROBE_JOIN_MESSAGE) ||
        !WriteMessage(OS_USER_THREAD_PROBE_COMPLETED_MESSAGE)) {
        os::user::ExitProcess(OS_USER_THREAD_PROBE_FAILURE_EXIT_CODE);
    }
    os::user::ExitProcess(OS_USER_THREAD_PROBE_SUCCESS_EXIT_CODE);
}

#include "os/user/synchronization.hpp"

#include "os/abi/system_call.hpp"
#include "os/abi/thread.hpp"
#include "os/user/system_call.hpp"

namespace os::user {

namespace {

constexpr uint32_t OS_USER_SYNCHRONIZATION_UNLOCKED_STATE = 0U;
constexpr uint32_t OS_USER_SYNCHRONIZATION_LOCKED_STATE = 1U;
constexpr uint32_t OS_USER_SYNCHRONIZATION_ONCE_NOT_STARTED_STATE = 0U;
constexpr uint32_t OS_USER_SYNCHRONIZATION_ONCE_RUNNING_STATE = 1U;
constexpr uint32_t OS_USER_SYNCHRONIZATION_ONCE_COMPLETED_STATE = 2U;
constexpr uint32_t OS_USER_SYNCHRONIZATION_SEQUENCE_INCREMENT = 1U;
constexpr uint64_t OS_USER_SYNCHRONIZATION_WAKE_ONE_COUNT = 1ULL;
constexpr int64_t OS_USER_SYNCHRONIZATION_SUCCESS_RESULT = 0LL;

[[nodiscard]] bool WaitResultAllowsRetry(const int64_t wait_result) noexcept {
    return wait_result == OS_USER_SYNCHRONIZATION_SUCCESS_RESULT ||
           wait_result == os::abi::OS_ABI_SYSTEM_CALL_RESULT_FUTEX_VALUE_CHANGED;
}

}

bool Mutex::Lock() noexcept {
    while (!this->TryLock()) {
        const int64_t wait_result =
            WaitPrivateFutex(&this->state_, OS_USER_SYNCHRONIZATION_LOCKED_STATE);
        if (!WaitResultAllowsRetry(wait_result)) {
            return false;
        }
    }
    return true;
}

bool Mutex::TryLock() noexcept {
    uint32_t expected_state = OS_USER_SYNCHRONIZATION_UNLOCKED_STATE;
    return __atomic_compare_exchange_n(&this->state_, &expected_state,
                                       OS_USER_SYNCHRONIZATION_LOCKED_STATE, false,
                                       __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}

void Mutex::Unlock() noexcept {
    __atomic_store_n(&this->state_, OS_USER_SYNCHRONIZATION_UNLOCKED_STATE, __ATOMIC_RELEASE);
    static_cast<void>(WakePrivateFutex(&this->state_, OS_USER_SYNCHRONIZATION_WAKE_ONE_COUNT));
}

bool ConditionVariable::Wait(Mutex &mutex) noexcept {
    // 必须先取得序列号再解锁；内核的 compare-and-block 会封闭解锁到睡眠之间的竞态窗口。
    const uint32_t observed_sequence = __atomic_load_n(&this->sequence_, __ATOMIC_ACQUIRE);
    mutex.Unlock();
    const int64_t wait_result = WaitPrivateFutex(&this->sequence_, observed_sequence);
    return mutex.Lock() && WaitResultAllowsRetry(wait_result);
}

void ConditionVariable::NotifyOne() noexcept {
    static_cast<void>(__atomic_add_fetch(
        &this->sequence_, OS_USER_SYNCHRONIZATION_SEQUENCE_INCREMENT, __ATOMIC_RELEASE));
    static_cast<void>(WakePrivateFutex(&this->sequence_, OS_USER_SYNCHRONIZATION_WAKE_ONE_COUNT));
}

void ConditionVariable::NotifyAll() noexcept {
    static_cast<void>(__atomic_add_fetch(
        &this->sequence_, OS_USER_SYNCHRONIZATION_SEQUENCE_INCREMENT, __ATOMIC_RELEASE));
    static_cast<void>(WakePrivateFutex(&this->sequence_, os::abi::OS_ABI_THREAD_WAIT_ANY_COUNT));
}

bool Once::Call(OnceFunction const function, void *const argument) noexcept {
    if (function == nullptr) {
        return false;
    }
    uint32_t expected_state = OS_USER_SYNCHRONIZATION_ONCE_NOT_STARTED_STATE;
    if (__atomic_compare_exchange_n(&this->state_, &expected_state,
                                    OS_USER_SYNCHRONIZATION_ONCE_RUNNING_STATE, false,
                                    __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        function(argument);
        __atomic_store_n(&this->state_, OS_USER_SYNCHRONIZATION_ONCE_COMPLETED_STATE,
                         __ATOMIC_RELEASE);
        static_cast<void>(WakePrivateFutex(&this->state_, os::abi::OS_ABI_THREAD_WAIT_ANY_COUNT));
        return true;
    }
    while (__atomic_load_n(&this->state_, __ATOMIC_ACQUIRE) ==
           OS_USER_SYNCHRONIZATION_ONCE_RUNNING_STATE) {
        const int64_t wait_result =
            WaitPrivateFutex(&this->state_, OS_USER_SYNCHRONIZATION_ONCE_RUNNING_STATE);
        if (!WaitResultAllowsRetry(wait_result)) {
            return false;
        }
    }
    return __atomic_load_n(&this->state_, __ATOMIC_ACQUIRE) ==
           OS_USER_SYNCHRONIZATION_ONCE_COMPLETED_STATE;
}

}

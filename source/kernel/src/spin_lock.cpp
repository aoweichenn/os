#include "os/kernel/spin_lock.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_SYNCHRONIZATION_UNLOCKED_STATE = 0ULL;
constexpr uint64_t OS_KERNEL_SYNCHRONIZATION_LOCKED_STATE = 1ULL;

// 这些常量来自编译器原子内建接口；用具名边界常量隔离不可由 freestanding
// C++20 标准库表达的内存序参数。
constexpr int OS_KERNEL_SYNCHRONIZATION_ATOMIC_ACQUIRE_ORDER = __ATOMIC_ACQUIRE;
constexpr int OS_KERNEL_SYNCHRONIZATION_ATOMIC_RELEASE_ORDER = __ATOMIC_RELEASE;
constexpr int OS_KERNEL_SYNCHRONIZATION_ATOMIC_RELAXED_ORDER = __ATOMIC_RELAXED;

void PauseProcessor() noexcept {
#if defined(__x86_64__)
    asm volatile("pause" : : : "memory");
#endif
}

}

bool SpinLock::TryLock() noexcept {
    return __atomic_exchange_n(&this->state_, OS_KERNEL_SYNCHRONIZATION_LOCKED_STATE,
                               OS_KERNEL_SYNCHRONIZATION_ATOMIC_ACQUIRE_ORDER) ==
           OS_KERNEL_SYNCHRONIZATION_UNLOCKED_STATE;
}

void SpinLock::Lock() noexcept {
    while (!this->TryLock()) {
        while (__atomic_load_n(&this->state_, OS_KERNEL_SYNCHRONIZATION_ATOMIC_RELAXED_ORDER) ==
               OS_KERNEL_SYNCHRONIZATION_LOCKED_STATE) {
            PauseProcessor();
        }
    }
}

void SpinLock::Unlock() noexcept {
    __atomic_store_n(&this->state_, OS_KERNEL_SYNCHRONIZATION_UNLOCKED_STATE,
                     OS_KERNEL_SYNCHRONIZATION_ATOMIC_RELEASE_ORDER);
}

bool SpinLock::IsLocked() const noexcept {
    return __atomic_load_n(&this->state_, OS_KERNEL_SYNCHRONIZATION_ATOMIC_RELAXED_ORDER) ==
           OS_KERNEL_SYNCHRONIZATION_LOCKED_STATE;
}

SpinLockGuard::SpinLockGuard(SpinLock &lock) noexcept : lock_(lock) { this->lock_.Lock(); }

SpinLockGuard::~SpinLockGuard() noexcept { this->lock_.Unlock(); }
}

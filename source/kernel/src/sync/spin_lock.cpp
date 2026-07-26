#include "os/kernel/sync/spin_lock.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_SYNCHRONIZATION_UNLOCKED_STATE = 0ULL;
constexpr uint64_t OS_KERNEL_SYNCHRONIZATION_LOCKED_STATE = 1ULL;

// 这些常量来自编译器原子内建接口；用具名边界常量隔离不可由 freestanding
// C++20 标准库表达的内存序参数。
constexpr int OS_KERNEL_SYNCHRONIZATION_ATOMIC_ACQUIRE_ORDER = __ATOMIC_ACQUIRE;
constexpr int OS_KERNEL_SYNCHRONIZATION_ATOMIC_RELEASE_ORDER = __ATOMIC_RELEASE;
constexpr int OS_KERNEL_SYNCHRONIZATION_ATOMIC_RELAXED_ORDER = __ATOMIC_RELAXED;

uint64_t current_spin_lock_depth;

void PauseProcessor() noexcept {
#if defined(__x86_64__)
    asm volatile("pause" : : : "memory");
#endif
}

}

bool SpinLock::TryLock() noexcept {
    const bool acquired =
        __atomic_exchange_n(&this->state_, OS_KERNEL_SYNCHRONIZATION_LOCKED_STATE,
                            OS_KERNEL_SYNCHRONIZATION_ATOMIC_ACQUIRE_ORDER) ==
        OS_KERNEL_SYNCHRONIZATION_UNLOCKED_STATE;
    if (acquired) {
        __atomic_add_fetch(&current_spin_lock_depth, OS_KERNEL_SYNCHRONIZATION_LOCKED_STATE,
                           OS_KERNEL_SYNCHRONIZATION_ATOMIC_RELAXED_ORDER);
    }
    return acquired;
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
    const uint64_t observed_depth =
        __atomic_load_n(&current_spin_lock_depth,
                        OS_KERNEL_SYNCHRONIZATION_ATOMIC_RELAXED_ORDER);
    if (observed_depth != OS_KERNEL_SYNCHRONIZATION_UNLOCKED_STATE) {
        __atomic_sub_fetch(&current_spin_lock_depth,
                           OS_KERNEL_SYNCHRONIZATION_LOCKED_STATE,
                           OS_KERNEL_SYNCHRONIZATION_ATOMIC_RELAXED_ORDER);
    }
    __atomic_store_n(&this->state_, OS_KERNEL_SYNCHRONIZATION_UNLOCKED_STATE,
                     OS_KERNEL_SYNCHRONIZATION_ATOMIC_RELEASE_ORDER);
}

bool SpinLock::IsLocked() const noexcept {
    return __atomic_load_n(&this->state_, OS_KERNEL_SYNCHRONIZATION_ATOMIC_RELAXED_ORDER) ==
           OS_KERNEL_SYNCHRONIZATION_LOCKED_STATE;
}

SpinLockGuard::SpinLockGuard(SpinLock &lock) noexcept : lock_(lock) { this->lock_.Lock(); }

SpinLockGuard::~SpinLockGuard() noexcept { this->lock_.Unlock(); }

bool IrqSaveSpinLock::TryLock(bool &interrupts_were_enabled) noexcept {
    interrupts_were_enabled = false;
    if (!this->IsUsable()) {
        return false;
    }
    const bool previous_interrupt_state = this->disable_interrupts_();
    if (!this->lock_.TryLock()) {
        this->restore_interrupts_(previous_interrupt_state);
        return false;
    }
    interrupts_were_enabled = previous_interrupt_state;
    return true;
}

bool IrqSaveSpinLock::Lock() noexcept {
    if (!this->IsUsable()) {
        return false;
    }
    const bool interrupts_were_enabled = this->disable_interrupts_();
    this->lock_.Lock();
    return interrupts_were_enabled;
}

void IrqSaveSpinLock::Unlock(const bool interrupts_were_enabled) noexcept {
    this->lock_.Unlock();
    if (this->restore_interrupts_ != nullptr) {
        this->restore_interrupts_(interrupts_were_enabled);
    }
}

bool IrqSaveSpinLock::IsLocked() const noexcept { return this->lock_.IsLocked(); }

bool IrqSaveSpinLock::IsUsable() const noexcept {
    return this->disable_interrupts_ != nullptr &&
           this->restore_interrupts_ != nullptr;
}

IrqSaveSpinLockGuard::IrqSaveSpinLockGuard(IrqSaveSpinLock &lock) noexcept
    : lock_(lock), interrupts_were_enabled_(this->lock_.Lock()) {}

IrqSaveSpinLockGuard::~IrqSaveSpinLockGuard() noexcept {
    this->lock_.Unlock(this->interrupts_were_enabled_);
}

uint64_t CurrentSpinLockDepth() noexcept {
    return __atomic_load_n(&current_spin_lock_depth,
                           OS_KERNEL_SYNCHRONIZATION_ATOMIC_RELAXED_ORDER);
}
}

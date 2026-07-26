#pragma once

#include <stdint.h>

namespace os::kernel {

using DisableInterruptsOperation = bool (*)() noexcept;
using RestoreInterruptsOperation = void (*)(bool interrupts_were_enabled) noexcept;

class SpinLock final {
  public:
    [[nodiscard]] bool TryLock() noexcept;
    void Lock() noexcept;
    void Unlock() noexcept;
    [[nodiscard]] bool IsLocked() const noexcept;

  private:
    uint64_t state_;
};

class SpinLockGuard final {
  public:
    explicit SpinLockGuard(SpinLock &lock) noexcept;
    ~SpinLockGuard() noexcept;

    SpinLockGuard(const SpinLockGuard &) = delete;
    SpinLockGuard &operator=(const SpinLockGuard &) = delete;

  private:
    SpinLock &lock_;
};

class IrqSaveSpinLock final {
  public:
    constexpr IrqSaveSpinLock(DisableInterruptsOperation disable_interrupts,
                              RestoreInterruptsOperation restore_interrupts) noexcept
        : disable_interrupts_(disable_interrupts),
          restore_interrupts_(restore_interrupts) {}

    [[nodiscard]] bool TryLock(bool &interrupts_were_enabled) noexcept;
    [[nodiscard]] bool Lock() noexcept;
    void Unlock(bool interrupts_were_enabled) noexcept;
    [[nodiscard]] bool IsLocked() const noexcept;
    [[nodiscard]] bool IsUsable() const noexcept;

  private:
    DisableInterruptsOperation disable_interrupts_{};
    RestoreInterruptsOperation restore_interrupts_{};
    SpinLock lock_{};
};

class IrqSaveSpinLockGuard final {
  public:
    explicit IrqSaveSpinLockGuard(IrqSaveSpinLock &lock) noexcept;
    ~IrqSaveSpinLockGuard() noexcept;

    IrqSaveSpinLockGuard(const IrqSaveSpinLockGuard &) = delete;
    IrqSaveSpinLockGuard &operator=(const IrqSaveSpinLockGuard &) = delete;

  private:
    IrqSaveSpinLock &lock_;
    bool interrupts_were_enabled_;
};

[[nodiscard]] uint64_t CurrentSpinLockDepth() noexcept;

}

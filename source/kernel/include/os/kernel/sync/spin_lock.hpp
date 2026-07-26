#pragma once

#include <stdint.h>

namespace os::kernel {

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

}

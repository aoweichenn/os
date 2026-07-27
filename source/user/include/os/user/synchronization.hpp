#pragma once

#include <stdint.h>

namespace os::user {

enum class ConditionWaitResult : uint64_t {
    ConditionSatisfied,
    TimedOut,
    Failed,
};

class Mutex final {
  public:
    Mutex() noexcept = default;
    Mutex(const Mutex &) = delete;
    Mutex &operator=(const Mutex &) = delete;

    [[nodiscard]] bool Lock() noexcept;
    [[nodiscard]] bool TryLock() noexcept;
    void Unlock() noexcept;

  private:
    uint32_t state_{};
};

class ConditionVariable final {
  public:
    ConditionVariable() noexcept = default;
    ConditionVariable(const ConditionVariable &) = delete;
    ConditionVariable &operator=(const ConditionVariable &) = delete;

    [[nodiscard]] bool Wait(Mutex &mutex) noexcept;
    [[nodiscard]] ConditionWaitResult
    WaitUntil(Mutex &mutex, uint64_t deadline_nanoseconds) noexcept;
    void NotifyOne() noexcept;
    void NotifyAll() noexcept;

  private:
    uint32_t sequence_{};
};

using OnceFunction = void (*)(void *argument) noexcept;

class Once final {
  public:
    Once() noexcept = default;
    Once(const Once &) = delete;
    Once &operator=(const Once &) = delete;

    [[nodiscard]] bool Call(OnceFunction function, void *argument) noexcept;

  private:
    uint32_t state_{};
};

}

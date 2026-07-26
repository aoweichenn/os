#pragma once

#include <stdint.h>

namespace os::foundation {

enum class ReferenceCounterStatus : uint64_t {
    Succeeded,
    EmptyInitialReferenceCount,
    ActiveReferencesRemain,
    ReferenceUnavailable,
    CounterOverflow,
};

// 该计数器只描述一个对象生命周期内的强引用数量。
// 当前阶段由单个 BSP 使用；进入并发阶段后，调用方必须在同一把锁内完成对象查找与计数修改。
class ReferenceCounter final {
  public:
    ReferenceCounter() noexcept;
    ReferenceCounter(const ReferenceCounter &) = delete;
    ReferenceCounter &operator=(const ReferenceCounter &) = delete;

    [[nodiscard]] ReferenceCounterStatus Start(uint64_t initial_reference_count) noexcept;
    [[nodiscard]] ReferenceCounterStatus TryAcquire() noexcept;
    [[nodiscard]] ReferenceCounterStatus
    TryRelease(bool &released_last_reference) noexcept;
    [[nodiscard]] uint64_t Count() const noexcept;
    [[nodiscard]] bool IsActive() const noexcept;

  private:
    uint64_t reference_count_;
};

}

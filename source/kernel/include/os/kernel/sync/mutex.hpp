#pragma once

#include "os/kernel/process/thread_scheduler.hpp"

#include <stdint.h>

namespace os::kernel {

enum class MutexStatus : uint64_t {
    Succeeded,
    Blocked,
    WouldBlock,
    NotInitialized,
    AlreadyInitialized,
    InvalidOwner,
    AlreadyOwned,
    NotOwner,
    SpinLockHeld,
    SchedulerFailure,
    WaitQueueFailure,
};

struct MutexStatistics final {
    ThreadId owner_thread_id;
    uint64_t acquisition_count;
    uint64_t contention_count;
    uint64_t handoff_count;
    uint64_t unlock_count;
    uint64_t waiting_thread_count;
    bool owned;
};

class Mutex final {
  public:
    [[nodiscard]] MutexStatus Initialize(WaitQueueId wait_queue_id) noexcept;
    [[nodiscard]] MutexStatus TryLock(ThreadId thread_id) noexcept;
    [[nodiscard]] MutexStatus Lock(ThreadScheduler &scheduler, ThreadId thread_id,
                                   ThreadSchedulingDecision &decision) noexcept;
    [[nodiscard]] MutexStatus Unlock(ThreadScheduler &scheduler, ThreadId thread_id,
                                     uint64_t &woken_thread_index) noexcept;
    [[nodiscard]] MutexStatus Reset() noexcept;
    [[nodiscard]] bool IsOwnedBy(ThreadId thread_id) const noexcept;
    [[nodiscard]] MutexStatistics Statistics() const noexcept;
    [[nodiscard]] const WaitQueue &GetWaitQueue() const noexcept;

  private:
    WaitQueue wait_queue_{};
    ThreadId owner_thread_id_{};
    uint64_t acquisition_count_{};
    uint64_t contention_count_{};
    uint64_t handoff_count_{};
    uint64_t unlock_count_{};
    bool initialized_{};
    bool owned_{};
    bool handoff_pending_{};
};

}

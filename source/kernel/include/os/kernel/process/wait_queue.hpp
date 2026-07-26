#pragma once

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_WAIT_QUEUE_INVALID_THREAD_INDEX = UINT64_MAX;

struct WaitQueueId final {
    uint64_t value;
};

enum class WaitCondition : uint64_t {
    None,
    PipeReadable,
    PipeWritable,
    DescriptorReadable,
    DescriptorWritable,
    MutexAvailable,
    TestCondition,
};

enum class WakeReason : uint64_t {
    None,
    ConditionSatisfied,
    Timeout,
    Signal,
    ObjectClosed,
    Cancelled,
};

enum class WaitQueueStatus : uint64_t {
    Succeeded,
    AlreadyInitialized,
    NotInitialized,
    InvalidIdentifier,
    WaitersRemain,
};

struct WaitQueueStatistics final {
    WaitQueueId queue_id;
    uint64_t waiting_thread_count;
    uint64_t enqueue_count;
    uint64_t wake_count;
    uint64_t close_count;
    bool closed;
};

class ThreadScheduler;

class WaitQueue final {
  public:
    [[nodiscard]] WaitQueueStatus Initialize(WaitQueueId queue_id) noexcept;
    [[nodiscard]] WaitQueueStatus Reset() noexcept;
    [[nodiscard]] WaitQueueStatistics Statistics() const noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] bool IsClosed() const noexcept;

  private:
    friend class ThreadScheduler;

    WaitQueueId queue_id_{};
    uint64_t head_thread_index_{OS_KERNEL_WAIT_QUEUE_INVALID_THREAD_INDEX};
    uint64_t tail_thread_index_{OS_KERNEL_WAIT_QUEUE_INVALID_THREAD_INDEX};
    uint64_t waiting_thread_count_{};
    uint64_t enqueue_count_{};
    uint64_t wake_count_{};
    uint64_t close_count_{};
    bool initialized_{};
    bool closed_{};
};

}

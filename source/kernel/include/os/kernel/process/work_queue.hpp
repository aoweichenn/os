#pragma once

#include <os/kernel/sync/spin_lock.hpp>

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_WORK_QUEUE_CAPACITY_LIMIT = 256ULL;
inline constexpr uint64_t OS_KERNEL_WORK_QUEUE_INVALID_INDEX = UINT64_MAX;

struct WorkHandle final {
    uint64_t slot_index;
    uint64_t generation;
};

enum class WorkState : uint64_t {
    Free,
    Idle,
    Delayed,
    Queued,
    Running,
    Completed,
    Cancelled,
};

enum class WorkExecutionResult : uint64_t {
    Succeeded,
    Failed,
};

using WorkOperation = WorkExecutionResult (*)(void *context) noexcept;

struct WorkQueueEntry final {
    uint64_t generation;
    WorkState state;
    WorkOperation operation;
    void *context;
    uint64_t deadline_nanoseconds;
    uint64_t enqueue_sequence;
    uint64_t delayed_heap_index;
    uint64_t previous_ready_index;
    uint64_t next_ready_index;
};

struct WorkExecution final {
    WorkHandle handle;
    WorkOperation operation;
    void *context;
};

enum class WorkQueueStatus : uint64_t {
    Succeeded,
    NotInitialized,
    AlreadyInitialized,
    NullEntryStorage,
    NullDelayedHeapStorage,
    InvalidCapacity,
    InvalidOperation,
    InvalidHandle,
    StaleHandle,
    CapacityExhausted,
    IdentifierExhausted,
    InvalidDeadline,
    InvalidState,
    AlreadyPending,
    AlreadyRunning,
    NotPending,
    NoReadyWork,
    AlreadyDraining,
    DrainInProgress,
    DrainIncomplete,
    CounterOverflow,
    CorruptedState,
};

struct WorkQueueStatistics final {
    uint64_t capacity;
    uint64_t registered_count;
    uint64_t idle_count;
    uint64_t delayed_count;
    uint64_t queued_count;
    uint64_t running_count;
    uint64_t completed_count;
    uint64_t cancelled_count;
    uint64_t peak_registered_count;
    uint64_t peak_pending_count;
    uint64_t peak_running_count;
    uint64_t registration_count;
    uint64_t release_count;
    uint64_t immediate_queue_count;
    uint64_t delayed_queue_count;
    uint64_t expedited_queue_count;
    uint64_t coalesced_queue_count;
    uint64_t delayed_promotion_count;
    uint64_t acquisition_count;
    uint64_t completion_count;
    uint64_t failed_execution_count;
    uint64_t cancellation_count;
    uint64_t reset_count;
    uint64_t drain_begin_count;
    uint64_t drain_end_count;
    uint64_t drain_rejection_count;
    uint64_t capacity_rejection_count;
    uint64_t stale_handle_rejection_count;
    bool draining;
};

class WorkQueue final {
  public:
    WorkQueue() noexcept = default;
    WorkQueue(const WorkQueue &) = delete;
    WorkQueue &operator=(const WorkQueue &) = delete;

    [[nodiscard]] WorkQueueStatus Initialize(WorkQueueEntry *entry_storage,
                                             uint64_t *delayed_heap_storage,
                                             uint64_t capacity) noexcept;
    [[nodiscard]] WorkQueueStatus Register(WorkOperation operation, void *context,
                                           WorkHandle &handle) noexcept;
    [[nodiscard]] WorkQueueStatus Release(WorkHandle handle) noexcept;
    [[nodiscard]] WorkQueueStatus Queue(WorkHandle handle) noexcept;
    [[nodiscard]] WorkQueueStatus QueueDelayed(WorkHandle handle,
                                               uint64_t deadline_nanoseconds) noexcept;
    [[nodiscard]] WorkQueueStatus Cancel(WorkHandle handle) noexcept;
    [[nodiscard]] WorkQueueStatus Reset(WorkHandle handle) noexcept;
    [[nodiscard]] WorkQueueStatus AcquireNext(uint64_t now_nanoseconds,
                                              WorkExecution &execution) noexcept;
    [[nodiscard]] WorkQueueStatus NextDeadline(uint64_t &deadline_nanoseconds,
                                               bool &deadline_available) const noexcept;
    [[nodiscard]] WorkQueueStatus Complete(WorkHandle handle, WorkExecutionResult result) noexcept;
    [[nodiscard]] WorkQueueStatus BeginDrain() noexcept;
    [[nodiscard]] WorkQueueStatus EndDrain() noexcept;
    [[nodiscard]] bool DrainComplete() const noexcept;
    [[nodiscard]] WorkQueueStatus Read(WorkHandle handle, WorkQueueEntry &entry) const noexcept;
    [[nodiscard]] WorkQueueStatistics Statistics() const noexcept;
    [[nodiscard]] WorkQueueStatus Validate() const noexcept;

  private:
    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] bool HandleIsValid(WorkHandle handle) const noexcept;
    [[nodiscard]] bool PendingCount(uint64_t &pending_count) const noexcept;
    [[nodiscard]] bool DelayedLess(uint64_t left_slot_index,
                                   uint64_t right_slot_index) const noexcept;
    void AppendReady(uint64_t slot_index) noexcept;
    void RemoveReady(uint64_t slot_index) noexcept;
    void SwapDelayed(uint64_t left_heap_index, uint64_t right_heap_index) noexcept;
    void BubbleDelayedUp(uint64_t heap_index) noexcept;
    void BubbleDelayedDown(uint64_t heap_index) noexcept;
    void InsertDelayed(uint64_t slot_index) noexcept;
    void RemoveDelayed(uint64_t heap_index) noexcept;
    [[nodiscard]] WorkQueueStatus PromoteDue(uint64_t now_nanoseconds) noexcept;
    [[nodiscard]] WorkQueueStatus QueueLocked(WorkHandle handle, bool delayed,
                                              uint64_t deadline_nanoseconds) noexcept;
    [[nodiscard]] WorkQueueStatistics StatisticsLocked() const noexcept;

    mutable SpinLock lock_{};
    WorkQueueEntry *entries_{};
    uint64_t *delayed_heap_{};
    uint64_t capacity_{};
    uint64_t delayed_count_{};
    uint64_t ready_head_index_{OS_KERNEL_WORK_QUEUE_INVALID_INDEX};
    uint64_t ready_tail_index_{OS_KERNEL_WORK_QUEUE_INVALID_INDEX};
    uint64_t next_enqueue_sequence_{};
    WorkQueueStatistics cumulative_statistics_{};
    bool draining_{};
    bool initialized_{};
};

}

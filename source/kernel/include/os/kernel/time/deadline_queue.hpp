#pragma once

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_DEADLINE_QUEUE_CAPACITY_LIMIT = 512ULL;
inline constexpr uint64_t OS_KERNEL_DEADLINE_QUEUE_INVALID_THREAD_INDEX = UINT64_MAX;

enum class DeadlineResolution : uint64_t {
    Expired,
    Cancelled,
};

enum class DeadlineQueueStatus : uint64_t {
    Succeeded,
    NotInitialized,
    AlreadyInitialized,
    InvalidCapacity,
    InvalidThreadIndex,
    AlreadyScheduled,
    EntryNotFound,
    SequenceExhausted,
    ActiveEntriesRemain,
    Corrupt,
};

struct DeadlineEntry final {
    uint64_t deadline_nanoseconds;
    uint64_t registration_sequence;
    uint64_t previous_thread_index;
    uint64_t next_thread_index;
    bool active;
};

struct DeadlineQueueStatistics final {
    uint64_t capacity;
    uint64_t active_entry_count;
    uint64_t peak_active_entry_count;
    uint64_t schedule_count;
    uint64_t expiration_count;
    uint64_t cancellation_count;
    uint64_t next_registration_sequence;
};

class DeadlineQueue final {
  public:
    [[nodiscard]] DeadlineQueueStatus Initialize(uint64_t capacity) noexcept;
    [[nodiscard]] DeadlineQueueStatus Reset() noexcept;
    [[nodiscard]] DeadlineQueueStatus Schedule(uint64_t thread_index,
                                               uint64_t deadline_nanoseconds) noexcept;
    [[nodiscard]] DeadlineQueueStatus Resolve(uint64_t thread_index,
                                              DeadlineResolution resolution) noexcept;
    [[nodiscard]] DeadlineQueueStatus PeekExpired(uint64_t now_nanoseconds,
                                                  uint64_t &thread_index,
                                                  bool &expired) const noexcept;
    [[nodiscard]] DeadlineQueueStatus Contains(uint64_t thread_index,
                                               bool &scheduled) const noexcept;
    [[nodiscard]] DeadlineQueueStatus Read(uint64_t thread_index,
                                           DeadlineEntry &entry) const noexcept;
    [[nodiscard]] DeadlineQueueStatistics Statistics() const noexcept;
    [[nodiscard]] DeadlineQueueStatus Validate() const noexcept;

  private:
    [[nodiscard]] bool EntryPrecedes(uint64_t left_thread_index,
                                     uint64_t right_thread_index) const noexcept;
    void Remove(uint64_t thread_index) noexcept;

    DeadlineEntry entries_[OS_KERNEL_DEADLINE_QUEUE_CAPACITY_LIMIT]{};
    uint64_t capacity_{};
    uint64_t head_thread_index_{OS_KERNEL_DEADLINE_QUEUE_INVALID_THREAD_INDEX};
    uint64_t tail_thread_index_{OS_KERNEL_DEADLINE_QUEUE_INVALID_THREAD_INDEX};
    DeadlineQueueStatistics statistics_{};
    bool initialized_{};
};

}

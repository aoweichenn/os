#pragma once

#include "os/kernel/process/wait_queue.hpp"

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_PRIVATE_FUTEX_CAPACITY_LIMIT = 512ULL;
inline constexpr uint64_t OS_KERNEL_PRIVATE_FUTEX_INVALID_INDEX = UINT64_MAX;
inline constexpr uint64_t OS_KERNEL_PRIVATE_FUTEX_WORD_ALIGNMENT_BYTES = sizeof(uint32_t);

struct PrivateFutexKey final {
    uint64_t address_space_identifier;
    uint64_t user_address;
};

struct PrivateFutexEntry final {
    PrivateFutexKey key;
    WaitQueue wait_queue;
    bool active;
};

enum class PrivateFutexStatus : uint64_t {
    Succeeded,
    NotInitialized,
    AlreadyInitialized,
    InvalidStorage,
    InvalidCapacity,
    InvalidAddressSpace,
    InvalidAddress,
    EntryCapacityExhausted,
    EntryNotFound,
    WaitersRemain,
    QueueFailure,
    Corrupt,
};

struct PrivateFutexStatistics final {
    uint64_t capacity;
    uint64_t active_entry_count;
    uint64_t waiting_thread_count;
    uint64_t peak_active_entry_count;
    uint64_t acquire_count;
    uint64_t release_count;
    uint64_t wait_prepare_count;
    uint64_t wake_operation_count;
    uint64_t cancellation_operation_count;
};

class PrivateFutexManager final {
  public:
    [[nodiscard]] PrivateFutexStatus Initialize(PrivateFutexEntry *entry_storage, uint64_t capacity,
                                                uint64_t first_queue_identifier) noexcept;
    [[nodiscard]] PrivateFutexStatus Acquire(const PrivateFutexKey &key, uint64_t &entry_index,
                                             WaitQueue *&wait_queue) noexcept;
    [[nodiscard]] PrivateFutexStatus Find(const PrivateFutexKey &key, uint64_t &entry_index,
                                          WaitQueue *&wait_queue) noexcept;
    [[nodiscard]] PrivateFutexStatus Read(uint64_t entry_index,
                                          PrivateFutexEntry &entry) const noexcept;
    [[nodiscard]] PrivateFutexStatus ReleaseIfEmpty(uint64_t entry_index, bool &released) noexcept;
    [[nodiscard]] PrivateFutexStatus RecordWaitPrepared() noexcept;
    [[nodiscard]] PrivateFutexStatus RecordWakeOperation() noexcept;
    [[nodiscard]] PrivateFutexStatus RecordCancellationOperation() noexcept;
    [[nodiscard]] PrivateFutexStatus Validate() const noexcept;
    [[nodiscard]] PrivateFutexStatistics Statistics() const noexcept;

  private:
    [[nodiscard]] bool KeyIsValid(const PrivateFutexKey &key) const noexcept;
    [[nodiscard]] bool KeysEqual(const PrivateFutexKey &left,
                                 const PrivateFutexKey &right) const noexcept;

    PrivateFutexEntry *entries_{};
    uint64_t capacity_{};
    uint64_t first_queue_identifier_{};
    PrivateFutexStatistics statistics_{};
    bool initialized_{};
};

}

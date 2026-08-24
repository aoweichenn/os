#pragma once

#include <os/kernel/sync/runtime_mutex.hpp>
#include <os/kernel/sync/spin_lock.hpp>

#include <stdint.h>

namespace os::kernel::fs {

inline constexpr uint64_t OS_KERNEL_INODE_IO_INVALID_SLOT_INDEX = UINT64_MAX;

struct InodeIoIdentity final {
    uint64_t superblock_identifier;
    uint64_t superblock_generation;
    uint64_t node_identifier;
    uint64_t node_generation;
};

struct InodeIoToken final {
    uint64_t slot_index;
    uint64_t generation;
};

struct InodeIoSlot final {
    InodeIoIdentity identity;
    uint64_t access_generation;
    uint64_t reference_count;
    uint64_t generation;
    RuntimeMutex mutex;
    bool cached;
};

struct InodeIoCoordinatorStatistics final {
    uint64_t capacity;
    uint64_t cached_slot_count;
    uint64_t referenced_slot_count;
    uint64_t active_reference_count;
    uint64_t peak_referenced_slot_count;
    uint64_t peak_active_reference_count;
    uint64_t acquisition_count;
    uint64_t release_count;
    uint64_t identity_reuse_count;
    uint64_t slot_replacement_count;
    uint64_t capacity_rejection_count;
    uint64_t access_generation_reset_count;
};

enum class InodeIoCoordinatorStatus : uint64_t {
    Succeeded,
    NotInitialized,
    AlreadyInitialized,
    InvalidStorage,
    InvalidCapacity,
    InvalidWaitQueueRange,
    InvalidIdentity,
    CapacityExhausted,
    InvalidToken,
    CounterOverflow,
    GenerationExhausted,
    Corrupt,
};

class InodeIoCoordinator final {
  public:
    [[nodiscard]] InodeIoCoordinatorStatus Initialize(InodeIoSlot *storage, uint64_t capacity,
                                                      uint64_t wait_queue_identifier_base) noexcept;
    [[nodiscard]] InodeIoCoordinatorStatus Acquire(const InodeIoIdentity &identity,
                                                   InodeIoToken &token) noexcept;
    [[nodiscard]] InodeIoCoordinatorStatus Release(InodeIoToken &token) noexcept;
    [[nodiscard]] InodeIoCoordinatorStatistics Statistics() const noexcept;
    [[nodiscard]] InodeIoCoordinatorStatus Validate() const noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;

  private:
    [[nodiscard]] uint64_t FindIdentity(const InodeIoIdentity &identity) const noexcept;
    [[nodiscard]] uint64_t FindReplacementCandidate() const noexcept;
    [[nodiscard]] bool TokenIsValid(const InodeIoToken &token) const noexcept;
    [[nodiscard]] uint64_t NextAccessGeneration() noexcept;

    InodeIoSlot *storage_{};
    uint64_t capacity_{};
    uint64_t access_generation_{};
    mutable SpinLock lock_{};
    InodeIoCoordinatorStatistics statistics_{};
    bool initialized_{};
};

[[nodiscard]] bool InodeIoIdentityIsValid(const InodeIoIdentity &identity) noexcept;
[[nodiscard]] bool InodeIoIdentitiesEqual(const InodeIoIdentity &left,
                                          const InodeIoIdentity &right) noexcept;

}

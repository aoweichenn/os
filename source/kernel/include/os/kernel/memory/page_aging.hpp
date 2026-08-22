#pragma once

#include <os/kernel/sync/spin_lock.hpp>

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_PAGE_AGING_CAPACITY_LIMIT = 32768ULL;
inline constexpr uint64_t OS_KERNEL_PAGE_AGING_HASH_CAPACITY_LIMIT = 65536ULL;
inline constexpr uint64_t OS_KERNEL_PAGE_AGING_INVALID_INDEX = UINT64_MAX;
inline constexpr uint64_t OS_KERNEL_PAGE_AGING_KIND_COUNT = 2ULL;
inline constexpr uint64_t OS_KERNEL_PAGE_AGING_STATE_COUNT = 2ULL;
inline constexpr uint64_t OS_KERNEL_PAGE_AGING_UNKNOWN_IDENTITY_GENERATION = 0ULL;

enum class PageAgingKind : uint64_t {
    None,
    File,
    Anonymous,
};

enum class PageAgingState : uint64_t {
    None,
    Active,
    Inactive,
};

struct PageAgingEntry final {
    uint64_t physical_address;
    PageAgingKind kind;
    PageAgingState state;
    uint64_t previous_queue_index;
    uint64_t next_queue_index;
    uint64_t next_free_index;
    uint64_t observed_round;
    uint64_t alias_observation_count;
    uint64_t identity_generation;
    bool referenced;
    bool reclaim_eligible;
    bool reclaim_candidate;
    bool tracked;
};

struct PageAgingEntrySnapshot final {
    uint64_t physical_address;
    PageAgingKind kind;
    PageAgingState state;
    uint64_t alias_observation_count;
    uint64_t identity_generation;
    bool referenced;
    bool reclaim_eligible;
    bool reclaim_candidate;
};

enum class PageAgingStatus : uint64_t {
    Succeeded,
    NotInitialized,
    AlreadyInitialized,
    NullEntryStorage,
    NullHashStorage,
    InvalidCapacity,
    InvalidHashCapacity,
    InvalidPhysicalAddress,
    InvalidKind,
    ObservationAlreadyActive,
    ObservationNotActive,
    EntryNotFound,
    CapacityExhausted,
    KindConflict,
    CounterOverflow,
    CorruptedState,
};

struct PageAgingStatistics final {
    uint64_t capacity;
    uint64_t hash_capacity;
    uint64_t tracked_page_count;
    uint64_t active_file_page_count;
    uint64_t inactive_file_page_count;
    uint64_t active_anonymous_page_count;
    uint64_t inactive_anonymous_page_count;
    uint64_t reclaim_candidate_count;
    uint64_t file_reclaim_candidate_count;
    uint64_t anonymous_reclaim_candidate_count;
    uint64_t peak_tracked_page_count;
    uint64_t peak_reclaim_candidate_count;
    uint64_t observation_round_count;
    uint64_t page_observation_count;
    uint64_t alias_observation_count;
    uint64_t referenced_observation_count;
    uint64_t unreferenced_observation_count;
    uint64_t promotion_count;
    uint64_t demotion_count;
    uint64_t active_retention_count;
    uint64_t inactive_retention_count;
    uint64_t reclaim_candidate_observation_count;
    uint64_t insertion_count;
    uint64_t removal_count;
    uint64_t unobserved_removal_count;
    uint64_t capacity_rejection_count;
    uint64_t reclassification_count;
    uint64_t generation_refresh_count;
    uint64_t kind_conflict_count;
    uint64_t last_kind_conflict_physical_address;
    PageAgingKind last_existing_kind;
    PageAgingKind last_observed_kind;
    uint64_t observation_cancellation_count;
    uint64_t forgotten_page_count;
    uint64_t reset_count;
    bool observation_active;
};

class PageAgingManager final {
  public:
    PageAgingManager() noexcept = default;
    PageAgingManager(const PageAgingManager &) = delete;
    PageAgingManager &operator=(const PageAgingManager &) = delete;

    [[nodiscard]] PageAgingStatus Initialize(PageAgingEntry *entry_storage, uint64_t entry_capacity,
                                             uint64_t *hash_storage,
                                             uint64_t hash_capacity) noexcept;
    [[nodiscard]] PageAgingStatus BeginObservation() noexcept;
    [[nodiscard]] PageAgingStatus Observe(
        uint64_t physical_address, PageAgingKind kind, bool accessed, bool reclaim_eligible,
        uint64_t identity_generation = OS_KERNEL_PAGE_AGING_UNKNOWN_IDENTITY_GENERATION) noexcept;
    [[nodiscard]] PageAgingStatus EndObservation() noexcept;
    [[nodiscard]] PageAgingStatus CancelObservation() noexcept;
    [[nodiscard]] PageAgingStatus Read(uint64_t physical_address, PageAgingKind kind,
                                       PageAgingEntrySnapshot &entry) const noexcept;
    [[nodiscard]] PageAgingStatus Forget(uint64_t physical_address, PageAgingKind kind) noexcept;
    [[nodiscard]] PageAgingStatus Reset() noexcept;
    [[nodiscard]] PageAgingStatistics Statistics() const noexcept;
    [[nodiscard]] PageAgingStatus Validate() const noexcept;

  private:
    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] static bool KindIsValid(PageAgingKind kind) noexcept;
    [[nodiscard]] static bool StateIsValid(PageAgingState state) noexcept;
    [[nodiscard]] static uint64_t KindIndex(PageAgingKind kind) noexcept;
    [[nodiscard]] static uint64_t StateIndex(PageAgingState state) noexcept;
    [[nodiscard]] uint64_t HashStart(uint64_t physical_address, PageAgingKind kind) const noexcept;
    [[nodiscard]] bool FindHashSlot(uint64_t physical_address, PageAgingKind kind,
                                    uint64_t &hash_slot, uint64_t &entry_index) const noexcept;
    [[nodiscard]] bool FindInsertionHashSlot(uint64_t physical_address, PageAgingKind kind,
                                             uint64_t &hash_slot) const noexcept;
    void AppendQueue(uint64_t entry_index) noexcept;
    void RemoveQueue(uint64_t entry_index) noexcept;
    void MoveQueue(uint64_t entry_index, PageAgingState new_state) noexcept;
    [[nodiscard]] PageAgingStatus Insert(uint64_t physical_address, PageAgingKind kind,
                                         uint64_t &entry_index) noexcept;
    void Remove(uint64_t entry_index, bool unobserved) noexcept;
    [[nodiscard]] PageAgingStatistics StatisticsLocked() const noexcept;

    mutable SpinLock lock_{};
    PageAgingEntry *entries_{};
    uint64_t *hash_{};
    uint64_t capacity_{};
    uint64_t hash_capacity_{};
    uint64_t free_head_index_{OS_KERNEL_PAGE_AGING_INVALID_INDEX};
    uint64_t queue_head_indices_[OS_KERNEL_PAGE_AGING_KIND_COUNT]
                                [OS_KERNEL_PAGE_AGING_STATE_COUNT]{};
    uint64_t queue_tail_indices_[OS_KERNEL_PAGE_AGING_KIND_COUNT]
                                [OS_KERNEL_PAGE_AGING_STATE_COUNT]{};
    uint64_t queue_counts_[OS_KERNEL_PAGE_AGING_KIND_COUNT][OS_KERNEL_PAGE_AGING_STATE_COUNT]{};
    uint64_t current_round_{};
    PageAgingStatistics cumulative_statistics_{};
    bool observation_active_{};
    bool initialized_{};
};

}

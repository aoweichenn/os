#include <os/kernel/memory/page_aging.hpp>

#include <os/kernel/memory/physical_frame_allocator.hpp>

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_PAGE_AGING_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_PAGE_AGING_SINGLE_UNIT = 1ULL;
constexpr uint64_t OS_KERNEL_PAGE_AGING_HASH_CAPACITY_RATIO = 2ULL;
constexpr uint64_t OS_KERNEL_PAGE_AGING_HASH_TOMBSTONE = UINT64_MAX - 1ULL;
constexpr uint64_t OS_KERNEL_PAGE_AGING_HASH_MULTIPLIER = 11400714819323198485ULL;
constexpr uint64_t OS_KERNEL_PAGE_AGING_PHYSICAL_ADDRESS_LIMIT = 0x0010000000000000ULL;

[[nodiscard]] bool IsPowerOfTwo(const uint64_t value) noexcept {
    return value != OS_KERNEL_PAGE_AGING_EMPTY_VALUE &&
           (value & (value - OS_KERNEL_PAGE_AGING_SINGLE_UNIT)) == OS_KERNEL_PAGE_AGING_EMPTY_VALUE;
}

[[nodiscard]] PageAgingEntry EmptyEntry(const uint64_t next_free_index) noexcept {
    return PageAgingEntry{
        .physical_address = OS_KERNEL_PAGE_AGING_EMPTY_VALUE,
        .kind = PageAgingKind::None,
        .state = PageAgingState::None,
        .previous_queue_index = OS_KERNEL_PAGE_AGING_INVALID_INDEX,
        .next_queue_index = OS_KERNEL_PAGE_AGING_INVALID_INDEX,
        .next_free_index = next_free_index,
        .observed_round = OS_KERNEL_PAGE_AGING_EMPTY_VALUE,
        .alias_observation_count = OS_KERNEL_PAGE_AGING_EMPTY_VALUE,
        .identity_generation = OS_KERNEL_PAGE_AGING_UNKNOWN_IDENTITY_GENERATION,
        .referenced = false,
        .reclaim_eligible = false,
        .reclaim_candidate = false,
        .tracked = false,
    };
}

}

PageAgingStatus PageAgingManager::Initialize(PageAgingEntry *const entry_storage,
                                             const uint64_t entry_capacity,
                                             uint64_t *const hash_storage,
                                             const uint64_t hash_capacity) noexcept {
    SpinLockGuard guard{this->lock_};
    if (this->initialized_) {
        return PageAgingStatus::AlreadyInitialized;
    }
    if (entry_storage == nullptr) {
        return PageAgingStatus::NullEntryStorage;
    }
    if (hash_storage == nullptr) {
        return PageAgingStatus::NullHashStorage;
    }
    if (entry_capacity == OS_KERNEL_PAGE_AGING_EMPTY_VALUE ||
        entry_capacity > OS_KERNEL_PAGE_AGING_CAPACITY_LIMIT) {
        return PageAgingStatus::InvalidCapacity;
    }
    if (!IsPowerOfTwo(hash_capacity) ||
        hash_capacity < entry_capacity * OS_KERNEL_PAGE_AGING_HASH_CAPACITY_RATIO ||
        hash_capacity > OS_KERNEL_PAGE_AGING_HASH_CAPACITY_LIMIT) {
        return PageAgingStatus::InvalidHashCapacity;
    }
    for (uint64_t entry_index = OS_KERNEL_PAGE_AGING_EMPTY_VALUE; entry_index < entry_capacity;
         ++entry_index) {
        const uint64_t next_free_index =
            entry_index + OS_KERNEL_PAGE_AGING_SINGLE_UNIT < entry_capacity
                ? entry_index + OS_KERNEL_PAGE_AGING_SINGLE_UNIT
                : OS_KERNEL_PAGE_AGING_INVALID_INDEX;
        entry_storage[entry_index] = EmptyEntry(next_free_index);
    }
    for (uint64_t hash_slot = OS_KERNEL_PAGE_AGING_EMPTY_VALUE; hash_slot < hash_capacity;
         ++hash_slot) {
        hash_storage[hash_slot] = OS_KERNEL_PAGE_AGING_INVALID_INDEX;
    }
    for (uint64_t kind_index = OS_KERNEL_PAGE_AGING_EMPTY_VALUE;
         kind_index < OS_KERNEL_PAGE_AGING_KIND_COUNT; ++kind_index) {
        for (uint64_t state_index = OS_KERNEL_PAGE_AGING_EMPTY_VALUE;
             state_index < OS_KERNEL_PAGE_AGING_STATE_COUNT; ++state_index) {
            this->queue_head_indices_[kind_index][state_index] = OS_KERNEL_PAGE_AGING_INVALID_INDEX;
            this->queue_tail_indices_[kind_index][state_index] = OS_KERNEL_PAGE_AGING_INVALID_INDEX;
            this->queue_counts_[kind_index][state_index] = OS_KERNEL_PAGE_AGING_EMPTY_VALUE;
        }
    }
    this->entries_ = entry_storage;
    this->hash_ = hash_storage;
    this->capacity_ = entry_capacity;
    this->hash_capacity_ = hash_capacity;
    this->free_head_index_ = OS_KERNEL_PAGE_AGING_EMPTY_VALUE;
    this->current_round_ = OS_KERNEL_PAGE_AGING_EMPTY_VALUE;
    this->cumulative_statistics_ = PageAgingStatistics{};
    this->cumulative_statistics_.capacity = entry_capacity;
    this->cumulative_statistics_.hash_capacity = hash_capacity;
    this->observation_active_ = false;
    this->initialized_ = true;
    return PageAgingStatus::Succeeded;
}

PageAgingStatus PageAgingManager::BeginObservation() noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->IsInitialized()) {
        return PageAgingStatus::NotInitialized;
    }
    if (this->observation_active_) {
        return PageAgingStatus::ObservationAlreadyActive;
    }
    if (this->current_round_ == UINT64_MAX ||
        this->cumulative_statistics_.observation_round_count == UINT64_MAX) {
        return PageAgingStatus::CounterOverflow;
    }
    ++this->current_round_;
    ++this->cumulative_statistics_.observation_round_count;
    this->observation_active_ = true;
    return PageAgingStatus::Succeeded;
}

PageAgingStatus PageAgingManager::Observe(const uint64_t physical_address, const PageAgingKind kind,
                                          const bool accessed, const bool reclaim_eligible,
                                          const uint64_t identity_generation) noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->IsInitialized()) {
        return PageAgingStatus::NotInitialized;
    }
    if (!this->observation_active_) {
        return PageAgingStatus::ObservationNotActive;
    }
    if ((physical_address &
         (OS_KERNEL_MEMORY_PAGE_SIZE_BYTES - OS_KERNEL_PAGE_AGING_SINGLE_UNIT)) !=
            OS_KERNEL_PAGE_AGING_EMPTY_VALUE ||
        physical_address >= OS_KERNEL_PAGE_AGING_PHYSICAL_ADDRESS_LIMIT) {
        return PageAgingStatus::InvalidPhysicalAddress;
    }
    if (!PageAgingManager::KindIsValid(kind)) {
        return PageAgingStatus::InvalidKind;
    }
    const PageAgingKind other_kind =
        kind == PageAgingKind::File ? PageAgingKind::Anonymous : PageAgingKind::File;
    uint64_t other_hash_slot = OS_KERNEL_PAGE_AGING_INVALID_INDEX;
    uint64_t other_entry_index = OS_KERNEL_PAGE_AGING_INVALID_INDEX;
    if (this->FindHashSlot(physical_address, other_kind, other_hash_slot, other_entry_index)) {
        if (this->entries_[other_entry_index].observed_round != this->current_round_) {
            if (this->cumulative_statistics_.reclassification_count == UINT64_MAX) {
                return PageAgingStatus::CounterOverflow;
            }
            this->Remove(other_entry_index, false);
            ++this->cumulative_statistics_.reclassification_count;
        } else {
            if (this->cumulative_statistics_.kind_conflict_count != UINT64_MAX) {
                ++this->cumulative_statistics_.kind_conflict_count;
            }
            this->cumulative_statistics_.last_kind_conflict_physical_address = physical_address;
            this->cumulative_statistics_.last_existing_kind = other_kind;
            this->cumulative_statistics_.last_observed_kind = kind;
            return PageAgingStatus::KindConflict;
        }
    }
    uint64_t hash_slot = OS_KERNEL_PAGE_AGING_INVALID_INDEX;
    uint64_t entry_index = OS_KERNEL_PAGE_AGING_INVALID_INDEX;
    if (!this->FindHashSlot(physical_address, kind, hash_slot, entry_index)) {
        const PageAgingStatus insert_status = this->Insert(physical_address, kind, entry_index);
        if (insert_status != PageAgingStatus::Succeeded) {
            return insert_status;
        }
    }
    PageAgingEntry &entry = this->entries_[entry_index];
    if (!entry.tracked || entry.physical_address != physical_address || entry.kind != kind) {
        return PageAgingStatus::CorruptedState;
    }
    if (entry.observed_round != this->current_round_) {
        if (identity_generation != OS_KERNEL_PAGE_AGING_UNKNOWN_IDENTITY_GENERATION &&
            entry.identity_generation != OS_KERNEL_PAGE_AGING_UNKNOWN_IDENTITY_GENERATION &&
            entry.identity_generation != identity_generation) {
            if (this->cumulative_statistics_.generation_refresh_count == UINT64_MAX) {
                return PageAgingStatus::CounterOverflow;
            }
            if (entry.state != PageAgingState::Active) {
                this->MoveQueue(entry_index, PageAgingState::Active);
            }
            ++this->cumulative_statistics_.generation_refresh_count;
        }
        if (identity_generation != OS_KERNEL_PAGE_AGING_UNKNOWN_IDENTITY_GENERATION) {
            entry.identity_generation = identity_generation;
        }
        entry.observed_round = this->current_round_;
        entry.alias_observation_count = OS_KERNEL_PAGE_AGING_SINGLE_UNIT;
        entry.referenced = accessed;
        entry.reclaim_eligible = reclaim_eligible;
        if (this->cumulative_statistics_.page_observation_count == UINT64_MAX ||
            this->cumulative_statistics_.alias_observation_count == UINT64_MAX) {
            return PageAgingStatus::CounterOverflow;
        }
        ++this->cumulative_statistics_.page_observation_count;
        ++this->cumulative_statistics_.alias_observation_count;
        return PageAgingStatus::Succeeded;
    }
    if (entry.alias_observation_count == UINT64_MAX ||
        this->cumulative_statistics_.alias_observation_count == UINT64_MAX) {
        return PageAgingStatus::CounterOverflow;
    }
    ++entry.alias_observation_count;
    ++this->cumulative_statistics_.alias_observation_count;
    entry.referenced = entry.referenced || accessed;
    entry.reclaim_eligible = entry.reclaim_eligible && reclaim_eligible;
    return PageAgingStatus::Succeeded;
}

PageAgingStatus PageAgingManager::EndObservation() noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->IsInitialized()) {
        return PageAgingStatus::NotInitialized;
    }
    if (!this->observation_active_) {
        return PageAgingStatus::ObservationNotActive;
    }
    uint64_t reclaim_candidate_count = OS_KERNEL_PAGE_AGING_EMPTY_VALUE;
    uint64_t file_reclaim_candidate_count = OS_KERNEL_PAGE_AGING_EMPTY_VALUE;
    uint64_t anonymous_reclaim_candidate_count = OS_KERNEL_PAGE_AGING_EMPTY_VALUE;
    for (uint64_t entry_index = OS_KERNEL_PAGE_AGING_EMPTY_VALUE; entry_index < this->capacity_;
         ++entry_index) {
        PageAgingEntry &entry = this->entries_[entry_index];
        if (!entry.tracked) {
            continue;
        }
        if (entry.observed_round != this->current_round_) {
            this->Remove(entry_index, true);
            continue;
        }
        uint64_t *transition_counter = nullptr;
        if (entry.referenced) {
            entry.reclaim_candidate = false;
            if (this->cumulative_statistics_.referenced_observation_count == UINT64_MAX) {
                return PageAgingStatus::CounterOverflow;
            }
            ++this->cumulative_statistics_.referenced_observation_count;
            if (entry.state == PageAgingState::Inactive) {
                this->MoveQueue(entry_index, PageAgingState::Active);
                transition_counter = &this->cumulative_statistics_.promotion_count;
            } else if (entry.state == PageAgingState::Active) {
                transition_counter = &this->cumulative_statistics_.active_retention_count;
            } else {
                return PageAgingStatus::CorruptedState;
            }
        } else {
            if (this->cumulative_statistics_.unreferenced_observation_count == UINT64_MAX) {
                return PageAgingStatus::CounterOverflow;
            }
            ++this->cumulative_statistics_.unreferenced_observation_count;
            if (entry.state == PageAgingState::Active) {
                this->MoveQueue(entry_index, PageAgingState::Inactive);
                entry.reclaim_candidate = false;
                transition_counter = &this->cumulative_statistics_.demotion_count;
            } else if (entry.state == PageAgingState::Inactive) {
                transition_counter = &this->cumulative_statistics_.inactive_retention_count;
                entry.reclaim_candidate = entry.reclaim_eligible;
                if (entry.reclaim_eligible) {
                    if (reclaim_candidate_count == UINT64_MAX ||
                        this->cumulative_statistics_.reclaim_candidate_observation_count ==
                            UINT64_MAX) {
                        return PageAgingStatus::CounterOverflow;
                    }
                    ++reclaim_candidate_count;
                    uint64_t &kind_candidate_count = entry.kind == PageAgingKind::File
                                                         ? file_reclaim_candidate_count
                                                         : anonymous_reclaim_candidate_count;
                    if (kind_candidate_count == UINT64_MAX) {
                        return PageAgingStatus::CounterOverflow;
                    }
                    ++kind_candidate_count;
                    ++this->cumulative_statistics_.reclaim_candidate_observation_count;
                }
            } else {
                return PageAgingStatus::CorruptedState;
            }
        }
        if (transition_counter == nullptr || *transition_counter == UINT64_MAX) {
            return PageAgingStatus::CounterOverflow;
        }
        ++(*transition_counter);
    }
    this->observation_active_ = false;
    const PageAgingStatistics statistics = this->StatisticsLocked();
    if (statistics.tracked_page_count > this->cumulative_statistics_.peak_tracked_page_count) {
        this->cumulative_statistics_.peak_tracked_page_count = statistics.tracked_page_count;
    }
    this->cumulative_statistics_.reclaim_candidate_count = reclaim_candidate_count;
    this->cumulative_statistics_.file_reclaim_candidate_count = file_reclaim_candidate_count;
    this->cumulative_statistics_.anonymous_reclaim_candidate_count =
        anonymous_reclaim_candidate_count;
    if (reclaim_candidate_count > this->cumulative_statistics_.peak_reclaim_candidate_count) {
        this->cumulative_statistics_.peak_reclaim_candidate_count = reclaim_candidate_count;
    }
    return PageAgingStatus::Succeeded;
}

PageAgingStatus PageAgingManager::CancelObservation() noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->IsInitialized()) {
        return PageAgingStatus::NotInitialized;
    }
    if (!this->observation_active_) {
        return PageAgingStatus::ObservationNotActive;
    }
    if (this->cumulative_statistics_.observation_cancellation_count == UINT64_MAX) {
        return PageAgingStatus::CounterOverflow;
    }
    this->observation_active_ = false;
    ++this->cumulative_statistics_.observation_cancellation_count;
    return PageAgingStatus::Succeeded;
}

PageAgingStatus PageAgingManager::Read(const uint64_t physical_address, const PageAgingKind kind,
                                       PageAgingEntrySnapshot &entry) const noexcept {
    entry = PageAgingEntrySnapshot{};
    SpinLockGuard guard{this->lock_};
    if (!this->IsInitialized()) {
        return PageAgingStatus::NotInitialized;
    }
    uint64_t hash_slot = OS_KERNEL_PAGE_AGING_INVALID_INDEX;
    uint64_t entry_index = OS_KERNEL_PAGE_AGING_INVALID_INDEX;
    if (!this->FindHashSlot(physical_address, kind, hash_slot, entry_index)) {
        return PageAgingStatus::EntryNotFound;
    }
    const PageAgingEntry &stored = this->entries_[entry_index];
    entry = PageAgingEntrySnapshot{
        .physical_address = stored.physical_address,
        .kind = stored.kind,
        .state = stored.state,
        .alias_observation_count = stored.alias_observation_count,
        .identity_generation = stored.identity_generation,
        .referenced = stored.referenced,
        .reclaim_eligible = stored.reclaim_eligible,
        .reclaim_candidate = stored.reclaim_candidate,
    };
    return PageAgingStatus::Succeeded;
}

PageAgingStatus PageAgingManager::Forget(const uint64_t physical_address,
                                         const PageAgingKind kind) noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->IsInitialized()) {
        return PageAgingStatus::NotInitialized;
    }
    if (this->observation_active_) {
        return PageAgingStatus::ObservationAlreadyActive;
    }
    if ((physical_address &
         (OS_KERNEL_MEMORY_PAGE_SIZE_BYTES - OS_KERNEL_PAGE_AGING_SINGLE_UNIT)) !=
            OS_KERNEL_PAGE_AGING_EMPTY_VALUE ||
        physical_address >= OS_KERNEL_PAGE_AGING_PHYSICAL_ADDRESS_LIMIT) {
        return PageAgingStatus::InvalidPhysicalAddress;
    }
    if (!PageAgingManager::KindIsValid(kind)) {
        return PageAgingStatus::InvalidKind;
    }
    uint64_t hash_slot = OS_KERNEL_PAGE_AGING_INVALID_INDEX;
    uint64_t entry_index = OS_KERNEL_PAGE_AGING_INVALID_INDEX;
    if (!this->FindHashSlot(physical_address, kind, hash_slot, entry_index)) {
        return PageAgingStatus::EntryNotFound;
    }
    if (this->cumulative_statistics_.removal_count == UINT64_MAX ||
        this->cumulative_statistics_.forgotten_page_count == UINT64_MAX) {
        return PageAgingStatus::CounterOverflow;
    }
    const PageAgingEntry &entry = this->entries_[entry_index];
    const bool reclaim_candidate = entry.reclaim_candidate;
    if (reclaim_candidate) {
        uint64_t &kind_candidate_count =
            kind == PageAgingKind::File
                ? this->cumulative_statistics_.file_reclaim_candidate_count
                : this->cumulative_statistics_.anonymous_reclaim_candidate_count;
        if (this->cumulative_statistics_.reclaim_candidate_count ==
                OS_KERNEL_PAGE_AGING_EMPTY_VALUE ||
            kind_candidate_count == OS_KERNEL_PAGE_AGING_EMPTY_VALUE) {
            return PageAgingStatus::CorruptedState;
        }
        --this->cumulative_statistics_.reclaim_candidate_count;
        --kind_candidate_count;
    }
    this->Remove(entry_index, false);
    ++this->cumulative_statistics_.forgotten_page_count;
    return PageAgingStatus::Succeeded;
}

PageAgingStatus PageAgingManager::Reset() noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->IsInitialized()) {
        return PageAgingStatus::NotInitialized;
    }
    if (this->observation_active_) {
        return PageAgingStatus::ObservationAlreadyActive;
    }
    if (this->cumulative_statistics_.reset_count == UINT64_MAX) {
        return PageAgingStatus::CounterOverflow;
    }
    for (uint64_t entry_index = OS_KERNEL_PAGE_AGING_EMPTY_VALUE; entry_index < this->capacity_;
         ++entry_index) {
        if (this->entries_[entry_index].tracked) {
            this->Remove(entry_index, false);
        }
    }
    this->cumulative_statistics_.reclaim_candidate_count = OS_KERNEL_PAGE_AGING_EMPTY_VALUE;
    this->cumulative_statistics_.file_reclaim_candidate_count = OS_KERNEL_PAGE_AGING_EMPTY_VALUE;
    this->cumulative_statistics_.anonymous_reclaim_candidate_count =
        OS_KERNEL_PAGE_AGING_EMPTY_VALUE;
    ++this->cumulative_statistics_.reset_count;
    return PageAgingStatus::Succeeded;
}

PageAgingStatistics PageAgingManager::Statistics() const noexcept {
    SpinLockGuard guard{this->lock_};
    return this->StatisticsLocked();
}

PageAgingStatus PageAgingManager::Validate() const noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->IsInitialized()) {
        return PageAgingStatus::NotInitialized;
    }
    uint64_t listed_count = OS_KERNEL_PAGE_AGING_EMPTY_VALUE;
    for (uint64_t kind_index = OS_KERNEL_PAGE_AGING_EMPTY_VALUE;
         kind_index < OS_KERNEL_PAGE_AGING_KIND_COUNT; ++kind_index) {
        for (uint64_t state_index = OS_KERNEL_PAGE_AGING_EMPTY_VALUE;
             state_index < OS_KERNEL_PAGE_AGING_STATE_COUNT; ++state_index) {
            uint64_t previous_index = OS_KERNEL_PAGE_AGING_INVALID_INDEX;
            uint64_t entry_index = this->queue_head_indices_[kind_index][state_index];
            uint64_t queue_count = OS_KERNEL_PAGE_AGING_EMPTY_VALUE;
            while (entry_index != OS_KERNEL_PAGE_AGING_INVALID_INDEX) {
                if (entry_index >= this->capacity_ || queue_count >= this->capacity_) {
                    return PageAgingStatus::CorruptedState;
                }
                const PageAgingEntry &entry = this->entries_[entry_index];
                if (!entry.tracked || entry.previous_queue_index != previous_index ||
                    PageAgingManager::KindIndex(entry.kind) != kind_index ||
                    PageAgingManager::StateIndex(entry.state) != state_index) {
                    return PageAgingStatus::CorruptedState;
                }
                ++queue_count;
                ++listed_count;
                previous_index = entry_index;
                entry_index = entry.next_queue_index;
            }
            if (queue_count != this->queue_counts_[kind_index][state_index] ||
                (queue_count == OS_KERNEL_PAGE_AGING_EMPTY_VALUE &&
                 this->queue_tail_indices_[kind_index][state_index] !=
                     OS_KERNEL_PAGE_AGING_INVALID_INDEX) ||
                (queue_count != OS_KERNEL_PAGE_AGING_EMPTY_VALUE &&
                 previous_index != this->queue_tail_indices_[kind_index][state_index])) {
                return PageAgingStatus::CorruptedState;
            }
        }
    }
    uint64_t free_count = OS_KERNEL_PAGE_AGING_EMPTY_VALUE;
    uint64_t free_index = this->free_head_index_;
    while (free_index != OS_KERNEL_PAGE_AGING_INVALID_INDEX) {
        if (free_index >= this->capacity_ || this->entries_[free_index].tracked ||
            free_count >= this->capacity_) {
            return PageAgingStatus::CorruptedState;
        }
        ++free_count;
        free_index = this->entries_[free_index].next_free_index;
    }
    uint64_t hash_entry_count = OS_KERNEL_PAGE_AGING_EMPTY_VALUE;
    for (uint64_t hash_slot = OS_KERNEL_PAGE_AGING_EMPTY_VALUE; hash_slot < this->hash_capacity_;
         ++hash_slot) {
        const uint64_t entry_index = this->hash_[hash_slot];
        if (entry_index == OS_KERNEL_PAGE_AGING_INVALID_INDEX ||
            entry_index == OS_KERNEL_PAGE_AGING_HASH_TOMBSTONE) {
            continue;
        }
        if (entry_index >= this->capacity_ || !this->entries_[entry_index].tracked) {
            return PageAgingStatus::CorruptedState;
        }
        ++hash_entry_count;
        uint64_t found_hash_slot = OS_KERNEL_PAGE_AGING_INVALID_INDEX;
        uint64_t found_entry_index = OS_KERNEL_PAGE_AGING_INVALID_INDEX;
        if (!this->FindHashSlot(this->entries_[entry_index].physical_address,
                                this->entries_[entry_index].kind, found_hash_slot,
                                found_entry_index) ||
            found_hash_slot != hash_slot || found_entry_index != entry_index) {
            return PageAgingStatus::CorruptedState;
        }
    }
    uint64_t tracked_entry_count = OS_KERNEL_PAGE_AGING_EMPTY_VALUE;
    uint64_t unused_entry_count = OS_KERNEL_PAGE_AGING_EMPTY_VALUE;
    uint64_t file_candidate_count = OS_KERNEL_PAGE_AGING_EMPTY_VALUE;
    uint64_t anonymous_candidate_count = OS_KERNEL_PAGE_AGING_EMPTY_VALUE;
    for (uint64_t entry_index = OS_KERNEL_PAGE_AGING_EMPTY_VALUE; entry_index < this->capacity_;
         ++entry_index) {
        const PageAgingEntry &entry = this->entries_[entry_index];
        if (entry.tracked) {
            if (!PageAgingManager::KindIsValid(entry.kind) ||
                !PageAgingManager::StateIsValid(entry.state)) {
                return PageAgingStatus::CorruptedState;
            }
            const uint64_t kind_index = PageAgingManager::KindIndex(entry.kind);
            const uint64_t state_index = PageAgingManager::StateIndex(entry.state);
            if ((entry.previous_queue_index == OS_KERNEL_PAGE_AGING_INVALID_INDEX &&
                 this->queue_head_indices_[kind_index][state_index] != entry_index) ||
                (entry.next_queue_index == OS_KERNEL_PAGE_AGING_INVALID_INDEX &&
                 this->queue_tail_indices_[kind_index][state_index] != entry_index) ||
                (entry.previous_queue_index != OS_KERNEL_PAGE_AGING_INVALID_INDEX &&
                 (entry.previous_queue_index >= this->capacity_ ||
                  this->entries_[entry.previous_queue_index].next_queue_index != entry_index)) ||
                (entry.next_queue_index != OS_KERNEL_PAGE_AGING_INVALID_INDEX &&
                 (entry.next_queue_index >= this->capacity_ ||
                  this->entries_[entry.next_queue_index].previous_queue_index != entry_index))) {
                return PageAgingStatus::CorruptedState;
            }
            if (!this->observation_active_ && entry.reclaim_candidate) {
                if (entry.state != PageAgingState::Inactive || !entry.reclaim_eligible) {
                    return PageAgingStatus::CorruptedState;
                }
                if (entry.kind == PageAgingKind::File) {
                    ++file_candidate_count;
                } else {
                    ++anonymous_candidate_count;
                }
            }
            ++tracked_entry_count;
        } else {
            ++unused_entry_count;
        }
    }
    const bool candidate_counts_valid =
        this->observation_active_ ||
        (file_candidate_count == this->cumulative_statistics_.file_reclaim_candidate_count &&
         anonymous_candidate_count ==
             this->cumulative_statistics_.anonymous_reclaim_candidate_count &&
         file_candidate_count + anonymous_candidate_count ==
             this->cumulative_statistics_.reclaim_candidate_count);
    return listed_count == tracked_entry_count && listed_count == hash_entry_count &&
                   free_count == unused_entry_count &&
                   listed_count + free_count == this->capacity_ && candidate_counts_valid
               ? PageAgingStatus::Succeeded
               : PageAgingStatus::CorruptedState;
}

bool PageAgingManager::IsInitialized() const noexcept {
    return this->initialized_ && this->entries_ != nullptr && this->hash_ != nullptr &&
           this->capacity_ != OS_KERNEL_PAGE_AGING_EMPTY_VALUE &&
           this->hash_capacity_ != OS_KERNEL_PAGE_AGING_EMPTY_VALUE;
}

bool PageAgingManager::KindIsValid(const PageAgingKind kind) noexcept {
    return kind == PageAgingKind::File || kind == PageAgingKind::Anonymous;
}

bool PageAgingManager::StateIsValid(const PageAgingState state) noexcept {
    return state == PageAgingState::Active || state == PageAgingState::Inactive;
}

uint64_t PageAgingManager::KindIndex(const PageAgingKind kind) noexcept {
    return kind == PageAgingKind::File ? OS_KERNEL_PAGE_AGING_EMPTY_VALUE
                                       : OS_KERNEL_PAGE_AGING_SINGLE_UNIT;
}

uint64_t PageAgingManager::StateIndex(const PageAgingState state) noexcept {
    return state == PageAgingState::Active ? OS_KERNEL_PAGE_AGING_EMPTY_VALUE
                                           : OS_KERNEL_PAGE_AGING_SINGLE_UNIT;
}

uint64_t PageAgingManager::HashStart(const uint64_t physical_address,
                                     const PageAgingKind kind) const noexcept {
    const uint64_t page_index = physical_address / OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
    const uint64_t mixed =
        (page_index ^ static_cast<uint64_t>(kind)) * OS_KERNEL_PAGE_AGING_HASH_MULTIPLIER;
    return mixed & (this->hash_capacity_ - OS_KERNEL_PAGE_AGING_SINGLE_UNIT);
}

bool PageAgingManager::FindHashSlot(const uint64_t physical_address, const PageAgingKind kind,
                                    uint64_t &hash_slot, uint64_t &entry_index) const noexcept {
    hash_slot = OS_KERNEL_PAGE_AGING_INVALID_INDEX;
    entry_index = OS_KERNEL_PAGE_AGING_INVALID_INDEX;
    const uint64_t first_hash_slot = this->HashStart(physical_address, kind);
    for (uint64_t probe_count = OS_KERNEL_PAGE_AGING_EMPTY_VALUE;
         probe_count < this->hash_capacity_; ++probe_count) {
        const uint64_t candidate_hash_slot =
            (first_hash_slot + probe_count) &
            (this->hash_capacity_ - OS_KERNEL_PAGE_AGING_SINGLE_UNIT);
        const uint64_t candidate_entry_index = this->hash_[candidate_hash_slot];
        if (candidate_entry_index == OS_KERNEL_PAGE_AGING_INVALID_INDEX) {
            return false;
        }
        if (candidate_entry_index == OS_KERNEL_PAGE_AGING_HASH_TOMBSTONE) {
            continue;
        }
        if (candidate_entry_index >= this->capacity_) {
            return false;
        }
        const PageAgingEntry &candidate = this->entries_[candidate_entry_index];
        if (candidate.tracked && candidate.physical_address == physical_address &&
            candidate.kind == kind) {
            hash_slot = candidate_hash_slot;
            entry_index = candidate_entry_index;
            return true;
        }
    }
    return false;
}

bool PageAgingManager::FindInsertionHashSlot(const uint64_t physical_address,
                                             const PageAgingKind kind,
                                             uint64_t &hash_slot) const noexcept {
    hash_slot = OS_KERNEL_PAGE_AGING_INVALID_INDEX;
    uint64_t first_tombstone = OS_KERNEL_PAGE_AGING_INVALID_INDEX;
    const uint64_t first_hash_slot = this->HashStart(physical_address, kind);
    for (uint64_t probe_count = OS_KERNEL_PAGE_AGING_EMPTY_VALUE;
         probe_count < this->hash_capacity_; ++probe_count) {
        const uint64_t candidate_hash_slot =
            (first_hash_slot + probe_count) &
            (this->hash_capacity_ - OS_KERNEL_PAGE_AGING_SINGLE_UNIT);
        if (this->hash_[candidate_hash_slot] == OS_KERNEL_PAGE_AGING_HASH_TOMBSTONE &&
            first_tombstone == OS_KERNEL_PAGE_AGING_INVALID_INDEX) {
            first_tombstone = candidate_hash_slot;
            continue;
        }
        if (this->hash_[candidate_hash_slot] == OS_KERNEL_PAGE_AGING_INVALID_INDEX) {
            hash_slot = first_tombstone == OS_KERNEL_PAGE_AGING_INVALID_INDEX ? candidate_hash_slot
                                                                              : first_tombstone;
            return true;
        }
    }
    if (first_tombstone != OS_KERNEL_PAGE_AGING_INVALID_INDEX) {
        hash_slot = first_tombstone;
        return true;
    }
    return false;
}

void PageAgingManager::AppendQueue(const uint64_t entry_index) noexcept {
    PageAgingEntry &entry = this->entries_[entry_index];
    const uint64_t kind_index = PageAgingManager::KindIndex(entry.kind);
    const uint64_t state_index = PageAgingManager::StateIndex(entry.state);
    entry.previous_queue_index = this->queue_tail_indices_[kind_index][state_index];
    entry.next_queue_index = OS_KERNEL_PAGE_AGING_INVALID_INDEX;
    if (entry.previous_queue_index == OS_KERNEL_PAGE_AGING_INVALID_INDEX) {
        this->queue_head_indices_[kind_index][state_index] = entry_index;
    } else {
        this->entries_[entry.previous_queue_index].next_queue_index = entry_index;
    }
    this->queue_tail_indices_[kind_index][state_index] = entry_index;
    ++this->queue_counts_[kind_index][state_index];
}

void PageAgingManager::RemoveQueue(const uint64_t entry_index) noexcept {
    PageAgingEntry &entry = this->entries_[entry_index];
    const uint64_t kind_index = PageAgingManager::KindIndex(entry.kind);
    const uint64_t state_index = PageAgingManager::StateIndex(entry.state);
    if (entry.previous_queue_index == OS_KERNEL_PAGE_AGING_INVALID_INDEX) {
        this->queue_head_indices_[kind_index][state_index] = entry.next_queue_index;
    } else {
        this->entries_[entry.previous_queue_index].next_queue_index = entry.next_queue_index;
    }
    if (entry.next_queue_index == OS_KERNEL_PAGE_AGING_INVALID_INDEX) {
        this->queue_tail_indices_[kind_index][state_index] = entry.previous_queue_index;
    } else {
        this->entries_[entry.next_queue_index].previous_queue_index = entry.previous_queue_index;
    }
    --this->queue_counts_[kind_index][state_index];
    entry.previous_queue_index = OS_KERNEL_PAGE_AGING_INVALID_INDEX;
    entry.next_queue_index = OS_KERNEL_PAGE_AGING_INVALID_INDEX;
}

void PageAgingManager::MoveQueue(const uint64_t entry_index,
                                 const PageAgingState new_state) noexcept {
    this->RemoveQueue(entry_index);
    this->entries_[entry_index].state = new_state;
    this->AppendQueue(entry_index);
}

PageAgingStatus PageAgingManager::Insert(const uint64_t physical_address, const PageAgingKind kind,
                                         uint64_t &entry_index) noexcept {
    entry_index = OS_KERNEL_PAGE_AGING_INVALID_INDEX;
    if (this->free_head_index_ == OS_KERNEL_PAGE_AGING_INVALID_INDEX) {
        if (this->cumulative_statistics_.capacity_rejection_count != UINT64_MAX) {
            ++this->cumulative_statistics_.capacity_rejection_count;
        }
        return PageAgingStatus::CapacityExhausted;
    }
    if (this->cumulative_statistics_.insertion_count == UINT64_MAX) {
        return PageAgingStatus::CounterOverflow;
    }
    uint64_t hash_slot = OS_KERNEL_PAGE_AGING_INVALID_INDEX;
    if (!this->FindInsertionHashSlot(physical_address, kind, hash_slot)) {
        return PageAgingStatus::CorruptedState;
    }
    entry_index = this->free_head_index_;
    this->free_head_index_ = this->entries_[entry_index].next_free_index;
    this->entries_[entry_index] = PageAgingEntry{
        .physical_address = physical_address,
        .kind = kind,
        .state = PageAgingState::Active,
        .previous_queue_index = OS_KERNEL_PAGE_AGING_INVALID_INDEX,
        .next_queue_index = OS_KERNEL_PAGE_AGING_INVALID_INDEX,
        .next_free_index = OS_KERNEL_PAGE_AGING_INVALID_INDEX,
        .observed_round = OS_KERNEL_PAGE_AGING_EMPTY_VALUE,
        .alias_observation_count = OS_KERNEL_PAGE_AGING_EMPTY_VALUE,
        .identity_generation = OS_KERNEL_PAGE_AGING_UNKNOWN_IDENTITY_GENERATION,
        .referenced = false,
        .reclaim_eligible = false,
        .reclaim_candidate = false,
        .tracked = true,
    };
    this->hash_[hash_slot] = entry_index;
    this->AppendQueue(entry_index);
    ++this->cumulative_statistics_.insertion_count;
    return PageAgingStatus::Succeeded;
}

void PageAgingManager::Remove(const uint64_t entry_index, const bool unobserved) noexcept {
    PageAgingEntry &entry = this->entries_[entry_index];
    uint64_t hash_slot = OS_KERNEL_PAGE_AGING_INVALID_INDEX;
    uint64_t found_entry_index = OS_KERNEL_PAGE_AGING_INVALID_INDEX;
    if (!this->FindHashSlot(entry.physical_address, entry.kind, hash_slot, found_entry_index) ||
        found_entry_index != entry_index) {
        return;
    }
    this->RemoveQueue(entry_index);
    this->hash_[hash_slot] = OS_KERNEL_PAGE_AGING_HASH_TOMBSTONE;
    const uint64_t previous_free_head = this->free_head_index_;
    entry = EmptyEntry(previous_free_head);
    this->free_head_index_ = entry_index;
    ++this->cumulative_statistics_.removal_count;
    if (unobserved) {
        ++this->cumulative_statistics_.unobserved_removal_count;
    }
}

PageAgingStatistics PageAgingManager::StatisticsLocked() const noexcept {
    PageAgingStatistics statistics = this->cumulative_statistics_;
    statistics.active_file_page_count =
        this->queue_counts_[OS_KERNEL_PAGE_AGING_EMPTY_VALUE][OS_KERNEL_PAGE_AGING_EMPTY_VALUE];
    statistics.inactive_file_page_count =
        this->queue_counts_[OS_KERNEL_PAGE_AGING_EMPTY_VALUE][OS_KERNEL_PAGE_AGING_SINGLE_UNIT];
    statistics.active_anonymous_page_count =
        this->queue_counts_[OS_KERNEL_PAGE_AGING_SINGLE_UNIT][OS_KERNEL_PAGE_AGING_EMPTY_VALUE];
    statistics.inactive_anonymous_page_count =
        this->queue_counts_[OS_KERNEL_PAGE_AGING_SINGLE_UNIT][OS_KERNEL_PAGE_AGING_SINGLE_UNIT];
    statistics.tracked_page_count =
        statistics.active_file_page_count + statistics.inactive_file_page_count +
        statistics.active_anonymous_page_count + statistics.inactive_anonymous_page_count;
    statistics.observation_active = this->observation_active_;
    return statistics;
}

}

#include <os/kernel/fs/inode_io_coordinator.hpp>

namespace os::kernel::fs {

namespace {

constexpr uint64_t OS_KERNEL_INODE_IO_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_INODE_IO_COUNTER_INCREMENT = 1ULL;

void IncreaseSaturating(uint64_t &value) noexcept {
    if (value != UINT64_MAX) {
        ++value;
    }
}

[[nodiscard]] bool IdentityIsEmpty(const InodeIoIdentity &identity) noexcept {
    return identity.superblock_identifier == OS_KERNEL_INODE_IO_EMPTY_VALUE &&
           identity.superblock_generation == OS_KERNEL_INODE_IO_EMPTY_VALUE &&
           identity.node_identifier == OS_KERNEL_INODE_IO_EMPTY_VALUE &&
           identity.node_generation == OS_KERNEL_INODE_IO_EMPTY_VALUE;
}

}

InodeIoCoordinatorStatus
InodeIoCoordinator::Initialize(InodeIoSlot *const storage, const uint64_t capacity,
                               const uint64_t wait_queue_identifier_base) noexcept {
    if (this->initialized_) {
        return InodeIoCoordinatorStatus::AlreadyInitialized;
    }
    if (storage == nullptr) {
        return InodeIoCoordinatorStatus::InvalidStorage;
    }
    if (capacity == OS_KERNEL_INODE_IO_EMPTY_VALUE) {
        return InodeIoCoordinatorStatus::InvalidCapacity;
    }
    if (wait_queue_identifier_base == OS_KERNEL_INODE_IO_EMPTY_VALUE ||
        capacity - OS_KERNEL_INODE_IO_COUNTER_INCREMENT > UINT64_MAX - wait_queue_identifier_base) {
        return InodeIoCoordinatorStatus::InvalidWaitQueueRange;
    }
    for (uint64_t slot_index = OS_KERNEL_INODE_IO_EMPTY_VALUE; slot_index < capacity;
         ++slot_index) {
        InodeIoSlot &slot = storage[slot_index];
        if (!IdentityIsEmpty(slot.identity) ||
            slot.access_generation != OS_KERNEL_INODE_IO_EMPTY_VALUE ||
            slot.reference_count != OS_KERNEL_INODE_IO_EMPTY_VALUE ||
            slot.generation != OS_KERNEL_INODE_IO_EMPTY_VALUE || slot.mutex.IsInitialized() ||
            slot.cached) {
            return InodeIoCoordinatorStatus::InvalidStorage;
        }
    }
    for (uint64_t slot_index = OS_KERNEL_INODE_IO_EMPTY_VALUE; slot_index < capacity;
         ++slot_index) {
        if (storage[slot_index].mutex.Initialize(WaitQueueId{
                .value = wait_queue_identifier_base + slot_index,
            }) != RuntimeMutexStatus::Succeeded) {
            return InodeIoCoordinatorStatus::InvalidStorage;
        }
    }
    this->storage_ = storage;
    this->capacity_ = capacity;
    this->access_generation_ = OS_KERNEL_INODE_IO_EMPTY_VALUE;
    this->lock_ = SpinLock{};
    this->statistics_ = InodeIoCoordinatorStatistics{
        .capacity = capacity,
        .cached_slot_count = OS_KERNEL_INODE_IO_EMPTY_VALUE,
        .referenced_slot_count = OS_KERNEL_INODE_IO_EMPTY_VALUE,
        .active_reference_count = OS_KERNEL_INODE_IO_EMPTY_VALUE,
        .peak_referenced_slot_count = OS_KERNEL_INODE_IO_EMPTY_VALUE,
        .peak_active_reference_count = OS_KERNEL_INODE_IO_EMPTY_VALUE,
        .acquisition_count = OS_KERNEL_INODE_IO_EMPTY_VALUE,
        .release_count = OS_KERNEL_INODE_IO_EMPTY_VALUE,
        .identity_reuse_count = OS_KERNEL_INODE_IO_EMPTY_VALUE,
        .slot_replacement_count = OS_KERNEL_INODE_IO_EMPTY_VALUE,
        .capacity_rejection_count = OS_KERNEL_INODE_IO_EMPTY_VALUE,
        .access_generation_reset_count = OS_KERNEL_INODE_IO_EMPTY_VALUE,
    };
    this->initialized_ = true;
    return InodeIoCoordinatorStatus::Succeeded;
}

InodeIoCoordinatorStatus InodeIoCoordinator::Acquire(const InodeIoIdentity &identity,
                                                     InodeIoToken &token) noexcept {
    token = InodeIoToken{
        .slot_index = OS_KERNEL_INODE_IO_INVALID_SLOT_INDEX,
        .generation = OS_KERNEL_INODE_IO_EMPTY_VALUE,
    };
    if (!this->initialized_ || this->storage_ == nullptr) {
        return InodeIoCoordinatorStatus::NotInitialized;
    }
    if (!InodeIoIdentityIsValid(identity)) {
        return InodeIoCoordinatorStatus::InvalidIdentity;
    }

    uint64_t selected_slot_index = OS_KERNEL_INODE_IO_INVALID_SLOT_INDEX;
    {
        SpinLockGuard guard{this->lock_};
        selected_slot_index = this->FindIdentity(identity);
        const bool identity_reused = selected_slot_index != OS_KERNEL_INODE_IO_INVALID_SLOT_INDEX;
        if (!identity_reused) {
            selected_slot_index = this->FindReplacementCandidate();
        }
        if (selected_slot_index == OS_KERNEL_INODE_IO_INVALID_SLOT_INDEX) {
            IncreaseSaturating(this->statistics_.capacity_rejection_count);
            return InodeIoCoordinatorStatus::CapacityExhausted;
        }

        InodeIoSlot &slot = this->storage_[selected_slot_index];
        if (slot.reference_count == UINT64_MAX ||
            this->statistics_.active_reference_count == UINT64_MAX) {
            return InodeIoCoordinatorStatus::CounterOverflow;
        }
        const uint64_t access_generation = this->NextAccessGeneration();
        if (access_generation == OS_KERNEL_INODE_IO_EMPTY_VALUE) {
            return InodeIoCoordinatorStatus::CounterOverflow;
        }
        if (!identity_reused) {
            if (slot.generation == UINT64_MAX) {
                return InodeIoCoordinatorStatus::GenerationExhausted;
            }
            if (slot.cached) {
                IncreaseSaturating(this->statistics_.slot_replacement_count);
            } else {
                ++this->statistics_.cached_slot_count;
            }
            ++slot.generation;
            slot.identity = identity;
            slot.cached = true;
        } else {
            IncreaseSaturating(this->statistics_.identity_reuse_count);
        }
        if (slot.reference_count == OS_KERNEL_INODE_IO_EMPTY_VALUE) {
            ++this->statistics_.referenced_slot_count;
            if (this->statistics_.referenced_slot_count >
                this->statistics_.peak_referenced_slot_count) {
                this->statistics_.peak_referenced_slot_count =
                    this->statistics_.referenced_slot_count;
            }
        }
        slot.access_generation = access_generation;
        ++slot.reference_count;
        ++this->statistics_.active_reference_count;
        if (this->statistics_.active_reference_count >
            this->statistics_.peak_active_reference_count) {
            this->statistics_.peak_active_reference_count =
                this->statistics_.active_reference_count;
        }
        IncreaseSaturating(this->statistics_.acquisition_count);
        token = InodeIoToken{
            .slot_index = selected_slot_index,
            .generation = slot.generation,
        };
    }

    this->storage_[selected_slot_index].mutex.Lock();
    return InodeIoCoordinatorStatus::Succeeded;
}

InodeIoCoordinatorStatus InodeIoCoordinator::Release(InodeIoToken &token) noexcept {
    if (!this->initialized_ || this->storage_ == nullptr) {
        return InodeIoCoordinatorStatus::NotInitialized;
    }
    {
        SpinLockGuard guard{this->lock_};
        if (!this->TokenIsValid(token)) {
            return InodeIoCoordinatorStatus::InvalidToken;
        }
    }

    InodeIoSlot &slot = this->storage_[token.slot_index];
    slot.mutex.Unlock();
    {
        SpinLockGuard guard{this->lock_};
        if (!this->TokenIsValid(token) ||
            this->statistics_.active_reference_count == OS_KERNEL_INODE_IO_EMPTY_VALUE) {
            return InodeIoCoordinatorStatus::Corrupt;
        }
        --slot.reference_count;
        --this->statistics_.active_reference_count;
        if (slot.reference_count == OS_KERNEL_INODE_IO_EMPTY_VALUE) {
            if (this->statistics_.referenced_slot_count == OS_KERNEL_INODE_IO_EMPTY_VALUE) {
                return InodeIoCoordinatorStatus::Corrupt;
            }
            --this->statistics_.referenced_slot_count;
        }
        IncreaseSaturating(this->statistics_.release_count);
    }
    token = InodeIoToken{
        .slot_index = OS_KERNEL_INODE_IO_INVALID_SLOT_INDEX,
        .generation = OS_KERNEL_INODE_IO_EMPTY_VALUE,
    };
    return InodeIoCoordinatorStatus::Succeeded;
}

InodeIoCoordinatorStatistics InodeIoCoordinator::Statistics() const noexcept {
    SpinLockGuard guard{this->lock_};
    return this->initialized_ ? this->statistics_ : InodeIoCoordinatorStatistics{};
}

InodeIoCoordinatorStatus InodeIoCoordinator::Validate() const noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_ || this->storage_ == nullptr ||
        this->capacity_ == OS_KERNEL_INODE_IO_EMPTY_VALUE ||
        this->statistics_.capacity != this->capacity_) {
        return InodeIoCoordinatorStatus::NotInitialized;
    }
    uint64_t cached_slot_count = OS_KERNEL_INODE_IO_EMPTY_VALUE;
    uint64_t referenced_slot_count = OS_KERNEL_INODE_IO_EMPTY_VALUE;
    uint64_t active_reference_count = OS_KERNEL_INODE_IO_EMPTY_VALUE;
    for (uint64_t slot_index = OS_KERNEL_INODE_IO_EMPTY_VALUE; slot_index < this->capacity_;
         ++slot_index) {
        const InodeIoSlot &slot = this->storage_[slot_index];
        if (!slot.mutex.IsInitialized()) {
            return InodeIoCoordinatorStatus::Corrupt;
        }
        if (!slot.cached) {
            if (!IdentityIsEmpty(slot.identity) ||
                slot.access_generation != OS_KERNEL_INODE_IO_EMPTY_VALUE ||
                slot.reference_count != OS_KERNEL_INODE_IO_EMPTY_VALUE ||
                slot.generation != OS_KERNEL_INODE_IO_EMPTY_VALUE) {
                return InodeIoCoordinatorStatus::Corrupt;
            }
            continue;
        }
        if (!InodeIoIdentityIsValid(slot.identity) ||
            slot.access_generation == OS_KERNEL_INODE_IO_EMPTY_VALUE ||
            slot.generation == OS_KERNEL_INODE_IO_EMPTY_VALUE ||
            active_reference_count > UINT64_MAX - slot.reference_count) {
            return InodeIoCoordinatorStatus::Corrupt;
        }
        ++cached_slot_count;
        if (slot.reference_count != OS_KERNEL_INODE_IO_EMPTY_VALUE) {
            ++referenced_slot_count;
            active_reference_count += slot.reference_count;
        }
        for (uint64_t comparison_index = OS_KERNEL_INODE_IO_EMPTY_VALUE;
             comparison_index < slot_index; ++comparison_index) {
            const InodeIoSlot &comparison = this->storage_[comparison_index];
            if (comparison.cached && InodeIoIdentitiesEqual(slot.identity, comparison.identity)) {
                return InodeIoCoordinatorStatus::Corrupt;
            }
        }
    }
    return cached_slot_count == this->statistics_.cached_slot_count &&
                   referenced_slot_count == this->statistics_.referenced_slot_count &&
                   active_reference_count == this->statistics_.active_reference_count &&
                   this->statistics_.peak_referenced_slot_count >= referenced_slot_count &&
                   this->statistics_.peak_active_reference_count >= active_reference_count
               ? InodeIoCoordinatorStatus::Succeeded
               : InodeIoCoordinatorStatus::Corrupt;
}

bool InodeIoCoordinator::IsInitialized() const noexcept {
    return this->initialized_ && this->storage_ != nullptr &&
           this->capacity_ != OS_KERNEL_INODE_IO_EMPTY_VALUE;
}

uint64_t InodeIoCoordinator::FindIdentity(const InodeIoIdentity &identity) const noexcept {
    for (uint64_t slot_index = OS_KERNEL_INODE_IO_EMPTY_VALUE; slot_index < this->capacity_;
         ++slot_index) {
        const InodeIoSlot &slot = this->storage_[slot_index];
        if (slot.cached && InodeIoIdentitiesEqual(slot.identity, identity)) {
            return slot_index;
        }
    }
    return OS_KERNEL_INODE_IO_INVALID_SLOT_INDEX;
}

uint64_t InodeIoCoordinator::FindReplacementCandidate() const noexcept {
    uint64_t candidate_index = OS_KERNEL_INODE_IO_INVALID_SLOT_INDEX;
    uint64_t candidate_access_generation = UINT64_MAX;
    for (uint64_t slot_index = OS_KERNEL_INODE_IO_EMPTY_VALUE; slot_index < this->capacity_;
         ++slot_index) {
        const InodeIoSlot &slot = this->storage_[slot_index];
        if (slot.reference_count != OS_KERNEL_INODE_IO_EMPTY_VALUE) {
            continue;
        }
        if (!slot.cached) {
            return slot_index;
        }
        if (slot.access_generation < candidate_access_generation) {
            candidate_index = slot_index;
            candidate_access_generation = slot.access_generation;
        }
    }
    return candidate_index;
}

bool InodeIoCoordinator::TokenIsValid(const InodeIoToken &token) const noexcept {
    return token.slot_index < this->capacity_ &&
           token.generation != OS_KERNEL_INODE_IO_EMPTY_VALUE &&
           this->storage_[token.slot_index].cached &&
           this->storage_[token.slot_index].generation == token.generation &&
           this->storage_[token.slot_index].reference_count != OS_KERNEL_INODE_IO_EMPTY_VALUE;
}

uint64_t InodeIoCoordinator::NextAccessGeneration() noexcept {
    if (this->access_generation_ == UINT64_MAX) {
        uint64_t compact_generation = OS_KERNEL_INODE_IO_EMPTY_VALUE;
        for (uint64_t slot_index = OS_KERNEL_INODE_IO_EMPTY_VALUE; slot_index < this->capacity_;
             ++slot_index) {
            InodeIoSlot &slot = this->storage_[slot_index];
            if (!slot.cached) {
                continue;
            }
            ++compact_generation;
            slot.access_generation = compact_generation;
        }
        this->access_generation_ = compact_generation;
        IncreaseSaturating(this->statistics_.access_generation_reset_count);
    }
    if (this->access_generation_ == UINT64_MAX) {
        return OS_KERNEL_INODE_IO_EMPTY_VALUE;
    }
    ++this->access_generation_;
    return this->access_generation_;
}

bool InodeIoIdentityIsValid(const InodeIoIdentity &identity) noexcept {
    return identity.superblock_identifier != OS_KERNEL_INODE_IO_EMPTY_VALUE &&
           identity.superblock_generation != OS_KERNEL_INODE_IO_EMPTY_VALUE &&
           identity.node_identifier != OS_KERNEL_INODE_IO_EMPTY_VALUE &&
           identity.node_generation != OS_KERNEL_INODE_IO_EMPTY_VALUE;
}

bool InodeIoIdentitiesEqual(const InodeIoIdentity &left, const InodeIoIdentity &right) noexcept {
    return left.superblock_identifier == right.superblock_identifier &&
           left.superblock_generation == right.superblock_generation &&
           left.node_identifier == right.node_identifier &&
           left.node_generation == right.node_generation;
}

}

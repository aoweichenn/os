#include <os/kernel/memory/swap_manager.hpp>

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_SWAP_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_SWAP_SINGLE_SLOT = 1ULL;
constexpr uint64_t OS_KERNEL_SWAP_FNV1A_OFFSET_BASIS = 14695981039346656037ULL;
constexpr uint64_t OS_KERNEL_SWAP_FNV1A_PRIME = 1099511628211ULL;

[[nodiscard]] uint64_t Maximum(const uint64_t left, const uint64_t right) noexcept {
    return left > right ? left : right;
}

}

SwapManagerStatus SwapManager::Initialize(SwapSlotEntry *const entries,
                                          const uint64_t slot_capacity,
                                          const uint64_t page_size_bytes,
                                          void *const operation_context,
                                          const SwapReadOperation read_operation,
                                          const SwapWriteOperation write_operation) noexcept {
    if (this->initialized_) {
        return SwapManagerStatus::AlreadyInitialized;
    }
    if (entries == nullptr) {
        return SwapManagerStatus::InvalidStorage;
    }
    if (page_size_bytes == OS_KERNEL_SWAP_EMPTY_VALUE ||
        (page_size_bytes & (page_size_bytes - OS_KERNEL_SWAP_SINGLE_SLOT)) !=
            OS_KERNEL_SWAP_EMPTY_VALUE) {
        return SwapManagerStatus::InvalidPageSize;
    }
    if (slot_capacity == OS_KERNEL_SWAP_EMPTY_VALUE ||
        slot_capacity > UINT64_MAX / page_size_bytes) {
        return SwapManagerStatus::InvalidCapacity;
    }
    if (operation_context == nullptr || read_operation == nullptr || write_operation == nullptr) {
        return SwapManagerStatus::InvalidOperation;
    }
    for (uint64_t slot_index = OS_KERNEL_SWAP_EMPTY_VALUE; slot_index < slot_capacity;
         ++slot_index) {
        entries[slot_index] = SwapSlotEntry{};
    }
    this->entries_ = entries;
    this->slot_capacity_ = slot_capacity;
    this->page_size_bytes_ = page_size_bytes;
    this->operation_context_ = operation_context;
    this->read_operation_ = read_operation;
    this->write_operation_ = write_operation;
    this->statistics_ = SwapManagerStatistics{
        .slot_capacity = slot_capacity,
        .active_slot_count = OS_KERNEL_SWAP_EMPTY_VALUE,
        .free_slot_count = slot_capacity,
        .peak_active_slot_count = OS_KERNEL_SWAP_EMPTY_VALUE,
        .successful_store_count = OS_KERNEL_SWAP_EMPTY_VALUE,
        .failed_store_count = OS_KERNEL_SWAP_EMPTY_VALUE,
        .successful_load_count = OS_KERNEL_SWAP_EMPTY_VALUE,
        .failed_load_count = OS_KERNEL_SWAP_EMPTY_VALUE,
        .checksum_failure_count = OS_KERNEL_SWAP_EMPTY_VALUE,
        .successful_clone_count = OS_KERNEL_SWAP_EMPTY_VALUE,
        .failed_clone_count = OS_KERNEL_SWAP_EMPTY_VALUE,
        .release_count = OS_KERNEL_SWAP_EMPTY_VALUE,
    };
    this->initialized_ = true;
    return SwapManagerStatus::Succeeded;
}

SwapManagerStatus SwapManager::Store(const SwapPageIdentity &identity, const uint8_t *const source,
                                     const uint64_t length_bytes, uint64_t &slot_index) noexcept {
    slot_index = UINT64_MAX;
    if (!this->initialized_) {
        return SwapManagerStatus::NotInitialized;
    }
    if (!this->IdentityIsValid(identity)) {
        return SwapManagerStatus::InvalidIdentity;
    }
    if (source == nullptr || length_bytes != this->page_size_bytes_) {
        return SwapManagerStatus::InvalidStorage;
    }
    uint64_t existing_slot_index = UINT64_MAX;
    if (this->FindEntry(identity, existing_slot_index) != nullptr) {
        return SwapManagerStatus::MappingAlreadyStored;
    }
    SwapSlotEntry *candidate = nullptr;
    for (uint64_t candidate_index = OS_KERNEL_SWAP_EMPTY_VALUE;
         candidate_index < this->slot_capacity_; ++candidate_index) {
        if (!this->entries_[candidate_index].active) {
            candidate = &this->entries_[candidate_index];
            slot_index = candidate_index;
            break;
        }
    }
    if (candidate == nullptr) {
        slot_index = UINT64_MAX;
        return SwapManagerStatus::CapacityExhausted;
    }
    if (this->statistics_.active_slot_count == UINT64_MAX ||
        this->statistics_.successful_store_count == UINT64_MAX ||
        this->statistics_.free_slot_count == OS_KERNEL_SWAP_EMPTY_VALUE) {
        slot_index = UINT64_MAX;
        return SwapManagerStatus::CounterOverflow;
    }
    if (!this->write_operation_(this->operation_context_, slot_index, source, length_bytes)) {
        slot_index = UINT64_MAX;
        if (this->statistics_.failed_store_count == UINT64_MAX) {
            return SwapManagerStatus::CounterOverflow;
        }
        ++this->statistics_.failed_store_count;
        return SwapManagerStatus::WriteFailed;
    }
    *candidate = SwapSlotEntry{
        .identity = identity,
        .checksum = this->CalculateChecksum(source),
        .active = true,
    };
    ++this->statistics_.active_slot_count;
    --this->statistics_.free_slot_count;
    ++this->statistics_.successful_store_count;
    this->statistics_.peak_active_slot_count =
        Maximum(this->statistics_.peak_active_slot_count, this->statistics_.active_slot_count);
    return SwapManagerStatus::Succeeded;
}

SwapManagerStatus SwapManager::LoadAndRelease(const SwapPageIdentity &identity,
                                              uint8_t *const destination,
                                              const uint64_t capacity_bytes) noexcept {
    if (!this->initialized_) {
        return SwapManagerStatus::NotInitialized;
    }
    if (!this->IdentityIsValid(identity)) {
        return SwapManagerStatus::InvalidIdentity;
    }
    if (destination == nullptr || capacity_bytes < this->page_size_bytes_) {
        return SwapManagerStatus::InvalidStorage;
    }
    uint64_t slot_index = UINT64_MAX;
    SwapSlotEntry *const entry = this->FindEntry(identity, slot_index);
    if (entry == nullptr) {
        return SwapManagerStatus::MappingNotFound;
    }
    if (!this->read_operation_(this->operation_context_, slot_index, destination,
                               this->page_size_bytes_)) {
        if (this->statistics_.failed_load_count == UINT64_MAX) {
            return SwapManagerStatus::CounterOverflow;
        }
        ++this->statistics_.failed_load_count;
        return SwapManagerStatus::ReadFailed;
    }
    if (this->CalculateChecksum(destination) != entry->checksum) {
        if (this->statistics_.failed_load_count == UINT64_MAX ||
            this->statistics_.checksum_failure_count == UINT64_MAX) {
            return SwapManagerStatus::CounterOverflow;
        }
        ++this->statistics_.failed_load_count;
        ++this->statistics_.checksum_failure_count;
        return SwapManagerStatus::ChecksumMismatch;
    }
    if (this->statistics_.active_slot_count == OS_KERNEL_SWAP_EMPTY_VALUE ||
        this->statistics_.free_slot_count == UINT64_MAX ||
        this->statistics_.successful_load_count == UINT64_MAX) {
        return SwapManagerStatus::CounterOverflow;
    }
    *entry = SwapSlotEntry{};
    --this->statistics_.active_slot_count;
    ++this->statistics_.free_slot_count;
    ++this->statistics_.successful_load_count;
    return SwapManagerStatus::Succeeded;
}

SwapManagerStatus SwapManager::Clone(const SwapPageIdentity &source_identity,
                                     const SwapPageIdentity &destination_identity,
                                     uint8_t *const scratch_page, const uint64_t capacity_bytes,
                                     uint64_t &destination_slot_index) noexcept {
    destination_slot_index = UINT64_MAX;
    if (!this->initialized_) {
        return SwapManagerStatus::NotInitialized;
    }
    if (!this->IdentityIsValid(source_identity) || !this->IdentityIsValid(destination_identity)) {
        return SwapManagerStatus::InvalidIdentity;
    }
    if (scratch_page == nullptr || capacity_bytes < this->page_size_bytes_) {
        return SwapManagerStatus::InvalidStorage;
    }
    uint64_t source_slot_index = UINT64_MAX;
    const SwapSlotEntry *const source_entry = this->FindEntry(source_identity, source_slot_index);
    if (source_entry == nullptr) {
        return SwapManagerStatus::MappingNotFound;
    }
    uint64_t existing_slot_index = UINT64_MAX;
    if (this->FindEntry(destination_identity, existing_slot_index) != nullptr) {
        return SwapManagerStatus::MappingAlreadyStored;
    }
    if (!this->read_operation_(this->operation_context_, source_slot_index, scratch_page,
                               this->page_size_bytes_)) {
        if (this->statistics_.failed_clone_count == UINT64_MAX) {
            return SwapManagerStatus::CounterOverflow;
        }
        ++this->statistics_.failed_clone_count;
        return SwapManagerStatus::ReadFailed;
    }
    if (this->CalculateChecksum(scratch_page) != source_entry->checksum) {
        if (this->statistics_.failed_clone_count == UINT64_MAX ||
            this->statistics_.checksum_failure_count == UINT64_MAX) {
            return SwapManagerStatus::CounterOverflow;
        }
        ++this->statistics_.failed_clone_count;
        ++this->statistics_.checksum_failure_count;
        return SwapManagerStatus::ChecksumMismatch;
    }
    if (this->statistics_.successful_clone_count == UINT64_MAX) {
        return SwapManagerStatus::CounterOverflow;
    }
    const SwapManagerStatus store_status = this->Store(
        destination_identity, scratch_page, this->page_size_bytes_, destination_slot_index);
    if (store_status != SwapManagerStatus::Succeeded) {
        if (this->statistics_.failed_clone_count == UINT64_MAX) {
            return SwapManagerStatus::CounterOverflow;
        }
        ++this->statistics_.failed_clone_count;
        return store_status;
    }
    ++this->statistics_.successful_clone_count;
    return SwapManagerStatus::Succeeded;
}

SwapManagerStatus SwapManager::Release(const SwapPageIdentity &identity) noexcept {
    if (!this->initialized_) {
        return SwapManagerStatus::NotInitialized;
    }
    if (!this->IdentityIsValid(identity)) {
        return SwapManagerStatus::InvalidIdentity;
    }
    uint64_t slot_index = UINT64_MAX;
    SwapSlotEntry *const entry = this->FindEntry(identity, slot_index);
    static_cast<void>(slot_index);
    if (entry == nullptr) {
        return SwapManagerStatus::MappingNotFound;
    }
    if (this->statistics_.active_slot_count == OS_KERNEL_SWAP_EMPTY_VALUE ||
        this->statistics_.free_slot_count == UINT64_MAX ||
        this->statistics_.release_count == UINT64_MAX) {
        return SwapManagerStatus::CounterOverflow;
    }
    *entry = SwapSlotEntry{};
    --this->statistics_.active_slot_count;
    ++this->statistics_.free_slot_count;
    ++this->statistics_.release_count;
    return SwapManagerStatus::Succeeded;
}

SwapManagerStatus SwapManager::FindSlot(const SwapPageIdentity &identity,
                                        uint64_t &slot_index) const noexcept {
    slot_index = UINT64_MAX;
    if (!this->initialized_) {
        return SwapManagerStatus::NotInitialized;
    }
    if (!this->IdentityIsValid(identity)) {
        return SwapManagerStatus::InvalidIdentity;
    }
    return this->FindEntry(identity, slot_index) == nullptr ? SwapManagerStatus::MappingNotFound
                                                            : SwapManagerStatus::Succeeded;
}

SwapManagerStatistics SwapManager::Statistics() const noexcept {
    return this->initialized_ ? this->statistics_ : SwapManagerStatistics{};
}

SwapManagerStatus SwapManager::Validate() const noexcept {
    if (!this->initialized_ || this->entries_ == nullptr || this->operation_context_ == nullptr ||
        this->read_operation_ == nullptr || this->write_operation_ == nullptr) {
        return SwapManagerStatus::NotInitialized;
    }
    uint64_t active_slot_count = OS_KERNEL_SWAP_EMPTY_VALUE;
    for (uint64_t slot_index = OS_KERNEL_SWAP_EMPTY_VALUE; slot_index < this->slot_capacity_;
         ++slot_index) {
        const SwapSlotEntry &entry = this->entries_[slot_index];
        if (!entry.active) {
            if (entry.identity.address_space_identifier != OS_KERNEL_SWAP_EMPTY_VALUE ||
                entry.identity.virtual_address != OS_KERNEL_SWAP_EMPTY_VALUE ||
                entry.checksum != OS_KERNEL_SWAP_EMPTY_VALUE) {
                return SwapManagerStatus::Corrupt;
            }
            continue;
        }
        if (!this->IdentityIsValid(entry.identity)) {
            return SwapManagerStatus::Corrupt;
        }
        for (uint64_t comparison_index = slot_index + OS_KERNEL_SWAP_SINGLE_SLOT;
             comparison_index < this->slot_capacity_; ++comparison_index) {
            const SwapSlotEntry &comparison = this->entries_[comparison_index];
            if (comparison.active && this->IdentitiesEqual(entry.identity, comparison.identity)) {
                return SwapManagerStatus::Corrupt;
            }
        }
        ++active_slot_count;
    }
    return active_slot_count == this->statistics_.active_slot_count &&
                   this->statistics_.free_slot_count == this->slot_capacity_ - active_slot_count &&
                   this->statistics_.slot_capacity == this->slot_capacity_ &&
                   this->statistics_.peak_active_slot_count >= active_slot_count &&
                   this->statistics_.peak_active_slot_count <= this->slot_capacity_
               ? SwapManagerStatus::Succeeded
               : SwapManagerStatus::Corrupt;
}

bool SwapManager::IdentityIsValid(const SwapPageIdentity &identity) const noexcept {
    return identity.address_space_identifier != OS_KERNEL_SWAP_EMPTY_VALUE &&
           this->page_size_bytes_ != OS_KERNEL_SWAP_EMPTY_VALUE &&
           (identity.virtual_address & (this->page_size_bytes_ - OS_KERNEL_SWAP_SINGLE_SLOT)) ==
               OS_KERNEL_SWAP_EMPTY_VALUE;
}

bool SwapManager::IdentitiesEqual(const SwapPageIdentity &left,
                                  const SwapPageIdentity &right) const noexcept {
    return left.address_space_identifier == right.address_space_identifier &&
           left.virtual_address == right.virtual_address;
}

SwapSlotEntry *SwapManager::FindEntry(const SwapPageIdentity &identity,
                                      uint64_t &slot_index) noexcept {
    if (this->statistics_.active_slot_count == OS_KERNEL_SWAP_EMPTY_VALUE) {
        slot_index = UINT64_MAX;
        return nullptr;
    }
    uint64_t observed_active_slot_count = OS_KERNEL_SWAP_EMPTY_VALUE;
    for (uint64_t candidate_index = OS_KERNEL_SWAP_EMPTY_VALUE;
         candidate_index < this->slot_capacity_; ++candidate_index) {
        SwapSlotEntry &entry = this->entries_[candidate_index];
        if (entry.active && this->IdentitiesEqual(entry.identity, identity)) {
            slot_index = candidate_index;
            return &entry;
        }
        if (entry.active) {
            ++observed_active_slot_count;
            if (observed_active_slot_count == this->statistics_.active_slot_count) {
                break;
            }
        }
    }
    slot_index = UINT64_MAX;
    return nullptr;
}

const SwapSlotEntry *SwapManager::FindEntry(const SwapPageIdentity &identity,
                                            uint64_t &slot_index) const noexcept {
    if (this->statistics_.active_slot_count == OS_KERNEL_SWAP_EMPTY_VALUE) {
        slot_index = UINT64_MAX;
        return nullptr;
    }
    uint64_t observed_active_slot_count = OS_KERNEL_SWAP_EMPTY_VALUE;
    for (uint64_t candidate_index = OS_KERNEL_SWAP_EMPTY_VALUE;
         candidate_index < this->slot_capacity_; ++candidate_index) {
        const SwapSlotEntry &entry = this->entries_[candidate_index];
        if (entry.active && this->IdentitiesEqual(entry.identity, identity)) {
            slot_index = candidate_index;
            return &entry;
        }
        if (entry.active) {
            ++observed_active_slot_count;
            if (observed_active_slot_count == this->statistics_.active_slot_count) {
                break;
            }
        }
    }
    slot_index = UINT64_MAX;
    return nullptr;
}

uint64_t SwapManager::CalculateChecksum(const uint8_t *const page) const noexcept {
    uint64_t checksum = OS_KERNEL_SWAP_FNV1A_OFFSET_BASIS;
    for (uint64_t byte_index = OS_KERNEL_SWAP_EMPTY_VALUE; byte_index < this->page_size_bytes_;
         ++byte_index) {
        checksum ^= static_cast<uint64_t>(page[byte_index]);
        checksum *= OS_KERNEL_SWAP_FNV1A_PRIME;
    }
    return checksum;
}

}

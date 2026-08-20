#include <os/kernel/memory/swap_manager.hpp>

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_SWAP_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_SWAP_SINGLE_SLOT = 1ULL;
constexpr uint64_t OS_KERNEL_SWAP_BITS_PER_BYTE = 8ULL;
constexpr uint64_t OS_KERNEL_SWAP_FNV1A_OFFSET_BASIS = 14695981039346656037ULL;
constexpr uint64_t OS_KERNEL_SWAP_FNV1A_PRIME = 1099511628211ULL;

[[nodiscard]] uint64_t Maximum(const uint64_t left, const uint64_t right) noexcept {
    return left > right ? left : right;
}

}

SwapManagerStatus SwapManager::Initialize(const uint64_t slot_capacity,
                                          const uint64_t page_size_bytes,
                                          void *const operation_context,
                                          const SwapEntryReadOperation entry_read_operation,
                                          const SwapEntryWriteOperation entry_write_operation,
                                          const SwapReadOperation read_operation,
                                          const SwapWriteOperation write_operation) noexcept {
    if (this->initialized_) {
        return SwapManagerStatus::AlreadyInitialized;
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
    if (operation_context == nullptr || entry_read_operation == nullptr ||
        entry_write_operation == nullptr || read_operation == nullptr ||
        write_operation == nullptr) {
        return SwapManagerStatus::InvalidOperation;
    }
    this->slot_capacity_ = slot_capacity;
    this->page_size_bytes_ = page_size_bytes;
    this->operation_context_ = operation_context;
    this->entry_read_operation_ = entry_read_operation;
    this->entry_write_operation_ = entry_write_operation;
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
    SwapSlotEntry candidate{};
    bool found = false;
    const SwapManagerStatus probe_status =
        this->Probe(identity, ProbePurpose::Insert, slot_index, candidate, found);
    if (probe_status != SwapManagerStatus::Succeeded) {
        slot_index = UINT64_MAX;
        return probe_status;
    }
    if (found) {
        slot_index = UINT64_MAX;
        return SwapManagerStatus::MappingAlreadyStored;
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
    candidate = SwapSlotEntry{
        .identity = identity,
        .checksum = this->CalculateChecksum(source),
        .state = SwapSlotState::Active,
    };
    if (!this->entry_write_operation_(this->operation_context_, slot_index, candidate)) {
        slot_index = UINT64_MAX;
        if (this->statistics_.failed_store_count == UINT64_MAX) {
            return SwapManagerStatus::CounterOverflow;
        }
        ++this->statistics_.failed_store_count;
        return SwapManagerStatus::MetadataWriteFailed;
    }
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
    SwapSlotEntry entry{};
    bool found = false;
    const SwapManagerStatus probe_status =
        this->Probe(identity, ProbePurpose::Find, slot_index, entry, found);
    if (probe_status != SwapManagerStatus::Succeeded) {
        return probe_status;
    }
    if (!found) {
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
    if (this->CalculateChecksum(destination) != entry.checksum) {
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
    const SwapSlotEntry tombstone{
        .identity = SwapPageIdentity{},
        .checksum = OS_KERNEL_SWAP_EMPTY_VALUE,
        .state = SwapSlotState::Tombstone,
    };
    if (!this->entry_write_operation_(this->operation_context_, slot_index, tombstone)) {
        if (this->statistics_.failed_load_count == UINT64_MAX) {
            return SwapManagerStatus::CounterOverflow;
        }
        ++this->statistics_.failed_load_count;
        return SwapManagerStatus::MetadataWriteFailed;
    }
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
    SwapSlotEntry source_entry{};
    bool source_found = false;
    const SwapManagerStatus source_probe_status = this->Probe(
        source_identity, ProbePurpose::Find, source_slot_index, source_entry, source_found);
    if (source_probe_status != SwapManagerStatus::Succeeded) {
        return source_probe_status;
    }
    if (!source_found) {
        return SwapManagerStatus::MappingNotFound;
    }
    if (!this->read_operation_(this->operation_context_, source_slot_index, scratch_page,
                               this->page_size_bytes_)) {
        if (this->statistics_.failed_clone_count == UINT64_MAX) {
            return SwapManagerStatus::CounterOverflow;
        }
        ++this->statistics_.failed_clone_count;
        return SwapManagerStatus::ReadFailed;
    }
    if (this->CalculateChecksum(scratch_page) != source_entry.checksum) {
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
    SwapSlotEntry entry{};
    bool found = false;
    const SwapManagerStatus probe_status =
        this->Probe(identity, ProbePurpose::Find, slot_index, entry, found);
    if (probe_status != SwapManagerStatus::Succeeded) {
        return probe_status;
    }
    if (!found) {
        return SwapManagerStatus::MappingNotFound;
    }
    if (this->statistics_.active_slot_count == OS_KERNEL_SWAP_EMPTY_VALUE ||
        this->statistics_.free_slot_count == UINT64_MAX ||
        this->statistics_.release_count == UINT64_MAX) {
        return SwapManagerStatus::CounterOverflow;
    }
    const SwapSlotEntry tombstone{
        .identity = SwapPageIdentity{},
        .checksum = OS_KERNEL_SWAP_EMPTY_VALUE,
        .state = SwapSlotState::Tombstone,
    };
    if (!this->entry_write_operation_(this->operation_context_, slot_index, tombstone)) {
        return SwapManagerStatus::MetadataWriteFailed;
    }
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
    if (this->statistics_.active_slot_count == OS_KERNEL_SWAP_EMPTY_VALUE) {
        return SwapManagerStatus::MappingNotFound;
    }
    SwapSlotEntry entry{};
    bool found = false;
    const SwapManagerStatus probe_status =
        this->Probe(identity, ProbePurpose::Find, slot_index, entry, found);
    if (probe_status != SwapManagerStatus::Succeeded) {
        return probe_status;
    }
    return found ? SwapManagerStatus::Succeeded : SwapManagerStatus::MappingNotFound;
}

SwapManagerStatistics SwapManager::Statistics() const noexcept {
    return this->initialized_ ? this->statistics_ : SwapManagerStatistics{};
}

SwapManagerStatus SwapManager::Validate() const noexcept {
    if (!this->initialized_ || this->operation_context_ == nullptr ||
        this->entry_read_operation_ == nullptr || this->entry_write_operation_ == nullptr ||
        this->read_operation_ == nullptr || this->write_operation_ == nullptr) {
        return SwapManagerStatus::NotInitialized;
    }
    if (this->statistics_.slot_capacity != this->slot_capacity_ ||
        this->statistics_.active_slot_count > this->slot_capacity_ ||
        this->statistics_.free_slot_count !=
            this->slot_capacity_ - this->statistics_.active_slot_count ||
        this->statistics_.peak_active_slot_count < this->statistics_.active_slot_count ||
        this->statistics_.peak_active_slot_count > this->slot_capacity_ ||
        this->statistics_.successful_load_count > this->statistics_.successful_store_count ||
        this->statistics_.release_count >
            this->statistics_.successful_store_count - this->statistics_.successful_load_count ||
        this->statistics_.successful_store_count - this->statistics_.successful_load_count -
                this->statistics_.release_count !=
            this->statistics_.active_slot_count) {
        return SwapManagerStatus::Corrupt;
    }
    return SwapManagerStatus::Succeeded;
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

SwapManagerStatus SwapManager::Probe(const SwapPageIdentity &identity, const ProbePurpose purpose,
                                     uint64_t &slot_index, SwapSlotEntry &entry,
                                     bool &found) const noexcept {
    slot_index = UINT64_MAX;
    entry = SwapSlotEntry{};
    found = false;
    uint64_t candidate_index = this->CalculateInitialSlot(identity);
    uint64_t first_tombstone_index = UINT64_MAX;
    for (uint64_t probe_count = OS_KERNEL_SWAP_EMPTY_VALUE; probe_count < this->slot_capacity_;
         ++probe_count) {
        SwapSlotEntry candidate{};
        if (!this->entry_read_operation_(this->operation_context_, candidate_index, candidate)) {
            return SwapManagerStatus::MetadataReadFailed;
        }
        if (candidate.state == SwapSlotState::Active) {
            if (!this->IdentityIsValid(candidate.identity)) {
                return SwapManagerStatus::Corrupt;
            }
            if (this->IdentitiesEqual(candidate.identity, identity)) {
                slot_index = candidate_index;
                entry = candidate;
                found = true;
                return SwapManagerStatus::Succeeded;
            }
        } else if (candidate.state == SwapSlotState::Tombstone) {
            if (first_tombstone_index == UINT64_MAX) {
                first_tombstone_index = candidate_index;
            }
        } else if (candidate.state == SwapSlotState::Empty) {
            if (purpose == ProbePurpose::Insert) {
                slot_index =
                    first_tombstone_index == UINT64_MAX ? candidate_index : first_tombstone_index;
            }
            return SwapManagerStatus::Succeeded;
        } else {
            return SwapManagerStatus::Corrupt;
        }
        candidate_index = candidate_index + OS_KERNEL_SWAP_SINGLE_SLOT == this->slot_capacity_
                              ? OS_KERNEL_SWAP_EMPTY_VALUE
                              : candidate_index + OS_KERNEL_SWAP_SINGLE_SLOT;
    }
    if (purpose == ProbePurpose::Insert && first_tombstone_index != UINT64_MAX) {
        slot_index = first_tombstone_index;
        return SwapManagerStatus::Succeeded;
    }
    return purpose == ProbePurpose::Insert ? SwapManagerStatus::CapacityExhausted
                                           : SwapManagerStatus::Succeeded;
}

uint64_t SwapManager::CalculateInitialSlot(const SwapPageIdentity &identity) const noexcept {
    uint64_t hash = OS_KERNEL_SWAP_FNV1A_OFFSET_BASIS;
    const uint64_t values[] = {identity.address_space_identifier, identity.virtual_address};
    for (const uint64_t value : values) {
        for (uint64_t shift_bits = OS_KERNEL_SWAP_EMPTY_VALUE; shift_bits < 64ULL;
             shift_bits += OS_KERNEL_SWAP_BITS_PER_BYTE) {
            hash ^= (value >> shift_bits) & 0xFFULL;
            hash *= OS_KERNEL_SWAP_FNV1A_PRIME;
        }
    }
    return hash % this->slot_capacity_;
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

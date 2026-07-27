#include "os/kernel/memory/user_page_reference.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_USER_PAGE_REFERENCE_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_USER_PAGE_REFERENCE_SINGLE_REFERENCE = 1ULL;
constexpr uint64_t OS_KERNEL_USER_PAGE_REFERENCE_FIRST_SHARED_COUNT = 2ULL;

[[nodiscard]] uint64_t Maximum(const uint64_t left, const uint64_t right) noexcept {
    return left > right ? left : right;
}

}

UserPageReferenceStatus
UserPageReferenceManager::Initialize(UserPageReferenceEntry *const entries,
                                     const uint64_t capacity,
                                     const uint64_t page_size_bytes) noexcept {
    if (this->initialized_) {
        return UserPageReferenceStatus::AlreadyInitialized;
    }
    if (entries == nullptr) {
        return UserPageReferenceStatus::InvalidStorage;
    }
    if (capacity == OS_KERNEL_USER_PAGE_REFERENCE_EMPTY_VALUE ||
        page_size_bytes == OS_KERNEL_USER_PAGE_REFERENCE_EMPTY_VALUE ||
        (page_size_bytes & (page_size_bytes - OS_KERNEL_USER_PAGE_REFERENCE_SINGLE_REFERENCE)) !=
            OS_KERNEL_USER_PAGE_REFERENCE_EMPTY_VALUE) {
        return UserPageReferenceStatus::InvalidCapacity;
    }
    for (uint64_t entry_index = OS_KERNEL_USER_PAGE_REFERENCE_EMPTY_VALUE;
         entry_index < capacity; ++entry_index) {
        entries[entry_index] = UserPageReferenceEntry{};
    }
    this->entries_ = entries;
    this->capacity_ = capacity;
    this->page_size_bytes_ = page_size_bytes;
    this->statistics_ = UserPageReferenceStatistics{
        .capacity = capacity,
    };
    this->lock_ = SpinLock{};
    this->initialized_ = true;
    return UserPageReferenceStatus::Succeeded;
}

UserPageReferenceStatus
UserPageReferenceManager::RetainForFork(const uint64_t physical_address,
                                        bool &first_share) noexcept {
    first_share = false;
    if (!this->initialized_) {
        return UserPageReferenceStatus::NotInitialized;
    }
    if (!this->IsPhysicalAddressValid(physical_address)) {
        return UserPageReferenceStatus::InvalidPhysicalAddress;
    }
    SpinLockGuard guard{this->lock_};
    UserPageReferenceEntry *entry = this->Find(physical_address);
    if (entry != nullptr) {
        if (entry->reference_count == UINT64_MAX ||
            this->statistics_.active_reference_count == UINT64_MAX) {
            return UserPageReferenceStatus::ReferenceOverflow;
        }
        ++entry->reference_count;
        ++this->statistics_.active_reference_count;
        ++this->statistics_.retain_count;
    } else {
        for (uint64_t entry_index = OS_KERNEL_USER_PAGE_REFERENCE_EMPTY_VALUE;
             entry_index < this->capacity_; ++entry_index) {
            if (this->entries_[entry_index].active) {
                continue;
            }
            entry = &this->entries_[entry_index];
            break;
        }
        if (entry == nullptr) {
            return UserPageReferenceStatus::CapacityExhausted;
        }
        *entry = UserPageReferenceEntry{
            .physical_address = physical_address,
            .reference_count = OS_KERNEL_USER_PAGE_REFERENCE_FIRST_SHARED_COUNT,
            .active = true,
        };
        ++this->statistics_.active_entry_count;
        this->statistics_.active_reference_count +=
            OS_KERNEL_USER_PAGE_REFERENCE_FIRST_SHARED_COUNT;
        ++this->statistics_.first_share_count;
        first_share = true;
    }
    this->statistics_.peak_active_entry_count =
        Maximum(this->statistics_.peak_active_entry_count,
                this->statistics_.active_entry_count);
    this->statistics_.peak_active_reference_count =
        Maximum(this->statistics_.peak_active_reference_count,
                this->statistics_.active_reference_count);
    return UserPageReferenceStatus::Succeeded;
}

UserPageReferenceStatus
UserPageReferenceManager::Release(const uint64_t physical_address,
                                  bool &release_frame) noexcept {
    release_frame = false;
    if (!this->initialized_) {
        return UserPageReferenceStatus::NotInitialized;
    }
    if (!this->IsPhysicalAddressValid(physical_address)) {
        return UserPageReferenceStatus::InvalidPhysicalAddress;
    }
    SpinLockGuard guard{this->lock_};
    UserPageReferenceEntry *const entry = this->Find(physical_address);
    if (entry == nullptr) {
        release_frame = true;
        return UserPageReferenceStatus::Succeeded;
    }
    if (entry->reference_count == OS_KERNEL_USER_PAGE_REFERENCE_EMPTY_VALUE ||
        this->statistics_.active_reference_count ==
            OS_KERNEL_USER_PAGE_REFERENCE_EMPTY_VALUE) {
        return UserPageReferenceStatus::ReferenceUnderflow;
    }
    --entry->reference_count;
    --this->statistics_.active_reference_count;
    ++this->statistics_.release_count;
    if (entry->reference_count == OS_KERNEL_USER_PAGE_REFERENCE_EMPTY_VALUE) {
        *entry = UserPageReferenceEntry{};
        if (this->statistics_.active_entry_count ==
            OS_KERNEL_USER_PAGE_REFERENCE_EMPTY_VALUE) {
            return UserPageReferenceStatus::Corrupt;
        }
        --this->statistics_.active_entry_count;
        release_frame = true;
    }
    return UserPageReferenceStatus::Succeeded;
}

UserPageReferenceStatus
UserPageReferenceManager::RestoreExclusive(const uint64_t physical_address) noexcept {
    if (!this->initialized_) {
        return UserPageReferenceStatus::NotInitialized;
    }
    if (!this->IsPhysicalAddressValid(physical_address)) {
        return UserPageReferenceStatus::InvalidPhysicalAddress;
    }
    SpinLockGuard guard{this->lock_};
    UserPageReferenceEntry *const entry = this->Find(physical_address);
    if (entry == nullptr) {
        return UserPageReferenceStatus::Succeeded;
    }
    if (entry->reference_count != OS_KERNEL_USER_PAGE_REFERENCE_SINGLE_REFERENCE) {
        return UserPageReferenceStatus::NotExclusive;
    }
    *entry = UserPageReferenceEntry{};
    if (this->statistics_.active_entry_count ==
            OS_KERNEL_USER_PAGE_REFERENCE_EMPTY_VALUE ||
        this->statistics_.active_reference_count ==
            OS_KERNEL_USER_PAGE_REFERENCE_EMPTY_VALUE) {
        return UserPageReferenceStatus::Corrupt;
    }
    --this->statistics_.active_entry_count;
    --this->statistics_.active_reference_count;
    ++this->statistics_.exclusive_restore_count;
    return UserPageReferenceStatus::Succeeded;
}

UserPageReferenceStatus
UserPageReferenceManager::ReadReferenceCount(const uint64_t physical_address,
                                             uint64_t &reference_count) const noexcept {
    reference_count = OS_KERNEL_USER_PAGE_REFERENCE_EMPTY_VALUE;
    if (!this->initialized_) {
        return UserPageReferenceStatus::NotInitialized;
    }
    if (!this->IsPhysicalAddressValid(physical_address)) {
        return UserPageReferenceStatus::InvalidPhysicalAddress;
    }
    SpinLockGuard guard{this->lock_};
    const UserPageReferenceEntry *const entry = this->Find(physical_address);
    if (entry == nullptr) {
        return UserPageReferenceStatus::ReferenceNotFound;
    }
    reference_count = entry->reference_count;
    return UserPageReferenceStatus::Succeeded;
}

UserPageReferenceStatus UserPageReferenceManager::Validate() const noexcept {
    if (!this->initialized_ || this->entries_ == nullptr) {
        return UserPageReferenceStatus::NotInitialized;
    }
    SpinLockGuard guard{this->lock_};
    uint64_t active_entry_count = OS_KERNEL_USER_PAGE_REFERENCE_EMPTY_VALUE;
    uint64_t active_reference_count = OS_KERNEL_USER_PAGE_REFERENCE_EMPTY_VALUE;
    for (uint64_t entry_index = OS_KERNEL_USER_PAGE_REFERENCE_EMPTY_VALUE;
         entry_index < this->capacity_; ++entry_index) {
        const UserPageReferenceEntry &entry = this->entries_[entry_index];
        if (!entry.active) {
            if (entry.physical_address != OS_KERNEL_USER_PAGE_REFERENCE_EMPTY_VALUE ||
                entry.reference_count != OS_KERNEL_USER_PAGE_REFERENCE_EMPTY_VALUE) {
                return UserPageReferenceStatus::Corrupt;
            }
            continue;
        }
        if (!this->IsPhysicalAddressValid(entry.physical_address) ||
            entry.reference_count == OS_KERNEL_USER_PAGE_REFERENCE_EMPTY_VALUE ||
            active_reference_count > UINT64_MAX - entry.reference_count) {
            return UserPageReferenceStatus::Corrupt;
        }
        for (uint64_t comparison_index =
                 entry_index + OS_KERNEL_USER_PAGE_REFERENCE_SINGLE_REFERENCE;
             comparison_index < this->capacity_; ++comparison_index) {
            const UserPageReferenceEntry &comparison = this->entries_[comparison_index];
            if (comparison.active &&
                comparison.physical_address == entry.physical_address) {
                return UserPageReferenceStatus::Corrupt;
            }
        }
        ++active_entry_count;
        active_reference_count += entry.reference_count;
    }
    return active_entry_count == this->statistics_.active_entry_count &&
                   active_reference_count ==
                       this->statistics_.active_reference_count &&
                   this->statistics_.active_entry_count <= this->capacity_ &&
                   this->statistics_.peak_active_entry_count >=
                       this->statistics_.active_entry_count &&
                   this->statistics_.peak_active_reference_count >=
                       this->statistics_.active_reference_count
               ? UserPageReferenceStatus::Succeeded
               : UserPageReferenceStatus::Corrupt;
}

UserPageReferenceStatistics UserPageReferenceManager::Statistics() const noexcept {
    if (!this->initialized_) {
        return UserPageReferenceStatistics{};
    }
    SpinLockGuard guard{this->lock_};
    return this->statistics_;
}

UserPageReferenceEntry *
UserPageReferenceManager::Find(const uint64_t physical_address) noexcept {
    for (uint64_t entry_index = OS_KERNEL_USER_PAGE_REFERENCE_EMPTY_VALUE;
         entry_index < this->capacity_; ++entry_index) {
        UserPageReferenceEntry &entry = this->entries_[entry_index];
        if (entry.active && entry.physical_address == physical_address) {
            return &entry;
        }
    }
    return nullptr;
}

const UserPageReferenceEntry *
UserPageReferenceManager::Find(const uint64_t physical_address) const noexcept {
    for (uint64_t entry_index = OS_KERNEL_USER_PAGE_REFERENCE_EMPTY_VALUE;
         entry_index < this->capacity_; ++entry_index) {
        const UserPageReferenceEntry &entry = this->entries_[entry_index];
        if (entry.active && entry.physical_address == physical_address) {
            return &entry;
        }
    }
    return nullptr;
}

bool UserPageReferenceManager::IsPhysicalAddressValid(
    const uint64_t physical_address) const noexcept {
    return this->page_size_bytes_ != OS_KERNEL_USER_PAGE_REFERENCE_EMPTY_VALUE &&
           (physical_address & (this->page_size_bytes_ -
                                OS_KERNEL_USER_PAGE_REFERENCE_SINGLE_REFERENCE)) ==
               OS_KERNEL_USER_PAGE_REFERENCE_EMPTY_VALUE;
}

}

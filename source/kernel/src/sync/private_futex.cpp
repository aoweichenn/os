#include "os/kernel/sync/private_futex.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_PRIVATE_FUTEX_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_PRIVATE_FUTEX_FIRST_INDEX = 0ULL;

[[nodiscard]] PrivateFutexEntry EmptyPrivateFutexEntry() noexcept {
    return PrivateFutexEntry{
        .key = PrivateFutexKey{},
        .wait_queue = WaitQueue{},
        .active = false,
    };
}

}

PrivateFutexStatus PrivateFutexManager::Initialize(PrivateFutexEntry *const entry_storage,
                                                   const uint64_t capacity,
                                                   const uint64_t first_queue_identifier) noexcept {
    if (this->initialized_) {
        return PrivateFutexStatus::AlreadyInitialized;
    }
    if (entry_storage == nullptr) {
        return PrivateFutexStatus::InvalidStorage;
    }
    if (capacity == OS_KERNEL_PRIVATE_FUTEX_EMPTY_VALUE ||
        capacity > OS_KERNEL_PRIVATE_FUTEX_CAPACITY_LIMIT ||
        first_queue_identifier == OS_KERNEL_PRIVATE_FUTEX_EMPTY_VALUE ||
        first_queue_identifier > UINT64_MAX - capacity) {
        return PrivateFutexStatus::InvalidCapacity;
    }
    for (uint64_t entry_index = OS_KERNEL_PRIVATE_FUTEX_FIRST_INDEX; entry_index < capacity;
         ++entry_index) {
        entry_storage[entry_index] = EmptyPrivateFutexEntry();
    }
    this->entries_ = entry_storage;
    this->capacity_ = capacity;
    this->first_queue_identifier_ = first_queue_identifier;
    this->statistics_ = PrivateFutexStatistics{
        .capacity = capacity,
        .active_entry_count = OS_KERNEL_PRIVATE_FUTEX_EMPTY_VALUE,
        .waiting_thread_count = OS_KERNEL_PRIVATE_FUTEX_EMPTY_VALUE,
        .peak_active_entry_count = OS_KERNEL_PRIVATE_FUTEX_EMPTY_VALUE,
        .acquire_count = OS_KERNEL_PRIVATE_FUTEX_EMPTY_VALUE,
        .release_count = OS_KERNEL_PRIVATE_FUTEX_EMPTY_VALUE,
        .wait_prepare_count = OS_KERNEL_PRIVATE_FUTEX_EMPTY_VALUE,
        .wake_operation_count = OS_KERNEL_PRIVATE_FUTEX_EMPTY_VALUE,
        .cancellation_operation_count = OS_KERNEL_PRIVATE_FUTEX_EMPTY_VALUE,
    };
    this->initialized_ = true;
    return PrivateFutexStatus::Succeeded;
}

PrivateFutexStatus PrivateFutexManager::Acquire(const PrivateFutexKey &key, uint64_t &entry_index,
                                                WaitQueue *&wait_queue) noexcept {
    entry_index = OS_KERNEL_PRIVATE_FUTEX_INVALID_INDEX;
    wait_queue = nullptr;
    if (!this->initialized_) {
        return PrivateFutexStatus::NotInitialized;
    }
    if (!this->KeyIsValid(key)) {
        return key.address_space_identifier == OS_KERNEL_PRIVATE_FUTEX_EMPTY_VALUE
                   ? PrivateFutexStatus::InvalidAddressSpace
                   : PrivateFutexStatus::InvalidAddress;
    }
    const PrivateFutexStatus find_status = this->Find(key, entry_index, wait_queue);
    if (find_status == PrivateFutexStatus::Succeeded) {
        return PrivateFutexStatus::Succeeded;
    }
    if (find_status != PrivateFutexStatus::EntryNotFound) {
        return find_status;
    }
    for (uint64_t candidate_index = OS_KERNEL_PRIVATE_FUTEX_FIRST_INDEX;
         candidate_index < this->capacity_; ++candidate_index) {
        PrivateFutexEntry &candidate = this->entries_[candidate_index];
        if (candidate.active) {
            continue;
        }
        const WaitQueueStatus queue_status = candidate.wait_queue.Initialize(
            WaitQueueId{.value = this->first_queue_identifier_ + candidate_index});
        if (queue_status != WaitQueueStatus::Succeeded) {
            return PrivateFutexStatus::QueueFailure;
        }
        candidate.key = key;
        candidate.active = true;
        ++this->statistics_.active_entry_count;
        ++this->statistics_.acquire_count;
        if (this->statistics_.active_entry_count > this->statistics_.peak_active_entry_count) {
            this->statistics_.peak_active_entry_count = this->statistics_.active_entry_count;
        }
        entry_index = candidate_index;
        wait_queue = &candidate.wait_queue;
        return PrivateFutexStatus::Succeeded;
    }
    return PrivateFutexStatus::EntryCapacityExhausted;
}

PrivateFutexStatus PrivateFutexManager::Find(const PrivateFutexKey &key, uint64_t &entry_index,
                                             WaitQueue *&wait_queue) noexcept {
    entry_index = OS_KERNEL_PRIVATE_FUTEX_INVALID_INDEX;
    wait_queue = nullptr;
    if (!this->initialized_) {
        return PrivateFutexStatus::NotInitialized;
    }
    if (!this->KeyIsValid(key)) {
        return key.address_space_identifier == OS_KERNEL_PRIVATE_FUTEX_EMPTY_VALUE
                   ? PrivateFutexStatus::InvalidAddressSpace
                   : PrivateFutexStatus::InvalidAddress;
    }
    for (uint64_t candidate_index = OS_KERNEL_PRIVATE_FUTEX_FIRST_INDEX;
         candidate_index < this->capacity_; ++candidate_index) {
        PrivateFutexEntry &candidate = this->entries_[candidate_index];
        if (candidate.active && this->KeysEqual(candidate.key, key)) {
            entry_index = candidate_index;
            wait_queue = &candidate.wait_queue;
            return PrivateFutexStatus::Succeeded;
        }
    }
    return PrivateFutexStatus::EntryNotFound;
}

PrivateFutexStatus PrivateFutexManager::Read(const uint64_t entry_index,
                                             PrivateFutexEntry &entry) const noexcept {
    if (!this->initialized_) {
        return PrivateFutexStatus::NotInitialized;
    }
    if (entry_index >= this->capacity_) {
        return PrivateFutexStatus::EntryNotFound;
    }
    entry = this->entries_[entry_index];
    return entry.active ? PrivateFutexStatus::Succeeded : PrivateFutexStatus::EntryNotFound;
}

PrivateFutexStatus PrivateFutexManager::ReleaseIfEmpty(const uint64_t entry_index,
                                                       bool &released) noexcept {
    released = false;
    if (!this->initialized_) {
        return PrivateFutexStatus::NotInitialized;
    }
    if (entry_index >= this->capacity_ || !this->entries_[entry_index].active) {
        return PrivateFutexStatus::EntryNotFound;
    }
    PrivateFutexEntry &entry = this->entries_[entry_index];
    const WaitQueueStatistics queue_statistics = entry.wait_queue.Statistics();
    if (queue_statistics.waiting_thread_count != OS_KERNEL_PRIVATE_FUTEX_EMPTY_VALUE) {
        return PrivateFutexStatus::WaitersRemain;
    }
    if (entry.wait_queue.Reset() != WaitQueueStatus::Succeeded ||
        this->statistics_.active_entry_count == OS_KERNEL_PRIVATE_FUTEX_EMPTY_VALUE) {
        return PrivateFutexStatus::QueueFailure;
    }
    entry = EmptyPrivateFutexEntry();
    --this->statistics_.active_entry_count;
    ++this->statistics_.release_count;
    released = true;
    return PrivateFutexStatus::Succeeded;
}

PrivateFutexStatus PrivateFutexManager::RecordWaitPrepared() noexcept {
    if (!this->initialized_) {
        return PrivateFutexStatus::NotInitialized;
    }
    ++this->statistics_.wait_prepare_count;
    return PrivateFutexStatus::Succeeded;
}

PrivateFutexStatus PrivateFutexManager::RecordWakeOperation() noexcept {
    if (!this->initialized_) {
        return PrivateFutexStatus::NotInitialized;
    }
    ++this->statistics_.wake_operation_count;
    return PrivateFutexStatus::Succeeded;
}

PrivateFutexStatus PrivateFutexManager::RecordCancellationOperation() noexcept {
    if (!this->initialized_) {
        return PrivateFutexStatus::NotInitialized;
    }
    ++this->statistics_.cancellation_operation_count;
    return PrivateFutexStatus::Succeeded;
}

PrivateFutexStatus PrivateFutexManager::Validate() const noexcept {
    if (!this->initialized_) {
        return PrivateFutexStatus::NotInitialized;
    }
    uint64_t active_entry_count = OS_KERNEL_PRIVATE_FUTEX_EMPTY_VALUE;
    for (uint64_t entry_index = OS_KERNEL_PRIVATE_FUTEX_FIRST_INDEX; entry_index < this->capacity_;
         ++entry_index) {
        const PrivateFutexEntry &entry = this->entries_[entry_index];
        if (!entry.active) {
            if (entry.key.address_space_identifier != OS_KERNEL_PRIVATE_FUTEX_EMPTY_VALUE ||
                entry.key.user_address != OS_KERNEL_PRIVATE_FUTEX_EMPTY_VALUE ||
                entry.wait_queue.IsInitialized()) {
                return PrivateFutexStatus::Corrupt;
            }
            continue;
        }
        if (!this->KeyIsValid(entry.key) || !entry.wait_queue.IsInitialized() ||
            entry.wait_queue.IsClosed()) {
            return PrivateFutexStatus::Corrupt;
        }
        for (uint64_t previous_index = OS_KERNEL_PRIVATE_FUTEX_FIRST_INDEX;
             previous_index < entry_index; ++previous_index) {
            if (this->entries_[previous_index].active &&
                this->KeysEqual(this->entries_[previous_index].key, entry.key)) {
                return PrivateFutexStatus::Corrupt;
            }
        }
        ++active_entry_count;
    }
    if (active_entry_count != this->statistics_.active_entry_count ||
        active_entry_count > this->capacity_ ||
        this->statistics_.peak_active_entry_count < active_entry_count ||
        this->statistics_.acquire_count < this->statistics_.release_count ||
        this->statistics_.acquire_count - this->statistics_.release_count != active_entry_count) {
        return PrivateFutexStatus::Corrupt;
    }
    return PrivateFutexStatus::Succeeded;
}

PrivateFutexStatistics PrivateFutexManager::Statistics() const noexcept {
    PrivateFutexStatistics statistics = this->statistics_;
    statistics.waiting_thread_count = OS_KERNEL_PRIVATE_FUTEX_EMPTY_VALUE;
    if (!this->initialized_) {
        return statistics;
    }
    for (uint64_t entry_index = OS_KERNEL_PRIVATE_FUTEX_FIRST_INDEX; entry_index < this->capacity_;
         ++entry_index) {
        if (this->entries_[entry_index].active) {
            statistics.waiting_thread_count +=
                this->entries_[entry_index].wait_queue.Statistics().waiting_thread_count;
        }
    }
    return statistics;
}

bool PrivateFutexManager::KeyIsValid(const PrivateFutexKey &key) const noexcept {
    return key.address_space_identifier != OS_KERNEL_PRIVATE_FUTEX_EMPTY_VALUE &&
           key.user_address != OS_KERNEL_PRIVATE_FUTEX_EMPTY_VALUE &&
           key.user_address % OS_KERNEL_PRIVATE_FUTEX_WORD_ALIGNMENT_BYTES ==
               OS_KERNEL_PRIVATE_FUTEX_EMPTY_VALUE;
}

bool PrivateFutexManager::KeysEqual(const PrivateFutexKey &left,
                                    const PrivateFutexKey &right) const noexcept {
    return left.address_space_identifier == right.address_space_identifier &&
           left.user_address == right.user_address;
}

}

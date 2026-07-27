#include "os/kernel/time/deadline_queue.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_DEADLINE_QUEUE_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_DEADLINE_QUEUE_FIRST_SEQUENCE = 1ULL;
constexpr uint64_t OS_KERNEL_DEADLINE_QUEUE_COUNTER_INCREMENT = 1ULL;

}

DeadlineQueueStatus DeadlineQueue::Initialize(const uint64_t capacity) noexcept {
    if (this->initialized_) {
        return DeadlineQueueStatus::AlreadyInitialized;
    }
    if (capacity == OS_KERNEL_DEADLINE_QUEUE_EMPTY_VALUE ||
        capacity > OS_KERNEL_DEADLINE_QUEUE_CAPACITY_LIMIT) {
        return DeadlineQueueStatus::InvalidCapacity;
    }

    this->capacity_ = capacity;
    this->head_thread_index_ = OS_KERNEL_DEADLINE_QUEUE_INVALID_THREAD_INDEX;
    this->tail_thread_index_ = OS_KERNEL_DEADLINE_QUEUE_INVALID_THREAD_INDEX;
    this->statistics_ = DeadlineQueueStatistics{
        .capacity = capacity,
        .active_entry_count = OS_KERNEL_DEADLINE_QUEUE_EMPTY_VALUE,
        .peak_active_entry_count = OS_KERNEL_DEADLINE_QUEUE_EMPTY_VALUE,
        .schedule_count = OS_KERNEL_DEADLINE_QUEUE_EMPTY_VALUE,
        .expiration_count = OS_KERNEL_DEADLINE_QUEUE_EMPTY_VALUE,
        .cancellation_count = OS_KERNEL_DEADLINE_QUEUE_EMPTY_VALUE,
        .next_registration_sequence = OS_KERNEL_DEADLINE_QUEUE_FIRST_SEQUENCE,
    };
    for (uint64_t thread_index = OS_KERNEL_DEADLINE_QUEUE_EMPTY_VALUE;
         thread_index < OS_KERNEL_DEADLINE_QUEUE_CAPACITY_LIMIT; ++thread_index) {
        this->entries_[thread_index] = DeadlineEntry{
            .deadline_nanoseconds = OS_KERNEL_DEADLINE_QUEUE_EMPTY_VALUE,
            .registration_sequence = OS_KERNEL_DEADLINE_QUEUE_EMPTY_VALUE,
            .previous_thread_index = OS_KERNEL_DEADLINE_QUEUE_INVALID_THREAD_INDEX,
            .next_thread_index = OS_KERNEL_DEADLINE_QUEUE_INVALID_THREAD_INDEX,
            .active = false,
        };
    }
    this->initialized_ = true;
    return DeadlineQueueStatus::Succeeded;
}

DeadlineQueueStatus DeadlineQueue::Reset() noexcept {
    if (!this->initialized_) {
        return DeadlineQueueStatus::NotInitialized;
    }
    if (this->statistics_.active_entry_count !=
        OS_KERNEL_DEADLINE_QUEUE_EMPTY_VALUE) {
        return DeadlineQueueStatus::ActiveEntriesRemain;
    }
    this->capacity_ = OS_KERNEL_DEADLINE_QUEUE_EMPTY_VALUE;
    this->head_thread_index_ = OS_KERNEL_DEADLINE_QUEUE_INVALID_THREAD_INDEX;
    this->tail_thread_index_ = OS_KERNEL_DEADLINE_QUEUE_INVALID_THREAD_INDEX;
    this->statistics_ = DeadlineQueueStatistics{};
    this->initialized_ = false;
    return DeadlineQueueStatus::Succeeded;
}

DeadlineQueueStatus DeadlineQueue::Schedule(
    const uint64_t thread_index, const uint64_t deadline_nanoseconds) noexcept {
    if (!this->initialized_) {
        return DeadlineQueueStatus::NotInitialized;
    }
    if (thread_index >= this->capacity_) {
        return DeadlineQueueStatus::InvalidThreadIndex;
    }
    DeadlineEntry &entry = this->entries_[thread_index];
    if (entry.active) {
        return DeadlineQueueStatus::AlreadyScheduled;
    }
    if (this->statistics_.next_registration_sequence == UINT64_MAX) {
        return DeadlineQueueStatus::SequenceExhausted;
    }

    entry = DeadlineEntry{
        .deadline_nanoseconds = deadline_nanoseconds,
        .registration_sequence =
            this->statistics_.next_registration_sequence,
        .previous_thread_index = OS_KERNEL_DEADLINE_QUEUE_INVALID_THREAD_INDEX,
        .next_thread_index = OS_KERNEL_DEADLINE_QUEUE_INVALID_THREAD_INDEX,
        .active = true,
    };
    this->statistics_.next_registration_sequence +=
        OS_KERNEL_DEADLINE_QUEUE_COUNTER_INCREMENT;

    uint64_t insertion_thread_index = this->head_thread_index_;
    while (insertion_thread_index !=
               OS_KERNEL_DEADLINE_QUEUE_INVALID_THREAD_INDEX &&
           !this->EntryPrecedes(thread_index, insertion_thread_index)) {
        insertion_thread_index =
            this->entries_[insertion_thread_index].next_thread_index;
    }
    if (insertion_thread_index ==
        OS_KERNEL_DEADLINE_QUEUE_INVALID_THREAD_INDEX) {
        entry.previous_thread_index = this->tail_thread_index_;
        if (this->tail_thread_index_ !=
            OS_KERNEL_DEADLINE_QUEUE_INVALID_THREAD_INDEX) {
            this->entries_[this->tail_thread_index_].next_thread_index =
                thread_index;
        } else {
            this->head_thread_index_ = thread_index;
        }
        this->tail_thread_index_ = thread_index;
    } else {
        DeadlineEntry &insertion_entry =
            this->entries_[insertion_thread_index];
        entry.next_thread_index = insertion_thread_index;
        entry.previous_thread_index = insertion_entry.previous_thread_index;
        if (insertion_entry.previous_thread_index !=
            OS_KERNEL_DEADLINE_QUEUE_INVALID_THREAD_INDEX) {
            this->entries_[insertion_entry.previous_thread_index]
                .next_thread_index = thread_index;
        } else {
            this->head_thread_index_ = thread_index;
        }
        insertion_entry.previous_thread_index = thread_index;
    }

    this->statistics_.active_entry_count +=
        OS_KERNEL_DEADLINE_QUEUE_COUNTER_INCREMENT;
    this->statistics_.schedule_count +=
        OS_KERNEL_DEADLINE_QUEUE_COUNTER_INCREMENT;
    if (this->statistics_.active_entry_count >
        this->statistics_.peak_active_entry_count) {
        this->statistics_.peak_active_entry_count =
            this->statistics_.active_entry_count;
    }
    return DeadlineQueueStatus::Succeeded;
}

DeadlineQueueStatus DeadlineQueue::Resolve(
    const uint64_t thread_index, const DeadlineResolution resolution) noexcept {
    if (!this->initialized_) {
        return DeadlineQueueStatus::NotInitialized;
    }
    if (thread_index >= this->capacity_) {
        return DeadlineQueueStatus::InvalidThreadIndex;
    }
    if (!this->entries_[thread_index].active) {
        return DeadlineQueueStatus::EntryNotFound;
    }
    this->Remove(thread_index);
    if (resolution == DeadlineResolution::Expired) {
        this->statistics_.expiration_count +=
            OS_KERNEL_DEADLINE_QUEUE_COUNTER_INCREMENT;
    } else {
        this->statistics_.cancellation_count +=
            OS_KERNEL_DEADLINE_QUEUE_COUNTER_INCREMENT;
    }
    return DeadlineQueueStatus::Succeeded;
}

DeadlineQueueStatus DeadlineQueue::PeekExpired(
    const uint64_t now_nanoseconds, uint64_t &thread_index,
    bool &expired) const noexcept {
    thread_index = OS_KERNEL_DEADLINE_QUEUE_INVALID_THREAD_INDEX;
    expired = false;
    if (!this->initialized_) {
        return DeadlineQueueStatus::NotInitialized;
    }
    if (this->head_thread_index_ ==
        OS_KERNEL_DEADLINE_QUEUE_INVALID_THREAD_INDEX) {
        return DeadlineQueueStatus::Succeeded;
    }
    const DeadlineEntry &head_entry =
        this->entries_[this->head_thread_index_];
    if (!head_entry.active) {
        return DeadlineQueueStatus::Corrupt;
    }
    if (head_entry.deadline_nanoseconds <= now_nanoseconds) {
        thread_index = this->head_thread_index_;
        expired = true;
    }
    return DeadlineQueueStatus::Succeeded;
}

DeadlineQueueStatus DeadlineQueue::Contains(
    const uint64_t thread_index, bool &scheduled) const noexcept {
    scheduled = false;
    if (!this->initialized_) {
        return DeadlineQueueStatus::NotInitialized;
    }
    if (thread_index >= this->capacity_) {
        return DeadlineQueueStatus::InvalidThreadIndex;
    }
    scheduled = this->entries_[thread_index].active;
    return DeadlineQueueStatus::Succeeded;
}

DeadlineQueueStatus DeadlineQueue::Read(const uint64_t thread_index,
                                        DeadlineEntry &entry) const noexcept {
    entry = DeadlineEntry{};
    if (!this->initialized_) {
        return DeadlineQueueStatus::NotInitialized;
    }
    if (thread_index >= this->capacity_) {
        return DeadlineQueueStatus::InvalidThreadIndex;
    }
    if (!this->entries_[thread_index].active) {
        return DeadlineQueueStatus::EntryNotFound;
    }
    entry = this->entries_[thread_index];
    return DeadlineQueueStatus::Succeeded;
}

DeadlineQueueStatistics DeadlineQueue::Statistics() const noexcept {
    return this->statistics_;
}

DeadlineQueueStatus DeadlineQueue::Validate() const noexcept {
    if (!this->initialized_) {
        return DeadlineQueueStatus::NotInitialized;
    }
    if (this->capacity_ == OS_KERNEL_DEADLINE_QUEUE_EMPTY_VALUE ||
        this->capacity_ > OS_KERNEL_DEADLINE_QUEUE_CAPACITY_LIMIT ||
        this->statistics_.capacity != this->capacity_ ||
        this->statistics_.active_entry_count >
            this->statistics_.peak_active_entry_count ||
        this->statistics_.peak_active_entry_count > this->capacity_ ||
        this->statistics_.next_registration_sequence <
            OS_KERNEL_DEADLINE_QUEUE_FIRST_SEQUENCE ||
        this->statistics_.next_registration_sequence !=
            this->statistics_.schedule_count +
                OS_KERNEL_DEADLINE_QUEUE_FIRST_SEQUENCE ||
        this->statistics_.schedule_count !=
            this->statistics_.expiration_count +
                this->statistics_.cancellation_count +
                this->statistics_.active_entry_count) {
        return DeadlineQueueStatus::Corrupt;
    }

    uint64_t visited_entry_count = OS_KERNEL_DEADLINE_QUEUE_EMPTY_VALUE;
    uint64_t previous_thread_index =
        OS_KERNEL_DEADLINE_QUEUE_INVALID_THREAD_INDEX;
    uint64_t current_thread_index = this->head_thread_index_;
    while (current_thread_index !=
           OS_KERNEL_DEADLINE_QUEUE_INVALID_THREAD_INDEX) {
        if (current_thread_index >= this->capacity_ ||
            visited_entry_count >= this->capacity_) {
            return DeadlineQueueStatus::Corrupt;
        }
        const DeadlineEntry &entry = this->entries_[current_thread_index];
        if (!entry.active ||
            entry.registration_sequence <
                OS_KERNEL_DEADLINE_QUEUE_FIRST_SEQUENCE ||
            entry.registration_sequence >=
                this->statistics_.next_registration_sequence ||
            entry.previous_thread_index != previous_thread_index) {
            return DeadlineQueueStatus::Corrupt;
        }
        if (previous_thread_index !=
                OS_KERNEL_DEADLINE_QUEUE_INVALID_THREAD_INDEX &&
            !this->EntryPrecedes(previous_thread_index,
                                 current_thread_index)) {
            return DeadlineQueueStatus::Corrupt;
        }
        previous_thread_index = current_thread_index;
        current_thread_index = entry.next_thread_index;
        visited_entry_count += OS_KERNEL_DEADLINE_QUEUE_COUNTER_INCREMENT;
    }
    if (visited_entry_count != this->statistics_.active_entry_count ||
        previous_thread_index != this->tail_thread_index_ ||
        ((visited_entry_count == OS_KERNEL_DEADLINE_QUEUE_EMPTY_VALUE) !=
         (this->head_thread_index_ ==
              OS_KERNEL_DEADLINE_QUEUE_INVALID_THREAD_INDEX &&
          this->tail_thread_index_ ==
              OS_KERNEL_DEADLINE_QUEUE_INVALID_THREAD_INDEX))) {
        return DeadlineQueueStatus::Corrupt;
    }

    uint64_t active_entry_count = OS_KERNEL_DEADLINE_QUEUE_EMPTY_VALUE;
    for (uint64_t thread_index = OS_KERNEL_DEADLINE_QUEUE_EMPTY_VALUE;
         thread_index < this->capacity_; ++thread_index) {
        const DeadlineEntry &entry = this->entries_[thread_index];
        if (entry.active) {
            active_entry_count += OS_KERNEL_DEADLINE_QUEUE_COUNTER_INCREMENT;
        } else if (
            entry.deadline_nanoseconds !=
                OS_KERNEL_DEADLINE_QUEUE_EMPTY_VALUE ||
            entry.registration_sequence !=
                OS_KERNEL_DEADLINE_QUEUE_EMPTY_VALUE ||
            entry.previous_thread_index !=
                OS_KERNEL_DEADLINE_QUEUE_INVALID_THREAD_INDEX ||
            entry.next_thread_index !=
                OS_KERNEL_DEADLINE_QUEUE_INVALID_THREAD_INDEX) {
            return DeadlineQueueStatus::Corrupt;
        }
    }
    return active_entry_count == visited_entry_count
               ? DeadlineQueueStatus::Succeeded
               : DeadlineQueueStatus::Corrupt;
}

bool DeadlineQueue::EntryPrecedes(
    const uint64_t left_thread_index,
    const uint64_t right_thread_index) const noexcept {
    const DeadlineEntry &left_entry = this->entries_[left_thread_index];
    const DeadlineEntry &right_entry = this->entries_[right_thread_index];
    return left_entry.deadline_nanoseconds <
               right_entry.deadline_nanoseconds ||
           (left_entry.deadline_nanoseconds ==
                right_entry.deadline_nanoseconds &&
            left_entry.registration_sequence <
                right_entry.registration_sequence);
}

void DeadlineQueue::Remove(const uint64_t thread_index) noexcept {
    DeadlineEntry &entry = this->entries_[thread_index];
    if (entry.previous_thread_index !=
        OS_KERNEL_DEADLINE_QUEUE_INVALID_THREAD_INDEX) {
        this->entries_[entry.previous_thread_index].next_thread_index =
            entry.next_thread_index;
    } else {
        this->head_thread_index_ = entry.next_thread_index;
    }
    if (entry.next_thread_index !=
        OS_KERNEL_DEADLINE_QUEUE_INVALID_THREAD_INDEX) {
        this->entries_[entry.next_thread_index].previous_thread_index =
            entry.previous_thread_index;
    } else {
        this->tail_thread_index_ = entry.previous_thread_index;
    }
    entry = DeadlineEntry{
        .deadline_nanoseconds = OS_KERNEL_DEADLINE_QUEUE_EMPTY_VALUE,
        .registration_sequence = OS_KERNEL_DEADLINE_QUEUE_EMPTY_VALUE,
        .previous_thread_index = OS_KERNEL_DEADLINE_QUEUE_INVALID_THREAD_INDEX,
        .next_thread_index = OS_KERNEL_DEADLINE_QUEUE_INVALID_THREAD_INDEX,
        .active = false,
    };
    this->statistics_.active_entry_count -=
        OS_KERNEL_DEADLINE_QUEUE_COUNTER_INCREMENT;
}

}

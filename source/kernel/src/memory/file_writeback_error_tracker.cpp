#include <os/kernel/memory/file_writeback_error_tracker.hpp>

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_FILE_WRITEBACK_ERROR_TRACKER_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_FILE_WRITEBACK_ERROR_TRACKER_RECORD_ALIGNMENT_BYTES = 64ULL;

}

struct alignas(OS_KERNEL_FILE_WRITEBACK_ERROR_TRACKER_RECORD_ALIGNMENT_BYTES)
    FileWritebackErrorTracker::ErrorRecord final {
    FileCacheIdentity identity;
    uint64_t open_description_count;
    uint64_t sequence;
    FileWritebackError latest_error;
    ErrorRecord *next;
};

FileWritebackErrorTrackerStatus
FileWritebackErrorTracker::Initialize(KernelHeap &heap) noexcept {
    SpinLockGuard guard{this->lock_};
    if (this->initialized_) {
        return FileWritebackErrorTrackerStatus::AlreadyInitialized;
    }
    if (heap.Validate() != KernelHeapStatus::Succeeded) {
        return FileWritebackErrorTrackerStatus::AllocationFailed;
    }
    this->heap_ = &heap;
    this->records_ = nullptr;
    this->statistics_ = FileWritebackErrorTrackerStatistics{};
    this->initialized_ = true;
    return FileWritebackErrorTrackerStatus::Succeeded;
}

FileWritebackErrorTrackerStatus
FileWritebackErrorTracker::Register(const FileCacheIdentity &identity,
                                    uint64_t &sampled_sequence) noexcept {
    SpinLockGuard guard{this->lock_};
    sampled_sequence = OS_KERNEL_FILE_WRITEBACK_ERROR_TRACKER_EMPTY_VALUE;
    if (!this->initialized_ || this->heap_ == nullptr) {
        return FileWritebackErrorTrackerStatus::NotInitialized;
    }
    if (!FileCacheIdentityIsValid(identity)) {
        return FileWritebackErrorTrackerStatus::InvalidIdentity;
    }
    if (this->statistics_.active_open_description_count == UINT64_MAX ||
        this->statistics_.registration_count == UINT64_MAX) {
        return FileWritebackErrorTrackerStatus::ReferenceOverflow;
    }
    ErrorRecord *record = this->Find(identity);
    if (record == nullptr) {
        if (this->statistics_.active_record_count == UINT64_MAX) {
            return FileWritebackErrorTrackerStatus::ReferenceOverflow;
        }
        void *allocation = nullptr;
        if (this->heap_->TryAllocate(sizeof(ErrorRecord), alignof(ErrorRecord), allocation) !=
            KernelHeapStatus::Succeeded) {
            return FileWritebackErrorTrackerStatus::AllocationFailed;
        }
        record = static_cast<ErrorRecord *>(allocation);
        *record = ErrorRecord{
            .identity = identity,
            .open_description_count = OS_KERNEL_FILE_WRITEBACK_ERROR_TRACKER_EMPTY_VALUE,
            .sequence = OS_KERNEL_FILE_WRITEBACK_ERROR_TRACKER_EMPTY_VALUE,
            .latest_error = FileWritebackError::None,
            .next = this->records_,
        };
        this->records_ = record;
        ++this->statistics_.active_record_count;
        if (this->statistics_.peak_record_count < this->statistics_.active_record_count) {
            this->statistics_.peak_record_count = this->statistics_.active_record_count;
        }
    }
    if (record->open_description_count == UINT64_MAX) {
        return FileWritebackErrorTrackerStatus::ReferenceOverflow;
    }
    sampled_sequence = record->sequence;
    ++record->open_description_count;
    ++this->statistics_.active_open_description_count;
    ++this->statistics_.registration_count;
    if (this->statistics_.peak_open_description_count <
        this->statistics_.active_open_description_count) {
        this->statistics_.peak_open_description_count =
            this->statistics_.active_open_description_count;
    }
    return FileWritebackErrorTrackerStatus::Succeeded;
}

FileWritebackErrorTrackerStatus
FileWritebackErrorTracker::Unregister(const FileCacheIdentity &identity) noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_ || this->heap_ == nullptr) {
        return FileWritebackErrorTrackerStatus::NotInitialized;
    }
    if (!FileCacheIdentityIsValid(identity)) {
        return FileWritebackErrorTrackerStatus::InvalidIdentity;
    }
    ErrorRecord *previous = nullptr;
    ErrorRecord *record = this->records_;
    while (record != nullptr && !FileCacheIdentitiesEqual(record->identity, identity)) {
        previous = record;
        record = record->next;
    }
    if (record == nullptr) {
        return FileWritebackErrorTrackerStatus::NotFound;
    }
    if (record->open_description_count ==
            OS_KERNEL_FILE_WRITEBACK_ERROR_TRACKER_EMPTY_VALUE ||
        this->statistics_.active_open_description_count ==
            OS_KERNEL_FILE_WRITEBACK_ERROR_TRACKER_EMPTY_VALUE ||
        this->statistics_.unregistration_count == UINT64_MAX) {
        return FileWritebackErrorTrackerStatus::ReferenceUnderflow;
    }
    --record->open_description_count;
    --this->statistics_.active_open_description_count;
    ++this->statistics_.unregistration_count;
    if (record->open_description_count != OS_KERNEL_FILE_WRITEBACK_ERROR_TRACKER_EMPTY_VALUE) {
        return FileWritebackErrorTrackerStatus::Succeeded;
    }
    if (previous == nullptr) {
        this->records_ = record->next;
    } else {
        previous->next = record->next;
    }
    if (this->heap_->TryRelease(record) != KernelHeapStatus::Succeeded ||
        this->statistics_.active_record_count ==
            OS_KERNEL_FILE_WRITEBACK_ERROR_TRACKER_EMPTY_VALUE) {
        return FileWritebackErrorTrackerStatus::MetadataReleaseFailed;
    }
    --this->statistics_.active_record_count;
    return FileWritebackErrorTrackerStatus::Succeeded;
}

FileWritebackErrorTrackerStatus
FileWritebackErrorTracker::Record(const FileCacheIdentity &identity,
                                  const FileWritebackError error) noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_) {
        return FileWritebackErrorTrackerStatus::NotInitialized;
    }
    if (!FileCacheIdentityIsValid(identity)) {
        return FileWritebackErrorTrackerStatus::InvalidIdentity;
    }
    if (error == FileWritebackError::None) {
        return FileWritebackErrorTrackerStatus::InvalidError;
    }
    ErrorRecord *const record = this->Find(identity);
    if (record == nullptr) {
        if (this->statistics_.unobserved_error_count != UINT64_MAX) {
            ++this->statistics_.unobserved_error_count;
        }
        return FileWritebackErrorTrackerStatus::Succeeded;
    }
    if (record->sequence == UINT64_MAX || this->statistics_.recorded_error_count == UINT64_MAX) {
        return FileWritebackErrorTrackerStatus::SequenceOverflow;
    }
    ++record->sequence;
    record->latest_error = error;
    ++this->statistics_.recorded_error_count;
    return FileWritebackErrorTrackerStatus::Succeeded;
}

FileWritebackErrorTrackerStatus
FileWritebackErrorTracker::Check(const FileCacheIdentity &identity,
                                 const uint64_t sampled_sequence, uint64_t &current_sequence,
                                 FileWritebackError &error) noexcept {
    SpinLockGuard guard{this->lock_};
    current_sequence = OS_KERNEL_FILE_WRITEBACK_ERROR_TRACKER_EMPTY_VALUE;
    error = FileWritebackError::None;
    if (!this->initialized_) {
        return FileWritebackErrorTrackerStatus::NotInitialized;
    }
    if (!FileCacheIdentityIsValid(identity)) {
        return FileWritebackErrorTrackerStatus::InvalidIdentity;
    }
    const ErrorRecord *const record = this->Find(identity);
    if (record == nullptr) {
        return FileWritebackErrorTrackerStatus::NotFound;
    }
    if (sampled_sequence > record->sequence ||
        (record->sequence != OS_KERNEL_FILE_WRITEBACK_ERROR_TRACKER_EMPTY_VALUE &&
         record->latest_error == FileWritebackError::None)) {
        return FileWritebackErrorTrackerStatus::Corrupt;
    }
    current_sequence = record->sequence;
    if (sampled_sequence != record->sequence) {
        if (this->statistics_.reported_error_count == UINT64_MAX) {
            return FileWritebackErrorTrackerStatus::SequenceOverflow;
        }
        error = record->latest_error;
        ++this->statistics_.reported_error_count;
    }
    return FileWritebackErrorTrackerStatus::Succeeded;
}

FileWritebackErrorTrackerStatistics FileWritebackErrorTracker::Statistics() const noexcept {
    SpinLockGuard guard{this->lock_};
    return this->initialized_ ? this->statistics_ : FileWritebackErrorTrackerStatistics{};
}

FileWritebackErrorTrackerStatus FileWritebackErrorTracker::Validate() const noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_ || this->heap_ == nullptr) {
        return FileWritebackErrorTrackerStatus::NotInitialized;
    }
    uint64_t record_count = OS_KERNEL_FILE_WRITEBACK_ERROR_TRACKER_EMPTY_VALUE;
    uint64_t open_description_count = OS_KERNEL_FILE_WRITEBACK_ERROR_TRACKER_EMPTY_VALUE;
    for (const ErrorRecord *record = this->records_; record != nullptr; record = record->next) {
        if (!FileCacheIdentityIsValid(record->identity) ||
            record->open_description_count ==
                OS_KERNEL_FILE_WRITEBACK_ERROR_TRACKER_EMPTY_VALUE ||
            (record->sequence == OS_KERNEL_FILE_WRITEBACK_ERROR_TRACKER_EMPTY_VALUE &&
             record->latest_error != FileWritebackError::None) ||
            (record->sequence != OS_KERNEL_FILE_WRITEBACK_ERROR_TRACKER_EMPTY_VALUE &&
             record->latest_error == FileWritebackError::None) ||
            open_description_count > UINT64_MAX - record->open_description_count) {
            return FileWritebackErrorTrackerStatus::Corrupt;
        }
        for (const ErrorRecord *comparison = record->next; comparison != nullptr;
             comparison = comparison->next) {
            if (FileCacheIdentitiesEqual(record->identity, comparison->identity)) {
                return FileWritebackErrorTrackerStatus::Corrupt;
            }
        }
        ++record_count;
        open_description_count += record->open_description_count;
    }
    return record_count == this->statistics_.active_record_count &&
                   open_description_count == this->statistics_.active_open_description_count &&
                   this->statistics_.peak_record_count >= record_count &&
                   this->statistics_.peak_open_description_count >= open_description_count &&
                   this->statistics_.registration_count ==
                       this->statistics_.unregistration_count + open_description_count
               ? FileWritebackErrorTrackerStatus::Succeeded
               : FileWritebackErrorTrackerStatus::Corrupt;
}

FileWritebackErrorTrackerStatus FileWritebackErrorTracker::Destroy() noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_) {
        return FileWritebackErrorTrackerStatus::NotInitialized;
    }
    if (this->records_ != nullptr ||
        this->statistics_.active_record_count !=
            OS_KERNEL_FILE_WRITEBACK_ERROR_TRACKER_EMPTY_VALUE ||
        this->statistics_.active_open_description_count !=
            OS_KERNEL_FILE_WRITEBACK_ERROR_TRACKER_EMPTY_VALUE) {
        return FileWritebackErrorTrackerStatus::RecordsRemain;
    }
    this->heap_ = nullptr;
    this->statistics_ = FileWritebackErrorTrackerStatistics{};
    this->initialized_ = false;
    return FileWritebackErrorTrackerStatus::Succeeded;
}

FileWritebackErrorTracker::ErrorRecord *
FileWritebackErrorTracker::Find(const FileCacheIdentity &identity) noexcept {
    for (ErrorRecord *record = this->records_; record != nullptr; record = record->next) {
        if (FileCacheIdentitiesEqual(record->identity, identity)) {
            return record;
        }
    }
    return nullptr;
}

const FileWritebackErrorTracker::ErrorRecord *
FileWritebackErrorTracker::Find(const FileCacheIdentity &identity) const noexcept {
    for (const ErrorRecord *record = this->records_; record != nullptr; record = record->next) {
        if (FileCacheIdentitiesEqual(record->identity, identity)) {
            return record;
        }
    }
    return nullptr;
}

}

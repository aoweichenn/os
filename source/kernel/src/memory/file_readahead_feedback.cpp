#include <os/kernel/memory/file_readahead_feedback.hpp>

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_FILE_READAHEAD_FEEDBACK_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_FILE_READAHEAD_FEEDBACK_FIRST_GENERATION = 1ULL;

}

FileReadaheadFeedbackStatus
FileReadaheadFeedbackLedger::Initialize(FileReadaheadFeedbackSlot *const slot_storage,
                                        const uint64_t capacity) noexcept {
    SpinLockGuard guard{this->lock_};
    if (this->initialized_) {
        return FileReadaheadFeedbackStatus::AlreadyInitialized;
    }
    if (slot_storage == nullptr) {
        return FileReadaheadFeedbackStatus::InvalidStorage;
    }
    if (capacity == OS_KERNEL_FILE_READAHEAD_FEEDBACK_EMPTY_VALUE) {
        return FileReadaheadFeedbackStatus::InvalidCapacity;
    }
    for (uint64_t slot_index = OS_KERNEL_FILE_READAHEAD_FEEDBACK_EMPTY_VALUE; slot_index < capacity;
         ++slot_index) {
        slot_storage[slot_index] = FileReadaheadFeedbackSlot{};
    }
    this->slots_ = slot_storage;
    this->capacity_ = capacity;
    this->statistics_ = FileReadaheadFeedbackStatistics{};
    this->statistics_.capacity = capacity;
    this->initialized_ = true;
    return FileReadaheadFeedbackStatus::Succeeded;
}

FileReadaheadFeedbackStatus
FileReadaheadFeedbackLedger::RegisterStream(const FileCacheIdentity &file_identity,
                                            FileReadaheadStreamToken &token) noexcept {
    SpinLockGuard guard{this->lock_};
    token = FileReadaheadStreamToken{};
    if (!this->initialized_ || this->slots_ == nullptr) {
        return FileReadaheadFeedbackStatus::NotInitialized;
    }
    if (!FileCacheIdentityIsValid(file_identity)) {
        return FileReadaheadFeedbackStatus::InvalidIdentity;
    }
    const uint64_t slot_index = this->FindFreeSlotIndex();
    if (slot_index == OS_KERNEL_FILE_READAHEAD_STREAM_INVALID_SLOT_INDEX) {
        return FileReadaheadFeedbackStatus::CapacityExhausted;
    }
    FileReadaheadFeedbackSlot &slot = this->slots_[slot_index];
    if (slot.generation == UINT64_MAX) {
        return FileReadaheadFeedbackStatus::GenerationExhausted;
    }
    if (this->statistics_.active_stream_count == UINT64_MAX ||
        this->statistics_.registration_count == UINT64_MAX) {
        return FileReadaheadFeedbackStatus::CounterOverflow;
    }
    const uint64_t generation =
        slot.generation == OS_KERNEL_FILE_READAHEAD_FEEDBACK_EMPTY_VALUE
            ? OS_KERNEL_FILE_READAHEAD_FEEDBACK_FIRST_GENERATION
            : slot.generation + OS_KERNEL_FILE_READAHEAD_FEEDBACK_FIRST_GENERATION;
    slot = FileReadaheadFeedbackSlot{
        .file_identity = file_identity,
        .pending_feedback = FileReadaheadFeedback{},
        .active_task_count = OS_KERNEL_FILE_READAHEAD_FEEDBACK_EMPTY_VALUE,
        .generation = generation,
        .state = FileReadaheadFeedbackSlotState::Active,
    };
    ++this->statistics_.active_stream_count;
    ++this->statistics_.registration_count;
    const uint64_t stream_count =
        this->statistics_.active_stream_count + this->statistics_.retiring_stream_count;
    if (stream_count > this->statistics_.peak_stream_count) {
        this->statistics_.peak_stream_count = stream_count;
    }
    token = FileReadaheadStreamToken{
        .slot_index = slot_index,
        .generation = generation,
    };
    return FileReadaheadFeedbackStatus::Succeeded;
}

FileReadaheadFeedbackStatus
FileReadaheadFeedbackLedger::RetainTask(const FileReadaheadStreamToken token) noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->TokenIsValid(token)) {
        return FileReadaheadFeedbackStatus::InvalidToken;
    }
    FileReadaheadFeedbackSlot &slot = this->slots_[token.slot_index];
    if (slot.state != FileReadaheadFeedbackSlotState::Active) {
        return FileReadaheadFeedbackStatus::InvalidState;
    }
    if (slot.active_task_count == UINT64_MAX || this->statistics_.active_task_count == UINT64_MAX ||
        this->statistics_.task_retain_count == UINT64_MAX) {
        return FileReadaheadFeedbackStatus::CounterOverflow;
    }
    ++slot.active_task_count;
    ++this->statistics_.active_task_count;
    ++this->statistics_.task_retain_count;
    if (this->statistics_.active_task_count > this->statistics_.peak_active_task_count) {
        this->statistics_.peak_active_task_count = this->statistics_.active_task_count;
    }
    return FileReadaheadFeedbackStatus::Succeeded;
}

FileReadaheadFeedbackStatus
FileReadaheadFeedbackLedger::ReleaseTask(const FileReadaheadStreamToken token) noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->TokenIsValid(token)) {
        return FileReadaheadFeedbackStatus::InvalidToken;
    }
    FileReadaheadFeedbackSlot &slot = this->slots_[token.slot_index];
    if ((slot.state != FileReadaheadFeedbackSlotState::Active &&
         slot.state != FileReadaheadFeedbackSlotState::Retiring) ||
        slot.active_task_count == OS_KERNEL_FILE_READAHEAD_FEEDBACK_EMPTY_VALUE ||
        this->statistics_.active_task_count == OS_KERNEL_FILE_READAHEAD_FEEDBACK_EMPTY_VALUE ||
        this->statistics_.task_release_count == UINT64_MAX) {
        return FileReadaheadFeedbackStatus::InvalidState;
    }
    if (slot.state == FileReadaheadFeedbackSlotState::Retiring &&
        slot.active_task_count == OS_KERNEL_FILE_READAHEAD_FEEDBACK_FIRST_GENERATION &&
        !this->CanReleaseRetiredSlot(FileReadaheadFeedbackSlot{
            .file_identity = slot.file_identity,
            .pending_feedback = slot.pending_feedback,
            .active_task_count = OS_KERNEL_FILE_READAHEAD_FEEDBACK_EMPTY_VALUE,
            .generation = slot.generation,
            .state = slot.state,
        })) {
        return FileReadaheadFeedbackStatus::CounterOverflow;
    }
    --slot.active_task_count;
    --this->statistics_.active_task_count;
    ++this->statistics_.task_release_count;
    if (slot.state == FileReadaheadFeedbackSlotState::Retiring &&
        slot.active_task_count == OS_KERNEL_FILE_READAHEAD_FEEDBACK_EMPTY_VALUE &&
        !this->ReleaseRetiredSlot(slot)) {
        return FileReadaheadFeedbackStatus::CounterOverflow;
    }
    return FileReadaheadFeedbackStatus::Succeeded;
}

FileReadaheadFeedbackStatus
FileReadaheadFeedbackLedger::Record(const FileReadaheadStreamToken token,
                                    const FileReadaheadFeedback &feedback) noexcept {
    SpinLockGuard guard{this->lock_};
    if (feedback.useful_page_count == OS_KERNEL_FILE_READAHEAD_FEEDBACK_EMPTY_VALUE &&
        feedback.wasted_page_count == OS_KERNEL_FILE_READAHEAD_FEEDBACK_EMPTY_VALUE) {
        return FileReadaheadFeedbackStatus::InvalidFeedback;
    }
    if (!this->TokenIsValid(token)) {
        if (this->statistics_.useful_page_record_count > UINT64_MAX - feedback.useful_page_count ||
            this->statistics_.wasted_page_record_count > UINT64_MAX - feedback.wasted_page_count ||
            this->statistics_.stale_useful_page_drop_count >
                UINT64_MAX - feedback.useful_page_count ||
            this->statistics_.stale_wasted_page_drop_count >
                UINT64_MAX - feedback.wasted_page_count) {
            return FileReadaheadFeedbackStatus::CounterOverflow;
        }
        this->statistics_.useful_page_record_count += feedback.useful_page_count;
        this->statistics_.wasted_page_record_count += feedback.wasted_page_count;
        this->statistics_.stale_useful_page_drop_count += feedback.useful_page_count;
        this->statistics_.stale_wasted_page_drop_count += feedback.wasted_page_count;
        return FileReadaheadFeedbackStatus::Succeeded;
    }
    FileReadaheadFeedbackSlot &slot = this->slots_[token.slot_index];
    if (slot.state != FileReadaheadFeedbackSlotState::Active &&
        slot.state != FileReadaheadFeedbackSlotState::Retiring) {
        return FileReadaheadFeedbackStatus::InvalidState;
    }
    if (slot.pending_feedback.useful_page_count > UINT64_MAX - feedback.useful_page_count ||
        slot.pending_feedback.wasted_page_count > UINT64_MAX - feedback.wasted_page_count ||
        this->statistics_.useful_page_record_count > UINT64_MAX - feedback.useful_page_count ||
        this->statistics_.wasted_page_record_count > UINT64_MAX - feedback.wasted_page_count) {
        return FileReadaheadFeedbackStatus::CounterOverflow;
    }
    slot.pending_feedback.useful_page_count += feedback.useful_page_count;
    slot.pending_feedback.wasted_page_count += feedback.wasted_page_count;
    this->statistics_.useful_page_record_count += feedback.useful_page_count;
    this->statistics_.wasted_page_record_count += feedback.wasted_page_count;
    return FileReadaheadFeedbackStatus::Succeeded;
}

FileReadaheadFeedbackStatus
FileReadaheadFeedbackLedger::Take(const FileReadaheadStreamToken token,
                                  FileReadaheadFeedback &feedback) noexcept {
    SpinLockGuard guard{this->lock_};
    feedback = FileReadaheadFeedback{};
    if (!this->TokenIsValid(token)) {
        return FileReadaheadFeedbackStatus::InvalidToken;
    }
    FileReadaheadFeedbackSlot &slot = this->slots_[token.slot_index];
    if (slot.state != FileReadaheadFeedbackSlotState::Active) {
        return FileReadaheadFeedbackStatus::InvalidState;
    }
    if ((slot.pending_feedback.useful_page_count != OS_KERNEL_FILE_READAHEAD_FEEDBACK_EMPTY_VALUE ||
         slot.pending_feedback.wasted_page_count !=
             OS_KERNEL_FILE_READAHEAD_FEEDBACK_EMPTY_VALUE) &&
        (this->statistics_.feedback_take_count == UINT64_MAX ||
         this->statistics_.useful_page_take_count >
             UINT64_MAX - slot.pending_feedback.useful_page_count ||
         this->statistics_.wasted_page_take_count >
             UINT64_MAX - slot.pending_feedback.wasted_page_count)) {
        return FileReadaheadFeedbackStatus::CounterOverflow;
    }
    feedback = slot.pending_feedback;
    slot.pending_feedback = FileReadaheadFeedback{};
    if (feedback.useful_page_count != OS_KERNEL_FILE_READAHEAD_FEEDBACK_EMPTY_VALUE ||
        feedback.wasted_page_count != OS_KERNEL_FILE_READAHEAD_FEEDBACK_EMPTY_VALUE) {
        ++this->statistics_.feedback_take_count;
        this->statistics_.useful_page_take_count += feedback.useful_page_count;
        this->statistics_.wasted_page_take_count += feedback.wasted_page_count;
    }
    return FileReadaheadFeedbackStatus::Succeeded;
}

FileReadaheadFeedbackStatus
FileReadaheadFeedbackLedger::RetireStream(const FileReadaheadStreamToken token) noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->TokenIsValid(token)) {
        return FileReadaheadFeedbackStatus::InvalidToken;
    }
    FileReadaheadFeedbackSlot &slot = this->slots_[token.slot_index];
    if (slot.state != FileReadaheadFeedbackSlotState::Active ||
        this->statistics_.active_stream_count == OS_KERNEL_FILE_READAHEAD_FEEDBACK_EMPTY_VALUE ||
        this->statistics_.retirement_count == UINT64_MAX) {
        return FileReadaheadFeedbackStatus::InvalidState;
    }
    if (slot.active_task_count == OS_KERNEL_FILE_READAHEAD_FEEDBACK_EMPTY_VALUE &&
        (this->statistics_.stream_release_count == UINT64_MAX ||
         this->statistics_.stale_useful_page_drop_count >
             UINT64_MAX - slot.pending_feedback.useful_page_count ||
         this->statistics_.stale_wasted_page_drop_count >
             UINT64_MAX - slot.pending_feedback.wasted_page_count)) {
        return FileReadaheadFeedbackStatus::CounterOverflow;
    }
    --this->statistics_.active_stream_count;
    ++this->statistics_.retirement_count;
    slot.state = FileReadaheadFeedbackSlotState::Retiring;
    ++this->statistics_.retiring_stream_count;
    if (slot.active_task_count == OS_KERNEL_FILE_READAHEAD_FEEDBACK_EMPTY_VALUE &&
        !this->ReleaseRetiredSlot(slot)) {
        return FileReadaheadFeedbackStatus::CounterOverflow;
    }
    return FileReadaheadFeedbackStatus::Succeeded;
}

FileReadaheadFeedbackStatistics FileReadaheadFeedbackLedger::Statistics() const noexcept {
    SpinLockGuard guard{this->lock_};
    return this->statistics_;
}

FileReadaheadFeedbackStatus FileReadaheadFeedbackLedger::Validate() const noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_ || this->slots_ == nullptr ||
        this->capacity_ == OS_KERNEL_FILE_READAHEAD_FEEDBACK_EMPTY_VALUE) {
        return FileReadaheadFeedbackStatus::NotInitialized;
    }
    uint64_t active_stream_count = OS_KERNEL_FILE_READAHEAD_FEEDBACK_EMPTY_VALUE;
    uint64_t retiring_stream_count = OS_KERNEL_FILE_READAHEAD_FEEDBACK_EMPTY_VALUE;
    uint64_t active_task_count = OS_KERNEL_FILE_READAHEAD_FEEDBACK_EMPTY_VALUE;
    uint64_t pending_useful_page_count = OS_KERNEL_FILE_READAHEAD_FEEDBACK_EMPTY_VALUE;
    uint64_t pending_wasted_page_count = OS_KERNEL_FILE_READAHEAD_FEEDBACK_EMPTY_VALUE;
    for (uint64_t slot_index = OS_KERNEL_FILE_READAHEAD_FEEDBACK_EMPTY_VALUE;
         slot_index < this->capacity_; ++slot_index) {
        const FileReadaheadFeedbackSlot &slot = this->slots_[slot_index];
        if (slot.state == FileReadaheadFeedbackSlotState::Free) {
            if (FileCacheIdentityIsValid(slot.file_identity) ||
                slot.pending_feedback.useful_page_count !=
                    OS_KERNEL_FILE_READAHEAD_FEEDBACK_EMPTY_VALUE ||
                slot.pending_feedback.wasted_page_count !=
                    OS_KERNEL_FILE_READAHEAD_FEEDBACK_EMPTY_VALUE ||
                slot.active_task_count != OS_KERNEL_FILE_READAHEAD_FEEDBACK_EMPTY_VALUE) {
                return FileReadaheadFeedbackStatus::Corrupt;
            }
            continue;
        }
        if (!FileCacheIdentityIsValid(slot.file_identity) ||
            slot.generation == OS_KERNEL_FILE_READAHEAD_FEEDBACK_EMPTY_VALUE ||
            active_task_count > UINT64_MAX - slot.active_task_count ||
            pending_useful_page_count > UINT64_MAX - slot.pending_feedback.useful_page_count ||
            pending_wasted_page_count > UINT64_MAX - slot.pending_feedback.wasted_page_count) {
            return FileReadaheadFeedbackStatus::Corrupt;
        }
        active_task_count += slot.active_task_count;
        pending_useful_page_count += slot.pending_feedback.useful_page_count;
        pending_wasted_page_count += slot.pending_feedback.wasted_page_count;
        if (slot.state == FileReadaheadFeedbackSlotState::Active) {
            ++active_stream_count;
        } else if (slot.state == FileReadaheadFeedbackSlotState::Retiring) {
            if (slot.active_task_count == OS_KERNEL_FILE_READAHEAD_FEEDBACK_EMPTY_VALUE) {
                return FileReadaheadFeedbackStatus::Corrupt;
            }
            ++retiring_stream_count;
        } else {
            return FileReadaheadFeedbackStatus::Corrupt;
        }
    }
    const uint64_t stream_count = active_stream_count + retiring_stream_count;
    return active_stream_count == this->statistics_.active_stream_count &&
                   retiring_stream_count == this->statistics_.retiring_stream_count &&
                   active_task_count == this->statistics_.active_task_count &&
                   stream_count <= this->capacity_ &&
                   this->statistics_.peak_stream_count >= stream_count &&
                   this->statistics_.peak_active_task_count >= active_task_count &&
                   this->statistics_.registration_count ==
                       this->statistics_.stream_release_count + stream_count &&
                   this->statistics_.retirement_count ==
                       this->statistics_.stream_release_count + retiring_stream_count &&
                   this->statistics_.task_retain_count ==
                       this->statistics_.task_release_count + active_task_count &&
                   this->statistics_.useful_page_record_count ==
                       this->statistics_.useful_page_take_count +
                           this->statistics_.stale_useful_page_drop_count +
                           pending_useful_page_count &&
                   this->statistics_.wasted_page_record_count ==
                       this->statistics_.wasted_page_take_count +
                           this->statistics_.stale_wasted_page_drop_count +
                           pending_wasted_page_count
               ? FileReadaheadFeedbackStatus::Succeeded
               : FileReadaheadFeedbackStatus::Corrupt;
}

bool FileReadaheadFeedbackLedger::TokenIsValid(
    const FileReadaheadStreamToken token) const noexcept {
    return this->initialized_ && token.slot_index < this->capacity_ &&
           token.generation != OS_KERNEL_FILE_READAHEAD_FEEDBACK_EMPTY_VALUE &&
           this->slots_[token.slot_index].state != FileReadaheadFeedbackSlotState::Free &&
           this->slots_[token.slot_index].generation == token.generation;
}

uint64_t FileReadaheadFeedbackLedger::FindFreeSlotIndex() const noexcept {
    for (uint64_t slot_index = OS_KERNEL_FILE_READAHEAD_FEEDBACK_EMPTY_VALUE;
         slot_index < this->capacity_; ++slot_index) {
        if (this->slots_[slot_index].state == FileReadaheadFeedbackSlotState::Free) {
            return slot_index;
        }
    }
    return OS_KERNEL_FILE_READAHEAD_STREAM_INVALID_SLOT_INDEX;
}

bool FileReadaheadFeedbackLedger::ReleaseRetiredSlot(FileReadaheadFeedbackSlot &slot) noexcept {
    if (!this->CanReleaseRetiredSlot(slot)) {
        return false;
    }
    this->statistics_.stale_useful_page_drop_count += slot.pending_feedback.useful_page_count;
    this->statistics_.stale_wasted_page_drop_count += slot.pending_feedback.wasted_page_count;
    const uint64_t generation = slot.generation;
    slot = FileReadaheadFeedbackSlot{};
    slot.generation = generation;
    --this->statistics_.retiring_stream_count;
    ++this->statistics_.stream_release_count;
    return true;
}

bool FileReadaheadFeedbackLedger::CanReleaseRetiredSlot(
    const FileReadaheadFeedbackSlot &slot) const noexcept {
    return slot.state == FileReadaheadFeedbackSlotState::Retiring &&
           slot.active_task_count == OS_KERNEL_FILE_READAHEAD_FEEDBACK_EMPTY_VALUE &&
           this->statistics_.retiring_stream_count !=
               OS_KERNEL_FILE_READAHEAD_FEEDBACK_EMPTY_VALUE &&
           this->statistics_.stream_release_count != UINT64_MAX &&
           this->statistics_.stale_useful_page_drop_count <=
               UINT64_MAX - slot.pending_feedback.useful_page_count &&
           this->statistics_.stale_wasted_page_drop_count <=
               UINT64_MAX - slot.pending_feedback.wasted_page_count;
}

bool FileReadaheadStreamTokenIsValid(const FileReadaheadStreamToken token) noexcept {
    return token.slot_index != OS_KERNEL_FILE_READAHEAD_STREAM_INVALID_SLOT_INDEX &&
           token.generation != OS_KERNEL_FILE_READAHEAD_FEEDBACK_EMPTY_VALUE;
}

bool FileReadaheadStreamTokensEqual(const FileReadaheadStreamToken left,
                                    const FileReadaheadStreamToken right) noexcept {
    return left.slot_index == right.slot_index && left.generation == right.generation;
}

bool FileReadaheadPageTagIsValid(const FileReadaheadPageTag &tag) noexcept {
    return FileReadaheadStreamTokenIsValid(tag.stream) &&
           tag.policy_generation != OS_KERNEL_FILE_READAHEAD_FEEDBACK_EMPTY_VALUE;
}

bool FileReadaheadPageTagIsEmpty(const FileReadaheadPageTag &tag) noexcept {
    return tag.stream.slot_index == OS_KERNEL_FILE_READAHEAD_FEEDBACK_EMPTY_VALUE &&
           tag.stream.generation == OS_KERNEL_FILE_READAHEAD_FEEDBACK_EMPTY_VALUE &&
           tag.policy_generation == OS_KERNEL_FILE_READAHEAD_FEEDBACK_EMPTY_VALUE;
}

}

#include <os/kernel/memory/background_reclaim.hpp>

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_BACKGROUND_RECLAIM_EMPTY_VALUE = 0ULL;

[[nodiscard]] uint64_t Minimum(const uint64_t left, const uint64_t right) noexcept {
    return left < right ? left : right;
}

[[nodiscard]] bool WatermarksAreValid(const MemoryWatermarks &watermarks) noexcept {
    return watermarks.minimum_free_page_count < watermarks.low_free_page_count &&
           watermarks.low_free_page_count < watermarks.high_free_page_count &&
           watermarks.high_free_page_count < watermarks.resident_limit_page_count;
}

}

BackgroundReclaimStatus BackgroundReclaimController::Initialize(
    const BackgroundReclaimConfiguration &configuration) noexcept {
    if (this->initialized_) {
        return BackgroundReclaimStatus::AlreadyInitialized;
    }
    if (configuration.batch_page_count == OS_KERNEL_BACKGROUND_RECLAIM_EMPTY_VALUE ||
        configuration.no_progress_backoff_nanoseconds == OS_KERNEL_BACKGROUND_RECLAIM_EMPTY_VALUE) {
        return BackgroundReclaimStatus::InvalidConfiguration;
    }
    this->configuration_ = configuration;
    this->statistics_ = BackgroundReclaimStatistics{
        .state = BackgroundReclaimState::Sleeping,
        .batch_page_count = configuration.batch_page_count,
        .no_progress_backoff_nanoseconds = configuration.no_progress_backoff_nanoseconds,
        .wake_count = OS_KERNEL_BACKGROUND_RECLAIM_EMPTY_VALUE,
        .sleep_count = OS_KERNEL_BACKGROUND_RECLAIM_EMPTY_VALUE,
        .batch_count = OS_KERNEL_BACKGROUND_RECLAIM_EMPTY_VALUE,
        .requested_page_count = OS_KERNEL_BACKGROUND_RECLAIM_EMPTY_VALUE,
        .clean_file_page_count = OS_KERNEL_BACKGROUND_RECLAIM_EMPTY_VALUE,
        .swapped_anonymous_page_count = OS_KERNEL_BACKGROUND_RECLAIM_EMPTY_VALUE,
        .reclaimed_page_count = OS_KERNEL_BACKGROUND_RECLAIM_EMPTY_VALUE,
        .written_page_count = OS_KERNEL_BACKGROUND_RECLAIM_EMPTY_VALUE,
        .no_progress_count = OS_KERNEL_BACKGROUND_RECLAIM_EMPTY_VALUE,
        .failure_count = OS_KERNEL_BACKGROUND_RECLAIM_EMPTY_VALUE,
        .backoff_count = OS_KERNEL_BACKGROUND_RECLAIM_EMPTY_VALUE,
        .resume_count = OS_KERNEL_BACKGROUND_RECLAIM_EMPTY_VALUE,
        .reset_count = OS_KERNEL_BACKGROUND_RECLAIM_EMPTY_VALUE,
        .next_deadline_nanoseconds = OS_KERNEL_BACKGROUND_RECLAIM_EMPTY_VALUE,
        .initialized = true,
    };
    this->initialized_ = true;
    return BackgroundReclaimStatus::Succeeded;
}

BackgroundReclaimStatus BackgroundReclaimController::Evaluate(
    const MemoryWatermarks &watermarks, const uint64_t resident_page_count,
    const uint64_t now_nanoseconds, BackgroundReclaimDecision &decision) noexcept {
    decision = BackgroundReclaimDecision{};
    if (!this->initialized_) {
        return BackgroundReclaimStatus::NotInitialized;
    }
    if (!WatermarksAreValid(watermarks) ||
        resident_page_count > watermarks.resident_limit_page_count) {
        return BackgroundReclaimStatus::InvalidPressureSample;
    }
    if (!BackgroundReclaimController::StateIsValid(this->statistics_.state)) {
        return BackgroundReclaimStatus::InvalidState;
    }

    const uint64_t free_page_count = watermarks.resident_limit_page_count - resident_page_count;
    if (this->statistics_.state == BackgroundReclaimState::Sleeping &&
        free_page_count < watermarks.low_free_page_count) {
        if (!this->AddCounter(this->statistics_.wake_count, 1ULL)) {
            return BackgroundReclaimStatus::CounterOverflow;
        }
        this->statistics_.state = BackgroundReclaimState::Running;
    }
    if (this->statistics_.state != BackgroundReclaimState::Sleeping &&
        free_page_count >= watermarks.high_free_page_count) {
        if (!this->AddCounter(this->statistics_.sleep_count, 1ULL)) {
            return BackgroundReclaimStatus::CounterOverflow;
        }
        this->statistics_.state = BackgroundReclaimState::Sleeping;
        this->statistics_.next_deadline_nanoseconds = OS_KERNEL_BACKGROUND_RECLAIM_EMPTY_VALUE;
    }
    if (this->statistics_.state == BackgroundReclaimState::Sleeping) {
        decision = BackgroundReclaimDecision{
            .state = this->statistics_.state,
            .action = BackgroundReclaimAction::Sleep,
            .free_page_count = free_page_count,
            .target_page_count = OS_KERNEL_BACKGROUND_RECLAIM_EMPTY_VALUE,
            .deadline_nanoseconds = OS_KERNEL_BACKGROUND_RECLAIM_EMPTY_VALUE,
        };
        return BackgroundReclaimStatus::Succeeded;
    }
    if (this->statistics_.state == BackgroundReclaimState::BackingOff &&
        now_nanoseconds < this->statistics_.next_deadline_nanoseconds) {
        decision = BackgroundReclaimDecision{
            .state = this->statistics_.state,
            .action = BackgroundReclaimAction::Wait,
            .free_page_count = free_page_count,
            .target_page_count = OS_KERNEL_BACKGROUND_RECLAIM_EMPTY_VALUE,
            .deadline_nanoseconds = this->statistics_.next_deadline_nanoseconds,
        };
        return BackgroundReclaimStatus::Succeeded;
    }
    if (this->statistics_.state == BackgroundReclaimState::BackingOff) {
        if (!this->AddCounter(this->statistics_.resume_count, 1ULL)) {
            return BackgroundReclaimStatus::CounterOverflow;
        }
        this->statistics_.state = BackgroundReclaimState::Running;
        this->statistics_.next_deadline_nanoseconds = OS_KERNEL_BACKGROUND_RECLAIM_EMPTY_VALUE;
    }
    const uint64_t target_page_count = Minimum(this->configuration_.batch_page_count,
                                               watermarks.high_free_page_count - free_page_count);
    if (target_page_count == OS_KERNEL_BACKGROUND_RECLAIM_EMPTY_VALUE) {
        return BackgroundReclaimStatus::Corrupt;
    }
    decision = BackgroundReclaimDecision{
        .state = this->statistics_.state,
        .action = BackgroundReclaimAction::Reclaim,
        .free_page_count = free_page_count,
        .target_page_count = target_page_count,
        .deadline_nanoseconds = OS_KERNEL_BACKGROUND_RECLAIM_EMPTY_VALUE,
    };
    return BackgroundReclaimStatus::Succeeded;
}

BackgroundReclaimStatus
BackgroundReclaimController::RecordBatch(const BackgroundReclaimBatchResult &result,
                                         const uint64_t now_nanoseconds) noexcept {
    if (!this->initialized_) {
        return BackgroundReclaimStatus::NotInitialized;
    }
    if (this->statistics_.state != BackgroundReclaimState::Running) {
        return BackgroundReclaimStatus::InvalidState;
    }
    if (result.requested_page_count == OS_KERNEL_BACKGROUND_RECLAIM_EMPTY_VALUE ||
        result.requested_page_count > this->configuration_.batch_page_count ||
        result.reclaimed_page_count > result.requested_page_count ||
        result.written_page_count > result.requested_page_count ||
        result.written_page_count > result.requested_page_count - result.reclaimed_page_count ||
        result.clean_file_page_count > UINT64_MAX - result.swapped_anonymous_page_count ||
        result.clean_file_page_count + result.swapped_anonymous_page_count !=
            result.reclaimed_page_count) {
        return BackgroundReclaimStatus::InvalidBatchResult;
    }
    if (!this->AddCounter(this->statistics_.batch_count, 1ULL) ||
        !this->AddCounter(this->statistics_.requested_page_count, result.requested_page_count) ||
        !this->AddCounter(this->statistics_.clean_file_page_count, result.clean_file_page_count) ||
        !this->AddCounter(this->statistics_.swapped_anonymous_page_count,
                          result.swapped_anonymous_page_count) ||
        !this->AddCounter(this->statistics_.reclaimed_page_count, result.reclaimed_page_count) ||
        !this->AddCounter(this->statistics_.written_page_count, result.written_page_count)) {
        return BackgroundReclaimStatus::CounterOverflow;
    }
    if (result.failed) {
        if (!this->AddCounter(this->statistics_.failure_count, 1ULL)) {
            return BackgroundReclaimStatus::CounterOverflow;
        }
        return this->EnterBackoff(now_nanoseconds);
    }
    if (result.reclaimed_page_count == OS_KERNEL_BACKGROUND_RECLAIM_EMPTY_VALUE &&
        result.written_page_count == OS_KERNEL_BACKGROUND_RECLAIM_EMPTY_VALUE) {
        if (!this->AddCounter(this->statistics_.no_progress_count, 1ULL)) {
            return BackgroundReclaimStatus::CounterOverflow;
        }
        return this->EnterBackoff(now_nanoseconds);
    }
    if (result.reclaimed_page_count == OS_KERNEL_BACKGROUND_RECLAIM_EMPTY_VALUE) {
        return this->EnterBackoff(now_nanoseconds);
    }
    return BackgroundReclaimStatus::Succeeded;
}

BackgroundReclaimStatus BackgroundReclaimController::Reset() noexcept {
    if (!this->initialized_) {
        return BackgroundReclaimStatus::NotInitialized;
    }
    if (!this->AddCounter(this->statistics_.reset_count, 1ULL) ||
        (this->statistics_.state != BackgroundReclaimState::Sleeping &&
         !this->AddCounter(this->statistics_.sleep_count, 1ULL))) {
        return BackgroundReclaimStatus::CounterOverflow;
    }
    this->statistics_.state = BackgroundReclaimState::Sleeping;
    this->statistics_.next_deadline_nanoseconds = OS_KERNEL_BACKGROUND_RECLAIM_EMPTY_VALUE;
    return BackgroundReclaimStatus::Succeeded;
}

BackgroundReclaimStatistics BackgroundReclaimController::Statistics() const noexcept {
    return this->initialized_ ? this->statistics_ : BackgroundReclaimStatistics{};
}

BackgroundReclaimStatus BackgroundReclaimController::Validate() const noexcept {
    if (!this->initialized_) {
        return BackgroundReclaimStatus::NotInitialized;
    }
    const bool backoff_deadline_valid =
        this->statistics_.state == BackgroundReclaimState::BackingOff
            ? this->statistics_.next_deadline_nanoseconds !=
                  OS_KERNEL_BACKGROUND_RECLAIM_EMPTY_VALUE
            : this->statistics_.next_deadline_nanoseconds ==
                  OS_KERNEL_BACKGROUND_RECLAIM_EMPTY_VALUE;
    const bool reclaimed_categories_valid =
        this->statistics_.clean_file_page_count <=
            UINT64_MAX - this->statistics_.swapped_anonymous_page_count &&
        this->statistics_.clean_file_page_count + this->statistics_.swapped_anonymous_page_count ==
            this->statistics_.reclaimed_page_count;
    const bool classified_backoff_count_valid =
        this->statistics_.no_progress_count <= UINT64_MAX - this->statistics_.failure_count &&
        this->statistics_.backoff_count >=
            this->statistics_.no_progress_count + this->statistics_.failure_count;
    return BackgroundReclaimController::StateIsValid(this->statistics_.state) &&
                   this->configuration_.batch_page_count == this->statistics_.batch_page_count &&
                   this->configuration_.no_progress_backoff_nanoseconds ==
                       this->statistics_.no_progress_backoff_nanoseconds &&
                   this->statistics_.batch_page_count != OS_KERNEL_BACKGROUND_RECLAIM_EMPTY_VALUE &&
                   this->statistics_.no_progress_backoff_nanoseconds !=
                       OS_KERNEL_BACKGROUND_RECLAIM_EMPTY_VALUE &&
                   this->statistics_.reclaimed_page_count <=
                       this->statistics_.requested_page_count &&
                   this->statistics_.written_page_count <=
                       this->statistics_.requested_page_count -
                           this->statistics_.reclaimed_page_count &&
                   this->statistics_.sleep_count <= this->statistics_.wake_count &&
                   this->statistics_.resume_count <= this->statistics_.backoff_count &&
                   reclaimed_categories_valid && classified_backoff_count_valid &&
                   backoff_deadline_valid && this->statistics_.initialized
               ? BackgroundReclaimStatus::Succeeded
               : BackgroundReclaimStatus::Corrupt;
}

bool BackgroundReclaimController::StateIsValid(const BackgroundReclaimState state) noexcept {
    return state == BackgroundReclaimState::Sleeping || state == BackgroundReclaimState::Running ||
           state == BackgroundReclaimState::BackingOff;
}

bool BackgroundReclaimController::AddCounter(uint64_t &counter, const uint64_t increment) noexcept {
    if (counter > UINT64_MAX - increment) {
        return false;
    }
    counter += increment;
    return true;
}

BackgroundReclaimStatus
BackgroundReclaimController::EnterBackoff(const uint64_t now_nanoseconds) noexcept {
    if (now_nanoseconds > UINT64_MAX - this->configuration_.no_progress_backoff_nanoseconds ||
        !this->AddCounter(this->statistics_.backoff_count, 1ULL)) {
        return BackgroundReclaimStatus::CounterOverflow;
    }
    this->statistics_.state = BackgroundReclaimState::BackingOff;
    this->statistics_.next_deadline_nanoseconds =
        now_nanoseconds + this->configuration_.no_progress_backoff_nanoseconds;
    return BackgroundReclaimStatus::Succeeded;
}

}

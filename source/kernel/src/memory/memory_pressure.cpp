#include <os/kernel/memory/memory_pressure.hpp>

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_MEMORY_PRESSURE_SINGLE_PAGE = 1ULL;
constexpr uint64_t OS_KERNEL_MEMORY_PRESSURE_MINIMUM_GAP_DIVISOR = 4ULL;
constexpr uint64_t OS_KERNEL_MEMORY_PRESSURE_MINIMUM_FAIR_TARGET_PAGE_COUNT = 2ULL;
constexpr uint64_t OS_KERNEL_MEMORY_PRESSURE_INTEGER_SQRT_INITIAL_BIT = 1ULL << 62ULL;
constexpr uint64_t OS_KERNEL_MEMORY_PRESSURE_INIT_PROCESS_IDENTIFIER = 1ULL;

[[nodiscard]] uint64_t Minimum(const uint64_t left, const uint64_t right) noexcept {
    return left < right ? left : right;
}

[[nodiscard]] uint64_t Maximum(const uint64_t left, const uint64_t right) noexcept {
    return left > right ? left : right;
}

[[nodiscard]] bool TryAdd(const uint64_t left, const uint64_t right, uint64_t &sum) noexcept {
    if (left > UINT64_MAX - right) {
        return false;
    }
    sum = left + right;
    return true;
}

[[nodiscard]] uint64_t IntegerSquareRoot(const uint64_t value) noexcept {
    uint64_t remainder = value;
    uint64_t root = OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE;
    uint64_t bit = OS_KERNEL_MEMORY_PRESSURE_INTEGER_SQRT_INITIAL_BIT;
    while (bit > remainder) {
        bit >>= 2ULL;
    }
    while (bit != OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE) {
        if (remainder >= root + bit) {
            remainder -= root + bit;
            root = (root >> 1ULL) + bit;
        } else {
            root >>= 1ULL;
        }
        bit >>= 2ULL;
    }
    return root;
}

[[nodiscard]] bool OvercommitModeIsValid(const MemoryOvercommitMode mode) noexcept {
    return mode == MemoryOvercommitMode::Heuristic || mode == MemoryOvercommitMode::Always ||
           mode == MemoryOvercommitMode::Never;
}

[[nodiscard]] bool AllocationClassIsValid(const MemoryAllocationClass allocation_class) noexcept {
    return allocation_class == MemoryAllocationClass::User ||
           allocation_class == MemoryAllocationClass::Kernel ||
           allocation_class == MemoryAllocationClass::Reclaim;
}

[[nodiscard]] bool CalculatePercent(const uint64_t value, const uint64_t percent,
                                    uint64_t &result) noexcept {
    if (percent == OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE) {
        result = OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE;
        return true;
    }
    const uint64_t quotient = value / OS_KERNEL_MEMORY_PRESSURE_PERCENT_DENOMINATOR;
    const uint64_t remainder = value % OS_KERNEL_MEMORY_PRESSURE_PERCENT_DENOMINATOR;
    if (quotient > UINT64_MAX / percent) {
        return false;
    }
    const uint64_t quotient_part = quotient * percent;
    if (remainder > UINT64_MAX / percent) {
        return false;
    }
    const uint64_t remainder_part =
        (remainder * percent) / OS_KERNEL_MEMORY_PRESSURE_PERCENT_DENOMINATOR;
    return TryAdd(quotient_part, remainder_part, result);
}

[[nodiscard]] bool CalculateCommitLimits(const MemoryOvercommitConfiguration &configuration,
                                         uint64_t &normal_limit,
                                         uint64_t &privileged_limit) noexcept {
    if (configuration.mode == MemoryOvercommitMode::Always) {
        normal_limit = UINT64_MAX;
        privileged_limit = UINT64_MAX;
        return true;
    }

    uint64_t physical_commit_page_count = configuration.physical_page_count;
    if (configuration.mode == MemoryOvercommitMode::Never &&
        !CalculatePercent(configuration.physical_page_count, configuration.overcommit_ratio_percent,
                          physical_commit_page_count)) {
        return false;
    }
    uint64_t total_limit = OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE;
    if (!TryAdd(physical_commit_page_count, configuration.swap_page_count, total_limit) ||
        configuration.admin_reserve_page_count > total_limit) {
        return false;
    }
    privileged_limit = total_limit;
    normal_limit = total_limit - configuration.admin_reserve_page_count;
    if (configuration.user_reserve_page_count > normal_limit) {
        return false;
    }
    normal_limit -= configuration.user_reserve_page_count;
    return true;
}

[[nodiscard]] uint64_t CalculateScaledRatio(const uint64_t value, const uint64_t divisor,
                                            const uint64_t scale) noexcept {
    uint64_t quotient = OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE;
    uint64_t remainder = OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE;
    for (uint64_t unit_index = OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE; unit_index < scale;
         ++unit_index) {
        if (remainder >= divisor - value) {
            remainder -= divisor - value;
            ++quotient;
        } else {
            remainder += value;
        }
    }
    return quotient;
}

[[nodiscard]] uint64_t CalculateOomScore(const uint64_t memory_page_count,
                                         const uint64_t allowed_page_count,
                                         const int64_t adjustment) noexcept {
    uint64_t base_score = OS_KERNEL_MEMORY_PRESSURE_OOM_BASE_SCORE_MAXIMUM;
    if (memory_page_count < allowed_page_count) {
        base_score = CalculateScaledRatio(memory_page_count, allowed_page_count,
                                          OS_KERNEL_MEMORY_PRESSURE_OOM_BASE_SCORE_MAXIMUM);
    }
    const int64_t adjusted_score = static_cast<int64_t>(base_score) + adjustment;
    if (adjusted_score <= 0LL) {
        return OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE;
    }
    return static_cast<uint64_t>(adjusted_score) >
                   OS_KERNEL_MEMORY_PRESSURE_OOM_ADJUSTED_SCORE_MAXIMUM
               ? OS_KERNEL_MEMORY_PRESSURE_OOM_ADJUSTED_SCORE_MAXIMUM
               : static_cast<uint64_t>(adjusted_score);
}

}

MemoryPressureStatus CalculateMemoryWatermarks(const uint64_t resident_limit_page_count,
                                               const uint64_t watermark_scale_factor,
                                               MemoryWatermarks &watermarks) noexcept {
    if (resident_limit_page_count == OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE ||
        watermark_scale_factor == OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE ||
        watermark_scale_factor > OS_KERNEL_MEMORY_PRESSURE_WATERMARK_SCALE_DENOMINATOR) {
        return MemoryPressureStatus::InvalidConfiguration;
    }

    uint64_t minimum_page_count = IntegerSquareRoot(resident_limit_page_count);
    if (minimum_page_count > UINT64_MAX / 2ULL) {
        return MemoryPressureStatus::CounterOverflow;
    }
    minimum_page_count *= 2ULL;
    minimum_page_count =
        Maximum(minimum_page_count, OS_KERNEL_MEMORY_PRESSURE_MINIMUM_FREE_PAGE_FLOOR);
    minimum_page_count =
        Minimum(minimum_page_count, OS_KERNEL_MEMORY_PRESSURE_MINIMUM_FREE_PAGE_CEILING);
    if (minimum_page_count >= resident_limit_page_count) {
        minimum_page_count =
            resident_limit_page_count / OS_KERNEL_MEMORY_PRESSURE_MINIMUM_GAP_DIVISOR;
    }
    if (minimum_page_count == OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE) {
        minimum_page_count = OS_KERNEL_MEMORY_PRESSURE_SINGLE_PAGE;
    }

    uint64_t scaled_gap =
        (resident_limit_page_count / OS_KERNEL_MEMORY_PRESSURE_WATERMARK_SCALE_DENOMINATOR) *
        watermark_scale_factor;
    const uint64_t scaled_remainder =
        ((resident_limit_page_count % OS_KERNEL_MEMORY_PRESSURE_WATERMARK_SCALE_DENOMINATOR) *
         watermark_scale_factor) /
        OS_KERNEL_MEMORY_PRESSURE_WATERMARK_SCALE_DENOMINATOR;
    if (!TryAdd(scaled_gap, scaled_remainder, scaled_gap)) {
        return MemoryPressureStatus::CounterOverflow;
    }
    uint64_t watermark_gap =
        Maximum(minimum_page_count / OS_KERNEL_MEMORY_PRESSURE_MINIMUM_GAP_DIVISOR, scaled_gap);
    watermark_gap = Maximum(watermark_gap, OS_KERNEL_MEMORY_PRESSURE_SINGLE_PAGE);

    if (minimum_page_count > resident_limit_page_count - OS_KERNEL_MEMORY_PRESSURE_SINGLE_PAGE) {
        return MemoryPressureStatus::InvalidConfiguration;
    }
    uint64_t low_page_count = OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE;
    if (!TryAdd(minimum_page_count, watermark_gap, low_page_count)) {
        return MemoryPressureStatus::CounterOverflow;
    }
    if (low_page_count >= resident_limit_page_count) {
        low_page_count =
            minimum_page_count + (resident_limit_page_count - minimum_page_count) / 2ULL;
    }
    uint64_t high_page_count = OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE;
    if (!TryAdd(low_page_count, watermark_gap, high_page_count)) {
        return MemoryPressureStatus::CounterOverflow;
    }
    if (high_page_count >= resident_limit_page_count) {
        high_page_count = resident_limit_page_count - OS_KERNEL_MEMORY_PRESSURE_SINGLE_PAGE;
    }
    if (!(minimum_page_count < low_page_count && low_page_count < high_page_count)) {
        return MemoryPressureStatus::InvalidConfiguration;
    }
    watermarks = MemoryWatermarks{
        .resident_limit_page_count = resident_limit_page_count,
        .minimum_free_page_count = minimum_page_count,
        .low_free_page_count = low_page_count,
        .high_free_page_count = high_page_count,
    };
    return MemoryPressureStatus::Succeeded;
}

MemoryPressureLevel ClassifyMemoryPressure(const MemoryWatermarks &watermarks,
                                           const uint64_t free_page_count) noexcept {
    if (free_page_count < watermarks.minimum_free_page_count) {
        return MemoryPressureLevel::BelowMinimum;
    }
    if (free_page_count < watermarks.low_free_page_count) {
        return MemoryPressureLevel::BelowLow;
    }
    if (free_page_count < watermarks.high_free_page_count) {
        return MemoryPressureLevel::BelowHigh;
    }
    return MemoryPressureLevel::Balanced;
}

MemoryPressureStatus
MemoryPressureController::Initialize(const MemoryPressureConfiguration &configuration) noexcept {
    if (this->initialized_) {
        return MemoryPressureStatus::AlreadyInitialized;
    }
    if (configuration.managed_page_count == OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE ||
        configuration.resident_limit_page_count == OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE ||
        configuration.resident_limit_page_count > configuration.managed_page_count ||
        configuration.initial_resident_page_count > configuration.resident_limit_page_count ||
        configuration.swappiness > OS_KERNEL_MEMORY_PRESSURE_MAXIMUM_SWAPPINESS) {
        return MemoryPressureStatus::InvalidConfiguration;
    }
    MemoryWatermarks watermarks{};
    const MemoryPressureStatus watermark_status = CalculateMemoryWatermarks(
        configuration.resident_limit_page_count, configuration.watermark_scale_factor, watermarks);
    if (watermark_status != MemoryPressureStatus::Succeeded) {
        return watermark_status;
    }
    this->configuration_ = configuration;
    this->statistics_ = MemoryPressureStatistics{
        .watermarks = watermarks,
        .managed_page_count = configuration.managed_page_count,
        .resident_page_count = configuration.initial_resident_page_count,
        .peak_resident_page_count = configuration.initial_resident_page_count,
        .swap_page_count = configuration.swap_page_count,
        .allocation_request_count = OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE,
        .allowed_allocation_count = OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE,
        .reclaim_request_count = OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE,
        .rejected_allocation_count = OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE,
        .reclaim_attempt_count = OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE,
        .reclaimed_page_count = OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE,
    };
    this->initialized_ = true;
    return MemoryPressureStatus::Succeeded;
}

MemoryPressureStatus
MemoryPressureController::PrepareAllocation(const uint64_t requested_page_count,
                                            const MemoryAllocationClass allocation_class,
                                            MemoryAllocationDecision &decision) noexcept {
    decision = MemoryAllocationDecision{};
    if (!this->initialized_) {
        return MemoryPressureStatus::NotInitialized;
    }
    if (requested_page_count == OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE ||
        !AllocationClassIsValid(allocation_class)) {
        return MemoryPressureStatus::InvalidRequest;
    }
    const uint64_t resident_limit = this->statistics_.watermarks.resident_limit_page_count;
    if (requested_page_count > resident_limit) {
        decision = MemoryAllocationDecision{
            .action = MemoryAllocationAction::Reject,
            .level = MemoryPressureLevel::BelowMinimum,
            .requested_page_count = requested_page_count,
            .target_reclaim_page_count = OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE,
        };
    } else if (this->statistics_.resident_page_count > resident_limit - requested_page_count) {
        const uint64_t overflow_page_count =
            this->statistics_.resident_page_count - (resident_limit - requested_page_count);
        uint64_t target_reclaim_page_count = UINT64_MAX;
        static_cast<void>(TryAdd(overflow_page_count,
                                 this->statistics_.watermarks.high_free_page_count,
                                 target_reclaim_page_count));
        decision = MemoryAllocationDecision{
            .action = allocation_class == MemoryAllocationClass::Reclaim
                          ? MemoryAllocationAction::Reject
                          : MemoryAllocationAction::Reclaim,
            .level = MemoryPressureLevel::BelowMinimum,
            .requested_page_count = requested_page_count,
            .target_reclaim_page_count = target_reclaim_page_count,
        };
    } else {
        const uint64_t free_after_page_count =
            resident_limit - this->statistics_.resident_page_count - requested_page_count;
        const MemoryPressureLevel level =
            ClassifyMemoryPressure(this->statistics_.watermarks, free_after_page_count);
        // low 到 min 之间给后台回收留下调度窗口；只有越过 min 才让分配者同步回收。
        const bool user_reclaim_required = allocation_class == MemoryAllocationClass::User &&
                                           level == MemoryPressureLevel::BelowMinimum;
        const bool kernel_reclaim_required = allocation_class == MemoryAllocationClass::Kernel &&
                                             level == MemoryPressureLevel::BelowMinimum;
        if (user_reclaim_required || kernel_reclaim_required) {
            decision = MemoryAllocationDecision{
                .action = MemoryAllocationAction::Reclaim,
                .level = level,
                .requested_page_count = requested_page_count,
                .target_reclaim_page_count =
                    this->statistics_.watermarks.high_free_page_count - free_after_page_count,
            };
        } else {
            decision = MemoryAllocationDecision{
                .action = MemoryAllocationAction::Allow,
                .level = level,
                .requested_page_count = requested_page_count,
                .target_reclaim_page_count = OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE,
            };
        }
    }

    if (this->statistics_.allocation_request_count == UINT64_MAX) {
        return MemoryPressureStatus::CounterOverflow;
    }
    if (decision.action == MemoryAllocationAction::Allow) {
        if (this->statistics_.allowed_allocation_count == UINT64_MAX) {
            return MemoryPressureStatus::CounterOverflow;
        }
        ++this->statistics_.allowed_allocation_count;
    } else if (decision.action == MemoryAllocationAction::Reclaim) {
        if (this->statistics_.reclaim_request_count == UINT64_MAX) {
            return MemoryPressureStatus::CounterOverflow;
        }
        ++this->statistics_.reclaim_request_count;
    } else {
        if (this->statistics_.rejected_allocation_count == UINT64_MAX) {
            return MemoryPressureStatus::CounterOverflow;
        }
        ++this->statistics_.rejected_allocation_count;
    }
    ++this->statistics_.allocation_request_count;
    return MemoryPressureStatus::Succeeded;
}

MemoryPressureStatus
MemoryPressureController::CommitAllocation(const uint64_t page_count) noexcept {
    if (!this->initialized_) {
        return MemoryPressureStatus::NotInitialized;
    }
    if (page_count == OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE) {
        return MemoryPressureStatus::InvalidRequest;
    }
    const uint64_t limit = this->statistics_.watermarks.resident_limit_page_count;
    if (page_count > limit || this->statistics_.resident_page_count > limit - page_count) {
        return MemoryPressureStatus::ResidentLimitExceeded;
    }
    this->statistics_.resident_page_count += page_count;
    this->statistics_.peak_resident_page_count =
        Maximum(this->statistics_.peak_resident_page_count, this->statistics_.resident_page_count);
    return MemoryPressureStatus::Succeeded;
}

MemoryPressureStatus MemoryPressureController::ReleaseResident(const uint64_t page_count) noexcept {
    if (!this->initialized_) {
        return MemoryPressureStatus::NotInitialized;
    }
    if (page_count == OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE) {
        return MemoryPressureStatus::InvalidRequest;
    }
    if (page_count > this->statistics_.resident_page_count) {
        return MemoryPressureStatus::ResidentAccountingUnderflow;
    }
    this->statistics_.resident_page_count -= page_count;
    return MemoryPressureStatus::Succeeded;
}

MemoryPressureStatus
MemoryPressureController::SynchronizeResident(const uint64_t observed_page_count) noexcept {
    if (!this->initialized_) {
        return MemoryPressureStatus::NotInitialized;
    }
    if (observed_page_count > this->statistics_.watermarks.resident_limit_page_count) {
        return MemoryPressureStatus::ResidentLimitExceeded;
    }
    this->statistics_.resident_page_count = observed_page_count;
    this->statistics_.peak_resident_page_count =
        Maximum(this->statistics_.peak_resident_page_count, observed_page_count);
    return MemoryPressureStatus::Succeeded;
}

MemoryPressureStatus
MemoryPressureController::ConfigureSwap(const uint64_t swap_page_count) noexcept {
    if (!this->initialized_) {
        return MemoryPressureStatus::NotInitialized;
    }
    this->configuration_.swap_page_count = swap_page_count;
    this->statistics_.swap_page_count = swap_page_count;
    return MemoryPressureStatus::Succeeded;
}

MemoryPressureStatus MemoryPressureController::ConfigureResidentLimit(
    const uint64_t resident_limit_page_count) noexcept {
    if (!this->initialized_) {
        return MemoryPressureStatus::NotInitialized;
    }
    if (resident_limit_page_count == OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE ||
        resident_limit_page_count > this->configuration_.managed_page_count ||
        resident_limit_page_count < this->statistics_.resident_page_count) {
        return MemoryPressureStatus::ResidentLimitExceeded;
    }
    MemoryWatermarks watermarks{};
    const MemoryPressureStatus status = CalculateMemoryWatermarks(
        resident_limit_page_count, this->configuration_.watermark_scale_factor, watermarks);
    if (status != MemoryPressureStatus::Succeeded) {
        return status;
    }
    this->configuration_.resident_limit_page_count = resident_limit_page_count;
    this->statistics_.watermarks = watermarks;
    return MemoryPressureStatus::Succeeded;
}

MemoryPressureStatus
MemoryPressureController::RecordReclaim(const uint64_t reclaimed_page_count) noexcept {
    if (!this->initialized_) {
        return MemoryPressureStatus::NotInitialized;
    }
    if (this->statistics_.reclaim_attempt_count == UINT64_MAX ||
        this->statistics_.reclaimed_page_count > UINT64_MAX - reclaimed_page_count) {
        return MemoryPressureStatus::CounterOverflow;
    }
    ++this->statistics_.reclaim_attempt_count;
    this->statistics_.reclaimed_page_count += reclaimed_page_count;
    return MemoryPressureStatus::Succeeded;
}

MemoryPressureStatistics MemoryPressureController::Statistics() const noexcept {
    return this->initialized_ ? this->statistics_ : MemoryPressureStatistics{};
}

MemoryPressureStatus MemoryPressureController::Validate() const noexcept {
    if (!this->initialized_) {
        return MemoryPressureStatus::NotInitialized;
    }
    const MemoryWatermarks &watermarks = this->statistics_.watermarks;
    uint64_t classified_request_count = OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE;
    const bool request_counts_valid =
        TryAdd(this->statistics_.allowed_allocation_count, this->statistics_.reclaim_request_count,
               classified_request_count) &&
        TryAdd(classified_request_count, this->statistics_.rejected_allocation_count,
               classified_request_count);
    return request_counts_valid &&
                   this->configuration_.managed_page_count ==
                       this->statistics_.managed_page_count &&
                   this->configuration_.resident_limit_page_count ==
                       watermarks.resident_limit_page_count &&
                   this->configuration_.swap_page_count == this->statistics_.swap_page_count &&
                   watermarks.minimum_free_page_count < watermarks.low_free_page_count &&
                   watermarks.low_free_page_count < watermarks.high_free_page_count &&
                   watermarks.high_free_page_count < watermarks.resident_limit_page_count &&
                   this->statistics_.resident_page_count <= watermarks.resident_limit_page_count &&
                   this->statistics_.peak_resident_page_count >=
                       this->statistics_.resident_page_count &&
                   this->statistics_.peak_resident_page_count <=
                       watermarks.resident_limit_page_count &&
                   classified_request_count == this->statistics_.allocation_request_count
               ? MemoryPressureStatus::Succeeded
               : MemoryPressureStatus::Corrupt;
}

MemoryReclaimPlanStatus PlanMemoryReclaim(const MemoryReclaimInput &input,
                                          MemoryReclaimPlan &plan) noexcept {
    plan = MemoryReclaimPlan{};
    if (input.swappiness > OS_KERNEL_MEMORY_PRESSURE_MAXIMUM_SWAPPINESS) {
        return MemoryReclaimPlanStatus::InvalidInput;
    }
    uint64_t available_file_page_count = OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE;
    if (!TryAdd(input.clean_file_page_count, input.dirty_file_page_count,
                available_file_page_count)) {
        return MemoryReclaimPlanStatus::CounterOverflow;
    }
    const uint64_t available_anonymous_page_count =
        input.swappiness == OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE
            ? OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE
            : Minimum(input.anonymous_page_count, input.free_swap_page_count);

    const uint64_t target_quotient =
        input.target_page_count / OS_KERNEL_MEMORY_PRESSURE_MAXIMUM_SWAPPINESS;
    const uint64_t target_remainder =
        input.target_page_count % OS_KERNEL_MEMORY_PRESSURE_MAXIMUM_SWAPPINESS;
    uint64_t desired_anonymous_page_count =
        target_quotient * input.swappiness +
        (target_remainder * input.swappiness) /
            OS_KERNEL_MEMORY_PRESSURE_MAXIMUM_SWAPPINESS;
    if (input.target_page_count >= OS_KERNEL_MEMORY_PRESSURE_MINIMUM_FAIR_TARGET_PAGE_COUNT &&
        available_file_page_count != OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE &&
        available_anonymous_page_count != OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE &&
        input.swappiness != OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE &&
        input.swappiness != OS_KERNEL_MEMORY_PRESSURE_MAXIMUM_SWAPPINESS) {
        desired_anonymous_page_count =
            Maximum(desired_anonymous_page_count, OS_KERNEL_MEMORY_PRESSURE_SINGLE_PAGE);
        desired_anonymous_page_count =
            Minimum(desired_anonymous_page_count,
                    input.target_page_count - OS_KERNEL_MEMORY_PRESSURE_SINGLE_PAGE);
    }

    const uint64_t desired_file_page_count =
        input.target_page_count - desired_anonymous_page_count;
    plan.file_budget_page_count =
        Minimum(desired_file_page_count, available_file_page_count);
    plan.anonymous_budget_page_count =
        Minimum(desired_anonymous_page_count, available_anonymous_page_count);
    uint64_t planned_budget_page_count = OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE;
    if (!TryAdd(plan.file_budget_page_count, plan.anonymous_budget_page_count,
                planned_budget_page_count)) {
        return MemoryReclaimPlanStatus::CounterOverflow;
    }
    uint64_t remaining_page_count = input.target_page_count - planned_budget_page_count;
    const uint64_t additional_file_page_count =
        Minimum(remaining_page_count, available_file_page_count - plan.file_budget_page_count);
    plan.file_budget_page_count += additional_file_page_count;
    remaining_page_count -= additional_file_page_count;
    const uint64_t additional_anonymous_page_count =
        Minimum(remaining_page_count,
                available_anonymous_page_count - plan.anonymous_budget_page_count);
    plan.anonymous_budget_page_count += additional_anonymous_page_count;
    remaining_page_count -= additional_anonymous_page_count;

    plan.clean_file_page_count =
        Minimum(plan.file_budget_page_count, input.clean_file_page_count);
    plan.writeback_file_page_count = plan.file_budget_page_count - plan.clean_file_page_count;
    plan.swap_out_page_count = plan.anonymous_budget_page_count;
    uint64_t planned_page_count = OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE;
    if (!TryAdd(plan.clean_file_page_count, plan.writeback_file_page_count, planned_page_count) ||
        !TryAdd(planned_page_count, plan.swap_out_page_count, planned_page_count)) {
        return MemoryReclaimPlanStatus::CounterOverflow;
    }
    plan.planned_reclaim_page_count = planned_page_count;
    plan.unreclaimable_page_count = remaining_page_count;
    return MemoryReclaimPlanStatus::Succeeded;
}

MemoryReclaimExecutionStatus ExecuteMemoryReclaim(const MemoryReclaimPlan &plan,
                                                  const MemoryReclaimOperations &operations,
                                                  void *const context,
                                                  MemoryReclaimExecutionResult &result) noexcept {
    result = MemoryReclaimExecutionResult{};
    if ((plan.clean_file_page_count != OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE &&
         operations.reclaim_clean_file_pages == nullptr) ||
        (plan.writeback_file_page_count != OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE &&
         operations.writeback_and_reclaim_file_pages == nullptr) ||
        (plan.swap_out_page_count != OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE &&
         operations.swap_out_anonymous_pages == nullptr)) {
        return MemoryReclaimExecutionStatus::InvalidOperations;
    }
    if (plan.clean_file_page_count != OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE &&
        (!operations.reclaim_clean_file_pages(context, plan.clean_file_page_count,
                                              result.clean_file_page_count) ||
         result.clean_file_page_count > plan.clean_file_page_count)) {
        return MemoryReclaimExecutionStatus::CleanReclaimFailed;
    }
    if (plan.writeback_file_page_count != OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE &&
        (!operations.writeback_and_reclaim_file_pages(context, plan.writeback_file_page_count,
                                                      result.reclaimed_written_file_page_count) ||
         result.reclaimed_written_file_page_count > plan.writeback_file_page_count)) {
        return MemoryReclaimExecutionStatus::FileWritebackFailed;
    }
    if (plan.swap_out_page_count != OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE &&
        (!operations.swap_out_anonymous_pages(context, plan.swap_out_page_count,
                                              result.swapped_anonymous_page_count) ||
         result.swapped_anonymous_page_count > plan.swap_out_page_count)) {
        return MemoryReclaimExecutionStatus::AnonymousSwapFailed;
    }
    uint64_t reclaimed_page_count = OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE;
    if (!TryAdd(result.clean_file_page_count, result.reclaimed_written_file_page_count,
                reclaimed_page_count) ||
        !TryAdd(reclaimed_page_count, result.swapped_anonymous_page_count, reclaimed_page_count)) {
        return MemoryReclaimExecutionStatus::CounterOverflow;
    }
    result.reclaimed_page_count = reclaimed_page_count;
    return plan.planned_reclaim_page_count != OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE &&
                   reclaimed_page_count == OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE
               ? MemoryReclaimExecutionStatus::NoProgress
               : MemoryReclaimExecutionStatus::Succeeded;
}

MemoryOvercommitStatus MemoryOvercommitAccountant::Initialize(
    const MemoryOvercommitConfiguration &configuration) noexcept {
    if (this->initialized_) {
        return MemoryOvercommitStatus::AlreadyInitialized;
    }
    if (!OvercommitModeIsValid(configuration.mode) ||
        configuration.physical_page_count == OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE ||
        configuration.overcommit_ratio_percent > OS_KERNEL_MEMORY_PRESSURE_PERCENT_DENOMINATOR) {
        return MemoryOvercommitStatus::InvalidConfiguration;
    }
    uint64_t normal_limit = OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE;
    uint64_t privileged_limit = OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE;
    if (!CalculateCommitLimits(configuration, normal_limit, privileged_limit)) {
        return MemoryOvercommitStatus::InvalidConfiguration;
    }
    this->configuration_ = configuration;
    this->statistics_ = MemoryOvercommitStatistics{
        .mode = configuration.mode,
        .normal_commit_limit_page_count = normal_limit,
        .privileged_commit_limit_page_count = privileged_limit,
        .committed_page_count = OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE,
        .peak_committed_page_count = OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE,
        .successful_commit_count = OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE,
        .rejected_commit_count = OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE,
        .uncommit_count = OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE,
    };
    this->initialized_ = true;
    return MemoryOvercommitStatus::Succeeded;
}

MemoryOvercommitStatus MemoryOvercommitAccountant::TryCommit(const uint64_t page_count,
                                                             const bool privileged) noexcept {
    if (!this->initialized_) {
        return MemoryOvercommitStatus::NotInitialized;
    }
    if (page_count == OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE) {
        return MemoryOvercommitStatus::InvalidRequest;
    }
    const uint64_t limit = privileged ? this->statistics_.privileged_commit_limit_page_count
                                      : this->statistics_.normal_commit_limit_page_count;
    if (page_count > limit || this->statistics_.committed_page_count > limit - page_count) {
        if (this->statistics_.rejected_commit_count == UINT64_MAX) {
            return MemoryOvercommitStatus::CounterOverflow;
        }
        ++this->statistics_.rejected_commit_count;
        return MemoryOvercommitStatus::CommitLimitExceeded;
    }
    if (this->statistics_.successful_commit_count == UINT64_MAX) {
        return MemoryOvercommitStatus::CounterOverflow;
    }
    this->statistics_.committed_page_count += page_count;
    this->statistics_.peak_committed_page_count = Maximum(
        this->statistics_.peak_committed_page_count, this->statistics_.committed_page_count);
    ++this->statistics_.successful_commit_count;
    return MemoryOvercommitStatus::Succeeded;
}

MemoryOvercommitStatus MemoryOvercommitAccountant::Uncommit(const uint64_t page_count) noexcept {
    if (!this->initialized_) {
        return MemoryOvercommitStatus::NotInitialized;
    }
    if (page_count == OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE) {
        return MemoryOvercommitStatus::InvalidRequest;
    }
    if (page_count > this->statistics_.committed_page_count) {
        return MemoryOvercommitStatus::CommitAccountingUnderflow;
    }
    if (this->statistics_.uncommit_count == UINT64_MAX) {
        return MemoryOvercommitStatus::CounterOverflow;
    }
    this->statistics_.committed_page_count -= page_count;
    ++this->statistics_.uncommit_count;
    return MemoryOvercommitStatus::Succeeded;
}

MemoryOvercommitStatistics MemoryOvercommitAccountant::Statistics() const noexcept {
    return this->initialized_ ? this->statistics_ : MemoryOvercommitStatistics{};
}

MemoryOvercommitStatus MemoryOvercommitAccountant::Validate() const noexcept {
    if (!this->initialized_) {
        return MemoryOvercommitStatus::NotInitialized;
    }
    return OvercommitModeIsValid(this->statistics_.mode) &&
                   this->statistics_.normal_commit_limit_page_count <=
                       this->statistics_.privileged_commit_limit_page_count &&
                   this->statistics_.committed_page_count <=
                       this->statistics_.privileged_commit_limit_page_count &&
                   this->statistics_.peak_committed_page_count >=
                       this->statistics_.committed_page_count &&
                   this->statistics_.peak_committed_page_count <=
                       this->statistics_.privileged_commit_limit_page_count
               ? MemoryOvercommitStatus::Succeeded
               : MemoryOvercommitStatus::Corrupt;
}

OomSelectionStatus SelectOomVictim(const OomCandidate *const candidates,
                                   const uint64_t candidate_count,
                                   const uint64_t allowed_page_count, OomVictim &victim) noexcept {
    victim = OomVictim{};
    if (candidates == nullptr || candidate_count == OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE ||
        allowed_page_count == OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE) {
        return OomSelectionStatus::InvalidInput;
    }
    bool found = false;
    for (uint64_t candidate_index = OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE;
         candidate_index < candidate_count; ++candidate_index) {
        const OomCandidate &candidate = candidates[candidate_index];
        if (!candidate.active || candidate.protected_process ||
            candidate.process_id == OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE ||
            candidate.process_id == OS_KERNEL_MEMORY_PRESSURE_INIT_PROCESS_IDENTIFIER ||
            candidate.score_adjustment < OS_KERNEL_MEMORY_PRESSURE_OOM_SCORE_ADJUST_MINIMUM ||
            candidate.score_adjustment > OS_KERNEL_MEMORY_PRESSURE_OOM_SCORE_ADJUST_MAXIMUM ||
            candidate.score_adjustment == OS_KERNEL_MEMORY_PRESSURE_OOM_SCORE_ADJUST_MINIMUM) {
            continue;
        }
        uint64_t memory_page_count = OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE;
        if (!TryAdd(candidate.resident_page_count, candidate.swapped_page_count,
                    memory_page_count)) {
            return OomSelectionStatus::CounterOverflow;
        }
        if (memory_page_count == OS_KERNEL_MEMORY_PRESSURE_EMPTY_VALUE) {
            continue;
        }
        const uint64_t score =
            CalculateOomScore(memory_page_count, allowed_page_count, candidate.score_adjustment);
        if (!found || score > victim.score ||
            (score == victim.score && memory_page_count > victim.memory_page_count) ||
            (score == victim.score && memory_page_count == victim.memory_page_count &&
             candidate.process_id < victim.process_id)) {
            victim = OomVictim{
                .process_id = candidate.process_id,
                .memory_page_count = memory_page_count,
                .score = score,
                .candidate_index = candidate_index,
            };
            found = true;
        }
    }
    return found ? OomSelectionStatus::Succeeded : OomSelectionStatus::NoEligibleProcess;
}

}

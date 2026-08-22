#pragma once

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_MEMORY_PRESSURE_WATERMARK_SCALE_DENOMINATOR = 10000ULL;
inline constexpr uint64_t OS_KERNEL_MEMORY_PRESSURE_DEFAULT_WATERMARK_SCALE_FACTOR = 10ULL;
inline constexpr uint64_t OS_KERNEL_MEMORY_PRESSURE_MINIMUM_FREE_PAGE_FLOOR = 256ULL;
inline constexpr uint64_t OS_KERNEL_MEMORY_PRESSURE_MINIMUM_FREE_PAGE_CEILING = 65536ULL;
inline constexpr uint64_t OS_KERNEL_MEMORY_PRESSURE_DEFAULT_OVERCOMMIT_RATIO_PERCENT = 50ULL;
inline constexpr uint64_t OS_KERNEL_MEMORY_PRESSURE_PERCENT_DENOMINATOR = 100ULL;
inline constexpr uint64_t OS_KERNEL_MEMORY_PRESSURE_DEFAULT_SWAPPINESS = 60ULL;
inline constexpr uint64_t OS_KERNEL_MEMORY_PRESSURE_MAXIMUM_SWAPPINESS = 200ULL;
inline constexpr int64_t OS_KERNEL_MEMORY_PRESSURE_OOM_SCORE_ADJUST_MINIMUM = -1000LL;
inline constexpr int64_t OS_KERNEL_MEMORY_PRESSURE_OOM_SCORE_ADJUST_MAXIMUM = 1000LL;
inline constexpr uint64_t OS_KERNEL_MEMORY_PRESSURE_OOM_BASE_SCORE_MAXIMUM = 1000ULL;
inline constexpr uint64_t OS_KERNEL_MEMORY_PRESSURE_OOM_ADJUSTED_SCORE_MAXIMUM = 2000ULL;

enum class MemoryPressureLevel : uint64_t {
    Balanced,
    BelowHigh,
    BelowLow,
    BelowMinimum,
};

enum class MemoryAllocationClass : uint64_t {
    User,
    Kernel,
    Reclaim,
};

enum class MemoryAllocationAction : uint64_t {
    Allow,
    Reclaim,
    Reject,
};

// 数值与 Linux overcommit_memory 的 0、1、2 保持一致，便于用户空间迁移配置。
enum class MemoryOvercommitMode : uint64_t {
    Heuristic = 0ULL,
    Always = 1ULL,
    Never = 2ULL,
};

struct MemoryWatermarks final {
    uint64_t resident_limit_page_count;
    uint64_t minimum_free_page_count;
    uint64_t low_free_page_count;
    uint64_t high_free_page_count;
};

struct MemoryPressureConfiguration final {
    uint64_t managed_page_count;
    uint64_t initial_resident_page_count;
    uint64_t resident_limit_page_count;
    uint64_t swap_page_count;
    uint64_t watermark_scale_factor;
    uint64_t swappiness;
};

struct MemoryAllocationDecision final {
    MemoryAllocationAction action;
    MemoryPressureLevel level;
    uint64_t requested_page_count;
    uint64_t target_reclaim_page_count;
};

struct MemoryPressureStatistics final {
    MemoryWatermarks watermarks;
    uint64_t managed_page_count;
    uint64_t resident_page_count;
    uint64_t peak_resident_page_count;
    uint64_t swap_page_count;
    uint64_t allocation_request_count;
    uint64_t allowed_allocation_count;
    uint64_t reclaim_request_count;
    uint64_t rejected_allocation_count;
    uint64_t reclaim_attempt_count;
    uint64_t reclaimed_page_count;
};

enum class MemoryPressureStatus : uint64_t {
    Succeeded,
    NotInitialized,
    AlreadyInitialized,
    InvalidConfiguration,
    InvalidRequest,
    ResidentLimitExceeded,
    ResidentAccountingUnderflow,
    CounterOverflow,
    Corrupt,
};

[[nodiscard]] MemoryPressureStatus CalculateMemoryWatermarks(uint64_t resident_limit_page_count,
                                                             uint64_t watermark_scale_factor,
                                                             MemoryWatermarks &watermarks) noexcept;
[[nodiscard]] MemoryPressureLevel ClassifyMemoryPressure(const MemoryWatermarks &watermarks,
                                                         uint64_t free_page_count) noexcept;

class MemoryPressureController final {
  public:
    [[nodiscard]] MemoryPressureStatus
    Initialize(const MemoryPressureConfiguration &configuration) noexcept;
    [[nodiscard]] MemoryPressureStatus
    PrepareAllocation(uint64_t requested_page_count, MemoryAllocationClass allocation_class,
                      MemoryAllocationDecision &decision) noexcept;
    [[nodiscard]] MemoryPressureStatus CommitAllocation(uint64_t page_count) noexcept;
    [[nodiscard]] MemoryPressureStatus ReleaseResident(uint64_t page_count) noexcept;
    [[nodiscard]] MemoryPressureStatus SynchronizeResident(uint64_t observed_page_count) noexcept;
    [[nodiscard]] MemoryPressureStatus ConfigureSwap(uint64_t swap_page_count) noexcept;
    [[nodiscard]] MemoryPressureStatus
    ConfigureResidentLimit(uint64_t resident_limit_page_count) noexcept;
    [[nodiscard]] MemoryPressureStatus RecordReclaim(uint64_t reclaimed_page_count) noexcept;
    [[nodiscard]] MemoryPressureStatistics Statistics() const noexcept;
    [[nodiscard]] MemoryPressureStatus Validate() const noexcept;

  private:
    MemoryPressureConfiguration configuration_{};
    MemoryPressureStatistics statistics_{};
    bool initialized_{};
};

struct MemoryReclaimInput final {
    uint64_t target_page_count;
    uint64_t clean_file_page_count;
    uint64_t dirty_file_page_count;
    uint64_t anonymous_page_count;
    uint64_t free_swap_page_count;
    uint64_t swappiness;
};

struct MemoryReclaimPlan final {
    uint64_t file_budget_page_count;
    uint64_t anonymous_budget_page_count;
    uint64_t clean_file_page_count;
    uint64_t writeback_file_page_count;
    uint64_t swap_out_page_count;
    uint64_t planned_reclaim_page_count;
    uint64_t unreclaimable_page_count;
};

enum class MemoryReclaimPlanStatus : uint64_t {
    Succeeded,
    InvalidInput,
    CounterOverflow,
};

[[nodiscard]] MemoryReclaimPlanStatus PlanMemoryReclaim(const MemoryReclaimInput &input,
                                                        MemoryReclaimPlan &plan) noexcept;

using MemoryReclaimOperation = bool (*)(void *context, uint64_t requested_page_count,
                                        uint64_t &reclaimed_page_count) noexcept;

struct MemoryReclaimOperations final {
    MemoryReclaimOperation reclaim_clean_file_pages;
    MemoryReclaimOperation writeback_and_reclaim_file_pages;
    MemoryReclaimOperation swap_out_anonymous_pages;
};

struct MemoryReclaimExecutionResult final {
    uint64_t clean_file_page_count;
    uint64_t reclaimed_written_file_page_count;
    uint64_t swapped_anonymous_page_count;
    uint64_t reclaimed_page_count;
};

enum class MemoryReclaimExecutionStatus : uint64_t {
    Succeeded,
    InvalidOperations,
    CleanReclaimFailed,
    FileWritebackFailed,
    AnonymousSwapFailed,
    CounterOverflow,
    NoProgress,
};

[[nodiscard]] MemoryReclaimExecutionStatus ExecuteMemoryReclaim(
    const MemoryReclaimPlan &plan, const MemoryReclaimOperations &operations, void *context,
    MemoryReclaimExecutionResult &result) noexcept;

struct MemoryOvercommitConfiguration final {
    MemoryOvercommitMode mode;
    uint64_t physical_page_count;
    uint64_t swap_page_count;
    uint64_t overcommit_ratio_percent;
    uint64_t admin_reserve_page_count;
    uint64_t user_reserve_page_count;
};

struct MemoryOvercommitStatistics final {
    MemoryOvercommitMode mode;
    uint64_t normal_commit_limit_page_count;
    uint64_t privileged_commit_limit_page_count;
    uint64_t committed_page_count;
    uint64_t peak_committed_page_count;
    uint64_t successful_commit_count;
    uint64_t rejected_commit_count;
    uint64_t uncommit_count;
};

enum class MemoryOvercommitStatus : uint64_t {
    Succeeded,
    NotInitialized,
    AlreadyInitialized,
    InvalidConfiguration,
    InvalidRequest,
    CommitLimitExceeded,
    CommitAccountingUnderflow,
    CounterOverflow,
    Corrupt,
};

class MemoryOvercommitAccountant final {
  public:
    [[nodiscard]] MemoryOvercommitStatus
    Initialize(const MemoryOvercommitConfiguration &configuration) noexcept;
    [[nodiscard]] MemoryOvercommitStatus TryCommit(uint64_t page_count, bool privileged) noexcept;
    [[nodiscard]] MemoryOvercommitStatus Uncommit(uint64_t page_count) noexcept;
    [[nodiscard]] MemoryOvercommitStatistics Statistics() const noexcept;
    [[nodiscard]] MemoryOvercommitStatus Validate() const noexcept;

  private:
    MemoryOvercommitConfiguration configuration_{};
    MemoryOvercommitStatistics statistics_{};
    bool initialized_{};
};

struct OomCandidate final {
    uint64_t process_id;
    uint64_t resident_page_count;
    uint64_t swapped_page_count;
    int64_t score_adjustment;
    bool protected_process;
    bool active;
};

struct OomVictim final {
    uint64_t process_id;
    uint64_t memory_page_count;
    uint64_t score;
    uint64_t candidate_index;
};

enum class OomSelectionStatus : uint64_t {
    Succeeded,
    InvalidInput,
    NoEligibleProcess,
    CounterOverflow,
};

[[nodiscard]] OomSelectionStatus SelectOomVictim(const OomCandidate *candidates,
                                                 uint64_t candidate_count,
                                                 uint64_t allowed_page_count,
                                                 OomVictim &victim) noexcept;

}

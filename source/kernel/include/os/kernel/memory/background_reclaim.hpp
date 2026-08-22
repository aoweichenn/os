#pragma once

#include <os/kernel/memory/memory_pressure.hpp>

#include <stdint.h>

namespace os::kernel {

enum class BackgroundReclaimState : uint64_t {
    Sleeping,
    Running,
    BackingOff,
};

enum class BackgroundReclaimAction : uint64_t {
    Sleep,
    Reclaim,
    Wait,
};

struct BackgroundReclaimConfiguration final {
    uint64_t batch_page_count;
    uint64_t no_progress_backoff_nanoseconds;
};

struct BackgroundReclaimDecision final {
    BackgroundReclaimState state;
    BackgroundReclaimAction action;
    uint64_t free_page_count;
    uint64_t target_page_count;
    uint64_t deadline_nanoseconds;
};

struct BackgroundReclaimBatchResult final {
    uint64_t requested_page_count;
    uint64_t clean_file_page_count;
    uint64_t swapped_anonymous_page_count;
    uint64_t reclaimed_page_count;
    uint64_t written_page_count;
    bool failed;
};

struct BackgroundReclaimStatistics final {
    BackgroundReclaimState state;
    uint64_t batch_page_count;
    uint64_t no_progress_backoff_nanoseconds;
    uint64_t wake_count;
    uint64_t sleep_count;
    uint64_t batch_count;
    uint64_t requested_page_count;
    uint64_t clean_file_page_count;
    uint64_t swapped_anonymous_page_count;
    uint64_t reclaimed_page_count;
    uint64_t written_page_count;
    uint64_t no_progress_count;
    uint64_t failure_count;
    uint64_t backoff_count;
    uint64_t resume_count;
    uint64_t reset_count;
    uint64_t next_deadline_nanoseconds;
    bool initialized;
};

enum class BackgroundReclaimStatus : uint64_t {
    Succeeded,
    NotInitialized,
    AlreadyInitialized,
    InvalidConfiguration,
    InvalidPressureSample,
    InvalidBatchResult,
    InvalidState,
    CounterOverflow,
    Corrupt,
};

class BackgroundReclaimController final {
  public:
    [[nodiscard]] BackgroundReclaimStatus
    Initialize(const BackgroundReclaimConfiguration &configuration) noexcept;
    [[nodiscard]] BackgroundReclaimStatus Evaluate(const MemoryWatermarks &watermarks,
                                                   uint64_t resident_page_count,
                                                   uint64_t now_nanoseconds,
                                                   BackgroundReclaimDecision &decision) noexcept;
    [[nodiscard]] BackgroundReclaimStatus RecordBatch(const BackgroundReclaimBatchResult &result,
                                                      uint64_t now_nanoseconds) noexcept;
    [[nodiscard]] BackgroundReclaimStatus Reset() noexcept;
    [[nodiscard]] BackgroundReclaimStatistics Statistics() const noexcept;
    [[nodiscard]] BackgroundReclaimStatus Validate() const noexcept;

  private:
    [[nodiscard]] static bool StateIsValid(BackgroundReclaimState state) noexcept;
    [[nodiscard]] bool AddCounter(uint64_t &counter, uint64_t increment) noexcept;
    [[nodiscard]] BackgroundReclaimStatus EnterBackoff(uint64_t now_nanoseconds) noexcept;

    BackgroundReclaimConfiguration configuration_{};
    BackgroundReclaimStatistics statistics_{};
    bool initialized_{};
};

}

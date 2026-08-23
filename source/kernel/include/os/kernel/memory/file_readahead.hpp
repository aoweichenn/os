#pragma once

#include <os/kernel/memory/memory_pressure.hpp>

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_FILE_READAHEAD_DEFAULT_MAXIMUM_WINDOW_PAGE_COUNT = 32ULL;

enum class FileReadaheadTrigger : uint64_t {
    DemandHit,
    DemandMiss,
    PrefetchedHit,
};

enum class FileReadaheadAction : uint64_t {
    None,
    Submit,
};

enum class FileReadaheadStatus : uint64_t {
    Succeeded,
    NotInitialized,
    AlreadyInitialized,
    InvalidConfiguration,
    InvalidAccess,
    InvalidFeedback,
    CounterOverflow,
    GenerationExhausted,
    Corrupt,
};

struct FileReadaheadConfiguration final {
    uint64_t maximum_window_page_count;
};

struct FileReadaheadAccess final {
    uint64_t first_page_index;
    uint64_t requested_page_count;
    uint64_t file_page_count;
    FileReadaheadTrigger trigger;
    MemoryPressureLevel pressure_level;
};

struct FileReadaheadDecision final {
    FileReadaheadAction action;
    uint64_t generation;
    uint64_t window_start_page_index;
    uint64_t window_page_count;
    uint64_t prefetch_start_page_index;
    uint64_t prefetch_page_count;
    uint64_t trigger_page_index;
    uint64_t effective_maximum_window_page_count;
    bool sequential_access;
    bool stream_reset;
};

struct FileReadaheadStatistics final {
    uint64_t configured_maximum_window_page_count;
    uint64_t adaptive_maximum_window_page_count;
    uint64_t effective_maximum_window_page_count;
    uint64_t window_start_page_index;
    uint64_t window_page_count;
    uint64_t asynchronous_page_count;
    uint64_t trigger_page_index;
    uint64_t next_expected_page_index;
    uint64_t generation;
    uint64_t access_count;
    uint64_t initial_access_count;
    uint64_t sequential_access_count;
    uint64_t random_access_count;
    uint64_t demand_hit_access_count;
    uint64_t demand_miss_access_count;
    uint64_t prefetched_hit_access_count;
    uint64_t submission_decision_count;
    uint64_t planned_window_page_count;
    uint64_t planned_prefetch_page_count;
    uint64_t window_growth_count;
    uint64_t window_shrink_count;
    uint64_t stream_reset_count;
    uint64_t feedback_count;
    uint64_t useful_prefetched_page_count;
    uint64_t wasted_prefetched_page_count;
    uint64_t feedback_shrink_count;
    uint64_t feedback_recovery_count;
    uint64_t pressure_limited_access_count;
    uint64_t pressure_disabled_access_count;
    MemoryPressureLevel pressure_level;
    bool stream_active;
    bool window_active;
};

class FileReadaheadPolicy final {
  public:
    [[nodiscard]] FileReadaheadStatus
    Initialize(const FileReadaheadConfiguration &configuration) noexcept;
    [[nodiscard]] FileReadaheadStatus ObserveAccess(const FileReadaheadAccess &access,
                                                    FileReadaheadDecision &decision) noexcept;
    [[nodiscard]] FileReadaheadStatus RecordFeedback(uint64_t useful_page_count,
                                                     uint64_t wasted_page_count) noexcept;
    [[nodiscard]] FileReadaheadStatus Reset() noexcept;
    [[nodiscard]] FileReadaheadStatistics Statistics() const noexcept;
    [[nodiscard]] FileReadaheadStatus Validate() const noexcept;

  private:
    FileReadaheadConfiguration configuration_{};
    FileReadaheadStatistics statistics_{};
    bool initialized_{};
};

}

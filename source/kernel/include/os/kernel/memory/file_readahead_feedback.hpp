#pragma once

#include <os/kernel/memory/file_cache_identity.hpp>
#include <os/kernel/sync/spin_lock.hpp>

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_FILE_READAHEAD_STREAM_INVALID_SLOT_INDEX = UINT64_MAX;

struct FileReadaheadStreamToken final {
    uint64_t slot_index;
    uint64_t generation;
};

struct FileReadaheadFeedback final {
    uint64_t useful_page_count;
    uint64_t wasted_page_count;
};

struct FileReadaheadPageTag final {
    FileReadaheadStreamToken stream;
    uint64_t policy_generation;
};

enum class FileReadaheadFeedbackSlotState : uint64_t {
    Free,
    Active,
    Retiring,
};

struct FileReadaheadFeedbackSlot final {
    FileCacheIdentity file_identity;
    FileReadaheadFeedback pending_feedback;
    uint64_t active_task_count;
    uint64_t generation;
    FileReadaheadFeedbackSlotState state;
};

struct FileReadaheadFeedbackStatistics final {
    uint64_t capacity;
    uint64_t active_stream_count;
    uint64_t retiring_stream_count;
    uint64_t active_task_count;
    uint64_t peak_stream_count;
    uint64_t peak_active_task_count;
    uint64_t registration_count;
    uint64_t retirement_count;
    uint64_t stream_release_count;
    uint64_t task_retain_count;
    uint64_t task_release_count;
    uint64_t useful_page_record_count;
    uint64_t wasted_page_record_count;
    uint64_t feedback_take_count;
    uint64_t useful_page_take_count;
    uint64_t wasted_page_take_count;
    uint64_t stale_useful_page_drop_count;
    uint64_t stale_wasted_page_drop_count;
};

enum class FileReadaheadFeedbackStatus : uint64_t {
    Succeeded,
    NotInitialized,
    AlreadyInitialized,
    InvalidStorage,
    InvalidCapacity,
    InvalidIdentity,
    InvalidToken,
    InvalidFeedback,
    CapacityExhausted,
    GenerationExhausted,
    CounterOverflow,
    InvalidState,
    Corrupt,
};

class FileReadaheadFeedbackLedger final {
  public:
    [[nodiscard]] FileReadaheadFeedbackStatus Initialize(FileReadaheadFeedbackSlot *slot_storage,
                                                         uint64_t capacity) noexcept;
    [[nodiscard]] FileReadaheadFeedbackStatus
    RegisterStream(const FileCacheIdentity &file_identity,
                   FileReadaheadStreamToken &token) noexcept;
    [[nodiscard]] FileReadaheadFeedbackStatus RetainTask(FileReadaheadStreamToken token) noexcept;
    [[nodiscard]] FileReadaheadFeedbackStatus ReleaseTask(FileReadaheadStreamToken token) noexcept;
    [[nodiscard]] FileReadaheadFeedbackStatus
    Record(FileReadaheadStreamToken token, const FileReadaheadFeedback &feedback) noexcept;
    [[nodiscard]] FileReadaheadFeedbackStatus Take(FileReadaheadStreamToken token,
                                                   FileReadaheadFeedback &feedback) noexcept;
    [[nodiscard]] FileReadaheadFeedbackStatus RetireStream(FileReadaheadStreamToken token) noexcept;
    [[nodiscard]] FileReadaheadFeedbackStatistics Statistics() const noexcept;
    [[nodiscard]] FileReadaheadFeedbackStatus Validate() const noexcept;

  private:
    [[nodiscard]] bool TokenIsValid(FileReadaheadStreamToken token) const noexcept;
    [[nodiscard]] uint64_t FindFreeSlotIndex() const noexcept;
    [[nodiscard]] bool CanReleaseRetiredSlot(const FileReadaheadFeedbackSlot &slot) const noexcept;
    [[nodiscard]] bool ReleaseRetiredSlot(FileReadaheadFeedbackSlot &slot) noexcept;

    mutable SpinLock lock_{};
    FileReadaheadFeedbackSlot *slots_{};
    uint64_t capacity_{};
    FileReadaheadFeedbackStatistics statistics_{};
    bool initialized_{};
};

[[nodiscard]] bool FileReadaheadStreamTokenIsValid(FileReadaheadStreamToken token) noexcept;
[[nodiscard]] bool FileReadaheadStreamTokensEqual(FileReadaheadStreamToken left,
                                                  FileReadaheadStreamToken right) noexcept;
[[nodiscard]] bool FileReadaheadPageTagIsValid(const FileReadaheadPageTag &tag) noexcept;
[[nodiscard]] bool FileReadaheadPageTagIsEmpty(const FileReadaheadPageTag &tag) noexcept;

}

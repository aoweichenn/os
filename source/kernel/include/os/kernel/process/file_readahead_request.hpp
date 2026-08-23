#pragma once

#include <os/kernel/fs/vfs.hpp>
#include <os/kernel/memory/file_readahead_feedback.hpp>
#include <os/kernel/sync/spin_lock.hpp>

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_FILE_READAHEAD_REQUEST_INVALID_SLOT_INDEX = UINT64_MAX;

enum class FileReadaheadRequestState : uint64_t {
    Free,
    Queued,
    Running,
};

struct FileReadaheadRequest final {
    fs::Vfs *vfs;
    fs::OpenFile open_file;
    uint64_t start_page_index;
    uint64_t page_count;
    uint64_t policy_generation;
    FileReadaheadStreamToken stream;
};

struct FileReadaheadRequestToken final {
    uint64_t slot_index;
    uint64_t generation;
};

struct FileReadaheadRequestSlot final {
    FileReadaheadRequest request;
    uint64_t generation;
    FileReadaheadRequestState state;
    bool cancellation_requested;
};

struct FileReadaheadRequestStatistics final {
    uint64_t capacity;
    uint64_t active_request_count;
    uint64_t queued_request_count;
    uint64_t running_request_count;
    uint64_t peak_active_request_count;
    uint64_t enqueue_count;
    uint64_t acquisition_count;
    uint64_t completion_count;
    uint64_t capacity_rejection_count;
    uint64_t queued_cancellation_count;
    uint64_t running_cancellation_request_count;
    uint64_t active_running_cancellation_count;
    uint64_t cancelled_completion_count;
};

enum class FileReadaheadRequestStatus : uint64_t {
    Succeeded,
    NotInitialized,
    AlreadyInitialized,
    InvalidStorage,
    InvalidCapacity,
    InvalidRequest,
    InvalidToken,
    CapacityExhausted,
    NoQueuedRequest,
    GenerationExhausted,
    CounterOverflow,
    InvalidState,
    Corrupt,
};

class FileReadaheadRequestQueue final {
  public:
    [[nodiscard]] FileReadaheadRequestStatus Initialize(FileReadaheadRequestSlot *slot_storage,
                                                        uint64_t *ready_storage,
                                                        uint64_t capacity) noexcept;
    [[nodiscard]] FileReadaheadRequestStatus Enqueue(const FileReadaheadRequest &request,
                                                     FileReadaheadRequestToken &token) noexcept;
    [[nodiscard]] FileReadaheadRequestStatus Acquire(FileReadaheadRequestToken &token,
                                                     FileReadaheadRequest &request) noexcept;
    [[nodiscard]] FileReadaheadRequestStatus Complete(FileReadaheadRequestToken token) noexcept;
    [[nodiscard]] FileReadaheadRequestStatus
    CancelStream(FileReadaheadStreamToken stream, uint64_t maximum_policy_generation,
                 FileReadaheadRequest *cancelled_request_storage,
                 uint64_t cancelled_request_capacity, uint64_t &cancelled_request_count) noexcept;
    [[nodiscard]] FileReadaheadRequestStatus
    CancelFile(const FileCacheIdentity &identity, FileReadaheadRequest *cancelled_request_storage,
               uint64_t cancelled_request_capacity, uint64_t &cancelled_request_count) noexcept;
    [[nodiscard]] FileReadaheadRequestStatus
    CancellationRequested(FileReadaheadRequestToken token, bool &cancellation_requested) noexcept;
    [[nodiscard]] FileReadaheadRequestStatistics Statistics() const noexcept;
    [[nodiscard]] FileReadaheadRequestStatus Validate() const noexcept;

  private:
    [[nodiscard]] bool RequestIsValid(const FileReadaheadRequest &request) const noexcept;
    [[nodiscard]] bool TokenIsValid(FileReadaheadRequestToken token) const noexcept;
    [[nodiscard]] uint64_t FindFreeSlotIndex() const noexcept;
    [[nodiscard]] FileReadaheadRequestStatus
    CancelMatching(const FileReadaheadStreamToken *stream, const FileCacheIdentity *identity,
                   uint64_t maximum_policy_generation,
                   FileReadaheadRequest *cancelled_request_storage,
                   uint64_t cancelled_request_capacity, uint64_t &cancelled_request_count) noexcept;
    [[nodiscard]] bool RequestMatches(const FileReadaheadRequest &request,
                                      const FileReadaheadStreamToken *stream,
                                      const FileCacheIdentity *identity,
                                      uint64_t maximum_policy_generation) const noexcept;

    mutable SpinLock lock_{};
    FileReadaheadRequestSlot *slots_{};
    uint64_t *ready_storage_{};
    uint64_t capacity_{};
    uint64_t ready_head_{};
    uint64_t ready_tail_{};
    uint64_t ready_count_{};
    FileReadaheadRequestStatistics statistics_{};
    bool initialized_{};
};

}

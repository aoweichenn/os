#pragma once

#include <os/kernel/fs/vfs.hpp>
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
};

struct FileReadaheadRequestToken final {
    uint64_t slot_index;
    uint64_t generation;
};

struct FileReadaheadRequestSlot final {
    FileReadaheadRequest request;
    uint64_t generation;
    FileReadaheadRequestState state;
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
    [[nodiscard]] FileReadaheadRequestStatistics Statistics() const noexcept;
    [[nodiscard]] FileReadaheadRequestStatus Validate() const noexcept;

  private:
    [[nodiscard]] bool RequestIsValid(const FileReadaheadRequest &request) const noexcept;
    [[nodiscard]] bool TokenIsValid(FileReadaheadRequestToken token) const noexcept;
    [[nodiscard]] uint64_t FindFreeSlotIndex() const noexcept;

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

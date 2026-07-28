#pragma once

#include "os/kernel/device/device_model.hpp"

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_BLOCK_REQUEST_INVALID_INDEX = UINT64_MAX;
inline constexpr uint64_t OS_KERNEL_BLOCK_REQUEST_KERNEL_OWNER_THREAD_INDEX = UINT64_MAX;
inline constexpr uint64_t OS_KERNEL_BLOCK_REQUEST_FIRST_IDENTIFIER = 1ULL;

enum class BlockOperation : uint64_t {
    Read,
    Write,
    Flush,
};

enum class BlockRequestState : uint64_t {
    Unused,
    Queued,
    Issued,
    Completed,
};

enum class BlockRequestResult : uint64_t {
    None,
    Succeeded,
    DeviceError,
    TimedOut,
    Cancelled,
};

struct BlockRequest final {
    uint64_t identifier;
    BlockOperation operation;
    uint64_t logical_block_address;
    uint8_t *buffer;
    uint64_t buffer_size_bytes;
    uint64_t owner_thread_index;
    uint64_t deadline_nanoseconds;
    BlockRequestState state;
    BlockRequestResult result;
    uint64_t next_queue_index;
};

struct BlockRequestQueueStatistics final {
    uint64_t capacity;
    uint64_t active_request_count;
    uint64_t queued_request_count;
    uint64_t issued_request_count;
    uint64_t completed_request_count;
    uint64_t peak_active_request_count;
    uint64_t submission_count;
    uint64_t issue_count;
    uint64_t successful_completion_count;
    uint64_t device_error_completion_count;
    uint64_t timeout_completion_count;
    uint64_t cancellation_count;
    uint64_t reap_count;
    uint64_t duplicate_resolution_count;
    uint64_t capacity_rejection_count;
};

enum class BlockRequestQueueStatus : uint64_t {
    Succeeded,
    NotInitialized,
    AlreadyInitialized,
    InvalidStorage,
    InvalidCapacity,
    InvalidRequest,
    CapacityExhausted,
    IdentifierExhausted,
    RequestNotFound,
    RequestNotQueued,
    RequestNotIssued,
    RequestNotCompleted,
    RequestAlreadyResolved,
    Corrupt,
};

class BlockRequestQueue final {
  public:
    BlockRequestQueue() noexcept = default;
    BlockRequestQueue(const BlockRequestQueue &) = delete;
    BlockRequestQueue &operator=(const BlockRequestQueue &) = delete;

    [[nodiscard]] BlockRequestQueueStatus
    Initialize(BlockRequest *storage, uint64_t capacity) noexcept;
    [[nodiscard]] BlockRequestQueueStatus
    Submit(BlockOperation operation, uint64_t logical_block_address, uint8_t *buffer,
           uint64_t buffer_size_bytes, uint64_t owner_thread_index,
           uint64_t deadline_nanoseconds, uint64_t &request_identifier) noexcept;
    [[nodiscard]] BlockRequestQueueStatus
    IssueNext(BlockRequest &request, bool &issued) noexcept;
    [[nodiscard]] BlockRequestQueueStatus
    Complete(uint64_t request_identifier, BlockRequestResult result) noexcept;
    [[nodiscard]] BlockRequestQueueStatus
    ResolveTimeout(uint64_t now_nanoseconds, BlockRequest &request, bool &resolved) noexcept;
    [[nodiscard]] BlockRequestQueueStatus
    CancelQueued(uint64_t request_identifier) noexcept;
    [[nodiscard]] BlockRequestQueueStatus
    Read(uint64_t request_identifier, BlockRequest &request) const noexcept;
    [[nodiscard]] BlockRequestQueueStatus Reap(uint64_t request_identifier) noexcept;
    [[nodiscard]] BlockRequestQueueStatus Validate() const noexcept;
    [[nodiscard]] BlockRequestQueueStatistics Statistics() const noexcept;

  private:
    [[nodiscard]] bool RequestIsValid(BlockOperation operation, uint64_t logical_block_address,
                                      const uint8_t *buffer, uint64_t buffer_size_bytes,
                                      uint64_t deadline_nanoseconds) const noexcept;
    [[nodiscard]] BlockRequest *Find(uint64_t request_identifier) noexcept;
    [[nodiscard]] const BlockRequest *Find(uint64_t request_identifier) const noexcept;
    [[nodiscard]] bool FindFreeIndex(uint64_t &request_index) const noexcept;
    [[nodiscard]] uint64_t IndexOf(const BlockRequest &request) const noexcept;
    void AppendQueuedIndex(uint64_t request_index) noexcept;
    [[nodiscard]] bool RemoveQueuedIndex(uint64_t request_index) noexcept;
    void RecordResolution(BlockRequestResult result) noexcept;

    BlockRequest *storage_{nullptr};
    uint64_t capacity_{};
    uint64_t queue_head_index_{OS_KERNEL_BLOCK_REQUEST_INVALID_INDEX};
    uint64_t queue_tail_index_{OS_KERNEL_BLOCK_REQUEST_INVALID_INDEX};
    uint64_t issued_index_{OS_KERNEL_BLOCK_REQUEST_INVALID_INDEX};
    uint64_t next_identifier_{OS_KERNEL_BLOCK_REQUEST_FIRST_IDENTIFIER};
    BlockRequestQueueStatistics statistics_{};
    bool initialized_{};
};

}

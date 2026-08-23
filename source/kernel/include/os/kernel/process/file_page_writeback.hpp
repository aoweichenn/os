#pragma once

#include <os/kernel/memory/file_page_cache.hpp>

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_FILE_PAGE_WRITEBACK_INVALID_SLOT_INDEX = UINT64_MAX;

enum class FilePageWritebackState : uint64_t {
    Free,
    Writing,
    Completed,
};

enum class FilePageWritebackWaiterState : uint64_t {
    Free,
    Registered,
    Waiting,
    Ready,
};

enum class FilePageWritebackStatus : uint64_t {
    Succeeded,
    NotInitialized,
    AlreadyInitialized,
    InvalidStorage,
    InvalidCapacity,
    InvalidWriteback,
    InvalidToken,
    InvalidThread,
    CapacityExhausted,
    WritebackAlreadyRegistered,
    WritebackNotFound,
    WaiterAlreadyRegistered,
    InvalidState,
    GenerationExhausted,
    CounterOverflow,
    Corrupt,
};

struct FilePageWritebackSlot final {
    FilePageIdentity identity;
    uint64_t physical_address;
    uint64_t writeback_generation;
    uint64_t owner_thread_index;
    uint64_t generation;
    uint64_t registered_waiter_count;
    uint64_t waiting_waiter_count;
    uint64_t remaining_result_count;
    FilePageCacheStatus result;
    FilePageWritebackState state;
};

struct FilePageWritebackWaiter final {
    uint64_t slot_index;
    uint64_t generation;
    FilePageWritebackWaiterState state;
};

struct FilePageWritebackCompletionDecision final {
    uint64_t slot_index;
    uint64_t wake_count;
};

struct FilePageWritebackStatistics final {
    uint64_t writeback_capacity;
    uint64_t waiter_capacity;
    uint64_t active_writeback_count;
    uint64_t writing_writeback_count;
    uint64_t completed_writeback_count;
    uint64_t registered_waiter_count;
    uint64_t waiting_waiter_count;
    uint64_t ready_waiter_count;
    uint64_t peak_active_writeback_count;
    uint64_t peak_waiter_count;
    uint64_t begin_count;
    uint64_t waiter_registration_count;
    uint64_t wait_commit_count;
    uint64_t immediate_completion_count;
    uint64_t completion_count;
    uint64_t broadcast_wake_count;
    uint64_t failure_broadcast_count;
    uint64_t result_take_count;
    uint64_t capacity_rejection_count;
};

class FilePageWritebackCoordinator final {
  public:
    [[nodiscard]] FilePageWritebackStatus Initialize(FilePageWritebackSlot *writeback_storage,
                                                     uint64_t writeback_capacity,
                                                     FilePageWritebackWaiter *waiter_storage,
                                                     uint64_t waiter_capacity) noexcept;
    [[nodiscard]] FilePageWritebackStatus Begin(const FilePageIdentity &identity,
                                                uint64_t physical_address,
                                                uint64_t writeback_generation,
                                                uint64_t owner_thread_index,
                                                FilePageWritebackToken &token) noexcept;
    [[nodiscard]] FilePageWritebackStatus RegisterWaiter(const FilePageIdentity &identity,
                                                         uint64_t physical_address,
                                                         uint64_t writeback_generation,
                                                         uint64_t waiter_thread_index,
                                                         FilePageWritebackToken &token) noexcept;
    [[nodiscard]] FilePageWritebackStatus PrepareWait(FilePageWritebackToken token,
                                                      uint64_t waiter_thread_index,
                                                      bool &wait_required) noexcept;
    [[nodiscard]] FilePageWritebackStatus
    Complete(FilePageWritebackToken token, uint64_t owner_thread_index, FilePageCacheStatus result,
             FilePageWritebackCompletionDecision &decision) noexcept;
    [[nodiscard]] FilePageWritebackStatus TakeResult(FilePageWritebackToken token,
                                                     uint64_t waiter_thread_index,
                                                     FilePageCacheStatus &result) noexcept;
    [[nodiscard]] FilePageWritebackStatus Validate() const noexcept;
    [[nodiscard]] FilePageWritebackStatistics Statistics() const noexcept;

  private:
    [[nodiscard]] bool TokenIsValid(FilePageWritebackToken token) const noexcept;
    [[nodiscard]] bool ResultIsTerminal(FilePageCacheStatus result) const noexcept;
    [[nodiscard]] uint64_t FindFreeSlotIndex() const noexcept;
    [[nodiscard]] uint64_t FindWritebackSlotIndex(const FilePageIdentity &identity,
                                                  uint64_t physical_address,
                                                  uint64_t writeback_generation) const noexcept;
    [[nodiscard]] bool OwnerHasWritingWriteback(uint64_t owner_thread_index) const noexcept;
    [[nodiscard]] FilePageWritebackStatus ReleaseSlot(uint64_t slot_index) noexcept;

    FilePageWritebackSlot *writeback_storage_{};
    uint64_t writeback_capacity_{};
    FilePageWritebackWaiter *waiter_storage_{};
    uint64_t waiter_capacity_{};
    FilePageWritebackStatistics statistics_{};
    bool initialized_{};
};

}

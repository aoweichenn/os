#pragma once

#include <os/kernel/memory/file_page_cache.hpp>

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_FILE_PAGE_LOAD_INVALID_SLOT_INDEX = UINT64_MAX;

enum class FilePageLoadState : uint64_t {
    Free,
    Loading,
    Completed,
};

enum class FilePageLoadWaiterState : uint64_t {
    Free,
    Registered,
    Waiting,
    Ready,
};

enum class FilePageLoadStatus : uint64_t {
    Succeeded,
    NotInitialized,
    AlreadyInitialized,
    InvalidStorage,
    InvalidCapacity,
    InvalidLoad,
    InvalidToken,
    InvalidThread,
    CapacityExhausted,
    LoadAlreadyRegistered,
    LoadNotFound,
    WaiterAlreadyRegistered,
    InvalidState,
    GenerationExhausted,
    CounterOverflow,
    Corrupt,
};

struct FilePageLoadSlot final {
    FilePageIdentity identity;
    uint64_t physical_address;
    uint64_t load_generation;
    uint64_t owner_thread_index;
    uint64_t generation;
    uint64_t registered_waiter_count;
    uint64_t waiting_waiter_count;
    uint64_t remaining_result_count;
    FilePageCacheStatus result;
    FilePageLoadState state;
};

struct FilePageLoadWaiter final {
    uint64_t slot_index;
    uint64_t generation;
    FilePageLoadWaiterState state;
};

struct FilePageLoadCompletionDecision final {
    uint64_t slot_index;
    uint64_t wake_count;
};

struct FilePageLoadStatistics final {
    uint64_t load_capacity;
    uint64_t waiter_capacity;
    uint64_t active_load_count;
    uint64_t loading_load_count;
    uint64_t completed_load_count;
    uint64_t registered_waiter_count;
    uint64_t waiting_waiter_count;
    uint64_t ready_waiter_count;
    uint64_t peak_active_load_count;
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

class FilePageLoadCoordinator final {
  public:
    [[nodiscard]] FilePageLoadStatus Initialize(FilePageLoadSlot *load_storage,
                                                uint64_t load_capacity,
                                                FilePageLoadWaiter *waiter_storage,
                                                uint64_t waiter_capacity) noexcept;
    [[nodiscard]] FilePageLoadStatus Begin(const FilePageIdentity &identity,
                                           uint64_t physical_address, uint64_t load_generation,
                                           uint64_t owner_thread_index,
                                           FilePageLoadToken &token) noexcept;
    [[nodiscard]] FilePageLoadStatus RegisterWaiter(const FilePageIdentity &identity,
                                                    uint64_t physical_address,
                                                    uint64_t load_generation,
                                                    uint64_t waiter_thread_index,
                                                    FilePageLoadToken &token) noexcept;
    [[nodiscard]] FilePageLoadStatus PrepareWait(FilePageLoadToken token,
                                                 uint64_t waiter_thread_index,
                                                 bool &wait_required) noexcept;
    [[nodiscard]] FilePageLoadStatus RegisteredWaiterCount(FilePageLoadToken token,
                                                           uint64_t owner_thread_index,
                                                           uint64_t &waiter_count) const noexcept;
    [[nodiscard]] FilePageLoadStatus Complete(FilePageLoadToken token, uint64_t owner_thread_index,
                                              FilePageCacheStatus result,
                                              FilePageLoadCompletionDecision &decision) noexcept;
    [[nodiscard]] FilePageLoadStatus TakeResult(FilePageLoadToken token,
                                                uint64_t waiter_thread_index,
                                                FilePageCacheStatus &result) noexcept;
    [[nodiscard]] FilePageLoadStatus Validate() const noexcept;
    [[nodiscard]] FilePageLoadStatistics Statistics() const noexcept;

  private:
    [[nodiscard]] bool TokenIsValid(FilePageLoadToken token) const noexcept;
    [[nodiscard]] bool ResultIsTerminal(FilePageCacheStatus result) const noexcept;
    [[nodiscard]] uint64_t FindFreeSlotIndex() const noexcept;
    [[nodiscard]] uint64_t FindLoadSlotIndex(const FilePageIdentity &identity,
                                             uint64_t physical_address,
                                             uint64_t load_generation) const noexcept;
    [[nodiscard]] bool OwnerHasActiveLoad(uint64_t owner_thread_index) const noexcept;
    [[nodiscard]] FilePageLoadStatus ReleaseSlot(uint64_t slot_index) noexcept;

    FilePageLoadSlot *load_storage_{};
    uint64_t load_capacity_{};
    FilePageLoadWaiter *waiter_storage_{};
    uint64_t waiter_capacity_{};
    FilePageLoadStatistics statistics_{};
    bool initialized_{};
};

}

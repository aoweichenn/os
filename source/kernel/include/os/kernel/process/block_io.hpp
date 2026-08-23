#pragma once

#include <os/kernel/device/block_request.hpp>

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_BLOCK_IO_INVALID_SLOT_INDEX = UINT64_MAX;
inline constexpr uint64_t OS_KERNEL_BLOCK_IO_FIRST_GENERATION = 1ULL;

enum class BlockIoState : uint64_t {
    Free,
    Registered,
    Waiting,
    Completed,
    Abandoned,
};

enum class BlockIoStatus : uint64_t {
    Succeeded,
    NotInitialized,
    AlreadyInitialized,
    InvalidStorage,
    InvalidCapacity,
    InvalidRequest,
    CapacityExhausted,
    RequestAlreadyRegistered,
    RequestNotFound,
    InvalidTicket,
    InvalidState,
    RequestAlreadyResolved,
    GenerationExhausted,
    CounterOverflow,
    Corrupt,
};

struct BlockIoTicket final {
    uint64_t slot_index;
    uint64_t generation;
};

struct BlockIoSlot final {
    uint64_t request_identifier;
    uint64_t owner_thread_index;
    uint64_t generation;
    BlockRequestResult result;
    BlockIoState state;
};

struct BlockIoCompletionDecision final {
    uint64_t owner_thread_index;
    bool wake_required;
    bool abandoned;
};

struct BlockIoStatistics final {
    uint64_t capacity;
    uint64_t active_request_count;
    uint64_t registered_request_count;
    uint64_t waiting_request_count;
    uint64_t completed_request_count;
    uint64_t abandoned_request_count;
    uint64_t peak_active_request_count;
    uint64_t registration_count;
    uint64_t wait_commit_count;
    uint64_t immediate_completion_count;
    uint64_t completion_count;
    uint64_t wake_required_count;
    uint64_t abandonment_count;
    uint64_t late_completion_count;
    uint64_t result_take_count;
    uint64_t capacity_rejection_count;
};

class BlockIoCoordinator final {
  public:
    [[nodiscard]] BlockIoStatus Initialize(BlockIoSlot *storage, uint64_t capacity) noexcept;
    [[nodiscard]] BlockIoStatus Register(uint64_t request_identifier, uint64_t owner_thread_index,
                                         BlockIoTicket &ticket) noexcept;
    [[nodiscard]] BlockIoStatus PrepareWait(BlockIoTicket ticket, bool &wait_required) noexcept;
    [[nodiscard]] BlockIoStatus Complete(uint64_t owner_thread_index, uint64_t request_identifier,
                                         BlockRequestResult result,
                                         BlockIoCompletionDecision &decision) noexcept;
    [[nodiscard]] BlockIoStatus TakeResult(BlockIoTicket ticket,
                                           BlockRequestResult &result) noexcept;
    [[nodiscard]] BlockIoStatus Abandon(BlockIoTicket ticket) noexcept;
    [[nodiscard]] BlockIoStatus Validate() const noexcept;
    [[nodiscard]] BlockIoStatistics Statistics() const noexcept;

  private:
    [[nodiscard]] bool ResultIsTerminal(BlockRequestResult result) const noexcept;
    [[nodiscard]] bool TicketIsValid(BlockIoTicket ticket) const noexcept;
    [[nodiscard]] uint64_t FindFreeSlotIndex() const noexcept;
    [[nodiscard]] uint64_t FindRequestSlotIndex(uint64_t owner_thread_index,
                                                uint64_t request_identifier) const noexcept;
    [[nodiscard]] bool OwnerHasActiveRequest(uint64_t owner_thread_index) const noexcept;
    [[nodiscard]] BlockIoStatus ReleaseSlot(uint64_t slot_index) noexcept;

    BlockIoSlot *storage_{};
    uint64_t capacity_{};
    BlockIoStatistics statistics_{};
    bool initialized_{};
};

}

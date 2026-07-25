#pragma once

#include "os/kernel/physical_memory_map.hpp"

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_MEMORY_PAGE_SIZE_BYTES = 4096ULL;

struct PhysicalFrame final {
    uint64_t physical_address;
};

struct PhysicalFrameAllocatorStatistics final {
    uint64_t managed_frame_count;
    uint64_t free_frame_count;
    uint64_t allocated_frame_count;
    uint64_t reserved_frame_count;
};

enum class PhysicalFrameAllocatorStatus : uint64_t {
    Succeeded,
    NullStateStorage,
    InvalidStateStorageSize,
    AlreadyInitialized,
    InvalidManagedLimit,
    InvalidMemoryMap,
    NoUsableFrames,
    NotInitialized,
    InvalidReservation,
    InvalidAllocationRange,
    OutOfMemory,
    InvalidFrameAddress,
    FrameNotAllocated,
};

[[nodiscard]] uint64_t
CalculatePhysicalFrameStateStorageSizeBytes(uint64_t managed_limit_address) noexcept;

class PhysicalFrameAllocator final {
  public:
    PhysicalFrameAllocator() noexcept;
    PhysicalFrameAllocator(uint8_t *state_storage, uint64_t state_storage_size_bytes) noexcept;

    [[nodiscard]] PhysicalFrameAllocatorStatus
    ConfigureStateStorage(uint8_t *state_storage, uint64_t state_storage_size_bytes) noexcept;
    [[nodiscard]] PhysicalFrameAllocatorStatus Initialize(const PhysicalMemoryMapEntry *entries,
                                                          uint64_t entry_count,
                                                          uint64_t managed_limit_address) noexcept;
    [[nodiscard]] PhysicalFrameAllocatorStatus ReserveRange(uint64_t begin_address,
                                                            uint64_t length_bytes) noexcept;
    [[nodiscard]] PhysicalFrameAllocatorStatus Allocate(PhysicalFrame &frame) noexcept;
    [[nodiscard]] PhysicalFrameAllocatorStatus AllocateInRange(uint64_t minimum_address,
                                                               uint64_t maximum_address_exclusive,
                                                               PhysicalFrame &frame) noexcept;
    [[nodiscard]] PhysicalFrameAllocatorStatus Release(PhysicalFrame frame) noexcept;
    [[nodiscard]] PhysicalFrameAllocatorStatistics Statistics() const noexcept;
    [[nodiscard]] uint64_t ManagedLimitAddress() const noexcept;

  private:
    enum class FrameState : uint8_t {
        Unavailable = 0U,
        Free = 1U,
        Allocated = 2U,
        Reserved = 3U,
    };

    [[nodiscard]] FrameState GetFrameState(uint64_t frame_index) const noexcept;
    void SetFrameState(uint64_t frame_index, FrameState state) noexcept;
    void SetFreeFrameStateRange(uint64_t first_frame_index, uint64_t end_frame_index) noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;

    uint8_t *state_storage_;
    uint64_t state_storage_size_bytes_;
    uint64_t managed_frame_count_;
    uint64_t free_frame_count_;
    uint64_t allocated_frame_count_;
    uint64_t reserved_frame_count_;
    uint64_t next_search_frame_index_;
};

}

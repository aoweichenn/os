#pragma once

#include "os/kernel/physical_memory_map.hpp"

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_MEMORY_PAGE_SIZE_BYTES = 4096ULL;

struct PhysicalFrame final {
    uint64_t physicalAddress;
};

struct PhysicalFrameAllocatorStatistics final {
    uint64_t managedFrameCount;
    uint64_t freeFrameCount;
    uint64_t allocatedFrameCount;
    uint64_t reservedFrameCount;
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
CalculatePhysicalFrameStateStorageSizeBytes(uint64_t managedLimitAddress) noexcept;

class PhysicalFrameAllocator final {
  public:
    PhysicalFrameAllocator() noexcept;
    PhysicalFrameAllocator(uint8_t *stateStorage, uint64_t stateStorageSizeBytes) noexcept;

    [[nodiscard]] PhysicalFrameAllocatorStatus
    ConfigureStateStorage(uint8_t *stateStorage, uint64_t stateStorageSizeBytes) noexcept;
    [[nodiscard]] PhysicalFrameAllocatorStatus Initialize(const PhysicalMemoryMapEntry *entries,
                                                          uint64_t entryCount,
                                                          uint64_t managedLimitAddress) noexcept;
    [[nodiscard]] PhysicalFrameAllocatorStatus ReserveRange(uint64_t beginAddress,
                                                            uint64_t lengthBytes) noexcept;
    [[nodiscard]] PhysicalFrameAllocatorStatus Allocate(PhysicalFrame &frame) noexcept;
    [[nodiscard]] PhysicalFrameAllocatorStatus AllocateInRange(uint64_t minimumAddress,
                                                               uint64_t maximumAddressExclusive,
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

    [[nodiscard]] FrameState GetFrameState(uint64_t frameIndex) const noexcept;
    void SetFrameState(uint64_t frameIndex, FrameState state) noexcept;
    void SetFreeFrameStateRange(uint64_t firstFrameIndex, uint64_t endFrameIndex) noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;

    uint8_t *stateStorage_;
    uint64_t stateStorageSizeBytes_;
    uint64_t managedFrameCount_;
    uint64_t freeFrameCount_;
    uint64_t allocatedFrameCount_;
    uint64_t reservedFrameCount_;
    uint64_t nextSearchFrameIndex_;
};

}

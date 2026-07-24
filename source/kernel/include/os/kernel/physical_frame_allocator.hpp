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
    InvalidManagedLimit,
    InvalidMemoryMap,
    NoUsableFrames,
    NotInitialized,
    InvalidReservation,
    OutOfMemory,
    InvalidFrameAddress,
    FrameNotAllocated,
};

class PhysicalFrameAllocator final {
  public:
    PhysicalFrameAllocator(uint8_t *stateStorage, uint64_t stateStorageSizeBytes) noexcept;

    [[nodiscard]] PhysicalFrameAllocatorStatus initialize(const PhysicalMemoryMapEntry *entries,
                                                          uint64_t entryCount,
                                                          uint64_t managedLimitAddress) noexcept;
    [[nodiscard]] PhysicalFrameAllocatorStatus reserveRange(uint64_t beginAddress,
                                                            uint64_t lengthBytes) noexcept;
    [[nodiscard]] PhysicalFrameAllocatorStatus allocate(PhysicalFrame &frame) noexcept;
    [[nodiscard]] PhysicalFrameAllocatorStatus release(PhysicalFrame frame) noexcept;
    [[nodiscard]] PhysicalFrameAllocatorStatistics statistics() const noexcept;
    [[nodiscard]] uint64_t managedLimitAddress() const noexcept;

  private:
    enum class FrameState : uint8_t {
        Unavailable = 0U,
        Free = 1U,
        Allocated = 2U,
        Reserved = 3U,
    };

    [[nodiscard]] FrameState frameState(uint64_t frameIndex) const noexcept;
    void setFrameState(uint64_t frameIndex, FrameState state) noexcept;
    [[nodiscard]] bool isInitialized() const noexcept;

    uint8_t *stateStorage_;
    uint64_t stateStorageSizeBytes_;
    uint64_t managedFrameCount_;
    uint64_t freeFrameCount_;
    uint64_t allocatedFrameCount_;
    uint64_t reservedFrameCount_;
    uint64_t nextSearchFrameIndex_;
};

}

#include "os/kernel/physical_frame_allocator.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_FRAME_ALLOCATOR_BITS_PER_STATE = 2ULL;
constexpr uint64_t OS_KERNEL_FRAME_ALLOCATOR_STATES_PER_BYTE = 4ULL;
constexpr uint8_t OS_KERNEL_FRAME_ALLOCATOR_STATE_MASK = 0x03U;
constexpr uint64_t OS_KERNEL_FRAME_ALLOCATOR_BYTE_ROUNDING =
    OS_KERNEL_FRAME_ALLOCATOR_STATES_PER_BYTE - 1ULL;
constexpr uint64_t OS_KERNEL_FRAME_ALLOCATOR_PAGE_MASK = OS_KERNEL_MEMORY_PAGE_SIZE_BYTES - 1ULL;
constexpr uint64_t OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE = 0ULL;

[[nodiscard]] uint64_t alignUpToPage(const uint64_t address) noexcept {
    return (address + OS_KERNEL_FRAME_ALLOCATOR_PAGE_MASK) & ~OS_KERNEL_FRAME_ALLOCATOR_PAGE_MASK;
}

[[nodiscard]] uint64_t alignDownToPage(const uint64_t address) noexcept {
    return address & ~OS_KERNEL_FRAME_ALLOCATOR_PAGE_MASK;
}

}

PhysicalFrameAllocator::PhysicalFrameAllocator(uint8_t *stateStorage,
                                               const uint64_t stateStorageSizeBytes) noexcept
    : stateStorage_{stateStorage}, stateStorageSizeBytes_{stateStorageSizeBytes},
      managedFrameCount_{0ULL}, freeFrameCount_{0ULL}, allocatedFrameCount_{0ULL},
      reservedFrameCount_{0ULL}, nextSearchFrameIndex_{0ULL} {}

PhysicalFrameAllocatorStatus
PhysicalFrameAllocator::initialize(const PhysicalMemoryMapEntry *entries, const uint64_t entryCount,
                                   const uint64_t managedLimitAddress) noexcept {
    if (this->stateStorage_ == nullptr) {
        return PhysicalFrameAllocatorStatus::NullStateStorage;
    }
    if (managedLimitAddress == OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE ||
        (managedLimitAddress & OS_KERNEL_FRAME_ALLOCATOR_PAGE_MASK) !=
            OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE) {
        return PhysicalFrameAllocatorStatus::InvalidManagedLimit;
    }

    PhysicalMemorySummary memorySummary{};
    if (validateAndSummarizePhysicalMemoryMap(entries, entryCount, managedLimitAddress,
                                              memorySummary) !=
        PhysicalMemoryMapValidationStatus::Succeeded) {
        return PhysicalFrameAllocatorStatus::InvalidMemoryMap;
    }

    const uint64_t managedFrameCount = managedLimitAddress / OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
    const uint64_t requiredStateStorageSizeBytes =
        (managedFrameCount + OS_KERNEL_FRAME_ALLOCATOR_BYTE_ROUNDING) /
        OS_KERNEL_FRAME_ALLOCATOR_STATES_PER_BYTE;
    if (requiredStateStorageSizeBytes > this->stateStorageSizeBytes_) {
        return PhysicalFrameAllocatorStatus::InvalidStateStorageSize;
    }

    for (uint64_t byteIndex = 0ULL; byteIndex < requiredStateStorageSizeBytes; ++byteIndex) {
        this->stateStorage_[byteIndex] = 0U;
    }
    this->managedFrameCount_ = managedFrameCount;
    this->freeFrameCount_ = 0ULL;
    this->allocatedFrameCount_ = 0ULL;
    this->reservedFrameCount_ = 0ULL;
    this->nextSearchFrameIndex_ = 0ULL;

    for (uint64_t entryIndex = 0ULL; entryIndex < entryCount; ++entryIndex) {
        const PhysicalMemoryMapEntry &entry = entries[entryIndex];
        if (entry.type != OS_KERNEL_MEMORY_MAP_USABLE_REGION_TYPE) {
            continue;
        }
        if (entry.baseAddress >= managedLimitAddress) {
            continue;
        }
        const uint64_t entryEndAddress = entry.baseAddress + entry.lengthBytes;
        const uint64_t firstFrameAddress = alignUpToPage(entry.baseAddress);
        const uint64_t clampedEndAddress =
            entryEndAddress < managedLimitAddress ? entryEndAddress : managedLimitAddress;
        const uint64_t endFrameAddress = alignDownToPage(clampedEndAddress);
        if (firstFrameAddress >= endFrameAddress) {
            continue;
        }
        for (uint64_t frameAddress = firstFrameAddress; frameAddress < endFrameAddress;
             frameAddress += OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) {
            const uint64_t frameIndex = frameAddress / OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
            this->setFrameState(frameIndex, FrameState::Free);
            ++this->freeFrameCount_;
        }
    }

    if (this->freeFrameCount_ == OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE) {
        this->managedFrameCount_ = 0ULL;
        this->nextSearchFrameIndex_ = 0ULL;
        return PhysicalFrameAllocatorStatus::NoUsableFrames;
    }
    return PhysicalFrameAllocatorStatus::Succeeded;
}

PhysicalFrameAllocatorStatus
PhysicalFrameAllocator::reserveRange(const uint64_t beginAddress,
                                     const uint64_t lengthBytes) noexcept {
    if (!this->isInitialized()) {
        return PhysicalFrameAllocatorStatus::NotInitialized;
    }
    if (lengthBytes == OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE ||
        beginAddress > UINT64_MAX - lengthBytes) {
        return PhysicalFrameAllocatorStatus::InvalidReservation;
    }
    const uint64_t endAddress = beginAddress + lengthBytes;
    const uint64_t managedLimit = this->managedLimitAddress();
    if (beginAddress >= managedLimit || endAddress > managedLimit ||
        endAddress > UINT64_MAX - OS_KERNEL_FRAME_ALLOCATOR_PAGE_MASK) {
        return PhysicalFrameAllocatorStatus::InvalidReservation;
    }
    const uint64_t firstFrameAddress = alignDownToPage(beginAddress);
    const uint64_t endFrameAddress = alignUpToPage(endAddress);
    for (uint64_t frameAddress = firstFrameAddress; frameAddress < endFrameAddress;
         frameAddress += OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) {
        const uint64_t frameIndex = frameAddress / OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
        if (this->frameState(frameIndex) == FrameState::Allocated) {
            return PhysicalFrameAllocatorStatus::InvalidReservation;
        }
    }
    for (uint64_t frameAddress = firstFrameAddress; frameAddress < endFrameAddress;
         frameAddress += OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) {
        const uint64_t frameIndex = frameAddress / OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
        const FrameState state = this->frameState(frameIndex);
        if (state == FrameState::Free) {
            this->setFrameState(frameIndex, FrameState::Reserved);
            --this->freeFrameCount_;
            ++this->reservedFrameCount_;
        }
    }
    return PhysicalFrameAllocatorStatus::Succeeded;
}

PhysicalFrameAllocatorStatus PhysicalFrameAllocator::allocate(PhysicalFrame &frame) noexcept {
    if (!this->isInitialized()) {
        return PhysicalFrameAllocatorStatus::NotInitialized;
    }
    if (this->freeFrameCount_ == OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE) {
        return PhysicalFrameAllocatorStatus::OutOfMemory;
    }

    for (uint64_t offset = 0ULL; offset < this->managedFrameCount_; ++offset) {
        const uint64_t frameIndex =
            (this->nextSearchFrameIndex_ + offset) % this->managedFrameCount_;
        if (this->frameState(frameIndex) != FrameState::Free) {
            continue;
        }
        this->setFrameState(frameIndex, FrameState::Allocated);
        --this->freeFrameCount_;
        ++this->allocatedFrameCount_;
        this->nextSearchFrameIndex_ = (frameIndex + 1ULL) % this->managedFrameCount_;
        frame.physicalAddress = frameIndex * OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
        return PhysicalFrameAllocatorStatus::Succeeded;
    }
    return PhysicalFrameAllocatorStatus::OutOfMemory;
}

PhysicalFrameAllocatorStatus PhysicalFrameAllocator::release(const PhysicalFrame frame) noexcept {
    if (!this->isInitialized()) {
        return PhysicalFrameAllocatorStatus::NotInitialized;
    }
    if ((frame.physicalAddress & OS_KERNEL_FRAME_ALLOCATOR_PAGE_MASK) !=
            OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE ||
        frame.physicalAddress >= this->managedLimitAddress()) {
        return PhysicalFrameAllocatorStatus::InvalidFrameAddress;
    }
    const uint64_t frameIndex = frame.physicalAddress / OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
    if (this->frameState(frameIndex) != FrameState::Allocated) {
        return PhysicalFrameAllocatorStatus::FrameNotAllocated;
    }
    this->setFrameState(frameIndex, FrameState::Free);
    ++this->freeFrameCount_;
    --this->allocatedFrameCount_;
    if (frameIndex < this->nextSearchFrameIndex_) {
        this->nextSearchFrameIndex_ = frameIndex;
    }
    return PhysicalFrameAllocatorStatus::Succeeded;
}

PhysicalFrameAllocatorStatistics PhysicalFrameAllocator::statistics() const noexcept {
    return PhysicalFrameAllocatorStatistics{
        .managedFrameCount = this->managedFrameCount_,
        .freeFrameCount = this->freeFrameCount_,
        .allocatedFrameCount = this->allocatedFrameCount_,
        .reservedFrameCount = this->reservedFrameCount_,
    };
}

uint64_t PhysicalFrameAllocator::managedLimitAddress() const noexcept {
    return this->managedFrameCount_ * OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
}

PhysicalFrameAllocator::FrameState
PhysicalFrameAllocator::frameState(const uint64_t frameIndex) const noexcept {
    const uint64_t byteIndex = frameIndex / OS_KERNEL_FRAME_ALLOCATOR_STATES_PER_BYTE;
    const uint64_t stateIndex = frameIndex % OS_KERNEL_FRAME_ALLOCATOR_STATES_PER_BYTE;
    const uint64_t shift = stateIndex * OS_KERNEL_FRAME_ALLOCATOR_BITS_PER_STATE;
    return static_cast<FrameState>(static_cast<uint8_t>((this->stateStorage_[byteIndex] >> shift) &
                                                        OS_KERNEL_FRAME_ALLOCATOR_STATE_MASK));
}

void PhysicalFrameAllocator::setFrameState(const uint64_t frameIndex,
                                           const FrameState state) noexcept {
    const uint64_t byteIndex = frameIndex / OS_KERNEL_FRAME_ALLOCATOR_STATES_PER_BYTE;
    const uint64_t stateIndex = frameIndex % OS_KERNEL_FRAME_ALLOCATOR_STATES_PER_BYTE;
    const uint64_t shift = stateIndex * OS_KERNEL_FRAME_ALLOCATOR_BITS_PER_STATE;
    const uint8_t shiftedMask = static_cast<uint8_t>(OS_KERNEL_FRAME_ALLOCATOR_STATE_MASK << shift);
    const uint8_t shiftedState = static_cast<uint8_t>(static_cast<uint8_t>(state) << shift);
    this->stateStorage_[byteIndex] =
        static_cast<uint8_t>((this->stateStorage_[byteIndex] & ~shiftedMask) | shiftedState);
}

bool PhysicalFrameAllocator::isInitialized() const noexcept {
    return this->managedFrameCount_ != OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE;
}

}

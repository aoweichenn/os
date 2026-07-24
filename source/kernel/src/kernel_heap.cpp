#include "os/kernel/kernel_heap.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_HEAP_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_HEAP_MINIMUM_ALIGNMENT_BYTES = 1ULL;

[[nodiscard]] bool isPowerOfTwo(const uint64_t value) noexcept {
    return value != OS_KERNEL_HEAP_EMPTY_VALUE &&
           (value & (value - OS_KERNEL_HEAP_MINIMUM_ALIGNMENT_BYTES)) == OS_KERNEL_HEAP_EMPTY_VALUE;
}

}

KernelHeap::KernelHeap() noexcept
    : baseAddress_{0ULL}, sizeBytes_{0ULL}, consumedBytes_{0ULL}, allocationCount_{0ULL} {}

KernelHeapStatus KernelHeap::initialize(const uint64_t baseAddress,
                                        const uint64_t sizeBytes) noexcept {
    if (baseAddress == OS_KERNEL_HEAP_EMPTY_VALUE || sizeBytes == OS_KERNEL_HEAP_EMPTY_VALUE ||
        baseAddress > UINT64_MAX - sizeBytes) {
        return KernelHeapStatus::InvalidRange;
    }
    this->baseAddress_ = baseAddress;
    this->sizeBytes_ = sizeBytes;
    this->consumedBytes_ = 0ULL;
    this->allocationCount_ = 0ULL;
    return KernelHeapStatus::Succeeded;
}

KernelHeapStatus KernelHeap::tryAllocate(const uint64_t sizeBytes, const uint64_t alignmentBytes,
                                         void *&allocation) noexcept {
    if (this->sizeBytes_ == OS_KERNEL_HEAP_EMPTY_VALUE) {
        return KernelHeapStatus::NotInitialized;
    }
    if (sizeBytes == OS_KERNEL_HEAP_EMPTY_VALUE) {
        return KernelHeapStatus::EmptyAllocation;
    }
    if (!isPowerOfTwo(alignmentBytes)) {
        return KernelHeapStatus::InvalidAlignment;
    }
    if (this->baseAddress_ > UINT64_MAX - this->consumedBytes_) {
        return KernelHeapStatus::AddressOverflow;
    }
    const uint64_t currentAddress = this->baseAddress_ + this->consumedBytes_;
    const uint64_t alignmentMask = alignmentBytes - OS_KERNEL_HEAP_MINIMUM_ALIGNMENT_BYTES;
    if (currentAddress > UINT64_MAX - alignmentMask) {
        return KernelHeapStatus::AddressOverflow;
    }
    const uint64_t alignedAddress = (currentAddress + alignmentMask) & ~alignmentMask;
    const uint64_t paddingBytes = alignedAddress - currentAddress;
    if (paddingBytes > this->sizeBytes_ - this->consumedBytes_ ||
        sizeBytes > this->sizeBytes_ - this->consumedBytes_ - paddingBytes) {
        return KernelHeapStatus::OutOfMemory;
    }

    this->consumedBytes_ += paddingBytes + sizeBytes;
    ++this->allocationCount_;
    allocation = reinterpret_cast<void *>(alignedAddress);
    return KernelHeapStatus::Succeeded;
}

KernelHeapStatistics KernelHeap::statistics() const noexcept {
    return KernelHeapStatistics{
        .capacityBytes = this->sizeBytes_,
        .consumedBytes = this->consumedBytes_,
        .remainingBytes = this->sizeBytes_ - this->consumedBytes_,
        .allocationCount = this->allocationCount_,
    };
}

}

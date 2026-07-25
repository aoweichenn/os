#pragma once

#include <stdint.h>

namespace os::kernel {

enum class KernelHeapStatus : uint64_t {
    Succeeded,
    InvalidRange,
    NotInitialized,
    EmptyAllocation,
    InvalidAlignment,
    AddressOverflow,
    OutOfMemory,
};

struct KernelHeapStatistics final {
    uint64_t capacityBytes;
    uint64_t consumedBytes;
    uint64_t remainingBytes;
    uint64_t allocationCount;
};

class KernelHeap final {
  public:
    KernelHeap() noexcept;

    [[nodiscard]] KernelHeapStatus Initialize(uint64_t baseAddress, uint64_t sizeBytes) noexcept;
    [[nodiscard]] KernelHeapStatus TryAllocate(uint64_t sizeBytes, uint64_t alignmentBytes,
                                               void *&allocation) noexcept;
    [[nodiscard]] KernelHeapStatistics Statistics() const noexcept;

  private:
    uint64_t baseAddress_;
    uint64_t sizeBytes_;
    uint64_t consumedBytes_;
    uint64_t allocationCount_;
};

}

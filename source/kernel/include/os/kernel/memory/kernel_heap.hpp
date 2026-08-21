#pragma once

#include <stdint.h>

namespace os::kernel {

enum class KernelHeapStatus : uint64_t {
    Succeeded,
    InvalidRange,
    AlreadyInitialized,
    NotInitialized,
    EmptyAllocation,
    InvalidAlignment,
    AddressOverflow,
    OutOfMemory,
    NullAllocation,
    InvalidAllocation,
    AllocationNotActive,
    CorruptedMetadata,
};

struct KernelHeapStatistics final {
    // 当前统计描述活动对象，累计统计用于观察整个启动周期。
    uint64_t capacity_bytes;
    uint64_t consumed_bytes;
    uint64_t remaining_bytes;
    uint64_t allocation_count;
    uint64_t active_requested_bytes;
    uint64_t successful_allocation_count;
    uint64_t release_count;
    uint64_t peak_consumed_bytes;
    uint64_t largest_free_allocation_bytes;
};

class KernelHeap final {
  public:
    constexpr KernelHeap() noexcept = default;

    // 堆直接使用给定连续虚拟区间存放块头和负载，不依赖任何外部运行库。
    [[nodiscard]] KernelHeapStatus Initialize(uint64_t base_address, uint64_t size_bytes) noexcept;
    [[nodiscard]] KernelHeapStatus TryAllocate(uint64_t size_bytes, uint64_t alignment_bytes,
                                               void *&allocation) noexcept;
    [[nodiscard]] KernelHeapStatus TryRelease(void *allocation) noexcept;
    [[nodiscard]] KernelHeapStatus Validate() const noexcept;
    [[nodiscard]] KernelHeapStatistics Statistics() const noexcept;

  private:
    struct BlockHeader;

    [[nodiscard]] uint64_t EndAddress() const noexcept;
    [[nodiscard]] bool IsAddressInsideHeap(uint64_t address) const noexcept;
    [[nodiscard]] bool IsBlockHeaderValid(const BlockHeader *block) const noexcept;
    [[nodiscard]] bool IsPhysicalBlock(const BlockHeader *block) const noexcept;
    [[nodiscard]] bool TryCalculateAllocationLayout(const BlockHeader *free_block,
                                                    uint64_t requested_size_bytes,
                                                    uint64_t alignment_bytes,
                                                    uint64_t &allocation_address,
                                                    uint64_t &prefix_size_bytes,
                                                    uint64_t &allocation_size_bytes) const noexcept;
    void ConfigureBlock(BlockHeader *block, uint64_t size_bytes, uint64_t previous_block_size_bytes,
                        uint64_t requested_size_bytes, uint64_t state_signature) noexcept;
    void InsertFreeBlock(BlockHeader *block) noexcept;
    void RemoveFreeBlock(BlockHeader *block) noexcept;
    void UpdateFollowingBlockPreviousSize(BlockHeader *block) noexcept;

    uint64_t base_address_{};
    uint64_t size_bytes_{};
    uint64_t consumed_bytes_{};
    uint64_t allocation_count_{};
    uint64_t active_requested_bytes_{};
    uint64_t successful_allocation_count_{};
    uint64_t release_count_{};
    uint64_t peak_consumed_bytes_{};
    BlockHeader *free_list_head_{nullptr};
};

}

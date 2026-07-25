#pragma once

#include "os/kernel/physical_memory_map.hpp"

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_MEMORY_PAGE_SIZE_BYTES = 4096ULL;
inline constexpr uint64_t OS_KERNEL_FRAME_ALLOCATOR_MAXIMUM_ORDER = 51ULL;
inline constexpr uint64_t OS_KERNEL_FRAME_ALLOCATOR_ORDER_COUNT =
    OS_KERNEL_FRAME_ALLOCATOR_MAXIMUM_ORDER + 1ULL;

struct PhysicalFrame final {
    uint64_t physical_address;
};

struct PhysicalFrameBlock final {
    uint64_t physical_address;
    uint64_t order;
};

struct PhysicalFrameAllocatorStatistics final {
    uint64_t managed_frame_count;
    uint64_t free_frame_count;
    uint64_t allocated_frame_count;
    uint64_t reserved_frame_count;
};

struct PhysicalFrameBuddyStatistics final {
    uint64_t metadata_storage_size_bytes;
    uint64_t maximum_order;
    uint64_t free_block_count;
    uint64_t active_block_count;
    uint64_t successful_allocation_count;
    uint64_t release_count;
    uint64_t split_count;
    uint64_t merge_count;
    uint64_t largest_free_order;
};

// 页状态回答“这一页归谁”，双位图回答“连续块以哪一阶存在”。
// 两套状态共同保留非法释放诊断能力，上层不能绕过其中任意一套。
enum class PhysicalFrameAllocatorStatus : uint64_t {
    Succeeded,
    NullStateStorage,
    InvalidStateStorageSize,
    NullBuddyStorage,
    InvalidBuddyStorageSize,
    AlreadyInitialized,
    BuddyAlreadyInitialized,
    InvalidManagedLimit,
    InvalidMemoryMap,
    NoUsableFrames,
    NotInitialized,
    BuddyNotInitialized,
    ExistingAllocationsPreventBuddyInitialization,
    InvalidReservation,
    ReservationAfterBuddyInitialization,
    InvalidAllocationRange,
    InvalidBlockOrder,
    InvalidBlockAlignment,
    OutOfMemory,
    InvalidFrameAddress,
    FrameNotAllocated,
    AllocationOrderMismatch,
    CorruptedState,
};

[[nodiscard]] uint64_t
CalculatePhysicalFrameStateStorageSizeBytes(uint64_t managed_limit_address) noexcept;
[[nodiscard]] uint64_t
CalculatePhysicalFrameBuddyStorageSizeBytes(uint64_t managed_limit_address) noexcept;

class PhysicalFrameAllocator final {
  public:
    PhysicalFrameAllocator() noexcept;
    PhysicalFrameAllocator(uint8_t *state_storage, uint64_t state_storage_size_bytes) noexcept;

    [[nodiscard]] PhysicalFrameAllocatorStatus
    ConfigureStateStorage(uint8_t *state_storage, uint64_t state_storage_size_bytes) noexcept;
    [[nodiscard]] PhysicalFrameAllocatorStatus
    ConfigureBuddyStorage(uint8_t *buddy_storage, uint64_t buddy_storage_size_bytes) noexcept;
    [[nodiscard]] PhysicalFrameAllocatorStatus Initialize(const PhysicalMemoryMapEntry *entries,
                                                          uint64_t entry_count,
                                                          uint64_t managed_limit_address) noexcept;
    [[nodiscard]] PhysicalFrameAllocatorStatus InitializeBuddy() noexcept;
    [[nodiscard]] PhysicalFrameAllocatorStatus ReserveRange(uint64_t begin_address,
                                                            uint64_t length_bytes) noexcept;
    [[nodiscard]] PhysicalFrameAllocatorStatus Allocate(PhysicalFrame &frame) noexcept;
    [[nodiscard]] PhysicalFrameAllocatorStatus AllocateInRange(uint64_t minimum_address,
                                                               uint64_t maximum_address_exclusive,
                                                               PhysicalFrame &frame) noexcept;
    [[nodiscard]] PhysicalFrameAllocatorStatus AllocateBlock(uint64_t order,
                                                             PhysicalFrameBlock &block) noexcept;
    [[nodiscard]] PhysicalFrameAllocatorStatus
    AllocateBlockInRange(uint64_t order, uint64_t minimum_address,
                         uint64_t maximum_address_exclusive, PhysicalFrameBlock &block) noexcept;
    [[nodiscard]] PhysicalFrameAllocatorStatus Release(PhysicalFrame frame) noexcept;
    [[nodiscard]] PhysicalFrameAllocatorStatus ReleaseBlock(PhysicalFrameBlock block) noexcept;
    [[nodiscard]] PhysicalFrameAllocatorStatistics Statistics() const noexcept;
    [[nodiscard]] PhysicalFrameBuddyStatistics BuddyStatistics() const noexcept;
    [[nodiscard]] PhysicalFrameAllocatorStatus ValidateBuddy() const noexcept;
    [[nodiscard]] uint64_t ManagedLimitAddress() const noexcept;

  private:
    enum class FrameState : uint8_t {
        Unavailable = 0U,
        Free = 1U,
        Allocated = 2U,
        Reserved = 3U,
    };

    enum class BlockBitmap : uint8_t {
        Free,
        Allocated,
    };

    [[nodiscard]] FrameState GetFrameState(uint64_t frame_index) const noexcept;
    void SetFrameState(uint64_t frame_index, FrameState state) noexcept;
    void SetFreeFrameStateRange(uint64_t first_frame_index, uint64_t end_frame_index) noexcept;
    [[nodiscard]] PhysicalFrameAllocatorStatus
    AllocateLegacyInRange(uint64_t minimum_address, uint64_t maximum_address_exclusive,
                          PhysicalFrame &frame) noexcept;
    [[nodiscard]] PhysicalFrameAllocatorStatus ReleaseLegacy(PhysicalFrame frame) noexcept;
    [[nodiscard]] uint64_t BlockFrameCount(uint64_t order) const noexcept;
    [[nodiscard]] uint64_t CompleteBlockCount(uint64_t order) const noexcept;
    [[nodiscard]] bool GetBlockBit(BlockBitmap bitmap, uint64_t order,
                                   uint64_t block_index) const noexcept;
    void SetBlockBit(BlockBitmap bitmap, uint64_t order, uint64_t block_index, bool value) noexcept;
    [[nodiscard]] bool FindSourceBlock(uint64_t source_order, uint64_t requested_order,
                                       uint64_t first_frame_index, uint64_t end_frame_index,
                                       uint64_t &source_block_index,
                                       uint64_t &target_frame_index) const noexcept;
    [[nodiscard]] bool FindContainingAllocation(uint64_t frame_index,
                                                uint64_t &allocation_order) const noexcept;
    [[nodiscard]] bool HasContainingBlock(BlockBitmap bitmap, uint64_t order,
                                          uint64_t block_index) const noexcept;
    [[nodiscard]] bool IsBlockState(uint64_t frame_index, uint64_t order,
                                    FrameState expected_state) const noexcept;
    void SetBlockState(uint64_t frame_index, uint64_t order, FrameState state) noexcept;
    [[nodiscard]] uint64_t CalculateMaximumOrder() const noexcept;
    [[nodiscard]] uint64_t CalculateLargestFreeOrder() const noexcept;
    [[nodiscard]] uint64_t CalculateTotalFreeBlockCount() const noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] bool IsBuddyInitialized() const noexcept;

    uint8_t *state_storage_;
    uint64_t state_storage_size_bytes_;
    uint8_t *buddy_storage_;
    uint64_t buddy_storage_size_bytes_;
    uint64_t buddy_bitmap_family_size_bytes_;
    uint64_t managed_frame_count_;
    uint64_t free_frame_count_;
    uint64_t allocated_frame_count_;
    uint64_t reserved_frame_count_;
    uint64_t next_search_frame_index_;
    uint64_t buddy_maximum_order_;
    uint64_t buddy_active_block_count_;
    uint64_t buddy_successful_allocation_count_;
    uint64_t buddy_release_count_;
    uint64_t buddy_split_count_;
    uint64_t buddy_merge_count_;
    uint64_t buddy_order_bitmap_offsets_[OS_KERNEL_FRAME_ALLOCATOR_ORDER_COUNT];
    uint64_t buddy_order_bitmap_size_bytes_[OS_KERNEL_FRAME_ALLOCATOR_ORDER_COUNT];
    uint64_t buddy_free_block_count_by_order_[OS_KERNEL_FRAME_ALLOCATOR_ORDER_COUNT];
};

}

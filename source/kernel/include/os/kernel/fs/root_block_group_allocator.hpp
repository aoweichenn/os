#pragma once

#include <os/kernel/fs/root_file_system_v5_format.hpp>

#include <stdint.h>

namespace os::kernel::fs {

inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_ALLOCATOR_MAXIMUM_RESERVATION_COUNT = 64ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_ALLOCATOR_INVALID_RESERVATION_SLOT = UINT64_MAX;

enum class RootBlockAllocatorStatus : uint64_t {
    Succeeded,
    NotInitialized,
    AlreadyInitialized,
    InvalidArgument,
    InvalidBitmap,
    InvalidReservation,
    CapacityExhausted,
    NotAllocated,
    ProtectedRange,
};

struct RootBlockAllocation final {
    uint64_t physical_start_block;
    uint64_t block_count;
    uint64_t group_index;
};

struct RootBlockReservationToken final {
    uint64_t slot_index;
    uint64_t generation;
};

struct RootBlockAllocatorStatistics final {
    uint64_t reserve_count;
    uint64_t commit_count;
    uint64_t abort_count;
    uint64_t release_count;
    uint64_t reserved_block_count;
    uint64_t committed_block_count;
    uint64_t released_block_count;
    uint64_t partial_allocation_count;
    uint64_t enospc_count;
    uint64_t locality_hit_count;
    uint64_t group_fallback_count;
    uint64_t active_reservation_count;
    uint64_t peak_active_reservation_count;
};

class RootBlockGroupAllocator final {
  public:
    RootBlockGroupAllocator() noexcept = default;

    [[nodiscard]] RootBlockAllocatorStatus
    Initialize(const RootV5Superblock &superblock, const RootV5GroupDescriptor *descriptors,
               uint64_t descriptor_count, uint8_t *block_bitmaps,
               uint64_t bitmap_storage_size_bytes, uint64_t *free_block_counts,
               uint64_t free_block_count_capacity, uint64_t protected_start_block,
               uint64_t protected_block_count) noexcept;
    [[nodiscard]] RootBlockAllocatorStatus Reserve(uint64_t requested_block_count,
                                                   uint64_t minimum_block_count,
                                                   uint64_t preferred_group_index,
                                                   RootBlockAllocation &allocation,
                                                   RootBlockReservationToken &token) noexcept;
    [[nodiscard]] RootBlockAllocatorStatus Commit(RootBlockReservationToken token) noexcept;
    [[nodiscard]] RootBlockAllocatorStatus Abort(RootBlockReservationToken token) noexcept;
    [[nodiscard]] RootBlockAllocatorStatus CanRelease(uint64_t physical_start_block,
                                                      uint64_t block_count) const noexcept;
    [[nodiscard]] RootBlockAllocatorStatus Release(uint64_t physical_start_block,
                                                   uint64_t block_count) noexcept;
    [[nodiscard]] bool IsAllocated(uint64_t physical_block) const noexcept;
    [[nodiscard]] RootBlockAllocatorStatus Validate() const noexcept;
    [[nodiscard]] uint64_t FreeBlockCount() const noexcept;
    [[nodiscard]] RootBlockAllocatorStatistics Statistics() const noexcept;

  private:
    struct Reservation final {
        RootBlockAllocation allocation;
        uint64_t generation;
        bool active;
    };

    [[nodiscard]] bool BitmapBit(uint64_t group_index, uint64_t group_block_index) const noexcept;
    void SetBitmapBit(uint64_t group_index, uint64_t group_block_index, bool allocated) noexcept;
    [[nodiscard]] bool RangeOverlapsProtected(uint64_t physical_start_block,
                                              uint64_t block_count) const noexcept;
    [[nodiscard]] RootBlockAllocatorStatus
    LocatePhysicalRange(uint64_t physical_start_block, uint64_t block_count, uint64_t &group_index,
                        uint64_t &group_block_index) const noexcept;
    [[nodiscard]] RootBlockAllocatorStatus ValidateToken(RootBlockReservationToken token,
                                                         Reservation *&reservation) noexcept;

    const RootV5Superblock *superblock_{nullptr};
    const RootV5GroupDescriptor *descriptors_{nullptr};
    uint8_t *block_bitmaps_{nullptr};
    uint64_t *free_block_counts_{nullptr};
    Reservation reservations_[OS_KERNEL_ROOTFS_V5_ALLOCATOR_MAXIMUM_RESERVATION_COUNT]{};
    RootBlockAllocatorStatistics statistics_{};
    uint64_t descriptor_count_{};
    uint64_t bitmap_storage_size_bytes_{};
    uint64_t protected_start_block_{};
    uint64_t protected_block_count_{};
    uint64_t next_reservation_generation_{1ULL};
    bool initialized_{};
};

}

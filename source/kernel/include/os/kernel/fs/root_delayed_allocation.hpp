#pragma once

#include <os/kernel/fs/root_block_group_allocator.hpp>
#include <os/kernel/fs/root_extent_tree.hpp>

#include <stdint.h>

namespace os::kernel::fs {

inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_DELAYED_MAXIMUM_RANGE_COUNT = 64ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_RANGE_QUERY_MAXIMUM_RECORD_COUNT = 320ULL;

enum class RootFileRangeKind : uint64_t {
    Hole,
    Delayed,
    Unwritten,
    Initialized,
};

enum class RootDelayedAllocationStatus : uint64_t {
    Succeeded,
    NotInitialized,
    AlreadyInitialized,
    InvalidArgument,
    AlreadyMapped,
    NotDelayed,
    WritebackActive,
    NoWriteback,
    CapacityExhausted,
    AllocationFailed,
    MappingFailed,
    NotFound,
};

struct RootDelayedRange final {
    uint64_t logical_start_block;
    uint64_t block_count;
};

struct RootFileRange final {
    uint64_t logical_start_block;
    uint64_t physical_start_block;
    uint64_t block_count;
    RootFileRangeKind kind;
};

struct RootWritebackToken final {
    RootBlockReservationToken reservation;
    RootBlockAllocation allocation;
    uint64_t logical_start_block;
    uint64_t generation;
    bool active;
};

struct RootDelayedAllocationStatistics final {
    uint64_t reserve_write_count;
    uint64_t delayed_merge_count;
    uint64_t writeback_begin_count;
    uint64_t writeback_complete_count;
    uint64_t writeback_abort_count;
    uint64_t fallocate_count;
    uint64_t punch_hole_count;
    uint64_t truncate_count;
    uint64_t seek_data_count;
    uint64_t seek_hole_count;
    uint64_t range_query_count;
    uint64_t delayed_block_count;
    uint64_t peak_delayed_block_count;
};

class RootDelayedAllocation final {
  public:
    RootDelayedAllocation() noexcept = default;

    [[nodiscard]] RootDelayedAllocationStatus Initialize(uint64_t initial_size_blocks) noexcept;
    [[nodiscard]] RootDelayedAllocationStatus ReserveWrite(uint64_t logical_start_block,
                                                           uint64_t block_count,
                                                           const RootExtentTree &tree) noexcept;
    [[nodiscard]] RootDelayedAllocationStatus
    BeginWriteback(uint64_t logical_start_block, uint64_t block_count,
                   uint64_t preferred_group_index, RootBlockGroupAllocator &allocator,
                   RootExtentTree &tree, RootWritebackToken &token) noexcept;
    [[nodiscard]] RootDelayedAllocationStatus CompleteWriteback(RootWritebackToken token,
                                                                RootBlockGroupAllocator &allocator,
                                                                RootExtentTree &tree) noexcept;
    [[nodiscard]] RootDelayedAllocationStatus AbortWriteback(RootWritebackToken token,
                                                             RootBlockGroupAllocator &allocator,
                                                             RootExtentTree &tree) noexcept;
    [[nodiscard]] RootDelayedAllocationStatus Fallocate(uint64_t logical_start_block,
                                                        uint64_t block_count, bool keep_size,
                                                        uint64_t preferred_group_index,
                                                        RootBlockGroupAllocator &allocator,
                                                        RootExtentTree &tree) noexcept;
    [[nodiscard]] RootDelayedAllocationStatus PunchHole(uint64_t logical_start_block,
                                                        uint64_t block_count,
                                                        RootBlockGroupAllocator &allocator,
                                                        RootExtentTree &tree) noexcept;
    [[nodiscard]] RootDelayedAllocationStatus Truncate(uint64_t new_size_blocks,
                                                       RootBlockGroupAllocator &allocator,
                                                       RootExtentTree &tree) noexcept;
    [[nodiscard]] RootDelayedAllocationStatus SeekData(uint64_t logical_start_block,
                                                       const RootExtentTree &tree,
                                                       uint64_t &result_logical_block) noexcept;
    [[nodiscard]] RootDelayedAllocationStatus SeekHole(uint64_t logical_start_block,
                                                       const RootExtentTree &tree,
                                                       uint64_t &result_logical_block) noexcept;
    [[nodiscard]] RootDelayedAllocationStatus
    QueryRanges(uint64_t logical_start_block, uint64_t block_count, const RootExtentTree &tree,
                RootFileRange *ranges, uint64_t range_capacity, uint64_t &range_count) noexcept;
    [[nodiscard]] RootDelayedAllocationStatus Validate(const RootExtentTree &tree) const noexcept;
    [[nodiscard]] uint64_t FileSizeBlocks() const noexcept;
    [[nodiscard]] uint64_t DelayedRangeCount() const noexcept;
    [[nodiscard]] RootDelayedAllocationStatistics Statistics() const noexcept;

  private:
    [[nodiscard]] bool DelayedRangeContains(uint64_t logical_start_block,
                                            uint64_t block_count) const noexcept;
    [[nodiscard]] bool RangeOverlapsDelayed(uint64_t logical_start_block,
                                            uint64_t block_count) const noexcept;
    [[nodiscard]] RootDelayedAllocationStatus RemoveDelayedRange(uint64_t logical_start_block,
                                                                 uint64_t block_count) noexcept;
    void NormalizeDelayedRanges() noexcept;

    RootDelayedRange delayed_ranges_[OS_KERNEL_ROOTFS_V5_DELAYED_MAXIMUM_RANGE_COUNT]{};
    RootWritebackToken active_writeback_{};
    RootDelayedAllocationStatistics statistics_{};
    uint64_t file_size_blocks_{};
    uint64_t delayed_range_count_{};
    uint64_t next_writeback_generation_{1ULL};
    bool initialized_{};
};

}

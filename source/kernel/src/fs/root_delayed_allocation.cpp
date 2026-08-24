#include <os/kernel/fs/root_delayed_allocation.hpp>

namespace os::kernel::fs {

namespace {

[[nodiscard]] bool TryRangeEnd(const uint64_t start, const uint64_t count, uint64_t &end) noexcept {
    if (count == 0ULL || start > UINT64_MAX - count) {
        return false;
    }
    end = start + count;
    return true;
}

[[nodiscard]] bool RangesOverlap(const uint64_t left_start, const uint64_t left_count,
                                 const uint64_t right_start, const uint64_t right_count) noexcept {
    uint64_t left_end = 0ULL;
    uint64_t right_end = 0ULL;
    return TryRangeEnd(left_start, left_count, left_end) &&
           TryRangeEnd(right_start, right_count, right_end) && left_start < right_end &&
           right_start < left_end;
}

[[nodiscard]] bool TreeRangeOverlaps(const RootExtentTree &tree, const uint64_t logical_start_block,
                                     const uint64_t block_count) noexcept {
    for (uint64_t extent_index = 0ULL; extent_index < tree.ExtentCount(); ++extent_index) {
        RootExtent extent{};
        if (tree.ExtentAt(extent_index, extent) != RootExtentStatus::Succeeded) {
            return true;
        }
        if (RangesOverlap(logical_start_block, block_count, extent.logical_start_block,
                          extent.block_count)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] RootFileRangeKind RangeKindFromExtent(const RootExtentState state) noexcept {
    return state == RootExtentState::Initialized ? RootFileRangeKind::Initialized
                                                 : RootFileRangeKind::Unwritten;
}

}

RootDelayedAllocationStatus
RootDelayedAllocation::Initialize(const uint64_t initial_size_blocks) noexcept {
    if (this->initialized_) {
        return RootDelayedAllocationStatus::AlreadyInitialized;
    }
    this->file_size_blocks_ = initial_size_blocks;
    this->delayed_range_count_ = 0ULL;
    this->next_writeback_generation_ = 1ULL;
    this->active_writeback_ = RootWritebackToken{};
    this->statistics_ = RootDelayedAllocationStatistics{};
    this->initialized_ = true;
    return RootDelayedAllocationStatus::Succeeded;
}

bool RootDelayedAllocation::RangeOverlapsDelayed(const uint64_t logical_start_block,
                                                 const uint64_t block_count) const noexcept {
    for (uint64_t range_index = 0ULL; range_index < this->delayed_range_count_; ++range_index) {
        if (RangesOverlap(logical_start_block, block_count,
                          this->delayed_ranges_[range_index].logical_start_block,
                          this->delayed_ranges_[range_index].block_count)) {
            return true;
        }
    }
    return false;
}

bool RootDelayedAllocation::DelayedRangeContains(const uint64_t logical_start_block,
                                                 const uint64_t block_count) const noexcept {
    uint64_t logical_end = 0ULL;
    if (!TryRangeEnd(logical_start_block, block_count, logical_end)) {
        return false;
    }
    for (uint64_t range_index = 0ULL; range_index < this->delayed_range_count_; ++range_index) {
        const RootDelayedRange &range = this->delayed_ranges_[range_index];
        if (logical_start_block >= range.logical_start_block &&
            logical_end <= range.logical_start_block + range.block_count) {
            return true;
        }
    }
    return false;
}

void RootDelayedAllocation::NormalizeDelayedRanges() noexcept {
    if (this->delayed_range_count_ < 2ULL) {
        return;
    }
    uint64_t output_index = 0ULL;
    for (uint64_t input_index = 1ULL; input_index < this->delayed_range_count_; ++input_index) {
        RootDelayedRange &output = this->delayed_ranges_[output_index];
        const RootDelayedRange &input = this->delayed_ranges_[input_index];
        const uint64_t output_end = output.logical_start_block + output.block_count;
        if (input.logical_start_block <= output_end) {
            const uint64_t input_end = input.logical_start_block + input.block_count;
            if (input_end > output_end) {
                output.block_count = input_end - output.logical_start_block;
            }
            ++this->statistics_.delayed_merge_count;
        } else {
            ++output_index;
            this->delayed_ranges_[output_index] = input;
        }
    }
    const uint64_t new_count = output_index + 1ULL;
    for (uint64_t clear_index = new_count; clear_index < this->delayed_range_count_;
         ++clear_index) {
        this->delayed_ranges_[clear_index] = RootDelayedRange{};
    }
    this->delayed_range_count_ = new_count;
}

RootDelayedAllocationStatus
RootDelayedAllocation::ReserveWrite(const uint64_t logical_start_block, const uint64_t block_count,
                                    const RootExtentTree &tree) noexcept {
    uint64_t logical_end = 0ULL;
    if (!this->initialized_ || !TryRangeEnd(logical_start_block, block_count, logical_end)) {
        return RootDelayedAllocationStatus::InvalidArgument;
    }
    if (this->active_writeback_.active &&
        RangesOverlap(logical_start_block, block_count, this->active_writeback_.logical_start_block,
                      this->active_writeback_.allocation.block_count)) {
        return RootDelayedAllocationStatus::WritebackActive;
    }
    if (TreeRangeOverlaps(tree, logical_start_block, block_count)) {
        return RootDelayedAllocationStatus::AlreadyMapped;
    }
    if (this->delayed_range_count_ >= OS_KERNEL_ROOTFS_V5_DELAYED_MAXIMUM_RANGE_COUNT &&
        !this->RangeOverlapsDelayed(logical_start_block, block_count)) {
        return RootDelayedAllocationStatus::CapacityExhausted;
    }
    uint64_t insert_index = 0ULL;
    while (insert_index < this->delayed_range_count_ &&
           this->delayed_ranges_[insert_index].logical_start_block < logical_start_block) {
        ++insert_index;
    }
    if (insert_index == this->delayed_range_count_ ||
        !RangesOverlap(logical_start_block, block_count,
                       this->delayed_ranges_[insert_index].logical_start_block,
                       this->delayed_ranges_[insert_index].block_count)) {
        for (uint64_t move_index = this->delayed_range_count_; move_index > insert_index;
             --move_index) {
            this->delayed_ranges_[move_index] = this->delayed_ranges_[move_index - 1ULL];
        }
        this->delayed_ranges_[insert_index] = RootDelayedRange{
            .logical_start_block = logical_start_block,
            .block_count = block_count,
        };
        ++this->delayed_range_count_;
    } else {
        RootDelayedRange &range = this->delayed_ranges_[insert_index];
        const uint64_t range_end = range.logical_start_block + range.block_count;
        const uint64_t new_start = logical_start_block < range.logical_start_block
                                       ? logical_start_block
                                       : range.logical_start_block;
        const uint64_t new_end = logical_end > range_end ? logical_end : range_end;
        range.logical_start_block = new_start;
        range.block_count = new_end - new_start;
    }
    this->NormalizeDelayedRanges();
    if (logical_end > this->file_size_blocks_) {
        this->file_size_blocks_ = logical_end;
    }
    ++this->statistics_.reserve_write_count;
    this->statistics_.delayed_block_count = 0ULL;
    for (uint64_t range_index = 0ULL; range_index < this->delayed_range_count_; ++range_index) {
        this->statistics_.delayed_block_count += this->delayed_ranges_[range_index].block_count;
    }
    if (this->statistics_.delayed_block_count > this->statistics_.peak_delayed_block_count) {
        this->statistics_.peak_delayed_block_count = this->statistics_.delayed_block_count;
    }
    return RootDelayedAllocationStatus::Succeeded;
}

RootDelayedAllocationStatus
RootDelayedAllocation::RemoveDelayedRange(const uint64_t logical_start_block,
                                          const uint64_t block_count) noexcept {
    uint64_t logical_end = 0ULL;
    if (!TryRangeEnd(logical_start_block, block_count, logical_end)) {
        return RootDelayedAllocationStatus::InvalidArgument;
    }
    RootDelayedRange rebuilt[OS_KERNEL_ROOTFS_V5_DELAYED_MAXIMUM_RANGE_COUNT]{};
    uint64_t rebuilt_count = 0ULL;
    for (uint64_t range_index = 0ULL; range_index < this->delayed_range_count_; ++range_index) {
        const RootDelayedRange &range = this->delayed_ranges_[range_index];
        const uint64_t range_end = range.logical_start_block + range.block_count;
        if (range_end <= logical_start_block || range.logical_start_block >= logical_end) {
            if (rebuilt_count >= OS_KERNEL_ROOTFS_V5_DELAYED_MAXIMUM_RANGE_COUNT) {
                return RootDelayedAllocationStatus::CapacityExhausted;
            }
            rebuilt[rebuilt_count++] = range;
            continue;
        }
        if (range.logical_start_block < logical_start_block) {
            if (rebuilt_count >= OS_KERNEL_ROOTFS_V5_DELAYED_MAXIMUM_RANGE_COUNT) {
                return RootDelayedAllocationStatus::CapacityExhausted;
            }
            rebuilt[rebuilt_count++] = RootDelayedRange{
                .logical_start_block = range.logical_start_block,
                .block_count = logical_start_block - range.logical_start_block,
            };
        }
        if (range_end > logical_end) {
            if (rebuilt_count >= OS_KERNEL_ROOTFS_V5_DELAYED_MAXIMUM_RANGE_COUNT) {
                return RootDelayedAllocationStatus::CapacityExhausted;
            }
            rebuilt[rebuilt_count++] = RootDelayedRange{
                .logical_start_block = logical_end,
                .block_count = range_end - logical_end,
            };
        }
    }
    for (uint64_t range_index = 0ULL; range_index < OS_KERNEL_ROOTFS_V5_DELAYED_MAXIMUM_RANGE_COUNT;
         ++range_index) {
        this->delayed_ranges_[range_index] =
            range_index < rebuilt_count ? rebuilt[range_index] : RootDelayedRange{};
    }
    this->delayed_range_count_ = rebuilt_count;
    this->statistics_.delayed_block_count = 0ULL;
    for (uint64_t range_index = 0ULL; range_index < this->delayed_range_count_; ++range_index) {
        this->statistics_.delayed_block_count += this->delayed_ranges_[range_index].block_count;
    }
    return RootDelayedAllocationStatus::Succeeded;
}

RootDelayedAllocationStatus RootDelayedAllocation::BeginWriteback(
    const uint64_t logical_start_block, const uint64_t block_count,
    const uint64_t preferred_group_index, RootBlockGroupAllocator &allocator, RootExtentTree &tree,
    RootWritebackToken &token) noexcept {
    token = RootWritebackToken{};
    if (!this->initialized_ || block_count == 0ULL ||
        this->next_writeback_generation_ == UINT64_MAX) {
        return RootDelayedAllocationStatus::InvalidArgument;
    }
    if (this->active_writeback_.active) {
        return RootDelayedAllocationStatus::WritebackActive;
    }
    if (!this->DelayedRangeContains(logical_start_block, block_count)) {
        return RootDelayedAllocationStatus::NotDelayed;
    }
    if (this->delayed_range_count_ == OS_KERNEL_ROOTFS_V5_DELAYED_MAXIMUM_RANGE_COUNT) {
        for (uint64_t range_index = 0ULL; range_index < this->delayed_range_count_; ++range_index) {
            const RootDelayedRange &range = this->delayed_ranges_[range_index];
            const uint64_t range_end = range.logical_start_block + range.block_count;
            const uint64_t logical_end = logical_start_block + block_count;
            if (logical_start_block > range.logical_start_block && logical_end < range_end) {
                return RootDelayedAllocationStatus::CapacityExhausted;
            }
        }
    }
    RootBlockAllocation allocation{};
    RootBlockReservationToken reservation{};
    if (allocator.Reserve(block_count, block_count, preferred_group_index, allocation,
                          reservation) != RootBlockAllocatorStatus::Succeeded) {
        return RootDelayedAllocationStatus::AllocationFailed;
    }
    const RootExtentStatus insert_status = tree.Insert(RootExtent{
        .logical_start_block = logical_start_block,
        .physical_start_block = allocation.physical_start_block,
        .block_count = allocation.block_count,
        .state = RootExtentState::Unwritten,
    });
    if (insert_status != RootExtentStatus::Succeeded) {
        static_cast<void>(allocator.Abort(reservation));
        return RootDelayedAllocationStatus::MappingFailed;
    }
    token = RootWritebackToken{
        .reservation = reservation,
        .allocation = allocation,
        .logical_start_block = logical_start_block,
        .generation = this->next_writeback_generation_,
        .active = true,
    };
    ++this->next_writeback_generation_;
    this->active_writeback_ = token;
    ++this->statistics_.writeback_begin_count;
    return RootDelayedAllocationStatus::Succeeded;
}

RootDelayedAllocationStatus
RootDelayedAllocation::CompleteWriteback(const RootWritebackToken token,
                                         RootBlockGroupAllocator &allocator,
                                         RootExtentTree &tree) noexcept {
    if (!this->active_writeback_.active) {
        return RootDelayedAllocationStatus::NoWriteback;
    }
    if (!token.active || token.generation != this->active_writeback_.generation ||
        token.reservation.slot_index != this->active_writeback_.reservation.slot_index ||
        token.reservation.generation != this->active_writeback_.reservation.generation) {
        return RootDelayedAllocationStatus::InvalidArgument;
    }
    if (tree.Convert(token.logical_start_block, token.allocation.block_count,
                     RootExtentState::Unwritten,
                     RootExtentState::Initialized) != RootExtentStatus::Succeeded) {
        return RootDelayedAllocationStatus::MappingFailed;
    }
    if (allocator.Commit(token.reservation) != RootBlockAllocatorStatus::Succeeded) {
        return RootDelayedAllocationStatus::AllocationFailed;
    }
    const RootDelayedAllocationStatus remove_status =
        this->RemoveDelayedRange(token.logical_start_block, token.allocation.block_count);
    if (remove_status != RootDelayedAllocationStatus::Succeeded) {
        return remove_status;
    }
    this->active_writeback_ = RootWritebackToken{};
    ++this->statistics_.writeback_complete_count;
    return RootDelayedAllocationStatus::Succeeded;
}

RootDelayedAllocationStatus
RootDelayedAllocation::AbortWriteback(const RootWritebackToken token,
                                      RootBlockGroupAllocator &allocator,
                                      RootExtentTree &tree) noexcept {
    if (!this->active_writeback_.active) {
        return RootDelayedAllocationStatus::NoWriteback;
    }
    if (!token.active || token.generation != this->active_writeback_.generation ||
        token.reservation.slot_index != this->active_writeback_.reservation.slot_index ||
        token.reservation.generation != this->active_writeback_.reservation.generation) {
        return RootDelayedAllocationStatus::InvalidArgument;
    }
    RootExtent removed[2]{};
    uint64_t removed_count = 0ULL;
    if (tree.Remove(token.logical_start_block, token.allocation.block_count, removed, 2ULL,
                    removed_count) != RootExtentStatus::Succeeded ||
        removed_count != 1ULL ||
        allocator.Abort(token.reservation) != RootBlockAllocatorStatus::Succeeded) {
        return RootDelayedAllocationStatus::MappingFailed;
    }
    this->active_writeback_ = RootWritebackToken{};
    ++this->statistics_.writeback_abort_count;
    return RootDelayedAllocationStatus::Succeeded;
}

RootDelayedAllocationStatus RootDelayedAllocation::Fallocate(const uint64_t logical_start_block,
                                                             const uint64_t block_count,
                                                             const bool keep_size,
                                                             const uint64_t preferred_group_index,
                                                             RootBlockGroupAllocator &allocator,
                                                             RootExtentTree &tree) noexcept {
    uint64_t logical_end = 0ULL;
    if (!this->initialized_ || this->active_writeback_.active ||
        !TryRangeEnd(logical_start_block, block_count, logical_end)) {
        return this->active_writeback_.active ? RootDelayedAllocationStatus::WritebackActive
                                              : RootDelayedAllocationStatus::InvalidArgument;
    }
    if (this->RangeOverlapsDelayed(logical_start_block, block_count) ||
        TreeRangeOverlaps(tree, logical_start_block, block_count)) {
        return RootDelayedAllocationStatus::AlreadyMapped;
    }
    RootBlockAllocation allocation{};
    RootBlockReservationToken reservation{};
    if (allocator.Reserve(block_count, block_count, preferred_group_index, allocation,
                          reservation) != RootBlockAllocatorStatus::Succeeded) {
        return RootDelayedAllocationStatus::AllocationFailed;
    }
    if (tree.Insert(RootExtent{
            .logical_start_block = logical_start_block,
            .physical_start_block = allocation.physical_start_block,
            .block_count = allocation.block_count,
            .state = RootExtentState::Unwritten,
        }) != RootExtentStatus::Succeeded) {
        static_cast<void>(allocator.Abort(reservation));
        return RootDelayedAllocationStatus::MappingFailed;
    }
    if (allocator.Commit(reservation) != RootBlockAllocatorStatus::Succeeded) {
        return RootDelayedAllocationStatus::AllocationFailed;
    }
    if (!keep_size && logical_end > this->file_size_blocks_) {
        this->file_size_blocks_ = logical_end;
    }
    ++this->statistics_.fallocate_count;
    return RootDelayedAllocationStatus::Succeeded;
}

RootDelayedAllocationStatus RootDelayedAllocation::PunchHole(const uint64_t logical_start_block,
                                                             const uint64_t block_count,
                                                             RootBlockGroupAllocator &allocator,
                                                             RootExtentTree &tree) noexcept {
    uint64_t logical_end = 0ULL;
    if (!this->initialized_ || this->active_writeback_.active ||
        !TryRangeEnd(logical_start_block, block_count, logical_end)) {
        return this->active_writeback_.active ? RootDelayedAllocationStatus::WritebackActive
                                              : RootDelayedAllocationStatus::InvalidArgument;
    }
    RootExtent mapped[OS_KERNEL_ROOTFS_V5_EXTENT_TREE_MAXIMUM_EXTENT_COUNT]{};
    uint64_t mapped_count = 0ULL;
    const RootExtentStatus collect_status =
        tree.Collect(logical_start_block, block_count, mapped,
                     OS_KERNEL_ROOTFS_V5_EXTENT_TREE_MAXIMUM_EXTENT_COUNT, mapped_count);
    if (collect_status != RootExtentStatus::Succeeded) {
        return RootDelayedAllocationStatus::MappingFailed;
    }
    if (tree.ExtentCount() == OS_KERNEL_ROOTFS_V5_EXTENT_TREE_MAXIMUM_EXTENT_COUNT) {
        for (uint64_t extent_index = 0ULL; extent_index < mapped_count; ++extent_index) {
            const RootExtent &extent = mapped[extent_index];
            const uint64_t extent_end = extent.logical_start_block + extent.block_count;
            if (logical_start_block > extent.logical_start_block && logical_end < extent_end) {
                return RootDelayedAllocationStatus::CapacityExhausted;
            }
        }
    }
    if (this->delayed_range_count_ == OS_KERNEL_ROOTFS_V5_DELAYED_MAXIMUM_RANGE_COUNT) {
        for (uint64_t range_index = 0ULL; range_index < this->delayed_range_count_; ++range_index) {
            const RootDelayedRange &range = this->delayed_ranges_[range_index];
            const uint64_t range_end = range.logical_start_block + range.block_count;
            if (logical_start_block > range.logical_start_block && logical_end < range_end) {
                return RootDelayedAllocationStatus::CapacityExhausted;
            }
        }
    }
    for (uint64_t extent_index = 0ULL; extent_index < mapped_count; ++extent_index) {
        if (allocator.CanRelease(mapped[extent_index].physical_start_block,
                                 mapped[extent_index].block_count) !=
            RootBlockAllocatorStatus::Succeeded) {
            return RootDelayedAllocationStatus::AllocationFailed;
        }
    }
    const RootDelayedAllocationStatus delayed_status =
        this->RemoveDelayedRange(logical_start_block, block_count);
    if (delayed_status != RootDelayedAllocationStatus::Succeeded) {
        return delayed_status;
    }
    if (mapped_count != 0ULL) {
        RootExtent removed[OS_KERNEL_ROOTFS_V5_EXTENT_TREE_MAXIMUM_EXTENT_COUNT]{};
        uint64_t removed_count = 0ULL;
        if (tree.Remove(logical_start_block, block_count, removed,
                        OS_KERNEL_ROOTFS_V5_EXTENT_TREE_MAXIMUM_EXTENT_COUNT,
                        removed_count) != RootExtentStatus::Succeeded ||
            removed_count != mapped_count) {
            return RootDelayedAllocationStatus::MappingFailed;
        }
        for (uint64_t extent_index = 0ULL; extent_index < removed_count; ++extent_index) {
            if (allocator.Release(removed[extent_index].physical_start_block,
                                  removed[extent_index].block_count) !=
                RootBlockAllocatorStatus::Succeeded) {
                return RootDelayedAllocationStatus::AllocationFailed;
            }
        }
    }
    ++this->statistics_.punch_hole_count;
    return RootDelayedAllocationStatus::Succeeded;
}

RootDelayedAllocationStatus RootDelayedAllocation::Truncate(const uint64_t new_size_blocks,
                                                            RootBlockGroupAllocator &allocator,
                                                            RootExtentTree &tree) noexcept {
    if (!this->initialized_ || this->active_writeback_.active) {
        return this->active_writeback_.active ? RootDelayedAllocationStatus::WritebackActive
                                              : RootDelayedAllocationStatus::NotInitialized;
    }
    if (new_size_blocks < this->file_size_blocks_) {
        uint64_t removal_end = this->file_size_blocks_;
        for (uint64_t extent_index = 0ULL; extent_index < tree.ExtentCount(); ++extent_index) {
            RootExtent extent{};
            if (tree.ExtentAt(extent_index, extent) != RootExtentStatus::Succeeded) {
                return RootDelayedAllocationStatus::MappingFailed;
            }
            const uint64_t extent_end = extent.logical_start_block + extent.block_count;
            if (extent_end > removal_end) {
                removal_end = extent_end;
            }
        }
        for (uint64_t range_index = 0ULL; range_index < this->delayed_range_count_; ++range_index) {
            const uint64_t range_end = this->delayed_ranges_[range_index].logical_start_block +
                                       this->delayed_ranges_[range_index].block_count;
            if (range_end > removal_end) {
                removal_end = range_end;
            }
        }
        const RootDelayedAllocationStatus status =
            this->PunchHole(new_size_blocks, removal_end - new_size_blocks, allocator, tree);
        if (status != RootDelayedAllocationStatus::Succeeded) {
            return status;
        }
    }
    this->file_size_blocks_ = new_size_blocks;
    ++this->statistics_.truncate_count;
    return RootDelayedAllocationStatus::Succeeded;
}

RootDelayedAllocationStatus
RootDelayedAllocation::QueryRanges(const uint64_t logical_start_block, const uint64_t block_count,
                                   const RootExtentTree &tree, RootFileRange *const ranges,
                                   const uint64_t range_capacity, uint64_t &range_count) noexcept {
    range_count = 0ULL;
    uint64_t logical_end = 0ULL;
    if (!this->initialized_ || !TryRangeEnd(logical_start_block, block_count, logical_end) ||
        (ranges == nullptr && range_capacity != 0ULL)) {
        return RootDelayedAllocationStatus::InvalidArgument;
    }
    for (uint64_t extent_index = 0ULL; extent_index < tree.ExtentCount(); ++extent_index) {
        RootExtent extent{};
        if (tree.ExtentAt(extent_index, extent) != RootExtentStatus::Succeeded) {
            return RootDelayedAllocationStatus::MappingFailed;
        }
        const uint64_t extent_end = extent.logical_start_block + extent.block_count;
        const uint64_t intersection_start = extent.logical_start_block > logical_start_block
                                                ? extent.logical_start_block
                                                : logical_start_block;
        const uint64_t intersection_end = extent_end < logical_end ? extent_end : logical_end;
        if (intersection_start >= intersection_end) {
            continue;
        }
        if (this->active_writeback_.active &&
            RangesOverlap(extent.logical_start_block, extent.block_count,
                          this->active_writeback_.logical_start_block,
                          this->active_writeback_.allocation.block_count)) {
            continue;
        }
        if (range_count >= range_capacity) {
            range_count = 0ULL;
            return RootDelayedAllocationStatus::CapacityExhausted;
        }
        ranges[range_count++] = RootFileRange{
            .logical_start_block = intersection_start,
            .physical_start_block =
                extent.physical_start_block + intersection_start - extent.logical_start_block,
            .block_count = intersection_end - intersection_start,
            .kind = RangeKindFromExtent(extent.state),
        };
    }
    for (uint64_t delayed_index = 0ULL; delayed_index < this->delayed_range_count_;
         ++delayed_index) {
        const RootDelayedRange &delayed = this->delayed_ranges_[delayed_index];
        const uint64_t delayed_end = delayed.logical_start_block + delayed.block_count;
        const uint64_t intersection_start = delayed.logical_start_block > logical_start_block
                                                ? delayed.logical_start_block
                                                : logical_start_block;
        const uint64_t intersection_end = delayed_end < logical_end ? delayed_end : logical_end;
        if (intersection_start >= intersection_end) {
            continue;
        }
        if (range_count >= range_capacity) {
            range_count = 0ULL;
            return RootDelayedAllocationStatus::CapacityExhausted;
        }
        ranges[range_count++] = RootFileRange{
            .logical_start_block = intersection_start,
            .physical_start_block = OS_KERNEL_ROOTFS_V5_NO_BLOCK,
            .block_count = intersection_end - intersection_start,
            .kind = RootFileRangeKind::Delayed,
        };
    }
    for (uint64_t left = 0ULL; left < range_count; ++left) {
        for (uint64_t right = left + 1ULL; right < range_count; ++right) {
            if (ranges[right].logical_start_block < ranges[left].logical_start_block) {
                const RootFileRange temporary = ranges[left];
                ranges[left] = ranges[right];
                ranges[right] = temporary;
            }
        }
    }
    ++this->statistics_.range_query_count;
    return RootDelayedAllocationStatus::Succeeded;
}

RootDelayedAllocationStatus
RootDelayedAllocation::SeekData(const uint64_t logical_start_block, const RootExtentTree &tree,
                                uint64_t &result_logical_block) noexcept {
    result_logical_block = 0ULL;
    if (!this->initialized_ || logical_start_block >= this->file_size_blocks_) {
        return RootDelayedAllocationStatus::NotFound;
    }
    RootFileRange ranges[OS_KERNEL_ROOTFS_V5_RANGE_QUERY_MAXIMUM_RECORD_COUNT]{};
    uint64_t range_count = 0ULL;
    const RootDelayedAllocationStatus status = this->QueryRanges(
        logical_start_block, this->file_size_blocks_ - logical_start_block, tree, ranges,
        OS_KERNEL_ROOTFS_V5_RANGE_QUERY_MAXIMUM_RECORD_COUNT, range_count);
    if (status != RootDelayedAllocationStatus::Succeeded) {
        return status;
    }
    for (uint64_t range_index = 0ULL; range_index < range_count; ++range_index) {
        const RootFileRange &range = ranges[range_index];
        if (range.kind != RootFileRangeKind::Initialized &&
            range.kind != RootFileRangeKind::Delayed) {
            continue;
        }
        const uint64_t range_end = range.logical_start_block + range.block_count;
        if (logical_start_block < range_end) {
            result_logical_block = logical_start_block > range.logical_start_block
                                       ? logical_start_block
                                       : range.logical_start_block;
            ++this->statistics_.seek_data_count;
            return RootDelayedAllocationStatus::Succeeded;
        }
    }
    return RootDelayedAllocationStatus::NotFound;
}

RootDelayedAllocationStatus
RootDelayedAllocation::SeekHole(const uint64_t logical_start_block, const RootExtentTree &tree,
                                uint64_t &result_logical_block) noexcept {
    result_logical_block = 0ULL;
    if (!this->initialized_ || logical_start_block >= this->file_size_blocks_) {
        return RootDelayedAllocationStatus::NotFound;
    }
    RootFileRange ranges[OS_KERNEL_ROOTFS_V5_RANGE_QUERY_MAXIMUM_RECORD_COUNT]{};
    uint64_t range_count = 0ULL;
    const RootDelayedAllocationStatus status = this->QueryRanges(
        logical_start_block, this->file_size_blocks_ - logical_start_block, tree, ranges,
        OS_KERNEL_ROOTFS_V5_RANGE_QUERY_MAXIMUM_RECORD_COUNT, range_count);
    if (status != RootDelayedAllocationStatus::Succeeded) {
        return status;
    }
    uint64_t cursor = logical_start_block;
    for (uint64_t range_index = 0ULL; range_index < range_count && cursor < this->file_size_blocks_;
         ++range_index) {
        const RootFileRange &range = ranges[range_index];
        const uint64_t range_end = range.logical_start_block + range.block_count;
        if (range_end <= cursor) {
            continue;
        }
        if (range.logical_start_block > cursor || range.kind == RootFileRangeKind::Unwritten) {
            result_logical_block = cursor;
            ++this->statistics_.seek_hole_count;
            return RootDelayedAllocationStatus::Succeeded;
        }
        if (range.kind == RootFileRangeKind::Initialized ||
            range.kind == RootFileRangeKind::Delayed) {
            cursor = range_end;
        }
    }
    result_logical_block = cursor < this->file_size_blocks_ ? cursor : this->file_size_blocks_;
    ++this->statistics_.seek_hole_count;
    return RootDelayedAllocationStatus::Succeeded;
}

RootDelayedAllocationStatus
RootDelayedAllocation::Validate(const RootExtentTree &tree) const noexcept {
    if (!this->initialized_ ||
        this->delayed_range_count_ > OS_KERNEL_ROOTFS_V5_DELAYED_MAXIMUM_RANGE_COUNT ||
        tree.Validate() != RootExtentStatus::Succeeded) {
        return RootDelayedAllocationStatus::NotInitialized;
    }
    uint64_t delayed_blocks = 0ULL;
    for (uint64_t range_index = 0ULL; range_index < this->delayed_range_count_; ++range_index) {
        const RootDelayedRange &range = this->delayed_ranges_[range_index];
        uint64_t range_end = 0ULL;
        if (!TryRangeEnd(range.logical_start_block, range.block_count, range_end) ||
            range_end > this->file_size_blocks_) {
            return RootDelayedAllocationStatus::InvalidArgument;
        }
        if (TreeRangeOverlaps(tree, range.logical_start_block, range.block_count)) {
            bool overlap_is_active_writeback = this->active_writeback_.active;
            for (uint64_t extent_index = 0ULL;
                 overlap_is_active_writeback && extent_index < tree.ExtentCount(); ++extent_index) {
                RootExtent extent{};
                if (tree.ExtentAt(extent_index, extent) != RootExtentStatus::Succeeded) {
                    return RootDelayedAllocationStatus::MappingFailed;
                }
                if (!RangesOverlap(range.logical_start_block, range.block_count,
                                   extent.logical_start_block, extent.block_count)) {
                    continue;
                }
                overlap_is_active_writeback =
                    extent.logical_start_block == this->active_writeback_.logical_start_block &&
                    extent.block_count == this->active_writeback_.allocation.block_count &&
                    extent.state == RootExtentState::Unwritten;
            }
            if (!overlap_is_active_writeback) {
                return RootDelayedAllocationStatus::InvalidArgument;
            }
        }
        if (range_index != 0ULL) {
            const RootDelayedRange &prior = this->delayed_ranges_[range_index - 1ULL];
            if (prior.logical_start_block + prior.block_count >= range.logical_start_block) {
                return RootDelayedAllocationStatus::InvalidArgument;
            }
        }
        delayed_blocks += range.block_count;
    }
    if (delayed_blocks != this->statistics_.delayed_block_count) {
        return RootDelayedAllocationStatus::InvalidArgument;
    }
    if (this->active_writeback_.active) {
        RootExtent mapped{};
        if (tree.FindNext(this->active_writeback_.logical_start_block, mapped) !=
                RootExtentStatus::Succeeded ||
            mapped.logical_start_block != this->active_writeback_.logical_start_block ||
            mapped.block_count != this->active_writeback_.allocation.block_count ||
            mapped.state != RootExtentState::Unwritten) {
            return RootDelayedAllocationStatus::MappingFailed;
        }
    }
    return RootDelayedAllocationStatus::Succeeded;
}

uint64_t RootDelayedAllocation::FileSizeBlocks() const noexcept { return this->file_size_blocks_; }

uint64_t RootDelayedAllocation::DelayedRangeCount() const noexcept {
    return this->delayed_range_count_;
}

RootDelayedAllocationStatistics RootDelayedAllocation::Statistics() const noexcept {
    return this->statistics_;
}

}

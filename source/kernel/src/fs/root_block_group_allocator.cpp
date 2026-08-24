#include <os/kernel/fs/root_block_group_allocator.hpp>

namespace os::kernel::fs {

namespace {

constexpr uint64_t OS_KERNEL_ROOTFS_V5_ALLOCATOR_BITS_PER_BYTE = 8ULL;
constexpr uint8_t OS_KERNEL_ROOTFS_V5_ALLOCATOR_SINGLE_BIT = 1U;

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

}

RootBlockAllocatorStatus RootBlockGroupAllocator::Initialize(
    const RootV5Superblock &superblock, const RootV5GroupDescriptor *const descriptors,
    const uint64_t descriptor_count, uint8_t *const block_bitmaps,
    const uint64_t bitmap_storage_size_bytes, uint64_t *const free_block_counts,
    const uint64_t free_block_count_capacity, const uint64_t protected_start_block,
    const uint64_t protected_block_count) noexcept {
    if (this->initialized_) {
        return RootBlockAllocatorStatus::AlreadyInitialized;
    }
    if (descriptors == nullptr || block_bitmaps == nullptr || free_block_counts == nullptr ||
        descriptor_count != superblock.group_count ||
        free_block_count_capacity < descriptor_count ||
        descriptor_count > UINT64_MAX / superblock.block_size_bytes ||
        bitmap_storage_size_bytes < descriptor_count * superblock.block_size_bytes ||
        (protected_block_count != 0ULL &&
         (protected_start_block > UINT64_MAX - protected_block_count ||
          protected_start_block + protected_block_count > superblock.total_block_count))) {
        return RootBlockAllocatorStatus::InvalidArgument;
    }
    this->superblock_ = &superblock;
    this->descriptors_ = descriptors;
    this->block_bitmaps_ = block_bitmaps;
    this->free_block_counts_ = free_block_counts;
    this->descriptor_count_ = descriptor_count;
    this->bitmap_storage_size_bytes_ = bitmap_storage_size_bytes;
    this->protected_start_block_ = protected_start_block;
    this->protected_block_count_ = protected_block_count;
    this->statistics_ = RootBlockAllocatorStatistics{};
    this->next_reservation_generation_ = 1ULL;
    this->initialized_ = true;
    const RootBlockAllocatorStatus status = this->Validate();
    if (status != RootBlockAllocatorStatus::Succeeded) {
        this->initialized_ = false;
        this->superblock_ = nullptr;
        this->descriptors_ = nullptr;
        this->block_bitmaps_ = nullptr;
        this->free_block_counts_ = nullptr;
    }
    return status;
}

bool RootBlockGroupAllocator::BitmapBit(const uint64_t group_index,
                                        const uint64_t group_block_index) const noexcept {
    const uint64_t byte_offset = group_index * this->superblock_->block_size_bytes +
                                 group_block_index / OS_KERNEL_ROOTFS_V5_ALLOCATOR_BITS_PER_BYTE;
    const uint8_t mask =
        static_cast<uint8_t>(OS_KERNEL_ROOTFS_V5_ALLOCATOR_SINGLE_BIT << static_cast<uint8_t>(
                                 group_block_index % OS_KERNEL_ROOTFS_V5_ALLOCATOR_BITS_PER_BYTE));
    return (this->block_bitmaps_[byte_offset] & mask) != 0U;
}

void RootBlockGroupAllocator::SetBitmapBit(const uint64_t group_index,
                                           const uint64_t group_block_index,
                                           const bool allocated) noexcept {
    const uint64_t byte_offset = group_index * this->superblock_->block_size_bytes +
                                 group_block_index / OS_KERNEL_ROOTFS_V5_ALLOCATOR_BITS_PER_BYTE;
    const uint8_t mask =
        static_cast<uint8_t>(OS_KERNEL_ROOTFS_V5_ALLOCATOR_SINGLE_BIT << static_cast<uint8_t>(
                                 group_block_index % OS_KERNEL_ROOTFS_V5_ALLOCATOR_BITS_PER_BYTE));
    if (allocated) {
        this->block_bitmaps_[byte_offset] =
            static_cast<uint8_t>(this->block_bitmaps_[byte_offset] | mask);
    } else {
        this->block_bitmaps_[byte_offset] =
            static_cast<uint8_t>(this->block_bitmaps_[byte_offset] & static_cast<uint8_t>(~mask));
    }
}

bool RootBlockGroupAllocator::RangeOverlapsProtected(const uint64_t physical_start_block,
                                                     const uint64_t block_count) const noexcept {
    return this->protected_block_count_ != 0ULL &&
           RangesOverlap(physical_start_block, block_count, this->protected_start_block_,
                         this->protected_block_count_);
}

RootBlockAllocatorStatus
RootBlockGroupAllocator::LocatePhysicalRange(const uint64_t physical_start_block,
                                             const uint64_t block_count, uint64_t &group_index,
                                             uint64_t &group_block_index) const noexcept {
    group_index = 0ULL;
    group_block_index = 0ULL;
    uint64_t physical_end = 0ULL;
    if (!this->initialized_ || !TryRangeEnd(physical_start_block, block_count, physical_end) ||
        physical_end > this->superblock_->total_block_count) {
        return RootBlockAllocatorStatus::InvalidArgument;
    }
    group_index = physical_start_block / this->superblock_->blocks_per_group;
    if (group_index >= this->descriptor_count_) {
        return RootBlockAllocatorStatus::InvalidArgument;
    }
    const RootV5GroupDescriptor &descriptor = this->descriptors_[group_index];
    group_block_index = physical_start_block - descriptor.first_block;
    if (physical_start_block < descriptor.data_start_block ||
        physical_end > descriptor.first_block + descriptor.block_count) {
        return RootBlockAllocatorStatus::InvalidArgument;
    }
    return RootBlockAllocatorStatus::Succeeded;
}

RootBlockAllocatorStatus RootBlockGroupAllocator::Reserve(
    const uint64_t requested_block_count, const uint64_t minimum_block_count,
    const uint64_t preferred_group_index, RootBlockAllocation &allocation,
    RootBlockReservationToken &token) noexcept {
    allocation = RootBlockAllocation{};
    token = RootBlockReservationToken{
        .slot_index = OS_KERNEL_ROOTFS_V5_ALLOCATOR_INVALID_RESERVATION_SLOT,
        .generation = 0ULL,
    };
    if (!this->initialized_ || requested_block_count == 0ULL || minimum_block_count == 0ULL ||
        minimum_block_count > requested_block_count ||
        preferred_group_index >= this->descriptor_count_ ||
        this->next_reservation_generation_ == UINT64_MAX) {
        return RootBlockAllocatorStatus::InvalidArgument;
    }
    uint64_t reservation_slot = OS_KERNEL_ROOTFS_V5_ALLOCATOR_INVALID_RESERVATION_SLOT;
    for (uint64_t slot_index = 0ULL;
         slot_index < OS_KERNEL_ROOTFS_V5_ALLOCATOR_MAXIMUM_RESERVATION_COUNT; ++slot_index) {
        if (!this->reservations_[slot_index].active) {
            reservation_slot = slot_index;
            break;
        }
    }
    if (reservation_slot == OS_KERNEL_ROOTFS_V5_ALLOCATOR_INVALID_RESERVATION_SLOT) {
        ++this->statistics_.enospc_count;
        return RootBlockAllocatorStatus::CapacityExhausted;
    }

    uint64_t selected_group = OS_KERNEL_ROOTFS_V5_ALLOCATOR_INVALID_RESERVATION_SLOT;
    uint64_t selected_group_block = 0ULL;
    uint64_t selected_count = 0ULL;
    for (uint64_t group_offset = 0ULL; group_offset < this->descriptor_count_; ++group_offset) {
        const uint64_t group_index =
            (preferred_group_index + group_offset) % this->descriptor_count_;
        const RootV5GroupDescriptor &descriptor = this->descriptors_[group_index];
        const uint64_t data_begin = descriptor.data_start_block - descriptor.first_block;
        const uint64_t data_end = data_begin + descriptor.data_block_count;
        uint64_t run_start = data_begin;
        uint64_t run_count = 0ULL;
        for (uint64_t group_block = data_begin; group_block <= data_end; ++group_block) {
            const bool allocated =
                group_block == data_end || this->BitmapBit(group_index, group_block) ||
                this->RangeOverlapsProtected(descriptor.first_block + group_block, 1ULL);
            if (!allocated) {
                if (run_count == 0ULL) {
                    run_start = group_block;
                }
                ++run_count;
                continue;
            }
            if (run_count >= minimum_block_count) {
                const uint64_t candidate_count =
                    run_count < requested_block_count ? run_count : requested_block_count;
                if (selected_group == OS_KERNEL_ROOTFS_V5_ALLOCATOR_INVALID_RESERVATION_SLOT ||
                    candidate_count > selected_count) {
                    selected_group = group_index;
                    selected_group_block = run_start;
                    selected_count = candidate_count;
                }
                if (candidate_count == requested_block_count) {
                    break;
                }
            }
            run_count = 0ULL;
        }
        if (selected_count == requested_block_count) {
            break;
        }
        if (selected_group != OS_KERNEL_ROOTFS_V5_ALLOCATOR_INVALID_RESERVATION_SLOT) {
            break;
        }
    }
    if (selected_group == OS_KERNEL_ROOTFS_V5_ALLOCATOR_INVALID_RESERVATION_SLOT) {
        ++this->statistics_.enospc_count;
        return RootBlockAllocatorStatus::CapacityExhausted;
    }
    const uint64_t physical_start =
        this->descriptors_[selected_group].first_block + selected_group_block;
    if (this->RangeOverlapsProtected(physical_start, selected_count)) {
        ++this->statistics_.enospc_count;
        return RootBlockAllocatorStatus::ProtectedRange;
    }
    for (uint64_t block_offset = 0ULL; block_offset < selected_count; ++block_offset) {
        this->SetBitmapBit(selected_group, selected_group_block + block_offset, true);
    }
    this->free_block_counts_[selected_group] -= selected_count;
    allocation = RootBlockAllocation{
        .physical_start_block = physical_start,
        .block_count = selected_count,
        .group_index = selected_group,
    };
    Reservation &reservation = this->reservations_[reservation_slot];
    reservation = Reservation{
        .allocation = allocation,
        .generation = this->next_reservation_generation_,
        .active = true,
    };
    token = RootBlockReservationToken{
        .slot_index = reservation_slot,
        .generation = reservation.generation,
    };
    ++this->next_reservation_generation_;
    ++this->statistics_.reserve_count;
    this->statistics_.reserved_block_count += selected_count;
    ++this->statistics_.active_reservation_count;
    if (this->statistics_.active_reservation_count >
        this->statistics_.peak_active_reservation_count) {
        this->statistics_.peak_active_reservation_count =
            this->statistics_.active_reservation_count;
    }
    if (selected_count < requested_block_count) {
        ++this->statistics_.partial_allocation_count;
    }
    if (selected_group == preferred_group_index) {
        ++this->statistics_.locality_hit_count;
    } else {
        ++this->statistics_.group_fallback_count;
    }
    return RootBlockAllocatorStatus::Succeeded;
}

RootBlockAllocatorStatus
RootBlockGroupAllocator::ValidateToken(const RootBlockReservationToken token,
                                       Reservation *&reservation) noexcept {
    reservation = nullptr;
    if (!this->initialized_ ||
        token.slot_index >= OS_KERNEL_ROOTFS_V5_ALLOCATOR_MAXIMUM_RESERVATION_COUNT) {
        return RootBlockAllocatorStatus::InvalidReservation;
    }
    Reservation &candidate = this->reservations_[token.slot_index];
    if (!candidate.active || candidate.generation != token.generation) {
        return RootBlockAllocatorStatus::InvalidReservation;
    }
    reservation = &candidate;
    return RootBlockAllocatorStatus::Succeeded;
}

RootBlockAllocatorStatus
RootBlockGroupAllocator::Commit(const RootBlockReservationToken token) noexcept {
    Reservation *reservation = nullptr;
    const RootBlockAllocatorStatus status = this->ValidateToken(token, reservation);
    if (status != RootBlockAllocatorStatus::Succeeded) {
        return status;
    }
    this->statistics_.committed_block_count += reservation->allocation.block_count;
    ++this->statistics_.commit_count;
    --this->statistics_.active_reservation_count;
    *reservation = Reservation{};
    return RootBlockAllocatorStatus::Succeeded;
}

RootBlockAllocatorStatus
RootBlockGroupAllocator::Abort(const RootBlockReservationToken token) noexcept {
    Reservation *reservation = nullptr;
    const RootBlockAllocatorStatus status = this->ValidateToken(token, reservation);
    if (status != RootBlockAllocatorStatus::Succeeded) {
        return status;
    }
    const RootBlockAllocation allocation = reservation->allocation;
    const RootV5GroupDescriptor &descriptor = this->descriptors_[allocation.group_index];
    const uint64_t group_block = allocation.physical_start_block - descriptor.first_block;
    for (uint64_t block_offset = 0ULL; block_offset < allocation.block_count; ++block_offset) {
        this->SetBitmapBit(allocation.group_index, group_block + block_offset, false);
    }
    this->free_block_counts_[allocation.group_index] += allocation.block_count;
    ++this->statistics_.abort_count;
    --this->statistics_.active_reservation_count;
    *reservation = Reservation{};
    return RootBlockAllocatorStatus::Succeeded;
}

RootBlockAllocatorStatus
RootBlockGroupAllocator::CanRelease(const uint64_t physical_start_block,
                                    const uint64_t block_count) const noexcept {
    uint64_t group_index = 0ULL;
    uint64_t group_block = 0ULL;
    const RootBlockAllocatorStatus status =
        this->LocatePhysicalRange(physical_start_block, block_count, group_index, group_block);
    if (status != RootBlockAllocatorStatus::Succeeded) {
        return status;
    }
    if (this->RangeOverlapsProtected(physical_start_block, block_count)) {
        return RootBlockAllocatorStatus::ProtectedRange;
    }
    for (uint64_t slot_index = 0ULL;
         slot_index < OS_KERNEL_ROOTFS_V5_ALLOCATOR_MAXIMUM_RESERVATION_COUNT; ++slot_index) {
        const Reservation &reservation = this->reservations_[slot_index];
        if (reservation.active && RangesOverlap(physical_start_block, block_count,
                                                reservation.allocation.physical_start_block,
                                                reservation.allocation.block_count)) {
            return RootBlockAllocatorStatus::InvalidReservation;
        }
    }
    for (uint64_t block_offset = 0ULL; block_offset < block_count; ++block_offset) {
        if (!this->BitmapBit(group_index, group_block + block_offset)) {
            return RootBlockAllocatorStatus::NotAllocated;
        }
    }
    return RootBlockAllocatorStatus::Succeeded;
}

RootBlockAllocatorStatus RootBlockGroupAllocator::Release(const uint64_t physical_start_block,
                                                          const uint64_t block_count) noexcept {
    const RootBlockAllocatorStatus can_release =
        this->CanRelease(physical_start_block, block_count);
    if (can_release != RootBlockAllocatorStatus::Succeeded) {
        return can_release;
    }
    uint64_t group_index = 0ULL;
    uint64_t group_block = 0ULL;
    static_cast<void>(
        this->LocatePhysicalRange(physical_start_block, block_count, group_index, group_block));
    for (uint64_t block_offset = 0ULL; block_offset < block_count; ++block_offset) {
        this->SetBitmapBit(group_index, group_block + block_offset, false);
    }
    this->free_block_counts_[group_index] += block_count;
    ++this->statistics_.release_count;
    this->statistics_.released_block_count += block_count;
    return RootBlockAllocatorStatus::Succeeded;
}

bool RootBlockGroupAllocator::IsAllocated(const uint64_t physical_block) const noexcept {
    if (!this->initialized_ || physical_block >= this->superblock_->total_block_count) {
        return false;
    }
    const uint64_t group_index = physical_block / this->superblock_->blocks_per_group;
    const uint64_t group_block = physical_block - this->descriptors_[group_index].first_block;
    return this->BitmapBit(group_index, group_block);
}

RootBlockAllocatorStatus RootBlockGroupAllocator::Validate() const noexcept {
    if (!this->initialized_ || this->superblock_ == nullptr || this->descriptors_ == nullptr ||
        this->block_bitmaps_ == nullptr || this->free_block_counts_ == nullptr ||
        this->descriptor_count_ != this->superblock_->group_count) {
        return RootBlockAllocatorStatus::NotInitialized;
    }
    uint64_t active_reservations = 0ULL;
    for (uint64_t group_index = 0ULL; group_index < this->descriptor_count_; ++group_index) {
        const RootV5GroupDescriptor &descriptor = this->descriptors_[group_index];
        if (ValidateRootV5GroupDescriptor(*this->superblock_, descriptor) !=
            RootV5FormatStatus::Succeeded) {
            return RootBlockAllocatorStatus::InvalidArgument;
        }
        const uint64_t data_begin = descriptor.data_start_block - descriptor.first_block;
        const uint64_t data_end = data_begin + descriptor.data_block_count;
        uint64_t observed_free = 0ULL;
        for (uint64_t group_block = 0ULL; group_block < this->superblock_->blocks_per_group;
             ++group_block) {
            const bool allocated = this->BitmapBit(group_index, group_block);
            if (this->RangeOverlapsProtected(descriptor.first_block + group_block, 1ULL) &&
                !allocated) {
                return RootBlockAllocatorStatus::InvalidBitmap;
            }
            if ((group_block < data_begin || group_block >= data_end) && !allocated) {
                return RootBlockAllocatorStatus::InvalidBitmap;
            }
            if (group_block >= data_begin && group_block < data_end && !allocated) {
                ++observed_free;
            }
        }
        if (observed_free != this->free_block_counts_[group_index]) {
            return RootBlockAllocatorStatus::InvalidBitmap;
        }
    }
    for (uint64_t slot_index = 0ULL;
         slot_index < OS_KERNEL_ROOTFS_V5_ALLOCATOR_MAXIMUM_RESERVATION_COUNT; ++slot_index) {
        const Reservation &reservation = this->reservations_[slot_index];
        if (!reservation.active) {
            continue;
        }
        ++active_reservations;
        if (reservation.generation == 0ULL ||
            reservation.allocation.group_index >= this->descriptor_count_) {
            return RootBlockAllocatorStatus::InvalidReservation;
        }
        for (uint64_t other_index = slot_index + 1ULL;
             other_index < OS_KERNEL_ROOTFS_V5_ALLOCATOR_MAXIMUM_RESERVATION_COUNT; ++other_index) {
            const Reservation &other = this->reservations_[other_index];
            if (other.active && RangesOverlap(reservation.allocation.physical_start_block,
                                              reservation.allocation.block_count,
                                              other.allocation.physical_start_block,
                                              other.allocation.block_count)) {
                return RootBlockAllocatorStatus::InvalidReservation;
            }
        }
    }
    return active_reservations == this->statistics_.active_reservation_count
               ? RootBlockAllocatorStatus::Succeeded
               : RootBlockAllocatorStatus::InvalidReservation;
}

uint64_t RootBlockGroupAllocator::FreeBlockCount() const noexcept {
    if (!this->initialized_) {
        return 0ULL;
    }
    uint64_t free_count = 0ULL;
    for (uint64_t group_index = 0ULL; group_index < this->descriptor_count_; ++group_index) {
        free_count += this->free_block_counts_[group_index];
    }
    return free_count;
}

RootBlockAllocatorStatistics RootBlockGroupAllocator::Statistics() const noexcept {
    return this->statistics_;
}

}

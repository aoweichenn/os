#include "os/kernel/memory/virtual_memory_area.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_VMA_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_VMA_SINGLE_UNIT = 1ULL;
constexpr uint64_t OS_KERNEL_VMA_FIRST_OWNER_IDENTIFIER = 1ULL;

[[nodiscard]] bool IsPowerOfTwo(const uint64_t value) noexcept {
    return value != OS_KERNEL_VMA_EMPTY_VALUE &&
           (value & (value - OS_KERNEL_VMA_SINGLE_UNIT)) == OS_KERNEL_VMA_EMPTY_VALUE;
}

void RestrictAreaToRange(VirtualMemoryArea &area,
                         const uint64_t new_begin_address,
                         const uint64_t new_end_address) noexcept {
    const uint64_t old_begin_address = area.begin_address;
    const uint64_t begin_delta_bytes =
        new_begin_address - old_begin_address;
    const uint64_t new_length_bytes =
        new_end_address - new_begin_address;
    if (IsFileBackedVirtualMemoryAreaKind(area.kind)) {
        area.backing_file_offset_bytes += begin_delta_bytes;
        area.backing_data_length_bytes =
            area.backing_data_length_bytes > begin_delta_bytes
                ? area.backing_data_length_bytes - begin_delta_bytes
                : OS_KERNEL_VMA_EMPTY_VALUE;
        if (area.backing_data_length_bytes > new_length_bytes) {
            area.backing_data_length_bytes = new_length_bytes;
        }
    }
    area.begin_address = new_begin_address;
    area.end_address = new_end_address;
}

}

bool IsFileBackedVirtualMemoryAreaKind(
    const VirtualMemoryAreaKind kind) noexcept {
    return kind == VirtualMemoryAreaKind::ExecutableImage ||
           kind == VirtualMemoryAreaKind::FilePrivate ||
           kind == VirtualMemoryAreaKind::FileShared;
}

VirtualMemoryAreaPool::VirtualMemoryAreaPool() noexcept
    : descriptors_(nullptr), capacity_(OS_KERNEL_VMA_EMPTY_VALUE),
      free_descriptor_head_index_(OS_KERNEL_VMA_INVALID_DESCRIPTOR_INDEX),
      active_descriptor_count_(OS_KERNEL_VMA_EMPTY_VALUE),
      peak_active_descriptor_count_(OS_KERNEL_VMA_EMPTY_VALUE),
      successful_acquire_count_(OS_KERNEL_VMA_EMPTY_VALUE),
      release_count_(OS_KERNEL_VMA_EMPTY_VALUE),
      next_owner_identifier_(OS_KERNEL_VMA_FIRST_OWNER_IDENTIFIER), initialized_(false) {}

VirtualMemoryAreaStatus
VirtualMemoryAreaPool::Initialize(VirtualMemoryAreaDescriptor *const descriptors,
                                  const uint64_t capacity) noexcept {
    if (this->initialized_) {
        return VirtualMemoryAreaStatus::AlreadyInitialized;
    }
    if (descriptors == nullptr) {
        return VirtualMemoryAreaStatus::InvalidStorage;
    }
    if (capacity == OS_KERNEL_VMA_EMPTY_VALUE ||
        capacity == OS_KERNEL_VMA_INVALID_DESCRIPTOR_INDEX) {
        return VirtualMemoryAreaStatus::InvalidCapacity;
    }

    this->descriptors_ = descriptors;
    this->capacity_ = capacity;
    for (uint64_t descriptor_index = OS_KERNEL_VMA_EMPTY_VALUE; descriptor_index < this->capacity_;
         ++descriptor_index) {
        const uint64_t next_descriptor_index =
            descriptor_index + OS_KERNEL_VMA_SINGLE_UNIT < this->capacity_
                ? descriptor_index + OS_KERNEL_VMA_SINGLE_UNIT
                : OS_KERNEL_VMA_INVALID_DESCRIPTOR_INDEX;
        this->descriptors_[descriptor_index] = VirtualMemoryAreaDescriptor{
            .area = {},
            .previous_descriptor_index = OS_KERNEL_VMA_INVALID_DESCRIPTOR_INDEX,
            .next_descriptor_index = next_descriptor_index,
            .owner_identifier = OS_KERNEL_VMA_EMPTY_VALUE,
            .active = false,
        };
    }
    this->free_descriptor_head_index_ = OS_KERNEL_VMA_EMPTY_VALUE;
    this->active_descriptor_count_ = OS_KERNEL_VMA_EMPTY_VALUE;
    this->peak_active_descriptor_count_ = OS_KERNEL_VMA_EMPTY_VALUE;
    this->successful_acquire_count_ = OS_KERNEL_VMA_EMPTY_VALUE;
    this->release_count_ = OS_KERNEL_VMA_EMPTY_VALUE;
    this->next_owner_identifier_ = OS_KERNEL_VMA_FIRST_OWNER_IDENTIFIER;
    this->initialized_ = true;
    return VirtualMemoryAreaStatus::Succeeded;
}

VirtualMemoryAreaPoolStatistics VirtualMemoryAreaPool::Statistics() const noexcept {
    if (!this->initialized_) {
        return VirtualMemoryAreaPoolStatistics{};
    }
    return VirtualMemoryAreaPoolStatistics{
        .capacity = this->capacity_,
        .active_descriptor_count = this->active_descriptor_count_,
        .free_descriptor_count = this->capacity_ - this->active_descriptor_count_,
        .peak_active_descriptor_count = this->peak_active_descriptor_count_,
        .successful_acquire_count = this->successful_acquire_count_,
        .release_count = this->release_count_,
    };
}

VirtualMemoryAreaStatus VirtualMemoryAreaPool::Validate() const noexcept {
    if (!this->initialized_) {
        return VirtualMemoryAreaStatus::NotInitialized;
    }

    uint64_t free_descriptor_count = OS_KERNEL_VMA_EMPTY_VALUE;
    uint64_t descriptor_index = this->free_descriptor_head_index_;
    while (descriptor_index != OS_KERNEL_VMA_INVALID_DESCRIPTOR_INDEX) {
        if (!this->IsDescriptorIndexValid(descriptor_index) ||
            this->descriptors_[descriptor_index].active ||
            free_descriptor_count >= this->capacity_) {
            return VirtualMemoryAreaStatus::Corrupt;
        }
        descriptor_index = this->descriptors_[descriptor_index].next_descriptor_index;
        ++free_descriptor_count;
    }

    uint64_t observed_active_descriptor_count = OS_KERNEL_VMA_EMPTY_VALUE;
    for (uint64_t observed_index = OS_KERNEL_VMA_EMPTY_VALUE; observed_index < this->capacity_;
         ++observed_index) {
        if (this->descriptors_[observed_index].active) {
            if (this->descriptors_[observed_index].owner_identifier == OS_KERNEL_VMA_EMPTY_VALUE) {
                return VirtualMemoryAreaStatus::Corrupt;
            }
            ++observed_active_descriptor_count;
        }
    }
    if (observed_active_descriptor_count != this->active_descriptor_count_ ||
        free_descriptor_count + observed_active_descriptor_count != this->capacity_ ||
        this->release_count_ > this->successful_acquire_count_) {
        return VirtualMemoryAreaStatus::Corrupt;
    }
    return VirtualMemoryAreaStatus::Succeeded;
}

VirtualMemoryAreaStatus VirtualMemoryAreaPool::Acquire(const uint64_t owner_identifier,
                                                       uint64_t &descriptor_index) noexcept {
    if (!this->initialized_) {
        return VirtualMemoryAreaStatus::NotInitialized;
    }
    if (owner_identifier == OS_KERNEL_VMA_EMPTY_VALUE) {
        return VirtualMemoryAreaStatus::Corrupt;
    }
    if (this->free_descriptor_head_index_ == OS_KERNEL_VMA_INVALID_DESCRIPTOR_INDEX) {
        return VirtualMemoryAreaStatus::MetadataExhausted;
    }

    descriptor_index = this->free_descriptor_head_index_;
    VirtualMemoryAreaDescriptor &descriptor = this->descriptors_[descriptor_index];
    if (descriptor.active) {
        return VirtualMemoryAreaStatus::Corrupt;
    }
    this->free_descriptor_head_index_ = descriptor.next_descriptor_index;
    descriptor = VirtualMemoryAreaDescriptor{
        .area = {},
        .previous_descriptor_index = OS_KERNEL_VMA_INVALID_DESCRIPTOR_INDEX,
        .next_descriptor_index = OS_KERNEL_VMA_INVALID_DESCRIPTOR_INDEX,
        .owner_identifier = owner_identifier,
        .active = true,
    };
    ++this->active_descriptor_count_;
    ++this->successful_acquire_count_;
    if (this->active_descriptor_count_ > this->peak_active_descriptor_count_) {
        this->peak_active_descriptor_count_ = this->active_descriptor_count_;
    }
    return VirtualMemoryAreaStatus::Succeeded;
}

VirtualMemoryAreaStatus VirtualMemoryAreaPool::Release(const uint64_t owner_identifier,
                                                       const uint64_t descriptor_index) noexcept {
    if (!this->initialized_) {
        return VirtualMemoryAreaStatus::NotInitialized;
    }
    if (!this->IsDescriptorIndexValid(descriptor_index)) {
        return VirtualMemoryAreaStatus::Corrupt;
    }
    VirtualMemoryAreaDescriptor &descriptor = this->descriptors_[descriptor_index];
    if (!descriptor.active || descriptor.owner_identifier != owner_identifier ||
        this->active_descriptor_count_ == OS_KERNEL_VMA_EMPTY_VALUE) {
        return VirtualMemoryAreaStatus::Corrupt;
    }

    descriptor = VirtualMemoryAreaDescriptor{
        .area = {},
        .previous_descriptor_index = OS_KERNEL_VMA_INVALID_DESCRIPTOR_INDEX,
        .next_descriptor_index = this->free_descriptor_head_index_,
        .owner_identifier = OS_KERNEL_VMA_EMPTY_VALUE,
        .active = false,
    };
    this->free_descriptor_head_index_ = descriptor_index;
    --this->active_descriptor_count_;
    ++this->release_count_;
    return VirtualMemoryAreaStatus::Succeeded;
}

uint64_t VirtualMemoryAreaPool::AllocateOwnerIdentifier() noexcept {
    if (!this->initialized_ ||
        this->next_owner_identifier_ == OS_KERNEL_VMA_INVALID_DESCRIPTOR_INDEX) {
        return OS_KERNEL_VMA_EMPTY_VALUE;
    }
    const uint64_t owner_identifier = this->next_owner_identifier_;
    ++this->next_owner_identifier_;
    return owner_identifier;
}

bool VirtualMemoryAreaPool::IsDescriptorIndexValid(const uint64_t descriptor_index) const noexcept {
    return descriptor_index < this->capacity_;
}

VirtualMemoryMap::VirtualMemoryMap() noexcept
    : pool_(nullptr), head_descriptor_index_(OS_KERNEL_VMA_INVALID_DESCRIPTOR_INDEX),
      area_count_(OS_KERNEL_VMA_EMPTY_VALUE), page_size_bytes_(OS_KERNEL_VMA_EMPTY_VALUE),
      hard_area_limit_(OS_KERNEL_VMA_EMPTY_VALUE), owner_identifier_(OS_KERNEL_VMA_EMPTY_VALUE),
      initialized_(false) {}

VirtualMemoryAreaStatus VirtualMemoryMap::Initialize(VirtualMemoryAreaPool &pool,
                                                     const uint64_t page_size_bytes,
                                                     const uint64_t hard_area_limit) noexcept {
    if (this->initialized_) {
        return VirtualMemoryAreaStatus::AlreadyInitialized;
    }
    if (pool.Validate() != VirtualMemoryAreaStatus::Succeeded) {
        return VirtualMemoryAreaStatus::InvalidStorage;
    }
    if (!IsPowerOfTwo(page_size_bytes)) {
        return VirtualMemoryAreaStatus::InvalidAlignment;
    }
    if (hard_area_limit == OS_KERNEL_VMA_EMPTY_VALUE ||
        hard_area_limit > pool.Statistics().capacity) {
        return VirtualMemoryAreaStatus::InvalidHardLimit;
    }
    const uint64_t owner_identifier = pool.AllocateOwnerIdentifier();
    if (owner_identifier == OS_KERNEL_VMA_EMPTY_VALUE) {
        return VirtualMemoryAreaStatus::MetadataExhausted;
    }

    this->pool_ = &pool;
    this->head_descriptor_index_ = OS_KERNEL_VMA_INVALID_DESCRIPTOR_INDEX;
    this->area_count_ = OS_KERNEL_VMA_EMPTY_VALUE;
    this->page_size_bytes_ = page_size_bytes;
    this->hard_area_limit_ = hard_area_limit;
    this->owner_identifier_ = owner_identifier;
    this->initialized_ = true;
    return VirtualMemoryAreaStatus::Succeeded;
}

VirtualMemoryAreaStatus VirtualMemoryMap::Insert(const VirtualMemoryArea &area) noexcept {
    if (!this->initialized_) {
        return VirtualMemoryAreaStatus::NotInitialized;
    }
    if (!this->IsRangeValid(area.begin_address, area.end_address)) {
        return VirtualMemoryAreaStatus::InvalidRange;
    }
    if (!this->IsBackingValid(area)) {
        return VirtualMemoryAreaStatus::InvalidBacking;
    }

    uint64_t previous_descriptor_index = OS_KERNEL_VMA_INVALID_DESCRIPTOR_INDEX;
    uint64_t next_descriptor_index = this->head_descriptor_index_;
    while (next_descriptor_index != OS_KERNEL_VMA_INVALID_DESCRIPTOR_INDEX &&
           this->pool_->descriptors_[next_descriptor_index].area.begin_address <
               area.begin_address) {
        previous_descriptor_index = next_descriptor_index;
        next_descriptor_index =
            this->pool_->descriptors_[next_descriptor_index].next_descriptor_index;
    }

    if (previous_descriptor_index != OS_KERNEL_VMA_INVALID_DESCRIPTOR_INDEX &&
        this->pool_->descriptors_[previous_descriptor_index].area.end_address >
            area.begin_address) {
        return VirtualMemoryAreaStatus::Overlap;
    }
    if (next_descriptor_index != OS_KERNEL_VMA_INVALID_DESCRIPTOR_INDEX &&
        area.end_address > this->pool_->descriptors_[next_descriptor_index].area.begin_address) {
        return VirtualMemoryAreaStatus::Overlap;
    }

    const bool merge_previous =
        previous_descriptor_index != OS_KERNEL_VMA_INVALID_DESCRIPTOR_INDEX &&
        this->pool_->descriptors_[previous_descriptor_index].area.end_address ==
            area.begin_address &&
        this->AreAttributesEqual(this->pool_->descriptors_[previous_descriptor_index].area, area);
    const bool merge_next =
        next_descriptor_index != OS_KERNEL_VMA_INVALID_DESCRIPTOR_INDEX &&
        area.end_address == this->pool_->descriptors_[next_descriptor_index].area.begin_address &&
        this->AreAttributesEqual(area, this->pool_->descriptors_[next_descriptor_index].area);

    if (merge_previous && merge_next) {
        this->pool_->descriptors_[previous_descriptor_index].area.end_address =
            this->pool_->descriptors_[next_descriptor_index].area.end_address;
        this->Unlink(next_descriptor_index);
        if (this->pool_->Release(this->owner_identifier_, next_descriptor_index) !=
            VirtualMemoryAreaStatus::Succeeded) {
            return VirtualMemoryAreaStatus::Corrupt;
        }
        --this->area_count_;
        return VirtualMemoryAreaStatus::Succeeded;
    }
    if (merge_previous) {
        this->pool_->descriptors_[previous_descriptor_index].area.end_address = area.end_address;
        return VirtualMemoryAreaStatus::Succeeded;
    }
    if (merge_next) {
        this->pool_->descriptors_[next_descriptor_index].area.begin_address = area.begin_address;
        return VirtualMemoryAreaStatus::Succeeded;
    }
    if (this->area_count_ >= this->hard_area_limit_) {
        return VirtualMemoryAreaStatus::AreaLimitExceeded;
    }

    uint64_t descriptor_index = OS_KERNEL_VMA_INVALID_DESCRIPTOR_INDEX;
    const VirtualMemoryAreaStatus acquire_status =
        this->pool_->Acquire(this->owner_identifier_, descriptor_index);
    if (acquire_status != VirtualMemoryAreaStatus::Succeeded) {
        return acquire_status;
    }
    this->pool_->descriptors_[descriptor_index].area = area;
    this->LinkBetween(descriptor_index, previous_descriptor_index, next_descriptor_index);
    ++this->area_count_;
    return VirtualMemoryAreaStatus::Succeeded;
}

VirtualMemoryAreaStatus
VirtualMemoryMap::Remove(const uint64_t begin_address, const uint64_t end_address,
                         const VirtualMemoryAreaKind required_kind) noexcept {
    if (!this->initialized_) {
        return VirtualMemoryAreaStatus::NotInitialized;
    }
    if (!this->IsRangeValid(begin_address, end_address)) {
        return VirtualMemoryAreaStatus::InvalidRange;
    }

    bool overlap_found = false;
    bool split_required = false;
    uint64_t descriptor_index = this->head_descriptor_index_;
    while (descriptor_index != OS_KERNEL_VMA_INVALID_DESCRIPTOR_INDEX) {
        const VirtualMemoryArea &area = this->pool_->descriptors_[descriptor_index].area;
        if (area.begin_address >= end_address) {
            break;
        }
        if (area.end_address > begin_address) {
            overlap_found = true;
            if (area.kind != required_kind) {
                return VirtualMemoryAreaStatus::KindMismatch;
            }
            if (area.begin_address < begin_address && end_address < area.end_address) {
                split_required = true;
            }
        }
        descriptor_index = this->pool_->descriptors_[descriptor_index].next_descriptor_index;
    }
    if (!overlap_found) {
        return VirtualMemoryAreaStatus::NotMapped;
    }
    if (split_required && this->area_count_ >= this->hard_area_limit_) {
        return VirtualMemoryAreaStatus::AreaLimitExceeded;
    }

    uint64_t split_descriptor_index = OS_KERNEL_VMA_INVALID_DESCRIPTOR_INDEX;
    if (split_required) {
        const VirtualMemoryAreaStatus acquire_status =
            this->pool_->Acquire(this->owner_identifier_, split_descriptor_index);
        if (acquire_status != VirtualMemoryAreaStatus::Succeeded) {
            return acquire_status;
        }
    }

    descriptor_index = this->head_descriptor_index_;
    while (descriptor_index != OS_KERNEL_VMA_INVALID_DESCRIPTOR_INDEX) {
        VirtualMemoryAreaDescriptor &descriptor = this->pool_->descriptors_[descriptor_index];
        const uint64_t next_descriptor_index = descriptor.next_descriptor_index;
        if (descriptor.area.begin_address >= end_address) {
            break;
        }
        if (descriptor.area.end_address <= begin_address) {
            descriptor_index = next_descriptor_index;
            continue;
        }

        if (begin_address <= descriptor.area.begin_address &&
            descriptor.area.end_address <= end_address) {
            this->Unlink(descriptor_index);
            if (this->pool_->Release(this->owner_identifier_, descriptor_index) !=
                VirtualMemoryAreaStatus::Succeeded) {
                return VirtualMemoryAreaStatus::Corrupt;
            }
            --this->area_count_;
        } else if (begin_address <= descriptor.area.begin_address) {
            RestrictAreaToRange(descriptor.area, end_address,
                                descriptor.area.end_address);
        } else if (descriptor.area.end_address <= end_address) {
            RestrictAreaToRange(descriptor.area,
                                descriptor.area.begin_address,
                                begin_address);
        } else {
            VirtualMemoryAreaDescriptor &split_descriptor =
                this->pool_->descriptors_[split_descriptor_index];
            split_descriptor.area = descriptor.area;
            RestrictAreaToRange(split_descriptor.area, end_address,
                                descriptor.area.end_address);
            RestrictAreaToRange(descriptor.area,
                                descriptor.area.begin_address,
                                begin_address);
            this->LinkBetween(split_descriptor_index, descriptor_index,
                              descriptor.next_descriptor_index);
            ++this->area_count_;
            split_descriptor_index = OS_KERNEL_VMA_INVALID_DESCRIPTOR_INDEX;
        }
        descriptor_index = next_descriptor_index;
    }
    if (split_descriptor_index != OS_KERNEL_VMA_INVALID_DESCRIPTOR_INDEX) {
        if (this->pool_->Release(this->owner_identifier_, split_descriptor_index) !=
            VirtualMemoryAreaStatus::Succeeded) {
            return VirtualMemoryAreaStatus::Corrupt;
        }
        return VirtualMemoryAreaStatus::Corrupt;
    }
    return VirtualMemoryAreaStatus::Succeeded;
}

VirtualMemoryAreaStatus VirtualMemoryMap::FindContaining(const uint64_t address,
                                                         VirtualMemoryArea &area) const noexcept {
    if (!this->initialized_) {
        return VirtualMemoryAreaStatus::NotInitialized;
    }
    uint64_t descriptor_index = this->head_descriptor_index_;
    while (descriptor_index != OS_KERNEL_VMA_INVALID_DESCRIPTOR_INDEX) {
        const VirtualMemoryArea &candidate = this->pool_->descriptors_[descriptor_index].area;
        if (address < candidate.begin_address) {
            break;
        }
        if (address < candidate.end_address) {
            area = candidate;
            return VirtualMemoryAreaStatus::Succeeded;
        }
        descriptor_index = this->pool_->descriptors_[descriptor_index].next_descriptor_index;
    }
    return VirtualMemoryAreaStatus::NotMapped;
}

VirtualMemoryAreaStatus VirtualMemoryMap::FindFirstGap(const uint64_t window_begin_address,
                                                       const uint64_t window_end_address,
                                                       const uint64_t length_bytes,
                                                       const uint64_t alignment_bytes,
                                                       uint64_t &begin_address) const noexcept {
    if (!this->initialized_) {
        return VirtualMemoryAreaStatus::NotInitialized;
    }
    if (!this->IsRangeValid(window_begin_address, window_end_address) ||
        length_bytes == OS_KERNEL_VMA_EMPTY_VALUE ||
        length_bytes % this->page_size_bytes_ != OS_KERNEL_VMA_EMPTY_VALUE) {
        return VirtualMemoryAreaStatus::InvalidRange;
    }
    if (!IsPowerOfTwo(alignment_bytes) || alignment_bytes < this->page_size_bytes_ ||
        alignment_bytes % this->page_size_bytes_ != OS_KERNEL_VMA_EMPTY_VALUE) {
        return VirtualMemoryAreaStatus::InvalidAlignment;
    }

    uint64_t candidate_address = this->AlignUp(window_begin_address, alignment_bytes);
    if (candidate_address < window_begin_address || candidate_address >= window_end_address ||
        length_bytes > window_end_address - candidate_address) {
        return VirtualMemoryAreaStatus::NotMapped;
    }

    uint64_t descriptor_index = this->head_descriptor_index_;
    while (descriptor_index != OS_KERNEL_VMA_INVALID_DESCRIPTOR_INDEX) {
        const VirtualMemoryArea &area = this->pool_->descriptors_[descriptor_index].area;
        if (area.end_address <= candidate_address) {
            descriptor_index = this->pool_->descriptors_[descriptor_index].next_descriptor_index;
            continue;
        }
        if (area.begin_address >= window_end_address) {
            break;
        }
        if (candidate_address <= area.begin_address &&
            length_bytes <= area.begin_address - candidate_address) {
            begin_address = candidate_address;
            return VirtualMemoryAreaStatus::Succeeded;
        }
        candidate_address = this->AlignUp(area.end_address, alignment_bytes);
        if (candidate_address < area.end_address || candidate_address >= window_end_address ||
            length_bytes > window_end_address - candidate_address) {
            return VirtualMemoryAreaStatus::NotMapped;
        }
        descriptor_index = this->pool_->descriptors_[descriptor_index].next_descriptor_index;
    }

    if (candidate_address < window_end_address &&
        length_bytes <= window_end_address - candidate_address) {
        begin_address = candidate_address;
        return VirtualMemoryAreaStatus::Succeeded;
    }
    return VirtualMemoryAreaStatus::NotMapped;
}

VirtualMemoryAreaStatus VirtualMemoryMap::ReadAt(const uint64_t ordinal,
                                                 VirtualMemoryArea &area) const noexcept {
    if (!this->initialized_) {
        return VirtualMemoryAreaStatus::NotInitialized;
    }
    uint64_t current_ordinal = OS_KERNEL_VMA_EMPTY_VALUE;
    uint64_t descriptor_index = this->head_descriptor_index_;
    while (descriptor_index != OS_KERNEL_VMA_INVALID_DESCRIPTOR_INDEX) {
        if (current_ordinal == ordinal) {
            area = this->pool_->descriptors_[descriptor_index].area;
            return VirtualMemoryAreaStatus::Succeeded;
        }
        ++current_ordinal;
        descriptor_index = this->pool_->descriptors_[descriptor_index].next_descriptor_index;
    }
    return VirtualMemoryAreaStatus::NotMapped;
}

VirtualMemoryMapStatistics VirtualMemoryMap::Statistics() const noexcept {
    if (!this->initialized_) {
        return VirtualMemoryMapStatistics{};
    }
    VirtualMemoryMapStatistics statistics{
        .area_count = this->area_count_,
        .mapped_page_count = OS_KERNEL_VMA_EMPTY_VALUE,
        .readable_page_count = OS_KERNEL_VMA_EMPTY_VALUE,
        .writable_page_count = OS_KERNEL_VMA_EMPTY_VALUE,
        .executable_page_count = OS_KERNEL_VMA_EMPTY_VALUE,
    };
    uint64_t descriptor_index = this->head_descriptor_index_;
    while (descriptor_index != OS_KERNEL_VMA_INVALID_DESCRIPTOR_INDEX) {
        const VirtualMemoryArea &area = this->pool_->descriptors_[descriptor_index].area;
        const uint64_t page_count =
            (area.end_address - area.begin_address) / this->page_size_bytes_;
        statistics.mapped_page_count += page_count;
        if (area.permissions.readable) {
            statistics.readable_page_count += page_count;
        }
        if (area.permissions.writable) {
            statistics.writable_page_count += page_count;
        }
        if (area.permissions.executable) {
            statistics.executable_page_count += page_count;
        }
        descriptor_index = this->pool_->descriptors_[descriptor_index].next_descriptor_index;
    }
    return statistics;
}

VirtualMemoryAreaStatus VirtualMemoryMap::Validate() const noexcept {
    if (!this->initialized_) {
        return VirtualMemoryAreaStatus::NotInitialized;
    }
    if (this->pool_ == nullptr || this->pool_->Validate() != VirtualMemoryAreaStatus::Succeeded ||
        !IsPowerOfTwo(this->page_size_bytes_) ||
        this->hard_area_limit_ == OS_KERNEL_VMA_EMPTY_VALUE ||
        this->owner_identifier_ == OS_KERNEL_VMA_EMPTY_VALUE) {
        return VirtualMemoryAreaStatus::Corrupt;
    }

    uint64_t observed_area_count = OS_KERNEL_VMA_EMPTY_VALUE;
    uint64_t previous_descriptor_index = OS_KERNEL_VMA_INVALID_DESCRIPTOR_INDEX;
    uint64_t descriptor_index = this->head_descriptor_index_;
    uint64_t previous_end_address = OS_KERNEL_VMA_EMPTY_VALUE;
    while (descriptor_index != OS_KERNEL_VMA_INVALID_DESCRIPTOR_INDEX) {
        if (!this->pool_->IsDescriptorIndexValid(descriptor_index) ||
            observed_area_count >= this->hard_area_limit_) {
            return VirtualMemoryAreaStatus::Corrupt;
        }
        const VirtualMemoryAreaDescriptor &descriptor = this->pool_->descriptors_[descriptor_index];
        if (!descriptor.active || descriptor.owner_identifier != this->owner_identifier_ ||
            descriptor.previous_descriptor_index != previous_descriptor_index ||
            !this->IsRangeValid(descriptor.area.begin_address, descriptor.area.end_address) ||
            !this->IsBackingValid(descriptor.area) ||
            (observed_area_count != OS_KERNEL_VMA_EMPTY_VALUE &&
             descriptor.area.begin_address < previous_end_address)) {
            return VirtualMemoryAreaStatus::Corrupt;
        }
        if (previous_descriptor_index != OS_KERNEL_VMA_INVALID_DESCRIPTOR_INDEX) {
            const VirtualMemoryArea &previous_area =
                this->pool_->descriptors_[previous_descriptor_index].area;
            if (previous_area.end_address == descriptor.area.begin_address &&
                this->AreAttributesEqual(previous_area, descriptor.area)) {
                return VirtualMemoryAreaStatus::Corrupt;
            }
        }
        previous_end_address = descriptor.area.end_address;
        previous_descriptor_index = descriptor_index;
        descriptor_index = descriptor.next_descriptor_index;
        ++observed_area_count;
    }
    if (observed_area_count != this->area_count_) {
        return VirtualMemoryAreaStatus::Corrupt;
    }
    return VirtualMemoryAreaStatus::Succeeded;
}

VirtualMemoryAreaStatus VirtualMemoryMap::Destroy() noexcept {
    if (!this->initialized_) {
        return VirtualMemoryAreaStatus::NotInitialized;
    }
    uint64_t descriptor_index = this->head_descriptor_index_;
    while (descriptor_index != OS_KERNEL_VMA_INVALID_DESCRIPTOR_INDEX) {
        const uint64_t next_descriptor_index =
            this->pool_->descriptors_[descriptor_index].next_descriptor_index;
        if (this->pool_->Release(this->owner_identifier_, descriptor_index) !=
            VirtualMemoryAreaStatus::Succeeded) {
            return VirtualMemoryAreaStatus::Corrupt;
        }
        descriptor_index = next_descriptor_index;
    }
    *this = VirtualMemoryMap{};
    return VirtualMemoryAreaStatus::Succeeded;
}

uint64_t VirtualMemoryMap::AreaCount() const noexcept { return this->area_count_; }

uint64_t VirtualMemoryMap::PageSizeBytes() const noexcept { return this->page_size_bytes_; }

bool VirtualMemoryMap::IsRangeValid(const uint64_t begin_address,
                                    const uint64_t end_address) const noexcept {
    return this->page_size_bytes_ != OS_KERNEL_VMA_EMPTY_VALUE && begin_address < end_address &&
           begin_address % this->page_size_bytes_ == OS_KERNEL_VMA_EMPTY_VALUE &&
           end_address % this->page_size_bytes_ == OS_KERNEL_VMA_EMPTY_VALUE;
}

bool VirtualMemoryMap::AreAttributesEqual(const VirtualMemoryArea &left,
                                          const VirtualMemoryArea &right) const noexcept {
    if (IsFileBackedVirtualMemoryAreaKind(left.kind) ||
        IsFileBackedVirtualMemoryAreaKind(right.kind)) {
        return false;
    }
    return left.permissions.readable == right.permissions.readable &&
           left.permissions.writable == right.permissions.writable &&
           left.permissions.executable == right.permissions.executable && left.kind == right.kind;
}

bool VirtualMemoryMap::IsBackingValid(
    const VirtualMemoryArea &area) const noexcept {
    const bool file_backed =
        IsFileBackedVirtualMemoryAreaKind(area.kind);
    if (!file_backed) {
        return area.backing_descriptor_index ==
                   OS_KERNEL_VMA_INVALID_DESCRIPTOR_INDEX &&
               area.backing_generation == OS_KERNEL_VMA_EMPTY_VALUE &&
               area.backing_file_offset_bytes ==
                   OS_KERNEL_VMA_EMPTY_VALUE &&
               area.backing_data_length_bytes ==
                   OS_KERNEL_VMA_EMPTY_VALUE;
    }
    return area.backing_descriptor_index !=
               OS_KERNEL_VMA_INVALID_DESCRIPTOR_INDEX &&
           area.backing_generation != OS_KERNEL_VMA_EMPTY_VALUE &&
           area.backing_file_offset_bytes % this->page_size_bytes_ ==
               OS_KERNEL_VMA_EMPTY_VALUE &&
           area.backing_data_length_bytes <=
               area.end_address - area.begin_address;
}

uint64_t VirtualMemoryMap::AlignUp(const uint64_t value, const uint64_t alignment) const noexcept {
    const uint64_t alignment_mask = alignment - OS_KERNEL_VMA_SINGLE_UNIT;
    if (value > UINT64_MAX - alignment_mask) {
        return OS_KERNEL_VMA_EMPTY_VALUE;
    }
    return (value + alignment_mask) & ~alignment_mask;
}

void VirtualMemoryMap::LinkBetween(const uint64_t descriptor_index,
                                   const uint64_t previous_descriptor_index,
                                   const uint64_t next_descriptor_index) noexcept {
    VirtualMemoryAreaDescriptor &descriptor = this->pool_->descriptors_[descriptor_index];
    descriptor.previous_descriptor_index = previous_descriptor_index;
    descriptor.next_descriptor_index = next_descriptor_index;
    if (previous_descriptor_index == OS_KERNEL_VMA_INVALID_DESCRIPTOR_INDEX) {
        this->head_descriptor_index_ = descriptor_index;
    } else {
        this->pool_->descriptors_[previous_descriptor_index].next_descriptor_index =
            descriptor_index;
    }
    if (next_descriptor_index != OS_KERNEL_VMA_INVALID_DESCRIPTOR_INDEX) {
        this->pool_->descriptors_[next_descriptor_index].previous_descriptor_index =
            descriptor_index;
    }
}

void VirtualMemoryMap::Unlink(const uint64_t descriptor_index) noexcept {
    const VirtualMemoryAreaDescriptor &descriptor = this->pool_->descriptors_[descriptor_index];
    if (descriptor.previous_descriptor_index == OS_KERNEL_VMA_INVALID_DESCRIPTOR_INDEX) {
        this->head_descriptor_index_ = descriptor.next_descriptor_index;
    } else {
        this->pool_->descriptors_[descriptor.previous_descriptor_index].next_descriptor_index =
            descriptor.next_descriptor_index;
    }
    if (descriptor.next_descriptor_index != OS_KERNEL_VMA_INVALID_DESCRIPTOR_INDEX) {
        this->pool_->descriptors_[descriptor.next_descriptor_index].previous_descriptor_index =
            descriptor.previous_descriptor_index;
    }
}

}

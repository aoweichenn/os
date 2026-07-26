#include "os/kernel/kernel_virtual_address_allocator.hpp"

#include "os/kernel/page_table.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_KVA_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_KVA_SINGLE_UNIT = 1ULL;
constexpr uint64_t OS_KERNEL_KVA_BINARY_SEARCH_DIVISOR = 2ULL;

[[nodiscard]] bool IsPowerOfTwo(const uint64_t value) noexcept {
    return value != OS_KERNEL_KVA_EMPTY_VALUE &&
           (value & (value - OS_KERNEL_KVA_SINGLE_UNIT)) == OS_KERNEL_KVA_EMPTY_VALUE;
}

[[nodiscard]] bool CanConvertPagesToBytes(const uint64_t page_count) noexcept {
    return page_count <= UINT64_MAX / OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
}

[[nodiscard]] bool IsPageAligned(const uint64_t address) noexcept {
    return (address & (OS_KERNEL_MEMORY_PAGE_SIZE_BYTES - OS_KERNEL_KVA_SINGLE_UNIT)) ==
           OS_KERNEL_KVA_EMPTY_VALUE;
}

[[nodiscard]] bool IsWindowConfigurationValid(const uint64_t window_begin_address,
                                              const uint64_t window_page_count) noexcept {
    if (!IsPageAligned(window_begin_address) || window_page_count == OS_KERNEL_KVA_EMPTY_VALUE ||
        !CanConvertPagesToBytes(window_page_count)) {
        return false;
    }
    const uint64_t window_size_bytes = window_page_count * OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
    if (window_begin_address > UINT64_MAX - window_size_bytes) {
        return false;
    }
    return IsCanonicalVirtualAddress(window_begin_address) &&
           IsCanonicalVirtualAddress(window_begin_address + window_size_bytes -
                                     OS_KERNEL_KVA_SINGLE_UNIT);
}

[[nodiscard]] bool TryAlignPageIndexUp(const uint64_t page_index,
                                       const uint64_t alignment_page_count,
                                       uint64_t &aligned_page_index) noexcept {
    const uint64_t alignment_mask = alignment_page_count - OS_KERNEL_KVA_SINGLE_UNIT;
    if (page_index > UINT64_MAX - alignment_mask) {
        return false;
    }
    aligned_page_index = (page_index + alignment_mask) & ~alignment_mask;
    return true;
}

}

KernelVirtualAddressAllocator::KernelVirtualAddressAllocator() noexcept
    : window_begin_address_{OS_KERNEL_KVA_EMPTY_VALUE},
      window_page_count_{OS_KERNEL_KVA_EMPTY_VALUE}, descriptors_{nullptr},
      descriptor_capacity_{OS_KERNEL_KVA_EMPTY_VALUE},
      active_descriptor_count_{OS_KERNEL_KVA_EMPTY_VALUE},
      allocated_page_count_{OS_KERNEL_KVA_EMPTY_VALUE},
      reserved_page_count_{OS_KERNEL_KVA_EMPTY_VALUE},
      active_allocation_count_{OS_KERNEL_KVA_EMPTY_VALUE},
      reservation_count_{OS_KERNEL_KVA_EMPTY_VALUE},
      successful_allocation_count_{OS_KERNEL_KVA_EMPTY_VALUE},
      release_count_{OS_KERNEL_KVA_EMPTY_VALUE},
      peak_allocated_page_count_{OS_KERNEL_KVA_EMPTY_VALUE},
      peak_active_allocation_count_{OS_KERNEL_KVA_EMPTY_VALUE} {}

KernelVirtualAddressAllocatorStatus KernelVirtualAddressAllocator::Initialize(
    const uint64_t window_begin_address, const uint64_t window_page_count,
    KernelVirtualAddressRangeDescriptor *const descriptor_storage,
    const uint64_t descriptor_capacity) noexcept {
    if (this->IsInitialized()) {
        return KernelVirtualAddressAllocatorStatus::AlreadyInitialized;
    }
    if (descriptor_storage == nullptr) {
        return KernelVirtualAddressAllocatorStatus::NullDescriptorStorage;
    }
    if (descriptor_capacity == OS_KERNEL_KVA_EMPTY_VALUE) {
        return KernelVirtualAddressAllocatorStatus::EmptyDescriptorCapacity;
    }
    if (!IsWindowConfigurationValid(window_begin_address, window_page_count)) {
        return KernelVirtualAddressAllocatorStatus::InvalidWindow;
    }

    for (uint64_t descriptor_index = OS_KERNEL_KVA_EMPTY_VALUE;
         descriptor_index < descriptor_capacity; ++descriptor_index) {
        descriptor_storage[descriptor_index] = KernelVirtualAddressRangeDescriptor{
            .begin_address = OS_KERNEL_KVA_EMPTY_VALUE,
            .page_count = OS_KERNEL_KVA_EMPTY_VALUE,
            .kind = KernelVirtualAddressRangeKind::Unused,
        };
    }
    this->window_begin_address_ = window_begin_address;
    this->window_page_count_ = window_page_count;
    this->descriptors_ = descriptor_storage;
    this->descriptor_capacity_ = descriptor_capacity;
    this->active_descriptor_count_ = OS_KERNEL_KVA_EMPTY_VALUE;
    this->allocated_page_count_ = OS_KERNEL_KVA_EMPTY_VALUE;
    this->reserved_page_count_ = OS_KERNEL_KVA_EMPTY_VALUE;
    this->active_allocation_count_ = OS_KERNEL_KVA_EMPTY_VALUE;
    this->reservation_count_ = OS_KERNEL_KVA_EMPTY_VALUE;
    this->successful_allocation_count_ = OS_KERNEL_KVA_EMPTY_VALUE;
    this->release_count_ = OS_KERNEL_KVA_EMPTY_VALUE;
    this->peak_allocated_page_count_ = OS_KERNEL_KVA_EMPTY_VALUE;
    this->peak_active_allocation_count_ = OS_KERNEL_KVA_EMPTY_VALUE;
    return KernelVirtualAddressAllocatorStatus::Succeeded;
}

KernelVirtualAddressAllocatorStatus
KernelVirtualAddressAllocator::ReserveRange(const uint64_t begin_address,
                                            const uint64_t page_count) noexcept {
    if (!this->IsInitialized()) {
        return KernelVirtualAddressAllocatorStatus::NotInitialized;
    }
    if (page_count == OS_KERNEL_KVA_EMPTY_VALUE || !CanConvertPagesToBytes(page_count)) {
        return KernelVirtualAddressAllocatorStatus::InvalidPageCount;
    }
    if (!IsPageAligned(begin_address)) {
        return KernelVirtualAddressAllocatorStatus::InvalidAlignment;
    }
    if (!this->IsRangeInsideWindow(begin_address, page_count)) {
        return KernelVirtualAddressAllocatorStatus::InvalidRange;
    }
    const uint64_t insertion_index = this->FindInsertionIndex(begin_address);
    const uint64_t range_size_bytes = page_count * OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
    const uint64_t range_end_address = begin_address + range_size_bytes;
    if ((insertion_index > OS_KERNEL_KVA_EMPTY_VALUE &&
         this->RangeEndAddress(this->descriptors_[insertion_index - OS_KERNEL_KVA_SINGLE_UNIT]) >
             begin_address) ||
        (insertion_index < this->active_descriptor_count_ &&
         range_end_address > this->descriptors_[insertion_index].begin_address)) {
        return KernelVirtualAddressAllocatorStatus::RangeOverlap;
    }
    if (this->active_descriptor_count_ == this->descriptor_capacity_) {
        return KernelVirtualAddressAllocatorStatus::MetadataExhausted;
    }
    if (this->reserved_page_count_ > UINT64_MAX - page_count ||
        this->reservation_count_ == UINT64_MAX) {
        return KernelVirtualAddressAllocatorStatus::CounterOverflow;
    }

    this->InsertDescriptor(insertion_index, KernelVirtualAddressRangeDescriptor{
                                                .begin_address = begin_address,
                                                .page_count = page_count,
                                                .kind = KernelVirtualAddressRangeKind::Reservation,
                                            });
    this->reserved_page_count_ += page_count;
    ++this->reservation_count_;
    return KernelVirtualAddressAllocatorStatus::Succeeded;
}

KernelVirtualAddressAllocatorStatus
KernelVirtualAddressAllocator::TryAllocate(const uint64_t page_count,
                                           const uint64_t alignment_page_count,
                                           KernelVirtualAddressRange &range) noexcept {
    if (!this->IsInitialized()) {
        return KernelVirtualAddressAllocatorStatus::NotInitialized;
    }
    if (page_count == OS_KERNEL_KVA_EMPTY_VALUE || !CanConvertPagesToBytes(page_count)) {
        return KernelVirtualAddressAllocatorStatus::InvalidPageCount;
    }
    if (!IsPowerOfTwo(alignment_page_count) || !CanConvertPagesToBytes(alignment_page_count)) {
        return KernelVirtualAddressAllocatorStatus::InvalidAlignment;
    }
    if (this->active_descriptor_count_ == this->descriptor_capacity_) {
        return KernelVirtualAddressAllocatorStatus::MetadataExhausted;
    }
    if (this->successful_allocation_count_ == UINT64_MAX ||
        this->active_allocation_count_ == UINT64_MAX ||
        this->allocated_page_count_ > UINT64_MAX - page_count) {
        return KernelVirtualAddressAllocatorStatus::CounterOverflow;
    }

    bool best_range_found = false;
    uint64_t best_address = OS_KERNEL_KVA_EMPTY_VALUE;
    uint64_t best_insertion_index = OS_KERNEL_KVA_EMPTY_VALUE;
    uint64_t best_gap_page_count = UINT64_MAX;
    uint64_t gap_begin_address = this->window_begin_address_;
    for (uint64_t descriptor_index = OS_KERNEL_KVA_EMPTY_VALUE;
         descriptor_index <= this->active_descriptor_count_; ++descriptor_index) {
        const uint64_t gap_end_address = descriptor_index < this->active_descriptor_count_
                                             ? this->descriptors_[descriptor_index].begin_address
                                             : this->WindowEndAddress();
        const uint64_t gap_begin_page_index = gap_begin_address / OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
        uint64_t aligned_page_index = OS_KERNEL_KVA_EMPTY_VALUE;
        const bool page_index_alignment_succeeded =
            TryAlignPageIndexUp(gap_begin_page_index, alignment_page_count, aligned_page_index);
        const bool alignment_succeeded =
            page_index_alignment_succeeded && CanConvertPagesToBytes(aligned_page_index);
        const uint64_t aligned_address = alignment_succeeded
                                             ? aligned_page_index * OS_KERNEL_MEMORY_PAGE_SIZE_BYTES
                                             : OS_KERNEL_KVA_EMPTY_VALUE;
        const uint64_t gap_page_count =
            (gap_end_address - gap_begin_address) / OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
        if (alignment_succeeded && aligned_address >= gap_begin_address &&
            aligned_address < gap_end_address &&
            page_count <= (gap_end_address - aligned_address) / OS_KERNEL_MEMORY_PAGE_SIZE_BYTES &&
            gap_page_count < best_gap_page_count) {
            best_range_found = true;
            best_address = aligned_address;
            best_insertion_index = descriptor_index;
            best_gap_page_count = gap_page_count;
            if (gap_page_count == page_count) {
                break;
            }
        }
        if (descriptor_index < this->active_descriptor_count_) {
            gap_begin_address = this->RangeEndAddress(this->descriptors_[descriptor_index]);
        }
    }

    if (!best_range_found) {
        return KernelVirtualAddressAllocatorStatus::OutOfVirtualAddressSpace;
    }
    this->InsertDescriptor(best_insertion_index,
                           KernelVirtualAddressRangeDescriptor{
                               .begin_address = best_address,
                               .page_count = page_count,
                               .kind = KernelVirtualAddressRangeKind::Allocation,
                           });
    this->allocated_page_count_ += page_count;
    ++this->active_allocation_count_;
    ++this->successful_allocation_count_;
    if (this->allocated_page_count_ > this->peak_allocated_page_count_) {
        this->peak_allocated_page_count_ = this->allocated_page_count_;
    }
    if (this->active_allocation_count_ > this->peak_active_allocation_count_) {
        this->peak_active_allocation_count_ = this->active_allocation_count_;
    }
    range = KernelVirtualAddressRange{
        .begin_address = best_address,
        .page_count = page_count,
    };
    return KernelVirtualAddressAllocatorStatus::Succeeded;
}

KernelVirtualAddressAllocatorStatus
KernelVirtualAddressAllocator::TryRelease(const KernelVirtualAddressRange range) noexcept {
    if (!this->IsInitialized()) {
        return KernelVirtualAddressAllocatorStatus::NotInitialized;
    }
    if (range.page_count == OS_KERNEL_KVA_EMPTY_VALUE ||
        !CanConvertPagesToBytes(range.page_count)) {
        return KernelVirtualAddressAllocatorStatus::InvalidPageCount;
    }
    if (!IsPageAligned(range.begin_address)) {
        return KernelVirtualAddressAllocatorStatus::InvalidAlignment;
    }
    if (!this->IsRangeInsideWindow(range.begin_address, range.page_count)) {
        return KernelVirtualAddressAllocatorStatus::InvalidRange;
    }

    const uint64_t descriptor_index = this->FindInsertionIndex(range.begin_address);
    if (descriptor_index == this->active_descriptor_count_ ||
        this->descriptors_[descriptor_index].begin_address != range.begin_address) {
        return KernelVirtualAddressAllocatorStatus::AllocationNotFound;
    }
    const KernelVirtualAddressRangeDescriptor &descriptor = this->descriptors_[descriptor_index];
    if (descriptor.kind == KernelVirtualAddressRangeKind::Reservation) {
        return KernelVirtualAddressAllocatorStatus::ReservedRange;
    }
    if (descriptor.kind != KernelVirtualAddressRangeKind::Allocation) {
        return KernelVirtualAddressAllocatorStatus::CorruptedState;
    }
    if (descriptor.page_count != range.page_count) {
        return KernelVirtualAddressAllocatorStatus::AllocationSizeMismatch;
    }
    if (this->allocated_page_count_ < range.page_count ||
        this->active_allocation_count_ == OS_KERNEL_KVA_EMPTY_VALUE ||
        this->release_count_ == UINT64_MAX) {
        return KernelVirtualAddressAllocatorStatus::CorruptedState;
    }

    this->allocated_page_count_ -= range.page_count;
    --this->active_allocation_count_;
    ++this->release_count_;
    this->RemoveDescriptor(descriptor_index);
    return KernelVirtualAddressAllocatorStatus::Succeeded;
}

bool KernelVirtualAddressAllocator::OwnsAllocation(
    const KernelVirtualAddressRange range) const noexcept {
    if (!this->IsInitialized() || range.page_count == OS_KERNEL_KVA_EMPTY_VALUE ||
        !CanConvertPagesToBytes(range.page_count) || !IsPageAligned(range.begin_address) ||
        !this->IsRangeInsideWindow(range.begin_address, range.page_count)) {
        return false;
    }
    const uint64_t descriptor_index = this->FindInsertionIndex(range.begin_address);
    if (descriptor_index == this->active_descriptor_count_) {
        return false;
    }
    const KernelVirtualAddressRangeDescriptor &descriptor = this->descriptors_[descriptor_index];
    return descriptor.begin_address == range.begin_address &&
           descriptor.page_count == range.page_count &&
           descriptor.kind == KernelVirtualAddressRangeKind::Allocation;
}

KernelVirtualAddressAllocatorStatus KernelVirtualAddressAllocator::Validate() const noexcept {
    if (!this->IsInitialized()) {
        return KernelVirtualAddressAllocatorStatus::NotInitialized;
    }
    if (!IsWindowConfigurationValid(this->window_begin_address_, this->window_page_count_) ||
        this->descriptor_capacity_ == OS_KERNEL_KVA_EMPTY_VALUE ||
        this->active_descriptor_count_ > this->descriptor_capacity_ ||
        this->successful_allocation_count_ < this->release_count_) {
        return KernelVirtualAddressAllocatorStatus::CorruptedState;
    }

    uint64_t calculated_allocated_page_count = OS_KERNEL_KVA_EMPTY_VALUE;
    uint64_t calculated_reserved_page_count = OS_KERNEL_KVA_EMPTY_VALUE;
    uint64_t calculated_active_allocation_count = OS_KERNEL_KVA_EMPTY_VALUE;
    uint64_t calculated_reservation_count = OS_KERNEL_KVA_EMPTY_VALUE;
    uint64_t previous_end_address = this->window_begin_address_;
    for (uint64_t descriptor_index = OS_KERNEL_KVA_EMPTY_VALUE;
         descriptor_index < this->active_descriptor_count_; ++descriptor_index) {
        const KernelVirtualAddressRangeDescriptor &descriptor =
            this->descriptors_[descriptor_index];
        if (descriptor.page_count == OS_KERNEL_KVA_EMPTY_VALUE ||
            !this->IsRangeInsideWindow(descriptor.begin_address, descriptor.page_count) ||
            descriptor.begin_address < previous_end_address) {
            return KernelVirtualAddressAllocatorStatus::CorruptedState;
        }
        if (descriptor.kind == KernelVirtualAddressRangeKind::Allocation) {
            if (calculated_allocated_page_count > UINT64_MAX - descriptor.page_count ||
                calculated_active_allocation_count == UINT64_MAX) {
                return KernelVirtualAddressAllocatorStatus::CorruptedState;
            }
            calculated_allocated_page_count += descriptor.page_count;
            ++calculated_active_allocation_count;
        } else if (descriptor.kind == KernelVirtualAddressRangeKind::Reservation) {
            if (calculated_reserved_page_count > UINT64_MAX - descriptor.page_count ||
                calculated_reservation_count == UINT64_MAX) {
                return KernelVirtualAddressAllocatorStatus::CorruptedState;
            }
            calculated_reserved_page_count += descriptor.page_count;
            ++calculated_reservation_count;
        } else {
            return KernelVirtualAddressAllocatorStatus::CorruptedState;
        }
        previous_end_address = this->RangeEndAddress(descriptor);
    }
    for (uint64_t descriptor_index = this->active_descriptor_count_;
         descriptor_index < this->descriptor_capacity_; ++descriptor_index) {
        const KernelVirtualAddressRangeDescriptor &descriptor =
            this->descriptors_[descriptor_index];
        if (descriptor.begin_address != OS_KERNEL_KVA_EMPTY_VALUE ||
            descriptor.page_count != OS_KERNEL_KVA_EMPTY_VALUE ||
            descriptor.kind != KernelVirtualAddressRangeKind::Unused) {
            return KernelVirtualAddressAllocatorStatus::CorruptedState;
        }
    }

    if (calculated_allocated_page_count != this->allocated_page_count_ ||
        calculated_reserved_page_count != this->reserved_page_count_ ||
        calculated_active_allocation_count != this->active_allocation_count_ ||
        calculated_reservation_count != this->reservation_count_ ||
        this->successful_allocation_count_ - this->release_count_ !=
            this->active_allocation_count_ ||
        this->peak_allocated_page_count_ < this->allocated_page_count_ ||
        this->peak_active_allocation_count_ < this->active_allocation_count_ ||
        this->allocated_page_count_ > this->window_page_count_ ||
        this->reserved_page_count_ > this->window_page_count_ - this->allocated_page_count_) {
        return KernelVirtualAddressAllocatorStatus::CorruptedState;
    }
    return KernelVirtualAddressAllocatorStatus::Succeeded;
}

KernelVirtualAddressAllocatorStatistics KernelVirtualAddressAllocator::Statistics() const noexcept {
    if (!this->IsInitialized()) {
        return KernelVirtualAddressAllocatorStatistics{};
    }
    return KernelVirtualAddressAllocatorStatistics{
        .window_begin_address = this->window_begin_address_,
        .window_size_bytes = this->window_page_count_ * OS_KERNEL_MEMORY_PAGE_SIZE_BYTES,
        .capacity_page_count = this->window_page_count_,
        .free_page_count =
            this->window_page_count_ - this->allocated_page_count_ - this->reserved_page_count_,
        .allocated_page_count = this->allocated_page_count_,
        .reserved_page_count = this->reserved_page_count_,
        .descriptor_capacity = this->descriptor_capacity_,
        .active_descriptor_count = this->active_descriptor_count_,
        .active_allocation_count = this->active_allocation_count_,
        .reservation_count = this->reservation_count_,
        .successful_allocation_count = this->successful_allocation_count_,
        .release_count = this->release_count_,
        .peak_allocated_page_count = this->peak_allocated_page_count_,
        .peak_active_allocation_count = this->peak_active_allocation_count_,
        .largest_free_range_page_count = this->CalculateLargestFreeRangePageCount(),
    };
}

bool KernelVirtualAddressAllocator::IsInitialized() const noexcept {
    return this->descriptors_ != nullptr;
}

bool KernelVirtualAddressAllocator::IsRangeInsideWindow(const uint64_t begin_address,
                                                        const uint64_t page_count) const noexcept {
    if (page_count == OS_KERNEL_KVA_EMPTY_VALUE || !CanConvertPagesToBytes(page_count) ||
        !IsPageAligned(begin_address) || begin_address < this->window_begin_address_) {
        return false;
    }
    const uint64_t size_bytes = page_count * OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
    return begin_address <= UINT64_MAX - size_bytes &&
           begin_address + size_bytes <= this->WindowEndAddress();
}

uint64_t KernelVirtualAddressAllocator::WindowEndAddress() const noexcept {
    return this->window_begin_address_ +
           this->window_page_count_ * OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
}

uint64_t KernelVirtualAddressAllocator::RangeEndAddress(
    const KernelVirtualAddressRangeDescriptor &descriptor) const noexcept {
    return descriptor.begin_address + descriptor.page_count * OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
}

uint64_t
KernelVirtualAddressAllocator::FindInsertionIndex(const uint64_t begin_address) const noexcept {
    uint64_t first_index = OS_KERNEL_KVA_EMPTY_VALUE;
    uint64_t remaining_count = this->active_descriptor_count_;
    while (remaining_count != OS_KERNEL_KVA_EMPTY_VALUE) {
        const uint64_t half_count = remaining_count / OS_KERNEL_KVA_BINARY_SEARCH_DIVISOR;
        const uint64_t middle_index = first_index + half_count;
        if (this->descriptors_[middle_index].begin_address < begin_address) {
            first_index = middle_index + OS_KERNEL_KVA_SINGLE_UNIT;
            remaining_count -= half_count + OS_KERNEL_KVA_SINGLE_UNIT;
        } else {
            remaining_count = half_count;
        }
    }
    return first_index;
}

uint64_t KernelVirtualAddressAllocator::CalculateLargestFreeRangePageCount() const noexcept {
    uint64_t largest_free_range_page_count = OS_KERNEL_KVA_EMPTY_VALUE;
    uint64_t gap_begin_address = this->window_begin_address_;
    for (uint64_t descriptor_index = OS_KERNEL_KVA_EMPTY_VALUE;
         descriptor_index <= this->active_descriptor_count_; ++descriptor_index) {
        const uint64_t gap_end_address = descriptor_index < this->active_descriptor_count_
                                             ? this->descriptors_[descriptor_index].begin_address
                                             : this->WindowEndAddress();
        const uint64_t gap_page_count =
            (gap_end_address - gap_begin_address) / OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
        if (gap_page_count > largest_free_range_page_count) {
            largest_free_range_page_count = gap_page_count;
        }
        if (descriptor_index < this->active_descriptor_count_) {
            gap_begin_address = this->RangeEndAddress(this->descriptors_[descriptor_index]);
        }
    }
    return largest_free_range_page_count;
}

void KernelVirtualAddressAllocator::InsertDescriptor(
    const uint64_t descriptor_index,
    const KernelVirtualAddressRangeDescriptor descriptor) noexcept {
    for (uint64_t move_index = this->active_descriptor_count_; move_index > descriptor_index;
         --move_index) {
        this->descriptors_[move_index] = this->descriptors_[move_index - OS_KERNEL_KVA_SINGLE_UNIT];
    }
    this->descriptors_[descriptor_index] = descriptor;
    ++this->active_descriptor_count_;
}

void KernelVirtualAddressAllocator::RemoveDescriptor(const uint64_t descriptor_index) noexcept {
    for (uint64_t move_index = descriptor_index + OS_KERNEL_KVA_SINGLE_UNIT;
         move_index < this->active_descriptor_count_; ++move_index) {
        this->descriptors_[move_index - OS_KERNEL_KVA_SINGLE_UNIT] = this->descriptors_[move_index];
    }
    --this->active_descriptor_count_;
    this->descriptors_[this->active_descriptor_count_] = KernelVirtualAddressRangeDescriptor{
        .begin_address = OS_KERNEL_KVA_EMPTY_VALUE,
        .page_count = OS_KERNEL_KVA_EMPTY_VALUE,
        .kind = KernelVirtualAddressRangeKind::Unused,
    };
}

}

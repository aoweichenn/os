#include "os/kernel/memory/kernel_type_cache.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_TYPE_CACHE_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_TYPE_CACHE_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_KERNEL_TYPE_CACHE_BITS_PER_BITMAP_BYTE = 8ULL;
constexpr uint64_t OS_KERNEL_TYPE_CACHE_BITMAP_BIT_MASK =
    OS_KERNEL_TYPE_CACHE_BITS_PER_BITMAP_BYTE - OS_KERNEL_TYPE_CACHE_COUNTER_INCREMENT;
constexpr uint64_t OS_KERNEL_TYPE_CACHE_FREE_LINK_SIZE_BYTES = sizeof(uint64_t);
constexpr uint64_t OS_KERNEL_TYPE_CACHE_INVALID_OBJECT_INDEX = UINT64_MAX;

struct KernelTypeCacheLayout final {
    uint64_t allocation_bitmap_size_bytes;
    uint64_t object_storage_offset_bytes;
    uint64_t storage_alignment_bytes;
    uint64_t slot_stride_bytes;
    uint64_t backing_storage_size_bytes;
};

[[nodiscard]] bool IsPowerOfTwo(const uint64_t value) noexcept {
    return value != OS_KERNEL_TYPE_CACHE_EMPTY_VALUE &&
           (value & (value - OS_KERNEL_TYPE_CACHE_COUNTER_INCREMENT)) ==
               OS_KERNEL_TYPE_CACHE_EMPTY_VALUE;
}

[[nodiscard]] uint64_t Maximum(const uint64_t left, const uint64_t right) noexcept {
    return left > right ? left : right;
}

[[nodiscard]] bool TryAlignUp(const uint64_t value, const uint64_t alignment_bytes,
                              uint64_t &aligned_value) noexcept {
    const uint64_t alignment_mask = alignment_bytes - OS_KERNEL_TYPE_CACHE_COUNTER_INCREMENT;
    if (value > UINT64_MAX - alignment_mask) {
        return false;
    }
    aligned_value = (value + alignment_mask) & ~alignment_mask;
    return true;
}

[[nodiscard]] bool TryCalculateLayout(const uint64_t object_size_bytes,
                                      const uint64_t object_alignment_bytes,
                                      const uint64_t capacity,
                                      KernelTypeCacheLayout &layout) noexcept {
    const uint64_t allocation_bitmap_size_bytes =
        capacity / OS_KERNEL_TYPE_CACHE_BITS_PER_BITMAP_BYTE +
        (capacity % OS_KERNEL_TYPE_CACHE_BITS_PER_BITMAP_BYTE == OS_KERNEL_TYPE_CACHE_EMPTY_VALUE
             ? OS_KERNEL_TYPE_CACHE_EMPTY_VALUE
             : OS_KERNEL_TYPE_CACHE_COUNTER_INCREMENT);
    const uint64_t storage_alignment_bytes =
        Maximum(object_alignment_bytes, OS_KERNEL_TYPE_CACHE_FREE_LINK_SIZE_BYTES);
    uint64_t slot_stride_bytes = OS_KERNEL_TYPE_CACHE_EMPTY_VALUE;
    if (!TryAlignUp(Maximum(object_size_bytes, OS_KERNEL_TYPE_CACHE_FREE_LINK_SIZE_BYTES),
                    storage_alignment_bytes, slot_stride_bytes)) {
        return false;
    }
    uint64_t object_storage_offset_bytes = OS_KERNEL_TYPE_CACHE_EMPTY_VALUE;
    if (!TryAlignUp(allocation_bitmap_size_bytes, storage_alignment_bytes,
                    object_storage_offset_bytes) ||
        capacity > UINT64_MAX / slot_stride_bytes) {
        return false;
    }
    const uint64_t object_storage_size_bytes = capacity * slot_stride_bytes;
    if (object_storage_offset_bytes > UINT64_MAX - object_storage_size_bytes) {
        return false;
    }
    layout = KernelTypeCacheLayout{
        .allocation_bitmap_size_bytes = allocation_bitmap_size_bytes,
        .object_storage_offset_bytes = object_storage_offset_bytes,
        .storage_alignment_bytes = storage_alignment_bytes,
        .slot_stride_bytes = slot_stride_bytes,
        .backing_storage_size_bytes = object_storage_offset_bytes + object_storage_size_bytes,
    };
    return true;
}

}

KernelFixedObjectCache::KernelFixedObjectCache() noexcept
    : heap_{nullptr}, backing_allocation_{nullptr}, allocation_bitmap_{nullptr},
      object_storage_base_address_{OS_KERNEL_TYPE_CACHE_EMPTY_VALUE},
      object_size_bytes_{OS_KERNEL_TYPE_CACHE_EMPTY_VALUE},
      object_alignment_bytes_{OS_KERNEL_TYPE_CACHE_EMPTY_VALUE},
      storage_alignment_bytes_{OS_KERNEL_TYPE_CACHE_EMPTY_VALUE},
      slot_stride_bytes_{OS_KERNEL_TYPE_CACHE_EMPTY_VALUE},
      capacity_{OS_KERNEL_TYPE_CACHE_EMPTY_VALUE},
      allocation_bitmap_size_bytes_{OS_KERNEL_TYPE_CACHE_EMPTY_VALUE},
      backing_storage_size_bytes_{OS_KERNEL_TYPE_CACHE_EMPTY_VALUE},
      free_list_head_index_{OS_KERNEL_TYPE_CACHE_INVALID_OBJECT_INDEX},
      active_object_count_{OS_KERNEL_TYPE_CACHE_EMPTY_VALUE},
      free_object_count_{OS_KERNEL_TYPE_CACHE_EMPTY_VALUE},
      successful_allocation_count_{OS_KERNEL_TYPE_CACHE_EMPTY_VALUE},
      release_count_{OS_KERNEL_TYPE_CACHE_EMPTY_VALUE},
      peak_active_object_count_{OS_KERNEL_TYPE_CACHE_EMPTY_VALUE} {}

KernelTypeCacheStatus KernelFixedObjectCache::Initialize(KernelHeap &heap,
                                                         const uint64_t object_size_bytes,
                                                         const uint64_t object_alignment_bytes,
                                                         const uint64_t capacity) noexcept {
    if (this->IsInitialized()) {
        return KernelTypeCacheStatus::AlreadyInitialized;
    }
    if (object_size_bytes == OS_KERNEL_TYPE_CACHE_EMPTY_VALUE) {
        return KernelTypeCacheStatus::EmptyObjectSize;
    }
    if (!IsPowerOfTwo(object_alignment_bytes)) {
        return KernelTypeCacheStatus::InvalidObjectAlignment;
    }
    if (capacity == OS_KERNEL_TYPE_CACHE_EMPTY_VALUE) {
        return KernelTypeCacheStatus::EmptyCapacity;
    }

    KernelTypeCacheLayout layout{};
    if (!TryCalculateLayout(object_size_bytes, object_alignment_bytes, capacity, layout) ||
        layout.backing_storage_size_bytes == OS_KERNEL_TYPE_CACHE_EMPTY_VALUE) {
        return KernelTypeCacheStatus::SizeOverflow;
    }
    void *backing_allocation = nullptr;
    if (heap.TryAllocate(layout.backing_storage_size_bytes, layout.storage_alignment_bytes,
                         backing_allocation) != KernelHeapStatus::Succeeded) {
        return KernelTypeCacheStatus::BackingAllocationFailed;
    }

    this->heap_ = &heap;
    this->backing_allocation_ = backing_allocation;
    this->allocation_bitmap_ = static_cast<uint8_t *>(backing_allocation);
    this->object_storage_base_address_ =
        reinterpret_cast<uint64_t>(backing_allocation) + layout.object_storage_offset_bytes;
    this->object_size_bytes_ = object_size_bytes;
    this->object_alignment_bytes_ = object_alignment_bytes;
    this->storage_alignment_bytes_ = layout.storage_alignment_bytes;
    this->slot_stride_bytes_ = layout.slot_stride_bytes;
    this->capacity_ = capacity;
    this->allocation_bitmap_size_bytes_ = layout.allocation_bitmap_size_bytes;
    this->backing_storage_size_bytes_ = layout.backing_storage_size_bytes;
    this->free_list_head_index_ = OS_KERNEL_TYPE_CACHE_EMPTY_VALUE;
    this->active_object_count_ = OS_KERNEL_TYPE_CACHE_EMPTY_VALUE;
    this->free_object_count_ = capacity;
    this->successful_allocation_count_ = OS_KERNEL_TYPE_CACHE_EMPTY_VALUE;
    this->release_count_ = OS_KERNEL_TYPE_CACHE_EMPTY_VALUE;
    this->peak_active_object_count_ = OS_KERNEL_TYPE_CACHE_EMPTY_VALUE;

    for (uint64_t byte_index = OS_KERNEL_TYPE_CACHE_EMPTY_VALUE;
         byte_index < this->allocation_bitmap_size_bytes_; ++byte_index) {
        this->allocation_bitmap_[byte_index] = 0U;
    }
    for (uint64_t object_index = OS_KERNEL_TYPE_CACHE_EMPTY_VALUE; object_index < this->capacity_;
         ++object_index) {
        const uint64_t next_object_index =
            object_index + OS_KERNEL_TYPE_CACHE_COUNTER_INCREMENT < this->capacity_
                ? object_index + OS_KERNEL_TYPE_CACHE_COUNTER_INCREMENT
                : OS_KERNEL_TYPE_CACHE_INVALID_OBJECT_INDEX;
        this->WriteFreeNextIndex(object_index, next_object_index);
    }
    return KernelTypeCacheStatus::Succeeded;
}

KernelTypeCacheStatus KernelFixedObjectCache::TryAcquire(void *&object_storage) noexcept {
    if (!this->IsInitialized()) {
        return KernelTypeCacheStatus::NotInitialized;
    }
    if (this->active_object_count_ > this->capacity_ ||
        this->free_object_count_ > this->capacity_ ||
        this->active_object_count_ != this->capacity_ - this->free_object_count_ ||
        this->successful_allocation_count_ < this->release_count_ ||
        this->successful_allocation_count_ - this->release_count_ != this->active_object_count_ ||
        this->peak_active_object_count_ < this->active_object_count_ ||
        this->peak_active_object_count_ > this->capacity_) {
        return KernelTypeCacheStatus::CorruptedState;
    }
    if (this->free_object_count_ == OS_KERNEL_TYPE_CACHE_EMPTY_VALUE) {
        if (this->free_list_head_index_ != OS_KERNEL_TYPE_CACHE_INVALID_OBJECT_INDEX) {
            return KernelTypeCacheStatus::CorruptedState;
        }
        return KernelTypeCacheStatus::OutOfObjects;
    }
    if (this->free_list_head_index_ >= this->capacity_ ||
        this->IsObjectAllocated(this->free_list_head_index_)) {
        return KernelTypeCacheStatus::CorruptedState;
    }
    const uint64_t acquired_object_index = this->free_list_head_index_;
    const uint64_t next_object_index = this->ReadFreeNextIndex(acquired_object_index);
    if ((this->free_object_count_ == OS_KERNEL_TYPE_CACHE_COUNTER_INCREMENT &&
         next_object_index != OS_KERNEL_TYPE_CACHE_INVALID_OBJECT_INDEX) ||
        (this->free_object_count_ > OS_KERNEL_TYPE_CACHE_COUNTER_INCREMENT &&
         (next_object_index >= this->capacity_ || next_object_index == acquired_object_index ||
          this->IsObjectAllocated(next_object_index)))) {
        return KernelTypeCacheStatus::CorruptedState;
    }
    if (this->successful_allocation_count_ == UINT64_MAX) {
        return KernelTypeCacheStatus::CounterOverflow;
    }

    this->SetObjectAllocated(acquired_object_index, true);
    this->free_list_head_index_ = next_object_index;
    ++this->active_object_count_;
    --this->free_object_count_;
    ++this->successful_allocation_count_;
    this->peak_active_object_count_ =
        Maximum(this->peak_active_object_count_, this->active_object_count_);
    object_storage = reinterpret_cast<void *>(this->ObjectAddress(acquired_object_index));
    return KernelTypeCacheStatus::Succeeded;
}

KernelTypeCacheStatus KernelFixedObjectCache::TryRelease(void *const object_storage) noexcept {
    if (!this->IsInitialized()) {
        return KernelTypeCacheStatus::NotInitialized;
    }
    if (object_storage == nullptr) {
        return KernelTypeCacheStatus::NullObject;
    }
    const uint64_t object_address = reinterpret_cast<uint64_t>(object_storage);
    const uint64_t object_storage_size_bytes = this->slot_stride_bytes_ * this->capacity_;
    if (object_address < this->object_storage_base_address_ ||
        object_address >= this->object_storage_base_address_ + object_storage_size_bytes) {
        return KernelTypeCacheStatus::InvalidObject;
    }
    const uint64_t object_offset_bytes = object_address - this->object_storage_base_address_;
    if (object_offset_bytes % this->slot_stride_bytes_ != OS_KERNEL_TYPE_CACHE_EMPTY_VALUE) {
        return KernelTypeCacheStatus::InvalidObject;
    }
    const uint64_t object_index = object_offset_bytes / this->slot_stride_bytes_;
    if (!this->IsObjectAllocated(object_index)) {
        return KernelTypeCacheStatus::ObjectNotActive;
    }
    if (this->active_object_count_ == OS_KERNEL_TYPE_CACHE_EMPTY_VALUE ||
        this->active_object_count_ > this->capacity_ ||
        this->free_object_count_ >= this->capacity_ ||
        this->active_object_count_ != this->capacity_ - this->free_object_count_ ||
        this->successful_allocation_count_ < this->release_count_ ||
        this->successful_allocation_count_ - this->release_count_ != this->active_object_count_ ||
        this->peak_active_object_count_ < this->active_object_count_ ||
        this->peak_active_object_count_ > this->capacity_ ||
        (this->free_object_count_ == OS_KERNEL_TYPE_CACHE_EMPTY_VALUE &&
         this->free_list_head_index_ != OS_KERNEL_TYPE_CACHE_INVALID_OBJECT_INDEX) ||
        (this->free_object_count_ != OS_KERNEL_TYPE_CACHE_EMPTY_VALUE &&
         (this->free_list_head_index_ >= this->capacity_ ||
          this->IsObjectAllocated(this->free_list_head_index_)))) {
        return KernelTypeCacheStatus::CorruptedState;
    }
    if (this->release_count_ == UINT64_MAX) {
        return KernelTypeCacheStatus::CounterOverflow;
    }

    this->SetObjectAllocated(object_index, false);
    this->WriteFreeNextIndex(object_index, this->free_list_head_index_);
    this->free_list_head_index_ = object_index;
    --this->active_object_count_;
    ++this->free_object_count_;
    ++this->release_count_;
    return KernelTypeCacheStatus::Succeeded;
}

KernelTypeCacheStatus KernelFixedObjectCache::Destroy() noexcept {
    if (!this->IsInitialized()) {
        return KernelTypeCacheStatus::NotInitialized;
    }
    if (this->active_object_count_ != OS_KERNEL_TYPE_CACHE_EMPTY_VALUE) {
        return KernelTypeCacheStatus::ActiveObjectsRemain;
    }
    if (this->Validate() != KernelTypeCacheStatus::Succeeded) {
        return KernelTypeCacheStatus::CorruptedState;
    }
    if (this->heap_->TryRelease(this->backing_allocation_) != KernelHeapStatus::Succeeded) {
        return KernelTypeCacheStatus::BackingReleaseFailed;
    }
    this->ResetState();
    return KernelTypeCacheStatus::Succeeded;
}

KernelTypeCacheStatus KernelFixedObjectCache::Validate() const noexcept {
    if (!this->IsInitialized()) {
        return KernelTypeCacheStatus::NotInitialized;
    }
    KernelTypeCacheLayout expected_layout{};
    if (!TryCalculateLayout(this->object_size_bytes_, this->object_alignment_bytes_,
                            this->capacity_, expected_layout) ||
        this->allocation_bitmap_ != static_cast<uint8_t *>(this->backing_allocation_) ||
        expected_layout.storage_alignment_bytes != this->storage_alignment_bytes_ ||
        expected_layout.slot_stride_bytes != this->slot_stride_bytes_ ||
        expected_layout.allocation_bitmap_size_bytes != this->allocation_bitmap_size_bytes_ ||
        expected_layout.backing_storage_size_bytes != this->backing_storage_size_bytes_ ||
        reinterpret_cast<uint64_t>(this->backing_allocation_) +
                expected_layout.object_storage_offset_bytes !=
            this->object_storage_base_address_ ||
        this->active_object_count_ > this->capacity_ ||
        this->free_object_count_ > this->capacity_ ||
        this->active_object_count_ != this->capacity_ - this->free_object_count_ ||
        this->peak_active_object_count_ < this->active_object_count_ ||
        this->peak_active_object_count_ > this->capacity_ ||
        this->successful_allocation_count_ < this->peak_active_object_count_ ||
        this->successful_allocation_count_ < this->release_count_ ||
        this->successful_allocation_count_ - this->release_count_ != this->active_object_count_) {
        return KernelTypeCacheStatus::CorruptedState;
    }

    uint64_t observed_active_object_count = OS_KERNEL_TYPE_CACHE_EMPTY_VALUE;
    for (uint64_t object_index = OS_KERNEL_TYPE_CACHE_EMPTY_VALUE; object_index < this->capacity_;
         ++object_index) {
        if (this->IsObjectAllocated(object_index)) {
            ++observed_active_object_count;
        }
    }
    const uint64_t used_tail_bit_count =
        this->capacity_ % OS_KERNEL_TYPE_CACHE_BITS_PER_BITMAP_BYTE;
    if (used_tail_bit_count != OS_KERNEL_TYPE_CACHE_EMPTY_VALUE) {
        const uint8_t used_tail_bit_mask = static_cast<uint8_t>((1U << used_tail_bit_count) - 1U);
        const uint8_t tail_byte = this->allocation_bitmap_[this->allocation_bitmap_size_bytes_ -
                                                           OS_KERNEL_TYPE_CACHE_COUNTER_INCREMENT];
        if ((tail_byte & static_cast<uint8_t>(~used_tail_bit_mask)) != 0U) {
            return KernelTypeCacheStatus::CorruptedState;
        }
    }

    uint64_t observed_free_object_count = OS_KERNEL_TYPE_CACHE_EMPTY_VALUE;
    uint64_t free_object_index = this->free_list_head_index_;
    while (free_object_index != OS_KERNEL_TYPE_CACHE_INVALID_OBJECT_INDEX) {
        if (observed_free_object_count >= this->capacity_ || free_object_index >= this->capacity_ ||
            this->IsObjectAllocated(free_object_index)) {
            return KernelTypeCacheStatus::CorruptedState;
        }
        free_object_index = this->ReadFreeNextIndex(free_object_index);
        ++observed_free_object_count;
    }
    if (observed_active_object_count != this->active_object_count_ ||
        observed_free_object_count != this->free_object_count_) {
        return KernelTypeCacheStatus::CorruptedState;
    }
    return KernelTypeCacheStatus::Succeeded;
}

KernelTypeCacheStatistics KernelFixedObjectCache::Statistics() const noexcept {
    return KernelTypeCacheStatistics{
        .object_size_bytes = this->object_size_bytes_,
        .object_alignment_bytes = this->object_alignment_bytes_,
        .slot_stride_bytes = this->slot_stride_bytes_,
        .capacity = this->capacity_,
        .active_object_count = this->active_object_count_,
        .free_object_count = this->free_object_count_,
        .successful_allocation_count = this->successful_allocation_count_,
        .release_count = this->release_count_,
        .peak_active_object_count = this->peak_active_object_count_,
        .backing_storage_size_bytes = this->backing_storage_size_bytes_,
    };
}

bool KernelFixedObjectCache::IsInitialized() const noexcept {
    return this->heap_ != nullptr && this->backing_allocation_ != nullptr &&
           this->allocation_bitmap_ != nullptr;
}

uint64_t KernelFixedObjectCache::ObjectAddress(const uint64_t object_index) const noexcept {
    return this->object_storage_base_address_ + object_index * this->slot_stride_bytes_;
}

bool KernelFixedObjectCache::IsObjectAllocated(const uint64_t object_index) const noexcept {
    const uint64_t byte_index = object_index / OS_KERNEL_TYPE_CACHE_BITS_PER_BITMAP_BYTE;
    const uint64_t bit_index = object_index & OS_KERNEL_TYPE_CACHE_BITMAP_BIT_MASK;
    const uint8_t bit = static_cast<uint8_t>(1U << bit_index);
    return (this->allocation_bitmap_[byte_index] & bit) != 0U;
}

void KernelFixedObjectCache::SetObjectAllocated(const uint64_t object_index,
                                                const bool allocated) noexcept {
    const uint64_t byte_index = object_index / OS_KERNEL_TYPE_CACHE_BITS_PER_BITMAP_BYTE;
    const uint64_t bit_index = object_index & OS_KERNEL_TYPE_CACHE_BITMAP_BIT_MASK;
    const uint8_t bit = static_cast<uint8_t>(1U << bit_index);
    if (allocated) {
        this->allocation_bitmap_[byte_index] =
            static_cast<uint8_t>(this->allocation_bitmap_[byte_index] | bit);
    } else {
        this->allocation_bitmap_[byte_index] =
            static_cast<uint8_t>(this->allocation_bitmap_[byte_index] & ~bit);
    }
}

uint64_t KernelFixedObjectCache::ReadFreeNextIndex(const uint64_t object_index) const noexcept {
    return *reinterpret_cast<const uint64_t *>(this->ObjectAddress(object_index));
}

void KernelFixedObjectCache::WriteFreeNextIndex(const uint64_t object_index,
                                                const uint64_t next_object_index) noexcept {
    *reinterpret_cast<uint64_t *>(this->ObjectAddress(object_index)) = next_object_index;
}

void KernelFixedObjectCache::ResetState() noexcept {
    this->heap_ = nullptr;
    this->backing_allocation_ = nullptr;
    this->allocation_bitmap_ = nullptr;
    this->object_storage_base_address_ = OS_KERNEL_TYPE_CACHE_EMPTY_VALUE;
    this->object_size_bytes_ = OS_KERNEL_TYPE_CACHE_EMPTY_VALUE;
    this->object_alignment_bytes_ = OS_KERNEL_TYPE_CACHE_EMPTY_VALUE;
    this->storage_alignment_bytes_ = OS_KERNEL_TYPE_CACHE_EMPTY_VALUE;
    this->slot_stride_bytes_ = OS_KERNEL_TYPE_CACHE_EMPTY_VALUE;
    this->capacity_ = OS_KERNEL_TYPE_CACHE_EMPTY_VALUE;
    this->allocation_bitmap_size_bytes_ = OS_KERNEL_TYPE_CACHE_EMPTY_VALUE;
    this->backing_storage_size_bytes_ = OS_KERNEL_TYPE_CACHE_EMPTY_VALUE;
    this->free_list_head_index_ = OS_KERNEL_TYPE_CACHE_INVALID_OBJECT_INDEX;
    this->active_object_count_ = OS_KERNEL_TYPE_CACHE_EMPTY_VALUE;
    this->free_object_count_ = OS_KERNEL_TYPE_CACHE_EMPTY_VALUE;
    this->successful_allocation_count_ = OS_KERNEL_TYPE_CACHE_EMPTY_VALUE;
    this->release_count_ = OS_KERNEL_TYPE_CACHE_EMPTY_VALUE;
    this->peak_active_object_count_ = OS_KERNEL_TYPE_CACHE_EMPTY_VALUE;
}

}

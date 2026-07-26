#include "os/kernel/physical_frame_allocator.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_FRAME_ALLOCATOR_BITS_PER_STATE = 2ULL;
constexpr uint64_t OS_KERNEL_FRAME_ALLOCATOR_STATES_PER_BYTE = 4ULL;
constexpr uint8_t OS_KERNEL_FRAME_ALLOCATOR_STATE_MASK = 0x03U;
constexpr uint64_t OS_KERNEL_FRAME_ALLOCATOR_BYTE_ROUNDING =
    OS_KERNEL_FRAME_ALLOCATOR_STATES_PER_BYTE - 1ULL;
constexpr uint64_t OS_KERNEL_FRAME_ALLOCATOR_PAGE_MASK = OS_KERNEL_MEMORY_PAGE_SIZE_BYTES - 1ULL;
constexpr uint64_t OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE = 0ULL;
constexpr uint8_t OS_KERNEL_FRAME_ALLOCATOR_FREE_STATE_BYTE = 0x55U;
constexpr uint64_t OS_KERNEL_FRAME_ALLOCATOR_BITS_PER_BITMAP_BYTE = 8ULL;
constexpr uint64_t OS_KERNEL_FRAME_ALLOCATOR_BITMAP_BIT_MASK =
    OS_KERNEL_FRAME_ALLOCATOR_BITS_PER_BITMAP_BYTE - 1ULL;
constexpr uint64_t OS_KERNEL_FRAME_ALLOCATOR_BITMAP_FAMILY_COUNT = 2ULL;
constexpr uint8_t OS_KERNEL_FRAME_ALLOCATOR_EMPTY_BITMAP_BYTE = 0U;

[[nodiscard]] uint64_t AlignUpToPage(const uint64_t address) noexcept {
    return (address + OS_KERNEL_FRAME_ALLOCATOR_PAGE_MASK) & ~OS_KERNEL_FRAME_ALLOCATOR_PAGE_MASK;
}

[[nodiscard]] uint64_t AlignDownToPage(const uint64_t address) noexcept {
    return address & ~OS_KERNEL_FRAME_ALLOCATOR_PAGE_MASK;
}

[[nodiscard]] uint64_t Minimum(const uint64_t left, const uint64_t right) noexcept {
    return left < right ? left : right;
}

[[nodiscard]] uint64_t FloorLog2(uint64_t value) noexcept {
    uint64_t order = OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE;
    while (value > 1ULL) {
        value >>= 1ULL;
        ++order;
    }
    return order;
}

[[nodiscard]] uint64_t BlockCountRoundedUp(const uint64_t frame_count,
                                           const uint64_t order) noexcept {
    const uint64_t block_frame_count = 1ULL << order;
    return (frame_count + block_frame_count - 1ULL) / block_frame_count;
}

[[nodiscard]] uint64_t BitmapSizeBytes(const uint64_t block_count) noexcept {
    return (block_count + OS_KERNEL_FRAME_ALLOCATOR_BITMAP_BIT_MASK) /
           OS_KERNEL_FRAME_ALLOCATOR_BITS_PER_BITMAP_BYTE;
}

}

uint64_t
CalculatePhysicalFrameStateStorageSizeBytes(const uint64_t managed_limit_address) noexcept {
    if (managed_limit_address == OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE ||
        (managed_limit_address & OS_KERNEL_FRAME_ALLOCATOR_PAGE_MASK) !=
            OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE) {
        return OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE;
    }
    const uint64_t managed_frame_count = managed_limit_address / OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
    return (managed_frame_count + OS_KERNEL_FRAME_ALLOCATOR_BYTE_ROUNDING) /
           OS_KERNEL_FRAME_ALLOCATOR_STATES_PER_BYTE;
}

uint64_t
CalculatePhysicalFrameBuddyStorageSizeBytes(const uint64_t managed_limit_address) noexcept {
    if (managed_limit_address == OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE ||
        (managed_limit_address & OS_KERNEL_FRAME_ALLOCATOR_PAGE_MASK) !=
            OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE) {
        return OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE;
    }
    const uint64_t managed_frame_count = managed_limit_address / OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
    const uint64_t maximum_order =
        Minimum(FloorLog2(managed_frame_count), OS_KERNEL_FRAME_ALLOCATOR_MAXIMUM_ORDER);
    uint64_t bitmap_family_size_bytes = OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE;
    for (uint64_t order = OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE; order <= maximum_order; ++order) {
        const uint64_t block_count = BlockCountRoundedUp(managed_frame_count, order);
        const uint64_t bitmap_size_bytes = BitmapSizeBytes(block_count);
        if (bitmap_family_size_bytes > UINT64_MAX - bitmap_size_bytes) {
            return OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE;
        }
        bitmap_family_size_bytes += bitmap_size_bytes;
    }
    if (bitmap_family_size_bytes > UINT64_MAX / OS_KERNEL_FRAME_ALLOCATOR_BITMAP_FAMILY_COUNT) {
        return OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE;
    }
    return bitmap_family_size_bytes * OS_KERNEL_FRAME_ALLOCATOR_BITMAP_FAMILY_COUNT;
}

PhysicalFrameAllocator::PhysicalFrameAllocator() noexcept
    : state_storage_{nullptr}, state_storage_size_bytes_{0ULL}, buddy_storage_{nullptr},
      buddy_storage_size_bytes_{0ULL}, buddy_bitmap_family_size_bytes_{0ULL},
      managed_frame_count_{0ULL}, free_frame_count_{0ULL}, allocated_frame_count_{0ULL},
      reserved_frame_count_{0ULL}, next_search_frame_index_{0ULL}, buddy_maximum_order_{0ULL},
      buddy_active_block_count_{0ULL}, buddy_successful_allocation_count_{0ULL},
      buddy_release_count_{0ULL}, buddy_split_count_{0ULL}, buddy_merge_count_{0ULL},
      buddy_order_bitmap_offsets_{}, buddy_order_bitmap_size_bytes_{},
      buddy_free_block_count_by_order_{} {}

PhysicalFrameAllocator::PhysicalFrameAllocator(uint8_t *state_storage,
                                               const uint64_t state_storage_size_bytes) noexcept
    : state_storage_{state_storage}, state_storage_size_bytes_{state_storage_size_bytes},
      buddy_storage_{nullptr}, buddy_storage_size_bytes_{0ULL},
      buddy_bitmap_family_size_bytes_{0ULL}, managed_frame_count_{0ULL}, free_frame_count_{0ULL},
      allocated_frame_count_{0ULL}, reserved_frame_count_{0ULL}, next_search_frame_index_{0ULL},
      buddy_maximum_order_{0ULL}, buddy_active_block_count_{0ULL},
      buddy_successful_allocation_count_{0ULL}, buddy_release_count_{0ULL},
      buddy_split_count_{0ULL}, buddy_merge_count_{0ULL}, buddy_order_bitmap_offsets_{},
      buddy_order_bitmap_size_bytes_{}, buddy_free_block_count_by_order_{} {}

PhysicalFrameAllocatorStatus
PhysicalFrameAllocator::ConfigureStateStorage(uint8_t *state_storage,
                                              const uint64_t state_storage_size_bytes) noexcept {
    if (this->IsInitialized()) {
        return PhysicalFrameAllocatorStatus::AlreadyInitialized;
    }
    if (state_storage == nullptr) {
        return PhysicalFrameAllocatorStatus::NullStateStorage;
    }
    if (state_storage_size_bytes == OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE) {
        return PhysicalFrameAllocatorStatus::InvalidStateStorageSize;
    }
    this->state_storage_ = state_storage;
    this->state_storage_size_bytes_ = state_storage_size_bytes;
    return PhysicalFrameAllocatorStatus::Succeeded;
}

PhysicalFrameAllocatorStatus
PhysicalFrameAllocator::ConfigureBuddyStorage(uint8_t *buddy_storage,
                                              const uint64_t buddy_storage_size_bytes) noexcept {
    if (this->IsBuddyInitialized()) {
        return PhysicalFrameAllocatorStatus::BuddyAlreadyInitialized;
    }
    if (buddy_storage == nullptr) {
        return PhysicalFrameAllocatorStatus::NullBuddyStorage;
    }
    if (buddy_storage_size_bytes == OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE) {
        return PhysicalFrameAllocatorStatus::InvalidBuddyStorageSize;
    }
    this->buddy_storage_ = buddy_storage;
    this->buddy_storage_size_bytes_ = buddy_storage_size_bytes;
    return PhysicalFrameAllocatorStatus::Succeeded;
}

PhysicalFrameAllocatorStatus
PhysicalFrameAllocator::Initialize(const PhysicalMemoryMapEntry *entries,
                                   const uint64_t entry_count,
                                   const uint64_t managed_limit_address) noexcept {
    if (this->IsInitialized()) {
        return PhysicalFrameAllocatorStatus::AlreadyInitialized;
    }
    if (this->state_storage_ == nullptr) {
        return PhysicalFrameAllocatorStatus::NullStateStorage;
    }
    if (managed_limit_address == OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE ||
        (managed_limit_address & OS_KERNEL_FRAME_ALLOCATOR_PAGE_MASK) !=
            OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE) {
        return PhysicalFrameAllocatorStatus::InvalidManagedLimit;
    }

    PhysicalMemorySummary memory_summary{};
    if (ValidateAndSummarizePhysicalMemoryMap(entries, entry_count, managed_limit_address,
                                              memory_summary) !=
        PhysicalMemoryMapValidationStatus::Succeeded) {
        return PhysicalFrameAllocatorStatus::InvalidMemoryMap;
    }

    const uint64_t managed_frame_count = managed_limit_address / OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
    const uint64_t required_state_storage_size_bytes =
        CalculatePhysicalFrameStateStorageSizeBytes(managed_limit_address);
    if (required_state_storage_size_bytes > this->state_storage_size_bytes_) {
        return PhysicalFrameAllocatorStatus::InvalidStateStorageSize;
    }

    for (uint64_t byte_index = 0ULL; byte_index < required_state_storage_size_bytes; ++byte_index) {
        this->state_storage_[byte_index] = 0U;
    }
    this->managed_frame_count_ = managed_frame_count;
    this->free_frame_count_ = 0ULL;
    this->allocated_frame_count_ = 0ULL;
    this->reserved_frame_count_ = 0ULL;
    this->next_search_frame_index_ = 0ULL;

    for (uint64_t entry_index = 0ULL; entry_index < entry_count; ++entry_index) {
        const PhysicalMemoryMapEntry &entry = entries[entry_index];
        if (entry.type != OS_KERNEL_MEMORY_MAP_USABLE_REGION_TYPE) {
            continue;
        }
        if (entry.base_address >= managed_limit_address) {
            continue;
        }
        const uint64_t entry_end_address = entry.base_address + entry.length_bytes;
        const uint64_t first_frame_address = AlignUpToPage(entry.base_address);
        const uint64_t clamped_end_address =
            entry_end_address < managed_limit_address ? entry_end_address : managed_limit_address;
        const uint64_t end_frame_address = AlignDownToPage(clamped_end_address);
        if (first_frame_address >= end_frame_address) {
            continue;
        }
        const uint64_t first_frame_index = first_frame_address / OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
        const uint64_t end_frame_index = end_frame_address / OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
        this->SetFreeFrameStateRange(first_frame_index, end_frame_index);
        this->free_frame_count_ += end_frame_index - first_frame_index;
    }

    if (this->free_frame_count_ == OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE) {
        this->managed_frame_count_ = 0ULL;
        this->next_search_frame_index_ = 0ULL;
        return PhysicalFrameAllocatorStatus::NoUsableFrames;
    }
    return PhysicalFrameAllocatorStatus::Succeeded;
}

PhysicalFrameAllocatorStatus PhysicalFrameAllocator::InitializeBuddy() noexcept {
    if (!this->IsInitialized()) {
        return PhysicalFrameAllocatorStatus::NotInitialized;
    }
    if (this->IsBuddyInitialized()) {
        return PhysicalFrameAllocatorStatus::BuddyAlreadyInitialized;
    }
    if (this->buddy_storage_ == nullptr) {
        return PhysicalFrameAllocatorStatus::NullBuddyStorage;
    }
    const uint64_t required_storage_size_bytes =
        CalculatePhysicalFrameBuddyStorageSizeBytes(this->ManagedLimitAddress());
    if (required_storage_size_bytes == OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE ||
        required_storage_size_bytes > this->buddy_storage_size_bytes_) {
        return PhysicalFrameAllocatorStatus::InvalidBuddyStorageSize;
    }
    if (this->allocated_frame_count_ != OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE) {
        return PhysicalFrameAllocatorStatus::ExistingAllocationsPreventBuddyInitialization;
    }

    for (uint64_t byte_index = 0ULL; byte_index < required_storage_size_bytes; ++byte_index) {
        this->buddy_storage_[byte_index] = OS_KERNEL_FRAME_ALLOCATOR_EMPTY_BITMAP_BYTE;
    }
    this->buddy_maximum_order_ = this->CalculateMaximumOrder();
    uint64_t bitmap_offset = OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE;
    for (uint64_t order = OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE;
         order < OS_KERNEL_FRAME_ALLOCATOR_ORDER_COUNT; ++order) {
        this->buddy_order_bitmap_offsets_[order] = bitmap_offset;
        this->buddy_order_bitmap_size_bytes_[order] = OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE;
        this->buddy_free_block_count_by_order_[order] = OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE;
        if (order <= this->buddy_maximum_order_) {
            const uint64_t block_count = BlockCountRoundedUp(this->managed_frame_count_, order);
            const uint64_t bitmap_size_bytes = BitmapSizeBytes(block_count);
            this->buddy_order_bitmap_size_bytes_[order] = bitmap_size_bytes;
            bitmap_offset += bitmap_size_bytes;
        }
    }
    this->buddy_bitmap_family_size_bytes_ = bitmap_offset;
    this->buddy_active_block_count_ = OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE;
    this->buddy_successful_allocation_count_ = OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE;
    this->buddy_release_count_ = OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE;
    this->buddy_split_count_ = OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE;
    this->buddy_merge_count_ = OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE;

    // 每段空闲 PFN 区间都按地址对齐约束贪心切成最大块，避免初始化后留下
    // 本可继续合并的同阶伙伴。
    uint64_t frame_index = OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE;
    while (frame_index < this->managed_frame_count_) {
        if (this->GetFrameState(frame_index) != FrameState::Free) {
            ++frame_index;
            continue;
        }
        uint64_t free_run_end_frame_index = frame_index + 1ULL;
        while (free_run_end_frame_index < this->managed_frame_count_ &&
               this->GetFrameState(free_run_end_frame_index) == FrameState::Free) {
            ++free_run_end_frame_index;
        }
        while (frame_index < free_run_end_frame_index) {
            const uint64_t remaining_frame_count = free_run_end_frame_index - frame_index;
            uint64_t order = Minimum(FloorLog2(remaining_frame_count), this->buddy_maximum_order_);
            while (order > OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE &&
                   (frame_index & (this->BlockFrameCount(order) - 1ULL)) !=
                       OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE) {
                --order;
            }
            const uint64_t block_index = frame_index >> order;
            this->SetBlockBit(BlockBitmap::Free, order, block_index, true);
            ++this->buddy_free_block_count_by_order_[order];
            frame_index += this->BlockFrameCount(order);
        }
    }
    return PhysicalFrameAllocatorStatus::Succeeded;
}

PhysicalFrameAllocatorStatus
PhysicalFrameAllocator::ReserveRange(const uint64_t begin_address,
                                     const uint64_t length_bytes) noexcept {
    if (!this->IsInitialized()) {
        return PhysicalFrameAllocatorStatus::NotInitialized;
    }
    if (this->IsBuddyInitialized()) {
        return PhysicalFrameAllocatorStatus::ReservationAfterBuddyInitialization;
    }
    if (length_bytes == OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE ||
        begin_address > UINT64_MAX - length_bytes) {
        return PhysicalFrameAllocatorStatus::InvalidReservation;
    }
    const uint64_t end_address = begin_address + length_bytes;
    const uint64_t managed_limit = this->ManagedLimitAddress();
    if (begin_address >= managed_limit || end_address > managed_limit ||
        end_address > UINT64_MAX - OS_KERNEL_FRAME_ALLOCATOR_PAGE_MASK) {
        return PhysicalFrameAllocatorStatus::InvalidReservation;
    }
    const uint64_t first_frame_address = AlignDownToPage(begin_address);
    const uint64_t end_frame_address = AlignUpToPage(end_address);
    for (uint64_t frame_address = first_frame_address; frame_address < end_frame_address;
         frame_address += OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) {
        const uint64_t frame_index = frame_address / OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
        if (this->GetFrameState(frame_index) == FrameState::Allocated) {
            return PhysicalFrameAllocatorStatus::InvalidReservation;
        }
    }
    for (uint64_t frame_address = first_frame_address; frame_address < end_frame_address;
         frame_address += OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) {
        const uint64_t frame_index = frame_address / OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
        const FrameState state = this->GetFrameState(frame_index);
        if (state == FrameState::Free) {
            this->SetFrameState(frame_index, FrameState::Reserved);
            --this->free_frame_count_;
            ++this->reserved_frame_count_;
        }
    }
    return PhysicalFrameAllocatorStatus::Succeeded;
}

PhysicalFrameAllocatorStatus PhysicalFrameAllocator::Allocate(PhysicalFrame &frame) noexcept {
    return this->AllocateInRange(OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE, this->ManagedLimitAddress(),
                                 frame);
}

PhysicalFrameAllocatorStatus
PhysicalFrameAllocator::AllocateInRange(const uint64_t minimum_address,
                                        const uint64_t maximum_address_exclusive,
                                        PhysicalFrame &frame) noexcept {
    if (!this->IsBuddyInitialized()) {
        return this->AllocateLegacyInRange(minimum_address, maximum_address_exclusive, frame);
    }
    PhysicalFrameBlock block{};
    const PhysicalFrameAllocatorStatus status = this->AllocateBlockInRange(
        OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE, minimum_address, maximum_address_exclusive, block);
    if (status == PhysicalFrameAllocatorStatus::Succeeded) {
        frame.physical_address = block.physical_address;
    }
    return status;
}

PhysicalFrameAllocatorStatus
PhysicalFrameAllocator::AllocateLegacyInRange(const uint64_t minimum_address,
                                              const uint64_t maximum_address_exclusive,
                                              PhysicalFrame &frame) noexcept {
    if (!this->IsInitialized()) {
        return PhysicalFrameAllocatorStatus::NotInitialized;
    }
    if ((minimum_address & OS_KERNEL_FRAME_ALLOCATOR_PAGE_MASK) !=
            OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE ||
        (maximum_address_exclusive & OS_KERNEL_FRAME_ALLOCATOR_PAGE_MASK) !=
            OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE ||
        minimum_address >= maximum_address_exclusive ||
        minimum_address >= this->ManagedLimitAddress()) {
        return PhysicalFrameAllocatorStatus::InvalidAllocationRange;
    }
    if (this->free_frame_count_ == OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE) {
        return PhysicalFrameAllocatorStatus::OutOfMemory;
    }

    const uint64_t clamped_maximum_address_exclusive =
        maximum_address_exclusive < this->ManagedLimitAddress() ? maximum_address_exclusive
                                                                : this->ManagedLimitAddress();
    const uint64_t first_frame_index = minimum_address / OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
    const uint64_t end_frame_index =
        clamped_maximum_address_exclusive / OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
    if (first_frame_index >= end_frame_index) {
        return PhysicalFrameAllocatorStatus::InvalidAllocationRange;
    }
    uint64_t search_frame_index = first_frame_index;
    if (minimum_address == OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE &&
        this->next_search_frame_index_ >= first_frame_index &&
        this->next_search_frame_index_ < end_frame_index) {
        search_frame_index = this->next_search_frame_index_;
    }
    const uint64_t range_frame_count = end_frame_index - first_frame_index;
    for (uint64_t offset = OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE; offset < range_frame_count;
         ++offset) {
        const uint64_t frame_index =
            first_frame_index +
            ((search_frame_index - first_frame_index + offset) % range_frame_count);
        if (this->GetFrameState(frame_index) != FrameState::Free) {
            continue;
        }
        this->SetFrameState(frame_index, FrameState::Allocated);
        --this->free_frame_count_;
        ++this->allocated_frame_count_;
        this->next_search_frame_index_ = (frame_index + 1ULL) % this->managed_frame_count_;
        frame.physical_address = frame_index * OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
        return PhysicalFrameAllocatorStatus::Succeeded;
    }
    return PhysicalFrameAllocatorStatus::OutOfMemory;
}

PhysicalFrameAllocatorStatus
PhysicalFrameAllocator::AllocateBlock(const uint64_t order, PhysicalFrameBlock &block) noexcept {
    return this->AllocateBlockInRange(order, OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE,
                                      this->ManagedLimitAddress(), block);
}

PhysicalFrameAllocatorStatus
PhysicalFrameAllocator::AllocateBlockInRange(const uint64_t order, const uint64_t minimum_address,
                                             const uint64_t maximum_address_exclusive,
                                             PhysicalFrameBlock &block) noexcept {
    if (!this->IsBuddyInitialized()) {
        return PhysicalFrameAllocatorStatus::BuddyNotInitialized;
    }
    if (order > this->buddy_maximum_order_ || order > OS_KERNEL_FRAME_ALLOCATOR_MAXIMUM_ORDER) {
        return PhysicalFrameAllocatorStatus::InvalidBlockOrder;
    }
    if ((minimum_address & OS_KERNEL_FRAME_ALLOCATOR_PAGE_MASK) !=
            OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE ||
        (maximum_address_exclusive & OS_KERNEL_FRAME_ALLOCATOR_PAGE_MASK) !=
            OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE ||
        minimum_address >= maximum_address_exclusive ||
        minimum_address >= this->ManagedLimitAddress()) {
        return PhysicalFrameAllocatorStatus::InvalidAllocationRange;
    }
    const uint64_t clamped_maximum_address_exclusive =
        Minimum(maximum_address_exclusive, this->ManagedLimitAddress());
    const uint64_t first_frame_index = minimum_address / OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
    const uint64_t end_frame_index =
        clamped_maximum_address_exclusive / OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
    const uint64_t requested_frame_count = this->BlockFrameCount(order);
    if (first_frame_index >= end_frame_index ||
        requested_frame_count > end_frame_index - first_frame_index ||
        requested_frame_count > this->free_frame_count_) {
        return PhysicalFrameAllocatorStatus::OutOfMemory;
    }

    uint64_t source_order = order;
    uint64_t source_block_index = OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE;
    uint64_t target_frame_index = OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE;
    bool source_found = false;
    while (source_order <= this->buddy_maximum_order_) {
        if (this->buddy_free_block_count_by_order_[source_order] !=
                OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE &&
            this->FindSourceBlock(source_order, order, first_frame_index, end_frame_index,
                                  source_block_index, target_frame_index)) {
            source_found = true;
            break;
        }
        ++source_order;
    }
    if (!source_found) {
        return PhysicalFrameAllocatorStatus::OutOfMemory;
    }
    if (!this->IsBlockState(target_frame_index, order, FrameState::Free)) {
        return PhysicalFrameAllocatorStatus::CorruptedState;
    }

    // 提交前独立重放分裂方向，保证后续阶段不会出现“已经改位图再失败”。
    uint64_t verified_order = source_order;
    uint64_t verified_frame_index = source_block_index << source_order;
    while (verified_order > order) {
        --verified_order;
        const uint64_t right_frame_index =
            verified_frame_index + this->BlockFrameCount(verified_order);
        if (target_frame_index >= right_frame_index) {
            verified_frame_index = right_frame_index;
        }
    }
    if (verified_frame_index != target_frame_index) {
        return PhysicalFrameAllocatorStatus::CorruptedState;
    }

    // 每次只把未选择的半块放回空闲集合；被选择的半块继续向目标阶分裂。
    this->SetBlockBit(BlockBitmap::Free, source_order, source_block_index, false);
    --this->buddy_free_block_count_by_order_[source_order];
    uint64_t current_order = source_order;
    uint64_t current_frame_index = source_block_index << source_order;
    while (current_order > order) {
        --current_order;
        const uint64_t half_frame_count = this->BlockFrameCount(current_order);
        const uint64_t right_frame_index = current_frame_index + half_frame_count;
        uint64_t free_frame_index = current_frame_index;
        if (target_frame_index < right_frame_index) {
            free_frame_index = right_frame_index;
        } else {
            current_frame_index = right_frame_index;
        }
        this->SetBlockBit(BlockBitmap::Free, current_order, free_frame_index >> current_order,
                          true);
        ++this->buddy_free_block_count_by_order_[current_order];
        ++this->buddy_split_count_;
    }
    this->SetBlockBit(BlockBitmap::Allocated, order, target_frame_index >> order, true);
    this->SetBlockState(target_frame_index, order, FrameState::Allocated);
    this->free_frame_count_ -= requested_frame_count;
    this->allocated_frame_count_ += requested_frame_count;
    ++this->buddy_active_block_count_;
    ++this->buddy_successful_allocation_count_;
    block = PhysicalFrameBlock{
        .physical_address = target_frame_index * OS_KERNEL_MEMORY_PAGE_SIZE_BYTES,
        .order = order,
    };
    return PhysicalFrameAllocatorStatus::Succeeded;
}

PhysicalFrameAllocatorStatus PhysicalFrameAllocator::Release(const PhysicalFrame frame) noexcept {
    if (!this->IsBuddyInitialized()) {
        return this->ReleaseLegacy(frame);
    }
    return this->ReleaseBlock(PhysicalFrameBlock{
        .physical_address = frame.physical_address,
        .order = OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE,
    });
}

PhysicalFrameAllocatorStatus
PhysicalFrameAllocator::ReleaseLegacy(const PhysicalFrame frame) noexcept {
    if (!this->IsInitialized()) {
        return PhysicalFrameAllocatorStatus::NotInitialized;
    }
    if ((frame.physical_address & OS_KERNEL_FRAME_ALLOCATOR_PAGE_MASK) !=
            OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE ||
        frame.physical_address >= this->ManagedLimitAddress()) {
        return PhysicalFrameAllocatorStatus::InvalidFrameAddress;
    }
    const uint64_t frame_index = frame.physical_address / OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
    if (this->GetFrameState(frame_index) != FrameState::Allocated) {
        return PhysicalFrameAllocatorStatus::FrameNotAllocated;
    }
    this->SetFrameState(frame_index, FrameState::Free);
    ++this->free_frame_count_;
    --this->allocated_frame_count_;
    if (frame_index < this->next_search_frame_index_) {
        this->next_search_frame_index_ = frame_index;
    }
    return PhysicalFrameAllocatorStatus::Succeeded;
}

PhysicalFrameAllocatorStatus
PhysicalFrameAllocator::ReleaseBlock(const PhysicalFrameBlock block) noexcept {
    if (!this->IsBuddyInitialized()) {
        return PhysicalFrameAllocatorStatus::BuddyNotInitialized;
    }
    if (block.order > this->buddy_maximum_order_ ||
        block.order > OS_KERNEL_FRAME_ALLOCATOR_MAXIMUM_ORDER) {
        return PhysicalFrameAllocatorStatus::InvalidBlockOrder;
    }
    if ((block.physical_address & OS_KERNEL_FRAME_ALLOCATOR_PAGE_MASK) !=
            OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE ||
        block.physical_address >= this->ManagedLimitAddress()) {
        return PhysicalFrameAllocatorStatus::InvalidFrameAddress;
    }
    const uint64_t frame_index = block.physical_address / OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
    const uint64_t block_frame_count = this->BlockFrameCount(block.order);
    if ((frame_index & (block_frame_count - 1ULL)) != OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE) {
        return PhysicalFrameAllocatorStatus::InvalidBlockAlignment;
    }
    if (block_frame_count > this->managed_frame_count_ - frame_index) {
        return PhysicalFrameAllocatorStatus::InvalidFrameAddress;
    }
    const uint64_t block_index = frame_index >> block.order;
    if (!this->GetBlockBit(BlockBitmap::Allocated, block.order, block_index)) {
        uint64_t allocation_order = OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE;
        if (this->FindContainingAllocation(frame_index, allocation_order)) {
            static_cast<void>(allocation_order);
            return PhysicalFrameAllocatorStatus::AllocationOrderMismatch;
        }
        return PhysicalFrameAllocatorStatus::FrameNotAllocated;
    }
    if (!this->IsBlockState(frame_index, block.order, FrameState::Allocated)) {
        return PhysicalFrameAllocatorStatus::CorruptedState;
    }

    this->SetBlockBit(BlockBitmap::Allocated, block.order, block_index, false);
    this->SetBlockState(frame_index, block.order, FrameState::Free);
    this->free_frame_count_ += block_frame_count;
    this->allocated_frame_count_ -= block_frame_count;
    --this->buddy_active_block_count_;
    ++this->buddy_release_count_;

    // 全局 PFN 对齐决定唯一伙伴；只有同阶伙伴仍空闲时才允许继续向上合并。
    uint64_t current_order = block.order;
    uint64_t current_block_index = block_index;
    while (current_order < this->buddy_maximum_order_) {
        const uint64_t buddy_block_index = current_block_index ^ 1ULL;
        if (buddy_block_index >= this->CompleteBlockCount(current_order) ||
            !this->GetBlockBit(BlockBitmap::Free, current_order, buddy_block_index)) {
            break;
        }
        this->SetBlockBit(BlockBitmap::Free, current_order, buddy_block_index, false);
        --this->buddy_free_block_count_by_order_[current_order];
        current_block_index = Minimum(current_block_index, buddy_block_index) >> 1ULL;
        ++current_order;
        ++this->buddy_merge_count_;
    }
    this->SetBlockBit(BlockBitmap::Free, current_order, current_block_index, true);
    ++this->buddy_free_block_count_by_order_[current_order];
    return PhysicalFrameAllocatorStatus::Succeeded;
}

bool PhysicalFrameAllocator::OwnsAllocation(const PhysicalFrame frame) const noexcept {
    if (!this->IsInitialized() ||
        (frame.physical_address & OS_KERNEL_FRAME_ALLOCATOR_PAGE_MASK) !=
            OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE ||
        frame.physical_address >= this->ManagedLimitAddress()) {
        return false;
    }
    const uint64_t frame_index = frame.physical_address / OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
    if (!this->IsBuddyInitialized()) {
        return this->GetFrameState(frame_index) == FrameState::Allocated;
    }
    return this->GetBlockBit(BlockBitmap::Allocated, OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE,
                             frame_index) &&
           this->IsBlockState(frame_index, OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE,
                              FrameState::Allocated);
}

PhysicalFrameAllocatorStatistics PhysicalFrameAllocator::Statistics() const noexcept {
    return PhysicalFrameAllocatorStatistics{
        .managed_frame_count = this->managed_frame_count_,
        .free_frame_count = this->free_frame_count_,
        .allocated_frame_count = this->allocated_frame_count_,
        .reserved_frame_count = this->reserved_frame_count_,
    };
}

PhysicalFrameBuddyStatistics PhysicalFrameAllocator::BuddyStatistics() const noexcept {
    return PhysicalFrameBuddyStatistics{
        .metadata_storage_size_bytes =
            this->buddy_bitmap_family_size_bytes_ * OS_KERNEL_FRAME_ALLOCATOR_BITMAP_FAMILY_COUNT,
        .maximum_order = this->buddy_maximum_order_,
        .free_block_count = this->CalculateTotalFreeBlockCount(),
        .active_block_count = this->buddy_active_block_count_,
        .successful_allocation_count = this->buddy_successful_allocation_count_,
        .release_count = this->buddy_release_count_,
        .split_count = this->buddy_split_count_,
        .merge_count = this->buddy_merge_count_,
        .largest_free_order = this->CalculateLargestFreeOrder(),
    };
}

PhysicalFrameAllocatorStatus PhysicalFrameAllocator::ValidateBuddy() const noexcept {
    if (!this->IsInitialized()) {
        return PhysicalFrameAllocatorStatus::NotInitialized;
    }
    if (!this->IsBuddyInitialized()) {
        return PhysicalFrameAllocatorStatus::BuddyNotInitialized;
    }
    const uint64_t required_storage_size_bytes =
        CalculatePhysicalFrameBuddyStorageSizeBytes(this->ManagedLimitAddress());
    if (required_storage_size_bytes !=
        this->buddy_bitmap_family_size_bytes_ * OS_KERNEL_FRAME_ALLOCATOR_BITMAP_FAMILY_COUNT) {
        return PhysicalFrameAllocatorStatus::CorruptedState;
    }

    // 校验不申请临时内存：逐阶核对位图、父子不重叠、页状态与加权统计。
    uint64_t observed_free_frame_count = OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE;
    uint64_t observed_allocated_frame_count = OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE;
    uint64_t observed_free_block_count = OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE;
    uint64_t observed_active_block_count = OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE;
    for (uint64_t order = OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE;
         order <= this->buddy_maximum_order_; ++order) {
        const uint64_t bitmap_block_capacity = this->buddy_order_bitmap_size_bytes_[order] *
                                               OS_KERNEL_FRAME_ALLOCATOR_BITS_PER_BITMAP_BYTE;
        const uint64_t complete_block_count = this->CompleteBlockCount(order);
        uint64_t observed_order_free_block_count = OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE;
        for (uint64_t block_index = OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE;
             block_index < bitmap_block_capacity; ++block_index) {
            const bool is_free = this->GetBlockBit(BlockBitmap::Free, order, block_index);
            const bool is_allocated = this->GetBlockBit(BlockBitmap::Allocated, order, block_index);
            if (!is_free && !is_allocated) {
                continue;
            }
            if (block_index >= complete_block_count || (is_free && is_allocated)) {
                return PhysicalFrameAllocatorStatus::CorruptedState;
            }
            const uint64_t block_frame_index = block_index << order;
            const uint64_t block_frame_count = this->BlockFrameCount(order);
            if (is_free) {
                const uint64_t buddy_block_index = block_index ^ 1ULL;
                if ((block_index & 1ULL) == OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE &&
                    buddy_block_index < complete_block_count &&
                    this->GetBlockBit(BlockBitmap::Free, order, buddy_block_index)) {
                    return PhysicalFrameAllocatorStatus::CorruptedState;
                }
                if (this->HasContainingBlock(BlockBitmap::Free, order, block_index) ||
                    this->HasContainingBlock(BlockBitmap::Allocated, order, block_index) ||
                    !this->IsBlockState(block_frame_index, order, FrameState::Free)) {
                    return PhysicalFrameAllocatorStatus::CorruptedState;
                }
                observed_free_frame_count += block_frame_count;
                ++observed_free_block_count;
                ++observed_order_free_block_count;
            }
            if (is_allocated) {
                if (this->HasContainingBlock(BlockBitmap::Free, order, block_index) ||
                    this->HasContainingBlock(BlockBitmap::Allocated, order, block_index) ||
                    !this->IsBlockState(block_frame_index, order, FrameState::Allocated)) {
                    return PhysicalFrameAllocatorStatus::CorruptedState;
                }
                observed_allocated_frame_count += block_frame_count;
                ++observed_active_block_count;
            }
        }
        if (observed_order_free_block_count != this->buddy_free_block_count_by_order_[order]) {
            return PhysicalFrameAllocatorStatus::CorruptedState;
        }
    }
    if (observed_free_frame_count != this->free_frame_count_ ||
        observed_allocated_frame_count != this->allocated_frame_count_ ||
        observed_free_block_count != this->CalculateTotalFreeBlockCount() ||
        observed_active_block_count != this->buddy_active_block_count_ ||
        this->buddy_successful_allocation_count_ < this->buddy_release_count_ ||
        this->buddy_successful_allocation_count_ - this->buddy_release_count_ !=
            this->buddy_active_block_count_) {
        return PhysicalFrameAllocatorStatus::CorruptedState;
    }
    return PhysicalFrameAllocatorStatus::Succeeded;
}

uint64_t PhysicalFrameAllocator::ManagedLimitAddress() const noexcept {
    return this->managed_frame_count_ * OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
}

PhysicalFrameAllocator::FrameState
PhysicalFrameAllocator::GetFrameState(const uint64_t frame_index) const noexcept {
    const uint64_t byte_index = frame_index / OS_KERNEL_FRAME_ALLOCATOR_STATES_PER_BYTE;
    const uint64_t state_index = frame_index % OS_KERNEL_FRAME_ALLOCATOR_STATES_PER_BYTE;
    const uint64_t shift = state_index * OS_KERNEL_FRAME_ALLOCATOR_BITS_PER_STATE;
    return static_cast<FrameState>(static_cast<uint8_t>(
        (this->state_storage_[byte_index] >> shift) & OS_KERNEL_FRAME_ALLOCATOR_STATE_MASK));
}

void PhysicalFrameAllocator::SetFrameState(const uint64_t frame_index,
                                           const FrameState state) noexcept {
    const uint64_t byte_index = frame_index / OS_KERNEL_FRAME_ALLOCATOR_STATES_PER_BYTE;
    const uint64_t state_index = frame_index % OS_KERNEL_FRAME_ALLOCATOR_STATES_PER_BYTE;
    const uint64_t shift = state_index * OS_KERNEL_FRAME_ALLOCATOR_BITS_PER_STATE;
    const uint8_t shifted_mask =
        static_cast<uint8_t>(OS_KERNEL_FRAME_ALLOCATOR_STATE_MASK << shift);
    const uint8_t shifted_state = static_cast<uint8_t>(static_cast<uint8_t>(state) << shift);
    this->state_storage_[byte_index] =
        static_cast<uint8_t>((this->state_storage_[byte_index] & ~shifted_mask) | shifted_state);
}

void PhysicalFrameAllocator::SetFreeFrameStateRange(const uint64_t first_frame_index,
                                                    const uint64_t end_frame_index) noexcept {
    uint64_t frame_index = first_frame_index;
    while (frame_index < end_frame_index &&
           frame_index % OS_KERNEL_FRAME_ALLOCATOR_STATES_PER_BYTE !=
               OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE) {
        this->SetFrameState(frame_index, FrameState::Free);
        ++frame_index;
    }
    const uint64_t complete_byte_end_frame_index =
        end_frame_index - (end_frame_index % OS_KERNEL_FRAME_ALLOCATOR_STATES_PER_BYTE);
    while (frame_index < complete_byte_end_frame_index) {
        this->state_storage_[frame_index / OS_KERNEL_FRAME_ALLOCATOR_STATES_PER_BYTE] =
            OS_KERNEL_FRAME_ALLOCATOR_FREE_STATE_BYTE;
        frame_index += OS_KERNEL_FRAME_ALLOCATOR_STATES_PER_BYTE;
    }
    while (frame_index < end_frame_index) {
        this->SetFrameState(frame_index, FrameState::Free);
        ++frame_index;
    }
}

uint64_t PhysicalFrameAllocator::BlockFrameCount(const uint64_t order) const noexcept {
    return 1ULL << order;
}

uint64_t PhysicalFrameAllocator::CompleteBlockCount(const uint64_t order) const noexcept {
    return this->managed_frame_count_ / this->BlockFrameCount(order);
}

bool PhysicalFrameAllocator::GetBlockBit(const BlockBitmap bitmap, const uint64_t order,
                                         const uint64_t block_index) const noexcept {
    const uint64_t family_offset = bitmap == BlockBitmap::Free
                                       ? OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE
                                       : this->buddy_bitmap_family_size_bytes_;
    const uint64_t byte_index = family_offset + this->buddy_order_bitmap_offsets_[order] +
                                block_index / OS_KERNEL_FRAME_ALLOCATOR_BITS_PER_BITMAP_BYTE;
    const uint64_t bit_index = block_index & OS_KERNEL_FRAME_ALLOCATOR_BITMAP_BIT_MASK;
    const uint8_t bit = static_cast<uint8_t>(1U << bit_index);
    return (this->buddy_storage_[byte_index] & bit) != 0U;
}

void PhysicalFrameAllocator::SetBlockBit(const BlockBitmap bitmap, const uint64_t order,
                                         const uint64_t block_index, const bool value) noexcept {
    const uint64_t family_offset = bitmap == BlockBitmap::Free
                                       ? OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE
                                       : this->buddy_bitmap_family_size_bytes_;
    const uint64_t byte_index = family_offset + this->buddy_order_bitmap_offsets_[order] +
                                block_index / OS_KERNEL_FRAME_ALLOCATOR_BITS_PER_BITMAP_BYTE;
    const uint64_t bit_index = block_index & OS_KERNEL_FRAME_ALLOCATOR_BITMAP_BIT_MASK;
    const uint8_t bit = static_cast<uint8_t>(1U << bit_index);
    if (value) {
        this->buddy_storage_[byte_index] =
            static_cast<uint8_t>(this->buddy_storage_[byte_index] | bit);
    } else {
        this->buddy_storage_[byte_index] =
            static_cast<uint8_t>(this->buddy_storage_[byte_index] & ~bit);
    }
}

bool PhysicalFrameAllocator::FindSourceBlock(const uint64_t source_order,
                                             const uint64_t requested_order,
                                             const uint64_t first_frame_index,
                                             const uint64_t end_frame_index,
                                             uint64_t &source_block_index,
                                             uint64_t &target_frame_index) const noexcept {
    const uint64_t source_frame_count = this->BlockFrameCount(source_order);
    const uint64_t requested_frame_count = this->BlockFrameCount(requested_order);
    const uint64_t first_source_block_index = first_frame_index / source_frame_count;
    const uint64_t final_source_block_index = (end_frame_index - 1ULL) / source_frame_count;
    const uint64_t complete_source_block_count = this->CompleteBlockCount(source_order);
    for (uint64_t candidate_source_block_index = first_source_block_index;
         candidate_source_block_index <= final_source_block_index &&
         candidate_source_block_index < complete_source_block_count;
         ++candidate_source_block_index) {
        if (!this->GetBlockBit(BlockBitmap::Free, source_order, candidate_source_block_index)) {
            continue;
        }
        const uint64_t source_frame_index = candidate_source_block_index << source_order;
        const uint64_t source_end_frame_index = source_frame_index + source_frame_count;
        const uint64_t overlap_begin_frame_index =
            first_frame_index > source_frame_index ? first_frame_index : source_frame_index;
        const uint64_t overlap_end_frame_index = Minimum(end_frame_index, source_end_frame_index);
        const uint64_t requested_frame_mask = requested_frame_count - 1ULL;
        const uint64_t aligned_target_frame_index =
            (overlap_begin_frame_index + requested_frame_mask) & ~requested_frame_mask;
        if (aligned_target_frame_index < overlap_end_frame_index &&
            requested_frame_count <= overlap_end_frame_index - aligned_target_frame_index) {
            source_block_index = candidate_source_block_index;
            target_frame_index = aligned_target_frame_index;
            return true;
        }
    }
    return false;
}

bool PhysicalFrameAllocator::FindContainingAllocation(const uint64_t frame_index,
                                                      uint64_t &allocation_order) const noexcept {
    for (uint64_t order = OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE;
         order <= this->buddy_maximum_order_; ++order) {
        const uint64_t block_index = frame_index >> order;
        if (block_index < this->CompleteBlockCount(order) &&
            this->GetBlockBit(BlockBitmap::Allocated, order, block_index)) {
            allocation_order = order;
            return true;
        }
    }
    return false;
}

bool PhysicalFrameAllocator::HasContainingBlock(const BlockBitmap bitmap, const uint64_t order,
                                                const uint64_t block_index) const noexcept {
    const uint64_t frame_index = block_index << order;
    uint64_t containing_order = order + 1ULL;
    while (containing_order <= this->buddy_maximum_order_) {
        const uint64_t containing_block_index = frame_index >> containing_order;
        if (containing_block_index < this->CompleteBlockCount(containing_order) &&
            this->GetBlockBit(bitmap, containing_order, containing_block_index)) {
            return true;
        }
        ++containing_order;
    }
    return false;
}

bool PhysicalFrameAllocator::IsBlockState(const uint64_t frame_index, const uint64_t order,
                                          const FrameState expected_state) const noexcept {
    const uint64_t end_frame_index = frame_index + this->BlockFrameCount(order);
    for (uint64_t current_frame_index = frame_index; current_frame_index < end_frame_index;
         ++current_frame_index) {
        if (this->GetFrameState(current_frame_index) != expected_state) {
            return false;
        }
    }
    return true;
}

void PhysicalFrameAllocator::SetBlockState(const uint64_t frame_index, const uint64_t order,
                                           const FrameState state) noexcept {
    const uint64_t end_frame_index = frame_index + this->BlockFrameCount(order);
    for (uint64_t current_frame_index = frame_index; current_frame_index < end_frame_index;
         ++current_frame_index) {
        this->SetFrameState(current_frame_index, state);
    }
}

uint64_t PhysicalFrameAllocator::CalculateMaximumOrder() const noexcept {
    return Minimum(FloorLog2(this->managed_frame_count_), OS_KERNEL_FRAME_ALLOCATOR_MAXIMUM_ORDER);
}

uint64_t PhysicalFrameAllocator::CalculateLargestFreeOrder() const noexcept {
    uint64_t order = this->buddy_maximum_order_;
    while (order > OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE &&
           this->buddy_free_block_count_by_order_[order] == OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE) {
        --order;
    }
    return order;
}

uint64_t PhysicalFrameAllocator::CalculateTotalFreeBlockCount() const noexcept {
    uint64_t free_block_count = OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE;
    for (uint64_t order = OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE;
         order <= this->buddy_maximum_order_; ++order) {
        free_block_count += this->buddy_free_block_count_by_order_[order];
    }
    return free_block_count;
}

bool PhysicalFrameAllocator::IsInitialized() const noexcept {
    return this->managed_frame_count_ != OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE;
}

bool PhysicalFrameAllocator::IsBuddyInitialized() const noexcept {
    return this->buddy_bitmap_family_size_bytes_ != OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE;
}

}

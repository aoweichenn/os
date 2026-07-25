#include "os/kernel/kernel_heap.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_HEAP_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_HEAP_POWER_OF_TWO_DECREMENT = 1ULL;
constexpr uint64_t OS_KERNEL_HEAP_BLOCK_ALIGNMENT_BYTES = 16ULL;
constexpr uint64_t OS_KERNEL_HEAP_BLOCK_HEADER_SIZE_BYTES = 48ULL;
constexpr uint64_t OS_KERNEL_HEAP_MINIMUM_BLOCK_SIZE_BYTES = 64ULL;
constexpr uint64_t OS_KERNEL_HEAP_FREE_BLOCK_SIGNATURE = 0x46524545424C4F43ULL;
constexpr uint64_t OS_KERNEL_HEAP_ALLOCATED_BLOCK_SIGNATURE = 0x414C4C4F43424C4BULL;
constexpr uint64_t OS_KERNEL_HEAP_RELEASED_BLOCK_SIGNATURE = 0x52454C4541534544ULL;

[[nodiscard]] bool IsPowerOfTwo(const uint64_t value) noexcept {
    return value != OS_KERNEL_HEAP_EMPTY_VALUE &&
           (value & (value - OS_KERNEL_HEAP_POWER_OF_TWO_DECREMENT)) == OS_KERNEL_HEAP_EMPTY_VALUE;
}

[[nodiscard]] uint64_t Maximum(const uint64_t left, const uint64_t right) noexcept {
    return left > right ? left : right;
}

[[nodiscard]] bool TryAlignUp(const uint64_t value, const uint64_t alignment_bytes,
                              uint64_t &aligned_value) noexcept {
    const uint64_t alignment_mask = alignment_bytes - OS_KERNEL_HEAP_POWER_OF_TWO_DECREMENT;
    if (value > UINT64_MAX - alignment_mask) {
        return false;
    }
    aligned_value = (value + alignment_mask) & ~alignment_mask;
    return true;
}
}

// 每个物理块都携带自身和前块长度，因此释放时无需遍历即可定位两个相邻块。
struct alignas(OS_KERNEL_HEAP_BLOCK_ALIGNMENT_BYTES) KernelHeap::BlockHeader final {
    uint64_t size_bytes;
    uint64_t previous_block_size_bytes;
    uint64_t requested_size_bytes;
    uint64_t state_signature;
    BlockHeader *previous_free_block;
    BlockHeader *next_free_block;
};

KernelHeap::KernelHeap() noexcept
    : base_address_{0ULL}, size_bytes_{0ULL}, consumed_bytes_{0ULL}, allocation_count_{0ULL},
      active_requested_bytes_{0ULL}, successful_allocation_count_{0ULL}, release_count_{0ULL},
      peak_consumed_bytes_{0ULL}, free_list_head_{nullptr} {
    static_assert(sizeof(BlockHeader) == OS_KERNEL_HEAP_BLOCK_HEADER_SIZE_BYTES);
    static_assert(alignof(BlockHeader) == OS_KERNEL_HEAP_BLOCK_ALIGNMENT_BYTES);
}

KernelHeapStatus KernelHeap::Initialize(const uint64_t base_address,
                                        const uint64_t size_bytes) noexcept {
    if (this->size_bytes_ != OS_KERNEL_HEAP_EMPTY_VALUE) {
        return KernelHeapStatus::AlreadyInitialized;
    }
    if (base_address == OS_KERNEL_HEAP_EMPTY_VALUE || size_bytes == OS_KERNEL_HEAP_EMPTY_VALUE ||
        size_bytes < OS_KERNEL_HEAP_MINIMUM_BLOCK_SIZE_BYTES ||
        (base_address & (OS_KERNEL_HEAP_BLOCK_ALIGNMENT_BYTES -
                         OS_KERNEL_HEAP_POWER_OF_TWO_DECREMENT)) != OS_KERNEL_HEAP_EMPTY_VALUE ||
        (size_bytes & (OS_KERNEL_HEAP_BLOCK_ALIGNMENT_BYTES -
                       OS_KERNEL_HEAP_POWER_OF_TWO_DECREMENT)) != OS_KERNEL_HEAP_EMPTY_VALUE ||
        base_address > UINT64_MAX - size_bytes) {
        return KernelHeapStatus::InvalidRange;
    }
    this->base_address_ = base_address;
    this->size_bytes_ = size_bytes;
    this->consumed_bytes_ = 0ULL;
    this->allocation_count_ = 0ULL;
    this->active_requested_bytes_ = 0ULL;
    this->successful_allocation_count_ = 0ULL;
    this->release_count_ = 0ULL;
    this->peak_consumed_bytes_ = 0ULL;
    this->free_list_head_ = reinterpret_cast<BlockHeader *>(base_address);
    this->ConfigureBlock(this->free_list_head_, size_bytes, OS_KERNEL_HEAP_EMPTY_VALUE,
                         OS_KERNEL_HEAP_EMPTY_VALUE, OS_KERNEL_HEAP_FREE_BLOCK_SIGNATURE);
    return KernelHeapStatus::Succeeded;
}

KernelHeapStatus KernelHeap::TryAllocate(const uint64_t size_bytes, const uint64_t alignment_bytes,
                                         void *&allocation) noexcept {
    if (this->size_bytes_ == OS_KERNEL_HEAP_EMPTY_VALUE) {
        return KernelHeapStatus::NotInitialized;
    }
    if (size_bytes == OS_KERNEL_HEAP_EMPTY_VALUE) {
        return KernelHeapStatus::EmptyAllocation;
    }
    if (!IsPowerOfTwo(alignment_bytes)) {
        return KernelHeapStatus::InvalidAlignment;
    }
    if (size_bytes > UINT64_MAX - OS_KERNEL_HEAP_BLOCK_HEADER_SIZE_BYTES) {
        return KernelHeapStatus::AddressOverflow;
    }
    uint64_t minimum_allocation_size_bytes = 0ULL;
    if (!TryAlignUp(OS_KERNEL_HEAP_BLOCK_HEADER_SIZE_BYTES + size_bytes,
                    OS_KERNEL_HEAP_BLOCK_ALIGNMENT_BYTES, minimum_allocation_size_bytes)) {
        return KernelHeapStatus::AddressOverflow;
    }

    BlockHeader *selected_block = nullptr;
    uint64_t selected_allocation_address = 0ULL;
    uint64_t selected_prefix_size_bytes = 0ULL;
    uint64_t selected_allocation_size_bytes = 0ULL;
    uint64_t selected_block_size_bytes = UINT64_MAX;
    const uint64_t maximum_block_count =
        this->size_bytes_ / OS_KERNEL_HEAP_MINIMUM_BLOCK_SIZE_BYTES;
    BlockHeader *candidate = this->free_list_head_;
    // best-fit 优先保留大块，降低小型内核堆被过早切碎的概率。
    for (uint64_t visited_block_count = 0ULL;
         candidate != nullptr && visited_block_count <= maximum_block_count;
         ++visited_block_count) {
        uint64_t candidate_allocation_address = 0ULL;
        uint64_t candidate_prefix_size_bytes = 0ULL;
        uint64_t candidate_allocation_size_bytes = 0ULL;
        if (!this->IsBlockHeaderValid(candidate) ||
            candidate->state_signature != OS_KERNEL_HEAP_FREE_BLOCK_SIGNATURE) {
            return KernelHeapStatus::CorruptedMetadata;
        }
        if (candidate->size_bytes >= minimum_allocation_size_bytes &&
            this->TryCalculateAllocationLayout(
                candidate, size_bytes, alignment_bytes, candidate_allocation_address,
                candidate_prefix_size_bytes, candidate_allocation_size_bytes) &&
            candidate->size_bytes < selected_block_size_bytes) {
            selected_block = candidate;
            selected_allocation_address = candidate_allocation_address;
            selected_prefix_size_bytes = candidate_prefix_size_bytes;
            selected_allocation_size_bytes = candidate_allocation_size_bytes;
            selected_block_size_bytes = candidate->size_bytes;
        }
        candidate = candidate->next_free_block;
    }
    if (candidate != nullptr) {
        return KernelHeapStatus::CorruptedMetadata;
    }
    if (selected_block == nullptr) {
        return KernelHeapStatus::OutOfMemory;
    }

    const uint64_t selected_block_address = reinterpret_cast<uint64_t>(selected_block);
    const uint64_t selected_block_previous_size_bytes = selected_block->previous_block_size_bytes;
    const uint64_t selected_block_end_address = selected_block_address + selected_block->size_bytes;
    this->RemoveFreeBlock(selected_block);

    // 对齐产生的前缀和剩余后缀只有达到最小块尺寸时才独立进入空闲链表。
    BlockHeader *allocation_block = reinterpret_cast<BlockHeader *>(selected_allocation_address);
    if (selected_prefix_size_bytes != OS_KERNEL_HEAP_EMPTY_VALUE) {
        this->ConfigureBlock(selected_block, selected_prefix_size_bytes,
                             selected_block_previous_size_bytes, OS_KERNEL_HEAP_EMPTY_VALUE,
                             OS_KERNEL_HEAP_FREE_BLOCK_SIGNATURE);
        this->InsertFreeBlock(selected_block);
        this->ConfigureBlock(allocation_block, selected_allocation_size_bytes,
                             selected_prefix_size_bytes, size_bytes,
                             OS_KERNEL_HEAP_ALLOCATED_BLOCK_SIGNATURE);
    } else {
        this->ConfigureBlock(allocation_block, selected_allocation_size_bytes,
                             selected_block_previous_size_bytes, size_bytes,
                             OS_KERNEL_HEAP_ALLOCATED_BLOCK_SIGNATURE);
    }

    const uint64_t allocation_end_address =
        selected_allocation_address + selected_allocation_size_bytes;
    if (allocation_end_address < selected_block_end_address) {
        BlockHeader *const suffix_block = reinterpret_cast<BlockHeader *>(allocation_end_address);
        this->ConfigureBlock(suffix_block, selected_block_end_address - allocation_end_address,
                             selected_allocation_size_bytes, OS_KERNEL_HEAP_EMPTY_VALUE,
                             OS_KERNEL_HEAP_FREE_BLOCK_SIGNATURE);
        this->InsertFreeBlock(suffix_block);
        this->UpdateFollowingBlockPreviousSize(suffix_block);
    } else {
        this->UpdateFollowingBlockPreviousSize(allocation_block);
    }

    this->consumed_bytes_ += selected_allocation_size_bytes;
    ++this->allocation_count_;
    this->active_requested_bytes_ += size_bytes;
    ++this->successful_allocation_count_;
    this->peak_consumed_bytes_ = Maximum(this->peak_consumed_bytes_, this->consumed_bytes_);
    allocation = reinterpret_cast<void *>(selected_allocation_address +
                                          OS_KERNEL_HEAP_BLOCK_HEADER_SIZE_BYTES);
    return KernelHeapStatus::Succeeded;
}

KernelHeapStatus KernelHeap::TryRelease(void *const allocation) noexcept {
    if (this->size_bytes_ == OS_KERNEL_HEAP_EMPTY_VALUE) {
        return KernelHeapStatus::NotInitialized;
    }
    if (allocation == nullptr) {
        return KernelHeapStatus::NullAllocation;
    }

    const uint64_t allocation_address = reinterpret_cast<uint64_t>(allocation);
    if (allocation_address < this->base_address_ + OS_KERNEL_HEAP_BLOCK_HEADER_SIZE_BYTES ||
        allocation_address >= this->EndAddress() ||
        (allocation_address &
         (OS_KERNEL_HEAP_BLOCK_ALIGNMENT_BYTES - OS_KERNEL_HEAP_POWER_OF_TWO_DECREMENT)) !=
            OS_KERNEL_HEAP_EMPTY_VALUE) {
        return KernelHeapStatus::InvalidAllocation;
    }
    BlockHeader *const allocation_block = reinterpret_cast<BlockHeader *>(
        allocation_address - OS_KERNEL_HEAP_BLOCK_HEADER_SIZE_BYTES);
    if (allocation_block->state_signature == OS_KERNEL_HEAP_FREE_BLOCK_SIGNATURE ||
        allocation_block->state_signature == OS_KERNEL_HEAP_RELEASED_BLOCK_SIGNATURE) {
        return KernelHeapStatus::AllocationNotActive;
    }
    if (allocation_block->state_signature != OS_KERNEL_HEAP_ALLOCATED_BLOCK_SIGNATURE ||
        !this->IsBlockHeaderValid(allocation_block) || !this->IsPhysicalBlock(allocation_block) ||
        allocation_block->requested_size_bytes == OS_KERNEL_HEAP_EMPTY_VALUE ||
        allocation_block->requested_size_bytes >
            allocation_block->size_bytes - OS_KERNEL_HEAP_BLOCK_HEADER_SIZE_BYTES) {
        return KernelHeapStatus::InvalidAllocation;
    }

    const uint64_t released_size_bytes = allocation_block->size_bytes;
    const uint64_t released_requested_size_bytes = allocation_block->requested_size_bytes;
    if (this->allocation_count_ == OS_KERNEL_HEAP_EMPTY_VALUE ||
        this->consumed_bytes_ < released_size_bytes ||
        this->active_requested_bytes_ < released_requested_size_bytes) {
        return KernelHeapStatus::CorruptedMetadata;
    }
    const uint64_t allocation_block_address = reinterpret_cast<uint64_t>(allocation_block);
    const uint64_t allocation_block_end_address = allocation_block_address + released_size_bytes;
    BlockHeader *previous_block = nullptr;
    if (allocation_block->previous_block_size_bytes != OS_KERNEL_HEAP_EMPTY_VALUE) {
        if (allocation_block->previous_block_size_bytes >
            allocation_block_address - this->base_address_) {
            return KernelHeapStatus::CorruptedMetadata;
        }
        previous_block = reinterpret_cast<BlockHeader *>(
            allocation_block_address - allocation_block->previous_block_size_bytes);
        if (!this->IsBlockHeaderValid(previous_block) ||
            reinterpret_cast<uint64_t>(previous_block) + previous_block->size_bytes !=
                allocation_block_address ||
            (previous_block->state_signature != OS_KERNEL_HEAP_FREE_BLOCK_SIGNATURE &&
             previous_block->state_signature != OS_KERNEL_HEAP_ALLOCATED_BLOCK_SIGNATURE)) {
            return KernelHeapStatus::CorruptedMetadata;
        }
    }

    BlockHeader *next_block = nullptr;
    if (allocation_block_end_address < this->EndAddress()) {
        next_block = reinterpret_cast<BlockHeader *>(allocation_block_end_address);
        if (!this->IsBlockHeaderValid(next_block) ||
            next_block->previous_block_size_bytes != released_size_bytes ||
            (next_block->state_signature != OS_KERNEL_HEAP_FREE_BLOCK_SIGNATURE &&
             next_block->state_signature != OS_KERNEL_HEAP_ALLOCATED_BLOCK_SIGNATURE)) {
            return KernelHeapStatus::CorruptedMetadata;
        }
    }

    // 所有相邻元数据预检完成后才开始修改，普通失败路径不会留下半合并状态。
    allocation_block->state_signature = OS_KERNEL_HEAP_RELEASED_BLOCK_SIGNATURE;
    allocation_block->requested_size_bytes = OS_KERNEL_HEAP_EMPTY_VALUE;
    allocation_block->previous_free_block = nullptr;
    allocation_block->next_free_block = nullptr;

    BlockHeader *merged_block = allocation_block;
    uint64_t merged_size_bytes = released_size_bytes;
    uint64_t merged_previous_block_size_bytes = allocation_block->previous_block_size_bytes;
    if (previous_block != nullptr &&
        previous_block->state_signature == OS_KERNEL_HEAP_FREE_BLOCK_SIGNATURE) {
        this->RemoveFreeBlock(previous_block);
        merged_block = previous_block;
        merged_size_bytes += previous_block->size_bytes;
        merged_previous_block_size_bytes = previous_block->previous_block_size_bytes;
    }
    if (next_block != nullptr &&
        next_block->state_signature == OS_KERNEL_HEAP_FREE_BLOCK_SIGNATURE) {
        this->RemoveFreeBlock(next_block);
        merged_size_bytes += next_block->size_bytes;
    }

    this->ConfigureBlock(merged_block, merged_size_bytes, merged_previous_block_size_bytes,
                         OS_KERNEL_HEAP_EMPTY_VALUE, OS_KERNEL_HEAP_FREE_BLOCK_SIGNATURE);
    this->UpdateFollowingBlockPreviousSize(merged_block);
    this->InsertFreeBlock(merged_block);
    this->consumed_bytes_ -= released_size_bytes;
    --this->allocation_count_;
    this->active_requested_bytes_ -= released_requested_size_bytes;
    ++this->release_count_;
    return KernelHeapStatus::Succeeded;
}

KernelHeapStatus KernelHeap::Validate() const noexcept {
    if (this->size_bytes_ == OS_KERNEL_HEAP_EMPTY_VALUE) {
        return KernelHeapStatus::NotInitialized;
    }

    const uint64_t maximum_block_count =
        this->size_bytes_ / OS_KERNEL_HEAP_MINIMUM_BLOCK_SIZE_BYTES;
    uint64_t block_address = this->base_address_;
    uint64_t previous_block_size_bytes = OS_KERNEL_HEAP_EMPTY_VALUE;
    uint64_t physical_free_block_count = 0ULL;
    uint64_t active_allocation_count = 0ULL;
    uint64_t active_requested_bytes = 0ULL;
    uint64_t consumed_bytes = 0ULL;
    uint64_t visited_block_count = 0ULL;
    while (block_address < this->EndAddress() && visited_block_count <= maximum_block_count) {
        const BlockHeader *const block = reinterpret_cast<const BlockHeader *>(block_address);
        if (!this->IsBlockHeaderValid(block) ||
            block->previous_block_size_bytes != previous_block_size_bytes) {
            return KernelHeapStatus::CorruptedMetadata;
        }
        if (block->state_signature == OS_KERNEL_HEAP_FREE_BLOCK_SIGNATURE) {
            if (block->requested_size_bytes != OS_KERNEL_HEAP_EMPTY_VALUE) {
                return KernelHeapStatus::CorruptedMetadata;
            }
            ++physical_free_block_count;
        } else if (block->state_signature == OS_KERNEL_HEAP_ALLOCATED_BLOCK_SIGNATURE) {
            if (block->requested_size_bytes == OS_KERNEL_HEAP_EMPTY_VALUE ||
                block->requested_size_bytes >
                    block->size_bytes - OS_KERNEL_HEAP_BLOCK_HEADER_SIZE_BYTES) {
                return KernelHeapStatus::CorruptedMetadata;
            }
            ++active_allocation_count;
            active_requested_bytes += block->requested_size_bytes;
            consumed_bytes += block->size_bytes;
        } else {
            return KernelHeapStatus::CorruptedMetadata;
        }
        previous_block_size_bytes = block->size_bytes;
        block_address += block->size_bytes;
        ++visited_block_count;
    }
    if (block_address != this->EndAddress() || visited_block_count > maximum_block_count) {
        return KernelHeapStatus::CorruptedMetadata;
    }

    // 物理块链验证边界标记；空闲链验证排序、反向链接与环路上界。
    uint64_t free_list_block_count = 0ULL;
    const BlockHeader *previous_free_block = nullptr;
    const BlockHeader *free_block = this->free_list_head_;
    while (free_block != nullptr && free_list_block_count <= maximum_block_count) {
        if (!this->IsBlockHeaderValid(free_block) ||
            free_block->state_signature != OS_KERNEL_HEAP_FREE_BLOCK_SIGNATURE ||
            free_block->previous_free_block != previous_free_block ||
            (previous_free_block != nullptr && reinterpret_cast<uint64_t>(previous_free_block) >=
                                                   reinterpret_cast<uint64_t>(free_block))) {
            return KernelHeapStatus::CorruptedMetadata;
        }
        previous_free_block = free_block;
        free_block = free_block->next_free_block;
        ++free_list_block_count;
    }
    if (free_block != nullptr || free_list_block_count != physical_free_block_count) {
        return KernelHeapStatus::CorruptedMetadata;
    }

    // 数量相等仍不足以证明集合相等，因此逐个确认每个物理空闲块恰好出现一次。
    block_address = this->base_address_;
    while (block_address < this->EndAddress()) {
        const BlockHeader *const block = reinterpret_cast<const BlockHeader *>(block_address);
        if (block->state_signature == OS_KERNEL_HEAP_FREE_BLOCK_SIGNATURE) {
            uint64_t occurrence_count = 0ULL;
            for (const BlockHeader *listed_block = this->free_list_head_; listed_block != nullptr;
                 listed_block = listed_block->next_free_block) {
                if (listed_block == block) {
                    ++occurrence_count;
                }
            }
            if (occurrence_count != 1ULL) {
                return KernelHeapStatus::CorruptedMetadata;
            }
        }
        block_address += block->size_bytes;
    }

    if (active_allocation_count != this->allocation_count_ ||
        active_requested_bytes != this->active_requested_bytes_ ||
        consumed_bytes != this->consumed_bytes_ || this->consumed_bytes_ > this->size_bytes_ ||
        this->peak_consumed_bytes_ < this->consumed_bytes_ ||
        this->successful_allocation_count_ != this->release_count_ + this->allocation_count_) {
        return KernelHeapStatus::CorruptedMetadata;
    }
    return KernelHeapStatus::Succeeded;
}

KernelHeapStatistics KernelHeap::Statistics() const noexcept {
    uint64_t largest_free_allocation_bytes = 0ULL;
    if (this->size_bytes_ != OS_KERNEL_HEAP_EMPTY_VALUE) {
        const uint64_t maximum_block_count =
            this->size_bytes_ / OS_KERNEL_HEAP_MINIMUM_BLOCK_SIZE_BYTES;
        const BlockHeader *free_block = this->free_list_head_;
        for (uint64_t visited_block_count = 0ULL;
             free_block != nullptr && visited_block_count <= maximum_block_count;
             ++visited_block_count) {
            if (!this->IsBlockHeaderValid(free_block) ||
                free_block->state_signature != OS_KERNEL_HEAP_FREE_BLOCK_SIGNATURE) {
                break;
            }
            const uint64_t free_allocation_bytes =
                free_block->size_bytes - OS_KERNEL_HEAP_BLOCK_HEADER_SIZE_BYTES;
            largest_free_allocation_bytes =
                Maximum(largest_free_allocation_bytes, free_allocation_bytes);
            free_block = free_block->next_free_block;
        }
    }
    return KernelHeapStatistics{
        .capacity_bytes = this->size_bytes_,
        .consumed_bytes = this->consumed_bytes_,
        .remaining_bytes = this->consumed_bytes_ <= this->size_bytes_
                               ? this->size_bytes_ - this->consumed_bytes_
                               : OS_KERNEL_HEAP_EMPTY_VALUE,
        .allocation_count = this->allocation_count_,
        .active_requested_bytes = this->active_requested_bytes_,
        .successful_allocation_count = this->successful_allocation_count_,
        .release_count = this->release_count_,
        .peak_consumed_bytes = this->peak_consumed_bytes_,
        .largest_free_allocation_bytes = largest_free_allocation_bytes,
    };
}

uint64_t KernelHeap::EndAddress() const noexcept { return this->base_address_ + this->size_bytes_; }

bool KernelHeap::IsAddressInsideHeap(const uint64_t address) const noexcept {
    return address >= this->base_address_ && address < this->EndAddress();
}

bool KernelHeap::IsBlockHeaderValid(const BlockHeader *const block) const noexcept {
    if (block == nullptr) {
        return false;
    }
    const uint64_t block_address = reinterpret_cast<uint64_t>(block);
    if (!this->IsAddressInsideHeap(block_address) ||
        (block_address & (OS_KERNEL_HEAP_BLOCK_ALIGNMENT_BYTES -
                          OS_KERNEL_HEAP_POWER_OF_TWO_DECREMENT)) != OS_KERNEL_HEAP_EMPTY_VALUE ||
        block->size_bytes < OS_KERNEL_HEAP_MINIMUM_BLOCK_SIZE_BYTES ||
        (block->size_bytes &
         (OS_KERNEL_HEAP_BLOCK_ALIGNMENT_BYTES - OS_KERNEL_HEAP_POWER_OF_TWO_DECREMENT)) !=
            OS_KERNEL_HEAP_EMPTY_VALUE ||
        block->size_bytes > this->EndAddress() - block_address) {
        return false;
    }
    return true;
}

bool KernelHeap::IsPhysicalBlock(const BlockHeader *const target_block) const noexcept {
    const uint64_t maximum_block_count =
        this->size_bytes_ / OS_KERNEL_HEAP_MINIMUM_BLOCK_SIZE_BYTES;
    uint64_t block_address = this->base_address_;
    uint64_t previous_block_size_bytes = OS_KERNEL_HEAP_EMPTY_VALUE;
    for (uint64_t visited_block_count = 0ULL;
         block_address < this->EndAddress() && visited_block_count <= maximum_block_count;
         ++visited_block_count) {
        const BlockHeader *const block = reinterpret_cast<const BlockHeader *>(block_address);
        if (!this->IsBlockHeaderValid(block) ||
            block->previous_block_size_bytes != previous_block_size_bytes ||
            (block->state_signature != OS_KERNEL_HEAP_FREE_BLOCK_SIGNATURE &&
             block->state_signature != OS_KERNEL_HEAP_ALLOCATED_BLOCK_SIGNATURE)) {
            return false;
        }
        if (block == target_block) {
            return true;
        }
        previous_block_size_bytes = block->size_bytes;
        block_address += block->size_bytes;
    }
    return false;
}

bool KernelHeap::TryCalculateAllocationLayout(const BlockHeader *const free_block,
                                              const uint64_t requested_size_bytes,
                                              const uint64_t alignment_bytes,
                                              uint64_t &allocation_address,
                                              uint64_t &prefix_size_bytes,
                                              uint64_t &allocation_size_bytes) const noexcept {
    const uint64_t effective_alignment_bytes =
        Maximum(alignment_bytes, OS_KERNEL_HEAP_BLOCK_ALIGNMENT_BYTES);
    const uint64_t free_block_address = reinterpret_cast<uint64_t>(free_block);
    const uint64_t free_block_end_address = free_block_address + free_block->size_bytes;
    if (free_block_address > UINT64_MAX - OS_KERNEL_HEAP_BLOCK_HEADER_SIZE_BYTES) {
        return false;
    }
    uint64_t payload_address = 0ULL;
    if (!TryAlignUp(free_block_address + OS_KERNEL_HEAP_BLOCK_HEADER_SIZE_BYTES,
                    effective_alignment_bytes, payload_address)) {
        return false;
    }
    uint64_t candidate_allocation_address =
        payload_address - OS_KERNEL_HEAP_BLOCK_HEADER_SIZE_BYTES;
    uint64_t candidate_prefix_size_bytes = candidate_allocation_address - free_block_address;
    // 不能表示成完整空闲块的短前缀会跳到下一个合法对齐位置。
    while (candidate_prefix_size_bytes != OS_KERNEL_HEAP_EMPTY_VALUE &&
           candidate_prefix_size_bytes < OS_KERNEL_HEAP_MINIMUM_BLOCK_SIZE_BYTES) {
        if (payload_address > UINT64_MAX - effective_alignment_bytes) {
            return false;
        }
        payload_address += effective_alignment_bytes;
        candidate_allocation_address = payload_address - OS_KERNEL_HEAP_BLOCK_HEADER_SIZE_BYTES;
        candidate_prefix_size_bytes = candidate_allocation_address - free_block_address;
    }

    if (requested_size_bytes > UINT64_MAX - OS_KERNEL_HEAP_BLOCK_HEADER_SIZE_BYTES) {
        return false;
    }
    uint64_t candidate_allocation_size_bytes = 0ULL;
    if (!TryAlignUp(OS_KERNEL_HEAP_BLOCK_HEADER_SIZE_BYTES + requested_size_bytes,
                    OS_KERNEL_HEAP_BLOCK_ALIGNMENT_BYTES, candidate_allocation_size_bytes) ||
        candidate_allocation_address > free_block_end_address ||
        candidate_allocation_size_bytes > free_block_end_address - candidate_allocation_address) {
        return false;
    }
    const uint64_t suffix_size_bytes =
        free_block_end_address - candidate_allocation_address - candidate_allocation_size_bytes;
    if (suffix_size_bytes != OS_KERNEL_HEAP_EMPTY_VALUE &&
        suffix_size_bytes < OS_KERNEL_HEAP_MINIMUM_BLOCK_SIZE_BYTES) {
        candidate_allocation_size_bytes += suffix_size_bytes;
    }
    allocation_address = candidate_allocation_address;
    prefix_size_bytes = candidate_prefix_size_bytes;
    allocation_size_bytes = candidate_allocation_size_bytes;
    return true;
}

void KernelHeap::ConfigureBlock(BlockHeader *const block, const uint64_t size_bytes,
                                const uint64_t previous_block_size_bytes,
                                const uint64_t requested_size_bytes,
                                const uint64_t state_signature) noexcept {
    block->size_bytes = size_bytes;
    block->previous_block_size_bytes = previous_block_size_bytes;
    block->requested_size_bytes = requested_size_bytes;
    block->state_signature = state_signature;
    block->previous_free_block = nullptr;
    block->next_free_block = nullptr;
}

void KernelHeap::InsertFreeBlock(BlockHeader *const block) noexcept {
    if (this->free_list_head_ == nullptr ||
        reinterpret_cast<uint64_t>(block) < reinterpret_cast<uint64_t>(this->free_list_head_)) {
        block->next_free_block = this->free_list_head_;
        if (this->free_list_head_ != nullptr) {
            this->free_list_head_->previous_free_block = block;
        }
        this->free_list_head_ = block;
        return;
    }

    BlockHeader *previous_block = this->free_list_head_;
    while (previous_block->next_free_block != nullptr &&
           reinterpret_cast<uint64_t>(previous_block->next_free_block) <
               reinterpret_cast<uint64_t>(block)) {
        previous_block = previous_block->next_free_block;
    }
    block->previous_free_block = previous_block;
    block->next_free_block = previous_block->next_free_block;
    if (previous_block->next_free_block != nullptr) {
        previous_block->next_free_block->previous_free_block = block;
    }
    previous_block->next_free_block = block;
}

void KernelHeap::RemoveFreeBlock(BlockHeader *const block) noexcept {
    if (block->previous_free_block != nullptr) {
        block->previous_free_block->next_free_block = block->next_free_block;
    } else {
        this->free_list_head_ = block->next_free_block;
    }
    if (block->next_free_block != nullptr) {
        block->next_free_block->previous_free_block = block->previous_free_block;
    }
    block->previous_free_block = nullptr;
    block->next_free_block = nullptr;
}

void KernelHeap::UpdateFollowingBlockPreviousSize(BlockHeader *const block) noexcept {
    const uint64_t following_block_address = reinterpret_cast<uint64_t>(block) + block->size_bytes;
    if (following_block_address < this->EndAddress()) {
        BlockHeader *const following_block =
            reinterpret_cast<BlockHeader *>(following_block_address);
        following_block->previous_block_size_bytes = block->size_bytes;
    }
}

}

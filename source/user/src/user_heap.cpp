#include "os/user/user_heap.hpp"

namespace os::user {

namespace {

constexpr uint64_t OS_USER_HEAP_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_USER_HEAP_SINGLE_UNIT = 1ULL;
constexpr uint64_t OS_USER_HEAP_INVALID_OFFSET = UINT64_MAX;
constexpr uint64_t OS_USER_HEAP_MINIMUM_PAYLOAD_BYTES = OS_USER_HEAP_BLOCK_ALIGNMENT_BYTES;
constexpr uint64_t OS_USER_HEAP_BLOCK_STATE_ALLOCATED = 0xA110CA7EULL;
constexpr uint64_t OS_USER_HEAP_BLOCK_STATE_FREE = 0xF4EEB10CULL;
constexpr uint64_t OS_USER_HEAP_BLOCK_SIGNATURE = 0x4F5348454150424CULL;
constexpr int64_t OS_USER_HEAP_NON_POSITIVE_BREAK_RESULT = 0LL;

[[nodiscard]] bool IsPowerOfTwo(const uint64_t value) noexcept {
    return value != OS_USER_HEAP_EMPTY_VALUE &&
           (value & (value - OS_USER_HEAP_SINGLE_UNIT)) == OS_USER_HEAP_EMPTY_VALUE;
}

}

UserHeap::UserHeap() noexcept
    : configuration_{}, base_address_(nullptr), capacity_bytes_(OS_USER_HEAP_EMPTY_VALUE),
      first_free_block_offset_(OS_USER_HEAP_INVALID_OFFSET),
      last_block_offset_(OS_USER_HEAP_INVALID_OFFSET),
      active_allocation_count_(OS_USER_HEAP_EMPTY_VALUE),
      active_requested_bytes_(OS_USER_HEAP_EMPTY_VALUE),
      successful_allocation_count_(OS_USER_HEAP_EMPTY_VALUE),
      release_count_(OS_USER_HEAP_EMPTY_VALUE), growth_count_(OS_USER_HEAP_EMPTY_VALUE),
      failed_allocation_count_(OS_USER_HEAP_EMPTY_VALUE),
      peak_active_requested_bytes_(OS_USER_HEAP_EMPTY_VALUE), initialized_(false) {}

UserHeapStatus UserHeap::Initialize(const UserHeapConfiguration &configuration) noexcept {
    if (this->initialized_) {
        return UserHeapStatus::AlreadyInitialized;
    }
    if (configuration.program_break_operation == nullptr ||
        !IsPowerOfTwo(configuration.page_size_bytes) ||
        configuration.page_size_bytes < OS_USER_HEAP_BLOCK_ALIGNMENT_BYTES ||
        configuration.maximum_capacity_bytes <
            sizeof(BlockHeader) + OS_USER_HEAP_MINIMUM_PAYLOAD_BYTES ||
        configuration.maximum_capacity_bytes % configuration.page_size_bytes !=
            OS_USER_HEAP_EMPTY_VALUE ||
        configuration.growth_quantum_bytes == OS_USER_HEAP_EMPTY_VALUE ||
        configuration.growth_quantum_bytes > configuration.maximum_capacity_bytes ||
        configuration.growth_quantum_bytes % configuration.page_size_bytes !=
            OS_USER_HEAP_EMPTY_VALUE) {
        return UserHeapStatus::InvalidConfiguration;
    }
    const int64_t current_break_result =
        configuration.program_break_operation(configuration.context, OS_USER_HEAP_EMPTY_VALUE);
    if (current_break_result <= OS_USER_HEAP_NON_POSITIVE_BREAK_RESULT ||
        static_cast<uint64_t>(current_break_result) % configuration.page_size_bytes !=
            OS_USER_HEAP_EMPTY_VALUE ||
        static_cast<uint64_t>(current_break_result) >
            static_cast<uint64_t>(INT64_MAX) - configuration.maximum_capacity_bytes) {
        return UserHeapStatus::ProgramBreakFailed;
    }

    this->configuration_ = configuration;
    this->base_address_ = reinterpret_cast<uint8_t *>(static_cast<uint64_t>(current_break_result));
    this->capacity_bytes_ = OS_USER_HEAP_EMPTY_VALUE;
    this->first_free_block_offset_ = OS_USER_HEAP_INVALID_OFFSET;
    this->last_block_offset_ = OS_USER_HEAP_INVALID_OFFSET;
    this->active_allocation_count_ = OS_USER_HEAP_EMPTY_VALUE;
    this->active_requested_bytes_ = OS_USER_HEAP_EMPTY_VALUE;
    this->successful_allocation_count_ = OS_USER_HEAP_EMPTY_VALUE;
    this->release_count_ = OS_USER_HEAP_EMPTY_VALUE;
    this->growth_count_ = OS_USER_HEAP_EMPTY_VALUE;
    this->failed_allocation_count_ = OS_USER_HEAP_EMPTY_VALUE;
    this->peak_active_requested_bytes_ = OS_USER_HEAP_EMPTY_VALUE;
    this->initialized_ = true;
    return UserHeapStatus::Succeeded;
}

UserHeapStatus UserHeap::Allocate(const uint64_t size_bytes, void *&allocation) noexcept {
    if (!this->initialized_) {
        return UserHeapStatus::NotInitialized;
    }
    if (size_bytes == OS_USER_HEAP_EMPTY_VALUE) {
        ++this->failed_allocation_count_;
        return UserHeapStatus::InvalidSize;
    }
    const uint64_t aligned_size_bytes =
        this->AlignUp(size_bytes, OS_USER_HEAP_BLOCK_ALIGNMENT_BYTES);
    if (aligned_size_bytes == OS_USER_HEAP_EMPTY_VALUE ||
        aligned_size_bytes > this->configuration_.maximum_capacity_bytes) {
        ++this->failed_allocation_count_;
        return UserHeapStatus::CapacityExhausted;
    }

    uint64_t block_offset = this->first_free_block_offset_;
    while (block_offset != OS_USER_HEAP_INVALID_OFFSET) {
        BlockHeader *const header = this->HeaderAt(block_offset);
        if (header == nullptr || header->signature != OS_USER_HEAP_BLOCK_SIGNATURE ||
            header->state != OS_USER_HEAP_BLOCK_STATE_FREE) {
            return UserHeapStatus::Corrupt;
        }
        if (header->payload_capacity_bytes >= aligned_size_bytes) {
            break;
        }
        block_offset = header->next_free_block_offset;
    }
    if (block_offset == OS_USER_HEAP_INVALID_OFFSET) {
        const UserHeapStatus growth_status = this->Grow(aligned_size_bytes);
        if (growth_status != UserHeapStatus::Succeeded) {
            ++this->failed_allocation_count_;
            return growth_status;
        }
        block_offset = this->first_free_block_offset_;
        while (block_offset != OS_USER_HEAP_INVALID_OFFSET) {
            BlockHeader *const header = this->HeaderAt(block_offset);
            if (header == nullptr || header->signature != OS_USER_HEAP_BLOCK_SIGNATURE ||
                header->state != OS_USER_HEAP_BLOCK_STATE_FREE) {
                return UserHeapStatus::Corrupt;
            }
            if (header->payload_capacity_bytes >= aligned_size_bytes) {
                break;
            }
            block_offset = header->next_free_block_offset;
        }
        if (block_offset == OS_USER_HEAP_INVALID_OFFSET) {
            return UserHeapStatus::Corrupt;
        }
    }

    BlockHeader *const header = this->HeaderAt(block_offset);
    this->RemoveFreeBlock(block_offset);
    const uint64_t remaining_payload_bytes = header->payload_capacity_bytes - aligned_size_bytes;
    if (remaining_payload_bytes >= sizeof(BlockHeader) + OS_USER_HEAP_MINIMUM_PAYLOAD_BYTES) {
        const uint64_t split_block_offset = block_offset + sizeof(BlockHeader) + aligned_size_bytes;
        BlockHeader *const split_header = this->HeaderAt(split_block_offset);
        if (split_header == nullptr) {
            return UserHeapStatus::Corrupt;
        }
        const uint64_t previous_next_offset = this->NextPhysicalOffset(block_offset, *header);
        this->InitializeHeader(*split_header, remaining_payload_bytes - sizeof(BlockHeader),
                               block_offset, OS_USER_HEAP_BLOCK_STATE_FREE);
        header->payload_capacity_bytes = aligned_size_bytes;
        if (previous_next_offset < this->capacity_bytes_) {
            BlockHeader *const next_header = this->HeaderAt(previous_next_offset);
            if (next_header == nullptr) {
                return UserHeapStatus::Corrupt;
            }
            next_header->previous_block_offset = split_block_offset;
        } else {
            this->last_block_offset_ = split_block_offset;
        }
        this->InsertFreeBlock(split_block_offset);
    }

    header->requested_size_bytes = size_bytes;
    header->state = OS_USER_HEAP_BLOCK_STATE_ALLOCATED;
    ++this->active_allocation_count_;
    this->active_requested_bytes_ += size_bytes;
    ++this->successful_allocation_count_;
    if (this->active_requested_bytes_ > this->peak_active_requested_bytes_) {
        this->peak_active_requested_bytes_ = this->active_requested_bytes_;
    }
    allocation = this->base_address_ + block_offset + sizeof(BlockHeader);
    return UserHeapStatus::Succeeded;
}

UserHeapStatus UserHeap::Release(void *const allocation) noexcept {
    if (!this->initialized_) {
        return UserHeapStatus::NotInitialized;
    }
    if (allocation == nullptr) {
        return UserHeapStatus::AllocationNotFound;
    }
    uint64_t block_offset = OS_USER_HEAP_INVALID_OFFSET;
    const UserHeapStatus find_status = this->FindAllocation(allocation, block_offset);
    if (find_status != UserHeapStatus::Succeeded) {
        return find_status;
    }
    BlockHeader *const header = this->HeaderAt(block_offset);
    if (header->state == OS_USER_HEAP_BLOCK_STATE_FREE) {
        return UserHeapStatus::AllocationAlreadyReleased;
    }
    if (header->state != OS_USER_HEAP_BLOCK_STATE_ALLOCATED ||
        header->requested_size_bytes > this->active_requested_bytes_ ||
        this->active_allocation_count_ == OS_USER_HEAP_EMPTY_VALUE) {
        return UserHeapStatus::Corrupt;
    }

    this->active_requested_bytes_ -= header->requested_size_bytes;
    --this->active_allocation_count_;
    ++this->release_count_;
    header->requested_size_bytes = OS_USER_HEAP_EMPTY_VALUE;
    header->state = OS_USER_HEAP_BLOCK_STATE_FREE;
    this->InsertFreeBlock(block_offset);
    this->CoalesceFreeBlock(block_offset);
    return UserHeapStatus::Succeeded;
}

UserHeapStatistics UserHeap::Statistics() const noexcept {
    if (!this->initialized_) {
        return UserHeapStatistics{};
    }
    uint64_t free_block_count = OS_USER_HEAP_EMPTY_VALUE;
    uint64_t largest_free_block_bytes = OS_USER_HEAP_EMPTY_VALUE;
    uint64_t block_offset = OS_USER_HEAP_EMPTY_VALUE;
    while (block_offset < this->capacity_bytes_) {
        const BlockHeader *const header = this->HeaderAt(block_offset);
        if (header == nullptr) {
            return UserHeapStatistics{};
        }
        if (header->state == OS_USER_HEAP_BLOCK_STATE_FREE) {
            ++free_block_count;
            if (header->payload_capacity_bytes > largest_free_block_bytes) {
                largest_free_block_bytes = header->payload_capacity_bytes;
            }
        }
        const uint64_t next_block_offset = this->NextPhysicalOffset(block_offset, *header);
        if (next_block_offset <= block_offset || next_block_offset > this->capacity_bytes_) {
            return UserHeapStatistics{};
        }
        block_offset = next_block_offset;
    }
    return UserHeapStatistics{
        .capacity_bytes = this->capacity_bytes_,
        .maximum_capacity_bytes = this->configuration_.maximum_capacity_bytes,
        .active_allocation_count = this->active_allocation_count_,
        .active_requested_bytes = this->active_requested_bytes_,
        .free_block_count = free_block_count,
        .largest_free_block_bytes = largest_free_block_bytes,
        .successful_allocation_count = this->successful_allocation_count_,
        .release_count = this->release_count_,
        .growth_count = this->growth_count_,
        .failed_allocation_count = this->failed_allocation_count_,
        .peak_active_requested_bytes = this->peak_active_requested_bytes_,
    };
}

UserHeapStatus UserHeap::Validate() const noexcept {
    if (!this->initialized_) {
        return UserHeapStatus::NotInitialized;
    }
    if (this->base_address_ == nullptr ||
        this->capacity_bytes_ > this->configuration_.maximum_capacity_bytes ||
        this->capacity_bytes_ % this->configuration_.page_size_bytes != OS_USER_HEAP_EMPTY_VALUE ||
        this->release_count_ > this->successful_allocation_count_) {
        return UserHeapStatus::Corrupt;
    }
    if (this->capacity_bytes_ == OS_USER_HEAP_EMPTY_VALUE) {
        return this->first_free_block_offset_ == OS_USER_HEAP_INVALID_OFFSET &&
                       this->last_block_offset_ == OS_USER_HEAP_INVALID_OFFSET &&
                       this->active_allocation_count_ == OS_USER_HEAP_EMPTY_VALUE
                   ? UserHeapStatus::Succeeded
                   : UserHeapStatus::Corrupt;
    }

    uint64_t observed_active_count = OS_USER_HEAP_EMPTY_VALUE;
    uint64_t observed_requested_bytes = OS_USER_HEAP_EMPTY_VALUE;
    uint64_t observed_free_count = OS_USER_HEAP_EMPTY_VALUE;
    uint64_t previous_block_offset = OS_USER_HEAP_INVALID_OFFSET;
    uint64_t block_offset = OS_USER_HEAP_EMPTY_VALUE;
    while (block_offset < this->capacity_bytes_) {
        const BlockHeader *const header = this->HeaderAt(block_offset);
        if (header == nullptr || header->signature != OS_USER_HEAP_BLOCK_SIGNATURE ||
            header->previous_block_offset != previous_block_offset ||
            header->payload_capacity_bytes < OS_USER_HEAP_MINIMUM_PAYLOAD_BYTES ||
            header->payload_capacity_bytes % OS_USER_HEAP_BLOCK_ALIGNMENT_BYTES !=
                OS_USER_HEAP_EMPTY_VALUE) {
            return UserHeapStatus::Corrupt;
        }
        if (header->state == OS_USER_HEAP_BLOCK_STATE_ALLOCATED) {
            if (header->requested_size_bytes == OS_USER_HEAP_EMPTY_VALUE ||
                header->requested_size_bytes > header->payload_capacity_bytes) {
                return UserHeapStatus::Corrupt;
            }
            ++observed_active_count;
            observed_requested_bytes += header->requested_size_bytes;
        } else if (header->state == OS_USER_HEAP_BLOCK_STATE_FREE) {
            if (header->requested_size_bytes != OS_USER_HEAP_EMPTY_VALUE) {
                return UserHeapStatus::Corrupt;
            }
            ++observed_free_count;
        } else {
            return UserHeapStatus::Corrupt;
        }
        previous_block_offset = block_offset;
        const uint64_t next_block_offset = this->NextPhysicalOffset(block_offset, *header);
        if (next_block_offset <= block_offset || next_block_offset > this->capacity_bytes_) {
            return UserHeapStatus::Corrupt;
        }
        block_offset = next_block_offset;
    }
    if (block_offset != this->capacity_bytes_ ||
        previous_block_offset != this->last_block_offset_ ||
        observed_active_count != this->active_allocation_count_ ||
        observed_requested_bytes != this->active_requested_bytes_) {
        return UserHeapStatus::Corrupt;
    }

    uint64_t free_list_count = OS_USER_HEAP_EMPTY_VALUE;
    uint64_t previous_free_offset = OS_USER_HEAP_INVALID_OFFSET;
    uint64_t free_offset = this->first_free_block_offset_;
    while (free_offset != OS_USER_HEAP_INVALID_OFFSET) {
        if (free_list_count >= observed_free_count) {
            return UserHeapStatus::Corrupt;
        }
        const BlockHeader *const header = this->HeaderAt(free_offset);
        if (header == nullptr || header->state != OS_USER_HEAP_BLOCK_STATE_FREE ||
            header->previous_free_block_offset != previous_free_offset) {
            return UserHeapStatus::Corrupt;
        }
        previous_free_offset = free_offset;
        free_offset = header->next_free_block_offset;
        ++free_list_count;
    }
    return free_list_count == observed_free_count ? UserHeapStatus::Succeeded
                                                  : UserHeapStatus::Corrupt;
}

uint64_t UserHeap::AlignUp(const uint64_t value, const uint64_t alignment) const noexcept {
    const uint64_t alignment_mask = alignment - OS_USER_HEAP_SINGLE_UNIT;
    if (value > UINT64_MAX - alignment_mask) {
        return OS_USER_HEAP_EMPTY_VALUE;
    }
    return (value + alignment_mask) & ~alignment_mask;
}

UserHeap::BlockHeader *UserHeap::HeaderAt(const uint64_t block_offset) const noexcept {
    if (this->base_address_ == nullptr || block_offset > this->capacity_bytes_ ||
        sizeof(BlockHeader) > this->capacity_bytes_ - block_offset) {
        return nullptr;
    }
    return reinterpret_cast<BlockHeader *>(this->base_address_ + block_offset);
}

uint64_t UserHeap::NextPhysicalOffset(const uint64_t block_offset,
                                      const BlockHeader &header) const noexcept {
    if (block_offset > UINT64_MAX - sizeof(BlockHeader)) {
        return OS_USER_HEAP_INVALID_OFFSET;
    }
    const uint64_t payload_offset = block_offset + sizeof(BlockHeader);
    if (header.payload_capacity_bytes > UINT64_MAX - payload_offset) {
        return OS_USER_HEAP_INVALID_OFFSET;
    }
    return payload_offset + header.payload_capacity_bytes;
}

UserHeapStatus UserHeap::Grow(const uint64_t minimum_payload_bytes) noexcept {
    if (minimum_payload_bytes > UINT64_MAX - sizeof(BlockHeader)) {
        return UserHeapStatus::CapacityExhausted;
    }
    const uint64_t minimum_growth_bytes = this->AlignUp(minimum_payload_bytes + sizeof(BlockHeader),
                                                        this->configuration_.page_size_bytes);
    if (minimum_growth_bytes == OS_USER_HEAP_EMPTY_VALUE) {
        return UserHeapStatus::CapacityExhausted;
    }
    const uint64_t growth_bytes = minimum_growth_bytes > this->configuration_.growth_quantum_bytes
                                      ? minimum_growth_bytes
                                      : this->configuration_.growth_quantum_bytes;
    if (growth_bytes > this->configuration_.maximum_capacity_bytes - this->capacity_bytes_) {
        return UserHeapStatus::CapacityExhausted;
    }
    const uint64_t requested_break_address =
        reinterpret_cast<uint64_t>(this->base_address_) + this->capacity_bytes_ + growth_bytes;
    const int64_t break_result = this->configuration_.program_break_operation(
        this->configuration_.context, requested_break_address);
    if (break_result <= OS_USER_HEAP_NON_POSITIVE_BREAK_RESULT ||
        static_cast<uint64_t>(break_result) != requested_break_address) {
        return UserHeapStatus::ProgramBreakFailed;
    }

    const uint64_t previous_capacity_bytes = this->capacity_bytes_;
    this->capacity_bytes_ += growth_bytes;
    if (previous_capacity_bytes == OS_USER_HEAP_EMPTY_VALUE) {
        BlockHeader *const first_header = this->HeaderAt(OS_USER_HEAP_EMPTY_VALUE);
        if (first_header == nullptr) {
            return UserHeapStatus::Corrupt;
        }
        this->InitializeHeader(*first_header, growth_bytes - sizeof(BlockHeader),
                               OS_USER_HEAP_INVALID_OFFSET, OS_USER_HEAP_BLOCK_STATE_FREE);
        this->last_block_offset_ = OS_USER_HEAP_EMPTY_VALUE;
        this->InsertFreeBlock(OS_USER_HEAP_EMPTY_VALUE);
    } else {
        BlockHeader *const previous_last_header = this->HeaderAt(this->last_block_offset_);
        if (previous_last_header == nullptr) {
            return UserHeapStatus::Corrupt;
        }
        if (previous_last_header->state == OS_USER_HEAP_BLOCK_STATE_FREE) {
            previous_last_header->payload_capacity_bytes += growth_bytes;
        } else {
            BlockHeader *const new_header = this->HeaderAt(previous_capacity_bytes);
            if (new_header == nullptr) {
                return UserHeapStatus::Corrupt;
            }
            this->InitializeHeader(*new_header, growth_bytes - sizeof(BlockHeader),
                                   this->last_block_offset_, OS_USER_HEAP_BLOCK_STATE_FREE);
            this->last_block_offset_ = previous_capacity_bytes;
            this->InsertFreeBlock(previous_capacity_bytes);
        }
    }
    ++this->growth_count_;
    return UserHeapStatus::Succeeded;
}

UserHeapStatus UserHeap::FindAllocation(void *const allocation,
                                        uint64_t &block_offset) const noexcept {
    const uint64_t allocation_address = reinterpret_cast<uint64_t>(allocation);
    const uint64_t base_address = reinterpret_cast<uint64_t>(this->base_address_);
    if (allocation_address < base_address + sizeof(BlockHeader) ||
        allocation_address >= base_address + this->capacity_bytes_) {
        return UserHeapStatus::AllocationNotFound;
    }
    uint64_t candidate_offset = OS_USER_HEAP_EMPTY_VALUE;
    while (candidate_offset < this->capacity_bytes_) {
        const BlockHeader *const header = this->HeaderAt(candidate_offset);
        if (header == nullptr || header->signature != OS_USER_HEAP_BLOCK_SIGNATURE) {
            return UserHeapStatus::Corrupt;
        }
        if (allocation_address == base_address + candidate_offset + sizeof(BlockHeader)) {
            block_offset = candidate_offset;
            return UserHeapStatus::Succeeded;
        }
        const uint64_t next_candidate_offset = this->NextPhysicalOffset(candidate_offset, *header);
        if (next_candidate_offset <= candidate_offset ||
            next_candidate_offset > this->capacity_bytes_) {
            return UserHeapStatus::Corrupt;
        }
        candidate_offset = next_candidate_offset;
    }
    return UserHeapStatus::AllocationNotFound;
}

void UserHeap::InitializeHeader(BlockHeader &header, const uint64_t payload_capacity_bytes,
                                const uint64_t previous_block_offset,
                                const uint64_t state) noexcept {
    header = BlockHeader{
        .payload_capacity_bytes = payload_capacity_bytes,
        .requested_size_bytes = OS_USER_HEAP_EMPTY_VALUE,
        .previous_block_offset = previous_block_offset,
        .next_free_block_offset = OS_USER_HEAP_INVALID_OFFSET,
        .previous_free_block_offset = OS_USER_HEAP_INVALID_OFFSET,
        .state = state,
        .signature = OS_USER_HEAP_BLOCK_SIGNATURE,
        .reserved = OS_USER_HEAP_EMPTY_VALUE,
    };
}

void UserHeap::InsertFreeBlock(const uint64_t block_offset) noexcept {
    BlockHeader *const header = this->HeaderAt(block_offset);
    header->previous_free_block_offset = OS_USER_HEAP_INVALID_OFFSET;
    header->next_free_block_offset = this->first_free_block_offset_;
    if (this->first_free_block_offset_ != OS_USER_HEAP_INVALID_OFFSET) {
        this->HeaderAt(this->first_free_block_offset_)->previous_free_block_offset = block_offset;
    }
    this->first_free_block_offset_ = block_offset;
}

void UserHeap::RemoveFreeBlock(const uint64_t block_offset) noexcept {
    BlockHeader *const header = this->HeaderAt(block_offset);
    if (header->previous_free_block_offset == OS_USER_HEAP_INVALID_OFFSET) {
        this->first_free_block_offset_ = header->next_free_block_offset;
    } else {
        this->HeaderAt(header->previous_free_block_offset)->next_free_block_offset =
            header->next_free_block_offset;
    }
    if (header->next_free_block_offset != OS_USER_HEAP_INVALID_OFFSET) {
        this->HeaderAt(header->next_free_block_offset)->previous_free_block_offset =
            header->previous_free_block_offset;
    }
    header->next_free_block_offset = OS_USER_HEAP_INVALID_OFFSET;
    header->previous_free_block_offset = OS_USER_HEAP_INVALID_OFFSET;
}

void UserHeap::UpdateNextPreviousOffset(const uint64_t block_offset,
                                        const BlockHeader &header) noexcept {
    const uint64_t next_block_offset = this->NextPhysicalOffset(block_offset, header);
    if (next_block_offset < this->capacity_bytes_) {
        this->HeaderAt(next_block_offset)->previous_block_offset = block_offset;
    } else {
        this->last_block_offset_ = block_offset;
    }
}

void UserHeap::CoalesceFreeBlock(uint64_t &block_offset) noexcept {
    BlockHeader *header = this->HeaderAt(block_offset);
    uint64_t next_block_offset = this->NextPhysicalOffset(block_offset, *header);
    if (next_block_offset < this->capacity_bytes_) {
        BlockHeader *const next_header = this->HeaderAt(next_block_offset);
        if (next_header->state == OS_USER_HEAP_BLOCK_STATE_FREE) {
            this->RemoveFreeBlock(next_block_offset);
            header->payload_capacity_bytes +=
                sizeof(BlockHeader) + next_header->payload_capacity_bytes;
            this->UpdateNextPreviousOffset(block_offset, *header);
        }
    }

    if (header->previous_block_offset != OS_USER_HEAP_INVALID_OFFSET) {
        const uint64_t previous_block_offset = header->previous_block_offset;
        BlockHeader *const previous_header = this->HeaderAt(previous_block_offset);
        if (previous_header->state == OS_USER_HEAP_BLOCK_STATE_FREE) {
            this->RemoveFreeBlock(block_offset);
            previous_header->payload_capacity_bytes +=
                sizeof(BlockHeader) + header->payload_capacity_bytes;
            this->UpdateNextPreviousOffset(previous_block_offset, *previous_header);
            block_offset = previous_block_offset;
        }
    }
}

}

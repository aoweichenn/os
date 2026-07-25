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

[[nodiscard]] uint64_t AlignUpToPage(const uint64_t address) noexcept {
    return (address + OS_KERNEL_FRAME_ALLOCATOR_PAGE_MASK) & ~OS_KERNEL_FRAME_ALLOCATOR_PAGE_MASK;
}

[[nodiscard]] uint64_t AlignDownToPage(const uint64_t address) noexcept {
    return address & ~OS_KERNEL_FRAME_ALLOCATOR_PAGE_MASK;
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

PhysicalFrameAllocator::PhysicalFrameAllocator() noexcept
    : state_storage_{nullptr}, state_storage_size_bytes_{0ULL}, managed_frame_count_{0ULL},
      free_frame_count_{0ULL}, allocated_frame_count_{0ULL}, reserved_frame_count_{0ULL},
      next_search_frame_index_{0ULL} {}

PhysicalFrameAllocator::PhysicalFrameAllocator(uint8_t *state_storage,
                                               const uint64_t state_storage_size_bytes) noexcept
    : state_storage_{state_storage}, state_storage_size_bytes_{state_storage_size_bytes},
      managed_frame_count_{0ULL}, free_frame_count_{0ULL}, allocated_frame_count_{0ULL},
      reserved_frame_count_{0ULL}, next_search_frame_index_{0ULL} {}

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
PhysicalFrameAllocator::Initialize(const PhysicalMemoryMapEntry *entries,
                                   const uint64_t entry_count,
                                   const uint64_t managed_limit_address) noexcept {
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

PhysicalFrameAllocatorStatus
PhysicalFrameAllocator::ReserveRange(const uint64_t begin_address,
                                     const uint64_t length_bytes) noexcept {
    if (!this->IsInitialized()) {
        return PhysicalFrameAllocatorStatus::NotInitialized;
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

PhysicalFrameAllocatorStatus PhysicalFrameAllocator::Release(const PhysicalFrame frame) noexcept {
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

PhysicalFrameAllocatorStatistics PhysicalFrameAllocator::Statistics() const noexcept {
    return PhysicalFrameAllocatorStatistics{
        .managed_frame_count = this->managed_frame_count_,
        .free_frame_count = this->free_frame_count_,
        .allocated_frame_count = this->allocated_frame_count_,
        .reserved_frame_count = this->reserved_frame_count_,
    };
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

bool PhysicalFrameAllocator::IsInitialized() const noexcept {
    return this->managed_frame_count_ != OS_KERNEL_FRAME_ALLOCATOR_EMPTY_VALUE;
}

}

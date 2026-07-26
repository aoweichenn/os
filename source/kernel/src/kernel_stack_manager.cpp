#include "os/kernel/kernel_stack_manager.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_STACK_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_STACK_SINGLE_UNIT = 1ULL;
constexpr uint64_t OS_KERNEL_STACK_ALIGNMENT_PAGE_COUNT = OS_KERNEL_STACK_SINGLE_UNIT;
constexpr uint64_t OS_KERNEL_STACK_VALUES_PER_PAGE =
    OS_KERNEL_MEMORY_PAGE_SIZE_BYTES / sizeof(uint64_t);

[[nodiscard]] bool PermissionsMatchKernelStack(const PagePermissions permissions) noexcept {
    return permissions.writable && !permissions.executable && !permissions.user_accessible &&
           !permissions.cache_disabled;
}

[[nodiscard]] bool IsRangeLayoutValid(const KernelStack &stack) noexcept {
    return stack.virtual_range.begin_address != OS_KERNEL_STACK_EMPTY_VALUE &&
           stack.virtual_range.page_count == OS_KERNEL_STACK_RANGE_PAGE_COUNT &&
           (stack.virtual_range.begin_address &
            (OS_KERNEL_MEMORY_PAGE_SIZE_BYTES - OS_KERNEL_STACK_SINGLE_UNIT)) ==
               OS_KERNEL_STACK_EMPTY_VALUE &&
           stack.virtual_range.begin_address <= UINT64_MAX - OS_KERNEL_STACK_RANGE_SIZE_BYTES &&
           IsCanonicalVirtualAddress(stack.virtual_range.begin_address) &&
           IsCanonicalVirtualAddress(stack.virtual_range.begin_address +
                                     OS_KERNEL_STACK_RANGE_SIZE_BYTES -
                                     OS_KERNEL_STACK_SINGLE_UNIT);
}

}

uint64_t KernelStackLowerGuardAddress(const KernelStack &stack) noexcept {
    return IsRangeLayoutValid(stack) ? stack.virtual_range.begin_address
                                     : OS_KERNEL_STACK_EMPTY_VALUE;
}

uint64_t KernelStackMappedBeginAddress(const KernelStack &stack) noexcept {
    return IsRangeLayoutValid(stack)
               ? stack.virtual_range.begin_address +
                     OS_KERNEL_STACK_LOWER_GUARD_PAGE_COUNT * OS_KERNEL_MEMORY_PAGE_SIZE_BYTES
               : OS_KERNEL_STACK_EMPTY_VALUE;
}

uint64_t KernelStackTopAddress(const KernelStack &stack) noexcept {
    const uint64_t mapped_begin_address = KernelStackMappedBeginAddress(stack);
    return mapped_begin_address != OS_KERNEL_STACK_EMPTY_VALUE
               ? mapped_begin_address + OS_KERNEL_STACK_SIZE_BYTES
               : OS_KERNEL_STACK_EMPTY_VALUE;
}

uint64_t KernelStackUpperGuardAddress(const KernelStack &stack) noexcept {
    return KernelStackTopAddress(stack);
}

KernelStackManager::KernelStackManager(PhysicalFrameAllocator &frame_allocator,
                                       KernelVirtualAddressAllocator &virtual_address_allocator,
                                       PageTableManager &page_table_manager,
                                       const KernelStackMemoryAccess memory_access) noexcept
    : frame_allocator_{&frame_allocator}, virtual_address_allocator_{&virtual_address_allocator},
      page_table_manager_{&page_table_manager}, memory_access_{memory_access}, stacks_{nullptr},
      slot_capacity_{OS_KERNEL_STACK_EMPTY_VALUE}, active_stack_count_{OS_KERNEL_STACK_EMPTY_VALUE},
      active_mapped_page_count_{OS_KERNEL_STACK_EMPTY_VALUE},
      successful_creation_count_{OS_KERNEL_STACK_EMPTY_VALUE},
      destruction_count_{OS_KERNEL_STACK_EMPTY_VALUE},
      peak_active_stack_count_{OS_KERNEL_STACK_EMPTY_VALUE},
      peak_active_mapped_page_count_{OS_KERNEL_STACK_EMPTY_VALUE} {}

KernelStackManagerStatus KernelStackManager::Initialize(KernelStack *const stack_storage,
                                                        const uint64_t slot_capacity) noexcept {
    if (this->IsInitialized()) {
        return KernelStackManagerStatus::AlreadyInitialized;
    }
    if (stack_storage == nullptr) {
        return KernelStackManagerStatus::NullStackStorage;
    }
    if (slot_capacity == OS_KERNEL_STACK_EMPTY_VALUE) {
        return KernelStackManagerStatus::EmptySlotCapacity;
    }
    if (slot_capacity > UINT64_MAX / OS_KERNEL_STACK_MAPPED_PAGE_COUNT ||
        slot_capacity > UINT64_MAX / OS_KERNEL_STACK_GUARD_PAGE_COUNT) {
        return KernelStackManagerStatus::InvalidSlotCapacity;
    }
    if (!this->IsMemoryAccessValid()) {
        return KernelStackManagerStatus::InvalidMemoryAccess;
    }
    if (this->frame_allocator_->ValidateBuddy() != PhysicalFrameAllocatorStatus::Succeeded ||
        this->virtual_address_allocator_->Validate() !=
            KernelVirtualAddressAllocatorStatus::Succeeded ||
        this->page_table_manager_->RootPhysicalAddress() == OS_KERNEL_STACK_EMPTY_VALUE) {
        return KernelStackManagerStatus::InvalidDependencyState;
    }

    for (uint64_t slot_index = OS_KERNEL_STACK_EMPTY_VALUE; slot_index < slot_capacity;
         ++slot_index) {
        stack_storage[slot_index] = KernelStack{};
    }
    this->stacks_ = stack_storage;
    this->slot_capacity_ = slot_capacity;
    this->active_stack_count_ = OS_KERNEL_STACK_EMPTY_VALUE;
    this->active_mapped_page_count_ = OS_KERNEL_STACK_EMPTY_VALUE;
    this->successful_creation_count_ = OS_KERNEL_STACK_EMPTY_VALUE;
    this->destruction_count_ = OS_KERNEL_STACK_EMPTY_VALUE;
    this->peak_active_stack_count_ = OS_KERNEL_STACK_EMPTY_VALUE;
    this->peak_active_mapped_page_count_ = OS_KERNEL_STACK_EMPTY_VALUE;
    return KernelStackManagerStatus::Succeeded;
}

KernelStackManagerStatus KernelStackManager::TryCreate(const uint64_t slot_index) noexcept {
    if (!this->IsInitialized()) {
        return KernelStackManagerStatus::NotInitialized;
    }
    if (slot_index >= this->slot_capacity_) {
        return KernelStackManagerStatus::InvalidSlotIndex;
    }
    KernelStack &destination_stack = this->stacks_[slot_index];
    if (destination_stack.active) {
        return KernelStackManagerStatus::SlotAlreadyActive;
    }
    if (!this->IsStackCleared(destination_stack)) {
        return KernelStackManagerStatus::CorruptedState;
    }
    if (this->active_stack_count_ == UINT64_MAX ||
        this->active_mapped_page_count_ > UINT64_MAX - OS_KERNEL_STACK_MAPPED_PAGE_COUNT ||
        this->successful_creation_count_ == UINT64_MAX) {
        return KernelStackManagerStatus::CounterOverflow;
    }

    KernelStack candidate_stack{};
    if (this->virtual_address_allocator_->TryAllocate(
            OS_KERNEL_STACK_RANGE_PAGE_COUNT, OS_KERNEL_STACK_ALIGNMENT_PAGE_COUNT,
            candidate_stack.virtual_range) != KernelVirtualAddressAllocatorStatus::Succeeded) {
        return KernelStackManagerStatus::VirtualAddressAllocationFailed;
    }

    PageMapping ignored_mapping{};
    for (uint64_t page_index = OS_KERNEL_STACK_EMPTY_VALUE;
         page_index < OS_KERNEL_STACK_RANGE_PAGE_COUNT; ++page_index) {
        const uint64_t virtual_address = candidate_stack.virtual_range.begin_address +
                                         page_index * OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
        if (this->page_table_manager_->QueryPage(virtual_address, ignored_mapping) !=
            PageTableStatus::NotMapped) {
            const bool release_succeeded =
                this->virtual_address_allocator_->TryRelease(candidate_stack.virtual_range) ==
                KernelVirtualAddressAllocatorStatus::Succeeded;
            return release_succeeded ? KernelStackManagerStatus::VirtualRangeNotClear
                                     : KernelStackManagerStatus::RollbackFailed;
        }
    }

    const PagePermissions stack_permissions{
        .writable = true,
        .executable = false,
        .user_accessible = false,
        .cache_disabled = false,
    };
    uint64_t allocated_frame_count = OS_KERNEL_STACK_EMPTY_VALUE;
    for (uint64_t data_page_index = OS_KERNEL_STACK_EMPTY_VALUE;
         data_page_index < OS_KERNEL_STACK_MAPPED_PAGE_COUNT; ++data_page_index) {
        PhysicalFrame frame{};
        if (this->frame_allocator_->Allocate(frame) != PhysicalFrameAllocatorStatus::Succeeded) {
            return this->RollbackCreation(candidate_stack, allocated_frame_count)
                       ? KernelStackManagerStatus::FrameAllocationFailed
                       : KernelStackManagerStatus::RollbackFailed;
        }
        candidate_stack.physical_frames[data_page_index] = frame;
        ++allocated_frame_count;
        if (!this->IsPhysicalFrameAccessible(frame)) {
            return this->RollbackCreation(candidate_stack, allocated_frame_count)
                       ? KernelStackManagerStatus::CorruptedState
                       : KernelStackManagerStatus::RollbackFailed;
        }
        this->ZeroPhysicalFrame(frame);
        if (this->page_table_manager_->MapPage(
                this->DataPageVirtualAddress(candidate_stack, data_page_index),
                frame.physical_address, stack_permissions) != PageTableStatus::Succeeded) {
            return this->RollbackCreation(candidate_stack, allocated_frame_count)
                       ? KernelStackManagerStatus::PageMappingFailed
                       : KernelStackManagerStatus::RollbackFailed;
        }
        ++candidate_stack.mapped_page_count;
    }

    candidate_stack.active = true;
    if (this->ValidateStack(candidate_stack) != KernelStackManagerStatus::Succeeded) {
        candidate_stack.active = false;
        return this->RollbackCreation(candidate_stack, allocated_frame_count)
                   ? KernelStackManagerStatus::MappingValidationFailed
                   : KernelStackManagerStatus::RollbackFailed;
    }

    destination_stack = candidate_stack;
    ++this->active_stack_count_;
    this->active_mapped_page_count_ += OS_KERNEL_STACK_MAPPED_PAGE_COUNT;
    ++this->successful_creation_count_;
    if (this->active_stack_count_ > this->peak_active_stack_count_) {
        this->peak_active_stack_count_ = this->active_stack_count_;
    }
    if (this->active_mapped_page_count_ > this->peak_active_mapped_page_count_) {
        this->peak_active_mapped_page_count_ = this->active_mapped_page_count_;
    }
    return KernelStackManagerStatus::Succeeded;
}

KernelStackManagerStatus KernelStackManager::TryDestroy(const uint64_t slot_index) noexcept {
    if (!this->IsInitialized()) {
        return KernelStackManagerStatus::NotInitialized;
    }
    if (slot_index >= this->slot_capacity_) {
        return KernelStackManagerStatus::InvalidSlotIndex;
    }
    KernelStack &stack = this->stacks_[slot_index];
    if (!stack.active) {
        return this->IsStackCleared(stack) ? KernelStackManagerStatus::SlotNotActive
                                           : KernelStackManagerStatus::CorruptedState;
    }
    if (this->ValidateStack(stack) != KernelStackManagerStatus::Succeeded ||
        this->active_stack_count_ == OS_KERNEL_STACK_EMPTY_VALUE ||
        this->active_mapped_page_count_ < OS_KERNEL_STACK_MAPPED_PAGE_COUNT) {
        return KernelStackManagerStatus::CorruptedState;
    }
    if (this->destruction_count_ == UINT64_MAX) {
        return KernelStackManagerStatus::CounterOverflow;
    }

    for (uint64_t remaining_page_count = OS_KERNEL_STACK_MAPPED_PAGE_COUNT;
         remaining_page_count > OS_KERNEL_STACK_EMPTY_VALUE; --remaining_page_count) {
        const uint64_t data_page_index = remaining_page_count - OS_KERNEL_STACK_SINGLE_UNIT;
        if (this->page_table_manager_->UnmapPage(this->DataPageVirtualAddress(
                stack, data_page_index)) != PageTableStatus::Succeeded) {
            return KernelStackManagerStatus::PageUnmappingFailed;
        }
    }
    for (uint64_t remaining_frame_count = OS_KERNEL_STACK_MAPPED_PAGE_COUNT;
         remaining_frame_count > OS_KERNEL_STACK_EMPTY_VALUE; --remaining_frame_count) {
        const uint64_t frame_index = remaining_frame_count - OS_KERNEL_STACK_SINGLE_UNIT;
        this->ZeroPhysicalFrame(stack.physical_frames[frame_index]);
        if (this->frame_allocator_->Release(stack.physical_frames[frame_index]) !=
            PhysicalFrameAllocatorStatus::Succeeded) {
            return KernelStackManagerStatus::FrameReleaseFailed;
        }
    }
    if (this->virtual_address_allocator_->TryRelease(stack.virtual_range) !=
        KernelVirtualAddressAllocatorStatus::Succeeded) {
        return KernelStackManagerStatus::VirtualAddressReleaseFailed;
    }

    stack = KernelStack{};
    --this->active_stack_count_;
    this->active_mapped_page_count_ -= OS_KERNEL_STACK_MAPPED_PAGE_COUNT;
    ++this->destruction_count_;
    return KernelStackManagerStatus::Succeeded;
}

KernelStackManagerStatus KernelStackManager::Read(const uint64_t slot_index,
                                                  KernelStack &stack) const noexcept {
    if (!this->IsInitialized()) {
        return KernelStackManagerStatus::NotInitialized;
    }
    if (slot_index >= this->slot_capacity_) {
        return KernelStackManagerStatus::InvalidSlotIndex;
    }
    if (!this->stacks_[slot_index].active) {
        return this->IsStackCleared(this->stacks_[slot_index])
                   ? KernelStackManagerStatus::SlotNotActive
                   : KernelStackManagerStatus::CorruptedState;
    }
    stack = this->stacks_[slot_index];
    return KernelStackManagerStatus::Succeeded;
}

bool KernelStackManager::Contains(const uint64_t slot_index, const uint64_t address,
                                  const uint64_t length_bytes) const noexcept {
    if (!this->IsInitialized() || slot_index >= this->slot_capacity_ ||
        !this->stacks_[slot_index].active || length_bytes == OS_KERNEL_STACK_EMPTY_VALUE ||
        address > UINT64_MAX - length_bytes) {
        return false;
    }
    const uint64_t stack_begin_address = KernelStackMappedBeginAddress(this->stacks_[slot_index]);
    const uint64_t stack_top_address = KernelStackTopAddress(this->stacks_[slot_index]);
    return stack_begin_address != OS_KERNEL_STACK_EMPTY_VALUE && address >= stack_begin_address &&
           address + length_bytes <= stack_top_address;
}

KernelStackManagerStatus KernelStackManager::Validate() const noexcept {
    if (!this->IsInitialized()) {
        return KernelStackManagerStatus::NotInitialized;
    }
    if (!this->IsMemoryAccessValid() ||
        this->virtual_address_allocator_->Validate() !=
            KernelVirtualAddressAllocatorStatus::Succeeded ||
        this->frame_allocator_->ValidateBuddy() != PhysicalFrameAllocatorStatus::Succeeded ||
        this->active_stack_count_ > this->slot_capacity_ ||
        this->successful_creation_count_ < this->destruction_count_ ||
        this->successful_creation_count_ - this->destruction_count_ != this->active_stack_count_ ||
        this->active_mapped_page_count_ !=
            this->active_stack_count_ * OS_KERNEL_STACK_MAPPED_PAGE_COUNT ||
        this->peak_active_stack_count_ < this->active_stack_count_ ||
        this->peak_active_mapped_page_count_ < this->active_mapped_page_count_) {
        return KernelStackManagerStatus::CorruptedState;
    }

    uint64_t calculated_active_stack_count = OS_KERNEL_STACK_EMPTY_VALUE;
    for (uint64_t slot_index = OS_KERNEL_STACK_EMPTY_VALUE; slot_index < this->slot_capacity_;
         ++slot_index) {
        const KernelStack &stack = this->stacks_[slot_index];
        if (!stack.active) {
            if (!this->IsStackCleared(stack)) {
                return KernelStackManagerStatus::CorruptedState;
            }
            continue;
        }
        if (this->ValidateStack(stack) != KernelStackManagerStatus::Succeeded) {
            return KernelStackManagerStatus::CorruptedState;
        }
        ++calculated_active_stack_count;
        for (uint64_t previous_slot_index = OS_KERNEL_STACK_EMPTY_VALUE;
             previous_slot_index < slot_index; ++previous_slot_index) {
            const KernelStack &previous_stack = this->stacks_[previous_slot_index];
            if (!previous_stack.active) {
                continue;
            }
            for (uint64_t frame_index = OS_KERNEL_STACK_EMPTY_VALUE;
                 frame_index < OS_KERNEL_STACK_MAPPED_PAGE_COUNT; ++frame_index) {
                for (uint64_t previous_frame_index = OS_KERNEL_STACK_EMPTY_VALUE;
                     previous_frame_index < OS_KERNEL_STACK_MAPPED_PAGE_COUNT;
                     ++previous_frame_index) {
                    if (stack.physical_frames[frame_index].physical_address ==
                        previous_stack.physical_frames[previous_frame_index].physical_address) {
                        return KernelStackManagerStatus::CorruptedState;
                    }
                }
            }
        }
    }
    return calculated_active_stack_count == this->active_stack_count_
               ? KernelStackManagerStatus::Succeeded
               : KernelStackManagerStatus::CorruptedState;
}

KernelStackManagerStatistics KernelStackManager::Statistics() const noexcept {
    if (!this->IsInitialized()) {
        return KernelStackManagerStatistics{};
    }
    return KernelStackManagerStatistics{
        .slot_capacity = this->slot_capacity_,
        .active_stack_count = this->active_stack_count_,
        .active_mapped_page_count = this->active_mapped_page_count_,
        .active_guard_page_count = this->active_stack_count_ * OS_KERNEL_STACK_GUARD_PAGE_COUNT,
        .successful_creation_count = this->successful_creation_count_,
        .destruction_count = this->destruction_count_,
        .peak_active_stack_count = this->peak_active_stack_count_,
        .peak_active_mapped_page_count = this->peak_active_mapped_page_count_,
    };
}

bool KernelStackManager::IsInitialized() const noexcept { return this->stacks_ != nullptr; }

bool KernelStackManager::IsMemoryAccessValid() const noexcept {
    return this->memory_access_.physical_memory_virtual_base != OS_KERNEL_STACK_EMPTY_VALUE &&
           this->memory_access_.maximum_physical_address_exclusive >=
               OS_KERNEL_MEMORY_PAGE_SIZE_BYTES &&
           this->memory_access_.physical_memory_virtual_base <=
               UINT64_MAX - this->memory_access_.maximum_physical_address_exclusive;
}

bool KernelStackManager::IsStackCleared(const KernelStack &stack) const noexcept {
    if (stack.virtual_range.begin_address != OS_KERNEL_STACK_EMPTY_VALUE ||
        stack.virtual_range.page_count != OS_KERNEL_STACK_EMPTY_VALUE ||
        stack.mapped_page_count != OS_KERNEL_STACK_EMPTY_VALUE || stack.active) {
        return false;
    }
    for (uint64_t frame_index = OS_KERNEL_STACK_EMPTY_VALUE;
         frame_index < OS_KERNEL_STACK_MAPPED_PAGE_COUNT; ++frame_index) {
        if (stack.physical_frames[frame_index].physical_address != OS_KERNEL_STACK_EMPTY_VALUE) {
            return false;
        }
    }
    return true;
}

bool KernelStackManager::IsPhysicalFrameAccessible(const PhysicalFrame frame) const noexcept {
    return frame.physical_address < this->memory_access_.maximum_physical_address_exclusive &&
           frame.physical_address <= this->memory_access_.maximum_physical_address_exclusive -
                                         OS_KERNEL_MEMORY_PAGE_SIZE_BYTES &&
           (frame.physical_address & (OS_KERNEL_MEMORY_PAGE_SIZE_BYTES -
                                      OS_KERNEL_STACK_SINGLE_UNIT)) == OS_KERNEL_STACK_EMPTY_VALUE;
}

KernelStackManagerStatus
KernelStackManager::ValidateStack(const KernelStack &stack) const noexcept {
    if (!stack.active || !IsRangeLayoutValid(stack) ||
        stack.mapped_page_count != OS_KERNEL_STACK_MAPPED_PAGE_COUNT ||
        !this->virtual_address_allocator_->OwnsAllocation(stack.virtual_range)) {
        return KernelStackManagerStatus::MappingValidationFailed;
    }
    PageMapping mapping{};
    if (this->page_table_manager_->QueryPage(KernelStackLowerGuardAddress(stack), mapping) !=
            PageTableStatus::NotMapped ||
        this->page_table_manager_->QueryPage(KernelStackUpperGuardAddress(stack), mapping) !=
            PageTableStatus::NotMapped) {
        return KernelStackManagerStatus::MappingValidationFailed;
    }
    for (uint64_t data_page_index = OS_KERNEL_STACK_EMPTY_VALUE;
         data_page_index < OS_KERNEL_STACK_MAPPED_PAGE_COUNT; ++data_page_index) {
        const PhysicalFrame frame = stack.physical_frames[data_page_index];
        if (!this->IsPhysicalFrameAccessible(frame) ||
            !this->frame_allocator_->OwnsAllocation(frame) ||
            this->page_table_manager_->QueryPage(
                this->DataPageVirtualAddress(stack, data_page_index), mapping) !=
                PageTableStatus::Succeeded ||
            mapping.physical_address != frame.physical_address ||
            mapping.page_size_bytes != OS_KERNEL_MEMORY_PAGE_SIZE_BYTES ||
            !PermissionsMatchKernelStack(mapping.permissions)) {
            return KernelStackManagerStatus::MappingValidationFailed;
        }
        for (uint64_t previous_frame_index = OS_KERNEL_STACK_EMPTY_VALUE;
             previous_frame_index < data_page_index; ++previous_frame_index) {
            if (frame.physical_address ==
                stack.physical_frames[previous_frame_index].physical_address) {
                return KernelStackManagerStatus::MappingValidationFailed;
            }
        }
    }
    return KernelStackManagerStatus::Succeeded;
}

uint64_t KernelStackManager::DataPageVirtualAddress(const KernelStack &stack,
                                                    const uint64_t data_page_index) const noexcept {
    return KernelStackMappedBeginAddress(stack) +
           data_page_index * OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
}

void KernelStackManager::ZeroPhysicalFrame(const PhysicalFrame frame) const noexcept {
    volatile uint64_t *const values = reinterpret_cast<volatile uint64_t *>(
        this->memory_access_.physical_memory_virtual_base + frame.physical_address);
    for (uint64_t value_index = OS_KERNEL_STACK_EMPTY_VALUE;
         value_index < OS_KERNEL_STACK_VALUES_PER_PAGE; ++value_index) {
        values[value_index] = OS_KERNEL_STACK_EMPTY_VALUE;
    }
}

bool KernelStackManager::RollbackCreation(KernelStack &stack,
                                          const uint64_t allocated_frame_count) noexcept {
    bool rollback_succeeded = true;
    for (uint64_t remaining_page_count = stack.mapped_page_count;
         remaining_page_count > OS_KERNEL_STACK_EMPTY_VALUE; --remaining_page_count) {
        const uint64_t data_page_index = remaining_page_count - OS_KERNEL_STACK_SINGLE_UNIT;
        rollback_succeeded = this->page_table_manager_->UnmapPage(this->DataPageVirtualAddress(
                                 stack, data_page_index)) == PageTableStatus::Succeeded &&
                             rollback_succeeded;
    }
    for (uint64_t remaining_frame_count = allocated_frame_count;
         remaining_frame_count > OS_KERNEL_STACK_EMPTY_VALUE; --remaining_frame_count) {
        const uint64_t frame_index = remaining_frame_count - OS_KERNEL_STACK_SINGLE_UNIT;
        if (this->IsPhysicalFrameAccessible(stack.physical_frames[frame_index])) {
            this->ZeroPhysicalFrame(stack.physical_frames[frame_index]);
        }
        rollback_succeeded = this->frame_allocator_->Release(stack.physical_frames[frame_index]) ==
                                 PhysicalFrameAllocatorStatus::Succeeded &&
                             rollback_succeeded;
    }
    rollback_succeeded = this->virtual_address_allocator_->TryRelease(stack.virtual_range) ==
                             KernelVirtualAddressAllocatorStatus::Succeeded &&
                         rollback_succeeded;
    stack = KernelStack{};
    return rollback_succeeded;
}

}

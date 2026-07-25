#include "os/kernel/page_table.hpp"

#include "os/kernel/processor.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_PAGE_TABLE_PRESENT_BIT = 0x0000000000000001ULL;
constexpr uint64_t OS_KERNEL_PAGE_TABLE_WRITABLE_BIT = 0x0000000000000002ULL;
constexpr uint64_t OS_KERNEL_PAGE_TABLE_USER_BIT = 0x0000000000000004ULL;
constexpr uint64_t OS_KERNEL_PAGE_TABLE_LARGE_PAGE_BIT = 0x0000000000000080ULL;
constexpr uint64_t OS_KERNEL_PAGE_TABLE_PHYSICAL_ADDRESS_MASK = 0x000FFFFFFFFFF000ULL;
constexpr uint64_t OS_KERNEL_PAGE_TABLE_LARGE_PAGE_PHYSICAL_ADDRESS_MASK = 0x000FFFFFFFE00000ULL;
constexpr uint64_t OS_KERNEL_PAGE_TABLE_PAGE_MASK = OS_KERNEL_MEMORY_PAGE_SIZE_BYTES - 1ULL;
constexpr uint64_t OS_KERNEL_PAGE_TABLE_LARGE_PAGE_MASK =
    OS_KERNEL_PAGE_TABLE_LARGE_PAGE_SIZE_BYTES - 1ULL;
constexpr uint64_t OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY = 0ULL;
constexpr uint64_t OS_KERNEL_PAGE_TABLE_NON_LEAF_LEVEL_COUNT = 3ULL;
constexpr uint64_t OS_KERNEL_PAGE_TABLE_LEVEL4_LOW_KERNEL_INDEX = 0ULL;
constexpr uint64_t OS_KERNEL_PAGE_TABLE_LEVEL4_USER_STACK_INDEX = 255ULL;
constexpr uint64_t OS_KERNEL_PAGE_TABLE_LEVEL3_USER_PROGRAM_INDEX = 1ULL;
constexpr uint64_t OS_KERNEL_PAGE_TABLE_LEVEL3_NUMBER = 3ULL;
constexpr uint64_t OS_KERNEL_PAGE_TABLE_LEVEL2_NUMBER = 2ULL;
constexpr uint64_t OS_KERNEL_PAGE_TABLE_LEVEL1_NUMBER = 1ULL;

}

PageTableManager::PageTableManager(PhysicalFrameAllocator &frame_allocator,
                                   const PageTableMemoryAccess memory_access) noexcept
    : frame_allocator_{&frame_allocator}, root_physical_address_{0ULL},
      memory_access_{memory_access} {}

PageTableManager::PageTableManager(PhysicalFrameAllocator &frame_allocator,
                                   const uint64_t root_physical_address,
                                   const PageTableMemoryAccess memory_access) noexcept
    : frame_allocator_{&frame_allocator}, root_physical_address_{root_physical_address},
      memory_access_{memory_access} {}

PageTableStatus PageTableManager::Initialize() noexcept {
    if (this->SetMemoryAccess(this->memory_access_) != PageTableStatus::Succeeded) {
        return PageTableStatus::InvalidMemoryAccess;
    }
    uint64_t root_physical_address = 0ULL;
    const PageTableStatus status = this->AllocateTable(root_physical_address);
    if (status != PageTableStatus::Succeeded) {
        return status;
    }
    this->root_physical_address_ = root_physical_address;
    return PageTableStatus::Succeeded;
}

PageTableStatus
PageTableManager::InitializeProcessRoot(const uint64_t template_root_physical_address) noexcept {
    if (this->SetMemoryAccess(this->memory_access_) != PageTableStatus::Succeeded) {
        return PageTableStatus::InvalidMemoryAccess;
    }
    if (template_root_physical_address == 0ULL ||
        (template_root_physical_address & OS_KERNEL_PAGE_TABLE_PAGE_MASK) != 0ULL) {
        return PageTableStatus::TemplateRootInvalid;
    }
    uint64_t *const template_root = this->TableAtPhysicalAddress(template_root_physical_address);
    const uint64_t template_low_entry = template_root[OS_KERNEL_PAGE_TABLE_LEVEL4_LOW_KERNEL_INDEX];
    if ((template_low_entry & OS_KERNEL_PAGE_TABLE_PRESENT_BIT) == 0ULL) {
        return PageTableStatus::TemplateRootInvalid;
    }
    if ((template_low_entry & OS_KERNEL_PAGE_TABLE_LARGE_PAGE_BIT) != 0ULL) {
        return PageTableStatus::UnexpectedLargePage;
    }

    uint64_t process_root_physical_address = 0ULL;
    PageTableStatus status = this->AllocateTable(process_root_physical_address);
    if (status != PageTableStatus::Succeeded) {
        return status;
    }
    uint64_t cloned_low_level3_physical_address = 0ULL;
    status = this->AllocateTable(cloned_low_level3_physical_address);
    if (status != PageTableStatus::Succeeded) {
        if (this->frame_allocator_->Release(
                PhysicalFrame{.physical_address = process_root_physical_address}) !=
            PhysicalFrameAllocatorStatus::Succeeded) {
            return PageTableStatus::FrameReleaseFailed;
        }
        return status;
    }

    uint64_t *const process_root = this->TableAtPhysicalAddress(process_root_physical_address);
    for (uint64_t entry_index = 0ULL; entry_index < OS_KERNEL_PAGE_TABLE_ENTRY_COUNT;
         ++entry_index) {
        process_root[entry_index] = template_root[entry_index];
    }
    process_root[OS_KERNEL_PAGE_TABLE_LEVEL4_USER_STACK_INDEX] = OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY;

    const uint64_t template_low_level3_physical_address =
        template_low_entry & OS_KERNEL_PAGE_TABLE_PHYSICAL_ADDRESS_MASK;
    uint64_t *const template_low_level3 =
        this->TableAtPhysicalAddress(template_low_level3_physical_address);
    uint64_t *const cloned_low_level3 =
        this->TableAtPhysicalAddress(cloned_low_level3_physical_address);
    for (uint64_t entry_index = 0ULL; entry_index < OS_KERNEL_PAGE_TABLE_ENTRY_COUNT;
         ++entry_index) {
        cloned_low_level3[entry_index] = template_low_level3[entry_index];
    }
    cloned_low_level3[OS_KERNEL_PAGE_TABLE_LEVEL3_USER_PROGRAM_INDEX] =
        OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY;
    process_root[OS_KERNEL_PAGE_TABLE_LEVEL4_LOW_KERNEL_INDEX] =
        (template_low_entry & ~OS_KERNEL_PAGE_TABLE_PHYSICAL_ADDRESS_MASK) |
        cloned_low_level3_physical_address;
    this->root_physical_address_ = process_root_physical_address;
    return PageTableStatus::Succeeded;
}

PageTableStatus PageTableManager::ReleaseProcessRoot() noexcept {
    if (this->root_physical_address_ == 0ULL) {
        return PageTableStatus::NotInitialized;
    }
    uint64_t *const process_root = this->TableAtPhysicalAddress(this->root_physical_address_);
    const uint64_t low_level3_entry = process_root[OS_KERNEL_PAGE_TABLE_LEVEL4_LOW_KERNEL_INDEX];
    if ((low_level3_entry & OS_KERNEL_PAGE_TABLE_PRESENT_BIT) == 0ULL ||
        (low_level3_entry & OS_KERNEL_PAGE_TABLE_LARGE_PAGE_BIT) != 0ULL) {
        return PageTableStatus::TemplateRootInvalid;
    }
    const uint64_t low_level3_physical_address =
        low_level3_entry & OS_KERNEL_PAGE_TABLE_PHYSICAL_ADDRESS_MASK;
    uint64_t *const low_level3 = this->TableAtPhysicalAddress(low_level3_physical_address);
    const uint64_t user_program_entry = low_level3[OS_KERNEL_PAGE_TABLE_LEVEL3_USER_PROGRAM_INDEX];
    if ((user_program_entry & OS_KERNEL_PAGE_TABLE_PRESENT_BIT) != 0ULL) {
        if ((user_program_entry & OS_KERNEL_PAGE_TABLE_LARGE_PAGE_BIT) != 0ULL) {
            return PageTableStatus::UnexpectedLargePage;
        }
        const PageTableStatus program_release_status =
            this->ReleaseOwnedTable(user_program_entry & OS_KERNEL_PAGE_TABLE_PHYSICAL_ADDRESS_MASK,
                                    OS_KERNEL_PAGE_TABLE_LEVEL2_NUMBER);
        if (program_release_status != PageTableStatus::Succeeded) {
            return program_release_status;
        }
    }

    const uint64_t user_stack_entry = process_root[OS_KERNEL_PAGE_TABLE_LEVEL4_USER_STACK_INDEX];
    if ((user_stack_entry & OS_KERNEL_PAGE_TABLE_PRESENT_BIT) != 0ULL) {
        if ((user_stack_entry & OS_KERNEL_PAGE_TABLE_LARGE_PAGE_BIT) != 0ULL) {
            return PageTableStatus::UnexpectedLargePage;
        }
        const PageTableStatus stack_release_status =
            this->ReleaseOwnedTable(user_stack_entry & OS_KERNEL_PAGE_TABLE_PHYSICAL_ADDRESS_MASK,
                                    OS_KERNEL_PAGE_TABLE_LEVEL3_NUMBER);
        if (stack_release_status != PageTableStatus::Succeeded) {
            return stack_release_status;
        }
    }

    if (this->frame_allocator_->Release(
            PhysicalFrame{.physical_address = low_level3_physical_address}) !=
            PhysicalFrameAllocatorStatus::Succeeded ||
        this->frame_allocator_->Release(
            PhysicalFrame{.physical_address = this->root_physical_address_}) !=
            PhysicalFrameAllocatorStatus::Succeeded) {
        return PageTableStatus::FrameReleaseFailed;
    }
    this->root_physical_address_ = 0ULL;
    return PageTableStatus::Succeeded;
}

PageTableStatus PageTableManager::MapPage(const uint64_t virtual_address,
                                          const uint64_t physical_address,
                                          const PagePermissions permissions) noexcept {
    if (this->root_physical_address_ == 0ULL) {
        return PageTableStatus::NotInitialized;
    }
    if (!IsCanonicalVirtualAddress(virtual_address)) {
        return PageTableStatus::InvalidVirtualAddress;
    }
    if ((virtual_address & OS_KERNEL_PAGE_TABLE_PAGE_MASK) != 0ULL) {
        return PageTableStatus::InvalidAlignment;
    }
    if (!this->IsPhysicalAddressValid(physical_address, OS_KERNEL_MEMORY_PAGE_SIZE_BYTES)) {
        return PageTableStatus::InvalidPhysicalAddress;
    }

    const PageTableIndices indices = CalculatePageTableIndices(virtual_address);
    uint64_t *level4 = this->TableAtPhysicalAddress(this->root_physical_address_);
    uint64_t level3_physical_address = 0ULL;
    PageTableStatus status = this->EnsureNextTable(
        level4[indices.level4], permissions.user_accessible, level3_physical_address);
    if (status != PageTableStatus::Succeeded) {
        return status;
    }
    uint64_t *level3 = this->TableAtPhysicalAddress(level3_physical_address);
    uint64_t level2_physical_address = 0ULL;
    status = this->EnsureNextTable(level3[indices.level3], permissions.user_accessible,
                                   level2_physical_address);
    if (status != PageTableStatus::Succeeded) {
        return status;
    }
    uint64_t *level2 = this->TableAtPhysicalAddress(level2_physical_address);
    uint64_t level1_physical_address = 0ULL;
    status = this->EnsureNextTable(level2[indices.level2], permissions.user_accessible,
                                   level1_physical_address);
    if (status != PageTableStatus::Succeeded) {
        return status;
    }
    uint64_t *level1 = this->TableAtPhysicalAddress(level1_physical_address);
    uint64_t &leaf_entry = level1[indices.level1];
    if ((leaf_entry & OS_KERNEL_PAGE_TABLE_PRESENT_BIT) != 0ULL) {
        return PageTableStatus::AlreadyMapped;
    }
    leaf_entry = EncodePageTableLeafEntry(physical_address, permissions);
    if (this->memory_access_.invalidate_active_mappings) {
        InvalidatePage(virtual_address);
    }
    return PageTableStatus::Succeeded;
}

PageTableStatus PageTableManager::MapLargePage(const uint64_t virtual_address,
                                               const uint64_t physical_address,
                                               const PagePermissions permissions) noexcept {
    if (this->root_physical_address_ == OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
        return PageTableStatus::NotInitialized;
    }
    if (!IsCanonicalVirtualAddress(virtual_address)) {
        return PageTableStatus::InvalidVirtualAddress;
    }
    if ((virtual_address & OS_KERNEL_PAGE_TABLE_LARGE_PAGE_MASK) !=
        OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
        return PageTableStatus::InvalidAlignment;
    }
    if (!this->IsPhysicalAddressValid(physical_address,
                                      OS_KERNEL_PAGE_TABLE_LARGE_PAGE_SIZE_BYTES)) {
        return PageTableStatus::InvalidPhysicalAddress;
    }

    const PageTableIndices indices = CalculatePageTableIndices(virtual_address);
    uint64_t *level4 = this->TableAtPhysicalAddress(this->root_physical_address_);
    uint64_t level3_physical_address = OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY;
    PageTableStatus status = this->EnsureNextTable(
        level4[indices.level4], permissions.user_accessible, level3_physical_address);
    if (status != PageTableStatus::Succeeded) {
        return status;
    }
    uint64_t *level3 = this->TableAtPhysicalAddress(level3_physical_address);
    uint64_t level2_physical_address = OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY;
    status = this->EnsureNextTable(level3[indices.level3], permissions.user_accessible,
                                   level2_physical_address);
    if (status != PageTableStatus::Succeeded) {
        return status;
    }
    uint64_t *level2 = this->TableAtPhysicalAddress(level2_physical_address);
    uint64_t &leaf_entry = level2[indices.level2];
    if ((leaf_entry & OS_KERNEL_PAGE_TABLE_PRESENT_BIT) != OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
        return PageTableStatus::AlreadyMapped;
    }
    leaf_entry = EncodePageTableLeafEntry(physical_address, permissions) |
                 OS_KERNEL_PAGE_TABLE_LARGE_PAGE_BIT;
    if (this->memory_access_.invalidate_active_mappings) {
        InvalidatePage(virtual_address);
    }
    return PageTableStatus::Succeeded;
}

PageTableStatus PageTableManager::UnmapPage(const uint64_t virtual_address) noexcept {
    if ((virtual_address & OS_KERNEL_PAGE_TABLE_PAGE_MASK) != 0ULL) {
        return PageTableStatus::InvalidAlignment;
    }
    uint64_t *leaf_entry = nullptr;
    const PageTableStatus status = this->WalkToLeaf(virtual_address, leaf_entry);
    if (status != PageTableStatus::Succeeded) {
        return status;
    }
    if ((*leaf_entry & OS_KERNEL_PAGE_TABLE_PRESENT_BIT) == 0ULL) {
        return PageTableStatus::NotMapped;
    }
    *leaf_entry = OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY;
    if (this->memory_access_.invalidate_active_mappings) {
        InvalidatePage(virtual_address);
    }
    return PageTableStatus::Succeeded;
}

PageTableStatus PageTableManager::QueryPage(const uint64_t virtual_address,
                                            PageMapping &mapping) const noexcept {
    if (this->root_physical_address_ == OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
        return PageTableStatus::NotInitialized;
    }
    if (!IsCanonicalVirtualAddress(virtual_address)) {
        return PageTableStatus::InvalidVirtualAddress;
    }
    const PageTableIndices indices = CalculatePageTableIndices(virtual_address);
    uint64_t *const level4 = this->TableAtPhysicalAddress(this->root_physical_address_);
    const uint64_t level4_entry = level4[indices.level4];
    if ((level4_entry & OS_KERNEL_PAGE_TABLE_PRESENT_BIT) == OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
        return PageTableStatus::NotMapped;
    }
    if ((level4_entry & OS_KERNEL_PAGE_TABLE_LARGE_PAGE_BIT) != OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
        return PageTableStatus::UnexpectedLargePage;
    }
    uint64_t *const level3 =
        this->TableAtPhysicalAddress(level4_entry & OS_KERNEL_PAGE_TABLE_PHYSICAL_ADDRESS_MASK);
    const uint64_t level3_entry = level3[indices.level3];
    if ((level3_entry & OS_KERNEL_PAGE_TABLE_PRESENT_BIT) == OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
        return PageTableStatus::NotMapped;
    }
    if ((level3_entry & OS_KERNEL_PAGE_TABLE_LARGE_PAGE_BIT) != OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
        return PageTableStatus::UnexpectedLargePage;
    }
    uint64_t *const level2 =
        this->TableAtPhysicalAddress(level3_entry & OS_KERNEL_PAGE_TABLE_PHYSICAL_ADDRESS_MASK);
    const uint64_t level2_entry = level2[indices.level2];
    if ((level2_entry & OS_KERNEL_PAGE_TABLE_PRESENT_BIT) == OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
        return PageTableStatus::NotMapped;
    }
    if ((level2_entry & OS_KERNEL_PAGE_TABLE_LARGE_PAGE_BIT) != OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
        mapping = DecodePageTableLeafEntry(level2_entry);
        mapping.physical_address =
            (level2_entry & OS_KERNEL_PAGE_TABLE_LARGE_PAGE_PHYSICAL_ADDRESS_MASK) +
            (virtual_address & OS_KERNEL_PAGE_TABLE_LARGE_PAGE_MASK);
        mapping.page_size_bytes = OS_KERNEL_PAGE_TABLE_LARGE_PAGE_SIZE_BYTES;
        return PageTableStatus::Succeeded;
    }
    uint64_t *const level1 =
        this->TableAtPhysicalAddress(level2_entry & OS_KERNEL_PAGE_TABLE_PHYSICAL_ADDRESS_MASK);
    const uint64_t level1_entry = level1[indices.level1];
    if ((level1_entry & OS_KERNEL_PAGE_TABLE_PRESENT_BIT) == OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
        return PageTableStatus::NotMapped;
    }
    mapping = DecodePageTableLeafEntry(level1_entry);
    return PageTableStatus::Succeeded;
}

uint64_t PageTableManager::RootPhysicalAddress() const noexcept {
    return this->root_physical_address_;
}

PageTableStatus
PageTableManager::SetMemoryAccess(const PageTableMemoryAccess memory_access) noexcept {
    if (!IsPageTableMemoryAccessValid(memory_access)) {
        return PageTableStatus::InvalidMemoryAccess;
    }
    this->memory_access_ = memory_access;
    return PageTableStatus::Succeeded;
}

uint64_t *PageTableManager::TableAtPhysicalAddress(const uint64_t physical_address) const noexcept {
    return reinterpret_cast<uint64_t *>(this->memory_access_.physical_memory_virtual_base +
                                        physical_address);
}

bool PageTableManager::IsPhysicalAddressValid(const uint64_t physical_address,
                                              const uint64_t page_size_bytes) const noexcept {
    return page_size_bytes != OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY &&
           (physical_address & (page_size_bytes - 1ULL)) == OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY &&
           physical_address < this->memory_access_.maximum_physical_address_exclusive &&
           page_size_bytes <=
               this->memory_access_.maximum_physical_address_exclusive - physical_address &&
           (physical_address & ~OS_KERNEL_PAGE_TABLE_PHYSICAL_ADDRESS_MASK) ==
               OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY;
}

PageTableStatus PageTableManager::AllocateTable(uint64_t &physical_address) noexcept {
    PhysicalFrame frame{};
    if (this->frame_allocator_->AllocateInRange(
            OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY,
            this->memory_access_.allocation_maximum_physical_address_exclusive,
            frame) != PhysicalFrameAllocatorStatus::Succeeded) {
        return PageTableStatus::FrameAllocationFailed;
    }
    uint64_t *table = this->TableAtPhysicalAddress(frame.physical_address);
    for (uint64_t entry_index = 0ULL; entry_index < OS_KERNEL_PAGE_TABLE_ENTRY_COUNT;
         ++entry_index) {
        table[entry_index] = OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY;
    }
    physical_address = frame.physical_address;
    return PageTableStatus::Succeeded;
}

PageTableStatus PageTableManager::EnsureNextTable(uint64_t &entry, const bool user_accessible,
                                                  uint64_t &physical_address) noexcept {
    if ((entry & OS_KERNEL_PAGE_TABLE_PRESENT_BIT) != 0ULL) {
        if ((entry & OS_KERNEL_PAGE_TABLE_LARGE_PAGE_BIT) != 0ULL) {
            return PageTableStatus::UnexpectedLargePage;
        }
        if (user_accessible) {
            entry |= OS_KERNEL_PAGE_TABLE_USER_BIT;
        }
        physical_address = entry & OS_KERNEL_PAGE_TABLE_PHYSICAL_ADDRESS_MASK;
        return PageTableStatus::Succeeded;
    }

    const PageTableStatus status = this->AllocateTable(physical_address);
    if (status != PageTableStatus::Succeeded) {
        return status;
    }
    entry = physical_address | OS_KERNEL_PAGE_TABLE_PRESENT_BIT | OS_KERNEL_PAGE_TABLE_WRITABLE_BIT;
    if (user_accessible) {
        entry |= OS_KERNEL_PAGE_TABLE_USER_BIT;
    }
    return PageTableStatus::Succeeded;
}

PageTableStatus PageTableManager::WalkToLeaf(const uint64_t virtual_address,
                                             uint64_t *&leaf_entry) const noexcept {
    if (this->root_physical_address_ == 0ULL) {
        return PageTableStatus::NotInitialized;
    }
    if (!IsCanonicalVirtualAddress(virtual_address)) {
        return PageTableStatus::InvalidVirtualAddress;
    }
    const PageTableIndices indices = CalculatePageTableIndices(virtual_address);
    uint64_t *current_table = this->TableAtPhysicalAddress(this->root_physical_address_);
    const uint64_t non_leaf_indices[] = {indices.level4, indices.level3, indices.level2};
    for (uint64_t level_index = 0ULL; level_index < OS_KERNEL_PAGE_TABLE_NON_LEAF_LEVEL_COUNT;
         ++level_index) {
        const uint64_t entry = current_table[non_leaf_indices[level_index]];
        if ((entry & OS_KERNEL_PAGE_TABLE_PRESENT_BIT) == 0ULL) {
            return PageTableStatus::NotMapped;
        }
        if ((entry & OS_KERNEL_PAGE_TABLE_LARGE_PAGE_BIT) != 0ULL) {
            return PageTableStatus::UnexpectedLargePage;
        }
        current_table =
            this->TableAtPhysicalAddress(entry & OS_KERNEL_PAGE_TABLE_PHYSICAL_ADDRESS_MASK);
    }
    leaf_entry = &current_table[indices.level1];
    return PageTableStatus::Succeeded;
}

PageTableStatus PageTableManager::ReleaseOwnedTable(const uint64_t table_physical_address,
                                                    const uint64_t table_level) noexcept {
    if (table_level == 0ULL || table_level > OS_KERNEL_PAGE_TABLE_LEVEL3_NUMBER) {
        return PageTableStatus::TemplateRootInvalid;
    }
    uint64_t *const table = this->TableAtPhysicalAddress(table_physical_address);
    for (uint64_t entry_index = 0ULL; entry_index < OS_KERNEL_PAGE_TABLE_ENTRY_COUNT;
         ++entry_index) {
        const uint64_t entry = table[entry_index];
        if ((entry & OS_KERNEL_PAGE_TABLE_PRESENT_BIT) == 0ULL) {
            continue;
        }
        if (table_level == OS_KERNEL_PAGE_TABLE_LEVEL1_NUMBER) {
            if (this->frame_allocator_->Release(PhysicalFrame{
                    .physical_address = entry & OS_KERNEL_PAGE_TABLE_PHYSICAL_ADDRESS_MASK,
                }) != PhysicalFrameAllocatorStatus::Succeeded) {
                return PageTableStatus::FrameReleaseFailed;
            }
            continue;
        }
        if ((entry & OS_KERNEL_PAGE_TABLE_LARGE_PAGE_BIT) != 0ULL) {
            return PageTableStatus::UnexpectedLargePage;
        }
        const PageTableStatus child_status = this->ReleaseOwnedTable(
            entry & OS_KERNEL_PAGE_TABLE_PHYSICAL_ADDRESS_MASK, table_level - 1ULL);
        if (child_status != PageTableStatus::Succeeded) {
            return child_status;
        }
    }
    return this->frame_allocator_->Release(
               PhysicalFrame{.physical_address = table_physical_address}) ==
                   PhysicalFrameAllocatorStatus::Succeeded
               ? PageTableStatus::Succeeded
               : PageTableStatus::FrameReleaseFailed;
}
}

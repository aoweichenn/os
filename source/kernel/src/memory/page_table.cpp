#include "os/kernel/memory/page_table.hpp"

#include "os/kernel/arch/processor.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_PAGE_TABLE_PRESENT_BIT = 0x0000000000000001ULL;
constexpr uint64_t OS_KERNEL_PAGE_TABLE_WRITABLE_BIT = 0x0000000000000002ULL;
constexpr uint64_t OS_KERNEL_PAGE_TABLE_USER_BIT = 0x0000000000000004ULL;
constexpr uint64_t OS_KERNEL_PAGE_TABLE_ACCESSED_BIT = 0x0000000000000020ULL;
constexpr uint64_t OS_KERNEL_PAGE_TABLE_LARGE_PAGE_BIT = 0x0000000000000080ULL;
constexpr uint64_t OS_KERNEL_PAGE_TABLE_PHYSICAL_ADDRESS_MASK = 0x000FFFFFFFFFF000ULL;
constexpr uint64_t OS_KERNEL_PAGE_TABLE_LARGE_PAGE_PHYSICAL_ADDRESS_MASK = 0x000FFFFFFFE00000ULL;
constexpr uint64_t OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY = 0ULL;
constexpr uint64_t OS_KERNEL_PAGE_TABLE_SINGLE_ENTRY = 1ULL;
constexpr uint64_t OS_KERNEL_PAGE_TABLE_PAGE_MASK =
    OS_KERNEL_MEMORY_PAGE_SIZE_BYTES - OS_KERNEL_PAGE_TABLE_SINGLE_ENTRY;
constexpr uint64_t OS_KERNEL_PAGE_TABLE_LARGE_PAGE_MASK =
    OS_KERNEL_PAGE_TABLE_LARGE_PAGE_SIZE_BYTES - OS_KERNEL_PAGE_TABLE_SINGLE_ENTRY;
constexpr uint64_t OS_KERNEL_PAGE_TABLE_NON_LEAF_LEVEL_COUNT = 3ULL;
constexpr uint64_t OS_KERNEL_PAGE_TABLE_HIERARCHY_LEVEL_COUNT = 4ULL;
constexpr uint64_t OS_KERNEL_PAGE_TABLE_LEVEL4_LOW_KERNEL_INDEX = 0ULL;
constexpr uint64_t OS_KERNEL_PAGE_TABLE_LEVEL4_USER_STACK_INDEX = 255ULL;
constexpr uint64_t OS_KERNEL_PAGE_TABLE_LEVEL3_USER_PROGRAM_INDEX = 1ULL;
constexpr uint64_t OS_KERNEL_PAGE_TABLE_LEVEL3_NUMBER = 3ULL;
constexpr uint64_t OS_KERNEL_PAGE_TABLE_LEVEL2_NUMBER = 2ULL;
constexpr uint64_t OS_KERNEL_PAGE_TABLE_LEVEL1_NUMBER = 1ULL;

[[nodiscard]] bool IsRootKindValid(const PageTableRootKind root_kind) noexcept {
    return root_kind == PageTableRootKind::Exclusive ||
           root_kind == PageTableRootKind::KernelShared || root_kind == PageTableRootKind::Process;
}

[[nodiscard]] bool ContainsTableAddress(const uint64_t *const table_addresses,
                                        const uint64_t table_address_count,
                                        const uint64_t candidate_address) noexcept {
    for (uint64_t address_index = OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY;
         address_index < table_address_count; ++address_index) {
        if (table_addresses[address_index] == candidate_address) {
            return true;
        }
    }
    return false;
}

}

PageTableManager::PageTableManager(PhysicalFrameAllocator &frame_allocator,
                                   const PageTableMemoryAccess memory_access,
                                   const PageTableRootKind root_kind) noexcept
    : frame_allocator_{&frame_allocator}, root_physical_address_{OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY},
      memory_access_{memory_access}, root_kind_{root_kind} {}

PageTableManager::PageTableManager(PhysicalFrameAllocator &frame_allocator,
                                   const uint64_t root_physical_address,
                                   const PageTableMemoryAccess memory_access,
                                   const PageTableRootKind root_kind) noexcept
    : frame_allocator_{&frame_allocator}, root_physical_address_{root_physical_address},
      memory_access_{memory_access}, root_kind_{root_kind} {}

PageTableStatus PageTableManager::Initialize() noexcept {
    if (!IsRootKindValid(this->root_kind_) || this->root_kind_ == PageTableRootKind::Process) {
        return PageTableStatus::InvalidRootKind;
    }
    if (this->root_physical_address_ != OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
        return PageTableStatus::AlreadyInitialized;
    }
    if (this->SetMemoryAccess(this->memory_access_) != PageTableStatus::Succeeded) {
        return PageTableStatus::InvalidMemoryAccess;
    }
    uint64_t root_physical_address = OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY;
    const PageTableStatus status = this->AllocateTable(root_physical_address);
    if (status != PageTableStatus::Succeeded) {
        return status;
    }
    this->root_physical_address_ = root_physical_address;
    return PageTableStatus::Succeeded;
}

PageTableStatus
PageTableManager::InitializeProcessRoot(const uint64_t template_root_physical_address) noexcept {
    if (this->root_kind_ != PageTableRootKind::Process) {
        return PageTableStatus::InvalidRootKind;
    }
    if (this->root_physical_address_ != OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
        return PageTableStatus::AlreadyInitialized;
    }
    if (this->SetMemoryAccess(this->memory_access_) != PageTableStatus::Succeeded) {
        return PageTableStatus::InvalidMemoryAccess;
    }
    if (template_root_physical_address == OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY ||
        !this->IsPhysicalAddressValid(template_root_physical_address,
                                      OS_KERNEL_MEMORY_PAGE_SIZE_BYTES)) {
        return PageTableStatus::TemplateRootInvalid;
    }
    if (!this->frame_allocator_->OwnsAllocation(
            PhysicalFrame{.physical_address = template_root_physical_address})) {
        return PageTableStatus::TableFrameNotOwned;
    }
    uint64_t *const template_root = this->TableAtPhysicalAddress(template_root_physical_address);
    const uint64_t template_low_entry = template_root[OS_KERNEL_PAGE_TABLE_LEVEL4_LOW_KERNEL_INDEX];
    if ((template_low_entry & OS_KERNEL_PAGE_TABLE_PRESENT_BIT) ==
        OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
        return PageTableStatus::TemplateRootInvalid;
    }
    if ((template_low_entry & OS_KERNEL_PAGE_TABLE_LARGE_PAGE_BIT) !=
        OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
        return PageTableStatus::UnexpectedLargePage;
    }
    const uint64_t template_low_level3_physical_address =
        template_low_entry & OS_KERNEL_PAGE_TABLE_PHYSICAL_ADDRESS_MASK;
    if (!this->IsPhysicalAddressValid(template_low_level3_physical_address,
                                      OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) ||
        template_low_level3_physical_address == template_root_physical_address) {
        return PageTableStatus::InvalidTableFrame;
    }
    if (!this->frame_allocator_->OwnsAllocation(
            PhysicalFrame{.physical_address = template_low_level3_physical_address})) {
        return PageTableStatus::TableFrameNotOwned;
    }

    uint64_t process_root_physical_address = OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY;
    PageTableStatus status = this->AllocateTable(process_root_physical_address);
    if (status != PageTableStatus::Succeeded) {
        return status;
    }
    uint64_t cloned_low_level3_physical_address = OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY;
    status = this->AllocateTable(cloned_low_level3_physical_address);
    if (status != PageTableStatus::Succeeded) {
        if (this->ReleaseTableFrame(process_root_physical_address) != PageTableStatus::Succeeded) {
            return PageTableStatus::FrameReleaseFailed;
        }
        return status;
    }

    uint64_t *const process_root = this->TableAtPhysicalAddress(process_root_physical_address);
    // 高半区四级项继续借用内核下级树；低地址三级表必须克隆，才能独立插入用户程序。
    for (uint64_t entry_index = OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY;
         entry_index < OS_KERNEL_PAGE_TABLE_ENTRY_COUNT; ++entry_index) {
        process_root[entry_index] = template_root[entry_index];
    }
    process_root[OS_KERNEL_PAGE_TABLE_LEVEL4_USER_STACK_INDEX] = OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY;

    uint64_t *const template_low_level3 =
        this->TableAtPhysicalAddress(template_low_level3_physical_address);
    uint64_t *const cloned_low_level3 =
        this->TableAtPhysicalAddress(cloned_low_level3_physical_address);
    for (uint64_t entry_index = OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY;
         entry_index < OS_KERNEL_PAGE_TABLE_ENTRY_COUNT; ++entry_index) {
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
    if (this->root_kind_ != PageTableRootKind::Process) {
        return PageTableStatus::InvalidRootKind;
    }
    if (this->root_physical_address_ == OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
        return PageTableStatus::NotInitialized;
    }
    if (!IsPageTableMemoryAccessValid(this->memory_access_)) {
        return PageTableStatus::InvalidMemoryAccess;
    }
    if (!this->IsPhysicalAddressValid(this->root_physical_address_,
                                      OS_KERNEL_MEMORY_PAGE_SIZE_BYTES)) {
        return PageTableStatus::InvalidTableFrame;
    }
    if (!this->frame_allocator_->OwnsAllocation(
            PhysicalFrame{.physical_address = this->root_physical_address_})) {
        return PageTableStatus::TableFrameNotOwned;
    }
    uint64_t *const process_root = this->TableAtPhysicalAddress(this->root_physical_address_);
    const uint64_t low_level3_entry = process_root[OS_KERNEL_PAGE_TABLE_LEVEL4_LOW_KERNEL_INDEX];
    if ((low_level3_entry & OS_KERNEL_PAGE_TABLE_PRESENT_BIT) == OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY ||
        (low_level3_entry & OS_KERNEL_PAGE_TABLE_LARGE_PAGE_BIT) !=
            OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
        return PageTableStatus::TemplateRootInvalid;
    }
    const uint64_t low_level3_physical_address =
        low_level3_entry & OS_KERNEL_PAGE_TABLE_PHYSICAL_ADDRESS_MASK;
    if (!this->IsPhysicalAddressValid(low_level3_physical_address,
                                      OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) ||
        low_level3_physical_address == this->root_physical_address_) {
        return PageTableStatus::InvalidTableFrame;
    }
    if (!this->frame_allocator_->OwnsAllocation(
            PhysicalFrame{.physical_address = low_level3_physical_address})) {
        return PageTableStatus::TableFrameNotOwned;
    }
    uint64_t *const low_level3 = this->TableAtPhysicalAddress(low_level3_physical_address);
    const uint64_t user_program_entry = low_level3[OS_KERNEL_PAGE_TABLE_LEVEL3_USER_PROGRAM_INDEX];
    if ((user_program_entry & OS_KERNEL_PAGE_TABLE_PRESENT_BIT) !=
        OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
        if ((user_program_entry & OS_KERNEL_PAGE_TABLE_LARGE_PAGE_BIT) !=
            OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
            return PageTableStatus::UnexpectedLargePage;
        }
        const uint64_t program_ancestor_table_addresses[] = {
            this->root_physical_address_,
            low_level3_physical_address,
        };
        const PageTableStatus program_release_status = this->ReleaseOwnedTable(
            user_program_entry & OS_KERNEL_PAGE_TABLE_PHYSICAL_ADDRESS_MASK,
            OS_KERNEL_PAGE_TABLE_LEVEL2_NUMBER, program_ancestor_table_addresses,
            sizeof(program_ancestor_table_addresses) / sizeof(program_ancestor_table_addresses[0]));
        if (program_release_status != PageTableStatus::Succeeded) {
            return program_release_status;
        }
        low_level3[OS_KERNEL_PAGE_TABLE_LEVEL3_USER_PROGRAM_INDEX] =
            OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY;
    }

    const uint64_t user_stack_entry = process_root[OS_KERNEL_PAGE_TABLE_LEVEL4_USER_STACK_INDEX];
    if ((user_stack_entry & OS_KERNEL_PAGE_TABLE_PRESENT_BIT) != OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
        if ((user_stack_entry & OS_KERNEL_PAGE_TABLE_LARGE_PAGE_BIT) !=
            OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
            return PageTableStatus::UnexpectedLargePage;
        }
        const uint64_t stack_ancestor_table_addresses[] = {
            this->root_physical_address_,
        };
        const PageTableStatus stack_release_status = this->ReleaseOwnedTable(
            user_stack_entry & OS_KERNEL_PAGE_TABLE_PHYSICAL_ADDRESS_MASK,
            OS_KERNEL_PAGE_TABLE_LEVEL3_NUMBER, stack_ancestor_table_addresses,
            sizeof(stack_ancestor_table_addresses) / sizeof(stack_ancestor_table_addresses[0]));
        if (stack_release_status != PageTableStatus::Succeeded) {
            return stack_release_status;
        }
        process_root[OS_KERNEL_PAGE_TABLE_LEVEL4_USER_STACK_INDEX] =
            OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY;
    }

    process_root[OS_KERNEL_PAGE_TABLE_LEVEL4_LOW_KERNEL_INDEX] = OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY;
    const PageTableStatus low_level3_release_status =
        this->ReleaseTableFrame(low_level3_physical_address);
    if (low_level3_release_status != PageTableStatus::Succeeded) {
        return low_level3_release_status;
    }
    const PageTableStatus root_release_status =
        this->ReleaseTableFrame(this->root_physical_address_);
    if (root_release_status != PageTableStatus::Succeeded) {
        return root_release_status;
    }
    this->root_physical_address_ = OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY;
    return PageTableStatus::Succeeded;
}

PageTableStatus PageTableManager::MapPage(const uint64_t virtual_address,
                                          const uint64_t physical_address,
                                          const PagePermissions permissions) noexcept {
    return this->MapLeaf(virtual_address, physical_address, permissions, false);
}

PageTableStatus PageTableManager::MapLargePage(const uint64_t virtual_address,
                                               const uint64_t physical_address,
                                               const PagePermissions permissions) noexcept {
    return this->MapLeaf(virtual_address, physical_address, permissions, true);
}

PageTableStatus PageTableManager::UnmapPage(const uint64_t virtual_address) noexcept {
    PageTableUnmapResult ignored_result{};
    return this->UnmapPage(virtual_address, ignored_result);
}

PageTableStatus PageTableManager::UnmapPage(const uint64_t virtual_address,
                                            PageTableUnmapResult &result) noexcept {
    if ((virtual_address & OS_KERNEL_PAGE_TABLE_PAGE_MASK) != OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
        return PageTableStatus::InvalidAlignment;
    }
    if (!IsCanonicalVirtualAddress(virtual_address)) {
        return PageTableStatus::InvalidVirtualAddress;
    }
    const PageTableIndices indices = CalculatePageTableIndices(virtual_address);
    if (!this->CanMutateAddress(indices)) {
        return PageTableStatus::SharedBranchMutationDenied;
    }

    PageTableWalkPath path{};
    const PageTableStatus status = this->WalkToLeaf(virtual_address, path);
    if (status != PageTableStatus::Succeeded) {
        return status;
    }
    if ((*path.level1_entry & OS_KERNEL_PAGE_TABLE_PRESENT_BIT) ==
        OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
        return PageTableStatus::NotMapped;
    }

    uint64_t *const level1 = this->TableAtPhysicalAddress(path.level1_physical_address);
    uint64_t *const level2 = this->TableAtPhysicalAddress(path.level2_physical_address);
    uint64_t *const level3 = this->TableAtPhysicalAddress(path.level3_physical_address);
    const bool reclaim_level1 = this->IsTableEmptyExcept(level1, indices.level1);
    const bool reclaim_level2 = reclaim_level1 && this->IsTableEmptyExcept(level2, indices.level2);
    const bool reclaim_level3 = reclaim_level2 && this->CanReclaimLevel3Table(indices) &&
                                this->IsTableEmptyExcept(level3, indices.level3);

    // 先验证全部待释放帧的精确所有权；任何失败都必须发生在页表项改动之前。
    const uint64_t candidate_table_physical_addresses[] = {
        path.level1_physical_address,
        path.level2_physical_address,
        path.level3_physical_address,
    };
    const bool candidate_table_reclaims[] = {
        reclaim_level1,
        reclaim_level2,
        reclaim_level3,
    };
    for (uint64_t candidate_index = OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY;
         candidate_index < OS_KERNEL_PAGE_TABLE_NON_LEAF_LEVEL_COUNT; ++candidate_index) {
        if (candidate_table_reclaims[candidate_index] &&
            !this->frame_allocator_->OwnsAllocation(PhysicalFrame{
                .physical_address = candidate_table_physical_addresses[candidate_index],
            })) {
            return PageTableStatus::TableFrameNotOwned;
        }
    }

    // 从父项断开整条空分支，只对目标虚拟页执行一次 TLB 失效，再由子到父释放。
    *path.level1_entry = OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY;
    if (reclaim_level1) {
        *path.level2_entry = OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY;
    }
    if (reclaim_level2) {
        *path.level3_entry = OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY;
    }
    if (reclaim_level3) {
        *path.level4_entry = OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY;
    }
    if (this->memory_access_.invalidate_active_mappings) {
        InvalidatePage(virtual_address);
    }

    PageTableStatus release_status = PageTableStatus::Succeeded;
    for (uint64_t candidate_index = OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY;
         candidate_index < OS_KERNEL_PAGE_TABLE_NON_LEAF_LEVEL_COUNT; ++candidate_index) {
        if (!candidate_table_reclaims[candidate_index]) {
            continue;
        }
        const PageTableStatus candidate_status =
            this->ReleaseTableFrame(candidate_table_physical_addresses[candidate_index]);
        if (candidate_status != PageTableStatus::Succeeded) {
            release_status = candidate_status;
        }
    }
    if (release_status != PageTableStatus::Succeeded) {
        return release_status;
    }

    const PageTableUnmapResult candidate_result{
        .reclaimed_level1_table_count =
            reclaim_level1 ? OS_KERNEL_PAGE_TABLE_SINGLE_ENTRY : OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY,
        .reclaimed_level2_table_count =
            reclaim_level2 ? OS_KERNEL_PAGE_TABLE_SINGLE_ENTRY : OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY,
        .reclaimed_level3_table_count =
            reclaim_level3 ? OS_KERNEL_PAGE_TABLE_SINGLE_ENTRY : OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY,
        .reclaimed_table_frame_count = static_cast<uint64_t>(reclaim_level1) +
                                       static_cast<uint64_t>(reclaim_level2) +
                                       static_cast<uint64_t>(reclaim_level3),
    };
    result = candidate_result;
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
    if (!IsRootKindValid(this->root_kind_)) {
        return PageTableStatus::InvalidRootKind;
    }
    if (!IsPageTableMemoryAccessValid(this->memory_access_)) {
        return PageTableStatus::InvalidMemoryAccess;
    }
    if (!this->IsPhysicalAddressValid(this->root_physical_address_,
                                      OS_KERNEL_MEMORY_PAGE_SIZE_BYTES)) {
        return PageTableStatus::InvalidTableFrame;
    }
    if (!this->frame_allocator_->OwnsAllocation(
            PhysicalFrame{.physical_address = this->root_physical_address_})) {
        return PageTableStatus::TableFrameNotOwned;
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
    const uint64_t level3_physical_address =
        level4_entry & OS_KERNEL_PAGE_TABLE_PHYSICAL_ADDRESS_MASK;
    if (!this->IsPhysicalAddressValid(level3_physical_address, OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) ||
        level3_physical_address == this->root_physical_address_) {
        return PageTableStatus::InvalidTableFrame;
    }
    if (!this->frame_allocator_->OwnsAllocation(
            PhysicalFrame{.physical_address = level3_physical_address})) {
        return PageTableStatus::TableFrameNotOwned;
    }
    uint64_t *const level3 = this->TableAtPhysicalAddress(level3_physical_address);
    const uint64_t level3_entry = level3[indices.level3];
    if ((level3_entry & OS_KERNEL_PAGE_TABLE_PRESENT_BIT) == OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
        return PageTableStatus::NotMapped;
    }
    if ((level3_entry & OS_KERNEL_PAGE_TABLE_LARGE_PAGE_BIT) != OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
        return PageTableStatus::UnexpectedLargePage;
    }
    const uint64_t level2_physical_address =
        level3_entry & OS_KERNEL_PAGE_TABLE_PHYSICAL_ADDRESS_MASK;
    if (!this->IsPhysicalAddressValid(level2_physical_address, OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) ||
        level2_physical_address == this->root_physical_address_ ||
        level2_physical_address == level3_physical_address) {
        return PageTableStatus::InvalidTableFrame;
    }
    if (!this->frame_allocator_->OwnsAllocation(
            PhysicalFrame{.physical_address = level2_physical_address})) {
        return PageTableStatus::TableFrameNotOwned;
    }
    uint64_t *const level2 = this->TableAtPhysicalAddress(level2_physical_address);
    const uint64_t level2_entry = level2[indices.level2];
    if ((level2_entry & OS_KERNEL_PAGE_TABLE_PRESENT_BIT) == OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
        return PageTableStatus::NotMapped;
    }
    if ((level2_entry & OS_KERNEL_PAGE_TABLE_LARGE_PAGE_BIT) != OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
        const uint64_t large_page_physical_address =
            level2_entry & OS_KERNEL_PAGE_TABLE_LARGE_PAGE_PHYSICAL_ADDRESS_MASK;
        if (!this->IsPhysicalAddressValid(large_page_physical_address,
                                          OS_KERNEL_PAGE_TABLE_LARGE_PAGE_SIZE_BYTES)) {
            return PageTableStatus::InvalidPhysicalAddress;
        }
        PageMapping candidate_mapping = DecodePageTableLeafEntry(level2_entry);
        candidate_mapping.physical_address =
            large_page_physical_address + (virtual_address & OS_KERNEL_PAGE_TABLE_LARGE_PAGE_MASK);
        candidate_mapping.page_size_bytes = OS_KERNEL_PAGE_TABLE_LARGE_PAGE_SIZE_BYTES;
        mapping = candidate_mapping;
        return PageTableStatus::Succeeded;
    }
    const uint64_t level1_physical_address =
        level2_entry & OS_KERNEL_PAGE_TABLE_PHYSICAL_ADDRESS_MASK;
    if (!this->IsPhysicalAddressValid(level1_physical_address, OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) ||
        level1_physical_address == this->root_physical_address_ ||
        level1_physical_address == level3_physical_address ||
        level1_physical_address == level2_physical_address) {
        return PageTableStatus::InvalidTableFrame;
    }
    if (!this->frame_allocator_->OwnsAllocation(
            PhysicalFrame{.physical_address = level1_physical_address})) {
        return PageTableStatus::TableFrameNotOwned;
    }
    uint64_t *const level1 = this->TableAtPhysicalAddress(level1_physical_address);
    const uint64_t level1_entry = level1[indices.level1];
    if ((level1_entry & OS_KERNEL_PAGE_TABLE_PRESENT_BIT) == OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
        return PageTableStatus::NotMapped;
    }
    const uint64_t page_physical_address =
        level1_entry & OS_KERNEL_PAGE_TABLE_PHYSICAL_ADDRESS_MASK;
    if (!this->IsPhysicalAddressValid(page_physical_address, OS_KERNEL_MEMORY_PAGE_SIZE_BYTES)) {
        return PageTableStatus::InvalidPhysicalAddress;
    }
    PageMapping candidate_mapping = DecodePageTableLeafEntry(level1_entry);
    candidate_mapping.physical_address =
        page_physical_address + (virtual_address & OS_KERNEL_PAGE_TABLE_PAGE_MASK);
    mapping = candidate_mapping;
    return PageTableStatus::Succeeded;
}

PageTableStatus
PageTableManager::ReplacePage(const uint64_t virtual_address,
                              const uint64_t physical_address,
                              const PagePermissions permissions) noexcept {
    if (this->root_physical_address_ == OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
        return PageTableStatus::NotInitialized;
    }
    if (!IsCanonicalVirtualAddress(virtual_address)) {
        return PageTableStatus::InvalidVirtualAddress;
    }
    if ((virtual_address & OS_KERNEL_PAGE_TABLE_PAGE_MASK) !=
            OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY ||
        !this->IsPhysicalAddressValid(physical_address,
                                      OS_KERNEL_MEMORY_PAGE_SIZE_BYTES)) {
        return PageTableStatus::InvalidAlignment;
    }
    if (this->root_kind_ != PageTableRootKind::Process ||
        !IsPageTableMemoryAccessValid(this->memory_access_)) {
        return PageTableStatus::InvalidRootKind;
    }
    if (permissions.writable && permissions.copy_on_write) {
        return PageTableStatus::InvalidMemoryAccess;
    }
    if (!this->frame_allocator_->OwnsAllocation(
            PhysicalFrame{.physical_address = physical_address})) {
        return PageTableStatus::TableFrameNotOwned;
    }
    const PageTableIndices indices = CalculatePageTableIndices(virtual_address);
    if (!this->CanMutateAddress(indices)) {
        return PageTableStatus::SharedBranchMutationDenied;
    }
    PageTableWalkPath path{};
    const PageTableStatus walk_status = this->WalkToLeaf(virtual_address, path);
    if (walk_status != PageTableStatus::Succeeded) {
        return walk_status;
    }
    if ((*path.level1_entry & OS_KERNEL_PAGE_TABLE_PRESENT_BIT) ==
        OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
        return PageTableStatus::NotMapped;
    }
    *path.level1_entry = EncodePageTableLeafEntry(physical_address, permissions);
    if (this->memory_access_.invalidate_active_mappings) {
        InvalidatePage(virtual_address);
    }
    return PageTableStatus::Succeeded;
}

PageTableStatus PageTableManager::TestAndClearAccessed(const uint64_t virtual_address,
                                                       PageMapping &mapping,
                                                       bool &accessed) noexcept {
    if ((virtual_address & OS_KERNEL_PAGE_TABLE_PAGE_MASK) != OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
        return PageTableStatus::InvalidAlignment;
    }
    const PageTableIndices indices = CalculatePageTableIndices(virtual_address);
    if (!this->CanMutateAddress(indices)) {
        return PageTableStatus::SharedBranchMutationDenied;
    }
    PageTableWalkPath path{};
    const PageTableStatus walk_status = this->WalkToLeaf(virtual_address, path);
    if (walk_status != PageTableStatus::Succeeded) {
        return walk_status;
    }
    if ((*path.level1_entry & OS_KERNEL_PAGE_TABLE_PRESENT_BIT) ==
        OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
        return PageTableStatus::NotMapped;
    }
    const bool candidate_accessed = (*path.level1_entry & OS_KERNEL_PAGE_TABLE_ACCESSED_BIT) !=
                                    OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY;
    PageMapping candidate_mapping = DecodePageTableLeafEntry(*path.level1_entry);
    candidate_mapping.physical_address =
        *path.level1_entry & OS_KERNEL_PAGE_TABLE_PHYSICAL_ADDRESS_MASK;
    if (candidate_accessed) {
        *path.level1_entry &= ~OS_KERNEL_PAGE_TABLE_ACCESSED_BIT;
        // CR3 切换会刷新非活动地址空间；若调用方正在修改活动根，仍必须立即失效 TLB。
        if (this->memory_access_.invalidate_active_mappings) {
            InvalidatePage(virtual_address);
        }
    }
    mapping = candidate_mapping;
    accessed = candidate_accessed;
    return PageTableStatus::Succeeded;
}

uint64_t PageTableManager::RootPhysicalAddress() const noexcept {
    return this->root_physical_address_;
}

PageTableRootKind PageTableManager::RootKind() const noexcept { return this->root_kind_; }

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
           (physical_address & (page_size_bytes - OS_KERNEL_PAGE_TABLE_SINGLE_ENTRY)) ==
               OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY &&
           physical_address < this->memory_access_.maximum_physical_address_exclusive &&
           page_size_bytes <=
               this->memory_access_.maximum_physical_address_exclusive - physical_address &&
           (physical_address & ~OS_KERNEL_PAGE_TABLE_PHYSICAL_ADDRESS_MASK) ==
               OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY;
}

bool PageTableManager::CanMutateAddress(const PageTableIndices indices) const noexcept {
    if (this->root_kind_ == PageTableRootKind::Exclusive ||
        this->root_kind_ == PageTableRootKind::KernelShared) {
        return true;
    }
    if (this->root_kind_ != PageTableRootKind::Process) {
        return false;
    }
    const bool user_program_branch =
        indices.level4 == OS_KERNEL_PAGE_TABLE_LEVEL4_LOW_KERNEL_INDEX &&
        indices.level3 == OS_KERNEL_PAGE_TABLE_LEVEL3_USER_PROGRAM_INDEX;
    const bool user_stack_branch = indices.level4 == OS_KERNEL_PAGE_TABLE_LEVEL4_USER_STACK_INDEX;
    return user_program_branch || user_stack_branch;
}

bool PageTableManager::CanReclaimLevel3Table(const PageTableIndices indices) const noexcept {
    if (this->root_kind_ == PageTableRootKind::Exclusive) {
        return true;
    }
    return this->root_kind_ == PageTableRootKind::Process &&
           indices.level4 == OS_KERNEL_PAGE_TABLE_LEVEL4_USER_STACK_INDEX;
}

bool PageTableManager::IsTableEmptyExcept(const uint64_t *const table,
                                          const uint64_t excluded_entry_index) const noexcept {
    for (uint64_t entry_index = OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY;
         entry_index < OS_KERNEL_PAGE_TABLE_ENTRY_COUNT; ++entry_index) {
        if (entry_index != excluded_entry_index &&
            table[entry_index] != OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
            return false;
        }
    }
    return true;
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
    for (uint64_t entry_index = OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY;
         entry_index < OS_KERNEL_PAGE_TABLE_ENTRY_COUNT; ++entry_index) {
        table[entry_index] = OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY;
    }
    physical_address = frame.physical_address;
    return PageTableStatus::Succeeded;
}

PageTableStatus PageTableManager::EnsureNextTable(uint64_t &entry, const bool user_accessible,
                                                  uint64_t &physical_address,
                                                  TableMutation &mutation) noexcept {
    mutation = TableMutation{
        .entry = &entry,
        .original_entry = entry,
        .table_physical_address = OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY,
        .table_created = false,
    };
    if ((entry & OS_KERNEL_PAGE_TABLE_PRESENT_BIT) != OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
        if ((entry & OS_KERNEL_PAGE_TABLE_LARGE_PAGE_BIT) != OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
            return PageTableStatus::UnexpectedLargePage;
        }
        const uint64_t existing_physical_address =
            entry & OS_KERNEL_PAGE_TABLE_PHYSICAL_ADDRESS_MASK;
        if (!this->IsPhysicalAddressValid(existing_physical_address,
                                          OS_KERNEL_MEMORY_PAGE_SIZE_BYTES)) {
            return PageTableStatus::InvalidTableFrame;
        }
        if (!this->frame_allocator_->OwnsAllocation(
                PhysicalFrame{.physical_address = existing_physical_address})) {
            return PageTableStatus::TableFrameNotOwned;
        }
        if (user_accessible) {
            entry |= OS_KERNEL_PAGE_TABLE_USER_BIT;
        }
        physical_address = existing_physical_address;
        mutation.table_physical_address = existing_physical_address;
        return PageTableStatus::Succeeded;
    }

    const PageTableStatus status = this->AllocateTable(physical_address);
    if (status != PageTableStatus::Succeeded) {
        return status;
    }
    mutation.table_physical_address = physical_address;
    mutation.table_created = true;
    entry = physical_address | OS_KERNEL_PAGE_TABLE_PRESENT_BIT | OS_KERNEL_PAGE_TABLE_WRITABLE_BIT;
    if (user_accessible) {
        entry |= OS_KERNEL_PAGE_TABLE_USER_BIT;
    }
    return PageTableStatus::Succeeded;
}

PageTableStatus PageTableManager::MapLeaf(const uint64_t virtual_address,
                                          const uint64_t physical_address,
                                          const PagePermissions permissions,
                                          const bool large_page) noexcept {
    if (this->root_physical_address_ == OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
        return PageTableStatus::NotInitialized;
    }
    if (!IsRootKindValid(this->root_kind_)) {
        return PageTableStatus::InvalidRootKind;
    }
    if (!IsPageTableMemoryAccessValid(this->memory_access_)) {
        return PageTableStatus::InvalidMemoryAccess;
    }
    if (permissions.writable && permissions.copy_on_write) {
        return PageTableStatus::InvalidMemoryAccess;
    }
    if (!IsCanonicalVirtualAddress(virtual_address)) {
        return PageTableStatus::InvalidVirtualAddress;
    }
    const uint64_t page_size_bytes =
        large_page ? OS_KERNEL_PAGE_TABLE_LARGE_PAGE_SIZE_BYTES : OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
    if ((virtual_address & (page_size_bytes - OS_KERNEL_PAGE_TABLE_SINGLE_ENTRY)) !=
        OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
        return PageTableStatus::InvalidAlignment;
    }
    if (!this->IsPhysicalAddressValid(physical_address, page_size_bytes)) {
        return PageTableStatus::InvalidPhysicalAddress;
    }
    const PageTableIndices indices = CalculatePageTableIndices(virtual_address);
    if (!this->CanMutateAddress(indices)) {
        return PageTableStatus::SharedBranchMutationDenied;
    }
    if (!this->IsPhysicalAddressValid(this->root_physical_address_,
                                      OS_KERNEL_MEMORY_PAGE_SIZE_BYTES)) {
        return PageTableStatus::InvalidTableFrame;
    }
    if (!this->frame_allocator_->OwnsAllocation(
            PhysicalFrame{.physical_address = this->root_physical_address_})) {
        return PageTableStatus::TableFrameNotOwned;
    }

    TableMutation mutations[OS_KERNEL_PAGE_TABLE_NON_LEAF_LEVEL_COUNT]{};
    uint64_t mutation_count = OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY;
    // 每穿过一级就记录父项原值与新表帧；后续失败可恢复 U/S 位并逆序释放新帧。
    uint64_t *const level4 = this->TableAtPhysicalAddress(this->root_physical_address_);
    uint64_t level3_physical_address = OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY;
    PageTableStatus status =
        this->EnsureNextTable(level4[indices.level4], permissions.user_accessible,
                              level3_physical_address, mutations[mutation_count]);
    ++mutation_count;
    if (status != PageTableStatus::Succeeded) {
        return this->RollbackTableMutations(virtual_address, mutations, mutation_count)
                   ? status
                   : PageTableStatus::RollbackFailed;
    }

    uint64_t *const level3 = this->TableAtPhysicalAddress(level3_physical_address);
    uint64_t level2_physical_address = OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY;
    status = this->EnsureNextTable(level3[indices.level3], permissions.user_accessible,
                                   level2_physical_address, mutations[mutation_count]);
    ++mutation_count;
    if (status != PageTableStatus::Succeeded) {
        return this->RollbackTableMutations(virtual_address, mutations, mutation_count)
                   ? status
                   : PageTableStatus::RollbackFailed;
    }

    uint64_t *const level2 = this->TableAtPhysicalAddress(level2_physical_address);
    if (large_page) {
        uint64_t &leaf_entry = level2[indices.level2];
        if ((leaf_entry & OS_KERNEL_PAGE_TABLE_PRESENT_BIT) != OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
            status = PageTableStatus::AlreadyMapped;
        } else {
            leaf_entry = EncodePageTableLeafEntry(physical_address, permissions) |
                         OS_KERNEL_PAGE_TABLE_LARGE_PAGE_BIT;
        }
    } else {
        uint64_t level1_physical_address = OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY;
        status = this->EnsureNextTable(level2[indices.level2], permissions.user_accessible,
                                       level1_physical_address, mutations[mutation_count]);
        ++mutation_count;
        if (status == PageTableStatus::Succeeded) {
            uint64_t *const level1 = this->TableAtPhysicalAddress(level1_physical_address);
            uint64_t &leaf_entry = level1[indices.level1];
            if ((leaf_entry & OS_KERNEL_PAGE_TABLE_PRESENT_BIT) !=
                OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
                status = PageTableStatus::AlreadyMapped;
            } else {
                leaf_entry = EncodePageTableLeafEntry(physical_address, permissions);
            }
        }
    }
    if (status != PageTableStatus::Succeeded) {
        return this->RollbackTableMutations(virtual_address, mutations, mutation_count)
                   ? status
                   : PageTableStatus::RollbackFailed;
    }

    if (this->memory_access_.invalidate_active_mappings) {
        InvalidatePage(virtual_address);
    }
    return PageTableStatus::Succeeded;
}

bool PageTableManager::RollbackTableMutations(const uint64_t virtual_address,
                                              TableMutation *const mutations,
                                              const uint64_t mutation_count) noexcept {
    bool rollback_succeeded = true;
    // 先恢复父项以断开所有新分支，再逆序释放新建页表，避免留下可达的空表帧。
    for (uint64_t mutation_index = OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY;
         mutation_index < mutation_count; ++mutation_index) {
        const TableMutation &mutation = mutations[mutation_index];
        if (mutation.table_created && !this->frame_allocator_->OwnsAllocation(PhysicalFrame{
                                          .physical_address = mutation.table_physical_address})) {
            rollback_succeeded = false;
        }
        if (mutation.entry != nullptr) {
            *mutation.entry = mutation.original_entry;
        }
    }
    if (this->memory_access_.invalidate_active_mappings) {
        InvalidatePage(virtual_address);
    }
    for (uint64_t remaining_mutation_count = mutation_count;
         remaining_mutation_count > OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY; --remaining_mutation_count) {
        const TableMutation &mutation =
            mutations[remaining_mutation_count - OS_KERNEL_PAGE_TABLE_SINGLE_ENTRY];
        if (!mutation.table_created) {
            continue;
        }
        if (this->ReleaseTableFrame(mutation.table_physical_address) !=
            PageTableStatus::Succeeded) {
            rollback_succeeded = false;
        }
    }
    return rollback_succeeded;
}

PageTableStatus PageTableManager::WalkToLeaf(const uint64_t virtual_address,
                                             PageTableWalkPath &path) const noexcept {
    if (this->root_physical_address_ == OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
        return PageTableStatus::NotInitialized;
    }
    if (!IsRootKindValid(this->root_kind_)) {
        return PageTableStatus::InvalidRootKind;
    }
    if (!IsPageTableMemoryAccessValid(this->memory_access_)) {
        return PageTableStatus::InvalidMemoryAccess;
    }
    if (!IsCanonicalVirtualAddress(virtual_address)) {
        return PageTableStatus::InvalidVirtualAddress;
    }
    if (!this->IsPhysicalAddressValid(this->root_physical_address_,
                                      OS_KERNEL_MEMORY_PAGE_SIZE_BYTES)) {
        return PageTableStatus::InvalidTableFrame;
    }
    if (!this->frame_allocator_->OwnsAllocation(
            PhysicalFrame{.physical_address = this->root_physical_address_})) {
        return PageTableStatus::TableFrameNotOwned;
    }

    const PageTableIndices indices = CalculatePageTableIndices(virtual_address);
    // 每次解引用子表前都校验范围、精确所有权和祖先环，损坏项不能变成宿主越界访问。
    uint64_t *const level4 = this->TableAtPhysicalAddress(this->root_physical_address_);
    uint64_t *const level4_entry = &level4[indices.level4];
    if ((*level4_entry & OS_KERNEL_PAGE_TABLE_PRESENT_BIT) == OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
        return PageTableStatus::NotMapped;
    }
    if ((*level4_entry & OS_KERNEL_PAGE_TABLE_LARGE_PAGE_BIT) != OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
        return PageTableStatus::UnexpectedLargePage;
    }
    const uint64_t level3_physical_address =
        *level4_entry & OS_KERNEL_PAGE_TABLE_PHYSICAL_ADDRESS_MASK;
    if (!this->IsPhysicalAddressValid(level3_physical_address, OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) ||
        level3_physical_address == this->root_physical_address_) {
        return PageTableStatus::InvalidTableFrame;
    }
    if (!this->frame_allocator_->OwnsAllocation(
            PhysicalFrame{.physical_address = level3_physical_address})) {
        return PageTableStatus::TableFrameNotOwned;
    }

    uint64_t *const level3 = this->TableAtPhysicalAddress(level3_physical_address);
    uint64_t *const level3_entry = &level3[indices.level3];
    if ((*level3_entry & OS_KERNEL_PAGE_TABLE_PRESENT_BIT) == OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
        return PageTableStatus::NotMapped;
    }
    if ((*level3_entry & OS_KERNEL_PAGE_TABLE_LARGE_PAGE_BIT) != OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
        return PageTableStatus::UnexpectedLargePage;
    }
    const uint64_t level2_physical_address =
        *level3_entry & OS_KERNEL_PAGE_TABLE_PHYSICAL_ADDRESS_MASK;
    if (!this->IsPhysicalAddressValid(level2_physical_address, OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) ||
        level2_physical_address == this->root_physical_address_ ||
        level2_physical_address == level3_physical_address) {
        return PageTableStatus::InvalidTableFrame;
    }
    if (!this->frame_allocator_->OwnsAllocation(
            PhysicalFrame{.physical_address = level2_physical_address})) {
        return PageTableStatus::TableFrameNotOwned;
    }

    uint64_t *const level2 = this->TableAtPhysicalAddress(level2_physical_address);
    uint64_t *const level2_entry = &level2[indices.level2];
    if ((*level2_entry & OS_KERNEL_PAGE_TABLE_PRESENT_BIT) == OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
        return PageTableStatus::NotMapped;
    }
    if ((*level2_entry & OS_KERNEL_PAGE_TABLE_LARGE_PAGE_BIT) != OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
        return PageTableStatus::UnexpectedLargePage;
    }
    const uint64_t level1_physical_address =
        *level2_entry & OS_KERNEL_PAGE_TABLE_PHYSICAL_ADDRESS_MASK;
    if (!this->IsPhysicalAddressValid(level1_physical_address, OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) ||
        level1_physical_address == this->root_physical_address_ ||
        level1_physical_address == level3_physical_address ||
        level1_physical_address == level2_physical_address) {
        return PageTableStatus::InvalidTableFrame;
    }
    if (!this->frame_allocator_->OwnsAllocation(
            PhysicalFrame{.physical_address = level1_physical_address})) {
        return PageTableStatus::TableFrameNotOwned;
    }

    uint64_t *const level1 = this->TableAtPhysicalAddress(level1_physical_address);
    const PageTableWalkPath candidate_path{
        .level4_entry = level4_entry,
        .level3_entry = level3_entry,
        .level2_entry = level2_entry,
        .level1_entry = &level1[indices.level1],
        .level3_physical_address = level3_physical_address,
        .level2_physical_address = level2_physical_address,
        .level1_physical_address = level1_physical_address,
    };
    path = candidate_path;
    return PageTableStatus::Succeeded;
}

PageTableStatus
PageTableManager::ReleaseTableFrame(const uint64_t table_physical_address) noexcept {
    if (!this->IsPhysicalAddressValid(table_physical_address, OS_KERNEL_MEMORY_PAGE_SIZE_BYTES)) {
        return PageTableStatus::InvalidTableFrame;
    }
    const PhysicalFrame table_frame{.physical_address = table_physical_address};
    if (!this->frame_allocator_->OwnsAllocation(table_frame)) {
        return PageTableStatus::TableFrameNotOwned;
    }
    uint64_t *const table = this->TableAtPhysicalAddress(table_physical_address);
    for (uint64_t entry_index = OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY;
         entry_index < OS_KERNEL_PAGE_TABLE_ENTRY_COUNT; ++entry_index) {
        table[entry_index] = OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY;
    }
    return this->frame_allocator_->Release(table_frame) == PhysicalFrameAllocatorStatus::Succeeded
               ? PageTableStatus::Succeeded
               : PageTableStatus::FrameReleaseFailed;
}

PageTableStatus PageTableManager::ReleaseOwnedTable(const uint64_t table_physical_address,
                                                    const uint64_t table_level,
                                                    const uint64_t *const ancestor_table_addresses,
                                                    const uint64_t ancestor_table_count) noexcept {
    if (table_level == OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY ||
        table_level > OS_KERNEL_PAGE_TABLE_LEVEL3_NUMBER) {
        return PageTableStatus::TemplateRootInvalid;
    }
    if (ancestor_table_addresses == nullptr ||
        ancestor_table_count >= OS_KERNEL_PAGE_TABLE_HIERARCHY_LEVEL_COUNT ||
        ContainsTableAddress(ancestor_table_addresses, ancestor_table_count,
                             table_physical_address)) {
        return PageTableStatus::InvalidTableFrame;
    }
    if (!this->IsPhysicalAddressValid(table_physical_address, OS_KERNEL_MEMORY_PAGE_SIZE_BYTES)) {
        return PageTableStatus::InvalidTableFrame;
    }
    if (!this->frame_allocator_->OwnsAllocation(
            PhysicalFrame{.physical_address = table_physical_address})) {
        return PageTableStatus::TableFrameNotOwned;
    }
    uint64_t current_ancestor_table_addresses[OS_KERNEL_PAGE_TABLE_HIERARCHY_LEVEL_COUNT]{};
    for (uint64_t ancestor_index = OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY;
         ancestor_index < ancestor_table_count; ++ancestor_index) {
        current_ancestor_table_addresses[ancestor_index] = ancestor_table_addresses[ancestor_index];
    }
    current_ancestor_table_addresses[ancestor_table_count] = table_physical_address;
    const uint64_t current_ancestor_table_count =
        ancestor_table_count + OS_KERNEL_PAGE_TABLE_SINGLE_ENTRY;

    uint64_t *const table = this->TableAtPhysicalAddress(table_physical_address);
    for (uint64_t entry_index = OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY;
         entry_index < OS_KERNEL_PAGE_TABLE_ENTRY_COUNT; ++entry_index) {
        const uint64_t entry = table[entry_index];
        if ((entry & OS_KERNEL_PAGE_TABLE_PRESENT_BIT) == OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
            continue;
        }
        const uint64_t child_physical_address = entry & OS_KERNEL_PAGE_TABLE_PHYSICAL_ADDRESS_MASK;
        if (table_level == OS_KERNEL_PAGE_TABLE_LEVEL1_NUMBER) {
            if (!this->IsPhysicalAddressValid(child_physical_address,
                                              OS_KERNEL_MEMORY_PAGE_SIZE_BYTES)) {
                return PageTableStatus::InvalidPhysicalAddress;
            }
            if (ContainsTableAddress(current_ancestor_table_addresses, current_ancestor_table_count,
                                     child_physical_address)) {
                return PageTableStatus::InvalidTableFrame;
            }
            const PhysicalFrame leaf_frame{
                .physical_address = child_physical_address,
            };
            if (!this->frame_allocator_->OwnsAllocation(leaf_frame)) {
                return PageTableStatus::TableFrameNotOwned;
            }
            if (this->frame_allocator_->Release(leaf_frame) !=
                PhysicalFrameAllocatorStatus::Succeeded) {
                return PageTableStatus::FrameReleaseFailed;
            }
            table[entry_index] = OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY;
            continue;
        }
        if ((entry & OS_KERNEL_PAGE_TABLE_LARGE_PAGE_BIT) != OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
            return PageTableStatus::UnexpectedLargePage;
        }
        if (!this->IsPhysicalAddressValid(child_physical_address,
                                          OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) ||
            ContainsTableAddress(current_ancestor_table_addresses, current_ancestor_table_count,
                                 child_physical_address)) {
            return PageTableStatus::InvalidTableFrame;
        }
        const PageTableStatus child_status = this->ReleaseOwnedTable(
            child_physical_address, table_level - OS_KERNEL_PAGE_TABLE_SINGLE_ENTRY,
            current_ancestor_table_addresses, current_ancestor_table_count);
        if (child_status != PageTableStatus::Succeeded) {
            return child_status;
        }
        table[entry_index] = OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY;
    }
    return this->ReleaseTableFrame(table_physical_address);
}
}

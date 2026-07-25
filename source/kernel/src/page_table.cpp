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

PageTableManager::PageTableManager(PhysicalFrameAllocator &frameAllocator,
                                   const PageTableMemoryAccess memoryAccess) noexcept
    : frameAllocator_{&frameAllocator}, rootPhysicalAddress_{0ULL}, memoryAccess_{memoryAccess} {}

PageTableManager::PageTableManager(PhysicalFrameAllocator &frameAllocator,
                                   const uint64_t rootPhysicalAddress,
                                   const PageTableMemoryAccess memoryAccess) noexcept
    : frameAllocator_{&frameAllocator}, rootPhysicalAddress_{rootPhysicalAddress},
      memoryAccess_{memoryAccess} {}

PageTableStatus PageTableManager::Initialize() noexcept {
    if (this->SetMemoryAccess(this->memoryAccess_) != PageTableStatus::Succeeded) {
        return PageTableStatus::InvalidMemoryAccess;
    }
    uint64_t rootPhysicalAddress = 0ULL;
    const PageTableStatus status = this->AllocateTable(rootPhysicalAddress);
    if (status != PageTableStatus::Succeeded) {
        return status;
    }
    this->rootPhysicalAddress_ = rootPhysicalAddress;
    return PageTableStatus::Succeeded;
}

PageTableStatus
PageTableManager::InitializeProcessRoot(const uint64_t templateRootPhysicalAddress) noexcept {
    if (this->SetMemoryAccess(this->memoryAccess_) != PageTableStatus::Succeeded) {
        return PageTableStatus::InvalidMemoryAccess;
    }
    if (templateRootPhysicalAddress == 0ULL ||
        (templateRootPhysicalAddress & OS_KERNEL_PAGE_TABLE_PAGE_MASK) != 0ULL) {
        return PageTableStatus::TemplateRootInvalid;
    }
    uint64_t *const templateRoot = this->TableAtPhysicalAddress(templateRootPhysicalAddress);
    const uint64_t templateLowEntry = templateRoot[OS_KERNEL_PAGE_TABLE_LEVEL4_LOW_KERNEL_INDEX];
    if ((templateLowEntry & OS_KERNEL_PAGE_TABLE_PRESENT_BIT) == 0ULL) {
        return PageTableStatus::TemplateRootInvalid;
    }
    if ((templateLowEntry & OS_KERNEL_PAGE_TABLE_LARGE_PAGE_BIT) != 0ULL) {
        return PageTableStatus::UnexpectedLargePage;
    }

    uint64_t processRootPhysicalAddress = 0ULL;
    PageTableStatus status = this->AllocateTable(processRootPhysicalAddress);
    if (status != PageTableStatus::Succeeded) {
        return status;
    }
    uint64_t clonedLowLevel3PhysicalAddress = 0ULL;
    status = this->AllocateTable(clonedLowLevel3PhysicalAddress);
    if (status != PageTableStatus::Succeeded) {
        if (this->frameAllocator_->Release(
                PhysicalFrame{.physicalAddress = processRootPhysicalAddress}) !=
            PhysicalFrameAllocatorStatus::Succeeded) {
            return PageTableStatus::FrameReleaseFailed;
        }
        return status;
    }

    uint64_t *const processRoot = this->TableAtPhysicalAddress(processRootPhysicalAddress);
    for (uint64_t entryIndex = 0ULL; entryIndex < OS_KERNEL_PAGE_TABLE_ENTRY_COUNT; ++entryIndex) {
        processRoot[entryIndex] = templateRoot[entryIndex];
    }
    processRoot[OS_KERNEL_PAGE_TABLE_LEVEL4_USER_STACK_INDEX] = OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY;

    const uint64_t templateLowLevel3PhysicalAddress =
        templateLowEntry & OS_KERNEL_PAGE_TABLE_PHYSICAL_ADDRESS_MASK;
    uint64_t *const templateLowLevel3 =
        this->TableAtPhysicalAddress(templateLowLevel3PhysicalAddress);
    uint64_t *const clonedLowLevel3 = this->TableAtPhysicalAddress(clonedLowLevel3PhysicalAddress);
    for (uint64_t entryIndex = 0ULL; entryIndex < OS_KERNEL_PAGE_TABLE_ENTRY_COUNT; ++entryIndex) {
        clonedLowLevel3[entryIndex] = templateLowLevel3[entryIndex];
    }
    clonedLowLevel3[OS_KERNEL_PAGE_TABLE_LEVEL3_USER_PROGRAM_INDEX] =
        OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY;
    processRoot[OS_KERNEL_PAGE_TABLE_LEVEL4_LOW_KERNEL_INDEX] =
        (templateLowEntry & ~OS_KERNEL_PAGE_TABLE_PHYSICAL_ADDRESS_MASK) |
        clonedLowLevel3PhysicalAddress;
    this->rootPhysicalAddress_ = processRootPhysicalAddress;
    return PageTableStatus::Succeeded;
}

PageTableStatus PageTableManager::ReleaseProcessRoot() noexcept {
    if (this->rootPhysicalAddress_ == 0ULL) {
        return PageTableStatus::NotInitialized;
    }
    uint64_t *const processRoot = this->TableAtPhysicalAddress(this->rootPhysicalAddress_);
    const uint64_t lowLevel3Entry = processRoot[OS_KERNEL_PAGE_TABLE_LEVEL4_LOW_KERNEL_INDEX];
    if ((lowLevel3Entry & OS_KERNEL_PAGE_TABLE_PRESENT_BIT) == 0ULL ||
        (lowLevel3Entry & OS_KERNEL_PAGE_TABLE_LARGE_PAGE_BIT) != 0ULL) {
        return PageTableStatus::TemplateRootInvalid;
    }
    const uint64_t lowLevel3PhysicalAddress =
        lowLevel3Entry & OS_KERNEL_PAGE_TABLE_PHYSICAL_ADDRESS_MASK;
    uint64_t *const lowLevel3 = this->TableAtPhysicalAddress(lowLevel3PhysicalAddress);
    const uint64_t userProgramEntry = lowLevel3[OS_KERNEL_PAGE_TABLE_LEVEL3_USER_PROGRAM_INDEX];
    if ((userProgramEntry & OS_KERNEL_PAGE_TABLE_PRESENT_BIT) != 0ULL) {
        if ((userProgramEntry & OS_KERNEL_PAGE_TABLE_LARGE_PAGE_BIT) != 0ULL) {
            return PageTableStatus::UnexpectedLargePage;
        }
        const PageTableStatus programReleaseStatus =
            this->ReleaseOwnedTable(userProgramEntry & OS_KERNEL_PAGE_TABLE_PHYSICAL_ADDRESS_MASK,
                                    OS_KERNEL_PAGE_TABLE_LEVEL2_NUMBER);
        if (programReleaseStatus != PageTableStatus::Succeeded) {
            return programReleaseStatus;
        }
    }

    const uint64_t userStackEntry = processRoot[OS_KERNEL_PAGE_TABLE_LEVEL4_USER_STACK_INDEX];
    if ((userStackEntry & OS_KERNEL_PAGE_TABLE_PRESENT_BIT) != 0ULL) {
        if ((userStackEntry & OS_KERNEL_PAGE_TABLE_LARGE_PAGE_BIT) != 0ULL) {
            return PageTableStatus::UnexpectedLargePage;
        }
        const PageTableStatus stackReleaseStatus =
            this->ReleaseOwnedTable(userStackEntry & OS_KERNEL_PAGE_TABLE_PHYSICAL_ADDRESS_MASK,
                                    OS_KERNEL_PAGE_TABLE_LEVEL3_NUMBER);
        if (stackReleaseStatus != PageTableStatus::Succeeded) {
            return stackReleaseStatus;
        }
    }

    if (this->frameAllocator_->Release(
            PhysicalFrame{.physicalAddress = lowLevel3PhysicalAddress}) !=
            PhysicalFrameAllocatorStatus::Succeeded ||
        this->frameAllocator_->Release(
            PhysicalFrame{.physicalAddress = this->rootPhysicalAddress_}) !=
            PhysicalFrameAllocatorStatus::Succeeded) {
        return PageTableStatus::FrameReleaseFailed;
    }
    this->rootPhysicalAddress_ = 0ULL;
    return PageTableStatus::Succeeded;
}

PageTableStatus PageTableManager::MapPage(const uint64_t virtualAddress,
                                          const uint64_t physicalAddress,
                                          const PagePermissions permissions) noexcept {
    if (this->rootPhysicalAddress_ == 0ULL) {
        return PageTableStatus::NotInitialized;
    }
    if (!IsCanonicalVirtualAddress(virtualAddress)) {
        return PageTableStatus::InvalidVirtualAddress;
    }
    if ((virtualAddress & OS_KERNEL_PAGE_TABLE_PAGE_MASK) != 0ULL) {
        return PageTableStatus::InvalidAlignment;
    }
    if (!this->IsPhysicalAddressValid(physicalAddress, OS_KERNEL_MEMORY_PAGE_SIZE_BYTES)) {
        return PageTableStatus::InvalidPhysicalAddress;
    }

    const PageTableIndices indices = CalculatePageTableIndices(virtualAddress);
    uint64_t *level4 = this->TableAtPhysicalAddress(this->rootPhysicalAddress_);
    uint64_t level3PhysicalAddress = 0ULL;
    PageTableStatus status = this->EnsureNextTable(
        level4[indices.level4], permissions.userAccessible, level3PhysicalAddress);
    if (status != PageTableStatus::Succeeded) {
        return status;
    }
    uint64_t *level3 = this->TableAtPhysicalAddress(level3PhysicalAddress);
    uint64_t level2PhysicalAddress = 0ULL;
    status = this->EnsureNextTable(level3[indices.level3], permissions.userAccessible,
                                   level2PhysicalAddress);
    if (status != PageTableStatus::Succeeded) {
        return status;
    }
    uint64_t *level2 = this->TableAtPhysicalAddress(level2PhysicalAddress);
    uint64_t level1PhysicalAddress = 0ULL;
    status = this->EnsureNextTable(level2[indices.level2], permissions.userAccessible,
                                   level1PhysicalAddress);
    if (status != PageTableStatus::Succeeded) {
        return status;
    }
    uint64_t *level1 = this->TableAtPhysicalAddress(level1PhysicalAddress);
    uint64_t &leafEntry = level1[indices.level1];
    if ((leafEntry & OS_KERNEL_PAGE_TABLE_PRESENT_BIT) != 0ULL) {
        return PageTableStatus::AlreadyMapped;
    }
    leafEntry = EncodePageTableLeafEntry(physicalAddress, permissions);
    if (this->memoryAccess_.invalidateActiveMappings) {
        InvalidatePage(virtualAddress);
    }
    return PageTableStatus::Succeeded;
}

PageTableStatus PageTableManager::MapLargePage(const uint64_t virtualAddress,
                                               const uint64_t physicalAddress,
                                               const PagePermissions permissions) noexcept {
    if (this->rootPhysicalAddress_ == OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
        return PageTableStatus::NotInitialized;
    }
    if (!IsCanonicalVirtualAddress(virtualAddress)) {
        return PageTableStatus::InvalidVirtualAddress;
    }
    if ((virtualAddress & OS_KERNEL_PAGE_TABLE_LARGE_PAGE_MASK) !=
        OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
        return PageTableStatus::InvalidAlignment;
    }
    if (!this->IsPhysicalAddressValid(physicalAddress,
                                      OS_KERNEL_PAGE_TABLE_LARGE_PAGE_SIZE_BYTES)) {
        return PageTableStatus::InvalidPhysicalAddress;
    }

    const PageTableIndices indices = CalculatePageTableIndices(virtualAddress);
    uint64_t *level4 = this->TableAtPhysicalAddress(this->rootPhysicalAddress_);
    uint64_t level3PhysicalAddress = OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY;
    PageTableStatus status = this->EnsureNextTable(
        level4[indices.level4], permissions.userAccessible, level3PhysicalAddress);
    if (status != PageTableStatus::Succeeded) {
        return status;
    }
    uint64_t *level3 = this->TableAtPhysicalAddress(level3PhysicalAddress);
    uint64_t level2PhysicalAddress = OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY;
    status = this->EnsureNextTable(level3[indices.level3], permissions.userAccessible,
                                   level2PhysicalAddress);
    if (status != PageTableStatus::Succeeded) {
        return status;
    }
    uint64_t *level2 = this->TableAtPhysicalAddress(level2PhysicalAddress);
    uint64_t &leafEntry = level2[indices.level2];
    if ((leafEntry & OS_KERNEL_PAGE_TABLE_PRESENT_BIT) != OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
        return PageTableStatus::AlreadyMapped;
    }
    leafEntry = EncodePageTableLeafEntry(physicalAddress, permissions) |
                OS_KERNEL_PAGE_TABLE_LARGE_PAGE_BIT;
    if (this->memoryAccess_.invalidateActiveMappings) {
        InvalidatePage(virtualAddress);
    }
    return PageTableStatus::Succeeded;
}

PageTableStatus PageTableManager::UnmapPage(const uint64_t virtualAddress) noexcept {
    if ((virtualAddress & OS_KERNEL_PAGE_TABLE_PAGE_MASK) != 0ULL) {
        return PageTableStatus::InvalidAlignment;
    }
    uint64_t *leafEntry = nullptr;
    const PageTableStatus status = this->WalkToLeaf(virtualAddress, leafEntry);
    if (status != PageTableStatus::Succeeded) {
        return status;
    }
    if ((*leafEntry & OS_KERNEL_PAGE_TABLE_PRESENT_BIT) == 0ULL) {
        return PageTableStatus::NotMapped;
    }
    *leafEntry = OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY;
    if (this->memoryAccess_.invalidateActiveMappings) {
        InvalidatePage(virtualAddress);
    }
    return PageTableStatus::Succeeded;
}

PageTableStatus PageTableManager::QueryPage(const uint64_t virtualAddress,
                                            PageMapping &mapping) const noexcept {
    if (this->rootPhysicalAddress_ == OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
        return PageTableStatus::NotInitialized;
    }
    if (!IsCanonicalVirtualAddress(virtualAddress)) {
        return PageTableStatus::InvalidVirtualAddress;
    }
    const PageTableIndices indices = CalculatePageTableIndices(virtualAddress);
    uint64_t *const level4 = this->TableAtPhysicalAddress(this->rootPhysicalAddress_);
    const uint64_t level4Entry = level4[indices.level4];
    if ((level4Entry & OS_KERNEL_PAGE_TABLE_PRESENT_BIT) == OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
        return PageTableStatus::NotMapped;
    }
    if ((level4Entry & OS_KERNEL_PAGE_TABLE_LARGE_PAGE_BIT) != OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
        return PageTableStatus::UnexpectedLargePage;
    }
    uint64_t *const level3 =
        this->TableAtPhysicalAddress(level4Entry & OS_KERNEL_PAGE_TABLE_PHYSICAL_ADDRESS_MASK);
    const uint64_t level3Entry = level3[indices.level3];
    if ((level3Entry & OS_KERNEL_PAGE_TABLE_PRESENT_BIT) == OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
        return PageTableStatus::NotMapped;
    }
    if ((level3Entry & OS_KERNEL_PAGE_TABLE_LARGE_PAGE_BIT) != OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
        return PageTableStatus::UnexpectedLargePage;
    }
    uint64_t *const level2 =
        this->TableAtPhysicalAddress(level3Entry & OS_KERNEL_PAGE_TABLE_PHYSICAL_ADDRESS_MASK);
    const uint64_t level2Entry = level2[indices.level2];
    if ((level2Entry & OS_KERNEL_PAGE_TABLE_PRESENT_BIT) == OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
        return PageTableStatus::NotMapped;
    }
    if ((level2Entry & OS_KERNEL_PAGE_TABLE_LARGE_PAGE_BIT) != OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
        mapping = DecodePageTableLeafEntry(level2Entry);
        mapping.physicalAddress =
            (level2Entry & OS_KERNEL_PAGE_TABLE_LARGE_PAGE_PHYSICAL_ADDRESS_MASK) +
            (virtualAddress & OS_KERNEL_PAGE_TABLE_LARGE_PAGE_MASK);
        mapping.pageSizeBytes = OS_KERNEL_PAGE_TABLE_LARGE_PAGE_SIZE_BYTES;
        return PageTableStatus::Succeeded;
    }
    uint64_t *const level1 =
        this->TableAtPhysicalAddress(level2Entry & OS_KERNEL_PAGE_TABLE_PHYSICAL_ADDRESS_MASK);
    const uint64_t level1Entry = level1[indices.level1];
    if ((level1Entry & OS_KERNEL_PAGE_TABLE_PRESENT_BIT) == OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY) {
        return PageTableStatus::NotMapped;
    }
    mapping = DecodePageTableLeafEntry(level1Entry);
    return PageTableStatus::Succeeded;
}

uint64_t PageTableManager::RootPhysicalAddress() const noexcept {
    return this->rootPhysicalAddress_;
}

PageTableStatus
PageTableManager::SetMemoryAccess(const PageTableMemoryAccess memoryAccess) noexcept {
    if (!IsPageTableMemoryAccessValid(memoryAccess)) {
        return PageTableStatus::InvalidMemoryAccess;
    }
    this->memoryAccess_ = memoryAccess;
    return PageTableStatus::Succeeded;
}

uint64_t *PageTableManager::TableAtPhysicalAddress(const uint64_t physicalAddress) const noexcept {
    return reinterpret_cast<uint64_t *>(this->memoryAccess_.physicalMemoryVirtualBase +
                                        physicalAddress);
}

bool PageTableManager::IsPhysicalAddressValid(const uint64_t physicalAddress,
                                              const uint64_t pageSizeBytes) const noexcept {
    return pageSizeBytes != OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY &&
           (physicalAddress & (pageSizeBytes - 1ULL)) == OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY &&
           physicalAddress < this->memoryAccess_.maximumPhysicalAddressExclusive &&
           pageSizeBytes <= this->memoryAccess_.maximumPhysicalAddressExclusive - physicalAddress &&
           (physicalAddress & ~OS_KERNEL_PAGE_TABLE_PHYSICAL_ADDRESS_MASK) ==
               OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY;
}

PageTableStatus PageTableManager::AllocateTable(uint64_t &physicalAddress) noexcept {
    PhysicalFrame frame{};
    if (this->frameAllocator_->AllocateInRange(
            OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY,
            this->memoryAccess_.allocationMaximumPhysicalAddressExclusive,
            frame) != PhysicalFrameAllocatorStatus::Succeeded) {
        return PageTableStatus::FrameAllocationFailed;
    }
    uint64_t *table = this->TableAtPhysicalAddress(frame.physicalAddress);
    for (uint64_t entryIndex = 0ULL; entryIndex < OS_KERNEL_PAGE_TABLE_ENTRY_COUNT; ++entryIndex) {
        table[entryIndex] = OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY;
    }
    physicalAddress = frame.physicalAddress;
    return PageTableStatus::Succeeded;
}

PageTableStatus PageTableManager::EnsureNextTable(uint64_t &entry, const bool userAccessible,
                                                  uint64_t &physicalAddress) noexcept {
    if ((entry & OS_KERNEL_PAGE_TABLE_PRESENT_BIT) != 0ULL) {
        if ((entry & OS_KERNEL_PAGE_TABLE_LARGE_PAGE_BIT) != 0ULL) {
            return PageTableStatus::UnexpectedLargePage;
        }
        if (userAccessible) {
            entry |= OS_KERNEL_PAGE_TABLE_USER_BIT;
        }
        physicalAddress = entry & OS_KERNEL_PAGE_TABLE_PHYSICAL_ADDRESS_MASK;
        return PageTableStatus::Succeeded;
    }

    const PageTableStatus status = this->AllocateTable(physicalAddress);
    if (status != PageTableStatus::Succeeded) {
        return status;
    }
    entry = physicalAddress | OS_KERNEL_PAGE_TABLE_PRESENT_BIT | OS_KERNEL_PAGE_TABLE_WRITABLE_BIT;
    if (userAccessible) {
        entry |= OS_KERNEL_PAGE_TABLE_USER_BIT;
    }
    return PageTableStatus::Succeeded;
}

PageTableStatus PageTableManager::WalkToLeaf(const uint64_t virtualAddress,
                                             uint64_t *&leafEntry) const noexcept {
    if (this->rootPhysicalAddress_ == 0ULL) {
        return PageTableStatus::NotInitialized;
    }
    if (!IsCanonicalVirtualAddress(virtualAddress)) {
        return PageTableStatus::InvalidVirtualAddress;
    }
    const PageTableIndices indices = CalculatePageTableIndices(virtualAddress);
    uint64_t *currentTable = this->TableAtPhysicalAddress(this->rootPhysicalAddress_);
    const uint64_t nonLeafIndices[] = {indices.level4, indices.level3, indices.level2};
    for (uint64_t levelIndex = 0ULL; levelIndex < OS_KERNEL_PAGE_TABLE_NON_LEAF_LEVEL_COUNT;
         ++levelIndex) {
        const uint64_t entry = currentTable[nonLeafIndices[levelIndex]];
        if ((entry & OS_KERNEL_PAGE_TABLE_PRESENT_BIT) == 0ULL) {
            return PageTableStatus::NotMapped;
        }
        if ((entry & OS_KERNEL_PAGE_TABLE_LARGE_PAGE_BIT) != 0ULL) {
            return PageTableStatus::UnexpectedLargePage;
        }
        currentTable =
            this->TableAtPhysicalAddress(entry & OS_KERNEL_PAGE_TABLE_PHYSICAL_ADDRESS_MASK);
    }
    leafEntry = &currentTable[indices.level1];
    return PageTableStatus::Succeeded;
}

PageTableStatus PageTableManager::ReleaseOwnedTable(const uint64_t tablePhysicalAddress,
                                                    const uint64_t tableLevel) noexcept {
    if (tableLevel == 0ULL || tableLevel > OS_KERNEL_PAGE_TABLE_LEVEL3_NUMBER) {
        return PageTableStatus::TemplateRootInvalid;
    }
    uint64_t *const table = this->TableAtPhysicalAddress(tablePhysicalAddress);
    for (uint64_t entryIndex = 0ULL; entryIndex < OS_KERNEL_PAGE_TABLE_ENTRY_COUNT; ++entryIndex) {
        const uint64_t entry = table[entryIndex];
        if ((entry & OS_KERNEL_PAGE_TABLE_PRESENT_BIT) == 0ULL) {
            continue;
        }
        if (tableLevel == OS_KERNEL_PAGE_TABLE_LEVEL1_NUMBER) {
            if (this->frameAllocator_->Release(PhysicalFrame{
                    .physicalAddress = entry & OS_KERNEL_PAGE_TABLE_PHYSICAL_ADDRESS_MASK,
                }) != PhysicalFrameAllocatorStatus::Succeeded) {
                return PageTableStatus::FrameReleaseFailed;
            }
            continue;
        }
        if ((entry & OS_KERNEL_PAGE_TABLE_LARGE_PAGE_BIT) != 0ULL) {
            return PageTableStatus::UnexpectedLargePage;
        }
        const PageTableStatus childStatus = this->ReleaseOwnedTable(
            entry & OS_KERNEL_PAGE_TABLE_PHYSICAL_ADDRESS_MASK, tableLevel - 1ULL);
        if (childStatus != PageTableStatus::Succeeded) {
            return childStatus;
        }
    }
    return this->frameAllocator_->Release(PhysicalFrame{.physicalAddress = tablePhysicalAddress}) ==
                   PhysicalFrameAllocatorStatus::Succeeded
               ? PageTableStatus::Succeeded
               : PageTableStatus::FrameReleaseFailed;
}

}

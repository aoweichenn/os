#include "os/kernel/page_table.hpp"

#include "os/kernel/processor.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_PAGE_TABLE_PRESENT_BIT = 0x0000000000000001ULL;
constexpr uint64_t OS_KERNEL_PAGE_TABLE_WRITABLE_BIT = 0x0000000000000002ULL;
constexpr uint64_t OS_KERNEL_PAGE_TABLE_USER_BIT = 0x0000000000000004ULL;
constexpr uint64_t OS_KERNEL_PAGE_TABLE_LARGE_PAGE_BIT = 0x0000000000000080ULL;
constexpr uint64_t OS_KERNEL_PAGE_TABLE_PHYSICAL_ADDRESS_MASK = 0x000FFFFFFFFFF000ULL;
constexpr uint64_t OS_KERNEL_PAGE_TABLE_PAGE_MASK = OS_KERNEL_MEMORY_PAGE_SIZE_BYTES - 1ULL;
constexpr uint64_t OS_KERNEL_PAGE_TABLE_EMPTY_ENTRY = 0ULL;
constexpr uint64_t OS_KERNEL_PAGE_TABLE_NON_LEAF_LEVEL_COUNT = 3ULL;

[[nodiscard]] uint64_t *TableAtPhysicalAddress(const uint64_t physicalAddress) noexcept {
    return reinterpret_cast<uint64_t *>(physicalAddress);
}

}

PageTableManager::PageTableManager(PhysicalFrameAllocator &frameAllocator) noexcept
    : frameAllocator_{&frameAllocator}, rootPhysicalAddress_{0ULL} {}

PageTableStatus PageTableManager::Initialize() noexcept {
    uint64_t rootPhysicalAddress = 0ULL;
    const PageTableStatus status = this->AllocateTable(rootPhysicalAddress);
    if (status != PageTableStatus::Succeeded) {
        return status;
    }
    this->rootPhysicalAddress_ = rootPhysicalAddress;
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
    if ((virtualAddress & OS_KERNEL_PAGE_TABLE_PAGE_MASK) != 0ULL ||
        (physicalAddress & OS_KERNEL_PAGE_TABLE_PAGE_MASK) != 0ULL) {
        return PageTableStatus::InvalidAlignment;
    }
    if ((physicalAddress & ~OS_KERNEL_PAGE_TABLE_PHYSICAL_ADDRESS_MASK) != 0ULL) {
        return PageTableStatus::InvalidPhysicalAddress;
    }

    const PageTableIndices indices = CalculatePageTableIndices(virtualAddress);
    uint64_t *level4 = TableAtPhysicalAddress(this->rootPhysicalAddress_);
    uint64_t level3PhysicalAddress = 0ULL;
    PageTableStatus status = this->EnsureNextTable(
        level4[indices.level4], permissions.userAccessible, level3PhysicalAddress);
    if (status != PageTableStatus::Succeeded) {
        return status;
    }
    uint64_t *level3 = TableAtPhysicalAddress(level3PhysicalAddress);
    uint64_t level2PhysicalAddress = 0ULL;
    status = this->EnsureNextTable(level3[indices.level3], permissions.userAccessible,
                                   level2PhysicalAddress);
    if (status != PageTableStatus::Succeeded) {
        return status;
    }
    uint64_t *level2 = TableAtPhysicalAddress(level2PhysicalAddress);
    uint64_t level1PhysicalAddress = 0ULL;
    status = this->EnsureNextTable(level2[indices.level2], permissions.userAccessible,
                                   level1PhysicalAddress);
    if (status != PageTableStatus::Succeeded) {
        return status;
    }
    uint64_t *level1 = TableAtPhysicalAddress(level1PhysicalAddress);
    uint64_t &leafEntry = level1[indices.level1];
    if ((leafEntry & OS_KERNEL_PAGE_TABLE_PRESENT_BIT) != 0ULL) {
        return PageTableStatus::AlreadyMapped;
    }
    leafEntry = EncodePageTableLeafEntry(physicalAddress, permissions);
    InvalidatePage(virtualAddress);
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
    InvalidatePage(virtualAddress);
    return PageTableStatus::Succeeded;
}

PageTableStatus PageTableManager::QueryPage(const uint64_t virtualAddress,
                                            PageMapping &mapping) const noexcept {
    uint64_t *leafEntry = nullptr;
    const PageTableStatus status = this->WalkToLeaf(virtualAddress, leafEntry);
    if (status != PageTableStatus::Succeeded) {
        return status;
    }
    if ((*leafEntry & OS_KERNEL_PAGE_TABLE_PRESENT_BIT) == 0ULL) {
        return PageTableStatus::NotMapped;
    }
    mapping = DecodePageTableLeafEntry(*leafEntry);
    return PageTableStatus::Succeeded;
}

uint64_t PageTableManager::RootPhysicalAddress() const noexcept {
    return this->rootPhysicalAddress_;
}

PageTableStatus PageTableManager::AllocateTable(uint64_t &physicalAddress) noexcept {
    PhysicalFrame frame{};
    if (this->frameAllocator_->Allocate(frame) != PhysicalFrameAllocatorStatus::Succeeded) {
        return PageTableStatus::FrameAllocationFailed;
    }
    uint64_t *table = TableAtPhysicalAddress(frame.physicalAddress);
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
    uint64_t *currentTable = TableAtPhysicalAddress(this->rootPhysicalAddress_);
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
        currentTable = TableAtPhysicalAddress(entry & OS_KERNEL_PAGE_TABLE_PHYSICAL_ADDRESS_MASK);
    }
    leafEntry = &currentTable[indices.level1];
    return PageTableStatus::Succeeded;
}

}

#pragma once

#include "os/kernel/physical_frame_allocator.hpp"

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_PAGE_TABLE_ENTRY_COUNT = 512ULL;
inline constexpr uint64_t OS_KERNEL_PAGE_TABLE_LARGE_PAGE_SIZE_BYTES = 2ULL * 1024ULL * 1024ULL;

struct PageTableIndices final {
    uint64_t level4;
    uint64_t level3;
    uint64_t level2;
    uint64_t level1;
};

struct PagePermissions final {
    bool writable;
    bool executable;
    bool userAccessible;
    bool cacheDisabled;
};

struct PageMapping final {
    uint64_t physicalAddress;
    uint64_t pageSizeBytes;
    PagePermissions permissions;
};

struct PageTableMemoryAccess final {
    uint64_t maximumPhysicalAddressExclusive;
    uint64_t physicalMemoryVirtualBase;
    uint64_t allocationMaximumPhysicalAddressExclusive;
    bool invalidateActiveMappings;
};

enum class PageTableStatus : uint64_t {
    Succeeded,
    NotInitialized,
    InvalidVirtualAddress,
    InvalidPhysicalAddress,
    InvalidAlignment,
    FrameAllocationFailed,
    TemplateRootInvalid,
    UnexpectedLargePage,
    AlreadyMapped,
    NotMapped,
    FrameReleaseFailed,
    InvalidMemoryAccess,
};

[[nodiscard]] bool IsCanonicalVirtualAddress(uint64_t virtualAddress) noexcept;
[[nodiscard]] PageTableIndices CalculatePageTableIndices(uint64_t virtualAddress) noexcept;
[[nodiscard]] bool IsPageTableMemoryAccessValid(PageTableMemoryAccess memoryAccess) noexcept;
[[nodiscard]] uint64_t EncodePageTableLeafEntry(uint64_t physicalAddress,
                                                PagePermissions permissions) noexcept;
[[nodiscard]] PageMapping DecodePageTableLeafEntry(uint64_t entry) noexcept;

class PageTableManager final {
  public:
    PageTableManager(PhysicalFrameAllocator &frameAllocator,
                     PageTableMemoryAccess memoryAccess) noexcept;
    PageTableManager(PhysicalFrameAllocator &frameAllocator, uint64_t rootPhysicalAddress,
                     PageTableMemoryAccess memoryAccess) noexcept;

    [[nodiscard]] PageTableStatus Initialize() noexcept;
    [[nodiscard]] PageTableStatus
    InitializeProcessRoot(uint64_t templateRootPhysicalAddress) noexcept;
    [[nodiscard]] PageTableStatus ReleaseProcessRoot() noexcept;
    [[nodiscard]] PageTableStatus MapPage(uint64_t virtualAddress, uint64_t physicalAddress,
                                          PagePermissions permissions) noexcept;
    [[nodiscard]] PageTableStatus MapLargePage(uint64_t virtualAddress, uint64_t physicalAddress,
                                               PagePermissions permissions) noexcept;
    [[nodiscard]] PageTableStatus UnmapPage(uint64_t virtualAddress) noexcept;
    [[nodiscard]] PageTableStatus QueryPage(uint64_t virtualAddress,
                                            PageMapping &mapping) const noexcept;
    [[nodiscard]] uint64_t RootPhysicalAddress() const noexcept;
    [[nodiscard]] PageTableStatus SetMemoryAccess(PageTableMemoryAccess memoryAccess) noexcept;

  private:
    [[nodiscard]] uint64_t *TableAtPhysicalAddress(uint64_t physicalAddress) const noexcept;
    [[nodiscard]] bool IsPhysicalAddressValid(uint64_t physicalAddress,
                                              uint64_t pageSizeBytes) const noexcept;
    [[nodiscard]] PageTableStatus AllocateTable(uint64_t &physicalAddress) noexcept;
    [[nodiscard]] PageTableStatus EnsureNextTable(uint64_t &entry, bool userAccessible,
                                                  uint64_t &physicalAddress) noexcept;
    [[nodiscard]] PageTableStatus WalkToLeaf(uint64_t virtualAddress,
                                             uint64_t *&leafEntry) const noexcept;
    [[nodiscard]] PageTableStatus ReleaseOwnedTable(uint64_t tablePhysicalAddress,
                                                    uint64_t tableLevel) noexcept;

    PhysicalFrameAllocator *frameAllocator_;
    uint64_t rootPhysicalAddress_;
    PageTableMemoryAccess memoryAccess_;
};

}

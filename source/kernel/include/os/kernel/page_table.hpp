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
    bool user_accessible;
    bool cache_disabled;
};

struct PageMapping final {
    uint64_t physical_address;
    uint64_t page_size_bytes;
    PagePermissions permissions;
};

struct PageTableMemoryAccess final {
    uint64_t maximum_physical_address_exclusive;
    uint64_t physical_memory_virtual_base;
    uint64_t allocation_maximum_physical_address_exclusive;
    bool invalidate_active_mappings;
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

[[nodiscard]] bool IsCanonicalVirtualAddress(uint64_t virtual_address) noexcept;
[[nodiscard]] PageTableIndices CalculatePageTableIndices(uint64_t virtual_address) noexcept;
[[nodiscard]] bool IsPageTableMemoryAccessValid(PageTableMemoryAccess memory_access) noexcept;
[[nodiscard]] uint64_t EncodePageTableLeafEntry(uint64_t physical_address,
                                                PagePermissions permissions) noexcept;
[[nodiscard]] PageMapping DecodePageTableLeafEntry(uint64_t entry) noexcept;

class PageTableManager final {
  public:
    PageTableManager(PhysicalFrameAllocator &frame_allocator,
                     PageTableMemoryAccess memory_access) noexcept;
    PageTableManager(PhysicalFrameAllocator &frame_allocator, uint64_t root_physical_address,
                     PageTableMemoryAccess memory_access) noexcept;

    [[nodiscard]] PageTableStatus Initialize() noexcept;
    [[nodiscard]] PageTableStatus
    InitializeProcessRoot(uint64_t template_root_physical_address) noexcept;
    [[nodiscard]] PageTableStatus ReleaseProcessRoot() noexcept;
    [[nodiscard]] PageTableStatus MapPage(uint64_t virtual_address, uint64_t physical_address,
                                          PagePermissions permissions) noexcept;
    [[nodiscard]] PageTableStatus MapLargePage(uint64_t virtual_address, uint64_t physical_address,
                                               PagePermissions permissions) noexcept;
    [[nodiscard]] PageTableStatus UnmapPage(uint64_t virtual_address) noexcept;
    [[nodiscard]] PageTableStatus QueryPage(uint64_t virtual_address,
                                            PageMapping &mapping) const noexcept;
    [[nodiscard]] uint64_t RootPhysicalAddress() const noexcept;
    [[nodiscard]] PageTableStatus SetMemoryAccess(PageTableMemoryAccess memory_access) noexcept;

  private:
    [[nodiscard]] uint64_t *TableAtPhysicalAddress(uint64_t physical_address) const noexcept;
    [[nodiscard]] bool IsPhysicalAddressValid(uint64_t physical_address,
                                              uint64_t page_size_bytes) const noexcept;
    [[nodiscard]] PageTableStatus AllocateTable(uint64_t &physical_address) noexcept;
    [[nodiscard]] PageTableStatus EnsureNextTable(uint64_t &entry, bool user_accessible,
                                                  uint64_t &physical_address) noexcept;
    [[nodiscard]] PageTableStatus WalkToLeaf(uint64_t virtual_address,
                                             uint64_t *&leaf_entry) const noexcept;
    [[nodiscard]] PageTableStatus ReleaseOwnedTable(uint64_t table_physical_address,
                                                    uint64_t table_level) noexcept;

    PhysicalFrameAllocator *frame_allocator_;
    uint64_t root_physical_address_;
    PageTableMemoryAccess memory_access_;
};

}

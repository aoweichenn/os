#pragma once

#include "os/kernel/physical_frame_allocator.hpp"

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_PAGE_TABLE_ENTRY_COUNT = 512ULL;

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
    PagePermissions permissions;
};

enum class PageTableStatus : uint64_t {
    Succeeded,
    NotInitialized,
    InvalidVirtualAddress,
    InvalidPhysicalAddress,
    InvalidAlignment,
    FrameAllocationFailed,
    UnexpectedLargePage,
    AlreadyMapped,
    NotMapped,
};

[[nodiscard]] bool isCanonicalVirtualAddress(uint64_t virtualAddress) noexcept;
[[nodiscard]] PageTableIndices calculatePageTableIndices(uint64_t virtualAddress) noexcept;
[[nodiscard]] uint64_t encodePageTableLeafEntry(uint64_t physicalAddress,
                                                PagePermissions permissions) noexcept;
[[nodiscard]] PageMapping decodePageTableLeafEntry(uint64_t entry) noexcept;

class PageTableManager final {
  public:
    explicit PageTableManager(PhysicalFrameAllocator &frameAllocator) noexcept;

    [[nodiscard]] PageTableStatus initialize() noexcept;
    [[nodiscard]] PageTableStatus mapPage(uint64_t virtualAddress, uint64_t physicalAddress,
                                          PagePermissions permissions) noexcept;
    [[nodiscard]] PageTableStatus unmapPage(uint64_t virtualAddress) noexcept;
    [[nodiscard]] PageTableStatus queryPage(uint64_t virtualAddress,
                                            PageMapping &mapping) const noexcept;
    [[nodiscard]] uint64_t rootPhysicalAddress() const noexcept;

  private:
    [[nodiscard]] PageTableStatus allocateTable(uint64_t &physicalAddress) noexcept;
    [[nodiscard]] PageTableStatus ensureNextTable(uint64_t &entry, bool userAccessible,
                                                  uint64_t &physicalAddress) noexcept;
    [[nodiscard]] PageTableStatus walkToLeaf(uint64_t virtualAddress,
                                             uint64_t *&leafEntry) const noexcept;

    PhysicalFrameAllocator *frameAllocator_;
    uint64_t rootPhysicalAddress_;
};

}

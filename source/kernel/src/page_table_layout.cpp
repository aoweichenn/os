#include "os/kernel/page_table.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_PAGE_LAYOUT_INDEX_MASK = 0x01FFULL;
constexpr uint64_t OS_KERNEL_PAGE_LAYOUT_LEVEL1_SHIFT = 12ULL;
constexpr uint64_t OS_KERNEL_PAGE_LAYOUT_LEVEL2_SHIFT = 21ULL;
constexpr uint64_t OS_KERNEL_PAGE_LAYOUT_LEVEL3_SHIFT = 30ULL;
constexpr uint64_t OS_KERNEL_PAGE_LAYOUT_LEVEL4_SHIFT = 39ULL;
constexpr uint64_t OS_KERNEL_PAGE_LAYOUT_CANONICAL_SIGN_SHIFT = 47ULL;
constexpr uint64_t OS_KERNEL_PAGE_LAYOUT_CANONICAL_UPPER_SHIFT = 48ULL;
constexpr uint64_t OS_KERNEL_PAGE_LAYOUT_CANONICAL_UPPER_POSITIVE = 0x0000ULL;
constexpr uint64_t OS_KERNEL_PAGE_LAYOUT_CANONICAL_UPPER_NEGATIVE = 0xFFFFULL;
constexpr uint64_t OS_KERNEL_PAGE_LAYOUT_PRESENT_BIT = 0x0000000000000001ULL;
constexpr uint64_t OS_KERNEL_PAGE_LAYOUT_WRITABLE_BIT = 0x0000000000000002ULL;
constexpr uint64_t OS_KERNEL_PAGE_LAYOUT_USER_BIT = 0x0000000000000004ULL;
constexpr uint64_t OS_KERNEL_PAGE_LAYOUT_CACHE_DISABLE_BIT = 0x0000000000000010ULL;
constexpr uint64_t OS_KERNEL_PAGE_LAYOUT_NO_EXECUTE_BIT = 0x8000000000000000ULL;
constexpr uint64_t OS_KERNEL_PAGE_LAYOUT_PHYSICAL_ADDRESS_MASK = 0x000FFFFFFFFFF000ULL;
constexpr uint64_t OS_KERNEL_PAGE_LAYOUT_SINGLE_BIT_MASK = 0x0000000000000001ULL;

}

bool isCanonicalVirtualAddress(const uint64_t virtualAddress) noexcept {
    const uint64_t upper = virtualAddress >> OS_KERNEL_PAGE_LAYOUT_CANONICAL_UPPER_SHIFT;
    const bool signBit = ((virtualAddress >> OS_KERNEL_PAGE_LAYOUT_CANONICAL_SIGN_SHIFT) &
                          OS_KERNEL_PAGE_LAYOUT_SINGLE_BIT_MASK) != 0ULL;
    return upper == (signBit ? OS_KERNEL_PAGE_LAYOUT_CANONICAL_UPPER_NEGATIVE
                             : OS_KERNEL_PAGE_LAYOUT_CANONICAL_UPPER_POSITIVE);
}

PageTableIndices calculatePageTableIndices(const uint64_t virtualAddress) noexcept {
    return PageTableIndices{
        .level4 = (virtualAddress >> OS_KERNEL_PAGE_LAYOUT_LEVEL4_SHIFT) &
                  OS_KERNEL_PAGE_LAYOUT_INDEX_MASK,
        .level3 = (virtualAddress >> OS_KERNEL_PAGE_LAYOUT_LEVEL3_SHIFT) &
                  OS_KERNEL_PAGE_LAYOUT_INDEX_MASK,
        .level2 = (virtualAddress >> OS_KERNEL_PAGE_LAYOUT_LEVEL2_SHIFT) &
                  OS_KERNEL_PAGE_LAYOUT_INDEX_MASK,
        .level1 = (virtualAddress >> OS_KERNEL_PAGE_LAYOUT_LEVEL1_SHIFT) &
                  OS_KERNEL_PAGE_LAYOUT_INDEX_MASK,
    };
}

uint64_t encodePageTableLeafEntry(const uint64_t physicalAddress,
                                  const PagePermissions permissions) noexcept {
    uint64_t entry = (physicalAddress & OS_KERNEL_PAGE_LAYOUT_PHYSICAL_ADDRESS_MASK) |
                     OS_KERNEL_PAGE_LAYOUT_PRESENT_BIT;
    if (permissions.writable) {
        entry |= OS_KERNEL_PAGE_LAYOUT_WRITABLE_BIT;
    }
    if (permissions.userAccessible) {
        entry |= OS_KERNEL_PAGE_LAYOUT_USER_BIT;
    }
    if (permissions.cacheDisabled) {
        entry |= OS_KERNEL_PAGE_LAYOUT_CACHE_DISABLE_BIT;
    }
    if (!permissions.executable) {
        entry |= OS_KERNEL_PAGE_LAYOUT_NO_EXECUTE_BIT;
    }
    return entry;
}

PageMapping decodePageTableLeafEntry(const uint64_t entry) noexcept {
    return PageMapping{
        .physicalAddress = entry & OS_KERNEL_PAGE_LAYOUT_PHYSICAL_ADDRESS_MASK,
        .permissions =
            PagePermissions{
                .writable = (entry & OS_KERNEL_PAGE_LAYOUT_WRITABLE_BIT) != 0ULL,
                .executable = (entry & OS_KERNEL_PAGE_LAYOUT_NO_EXECUTE_BIT) == 0ULL,
                .userAccessible = (entry & OS_KERNEL_PAGE_LAYOUT_USER_BIT) != 0ULL,
                .cacheDisabled = (entry & OS_KERNEL_PAGE_LAYOUT_CACHE_DISABLE_BIT) != 0ULL,
            },
    };
}

}

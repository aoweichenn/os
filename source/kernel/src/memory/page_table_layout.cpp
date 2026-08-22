#include "os/kernel/memory/page_table.hpp"

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
constexpr uint64_t OS_KERNEL_PAGE_LAYOUT_ACCESSED_BIT = 0x0000000000000020ULL;
constexpr uint64_t OS_KERNEL_PAGE_LAYOUT_COPY_ON_WRITE_BIT = 0x0000000000000200ULL;
constexpr uint64_t OS_KERNEL_PAGE_LAYOUT_NO_EXECUTE_BIT = 0x8000000000000000ULL;
constexpr uint64_t OS_KERNEL_PAGE_LAYOUT_PHYSICAL_ADDRESS_MASK = 0x000FFFFFFFFFF000ULL;
constexpr uint64_t OS_KERNEL_PAGE_LAYOUT_SINGLE_BIT_MASK = 0x0000000000000001ULL;
constexpr uint64_t OS_KERNEL_PAGE_LAYOUT_PHYSICAL_ADDRESS_LIMIT = 0x0010000000000000ULL;
constexpr uint64_t OS_KERNEL_PAGE_LAYOUT_PAGE_MASK = OS_KERNEL_MEMORY_PAGE_SIZE_BYTES - 1ULL;
constexpr uint64_t OS_KERNEL_PAGE_LAYOUT_EMPTY_VALUE = 0ULL;

}

bool IsCanonicalVirtualAddress(const uint64_t virtual_address) noexcept {
    const uint64_t upper = virtual_address >> OS_KERNEL_PAGE_LAYOUT_CANONICAL_UPPER_SHIFT;
    const bool sign_bit = ((virtual_address >> OS_KERNEL_PAGE_LAYOUT_CANONICAL_SIGN_SHIFT) &
                           OS_KERNEL_PAGE_LAYOUT_SINGLE_BIT_MASK) != 0ULL;
    return upper == (sign_bit ? OS_KERNEL_PAGE_LAYOUT_CANONICAL_UPPER_NEGATIVE
                              : OS_KERNEL_PAGE_LAYOUT_CANONICAL_UPPER_POSITIVE);
}

PageTableIndices CalculatePageTableIndices(const uint64_t virtual_address) noexcept {
    return PageTableIndices{
        .level4 = (virtual_address >> OS_KERNEL_PAGE_LAYOUT_LEVEL4_SHIFT) &
                  OS_KERNEL_PAGE_LAYOUT_INDEX_MASK,
        .level3 = (virtual_address >> OS_KERNEL_PAGE_LAYOUT_LEVEL3_SHIFT) &
                  OS_KERNEL_PAGE_LAYOUT_INDEX_MASK,
        .level2 = (virtual_address >> OS_KERNEL_PAGE_LAYOUT_LEVEL2_SHIFT) &
                  OS_KERNEL_PAGE_LAYOUT_INDEX_MASK,
        .level1 = (virtual_address >> OS_KERNEL_PAGE_LAYOUT_LEVEL1_SHIFT) &
                  OS_KERNEL_PAGE_LAYOUT_INDEX_MASK,
    };
}

bool IsPageTableMemoryAccessValid(const PageTableMemoryAccess memory_access) noexcept {
    if (memory_access.maximum_physical_address_exclusive == OS_KERNEL_PAGE_LAYOUT_EMPTY_VALUE ||
        memory_access.maximum_physical_address_exclusive >
            OS_KERNEL_PAGE_LAYOUT_PHYSICAL_ADDRESS_LIMIT ||
        (memory_access.maximum_physical_address_exclusive & OS_KERNEL_PAGE_LAYOUT_PAGE_MASK) !=
            OS_KERNEL_PAGE_LAYOUT_EMPTY_VALUE ||
        memory_access.allocation_maximum_physical_address_exclusive ==
            OS_KERNEL_PAGE_LAYOUT_EMPTY_VALUE ||
        memory_access.allocation_maximum_physical_address_exclusive >
            memory_access.maximum_physical_address_exclusive ||
        (memory_access.allocation_maximum_physical_address_exclusive &
         OS_KERNEL_PAGE_LAYOUT_PAGE_MASK) != OS_KERNEL_PAGE_LAYOUT_EMPTY_VALUE ||
        memory_access.physical_memory_virtual_base >
            UINT64_MAX - (memory_access.maximum_physical_address_exclusive -
                          OS_KERNEL_MEMORY_PAGE_SIZE_BYTES)) {
        return false;
    }
    return IsCanonicalVirtualAddress(memory_access.physical_memory_virtual_base) &&
           IsCanonicalVirtualAddress(memory_access.physical_memory_virtual_base +
                                     memory_access.maximum_physical_address_exclusive -
                                     OS_KERNEL_MEMORY_PAGE_SIZE_BYTES);
}

uint64_t EncodePageTableLeafEntry(const uint64_t physical_address,
                                  const PagePermissions permissions) noexcept {
    uint64_t entry = (physical_address & OS_KERNEL_PAGE_LAYOUT_PHYSICAL_ADDRESS_MASK) |
                     OS_KERNEL_PAGE_LAYOUT_PRESENT_BIT;
    if (permissions.writable) {
        entry |= OS_KERNEL_PAGE_LAYOUT_WRITABLE_BIT;
    }
    if (permissions.user_accessible) {
        entry |= OS_KERNEL_PAGE_LAYOUT_USER_BIT;
    }
    if (permissions.cache_disabled) {
        entry |= OS_KERNEL_PAGE_LAYOUT_CACHE_DISABLE_BIT;
    }
    if (permissions.copy_on_write) {
        entry |= OS_KERNEL_PAGE_LAYOUT_COPY_ON_WRITE_BIT;
    }
    if (!permissions.executable) {
        entry |= OS_KERNEL_PAGE_LAYOUT_NO_EXECUTE_BIT;
    }
    return entry;
}

PageMapping DecodePageTableLeafEntry(const uint64_t entry) noexcept {
    return PageMapping{
        .physical_address = entry & OS_KERNEL_PAGE_LAYOUT_PHYSICAL_ADDRESS_MASK,
        .page_size_bytes = OS_KERNEL_MEMORY_PAGE_SIZE_BYTES,
        .permissions =
            PagePermissions{
                .writable = (entry & OS_KERNEL_PAGE_LAYOUT_WRITABLE_BIT) != 0ULL,
                .executable = (entry & OS_KERNEL_PAGE_LAYOUT_NO_EXECUTE_BIT) == 0ULL,
                .user_accessible = (entry & OS_KERNEL_PAGE_LAYOUT_USER_BIT) != 0ULL,
                .cache_disabled = (entry & OS_KERNEL_PAGE_LAYOUT_CACHE_DISABLE_BIT) != 0ULL,
                .copy_on_write = (entry & OS_KERNEL_PAGE_LAYOUT_COPY_ON_WRITE_BIT) != 0ULL,
            },
        .accessed = (entry & OS_KERNEL_PAGE_LAYOUT_ACCESSED_BIT) != 0ULL,
    };
}

}

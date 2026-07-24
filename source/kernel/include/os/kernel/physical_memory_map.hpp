#pragma once

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_MEMORY_MAP_ENTRY_SIZE_BYTES = 24ULL;
inline constexpr uint64_t OS_KERNEL_MEMORY_MAP_MAXIMUM_ENTRY_COUNT = 128ULL;
inline constexpr uint32_t OS_KERNEL_MEMORY_MAP_USABLE_REGION_TYPE = 1U;

struct PhysicalMemoryMapEntry final {
    uint64_t baseAddress;
    uint64_t lengthBytes;
    uint32_t type;
    uint32_t attributes;
};

struct PhysicalMemorySummary final {
    uint64_t totalBytes;
    uint64_t usableBytes;
    uint64_t managedUsableBytes;
    uint64_t highestAddressExclusive;
    uint64_t usableRegionCount;
};

enum class PhysicalMemoryMapValidationStatus : uint64_t {
    Succeeded,
    NullEntries,
    InvalidEntryCount,
    InvalidManagedLimit,
    EmptyRegion,
    AddressOverflow,
    UnsortedRegions,
    OverlappingRegions,
    TotalSizeOverflow,
    UsableSizeOverflow,
    NoManagedUsableMemory,
};

[[nodiscard]] PhysicalMemoryMapValidationStatus
validateAndSummarizePhysicalMemoryMap(const PhysicalMemoryMapEntry *entries, uint64_t entryCount,
                                      uint64_t managedLimitAddress,
                                      PhysicalMemorySummary &summary) noexcept;

static_assert(sizeof(PhysicalMemoryMapEntry) == OS_KERNEL_MEMORY_MAP_ENTRY_SIZE_BYTES);

}
